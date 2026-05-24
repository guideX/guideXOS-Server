#pragma once

#ifdef GXOS_BARE_METAL
#include <stddef.h>
#else
#include <string>
#endif

namespace gxos {
namespace apps {

enum class LaunchTargetType {
    Unknown = 0,
    BuiltInApp,
    ManifestApp,
    NativeElfApp,
    GXAppPackage,
    ShellAction,
    LegacyAlias,
    FileOpen,
    CrossArchEmulatedApp,
    Service,
    HypervisorGuest,
    Script
};

struct LaunchTarget {
#ifdef GXOS_BARE_METAL
    const char* originalLabel = "";
#else
    std::string originalLabel;
#endif
    LaunchTargetType type = LaunchTargetType::Unknown;

#ifdef GXOS_BARE_METAL
    const char* appId = "";
    const char* displayName = "";
    const char* dispatchLaunchName = "";
    const char* legacyAlias = "";
    const char* shellAction = "";
    const char* pathParameter = "";
#else
    std::string appId;
    std::string displayName;
    std::string dispatchLaunchName;
    std::string legacyAlias;
    std::string shellAction;
    std::string pathParameter;
#endif

    bool hostedAvailable = false;
    bool bareMetalAvailable = false;

#ifdef GXOS_BARE_METAL
    const char* diagnosticStatus = "";
    const char* diagnosticReason = "";
#else
    std::string diagnosticStatus;
    std::string diagnosticReason;
#endif
};

inline const char* ToString(LaunchTargetType type) {
    switch (type) {
    case LaunchTargetType::BuiltInApp: return "BuiltInApp";
    case LaunchTargetType::ManifestApp: return "ManifestApp";
    case LaunchTargetType::NativeElfApp: return "NativeElfApp";
    case LaunchTargetType::GXAppPackage: return "GXAppPackage";
    case LaunchTargetType::ShellAction: return "ShellAction";
    case LaunchTargetType::LegacyAlias: return "LegacyAlias";
    case LaunchTargetType::FileOpen: return "FileOpen";
    case LaunchTargetType::CrossArchEmulatedApp: return "CrossArchEmulatedApp";
    case LaunchTargetType::Service: return "Service";
    case LaunchTargetType::HypervisorGuest: return "HypervisorGuest";
    case LaunchTargetType::Script: return "Script";
    case LaunchTargetType::Unknown:
    default: return "Unknown";
    }
}

} // namespace apps
} // namespace gxos
