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
    return std::string();
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
    for (const DefaultHandlerOverride& record : m_defaultHandlerStore.Overrides()) extensions.push_back(record.extension);
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

    m_appsById[app.manifest.id] = m_apps.size();
    m_apps.push_back(app);
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

        m_apps[existing->second] = app;
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

const char* AppRegistry::ToString(ConfiguredDefaultHandlerStatus status) {
    switch (status) {
    case ConfiguredDefaultHandlerStatus::Available: return "available";
    case ConfiguredDefaultHandlerStatus::RegistrationMissing: return "registration-missing";
    case ConfiguredDefaultHandlerStatus::CapabilityMissing: return "capability-missing";
    case ConfiguredDefaultHandlerStatus::DocumentActivationUnsupported: return "document-activation-unsupported";
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
    case DefaultHandlerMutationStatus::UnknownApplication: return "unknown-application";
    case DefaultHandlerMutationStatus::CapabilityMissing: return "capability-missing";
    case DefaultHandlerMutationStatus::DocumentActivationUnsupported: return "document-activation-unsupported";
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
