#include "native_app_runtime.h"

#include "allocator.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "native_app_process_table.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace gxos {
namespace apps {
namespace {

NativeAppLifecycleState g_hostLifecycleState = NativeAppLifecycleState::Created;
NativeAppRuntimeContext* g_activeRuntimeContext = nullptr;

constexpr int kMinWindowWidth = 64;
constexpr int kMinWindowHeight = 64;
constexpr int kMaxWindowWidth = 4096;
constexpr int kMaxWindowHeight = 4096;
constexpr int kMinDrawCoordinate = 0;
constexpr int kMaxDrawCoordinate = 16384;
constexpr uint64_t kWindowCreateTimeoutMs = 500;
constexpr int kMinWaitForCloseTimeoutMs = 0;
constexpr int kMaxWaitForCloseTimeoutMs = 300000;
constexpr int kMinPollEventTimeoutMs = 0;
constexpr int kMaxPollEventTimeoutMs = 30000;

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
    if (!ctx->host) return nullptr;
    if (ctx->size < sizeof(NativeGxAppContext)) return nullptr;
    if (g_activeRuntimeContext && ctx->host == &g_activeRuntimeContext->hostCalls) return g_activeRuntimeContext;
    return nullptr;
}

bool hasPermission(const NativeAppRuntimeContext& context, const std::string& permission) {
    for (const std::string& granted : context.permissions) {
        if (granted == permission) return true;
    }
    return false;
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

gx_result hostRequestWindow(NativeGxAppContext* ctx, const char* title, int width, int height, gx_handle* outWindow) {
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
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " request_window denied: missing window permission");
        return context->lastRequestWindowResult;
    }

    uint64_t nativeAppPid = Allocator::currentPid();
    if (context->processId == 0) context->processId = nativeAppPid;
    ipc::Message request;
    request.srcPid = nativeAppPid;
    request.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
    std::string payload = std::string(title) + "|" + std::to_string(width) + "|" + std::to_string(height);
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
        Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " request_window title=\"" + context->lastRequestedWindowTitle + "\" size=" + std::to_string(width) + "x" + std::to_string(height) + " windowId=" + std::to_string(windowId));
        return GX_OK;
    }

    context->lastRequestWindowResult = GX_ERROR_UNSUPPORTED;
    Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " request_window failed: compositor unavailable or no MT_Create ack");
    return context->lastRequestWindowResult;
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
        Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " draw_text denied: missing draw/window permission");
        return context->lastDrawTextResult;
    }

    ipc::Message request;
    request.srcPid = Allocator::currentPid();
    request.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
    std::string payload = std::to_string(window) + "|@" + std::to_string(x) + "," + std::to_string(y) + "|" + context->lastDrawText;
    request.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(request), false);

    context->lastDrawTextResult = GX_OK;
    Logger::write(LogLevel::Info, "[NativeAppHost] App: " + appLabel(context) + " draw_text windowId=" + std::to_string(window) + " pos=" + std::to_string(x) + "," + std::to_string(y) + " text=\"" + context->lastDrawText + "\"");
    return GX_OK;
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
        bool parsed = eventType == GX_EVENT_WINDOW_PAINT
            ? parseFramePayload(payload, window, paintWidth, paintHeight)
            : parseWindowId(payload, window);
        if (!parsed) {
            context->lastPollEventResult = GX_ERROR_UNSUPPORTED;
            Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " poll_event unsupported: GUI event did not identify a window");
            NativeAppProcessTable::UpdateFromRuntime(*context);
            return context->lastPollEventResult;
        }

        if (!ownsWindow(*context, window)) {
            context->lastPollEventResult = GX_ERROR_UNSUPPORTED;
            Logger::write(LogLevel::Warn, "[NativeAppHost] App: " + appLabel(context) + " poll_event unsupported: encountered event for an unowned window");
            NativeAppProcessTable::UpdateFromRuntime(*context);
            return context->lastPollEventResult;
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
        }
        context->lastEventType = eventType;
        context->lastEventWindow = window;
        context->lastPollEventResult = GX_OK;
        if (eventType == GX_EVENT_WINDOW_CLOSE) removeOwnedWindow(*context, window);
        NativeAppProcessTable::UpdateFromRuntime(*context);
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
    context.hostCalls.wait_for_close = hostWaitForClose;
    context.hostCalls.poll_event = hostPollEvent;
    context.hostCalls.exit = hostExit;

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
        addDiagnostic(context, std::string("Unsupported Native app ABI: ") + launchResult.abi);
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
    context.lifecycleState = NativeAppLifecycleState::Running;
    context.startTime = std::chrono::steady_clock::now();
    g_hostLifecycleState = NativeAppLifecycleState::Running;
    g_activeRuntimeContext = &context;
    Logger::write(LogLevel::Info, "[NativeAppRuntime] App: " + appLabel(&context) + " runtimeId=" + std::to_string(context.runtimeId) + " entering host call dispatch");
}

void NativeAppRuntime::EndHostCallDispatch(NativeAppRuntimeContext& context) {
    if (g_activeRuntimeContext == &context) g_activeRuntimeContext = nullptr;
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
