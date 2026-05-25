#include "desktop_service.h"
#include "app_launch_resolver.h"
#include "app_manifest_loader.h"
#include "app_registry.h"
#include "built_in_app_metadata.h"
#include "desktop_config.h"
#include "elf_validator.h"
#include "fs.h"
#include "logger.h"
#include "lifecycle.h"
#include "process.h"
#include "native_app_runtime.h"
#include "native_elf_executor.h"
#include "native_elf_image_loader.h"
#include "native_elf_launch_pipeline.h"
#include "notepad.h"
#include "calculator.h"
#include "console_window.h"
#include "file_explorer.h"
#include "clock.h"
#include "task_manager.h"
#include "paint.h"
#include "image_viewer.h"
#include "onscreen_keyboard.h"
#include "notification_manager.h"
#include "shutdown_dialog.h"
#include "disk_manager.h"
#include "control_panel.h"
#include "display_options.h"
#include "navigator.h"
#include "trash.h"
#include "package_manager.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <chrono>
#include <thread>
/// <summary>
/// guideX OS GUI - Desktop Service
/// </summary>
namespace gxos {
    /// <summary>
	/// GUI Namespace
    /// </summary>
    namespace gui {

        static apps::AppRegistry s_appRegistry;
        static bool s_appRegistryInitialized = false;
        static size_t s_appRegistryInitializeCount = 0;
        static apps::AppScanResult s_lastManifestScanResult;
        static apps::AppScanResult s_lastBuiltInRegisterResult;
        static std::mutex s_launchTargetShadowCountersMutex;
        static LaunchTargetShadowCounters s_launchTargetShadowCounters;

        static std::string launchTargetTypeCoverageSummaryLine();

        static void logScanIssues(const char* label, const std::vector<apps::AppScanIssue>& issues) {
            for (const auto& issue : issues) {
                std::string message = std::string(label) + ": ";
                if (!issue.appId.empty()) message += issue.appId + " ";
                for (const std::string& error : issue.errors) message += error + "; ";
                Logger::write(LogLevel::Warn, message);
            }
        }

        static std::string launchNameForApp(const apps::RegisteredApp& app) {
            auto hint = app.manifest.desktopRegistryHints.find("registeredName");
            if (hint != app.manifest.desktopRegistryHints.end() && !hint->second.empty()) return hint->second;
            const apps::AppEntry* entry = app.FindCompatibleEntry("any");
            if (entry && !entry->entryPoint.empty()) return entry->entryPoint;
            return app.manifest.displayName;
        }

        static const RegisteredDesktopApp* findRegisteredApp(const std::string& name) {
            for (const auto& app : DesktopService::GetRegisteredApps()) {
                if (app.displayName == name || app.launchName == name || app.id == name) return &app;
            }
            return nullptr;
        }

        static const apps::RegisteredApp* findRegistryApp(const RegisteredDesktopApp& app) {
            const apps::RegisteredApp* registryApp = s_appRegistry.FindById(app.id);
            if (registryApp) return registryApp;
            return s_appRegistry.FindByDisplayName(app.displayName);
        }

        static bool isAppModelDemoApp(const RegisteredDesktopApp& app) {
            if (app.displayName == "App Model Demo" ||
                app.displayName == "Hello World" ||
                app.displayName == "Resource Viewer" ||
                app.displayName == "HelloWorld ELF" ||
                app.displayName == "Native App Debug Viewer") {
                return true;
            }

            return app.kind == apps::AppKind::NativeElf || app.kind == apps::AppKind::GXAppPackage;
        }

        static bool registeredDesktopAppMatchesMetadata(const RegisteredDesktopApp& app, const apps::BuiltInAppMetadata& metadata) {
            return (metadata.appId && app.id == metadata.appId) ||
                (metadata.displayName && app.displayName == metadata.displayName) ||
                (metadata.launchName && app.launchName == metadata.launchName);
        }

        static const apps::BuiltInAppMetadata* findMetadataForRegisteredDesktopApp(const RegisteredDesktopApp& app) {
            const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByAppId(app.id.c_str());
            if (metadata) return metadata;
            metadata = apps::FindBuiltInAppMetadataByDisplayName(app.displayName.c_str());
            if (metadata) return metadata;
            return apps::FindBuiltInAppMetadataByLaunchName(app.launchName.c_str());
        }

        static bool currentHostedRegistrationExistsForMetadata(const apps::BuiltInAppMetadata& metadata) {
            for (const auto& app : DesktopService::GetRegisteredApps()) {
                if (app.kind == apps::AppKind::BuiltIn && registeredDesktopAppMatchesMetadata(app, metadata)) return true;
            }
            return false;
        }

        static bool bareMetalRegistrationNameMatchesMetadata(const char* name, const apps::BuiltInAppMetadata& metadata) {
            return (metadata.kernelAppName && apps::detail::builtInTextEquals(name, metadata.kernelAppName)) ||
                (metadata.kernelLegacyAlias && apps::detail::builtInTextEquals(name, metadata.kernelLegacyAlias));
        }

        static const std::vector<const char*>& currentBareMetalKernelRegistrationNames() {
            // Diagnostic mirror of kernel/core/kernel_apps.cpp registerKernelApps().
            // It reports coverage only; it is not a launch source or policy table.
            static const std::vector<const char*> names = {
                "Notepad",
                "Calculator",
                "DisplayOptions",
                "TaskManager",
                "FileExplorer",
                "Files",
                "guideXOS Navigator",
                "Trash",
                "DiskManager"
            };
            return names;
        }

        static bool currentBareMetalRegistrationExistsForMetadata(const apps::BuiltInAppMetadata& metadata) {
            for (const char* name : currentBareMetalKernelRegistrationNames()) {
                if (bareMetalRegistrationNameMatchesMetadata(name, metadata)) return true;
            }
            return false;
        }

        struct UiLaunchLabelDiagnostic {
            std::string label;
            std::string source;
            std::string fallbackIdentity;
            std::string note;
        };

        static UiLaunchLabelDiagnostic makeUiLaunchLabelDiagnostic(const std::string& label, const std::string& source, const std::string& fallbackIdentity = "", const std::string& note = "") {
            UiLaunchLabelDiagnostic diagnostic;
            diagnostic.label = label;
            diagnostic.source = source;
            diagnostic.fallbackIdentity = fallbackIdentity;
            diagnostic.note = note;
            return diagnostic;
        }

        static std::vector<UiLaunchLabelDiagnostic> currentCompositorUiLaunchLabelsForDiagnostic() {
            // Diagnostic mirror of compositor UI labels only. This is not launch policy
            // and must stay read-only until the app launch rewrite phase.
            // AppModel and ComputerFiles are intentionally preserved Phase 1 aliases:
            // document them here so Phase 2 can replace ad hoc UI labels with a
            // real launch-resolution surface without breaking existing config.
            return {
                makeUiLaunchLabelDiagnostic("App Model Demo", "compositor launchAction special-case"),
                makeUiLaunchLabelDiagnostic("AppModel", "compositor legacy AppModel alias", "App Model Demo", "legacy alias retained for old pins/config"),
                makeUiLaunchLabelDiagnostic("Trash", "desktop system icon"),
                makeUiLaunchLabelDiagnostic("ControlPanel", "desktop system/settings entry"),
                makeUiLaunchLabelDiagnostic("TaskManager", "taskbar/system menu"),
                makeUiLaunchLabelDiagnostic("Console", "taskbar/start menu shortcut"),
                makeUiLaunchLabelDiagnostic("ComputerFiles", "desktop/start system file manager entry", "", "shell/system label; not a built-in metadata identity")
            };
        }

        static bool uiDiagnosticHasLabel(const std::vector<UiLaunchLabelDiagnostic>& labels, const std::string& label) {
            for (const auto& entry : labels) {
                if (entry.label == label) return true;
            }
            return false;
        }

        static int hostedAvailableMetadataCount() {
            int count = 0;
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                if (apps::IsBuiltInAppAvailableInHosted(apps::kBuiltInAppMetadata[i])) ++count;
            }
            return count;
        }

