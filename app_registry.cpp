#include "app_registry.h"

#include "app_manifest_loader.h"
#include "app_manifest_validator.h"
#include "built_in_app_metadata.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace gxos {
namespace apps {

bool NormalizeDocumentExtension(const std::string& extension, std::string& normalized) {
    normalized.clear();
    if (extension.size() > kAppModelMaxFileExtensionBytes) return false;
    normalized = extension;
    for (char& ch : normalized) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    if (normalized.size() < 2 || normalized[0] != '.') {
        normalized.clear();
        return false;
    }
    for (size_t i = 1; i < normalized.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(normalized[i]);
        const bool asciiAlphaNumeric = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        if (!asciiAlphaNumeric && normalized[i] != '_' && normalized[i] != '-') {
            normalized.clear();
            return false;
        }
    }
    return true;
}

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

std::string builtInDefaultAppId(const std::string& extension) {
    if (extension == ".txt" || extension == ".log" || extension == ".ini" || extension == ".cfg") {
        return "gxos.builtin.notepad";
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") return "gxos.builtin.imageviewer";
    if (extension == ".html" || extension == ".htm") return "guidexos.navigator";
    return std::string();
}

std::string builtInDefaultProtocolAppId(const std::string& scheme) {
    return (scheme == "http" || scheme == "https") ? "guidexos.navigator" : std::string();
}

enum class PathExtensionStatus { Valid, NoExtension, InvalidPath, InvalidExtension };

PathExtensionStatus extensionFromPath(const std::string& path, std::string& extension) {
    extension.clear();
    if (!IsValidDocumentActivationPath(path)) return PathExtensionStatus::InvalidPath;

    const size_t separator = path.find_last_of("/\\");
    const size_t baseNameStart = separator == std::string::npos ? 0 : separator + 1;
    if (baseNameStart >= path.size()) return PathExtensionStatus::InvalidPath;
    const std::string baseName = path.substr(baseNameStart);
    const size_t dot = baseName.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return PathExtensionStatus::NoExtension;
    if (dot + 1 >= baseName.size()) return PathExtensionStatus::InvalidExtension;

    const std::string rawExtension = baseName.substr(dot);
    if (!NormalizeDocumentExtension(rawExtension, extension)) return PathExtensionStatus::InvalidExtension;
    return PathExtensionStatus::Valid;
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

    if (metadata.actionIds && metadata.actionLabels && metadata.actionCount <= kAppModelMaxActionsPerApp) {
        for (size_t i = 0; i < metadata.actionCount; ++i) {
            if (!metadata.actionIds[i] || !metadata.actionLabels[i]) continue;
            app.manifest.actions.push_back({ metadata.actionIds[i], metadata.actionLabels[i] });
        }
        app.appActionBackendAvailable = !app.manifest.actions.empty();
    }

    AppEntry entry;
    entry.architecture = "any";
    entry.path = std::string("builtin/") + app.manifest.displayName;
    entry.entryPoint = BuiltInAppCanonicalLaunchName(metadata);
    entry.abi = "guidexos-desktop-service-v1";
    entry.runtime = "builtin-hosted";
    app.manifest.entries.push_back(entry);

    if (app.manifest.id == "gxos.builtin.notepad") {
        app.manifest.supportsDocumentActivation = true;
        app.documentActivationBackendAvailable = true;
        app.manifest.fileAssociations = {
            { ".txt", "text/plain", "Plain text document" },
            { ".log", "text/plain", "Log file" },
            { ".ini", "text/plain", "INI configuration file" },
            { ".cfg", "text/plain", "CFG configuration file" }
        };
    } else if (app.manifest.id == "gxos.builtin.imageviewer") {
        app.manifest.supportsDocumentActivation = true;
        app.documentActivationBackendAvailable = true;
        app.manifest.fileAssociations = {
            { ".png", "image/png", "PNG image" },
            { ".jpg", "image/jpeg", "JPEG image" },
            { ".jpeg", "image/jpeg", "JPEG image" }
        };
    } else if (app.manifest.id == "guidexos.navigator") {
        app.manifest.supportsDocumentActivation = true;
        app.documentActivationBackendAvailable = true;
        app.manifest.fileAssociations = {
            { ".html", "text/html", "HTML document" },
            { ".htm", "text/html", "HTML document" }
        };
        app.manifest.protocols = { "http", "https" };
        app.manifest.supportsProtocolActivation = true;
        app.protocolActivationBackendAvailable = true;
    } else if (app.manifest.id == "gxos.builtin.fileexplorer") {
        app.manifest.supportsFolderActivation = true;
        app.folderActivationBackendAvailable = true;
    }

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

AppRegistry::AppRegistry(bool preferSystemAppsOverUserApps, const std::filesystem::path& defaultHandlerStorePath)
    : m_preferSystemAppsOverUserApps(preferSystemAppsOverUserApps), m_sources(DefaultSources()),
      m_defaultHandlerStore(defaultHandlerStorePath.string()) {
}

bool AppRegistry::NormalizeDocumentExtension(const std::string& extension, std::string& normalized) {
    return apps::NormalizeDocumentExtension(extension, normalized);
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
    m_fileAssociations.clear();
    m_fileAssociationCapacityExceeded = false;
    m_protocolHandlers.clear();
    m_protocolHandlerCapacityExceeded = false;
    m_folderHandlers.clear();
    m_folderHandlerCapacityExceeded = false;
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

            if (result.scannedManifestCount >= kAppModelMaxRegistryApps) {
                result.invalidApps.push_back(makeIssue(source.kind, entry.path(), std::string(),
                    { "App Model manifest scan capacity reached" }));
                break;
            }
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
    RebuildFileAssociations();
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
    RebuildFileAssociations();
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
    RebuildFileAssociations();
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

DocumentHandlerList AppRegistry::EnumerateCapableHandlers(const std::string& requestedExtension) const {
    DocumentHandlerList result;
    if (!NormalizeDocumentExtension(requestedExtension, result.extension)) return result;
    result.validExtension = true;

    std::vector<DocumentHandlerInfo> candidates;
    candidates.reserve(std::min(m_fileAssociations.size(), kAppModelMaxFileAssociationRecords));
    for (const FileAssociationRecord& record : m_fileAssociations) {
        if (record.extension != result.extension) continue;
        auto appIt = m_appsById.find(record.appId);
        const RegisteredApp* app = appIt == m_appsById.end() || appIt->second >= m_apps.size()
            ? nullptr : &m_apps[appIt->second];

        DocumentHandlerInfo handler;
        handler.appId = record.appId;
        handler.registrationOwner = record.registrationOwner;
        handler.registrationGeneration = record.registrationGeneration;
        handler.registrationCurrent = app &&
            app->temporaryOwnerRuntimeId == record.registrationOwner &&
            app->temporaryGeneration == record.registrationGeneration;
        if (app) {
            handler.displayName = app->manifest.displayName;
            handler.supportsDocumentActivation = record.supportsDocumentActivation && app->manifest.supportsDocumentActivation;
            handler.backendAvailable = record.backendAvailable && app->documentActivationBackendAvailable;
        } else {
            handler.supportsDocumentActivation = record.supportsDocumentActivation;
            handler.backendAvailable = false;
        }
        handler.available = handler.registrationCurrent && handler.supportsDocumentActivation && handler.backendAvailable;

        const bool duplicate = std::any_of(candidates.begin(), candidates.end(), [&](const DocumentHandlerInfo& existing) {
            return existing.appId == handler.appId &&
                existing.registrationOwner == handler.registrationOwner &&
                existing.registrationGeneration == handler.registrationGeneration;
        });
        if (!duplicate) candidates.push_back(std::move(handler));
    }

    result.declaredHandlerCount = candidates.size();
    result.availableHandlerCount = static_cast<size_t>(std::count_if(candidates.begin(), candidates.end(),
        [](const DocumentHandlerInfo& handler) { return handler.available; }));

    const std::string configuredOverride = m_defaultHandlerStore.Find(result.extension)
        ? m_defaultHandlerStore.Find(result.extension)->appId : std::string();
    const std::string preferredDefault = builtInDefaultAppId(result.extension);
    auto matchesPreferred = [&](const DocumentHandlerInfo& handler) {
        return !preferredDefault.empty() && handler.appId == preferredDefault;
    };
    auto durableAndAvailable = [&](const DocumentHandlerInfo& handler) {
        if (!handler.available) return false;
        const RegisteredApp* app = FindById(handler.appId);
        return app && !app->temporaryDevelopment && app->sourceKind != AppSourceKind::DevelopmentTemporary;
    };
    auto defaultIt = candidates.end();
    if (!configuredOverride.empty()) {
        defaultIt = std::find_if(candidates.begin(), candidates.end(), [&](const DocumentHandlerInfo& handler) {
            return handler.appId == configuredOverride && durableAndAvailable(handler);
        });
    }
    if (defaultIt == candidates.end()) {
        defaultIt = std::find_if(candidates.begin(), candidates.end(), [&](const DocumentHandlerInfo& handler) {
            return matchesPreferred(handler) && handler.available;
        });
    }
    if (defaultIt == candidates.end()) {
        defaultIt = std::find_if(candidates.begin(), candidates.end(),
            [](const DocumentHandlerInfo& handler) { return handler.available; });
    }
    if (defaultIt == candidates.end() && !preferredDefault.empty()) {
        defaultIt = std::find_if(candidates.begin(), candidates.end(), matchesPreferred);
    }
    if (defaultIt == candidates.end() && !candidates.empty()) defaultIt = candidates.begin();
    if (defaultIt != candidates.end()) defaultIt->isDefault = true;

    std::sort(candidates.begin(), candidates.end(), [](const DocumentHandlerInfo& left, const DocumentHandlerInfo& right) {
        if (left.isDefault != right.isDefault) return left.isDefault;
        if (left.available != right.available) return left.available;
        if (left.appId != right.appId) return left.appId < right.appId;
        if (left.registrationOwner != right.registrationOwner) return left.registrationOwner < right.registrationOwner;
        return left.registrationGeneration < right.registrationGeneration;
    });

    result.count = std::min(candidates.size(), result.handlers.size());
    result.truncated = candidates.size() > result.handlers.size();
    for (size_t i = 0; i < result.count; ++i) result.handlers[i] = std::move(candidates[i]);
    return result;
}

DocumentHandlerList AppRegistry::EnumerateCapableHandlersForPath(const std::string& path) const {
    std::string extension;
    if (extensionFromPath(path, extension) != PathExtensionStatus::Valid) return DocumentHandlerList{};
    return EnumerateCapableHandlers(extension);
}

ProtocolHandlerList AppRegistry::EnumerateCapableProtocolHandlers(const std::string& requestedScheme) const {
    ProtocolHandlerList result;
    if (!NormalizeProtocolScheme(requestedScheme, result.scheme)) return result;
    result.validScheme = true;

    std::vector<ProtocolHandlerInfo> candidates;
    candidates.reserve(std::min(m_protocolHandlers.size(), kAppModelMaxProtocolRecords));
    for (const ProtocolHandlerRecord& record : m_protocolHandlers) {
        if (record.scheme != result.scheme) continue;
        const auto appIt = m_appsById.find(record.appId);
        const RegisteredApp* app = appIt == m_appsById.end() || appIt->second >= m_apps.size()
            ? nullptr : &m_apps[appIt->second];
        ProtocolHandlerInfo handler;
        handler.appId = record.appId;
        handler.registrationOwner = record.registrationOwner;
        handler.registrationGeneration = record.registrationGeneration;
        handler.registrationCurrent = app && app->temporaryOwnerRuntimeId == record.registrationOwner &&
            app->temporaryGeneration == record.registrationGeneration;
        if (app) {
            handler.displayName = app->manifest.displayName;
            handler.supportsProtocolActivation = record.supportsProtocolActivation && app->manifest.supportsProtocolActivation;
            handler.backendAvailable = record.backendAvailable && app->protocolActivationBackendAvailable;
        } else {
            handler.supportsProtocolActivation = record.supportsProtocolActivation;
        }
        handler.available = handler.registrationCurrent && handler.supportsProtocolActivation && handler.backendAvailable;
        const bool duplicate = std::any_of(candidates.begin(), candidates.end(), [&](const ProtocolHandlerInfo& existing) {
            return existing.appId == handler.appId && existing.registrationOwner == handler.registrationOwner &&
                existing.registrationGeneration == handler.registrationGeneration;
        });
        if (!duplicate) candidates.push_back(std::move(handler));
    }

    result.declaredHandlerCount = candidates.size();
    result.availableHandlerCount = static_cast<size_t>(std::count_if(candidates.begin(), candidates.end(),
        [](const ProtocolHandlerInfo& handler) { return handler.available; }));
    const DefaultHandlerOverride* configured = m_defaultHandlerStore.Find(
        result.scheme, DefaultHandlerOverride::KeyKind::Protocol);
    const std::string configuredId = configured ? configured->appId : std::string();
    const std::string preferredId = builtInDefaultProtocolAppId(result.scheme);
    const auto isDurableAvailable = [&](const ProtocolHandlerInfo& handler) {
        if (!handler.available) return false;
        const RegisteredApp* app = FindById(handler.appId);
        return app && !app->temporaryDevelopment && app->sourceKind != AppSourceKind::DevelopmentTemporary;
    };
    auto selected = candidates.end();
    if (!configuredId.empty()) {
        selected = std::find_if(candidates.begin(), candidates.end(), [&](const ProtocolHandlerInfo& handler) {
            return handler.appId == configuredId && isDurableAvailable(handler);
        });
    }
    if (selected == candidates.end() && !preferredId.empty()) {
        selected = std::find_if(candidates.begin(), candidates.end(), [&](const ProtocolHandlerInfo& handler) {
            return handler.appId == preferredId && handler.available;
        });
    }
    if (selected == candidates.end()) {
        selected = std::find_if(candidates.begin(), candidates.end(),
            [](const ProtocolHandlerInfo& handler) { return handler.available; });
    }
    if (selected == candidates.end() && !preferredId.empty()) {
        selected = std::find_if(candidates.begin(), candidates.end(), [&](const ProtocolHandlerInfo& handler) {
            return handler.appId == preferredId;
        });
    }
    if (selected == candidates.end() && !candidates.empty()) selected = candidates.begin();
    if (selected != candidates.end()) selected->isDefault = true;

    std::sort(candidates.begin(), candidates.end(), [](const ProtocolHandlerInfo& left, const ProtocolHandlerInfo& right) {
        if (left.isDefault != right.isDefault) return left.isDefault;
        if (left.available != right.available) return left.available;
        if (left.appId != right.appId) return left.appId < right.appId;
        if (left.registrationOwner != right.registrationOwner) return left.registrationOwner < right.registrationOwner;
        return left.registrationGeneration < right.registrationGeneration;
    });
    result.count = std::min(candidates.size(), result.handlers.size());
    result.truncated = candidates.size() > result.handlers.size();
    for (size_t i = 0; i < result.count; ++i) result.handlers[i] = std::move(candidates[i]);
    return result;
}

UriActivationResolution AppRegistry::ResolveUriActivation(const std::string& uri) const {
    UriActivationResolution result;
    if (uri.empty() || uri.size() > kAppModelMaxUriBytes ||
        std::any_of(uri.begin(), uri.end(), [](unsigned char ch) { return ch == 0 || ch < 0x20u || ch == 0x7fu; })) {
        result.status = UriActivationResolutionStatus::InvalidUri;
        result.reason = "URI is empty, contains control characters, or exceeds the URI byte bound";
        return result;
    }
    if (!GetUriActivationScheme(uri, result.scheme)) {
        result.status = UriActivationResolutionStatus::InvalidScheme;
        result.reason = "URI must start with a valid bounded ASCII protocol scheme and colon";
        return result;
    }
    const ProtocolHandlerList handlers = EnumerateCapableProtocolHandlers(result.scheme);
    if (!handlers.validScheme) {
        result.status = UriActivationResolutionStatus::InvalidScheme;
        result.reason = "protocol scheme is malformed or over capacity";
        return result;
    }
    const ProtocolHandlerInfo* selected = nullptr;
    for (size_t i = 0; i < handlers.count; ++i) {
        if (handlers.handlers[i].isDefault) {
            selected = &handlers.handlers[i];
            break;
        }
    }
    if (!selected) {
        result.status = m_protocolHandlerCapacityExceeded
            ? UriActivationResolutionStatus::RegistryCapacityExceeded : UriActivationResolutionStatus::NoHandler;
        result.reason = m_protocolHandlerCapacityExceeded
            ? "protocol registry capacity was reached; unresolved schemes fail closed"
            : "no registered application declared this protocol";
        return result;
    }
    return ResolveUriActivation(*selected, uri);
}

UriActivationResolution AppRegistry::ResolveUriActivation(const ProtocolHandlerInfo& handler,
                                                           const std::string& uri) const {
    UriActivationResolution result;
    if (uri.empty() || uri.size() > kAppModelMaxUriBytes ||
        std::any_of(uri.begin(), uri.end(), [](unsigned char ch) { return ch == 0 || ch < 0x20u || ch == 0x7fu; })) {
        result.status = UriActivationResolutionStatus::InvalidUri;
        result.reason = "URI is empty, contains control characters, or exceeds the URI byte bound";
        return result;
    }
    if (!GetUriActivationScheme(uri, result.scheme)) {
        result.status = UriActivationResolutionStatus::InvalidScheme;
        result.reason = "URI must start with a valid bounded ASCII protocol scheme and colon";
        return result;
    }
    if (handler.appId.empty() || handler.appId.size() > kAppModelMaxAppIdBytes) {
        result.status = UriActivationResolutionStatus::HandlerMissing;
        result.reason = "canonical application ID is empty or over capacity";
        return result;
    }
    const RegisteredApp* app = FindById(handler.appId);
    if (!app) {
        result.status = UriActivationResolutionStatus::HandlerMissing;
        result.reason = "selected canonical application ID is not registered";
        return result;
    }
    result.appId = handler.appId;
    result.displayName = app->manifest.displayName;
    if (app->temporaryOwnerRuntimeId != handler.registrationOwner ||
        app->temporaryGeneration != handler.registrationGeneration) {
        result.status = UriActivationResolutionStatus::HandlerStale;
        result.reason = "selected handler registration owner or generation changed after enumeration";
        return result;
    }
    const auto declaration = std::find_if(m_protocolHandlers.begin(), m_protocolHandlers.end(), [&](const ProtocolHandlerRecord& record) {
        return record.scheme == result.scheme && record.appId == handler.appId &&
            record.registrationOwner == handler.registrationOwner && record.registrationGeneration == handler.registrationGeneration;
    });
    if (declaration == m_protocolHandlers.end() || !app->manifest.supportsProtocolActivation ||
        !declaration->supportsProtocolActivation) {
        result.status = UriActivationResolutionStatus::HandlerDoesNotSupportProtocol;
        result.reason = "selected application does not currently declare support for this protocol";
        return result;
    }
    if (!app->protocolActivationBackendAvailable || !declaration->backendAvailable) {
        result.status = UriActivationResolutionStatus::HandlerUnavailable;
        result.reason = "current runtime has no protocol activation dispatcher for this application";
        return result;
    }
    result.status = UriActivationResolutionStatus::Resolved;
    result.activation.kind = AppActivationKind::Uri;
    result.activation.appId = app->manifest.id;
    result.activation.uri = uri;
    result.activation.registrationOwner = app->temporaryOwnerRuntimeId;
    result.activation.registrationGeneration = app->temporaryGeneration;
    result.reason = "URI resolved to the current capable registration with an owned URI value";
    return result;
}

ProtocolDefaultHandlerInfo AppRegistry::GetDefaultProtocolHandlerInfo(const std::string& requestedScheme) const {
    ProtocolDefaultHandlerInfo info;
    if (!NormalizeProtocolScheme(requestedScheme, info.scheme)) return info;
    info.builtInDefaultAppId = builtInDefaultProtocolAppId(info.scheme);
    const DefaultHandlerOverride* configured = m_defaultHandlerStore.Find(
        info.scheme, DefaultHandlerOverride::KeyKind::Protocol);
    if (configured) {
        info.configuredOverrideAppId = configured->appId;
        const RegisteredApp* app = FindById(configured->appId);
        if (!app) info.configuredStatus = ConfiguredDefaultHandlerStatus::RegistrationMissing;
        else if (app->temporaryDevelopment || app->sourceKind == AppSourceKind::DevelopmentTemporary)
            info.configuredStatus = ConfiguredDefaultHandlerStatus::NonDurableRegistration;
        else if (!HasDeclaredProtocol(*app, info.scheme))
            info.configuredStatus = ConfiguredDefaultHandlerStatus::CapabilityMissing;
        else if (!app->manifest.supportsProtocolActivation)
            info.configuredStatus = ConfiguredDefaultHandlerStatus::ProtocolActivationUnsupported;
        else if (!app->protocolActivationBackendAvailable)
            info.configuredStatus = ConfiguredDefaultHandlerStatus::TemporarilyUnavailable;
        else {
            const bool indexed = std::any_of(m_protocolHandlers.begin(), m_protocolHandlers.end(), [&](const ProtocolHandlerRecord& record) {
                return record.scheme == info.scheme && record.appId == configured->appId &&
                    record.registrationOwner == app->temporaryOwnerRuntimeId && record.registrationGeneration == app->temporaryGeneration;
            });
            info.configuredStatus = indexed ? ConfiguredDefaultHandlerStatus::Available
                : (m_protocolHandlerCapacityExceeded ? ConfiguredDefaultHandlerStatus::RegistryCapacityExceeded
                                                     : ConfiguredDefaultHandlerStatus::CapabilityMissing);
        }
    }
    const ProtocolHandlerList handlers = EnumerateCapableProtocolHandlers(info.scheme);
    for (size_t i = 0; i < handlers.count; ++i) {
        if (handlers.handlers[i].isDefault) {
            info.effectiveDefaultAppId = handlers.handlers[i].appId;
            info.effectiveDefaultAvailable = handlers.handlers[i].available;
            break;
        }
    }
    return info;
}

DefaultHandlerMutationResult AppRegistry::SetDefaultProtocolHandler(const std::string& requestedScheme,
                                                                    const std::string& canonicalAppId) {
    DefaultHandlerMutationResult result;
    std::string scheme;
    if (!NormalizeProtocolScheme(requestedScheme, scheme)) {
        result.status = DefaultHandlerMutationStatus::InvalidProtocol;
        result.reason = "protocol scheme is malformed or over capacity";
        return result;
    }
    result.key = scheme;
    result.protocolKey = true;
    result.appId = canonicalAppId;
    std::string reloadError;
    if (!m_defaultHandlerStore.Reload(reloadError)) {
        result.status = DefaultHandlerMutationStatus::PersistenceFailure;
        result.reason = reloadError.empty() ? "authoritative default-handler configuration could not be reread" : reloadError;
        return result;
    }
    const RegisteredApp* app = FindById(canonicalAppId);
    if (!app) {
        result.status = DefaultHandlerMutationStatus::UnknownApplication;
        result.reason = "canonical App Model ID is not currently registered";
        return result;
    }
    if (app->temporaryDevelopment || app->sourceKind == AppSourceKind::DevelopmentTemporary) {
        result.status = DefaultHandlerMutationStatus::NonDurableRegistration;
        result.reason = "temporary development registrations cannot become durable machine defaults";
        return result;
    }
    if (!HasDeclaredProtocol(*app, scheme)) {
        result.status = DefaultHandlerMutationStatus::ProtocolCapabilityMissing;
        result.reason = "application does not currently declare this normalized protocol";
        return result;
    }
    if (!app->manifest.supportsProtocolActivation) {
        result.status = DefaultHandlerMutationStatus::ProtocolActivationUnsupported;
        result.reason = "application does not declare protocol activation support";
        return result;
    }
    if (!app->protocolActivationBackendAvailable) {
        result.status = DefaultHandlerMutationStatus::HandlerUnavailable;
        result.reason = "current hosted backend cannot dispatch protocol activation to this application";
        return result;
    }
    const bool indexed = std::any_of(m_protocolHandlers.begin(), m_protocolHandlers.end(), [&](const ProtocolHandlerRecord& record) {
        return record.scheme == scheme && record.appId == canonicalAppId &&
            record.registrationOwner == app->temporaryOwnerRuntimeId && record.registrationGeneration == app->temporaryGeneration;
    });
    if (!indexed) {
        result.status = m_protocolHandlerCapacityExceeded ? DefaultHandlerMutationStatus::CapacityExceeded
                                                         : DefaultHandlerMutationStatus::ProtocolCapabilityMissing;
        result.reason = m_protocolHandlerCapacityExceeded
            ? "the bounded AppRegistry protocol index cannot safely resolve this capability"
            : "the declared capability is not present in the current AppRegistry index";
        return result;
    }
    std::vector<DefaultHandlerOverride> updated = m_defaultHandlerStore.Overrides();
    auto existing = std::find_if(updated.begin(), updated.end(), [&](const DefaultHandlerOverride& entry) {
        return entry.keyKind == DefaultHandlerOverride::KeyKind::Protocol && entry.extension == scheme;
    });
    if (existing == updated.end()) {
        if (updated.size() >= kAppModelMaxDefaultHandlerOverrides) {
            result.status = DefaultHandlerMutationStatus::CapacityExceeded;
            result.reason = "machine default-handler override capacity is full";
            return result;
        }
        updated.push_back({ scheme, canonicalAppId, DefaultHandlerOverride::KeyKind::Protocol });
    } else existing->appId = canonicalAppId;
    std::string error;
    if (!m_defaultHandlerStore.Commit(updated, error)) {
        result.status = m_defaultHandlerStore.Diagnostics().lastWriteStatus == DefaultHandlerStoreWriteStatus::VerificationFailed
            ? DefaultHandlerMutationStatus::VerificationFailure : DefaultHandlerMutationStatus::PersistenceFailure;
        result.reason = error;
        return result;
    }
    const ProtocolDefaultHandlerInfo verified = GetDefaultProtocolHandlerInfo(scheme);
    if (verified.configuredOverrideAppId != canonicalAppId || verified.effectiveDefaultAppId != canonicalAppId ||
        verified.configuredStatus != ConfiguredDefaultHandlerStatus::Available) {
        result.status = DefaultHandlerMutationStatus::VerificationFailure;
        result.reason = "durable protocol override reread did not resolve to the requested current handler";
    }
    return result;
}

DefaultHandlerMutationResult AppRegistry::ClearDefaultProtocolHandler(const std::string& requestedScheme) {
    DefaultHandlerMutationResult result;
    std::string scheme;
    if (!NormalizeProtocolScheme(requestedScheme, scheme)) {
        result.status = DefaultHandlerMutationStatus::InvalidProtocol;
        result.reason = "protocol scheme is malformed or over capacity";
        return result;
    }
    result.key = scheme;
    result.protocolKey = true;
    std::string reloadError;
    if (!m_defaultHandlerStore.Reload(reloadError)) {
        result.status = DefaultHandlerMutationStatus::PersistenceFailure;
        result.reason = reloadError.empty() ? "authoritative default-handler configuration could not be reread" : reloadError;
        return result;
    }
    std::vector<DefaultHandlerOverride> updated = m_defaultHandlerStore.Overrides();
    const size_t oldSize = updated.size();
    updated.erase(std::remove_if(updated.begin(), updated.end(), [&](const DefaultHandlerOverride& entry) {
        return entry.keyKind == DefaultHandlerOverride::KeyKind::Protocol && entry.extension == scheme;
    }), updated.end());
    if (updated.size() != oldSize) {
        std::string error;
        if (!m_defaultHandlerStore.Commit(updated, error)) {
            result.status = m_defaultHandlerStore.Diagnostics().lastWriteStatus == DefaultHandlerStoreWriteStatus::VerificationFailed
                ? DefaultHandlerMutationStatus::VerificationFailure : DefaultHandlerMutationStatus::PersistenceFailure;
            result.reason = error;
            return result;
        }
    }
    if (m_defaultHandlerStore.Find(scheme, DefaultHandlerOverride::KeyKind::Protocol)) {
        result.status = DefaultHandlerMutationStatus::VerificationFailure;
        result.reason = "cleared protocol override remained present after authoritative reread";
    }
    return result;
}

std::vector<std::string> AppRegistry::GetKnownProtocols() const {
    std::vector<std::string> protocols;
    protocols.reserve(m_protocolHandlers.size() + 2);
    for (const ProtocolHandlerRecord& record : m_protocolHandlers) protocols.push_back(record.scheme);
    for (const DefaultHandlerOverride& record : m_defaultHandlerStore.Overrides())
        if (record.keyKind == DefaultHandlerOverride::KeyKind::Protocol) protocols.push_back(record.extension);
    protocols.push_back("http");
    protocols.push_back("https");
    std::sort(protocols.begin(), protocols.end());
    protocols.erase(std::unique(protocols.begin(), protocols.end()), protocols.end());
    return protocols;
}

bool AppRegistry::IsUriActivationCurrent(const AppActivationContext& activation) const {
    if (activation.kind != AppActivationKind::Uri || activation.appId.empty() ||
        activation.appId.size() > kAppModelMaxAppIdBytes || !IsValidUriActivationUri(activation.uri)) return false;
    ProtocolHandlerInfo expected;
    expected.appId = activation.appId;
    expected.registrationOwner = activation.registrationOwner;
    expected.registrationGeneration = activation.registrationGeneration;
    const UriActivationResolution current = ResolveUriActivation(expected, activation.uri);
    return current.launchable() && current.appId == activation.appId &&
        current.activation.registrationOwner == activation.registrationOwner &&
        current.activation.registrationGeneration == activation.registrationGeneration && current.activation.uri == activation.uri;
}

FolderHandlerList AppRegistry::EnumerateCapableFolderHandlers() const {
    FolderHandlerList result;
    std::vector<FolderHandlerInfo> candidates;
    candidates.reserve(std::min(m_folderHandlers.size(), kAppModelMaxFolderHandlerRecords));
    for (const FolderHandlerRecord& record : m_folderHandlers) {
        const auto appIt = m_appsById.find(record.appId);
        const RegisteredApp* app = appIt == m_appsById.end() || appIt->second >= m_apps.size()
            ? nullptr : &m_apps[appIt->second];
        FolderHandlerInfo handler;
        handler.appId = record.appId;
        handler.registrationOwner = record.registrationOwner;
        handler.registrationGeneration = record.registrationGeneration;
        handler.registrationCurrent = app && app->temporaryOwnerRuntimeId == record.registrationOwner &&
            app->temporaryGeneration == record.registrationGeneration;
        if (app) {
            handler.displayName = app->manifest.displayName;
            handler.supportsFolderActivation = record.supportsFolderActivation && app->manifest.supportsFolderActivation;
            handler.backendAvailable = record.backendAvailable && app->folderActivationBackendAvailable;
        } else {
            handler.supportsFolderActivation = record.supportsFolderActivation;
        }
        handler.available = handler.registrationCurrent && handler.supportsFolderActivation && handler.backendAvailable;
        const bool duplicate = std::any_of(candidates.begin(), candidates.end(), [&](const FolderHandlerInfo& existing) {
            return existing.appId == handler.appId && existing.registrationOwner == handler.registrationOwner &&
                existing.registrationGeneration == handler.registrationGeneration;
        });
        if (!duplicate) candidates.push_back(std::move(handler));
    }

    result.declaredHandlerCount = candidates.size();
    result.availableHandlerCount = static_cast<size_t>(std::count_if(candidates.begin(), candidates.end(),
        [](const FolderHandlerInfo& handler) { return handler.available; }));
    auto selected = std::find_if(candidates.begin(), candidates.end(), [](const FolderHandlerInfo& handler) {
        return handler.appId == "gxos.builtin.fileexplorer" && handler.available;
    });
    if (selected == candidates.end()) {
        selected = std::find_if(candidates.begin(), candidates.end(),
            [](const FolderHandlerInfo& handler) { return handler.available; });
    }
    if (selected == candidates.end()) {
        selected = std::find_if(candidates.begin(), candidates.end(), [](const FolderHandlerInfo& handler) {
            return handler.appId == "gxos.builtin.fileexplorer";
        });
    }
    if (selected != candidates.end()) selected->isDefault = true;

    std::sort(candidates.begin(), candidates.end(), [](const FolderHandlerInfo& left, const FolderHandlerInfo& right) {
        if (left.isDefault != right.isDefault) return left.isDefault;
        if (left.available != right.available) return left.available;
        if (left.appId != right.appId) return left.appId < right.appId;
        if (left.registrationOwner != right.registrationOwner) return left.registrationOwner < right.registrationOwner;
        return left.registrationGeneration < right.registrationGeneration;
    });
    result.count = std::min(candidates.size(), result.handlers.size());
    result.truncated = candidates.size() > result.handlers.size();
    for (size_t i = 0; i < result.count; ++i) result.handlers[i] = std::move(candidates[i]);
    return result;
}

FolderActivationResolution AppRegistry::ResolveFolderActivation(const std::string& path) const {
    FolderActivationResolution result;
    if (!IsValidFolderActivationPath(path)) {
        result.status = FolderActivationResolutionStatus::InvalidPath;
        result.reason = "folder path is empty, contains control characters, or exceeds the App Model path bound";
        return result;
    }
    const FolderHandlerList handlers = EnumerateCapableFolderHandlers();
    const FolderHandlerInfo* selected = nullptr;
    for (size_t i = 0; i < handlers.count; ++i) {
        if (handlers.handlers[i].isDefault) { selected = &handlers.handlers[i]; break; }
    }
    if (!selected) {
        result.status = m_folderHandlerCapacityExceeded
            ? FolderActivationResolutionStatus::RegistryCapacityExceeded : FolderActivationResolutionStatus::NoHandler;
        result.reason = m_folderHandlerCapacityExceeded
            ? "folder handler registry capacity was reached; activation fails closed"
            : "no registered application declared folder activation support";
        return result;
    }
    return ResolveFolderActivation(*selected, path);
}

FolderActivationResolution AppRegistry::ResolveFolderActivation(const FolderHandlerInfo& handler,
                                                                 const std::string& path) const {
    FolderActivationResolution result;
    if (!IsValidFolderActivationPath(path)) {
        result.status = FolderActivationResolutionStatus::InvalidPath;
        result.reason = "folder path is empty, contains control characters, or exceeds the App Model path bound";
        return result;
    }
    if (handler.appId.empty() || handler.appId.size() > kAppModelMaxAppIdBytes) {
        result.status = FolderActivationResolutionStatus::HandlerMissing;
        result.reason = "canonical application ID is empty or over capacity";
        return result;
    }
    const RegisteredApp* app = FindById(handler.appId);
    if (!app) {
        result.status = FolderActivationResolutionStatus::HandlerMissing;
        result.reason = "selected canonical application ID is not registered";
        return result;
    }
    result.appId = handler.appId;
    result.displayName = app->manifest.displayName;
    if (app->temporaryOwnerRuntimeId != handler.registrationOwner ||
        app->temporaryGeneration != handler.registrationGeneration) {
        result.status = FolderActivationResolutionStatus::HandlerStale;
        result.reason = "selected handler registration owner or generation changed after enumeration";
        return result;
    }
    const auto declaration = std::find_if(m_folderHandlers.begin(), m_folderHandlers.end(), [&](const FolderHandlerRecord& record) {
        return record.appId == handler.appId && record.registrationOwner == handler.registrationOwner &&
            record.registrationGeneration == handler.registrationGeneration;
    });
    if (declaration == m_folderHandlers.end() || !app->manifest.supportsFolderActivation ||
        !declaration->supportsFolderActivation) {
        result.status = FolderActivationResolutionStatus::HandlerDoesNotSupportFolders;
        result.reason = "selected application does not currently declare folder activation support";
        return result;
    }
    if (!app->folderActivationBackendAvailable || !declaration->backendAvailable) {
        result.status = FolderActivationResolutionStatus::HandlerUnavailable;
        result.reason = "current runtime has no folder activation dispatcher for this application";
        return result;
    }
    result.status = FolderActivationResolutionStatus::Resolved;
    result.activation.kind = AppActivationKind::Folder;
    result.activation.appId = app->manifest.id;
    result.activation.folderPath = path;
    result.activation.registrationOwner = app->temporaryOwnerRuntimeId;
    result.activation.registrationGeneration = app->temporaryGeneration;
    result.reason = "folder resolved to the current capable registration with an owned folder path";
    return result;
}

bool AppRegistry::IsFolderActivationCurrent(const AppActivationContext& activation) const {
    if (activation.kind != AppActivationKind::Folder || activation.appId.empty() ||
        !IsValidFolderActivationPath(activation.folderPath)) return false;
    FolderHandlerInfo expected;
    expected.appId = activation.appId;
    expected.registrationOwner = activation.registrationOwner;
    expected.registrationGeneration = activation.registrationGeneration;
    const FolderActivationResolution current = ResolveFolderActivation(expected, activation.folderPath);
    return current.launchable() && current.activation.appId == activation.appId &&
        current.activation.registrationOwner == activation.registrationOwner &&
        current.activation.registrationGeneration == activation.registrationGeneration &&
        current.activation.folderPath == activation.folderPath;
}

bool AppRegistry::SetFolderActivationBackendAvailable(const std::string& canonicalAppId, bool available) {
    auto found = m_appsById.find(canonicalAppId);
    if (found == m_appsById.end() || found->second >= m_apps.size()) return false;
    RegisteredApp& app = m_apps[found->second];
    if (available && !app.manifest.supportsFolderActivation) return false;
    if (app.folderActivationBackendAvailable == available) return true;
    app.folderActivationBackendAvailable = available;
    RebuildFolderHandlers();
    return true;
}

const std::vector<FolderHandlerRecord>& AppRegistry::GetFolderHandlers() const { return m_folderHandlers; }
bool AppRegistry::FolderHandlerCapacityExceeded() const { return m_folderHandlerCapacityExceeded; }

AppActionList AppRegistry::EnumerateAppActions(const std::string& canonicalAppId) const {
    AppActionList result;
    if (canonicalAppId.empty() || canonicalAppId.size() > kAppModelMaxAppIdBytes) return result;
    const RegisteredApp* app = FindById(canonicalAppId);
    if (!app) return result;

    result.appFound = true;
    result.appId = app->manifest.id;
    result.declaredActionCount = app->manifest.actions.size();
    if (!app->appActionBackendAvailable || app->registrationGeneration == 0) return result;

    std::vector<AppActionInfo> available;
    available.reserve(std::min(app->manifest.actions.size(), kAppModelMaxActionsPerApp));
    for (const AppActionDeclaration& declaration : app->manifest.actions) {
        if (!IsValidAppActionId(declaration.id) || declaration.label.empty() ||
            declaration.label.size() > kAppModelMaxActionLabelBytes) continue;
        AppActionInfo action;
        action.appId = app->manifest.id;
        action.actionId = declaration.id;
        action.label = declaration.label;
        action.registrationGeneration = app->registrationGeneration;
        action.registrationCurrent = true;
        action.backendAvailable = true;
        action.available = true;
        available.push_back(std::move(action));
    }
    std::sort(available.begin(), available.end(), [](const AppActionInfo& left, const AppActionInfo& right) {
        return left.actionId < right.actionId;
    });
    result.availableActionCount = available.size();
    result.count = std::min(available.size(), result.actions.size());
    result.truncated = available.size() > result.actions.size();
    for (size_t i = 0; i < result.count; ++i) result.actions[i] = std::move(available[i]);
    return result;
}

AppActionResolution AppRegistry::ResolveAppAction(const std::string& canonicalAppId,
                                                  const std::string& actionId,
                                                  uint64_t expectedRegistrationGeneration) const {
    AppActionResolution result;
    if (canonicalAppId.empty() || canonicalAppId.size() > kAppModelMaxAppIdBytes) {
        result.status = AppActionResolutionStatus::InvalidAppId;
        result.reason = "canonical application ID is empty or over capacity";
        return result;
    }
    if (!IsValidAppActionId(actionId)) {
        result.status = AppActionResolutionStatus::InvalidActionId;
        result.reason = "application action ID is malformed or over capacity";
        return result;
    }
    const RegisteredApp* app = FindById(canonicalAppId);
    if (!app) {
        result.status = AppActionResolutionStatus::UnknownApp;
        result.reason = "canonical application ID is not registered";
        return result;
    }
    if (expectedRegistrationGeneration != 0 &&
        expectedRegistrationGeneration != app->registrationGeneration) {
        result.status = AppActionResolutionStatus::RegistrationStale;
        result.reason = "application registration generation changed after action enumeration";
        return result;
    }
    const auto declaration = std::find_if(app->manifest.actions.begin(), app->manifest.actions.end(),
        [&](const AppActionDeclaration& candidate) { return candidate.id == actionId; });
    if (declaration == app->manifest.actions.end()) {
        result.status = AppActionResolutionStatus::ActionNotDeclared;
        result.reason = "application does not currently declare this action ID";
        return result;
    }
    result.action.appId = app->manifest.id;
    result.action.actionId = declaration->id;
    result.action.label = declaration->label;
    result.action.registrationGeneration = app->registrationGeneration;
    result.action.registrationCurrent = app->registrationGeneration != 0;
    result.action.backendAvailable = app->appActionBackendAvailable;
    result.action.available = result.action.registrationCurrent && result.action.backendAvailable;
    if (!result.action.registrationCurrent) {
        result.status = AppActionResolutionStatus::RegistrationStale;
        result.reason = "application registration has no current registry generation";
    } else if (!result.action.backendAvailable) {
        result.status = AppActionResolutionStatus::HandlerUnavailable;
        result.reason = "the registered application has no current action dispatcher";
    } else {
        result.status = AppActionResolutionStatus::Resolved;
    }
    return result;
}

bool AppRegistry::IsAppActionCurrent(const AppActionInfo& action) const {
    if (!action.available || !action.registrationCurrent || action.registrationGeneration == 0) return false;
    const AppActionResolution current = ResolveAppAction(action.appId, action.actionId, action.registrationGeneration);
    return current.invocable() && current.action.label == action.label;
}

bool AppRegistry::SetAppActionBackendAvailable(const std::string& canonicalAppId, bool available) {
    const auto found = m_appsById.find(canonicalAppId);
    if (found == m_appsById.end() || found->second >= m_apps.size()) return false;
    RegisteredApp& app = m_apps[found->second];
    if (available && app.manifest.actions.empty()) return false;
    if (app.appActionBackendAvailable == available) return true;
    if (m_nextRegistrationGeneration == 0 || m_nextRegistrationGeneration == std::numeric_limits<uint64_t>::max()) return false;
    app.appActionBackendAvailable = available;
    app.registrationGeneration = m_nextRegistrationGeneration++;
    return true;
}

bool AppRegistry::SetProtocolActivationBackendAvailable(const std::string& canonicalAppId, bool available) {
    const auto found = m_appsById.find(canonicalAppId);
    if (found == m_appsById.end() || found->second >= m_apps.size()) return false;
    RegisteredApp& app = m_apps[found->second];
    if (available && (!app.manifest.supportsProtocolActivation || app.manifest.protocols.empty())) return false;
    app.protocolActivationBackendAvailable = available;
    RebuildProtocolHandlers();
    return true;
}

const std::vector<ProtocolHandlerRecord>& AppRegistry::GetProtocolHandlers() const { return m_protocolHandlers; }
bool AppRegistry::ProtocolHandlerCapacityExceeded() const { return m_protocolHandlerCapacityExceeded; }

DefaultHandlerMutationResult AppRegistry::SetDefaultHandler(const std::string& requestedExtension,
                                                            const std::string& canonicalAppId) {
    DefaultHandlerMutationResult result;
    std::string extension;
    if (!NormalizeDocumentExtension(requestedExtension, extension)) {
        result.status = DefaultHandlerMutationStatus::InvalidExtension;
        result.reason = "extension is malformed or over capacity";
        return result;
    }
    result.extension = extension;
    result.appId = canonicalAppId;
    std::string reloadError;
    if (!m_defaultHandlerStore.Reload(reloadError)) {
        result.status = DefaultHandlerMutationStatus::PersistenceFailure;
        result.reason = reloadError.empty() ? "authoritative default-handler configuration could not be reread" : reloadError;
        return result;
    }
    const RegisteredApp* app = FindById(canonicalAppId);
    if (!app) {
        result.status = DefaultHandlerMutationStatus::UnknownApplication;
        result.reason = "canonical App Model ID is not currently registered";
        return result;
    }
    if (app->temporaryDevelopment || app->sourceKind == AppSourceKind::DevelopmentTemporary) {
        result.status = DefaultHandlerMutationStatus::NonDurableRegistration;
        result.reason = "temporary development registrations cannot become durable machine defaults";
        return result;
    }
    if (!HasDeclaredCapability(*app, extension)) {
        result.status = DefaultHandlerMutationStatus::CapabilityMissing;
        result.reason = "application does not currently declare this normalized extension";
        return result;
    }
    if (!app->manifest.supportsDocumentActivation) {
        result.status = DefaultHandlerMutationStatus::DocumentActivationUnsupported;
        result.reason = "application does not declare document activation support";
        return result;
    }
    if (!app->documentActivationBackendAvailable) {
        result.status = DefaultHandlerMutationStatus::HandlerUnavailable;
        result.reason = "current hosted backend cannot dispatch document activation to this application";
        return result;
    }
    const bool indexed = std::any_of(m_fileAssociations.begin(), m_fileAssociations.end(), [&](const FileAssociationRecord& record) {
        return record.extension == extension && record.appId == canonicalAppId &&
            record.registrationOwner == app->temporaryOwnerRuntimeId &&
            record.registrationGeneration == app->temporaryGeneration;
    });
    if (!indexed) {
        result.status = m_fileAssociationCapacityExceeded
            ? DefaultHandlerMutationStatus::CapacityExceeded : DefaultHandlerMutationStatus::CapabilityMissing;
        result.reason = m_fileAssociationCapacityExceeded
            ? "the bounded AppRegistry association index cannot safely resolve this capability"
            : "the declared capability is not present in the current AppRegistry index";
        return result;
    }

    std::vector<DefaultHandlerOverride> updated = m_defaultHandlerStore.Overrides();
    auto existing = std::find_if(updated.begin(), updated.end(), [&](const DefaultHandlerOverride& entry) {
        return entry.extension == extension;
    });
    if (existing == updated.end()) {
        if (updated.size() >= kAppModelMaxDefaultHandlerOverrides) {
            result.status = DefaultHandlerMutationStatus::CapacityExceeded;
            result.reason = "machine default-handler override capacity is full";
            return result;
        }
        updated.push_back({extension, canonicalAppId});
    } else {
        existing->appId = canonicalAppId;
    }

    std::string error;
    if (!m_defaultHandlerStore.Commit(updated, error)) {
        result.status = m_defaultHandlerStore.Diagnostics().lastWriteStatus == DefaultHandlerStoreWriteStatus::VerificationFailed
            ? DefaultHandlerMutationStatus::VerificationFailure : DefaultHandlerMutationStatus::PersistenceFailure;
        result.reason = error;
        return result;
    }
    const DefaultHandlerInfo verified = GetDefaultHandlerInfo(extension);
    if (verified.configuredOverrideAppId != canonicalAppId || verified.effectiveDefaultAppId != canonicalAppId ||
        verified.configuredStatus != ConfiguredDefaultHandlerStatus::Available) {
        result.status = DefaultHandlerMutationStatus::VerificationFailure;
        result.reason = "durable override reread succeeded but did not resolve to the requested current handler";
        return result;
    }
    return result;
}

DefaultHandlerMutationResult AppRegistry::ClearDefaultHandler(const std::string& requestedExtension) {
    DefaultHandlerMutationResult result;
    std::string extension;
    if (!NormalizeDocumentExtension(requestedExtension, extension)) {
        result.status = DefaultHandlerMutationStatus::InvalidExtension;
        result.reason = "extension is malformed or over capacity";
        return result;
    }
    result.extension = extension;
    std::string reloadError;
    if (!m_defaultHandlerStore.Reload(reloadError)) {
        result.status = DefaultHandlerMutationStatus::PersistenceFailure;
        result.reason = reloadError.empty() ? "authoritative default-handler configuration could not be reread" : reloadError;
        return result;
    }
    std::vector<DefaultHandlerOverride> updated = m_defaultHandlerStore.Overrides();
    const size_t oldSize = updated.size();
    updated.erase(std::remove_if(updated.begin(), updated.end(), [&](const DefaultHandlerOverride& entry) {
        return entry.extension == extension;
    }), updated.end());
    if (updated.size() != oldSize) {
        std::string error;
        if (!m_defaultHandlerStore.Commit(updated, error)) {
            result.status = m_defaultHandlerStore.Diagnostics().lastWriteStatus == DefaultHandlerStoreWriteStatus::VerificationFailed
                ? DefaultHandlerMutationStatus::VerificationFailure : DefaultHandlerMutationStatus::PersistenceFailure;
            result.reason = error;
            return result;
        }
    }
    if (m_defaultHandlerStore.Find(extension)) {
        result.status = DefaultHandlerMutationStatus::VerificationFailure;
        result.reason = "cleared override remained present after authoritative reread";
    }
    return result;
}

DefaultHandlerInfo AppRegistry::GetDefaultHandlerInfo(const std::string& requestedExtension) const {
    DefaultHandlerInfo info;
    if (!NormalizeDocumentExtension(requestedExtension, info.extension)) return info;
    info.builtInDefaultAppId = builtInDefaultAppId(info.extension);
    const DefaultHandlerOverride* configured = m_defaultHandlerStore.Find(info.extension);
    if (configured) {
        info.configuredOverrideAppId = configured->appId;
        const RegisteredApp* app = FindById(configured->appId);
        if (!app) {
            info.configuredStatus = ConfiguredDefaultHandlerStatus::RegistrationMissing;
        } else if (app->temporaryDevelopment || app->sourceKind == AppSourceKind::DevelopmentTemporary) {
            info.configuredStatus = ConfiguredDefaultHandlerStatus::NonDurableRegistration;
        } else if (!HasDeclaredCapability(*app, info.extension)) {
            info.configuredStatus = ConfiguredDefaultHandlerStatus::CapabilityMissing;
        } else if (!app->manifest.supportsDocumentActivation) {
            info.configuredStatus = ConfiguredDefaultHandlerStatus::DocumentActivationUnsupported;
        } else if (!app->documentActivationBackendAvailable) {
            info.configuredStatus = ConfiguredDefaultHandlerStatus::TemporarilyUnavailable;
        } else {
            const bool indexed = std::any_of(m_fileAssociations.begin(), m_fileAssociations.end(), [&](const FileAssociationRecord& record) {
                return record.extension == info.extension && record.appId == configured->appId &&
                    record.registrationOwner == app->temporaryOwnerRuntimeId &&
                    record.registrationGeneration == app->temporaryGeneration;
            });
            info.configuredStatus = indexed ? ConfiguredDefaultHandlerStatus::Available
                : (m_fileAssociationCapacityExceeded
                    ? ConfiguredDefaultHandlerStatus::RegistryCapacityExceeded
                    : ConfiguredDefaultHandlerStatus::CapabilityMissing);
        }
    }

    const DocumentHandlerList handlers = EnumerateCapableHandlers(info.extension);
    for (size_t i = 0; i < handlers.count; ++i) {
        if (!handlers.handlers[i].isDefault) continue;
        info.effectiveDefaultAppId = handlers.handlers[i].appId;
        info.effectiveDefaultAvailable = handlers.handlers[i].available;
        break;
    }
    return info;
}

std::vector<std::string> AppRegistry::GetKnownDocumentExtensions() const {
    std::vector<std::string> extensions;
    extensions.reserve(m_fileAssociations.size() + m_defaultHandlerStore.Overrides().size() + 4);
    for (const FileAssociationRecord& record : m_fileAssociations) extensions.push_back(record.extension);
    for (const DefaultHandlerOverride& record : m_defaultHandlerStore.Overrides())
        if (record.keyKind == DefaultHandlerOverride::KeyKind::Extension) extensions.push_back(record.extension);
    extensions.insert(extensions.end(), { ".txt", ".log", ".ini", ".cfg" });
    std::sort(extensions.begin(), extensions.end());
    extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());
    return extensions;
}

DefaultHandlerStoreDiagnostics AppRegistry::GetDefaultHandlerStoreDiagnostics() const {
    return m_defaultHandlerStore.Diagnostics();
}

bool AppRegistry::ReloadDefaultHandlerConfiguration(std::string& error) {
    return m_defaultHandlerStore.Reload(error);
}

bool AppRegistry::HasDeclaredCapability(const RegisteredApp& app, const std::string& normalizedExtension) const {
    return std::any_of(app.manifest.fileAssociations.begin(), app.manifest.fileAssociations.end(),
        [&](const FileAssociation& association) {
            std::string normalized;
            return NormalizeDocumentExtension(association.extension, normalized) && normalized == normalizedExtension;
        });
}

bool AppRegistry::HasDeclaredProtocol(const RegisteredApp& app, const std::string& normalizedScheme) const {
    return std::any_of(app.manifest.protocols.begin(), app.manifest.protocols.end(),
        [&](const std::string& declaration) {
            std::string normalized;
            return NormalizeProtocolScheme(declaration, normalized) && normalized == normalizedScheme;
        });
}

FileAssociationResolution AppRegistry::ResolveFileAssociation(const std::string& path) const {
    std::string extension;
    const PathExtensionStatus pathStatus = extensionFromPath(path, extension);
    if (pathStatus != PathExtensionStatus::Valid) {
        FileAssociationResolution resolution;
        if (pathStatus == PathExtensionStatus::InvalidPath) {
            resolution.status = FileAssociationResolutionStatus::InvalidPath;
            resolution.reason = "document path is empty, malformed, or over capacity";
        } else if (pathStatus == PathExtensionStatus::NoExtension) {
            resolution.status = FileAssociationResolutionStatus::NoExtension;
            resolution.reason = "document name has no extension";
        } else {
            resolution.status = FileAssociationResolutionStatus::InvalidExtension;
            resolution.reason = "document extension is malformed or over capacity";
        }
        return resolution;
    }
    return ResolveFileAssociationForExtension(path, extension);
}

FileAssociationResolution AppRegistry::ResolveFileAssociationForExtension(const std::string& path,
                                                                           const std::string& extension) const {
    const DocumentHandlerList handlers = EnumerateCapableHandlers(extension);
    if (!handlers.validExtension) {
        FileAssociationResolution resolution;
        resolution.status = FileAssociationResolutionStatus::InvalidExtension;
        resolution.reason = "document extension is malformed or over capacity";
        return resolution;
    }
    if (handlers.count == 0) {
        FileAssociationResolution resolution;
        resolution.extension = handlers.extension;
        resolution.status = m_fileAssociationCapacityExceeded
            ? FileAssociationResolutionStatus::RegistryCapacityExceeded
            : FileAssociationResolutionStatus::NoAssociation;
        resolution.reason = m_fileAssociationCapacityExceeded
            ? "association registry capacity was reached; unresolved extensions fail closed"
            : "no application declared this extension";
        return resolution;
    }
    const DocumentHandlerInfo* selected = nullptr;
    for (size_t i = 0; i < handlers.count; ++i) {
        if (handlers.handlers[i].isDefault) {
            selected = &handlers.handlers[i];
            break;
        }
    }
    if (!selected) selected = &handlers.handlers[0];
    return ResolveDocumentActivation(*selected, path);
}

FileAssociationResolution AppRegistry::ResolveDocumentActivation(const DocumentHandlerInfo& handler,
                                                                 const std::string& path) const {
    return ResolveDocumentActivation(handler.appId, path, handler.registrationOwner, handler.registrationGeneration);
}

FileAssociationResolution AppRegistry::ResolveDocumentActivation(const std::string& canonicalAppId,
                                                                 const std::string& path,
                                                                 uint64_t expectedOwner,
                                                                 uint64_t expectedGeneration) const {
    FileAssociationResolution resolution;
    std::string extension;
    const PathExtensionStatus pathStatus = extensionFromPath(path, extension);
    if (pathStatus != PathExtensionStatus::Valid) {
        if (pathStatus == PathExtensionStatus::InvalidPath) {
            resolution.status = FileAssociationResolutionStatus::InvalidPath;
            resolution.reason = "document path is empty, malformed, or over capacity";
        } else if (pathStatus == PathExtensionStatus::NoExtension) {
            resolution.status = FileAssociationResolutionStatus::NoExtension;
            resolution.reason = "document name has no extension";
        } else {
            resolution.status = FileAssociationResolutionStatus::InvalidExtension;
            resolution.reason = "document extension is malformed or over capacity";
        }
        return resolution;
    }
    resolution.extension = extension;
    if (canonicalAppId.empty() || canonicalAppId.size() > kAppModelMaxAppIdBytes) {
        resolution.status = FileAssociationResolutionStatus::HandlerMissing;
        resolution.reason = "canonical application ID is empty or over capacity";
        return resolution;
    }
    resolution.appId = canonicalAppId;

    auto appIt = m_appsById.find(canonicalAppId);
    if (appIt == m_appsById.end() || appIt->second >= m_apps.size()) {
        resolution.status = FileAssociationResolutionStatus::HandlerMissing;
        resolution.reason = "selected canonical application ID is not registered";
        return resolution;
    }
    const RegisteredApp& app = m_apps[appIt->second];
    resolution.displayName = app.manifest.displayName;
    if ((expectedOwner != 0 || expectedGeneration != 0) &&
        (app.temporaryOwnerRuntimeId != expectedOwner || app.temporaryGeneration != expectedGeneration)) {
        resolution.status = FileAssociationResolutionStatus::HandlerStale;
        resolution.reason = "selected handler registration owner or generation changed after enumeration";
        return resolution;
    }

    const FileAssociationRecord* declaration = nullptr;
    for (const FileAssociationRecord& record : m_fileAssociations) {
        if (record.extension == extension && record.appId == canonicalAppId) {
            declaration = &record;
            break;
        }
    }
    if (!declaration) {
        resolution.status = FileAssociationResolutionStatus::HandlerDoesNotSupportDocuments;
        resolution.reason = "selected application does not declare support for this extension";
        return resolution;
    }
    if (app.temporaryOwnerRuntimeId != declaration->registrationOwner ||
        app.temporaryGeneration != declaration->registrationGeneration) {
        resolution.status = FileAssociationResolutionStatus::HandlerStale;
        resolution.reason = "association registration owner or generation is stale";
        return resolution;
    }
    if (!app.manifest.supportsDocumentActivation || !declaration->supportsDocumentActivation) {
        resolution.status = FileAssociationResolutionStatus::HandlerDoesNotSupportDocuments;
        resolution.reason = "registered handler does not declare document activation support";
        return resolution;
    }
    if (!app.documentActivationBackendAvailable || !declaration->backendAvailable) {
        resolution.status = FileAssociationResolutionStatus::HandlerUnavailable;
        resolution.reason = "current runtime has no document activation dispatcher for this handler";
        return resolution;
    }

    resolution.status = FileAssociationResolutionStatus::Resolved;
    resolution.activation.kind = AppActivationKind::Document;
    resolution.activation.appId = app.manifest.id;
    resolution.activation.documentPath = path;
    resolution.activation.registrationOwner = app.temporaryOwnerRuntimeId;
    resolution.activation.registrationGeneration = app.temporaryGeneration;
    resolution.reason = "explicit handler selection resolved to the current capable registration";
    return resolution;
}

bool AppRegistry::IsDocumentActivationCurrent(const AppActivationContext& activation) const {
    if (activation.kind != AppActivationKind::Document || activation.appId.empty() ||
        activation.appId.size() > kAppModelMaxAppIdBytes || !IsValidDocumentActivationPath(activation.documentPath)) {
        return false;
    }
    const FileAssociationResolution current = ResolveDocumentActivation(activation.appId, activation.documentPath,
        activation.registrationOwner, activation.registrationGeneration);
    return current.launchable() && current.appId == activation.appId &&
        current.activation.registrationOwner == activation.registrationOwner &&
        current.activation.registrationGeneration == activation.registrationGeneration;
}

bool AppRegistry::SetDocumentActivationBackendAvailable(const std::string& canonicalAppId, bool available) {
    auto found = m_appsById.find(canonicalAppId);
    if (found == m_appsById.end() || found->second >= m_apps.size()) return false;
    RegisteredApp& app = m_apps[found->second];
    if (available && (!app.manifest.supportsDocumentActivation || app.manifest.fileAssociations.empty())) return false;
    if (app.documentActivationBackendAvailable == available) return true;
    app.documentActivationBackendAvailable = available;
    RebuildFileAssociations();
    return true;
}

const std::vector<FileAssociationRecord>& AppRegistry::GetFileAssociations() const {
    return m_fileAssociations;
}

bool AppRegistry::FileAssociationCapacityExceeded() const {
    return m_fileAssociationCapacityExceeded;
}

void AppRegistry::RebuildFileAssociations() {
    // Both dimensions are bounded: at most 512 registered apps, each with at
    // most 16 declarations. Sort before applying the 256-record index cap so
    // overflow behavior does not depend on directory enumeration order.
    std::vector<FileAssociationRecord> candidates;
    candidates.reserve(std::min(m_apps.size() * kAppModelMaxFileAssociationsPerApp,
        kAppModelMaxRegistryApps * kAppModelMaxFileAssociationsPerApp));
    for (const RegisteredApp& app : m_apps) {
        for (const FileAssociation& declaration : app.manifest.fileAssociations) {
            FileAssociationRecord record;
            NormalizeDocumentExtension(declaration.extension, record.extension);
            record.appId = app.manifest.id;
            record.contentType = declaration.contentType;
            record.description = declaration.description;
            record.registrationOwner = app.temporaryOwnerRuntimeId;
            record.registrationGeneration = app.temporaryGeneration;
            record.supportsDocumentActivation = app.manifest.supportsDocumentActivation;
            record.backendAvailable = app.documentActivationBackendAvailable;
            candidates.push_back(std::move(record));
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const FileAssociationRecord& a, const FileAssociationRecord& b) {
        if (a.extension != b.extension) return a.extension < b.extension;
        if (a.appId != b.appId) return a.appId < b.appId;
        if (a.registrationOwner != b.registrationOwner) return a.registrationOwner < b.registrationOwner;
        return a.registrationGeneration < b.registrationGeneration;
    });

    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const FileAssociationRecord& a, const FileAssociationRecord& b) {
        return a.extension == b.extension && a.appId == b.appId &&
            a.registrationOwner == b.registrationOwner && a.registrationGeneration == b.registrationGeneration;
    }), candidates.end());

