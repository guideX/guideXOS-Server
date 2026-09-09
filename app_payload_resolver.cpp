#include "app_payload_resolver.h"

#include <system_error>

namespace gxos {
namespace apps {
namespace {

constexpr size_t kMaximumArchitectureLength = 16;

bool is_wildcard(const std::string& value)
{
    return value == "any" || value == "*";
}

bool is_within(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    std::error_code error;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error) return false;
    const std::filesystem::path canonicalCandidate = std::filesystem::weakly_canonical(candidate, error);
    if (error) return false;
    const std::filesystem::path relative = std::filesystem::relative(canonicalCandidate, canonicalRoot, error);
    if (error || relative.empty() || relative == ".") return false;
    const std::string generic = relative.generic_string();
    return generic != ".." && generic.rfind("../", 0) != 0;
}

AppPayloadResolution failure(AppPayloadResolutionStatus status,
                             NativeArchitecture architecture,
                             const std::string& reason)
{
    AppPayloadResolution result;
    result.status = status;
    result.architecture = architecture;
    result.architectureName = NativeArchitectureToString(architecture);
    result.reason = reason;
    return result;
}

bool architecture_is_declared(const AppManifest& manifest, NativeArchitecture architecture)
{
    if (manifest.supportedArchitectures.empty()) return true;
    for (const std::string& value : manifest.supportedArchitectures) {
        if (is_wildcard(value)) return true;
        if (value.size() <= kMaximumArchitectureLength &&
            NativeArchitectureFromString(value.c_str()) == architecture) return true;
    }
    return false;
}

bool exact_entry_for_architecture(const AppManifest& manifest,
                                  NativeArchitecture architecture,
                                  const AppEntry** selected,
                                  bool* duplicate)
{
    if (selected) *selected = nullptr;
    if (duplicate) *duplicate = false;
    for (const AppEntry& entry : manifest.entries) {
        if (is_wildcard(entry.architecture) ||
            NativeArchitectureFromString(entry.architecture.c_str()) != architecture) continue;
        if (selected && *selected) {
            if (duplicate) *duplicate = true;
            return false;
        }
        if (selected) *selected = &entry;
    }
    return true;
}

const AppEntry* wildcard_entry(const AppManifest& manifest)
{
    const AppEntry* selected = nullptr;
    for (const AppEntry& entry : manifest.entries) {
        if (!is_wildcard(entry.architecture)) continue;
        if (selected) return nullptr;
        selected = &entry;
    }
    return selected;
}

AppPayloadResolution select_entry(const std::filesystem::path& packageRoot,
                                  NativeArchitecture architecture,
                                  const AppEntry& entry)
{
    if (!AppPayloadResolver::IsSafeRelativePath(entry.path)) {
        return failure(AppPayloadResolutionStatus::InvalidPath, architecture,
                       "Native payload path is absolute, traverses the package root, or exceeds the path limit");
    }

    const std::filesystem::path candidate = packageRoot / std::filesystem::path(entry.path);
    if (!is_within(packageRoot, candidate)) {
        return failure(AppPayloadResolutionStatus::InvalidPath, architecture,
                       "Native payload path escapes the application package root");
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) || error) {
        return failure(AppPayloadResolutionStatus::PayloadNotFound, architecture,
                       "Selected native payload is missing or is not a regular file");
    }

    AppPayloadResolution result;
    result.success = true;
    result.status = AppPayloadResolutionStatus::Selected;
    result.architecture = architecture;
    result.architectureName = NativeArchitectureToString(architecture);
    result.relativePath = std::filesystem::path(entry.path).generic_string();
    result.payloadPath = candidate;
    result.entry = entry;
    result.reason = "Native payload selected for current architecture";
    return result;
}

} // namespace

const char* AppPayloadResolver::StatusToString(AppPayloadResolutionStatus status)
{
    switch (status) {
    case AppPayloadResolutionStatus::Selected: return "SELECTED";
    case AppPayloadResolutionStatus::InvalidManifest: return "INVALID_MANIFEST";
    case AppPayloadResolutionStatus::UnsupportedArchitecture: return "UNSUPPORTED_ARCHITECTURE";
    case AppPayloadResolutionStatus::ArchitectureNotAvailable: return "APP_ARCHITECTURE_NOT_AVAILABLE";
    case AppPayloadResolutionStatus::InvalidPath: return "INVALID_PAYLOAD_PATH";
    case AppPayloadResolutionStatus::PayloadNotFound: return "PAYLOAD_NOT_FOUND";
    default: return "UNKNOWN";
    }
}

