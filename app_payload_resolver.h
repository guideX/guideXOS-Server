#pragma once

#include "app_manifest.h"
#include "kernel/core/include/kernel/native_architecture.h"

#include <filesystem>
#include <string>

namespace gxos {
namespace apps {

enum class AppPayloadResolutionStatus {
    Selected = 0,
    InvalidManifest,
    UnsupportedArchitecture,
    ArchitectureNotAvailable,
    InvalidPath,
    PayloadNotFound
};

struct AppPayloadResolution {
    bool success = false;
    AppPayloadResolutionStatus status = AppPayloadResolutionStatus::InvalidManifest;
    NativeArchitecture architecture = NativeArchitecture::Unknown;
    std::string architectureName;
    std::string relativePath;
    std::filesystem::path payloadPath;
    AppEntry entry;
    std::string reason;
};

class AppPayloadResolver {
public:
    static AppPayloadResolution Resolve(const AppManifest& manifest,
                                        const std::filesystem::path& packageRoot,
                                        NativeArchitecture architecture);
    static AppPayloadResolution Resolve(const AppManifest& manifest,
                                        const std::filesystem::path& packageRoot,
                                        const std::string& architecture);

    static const char* StatusToString(AppPayloadResolutionStatus status);
    static bool IsSafeRelativePath(const std::string& value, size_t maximumLength = 512);
    static bool IsSafeExecutableName(const std::string& value, size_t maximumLength = 128);
};

} // namespace apps
} // namespace gxos
