#include "desktop_service.h"
#include "app_launch_resolver.h"
#include "app_registry.h"
#include "desktop_config.h"
#include "elf_validator.h"
#include "fs.h"
#include "logger.h"
#include "lifecycle.h"
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
#include "shutdown_dialog.h"
#include "disk_manager.h"
#include "control_panel.h"
#include "hd_installer.h"
#include "package_manager.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
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
                if (!issue.manifestPath.empty()) message += issue.manifestPath.string() + " ";
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

        static void refreshRegisteredAppsFromRegistry() {
            for (const auto& app : s_appRegistry.GetAllApps()) {
                DesktopService::RegisterApp(app.manifest.id, app.manifest.displayName, app.manifest.icon, app.manifest.kind, launchNameForApp(app), apps::AppRegistry::ToString(app.sourceKind));
            }
        }

        static void appendScanIssues(std::ostringstream& oss, const char* label, const std::vector<apps::AppScanIssue>& issues) {
            oss << label << ": " << issues.size() << "\n";
            for (const auto& issue : issues) {
                oss << "  source=" << apps::AppRegistry::ToString(issue.sourceKind);
                if (!issue.appId.empty()) oss << " id=" << issue.appId;
                if (!issue.manifestPath.empty()) oss << " path=" << issue.manifestPath.string();
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

            s_apps.clear();
            refreshRegisteredAppsFromRegistry();
            s_appRegistryInitialized = true;
            Logger::write(LogLevel::Info, "AppRegistry initialized, desktop apps=" + std::to_string(s_apps.size()));
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
                oss << "  " << apps::AppRegistry::ToString(source.kind) << " " << source.path.string() << "\n";
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
                    << " source=" << app.source << "\n";
            }
            oss << "launchPolicy: BuiltIn uses existing hardcoded launch branch; NativeElf/GXAppPackage return: manifest found but execution is not implemented yet\n";
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
            oss << "manifestPath: " << app->manifestPath.string() << "\n";
            oss << "appDirectory: " << app->appDirectory.string() << "\n";

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

            oss << "\n[Runtime]\n";
            oss << "lifecycleState: " << apps::NativeAppRuntime::ToString(runtimeContext.lifecycleState) << "\n";
            oss << "permissions: ";
            for (size_t i = 0; i < runtimeContext.permissions.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << runtimeContext.permissions[i];
            }
            oss << "\n";
            oss << "apiVersion: " << runtimeContext.hostCalls.version << "\n";
            oss << "hostCallsAvailable: " << ((runtimeContext.hostCalls.log && runtimeContext.hostCalls.get_api_version && runtimeContext.hostCalls.request_window && runtimeContext.hostCalls.exit) ? "true" : "false") << "\n";
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
            oss << "returnCode: " << executionResult.exitCode << "\n";
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
                apps::NativeElfLaunchResult nativeElfResult = apps::NativeElfLaunchPipeline::PrepareLaunch(*registryApp, launchDecision);
                if (nativeElfResult.success) {
                    apps::NativeElfImage nativeElfImage = apps::NativeElfImageLoader::LoadImage(nativeElfResult);
                    if (nativeElfImage.success) {
                        apps::NativeAppRuntimeContext runtimeContext = apps::NativeAppRuntime::Prepare(*registryApp, launchDecision, nativeElfResult, nativeElfImage);
                        if (runtimeContext.success) {
                            std::string executorReason;
                            if (apps::NativeElfExecutor::CanExecute(nativeElfResult, nativeElfImage, runtimeContext, &executorReason)) {
                                error = "Native app executor available; use nativeapp.inspect or nativeapp.smoketest for experimental execution";
                            } else {
                                error = "Native app runtime prepared; execution disabled/not available";
                                if (!executorReason.empty()) error += ": " + executorReason;
                            }
                        } else {
                            std::ostringstream details;
                            details << "Native app runtime prepare failed";
                            if (!runtimeContext.diagnostics.empty()) {
                                details << ": ";
                                for (size_t i = 0; i < runtimeContext.diagnostics.size(); ++i) {
                                    if (i > 0) details << "; ";
                                    details << runtimeContext.diagnostics[i];
                                }
                            }
                            error = details.str();
                        }
                    } else {
                        std::ostringstream details;
                        details << "Native ELF image load failed";
                        if (!nativeElfImage.diagnostics.empty()) {
                            details << ": ";
                            for (size_t i = 0; i < nativeElfImage.diagnostics.size(); ++i) {
                                if (i > 0) details << "; ";
                                details << nativeElfImage.diagnostics[i];
                            }
                        }
                        error = details.str();
                    }
                } else {
                    std::ostringstream details;
                    details << "Native ELF validation failed";
                    if (!nativeElfResult.validationErrors.empty()) {
                        details << ": ";
                        for (size_t i = 0; i < nativeElfResult.validationErrors.size(); ++i) {
                            if (i > 0) details << "; ";
                            details << nativeElfResult.validationErrors[i];
                        }
                    } else if (!nativeElfResult.message.empty()) {
                        details << ": " << nativeElfResult.message;
                    }
                    error = details.str();
                }
                return false;
            }

            if (launchDecision.strategy == apps::AppLaunchStrategy::GXAppPackage) {
                error = "GXApp launch pipeline not implemented";
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
            else if (appName == "HDInstaller") {
                apps::HDInstaller::Launch();
            }
            else {
                error = "Application launcher not implemented: " + name;
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