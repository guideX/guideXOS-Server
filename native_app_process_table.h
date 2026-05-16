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
    uint32_t pollEventCallCount = 0;
    gx_event_type lastEventType = GX_EVENT_NONE;
    gx_handle lastEventWindow = 0;
    gx_result lastPollEventResult = GX_OK;
    uint32_t drawRectCallCount = 0;
    gx_handle lastDrawRectWindow = 0;
    int lastDrawRectWidth = 0;
    int lastDrawRectHeight = 0;
    uint32_t lastDrawRectColor = 0;
    gx_result lastDrawRectResult = GX_OK;
    uint32_t paintEventCount = 0;
    gx_handle lastPaintWindow = 0;
    int lastPaintWidth = 0;
    int lastPaintHeight = 0;
    uint32_t keyEventCount = 0;
    gx_handle lastKeyWindow = 0;
    int lastKeyCode = 0;
    int lastKeyAction = 0;
    int lastKeyModifiers = 0;
    uint32_t mouseEventCount = 0;
    gx_handle lastMouseWindow = 0;
    int lastMouseX = 0;
    int lastMouseY = 0;
    int lastMousePackedButtonAction = 0;
    int lastMouseModifiers = 0;
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
    static bool IsNativeProcessId(uint64_t processId);
};

} // namespace apps
} // namespace gxos
