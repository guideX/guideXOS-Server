#pragma once

#include <string>

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
    std::string originalLabel;
    LaunchTargetType type = LaunchTargetType::Unknown;

    std::string appId;
    std::string displayName;
    std::string dispatchLaunchName;
    std::string legacyAlias;
    std::string shellAction;
    std::string pathParameter;

    bool hostedAvailable = false;
    bool bareMetalAvailable = false;

    std::string diagnosticStatus;
    std::string diagnosticReason;
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
