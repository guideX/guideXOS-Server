#include "app_registry.h"

#include "app_manifest_loader.h"
#include "built_in_app_metadata.h"

#include <algorithm>
#include <limits>
#include <utility>

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

bool entryPathIsContainedAndPresent(const RegisteredApp& app, const AppEntry& entry) {
    if (entry.path.empty() || app.appDirectory.empty()) return false;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::weakly_canonical(app.appDirectory, error);
    if (error) return false;
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(app.appDirectory / std::filesystem::path(entry.path), error);
    if (error) return false;
    const std::filesystem::path relative = std::filesystem::relative(candidate, root, error);
    if (error || relative.empty() || relative == "." || relative == ".." || relative.string().rfind(".." + std::string(1, std::filesystem::path::preferred_separator), 0) == 0) {
        return false;
    }
    return std::filesystem::is_regular_file(candidate, error) && !error;
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
    const DisplayNameResolution resolution = ResolveByDisplayName(displayName);
    return resolution.status == DisplayNameResolutionStatus::Resolved ? resolution.app : nullptr;
}

DisplayNameResolution AppRegistry::ResolveByDisplayName(const std::string& displayName,
                                                        const std::string& architecture,
                                                        bool includeTemporaryDevelopment) const {
    DisplayNameResolution resolution;
    if (displayName.empty()) {
        resolution.reason = "Display name is empty";
        return resolution;
    }

    const std::string currentArchitecture = architecture.empty() ? "amd64" : architecture;
    for (const RegisteredApp& app : m_apps) {
        if (app.manifest.displayName != displayName) continue;

        DisplayNameMatch match;
        match.app = &app;
        match.sourcePriority = DisplayNameSourcePriority(app.sourceKind);
        match.eligible = true;

        if (app.manifest.id.empty()) {
            match.eligible = false;
            match.reason = "missing canonical application id";
        } else if (app.manifest.displayName.empty()) {
            match.eligible = false;
            match.reason = "missing display name";
        } else if (app.temporaryDevelopment && !includeTemporaryDevelopment) {
            match.eligible = false;
            match.reason = "temporary development registrations require an explicit development route";
        } else {
            const AppEntry* entry = app.FindCompatibleEntry(currentArchitecture);
            if (!entry) {
                match.eligible = false;
                match.reason = "no compatible launch entry";
            } else if ((app.manifest.kind == AppKind::NativeElf || app.manifest.kind == AppKind::GXAppPackage) &&
                       (entry->path.empty() || app.appDirectory.empty())) {
                match.eligible = false;
                match.reason = "entry path is unavailable";
            } else if ((app.manifest.kind == AppKind::NativeElf || app.manifest.kind == AppKind::GXAppPackage) &&
                       !entryPathIsContainedAndPresent(app, *entry)) {
                match.eligible = false;
                match.reason = "entry path is missing or outside the application directory";
            }
        }

        resolution.matches.push_back(std::move(match));
    }

    std::sort(resolution.matches.begin(), resolution.matches.end(), [](const DisplayNameMatch& left, const DisplayNameMatch& right) {
        if (left.sourcePriority != right.sourcePriority) return left.sourcePriority < right.sourcePriority;
        const std::string leftId = left.app ? left.app->manifest.id : std::string();
        const std::string rightId = right.app ? right.app->manifest.id : std::string();
        if (leftId != rightId) return leftId < rightId;
        const std::string leftPath = left.app ? left.app->manifestPath.generic_string() : std::string();
        const std::string rightPath = right.app ? right.app->manifestPath.generic_string() : std::string();
        return leftPath < rightPath;
    });

    int bestPriority = std::numeric_limits<int>::max();
    size_t eligibleCount = 0;
    for (const DisplayNameMatch& match : resolution.matches) {
        if (!match.eligible) continue;
        bestPriority = std::min(bestPriority, match.sourcePriority);
    }
    if (bestPriority == std::numeric_limits<int>::max()) {
        resolution.reason = "No eligible display-name registration";
        return resolution;
    }

    const DisplayNameMatch* selected = nullptr;
    for (const DisplayNameMatch& match : resolution.matches) {
        if (!match.eligible || match.sourcePriority != bestPriority) continue;
        ++eligibleCount;
        selected = &match;
    }

    if (eligibleCount == 1 && selected) {
        resolution.status = DisplayNameResolutionStatus::Resolved;
        resolution.app = selected->app;
        resolution.reason = "Resolved by explicit source priority and stable canonical-id ordering";
        return resolution;
    }

    resolution.status = DisplayNameResolutionStatus::Ambiguous;
    resolution.reason = "Multiple equally eligible display-name registrations at source priority " + std::to_string(bestPriority);
    return resolution;
}

