#include "native_elf_executor.h"

#include "app_launch_resolver.h"
#include "logger.h"

#include <sstream>

namespace gxos {
namespace apps {
namespace {

bool experimentalExecutionEnabled() {
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    return true;
#else
    return false;
#endif
}

std::string hostArchitecture() {
    return AppLaunchResolver::CurrentArchitecture();
}

void addDiagnostic(NativeElfExecutionResult& result, const std::string& diagnostic) {
    result.diagnostics.push_back(diagnostic);
    if (result.message.empty()) result.message = diagnostic;
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
    std::ostringstream oss;
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) oss << "; ";
        oss << diagnostics[i];
    }
    return oss.str();
}

bool isSupportedStaticImage(const NativeElfImage& image) {
    return image.success && !image.hasInterpreter;
}

} // namespace

bool NativeElfExecutor::CanExecute(
    const NativeElfLaunchResult& launchResult,
    const NativeElfImage& image,
    const NativeAppRuntimeContext& runtimeContext,
    std::string* reason) {
    std::string localReason;
    bool canExecute = false;

    if (!experimentalExecutionEnabled()) {
        localReason = "Native ELF execution disabled by build flag";
    } else if (hostArchitecture() != launchResult.architecture) {
        localReason = "Native ELF architecture mismatch: host=" + hostArchitecture() + " app=" + launchResult.architecture;
    } else if (image.hasInterpreter) {
        localReason = "Native ELF requires PT_INTERP/dynamic linking, which is not supported";
    } else if (!isSupportedStaticImage(image)) {
        localReason = "Native ELF image is not a supported static executable image";
    } else if (launchResult.abi != kGuideXOSNativeAbiName) {
        localReason = std::string("Unsupported Native app ABI: ") + launchResult.abi;
    } else if (runtimeContext.lifecycleState != NativeAppLifecycleState::Prepared) {
        localReason = std::string("Native app runtime state is not Prepared: ") + NativeAppRuntime::ToString(runtimeContext.lifecycleState);
    } else {
        canExecute = true;
        localReason = "Native ELF executor is available, but actual execution is not implemented yet";
    }

    if (reason) *reason = localReason;
    LogDecision(launchResult.appId, launchResult.architecture, canExecute, localReason, canExecute ? "available" : "unavailable");
    return canExecute;
}

NativeElfExecutionResult NativeElfExecutor::Execute(
    const NativeElfLaunchResult& launchResult,
    const NativeElfImage& image,
    const NativeAppRuntimeContext& runtimeContext) {
    NativeElfExecutionResult result;
    result.appId = launchResult.appId;
    result.architecture = launchResult.architecture;
    result.exitCode = 0;

    if (!experimentalExecutionEnabled()) {
        addDiagnostic(result, "Native ELF execution disabled by build flag");
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (hostArchitecture() != launchResult.architecture) {
        addDiagnostic(result, "Native ELF architecture mismatch: host=" + hostArchitecture() + " app=" + launchResult.architecture);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (image.hasInterpreter) {
        addDiagnostic(result, "Native ELF requires PT_INTERP/dynamic linking, which is not supported");
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (!isSupportedStaticImage(image)) {
        addDiagnostic(result, "Native ELF image is not a supported static executable image");
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (launchResult.abi != kGuideXOSNativeAbiName) {
        addDiagnostic(result, std::string("Unsupported Native app ABI: ") + launchResult.abi);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (runtimeContext.lifecycleState != NativeAppLifecycleState::Prepared) {
        addDiagnostic(result, std::string("Native app runtime state is not Prepared: ") + NativeAppRuntime::ToString(runtimeContext.lifecycleState));
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    addDiagnostic(result, "Native app executor available; actual execution not implemented yet");
    result.message = joinDiagnostics(result.diagnostics);
    LogDecision(result.appId, result.architecture, true, result.message, "not-executed");
    return result;
}

void NativeElfExecutor::LogDecision(const std::string& appId, const std::string& architecture, bool canExecute, const std::string& reason, const std::string& result) {
    std::ostringstream oss;
    oss << "[NativeElfExecutor] "
        << "App: " << appId
        << " Architecture: " << architecture
        << " CanExecute: " << (canExecute ? "true" : "false")
        << " Reason: " << reason
        << " Result: " << result;
    Logger::write(canExecute ? LogLevel::Info : LogLevel::Warn, oss.str());
}

} // namespace apps
} // namespace gxos
