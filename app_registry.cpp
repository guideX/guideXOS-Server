#include "app_registry.h"

#include "app_manifest_loader.h"
#include "built_in_app_metadata.h"

#include <cctype>
#include <cstdlib>

namespace gxos {
namespace apps {
namespace {

std::string joinKnownAliases(const BuiltInAppMetadata& metadata) {
    std::string result;
    if (!metadata.knownAliases || metadata.knownAliasCount == 0) return result;

    for (size_t i = 0; i < metadata.knownAliasCount; ++i) {
        const char* alias = metadata.knownAliases[i];
        if (!alias || !alias[0]) continue;
        if (!result.empty()) result += "|";
        result += alias;
    }
    return result;
}

bool architectureMatches(const std::string& entryArchitecture, const std::string& currentArchitecture) {
    return entryArchitecture == currentArchitecture || entryArchitecture == "any" || entryArchitecture == "*";
}

std::string trimCopy(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;

    return value.substr(begin, end - begin);
}

std::vector<AppRegistrySource> experimentalSourcesFromEnvironment() {
    std::vector<AppRegistrySource> sources;

    // Opt-in staging hook for isolated experimental app registries.
    const char* stageRootEnv = std::getenv("GXOS_NATIVE_ELF_STAGE_ROOT");
    if (!stageRootEnv || !stageRootEnv[0]) return sources;

    std::string raw(stageRootEnv);
    size_t start = 0;
    while (start <= raw.size()) {
        size_t end = raw.find(';', start);
        std::string token = trimCopy(raw.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!token.empty()) {
            AppRegistrySource source;
            source.kind = AppSourceKind::Package;
            source.path = std::filesystem::path(token);
            sources.push_back(source);
        }

        if (end == std::string::npos) break;
        start = end + 1;
    }

    return sources;
}

RegisteredApp makeBuiltInApp(const BuiltInAppMetadata& metadata) {
    RegisteredApp app;
    app.sourceKind = AppSourceKind::BuiltIn;
    app.manifest.schemaVersion = kSupportedAppManifestSchemaVersion;
    app.manifest.id = metadata.appId ? metadata.appId : std::string();
    app.manifest.displayName = metadata.displayName ? metadata.displayName : std::string();
    app.manifest.version = "1.0.0";
    app.manifest.publisher = "guideXOS";
    app.manifest.description = metadata.description ? metadata.description : "Built-in guideXOS application.";
    app.manifest.category = metadata.category ? metadata.category : "BuiltIn";
    app.manifest.kind = AppKind::BuiltIn;
    app.manifest.icon = metadata.iconKey ? metadata.iconKey : std::string();
    app.manifest.defaultWindow.width = metadata.defaultWindowWidth;
    app.manifest.defaultWindow.height = metadata.defaultWindowHeight;
    app.manifest.defaultWindow.resizable = metadata.defaultWindowResizable;
    app.manifest.supportedArchitectures.push_back("any");

    AppEntry entry;
    entry.architecture = "any";
    entry.path = std::string("builtin/") + app.manifest.displayName;
    entry.entryPoint = BuiltInAppCanonicalLaunchName(metadata);
    entry.abi = "guidexos-desktop-service-v1";
    entry.runtime = "builtin-hosted";
    app.manifest.entries.push_back(entry);

    app.manifest.permissions.push_back("desktop.window");
    app.manifest.desktopRegistryHints["registeredName"] = entry.entryPoint;
    app.manifest.desktopRegistryHints["canonicalLaunchName"] = entry.entryPoint;
    app.manifest.desktopRegistryHints["knownAliases"] = joinKnownAliases(metadata);
    app.manifest.desktopRegistryHints["appearsInStartMenu"] = metadata.appearsInStartMenu ? "true" : "false";
    app.manifest.desktopRegistryHints["canAppearOnDesktop"] = metadata.canAppearOnDesktop ? "true" : "false";
    app.manifest.desktopRegistryHints["recordRecentPrograms"] = metadata.recordRecentPrograms ? "true" : "false";
    app.manifest.desktopRegistryHints["acceptsFileTargets"] = metadata.acceptsFileTargets ? "true" : "false";
    app.manifest.desktopRegistryHints["acceptsFolderTargets"] = metadata.acceptsFolderTargets ? "true" : "false";
    app.manifest.desktopRegistryHints["systemShellObject"] = metadata.systemShellObject ? "true" : "false";
    app.manifest.desktopRegistryHints["riskyForActiveTypedDispatch"] = metadata.riskyForActiveTypedDispatch ? "true" : "false";
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

std::vector<const BuiltInAppMetadata*> defaultHostedBuiltInApps() {
    std::vector<const BuiltInAppMetadata*> apps;
    for (size_t i = 0; i < kBuiltInAppMetadataCount; ++i) {
        if (!IsBuiltInAppAvailableInHosted(kBuiltInAppMetadata[i])) continue;
        apps.push_back(&kBuiltInAppMetadata[i]);
    }
    return apps;
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
    AppScanResult result;
    const std::vector<const BuiltInAppMetadata*> builtIns = defaultHostedBuiltInApps();
    for (const BuiltInAppMetadata* metadata : builtIns) {
        if (!metadata) continue;
        ++result.scannedManifestCount;
        RegisterApp(makeBuiltInApp(*metadata), result);
    }

    result.registeredAppCount = m_apps.size();
    result.registeredApps = m_apps;
    return result;
}

AppScanResult AppRegistry::RegisterBuiltInAppsAsManifests(const std::vector<std::string>& appNames) {
    AppScanResult result;
    for (const std::string& appName : appNames) {
        const BuiltInAppMetadata* metadata = FindBuiltInAppMetadataByDisplayName(appName.c_str());
        if (!metadata || !IsBuiltInAppAvailableInHosted(*metadata)) continue;
        ++result.scannedManifestCount;
        RegisterApp(makeBuiltInApp(*metadata), result);
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
    std::vector<AppRegistrySource> sources = {
        { AppSourceKind::SystemApps, "/system/apps" },
        { AppSourceKind::SystemApps, "sdk/samples" },
        { AppSourceKind::SystemApps, "examples/apps" },
        { AppSourceKind::Package, "/Apps" },
        { AppSourceKind::UserApps, "/users/default/apps" }
    };

    const std::vector<AppRegistrySource> stagedSources = experimentalSourcesFromEnvironment();
    sources.insert(sources.end(), stagedSources.begin(), stagedSources.end());
    return sources;
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
