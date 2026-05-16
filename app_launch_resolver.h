#pragma once

#include "app_registry.h"

#include <string>

namespace gxos {
namespace apps {

enum class AppLaunchStrategy {
    BuiltIn = 0,
    NativeElf,
    GXAppPackage,
    Service,
    HypervisorGuest,
    Script,
    Unknown
};

struct LaunchDecision {
    bool success = false;
    AppLaunchStrategy strategy = AppLaunchStrategy::Unknown;
    std::string architecture;
    std::string entryPath;
    std::string runtime;
    std::string reason;
    std::string launchName;
    std::string appId;
};

class AppLaunchResolver {
public:
    AppLaunchResolver(const AppRegistry& registry, const std::string& currentArchitecture);

    LaunchDecision ResolveLaunch(const RegisteredApp& app) const;

    static std::string CurrentArchitecture();
    static const char* ToString(AppLaunchStrategy strategy);

private:
    const AppRegistry& m_registry;
    std::string m_currentArchitecture;

    LaunchDecision MakeFailure(const RegisteredApp& app, AppLaunchStrategy strategy, const std::string& reason) const;
    AppLaunchStrategy StrategyForKind(AppKind kind) const;
    bool IsArchitectureSupportedByManifest(const AppManifest& manifest) const;
    std::string ResolveEntryPath(const RegisteredApp& app, const AppEntry& entry) const;
    std::string LaunchNameForApp(const RegisteredApp& app, const AppEntry* entry) const;
    void LogDecision(const LaunchDecision& decision) const;
};

} // namespace apps
} // namespace gxos