const AppEntry* AppRegistry::FindCompatibleEntry(const std::string& appId, const std::string& currentArchitecture) const {
    const RegisteredApp* app = FindById(appId);
    return app ? app->FindCompatibleEntry(currentArchitecture) : nullptr;
}

std::vector<AppRegistrySource> AppRegistry::DefaultSources() {
    // Prefer the checkout-local package layout when developing the hosted
    // Server from a source checkout. Production-style hosted roots still use
    // /Apps when no local Apps directory is present.
    const std::filesystem::path packageRoot = std::filesystem::exists("Apps")
        ? std::filesystem::path("Apps")
        : std::filesystem::path("/Apps");
    return {
        { AppSourceKind::SystemApps, "/system/apps" },
        { AppSourceKind::SystemApps, "sdk/samples" },
        { AppSourceKind::SystemApps, "examples/apps" },
        { AppSourceKind::Package, packageRoot },
        { AppSourceKind::UserApps, "/users/default/apps" }
    };
}

bool AppRegistry::RegisterTemporaryDevelopmentApp(const RegisteredApp& app, std::string& error) {
    error.clear();
    if (app.manifest.id.empty() || app.manifest.displayName.empty() ||
        app.sourceKind != AppSourceKind::DevelopmentTemporary || !app.temporaryDevelopment ||
        app.temporaryOwnerRuntimeId == 0 || app.temporaryGeneration == 0) {
        error = "invalid temporary development registration";
        return false;
    }

    auto existing = m_appsById.find(app.manifest.id);
    if (existing != m_appsById.end()) {
        const RegisteredApp& current = m_apps[existing->second];
        if (!current.temporaryDevelopment) {
            error = "APPLICATION_ID_INSTALLED";
            return false;
        }
        if (current.temporaryOwnerRuntimeId != app.temporaryOwnerRuntimeId) {
            error = "APPLICATION_ID_IN_USE";
            return false;
        }
        error = "DEPLOYMENT_ALREADY_ACTIVE";
        return false;
    }

    m_appsById[app.manifest.id] = m_apps.size();
    m_apps.push_back(app);
    return true;
}

bool AppRegistry::UnregisterTemporaryDevelopmentApp(const std::string& appId, uint64_t ownerRuntimeId, uint64_t generation) {
    auto existing = m_appsById.find(appId);
    if (existing == m_appsById.end()) return false;
    const size_t index = existing->second;
    const RegisteredApp& current = m_apps[index];
    if (!current.temporaryDevelopment || current.temporaryOwnerRuntimeId != ownerRuntimeId || current.temporaryGeneration != generation) return false;

    m_apps.erase(m_apps.begin() + static_cast<std::ptrdiff_t>(index));
    m_appsById.clear();
    for (size_t i = 0; i < m_apps.size(); ++i) m_appsById[m_apps[i].manifest.id] = i;
    return true;
}

const char* AppRegistry::ToString(AppSourceKind kind) {
    switch (kind) {
    case AppSourceKind::BuiltIn: return "BuiltIn";
    case AppSourceKind::SystemApps: return "SystemApps";
    case AppSourceKind::UserApps: return "UserApps";
    case AppSourceKind::Package: return "Package";
    case AppSourceKind::DevelopmentTemporary: return "DevelopmentTemporary";
    default: return "Unknown";
    }
}

const char* AppRegistry::ToString(DisplayNameResolutionStatus status) {
    switch (status) {
    case DisplayNameResolutionStatus::Resolved: return "Resolved";
    case DisplayNameResolutionStatus::Ambiguous: return "Ambiguous";
    case DisplayNameResolutionStatus::NotFound:
    default: return "NotFound";
    }
}

int AppRegistry::DisplayNameSourcePriority(AppSourceKind kind) {
    // Persistent packaged identity is authoritative for compatibility labels.
    // Built-ins remain ahead of SDK/validation manifests, while temporary
    // development records are available only through their canonical route.
    switch (kind) {
    case AppSourceKind::Package: return 0;
    case AppSourceKind::BuiltIn: return 1;
    case AppSourceKind::UserApps: return 2;
    case AppSourceKind::SystemApps: return 3;
    case AppSourceKind::DevelopmentTemporary: return 4;
    default: return 5;
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
