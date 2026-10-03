#pragma once

#include "app_manifest.h"
#include "app_activation.h"
#include "app_default_handler_store.h"
#include "app_model_limits.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

bool NormalizeDocumentExtension(const std::string& extension, std::string& normalized);

enum class AppSourceKind {
    BuiltIn = 0,
    SystemApps,
    UserApps,
    Package,
    DevelopmentTemporary
};

enum class DisplayNameResolutionStatus {
    NotFound = 0,
    Resolved,
    Ambiguous
};

enum class FileAssociationResolutionStatus {
    Resolved = 0,
    InvalidPath,
    NoExtension,
    InvalidExtension,
    NoAssociation,
    Ambiguous,
    HandlerMissing,
    HandlerStale,
    HandlerDoesNotSupportDocuments,
    HandlerUnavailable,
    RegistryCapacityExceeded
};

enum class UriActivationResolutionStatus {
    Resolved = 0,
    InvalidUri,
    InvalidScheme,
    NoHandler,
    HandlerMissing,
    HandlerStale,
    HandlerDoesNotSupportProtocol,
    HandlerUnavailable,
    RegistryCapacityExceeded
};

enum class ConfiguredDefaultHandlerStatus {
    NotConfigured = 0,
    Available,
    RegistrationMissing,
    CapabilityMissing,
    DocumentActivationUnsupported,
    ProtocolActivationUnsupported,
    TemporarilyUnavailable,
    NonDurableRegistration,
    RegistryCapacityExceeded
};

enum class DefaultHandlerMutationStatus {
    Success = 0,
    InvalidExtension,
    InvalidProtocol,
    UnknownApplication,
    CapabilityMissing,
    ProtocolCapabilityMissing,
    DocumentActivationUnsupported,
    ProtocolActivationUnsupported,
    HandlerUnavailable,
    NonDurableRegistration,
    CapacityExceeded,
    PersistenceFailure,
    VerificationFailure
};

struct DefaultHandlerInfo {
    std::string extension;
    std::string builtInDefaultAppId;
    std::string configuredOverrideAppId;
    std::string effectiveDefaultAppId;
    ConfiguredDefaultHandlerStatus configuredStatus = ConfiguredDefaultHandlerStatus::NotConfigured;
    bool effectiveDefaultAvailable = false;
};

struct DefaultHandlerMutationResult {
    DefaultHandlerMutationStatus status = DefaultHandlerMutationStatus::Success;
    std::string extension;
    std::string key;
    bool protocolKey = false;
    std::string appId;
    std::string reason;

    bool succeeded() const { return status == DefaultHandlerMutationStatus::Success; }
};

struct FileAssociationRecord {
    std::string extension;
    std::string appId;
    std::string contentType;
    std::string description;
    uint64_t registrationOwner = 0;
    uint64_t registrationGeneration = 0;
    bool supportsDocumentActivation = false;
    bool backendAvailable = false;
    bool ambiguous = false;
};

struct FileAssociationResolution {
    FileAssociationResolutionStatus status = FileAssociationResolutionStatus::NoAssociation;
    std::string extension;
    std::string appId;
    std::string displayName;
    AppActivationContext activation;
    std::string reason;

    bool launchable() const { return status == FileAssociationResolutionStatus::Resolved; }
};

struct ProtocolHandlerInfo {
    std::string appId;
    std::string displayName;
    uint64_t registrationOwner = 0;
    uint64_t registrationGeneration = 0;
    bool supportsProtocolActivation = false;
    bool registrationCurrent = false;
    bool backendAvailable = false;
    bool available = false;
    bool isDefault = false;
};

struct ProtocolHandlerList {
    std::string scheme;
    std::array<ProtocolHandlerInfo, kAppModelMaxProtocolHandlersPerScheme> handlers{};
    size_t count = 0;
    size_t declaredHandlerCount = 0;
    size_t availableHandlerCount = 0;
    bool truncated = false;
    bool validScheme = false;
};

struct ProtocolDefaultHandlerInfo {
    std::string scheme;
    std::string builtInDefaultAppId;
    std::string configuredOverrideAppId;
    std::string effectiveDefaultAppId;
    ConfiguredDefaultHandlerStatus configuredStatus = ConfiguredDefaultHandlerStatus::NotConfigured;
    bool effectiveDefaultAvailable = false;
};

struct UriActivationResolution {
    UriActivationResolutionStatus status = UriActivationResolutionStatus::NoHandler;
    std::string scheme;
    std::string appId;
    std::string displayName;
    AppActivationContext activation;
    std::string reason;