    m_fileAssociationCapacityExceeded = candidates.size() > kAppModelMaxFileAssociationRecords;
    m_fileAssociations.clear();
    m_fileAssociations.reserve(std::min(candidates.size(), kAppModelMaxFileAssociationRecords));
    for (size_t i = 0; i < candidates.size() && i < kAppModelMaxFileAssociationRecords; ++i) {
        candidates[i].ambiguous = false;
        m_fileAssociations.push_back(candidates[i]);
    }
    RebuildProtocolHandlers();
}

void AppRegistry::RebuildProtocolHandlers() {
    std::vector<ProtocolHandlerRecord> candidates;
    candidates.reserve(std::min(m_apps.size() * kAppModelMaxProtocolsPerApp,
        kAppModelMaxRegistryApps * kAppModelMaxProtocolsPerApp));
    for (const RegisteredApp& app : m_apps) {
        for (const std::string& declaration : app.manifest.protocols) {
            ProtocolHandlerRecord record;
            if (!NormalizeProtocolScheme(declaration, record.scheme)) continue;
            record.appId = app.manifest.id;
            record.registrationOwner = app.temporaryOwnerRuntimeId;
            record.registrationGeneration = app.temporaryGeneration;
            record.supportsProtocolActivation = app.manifest.supportsProtocolActivation;
            record.backendAvailable = app.protocolActivationBackendAvailable;
            candidates.push_back(std::move(record));
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const ProtocolHandlerRecord& left, const ProtocolHandlerRecord& right) {
        if (left.scheme != right.scheme) return left.scheme < right.scheme;
        if (left.appId != right.appId) return left.appId < right.appId;
        if (left.registrationOwner != right.registrationOwner) return left.registrationOwner < right.registrationOwner;
        return left.registrationGeneration < right.registrationGeneration;
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const ProtocolHandlerRecord& left, const ProtocolHandlerRecord& right) {
        return left.scheme == right.scheme && left.appId == right.appId &&
            left.registrationOwner == right.registrationOwner && left.registrationGeneration == right.registrationGeneration;
    }), candidates.end());
    m_protocolHandlerCapacityExceeded = candidates.size() > kAppModelMaxProtocolRecords;
    m_protocolHandlers.clear();
    m_protocolHandlers.reserve(std::min(candidates.size(), kAppModelMaxProtocolRecords));
    for (size_t i = 0; i < candidates.size() && i < kAppModelMaxProtocolRecords; ++i)
        m_protocolHandlers.push_back(std::move(candidates[i]));
    RebuildFolderHandlers();
}

