#include "native_app_runtime.h"

#include "allocator.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "native_app_debug_log.h"
#include "native_app_process_table.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace gxos {
namespace apps {
namespace {

NativeAppLifecycleState g_hostLifecycleState = NativeAppLifecycleState::Created;
NativeAppRuntimeContext* g_activeRuntimeContext = nullptr;
std::atomic<uint64_t> g_nextRuntimeId{ 1 };

constexpr int kMinWindowWidth = 64;
constexpr int kMinWindowHeight = 64;
constexpr int kMaxWindowWidth = 4096;
constexpr int kMaxWindowHeight = 4096;
constexpr int kMinDrawCoordinate = 0;
constexpr int kMaxDrawCoordinate = 16384;
constexpr int kMinDrawSize = 1;
constexpr int kMaxDrawSize = 4096;
// A compositor can briefly drain queued input/paint work between repeated
// Native ELF launches. Keep window creation bounded, but allow that queue to
// clear before reporting a false compositor-unavailable result.
constexpr uint64_t kWindowCreateTimeoutMs = 5000;
constexpr int kMinWaitForCloseTimeoutMs = 0;
constexpr int kMaxWaitForCloseTimeoutMs = 300000;
constexpr int kMinPollEventTimeoutMs = 0;
constexpr int kMaxPollEventTimeoutMs = 30000;
constexpr uint32_t kMaxFilePathLength = 240;
constexpr uint32_t kMaxFileReadBytes = 64u * 1024u;
constexpr uint64_t kMaxPresentFrameBytes = 16ull * 1024ull * 1024ull;
constexpr uint64_t kMaxNativeAppStackBytes = 8ull * 1024ull * 1024ull;

bool frameDiagnosticsEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("GXOS_PACMAN_FRAME_DIAGNOSTICS");
        if (!value || !*value) return false;
        std::string lower;
        lower.reserve(std::char_traits<char>::length(value));
        for (const char* p = value; *p; ++p) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        }
        return !(lower == "0" || lower == "false" || lower == "off" || lower == "no");
    }();
    return enabled;
}

std::string appLabel(const NativeAppRuntimeContext* context) {
    if (!context) return "<unknown>";
    if (!context->displayName.empty()) return context->appId + " (" + context->displayName + ")";
    return context->appId.empty() ? "<unknown>" : context->appId;
}

bool parseFramePayload(const std::string& payload, uint64_t& id, int& width, int& height) {
    std::istringstream iss(payload);
    std::string idText;
    std::string widthText;
    std::string heightText;
    std::getline(iss, idText, '|');
    std::getline(iss, widthText, '|');
    std::getline(iss, heightText, '|');
    if (idText.empty()) return false;
    try {
        id = std::stoull(idText);
        width = widthText.empty() ? 0 : std::stoi(widthText);
        height = heightText.empty() ? 0 : std::stoi(heightText);
        return id != 0 && width >= 0 && height >= 0;
    } catch (...) {
        return false;
    }
}

bool experimentalExecutionEnabled() {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    return true;
#else
    return false;
#endif
}

NativeAppRuntimeContext* runtimeContextFor(NativeGxAppContext* ctx) {
    if (!ctx) return nullptr;
    if (!g_activeRuntimeContext || ctx != g_activeRuntimeContext->activeGxContext) return nullptr;
    if (!ctx->host) return nullptr;
    if (ctx->size < sizeof(NativeGxAppContext)) return nullptr;
    if (ctx->host == &g_activeRuntimeContext->hostCalls) return g_activeRuntimeContext;
    return nullptr;
}

std::string pointerText(const void* pointer) {
    std::ostringstream oss;
    oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(pointer) << std::dec;
    return oss.str();
}

bool addressRangeContains(uint64_t base, uint64_t end, uint64_t address, uint64_t bytes) {
    return end > base && address >= base && address < end && bytes <= end - address;
}

bool nativeBufferRangeContains(const NativeAppRuntimeContext& context, const void* pointer, uint64_t bytes) {
    if (!pointer || bytes == 0) return false;

    const uint64_t address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
    return addressRangeContains(context.nativeImageBaseAddress, context.nativeImageEndAddress, address, bytes) ||
        addressRangeContains(context.nativeStackBaseAddress, context.nativeStackEndAddress, address, bytes);
}

std::string nativeImageRangeClassification(const NativeAppRuntimeContext& context, const void* pointer, uint64_t bytes) {
    if (!pointer) return "null";
    if (bytes == 0) return "zero-length";
    const uint64_t address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
    if (addressRangeContains(context.nativeImageBaseAddress, context.nativeImageEndAddress, address, bytes)) return "inside-native-image";
    if (addressRangeContains(context.nativeStackBaseAddress, context.nativeStackEndAddress, address, bytes)) return "inside-active-native-stack";
    if (context.nativeImageEndAddress <= context.nativeImageBaseAddress && context.nativeStackEndAddress <= context.nativeStackBaseAddress) return "native-memory-ranges-unavailable";
    return "outside-native-memory";
}

bool copyNativePath(const NativeAppRuntimeContext& context, const char* path, std::string& output, std::string& failureReason) {
    output.clear();
    failureReason.clear();
    if (!path) {
        failureReason = "null-path";
        return false;
    }

    const uint64_t start = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(path));
    for (uint32_t i = 0; i <= kMaxFilePathLength; ++i) {
        if (start > std::numeric_limits<uint64_t>::max() - i) {
            failureReason = "path-address-overflow";
            return false;
        }

        const char* current = reinterpret_cast<const char*>(static_cast<uintptr_t>(start + i));
        if (!nativeBufferRangeContains(context, current, 1)) {
            failureReason = "path-outside-native-image";
            return false;
        }

        const char value = *current;
        if (value == '\0') return true;
        output.push_back(value);
    }

    failureReason = "path-not-null-terminated-within-limit";
    return false;
}

void logInvalidFileReadContext(
    NativeGxAppContext* ctx,
    const char* operation,
    uint64_t offset,
    uint32_t requestedBytes,
    const void* destination,
    const uint32_t* outBytesRead,
    gx_result result) {
    const bool active = g_activeRuntimeContext != nullptr;
    const bool contextPointerMatches = active && ctx != nullptr && ctx == g_activeRuntimeContext->activeGxContext;
    std::ostringstream oss;
    oss << "[NativeAppHost] " << operation
        << " phase=context-resolution"
        << " runtimeId=" << (active ? std::to_string(g_activeRuntimeContext->runtimeId) : "<none>")
        << " appId=" << (active ? g_activeRuntimeContext->appId : "<none>")
        << " processId=" << (active && g_activeRuntimeContext->processId != 0 ? std::to_string(g_activeRuntimeContext->processId) : std::to_string(Allocator::currentPid()))
        << " contextActive=" << (active ? "yes" : "no")
        << " contextPointerMatched=" << (contextPointerMatches ? "yes" : "no")
        << " requestedPath=<unavailable>"
        << " offset=" << offset
        << " length=" << requestedBytes
        << " destination=" << pointerText(destination)
        << " destinationRange=<unavailable>"
        << " outBytesRead=" << pointerText(outBytesRead)
        << " result=" << result
        << " actualHostRead=not-started"
        << " returnPropagation=about-to-return";
    Logger::write(LogLevel::Warn, oss.str());
}

