#include "native_app_runtime.h"

#include "allocator.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"

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

std::string appLabel(const NativeAppRuntimeContext* context) {
    if (!context) return "<unknown>";
    if (!context->displayName.empty()) return context->appId + " (" + context->displayName + ")";
    return context->appId.empty() ? "<unknown>" : context->appId;
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

gx_result hostExit(NativeGxAppContext* ctx, gx_result exitCode) {
    NativeAppRuntimeContext* context = runtimeContextFor(ctx);
    if (!context) {
        Logger::write(LogLevel::Warn, "[NativeAppHost] exit rejected: invalid app context or host table");
        return GX_ERROR_INVALID_ARGUMENT;
    }

    g_hostLifecycleState = NativeAppLifecycleState::Exited;
    context->lifecycleState = NativeAppLifecycleState::Exited;
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
    context.lifecycleState = NativeAppLifecycleState::Created;

    context.hostCalls.size = static_cast<uint32_t>(sizeof(NativeHostCallTable));
    context.hostCalls.version = kGuideXOSNativeApiVersion;
    context.hostCalls.log = hostLog;
    context.hostCalls.get_api_version = hostGetApiVersion;
    context.hostCalls.request_window = hostRequestWindow;
    context.hostCalls.draw_text = hostDrawText;
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
    g_activeRuntimeContext = &context;
}

void NativeAppRuntime::EndHostCallDispatch(NativeAppRuntimeContext& context) {
    if (g_activeRuntimeContext == &context) g_activeRuntimeContext = nullptr;
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
