#pragma once

#include "app_model_limits.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

constexpr size_t kAppModelMaxDefaultHandlerOverrides = 128;
constexpr size_t kAppModelDefaultHandlerConfigMaxBytes = 32 * 1024;

struct DefaultHandlerOverride {
    std::string extension;
    std::string appId;

    bool operator==(const DefaultHandlerOverride& other) const {
        return extension == other.extension && appId == other.appId;
    }
};

enum class DefaultHandlerStoreLoadStatus {
    NotLoaded = 0,
    Missing,
    Loaded,
    PartiallyLoaded,
    Invalid,
    ReadFailure
};

enum class DefaultHandlerStoreWriteStatus {
    Never = 0,
    Succeeded,
    WriteFailed,
    ReplaceFailed,
    VerificationFailed
};

struct DefaultHandlerStoreDiagnostics {
    DefaultHandlerStoreLoadStatus loadStatus = DefaultHandlerStoreLoadStatus::NotLoaded;
    DefaultHandlerStoreWriteStatus lastWriteStatus = DefaultHandlerStoreWriteStatus::Never;
    size_t overrideCount = 0;
    size_t invalidRecordCount = 0;
};

// AppRegistry's single durable owner for machine-global handler policy.
// The file stores only normalized extensions and canonical App Model IDs.
class DefaultAppHandlerStore {
public:
    explicit DefaultAppHandlerStore(std::string path = "appmodel-default-handlers.cfg");

    bool Reload(std::string& error);
    const std::vector<DefaultHandlerOverride>& Overrides() const { return m_overrides; }
    const DefaultHandlerOverride* Find(const std::string& normalizedExtension) const;
    const DefaultHandlerStoreDiagnostics& Diagnostics() const { return m_diagnostics; }

    // Commits a complete replacement, then rereads and validates the durable
    // file before changing this owner's in-memory snapshot.
    bool Commit(const std::vector<DefaultHandlerOverride>& overrides, std::string& error);

    static bool ParseText(const std::string& text,
                          std::vector<DefaultHandlerOverride>& overrides,
                          size_t& invalidRecordCount,
                          std::string& error);
    static bool Serialize(const std::vector<DefaultHandlerOverride>& overrides,
                          std::string& text,
                          std::string& error);

    static const char* ToString(DefaultHandlerStoreLoadStatus status);
    static const char* ToString(DefaultHandlerStoreWriteStatus status);

private:
    bool ReadCurrent(std::vector<DefaultHandlerOverride>& overrides,
                     size_t& invalidRecordCount,
                     DefaultHandlerStoreLoadStatus& status,
                     std::string& error) const;

    std::string m_path;
    std::vector<DefaultHandlerOverride> m_overrides;
    DefaultHandlerStoreDiagnostics m_diagnostics;
};

} // namespace apps
} // namespace gxos