    bool launchable() const { return status == UriActivationResolutionStatus::Resolved; }
};

struct ProtocolHandlerRecord {
    std::string scheme;
    std::string appId;
    uint64_t registrationOwner = 0;
    uint64_t registrationGeneration = 0;
    bool supportsProtocolActivation = false;
    bool backendAvailable = false;
};

// Value-owned handler identity captured when a menu is built. owner/generation
// are lifecycle guards for temporary registrations; appId remains the only
// application identity used for lookup and dispatch.
struct DocumentHandlerInfo {
    std::string appId;
    std::string displayName;
    uint64_t registrationOwner = 0;
    uint64_t registrationGeneration = 0;
    bool supportsDocumentActivation = false;
    bool registrationCurrent = false;
    bool backendAvailable = false;
    bool available = false;
    bool isDefault = false;
};

struct DocumentHandlerList {
    std::string extension;
    std::array<DocumentHandlerInfo, kAppModelMaxDocumentHandlersPerExtension> handlers{};
    size_t count = 0;
    size_t declaredHandlerCount = 0;
    size_t availableHandlerCount = 0;
    bool truncated = false;
    bool validExtension = false;
};

struct RegisteredApp {
    AppManifest manifest;
    AppSourceKind sourceKind = AppSourceKind::UserApps;
    std::filesystem::path manifestPath;
    std::filesystem::path appDirectory;
    bool temporaryDevelopment = false;
    uint64_t temporaryOwnerRuntimeId = 0;
    uint64_t temporaryGeneration = 0;
    // Runtime-owned capability: the current backend has a production route
    // that can launch this registration with a document activation context.
    bool documentActivationBackendAvailable = false;
    bool protocolActivationBackendAvailable = false;

    const AppEntry* FindCompatibleEntry(const std::string& currentArchitecture) const;
};

struct AppScanIssue {
    AppSourceKind sourceKind = AppSourceKind::UserApps;
    std::filesystem::path manifestPath;
    std::string appId;
    std::vector<std::string> errors;
};

struct AppScanResult {
    size_t scannedManifestCount = 0;
    size_t registeredAppCount = 0;
    std::vector<RegisteredApp> registeredApps;
    std::vector<AppScanIssue> invalidApps;
    std::vector<AppScanIssue> duplicateApps;
};

struct AppRegistrySource {
    AppSourceKind kind = AppSourceKind::UserApps;
    std::filesystem::path path;
};

struct DisplayNameMatch {
    const RegisteredApp* app = nullptr;
    bool eligible = false;
    int sourcePriority = 0;
    std::string reason;
};

struct DisplayNameResolution {
    DisplayNameResolutionStatus status = DisplayNameResolutionStatus::NotFound;
    const RegisteredApp* app = nullptr;
    std::vector<DisplayNameMatch> matches;
    std::string reason;
};

class AppRegistry {
public:
    AppRegistry();
    explicit AppRegistry(bool preferSystemAppsOverUserApps);
    AppRegistry(bool preferSystemAppsOverUserApps, const std::filesystem::path& defaultHandlerStorePath);

    void SetPreferSystemAppsOverUserApps(bool enabled);
    bool PreferSystemAppsOverUserApps() const;

    void Clear();
    void SetSources(const std::vector<AppRegistrySource>& sources);
    void AddSource(AppSourceKind kind, const std::filesystem::path& path);

    AppScanResult Scan();
    AppScanResult Scan(const std::vector<AppRegistrySource>& sources);
    AppScanResult RegisterBuiltInAppsAsManifests();
    AppScanResult RegisterBuiltInAppsAsManifests(const std::vector<std::string>& appNames);

    // Development Run uses an in-memory registration that is never included
    // in source scanning or persistent package discovery.
    bool RegisterTemporaryDevelopmentApp(const RegisteredApp& app, std::string& error);
    bool UnregisterTemporaryDevelopmentApp(const std::string& appId, uint64_t ownerRuntimeId, uint64_t generation);
#if defined(GXOS_APPMODEL_TESTING)
    bool RegisterTestDurableApp(const RegisteredApp& app, std::string& error);
    bool SetTestDocumentActivationBackend(const std::string& appId, bool available);
    bool SetTestProtocolActivationBackend(const std::string& appId, bool available);
#endif

