#pragma once

#include "app_manifest.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

enum class AppSourceKind {
    BuiltIn = 0,
    SystemApps,
    UserApps,
    Package
};

struct RegisteredApp {
    AppManifest manifest;
    AppSourceKind sourceKind = AppSourceKind::UserApps;
    std::filesystem::path manifestPath;
    std::filesystem::path appDirectory;

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

class AppRegistry {
public:
    AppRegistry();
    explicit AppRegistry(bool preferSystemAppsOverUserApps);

    void SetPreferSystemAppsOverUserApps(bool enabled);
    bool PreferSystemAppsOverUserApps() const;

    void Clear();
    void SetSources(const std::vector<AppRegistrySource>& sources);
    void AddSource(AppSourceKind kind, const std::filesystem::path& path);

    AppScanResult Scan();
    AppScanResult Scan(const std::vector<AppRegistrySource>& sources);
    AppScanResult RegisterBuiltInAppsAsManifests();
    AppScanResult RegisterBuiltInAppsAsManifests(const std::vector<std::string>& appNames);

    const std::vector<RegisteredApp>& GetAllApps() const;
    const RegisteredApp* FindById(const std::string& appId) const;
    const RegisteredApp* FindByDisplayName(const std::string& displayName) const;
    const AppEntry* FindCompatibleEntry(const std::string& appId, const std::string& currentArchitecture) const;

    static std::vector<AppRegistrySource> DefaultSources();
    static const char* ToString(AppSourceKind kind);

private:
    bool RegisterApp(const RegisteredApp& app, AppScanResult& result);
    bool ShouldReplaceDuplicate(const RegisteredApp& existingApp, const RegisteredApp& newApp) const;

    bool m_preferSystemAppsOverUserApps = false;
    std::vector<AppRegistrySource> m_sources;
    std::vector<RegisteredApp> m_apps;
    std::map<std::string, size_t> m_appsById;
};

} // namespace apps
} // namespace gxos
