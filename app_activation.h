#pragma once

#include "app_model_limits.h"

#include <cstdint>
#include <string>

namespace gxos {
namespace apps {

enum class AppActivationKind {
    Application = 0,
    Document
};

// Owned activation data carried by launch storage for the lifetime of the app.
// The document path is data; it is never interpreted as a command line.
struct AppActivationContext {
    AppActivationKind kind = AppActivationKind::Application;
    std::string appId;
    std::string documentPath;
    uint64_t registrationOwner = 0;
    uint64_t registrationGeneration = 0;
};

inline bool IsValidDocumentActivationPath(const std::string& path) {
    if (path.empty() || path.size() > kAppModelMaxDocumentPathBytes) return false;
    for (unsigned char byte : path) {
        if (byte == 0 || byte < 0x20u || byte == 0x7fu) return false;
    }
    return true;
}

} // namespace apps
} // namespace gxos
