#pragma once

#include "app_manifest.h"

#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct AppManifestValidationResult {
    bool valid = false;
    std::vector<std::string> errors;
};

class AppManifestValidator {
public:
    static AppManifestValidationResult Validate(const AppManifest& manifest);
    static bool IsKnownPermission(const std::string& permission);
};

} // namespace apps
} // namespace gxos