    const std::vector<RegisteredApp>& GetAllApps() const;
    const RegisteredApp* FindById(const std::string& appId) const;
    const RegisteredApp* FindByDisplayName(const std::string& displayName) const;
    DisplayNameResolution ResolveByDisplayName(const std::string& displayName,
                                               const std::string& architecture = "amd64",
                                               bool includeTemporaryDevelopment = false) const;
    const AppEntry* FindCompatibleEntry(const std::string& appId, const std::string& currentArchitecture) const;
    // One association record is one application's declared capability.
    // Defaults are resolved separately, with stable canonical-ID ordering.
    DocumentHandlerList EnumerateCapableHandlers(const std::string& extension) const;
    DocumentHandlerList EnumerateCapableHandlersForPath(const std::string& path) const;
    DefaultHandlerMutationResult SetDefaultHandler(const std::string& extension, const std::string& canonicalAppId);
    DefaultHandlerMutationResult ClearDefaultHandler(const std::string& extension);
    DefaultHandlerInfo GetDefaultHandlerInfo(const std::string& extension) const;
    std::vector<std::string> GetKnownDocumentExtensions() const;
    DefaultHandlerStoreDiagnostics GetDefaultHandlerStoreDiagnostics() const;
    bool ReloadDefaultHandlerConfiguration(std::string& error);
    FileAssociationResolution ResolveFileAssociation(const std::string& path) const;
    FileAssociationResolution ResolveDocumentActivation(const std::string& canonicalAppId,
                                                        const std::string& path,
                                                        uint64_t expectedOwner = 0,
                                                        uint64_t expectedGeneration = 0) const;
    FileAssociationResolution ResolveDocumentActivation(const DocumentHandlerInfo& handler,
                                                        const std::string& path) const;
    bool IsDocumentActivationCurrent(const AppActivationContext& activation) const;
    bool SetDocumentActivationBackendAvailable(const std::string& canonicalAppId, bool available);
    const std::vector<FileAssociationRecord>& GetFileAssociations() const;
    bool FileAssociationCapacityExceeded() const;

    ProtocolHandlerList EnumerateCapableProtocolHandlers(const std::string& scheme) const;
    UriActivationResolution ResolveUriActivation(const std::string& uri) const;
    UriActivationResolution ResolveUriActivation(const ProtocolHandlerInfo& handler, const std::string& uri) const;
    ProtocolDefaultHandlerInfo GetDefaultProtocolHandlerInfo(const std::string& scheme) const;
    DefaultHandlerMutationResult SetDefaultProtocolHandler(const std::string& scheme, const std::string& canonicalAppId);
    DefaultHandlerMutationResult ClearDefaultProtocolHandler(const std::string& scheme);
    std::vector<std::string> GetKnownProtocols() const;
    bool IsUriActivationCurrent(const AppActivationContext& activation) const;
    bool SetProtocolActivationBackendAvailable(const std::string& canonicalAppId, bool available);
    const std::vector<ProtocolHandlerRecord>& GetProtocolHandlers() const;
    bool ProtocolHandlerCapacityExceeded() const;

    static std::vector<AppRegistrySource> DefaultSources();
    static const char* ToString(AppSourceKind kind);
    static const char* ToString(DisplayNameResolutionStatus status);
    static const char* ToString(FileAssociationResolutionStatus status);
    static const char* ToString(UriActivationResolutionStatus status);
    static const char* ToString(ConfiguredDefaultHandlerStatus status);
    static const char* ToString(DefaultHandlerMutationStatus status);
    static int DisplayNameSourcePriority(AppSourceKind kind);

    // Phase 6/7 extension contract shared by path resolution and persistence.
    static bool NormalizeDocumentExtension(const std::string& extension, std::string& normalized);

private:
    bool RegisterApp(const RegisteredApp& app, AppScanResult& result);
    bool ShouldReplaceDuplicate(const RegisteredApp& existingApp, const RegisteredApp& newApp) const;
    FileAssociationResolution ResolveFileAssociationForExtension(const std::string& path,
                                                                 const std::string& extension) const;
    bool HasDeclaredCapability(const RegisteredApp& app, const std::string& normalizedExtension) const;
    bool HasDeclaredProtocol(const RegisteredApp& app, const std::string& normalizedScheme) const;
    void RebuildFileAssociations();
    void RebuildProtocolHandlers();

    bool m_preferSystemAppsOverUserApps = false;
    std::vector<AppRegistrySource> m_sources;
    std::vector<RegisteredApp> m_apps;
    std::map<std::string, size_t> m_appsById;
    std::vector<FileAssociationRecord> m_fileAssociations;
    bool m_fileAssociationCapacityExceeded = false;
    std::vector<ProtocolHandlerRecord> m_protocolHandlers;
    bool m_protocolHandlerCapacityExceeded = false;
    DefaultAppHandlerStore m_defaultHandlerStore;
};

} // namespace apps
} // namespace gxos