void logFileReadBoundary(
    const NativeAppRuntimeContext& context,
    const char* operation,
    const std::string& requestedPath,
    const std::string& resolvedPath,
    uint64_t offset,
    uint32_t requestedBytes,
    const void* destination,
    uint32_t bytesRead,
    const uint32_t* outBytesRead,
    const char* phase,
    const char* actualHostRead,
    bool contextActive,
    bool contextMatched,
    gx_result result) {
    std::ostringstream oss;
    oss << "[NativeAppHost] " << operation
        << " phase=" << phase
        << " runtimeId=" << context.runtimeId
        << " appId=" << context.appId
        << " processId=" << (context.processId != 0 ? context.processId : Allocator::currentPid())
        << " contextActive=" << (contextActive ? "yes" : "no")
        << " contextPointerMatched=" << (contextMatched ? "yes" : "no")
        << " requestedPath=\"" << (requestedPath.empty() ? "<unavailable>" : requestedPath) << "\""
        << " offset=" << offset
        << " length=" << requestedBytes
        << " destination=" << pointerText(destination)
        << " destinationRange=" << nativeImageRangeClassification(context, destination, requestedBytes)
        << " outBytesRead=" << pointerText(outBytesRead)
        << " outBytesReadRange=" << nativeImageRangeClassification(context, outBytesRead, sizeof(uint32_t))
        << " resolvedPackagePath=\"" << context.appDirectory << "\""
        << " resolvedPath=\"" << (resolvedPath.empty() ? "<none>" : resolvedPath) << "\""
        << " bytesRead=" << bytesRead
        << " result=" << result
        << " actualHostRead=" << actualHostRead
        << " returnPropagation=about-to-return";
    Logger::write(result == GX_OK ? LogLevel::Info : LogLevel::Warn, oss.str());
    NativeAppDebugLog::Add(context.runtimeId, context.appId, result == GX_OK ? "info" : "warn", oss.str());
}

bool hasPermission(const NativeAppRuntimeContext& context, const std::string& permission) {
    for (const std::string& granted : context.permissions) {
        if (granted == permission) return true;
    }
    return false;
}

bool pathContainsTraversal(const std::string& path) {
    std::string::size_type segmentStart = 0;
    for (std::string::size_type i = 0; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/' && path[i] != static_cast<char>(92)) continue;
        if (i - segmentStart == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.') return true;
        segmentStart = i + 1;
    }
    return false;
}

gx_result resolveFileReadPath(NativeAppRuntimeContext& context, const char* path, std::string& resolvedPath) {
    context.lastFilePath.clear();

    std::string requestedPath;
    std::string pathFailure;
    if (!copyNativePath(context, path, requestedPath, pathFailure) || requestedPath.empty()) {
        context.lastFilePath = pathFailure.empty() ? "<empty-path>" : "<" + pathFailure + ">";
        return GX_ERROR_INVALID_ARGUMENT;
    }

    context.lastFilePath = requestedPath;
    if (requestedPath.size() > kMaxFilePathLength) return GX_ERROR_INVALID_ARGUMENT;
    if (requestedPath.find(':') != std::string::npos) return GX_ERROR_PERMISSION_DENIED;
    if (!context.appDirectory.empty() && requestedPath.rfind(context.appDirectory, 0) == 0) return GX_ERROR_PERMISSION_DENIED;
    if (requestedPath[0] == '/' || requestedPath[0] == static_cast<char>(92)) return GX_ERROR_PERMISSION_DENIED;
    if (requestedPath.size() >= 2 && requestedPath[1] == ':') return GX_ERROR_PERMISSION_DENIED;
    if (pathContainsTraversal(requestedPath)) return GX_ERROR_PERMISSION_DENIED;
    if (context.appDirectory.empty()) return GX_ERROR_INVALID_ARGUMENT;

    resolvedPath = context.appDirectory;
    if (!resolvedPath.empty() && resolvedPath.back() != '/' && resolvedPath.back() != static_cast<char>(92)) resolvedPath.push_back('/');
    resolvedPath += requestedPath;
    return GX_OK;
}

bool parseWindowId(const std::string& payload, uint64_t& id) {
    std::istringstream iss(payload);
    std::string idText;
    std::getline(iss, idText, '|');
    if (idText.empty()) return false;
    try {
        id = std::stoull(idText);
        return id != 0;
    } catch (...) {
        return false;
    }
}

bool tryParseInt(const std::string& text, int& value) {
    if (text.empty()) return false;
    try {
        size_t consumed = 0;
        int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool tryParseUnsigned64(const std::string& text, uint64_t& value) {
    if (text.empty()) return false;
    try {
        size_t consumed = 0;
        uint64_t parsed = std::stoull(text, &consumed);
        if (consumed != text.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseKeyPayload(const std::string& payload, int& keyCode, int& action, int& modifiers, uint64_t& window) {
    std::istringstream iss(payload);
    std::string keyCodeText;
    std::string actionText;
    std::string modifiersText;
    std::string windowText;
    std::getline(iss, keyCodeText, '|');
    std::getline(iss, actionText, '|');
    std::getline(iss, modifiersText, '|');
    std::getline(iss, windowText, '|');

    if (!tryParseInt(keyCodeText, keyCode) || keyCode < 0) return false;
    if (actionText == "down") {
        action = GX_KEY_ACTION_DOWN;
    } else if (actionText == "up") {
        action = GX_KEY_ACTION_UP;
    } else if (tryParseInt(actionText, action)) {
        if (action != GX_KEY_ACTION_UP && action != GX_KEY_ACTION_DOWN) return false;
    } else {
        return false;
    }

    modifiers = 0;
    if (!modifiersText.empty() && !tryParseInt(modifiersText, modifiers)) return false;
    modifiers &= GX_KEY_MOD_SHIFT | GX_KEY_MOD_CTRL | GX_KEY_MOD_ALT;

    window = 0;
    if (!windowText.empty() && !tryParseUnsigned64(windowText, window)) return false;
    return true;
}

bool parseMousePayload(const std::string& payload, int& x, int& y, int& packedButtonAction, int& modifiers, uint64_t& window) {
    std::istringstream iss(payload);
    std::string xText;
    std::string yText;
    std::string buttonText;
    std::string actionText;
    std::string modifiersText;
    std::string windowText;
    std::getline(iss, xText, '|');
    std::getline(iss, yText, '|');
    std::getline(iss, buttonText, '|');
    std::getline(iss, actionText, '|');
    std::getline(iss, modifiersText, '|');
    std::getline(iss, windowText, '|');

    int button = GX_MOUSE_BUTTON_NONE;
    int action = GX_MOUSE_ACTION_MOVE;
    int wheelDelta = 0;
    if (!tryParseInt(xText, x) || !tryParseInt(yText, y)) return false;
    if (!tryParseInt(buttonText, button)) return false;
    if (button < GX_MOUSE_BUTTON_NONE || button > GX_MOUSE_BUTTON_MIDDLE) return false;

    if (actionText == "move") {
        action = GX_MOUSE_ACTION_MOVE;
    } else if (actionText == "down") {
        action = GX_MOUSE_ACTION_DOWN;
    } else if (actionText == "up") {
        action = GX_MOUSE_ACTION_UP;
    } else if (actionText == "double" || actionText == "double_click") {
        action = GX_MOUSE_ACTION_DOUBLE_CLICK;
    } else if (actionText.rfind("wheel", 0) == 0) {
        action = GX_MOUSE_ACTION_WHEEL;
        if (actionText == "wheel" || actionText == "wheelup") {
            wheelDelta = 1;
        } else if (actionText == "wheeldown") {
            wheelDelta = -1;
        } else {
            size_t colon = actionText.find(':');
            if (colon == std::string::npos || colon + 1 >= actionText.size()) return false;
            if (!tryParseInt(actionText.substr(colon + 1), wheelDelta) || wheelDelta == 0) return false;
        }
    } else if (tryParseInt(actionText, action)) {
        if (action < GX_MOUSE_ACTION_MOVE || action > GX_MOUSE_ACTION_WHEEL) return false;
    } else {
        return false;
    }

    modifiers = 0;
    if (!modifiersText.empty() && !tryParseInt(modifiersText, modifiers)) return false;
    if (action == GX_MOUSE_ACTION_WHEEL) modifiers = wheelDelta;

    window = 0;
    if (!windowText.empty() && !tryParseUnsigned64(windowText, window)) return false;
    packedButtonAction = GX_MOUSE_PACK(button, action);
    return true;
}

bool ownsWindow(const NativeAppRuntimeContext& context, gx_handle window) {
    for (gx_handle createdWindow : context.createdWindowHandles) {
        if (createdWindow == window) return true;
    }
    return false;
}

void removeOwnedWindow(NativeAppRuntimeContext& context, gx_handle window) {
    auto it = std::remove(context.createdWindowHandles.begin(), context.createdWindowHandles.end(), window);
    context.createdWindowHandles.erase(it, context.createdWindowHandles.end());
}

void initializeEvent(gx_event* outEvent) {
    if (!outEvent) return;
    outEvent->size = static_cast<uint32_t>(sizeof(gx_event));
    outEvent->type = GX_EVENT_NONE;
    outEvent->window = 0;
    outEvent->param1 = 0;
    outEvent->param2 = 0;
    outEvent->param3 = 0;
    outEvent->param4 = 0;
}

gx_event_type eventTypeForMessage(uint32_t messageType) {
    if (messageType == static_cast<uint32_t>(gui::MsgType::MT_Close)) return GX_EVENT_WINDOW_CLOSE;
    if (messageType == static_cast<uint32_t>(gui::MsgType::MT_SetFocus)) return GX_EVENT_WINDOW_FOCUS;
    if (messageType == static_cast<uint32_t>(gui::MsgType::MT_ClearFocus)) return GX_EVENT_WINDOW_BLUR;
    if (messageType == static_cast<uint32_t>(gui::MsgType::MT_InputKey)) return GX_EVENT_KEY;
    if (messageType == static_cast<uint32_t>(gui::MsgType::MT_InputMouse)) return GX_EVENT_MOUSE;
    if (messageType == static_cast<uint32_t>(gui::MsgType::MT_RequestFrame)) return GX_EVENT_WINDOW_PAINT;
    return GX_EVENT_NONE;
}

void requestPaintForOwnedWindows(NativeAppRuntimeContext& context) {
    uint64_t nativeAppPid = context.processId != 0 ? context.processId : Allocator::currentPid();
    if (context.processId == 0) context.processId = nativeAppPid;
    for (gx_handle window : context.createdWindowHandles) {
        if (window == 0) continue;
        ipc::Message request;
        request.srcPid = nativeAppPid;
        request.type = static_cast<uint32_t>(gui::MsgType::MT_RequestFrame);
        std::string payload = std::to_string(window);
        request.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(request), false);
    }
}

gx_result hostLog(NativeGxAppContext* ctx, const char* message) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] log rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    const char* safeMessage = message ? message : "<null>";
    ++context->hostLogCallCount;
    context->lastHostLogMessage = safeMessage;
    NativeAppDebugLog::Add(context->runtimeId, context->appId, "info", std::string("host log: ") + safeMessage);
    Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " log: " + safeMessage);
    return GX_OK;
}

uint32_t hostGetApiVersion(NativeGxAppContext* ctx) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] get_api_version rejected: invalid app context or host table");
        return 0;
    }

    context->lastApiVersionReturned = kGuideXOSNativeApiVersion;
    return kGuideXOSNativeApiVersion;
}

