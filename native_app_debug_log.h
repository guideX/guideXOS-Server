#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct NativeAppDebugLogEntry {
    uint64_t timestamp = 0;
    uint64_t runtimeId = 0;
    std::string appId;
    std::string severity;
    std::string message;
};

class NativeAppDebugLog {
public:
    static void Add(uint64_t runtimeId, const std::string& appId, const std::string& severity, const std::string& message);
    static std::vector<NativeAppDebugLogEntry> Recent(size_t maxCount);
};

} // namespace apps
} // namespace gxos
