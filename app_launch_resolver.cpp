#include "app_launch_resolver.h"

#include "app_payload_resolver.h"

#include "logger.h"

#include <sstream>

namespace gxos {
namespace apps {
AppLaunchResolver::AppLaunchResolver(const AppRegistry& registry, const std::string& currentArchitecture)
    : m_registry(registry), m_currentArchitecture(currentArchitecture.empty() ? CurrentArchitecture() : currentArchitecture) {
    const NativeArchitecture architecture = NativeArchitectureFromString(m_currentArchitecture.c_str());
    if (architecture != NativeArchitecture::Unknown) {
        m_currentArchitecture = NativeArchitectureToString(architecture);
    }
}

LaunchDecision AppLaunchResolver::ResolveLaunch(const RegisteredApp& app) const {
    AppLaunchStrategy strategy = StrategyForKind(app.manifest.kind);
    if (app.manifest.id.empty()) return MakeFailure(app, strategy, "Manifest id is required");
    if (!IsArchitectureSupportedByManifest(app.manifest))
        return MakeFailure(app, strategy, "Application does not support current architecture: " + m_currentArchitecture);

    const AppEntry* entry = m_registry.FindCompatibleEntry(app.manifest.id, m_currentArchitecture);
    if (!entry) entry = app.FindCompatibleEntry(m_currentArchitecture);

    AppPayloadResolution payload;
    if (strategy == AppLaunchStrategy::NativeElf || strategy == AppLaunchStrategy::GXAppPackage) {
        payload = AppPayloadResolver::Resolve(app.manifest, app.appDirectory, m_currentArchitecture);
        if (!payload.success) {
            return MakeFailure(app, strategy,
                               std::string(AppPayloadResolver::StatusToString(payload.status)) + ": " + payload.reason);
        }
        entry = &payload.entry;
    }

    switch (strategy) {
    case AppLaunchStrategy::BuiltIn:
        if (!entry) return MakeFailure(app, strategy, "Built-in app has no compatible launch entry");
        break;
    case AppLaunchStrategy::NativeElf:
        if (!entry) return MakeFailure(app, strategy, "Native ELF app has no compatible launch entry");
        if (entry->path.empty()) return MakeFailure(app, strategy, "Native ELF entry path is required");
        if (entry->architecture.empty()) return MakeFailure(app, strategy, "Native ELF entry architecture is required");
        break;
    case AppLaunchStrategy::GXAppPackage:
        if (!entry) return MakeFailure(app, strategy, "GXApp package has no compatible launch entry");
        if (entry->path.empty()) return MakeFailure(app, strategy, "GXApp package entry path is required");
        break;
    case AppLaunchStrategy::Service:
        if (!entry) return MakeFailure(app, strategy, "Service launch placeholder has no compatible entry");
        break;
    case AppLaunchStrategy::HypervisorGuest:
        if (!entry) return MakeFailure(app, strategy, "Hypervisor guest launch placeholder has no compatible entry");
        break;
    case AppLaunchStrategy::Script:
        if (!entry) return MakeFailure(app, strategy, "Script launch placeholder has no compatible entry");
        break;
    case AppLaunchStrategy::Unknown:
    default:
        return MakeFailure(app, strategy, "Unknown app launch strategy");
    }

    LaunchDecision decision;
    decision.success = true;
    decision.strategy = strategy;
    decision.architecture = payload.success ? payload.architectureName : (entry ? entry->architecture : m_currentArchitecture);
    decision.entryPath = payload.success ? payload.relativePath : (entry ? ResolveEntryPath(app, *entry) : std::string());
    decision.runtime = entry ? entry->runtime : std::string();
    decision.reason = "Launch resolved";
    decision.launchName = LaunchNameForApp(app, entry);
    decision.appId = app.manifest.id;
    LogDecision(decision);
    return decision;
}

std::string AppLaunchResolver::CurrentArchitecture() {
    return NativeArchitectureToString(CurrentNativeArchitecture());
}

const char* AppLaunchResolver::ToString(AppLaunchStrategy strategy) {
    switch (strategy) {
    case AppLaunchStrategy::BuiltIn: return "BuiltIn";
    case AppLaunchStrategy::NativeElf: return "NativeElf";
    case AppLaunchStrategy::GXAppPackage: return "GXAppPackage";
    case AppLaunchStrategy::Service: return "Service";
    case AppLaunchStrategy::HypervisorGuest: return "HypervisorGuest";
    case AppLaunchStrategy::Script: return "Script";
    case AppLaunchStrategy::Unknown:
    default: return "Unknown";
    }
}

LaunchDecision AppLaunchResolver::MakeFailure(const RegisteredApp& app, AppLaunchStrategy strategy, const std::string& reason) const {
    LaunchDecision decision;
    decision.success = false;
    decision.strategy = strategy;
    decision.architecture = m_currentArchitecture;
    decision.reason = reason;
    decision.launchName = LaunchNameForApp(app, nullptr);
    decision.appId = app.manifest.id;
    LogDecision(decision);
    return decision;
}

AppLaunchStrategy AppLaunchResolver::StrategyForKind(AppKind kind) const {
    switch (kind) {
    case AppKind::BuiltIn: return AppLaunchStrategy::BuiltIn;
    case AppKind::NativeElf: return AppLaunchStrategy::NativeElf;
    case AppKind::GXAppPackage: return AppLaunchStrategy::GXAppPackage;
    case AppKind::Service: return AppLaunchStrategy::Service;
    case AppKind::HypervisorGuest: return AppLaunchStrategy::HypervisorGuest;
    case AppKind::Script: return AppLaunchStrategy::Script;
    case AppKind::Unknown:
    default: return AppLaunchStrategy::Unknown;
    }
}

bool AppLaunchResolver::IsArchitectureSupportedByManifest(const AppManifest& manifest) const {
    if (manifest.supportedArchitectures.empty()) return true;
    const NativeArchitecture current = NativeArchitectureFromString(m_currentArchitecture.c_str());
    for (const std::string& architecture : manifest.supportedArchitectures) {
        if (architecture == "any" || architecture == "*" ||
            (current != NativeArchitecture::Unknown &&
             NativeArchitectureFromString(architecture.c_str()) == current)) return true;
    }
    return false;
}

std::string AppLaunchResolver::ResolveEntryPath(const RegisteredApp& app, const AppEntry& entry) const {
    std::filesystem::path entryPath(entry.path);
    return entryPath.string();
}

std::string AppLaunchResolver::LaunchNameForApp(const RegisteredApp& app, const AppEntry* entry) const {
    auto hint = app.manifest.desktopRegistryHints.find("registeredName");
    if (hint != app.manifest.desktopRegistryHints.end() && !hint->second.empty()) return hint->second;
    if (entry && !entry->entryPoint.empty()) return entry->entryPoint;
    if (!app.manifest.displayName.empty()) return app.manifest.displayName;
    return app.manifest.id;
}

void AppLaunchResolver::LogDecision(const LaunchDecision& decision) const {
    std::ostringstream oss;
    oss << "[LaunchResolver] "
        << "App: " << decision.appId
        << " Strategy: " << ToString(decision.strategy)
        << " Architecture: " << decision.architecture
        << " Entry: " << decision.entryPath
        << " Result: " << (decision.success ? "success" : "failure")
        << " Reason: " << decision.reason;
    Logger::write(decision.success ? LogLevel::Info : LogLevel::Warn, oss.str());
}

} // namespace apps
} // namespace gxos