uint64_t hostGetTicksMs(NativeGxAppContext* ctx) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] get_ticks_ms rejected: invalid app context or host table");
        return 0;
    }

    static const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - epoch).count();
    return elapsed < 0 ? 0u : static_cast<uint64_t>(elapsed);
}

gx_result hostFileExists(NativeGxAppContext* ctx, const char* path, uint32_t* outExists) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] file_exists rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ++context->fileExistsCallCount;
    context->lastFileReadBytes = 0;
    context->lastFileIoResult = GX_ERROR_INVALID_ARGUMENT;
    const bool outputPointerValid = nativeBufferRangeContains(*context, outExists, sizeof(uint32_t));
    if (outputPointerValid) *outExists = 0;

    if (!outputPointerValid) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " file_exists rejected: null output pointer");
        NativeAppDebugLog::Add(context->runtimeId, context->appId, "warn", "file_exists failed: null output pointer");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastFileIoResult;
    }

    if (!hasPermission(*context, "file.read")) {
        context->lastFileIoResult = GX_ERROR_PERMISSION_DENIED;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " file_exists denied: missing permission file.read");
        NativeAppDebugLog::Add(context->runtimeId, context->appId, "warn", "file_exists failed: missing permission file.read");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastFileIoResult;
    }

    std::string resolvedPath;
    context->lastFileIoResult = resolveFileReadPath(*context, path, resolvedPath);
    if (context->lastFileIoResult != GX_OK) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " file_exists rejected: unsafe path");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastFileIoResult;
    }

    std::ifstream input(resolvedPath.c_str(), std::ios::binary);
    *outExists = input ? 1u : 0u;
    context->lastFileIoResult = GX_OK;

    NativeAppProcessTable::UpdateFromRuntime(*context);
    NativeAppDebugLog::Add(context->runtimeId, context->appId, "info", "file_exists path=\"" + context->lastFilePath + "\" exists=" + std::to_string(*outExists));
    Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " file_exists path=\"" + context->lastFilePath + "\" exists=" + std::to_string(*outExists));
    return context->lastFileIoResult;
}

