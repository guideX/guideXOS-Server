#include "app_manifest_validator.h"

#include "app_payload_resolver.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace gxos {
namespace apps {
namespace {

bool isRelativePath(const std::string& value) {
    if (value.empty()) return true;
    std::filesystem::path path(value);
    return path.is_relative() && value.find(':') == std::string::npos;
}

bool isSafeRelativePath(const std::string& value) {
    return AppPayloadResolver::IsSafeRelativePath(value);
}

bool isValidFileExtension(const std::string& extension) {
    if (extension.size() < 2 || extension[0] != '.') return false;
    for (size_t i = 1; i < extension.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(extension[i]);
        if (!std::isalnum(c) && extension[i] != '_' && extension[i] != '-') return false;
    }
    return true;
}

void addError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
}

} // namespace

bool AppManifestValidator::IsKnownPermission(const std::string& permission) {
    static const std::vector<std::string> knownPermissions = {
        "filesystem.read",
        "filesystem.write",
        "network.client",
        "network.server",
        "ipc",
        "desktop.window",
        "audio.output",
        "input.keyboard",
        "input.pointer",
        "log",
        "window",
        "draw",
        "file.read",
        "system.settings",
        "hypervisor.guest",
        "service.background"
    };

    return std::find(knownPermissions.begin(), knownPermissions.end(), permission) != knownPermissions.end();
}

AppManifestValidationResult AppManifestValidator::Validate(const AppManifest& manifest) {
    AppManifestValidationResult result;

    if (manifest.schemaVersion != kSupportedAppManifestSchemaVersion) {
        addError(result.errors, "Unsupported manifest schema version: " + std::to_string(manifest.schemaVersion));
    }

    if (manifest.id.empty()) {
        addError(result.errors, "Manifest id is required.");
    }

    if (manifest.displayName.empty()) {
        addError(result.errors, "Manifest displayName is required.");
    }

    if (!IsKnownAppKind(manifest.kind)) {
        addError(result.errors, "Manifest kind is invalid.");
    }

    if (manifest.version.empty()) {
        addError(result.errors, "Manifest version is required.");
    }

    if ((manifest.kind == AppKind::NativeElf || manifest.kind == AppKind::GXAppPackage) && manifest.supportedArchitectures.empty()) {
        addError(result.errors, "NativeElf and GXAppPackage manifests require at least one supported architecture.");
    }

    if (!manifest.icon.empty() && !isRelativePath(manifest.icon)) {
        addError(result.errors, "Manifest icon path must be relative.");
    }

    if (!manifest.executable.empty() && !AppPayloadResolver::IsSafeExecutableName(manifest.executable)) {
        addError(result.errors, "Manifest executable must be a safe relative file name.");
    }

    if ((manifest.kind == AppKind::NativeElf || manifest.kind == AppKind::GXAppPackage) &&
        manifest.entries.empty() && manifest.executable.empty()) {
        addError(result.errors, "Native package requires entries or a canonical executable name.");
    }

    for (const AppEntry& entry : manifest.entries) {
        if (entry.architecture.size() > 16) {
            addError(result.errors, "Invalid manifest entry: architecture identifier is too long.");
        }
        if (!isRelativePath(entry.path) || !isSafeRelativePath(entry.path)) {
            addError(result.errors, "Invalid manifest entry: entry path must be relative: " + entry.path);
        }
        if ((manifest.kind == AppKind::NativeElf || manifest.kind == AppKind::GXAppPackage) && entry.path.empty()) {
            addError(result.errors, "Invalid manifest entry: entry path is required.");
        }
        if ((manifest.kind == AppKind::NativeElf || manifest.kind == AppKind::GXAppPackage) && entry.architecture.empty()) {
            addError(result.errors, "Invalid manifest entry: entry architecture is required.");
        }
    }

    for (const std::string& architecture : manifest.supportedArchitectures) {
        if (architecture.size() > 16) {
            addError(result.errors, "Invalid manifest: architecture identifier is too long.");
        }
    }

    for (size_t i = 0; i < manifest.entries.size(); ++i) {
        if (manifest.entries[i].architecture == "any" || manifest.entries[i].architecture == "*") continue;
        const NativeArchitecture current = NativeArchitectureFromString(manifest.entries[i].architecture.c_str());
        if (current == NativeArchitecture::Unknown) continue;
        for (size_t j = i + 1; j < manifest.entries.size(); ++j) {
            if (NativeArchitectureFromString(manifest.entries[j].architecture.c_str()) == current) {
                addError(result.errors, "Duplicate native payload architecture: " + manifest.entries[i].architecture);
                break;
            }
        }
    }

    for (const std::string& permission : manifest.permissions) {
        if (!IsKnownPermission(permission)) {
            addError(result.errors, "Unknown permission: " + permission);
        }
    }

    for (const FileAssociation& association : manifest.fileAssociations) {
        if (!isValidFileExtension(association.extension)) {
            addError(result.errors, "Invalid file association extension: " + association.extension);
        }
    }

    result.valid = result.errors.empty();
    return result;
}

} // namespace apps
} // namespace gxos
