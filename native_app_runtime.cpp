#include "native_app_runtime.h"

#include "logger.h"

#include <sstream>

namespace gxos {
namespace apps {
namespace {

NativeAppLifecycleState g_hostLifecycleState = NativeAppLifecycleState::Created;

gx_result hostLog(const char* message) {
    if (!message) return GX_ERROR_INVALID_ARGUMENT;
    Logger::write(LogLevel::Info, std::string("[NativeAppHost] ") + message);
    return GX_OK;
}

uint32_t hostGetApiVersion() {
    return kGuideXOSNativeApiVersion;
}

gx_result hostRequestWindow() {
    Logger::write(LogLevel::Warn, "[NativeAppHost] request_window is not implemented");
    return GX_ERROR_UNSUPPORTED;
}

gx_result hostExit(gx_result exitCode) {
    g_hostLifecycleState = NativeAppLifecycleState::Exited;
    Logger::write(LogLevel::Info, "[NativeAppHost] exit requested with code " + std::to_string(exitCode));
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
        addDiagnostic(context, "Native app runtime prepared; execution not implemented yet");
    } else {
        context.lifecycleState = NativeAppLifecycleState::Failed;
        g_hostLifecycleState = NativeAppLifecycleState::Failed;
    }

    LogContext(context, launchResult.abi);
    return context;
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
