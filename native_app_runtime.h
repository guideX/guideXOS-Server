#pragma once

#include "app_launch_resolver.h"
#include "native_elf_image_loader.h"

#include <cstddef>
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

enum : int {
    GX_KEY_ACTION_UP = 0,
    GX_KEY_ACTION_DOWN = 1,
    GX_KEY_MOD_SHIFT = 1,
    GX_KEY_MOD_CTRL = 2,
    GX_KEY_MOD_ALT = 4
};

enum : int {
    GX_KEY_LEFT = 37,
    GX_KEY_UP = 38,
    GX_KEY_RIGHT = 39,
    GX_KEY_DOWN = 40
};

enum : int {
    GX_MOUSE_ACTION_MOVE = 0,
    GX_MOUSE_ACTION_DOWN = 1,
    GX_MOUSE_ACTION_UP = 2,
    GX_MOUSE_ACTION_DOUBLE_CLICK = 3,
    GX_MOUSE_ACTION_WHEEL = 4,
    GX_MOUSE_BUTTON_NONE = 0,
    GX_MOUSE_BUTTON_LEFT = 1,
    GX_MOUSE_BUTTON_RIGHT = 2,
    GX_MOUSE_BUTTON_MIDDLE = 3
};

constexpr int GX_MOUSE_PACK(int button, int action) { return ((button & 0xFFFF) << 16) | (action & 0xFFFF); }
constexpr int GX_MOUSE_ACTION(int value) { return value & 0xFFFF; }
constexpr int GX_MOUSE_BUTTON(int value) { return (value >> 16) & 0xFFFF; }

struct gx_event {
    uint32_t size = 0;
    gx_event_type type = GX_EVENT_NONE;
    gx_handle window = 0;
    int param1 = 0;
    int param2 = 0;
    int param3 = 0;
    int param4 = 0;
};

enum : uint32_t {
    GX_FILE_TYPE_UNKNOWN = 0,
    GX_FILE_TYPE_REGULAR = 1,
    GX_FILE_TYPE_DIRECTORY = 2
};

struct gx_file_info {
    uint32_t type = GX_FILE_TYPE_UNKNOWN;
    uint32_t reserved = 0;
    uint64_t size = 0;
};

struct gx_file_entry {
    uint32_t type = GX_FILE_TYPE_UNKNOWN;
    uint32_t reserved = 0;
    uint64_t size = 0;
    char name[128] = {};
};

struct NativeHostCallTable {
    uint32_t size = 0;
    uint32_t version = kGuideXOSNativeApiVersion;
    gx_result (*log)(NativeGxAppContext* ctx, const char* message) = nullptr;
    uint32_t (*get_api_version)(NativeGxAppContext* ctx) = nullptr;
    gx_result (*request_window)(NativeGxAppContext* ctx, const char* title, int width, int height, gx_handle* outWindow) = nullptr;
    gx_result (*draw_text)(NativeGxAppContext* ctx, gx_handle window, int x, int y, const char* text) = nullptr;
    gx_result (*draw_rect)(NativeGxAppContext* ctx, gx_handle window, int x, int y, int width, int height, uint32_t color) = nullptr;
    gx_result (*wait_for_close)(NativeGxAppContext* ctx, gx_handle window, int timeoutMs) = nullptr;
    gx_result (*poll_event)(NativeGxAppContext* ctx, gx_event* outEvent, int timeoutMs) = nullptr;
    gx_result (*exit)(NativeGxAppContext* ctx, gx_result exitCode) = nullptr;
    gx_result (*file_read_all)(NativeGxAppContext* ctx, const char* path, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) = nullptr;
    gx_result (*file_exists)(NativeGxAppContext* ctx, const char* path, uint32_t* outExists) = nullptr;
    gx_result (*request_window_ex)(NativeGxAppContext* ctx, const char* title, int width, int height, uint32_t flags, gx_handle* outWindow) = nullptr;
    gx_result (*file_read)(NativeGxAppContext* ctx, const char* path, uint64_t offset, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) = nullptr;
    gx_result (*present_frame)(NativeGxAppContext* ctx, gx_handle window, int x, int y, int width, int height, uint32_t strideBytes, uint32_t pixelFormat, const void* pixels, uint32_t pixelBytes) = nullptr;
    uint64_t (*get_ticks_ms)(NativeGxAppContext* ctx) = nullptr;
    gx_result (*file_stat)(NativeGxAppContext* ctx, const char* path, gx_file_info* outInfo) = nullptr;
    gx_result (*file_read_workspace)(NativeGxAppContext* ctx, const char* path, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) = nullptr;
    gx_result (*file_list)(NativeGxAppContext* ctx, const char* path, gx_file_entry* entries, uint32_t capacity, uint32_t* outCount, uint32_t* outTruncated) = nullptr;
    gx_result (*file_write_all)(NativeGxAppContext* ctx, const char* path, const void* buffer, uint32_t bufferSize, uint32_t* outBytesWritten) = nullptr;
    gx_result (*file_create_directory)(NativeGxAppContext* ctx, const char* path) = nullptr;
    gx_result (*file_remove)(NativeGxAppContext* ctx, const char* path) = nullptr;
};

