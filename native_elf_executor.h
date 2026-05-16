#pragma once

#include "native_app_runtime.h"
#include "native_elf_image_loader.h"
#include "native_elf_launch_pipeline.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct NativeElfExecutionResult {
    bool success = false;
    std::string appId;
    std::string architecture;
    int32_t exitCode = 0;
    std::string message;
    std::vector<std::string> diagnostics;
    uint32_t hostLogCallCount = 0;
    std::string lastHostLogMessage;
    uint32_t apiVersionReturned = 0;
    uint32_t requestWindowCallCount = 0;
    uint64_t lastWindowId = 0;
    std::string lastWindowTitle;
    int32_t requestWindowResult = GX_OK;
    uint32_t drawTextCallCount = 0;
    uint64_t lastDrawTextWindow = 0;
    std::string lastDrawText;
    int32_t lastDrawTextResult = GX_OK;
    uint32_t drawRectCallCount = 0;
    uint64_t lastDrawRectWindow = 0;
    int lastDrawRectWidth = 0;
    int lastDrawRectHeight = 0;
    uint32_t lastDrawRectColor = 0;
    int32_t lastDrawRectResult = GX_OK;
    uint32_t waitForCloseCallCount = 0;
    uint64_t lastWaitWindow = 0;
    int lastWaitTimeoutMs = 0;
    int32_t lastWaitResult = GX_OK;
    uint32_t pollEventCallCount = 0;
    gx_event_type lastEventType = GX_EVENT_NONE;
    uint64_t lastEventWindow = 0;
    int32_t lastPollEventResult = GX_OK;
    uint32_t paintEventCount = 0;
    uint64_t lastPaintWindow = 0;
    int lastPaintWidth = 0;
    int lastPaintHeight = 0;
    uint32_t keyEventCount = 0;
    uint64_t lastKeyWindow = 0;
    int lastKeyCode = 0;
    int lastKeyAction = 0;
    int lastKeyModifiers = 0;
    uint32_t mouseEventCount = 0;
    uint64_t lastMouseWindow = 0;
    int lastMouseX = 0;
    int lastMouseY = 0;
    int lastMousePackedButtonAction = 0;
    int lastMouseModifiers = 0;
    uint32_t fileReadCallCount = 0;
    uint32_t fileExistsCallCount = 0;
    std::string lastFilePath;
    uint32_t lastFileReadBytes = 0;
    int32_t lastFileIoResult = GX_OK;
    uint64_t runtimeId = 0;
    std::string lifecycleStateBeforeExecution;
    std::string lifecycleStateAfterExecution;
    bool cleanupAttempted = false;
    uint32_t cleanedWindowCount = 0;
    uint32_t remainingOwnedWindowCount = 0;
    std::string failureReason;
    uint64_t preferredBaseAddress = 0;
    uint64_t actualMappedBaseAddress = 0;
    bool preferredBaseMappingAttempted = false;
    bool preferredBaseMappingSucceeded = false;
    bool trampolineUsed = false;
    uint64_t entryHostAddress = 0;
};

class NativeElfExecutor {
public:
    static bool CanExecute(
        const NativeElfLaunchResult& launchResult,
        const NativeElfImage& image,
        const NativeAppRuntimeContext& runtimeContext,
        std::string* reason = nullptr);

    static NativeElfExecutionResult Execute(
        const NativeElfLaunchResult& launchResult,
        const NativeElfImage& image,
        NativeAppRuntimeContext& runtimeContext);

    static bool ExperimentalExecutionEnabled();

private:
    static void LogDecision(const std::string& appId, const std::string& architecture, bool canExecute, const std::string& reason, const std::string& result);
};

} // namespace apps
} // namespace gxos
