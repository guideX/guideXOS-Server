#include "native_elf_executor.h"

#include "app_launch_resolver.h"
#include "executable_memory.h"
#include "logger.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
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

std::string pointerToString(void* address) {
    std::ostringstream oss;
    oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(address);
    return oss.str();
}

bool isSupportedStaticImage(const NativeElfImage& image) {
    return image.success && !image.hasInterpreter && image.isExecutable;
}

bool isAmd64HostAndApp(const NativeElfLaunchResult& launchResult) {
    return hostArchitecture() == "amd64" && launchResult.architecture == "amd64";
}

bool checkedAdd(uint64_t left, uint64_t right, uint64_t& result) {
    if (left > std::numeric_limits<uint64_t>::max() - right) return false;
    result = left + right;
    return true;
}

ExecutableMemoryProtection protectionForFlags(uint32_t flags) {
    constexpr uint32_t kPfX = 1;
    constexpr uint32_t kPfW = 2;
    if ((flags & kPfX) != 0) return ExecutableMemoryProtection::ReadExecute;
    if ((flags & kPfW) != 0) return ExecutableMemoryProtection::ReadWrite;
    return ExecutableMemoryProtection::Read;
}

#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
using gx_entry_fn = gx_result (*)(NativeGxAppContext* ctx);
#endif

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
    } else if (!isAmd64HostAndApp(launchResult)) {
        localReason = "Native ELF experimental execution supports amd64 host running amd64 apps only";
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

    if (!isAmd64HostAndApp(launchResult)) {
        addDiagnostic(result, "Native ELF experimental execution supports amd64 host running amd64 apps only");
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

    uint64_t minVirtualAddress = std::numeric_limits<uint64_t>::max();
    uint64_t maxVirtualAddress = 0;
    for (const NativeElfSegment& segment : image.loadedSegments) {
        uint64_t segmentEnd = 0;
        if (!checkedAdd(segment.virtualAddress, segment.memorySize, segmentEnd)) {
            addDiagnostic(result, "Native ELF segment virtual range overflows");
            LogDecision(result.appId, result.architecture, false, result.message, "failure");
            return result;
        }
        if (segment.virtualAddress < minVirtualAddress) minVirtualAddress = segment.virtualAddress;
        if (segmentEnd > maxVirtualAddress) maxVirtualAddress = segmentEnd;
    }

    if (minVirtualAddress == std::numeric_limits<uint64_t>::max() || maxVirtualAddress <= minVirtualAddress) {
        addDiagnostic(result, "Native ELF image has invalid virtual address range");
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    uint64_t mappingSize64 = maxVirtualAddress - minVirtualAddress;
    if (mappingSize64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        addDiagnostic(result, "Native ELF mapping is too large for host address space");
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    ExecutableMemoryBlock mapping;
    std::string memoryError;
    if (!ExecutableMemory::Allocate(static_cast<size_t>(mappingSize64), mapping, memoryError)) {
        addDiagnostic(result, "Executable memory allocation failed: " + memoryError);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    for (const NativeElfSegment& segment : image.loadedSegments) {
        size_t offset = static_cast<size_t>(segment.virtualAddress - minVirtualAddress);
        if (segment.data.size() > mapping.size - offset) {
            addDiagnostic(result, "Native ELF segment copy is out of bounds");
            ExecutableMemory::Free(mapping);
            LogDecision(result.appId, result.architecture, false, result.message, "failure");
            return result;
        }
        if (!segment.data.empty()) std::memcpy(static_cast<char*>(mapping.base) + offset, segment.data.data(), segment.data.size());
    }

    for (const NativeElfSegment& segment : image.loadedSegments) {
        size_t offset = static_cast<size_t>(segment.virtualAddress - minVirtualAddress);
        if (!ExecutableMemory::Protect(mapping, offset, static_cast<size_t>(segment.memorySize), protectionForFlags(segment.flags), memoryError)) {
            addDiagnostic(result, "Executable memory protection failed: " + memoryError);
            ExecutableMemory::Free(mapping);
            LogDecision(result.appId, result.architecture, false, result.message, "failure");
            return result;
        }
    }

    if (image.entryPointVirtualAddress < minVirtualAddress || image.entryPointVirtualAddress >= maxVirtualAddress) {
        addDiagnostic(result, "Native ELF entry point is outside mapped image");
        ExecutableMemory::Free(mapping);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    void* entryAddress = static_cast<char*>(mapping.base) + static_cast<size_t>(image.entryPointVirtualAddress - minVirtualAddress);
    addDiagnostic(result, "Native ELF mapped for experimental execution");
    addDiagnostic(result, "Entry host address resolved: " + pointerToString(entryAddress));

#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    NativeGxAppContext appContext;
    appContext.size = static_cast<uint32_t>(sizeof(NativeGxAppContext));
    appContext.apiVersion = kGuideXOSNativeApiVersion;
    appContext.host = &runtimeContext.hostCalls;
    appContext.userData = nullptr;
    gx_entry_fn entry = reinterpret_cast<gx_entry_fn>(entryAddress);
#ifdef _WIN32
    __try {
        result.exitCode = entry(&appContext);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        addDiagnostic(result, "Native ELF execution raised a structured exception");
        result.exitCode = GX_ERROR_FAILED;
    }
#else
    result.exitCode = entry(&appContext);
#endif
    result.success = result.exitCode == GX_OK;
    addDiagnostic(result, std::string("Native ELF gx_main returned ") + std::to_string(result.exitCode));
#endif

    result.message = joinDiagnostics(result.diagnostics);
    ExecutableMemory::Free(mapping);
    LogDecision(result.appId, result.architecture, result.success, result.message, result.success ? "executed" : "failure");
    return result;
}

bool NativeElfExecutor::ExperimentalExecutionEnabled() {
    return experimentalExecutionEnabled();
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