        static int bareMetalAvailableMetadataCount() {
            int count = 0;
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                if (apps::IsBuiltInAppAvailableInBareMetal(apps::kBuiltInAppMetadata[i])) ++count;
            }
            return count;
        }

        static int hostedRegisteredBuiltInsMissingMetadataCount() {
            int count = 0;
            for (const auto& app : DesktopService::GetRegisteredApps()) {
                if (app.kind != apps::AppKind::BuiltIn) continue;
                if (findMetadataForRegisteredDesktopApp(app)) continue;
                ++count;
            }
            return count;
        }

        struct ManifestOrigin {
            apps::AppSourceKind sourceKind = apps::AppSourceKind::UserApps;
            std::string source;
            std::filesystem::path sourceRoot;
            std::filesystem::path manifestPath;
            std::string appId;
            std::string displayName;
            apps::AppKind kind = apps::AppKind::Unknown;
        };

        static bool startsWith(const std::string& value, const char* prefix) {
            const std::string prefixText = prefix ? prefix : "";
            return value.size() >= prefixText.size() && value.compare(0, prefixText.size(), prefixText) == 0;
        }

        static std::string normalizedGenericPath(const std::filesystem::path& path) {
            return path.lexically_normal().generic_string();
        }

        static std::vector<ManifestOrigin> collectManifestOrigins() {
            std::vector<ManifestOrigin> origins;
            for (const apps::AppRegistrySource& source : apps::AppRegistry::DefaultSources()) {
                if (!std::filesystem::exists(source.path)) continue;

                std::error_code error;
                std::filesystem::recursive_directory_iterator it(source.path, std::filesystem::directory_options::skip_permission_denied, error);
                std::filesystem::recursive_directory_iterator end;
                if (error) continue;

                for (; it != end; it.increment(error)) {
                    if (error) {
                        error.clear();
                        continue;
                    }

                    const std::filesystem::directory_entry& entry = *it;
                    if (!entry.is_regular_file(error) || error) {
                        error.clear();
                        continue;
                    }

                    if (entry.path().filename() != "app.json") continue;
                    apps::AppManifestLoadResult loaded = apps::AppManifestLoader::LoadFromFile(entry.path());
                    if (!loaded.valid || loaded.manifest.id.empty()) continue;

                    ManifestOrigin origin;
                    origin.sourceKind = source.kind;
                    origin.source = apps::AppRegistry::ToString(source.kind);
                    origin.sourceRoot = source.path;
                    origin.manifestPath = entry.path();
                    origin.appId = loaded.manifest.id;
                    origin.displayName = loaded.manifest.displayName;
                    origin.kind = loaded.manifest.kind;
                    origins.push_back(origin);
                }
            }
            return origins;
        }

        static std::map<std::string, std::vector<ManifestOrigin>> collectManifestOriginsById() {
            std::map<std::string, std::vector<ManifestOrigin>> originsById;
            for (const ManifestOrigin& origin : collectManifestOrigins()) {
                originsById[origin.appId].push_back(origin);
            }
            return originsById;
        }

        static bool originIsSdkSample(const ManifestOrigin& origin) {
            return normalizedGenericPath(origin.sourceRoot) == "sdk/samples";
        }

        static bool originIsRepoExample(const ManifestOrigin& origin) {
            return normalizedGenericPath(origin.sourceRoot) == "examples/apps";
        }

        static bool originIsInstalledPackage(const ManifestOrigin& origin) {
            return origin.sourceKind == apps::AppSourceKind::Package;
        }

        static void appendNamespaceWarning(std::ostringstream& oss, const ManifestOrigin& origin, const char* warning) {
            oss << "  id=" << origin.appId
                << " displayName=" << origin.displayName
                << " source=" << origin.source
                << " sourceRoot=" << origin.sourceRoot.generic_string()
                << " path=" << origin.manifestPath.string() << "\n";
            oss << "    warning=" << warning << "\n";
        }

        static int appendNamespaceWarningsForOrigin(std::ostringstream* oss, const ManifestOrigin& origin) {
            int warningCount = 0;
            const bool usesBuiltInNamespace = startsWith(origin.appId, "gxos.builtin.");
            const bool usesSampleNamespace = startsWith(origin.appId, "com.guidexos.samples.");
            const bool usesExampleNamespace = startsWith(origin.appId, "com.guidexos.examples.");

            if (origin.sourceKind != apps::AppSourceKind::BuiltIn && usesBuiltInNamespace) {
                ++warningCount;
                if (oss) appendNamespaceWarning(*oss, origin, "non-built-in manifest uses gxos.builtin.*; built-ins own this namespace");
            }
            if (originIsSdkSample(origin) && !usesSampleNamespace) {
                ++warningCount;
                if (oss) appendNamespaceWarning(*oss, origin, "SDK sample manifest should use com.guidexos.samples.*");
            }
            if (originIsRepoExample(origin) && !usesExampleNamespace) {
                ++warningCount;
                if (oss) appendNamespaceWarning(*oss, origin, "repo example manifest should use com.guidexos.examples.*");
            }
            if (originIsInstalledPackage(origin) && (usesBuiltInNamespace || usesSampleNamespace || usesExampleNamespace)) {
                ++warningCount;
                if (oss) appendNamespaceWarning(*oss, origin, "installed /Apps manifest should use a normal installed app id, not builtin/sample/example namespaces");
            }

            return warningCount;
        }

        static int appIdNamespaceWarningCount() {
            int warningCount = 0;
            for (const ManifestOrigin& origin : collectManifestOrigins()) {
                warningCount += appendNamespaceWarningsForOrigin(nullptr, origin);
            }
            return warningCount;
        }

        static int appendAppIdNamespaceWarnings(std::ostringstream& oss) {
            int warningCount = 0;
            oss << "appIdNamespaceWarnings(nonFatal):\n";

            for (const ManifestOrigin& origin : collectManifestOrigins()) {
                warningCount += appendNamespaceWarningsForOrigin(&oss, origin);
            }

            if (warningCount == 0) oss << "  none\n";
            return warningCount;
        }

        static void appendUiLaunchAliasMetadataDiagnostic(std::ostringstream& oss) {
            std::vector<UiLaunchLabelDiagnostic> labels = currentCompositorUiLaunchLabelsForDiagnostic();

            for (const auto& app : DesktopService::GetRegisteredApps()) {
                if (app.kind != apps::AppKind::BuiltIn) continue;
                const apps::BuiltInAppMetadata* metadata = findMetadataForRegisteredDesktopApp(app);
                if (!metadata) continue;
                if (!app.displayName.empty() && !uiDiagnosticHasLabel(labels, app.displayName)) {
                    labels.push_back(makeUiLaunchLabelDiagnostic(app.displayName, "registered built-in display name"));
                }
                if (!app.launchName.empty() && app.launchName != app.displayName && !uiDiagnosticHasLabel(labels, app.launchName)) {
                    labels.push_back(makeUiLaunchLabelDiagnostic(app.launchName, "registered built-in launch name"));
                }
            }

            int cleanCount = 0;
            int fallbackCount = 0;
            int missingCount = 0;

            oss << "uiLaunchAliasMetadataCoverage:\n";

            oss << "  cleanMetadataMatches:\n";
            for (const auto& label : labels) {
                const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByIdentity(label.label.c_str());
                if (!metadata) continue;
                ++cleanCount;
                oss << "    label=" << label.label
                    << " source=" << label.source
                    << " metadataId=" << (metadata->appId ? metadata->appId : "")
                    << " match=direct\n";
            }
            if (cleanCount == 0) oss << "    none\n";

            oss << "  fallbackOrAliasMatches:\n";
            for (const auto& label : labels) {
                if (label.fallbackIdentity.empty()) continue;
                const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByIdentity(label.fallbackIdentity.c_str());
                if (!metadata) continue;
                ++fallbackCount;
                oss << "    label=" << label.label
                    << " source=" << label.source
                    << " fallbackIdentity=" << label.fallbackIdentity
                    << " metadataId=" << (metadata->appId ? metadata->appId : "");
                if (!label.note.empty()) oss << " note=" << label.note;
                oss << "\n";
            }
            if (fallbackCount == 0) oss << "    none\n";

            oss << "  noMetadataMatch:\n";
            for (const auto& label : labels) {
                if (apps::FindBuiltInAppMetadataByIdentity(label.label.c_str())) continue;
                if (!label.fallbackIdentity.empty() && apps::FindBuiltInAppMetadataByIdentity(label.fallbackIdentity.c_str())) continue;
                ++missingCount;
                oss << "    label=" << label.label << " source=" << label.source;
                if (!label.note.empty()) oss << " note=" << label.note;
                oss << "\n";
            }
            if (missingCount == 0) oss << "    none\n";

            int unreachableCount = 0;
            oss << "  metadataLaunchNamesNotReachableFromCurrentUiPaths:\n";
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                const apps::BuiltInAppMetadata& metadata = apps::kBuiltInAppMetadata[i];
                if (!apps::IsBuiltInAppAvailableInHosted(metadata)) continue;
                const bool reachable =
                    (metadata.displayName && uiDiagnosticHasLabel(labels, metadata.displayName)) ||
                    (metadata.launchName && uiDiagnosticHasLabel(labels, metadata.launchName)) ||
                    (metadata.appId && uiDiagnosticHasLabel(labels, metadata.appId));
                if (reachable) continue;
                ++unreachableCount;
                oss << "    id=" << (metadata.appId ? metadata.appId : "")
                    << " displayName=" << (metadata.displayName ? metadata.displayName : "")
                    << " launchName=" << (metadata.launchName ? metadata.launchName : "") << "\n";
            }
            if (unreachableCount == 0) oss << "    none\n";

            oss << "  status: "
                << (missingCount == 0 && unreachableCount == 0 ? "OK" : "WARN")
                << " clean=" << cleanCount
                << " alias=" << fallbackCount
                << " noMetadata=" << missingCount
                << " unreachableMetadata=" << unreachableCount << "\n";
        }

        static std::set<std::string> duplicateIdsFromScanIssues(const apps::AppScanResult& a, const apps::AppScanResult& b) {
            std::set<std::string> ids;
            for (const auto& issue : a.duplicateApps) {
                if (!issue.appId.empty()) ids.insert(issue.appId);
            }
            for (const auto& issue : b.duplicateApps) {
                if (!issue.appId.empty()) ids.insert(issue.appId);
            }
            return ids;
        }

        static int bareMetalRegisteredKernelAppsMissingMetadataCount() {
            int count = 0;
            for (const char* name : currentBareMetalKernelRegistrationNames()) {
                if (apps::FindBuiltInAppMetadataByIdentity(name)) continue;
                ++count;
            }
            return count;
        }

        static int metadataWithoutHostedRegistrationCount() {
            int count = 0;
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                const apps::BuiltInAppMetadata& metadata = apps::kBuiltInAppMetadata[i];
                if (!apps::IsBuiltInAppAvailableInHosted(metadata)) continue;
                if (currentHostedRegistrationExistsForMetadata(metadata)) continue;
                ++count;
            }
            return count;
        }

        static int metadataWithoutBareMetalRegistrationCount() {
            int count = 0;
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                const apps::BuiltInAppMetadata& metadata = apps::kBuiltInAppMetadata[i];
                if (!apps::IsBuiltInAppAvailableInBareMetal(metadata)) continue;
                if (currentBareMetalRegistrationExistsForMetadata(metadata)) continue;
                ++count;
            }
            return count;
        }

        static const char* statusText(bool ok) {
            return ok ? "OK" : "WARN";
        }

        static const char* duplicateOwnershipHint(const std::string& appId) {
            if (appId.find("gxos.builtin.") == 0) {
                return "built-in namespace is owned by shared metadata; sample/example mirrors should use com.guidexos.samples.* or com.guidexos.examples.* ids";
            }
            if (appId.find("com.guidexos.samples.") == 0) {
                return "SDK sample namespace; staged /Apps manifests should normally use installed-app ids instead of sample source ids";
            }
            if (appId == "com.guidexos.helloworld" || appId == "com.guidexos.resourceviewer") {
                return "likely SDK sample source plus staged /Apps mirror sharing an installed-app id; SDK source manifests should use com.guidexos.samples.*";
            }
            return "check whether one manifest is a source sample, example, installed mirror, or synthetic built-in registration";
        }

        static void ensureDefaultAppModelPins() {
            const char* defaults[] = {
                "App Model Demo",
                "Native App Debug Viewer",
                "Hello World",
                "Resource Viewer"
            };

            for (const char* name : defaults) {
                if (!findRegisteredApp(name) || DesktopService::IsPinned(name)) continue;
                DesktopService::PinApp(name);
                Logger::write(LogLevel::Info, std::string("Pinned app-model demo app: ") + name);
            }
        }

        static void refreshRegisteredAppsFromRegistry() {
            for (const auto& app : s_appRegistry.GetAllApps()) {
                Logger::write(LogLevel::Info, std::string("AppModel registry candidate: ") +
                    " displayName=" + app.manifest.displayName +
                    " id=" + app.manifest.id +
                    " kind=" + apps::ToString(app.manifest.kind) +
                    " source=" + apps::AppRegistry::ToString(app.sourceKind));
                DesktopService::RegisterApp(app.manifest.id, app.manifest.displayName, app.manifest.icon, app.manifest.kind, launchNameForApp(app), apps::AppRegistry::ToString(app.sourceKind));
            }
        }

        static void appendScanIssues(std::ostringstream& oss, const char* label, const std::vector<apps::AppScanIssue>& issues) {
            oss << label << ": " << issues.size() << "\n";
            for (const auto& issue : issues) {
                oss << "  source=" << apps::AppRegistry::ToString(issue.sourceKind);
                if (!issue.appId.empty()) oss << " id=" << issue.appId;
                if (!issue.errors.empty()) {
                    oss << " errors=";
                    for (size_t i = 0; i < issue.errors.size(); ++i) {
                        if (i > 0) oss << "; ";
                        oss << issue.errors[i];
                    }
                }
                oss << "\n";
            }
        }

        static std::string joinMessages(const std::vector<std::string>& messages) {
            std::ostringstream oss;
            for (size_t i = 0; i < messages.size(); ++i) {
                if (i > 0) oss << "; ";
                oss << messages[i];
            }
            return oss.str();
        }

        static uint64_t launchNativeElfProcess(const apps::RegisteredApp& registryApp, const apps::LaunchDecision& launchDecision) {
            ProcessSpec spec;
            spec.name = std::string("nativeelf:") + (registryApp.manifest.id.empty() ? registryApp.manifest.displayName : registryApp.manifest.id);
            spec.entry = [registryApp, launchDecision](int, char**) -> int {
                apps::NativeElfLaunchResult nativeElfResult = apps::NativeElfLaunchPipeline::PrepareLaunch(registryApp, launchDecision);
                if (!nativeElfResult.success) {
                    std::string message = std::string("Native app launch failed: ") + (nativeElfResult.validationErrors.empty() ? nativeElfResult.message : joinMessages(nativeElfResult.validationErrors));
                    Logger::write(LogLevel::Warn, message);
                    NotificationManager::Add(message, NotificationLevel::Error);
                    return apps::GX_ERROR_FAILED;
                }

                apps::NativeElfImage nativeElfImage = apps::NativeElfImageLoader::LoadImage(nativeElfResult);
                if (!nativeElfImage.success) {
                    std::string message = std::string("Native app image load failed: ") + (nativeElfImage.diagnostics.empty() ? nativeElfResult.elfPath : joinMessages(nativeElfImage.diagnostics));
                    Logger::write(LogLevel::Warn, message);
                    NotificationManager::Add(message, NotificationLevel::Error);
                    return apps::GX_ERROR_FAILED;
                }

                apps::NativeAppRuntimeContext runtimeContext = apps::NativeAppRuntime::Prepare(registryApp, launchDecision, nativeElfResult, nativeElfImage);
                if (!runtimeContext.success) {
                    std::string message = std::string("Native app runtime prepare failed: ") + joinMessages(runtimeContext.diagnostics);
                    Logger::write(LogLevel::Warn, message);
                    NotificationManager::Add(message, NotificationLevel::Error);
                    return apps::GX_ERROR_FAILED;
                }

                std::string executorReason;
                if (!apps::NativeElfExecutor::CanExecute(nativeElfResult, nativeElfImage, runtimeContext, &executorReason)) {
                    std::string message = std::string("Native app launch unavailable: ") + executorReason;
                    Logger::write(LogLevel::Warn, message);
                    NotificationManager::Add(message, NotificationLevel::Error);
                    return apps::GX_ERROR_FAILED;
                }

                apps::NativeElfExecutionResult executionResult = apps::NativeElfExecutor::Execute(nativeElfResult, nativeElfImage, runtimeContext);
                if (!executionResult.success) {
                    std::string message = std::string("Native app execution failed: ") + (executionResult.failureReason.empty() ? executionResult.message : executionResult.failureReason);
                    Logger::write(LogLevel::Warn, message);
                    NotificationManager::Add(message, NotificationLevel::Error);
                    return executionResult.exitCode == 0 ? apps::GX_ERROR_FAILED : executionResult.exitCode;
                }

                Logger::write(LogLevel::Info, std::string("Native app execution completed: ") + registryApp.manifest.displayName + " exitCode=" + std::to_string(executionResult.exitCode));
                return executionResult.exitCode;
            };

            return ProcessTable::spawn(spec, { spec.name });
        }

        static void ensureDefaultAppsRegistered() {
            if (s_appRegistryInitialized) return;

            ++s_appRegistryInitializeCount;
            Logger::write(LogLevel::Info, "AppRegistry initializing, count=" + std::to_string(s_appRegistryInitializeCount));

            s_lastManifestScanResult = s_appRegistry.Scan();
            Logger::write(LogLevel::Info, "AppRegistry manifest scan succeeded: scanned=" + std::to_string(s_lastManifestScanResult.scannedManifestCount) + ", registered=" + std::to_string(s_lastManifestScanResult.registeredAppCount));
            logScanIssues("Invalid app manifest", s_lastManifestScanResult.invalidApps);
            logScanIssues("Duplicate app id", s_lastManifestScanResult.duplicateApps);

            s_lastBuiltInRegisterResult = s_appRegistry.RegisterBuiltInAppsAsManifests();
            logScanIssues("Duplicate app id", s_lastBuiltInRegisterResult.duplicateApps);

            refreshRegisteredAppsFromRegistry();
            ensureDefaultAppModelPins();
            s_appRegistryInitialized = true;
            Logger::write(LogLevel::Info, "AppRegistry initialized, desktop apps=" + std::to_string(DesktopService::GetRegisteredApps().size()));
        }

        static std::string canonicalAppName(const std::string& name) {
            if (name == "Files" || name == "ComputerFiles") return "FileExplorer";
            if (name == "Shutdown") return "ShutdownDialog";
            return name;
        }

        static apps::LaunchTargetType launchTargetTypeForAppKind(apps::AppKind kind) {
            switch (kind) {
            case apps::AppKind::BuiltIn: return apps::LaunchTargetType::BuiltInApp;
            case apps::AppKind::NativeElf: return apps::LaunchTargetType::NativeElfApp;
            case apps::AppKind::GXAppPackage: return apps::LaunchTargetType::GXAppPackage;
            case apps::AppKind::Service: return apps::LaunchTargetType::Service;
            case apps::AppKind::HypervisorGuest: return apps::LaunchTargetType::HypervisorGuest;
            case apps::AppKind::Script: return apps::LaunchTargetType::Script;
            case apps::AppKind::Unknown:
            default: return apps::LaunchTargetType::ManifestApp;
            }
        }

        static bool isPathLikeLaunchLabel(const std::string& label) {
            if (label.find('/') != std::string::npos || label.find('\\') != std::string::npos) return true;
            return label.size() > 2 && label[1] == ':';
        }

        static void fillLaunchTargetFromMetadata(apps::LaunchTarget& target, const apps::BuiltInAppMetadata& metadata) {
            target.type = apps::LaunchTargetType::BuiltInApp;
            target.appId = metadata.appId ? metadata.appId : "";
            target.displayName = metadata.displayName ? metadata.displayName : "";
            target.dispatchLaunchName = metadata.launchName ? metadata.launchName : "";
            target.hostedAvailable = apps::IsBuiltInAppAvailableInHosted(metadata);
            target.bareMetalAvailable = apps::IsBuiltInAppAvailableInBareMetal(metadata);
        }

        static void fillLaunchTargetFromRegisteredApp(apps::LaunchTarget& target, const RegisteredDesktopApp& app) {
            target.type = launchTargetTypeForAppKind(app.kind);
            target.appId = app.id;
            target.displayName = app.displayName;
            target.dispatchLaunchName = app.launchName;
            target.hostedAvailable = true;
            if (const apps::BuiltInAppMetadata* metadata = findMetadataForRegisteredDesktopApp(app)) {
                target.bareMetalAvailable = apps::IsBuiltInAppAvailableInBareMetal(*metadata);
            }
        }

        static void fillLaunchTargetFromRegistryApp(apps::LaunchTarget& target, const apps::RegisteredApp& app) {
            target.type = launchTargetTypeForAppKind(app.manifest.kind);
            target.appId = app.manifest.id;
            target.displayName = app.manifest.displayName;
            target.dispatchLaunchName = launchNameForApp(app);
            target.hostedAvailable = true;
            if (const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByAppId(app.manifest.id.c_str())) {
                target.bareMetalAvailable = apps::IsBuiltInAppAvailableInBareMetal(*metadata);
            }
        }

        static const char* diagnosticBool(bool value) {
            return value ? "true" : "false";
        }

        static bool launchTargetShadowIsUnresolved(const apps::LaunchTarget& target) {
            return target.type == apps::LaunchTargetType::Unknown ||
                target.diagnosticStatus.rfind("unresolved", 0) == 0;
        }

        static bool launchTargetShadowIsAliasFallback(const apps::LaunchTarget& target, const std::string& actualDispatch) {
            return !target.dispatchLaunchName.empty() && target.dispatchLaunchName != actualDispatch;
        }

        static bool launchTargetShadowAdapterMismatchIsAccepted(const apps::LaunchTarget& target, const std::string& actualDispatch, const std::string& adapterLegacyDispatch) {
            if (adapterLegacyDispatch.empty() || adapterLegacyDispatch == actualDispatch) return false;
            if (target.type == apps::LaunchTargetType::LegacyAlias || target.type == apps::LaunchTargetType::ShellAction) return true;
            if (target.diagnosticStatus == "resolved-alias" || target.diagnosticStatus == "resolved-shell") return true;
            return false;
        }

        static std::string launchTargetShadowAdapterComparisonStatus(const apps::LaunchTarget& target, const std::string& actualDispatch, const std::string& adapterLegacyDispatch) {
            if (!adapterLegacyDispatch.empty() && adapterLegacyDispatch == actualDispatch) return "match";
            if (launchTargetShadowAdapterMismatchIsAccepted(target, actualDispatch, adapterLegacyDispatch)) return "accepted-mismatch";
            return "unexpected-mismatch";
        }

        static std::string resolveUiLaunchTargetInput(const std::string& source, const std::string& uiLabel, const std::string& shortcutTarget, const std::string& actualDispatch) {
            if (source == "DesktopShortcut") {
                if (!shortcutTarget.empty()) return shortcutTarget;
                if (!uiLabel.empty()) return uiLabel;
            }
            if (!uiLabel.empty()) return uiLabel;
            if (!shortcutTarget.empty()) return shortcutTarget;
            return actualDispatch;
        }

        static void countLaunchTargetShadowAdapterComparison(uint64_t& matches, uint64_t& acceptedMismatches, uint64_t& unexpectedMismatches, const std::string& comparisonStatus) {
            if (comparisonStatus == "match") ++matches;
            else if (comparisonStatus == "accepted-mismatch") ++acceptedMismatches;
            else ++unexpectedMismatches;
        }

        static void appendLaunchTargetShadowSourceLine(std::ostringstream& oss, const char* source, uint64_t observations, uint64_t unresolved, uint64_t aliasFallback, uint64_t adapterMatches, uint64_t adapterAcceptedMismatches, uint64_t adapterUnexpectedMismatches) {
            oss << "  source=" << source
                << " observations=" << observations
                << " unresolved=" << unresolved
                << " aliasFallback=" << aliasFallback
                << " adapterMatches=" << adapterMatches
                << " adapterAcceptedMismatches=" << adapterAcceptedMismatches
                << " adapterUnexpectedMismatches=" << adapterUnexpectedMismatches << "\n";
        }

        static void appendLaunchTargetShadowCandidateSourceLine(std::ostringstream& oss, const char* source, uint64_t matches, uint64_t acceptedMismatches, uint64_t unexpectedMismatches) {
            oss << "  typedDispatchCandidate source=" << source
                << " matches=" << matches
                << " acceptedMismatches=" << acceptedMismatches
                << " unexpectedMismatches=" << unexpectedMismatches << "\n";
        }

        struct LaunchStorageResolutionCounts {
            size_t total = 0;
            size_t typedDerivable = 0;
            size_t unresolved = 0;
        };

        static void countLaunchStorageLabel(LaunchStorageResolutionCounts& counts, const std::string& label) {
            if (label.empty()) return;
            ++counts.total;
            const apps::LaunchTarget target = DesktopService::ResolveLaunchTarget(label);
            if (target.type == apps::LaunchTargetType::Unknown) ++counts.unresolved;
            else ++counts.typedDerivable;
        }

        static LaunchStorageResolutionCounts countLaunchStorageLabels(const std::vector<std::string>& labels) {
            LaunchStorageResolutionCounts counts;
            for (const std::string& label : labels) countLaunchStorageLabel(counts, label);
            return counts;
        }

        static void appendLaunchStorageSite(std::ostringstream& oss, const std::string& site, const std::string& location, const std::string& fields, const std::string& stores, const LaunchStorageResolutionCounts& counts, const std::string& risk, const std::string& note) {
            oss << "  site=" << site
                << " location=" << location
                << " fields=" << fields
                << " stores=" << stores
                << " count=" << counts.total
                << " typedDerivable=" << counts.typedDerivable
                << " unresolved=" << counts.unresolved
                << " risk=" << risk
                << " note=" << note << "\n";
        }

        static void appendLaunchStorageStaticSite(std::ostringstream& oss, const std::string& site, const std::string& location, const std::string& fields, const std::string& stores, const std::string& count, const std::string& typedDerivable, const std::string& risk, const std::string& note) {
            oss << "  site=" << site
                << " location=" << location
                << " fields=" << fields
                << " stores=" << stores
                << " count=" << count
                << " typedDerivable=" << typedDerivable
                << " risk=" << risk
                << " note=" << note << "\n";
        }

        struct LaunchStoragePreviewCounts {
            struct UnsupportedAliasDetail {
                std::string target;
                std::string label;
                std::string mapsTo;
                std::string reason;
                size_t count = 0;
            };

            size_t total = 0;
            size_t ready = 0;
            size_t alias = 0;
            size_t shellAction = 0;
            size_t unresolved = 0;
            size_t skippedLayoutOnly = 0;
            size_t targetSpecificUnsupportedAliases = 0;
            size_t highRisk = 0;
            size_t printed = 0;
            size_t truncated = 0;
            std::vector<UnsupportedAliasDetail> unsupportedAliasDetails;
        };

        static std::string quoteDiagnosticValue(const std::string& value) {
            std::ostringstream out;
            out << '"';
            for (char ch : value) {
                if (ch == '"' || ch == '\\') out << '\\' << ch;
                else if (ch == '\n') out << "\\n";
                else if (ch == '\r') out << "\\r";
                else out << ch;
            }
            out << '"';
            return out.str();
        }

        static std::string launchStorageExistingKind(const std::string& value, const apps::LaunchTarget& target, const std::string& hint) {
            if (!hint.empty()) return hint;
            if (target.type == apps::LaunchTargetType::LegacyAlias) return "legacy alias";
            if (target.type == apps::LaunchTargetType::ShellAction) return "shell action";
            if (target.type == apps::LaunchTargetType::FileOpen || isPathLikeLaunchLabel(value)) return "file path";
            if (!target.appId.empty() && value == target.appId) return "app ID";
            if (!target.displayName.empty() && value == target.displayName) return "display name";
            if (!target.dispatchLaunchName.empty() && value == target.dispatchLaunchName) return "launch name";
            return target.type == apps::LaunchTargetType::Unknown ? "unknown" : "launch string";
        }

        static std::string launchStoragePreviewStatus(const apps::LaunchTarget& target) {
            if (target.type == apps::LaunchTargetType::Unknown) return "unresolved";
            if (target.type == apps::LaunchTargetType::LegacyAlias) return "alias";
            if (target.type == apps::LaunchTargetType::ShellAction) return "shell-action";
            return "ready";
        }

        static std::string launchStoragePreviewRisk(const std::string& baseRisk, const std::string& status) {
            if (status == "unresolved") return "high";
            if (status == "alias" || status == "shell-action") return "medium";
            return baseRisk.empty() ? "medium" : baseRisk;
        }

        static bool isTargetSpecificUnsupportedAlias(const apps::LaunchTarget& target) {
            return target.type == apps::LaunchTargetType::LegacyAlias &&
                target.diagnosticStatus == "unsupported-target";
        }

        static std::string compactUnsupportedAliasReason(const apps::LaunchTarget& target) {
            if (target.diagnosticStatus == "unsupported-target") {
                return "No bare-metal AppManager registration yet";
            }
            return target.diagnosticReason;
        }

        static std::string proposedLaunchTargetRecord(const apps::LaunchTarget& target) {
            std::ostringstream record;
            record << "{targetType=" << apps::ToString(target.type);
            if (!target.appId.empty()) record << ",appId=" << quoteDiagnosticValue(target.appId);
            if (!target.displayName.empty()) record << ",displayName=" << quoteDiagnosticValue(target.displayName);
            if (!target.dispatchLaunchName.empty()) record << ",launchName=" << quoteDiagnosticValue(target.dispatchLaunchName);
            if (!target.legacyAlias.empty()) record << ",legacyAlias=" << quoteDiagnosticValue(target.legacyAlias);
            if (!target.shellAction.empty()) record << ",shellAction=" << quoteDiagnosticValue(target.shellAction);
            if (!target.pathParameter.empty()) record << ",path=" << quoteDiagnosticValue(target.pathParameter);
            record << ",hosted=" << diagnosticBool(target.hostedAvailable)
                   << ",bareMetal=" << diagnosticBool(target.bareMetalAvailable)
                   << "}";
            return record.str();
        }

        static void countLaunchStoragePreviewStatus(LaunchStoragePreviewCounts& counts, const std::string& status, const std::string& risk) {
            ++counts.total;
            if (status == "ready") ++counts.ready;
            else if (status == "alias") ++counts.alias;
            else if (status == "shell-action") ++counts.shellAction;
            else if (status == "skip-layout-only") ++counts.skippedLayoutOnly;
            else ++counts.unresolved;
            if (risk == "high") ++counts.highRisk;
        }

        static void countLaunchStoragePreviewTargetSpecificAlias(LaunchStoragePreviewCounts& counts, const apps::LaunchTarget& target, const std::string& targetName) {
            if (!isTargetSpecificUnsupportedAlias(target)) return;
            ++counts.targetSpecificUnsupportedAliases;

            const std::string label = target.legacyAlias.empty() ? target.originalLabel : target.legacyAlias;
            const std::string mapsTo = target.displayName.empty() ? target.appId : target.displayName;
            const std::string reason = compactUnsupportedAliasReason(target);
            for (LaunchStoragePreviewCounts::UnsupportedAliasDetail& detail : counts.unsupportedAliasDetails) {
                if (detail.target == targetName && detail.label == label && detail.mapsTo == mapsTo && detail.reason == reason) {
                    ++detail.count;
                    return;
                }
            }

            LaunchStoragePreviewCounts::UnsupportedAliasDetail detail;
            detail.target = targetName;
            detail.label = label;
            detail.mapsTo = mapsTo;
            detail.reason = reason;
            detail.count = 1;
            counts.unsupportedAliasDetails.push_back(detail);
        }

        static void appendLaunchStoragePreviewRecord(std::ostringstream& oss, LaunchStoragePreviewCounts& counts, const std::string& site, size_t index, const std::string& value, const std::string& existingKindHint, const std::string& baseRisk, const size_t maxRows) {
            const apps::LaunchTarget target = DesktopService::ResolveLaunchTarget(value);
            std::string adapterStatus;
            std::string adapterReason;
            const std::string legacyDispatch = DesktopService::LegacyDispatchStringForLaunchTarget(target, adapterStatus, adapterReason);
            const std::string status = launchStoragePreviewStatus(target);
            const std::string risk = launchStoragePreviewRisk(baseRisk, status);
            countLaunchStoragePreviewStatus(counts, status, risk);
            countLaunchStoragePreviewTargetSpecificAlias(counts, target, "hosted");

            if (counts.printed >= maxRows) {
                ++counts.truncated;
                return;
            }

            ++counts.printed;
            oss << "  record site=" << site
                << " index=" << index
                << " existing=" << quoteDiagnosticValue(value)
                << " existingKind=" << quoteDiagnosticValue(launchStorageExistingKind(value, target, existingKindHint))
                << " resolvedType=" << apps::ToString(target.type)
                << " appId=" << quoteDiagnosticValue(target.appId)
                << " displayName=" << quoteDiagnosticValue(target.displayName)
                << " legacyDispatch=" << quoteDiagnosticValue(legacyDispatch)
                << " proposed=" << proposedLaunchTargetRecord(target)
                << " risk=" << risk
                << " status=" << status;
            if (!target.diagnosticReason.empty()) oss << " reason=" << quoteDiagnosticValue(target.diagnosticReason);
            oss << "\n";
        }

        static void appendLaunchStoragePreviewSkip(std::ostringstream& oss, LaunchStoragePreviewCounts& counts, const std::string& site, size_t index, const std::string& value, const std::string& note, const size_t maxRows) {
            countLaunchStoragePreviewStatus(counts, "skip-layout-only", "low");
            if (counts.printed >= maxRows) {
                ++counts.truncated;
                return;
            }

            ++counts.printed;
            oss << "  record site=" << site
                << " index=" << index
                << " existing=" << quoteDiagnosticValue(value)
                << " existingKind=\"layout key\""
                << " resolvedType=Unknown"
                << " appId=\"\" displayName=\"\" legacyDispatch=\"\""
                << " proposed={skip=\"layout-only\"}"
                << " risk=low"
                << " status=skip-layout-only"
                << " reason=" << quoteDiagnosticValue(note) << "\n";
        }

        static void appendLaunchStoragePreviewLabels(std::ostringstream& oss, LaunchStoragePreviewCounts& counts, const std::string& site, const std::vector<std::string>& labels, const std::string& existingKindHint, const std::string& risk, const size_t maxRows) {
            for (size_t i = 0; i < labels.size(); ++i) {
                appendLaunchStoragePreviewRecord(oss, counts, site, i, labels[i], existingKindHint, risk, maxRows);
            }
        }

        static LaunchStoragePreviewCounts collectLaunchStoragePreviewCounts(
            bool cfgLoaded,
            const DesktopConfigData& cfg,
            const std::vector<std::string>& inMemoryPinned,
            const std::vector<std::string>& inMemoryRecentPrograms,
            const std::vector<std::string>& inMemoryRecentDocuments,
            const std::vector<RegisteredDesktopApp>& registeredApps,
            std::ostringstream* rows,
            const size_t maxRows) {
            LaunchStoragePreviewCounts counts;
            std::ostringstream sink;
            std::ostringstream& out = rows ? *rows : sink;

            if (cfgLoaded) {
                appendLaunchStoragePreviewLabels(out, counts, "desktop.json:pinned", cfg.pinned, "", "medium", maxRows);
                appendLaunchStoragePreviewLabels(out, counts, "desktop.json:recent", cfg.recent, "", "medium", maxRows);

                for (size_t i = 0; i < cfg.desktopShortcuts.size(); ++i) {
                    const DesktopShortcutRec& shortcut = cfg.desktopShortcuts[i];
                    const std::string type = shortcut.shortcutType.empty() ? (shortcut.targetPath.empty() ? "App" : "File") : shortcut.shortcutType;
                    if (type == "App") {
                        appendLaunchStoragePreviewRecord(out, counts, "desktop.json:desktopShortcuts.App", i, shortcut.targetAppId, "app ID", "low", maxRows);
                    } else if (type == "File" || type == "Folder") {
                        appendLaunchStoragePreviewRecord(out, counts, "desktop.json:desktopShortcuts.FileFolder", i, shortcut.targetPath, "file path", "low", maxRows);
                    }
                }

                for (size_t i = 0; i < cfg.iconPositions.size(); ++i) {
                    appendLaunchStoragePreviewSkip(out, counts, "desktop.json:iconPositions", i, cfg.iconPositions[i].name, "Icon position record stores layout only and should not become a launch target", maxRows);
                }
            }

            appendLaunchStoragePreviewLabels(out, counts, "DesktopService:s_pinned", inMemoryPinned, "", "medium", maxRows);
            appendLaunchStoragePreviewLabels(out, counts, "DesktopService:s_recentPrograms", inMemoryRecentPrograms, "", "medium", maxRows);
            appendLaunchStoragePreviewLabels(out, counts, "DesktopService:s_recentDocuments", inMemoryRecentDocuments, "file path", "low", maxRows);

            std::vector<std::string> allProgramLabels;
            for (const RegisteredDesktopApp& app : registeredApps) allProgramLabels.push_back(app.displayName);
            appendLaunchStoragePreviewLabels(out, counts, "Compositor:g_startMenuAllProgsSorted", allProgramLabels, "display name", "low", maxRows);

            std::vector<std::string> shellLabels = { "ComputerFiles", "Console", "Trash", "ControlPanel", "TaskManager" };
            appendLaunchStoragePreviewLabels(out, counts, "Compositor:rightColumnAndSystemObjects", shellLabels, "", "medium", maxRows);

            return counts;
        }

        static void countLaunchStoragePreviewTarget(LaunchStoragePreviewCounts& counts, const apps::LaunchTarget& target, const std::string& baseRisk, const std::string& targetName) {
            const std::string status = launchStoragePreviewStatus(target);
            const std::string risk = launchStoragePreviewRisk(baseRisk, status);
            countLaunchStoragePreviewStatus(counts, status, risk);
            countLaunchStoragePreviewTargetSpecificAlias(counts, target, targetName);
        }

        static void countLaunchStoragePreviewSkipOnly(LaunchStoragePreviewCounts& counts) {
            countLaunchStoragePreviewStatus(counts, "skip-layout-only", "low");
        }

        static apps::LaunchTarget resolveBareMetalLaunchTargetForComparison(const std::string& label);

        static void countBareMetalPreviewMirrorLabel(LaunchStoragePreviewCounts& counts, const std::string& label, const std::string& baseRisk) {
            countLaunchStoragePreviewTarget(counts, resolveBareMetalLaunchTargetForComparison(label), baseRisk, "bareMetal");
        }

        static LaunchStoragePreviewCounts bareMetalStoragePreviewCountsForComparison() {
            LaunchStoragePreviewCounts counts;

            const char* const startMenuApps[] = {
                "Calculator", "Notepad", "Console", "Trash", "TaskManager", "DiskManager",
                "DisplayOptions", "guideXOS Navigator", "HDInstaller", "AppModel", "Paint",
                "Clock", "Files", "ImgViewer"
            };
            for (const char* label : startMenuApps) countBareMetalPreviewMirrorLabel(counts, label, "medium");

            const char* const allPrograms[] = {
                "Calculator", "Clock", "Console", "ControlPanel", "DiskManager", "Files",
                "guideXOS Navigator", "HDInstaller", "ImgViewer", "AppModel", "Notepad",
                "Paint", "TaskManager", "Trash"
            };
            for (const char* label : allPrograms) countBareMetalPreviewMirrorLabel(counts, label, "medium");

            const char* const rightColumn[] = {
                "Computer", "Documents", "Pictures", "Music", "Network", "Control Panel", "Settings"
            };
            for (const char* label : rightColumn) countBareMetalPreviewMirrorLabel(counts, label, "medium");

            const char* const systemIconFlags[] = { "Trash", "ThisSystem", "FileManager", "SystemSettings" };
            for (const char* ignored : systemIconFlags) {
                (void)ignored;
                countLaunchStoragePreviewSkipOnly(counts);
            }

            return counts;
        }

        static void appendLaunchStoragePreviewCountsLine(std::ostringstream& oss, const std::string& label, const LaunchStoragePreviewCounts& counts, const std::string& extra = "") {
            oss << label
                << " total=" << counts.total
                << " ready=" << counts.ready
                << " alias=" << counts.alias
                << " shellAction=" << counts.shellAction
                << " unresolved=" << counts.unresolved
                << " skippedLayoutOnly=" << counts.skippedLayoutOnly
                << " targetSpecificUnsupportedAliases=" << counts.targetSpecificUnsupportedAliases
                << " highRisk=" << counts.highRisk;
            if (!extra.empty()) oss << " " << extra;
            oss << "\n";
        }

        static void appendLaunchStoragePreviewUnsupportedAliasDetails(std::ostringstream& oss, const LaunchStoragePreviewCounts& hostedCounts, const LaunchStoragePreviewCounts& bareMetalCounts) {
            const size_t totalDetails = hostedCounts.unsupportedAliasDetails.size() + bareMetalCounts.unsupportedAliasDetails.size();
            if (totalDetails == 0) return;

            oss << "targetSpecificUnsupportedAliasDetails:\n";
            const auto appendDetails = [&oss](const LaunchStoragePreviewCounts& counts) {
                for (const LaunchStoragePreviewCounts::UnsupportedAliasDetail& detail : counts.unsupportedAliasDetails) {
                    oss << "  target=" << detail.target
                        << " label=" << quoteDiagnosticValue(detail.label)
                        << " count=" << detail.count
                        << " mapsTo=" << quoteDiagnosticValue(detail.mapsTo)
                        << " reason=" << quoteDiagnosticValue(detail.reason) << "\n";
                }
            };
            appendDetails(hostedCounts);
            appendDetails(bareMetalCounts);
        }

        static const char* const kLaunchTargetComparisonLabels[] = {
            "Notepad",
            "gxos.builtin.notepad",
            "FileExplorer",
            "Files",
            "guideXOS Navigator",
            "ComputerFiles",
            "AppModel",
            "TotallyUnknownLaunchThing"
        };

        static bool isBareMetalShellLabelForComparison(const std::string& label) {
            return label == "Console" ||
                label == "Terminal" ||
                label == "Computer" ||
                label == "This System" ||
                label == "Documents" ||
                label == "Pictures" ||
                label == "Music" ||
                label == "Network" ||
                label == "Control Panel" ||
                label == "Settings" ||
                label == "System Settings";
        }

        static apps::LaunchTarget resolveBareMetalLaunchTargetForComparison(const std::string& label) {
            apps::LaunchTarget target;
            target.originalLabel = label;

            if (label.empty()) {
                target.type = apps::LaunchTargetType::Unknown;
                target.diagnosticStatus = "unresolved";
                target.diagnosticReason = "No launch label supplied";
                return target;
            }

            if (label == "AppModel") {
                if (const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByAppId("gxos.builtin.appmodeldemo")) {
                    fillLaunchTargetFromMetadata(target, *metadata);
                }
                target.type = apps::LaunchTargetType::LegacyAlias;
                target.legacyAlias = "AppModel";
                target.appId = "gxos.builtin.appmodeldemo";
                target.displayName = "App Model Demo";
                target.dispatchLaunchName = "AppModel";
                target.hostedAvailable = true;
                target.bareMetalAvailable = true;
                target.diagnosticStatus = "resolved-alias";
                target.diagnosticReason = "Bare-metal AppModel opens the local app-model explanation view";
                return target;
            }

            if (label == "Files") {
                if (const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByKernelLegacyAlias("Files")) {
                    fillLaunchTargetFromMetadata(target, *metadata);
                }
                target.type = apps::LaunchTargetType::LegacyAlias;
                target.legacyAlias = "Files";
                target.displayName = "FileExplorer";
                target.dispatchLaunchName = "Files";
                target.bareMetalAvailable = true;
                target.diagnosticStatus = "resolved-alias";
                target.diagnosticReason = "Bare-metal legacy alias registered for FileExplorer";
                return target;
            }

            if (label == "ImgViewer") {
                if (const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByAppId("gxos.builtin.imageviewer")) {
                    fillLaunchTargetFromMetadata(target, *metadata);
                }
                target.type = apps::LaunchTargetType::LegacyAlias;
                target.legacyAlias = "ImgViewer";
                target.appId = "gxos.builtin.imageviewer";
                target.displayName = "ImageViewer";
                target.dispatchLaunchName = "ImgViewer";
                target.hostedAvailable = true;
                target.bareMetalAvailable = false;
                target.diagnosticStatus = "unsupported-target";
                target.diagnosticReason = "Bare-metal static Start Menu label for hosted ImageViewer; no current bare-metal AppManager registration";
                return target;
            }

            if (isBareMetalShellLabelForComparison(label)) {
                target.type = apps::LaunchTargetType::ShellAction;
                target.displayName = label;
                target.shellAction = label;
                target.hostedAvailable = false;
                target.bareMetalAvailable = true;
                target.diagnosticStatus = "resolved-shell";
                if (label == "Console" || label == "Terminal") {
                    target.dispatchLaunchName = "Console";
                } else if (label == "This System") {
                    target.dispatchLaunchName = "Files";
                    target.pathParameter = "/";
                } else if (label == "Control Panel" || label == "Settings" || label == "System Settings") {
                    target.dispatchLaunchName = "DisplayOptions";
                }
                target.diagnosticReason = "Bare-metal shell/system label mirror for comparison diagnostics";
                return target;
            }

            if (isPathLikeLaunchLabel(label)) {
                target.type = apps::LaunchTargetType::FileOpen;
                target.pathParameter = label;
                target.hostedAvailable = false;
                target.bareMetalAvailable = true;
                target.diagnosticStatus = "file-open-unchecked";
                target.diagnosticReason = "Hosted comparison mirror cannot stat the bare-metal VFS";
                return target;
            }

            if (const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByIdentity(label.c_str())) {
                target.type = apps::LaunchTargetType::BuiltInApp;
                target.appId = metadata->appId ? metadata->appId : "";
                target.displayName = metadata->displayName ? metadata->displayName : "";
                target.dispatchLaunchName = metadata->kernelAppName ? metadata->kernelAppName : (metadata->launchName ? metadata->launchName : "");
                target.hostedAvailable = apps::IsBuiltInAppAvailableInHosted(*metadata);
                target.bareMetalAvailable = apps::IsBuiltInAppAvailableInBareMetal(*metadata) && currentBareMetalRegistrationExistsForMetadata(*metadata);
                target.diagnosticStatus = target.bareMetalAvailable ? "resolved" : "unsupported-target";
                target.diagnosticReason = target.bareMetalAvailable
                    ? "Matched shared built-in metadata and mirrored bare-metal registration"
                    : "Matched shared metadata, but no mirrored bare-metal registration exists";
                return target;
            }

            target.type = apps::LaunchTargetType::Unknown;
            target.diagnosticStatus = "unresolved";
            target.diagnosticReason = "No mirrored bare-metal target matched";
            return target;
        }

        static bool sameLaunchTargetCore(const apps::LaunchTarget& a, const apps::LaunchTarget& b) {
            return a.type == b.type &&
                a.appId == b.appId &&
                a.dispatchLaunchName == b.dispatchLaunchName &&
                a.diagnosticStatus == b.diagnosticStatus;
        }

        static bool sameAppIdNonEmpty(const apps::LaunchTarget& a, const apps::LaunchTarget& b) {
            return !a.appId.empty() && a.appId == b.appId;
        }

        static std::string launchTargetComparisonStatus(const std::string& label, const apps::LaunchTarget& hosted, const apps::LaunchTarget& bareMetal) {
            if (sameLaunchTargetCore(hosted, bareMetal)) return "exact";
            if (label == "ComputerFiles") return "intentional-difference";
            if (label == "AppModel") return "intentional-difference";
            if (sameAppIdNonEmpty(hosted, bareMetal) &&
                (hosted.type == apps::LaunchTargetType::LegacyAlias || bareMetal.type == apps::LaunchTargetType::LegacyAlias)) {
                return "accepted-alias";
            }
            return "unexpected-drift";
        }

        static std::string launchTargetComparisonNote(const std::string& label, const std::string& status) {
            if (status == "exact") return "hosted and bare-metal diagnostic targets match";
            if (status == "accepted-alias") return "same app identity with an accepted legacy alias difference";
            if (label == "ComputerFiles") return "hosted shell/system label; bare-metal uses separate right-column labels and system objects";
            if (label == "AppModel") return "legacy app-model demo alias has target-specific dispatch names";
            return "investigate launch target drift before feeding typed targets into dispatch";
        }

        struct LaunchTargetComparisonCounts {
            int labels = 0;
            int exactMatches = 0;
            int acceptedAliasMatches = 0;
            int intentionalDifferences = 0;
            int unexpectedDrift = 0;
        };

        static void recordLaunchTargetComparisonResult(LaunchTargetComparisonCounts& counts, const std::string& result) {
            ++counts.labels;
            if (result == "exact") ++counts.exactMatches;
            else if (result == "accepted-alias") ++counts.acceptedAliasMatches;
            else if (result == "intentional-difference") ++counts.intentionalDifferences;
            else ++counts.unexpectedDrift;
        }

        static LaunchTargetComparisonCounts launchTargetComparisonCounts() {
            LaunchTargetComparisonCounts counts;
            const size_t labelCount = sizeof(kLaunchTargetComparisonLabels) / sizeof(kLaunchTargetComparisonLabels[0]);
            for (size_t i = 0; i < labelCount; ++i) {
                const std::string label = kLaunchTargetComparisonLabels[i];
                const apps::LaunchTarget hosted = DesktopService::ResolveLaunchTarget(label);
                const apps::LaunchTarget bareMetal = resolveBareMetalLaunchTargetForComparison(label);
                recordLaunchTargetComparisonResult(counts, launchTargetComparisonStatus(label, hosted, bareMetal));
            }
            return counts;
        }

        // Static member initialization
        std::vector<PinnedItem> DesktopService::s_pinned;
        std::vector<RecentProgramEntry> DesktopService::s_recentPrograms;
        std::vector<RecentDocumentEntry> DesktopService::s_recentDocuments;
        std::vector<RegisteredDesktopApp> DesktopService::s_apps;

        static uint64_t currentTicks() {
            return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        void DesktopService::PinApp(const std::string& name) {
            if (name.empty() || IsPinned(name)) return;
            PinnedItem item;
            item.name = name;
            item.kind = PinnedKind::App;
            item.iconName = "document"; // default
            s_pinned.push_back(item);
            SaveState();
            Logger::write(LogLevel::Info, std::string("Pinned app: ") + name);
        }
        /// <summary>
        /// Pin File
        /// </summary>
        /// <param name="displayName"></param>
        /// <param name="absolutePath"></param>
        void DesktopService::PinFile(const std::string& displayName, const std::string& absolutePath) {
            if (displayName.empty() || absolutePath.empty() || IsPinned(displayName)) return;
            PinnedItem item;
            item.name = displayName;
            item.path = absolutePath;
            item.kind = PinnedKind::File;
            item.iconName = "document";
            s_pinned.push_back(item);
            SaveState();
            Logger::write(LogLevel::Info, std::string("Pinned file: ") + displayName + " -> " + absolutePath);
        }
        /// <summary>
        /// Pin Special
        /// </summary>
        /// <param name="name"></param>
        void DesktopService::PinSpecial(const std::string& name) {
            if (name.empty() || IsPinned(name)) return;
            PinnedItem item;
            item.name = name;
            item.kind = PinnedKind::Special;
            item.iconName = "folder";
            s_pinned.push_back(item);
            SaveState();
            Logger::write(LogLevel::Info, std::string("Pinned special: ") + name);
        }

        void DesktopService::Unpin(const std::string& name) {
            for (auto it = s_pinned.begin(); it != s_pinned.end(); ++it) {
                if (it->name == name) {
                    s_pinned.erase(it);
                    SaveState();
                    Logger::write(LogLevel::Info, std::string("Unpinned: ") + name);
                    return;
                }
            }
        }

        bool DesktopService::IsPinned(const std::string& name) {
            for (const auto& item : s_pinned) {
                if (item.name == name) return true;
            }
            return false;
        }

        void DesktopService::AddRecentProgram(const std::string& name) {
            if (name.empty()) return;

            // Remove existing entry if present
            for (auto it = s_recentPrograms.begin(); it != s_recentPrograms.end(); ++it) {
                if (it->name == name) {
                    s_recentPrograms.erase(it);
                    break;
                }
            }

            // Add to front
            RecentProgramEntry entry;
            entry.name = name;
            entry.lastUsedTicks = currentTicks();
            entry.iconName = "document";
            s_recentPrograms.insert(s_recentPrograms.begin(), entry);

            // Trim to max
            if (s_recentPrograms.size() > kMaxRecentPrograms) {
                s_recentPrograms.resize(kMaxRecentPrograms);
            }

            SaveState();
        }

        void DesktopService::AddRecentDocument(const std::string& path) {
            if (path.empty()) return;

            // Remove existing
            for (auto it = s_recentDocuments.begin(); it != s_recentDocuments.end(); ++it) {
                if (it->path == path) {
                    s_recentDocuments.erase(it);
                    break;
                }
            }

            // Add to front
            RecentDocumentEntry entry;
            entry.path = path;
            entry.lastUsedTicks = currentTicks();
            entry.iconName = "document";
            s_recentDocuments.insert(s_recentDocuments.begin(), entry);

            // Trim
            if (s_recentDocuments.size() > kMaxRecentDocuments) {
                s_recentDocuments.resize(kMaxRecentDocuments);
            }

            SaveState();
        }

        void DesktopService::RegisterApp(const std::string& name, const std::string& iconName) {
            if (name.empty()) return;
            RegisterApp(std::string("gxos.legacy.") + name, name, iconName, apps::AppKind::BuiltIn, name);
        }

        void DesktopService::RegisterApp(const std::string& id, const std::string& displayName, const std::string& icon, apps::AppKind kind, const std::string& launchName) {
            RegisterApp(id, displayName, icon, kind, launchName, "Legacy");
        }

        void DesktopService::RegisterApp(const std::string& id, const std::string& displayName, const std::string& icon, apps::AppKind kind, const std::string& launchName, const std::string& source) {
            if (id.empty() || displayName.empty()) return;
            for (auto& app : s_apps) {
                if (app.id == id) {
                    app.displayName = displayName;
                    app.icon = icon;
                    app.kind = kind;
                    app.launchName = launchName.empty() ? displayName : launchName;
                    app.source = source;
                    return;
                }
            }

            RegisteredDesktopApp app;
            app.id = id;
            app.displayName = displayName;
            app.icon = icon;
            app.kind = kind;
            app.launchName = launchName.empty() ? displayName : launchName;
            app.source = source;
            s_apps.push_back(app);
            Logger::write(LogLevel::Info, std::string("Registered app: ") + displayName);
        }

        std::string DesktopService::GetRegisteredAppsVerboseDiagnostic() {
            ensureDefaultAppsRegistered();

            std::ostringstream oss;
            oss << "AppRegistry diagnostic\n";
            oss << "initialized=" << (s_appRegistryInitialized ? "true" : "false") << " initCount=" << s_appRegistryInitializeCount << "\n";
            oss << "sources:\n";
            for (const auto& source : apps::AppRegistry::DefaultSources()) {
                oss << "  " << apps::AppRegistry::ToString(source.kind) << "\n";
            }
            oss << "manifestScan scanned=" << s_lastManifestScanResult.scannedManifestCount << " registered=" << s_lastManifestScanResult.registeredAppCount << "\n";
            oss << "builtInRegister scanned=" << s_lastBuiltInRegisterResult.scannedManifestCount << " registered=" << s_lastBuiltInRegisterResult.registeredAppCount << "\n";
            appendScanIssues(oss, "invalidManifests", s_lastManifestScanResult.invalidApps);
            appendScanIssues(oss, "duplicateAppIds(manifestScan)", s_lastManifestScanResult.duplicateApps);
            appendScanIssues(oss, "duplicateAppIds(builtInRegister)", s_lastBuiltInRegisterResult.duplicateApps);
            oss << "registeredApps: " << s_apps.size() << "\n";
            for (const auto& app : s_apps) {
                oss << "  id=" << app.id
                    << " displayName=" << app.displayName
                    << " kind=" << apps::ToString(app.kind)
                    << " icon=" << app.icon
                    << " launchName=" << app.launchName
                    << " source=" << app.source;
                if (app.displayName == "HDInstaller" || app.launchName == "HDInstaller") {
                    oss << " availability=unavailable reason=HD Installer is not available in this runtime target";
                }
                oss << "\n";
            }
            oss << "launchPolicy: BuiltIn uses existing hardcoded launch branch; NativeElf/GXAppPackage return: manifest found but execution is not implemented yet\n";
            oss << "\n" << BuiltInAppMetadataCoverageDiagnostic();
            return oss.str();
        }

        std::string DesktopService::GetRegisteredAppsDiagnostic() {
            ensureDefaultAppsRegistered();

            std::ostringstream oss;
            oss << "Registered Applications (" << s_apps.size() << "):\n";
            for (const auto& app : s_apps) {
                oss << "  " << app.displayName
                    << " [" << apps::ToString(app.kind) << "]"
                    << " id=" << app.id
                    << " launch=" << app.launchName
                    << " source=" << app.source << "\n";
            }
            return oss.str();
        }

        std::string DesktopService::AppModelSummaryDiagnostic() {
            ensureDefaultAppsRegistered();

            const size_t duplicateCount = duplicateIdsFromScanIssues(s_lastManifestScanResult, s_lastBuiltInRegisterResult).size();
            const int namespaceWarningCount = appIdNamespaceWarningCount();
            const int hostedRegisteredMissingMetadata = hostedRegisteredBuiltInsMissingMetadataCount();
            const int bareMetalRegisteredMissingMetadata = bareMetalRegisteredKernelAppsMissingMetadataCount();
            const int metadataWithoutHostedRegistration = metadataWithoutHostedRegistrationCount();
            const int metadataWithoutBareMetalRegistration = metadataWithoutBareMetalRegistrationCount();
            const LaunchTargetComparisonCounts launchTargetCounts = launchTargetComparisonCounts();
            const LaunchTargetShadowCounters shadowCounters = GetLaunchTargetShadowCounters();
            DesktopConfigData storagePreviewCfg;
            std::string storagePreviewCfgErr;
            const bool storagePreviewCfgLoaded = DesktopConfig::Load("desktop.json", storagePreviewCfg, storagePreviewCfgErr);
            std::vector<std::string> storagePreviewPinned;
            for (const PinnedItem& item : s_pinned) storagePreviewPinned.push_back(item.name);
            std::vector<std::string> storagePreviewRecentPrograms;
            for (const RecentProgramEntry& entry : s_recentPrograms) storagePreviewRecentPrograms.push_back(entry.name);
            std::vector<std::string> storagePreviewRecentDocuments;
            for (const RecentDocumentEntry& entry : s_recentDocuments) storagePreviewRecentDocuments.push_back(entry.path);
            const LaunchStoragePreviewCounts storagePreviewCounts = collectLaunchStoragePreviewCounts(
                storagePreviewCfgLoaded,
                storagePreviewCfg,
                storagePreviewPinned,
                storagePreviewRecentPrograms,
                storagePreviewRecentDocuments,
                s_apps,
                nullptr,
                0);
            const LaunchStoragePreviewCounts storagePreviewCompareBareMetalCounts = bareMetalStoragePreviewCountsForComparison();
            const bool duplicateOk = duplicateCount == 0;
            const bool namespaceOk = namespaceWarningCount == 0;
            const bool hostedCoverageOk = hostedRegisteredMissingMetadata == 0 && metadataWithoutHostedRegistration == 0;
            const bool bareMetalCoverageOk = bareMetalRegisteredMissingMetadata == 0 && metadataWithoutBareMetalRegistration == 0;
            const bool targetDriftOk = hostedCoverageOk && bareMetalCoverageOk;
            const bool invalidManifestOk = s_lastManifestScanResult.invalidApps.empty();
            const bool launchTargetComparisonOk = launchTargetCounts.unexpectedDrift == 0;
            const bool launchStoragePreviewOk = storagePreviewCounts.unresolved == 0 && storagePreviewCounts.highRisk == 0;
            const size_t launchStoragePreviewUnexpectedDrift = storagePreviewCounts.highRisk + storagePreviewCompareBareMetalCounts.highRisk;
            const bool launchStoragePreviewCompareOk = launchStoragePreviewUnexpectedDrift == 0;
            const bool overallOk = duplicateOk && namespaceOk && hostedCoverageOk && bareMetalCoverageOk && invalidManifestOk && launchTargetComparisonOk && launchStoragePreviewOk && launchStoragePreviewCompareOk;

            std::ostringstream oss;
            oss << "[AppModelSummary]\n";
            oss << "registeredApps: " << s_apps.size() << "\n";
            oss << "manifestScannedApps: " << s_lastManifestScanResult.scannedManifestCount << "\n";
            oss << "manifestRegisteredApps: " << s_lastManifestScanResult.registeredAppCount << "\n";
            oss << "syntheticBuiltInCount: " << s_lastBuiltInRegisterResult.scannedManifestCount << "\n";
            oss << "duplicateAppIds: " << statusText(duplicateOk) << " count=" << duplicateCount << "\n";
            oss << "namespaceWarnings: " << statusText(namespaceOk) << " count=" << namespaceWarningCount << "\n";
            oss << "hostedMetadataCoverage: " << statusText(hostedCoverageOk)
                << " available=" << hostedAvailableMetadataCount()
                << " missingMetadata=" << hostedRegisteredMissingMetadata
                << " missingRegistration=" << metadataWithoutHostedRegistration << "\n";
            oss << "bareMetalMetadataCoverage: " << statusText(bareMetalCoverageOk)
                << " available=" << bareMetalAvailableMetadataCount()
                << " missingMetadata=" << bareMetalRegisteredMissingMetadata
                << " missingRegistration=" << metadataWithoutBareMetalRegistration << "\n";
            oss << "targetDrift: " << statusText(targetDriftOk)
                << " hostedMissing=" << metadataWithoutHostedRegistration
                << " bareMetalMissing=" << metadataWithoutBareMetalRegistration << "\n";
            oss << "invalidManifests: " << statusText(invalidManifestOk)
                << " count=" << s_lastManifestScanResult.invalidApps.size() << "\n";
            oss << "launchTargetComparison: " << statusText(launchTargetComparisonOk)
                << " labels=" << launchTargetCounts.labels
                << " exact=" << launchTargetCounts.exactMatches
                << " acceptedAliases=" << launchTargetCounts.acceptedAliasMatches
                << " intentionalDifferences=" << launchTargetCounts.intentionalDifferences
                << " unexpectedDrift=" << launchTargetCounts.unexpectedDrift << "\n";
            oss << "launchTargetShadow: observations=" << shadowCounters.totalObservations
                << " unresolved=" << shadowCounters.unresolvedObservations
                << " aliasFallback=" << shadowCounters.aliasFallbackObservations
                << " adapterMatches=" << shadowCounters.adapterMatches
                << " adapterAcceptedMismatches=" << shadowCounters.adapterAcceptedMismatches
                << " adapterUnexpectedMismatches=" << shadowCounters.adapterUnexpectedMismatches
                << " typedDispatchCandidateMatches=" << shadowCounters.typedDispatchCandidateMatches
                << " typedDispatchCandidateAcceptedMismatches=" << shadowCounters.typedDispatchCandidateAcceptedMismatches
                << " typedDispatchCandidateUnexpectedMismatches=" << shadowCounters.typedDispatchCandidateUnexpectedMismatches
                << " startMenu=" << shadowCounters.startMenuObservations
                << " startMenuTypedMatches=" << shadowCounters.startMenuTypedDispatchCandidateMatches
                << " desktopShortcut=" << shadowCounters.desktopShortcutObservations
                << " desktopShortcutTypedMatches=" << shadowCounters.desktopShortcutTypedDispatchCandidateMatches
                << " nonFatal=true\n";
            oss << "launchStoragePreview: " << statusText(launchStoragePreviewOk)
                << " total=" << storagePreviewCounts.total
                << " ready=" << storagePreviewCounts.ready
                << " alias=" << storagePreviewCounts.alias
                << " shellAction=" << storagePreviewCounts.shellAction
                << " unresolved=" << storagePreviewCounts.unresolved
                << " skippedLayoutOnly=" << storagePreviewCounts.skippedLayoutOnly
                << " targetSpecificUnsupportedAliases=" << storagePreviewCounts.targetSpecificUnsupportedAliases
                << " highRisk=" << storagePreviewCounts.highRisk
                << " writesStorage=false\n";
            oss << "launchStoragePreviewCompare: " << statusText(launchStoragePreviewCompareOk)
                << " hostedTargetSpecificUnsupportedAliases=" << storagePreviewCounts.targetSpecificUnsupportedAliases
                << " bareMetalTargetSpecificUnsupportedAliases=" << storagePreviewCompareBareMetalCounts.targetSpecificUnsupportedAliases
                << " unexpectedDrift=" << launchStoragePreviewUnexpectedDrift
                << " writesStorage=false\n";
            oss << launchTargetTypeCoverageSummaryLine();
            oss << "overall: " << statusText(overallOk) << "\n";
            oss << "detailCommands: desktop.appmodel.coverage, desktop.apps.verbose, desktop.launch.compare, desktop.launch.storage, desktop.launch.storage.preview, desktop.launch.storage.preview.compare, desktop.launch.types\n";
            return oss.str();
        }

        apps::LaunchTarget DesktopService::ResolveLaunchTarget(const std::string& label) {
            ensureDefaultAppsRegistered();

            apps::LaunchTarget target;
            target.originalLabel = label;

            if (label.empty()) {
                target.type = apps::LaunchTargetType::Unknown;
                target.diagnosticStatus = "unresolved";
                target.diagnosticReason = "No launch label supplied";
                return target;
            }

            // Diagnostic-only legacy alias: keep reporting the old UI label without
            // changing the current string dispatch path.
            if (label == "AppModel") {
                if (const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByAppId("gxos.builtin.appmodeldemo")) {
                    fillLaunchTargetFromMetadata(target, *metadata);
                }
                target.type = apps::LaunchTargetType::LegacyAlias;
                target.legacyAlias = "AppModel";
                if (target.displayName.empty()) target.displayName = "App Model Demo";
                if (target.dispatchLaunchName.empty()) target.dispatchLaunchName = "App Model Demo";
                target.diagnosticStatus = "resolved-alias";
                target.diagnosticReason = "Legacy hosted UI alias for App Model Demo; launch behavior is unchanged";
                return target;
            }

            // Diagnostic-only shell/system label. ComputerFiles is a desktop shell
            // affordance today, not a built-in app metadata identity.
            if (label == "ComputerFiles") {
                target.type = apps::LaunchTargetType::ShellAction;
                target.displayName = "Computer Files";
                target.dispatchLaunchName = canonicalAppName(label);
                target.shellAction = "ComputerFiles";
                target.hostedAvailable = true;
                target.bareMetalAvailable = false;
                target.diagnosticStatus = "resolved-shell";
                target.diagnosticReason = "Shell/system label currently canonicalizes to FileExplorer in hosted dispatch; not a built-in app identity";
                return target;
            }

            if (isPathLikeLaunchLabel(label)) {
                target.type = apps::LaunchTargetType::FileOpen;
                target.pathParameter = label;
                target.hostedAvailable = true;
                target.bareMetalAvailable = true;
                target.diagnosticStatus = "resolved-file-open";
                target.diagnosticReason = "Path-like launch label; current file-open handlers remain separate from app launch dispatch";
                return target;
            }

            if (const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByIdentity(label.c_str())) {
                fillLaunchTargetFromMetadata(target, *metadata);
                target.diagnosticStatus = "resolved";
                target.diagnosticReason = "Matched shared built-in app metadata by id, display name, launch name, kernel name, or legacy kernel alias";
                return target;
            }

            const RegisteredDesktopApp* desktopApp = findRegisteredApp(label);
            const std::string canonicalLabel = canonicalAppName(label);
            if (!desktopApp && canonicalLabel != label) desktopApp = findRegisteredApp(canonicalLabel);
            if (desktopApp) {
                fillLaunchTargetFromRegisteredApp(target, *desktopApp);
                target.diagnosticStatus = "resolved";
                target.diagnosticReason = "Matched hosted registered desktop app by id, display name, or dispatch launch name";
                return target;
            }

            const apps::RegisteredApp* registryApp = s_appRegistry.FindById(label);
            if (!registryApp) registryApp = s_appRegistry.FindByDisplayName(label);
            if (registryApp) {
                fillLaunchTargetFromRegistryApp(target, *registryApp);
                target.diagnosticStatus = "resolved";
                target.diagnosticReason = "Matched manifest app registry by id or display name";
                return target;
            }

            target.type = apps::LaunchTargetType::Unknown;
            target.diagnosticStatus = "unresolved";
            target.diagnosticReason = "No built-in metadata, registered app, manifest app, legacy alias, shell action, or path-like target matched";
            return target;
        }

        std::string DesktopService::ResolveLaunchTargetDiagnostic(const std::string& label) {
            apps::LaunchTarget target = ResolveLaunchTarget(label);

            std::ostringstream oss;
            oss << "[LaunchTarget]\n";
            oss << "originalLabel: " << target.originalLabel << "\n";
            oss << "resolvedType: " << apps::ToString(target.type) << "\n";
            oss << "appId: " << target.appId << "\n";
            oss << "displayName: " << target.displayName << "\n";
            oss << "dispatchLaunchName: " << target.dispatchLaunchName << "\n";
            oss << "legacyAlias: " << target.legacyAlias << "\n";
            oss << "shellAction: " << target.shellAction << "\n";
            oss << "pathParameter: " << target.pathParameter << "\n";
            oss << "hostedAvailable: " << diagnosticBool(target.hostedAvailable) << "\n";
            oss << "bareMetalAvailable: " << diagnosticBool(target.bareMetalAvailable) << "\n";
            oss << "status: " << target.diagnosticStatus << "\n";
            oss << "reason: " << target.diagnosticReason << "\n";
            return oss.str();
        }

        std::string DesktopService::LegacyDispatchStringForLaunchTarget(const apps::LaunchTarget& target, std::string& status, std::string& reason) {
            status = "unsupported";
            reason = "No legacy dispatch mapping for this launch target";

            switch (target.type) {
            case apps::LaunchTargetType::BuiltInApp:
            case apps::LaunchTargetType::LegacyAlias:
            case apps::LaunchTargetType::ShellAction:
            case apps::LaunchTargetType::ManifestApp:
            case apps::LaunchTargetType::NativeElfApp:
            case apps::LaunchTargetType::GXAppPackage:
            case apps::LaunchTargetType::Service:
            case apps::LaunchTargetType::HypervisorGuest:
            case apps::LaunchTargetType::Script:
                if (!target.dispatchLaunchName.empty()) {
                    status = "ok";
                    reason = "Adapter returns the resolver dispatchLaunchName used by the current hosted dispatch surface";
                    return target.dispatchLaunchName;
                }
                break;
            case apps::LaunchTargetType::FileOpen:
                if (!target.dispatchLaunchName.empty()) {
                    status = "ok";
                    reason = "File-open target already carries an explicit dispatchLaunchName";
                    return target.dispatchLaunchName;
                }
                status = "unsupported";
                reason = "Hosted file-open behavior currently uses OpenFilesystemEntry, not DesktopService::LaunchApp dispatch";
                return "";
            case apps::LaunchTargetType::CrossArchEmulatedApp:
                status = "unsupported";
                reason = "Cross-arch/emulated launch dispatch is not implemented";
                return "";
            case apps::LaunchTargetType::Unknown:
            default:
                status = "unsupported";
                reason = "Unknown launch target has no legacy dispatch string";
                return "";
            }

            return "";
        }

        TypedDispatchCandidateResult DesktopService::ComputeTypedDispatchCandidateForUiLaunch(const std::string& source, const std::string& uiLabel, const std::string& shortcutTarget, const std::string& actualDispatch) {
            TypedDispatchCandidateResult result;
            result.resolutionInput = resolveUiLaunchTargetInput(source, uiLabel, shortcutTarget, actualDispatch);
            result.target = ResolveLaunchTarget(result.resolutionInput);
            result.typedDispatchCandidate = LegacyDispatchStringForLaunchTarget(result.target, result.typedDispatchCandidateStatus, result.typedDispatchCandidateReason);
            result.typedDispatchCandidateMatchesActual = !result.typedDispatchCandidate.empty() && result.typedDispatchCandidate == actualDispatch;
            result.typedDispatchCandidateComparison = launchTargetShadowAdapterComparisonStatus(result.target, actualDispatch, result.typedDispatchCandidate);
            return result;
        }

        std::string DesktopService::LaunchTargetAdapterDiagnostic(const std::string& label) {
            apps::LaunchTarget target = ResolveLaunchTarget(label);
            std::string adapterStatus;
            std::string adapterReason;
            const std::string legacyDispatch = LegacyDispatchStringForLaunchTarget(target, adapterStatus, adapterReason);
            const bool matchesResolvedDispatch = !legacyDispatch.empty() && legacyDispatch == target.dispatchLaunchName;
            const bool matchesOriginalLabel = !legacyDispatch.empty() && legacyDispatch == label;

            std::ostringstream oss;
            oss << "[LaunchTargetAdapter]\n";
            oss << "originalLabel: " << target.originalLabel << "\n";
            oss << "resolvedType: " << apps::ToString(target.type) << "\n";
            oss << "appId: " << target.appId << "\n";
            oss << "resolvedDispatchName: " << target.dispatchLaunchName << "\n";
            oss << "adapterLegacyDispatchString: " << legacyDispatch << "\n";
            oss << "matchesResolvedDispatch: " << diagnosticBool(matchesResolvedDispatch) << "\n";
            oss << "matchesOriginalLabel: " << diagnosticBool(matchesOriginalLabel) << "\n";
            oss << "status: " << adapterStatus << "\n";
            oss << "reason: " << adapterReason << "\n";
            oss << "nonFatal: true\n";
            return oss.str();
        }

        std::string DesktopService::LaunchTargetComparisonDiagnostic() {
            ensureDefaultAppsRegistered();

            LaunchTargetComparisonCounts counts;

            std::ostringstream rows;
            const size_t labelCount = sizeof(kLaunchTargetComparisonLabels) / sizeof(kLaunchTargetComparisonLabels[0]);
            for (size_t i = 0; i < labelCount; ++i) {
                const std::string label = kLaunchTargetComparisonLabels[i];
                apps::LaunchTarget hosted = ResolveLaunchTarget(label);
                apps::LaunchTarget bareMetal = resolveBareMetalLaunchTargetForComparison(label);
                const std::string result = launchTargetComparisonStatus(label, hosted, bareMetal);

                recordLaunchTargetComparisonResult(counts, result);

                rows << "  label=" << label
                    << " result=" << result
                    << " hosted{type=" << apps::ToString(hosted.type)
                    << " status=" << hosted.diagnosticStatus
                    << " dispatch=" << hosted.dispatchLaunchName
                    << " appId=" << hosted.appId
                    << "} bareMetal{type=" << apps::ToString(bareMetal.type)
                    << " status=" << bareMetal.diagnosticStatus
                    << " dispatch=" << bareMetal.dispatchLaunchName
                    << " appId=" << bareMetal.appId
                    << "} note=" << launchTargetComparisonNote(label, result)
                    << "\n";
            }

            std::ostringstream oss;
            oss << "[LaunchTargetComparison]\n";
            oss << "labels: " << counts.labels << "\n";
            oss << "exactMatches: " << counts.exactMatches << "\n";
            oss << "acceptedAliasMatches: " << counts.acceptedAliasMatches << "\n";
            oss << "intentionalDifferences: " << counts.intentionalDifferences << "\n";
            oss << "unexpectedDrift: " << counts.unexpectedDrift << "\n";
            oss << "entries:\n";
            oss << rows.str();
            oss << "overall: " << (counts.unexpectedDrift == 0 ? "OK" : "WARN") << "\n";
            oss << "nonFatal: true\n";
            return oss.str();
        }

        std::string DesktopService::RecordLaunchTargetShadowObservation(const std::string& source, const apps::LaunchTarget& target, const std::string& actualDispatch, const std::string& adapterLegacyDispatch) {
            const bool unresolved = launchTargetShadowIsUnresolved(target);
            const bool aliasFallback = launchTargetShadowIsAliasFallback(target, actualDispatch);
            const std::string adapterComparisonStatus = launchTargetShadowAdapterComparisonStatus(target, actualDispatch, adapterLegacyDispatch);
            const std::string typedDispatchCandidateComparisonStatus = launchTargetShadowAdapterComparisonStatus(target, actualDispatch, adapterLegacyDispatch);

            std::lock_guard<std::mutex> lock(s_launchTargetShadowCountersMutex);
            ++s_launchTargetShadowCounters.totalObservations;
            if (unresolved) ++s_launchTargetShadowCounters.unresolvedObservations;
            if (aliasFallback) ++s_launchTargetShadowCounters.aliasFallbackObservations;
            countLaunchTargetShadowAdapterComparison(
                s_launchTargetShadowCounters.adapterMatches,
                s_launchTargetShadowCounters.adapterAcceptedMismatches,
                s_launchTargetShadowCounters.adapterUnexpectedMismatches,
                adapterComparisonStatus);
            countLaunchTargetShadowAdapterComparison(
                s_launchTargetShadowCounters.typedDispatchCandidateMatches,
                s_launchTargetShadowCounters.typedDispatchCandidateAcceptedMismatches,
                s_launchTargetShadowCounters.typedDispatchCandidateUnexpectedMismatches,
                typedDispatchCandidateComparisonStatus);

            if (source == "StartMenu") {
                ++s_launchTargetShadowCounters.startMenuObservations;
                if (unresolved) ++s_launchTargetShadowCounters.startMenuUnresolved;
                if (aliasFallback) ++s_launchTargetShadowCounters.startMenuAliasFallback;
                countLaunchTargetShadowAdapterComparison(
                    s_launchTargetShadowCounters.startMenuAdapterMatches,
                    s_launchTargetShadowCounters.startMenuAdapterAcceptedMismatches,
                    s_launchTargetShadowCounters.startMenuAdapterUnexpectedMismatches,
                    adapterComparisonStatus);
                countLaunchTargetShadowAdapterComparison(
                    s_launchTargetShadowCounters.startMenuTypedDispatchCandidateMatches,
                    s_launchTargetShadowCounters.startMenuTypedDispatchCandidateAcceptedMismatches,
                    s_launchTargetShadowCounters.startMenuTypedDispatchCandidateUnexpectedMismatches,
                    typedDispatchCandidateComparisonStatus);
            } else if (source == "DesktopShortcut") {
                ++s_launchTargetShadowCounters.desktopShortcutObservations;
                if (unresolved) ++s_launchTargetShadowCounters.desktopShortcutUnresolved;
                if (aliasFallback) ++s_launchTargetShadowCounters.desktopShortcutAliasFallback;
                countLaunchTargetShadowAdapterComparison(
                    s_launchTargetShadowCounters.desktopShortcutAdapterMatches,
                    s_launchTargetShadowCounters.desktopShortcutAdapterAcceptedMismatches,
                    s_launchTargetShadowCounters.desktopShortcutAdapterUnexpectedMismatches,
                    adapterComparisonStatus);
                countLaunchTargetShadowAdapterComparison(
                    s_launchTargetShadowCounters.desktopShortcutTypedDispatchCandidateMatches,
                    s_launchTargetShadowCounters.desktopShortcutTypedDispatchCandidateAcceptedMismatches,
                    s_launchTargetShadowCounters.desktopShortcutTypedDispatchCandidateUnexpectedMismatches,
                    typedDispatchCandidateComparisonStatus);
            } else {
                ++s_launchTargetShadowCounters.otherObservations;
                if (unresolved) ++s_launchTargetShadowCounters.otherUnresolved;
                if (aliasFallback) ++s_launchTargetShadowCounters.otherAliasFallback;
                countLaunchTargetShadowAdapterComparison(
                    s_launchTargetShadowCounters.otherAdapterMatches,
                    s_launchTargetShadowCounters.otherAdapterAcceptedMismatches,
                    s_launchTargetShadowCounters.otherAdapterUnexpectedMismatches,
                    adapterComparisonStatus);
                countLaunchTargetShadowAdapterComparison(
                    s_launchTargetShadowCounters.otherTypedDispatchCandidateMatches,
                    s_launchTargetShadowCounters.otherTypedDispatchCandidateAcceptedMismatches,
                    s_launchTargetShadowCounters.otherTypedDispatchCandidateUnexpectedMismatches,
                    typedDispatchCandidateComparisonStatus);
            }

            return adapterComparisonStatus;
        }

        LaunchTargetShadowCounters DesktopService::GetLaunchTargetShadowCounters() {
            std::lock_guard<std::mutex> lock(s_launchTargetShadowCountersMutex);
            return s_launchTargetShadowCounters;
        }

        std::string DesktopService::LaunchTargetShadowDiagnostic() {
            const LaunchTargetShadowCounters counters = GetLaunchTargetShadowCounters();

            std::ostringstream oss;
            oss << "[LaunchTargetShadow]\n";
            oss << "observations: " << counters.totalObservations << "\n";
            oss << "unresolved: " << counters.unresolvedObservations << "\n";
            oss << "aliasFallback: " << counters.aliasFallbackObservations << "\n";
            oss << "adapterMatches: " << counters.adapterMatches << "\n";
            oss << "adapterAcceptedMismatches: " << counters.adapterAcceptedMismatches << "\n";
            oss << "adapterUnexpectedMismatches: " << counters.adapterUnexpectedMismatches << "\n";
            oss << "typedDispatchCandidateMatches: " << counters.typedDispatchCandidateMatches << "\n";
            oss << "typedDispatchCandidateAcceptedMismatches: " << counters.typedDispatchCandidateAcceptedMismatches << "\n";
            oss << "typedDispatchCandidateUnexpectedMismatches: " << counters.typedDispatchCandidateUnexpectedMismatches << "\n";
            oss << "bySource:\n";
            appendLaunchTargetShadowSourceLine(oss, "StartMenu", counters.startMenuObservations, counters.startMenuUnresolved, counters.startMenuAliasFallback, counters.startMenuAdapterMatches, counters.startMenuAdapterAcceptedMismatches, counters.startMenuAdapterUnexpectedMismatches);
            appendLaunchTargetShadowSourceLine(oss, "DesktopShortcut", counters.desktopShortcutObservations, counters.desktopShortcutUnresolved, counters.desktopShortcutAliasFallback, counters.desktopShortcutAdapterMatches, counters.desktopShortcutAdapterAcceptedMismatches, counters.desktopShortcutAdapterUnexpectedMismatches);
            appendLaunchTargetShadowSourceLine(oss, "Other", counters.otherObservations, counters.otherUnresolved, counters.otherAliasFallback, counters.otherAdapterMatches, counters.otherAdapterAcceptedMismatches, counters.otherAdapterUnexpectedMismatches);
            appendLaunchTargetShadowCandidateSourceLine(oss, "StartMenu", counters.startMenuTypedDispatchCandidateMatches, counters.startMenuTypedDispatchCandidateAcceptedMismatches, counters.startMenuTypedDispatchCandidateUnexpectedMismatches);
            appendLaunchTargetShadowCandidateSourceLine(oss, "DesktopShortcut", counters.desktopShortcutTypedDispatchCandidateMatches, counters.desktopShortcutTypedDispatchCandidateAcceptedMismatches, counters.desktopShortcutTypedDispatchCandidateUnexpectedMismatches);
            appendLaunchTargetShadowCandidateSourceLine(oss, "Other", counters.otherTypedDispatchCandidateMatches, counters.otherTypedDispatchCandidateAcceptedMismatches, counters.otherTypedDispatchCandidateUnexpectedMismatches);
            oss << "reset: not available\n";
            oss << "nonFatal: true\n";
            return oss.str();
        }

        std::string DesktopService::LaunchStorageDiagnostic() {
            ensureDefaultAppsRegistered();

            DesktopConfigData cfg;
            std::string cfgErr;
            const bool cfgLoaded = DesktopConfig::Load("desktop.json", cfg, cfgErr);

            std::vector<std::string> desktopShortcutAppTargets;
            std::vector<std::string> desktopShortcutFileTargets;
            if (cfgLoaded) {
                for (const DesktopShortcutRec& shortcut : cfg.desktopShortcuts) {
                    const std::string type = shortcut.shortcutType.empty() ? (shortcut.targetPath.empty() ? "App" : "File") : shortcut.shortcutType;
                    if (type == "App") desktopShortcutAppTargets.push_back(shortcut.targetAppId);
                    else if (type == "File" || type == "Folder") desktopShortcutFileTargets.push_back(shortcut.targetPath);
                }
            }

            std::vector<std::string> inMemoryPinned;
            for (const PinnedItem& item : s_pinned) inMemoryPinned.push_back(item.name);

            std::vector<std::string> inMemoryRecentPrograms;
            for (const RecentProgramEntry& entry : s_recentPrograms) inMemoryRecentPrograms.push_back(entry.name);

            std::vector<std::string> inMemoryRecentDocuments;
            for (const RecentDocumentEntry& entry : s_recentDocuments) inMemoryRecentDocuments.push_back(entry.path);

            std::ostringstream oss;
            oss << "[LaunchStringStorage]\n";
            oss << "nonFatal: true\n";
            oss << "migrationState: not-started\n";
            oss << "hostedConfig: path=desktop.json loaded=" << diagnosticBool(cfgLoaded);
            if (!cfgLoaded) oss << " error=" << cfgErr;
            oss << "\n";
            oss << "hostedCounts: pinned=" << (cfgLoaded ? cfg.pinned.size() : 0)
                << " recent=" << (cfgLoaded ? cfg.recent.size() : 0)
                << " desktopShortcuts=" << (cfgLoaded ? cfg.desktopShortcuts.size() : 0)
                << " iconPositions=" << (cfgLoaded ? cfg.iconPositions.size() : 0)
                << " servicePinned=" << s_pinned.size()
                << " serviceRecentPrograms=" << s_recentPrograms.size()
                << " serviceRecentDocuments=" << s_recentDocuments.size()
                << " registeredApps=" << s_apps.size() << "\n";

            oss << "hostedSites:\n";
            appendLaunchStorageSite(oss,
                "desktop.json:pinned",
                "desktop.json",
                "pinned[]",
                "mixed displayName|legacyAlias|shellAction",
                cfgLoaded ? countLaunchStorageLabels(cfg.pinned) : LaunchStorageResolutionCounts{},
                "medium",
                "feeds Start Menu pinned/recent and persists user-visible app labels");
            appendLaunchStorageSite(oss,
                "desktop.json:recent",
                "desktop.json",
                "recent[]",
                "mixed displayName|legacyAlias|shellAction",
                cfgLoaded ? countLaunchStorageLabels(cfg.recent) : LaunchStorageResolutionCounts{},
                "medium",
                "updated by compositor launchAction using the original dispatch string");
            appendLaunchStorageSite(oss,
                "desktop.json:desktopShortcuts.App",
                "desktop.json",
                "desktopShortcuts[].shortcutType,targetAppId,label",
                "targetAppId plus display label",
                countLaunchStorageLabels(desktopShortcutAppTargets),
                "low",
                "already stores stable app id when created from Start Menu");
            appendLaunchStorageSite(oss,
                "desktop.json:desktopShortcuts.FileFolder",
                "desktop.json",
                "desktopShortcuts[].shortcutType,targetPath,label",
                "file/folder path plus display label",
                countLaunchStorageLabels(desktopShortcutFileTargets),
                "low",
                "maps naturally to FileOpen typed targets");
            appendLaunchStorageStaticSite(oss,
                "desktop.json:iconPositions",
                "desktop.json",
                "iconPositions[].name,x,y",
                "layout key, not launch source",
                cfgLoaded ? std::to_string(cfg.iconPositions.size()) : "0",
                "not-applicable",
                "low",
                "keys may contain app ids or paths but only restore icon positions");
            appendLaunchStorageSite(oss,
                "DesktopService:s_pinned",
                "memory mirror of desktop.json:pinned",
                "PinnedItem.name,path,kind",
                "name currently serialized without kind",
                countLaunchStorageLabels(inMemoryPinned),
                "medium",
                "kind/path are in memory but SaveState currently writes only names");
            appendLaunchStorageSite(oss,
                "DesktopService:s_recentPrograms",
                "memory mirror of desktop.json:recent",
                "RecentProgramEntry.name",
                "displayName|legacyAlias|shellAction",
                countLaunchStorageLabels(inMemoryRecentPrograms),
                "medium",
                "program recents persist as strings");
            appendLaunchStorageSite(oss,
                "DesktopService:s_recentDocuments",
                "memory only",
                "RecentDocumentEntry.path",
                "file path",
                countLaunchStorageLabels(inMemoryRecentDocuments),
                "low",
                "not currently persisted by DesktopService::SaveState");
            appendLaunchStorageStaticSite(oss,
                "Compositor:g_startMenuAllProgsSorted",
                "memory derived from DesktopService::GetRegisteredApps",
                "displayName",
                "registered app display names",
                std::to_string(s_apps.size()),
                "yes",
                "low",
                "generated each refresh from registry; no persistence");
            appendLaunchStorageStaticSite(oss,
                "Compositor:g_startMenuPinnedRecent",
                "memory derived from desktop.json pinned/recent",
                "string label",
                "mixed displayName|legacyAlias|shellAction",
                cfgLoaded ? std::to_string(cfg.pinned.size() + cfg.recent.size()) : "0",
                "mostly",
                "medium",
                "deduplicated visible list, still string-based");
            appendLaunchStorageStaticSite(oss,
                "Compositor:rightColumnAndSystemObjects",
                "compositor.cpp",
                "ComputerFiles,Console,Trash,ControlPanel,TaskManager",
                "shell action or legacy launch name",
                "5",
                "yes",
                "medium",
                "shell/system labels need typed ShellAction migration");
            appendLaunchStorageStaticSite(oss,
                "Compositor:taskbarButtons",
                "memory derived from open windows",
                "WinInfo.title,taskbarIcon",
                "active window title, not persisted launch source",
                "dynamic",
                "not-applicable",
                "low",
                "no separate hosted taskbar pin storage found in this pass");

            oss << "bareMetalSites:\n";
            appendLaunchStorageStaticSite(oss,
                "desktop.cpp:s_startMenuApps[].name",
                "kernel/core/desktop.cpp",
                "name,pinned,recent",
                "kernel launch name or legacy alias",
                "14",
                "mostly",
                "medium",
                "static Start Menu pinned/recent list includes AppModel and Files aliases");
            appendLaunchStorageStaticSite(oss,
                "desktop.cpp:s_allProgramsList[]",
                "kernel/core/desktop.cpp",
                "string",
                "kernel launch name or legacy alias",
                "14",
                "mostly",
                "medium",
                "static All Programs list, not manifest-driven");
            appendLaunchStorageStaticSite(oss,
                "VFS:/desktop.shortcuts",
                "/desktop.shortcuts",
                "shortcutType<TAB>target<TAB>label",
                "App launch name or File/Folder path plus label",
                "up-to-16",
                "yes",
                "medium",
                "bare-metal persisted shortcut format v2");
            appendLaunchStorageStaticSite(oss,
                "desktop.cpp:s_desktopIcons[]",
                "kernel/core/desktop.cpp",
                "label,path,pinned,recent,kind,systemObject",
                "system labels, app launch names, file paths",
                "static+dynamic",
                "mostly",
                "medium",
                "runtime desktop source for icon launch and recent flags");
            appendLaunchStorageStaticSite(oss,
                "VFS:/.desktop_icons",
                "/.desktop_icons",
                "layoutKey<TAB>x<TAB>y",
                "layout key, not launch source",
                "dynamic",
                "not-applicable",
                "low",
                "position-only storage can keep old string keys through migration");
            appendLaunchStorageStaticSite(oss,
                "VFS:/desktop.system.icons",
                "/desktop.system.icons",
                "Trash,ThisSystem,FileManager,SystemSettings",
                "system object visibility flags",
                "4",
                "not-applicable",
                "low",
                "shell/system affordance visibility, not app identity");
            appendLaunchStorageStaticSite(oss,
                "desktop.cpp:s_taskbarEntries[]",
                "kernel/core/desktop.cpp",
                "title,color,active",
                "taskbar entry label, currently disabled/static",
                "0",
                "not-applicable",
                "low",
                "no separate bare-metal taskbar pin storage found in this pass");

            return oss.str();
        }

        std::string DesktopService::LaunchStoragePreviewDiagnostic() {
            ensureDefaultAppsRegistered();

            DesktopConfigData cfg;
            std::string cfgErr;
            const bool cfgLoaded = DesktopConfig::Load("desktop.json", cfg, cfgErr);
            const size_t maxRows = 96;

            std::vector<std::string> inMemoryPinned;
            for (const PinnedItem& item : s_pinned) inMemoryPinned.push_back(item.name);

            std::vector<std::string> inMemoryRecentPrograms;
            for (const RecentProgramEntry& entry : s_recentPrograms) inMemoryRecentPrograms.push_back(entry.name);

            std::vector<std::string> inMemoryRecentDocuments;
            for (const RecentDocumentEntry& entry : s_recentDocuments) inMemoryRecentDocuments.push_back(entry.path);

            std::ostringstream rows;
            const LaunchStoragePreviewCounts counts = collectLaunchStoragePreviewCounts(
                cfgLoaded,
                cfg,
                inMemoryPinned,
                inMemoryRecentPrograms,
                inMemoryRecentDocuments,
                s_apps,
                &rows,
                maxRows);

            std::ostringstream oss;
            oss << "[LaunchStringStoragePreview]\n";
            oss << "nonFatal: true\n";
            oss << "migrationState: preview-only\n";
            oss << "writesStorage: false\n";
            oss << "hostedConfig: path=desktop.json loaded=" << diagnosticBool(cfgLoaded);
            if (!cfgLoaded) oss << " error=" << cfgErr;
            oss << "\n";
            oss << "summary: totalRecords=" << counts.total
                << " ready=" << counts.ready
                << " alias=" << counts.alias
                << " shellAction=" << counts.shellAction
                << " unresolved=" << counts.unresolved
                << " skippedLayoutOnly=" << counts.skippedLayoutOnly
                << " targetSpecificUnsupportedAliases=" << counts.targetSpecificUnsupportedAliases
                << " highRisk=" << counts.highRisk
                << " printed=" << counts.printed
                << " truncated=" << counts.truncated << "\n";
            oss << "rowCap: " << maxRows << "\n";
            oss << "records:\n";
            oss << rows.str();
            return oss.str();
        }

        std::string DesktopService::LaunchStoragePreviewComparisonDiagnostic() {
            ensureDefaultAppsRegistered();

            DesktopConfigData cfg;
            std::string cfgErr;
            const bool cfgLoaded = DesktopConfig::Load("desktop.json", cfg, cfgErr);

            std::vector<std::string> inMemoryPinned;
            for (const PinnedItem& item : s_pinned) inMemoryPinned.push_back(item.name);

            std::vector<std::string> inMemoryRecentPrograms;
            for (const RecentProgramEntry& entry : s_recentPrograms) inMemoryRecentPrograms.push_back(entry.name);

            std::vector<std::string> inMemoryRecentDocuments;
            for (const RecentDocumentEntry& entry : s_recentDocuments) inMemoryRecentDocuments.push_back(entry.path);

            const LaunchStoragePreviewCounts hostedCounts = collectLaunchStoragePreviewCounts(
                cfgLoaded,
                cfg,
                inMemoryPinned,
                inMemoryRecentPrograms,
                inMemoryRecentDocuments,
                s_apps,
                nullptr,
                0);
            const LaunchStoragePreviewCounts bareMetalCounts = bareMetalStoragePreviewCountsForComparison();

            const size_t unexpectedDrift = hostedCounts.highRisk + bareMetalCounts.highRisk;
            const bool overallOk = unexpectedDrift == 0;

            std::ostringstream oss;
            oss << "[LaunchStringStoragePreviewComparison]\n";
            oss << "nonFatal: true\n";
            oss << "migrationState: preview-only\n";
            oss << "writesStorageHosted=false\n";
            oss << "writesStorageBareMetal=false\n";
            oss << "hostedConfig: path=desktop.json loaded=" << diagnosticBool(cfgLoaded);
            if (!cfgLoaded) oss << " error=" << cfgErr;
            oss << "\n";
            appendLaunchStoragePreviewCountsLine(oss, "hosted", hostedCounts, "source=live-hosted");
            appendLaunchStoragePreviewCountsLine(oss, "bareMetal", bareMetalCounts, "source=hosted-static-mirror dynamicVfs=not-inspected-here");
            appendLaunchStoragePreviewUnsupportedAliasDetails(oss, hostedCounts, bareMetalCounts);
            oss << "intentionalDifferences: 6\n";
            oss << "  difference=hosted-desktop-json note=hosted owns live desktop.json pinned/recent/desktopShortcuts/iconPositions storage\n";
            oss << "  difference=bare-metal-vfs note=bare-metal owns VFS /desktop.shortcuts, /.desktop_icons, and /desktop.system.icons storage\n";
            oss << "  difference=start-menu-source note=hosted all-programs are registry-derived while bare-metal Start Menu arrays are static today\n";
            oss << "  difference=shell-labels note=hosted ComputerFiles is a shell label while bare-metal uses Computer/Documents/Pictures/Music/Network/Settings labels\n";
            oss << "  difference=dynamic-runtime-sites note=desktop icon/taskbar runtime labels are target-specific and are not migrated in this diagnostic\n";
            oss << "  difference=bare-metal-imgviewer note=ImgViewer is a diagnostic-only legacy/static label for hosted ImageViewer and remains unsupported on bare-metal\n";
            oss << "unexpectedDrift: " << unexpectedDrift << "\n";
            if (bareMetalCounts.highRisk > 0) {
                oss << "  drift=bareMetalHighRisk count=" << bareMetalCounts.highRisk
                    << " note=bare-metal static preview mirror has unresolved/high-risk labels; inspect bare-metal desktop.launch.storage.preview for row detail\n";
            }
            if (hostedCounts.highRisk > 0) {
                oss << "  drift=hostedHighRisk count=" << hostedCounts.highRisk
                    << " note=hosted preview has unresolved/high-risk labels; inspect desktop.launch.storage.preview for row detail\n";
            }
            oss << "overall: " << statusText(overallOk) << "\n";
            oss << "detailCommands: desktop.launch.storage.preview, desktop.launch.storage\n";
            return oss.str();
        }

        static std::set<std::string> collectLaunchTargetTypeCoverageLabels() {
            std::set<std::string> labels;

            // Registered apps (display names, launch names, app IDs)
            for (const auto& app : DesktopService::GetRegisteredApps()) {
                if (!app.displayName.empty()) labels.insert(app.displayName);
                if (!app.launchName.empty() && app.launchName != app.displayName) labels.insert(app.launchName);
                if (!app.id.empty()) labels.insert(app.id);
            }

            // Built-in metadata identities
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                const apps::BuiltInAppMetadata& metadata = apps::kBuiltInAppMetadata[i];
                if (metadata.appId && metadata.appId[0]) labels.insert(metadata.appId);
                if (metadata.displayName && metadata.displayName[0]) labels.insert(metadata.displayName);
                if (metadata.launchName && metadata.launchName[0]) labels.insert(metadata.launchName);
                if (metadata.kernelAppName && metadata.kernelAppName[0]) labels.insert(metadata.kernelAppName);
                if (metadata.kernelLegacyAlias && metadata.kernelLegacyAlias[0]) labels.insert(metadata.kernelLegacyAlias);
            }

            // UI launch labels (compositor, desktop, start menu)
            std::vector<UiLaunchLabelDiagnostic> uiLabels = currentCompositorUiLaunchLabelsForDiagnostic();
            for (const auto& entry : uiLabels) {
                if (!entry.label.empty()) labels.insert(entry.label);
                if (!entry.fallbackIdentity.empty()) labels.insert(entry.fallbackIdentity);
            }

            // Known bare-metal kernel app names
            for (const char* name : currentBareMetalKernelRegistrationNames()) {
                labels.insert(name);
            }

            // Bare-metal shell/system labels
            const char* const bareMetalShellLabels[] = {
                "Console", "Terminal", "Computer", "This System", "Documents",
                "Pictures", "Music", "Network", "Control Panel", "Settings", "System Settings"
            };
            for (const char* label : bareMetalShellLabels) {
                labels.insert(label);
            }

            // Known legacy aliases and special cases
            labels.insert("AppModel");
            labels.insert("Files");
            labels.insert("ImgViewer");
            labels.insert("ComputerFiles");

            // Test unknown case
            labels.insert("TotallyUnknownLaunchThing");

            return labels;
        }

        struct LaunchTargetTypeCounts {
            int hostedAvailable = 0;
            int bareMetalAvailable = 0;
            int hostedOnly = 0;
            int bareMetalOnly = 0;
            int expectedUnsupportedOnTarget = 0;
            int unexpectedUnsupportedOnTarget = 0;
            int unknownLabels = 0;
            int totalLabels = 0;
        };

        struct LaunchTargetTypeCoverageSummary {
            size_t totalLabels = 0;
            int coveredTypes = 0;
            int totalHostedAvailable = 0;
            int totalBareMetalAvailable = 0;
            int totalHostedOnly = 0;
            int totalBareMetalOnly = 0;
            int totalExpectedUnsupported = 0;
            int totalUnexpectedUnsupported = 0;
            int totalUnknownLabels = 0;
        };

        static std::map<apps::LaunchTargetType, LaunchTargetTypeCounts> collectLaunchTargetTypeCoverageCounts(const std::set<std::string>& labels);

        static bool isExpectedUnsupportedLaunchTargetLabel(const std::string& label) {
            return label == "ImgViewer";
        }

        static apps::LaunchTargetType launchTargetTypeForCoverageStatus(const apps::LaunchTarget& hostedTarget, const apps::LaunchTarget& bareMetalTarget) {
            if (hostedTarget.type != apps::LaunchTargetType::Unknown) return hostedTarget.type;
            if (bareMetalTarget.type != apps::LaunchTargetType::Unknown) return bareMetalTarget.type;
            return apps::LaunchTargetType::Unknown;
        }

        static LaunchTargetTypeCoverageSummary summarizeLaunchTargetTypeCoverage(const std::map<apps::LaunchTargetType, LaunchTargetTypeCounts>& typeCounts, size_t totalLabels) {
            LaunchTargetTypeCoverageSummary summary;
            summary.totalLabels = totalLabels;

            for (const auto& entry : typeCounts) {
                const LaunchTargetTypeCounts& counts = entry.second;
                if (counts.totalLabels > 0 || counts.hostedAvailable > 0 || counts.bareMetalAvailable > 0 ||
                    counts.expectedUnsupportedOnTarget > 0 || counts.unexpectedUnsupportedOnTarget > 0 || counts.unknownLabels > 0) {
                    ++summary.coveredTypes;
                }
                summary.totalHostedAvailable += counts.hostedAvailable;
                summary.totalBareMetalAvailable += counts.bareMetalAvailable;
                summary.totalHostedOnly += counts.hostedOnly;
                summary.totalBareMetalOnly += counts.bareMetalOnly;
                summary.totalExpectedUnsupported += counts.expectedUnsupportedOnTarget;
                summary.totalUnexpectedUnsupported += counts.unexpectedUnsupportedOnTarget;
                summary.totalUnknownLabels += counts.unknownLabels;
            }

            return summary;
        }

        static std::string launchTargetTypeCoverageSummaryLine() {
            const std::set<std::string> labels = collectLaunchTargetTypeCoverageLabels();
            const std::map<apps::LaunchTargetType, LaunchTargetTypeCounts> typeCounts = collectLaunchTargetTypeCoverageCounts(labels);
            const LaunchTargetTypeCoverageSummary summary = summarizeLaunchTargetTypeCoverage(typeCounts, labels.size());

            std::ostringstream oss;
            oss << "launchTargetTypes: " << statusText(summary.totalUnexpectedUnsupported == 0)
                << " labels=" << summary.totalLabels
                << " coveredTypes=" << summary.coveredTypes
                << " hostedAvailable=" << summary.totalHostedAvailable
                << " bareMetalAvailable=" << summary.totalBareMetalAvailable
                << " hostedOnly=" << summary.totalHostedOnly
                << " bareMetalOnly=" << summary.totalBareMetalOnly
                << " expectedUnsupportedOnTarget=" << summary.totalExpectedUnsupported
                << " unexpectedUnsupportedOnTarget=" << summary.totalUnexpectedUnsupported
                << " unknownLabels=" << summary.totalUnknownLabels
                << " nonFatal=true\n";
            return oss.str();
        }

        static std::map<apps::LaunchTargetType, LaunchTargetTypeCounts> collectLaunchTargetTypeCoverageCounts(const std::set<std::string>& labels) {
            std::map<apps::LaunchTargetType, LaunchTargetTypeCounts> typeCounts;

            const apps::LaunchTargetType allTypes[] = {
                apps::LaunchTargetType::BuiltInApp,
                apps::LaunchTargetType::ManifestApp,
                apps::LaunchTargetType::NativeElfApp,
                apps::LaunchTargetType::GXAppPackage,
                apps::LaunchTargetType::ShellAction,
                apps::LaunchTargetType::LegacyAlias,
                apps::LaunchTargetType::FileOpen,
                apps::LaunchTargetType::CrossArchEmulatedApp,
                apps::LaunchTargetType::Service,
                apps::LaunchTargetType::HypervisorGuest,
                apps::LaunchTargetType::Script,
                apps::LaunchTargetType::Unknown
            };

            for (apps::LaunchTargetType type : allTypes) {
                typeCounts[type] = LaunchTargetTypeCounts{};
            }

            for (const std::string& label : labels) {
                apps::LaunchTarget hostedTarget = DesktopService::ResolveLaunchTarget(label);
                apps::LaunchTarget bareMetalTarget = resolveBareMetalLaunchTargetForComparison(label);

                if (hostedTarget.hostedAvailable) {
                    typeCounts[hostedTarget.type].hostedAvailable++;
                    typeCounts[hostedTarget.type].totalLabels++;
                }

                if (bareMetalTarget.bareMetalAvailable && bareMetalTarget.type != hostedTarget.type) {
                    typeCounts[bareMetalTarget.type].bareMetalAvailable++;
                    if (!hostedTarget.hostedAvailable) {
                        typeCounts[bareMetalTarget.type].totalLabels++;
                    }
                } else if (bareMetalTarget.bareMetalAvailable) {
                    typeCounts[hostedTarget.type].bareMetalAvailable++;
                }

                const apps::LaunchTargetType primaryType = hostedTarget.type;
                if (hostedTarget.hostedAvailable && bareMetalTarget.bareMetalAvailable) {
                } else if (hostedTarget.hostedAvailable && !bareMetalTarget.bareMetalAvailable) {
                    typeCounts[primaryType].hostedOnly++;
                } else if (!hostedTarget.hostedAvailable && bareMetalTarget.bareMetalAvailable) {
                    typeCounts[bareMetalTarget.type].bareMetalOnly++;
                } else {
                    const apps::LaunchTargetType statusType = launchTargetTypeForCoverageStatus(hostedTarget, bareMetalTarget);
                    if (statusType == apps::LaunchTargetType::Unknown) {
                        typeCounts[statusType].unknownLabels++;
                    } else if (isExpectedUnsupportedLaunchTargetLabel(label)) {
                        typeCounts[statusType].expectedUnsupportedOnTarget++;
                    } else {
                        typeCounts[statusType].unexpectedUnsupportedOnTarget++;
                    }
                }
            }

            return typeCounts;
        }

        std::string DesktopService::LaunchTargetTypeCoverageDiagnostic() {
            ensureDefaultAppsRegistered();

            std::ostringstream oss;
            oss << "[LaunchTargetTypeCoverage]\n";
            oss << "nonFatal: true\n";
            oss << "description: Launch target resolver coverage by LaunchTargetType\n";

            std::set<std::string> labels = collectLaunchTargetTypeCoverageLabels();
            const std::map<apps::LaunchTargetType, LaunchTargetTypeCounts> typeCounts = collectLaunchTargetTypeCoverageCounts(labels);
            const LaunchTargetTypeCoverageSummary summary = summarizeLaunchTargetTypeCoverage(typeCounts, labels.size());
            oss << "totalLabels: " << labels.size() << "\n";

            const apps::LaunchTargetType allTypes[] = {
                apps::LaunchTargetType::BuiltInApp,
                apps::LaunchTargetType::ManifestApp,
                apps::LaunchTargetType::NativeElfApp,
                apps::LaunchTargetType::GXAppPackage,
                apps::LaunchTargetType::ShellAction,
                apps::LaunchTargetType::LegacyAlias,
                apps::LaunchTargetType::FileOpen,
                apps::LaunchTargetType::CrossArchEmulatedApp,
                apps::LaunchTargetType::Service,
                apps::LaunchTargetType::HypervisorGuest,
                apps::LaunchTargetType::Script,
                apps::LaunchTargetType::Unknown
            };

            oss << "\nlaunchTargetTypeCoverage:\n";
            for (apps::LaunchTargetType type : allTypes) {
                const LaunchTargetTypeCounts& counts = typeCounts.at(type);
                if (counts.totalLabels == 0 && counts.hostedAvailable == 0 && counts.bareMetalAvailable == 0 &&
                    counts.expectedUnsupportedOnTarget == 0 && counts.unexpectedUnsupportedOnTarget == 0 && counts.unknownLabels == 0) continue;

                oss << "  type=" << apps::ToString(type)
                    << " hostedAvailable=" << counts.hostedAvailable
                    << " bareMetalAvailable=" << counts.bareMetalAvailable
                    << " hostedOnly=" << counts.hostedOnly
                    << " bareMetalOnly=" << counts.bareMetalOnly
                    << " expectedUnsupportedOnTarget=" << counts.expectedUnsupportedOnTarget
                    << " unexpectedUnsupportedOnTarget=" << counts.unexpectedUnsupportedOnTarget
                    << " unknownLabels=" << counts.unknownLabels
                    << " totalLabels=" << counts.totalLabels << "\n";
            }

            oss << "\nsummary:"
                << " coveredTypes=" << summary.coveredTypes
                << " totalHostedAvailable=" << summary.totalHostedAvailable
                << " totalBareMetalAvailable=" << summary.totalBareMetalAvailable
                << " hostedOnly=" << summary.totalHostedOnly
                << " bareMetalOnly=" << summary.totalBareMetalOnly
                << " expectedUnsupportedOnTarget=" << summary.totalExpectedUnsupported
                << " unexpectedUnsupportedOnTarget=" << summary.totalUnexpectedUnsupported
                << " unknownLabels=" << summary.totalUnknownLabels << "\n";

            oss << "status: " << (summary.totalUnexpectedUnsupported > 0 ? "WARN" : "OK")
                << " note: Expected target-specific unsupported labels and unknown probe labels are non-fatal and informational\n";

            return oss.str();
        }

        std::string DesktopService::BuiltInAppMetadataCoverageDiagnostic() {
            ensureDefaultAppsRegistered();

            int hostedAvailable = 0;
            int bareMetalAvailable = 0;
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                if (apps::IsBuiltInAppAvailableInHosted(apps::kBuiltInAppMetadata[i])) ++hostedAvailable;
                if (apps::IsBuiltInAppAvailableInBareMetal(apps::kBuiltInAppMetadata[i])) ++bareMetalAvailable;
            }

            std::ostringstream oss;
            oss << "[BuiltInMetadataCoverage]\n";
            oss << "metadataEntries: " << apps::kBuiltInAppMetadataCount << "\n";
            oss << "hostedAvailableEntries: " << hostedAvailable << "\n";
            oss << "bareMetalAvailableEntries: " << bareMetalAvailable << "\n";

            oss << "metadata:\n";
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                const apps::BuiltInAppMetadata& metadata = apps::kBuiltInAppMetadata[i];
                oss << "  id=" << (metadata.appId ? metadata.appId : "")
                    << " displayName=" << (metadata.displayName ? metadata.displayName : "")
                    << " launchName=" << (metadata.launchName ? metadata.launchName : "")
                    << " kernelAppName=" << (metadata.kernelAppName ? metadata.kernelAppName : "")
                    << " kernelAlias=" << (metadata.kernelLegacyAlias ? metadata.kernelLegacyAlias : "")
                    << " hosted=" << (apps::IsBuiltInAppAvailableInHosted(metadata) ? "true" : "false")
                    << " bareMetal=" << (apps::IsBuiltInAppAvailableInBareMetal(metadata) ? "true" : "false")
                    << "\n";
            }

            int hostedRegisteredMissingMetadata = 0;
            oss << "hostedRegisteredBuiltInsMissingMetadata:\n";
            for (const auto& app : s_apps) {
                if (app.kind != apps::AppKind::BuiltIn) continue;
                if (findMetadataForRegisteredDesktopApp(app)) continue;
                ++hostedRegisteredMissingMetadata;
                oss << "  id=" << app.id
                    << " displayName=" << app.displayName
                    << " launchName=" << app.launchName
                    << " source=" << app.source << "\n";
            }
            if (hostedRegisteredMissingMetadata == 0) oss << "  none\n";

            int bareMetalRegisteredMissingMetadata = 0;
            oss << "bareMetalRegisteredKernelAppsMissingMetadata:\n";
            for (const char* name : currentBareMetalKernelRegistrationNames()) {
                if (apps::FindBuiltInAppMetadataByIdentity(name)) continue;
                ++bareMetalRegisteredMissingMetadata;
                oss << "  name=" << name << "\n";
            }
            if (bareMetalRegisteredMissingMetadata == 0) oss << "  none\n";

            int metadataWithoutHostedRegistration = 0;
            oss << "metadataWithNoCurrentHostedRegistration:\n";
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                const apps::BuiltInAppMetadata& metadata = apps::kBuiltInAppMetadata[i];
                if (!apps::IsBuiltInAppAvailableInHosted(metadata)) continue;
                if (currentHostedRegistrationExistsForMetadata(metadata)) continue;
                ++metadataWithoutHostedRegistration;
                oss << "  id=" << (metadata.appId ? metadata.appId : "")
                    << " displayName=" << (metadata.displayName ? metadata.displayName : "") << "\n";
            }
            if (metadataWithoutHostedRegistration == 0) oss << "  none\n";

            int metadataWithoutBareMetalRegistration = 0;
            oss << "metadataWithNoCurrentBareMetalRegistration:\n";
            for (size_t i = 0; i < apps::kBuiltInAppMetadataCount; ++i) {
                const apps::BuiltInAppMetadata& metadata = apps::kBuiltInAppMetadata[i];
                if (!apps::IsBuiltInAppAvailableInBareMetal(metadata)) continue;
                if (currentBareMetalRegistrationExistsForMetadata(metadata)) continue;
                ++metadataWithoutBareMetalRegistration;
                oss << "  id=" << (metadata.appId ? metadata.appId : "")
                    << " kernelAppName=" << (metadata.kernelAppName ? metadata.kernelAppName : "") << "\n";
            }
            if (metadataWithoutBareMetalRegistration == 0) oss << "  none\n";

            oss << "duplicateAppIdsDiscovered:\n";
            std::set<std::string> duplicateIds = duplicateIdsFromScanIssues(s_lastManifestScanResult, s_lastBuiltInRegisterResult);
            if (duplicateIds.empty()) {
                oss << "  none\n";
            } else {
                std::map<std::string, std::vector<ManifestOrigin>> origins = collectManifestOriginsById();
                for (const std::string& appId : duplicateIds) {
                    oss << "  id=" << appId << "\n";
                    oss << "    ownershipHint=" << duplicateOwnershipHint(appId) << "\n";
                    auto originIt = origins.find(appId);
                    if (originIt != origins.end()) {
                        for (const ManifestOrigin& origin : originIt->second) {
                            oss << "    manifest source=" << origin.source
                                << " kind=" << apps::ToString(origin.kind)
                                << " displayName=" << origin.displayName
                                << " path=" << origin.manifestPath.string() << "\n";
                        }
                    }
                    for (const auto& issue : s_lastManifestScanResult.duplicateApps) {
                        if (issue.appId == appId) {
                            oss << "    duplicateDuring=manifestScan source=" << apps::AppRegistry::ToString(issue.sourceKind)
                                << " path=" << issue.manifestPath.string() << "\n";
                        }
                    }
                    for (const auto& issue : s_lastBuiltInRegisterResult.duplicateApps) {
                        if (issue.appId == appId) {
                            oss << "    duplicateDuring=builtInRegister source=" << apps::AppRegistry::ToString(issue.sourceKind)
                                << " path=" << issue.manifestPath.string() << "\n";
                        }
                    }
                }
            }

            appendAppIdNamespaceWarnings(oss);
            appendUiLaunchAliasMetadataDiagnostic(oss);
            oss << "\n" << LaunchTargetShadowDiagnostic();

            return oss.str();
        }

        std::vector<RegisteredDesktopApp> DesktopService::GetAppModelDemoApps() {
            ensureDefaultAppsRegistered();

            std::vector<RegisteredDesktopApp> demos;
            for (const auto& app : s_apps) {
                if (isAppModelDemoApp(app)) demos.push_back(app);
            }

            std::sort(demos.begin(), demos.end(), [](const RegisteredDesktopApp& a, const RegisteredDesktopApp& b) {
                return a.displayName < b.displayName;
            });
            return demos;
        }

        std::string DesktopService::NativeAppCapabilitiesDiagnostic() {
            std::ostringstream oss;
            oss << "nativeapp.capabilities\n";
            oss << "experimental execution enabled: " << (apps::NativeElfExecutor::ExperimentalExecutionEnabled() ? "true" : "false") << "\n";
            oss << "host architecture: " << apps::AppLaunchResolver::CurrentArchitecture() << "\n";
            oss << "supported native execution architecture: amd64\n";
            oss << "supported ELF type: static ET_EXEC\n";
            oss << "dynamic linking supported: false\n";
            oss << "relocations supported: false\n";
            oss << "cross-architecture execution supported: false\n";
            oss << "supported ABI: " << apps::kGuideXOSNativeAbiName << "\n";
            oss << "available host calls:\n";
            oss << "  log\n";
            oss << "  get_api_version\n";
            oss << "  request_window\n";
            oss << "  draw_text\n";
            oss << "  draw_rect\n";
            oss << "  wait_for_close\n";
            oss << "  poll_event\n";
            oss << "  file_exists\n";
            oss << "  file_read_all\n";
            return oss.str();
        }

        std::string DesktopService::InspectNativeAppPipeline(const std::string& appIdOrDisplayName) {
            ensureDefaultAppsRegistered();

            std::ostringstream oss;
            oss << "nativeapp.inspect " << appIdOrDisplayName << "\n";

            const apps::RegisteredApp* app = s_appRegistry.FindById(appIdOrDisplayName);
            if (!app) app = s_appRegistry.FindByDisplayName(appIdOrDisplayName);
            if (!app) {
                oss << "Result: app not found\n";
                return oss.str();
            }

            apps::AppLaunchResolver launchResolver(s_appRegistry, apps::AppLaunchResolver::CurrentArchitecture());
            apps::LaunchDecision launchDecision = launchResolver.ResolveLaunch(*app);
            const apps::AppEntry* selectedEntry = app->FindCompatibleEntry(launchDecision.architecture);

            oss << "\n[Manifest]\n";
            oss << "appId: " << app->manifest.id << "\n";
            oss << "displayName: " << app->manifest.displayName << "\n";
            oss << "kind: " << apps::ToString(app->manifest.kind) << "\n";
            oss << "version: " << app->manifest.version << "\n";
            oss << "publisher: " << app->manifest.publisher << "\n";

            oss << "\n[LaunchResolution]\n";
            oss << "selectedStrategy: " << apps::AppLaunchResolver::ToString(launchDecision.strategy) << "\n";
            oss << "selectedArchitecture: " << launchDecision.architecture << "\n";
            oss << "selectedEntryPath: " << launchDecision.entryPath << "\n";
            oss << "runtime: " << launchDecision.runtime << "\n";
            oss << "abi: " << (selectedEntry ? selectedEntry->abi : std::string()) << "\n";
            oss << "resolverSuccess: " << (launchDecision.success ? "true" : "false") << "\n";
            oss << "resolverReason: " << launchDecision.reason << "\n";

            if (launchDecision.strategy != apps::AppLaunchStrategy::NativeElf || !launchDecision.success) {
                oss << "\nResult: not a resolved NativeElf launch\n";
                return oss.str();
            }

            apps::NativeElfLaunchResult nativeElfResult = apps::NativeElfLaunchPipeline::PrepareLaunch(*app, launchDecision);

            std::vector<uint8_t> elfBytes;
            bool fileExists = FS::exists(nativeElfResult.elfPath);
            apps::ElfValidationResult elfValidation;
            if (fileExists && FS::readAll(nativeElfResult.elfPath, elfBytes)) {
                elfValidation = apps::ElfValidator::Validate(elfBytes, launchDecision.architecture);
            }

            oss << "\n[ElfValidation]\n";
            oss << "elfPath: " << nativeElfResult.elfPath << "\n";
            oss << "fileExists: " << (fileExists ? "true" : "false") << "\n";
            oss << "elfClass: " << elfValidation.elfClass << "\n";
            oss << "endian: " << elfValidation.endian << "\n";
            oss << "machineType: " << elfValidation.machineType << "\n";
            oss << "elfType: " << elfValidation.elfType << "\n";
            oss << "validationSuccess: " << (nativeElfResult.success ? "true" : "false") << "\n";
            if (!nativeElfResult.validationErrors.empty()) {
                oss << "validationErrors: ";
                for (size_t i = 0; i < nativeElfResult.validationErrors.size(); ++i) {
                    if (i > 0) oss << "; ";
                    oss << nativeElfResult.validationErrors[i];
                }
                oss << "\n";
            }

            apps::NativeElfImage nativeElfImage;
            if (nativeElfResult.success) nativeElfImage = apps::NativeElfImageLoader::LoadImage(nativeElfResult);

            oss << "\n[ElfImage]\n";
            oss << "entryVirtualAddress: 0x" << std::hex << nativeElfImage.entryPointVirtualAddress << std::dec << "\n";
            oss << "preferredBase: 0x" << std::hex << nativeElfImage.preferredBaseAddress << std::dec << "\n";
            oss << "imageSize: " << nativeElfImage.imageSize << "\n";
            oss << "segmentCount: " << nativeElfImage.loadedSegments.size() << "\n";
            oss << "ptInterpPresent: " << (nativeElfImage.hasInterpreter ? "true" : "false") << "\n";
            oss << "pieOrDynamic: " << (nativeElfImage.isPositionIndependent ? "true" : "false") << "\n";
            oss << "imageLoaderSuccess: " << (nativeElfImage.success ? "true" : "false") << "\n";
            if (!nativeElfImage.diagnostics.empty()) {
                oss << "imageDiagnostics: ";
                for (size_t i = 0; i < nativeElfImage.diagnostics.size(); ++i) {
                    if (i > 0) oss << "; ";
                    oss << nativeElfImage.diagnostics[i];
                }
                oss << "\n";
            }

            apps::NativeAppRuntimeContext runtimeContext;
            if (nativeElfImage.success) runtimeContext = apps::NativeAppRuntime::Prepare(*app, launchDecision, nativeElfResult, nativeElfImage);
            if (runtimeContext.success) runtimeContext.environment["GX_NATIVE_SMOKETEST"] = "1";

            oss << "\n[Runtime]\n";
            oss << "runtimeId: " << runtimeContext.runtimeId << "\n";
            oss << "processId: " << runtimeContext.processId << "\n";
            oss << "lifecycleState: " << apps::NativeAppRuntime::ToString(runtimeContext.lifecycleState) << "\n";
            oss << "permissions: ";
            for (size_t i = 0; i < runtimeContext.permissions.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << runtimeContext.permissions[i];
            }
            oss << "\n";
            oss << "apiVersion: " << runtimeContext.hostCalls.version << "\n";
            oss << "hostCallsAvailable: " << ((runtimeContext.hostCalls.log && runtimeContext.hostCalls.get_api_version && runtimeContext.hostCalls.request_window && runtimeContext.hostCalls.draw_text && runtimeContext.hostCalls.draw_rect && runtimeContext.hostCalls.wait_for_close && runtimeContext.hostCalls.poll_event && runtimeContext.hostCalls.exit) ? "true" : "false") << "\n";
            oss << "runtimeSuccess: " << (runtimeContext.success ? "true" : "false") << "\n";
            if (!runtimeContext.diagnostics.empty()) {
                oss << "runtimeDiagnostics: ";
                for (size_t i = 0; i < runtimeContext.diagnostics.size(); ++i) {
                    if (i > 0) oss << "; ";
                    oss << runtimeContext.diagnostics[i];
                }
                oss << "\n";
            }

            std::string executorReason;
            bool canExecute = runtimeContext.success && apps::NativeElfExecutor::CanExecute(nativeElfResult, nativeElfImage, runtimeContext, &executorReason);
            oss << "\n[ExecutorGate]\n";
            oss << "canExecute: " << (canExecute ? "true" : "false") << "\n";
            oss << "reason: " << executorReason << "\n";
            oss << "buildFlagEnabled: " << (apps::NativeElfExecutor::ExperimentalExecutionEnabled() ? "true" : "false") << "\n";
            oss << "hostArchitecture: " << apps::AppLaunchResolver::CurrentArchitecture() << "\n";
            oss << "appArchitecture: " << nativeElfResult.architecture << "\n";
            apps::NativeElfExecutionResult executionResult;
            if (canExecute) executionResult = apps::NativeElfExecutor::Execute(nativeElfResult, nativeElfImage, runtimeContext);
            oss << "executableMappingPossible: " << (canExecute ? "true" : "false") << "\n";
            oss << "executionAttempted: " << (canExecute ? "true" : "false") << "\n";
            oss << "executionSuccess: " << (executionResult.success ? "true" : "false") << "\n";
            oss << "runtimeId: " << executionResult.runtimeId << "\n";
            oss << "lifecycleStateBeforeExecution: " << executionResult.lifecycleStateBeforeExecution << "\n";
            oss << "lifecycleStateAfterExecution: " << executionResult.lifecycleStateAfterExecution << "\n";
            oss << "returnCode: " << executionResult.exitCode << "\n";
            oss << "exitCode: " << executionResult.exitCode << "\n";
            oss << "cleanupAttempted: " << (executionResult.cleanupAttempted ? "true" : "false") << "\n";
            oss << "cleanedWindowCount: " << executionResult.cleanedWindowCount << "\n";
            oss << "remainingOwnedWindowCount: " << executionResult.remainingOwnedWindowCount << "\n";
            oss << "preferredBase: 0x" << std::hex << executionResult.preferredBaseAddress << std::dec << "\n";
            oss << "actualMappedBase: 0x" << std::hex << executionResult.actualMappedBaseAddress << std::dec << "\n";
            oss << "preferredBaseMappingAttempted: " << (executionResult.preferredBaseMappingAttempted ? "true" : "false") << "\n";
            oss << "preferredBaseMappingSuccess: " << (executionResult.preferredBaseMappingSucceeded ? "true" : "false") << "\n";
            oss << "trampolineUsed: " << (executionResult.trampolineUsed ? "true" : "false") << "\n";
            oss << "entryHostAddress: 0x" << std::hex << executionResult.entryHostAddress << std::dec << "\n";
            oss << "gxMainReturnCode: " << executionResult.exitCode << "\n";
            if (!executionResult.failureReason.empty()) oss << "failureReason: " << executionResult.failureReason << "\n";
            oss << "requestWindowCallCount: " << executionResult.requestWindowCallCount << "\n";
            oss << "lastWindowId: " << executionResult.lastWindowId << "\n";
            oss << "lastWindowTitle: " << executionResult.lastWindowTitle << "\n";
            oss << "requestWindowResult: " << executionResult.requestWindowResult << "\n";
            oss << "drawTextCallCount: " << executionResult.drawTextCallCount << "\n";
            oss << "lastDrawTextWindow: " << executionResult.lastDrawTextWindow << "\n";
            oss << "lastDrawText: " << executionResult.lastDrawText << "\n";
            oss << "lastDrawTextResult: " << executionResult.lastDrawTextResult << "\n";
            oss << "drawRectCallCount: " << executionResult.drawRectCallCount << "\n";
            oss << "lastDrawRectWindow: " << executionResult.lastDrawRectWindow << "\n";
            oss << "lastDrawRectWidth: " << executionResult.lastDrawRectWidth << "\n";
            oss << "lastDrawRectHeight: " << executionResult.lastDrawRectHeight << "\n";
            oss << "lastDrawRectColor: " << executionResult.lastDrawRectColor << "\n";
            oss << "lastDrawRectResult: " << executionResult.lastDrawRectResult << "\n";
            oss << "waitForCloseCallCount: " << executionResult.waitForCloseCallCount << "\n";
            oss << "lastWaitWindow: " << executionResult.lastWaitWindow << "\n";
            oss << "lastWaitTimeoutMs: " << executionResult.lastWaitTimeoutMs << "\n";
            oss << "lastWaitResult: " << executionResult.lastWaitResult << "\n";
            oss << "pollEventCallCount: " << executionResult.pollEventCallCount << "\n";
            oss << "lastEventType: " << static_cast<uint32_t>(executionResult.lastEventType) << "\n";
            oss << "lastEventWindow: " << executionResult.lastEventWindow << "\n";
            oss << "lastPollEventResult: " << executionResult.lastPollEventResult << "\n";
            oss << "paintEventCount: " << executionResult.paintEventCount << "\n";
            oss << "lastPaintWindow: " << executionResult.lastPaintWindow << "\n";
            oss << "lastPaintWidth: " << executionResult.lastPaintWidth << "\n";
            oss << "lastPaintHeight: " << executionResult.lastPaintHeight << "\n";
            oss << "keyEventCount: " << executionResult.keyEventCount << "\n";
            oss << "lastKeyWindow: " << executionResult.lastKeyWindow << "\n";
            oss << "lastKeyCode: " << executionResult.lastKeyCode << "\n";
            oss << "lastKeyAction: " << executionResult.lastKeyAction << "\n";
            oss << "lastKeyModifiers: " << executionResult.lastKeyModifiers << "\n";
            oss << "mouseEventCount: " << executionResult.mouseEventCount << "\n";
            oss << "lastMouseWindow: " << executionResult.lastMouseWindow << "\n";
            oss << "lastMouseX: " << executionResult.lastMouseX << "\n";
            oss << "lastMouseY: " << executionResult.lastMouseY << "\n";
            oss << "lastMousePackedButtonAction: " << executionResult.lastMousePackedButtonAction << "\n";
            oss << "lastMouseModifiers: " << executionResult.lastMouseModifiers << "\n";
            if (!executionResult.diagnostics.empty()) {
                oss << "executionDiagnostics: ";
                for (size_t i = 0; i < executionResult.diagnostics.size(); ++i) {
                    if (i > 0) oss << "; ";
                    oss << executionResult.diagnostics[i];
                }
                oss << "\n";
            } else {
                oss << "executionDiagnostics: execution skipped or unavailable\n";
            }
            oss << "\nResult: inspection complete; " << (canExecute ? "experimental execution path attempted" : "no ELF code executed") << "\n";
            return oss.str();
        }

        std::string DesktopService::NativeAppPipelineSmokeTest(const std::string& appIdOrDisplayName) {
            std::ostringstream oss;
            oss << "NativeAppPipelineSmokeTest(" << appIdOrDisplayName << ")\n";
            oss << InspectNativeAppPipeline(appIdOrDisplayName);
            oss << "\nExpected: find manifest, validate ELF if present, load image, prepare runtime, stop at executor gate unless experimental execution is enabled.\n";
            return oss.str();
        }

        bool DesktopService::OpenFilesystemEntry(const std::string& path, bool isDirectory, std::string& error) {
            error.clear();
            Logger::write(LogLevel::Info, std::string("Desktop filesystem open requested path=") + path + " directory=" + (isDirectory ? "true" : "false"));
            if (path.empty()) {
                error = "No filesystem path supplied";
                return false;
            }

            if (isDirectory) {
                apps::FileExplorer::Launch(path);
                return true;
            }

            std::string lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.size() >= 4 && (lower.substr(lower.size() - 4) == ".txt" || lower.substr(lower.size() - 4) == ".log" || lower.substr(lower.size() - 4) == ".cfg" || lower.substr(lower.size() - 4) == ".ini")) {
                apps::Notepad::LaunchWithFile(path);
                return true;
            }
            if ((lower.size() >= 4 && (lower.substr(lower.size() - 4) == ".png" || lower.substr(lower.size() - 4) == ".bmp" || lower.substr(lower.size() - 4) == ".jpg" || lower.substr(lower.size() - 4) == ".gif")) ||
                (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".jpeg")) {
                apps::ImageViewer::Launch(path);
                return true;
            }

            error = "No file association registered for " + path;
            Logger::write(LogLevel::Warn, "Desktop filesystem open failed: " + error);
            NotificationManager::Add(error, NotificationLevel::Error);
            return false;
        }

        bool DesktopService::LaunchApp(const std::string& name, std::string& error) {
            ensureDefaultAppsRegistered();
            std::string appName = canonicalAppName(name);
            const RegisteredDesktopApp* manifestApp = findRegisteredApp(appName);

            // Installed universal applications are launched through the package manager.
            if (!manifestApp) {
                if (PackageManager::LaunchGXApp(appName, error)) {
                    AddRecentProgram(appName);
                    return true;
                }
                error = "Application not registered: " + name;
                return false;
            }

            const apps::RegisteredApp* registryApp = findRegistryApp(*manifestApp);
            if (!registryApp) {
                error = "Application manifest not found: " + name;
                return false;
            }

            apps::AppLaunchResolver launchResolver(s_appRegistry, apps::AppLaunchResolver::CurrentArchitecture());
            apps::LaunchDecision launchDecision = launchResolver.ResolveLaunch(*registryApp);
            if (!launchDecision.success) {
                error = launchDecision.reason;
                return false;
            }

            if (launchDecision.strategy == apps::AppLaunchStrategy::NativeElf) {
                const apps::AppEntry* nativeEntry = registryApp->FindCompatibleEntry(launchDecision.architecture);
                std::string resolvedNativeElfPath;
                if (nativeEntry && !nativeEntry->path.empty() && !registryApp->appDirectory.empty()) {
                    resolvedNativeElfPath = (registryApp->appDirectory / std::filesystem::path(nativeEntry->path)).string();
                }

                if (!apps::NativeElfExecutor::ExperimentalExecutionEnabled()) {
                    error = std::string("Native app launch requires the experimental hosted runtime. Build with build-native-experimental.bat and run with run-server-experimental.bat: ") + manifestApp->displayName;
                    if (!resolvedNativeElfPath.empty() && !FS::exists(resolvedNativeElfPath)) {
                        error += " (sample binary not built: " + resolvedNativeElfPath + ")";
                    }
                    NotificationManager::Add(error, NotificationLevel::Error);
                    return false;
                }

                if (!resolvedNativeElfPath.empty() && !FS::exists(resolvedNativeElfPath)) {
                    error = std::string("Native app discovered but sample binary is missing: ") + resolvedNativeElfPath;
                    NotificationManager::Add(error, NotificationLevel::Error);
                    return false;
                }

                uint64_t nativePid = launchNativeElfProcess(*registryApp, launchDecision);
                if (nativePid == 0) {
                    error = std::string("Native app launch failed to start process: ") + manifestApp->displayName;
                    NotificationManager::Add(error, NotificationLevel::Error);
                    return false;
                }

                Logger::write(LogLevel::Info, std::string("Launched native app process: ") + manifestApp->displayName + " pid=" + std::to_string(nativePid));
                return true;
            }

            if (launchDecision.strategy == apps::AppLaunchStrategy::GXAppPackage) {
                error = "GXApp launch pipeline not implemented";
                NotificationManager::Add(error, NotificationLevel::Error);
                return false;
            }

            if (launchDecision.strategy != apps::AppLaunchStrategy::BuiltIn) {
                Logger::write(LogLevel::Warn, std::string("Launch attempted for unsupported launch strategy: ") + apps::AppLaunchResolver::ToString(launchDecision.strategy) + " id=" + launchDecision.appId);
                error = std::string("Manifest found for ") + manifestApp->displayName + " but execution is not implemented yet for " + apps::AppLaunchResolver::ToString(launchDecision.strategy);
                return false;
            }

            if (!launchDecision.launchName.empty()) appName = canonicalAppName(launchDecision.launchName);

            // Ensure compositor is running before launching any GUI app
            // Track if we just started it so we can wait for it to initialize
            uint64_t prevCompositorPid = Lifecycle::state().compositorPid;
            uint64_t compositorPid = Lifecycle::ensureCompositor();
            if (compositorPid == 0) {
                error = "Compositor failed to start";
                return false;
            }
            
            // If compositor was just started, wait briefly for it to initialize
            if (prevCompositorPid == 0 && compositorPid != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                Logger::write(LogLevel::Info, "LaunchApp: Waited for compositor to initialize");
            }

            // Add to recent
            AddRecentProgram(name);

            // Launch the actual application
            if (appName == "Notepad") {
                apps::Notepad::Launch();
            }
            else if (appName == "Calculator") {
                apps::Calculator::Launch();
            }
            else if (appName == "Console") {
                apps::ConsoleWindow::Launch();
            }
            else if (appName == "FileExplorer") {
                apps::FileExplorer::Launch();
            }
            else if (appName == "Clock") {
                apps::Clock::Launch();
            }
            else if (appName == "TaskManager") {
                apps::TaskManager::Launch();
            }
            else if (appName == "Paint") {
                apps::Paint::Launch();
            }
            else if (appName == "ImageViewer") {
                apps::ImageViewer::Launch();
            }
            else if (appName == "OnScreenKeyboard") {
                apps::OnScreenKeyboard::Launch();
            }
            else if (appName == "ShutdownDialog") {
                apps::ShutdownDialog::Launch();
            }
            else if (appName == "DiskManager") {
                apps::DiskManager::Launch();
            }
            else if (appName == "ControlPanel") {
                apps::ControlPanel::Launch();
            }
            else if (appName == "DisplayOptions" || appName == "Display Settings" || appName == "Desktop Background" || appName == "Wallpaper") {
                apps::DisplayOptions::Launch();
            }
            else if (appName == "guideXOS Navigator") {
                // Hosted/compositor Navigator launch path: app-model registration
                // resolves here, then starts the authoritative Navigator process.
                apps::Navigator::Launch();
            }
            else if (appName == "Trash") {
                apps::Trash::Launch();
            }
            else if (appName == "HDInstaller") {
                error = "HD Installer is not available in this runtime target";
                NotificationManager::Add(error, NotificationLevel::Error);
                return false;
            }
            else if (appName == "Native App Debug Viewer") {
                apps::ConsoleWindow::Launch();
                NotificationManager::Add("Native App Debug Viewer opened. Try: nativeapp.inspect Hello World", NotificationLevel::Info);
            }
            else {
                error = "Application launcher not implemented: " + name;
                NotificationManager::Add(error, NotificationLevel::Error);
                return false;
            }

            Logger::write(LogLevel::Info, std::string("Launched app: ") + name);
            return true;
        }

        void DesktopService::LoadState() {
            // Load from desktop.json
            DesktopConfigData cfg;
            std::string err;
            if (!DesktopConfig::Load("desktop.json", cfg, err)) {
                Logger::write(LogLevel::Info, std::string("Desktop config not found (first run): ") + err);
                ensureDefaultAppsRegistered();
                SaveState();
                return;
            }

            // Load pinned from cfg.pinned
            s_pinned.clear();
            for (const auto& p : cfg.pinned) {
                PinnedItem item;
                item.name = p;
                item.kind = PinnedKind::App; // Default to app; TODO: enhance config to store kind
                item.iconName = "document";
                s_pinned.push_back(item);
            }

            // Load recent from cfg.recent
            s_recentPrograms.clear();
            for (const auto& r : cfg.recent) {
                RecentProgramEntry entry;
                entry.name = r;
                entry.lastUsedTicks = currentTicks();
                entry.iconName = "document";
                s_recentPrograms.push_back(entry);
            }

            ensureDefaultAppsRegistered();
            SaveState();

            Logger::write(LogLevel::Info, "Desktop state loaded");
        }

        void DesktopService::SaveState() {
            // Save to desktop.json
            DesktopConfigData cfg;
            std::string err;

            // Load existing config to preserve wallpaper and windows
            if (DesktopConfig::Load("desktop.json", cfg, err)) {
                // Keep existing wallpaper and windows
            }

            // Update pinned
            cfg.pinned.clear();
            for (const auto& item : s_pinned) {
                cfg.pinned.push_back(item.name);
            }

            // Update recent
            cfg.recent.clear();
            for (const auto& prog : s_recentPrograms) {
                cfg.recent.push_back(prog.name);
            }

            if (!DesktopConfig::Save("desktop.json", cfg, err)) {
                Logger::write(LogLevel::Error, std::string("Failed to save desktop config: ") + err);
            }
        }
    }
}