gx_result hostFileReadAll(NativeGxAppContext* ctx, const char* path, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] file_read_all rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ++context->fileReadCallCount;
    context->lastFileReadBytes = 0;
    context->lastFileIoResult = GX_ERROR_INVALID_ARGUMENT;
    const bool outputPointerValid = nativeBufferRangeContains(*context, outBytesRead, sizeof(uint32_t));
    if (outputPointerValid) *outBytesRead = 0;
    if (!buffer || bufferSize == 0 || !outputPointerValid || !nativeBufferRangeContains(*context, buffer, bufferSize)) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " file_read_all rejected: invalid buffer or output pointer");
        NativeAppDebugLog::Add(context->runtimeId, context->appId, "warn", "file_read_all failed: invalid buffer or output pointer");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastFileIoResult;
    }

    if (!hasPermission(*context, "file.read")) {
        context->lastFileIoResult = GX_ERROR_PERMISSION_DENIED;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " file_read_all denied: missing permission file.read");
        NativeAppDebugLog::Add(context->runtimeId, context->appId, "warn", "file_read_all failed: missing permission file.read");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastFileIoResult;
    }

    std::string resolvedPath;
    context->lastFileIoResult = resolveFileReadPath(*context, path, resolvedPath);
    if (context->lastFileIoResult != GX_OK) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " file_read_all rejected: unsafe path");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastFileIoResult;
    }

    try {
        std::ifstream input(resolvedPath.c_str(), std::ios::binary);
        if (!input) {
            context->lastFileIoResult = GX_ERROR_FAILED;
            Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " file_read_all failed: missing resource file " + context->lastFilePath);
            NativeAppDebugLog::Add(context->runtimeId, context->appId, "warn", "file_read_all failed: missing resource " + context->lastFilePath);
            NativeAppProcessTable::UpdateFromRuntime(*context);
            return context->lastFileIoResult;
        }

        input.seekg(0, std::ios::end);
        std::streamoff streamSize = input.tellg();
        if (streamSize < 0) {
            context->lastFileIoResult = GX_ERROR_FAILED;
            NativeAppProcessTable::UpdateFromRuntime(*context);
            return context->lastFileIoResult;
        }

        uint64_t fileSize = static_cast<uint64_t>(streamSize);
        if (fileSize > kMaxFileReadBytes || fileSize > bufferSize || fileSize > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            context->lastFileIoResult = GX_ERROR_UNSUPPORTED;
            NativeAppProcessTable::UpdateFromRuntime(*context);
            return context->lastFileIoResult;
        }

        input.seekg(0, std::ios::beg);
        if (!input) {
            context->lastFileIoResult = GX_ERROR_FAILED;
            NativeAppProcessTable::UpdateFromRuntime(*context);
            return context->lastFileIoResult;
        }

        if (fileSize > 0) input.read(static_cast<char*>(buffer), static_cast<std::streamsize>(fileSize));
        if (!input || static_cast<uint64_t>(input.gcount()) != fileSize) {
            context->lastFileIoResult = GX_ERROR_FAILED;
            NativeAppProcessTable::UpdateFromRuntime(*context);
            return context->lastFileIoResult;
        }

        *outBytesRead = static_cast<uint32_t>(fileSize);
        context->lastFileReadBytes = *outBytesRead;
        context->lastFileIoResult = GX_OK;
    } catch (...) {
        context->lastFileIoResult = GX_ERROR_FAILED;
    }

    NativeAppProcessTable::UpdateFromRuntime(*context);
    NativeAppDebugLog::Add(context->runtimeId, context->appId, context->lastFileIoResult == GX_OK ? "info" : "warn", "file_read_all path=\"" + context->lastFilePath + "\" bytes=" + std::to_string(context->lastFileReadBytes) + " result=" + std::to_string(context->lastFileIoResult));
    Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " file_read_all path=\"" + context->lastFilePath + "\" bytes=" + std::to_string(context->lastFileReadBytes) + " result=" + std::to_string(context->lastFileIoResult));
    return context->lastFileIoResult;
}

gx_result hostFileRead(NativeGxAppContext* ctx, const char* path, uint64_t offset, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) {
    const bool contextActive = g_activeRuntimeContext != nullptr;
    const bool contextPointerMatched = contextActive && ctx != nullptr && ctx == g_activeRuntimeContext->activeGxContext;
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        logInvalidFileReadContext(ctx, "file_read", offset, bufferSize, buffer, outBytesRead, GX_ERROR_INVALID_ARGUMENT);
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ++context->fileReadChunkCallCount;
    context->lastFileReadOffset = offset;
    context->lastFileReadBytes = 0;
    context->lastFileIoResult = GX_ERROR_INVALID_ARGUMENT;
    std::string resolvedPath;
    std::string requestedPath;
    std::string pathFailure;
    if (copyNativePath(*context, path, requestedPath, pathFailure)) context->lastFilePath = requestedPath;
    else context->lastFilePath = pathFailure.empty() ? "<unavailable>" : "<" + pathFailure + ">";

    auto finish = [&](gx_result result, const char* phase, const char* actualHostRead) -> gx_result {
        context->lastFileReadBytes = (outBytesRead && nativeBufferRangeContains(*context, outBytesRead, sizeof(uint32_t))) ? *outBytesRead : 0;
        context->lastFileIoResult = result;
        NativeAppProcessTable::UpdateFromRuntime(*context);
        logFileReadBoundary(*context, "file_read", requestedPath, resolvedPath, offset, bufferSize, buffer,
            context->lastFileReadBytes, outBytesRead, phase, actualHostRead, contextActive, contextPointerMatched, result);
        return result;
    };

    const bool outputPointerValid = nativeBufferRangeContains(*context, outBytesRead, sizeof(uint32_t));
    if (outputPointerValid) *outBytesRead = 0;
    if (!buffer || bufferSize == 0 || !outputPointerValid || bufferSize > kMaxFileReadBytes) {
        return finish(context->lastFileIoResult, "destination-validation", "not-started");
    }
    if (!nativeBufferRangeContains(*context, buffer, bufferSize)) {
        return finish(GX_ERROR_INVALID_ARGUMENT, "destination-validation", "not-started");
    }
    if (!hasPermission(*context, "file.read")) {
        return finish(GX_ERROR_PERMISSION_DENIED, "permission-validation", "not-started");
    }

    context->lastFileIoResult = resolveFileReadPath(*context, path, resolvedPath);
    if (context->lastFileIoResult != GX_OK) {
        return finish(context->lastFileIoResult, "package-path-validation", "not-started");
    }

    try {
        std::ifstream input(resolvedPath.c_str(), std::ios::binary);
        if (!input) {
            return finish(GX_ERROR_FAILED, "file-open", "open-failed");
        } else {
            input.seekg(0, std::ios::end);
            const std::streamoff streamSize = input.tellg();
            if (streamSize < 0) return finish(GX_ERROR_FAILED, "file-size", "open-complete");
            const uint64_t fileSize = static_cast<uint64_t>(streamSize);
            if (offset > fileSize) return finish(GX_ERROR_INVALID_ARGUMENT, "offset-validation", "open-complete");

            const uint64_t available = fileSize - offset;
            const uint32_t bytesToRead = static_cast<uint32_t>(std::min<uint64_t>(available, bufferSize));
            input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!input) return finish(GX_ERROR_FAILED, "file-seek", "open-complete");

            if (bytesToRead > 0) {
                if (!input.read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytesToRead)) ||
                    static_cast<uint32_t>(input.gcount()) != bytesToRead) {
                    return finish(GX_ERROR_FAILED, "file-read", "read-attempt-failed");
                }
            }

            *outBytesRead = bytesToRead;
            return finish(GX_OK, "return", "read-complete");
        }
    } catch (...) {
        return finish(GX_ERROR_FAILED, "file-read-exception", "read-attempt-failed");
    }
}

