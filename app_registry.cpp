#include "app_registry.h"

#include "app_manifest_loader.h"

namespace gxos {
namespace apps {
namespace {

bool architectureMatches(const std::string& entryArchitecture, const std::string& currentArchitecture) {
    return entryArchitecture == currentArchitecture || entryArchitecture == "any" || entryArchitecture == "*";
}

std::string builtInAppId(const std::string& appName) {
    std::string id = "gxos.builtin.";
    for (char c : appName) {
        if (c >= 'A' && c <= 'Z') {
            id.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if (c >= 'a' && c <= 'z') {
            id.push_back(c);
        } else if (c >= '0' && c <= '9') {
            id.push_back(c);
        } else if (c == '-' || c == '_') {
            id.push_back(c);
        }
    }
    return id;
}

RegisteredApp makeBuiltInApp(const std::string& appName) {
    RegisteredApp app;
    app.sourceKind = AppSourceKind::BuiltIn;
    app.manifest.schemaVersion = kSupportedAppManifestSchemaVersion;
    app.manifest.id = builtInAppId(appName);
    app.manifest.displayName = appName;
    app.manifest.version = "1.0.0";
    app.manifest.publisher = "guideXOS";
    app.manifest.description = "Built-in guideXOS application.";
    app.manifest.category = "BuiltIn";
    app.manifest.kind = AppKind::BuiltIn;
    if (appName == "Trash") {
        app.manifest.description = "Built-in guideXOS Trash placeholder.";
        app.manifest.icon = "trash.empty";
    }
    app.manifest.supportedArchitectures.push_back("any");

    AppEntry entry;
    entry.architecture = "any";
    entry.path = "builtin/" + appName;
    entry.entryPoint = appName;
    entry.abi = "guidexos-desktop-service-v1";
    entry.runtime = "builtin-hosted";
    app.manifest.entries.push_back(entry);

    app.manifest.permissions.push_back("desktop.window");
    app.manifest.desktopRegistryHints["registeredName"] = appName;
    return app;
}

AppScanIssue makeIssue(AppSourceKind sourceKind, const std::filesystem::path& manifestPath, const std::string& appId, const std::vector<std::string>& errors) {
    AppScanIssue issue;
    issue.sourceKind = sourceKind;
    issue.manifestPath = manifestPath;
    issue.appId = appId;
    issue.errors = errors;
    return issue;
}

const std::vector<std::string>& defaultBuiltInAppNames() {
    static const std::vector<std::string> appNames = {
        "Notepad",
        "Calculator",
        "Clock",
        "Console",
        "FileExplorer",
        "Trash",
        "TaskManager",
        "Paint",
        "ImageViewer",
        "OnScreenKeyboard",
        "ShutdownDialog",
        "DiskManager",
        "ControlPanel",
        "App Model Demo",
        "Native App Debug Viewer",
        "HDInstaller"
    };
    return appNames;
}

} // namespace

const AppEntry* RegisteredApp::FindCompatibleEntry(const std::string& currentArchitecture) const {
    for (const AppEntry& entry : manifest.entries) {
        if (architectureMatches(entry.architecture, currentArchitecture)) return &entry;
    }

    if (manifest.kind == AppKind::BuiltIn && !manifest.entries.empty()) return &manifest.entries.front();
    return nullptr;
}

AppRegistry::AppRegistry()
    : m_sources(DefaultSources()) {
}

AppRegistry::AppRegistry(bool preferSystemAppsOverUserApps)
    : m_preferSystemAppsOverUserApps(preferSystemAppsOverUserApps), m_sources(DefaultSources()) {
}

void AppRegistry::SetPreferSystemAppsOverUserApps(bool enabled) {
    m_preferSystemAppsOverUserApps = enabled;
}

bool AppRegistry::PreferSystemAppsOverUserApps() const {
    return m_preferSystemAppsOverUserApps;
}

void AppRegistry::Clear() {
    m_apps.clear();
    m_appsById.clear();
}

void AppRegistry::SetSources(const std::vector<AppRegistrySource>& sources) {
    m_sources = sources;
}

void AppRegistry::AddSource(AppSourceKind kind, const std::filesystem::path& path) {
    AppRegistrySource source;
    source.kind = kind;
    source.path = path;
    m_sources.push_back(source);
}

AppScanResult AppRegistry::Scan() {
    return Scan(m_sources);
}

AppScanResult AppRegistry::Scan(const std::vector<AppRegistrySource>& sources) {
    Clear();

    AppScanResult result;
    for (const AppRegistrySource& source : sources) {
        if (!std::filesystem::exists(source.path)) continue;

        std::error_code error;
        std::filesystem::recursive_directory_iterator it(source.path, std::filesystem::directory_options::skip_permission_denied, error);
        std::filesystem::recursive_directory_iterator end;
        if (error) {
            result.invalidApps.push_back(makeIssue(source.kind, source.path, std::string(), { "Unable to scan app source: " + error.message() }));
            continue;
        }

        for (; it != end; it.increment(error)) {
            if (error) {
                result.invalidApps.push_back(makeIssue(source.kind, source.path, std::string(), { "Unable to continue app source scan: " + error.message() }));
                error.clear();
                continue;
            }

            const std::filesystem::directory_entry& entry = *it;
            if (!entry.is_regular_file(error) || error) {
                error.clear();
                continue;
            }

            if (entry.path().filename() != "app.json") continue;

            ++result.scannedManifestCount;
            AppManifestLoadResult loadResult = AppManifestLoader::LoadFromFile(entry.path());
            if (!loadResult.valid) {
                result.invalidApps.push_back(makeIssue(source.kind, entry.path(), loadResult.manifest.id, loadResult.errors));
                continue;
            }

            RegisteredApp app;
            app.manifest = loadResult.manifest;
            app.sourceKind = source.kind;
            app.manifestPath = entry.path();
            app.appDirectory = entry.path().parent_path();
            RegisterApp(app, result);
        }
    }

    result.registeredAppCount = m_apps.size();
    result.registeredApps = m_apps;
    return result;
}

AppScanResult AppRegistry::RegisterBuiltInAppsAsManifests() {
    return RegisterBuiltInAppsAsManifests(defaultBuiltInAppNames());
}

AppScanResult AppRegistry::RegisterBuiltInAppsAsManifests(const std::vector<std::string>& appNames) {
    AppScanResult result;
    for (const std::string& appName : appNames) {
        ++result.scannedManifestCount;
        RegisterApp(makeBuiltInApp(appName), result);
    }

    result.registeredAppCount = m_apps.size();
    result.registeredApps = m_apps;
    return result;
}

const std::vector<RegisteredApp>& AppRegistry::GetAllApps() const {
    return m_apps;
}

const RegisteredApp* AppRegistry::FindById(const std::string& appId) const {
    auto it = m_appsById.find(appId);
    return it == m_appsById.end() ? nullptr : &m_apps[it->second];
}

const RegisteredApp* AppRegistry::FindByDisplayName(const std::string& displayName) const {
    for (const RegisteredApp& app : m_apps) {
        if (app.manifest.displayName == displayName) return &app;
    }
    return nullptr;
}

const AppEntry* AppRegistry::FindCompatibleEntry(const std::string& appId, const std::string& currentArchitecture) const {
    const RegisteredApp* app = FindById(appId);
    return app ? app->FindCompatibleEntry(currentArchitecture) : nullptr;
}

std::vector<AppRegistrySource> AppRegistry::DefaultSources() {
    return {
        { AppSourceKind::SystemApps, "/system/apps" },
        { AppSourceKind::SystemApps, "sdk/samples" },
        { AppSourceKind::SystemApps, "examples/apps" },
        { AppSourceKind::Package, "/Apps" },
        { AppSourceKind::UserApps, "/users/default/apps" }
    };
}

const char* AppRegistry::ToString(AppSourceKind kind) {
    switch (kind) {
    case AppSourceKind::BuiltIn: return "BuiltIn";
    case AppSourceKind::SystemApps: return "SystemApps";
    case AppSourceKind::UserApps: return "UserApps";
    case AppSourceKind::Package: return "Package";
    default: return "Unknown";
    }
}

bool AppRegistry::RegisterApp(const RegisteredApp& app, AppScanResult& result) {
    auto existing = m_appsById.find(app.manifest.id);
    if (existing != m_appsById.end()) {
        std::vector<std::string> errors = { "Duplicate app id: " + app.manifest.id };
        result.duplicateApps.push_back(makeIssue(app.sourceKind, app.manifestPath, app.manifest.id, errors));
        if (!ShouldReplaceDuplicate(m_apps[existing->second], app)) return false;

        m_apps[existing->second] = app;
        return true;
    }

    m_appsById[app.manifest.id] = m_apps.size();
    m_apps.push_back(app);
    return true;
}

bool AppRegistry::ShouldReplaceDuplicate(const RegisteredApp& existingApp, const RegisteredApp& newApp) const {
    if (existingApp.sourceKind == AppSourceKind::SystemApps && newApp.sourceKind == AppSourceKind::UserApps) {
        return !m_preferSystemAppsOverUserApps;
    }

    if (existingApp.sourceKind == AppSourceKind::UserApps && newApp.sourceKind == AppSourceKind::SystemApps) {
        return m_preferSystemAppsOverUserApps;
    }

    return false;
}

} // namespace apps
} // namespace gxos
