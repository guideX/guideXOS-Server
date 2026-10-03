#include "app_manifest_validator.h"
#include "app_model_limits.h"
#include "app_activation.h"

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

bool isValidFileExtension(const std::string& extension) {
    if (extension.size() < 2 || extension.size() > kAppModelMaxFileExtensionBytes || extension[0] != '.') return false;
    for (size_t i = 1; i < extension.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(extension[i]);
        const bool asciiAlphaNumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!asciiAlphaNumeric && extension[i] != '_' && extension[i] != '-') return false;
    }
    return true;
}

void addError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
}

bool hasControlCharacter(const std::string& value) {
    for (unsigned char c : value) if (c < 0x20u || c == 0x7fu) return true;
    return false;
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
    } else if (manifest.id.size() > kAppModelMaxAppIdBytes ||
               hasControlCharacter(manifest.id)) {
        addError(result.errors, "Manifest id exceeds the App Model identity bound.");
    }

    if (manifest.displayName.empty()) {
        addError(result.errors, "Manifest displayName is required.");
    } else if (manifest.displayName.size() > kAppModelMaxDisplayNameBytes ||
               hasControlCharacter(manifest.displayName)) {
        addError(result.errors, "Manifest displayName exceeds the App Model display-name bound.");
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
    if (manifest.entries.size() > kAppModelMaxEntriesPerManifest) {
        addError(result.errors, "Manifest entry count exceeds the App Model bound.");
    }

    if (!manifest.icon.empty() && !isRelativePath(manifest.icon)) {
        addError(result.errors, "Manifest icon path must be relative.");
    }
    if (manifest.icon.size() > kAppModelMaxEntryPathBytes || hasControlCharacter(manifest.icon)) {
        addError(result.errors, "Manifest icon path exceeds the App Model bound.");
    }
    if (manifest.supportedArchitectures.size() > kAppModelMaxEntriesPerManifest ||
        manifest.permissions.size() > kAppModelMaxEntriesPerManifest ||
        manifest.fileAssociations.size() > kAppModelMaxFileAssociationsPerApp ||
        manifest.protocols.size() > kAppModelMaxProtocolsPerApp ||
        manifest.actions.size() > kAppModelMaxActionsPerApp ||
        manifest.desktopRegistryHints.size() > kAppModelMaxEntriesPerManifest) {
        addError(result.errors, "Manifest metadata count exceeds the App Model bound.");
    }
    for (const std::string& architecture : manifest.supportedArchitectures) {
        if (architecture.size() > kAppModelMaxDisplayNameBytes || hasControlCharacter(architecture)) {
            addError(result.errors, "Supported architecture value exceeds the App Model bound.");
        }
    }
    for (const auto& hint : manifest.desktopRegistryHints) {
        if (hint.first.size() > kAppModelMaxDisplayNameBytes ||
            hint.second.size() > kAppModelMaxDisplayNameBytes ||
            hasControlCharacter(hint.first) || hasControlCharacter(hint.second)) {
            addError(result.errors, "Desktop registry hint exceeds the App Model bound.");
        }
    }

    for (const AppEntry& entry : manifest.entries) {
        if (entry.path.size() > kAppModelMaxEntryPathBytes || hasControlCharacter(entry.path)) {
            addError(result.errors, "Invalid manifest entry: entry path exceeds the App Model bound.");
        }
        if (entry.architecture.size() > kAppModelMaxDisplayNameBytes ||
            entry.entryPoint.size() > kAppModelMaxDisplayNameBytes ||
            entry.abi.size() > kAppModelMaxDisplayNameBytes ||
            entry.runtime.size() > kAppModelMaxDisplayNameBytes ||
            hasControlCharacter(entry.architecture) || hasControlCharacter(entry.entryPoint) ||
            hasControlCharacter(entry.abi) || hasControlCharacter(entry.runtime)) {
            addError(result.errors, "Manifest entry metadata exceeds the App Model bound.");
        }
        if (!isRelativePath(entry.path)) {
            addError(result.errors, "Invalid manifest entry: entry path must be relative: " + entry.path);
        }
        if ((manifest.kind == AppKind::NativeElf || manifest.kind == AppKind::GXAppPackage) && entry.path.empty()) {
            addError(result.errors, "Invalid manifest entry: entry path is required.");
        }
        if ((manifest.kind == AppKind::NativeElf || manifest.kind == AppKind::GXAppPackage) && entry.architecture.empty()) {
            addError(result.errors, "Invalid manifest entry: entry architecture is required.");
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
        if (association.contentType.size() > kAppModelMaxDisplayNameBytes ||
            association.description.size() > kAppModelMaxDisplayNameBytes ||
            hasControlCharacter(association.contentType) || hasControlCharacter(association.description)) {
            addError(result.errors, "File association metadata exceeds the App Model bound.");
        }
    }

    std::vector<std::string> normalizedProtocols;
    normalizedProtocols.reserve(manifest.protocols.size());
    for (const std::string& protocol : manifest.protocols) {
        std::string normalized;
        if (!NormalizeProtocolScheme(protocol, normalized)) {
            addError(result.errors, "Invalid or overlong protocol scheme: " + protocol);
            continue;
        }
        if (std::find(normalizedProtocols.begin(), normalizedProtocols.end(), normalized) != normalizedProtocols.end()) {
            addError(result.errors, "Duplicate protocol scheme after ASCII case normalization: " + protocol);
            continue;
        }
        normalizedProtocols.push_back(std::move(normalized));
    }
    if (!manifest.protocols.empty() && !manifest.supportsProtocolActivation) {
        addError(result.errors, "Protocol declarations require supportsProtocolActivation=true.");
    }

    std::vector<std::string> actionIds;
    actionIds.reserve(manifest.actions.size());
    for (const AppActionDeclaration& action : manifest.actions) {
        if (!IsValidAppActionId(action.id)) {
            addError(result.errors, "Invalid or overlong application action ID: " + action.id);
        } else if (std::find(actionIds.begin(), actionIds.end(), action.id) != actionIds.end()) {
            addError(result.errors, "Duplicate application action ID: " + action.id);
        } else {
            actionIds.push_back(action.id);
        }
        if (action.label.empty() || action.label.size() > kAppModelMaxActionLabelBytes || hasControlCharacter(action.label)) {
            addError(result.errors, "Application action label is empty, contains controls, or exceeds the App Model bound.");
        }
    }

    result.valid = result.errors.empty();
    return result;
}

} // namespace apps
} // namespace gxos