gx_result hostRequestWindowEx(NativeGxAppContext* ctx, const char* title, int width, int height, uint32_t flags, gx_handle* outWindow) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] request_window rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ++context->requestWindowCallCount;
    context->lastRequestWindowResult = GX_ERROR_INVALID_ARGUMENT;
    if (outWindow) *outWindow = 0;

    if (!title || !title[0] || !outWindow) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " request_window rejected: invalid title or output handle");
        return context->lastRequestWindowResult;
    }

    context->lastRequestedWindowTitle = title;
    if (width < kMinWindowWidth || height < kMinWindowHeight || width > kMaxWindowWidth || height > kMaxWindowHeight) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " request_window rejected: invalid size " + std::to_string(width) + "x" + std::to_string(height));
        return context->lastRequestWindowResult;
    }

    if (!hasPermission(*context, "window")) {
        context->lastRequestWindowResult = GX_ERROR_PERMISSION_DENIED;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " request_window denied: missing permission window");
        return context->lastRequestWindowResult;
    }

    uint64_t nativeAppPid = Allocator::currentPid();
    if (context->processId == 0) context->processId = nativeAppPid;
    ipc::Message request;
    request.srcPid = nativeAppPid;
    request.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
    std::string payload = std::string(title) + "|" + std::to_string(width) + "|" + std::to_string(height) + "|" + std::to_string(flags);
    request.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(request), false);

    ipc::Message ack;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kWindowCreateTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!ipc::Bus::pop("gui.output", ack, 25)) continue;
        if (ack.type != static_cast<uint32_t>(gui::MsgType::MT_Create)) continue;
        uint64_t windowId = 0;
        std::string ackPayload(ack.data.begin(), ack.data.end());
        if (!parseWindowId(ackPayload, windowId)) continue;

        *outWindow = windowId;
        context->createdWindowHandles.push_back(windowId);
        context->lastCreatedWindowId = windowId;
        context->lastRequestWindowResult = GX_OK;
        NativeAppProcessTable::UpdateFromRuntime(*context);
        NativeAppDebugLog::Add(context->runtimeId, context->appId, "info", "request_window title=\"" + context->lastRequestedWindowTitle + "\" windowId=" + std::to_string(windowId));
        Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " request_window title=\"" + context->lastRequestedWindowTitle + "\" size=" + std::to_string(width) + "x" + std::to_string(height) + " windowId=" + std::to_string(windowId));
        return GX_OK;
    }

    context->lastRequestWindowResult = GX_ERROR_UNSUPPORTED;
    Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " request_window failed: compositor unavailable or no MT_Create ack");
    NativeAppDebugLog::Add(context->runtimeId, context->appId, "warn", "request_window failed: compositor unavailable or no MT_Create ack");
    return context->lastRequestWindowResult;
}

gx_result hostRequestWindow(NativeGxAppContext* ctx, const char* title, int width, int height, gx_handle* outWindow) {
    return hostRequestWindowEx(ctx, title, width, height, 1u, outWindow);
}

gx_result hostDrawText(NativeGxAppContext* ctx, gx_handle window, int x, int y, const char* text) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] draw_text rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ++context->drawTextCallCount;
    context->lastDrawTextWindow = window;
    context->lastDrawTextResult = GX_ERROR_INVALID_ARGUMENT;

    if (window == 0) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_text rejected: invalid window handle");
        return context->lastDrawTextResult;
    }

    if (!ownsWindow(*context, window)) {
        context->lastDrawTextResult = GX_ERROR_PERMISSION_DENIED;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_text denied: window is not owned by this native runtime");
        return context->lastDrawTextResult;
    }

    if (!text) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_text rejected: null text");
        return context->lastDrawTextResult;
    }

    context->lastDrawText = text;
    if (x < kMinDrawCoordinate || y < kMinDrawCoordinate || x > kMaxDrawCoordinate || y > kMaxDrawCoordinate) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_text rejected: invalid position " + std::to_string(x) + "," + std::to_string(y));
        return context->lastDrawTextResult;
    }

    if (!hasPermission(*context, "draw") && !hasPermission(*context, "window")) {
        context->lastDrawTextResult = GX_ERROR_PERMISSION_DENIED;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_text denied: missing permission draw/window");
        return context->lastDrawTextResult;
    }

    ipc::Message request;
    request.srcPid = Allocator::currentPid();
    request.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
    std::string payload = std::to_string(window) + "|@" + std::to_string(x) + "," + std::to_string(y) + "|" + context->lastDrawText;
    request.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(request), false);

    context->lastDrawTextResult = GX_OK;
    NativeAppProcessTable::UpdateFromRuntime(*context);
    NativeAppDebugLog::Add(context->runtimeId, context->appId, "info", "draw_text windowId=" + std::to_string(window) + " textLength=" + std::to_string(context->lastDrawText.size()));
    Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " draw_text windowId=" + std::to_string(window) + " pos=" + std::to_string(x) + "," + std::to_string(y) + " text=\"" + context->lastDrawText + "\"");
    return GX_OK;
}

gx_result hostDrawRect(NativeGxAppContext* ctx, gx_handle window, int x, int y, int width, int height, uint32_t color) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] draw_rect rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ++context->drawRectCallCount;
    context->lastDrawRectWindow = window;
    context->lastDrawRectX = x;
    context->lastDrawRectY = y;
    context->lastDrawRectWidth = width;
    context->lastDrawRectHeight = height;
    context->lastDrawRectColor = color;
    context->lastDrawRectResult = GX_ERROR_INVALID_ARGUMENT;

    if (window == 0) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_rect rejected: invalid window handle");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastDrawRectResult;
    }

    if (!ownsWindow(*context, window)) {
        context->lastDrawRectResult = GX_ERROR_PERMISSION_DENIED;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_rect denied: window is not owned by this native runtime");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastDrawRectResult;
    }

    if (x < kMinDrawCoordinate || y < kMinDrawCoordinate || x > kMaxDrawCoordinate || y > kMaxDrawCoordinate ||
        width < kMinDrawSize || height < kMinDrawSize || width > kMaxDrawSize || height > kMaxDrawSize ||
        x > kMaxDrawCoordinate - width || y > kMaxDrawCoordinate - height) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_rect rejected: invalid rect " + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(width) + "x" + std::to_string(height));
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastDrawRectResult;
    }

    if (!hasPermission(*context, "draw") && !hasPermission(*context, "window")) {
        context->lastDrawRectResult = GX_ERROR_PERMISSION_DENIED;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_rect denied: missing permission draw/window");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastDrawRectResult;
    }

    uint32_t rgb = color & 0x00FFFFFFu;
    uint32_t red = (rgb >> 16) & 0xFFu;
    uint32_t green = (rgb >> 8) & 0xFFu;
    uint32_t blue = rgb & 0xFFu;

    try {
        ipc::Message request;
        request.srcPid = Allocator::currentPid();
        request.type = static_cast<uint32_t>(gui::MsgType::MT_DrawRect);
        std::string payload = std::to_string(window) + "|" + std::to_string(x) + "|" + std::to_string(y) + "|" + std::to_string(width) + "|" + std::to_string(height) + "|" + std::to_string(red) + "|" + std::to_string(green) + "|" + std::to_string(blue);
        request.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(request), false);
    } catch (...) {
        context->lastDrawRectResult = GX_ERROR_INTERNAL;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_rect failed: compositor publish failed");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastDrawRectResult;
    }

    context->lastDrawRectResult = GX_OK;
    NativeAppProcessTable::UpdateFromRuntime(*context);
    NativeAppDebugLog::Add(context->runtimeId, context->appId, "info", "draw_rect windowId=" + std::to_string(window) + " size=" + std::to_string(width) + "x" + std::to_string(height));
    Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " draw_rect windowId=" + std::to_string(window) + " rect=" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(width) + "x" + std::to_string(height) + " color=0x" + std::to_string(rgb));
    return GX_OK;
}