void AppRegistry::RebuildFolderHandlers() {
    std::vector<FolderHandlerRecord> candidates;
    candidates.reserve(std::min(m_apps.size(), kAppModelMaxRegistryApps));
    for (const RegisteredApp& app : m_apps) {
        if (!app.manifest.supportsFolderActivation) continue;
        FolderHandlerRecord record;
        record.appId = app.manifest.id;
        record.registrationOwner = app.temporaryOwnerRuntimeId;
        record.registrationGeneration = app.temporaryGeneration;
        record.supportsFolderActivation = app.manifest.supportsFolderActivation;
        record.backendAvailable = app.folderActivationBackendAvailable;
        candidates.push_back(std::move(record));
    }
    std::sort(candidates.begin(), candidates.end(), [](const FolderHandlerRecord& left, const FolderHandlerRecord& right) {
        if (left.appId != right.appId) return left.appId < right.appId;
        if (left.registrationOwner != right.registrationOwner) return left.registrationOwner < right.registrationOwner;
        return left.registrationGeneration < right.registrationGeneration;
    });
    m_folderHandlerCapacityExceeded = candidates.size() > kAppModelMaxFolderHandlerRecords;
    m_folderHandlers.clear();
    m_folderHandlers.reserve(std::min(candidates.size(), kAppModelMaxFolderHandlerRecords));
    for (size_t i = 0; i < candidates.size() && i < kAppModelMaxFolderHandlerRecords; ++i)
        m_folderHandlers.push_back(std::move(candidates[i]));
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
    const AppManifestValidationResult validation = AppManifestValidator::Validate(app.manifest);
    if (!validation.valid) {
        error = validation.errors.empty() ? "invalid temporary development manifest" : validation.errors.front();
        return false;
    }
    if (app.manifest.id.size() > kAppModelMaxAppIdBytes ||
        app.manifest.entries.size() > kAppModelMaxEntriesPerManifest ||
        app.manifestPath.generic_string().size() > kAppModelMaxEntryPathBytes ||
        app.appDirectory.generic_string().size() > kAppModelMaxEntryPathBytes) {
        error = "temporary development registration exceeds App Model capacity";
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

    if (m_apps.size() >= kAppModelMaxRegistryApps) {
        error = "APP_REGISTRY_FULL";
        return false;
    }

    if (m_nextRegistrationGeneration == 0 || m_nextRegistrationGeneration == std::numeric_limits<uint64_t>::max()) {
        error = "APP_REGISTRATION_GENERATION_EXHAUSTED";
        return false;
    }

    m_appsById[app.manifest.id] = m_apps.size();
    RegisteredApp accepted = app;
    accepted.registrationGeneration = m_nextRegistrationGeneration++;
    m_apps.push_back(std::move(accepted));
    RebuildFileAssociations();
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
    RebuildFileAssociations();
    return true;
}

#if defined(GXOS_APPMODEL_TESTING)
bool AppRegistry::RegisterTestDurableApp(const RegisteredApp& app, std::string& error) {
    error.clear();
    if (app.temporaryDevelopment || app.sourceKind == AppSourceKind::DevelopmentTemporary ||
        app.temporaryOwnerRuntimeId != 0 || app.temporaryGeneration != 0) {
        error = "test durable registrations must use persistent App Model identity";
        return false;
    }
    const AppManifestValidationResult validation = AppManifestValidator::Validate(app.manifest);
    if (!validation.valid) {
        error = validation.errors.empty() ? "test durable application manifest is invalid" : validation.errors.front();
        return false;
    }
    AppScanResult result;
    if (!RegisterApp(app, result)) {
        error = result.invalidApps.empty() ? "test durable application registration failed" : result.invalidApps.back().errors.front();
        return false;
    }
    RebuildFileAssociations();
    return true;
}

bool AppRegistry::SetTestDocumentActivationBackend(const std::string& appId, bool available) {
    auto found = m_appsById.find(appId);
    if (found == m_appsById.end() || found->second >= m_apps.size()) return false;
    m_apps[found->second].documentActivationBackendAvailable = available;
    RebuildFileAssociations();
    return true;
}

bool AppRegistry::SetTestProtocolActivationBackend(const std::string& appId, bool available) {
    auto found = m_appsById.find(appId);
    if (found == m_appsById.end() || found->second >= m_apps.size()) return false;
    m_apps[found->second].protocolActivationBackendAvailable = available;
    RebuildProtocolHandlers();
    return true;
}

bool AppRegistry::SetTestFolderActivationBackend(const std::string& appId, bool available) {
    return SetFolderActivationBackendAvailable(appId, available);
}

bool AppRegistry::SetTestAppActionBackend(const std::string& appId, bool available) {
    return SetAppActionBackendAvailable(appId, available);
}
#endif

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
    if (app.manifest.id.empty() || app.manifest.id.size() > kAppModelMaxAppIdBytes ||
        app.manifest.displayName.empty() || app.manifest.displayName.size() > kAppModelMaxDisplayNameBytes ||
        app.manifest.entries.size() > kAppModelMaxEntriesPerManifest ||
        app.manifest.actions.size() > kAppModelMaxActionsPerApp ||
        app.manifestPath.generic_string().size() > kAppModelMaxEntryPathBytes ||
        app.appDirectory.generic_string().size() > kAppModelMaxEntryPathBytes) {
        result.invalidApps.push_back(makeIssue(app.sourceKind, app.manifestPath, app.manifest.id,
            { "App Model identity, display name, or entry count exceeds its bound" }));
        return false;
    }
    auto existing = m_appsById.find(app.manifest.id);
    if (existing != m_appsById.end()) {
        std::vector<std::string> errors = { "Duplicate app id: " + app.manifest.id };
        result.duplicateApps.push_back(makeIssue(app.sourceKind, app.manifestPath, app.manifest.id, errors));
        if (!ShouldReplaceDuplicate(m_apps[existing->second], app)) return false;
        if (m_nextRegistrationGeneration == 0 || m_nextRegistrationGeneration == std::numeric_limits<uint64_t>::max()) {
            result.invalidApps.push_back(makeIssue(app.sourceKind, app.manifestPath, app.manifest.id,
                { "App Model registration generation capacity reached" }));
            return false;
        }
        RegisteredApp accepted = app;
        accepted.registrationGeneration = m_nextRegistrationGeneration++;
        m_apps[existing->second] = std::move(accepted);
        return true;
    }

    if (m_apps.size() >= kAppModelMaxRegistryApps) {
        const bool alreadyReported = std::any_of(result.invalidApps.begin(), result.invalidApps.end(),
            [](const AppScanIssue& issue) {
                return !issue.errors.empty() && issue.errors.front() == "App Model registry capacity reached";
            });
        if (!alreadyReported) {
            result.invalidApps.push_back(makeIssue(app.sourceKind, app.manifestPath, app.manifest.id,
                { "App Model registry capacity reached" }));
        }
        return false;
    }

    if (m_nextRegistrationGeneration == 0 || m_nextRegistrationGeneration == std::numeric_limits<uint64_t>::max()) {
        result.invalidApps.push_back(makeIssue(app.sourceKind, app.manifestPath, app.manifest.id,
            { "App Model registration generation capacity reached" }));
        return false;
    }

    m_appsById[app.manifest.id] = m_apps.size();
    RegisteredApp accepted = app;
    accepted.registrationGeneration = m_nextRegistrationGeneration++;
    m_apps.push_back(std::move(accepted));
    return true;
}

