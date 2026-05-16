#include "native_app_debug_log.h"

#include <algorithm>
#include <mutex>

namespace gxos {
namespace apps {
namespace {

constexpr size_t kMaxNativeAppDebugLogEntries = 200;
std::mutex g_debugLogMutex;
std::vector<NativeAppDebugLogEntry> g_debugLogEntries;
uint64_t g_nextDebugLogTimestamp = 1;

} // namespace

void NativeAppDebugLog::Add(uint64_t runtimeId, const std::string& appId, const std::string& severity, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_debugLogMutex);

    NativeAppDebugLogEntry entry;
    entry.timestamp = g_nextDebugLogTimestamp++;
    entry.runtimeId = runtimeId;
    entry.appId = appId;
    entry.severity = severity;
    entry.message = message;
    g_debugLogEntries.push_back(entry);

    if (g_debugLogEntries.size() > kMaxNativeAppDebugLogEntries) {
        g_debugLogEntries.erase(g_debugLogEntries.begin(), g_debugLogEntries.begin() + static_cast<std::ptrdiff_t>(g_debugLogEntries.size() - kMaxNativeAppDebugLogEntries));
    }
}

std::vector<NativeAppDebugLogEntry> NativeAppDebugLog::Recent(size_t maxCount) {
    std::lock_guard<std::mutex> lock(g_debugLogMutex);

    if (maxCount == 0 || g_debugLogEntries.empty()) return {};
    size_t count = std::min(maxCount, g_debugLogEntries.size());
    return std::vector<NativeAppDebugLogEntry>(g_debugLogEntries.end() - static_cast<std::ptrdiff_t>(count), g_debugLogEntries.end());
}

} // namespace apps
} // namespace gxos