gx_result hostPresentFrame(NativeGxAppContext* ctx, gx_handle window, int x, int y, int width, int height,
                           uint32_t strideBytes, uint32_t pixelFormat, const void* pixels, uint32_t pixelBytes) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) return GX_ERROR_INVALID_ARGUMENT;

    ++context->presentFrameCallCount;
    context->lastPresentFrameWindow = window;
    context->lastPresentFrameX = x;
    context->lastPresentFrameY = y;
    context->lastPresentFrameWidth = width;
    context->lastPresentFrameHeight = height;
    context->lastPresentFrameStrideBytes = strideBytes;
    context->lastPresentFramePixelFormat = pixelFormat;
    context->lastPresentFrameBytes = pixelBytes;
    context->lastPresentFrameResult = GX_ERROR_INVALID_ARGUMENT;

    const auto logFrameBoundary = [&](const char* validation, const char* presentation) {
        if (!frameDiagnosticsEnabled()) return;
        std::ostringstream oss;
        oss << "Native ELF frame boundary runtimeId=" << context->runtimeId
            << " windowId=" << window
            << " incomingFrameSeq=" << context->presentFrameCallCount
            << " width=" << width
            << " height=" << height
            << " stride=" << strideBytes
            << " format=" << pixelFormat
            << " bytes=" << pixelBytes
            << " validation=" << validation
            << " presentation=" << presentation
            << " result=" << context->lastPresentFrameResult;
        Logger::write(LogLevel::Info, oss.str());
    };

    if (window == 0 || !pixels || x < 0 || y < 0 || width < 1 || height < 1 || width > kMaxWindowWidth || height > kMaxWindowHeight ||
        pixelFormat != gui::kPixelFormatXrgb8888 || strideBytes < static_cast<uint32_t>(width * 4)) {
        logFrameBoundary("FAIL", "NOT_ATTEMPTED");
        return context->lastPresentFrameResult;
    }
    if (!ownsWindow(*context, window)) {
        context->lastPresentFrameResult = GX_ERROR_PERMISSION_DENIED;
        logFrameBoundary("FAIL", "NOT_ATTEMPTED");
        return context->lastPresentFrameResult;
    }
    if (!hasPermission(*context, "draw") && !hasPermission(*context, "window")) {
        context->lastPresentFrameResult = GX_ERROR_PERMISSION_DENIED;
        logFrameBoundary("FAIL", "NOT_ATTEMPTED");
        return context->lastPresentFrameResult;
    }

    const uint64_t requiredBytes = static_cast<uint64_t>(strideBytes) * static_cast<uint64_t>(height);
    if (requiredBytes > kMaxPresentFrameBytes || requiredBytes != pixelBytes) {
        context->lastPresentFrameResult = GX_ERROR_UNSUPPORTED;
        logFrameBoundary("FAIL", "NOT_ATTEMPTED");
        return context->lastPresentFrameResult;
    }
    if (!nativeBufferRangeContains(*context, pixels, requiredBytes)) {
        context->lastPresentFrameResult = GX_ERROR_INVALID_ARGUMENT;
        logFrameBoundary("FAIL", "NOT_ATTEMPTED");
        return context->lastPresentFrameResult;
    }

    try {
        ipc::Message request;
        request.srcPid = Allocator::currentPid();
        request.type = static_cast<uint32_t>(gui::MsgType::MT_FramePresent);
        request.data = gui::packFramePresent(window, x, y, width, height, strideBytes, pixelFormat, pixels, pixelBytes,
            frameDiagnosticsEnabled() ? context->presentFrameCallCount : 0);
        ipc::Bus::publish("gui.input", std::move(request), false);
        context->lastPresentFrameResult = GX_OK;
    } catch (...) {
        context->lastPresentFrameResult = GX_ERROR_INTERNAL;
    }
    logFrameBoundary("PASS", context->lastPresentFrameResult == GX_OK ? "PASS" : "FAIL");
    NativeAppProcessTable::UpdateFromRuntime(*context);
    NativeAppDebugLog::Add(context->runtimeId, context->appId, context->lastPresentFrameResult == GX_OK ? "info" : "warn",
        "present_frame windowId=" + std::to_string(window) + " size=" + std::to_string(width) + "x" + std::to_string(height) +
        " bytes=" + std::to_string(pixelBytes) + " result=" + std::to_string(context->lastPresentFrameResult));
    return context->lastPresentFrameResult;
}

gx_result hostPollEvent(NativeGxAppContext* ctx, gx_event* outEvent, int timeoutMs) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] poll_event rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ++context->pollEventCallCount;
    context->lastEventType = GX_EVENT_NONE;
    context->lastEventWindow = 0;
    context->lastPollEventResult = GX_ERROR_INVALID_ARGUMENT;

    if (!outEvent || timeoutMs < kMinPollEventTimeoutMs || timeoutMs > kMaxPollEventTimeoutMs) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " poll_event rejected: invalid output event or timeout");
        NativeAppProcessTable::UpdateFromRuntime(*context);
        return context->lastPollEventResult;
    }

    initializeEvent(outEvent);
    requestPaintForOwnedWindows(*context);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    ipc::Message message;
    do {
        uint64_t remainingMs = 0;
        if (timeoutMs > 0) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            remainingMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            if (remainingMs > 25) remainingMs = 25;
        }

        if (!ipc::Bus::pop("gui.output", message, remainingMs)) break;

        gx_event_type eventType = eventTypeForMessage(message.type);
        if (eventType == GX_EVENT_NONE) continue;

        std::string payload(message.data.begin(), message.data.end());
        uint64_t window = 0;
        int paintWidth = 0;
        int paintHeight = 0;
        int keyCode = 0;
        int keyAction = 0;
        int keyModifiers = 0;
        int mouseX = 0;
        int mouseY = 0;
        int mousePackedButtonAction = 0;
        int mouseModifiers = 0;
        bool parsed = false;
        if (eventType == GX_EVENT_WINDOW_PAINT) {
            parsed = parseFramePayload(payload, window, paintWidth, paintHeight);
        } else if (eventType == GX_EVENT_KEY) {
            parsed = parseKeyPayload(payload, keyCode, keyAction, keyModifiers, window);
            if (parsed && window == 0) window = context->focusedOwnedWindow;
        } else if (eventType == GX_EVENT_MOUSE) {
            parsed = parseMousePayload(payload, mouseX, mouseY, mousePackedButtonAction, mouseModifiers, window);
            if (parsed && window == 0) window = context->focusedOwnedWindow;
        } else {
            parsed = parseWindowId(payload, window);
        }
        if (!parsed) {
            context->lastPollEventResult = GX_ERROR_UNSUPPORTED;
            Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " poll_event unsupported: GUI event did not identify a window");
            NativeAppProcessTable::UpdateFromRuntime(*context);
            return context->lastPollEventResult;
        }

        if (!ownsWindow(*context, window)) {
            Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " poll_event skipped event for an unowned window");
            continue;
        }

        outEvent->type = eventType;
        outEvent->window = window;
        if (eventType == GX_EVENT_WINDOW_PAINT) {
            outEvent->param1 = paintWidth;
            outEvent->param2 = paintHeight;
            ++context->paintEventCount;
            context->lastPaintWindow = window;
            context->lastPaintWidth = paintWidth;
            context->lastPaintHeight = paintHeight;
        } else if (eventType == GX_EVENT_WINDOW_FOCUS) {
            context->focusedOwnedWindow = window;
        } else if (eventType == GX_EVENT_WINDOW_BLUR) {
            if (context->focusedOwnedWindow == window) context->focusedOwnedWindow = 0;
        } else if (eventType == GX_EVENT_KEY) {
            outEvent->param1 = keyCode;
            outEvent->param2 = keyAction;
            outEvent->param3 = keyModifiers;
            ++context->keyEventCount;
            context->lastKeyWindow = window;
            context->lastKeyCode = keyCode;
            context->lastKeyAction = keyAction;
            context->lastKeyModifiers = keyModifiers;
        } else if (eventType == GX_EVENT_MOUSE) {
            outEvent->param1 = mouseX;
            outEvent->param2 = mouseY;
            outEvent->param3 = mousePackedButtonAction;
            outEvent->param4 = mouseModifiers;
            ++context->mouseEventCount;
            context->lastMouseWindow = window;
            context->lastMouseX = mouseX;
            context->lastMouseY = mouseY;
            context->lastMousePackedButtonAction = mousePackedButtonAction;
            context->lastMouseModifiers = mouseModifiers;
        }
        context->lastEventType = eventType;
        context->lastEventWindow = window;
        context->lastPollEventResult = GX_OK;
        if (eventType == GX_EVENT_WINDOW_CLOSE) removeOwnedWindow(*context, window);
        NativeAppProcessTable::UpdateFromRuntime(*context);
        if (eventType == GX_EVENT_WINDOW_CLOSE) NativeAppDebugLog::Add(context->runtimeId, context->appId, "info", "poll_event close windowId=" + std::to_string(window));
        Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " poll_event returned type=" + std::to_string(static_cast<uint32_t>(eventType)) + " windowId=" + std::to_string(window));
        return GX_OK;
    } while (timeoutMs > 0);

    context->lastPollEventResult = GX_ERROR_TIMEOUT;
    NativeAppProcessTable::UpdateFromRuntime(*context);
    return context->lastPollEventResult;
}