bool AppRegistry::ShouldReplaceDuplicate(const RegisteredApp& existingApp, const RegisteredApp& newApp) const {
    if (existingApp.sourceKind == AppSourceKind::SystemApps && newApp.sourceKind == AppSourceKind::UserApps) {
        return !m_preferSystemAppsOverUserApps;
    }

    if (existingApp.sourceKind == AppSourceKind::UserApps && newApp.sourceKind == AppSourceKind::SystemApps) {
        return m_preferSystemAppsOverUserApps;
    }

    if (existingApp.sourceKind == newApp.sourceKind) {
        const std::string existingPath = existingApp.manifestPath.lexically_normal().generic_string();
        const std::string newPath = newApp.manifestPath.lexically_normal().generic_string();
        return !newPath.empty() && (existingPath.empty() || newPath < existingPath);
    }

    return false;
}

const char* AppRegistry::ToString(FileAssociationResolutionStatus status) {
    switch (status) {
    case FileAssociationResolutionStatus::Resolved: return "resolved";
    case FileAssociationResolutionStatus::InvalidPath: return "invalid-path";
    case FileAssociationResolutionStatus::NoExtension: return "no-extension";
    case FileAssociationResolutionStatus::InvalidExtension: return "invalid-extension";
    case FileAssociationResolutionStatus::NoAssociation: return "no-association";
    case FileAssociationResolutionStatus::Ambiguous: return "ambiguous";
    case FileAssociationResolutionStatus::HandlerMissing: return "handler-missing";
    case FileAssociationResolutionStatus::HandlerStale: return "handler-stale";
    case FileAssociationResolutionStatus::HandlerDoesNotSupportDocuments: return "handler-no-document-activation";
    case FileAssociationResolutionStatus::HandlerUnavailable: return "handler-unavailable";
    case FileAssociationResolutionStatus::RegistryCapacityExceeded: return "capacity-exceeded";
    default: return "unknown";
    }
}

