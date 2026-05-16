#include "app_launch_resolver.h"

#include "logger.h"

#include <sstream>

namespace gxos {
namespace apps {
namespace {

bool architectureMatches(const std::string& candidate, const std::string& currentArchitecture) {
    return candidate == currentArchitecture || candidate == "any" || candidate == "*";
}

} // namespace

AppLaunchResolver::AppLaunchResolver(const AppRegistry& registry, const std::string& currentArchitecture)
    : m_registry(registry), m_currentArchitecture(currentArchitecture.empty() ? CurrentArchitecture() : currentArchitecture) {
}

LaunchDecision AppLaunchResolver::ResolveLaunch(const RegisteredApp& app) const {
    AppLaunchStrategy strategy = StrategyForKind(app.manifest.kind);
    const AppEntry* entry = m_registry.FindCompatibleEntry(app.manifest.id, m_currentArchitecture);
    if (!entry) entry = app.FindCompatibleEntry(m_currentArchitecture);

    if (app.manifest.id.empty()) return MakeFailure(app, strategy, "Manifest id is required");
    if (!IsArchitectureSupportedByManifest(app.manifest)) return MakeFailure(app, strategy, "Application does not support current architecture: " + m_currentArchitecture);

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
    decision.architecture = entry ? entry->architecture : m_currentArchitecture;
    decision.entryPath = entry ? ResolveEntryPath(app, *entry) : std::string();
    decision.runtime = entry ? entry->runtime : std::string();
    decision.reason = "Launch resolved";
    decision.launchName = LaunchNameForApp(app, entry);
    decision.appId = app.manifest.id;
    LogDecision(decision);
    return decision;
}

std::string AppLaunchResolver::CurrentArchitecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return "amd64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__ia64__) || defined(_M_IA64)
    return "ia64";
#elif defined(__mips64)
    return "mips64";
#elif defined(__sparc__) && defined(__arch64__)
    return "sparc64";
#elif defined(__sparc__)
    return "sparc";
#elif defined(__powerpc64__) || defined(__ppc64__)
    return "ppc64";
#elif defined(__riscv) && (__riscv_xlen == 64)
    return "riscv64";
#elif defined(__loongarch64)
    return "loongarch64";
#else
    return "x86";
#endif
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
    for (const std::string& architecture : manifest.supportedArchitectures) {
        if (architectureMatches(architecture, m_currentArchitecture)) return true;
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