gx_result hostWaitForClose(NativeGxAppContext* ctx, gx_handle window, int timeoutMs) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] wait_for_close rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ++context->waitForCloseCallCount;
    context->lastWaitWindow = window;
    context->lastWaitTimeoutMs = timeoutMs;
    context->lastWaitResult = GX_ERROR_INVALID_ARGUMENT;

    if (window == 0 || timeoutMs < kMinWaitForCloseTimeoutMs || timeoutMs > kMaxWaitForCloseTimeoutMs) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " wait_for_close rejected: invalid window or timeout");
        return context->lastWaitResult;
    }

    if (!ownsWindow(*context, window)) {
        context->lastWaitResult = GX_ERROR_PERMISSION_DENIED;
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " wait_for_close denied: window is not owned by this native runtime");
        return context->lastWaitResult;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    ipc::Message message;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!ipc::Bus::pop("gui.output", message, 25)) continue;
        if (message.type != static_cast<uint32_t>(gui::MsgType::MT_Close)) continue;

        std::string payload(message.data.begin(), message.data.end());
        uint64_t closedWindow = 0;
        if (!parseWindowId(payload, closedWindow)) {
            context->lastWaitResult = GX_ERROR_UNSUPPORTED;
            Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " wait_for_close unsupported: MT_Close did not identify a window");
            return context->lastWaitResult;
        }

        if (closedWindow == window) {
            context->lastWaitResult = GX_OK;
            removeOwnedWindow(*context, window);
            NativeAppProcessTable::UpdateFromRuntime(*context);
            Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " wait_for_close completed windowId=" + std::to_string(window));
            return GX_OK;
        }
    }

    context->lastWaitResult = GX_ERROR_TIMEOUT;
    Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " wait_for_close timed out windowId=" + std::to_string(window) + " timeoutMs=" + std::to_string(timeoutMs));
    return context->lastWaitResult;
}

void publishWindowClose(uint64_t processId, gx_handle window) {
    ipc::Message request;
    request.srcPid = processId;
    request.type = static_cast<uint32_t>(gui::MsgType::MT_Close);
    std::string payload = std::to_string(window);
    request.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(request), false);
}

gx_result hostExit(NativeGxAppContext* ctx, gx_result exitCode) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] exit rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    g_hostLifecycleState = NativeAppLifecycleState::Exited;
    context->lifecycleState = NativeAppLifecycleState::Exited;
    context->exitCode = exitCode;
    Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " exit requested with code " + std::to_string(exitCode));
    return GX_OK;
}

void addDiagnostic(NativeAppRuntimeContext& context, const std::string& diagnostic) {
    context.diagnostics.push_back(diagnostic);
}

std::string joinStrings(const std::vector<std::string>& values) {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << "; ";
        oss << values[i];
    }
    return oss.str();
}

} // namespace

NativeAppRuntimeContext NativeAppRuntime::Prepare(
    const RegisteredApp& app,
    const LaunchDecision& launchDecision,
    const NativeElfLaunchResult& launchResult,
    const NativeElfImage& image) {
    NativeAppRuntimeContext context;
    context.appId = app.manifest.id;
    context.runtimeId = g_nextRuntimeId.fetch_add(1);
    context.displayName = app.manifest.displayName;
    context.architecture = launchResult.architecture.empty() ? launchDecision.architecture : launchResult.architecture;
    context.processId = 0;
    context.appDirectory = app.appDirectory.string();
    context.nativeImageBaseAddress = image.preferredBaseAddress;
    if (image.imageSize <= std::numeric_limits<uint64_t>::max() - context.nativeImageBaseAddress) {
        context.nativeImageEndAddress = context.nativeImageBaseAddress + image.imageSize;
    }
    context.permissions = app.manifest.permissions;
    context.arguments.push_back(context.displayName.empty() ? context.appId : context.displayName);
    context.environment["GX_APP_ID"] = context.appId;
    context.environment["GX_APP_DISPLAY_NAME"] = context.displayName;
    context.environment["GX_APP_DIRECTORY"] = context.appDirectory;
    context.environment["GX_APP_ARCHITECTURE"] = context.architecture;
    context.environment["GX_APP_ABI"] = launchResult.abi;
    context.environment["GX_NATIVE_RUNTIME_ID"] = std::to_string(context.runtimeId);
    context.lifecycleState = NativeAppLifecycleState::Created;

    context.hostCalls.size = static_cast<uint32_t>(sizeof(NativeHostCallTable));
    context.hostCalls.version = kGuideXOSNativeApiVersion;
    context.hostCalls.log = hostLog;
    context.hostCalls.get_api_version = hostGetApiVersion;
    context.hostCalls.request_window = hostRequestWindow;
    context.hostCalls.draw_text = hostDrawText;
    context.hostCalls.draw_rect = hostDrawRect;
    context.hostCalls.wait_for_close = hostWaitForClose;
    context.hostCalls.poll_event = hostPollEvent;
    context.hostCalls.exit = hostExit;
    context.hostCalls.file_read_all = hostFileReadAll;
    context.hostCalls.file_exists = hostFileExists;
    context.hostCalls.request_window_ex = hostRequestWindowEx;
    context.hostCalls.file_read = hostFileRead;
    context.hostCalls.present_frame = hostPresentFrame;
    context.hostCalls.get_ticks_ms = hostGetTicksMs;

    if (launchDecision.strategy != AppLaunchStrategy::NativeElf) {
        addDiagnostic(context, "Launch decision strategy is not NativeElf");
    }
    if (!launchResult.success) {
        addDiagnostic(context, "Native ELF launch result was not successful");
    }
    if (!image.success) {
        addDiagnostic(context, "Native ELF image was not loaded successfully");
    }
    if (launchResult.abi != kGuideXOSNativeAbiName) {
        addDiagnostic(context, std::string("ABI mismatch: expected ") + kGuideXOSNativeAbiName + ", got " + launchResult.abi);
    }
    if (context.appId.empty()) {
        addDiagnostic(context, "Native app id is empty");
    }

    if (context.diagnostics.empty()) {
        context.success = true;
        context.lifecycleState = NativeAppLifecycleState::Prepared;
        g_hostLifecycleState = NativeAppLifecycleState::Prepared;
        addDiagnostic(context, "Native app runtime prepared");
    } else {
        context.lifecycleState = NativeAppLifecycleState::Failed;
        g_hostLifecycleState = NativeAppLifecycleState::Failed;
    }

    LogContext(context, launchResult.abi);
    return context;
}

