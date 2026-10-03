#pragma once

#include "app_model_limits.h"

#include <cstdint>
#include <string>

namespace gxos {
namespace apps {

enum class AppActivationKind {
    Application = 0,
    Document,
    Uri,
    Folder
};

// Owned activation data carried by launch storage for the lifetime of the app.
// The document path is data; it is never interpreted as a command line.
struct AppActivationContext {
    AppActivationKind kind = AppActivationKind::Application;
    std::string appId;
    std::string documentPath;
    std::string uri;
    std::string folderPath;
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

inline bool IsValidFolderActivationPath(const std::string& path) {
    if (path.empty() || path.size() > kAppModelMaxFolderPathBytes) return false;
    for (unsigned char byte : path) {
        if (byte == 0 || byte < 0x20u || byte == 0x7fu) return false;
    }
    return true;
}

// App action IDs are canonical machine identities, never display labels or
// function names. Grammar: 1..48 ASCII bytes, lowercase letters/digits with
// single interior hyphens; first and last bytes must be alphanumeric.
inline bool IsValidAppActionId(const std::string& id) {
    if (id.empty() || id.size() > kAppModelMaxActionIdBytes) return false;
    bool previousHyphen = false;
    for (size_t i = 0; i < id.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(id[i]);
        const bool alpha = byte >= 'a' && byte <= 'z';
        const bool digit = byte >= '0' && byte <= '9';
        if (alpha || digit) {
            previousHyphen = false;
            continue;
        }
        if (byte != '-' || i == 0 || i + 1 == id.size() || previousHyphen) return false;
        previousHyphen = true;
    }
    return true;
}

inline bool NormalizeProtocolScheme(const std::string& scheme, std::string& normalized) {
    normalized.clear();
    if (scheme.empty() || scheme.size() > kAppModelMaxProtocolSchemeBytes) return false;
    normalized.reserve(scheme.size());
    for (size_t i = 0; i < scheme.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(scheme[i]);
        const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        if (i == 0 ? !alpha : (!alpha && !digit && ch != '+' && ch != '-' && ch != '.')) {
            normalized.clear();
            return false;
        }
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<unsigned char>(ch - 'A' + 'a');
        normalized.push_back(static_cast<char>(ch));
    }
    return true;
}

inline bool GetUriActivationScheme(const std::string& uri, std::string& scheme) {
    scheme.clear();
    if (uri.empty() || uri.size() > kAppModelMaxUriBytes) return false;
    for (unsigned char byte : uri) {
        if (byte == 0 || byte < 0x20u || byte == 0x7fu) return false;
    }
    const size_t colon = uri.find(':');
    if (colon == std::string::npos) return false;
    return NormalizeProtocolScheme(uri.substr(0, colon), scheme);
}

inline bool IsValidUriActivationUri(const std::string& uri) {
    std::string ignored;
    return GetUriActivationScheme(uri, ignored);
}

} // namespace apps
} // namespace gxos
