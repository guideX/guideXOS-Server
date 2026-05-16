#pragma once

#include "app_launch_resolver.h"
#include "native_elf_image_loader.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <chrono>

namespace gxos {
namespace apps {

struct NativeGxAppContext;

constexpr uint32_t kGuideXOSNativeApiVersion = 0u;
constexpr const char* kGuideXOSNativeAbiName = "guidexos-c-abi-v1";

typedef int32_t gx_result;

enum : gx_result {
    GX_OK = 0,
    GX_ERROR_NOT_IMPLEMENTED = -1,
    GX_ERROR_INVALID_ARGUMENT = -2,
    GX_ERROR_UNSUPPORTED = -3,
    GX_ERROR_FAILED = -4,
    GX_ERROR_PERMISSION_DENIED = -5,
    GX_ERROR_INTERNAL = -6,
    GX_ERROR_TIMEOUT = -7
};

typedef uint64_t gx_handle;

enum gx_event_type : uint32_t {
    GX_EVENT_NONE = 0,
    GX_EVENT_WINDOW_CLOSE = 1,
    GX_EVENT_WINDOW_FOCUS = 2,
    GX_EVENT_WINDOW_BLUR = 3,
    GX_EVENT_KEY = 4,
    GX_EVENT_MOUSE = 5,
    GX_EVENT_WINDOW_PAINT = 6
};

struct gx_event {
    uint32_t size = 0;
    gx_event_type type = GX_EVENT_NONE;
    gx_handle window = 0;
    int param1 = 0;
    int param2 = 0;
    int param3 = 0;
    int param4 = 0;
};

struct NativeHostCallTable {
    uint32_t size = 0;
    uint32_t version = kGuideXOSNativeApiVersion;
    gx_result (*log)(NativeGxAppContext* ctx, const char* message) = nullptr;
    uint32_t (*get_api_version)(NativeGxAppContext* ctx) = nullptr;
    gx_result (*request_window)(NativeGxAppContext* ctx, const char* title, int width, int height, gx_handle* outWindow) = nullptr;
    gx_result (*draw_text)(NativeGxAppContext* ctx, gx_handle window, int x, int y, const char* text) = nullptr;
    gx_result (*wait_for_close)(NativeGxAppContext* ctx, gx_handle window, int timeoutMs) = nullptr;
    gx_result (*poll_event)(NativeGxAppContext* ctx, gx_event* outEvent, int timeoutMs) = nullptr;
    gx_result (*exit)(NativeGxAppContext* ctx, gx_result exitCode) = nullptr;
};

enum class NativeAppLifecycleState {
    Created = 0,
    Prepared,
    Running,
    Suspended,
    Closing,
    Exited,
    Failed
};

struct NativeAppRuntimeContext {
    bool success = false;
    std::string appId;
    uint64_t runtimeId = 0;
    std::string displayName;
    std::string architecture;
    uint64_t processId = 0;
    std::string appDirectory;
    std::vector<std::string> permissions;
    NativeHostCallTable hostCalls;
    std::map<std::string, std::string> environment;
    std::vector<std::string> arguments;
    NativeAppLifecycleState lifecycleState = NativeAppLifecycleState::Created;
    int32_t exitCode = 0;
    std::string failureReason;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    std::vector<std::string> diagnostics;
    bool cleanupAttempted = false;
    uint32_t cleanedWindowCount = 0;
    uint32_t hostLogCallCount = 0;
    std::string lastHostLogMessage;
    uint32_t lastApiVersionReturned = 0;
    uint32_t unsupportedHostCallCount = 0;
    std::vector<gx_handle> createdWindowHandles;
    uint32_t requestWindowCallCount = 0;
    gx_handle lastCreatedWindowId = 0;
    std::string lastRequestedWindowTitle;
    gx_result lastRequestWindowResult = GX_OK;
    uint32_t drawTextCallCount = 0;
    gx_handle lastDrawTextWindow = 0;
    std::string lastDrawText;
    gx_result lastDrawTextResult = GX_OK;
    uint32_t waitForCloseCallCount = 0;
    gx_handle lastWaitWindow = 0;
    int lastWaitTimeoutMs = 0;
    gx_result lastWaitResult = GX_OK;
    uint32_t pollEventCallCount = 0;
    gx_event_type lastEventType = GX_EVENT_NONE;
    gx_handle lastEventWindow = 0;
    gx_result lastPollEventResult = GX_OK;
    uint32_t paintEventCount = 0;
    gx_handle lastPaintWindow = 0;
    int lastPaintWidth = 0;
    int lastPaintHeight = 0;
};

struct NativeGxAppContext {
    uint32_t size = 0;
    uint32_t apiVersion = kGuideXOSNativeApiVersion;
    const NativeHostCallTable* host = nullptr;
    void* userData = nullptr;
};

class NativeAppRuntime {
public:
    static NativeAppRuntimeContext Prepare(
        const RegisteredApp& app,
        const LaunchDecision& launchDecision,
        const NativeElfLaunchResult& launchResult,
        const NativeElfImage& image);

    static void BeginHostCallDispatch(NativeAppRuntimeContext& context);
    static void EndHostCallDispatch(NativeAppRuntimeContext& context);
    static void Cleanup(NativeAppRuntimeContext& context, NativeAppLifecycleState finalState, int32_t exitCode, const std::string& failureReason = std::string());
    static const char* ToString(NativeAppLifecycleState state);

private:
    static void LogContext(const NativeAppRuntimeContext& context, const std::string& abi);
};

} // namespace apps
} // namespace gxos
