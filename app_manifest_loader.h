#pragma once

#include "app_manifest.h"

#include <filesystem>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct AppManifestLoadResult {
    bool valid = false;
    AppManifest manifest;
    std::vector<std::string> errors;
};

class AppManifestLoader {
public:
    static AppManifestLoadResult LoadFromFile(const std::filesystem::path& appJsonPath);
    static AppManifestLoadResult LoadFromDirectory(const std::filesystem::path& appDirectory);
    static AppManifestLoadResult LoadFromString(const std::string& jsonText);
};

} // namespace apps
} // namespace gxos