const char* AppRegistry::ToString(UriActivationResolutionStatus status) {
    switch (status) {
    case UriActivationResolutionStatus::Resolved: return "resolved";
    case UriActivationResolutionStatus::InvalidUri: return "invalid-uri";
    case UriActivationResolutionStatus::InvalidScheme: return "invalid-scheme";
    case UriActivationResolutionStatus::NoHandler: return "no-handler";
    case UriActivationResolutionStatus::HandlerMissing: return "handler-missing";
    case UriActivationResolutionStatus::HandlerStale: return "handler-stale";
    case UriActivationResolutionStatus::HandlerDoesNotSupportProtocol: return "handler-no-protocol-activation";
    case UriActivationResolutionStatus::HandlerUnavailable: return "handler-unavailable";
    case UriActivationResolutionStatus::RegistryCapacityExceeded: return "capacity-exceeded";
    default: return "unknown";
    }
}

const char* AppRegistry::ToString(FolderActivationResolutionStatus status) {
    switch (status) {
    case FolderActivationResolutionStatus::Resolved: return "resolved";
    case FolderActivationResolutionStatus::InvalidPath: return "invalid-path";
    case FolderActivationResolutionStatus::NoHandler: return "no-handler";
    case FolderActivationResolutionStatus::HandlerMissing: return "handler-missing";
    case FolderActivationResolutionStatus::HandlerStale: return "handler-stale";
    case FolderActivationResolutionStatus::HandlerDoesNotSupportFolders: return "handler-does-not-support-folders";
    case FolderActivationResolutionStatus::HandlerUnavailable: return "handler-unavailable";
    case FolderActivationResolutionStatus::RegistryCapacityExceeded: return "registry-capacity-exceeded";
    default: return "unknown";
    }
}