void NativeAppRuntime::BeginHostCallDispatch(NativeAppRuntimeContext& context) {
    if (context.processId == 0) context.processId = Allocator::currentPid();
    volatile uint8_t stackMarker = 0;
    const uint64_t stackAddress = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&stackMarker));
    context.nativeStackBaseAddress = stackAddress > kMaxNativeAppStackBytes ? stackAddress - kMaxNativeAppStackBytes : 0;
    context.nativeStackEndAddress = stackAddress <= std::numeric_limits<uint64_t>::max() - kMaxNativeAppStackBytes
        ? stackAddress + kMaxNativeAppStackBytes : std::numeric_limits<uint64_t>::max();
    context.lifecycleState = NativeAppLifecycleState::Running;
    context.startTime = std::chrono::steady_clock::now();
    g_hostLifecycleState = NativeAppLifecycleState::Running;
    g_activeRuntimeContext = &context;
    Logger::write(LogLevel::Info, "[NativeAppRuntime] App: " + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId) + " entering host call dispatch");
}

void NativeAppRuntime::EndHostCallDispatch(NativeAppRuntimeContext& context) {
    if (g_activeRuntimeContext == &context) g_activeRuntimeContext = nullptr;
}

void NativeAppRuntime::RequestCloseOwnedWindows(NativeAppRuntimeContext& context) {
    uint64_t closePid = context.processId != 0 ? context.processId : Allocator::currentPid();
    for (gx_handle window : context.createdWindowHandles) {
        if (window != 0) publishWindowClose(closePid, window);
    }
}

void NativeAppRuntime::Cleanup(NativeAppRuntimeContext& context, NativeAppLifecycleState finalState, int32_t exitCode, const std::string& failureReason) {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    try {
        if (context.cleanupAttempted) {
            Logger::write(LogLevel::Info, "[NativeAppRuntime] Cleanup already attempted for app=" + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId) + " remainingWindows=" + std::to_string(context.createdWindowHandles.size()));
            context.exitCode = exitCode;
            if (!failureReason.empty()) context.failureReason = failureReason;
            if (context.lifecycleState != NativeAppLifecycleState::Failed && finalState == NativeAppLifecycleState::Failed) context.lifecycleState = NativeAppLifecycleState::Failed;
            return;
        }

        context.cleanupAttempted = true;
        context.lifecycleState = NativeAppLifecycleState::Closing;
        g_hostLifecycleState = NativeAppLifecycleState::Closing;
        context.exitCode = exitCode;
        context.failureReason = failureReason;

        Logger::write(LogLevel::Info, "[NativeAppRuntime] Cleanup begin app=" + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId) + " processId=" + std::to_string(context.processId) + " ownedWindows=" + std::to_string(context.createdWindowHandles.size()));
        NativeAppDebugLog::Add(context.runtimeId, context.appId, "info", "cleanup started ownedWindows=" + std::to_string(context.createdWindowHandles.size()));

        std::vector<gx_handle> windowsToClose = context.createdWindowHandles;
        context.createdWindowHandles.clear();
        uint64_t cleanupPid = context.processId != 0 ? context.processId : Allocator::currentPid();
        for (gx_handle window : windowsToClose) {
            if (window == 0) {
                Logger::write(LogLevel::Warn, "[NativeAppRuntime] Cleanup skipped invalid window handle app=" + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId));
                continue;
            }

            try {
                publishWindowClose(cleanupPid, window);
                ++context.cleanedWindowCount;
                Logger::write(LogLevel::Info, "[NativeAppRuntime] Cleanup published MT_Close app=" + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId) + " windowId=" + std::to_string(window));
            } catch (const std::exception& ex) {
                Logger::write(LogLevel::Warn, "[NativeAppRuntime] Cleanup failed to publish MT_Close app=" + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId) + " windowId=" + std::to_string(window) + " reason=" + ex.what());
            } catch (...) {
                Logger::write(LogLevel::Warn, "[NativeAppRuntime] Cleanup failed to publish MT_Close app=" + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId) + " windowId=" + std::to_string(window) + " reason=unknown");
            }
        }

        context.endTime = std::chrono::steady_clock::now();
        context.lifecycleState = finalState == NativeAppLifecycleState::Failed ? NativeAppLifecycleState::Failed : NativeAppLifecycleState::Exited;
        g_hostLifecycleState = context.lifecycleState;
        NativeAppDebugLog::Add(context.runtimeId, context.appId, context.lifecycleState == NativeAppLifecycleState::Failed ? "error" : "info", "cleanup completed state=" + std::string(ToString(context.lifecycleState)) + " cleanedWindows=" + std::to_string(context.cleanedWindowCount) + " remainingWindows=" + std::to_string(context.createdWindowHandles.size()));
        Logger::write(context.lifecycleState == NativeAppLifecycleState::Failed ? LogLevel::Warn : LogLevel::Info, "[NativeAppRuntime] Cleanup complete app=" + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId) + " state=" + ToString(context.lifecycleState) + " exitCode=" + std::to_string(context.exitCode) + " cleanedWindows=" + std::to_string(context.cleanedWindowCount) + " remainingWindows=" + std::to_string(context.createdWindowHandles.size()) + (context.failureReason.empty() ? std::string() : " failureReason=" + context.failureReason));
    } catch (...) {
        context.endTime = std::chrono::steady_clock::now();
        context.lifecycleState = NativeAppLifecycleState::Failed;
        g_hostLifecycleState = NativeAppLifecycleState::Failed;
        context.failureReason = context.failureReason.empty() ? "cleanup raised an unexpected exception" : context.failureReason;
        Logger::write(LogLevel::Error, "[NativeAppRuntime] Cleanup suppressed unexpected exception app=" + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId));
    }
#else
    (void)context;
    (void)finalState;
    (void)exitCode;
    (void)failureReason;
#endif
}

const char* NativeAppRuntime::ToString(NativeAppLifecycleState state) {
    switch (state) {
    case NativeAppLifecycleState::Created: return "Created";
    case NativeAppLifecycleState::Prepared: return "Prepared";
    case NativeAppLifecycleState::Running: return "Running";
    case NativeAppLifecycleState::Suspended: return "Suspended";
    case NativeAppLifecycleState::Closing: return "Closing";
    case NativeAppLifecycleState::Exited: return "Exited";
    case NativeAppLifecycleState::Failed: return "Failed";
    default: return "Unknown";
    }
}

void NativeAppRuntime::LogContext(const NativeAppRuntimeContext& context, const std::string& abi) {
    std::ostringstream oss;
    oss << "[NativeAppRuntime] "
        << "App: " << context.appId
        << " RuntimeId: " << context.runtimeId
        << " ProcessId: " << context.processId
        << " Architecture: " << context.architecture
        << " ABI: " << abi
        << " Permissions: " << context.permissions.size()
        << " State: " << ToString(context.lifecycleState)
        << " Result: " << (context.success ? "success" : "failure")
        << " Diagnostics: " << joinStrings(context.diagnostics);
    Logger::write(context.success ? LogLevel::Info : LogLevel::Warn, oss.str());
}

} // namespace apps
} // namespace gxos
