#pragma once

#include "native_app_runtime.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct NativeAppProcessInfo {
    uint64_t nativePid = 0;
    uint64_t runtimeId = 0;
    std::string appId;
    std::string displayName;
    std::string architecture;
    NativeAppLifecycleState lifecycleState = NativeAppLifecycleState::Created;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    int32_t exitCode = 0;
    std::string failureReason;
    uint32_t createdWindowCount = 0;
    uint32_t cleanedWindowCount = 0;
    uint32_t remainingWindowCount = 0;
    bool experimentalExecutionEnabled = false;
    std::string hostArchitecture;
};

class NativeAppProcessTable {
public:
    static uint64_t AllocateRuntimeId();
    static void RegisterPrepared(const NativeAppRuntimeContext& context, bool experimentalExecutionEnabled, const std::string& hostArchitecture);
    static void MarkRunning(uint64_t runtimeId);
    static void UpdateFromRuntime(const NativeAppRuntimeContext& context);
    static void MarkCompleted(uint64_t runtimeId, NativeAppLifecycleState state, int32_t exitCode, const std::string& failureReason);
    static std::vector<NativeAppProcessInfo> List();
    static bool Find(uint64_t runtimeId, NativeAppProcessInfo& outInfo);
};

} // namespace apps
} // namespace gxos