bool AppPayloadResolver::IsSafeRelativePath(const std::string& value, size_t maximumLength)
{
    if (value.empty() || value.size() > maximumLength || value.front() == '/' ||
        value.back() == '/' || value.find('\\') != std::string::npos ||
        value.find(':') != std::string::npos || value.find('\0') != std::string::npos) return false;

    size_t componentStart = 0;
    while (componentStart < value.size()) {
        const size_t separator = value.find('/', componentStart);
        const size_t componentLength = separator == std::string::npos
            ? value.size() - componentStart : separator - componentStart;
        if (componentLength == 0 ||
            (componentLength == 1 && value[componentStart] == '.') ||
            (componentLength == 2 && value[componentStart] == '.' && value[componentStart + 1] == '.')) return false;
        for (size_t i = componentStart; i < componentStart + componentLength; ++i) {
            const unsigned char character = static_cast<unsigned char>(value[i]);
            if (character < 0x20 || character == 0x7f) return false;
        }
        if (separator == std::string::npos) break;
        componentStart = separator + 1;
    }
    return true;
}

bool AppPayloadResolver::IsSafeExecutableName(const std::string& value, size_t maximumLength)
{
    if (!IsSafeRelativePath(value, maximumLength) || value.find('/') != std::string::npos) return false;
    return value != "." && value != "..";
}

AppPayloadResolution AppPayloadResolver::Resolve(const AppManifest& manifest,
                                                 const std::filesystem::path& packageRoot,
                                                 NativeArchitecture architecture)
{
    if (architecture == NativeArchitecture::Unknown) {
        return failure(AppPayloadResolutionStatus::UnsupportedArchitecture, architecture,
                       "Current native architecture is unsupported by the NativeElf package resolver");
    }
    if (manifest.kind != AppKind::NativeElf && manifest.kind != AppKind::GXAppPackage) {
        return failure(AppPayloadResolutionStatus::InvalidManifest, architecture,
                       "Manifest kind does not use native payload resolution");
    }
    if (packageRoot.empty() || manifest.id.empty()) {
        return failure(AppPayloadResolutionStatus::InvalidManifest, architecture,
                       "Native package root and manifest identity are required");
    }
    if (!architecture_is_declared(manifest, architecture)) {
        return failure(AppPayloadResolutionStatus::ArchitectureNotAvailable, architecture,
                       "Application manifest does not declare the current architecture");
    }

    const AppEntry* selected = nullptr;
    bool duplicate = false;
    if (!exact_entry_for_architecture(manifest, architecture, &selected, &duplicate) || duplicate) {
        return failure(AppPayloadResolutionStatus::InvalidManifest, architecture,
                       "Manifest contains duplicate native payload declarations for one architecture");
    }
    if (selected) return select_entry(packageRoot, architecture, *selected);

    const AppEntry* wildcard = wildcard_entry(manifest);
    if (wildcard) return select_entry(packageRoot, architecture, *wildcard);
    if (!manifest.entries.empty()) {
        return failure(AppPayloadResolutionStatus::ArchitectureNotAvailable, architecture,
                       "No native payload entry is declared for the current architecture");
    }

    if (!IsSafeExecutableName(manifest.executable)) {
        return failure(AppPayloadResolutionStatus::InvalidManifest, architecture,
                       "Canonical native executable name is missing or unsafe");
    }
    AppEntry inferred;
    inferred.architecture = NativeArchitectureToString(architecture);
    inferred.path = std::string("bin/") + inferred.architecture + "/" + manifest.executable;
    inferred.entryPoint = "gx_main";
    inferred.abi = "guidexos-c-abi-v1";
    inferred.runtime = "native-elf";
    return select_entry(packageRoot, architecture, inferred);
}

AppPayloadResolution AppPayloadResolver::Resolve(const AppManifest& manifest,
                                                 const std::filesystem::path& packageRoot,
                                                 const std::string& architecture)
{
    if (architecture.size() > kMaximumArchitectureLength) {
        return failure(AppPayloadResolutionStatus::UnsupportedArchitecture,
                       NativeArchitecture::Unknown, "Architecture identifier exceeds the resolver limit");
    }
    const NativeArchitecture parsed = NativeArchitectureFromString(architecture.c_str());
    if (parsed == NativeArchitecture::Unknown) {
        return failure(AppPayloadResolutionStatus::UnsupportedArchitecture, parsed,
                       "Architecture identifier is not a supported canonical native architecture");
    }
    return Resolve(manifest, packageRoot, parsed);
}

} // namespace apps
} // namespace gxos