static_assert(offsetof(NativeHostCallTable, log) == 8, "native ABI log slot changed");
static_assert(offsetof(NativeHostCallTable, get_api_version) == 16, "native ABI version slot changed");
static_assert(offsetof(NativeHostCallTable, request_window) == 24, "native ABI request_window slot changed");
static_assert(offsetof(NativeHostCallTable, file_read_all) == 72, "native ABI file_read_all slot changed");
static_assert(offsetof(NativeHostCallTable, file_exists) == 80, "native ABI file_exists slot changed");
static_assert(offsetof(NativeHostCallTable, request_window_ex) == 88, "native ABI request_window_ex slot changed");
static_assert(offsetof(NativeHostCallTable, file_read) == 96, "native ABI file_read slot changed");
static_assert(offsetof(NativeHostCallTable, present_frame) == 104, "native ABI present_frame slot changed");
static_assert(offsetof(NativeHostCallTable, get_ticks_ms) == 112, "native ABI get_ticks_ms slot changed");
static_assert(offsetof(NativeHostCallTable, file_stat) == 120, "native ABI file_stat slot changed");
static_assert(offsetof(NativeHostCallTable, file_read_workspace) == 128, "native ABI file_read_workspace slot changed");
static_assert(offsetof(NativeHostCallTable, file_list) == 136, "native ABI file_list slot changed");
static_assert(offsetof(NativeHostCallTable, file_write_all) == 144, "native ABI file_write_all slot changed");
static_assert(offsetof(NativeHostCallTable, file_create_directory) == 152, "native ABI file_create_directory slot changed");
static_assert(offsetof(NativeHostCallTable, file_remove) == 160, "native ABI file_remove slot changed");
static_assert(sizeof(NativeHostCallTable) == 168, "native ABI host call table size changed");

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
    uint64_t nativeImageBaseAddress = 0;
    uint64_t nativeImageEndAddress = 0;
    uint64_t nativeStackBaseAddress = 0;
    uint64_t nativeStackEndAddress = 0;
    NativeGxAppContext* activeGxContext = nullptr;
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
    uint32_t drawRectCallCount = 0;
    gx_handle lastDrawRectWindow = 0;
    int lastDrawRectX = 0;
    int lastDrawRectY = 0;
    int lastDrawRectWidth = 0;
    int lastDrawRectHeight = 0;
    uint32_t lastDrawRectColor = 0;
    gx_result lastDrawRectResult = GX_OK;
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
    gx_handle focusedOwnedWindow = 0;
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
    uint32_t fileReadCallCount = 0;
    uint32_t fileReadChunkCallCount = 0;
    uint32_t fileExistsCallCount = 0;
    std::string lastFilePath;
    uint32_t lastFileReadBytes = 0;
    gx_result lastFileIoResult = GX_OK;
    uint64_t lastFileReadOffset = 0;
    uint32_t presentFrameCallCount = 0;
    gx_handle lastPresentFrameWindow = 0;
    int lastPresentFrameX = 0;
    int lastPresentFrameY = 0;
    int lastPresentFrameWidth = 0;
    int lastPresentFrameHeight = 0;
    uint32_t lastPresentFrameStrideBytes = 0;
    uint32_t lastPresentFramePixelFormat = 0;
    uint32_t lastPresentFrameBytes = 0;
    gx_result lastPresentFrameResult = GX_OK;
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
    static void RequestCloseOwnedWindows(NativeAppRuntimeContext& context);
    static void Cleanup(NativeAppRuntimeContext& context, NativeAppLifecycleState finalState, int32_t exitCode, const std::string& failureReason = std::string());
    static const char* ToString(NativeAppLifecycleState state);

private:
    static void LogContext(const NativeAppRuntimeContext& context, const std::string& abi);
};

#ifdef GX_NATIVE_FILESYSTEM_CONTRACT_TEST
bool RunNativeFilesystemContractTest(std::string* failure);
#endif

} // namespace apps
} // namespace gxos
