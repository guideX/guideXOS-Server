#pragma once

#include <map>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

constexpr int kSupportedAppManifestSchemaVersion = 1;

enum class AppKind {
    Unknown = 0,
    BuiltIn,
    NativeElf,
    GXAppPackage,
    Service,
    HypervisorGuest,
    Script
};

struct AppEntry {
    std::string architecture;
    std::string path;
    std::string entryPoint;
    std::string abi;
    std::string runtime;
};

struct FileAssociation {
    std::string extension;
    std::string contentType;
    std::string description;
};

struct DefaultWindow {
    int width = 0;
    int height = 0;
    bool resizable = true;
};

struct AppManifest {
    int schemaVersion = kSupportedAppManifestSchemaVersion;
    std::string id;
    std::string displayName;
    std::string version;
    std::string publisher;
    std::string description;
    std::string category;
    AppKind kind = AppKind::Unknown;
    std::string icon;
    std::string minGuideXOSVersion;
    std::vector<std::string> supportedArchitectures;
    std::vector<AppEntry> entries;
    std::vector<std::string> permissions;
    std::vector<FileAssociation> fileAssociations;
    DefaultWindow defaultWindow;
    std::map<std::string, std::string> desktopRegistryHints;
};

const char* ToString(AppKind kind);
AppKind AppKindFromString(const std::string& value);
bool IsKnownAppKind(AppKind kind);

} // namespace apps
} // namespace gxos
