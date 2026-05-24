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

        static int appendAppIdNamespaceWarnings(std::ostringstream& oss) {
            int warningCount = 0;
            oss << "appIdNamespaceWarnings(nonFatal):\n";

            for (const ManifestOrigin& origin : collectManifestOrigins()) {
                const bool usesBuiltInNamespace = startsWith(origin.appId, "gxos.builtin.");
                const bool usesSampleNamespace = startsWith(origin.appId, "com.guidexos.samples.");
                const bool usesExampleNamespace = startsWith(origin.appId, "com.guidexos.examples.");

                if (origin.sourceKind != apps::AppSourceKind::BuiltIn && usesBuiltInNamespace) {
                    ++warningCount;
                    appendNamespaceWarning(oss, origin, "non-built-in manifest uses gxos.builtin.*; built-ins own this namespace");
                }
                if (originIsSdkSample(origin) && !usesSampleNamespace) {
                    ++warningCount;
                    appendNamespaceWarning(oss, origin, "SDK sample manifest should use com.guidexos.samples.*");
                }
                if (originIsRepoExample(origin) && !usesExampleNamespace) {
                    ++warningCount;
                    appendNamespaceWarning(oss, origin, "repo example manifest should use com.guidexos.examples.*");
                }
                if (originIsInstalledPackage(origin) && (usesBuiltInNamespace || usesSampleNamespace || usesExampleNamespace)) {
                    ++warningCount;
                    appendNamespaceWarning(oss, origin, "installed /Apps manifest should use a normal installed app id, not builtin/sample/example namespaces");
                }
            }

            if (warningCount == 0) oss << "  none\n";
            return warningCount;
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