const char* AppRegistry::ToString(AppActionResolutionStatus status) {
    switch (status) {
    case AppActionResolutionStatus::Resolved: return "resolved";
    case AppActionResolutionStatus::InvalidAppId: return "invalid-app-id";
    case AppActionResolutionStatus::InvalidActionId: return "invalid-action-id";
    case AppActionResolutionStatus::UnknownApp: return "unknown-app";
    case AppActionResolutionStatus::ActionNotDeclared: return "action-not-declared";
    case AppActionResolutionStatus::RegistrationStale: return "registration-stale";
    case AppActionResolutionStatus::HandlerUnavailable: return "handler-unavailable";
    default: return "unknown";
    }
}

const char* AppRegistry::ToString(AppActionInvocationStatus status) {
    switch (status) {
    case AppActionInvocationStatus::Success: return "success";
    case AppActionInvocationStatus::AppUnavailable: return "app-unavailable";
    case AppActionInvocationStatus::ActionUnavailable: return "action-unavailable";
    case AppActionInvocationStatus::StaleRegistration: return "stale-registration";
    case AppActionInvocationStatus::DispatchFailure: return "dispatch-failure";
    default: return "unknown";
    }
}

const char* AppRegistry::ToString(ConfiguredDefaultHandlerStatus status) {
    switch (status) {
    case ConfiguredDefaultHandlerStatus::Available: return "available";
    case ConfiguredDefaultHandlerStatus::RegistrationMissing: return "registration-missing";
    case ConfiguredDefaultHandlerStatus::CapabilityMissing: return "capability-missing";
    case ConfiguredDefaultHandlerStatus::DocumentActivationUnsupported: return "document-activation-unsupported";
    case ConfiguredDefaultHandlerStatus::ProtocolActivationUnsupported: return "protocol-activation-unsupported";
    case ConfiguredDefaultHandlerStatus::TemporarilyUnavailable: return "temporarily-unavailable";
    case ConfiguredDefaultHandlerStatus::NonDurableRegistration: return "non-durable-registration";
    case ConfiguredDefaultHandlerStatus::RegistryCapacityExceeded: return "registry-capacity-exceeded";
    case ConfiguredDefaultHandlerStatus::NotConfigured:
    default: return "not-configured";
    }
}

