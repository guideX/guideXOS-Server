#include "app_manifest.h"

namespace gxos {
namespace apps {

const char* ToString(AppKind kind) {
    switch (kind) {
    case AppKind::BuiltIn: return "BuiltIn";
    case AppKind::NativeElf: return "NativeElf";
    case AppKind::GXAppPackage: return "GXAppPackage";
    case AppKind::Service: return "Service";
    case AppKind::HypervisorGuest: return "HypervisorGuest";
    case AppKind::Script: return "Script";
    case AppKind::Unknown:
    default: return "Unknown";
    }
}

AppKind AppKindFromString(const std::string& value) {
    if (value == "BuiltIn" || value == "built-in" || value == "builtin") return AppKind::BuiltIn;
    if (value == "NativeElf" || value == "native-elf") return AppKind::NativeElf;
    if (value == "GXAppPackage" || value == "gxapp-package") return AppKind::GXAppPackage;
    if (value == "Service" || value == "service") return AppKind::Service;
    if (value == "HypervisorGuest" || value == "hypervisor-guest") return AppKind::HypervisorGuest;
    if (value == "Script" || value == "script") return AppKind::Script;
    return AppKind::Unknown;
}

bool IsKnownAppKind(AppKind kind) {
    return kind != AppKind::Unknown;
}

} // namespace apps
} // namespace gxos