const char* AppRegistry::ToString(DefaultHandlerMutationStatus status) {
    switch (status) {
    case DefaultHandlerMutationStatus::InvalidExtension: return "invalid-extension";
    case DefaultHandlerMutationStatus::InvalidProtocol: return "invalid-protocol";
    case DefaultHandlerMutationStatus::UnknownApplication: return "unknown-application";
    case DefaultHandlerMutationStatus::CapabilityMissing: return "capability-missing";
    case DefaultHandlerMutationStatus::ProtocolCapabilityMissing: return "protocol-capability-missing";
    case DefaultHandlerMutationStatus::DocumentActivationUnsupported: return "document-activation-unsupported";
    case DefaultHandlerMutationStatus::ProtocolActivationUnsupported: return "protocol-activation-unsupported";
    case DefaultHandlerMutationStatus::HandlerUnavailable: return "handler-unavailable";
    case DefaultHandlerMutationStatus::NonDurableRegistration: return "non-durable-registration";
    case DefaultHandlerMutationStatus::CapacityExceeded: return "capacity-exceeded";
    case DefaultHandlerMutationStatus::PersistenceFailure: return "persistence-failure";
    case DefaultHandlerMutationStatus::VerificationFailure: return "verification-failure";
    case DefaultHandlerMutationStatus::Success:
    default: return "success";
    }
}

} // namespace apps
} // namespace gxos
