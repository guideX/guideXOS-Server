#include "native_elf_executor.h"

#include "app_launch_resolver.h"
#include "allocator.h"
#include "executable_memory.h"
#include "logger.h"
#include "native_app_debug_log.h"
#include "native_app_process_table.h"
#include "process.h"
#include "native_elf_trampoline_win64.h"

#if defined(_WIN32) && defined(__x86_64__)
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>

namespace gxos {
namespace apps {
namespace {

#if defined(_WIN32) && defined(__x86_64__)
struct NativeElfFaultCaptureState {
    volatile LONG enabled = 0;
    volatile uintptr_t entryAddress = 0;
    volatile uintptr_t mappedBase = 0;
    volatile uintptr_t appContext = 0;
    volatile uintptr_t tlsIndexAddress = 0;
    volatile uintptr_t tlsBlock = 0;
    volatile uint64_t tlsBlockSize = 0;
    volatile unsigned long tlsSlot = 0xFFFFFFFFul;
};

NativeElfFaultCaptureState g_nativeElfFaultCapture;
PVOID g_nativeElfFaultHandler = nullptr;

void appendFaultText(char*& cursor, char* end, const char* text) {
    while (*text != '\0' && cursor < end) *cursor++ = *text++;
}

void appendFaultHex(char*& cursor, char* end, uint64_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    appendFaultText(cursor, end, "0x");
    char reversed[16];
    size_t count = 0;
    do {
        reversed[count++] = digits[value & 0xFu];
        value >>= 4;
    } while (value != 0 && count < sizeof(reversed));
    while (count != 0 && cursor < end) *cursor++ = reversed[--count];
}

void appendFaultField(char*& cursor, char* end, const char* name, uint64_t value) {
    appendFaultText(cursor, end, name);
    appendFaultHex(cursor, end, value);
    appendFaultText(cursor, end, " ");
}

LONG CALLBACK nativeElfFaultHandler(EXCEPTION_POINTERS* exceptionPointers) {
    if (InterlockedCompareExchange(&g_nativeElfFaultCapture.enabled, 0, 0) == 0 ||
        exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr ||
        exceptionPointers->ContextRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedExchange(&g_nativeElfFaultCapture.enabled, 0) == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    char buffer[2048];
    char* cursor = buffer;
    char* end = buffer + sizeof(buffer) - 2;
    const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
    const CONTEXT* context = exceptionPointers->ContextRecord;
    appendFaultText(cursor, end, "[NativeElfFault] ");
    appendFaultField(cursor, end, "code=", record->ExceptionCode);
    appendFaultField(cursor, end, "faultAddress=", reinterpret_cast<uintptr_t>(record->ExceptionAddress));
    appendFaultField(cursor, end, "rip=", context->Rip);
    appendFaultField(cursor, end, "rsp=", context->Rsp);
    appendFaultField(cursor, end, "rspMod16=", context->Rsp & 0xFu);
    appendFaultField(cursor, end, "rbp=", context->Rbp);
    appendFaultField(cursor, end, "rax=", context->Rax);
    appendFaultField(cursor, end, "rbx=", context->Rbx);
    appendFaultField(cursor, end, "rcx=", context->Rcx);
    appendFaultField(cursor, end, "rdx=", context->Rdx);
    appendFaultField(cursor, end, "r8=", context->R8);
    appendFaultField(cursor, end, "r9=", context->R9);
    appendFaultField(cursor, end, "fsSelector=", context->SegFs);
    appendFaultField(cursor, end, "gsSelector=", context->SegGs);
    appendFaultField(cursor, end, "entry=", g_nativeElfFaultCapture.entryAddress);
    appendFaultField(cursor, end, "mappedBase=", g_nativeElfFaultCapture.mappedBase);
    appendFaultField(cursor, end, "appContext=", g_nativeElfFaultCapture.appContext);
    appendFaultField(cursor, end, "tlsIndexAddress=", g_nativeElfFaultCapture.tlsIndexAddress);
    appendFaultField(cursor, end, "tlsBlock=", g_nativeElfFaultCapture.tlsBlock);
    appendFaultField(cursor, end, "tlsBlockSize=", g_nativeElfFaultCapture.tlsBlockSize);
    appendFaultField(cursor, end, "tlsSlot=", g_nativeElfFaultCapture.tlsSlot);
    appendFaultField(cursor, end, "info0=", record->NumberParameters > 0 ? record->ExceptionInformation[0] : 0);
    appendFaultField(cursor, end, "info1=", record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0);

    if (g_nativeElfFaultCapture.appContext != 0) {
        const NativeGxAppContext* appContext = reinterpret_cast<const NativeGxAppContext*>(g_nativeElfFaultCapture.appContext);
        appendFaultField(cursor, end, "ctx.size=", appContext->size);
        appendFaultField(cursor, end, "ctx.apiVersion=", appContext->apiVersion);
        appendFaultField(cursor, end, "ctx.host=", reinterpret_cast<uintptr_t>(appContext->host));
        appendFaultField(cursor, end, "ctx.userData=", reinterpret_cast<uintptr_t>(appContext->userData));
    }
    appendFaultText(cursor, end, "\r\n");
    DWORD written = 0;
    HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
    if (stderrHandle != nullptr && stderrHandle != INVALID_HANDLE_VALUE) {
        (void)WriteFile(stderrHandle, buffer, static_cast<DWORD>(cursor - buffer), &written, nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void enableNativeElfFaultDiagnostics() {
    if (g_nativeElfFaultHandler == nullptr) {
        g_nativeElfFaultHandler = AddVectoredExceptionHandler(1, nativeElfFaultHandler);
    }
}

void disableNativeElfFaultDiagnostics() {
    InterlockedExchange(&g_nativeElfFaultCapture.enabled, 0);
    if (g_nativeElfFaultHandler != nullptr) {
        (void)RemoveVectoredExceptionHandler(g_nativeElfFaultHandler);
        g_nativeElfFaultHandler = nullptr;
    }
}
#endif

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

uint64_t steadyClockMillis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

gxos::ProcessTombstoneRecord makeNativeTombstoneRecord(
    const NativeAppRuntimeContext& runtimeContext,
    bool executionFailed,
    const std::string& failureReason) {
    gxos::ProcessTombstoneRecord record;
    record.pid = runtimeContext.processId != 0 ? runtimeContext.processId : Allocator::currentPid();
    record.displayName = runtimeContext.displayName.empty() ? runtimeContext.appId : runtimeContext.displayName;
    record.appId = runtimeContext.appId;
    record.windowTitle = runtimeContext.lastRequestedWindowTitle;
    record.reason = "NormalExit";
    if (executionFailed) {
        if (failureReason.find("exception") != std::string::npos || failureReason.find("Exception") != std::string::npos) {
            record.reason = "Crashed";
        } else {
            record.reason = "Unknown";
        }
    }
    record.exitCodeAvailable = true;
    record.exitCode = runtimeContext.exitCode;
    if (runtimeContext.startTime.time_since_epoch().count() != 0) {
        record.startedAtMsAvailable = true;
        record.startedAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            runtimeContext.startTime.time_since_epoch()).count());
    }
    if (runtimeContext.endTime.time_since_epoch().count() != 0) {
        record.endedAtMsAvailable = true;
        record.endedAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            runtimeContext.endTime.time_since_epoch()).count());
    } else {
        record.endedAtMsAvailable = true;
        record.endedAtMs = steadyClockMillis();
    }
    if (record.startedAtMsAvailable && record.endedAtMs >= record.startedAtMs) {
        record.runtimeMsAvailable = true;
        record.runtimeMs = record.endedAtMs - record.startedAtMs;
    }
    record.finalMemoryBytesAvailable = true;
    record.finalMemoryBytes = Allocator::pidBytes(record.pid);
    record.lastMessage = failureReason.empty()
        ? (executionFailed ? std::string("native app ended without a recorded failure reason") : std::string("native app exited"))
        : failureReason;
    record.restoreSupported = false;
    record.endSupported = false;
    return record;
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
#if defined(_WIN32) && defined(__x86_64__)
using gx_entry_fn = NativeElfWin64Entry;
#else
using gx_entry_fn = gx_result (*)(NativeGxAppContext* ctx);
#endif
#endif

#if defined(_WIN32) && defined(__x86_64__)
constexpr unsigned long kTlsOutOfIndexes = 0xFFFFFFFFul;

extern "C" {
__declspec(dllimport) unsigned long __stdcall TlsAlloc(void);
__declspec(dllimport) int __stdcall TlsFree(unsigned long tlsIndex);
__declspec(dllimport) int __stdcall TlsSetValue(unsigned long tlsIndex, void* tlsValue);
}

struct NativeElfTlsBootstrap {
    unsigned long slot = kTlsOutOfIndexes;
    uint64_t tlsIndexAddress = 0;
    size_t tlsBlockSize = 0;
    std::vector<uint8_t> block;

    void reset() {
        if (slot != kTlsOutOfIndexes) {
            (void)TlsSetValue(slot, nullptr);
            (void)TlsFree(slot);
            slot = kTlsOutOfIndexes;
        }
        tlsIndexAddress = 0;
        tlsBlockSize = 0;
        block.clear();
    }

    ~NativeElfTlsBootstrap() {
        reset();
    }
};

bool tryGetEnvironmentValue(const NativeAppRuntimeContext& runtimeContext, const char* key, std::string& value) {
    auto it = runtimeContext.environment.find(key);
    if (it == runtimeContext.environment.end() || it->second.empty()) return false;
    value = it->second;
    return true;
}

bool tryParseUnsigned64(const std::string& text, uint64_t& value) {
    if (text.empty()) return false;
    try {
        size_t consumed = 0;
        uint64_t parsed = std::stoull(text, &consumed, 0);
        if (consumed != text.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool prepareNativeElfTlsBootstrap(
    const NativeAppRuntimeContext& runtimeContext,
    uint64_t minVirtualAddress,
    uint64_t maxVirtualAddress,
    ExecutableMemoryBlock& mapping,
    NativeElfExecutionResult& result,
    NativeElfTlsBootstrap& bootstrap) {
    std::string tlsIndexAddressText;
    std::string tlsBlockSizeText;
    bool hasIndexHint = tryGetEnvironmentValue(runtimeContext, "GX_NATIVE_ELF_TLS_INDEX_ADDRESS", tlsIndexAddressText);
    bool hasBlockHint = tryGetEnvironmentValue(runtimeContext, "GX_NATIVE_ELF_TLS_BLOCK_SIZE", tlsBlockSizeText);
    if (!hasIndexHint && !hasBlockHint) return true;
    if (!hasIndexHint || !hasBlockHint) {
        addDiagnostic(result, "Native ELF TLS bootstrap hints are incomplete");
        return false;
    }

    uint64_t tlsIndexAddress = 0;
    uint64_t tlsBlockSize64 = 0;
    if (!tryParseUnsigned64(tlsIndexAddressText, tlsIndexAddress)) {
        addDiagnostic(result, "Native ELF TLS bootstrap index address is invalid: " + tlsIndexAddressText);
        return false;
    }
    if (!tryParseUnsigned64(tlsBlockSizeText, tlsBlockSize64) || tlsBlockSize64 == 0) {
        addDiagnostic(result, "Native ELF TLS bootstrap block size is invalid: " + tlsBlockSizeText);
        return false;
    }
    if (tlsBlockSize64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        addDiagnostic(result, "Native ELF TLS bootstrap block size is too large");
        return false;
    }

    uint64_t tlsIndexEndAddress = 0;
    if (tlsIndexAddress < minVirtualAddress || !checkedAdd(tlsIndexAddress, sizeof(uint32_t), tlsIndexEndAddress) || tlsIndexEndAddress > maxVirtualAddress) {
        addDiagnostic(result, "Native ELF TLS bootstrap index address is outside the mapped image");
        return false;
    }

    bootstrap.slot = TlsAlloc();
    if (bootstrap.slot == kTlsOutOfIndexes) {
        addDiagnostic(result, "Native ELF TLS bootstrap failed: TlsAlloc returned TLS_OUT_OF_INDEXES");
        return false;
    }

    try {
        bootstrap.block.assign(static_cast<size_t>(tlsBlockSize64), 0u);
    } catch (const std::exception& ex) {
        addDiagnostic(result, std::string("Native ELF TLS bootstrap failed to allocate the per-thread block: ") + ex.what());
        bootstrap.reset();
        return false;
    } catch (...) {
        addDiagnostic(result, "Native ELF TLS bootstrap failed to allocate the per-thread block");
        bootstrap.reset();
        return false;
    }

    if (!TlsSetValue(bootstrap.slot, bootstrap.block.data())) {
        addDiagnostic(result, "Native ELF TLS bootstrap failed: TlsSetValue rejected the per-thread block");
        bootstrap.reset();
        return false;
    }
    size_t tlsIndexOffset = static_cast<size_t>(tlsIndexAddress - minVirtualAddress);
    if (tlsIndexOffset > mapping.size || sizeof(uint32_t) > mapping.size - tlsIndexOffset) {
        addDiagnostic(result, "Native ELF TLS bootstrap index address is out of bounds for the mapped image");
        bootstrap.reset();
        return false;
    }
    uint32_t slotValue = static_cast<uint32_t>(bootstrap.slot);
    std::memcpy(static_cast<char*>(mapping.base) + tlsIndexOffset, &slotValue, sizeof(slotValue));
    bootstrap.tlsIndexAddress = tlsIndexAddress;
    bootstrap.tlsBlockSize = static_cast<size_t>(tlsBlockSize64);
#if defined(_WIN32) && defined(__x86_64__)
    g_nativeElfFaultCapture.tlsIndexAddress = static_cast<uintptr_t>(tlsIndexAddress);
    g_nativeElfFaultCapture.tlsBlock = reinterpret_cast<uintptr_t>(bootstrap.block.data());
    g_nativeElfFaultCapture.tlsBlockSize = static_cast<uint64_t>(bootstrap.tlsBlockSize);
    g_nativeElfFaultCapture.tlsSlot = bootstrap.slot;
#endif
    addDiagnostic(result, "Native ELF TLS bootstrap installed");
    addDiagnostic(result, "TLS slot index: " + std::to_string(bootstrap.slot));
    addDiagnostic(result, "TLS block size: " + std::to_string(bootstrap.tlsBlockSize));
    return true;
}
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
        localReason = "Wrong architecture: host=" + hostArchitecture() + " app=" + launchResult.architecture + "; cross-architecture execution is not supported";
    } else if (!isAmd64HostAndApp(launchResult)) {
        localReason = "Native ELF experimental execution supports amd64 host running amd64 apps only";
    } else if (image.hasInterpreter) {
        localReason = "PT_INTERP present; dynamic linker/dynamic linking is not supported";
    } else if (!isSupportedStaticImage(image)) {
        localReason = "Native ELF image is not a supported static executable image";
    } else if (launchResult.abi != kGuideXOSNativeAbiName) {
        localReason = std::string("ABI mismatch: expected ") + kGuideXOSNativeAbiName + ", got " + launchResult.abi;
    } else if (runtimeContext.lifecycleState != NativeAppLifecycleState::Prepared) {
        localReason = std::string("Native app runtime state is not Prepared: ") + NativeAppRuntime::ToString(runtimeContext.lifecycleState);
    } else {
        canExecute = true;
        localReason = "Native ELF executor gate passed";
    }

    if (reason) *reason = localReason;
    LogDecision(launchResult.appId, launchResult.architecture, canExecute, localReason, canExecute ? "available" : "unavailable");
    return canExecute;
}

NativeElfExecutionResult NativeElfExecutor::Execute(
    const NativeElfLaunchResult& launchResult,
    const NativeElfImage& image,
    NativeAppRuntimeContext& runtimeContext) {
    NativeElfExecutionResult result;
    result.appId = launchResult.appId;
    result.architecture = launchResult.architecture;
    result.exitCode = 0;
    result.runtimeId = runtimeContext.runtimeId;
    result.lifecycleStateBeforeExecution = NativeAppRuntime::ToString(runtimeContext.lifecycleState);
    result.preferredBaseAddress = image.preferredBaseAddress;

    if (!experimentalExecutionEnabled()) {
        addDiagnostic(result, "Native ELF execution disabled by build flag");
        NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (hostArchitecture() != launchResult.architecture) {
        addDiagnostic(result, "Wrong architecture: host=" + hostArchitecture() + " app=" + launchResult.architecture + "; cross-architecture execution is not supported");
        NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (!isAmd64HostAndApp(launchResult)) {
        addDiagnostic(result, "Native ELF experimental execution supports amd64 host running amd64 apps only");
        NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (image.hasInterpreter) {
        addDiagnostic(result, "PT_INTERP present; dynamic linker/dynamic linking is not supported");
        NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (!isSupportedStaticImage(image)) {
        addDiagnostic(result, "Native ELF image is not a supported static executable image");
        NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (launchResult.abi != kGuideXOSNativeAbiName) {
        addDiagnostic(result, std::string("ABI mismatch: expected ") + kGuideXOSNativeAbiName + ", got " + launchResult.abi);
        NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (runtimeContext.lifecycleState != NativeAppLifecycleState::Prepared) {
        addDiagnostic(result, std::string("Native app runtime state is not Prepared: ") + NativeAppRuntime::ToString(runtimeContext.lifecycleState));
        NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    uint64_t minVirtualAddress = std::numeric_limits<uint64_t>::max();
    uint64_t maxVirtualAddress = 0;
    for (const NativeElfSegment& segment : image.loadedSegments) {
        uint64_t segmentEnd = 0;
        if (!checkedAdd(segment.virtualAddress, segment.memorySize, segmentEnd)) {
            addDiagnostic(result, "Native ELF segment virtual range overflows");
            NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
            LogDecision(result.appId, result.architecture, false, result.message, "failure");
            return result;
        }
        if (segment.virtualAddress < minVirtualAddress) minVirtualAddress = segment.virtualAddress;
        if (segmentEnd > maxVirtualAddress) maxVirtualAddress = segmentEnd;
    }

    if (minVirtualAddress == std::numeric_limits<uint64_t>::max() || maxVirtualAddress <= minVirtualAddress) {
        addDiagnostic(result, "Native ELF image has invalid virtual address range");
        NativeAppDebugLog::Add(runtimeContext.runtimeId, runtimeContext.appId, "error", result.message);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (image.isPositionIndependent) {
        addDiagnostic(result, "ET_DYN/PIE execution is unsupported; relocations are not implemented");
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }

    if (image.preferredBaseAddress != minVirtualAddress) {
        addDiagnostic(result, "ET_EXEC preferred base does not match minimum PT_LOAD virtual address");
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
    result.preferredBaseMappingAttempted = true;
    void* preferredBase = reinterpret_cast<void*>(static_cast<uintptr_t>(image.preferredBaseAddress));
    if (!ExecutableMemory::AllocateAt(preferredBase, static_cast<size_t>(mappingSize64), mapping, memoryError)) {
        addDiagnostic(result, "Preferred-base allocation failure: ET_EXEC preferred base could not be mapped; relocations are not supported");
        if (!memoryError.empty()) addDiagnostic(result, "preferred-base mapping error: " + memoryError);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }
    result.actualMappedBaseAddress = reinterpret_cast<uint64_t>(mapping.base);
    result.preferredBaseMappingSucceeded = mapping.base == preferredBase;
    if (!result.preferredBaseMappingSucceeded) {
        addDiagnostic(result, "Preferred-base allocation failure: ET_EXEC preferred base could not be mapped exactly; relocations are not supported");
        ExecutableMemory::Free(mapping);
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

#if defined(_WIN32) && defined(__x86_64__)
    NativeElfTlsBootstrap tlsBootstrap;
    if (!prepareNativeElfTlsBootstrap(runtimeContext, minVirtualAddress, maxVirtualAddress, mapping, result, tlsBootstrap)) {
        ExecutableMemory::Free(mapping);
        LogDecision(result.appId, result.architecture, false, result.message, "failure");
        return result;
    }
#endif

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
    result.entryHostAddress = reinterpret_cast<uint64_t>(entryAddress);
    addDiagnostic(result, "Native ELF mapped for experimental execution");
    addDiagnostic(result, "Preferred base: " + pointerToString(reinterpret_cast<void*>(static_cast<uintptr_t>(result.preferredBaseAddress))));
    addDiagnostic(result, "Actual mapped base: " + pointerToString(mapping.base));
    addDiagnostic(result, std::string("Preferred-base mapping: ") + (result.preferredBaseMappingSucceeded ? "success" : "failure"));
    addDiagnostic(result, "Entry host address resolved: " + pointerToString(entryAddress));

#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
    NativeGxAppContext appContext;
    appContext.size = static_cast<uint32_t>(sizeof(NativeGxAppContext));
    appContext.apiVersion = kGuideXOSNativeApiVersion;
    appContext.host = &runtimeContext.hostCalls;
    appContext.userData = nullptr;
    gx_entry_fn entry = reinterpret_cast<gx_entry_fn>(entryAddress);
#if defined(_WIN32) && defined(__x86_64__)
    const bool faultDiagnosticsEnabled = std::getenv("GX_NATIVE_ELF_FAULT_DIAGNOSTICS") != nullptr;
    if (faultDiagnosticsEnabled) {
        enableNativeElfFaultDiagnostics();
        g_nativeElfFaultCapture.entryAddress = reinterpret_cast<uintptr_t>(entryAddress);
        g_nativeElfFaultCapture.mappedBase = reinterpret_cast<uintptr_t>(mapping.base);
        g_nativeElfFaultCapture.appContext = reinterpret_cast<uintptr_t>(&appContext);
        InterlockedExchange(&g_nativeElfFaultCapture.enabled, 1);
    }
#endif
    NativeAppProcessTable::RegisterPrepared(runtimeContext, true, hostArchitecture());
    NativeAppRuntime::BeginHostCallDispatch(runtimeContext);
    NativeAppProcessTable::MarkRunning(runtimeContext.runtimeId);
    bool executionFailed = false;
    std::string failureReason;
    std::thread smokeTestCloseThread;
    if (runtimeContext.environment.find("GX_NATIVE_SMOKETEST") != runtimeContext.environment.end()) {
        smokeTestCloseThread = std::thread([&runtimeContext]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            NativeAppRuntime::RequestCloseOwnedWindows(runtimeContext);
        });
    }
#if defined(_WIN32) && defined(_MSC_VER)
    __try {
        result.exitCode = entry(&appContext);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        failureReason = "Native ELF execution raised a structured exception";
        addDiagnostic(result, failureReason);
        result.exitCode = GX_ERROR_FAILED;
        executionFailed = true;
    }
#else
    try {
#if defined(_WIN32) && defined(__x86_64__)
        result.trampolineUsed = true;
        addDiagnostic(result, "Windows amd64 trampoline used: yes");
        result.exitCode = CallNativeElfWin64Entry(entry, &appContext);
#else
        result.trampolineUsed = false;
        addDiagnostic(result, "Windows amd64 trampoline used: no");
        result.exitCode = entry(&appContext);
#endif
    } catch (const std::exception& ex) {
        failureReason = std::string("Native ELF execution raised an exception: ") + ex.what();
        addDiagnostic(result, failureReason);
        result.exitCode = GX_ERROR_FAILED;
        executionFailed = true;
    } catch (...) {
        failureReason = "Native ELF execution raised an unknown exception";
        addDiagnostic(result, failureReason);
        result.exitCode = GX_ERROR_FAILED;
        executionFailed = true;
    }
#endif
#if defined(_WIN32) && defined(__x86_64__)
    if (faultDiagnosticsEnabled) disableNativeElfFaultDiagnostics();
#endif
    if (smokeTestCloseThread.joinable()) smokeTestCloseThread.join();
    NativeAppRuntime::EndHostCallDispatch(runtimeContext);
    if (runtimeContext.lastWaitResult == GX_ERROR_TIMEOUT && result.exitCode == GX_OK) addDiagnostic(result, "wait_for_close timed out; cleaning up remaining owned windows");
    NativeAppRuntime::Cleanup(runtimeContext, (executionFailed || result.exitCode != GX_OK) ? NativeAppLifecycleState::Failed : NativeAppLifecycleState::Exited, result.exitCode, failureReason);
    const gxos::ProcessTombstoneRecord tombstone = makeNativeTombstoneRecord(runtimeContext, executionFailed, failureReason);
    if (ProcessTable::claimTombstoneCapture(tombstone.pid)) {
        ProcessTable::recordTombstone(tombstone);
    }
    NativeAppProcessTable::UpdateFromRuntime(runtimeContext);
    NativeAppProcessTable::MarkCompleted(runtimeContext.runtimeId, runtimeContext.lifecycleState, runtimeContext.exitCode, runtimeContext.failureReason);
    result.success = result.exitCode == GX_OK && !executionFailed;
    result.hostLogCallCount = runtimeContext.hostLogCallCount;
    result.lastHostLogMessage = runtimeContext.lastHostLogMessage;
    result.apiVersionReturned = runtimeContext.lastApiVersionReturned;
    result.requestWindowCallCount = runtimeContext.requestWindowCallCount;
    result.lastWindowId = runtimeContext.lastCreatedWindowId;
    result.lastWindowTitle = runtimeContext.lastRequestedWindowTitle;
    result.requestWindowResult = runtimeContext.lastRequestWindowResult;
    result.drawTextCallCount = runtimeContext.drawTextCallCount;
    result.lastDrawTextWindow = runtimeContext.lastDrawTextWindow;
    result.lastDrawText = runtimeContext.lastDrawText;
    result.lastDrawTextResult = runtimeContext.lastDrawTextResult;
    result.drawRectCallCount = runtimeContext.drawRectCallCount;
    result.lastDrawRectWindow = runtimeContext.lastDrawRectWindow;
    result.lastDrawRectWidth = runtimeContext.lastDrawRectWidth;
    result.lastDrawRectHeight = runtimeContext.lastDrawRectHeight;
    result.lastDrawRectColor = runtimeContext.lastDrawRectColor;
    result.lastDrawRectResult = runtimeContext.lastDrawRectResult;
    result.waitForCloseCallCount = runtimeContext.waitForCloseCallCount;
    result.lastWaitWindow = runtimeContext.lastWaitWindow;
    result.lastWaitTimeoutMs = runtimeContext.lastWaitTimeoutMs;
    result.lastWaitResult = runtimeContext.lastWaitResult;
    result.pollEventCallCount = runtimeContext.pollEventCallCount;
    result.lastEventType = runtimeContext.lastEventType;
    result.lastEventWindow = runtimeContext.lastEventWindow;
    result.lastPollEventResult = runtimeContext.lastPollEventResult;
    result.paintEventCount = runtimeContext.paintEventCount;
    result.lastPaintWindow = runtimeContext.lastPaintWindow;
    result.lastPaintWidth = runtimeContext.lastPaintWidth;
    result.lastPaintHeight = runtimeContext.lastPaintHeight;
    result.keyEventCount = runtimeContext.keyEventCount;
    result.lastKeyWindow = runtimeContext.lastKeyWindow;
    result.lastKeyCode = runtimeContext.lastKeyCode;
    result.lastKeyAction = runtimeContext.lastKeyAction;
    result.lastKeyModifiers = runtimeContext.lastKeyModifiers;
    result.mouseEventCount = runtimeContext.mouseEventCount;
    result.lastMouseWindow = runtimeContext.lastMouseWindow;
    result.lastMouseX = runtimeContext.lastMouseX;
    result.lastMouseY = runtimeContext.lastMouseY;
    result.lastMousePackedButtonAction = runtimeContext.lastMousePackedButtonAction;
    result.lastMouseModifiers = runtimeContext.lastMouseModifiers;
    result.fileReadCallCount = runtimeContext.fileReadCallCount;
    result.fileExistsCallCount = runtimeContext.fileExistsCallCount;
    result.lastFilePath = runtimeContext.lastFilePath;
    result.lastFileReadBytes = runtimeContext.lastFileReadBytes;
    result.lastFileIoResult = runtimeContext.lastFileIoResult;
    result.lifecycleStateAfterExecution = NativeAppRuntime::ToString(runtimeContext.lifecycleState);
    result.cleanupAttempted = runtimeContext.cleanupAttempted;
    result.cleanedWindowCount = runtimeContext.cleanedWindowCount;
    result.remainingOwnedWindowCount = static_cast<uint32_t>(runtimeContext.createdWindowHandles.size());
    result.failureReason = runtimeContext.failureReason;
    addDiagnostic(result, std::string("Native ELF gx_main returned ") + std::to_string(result.exitCode));
    addDiagnostic(result, "runtimeId: " + std::to_string(result.runtimeId));
    addDiagnostic(result, "lifecycle state before execution: " + result.lifecycleStateBeforeExecution);
    addDiagnostic(result, "lifecycle state after execution: " + result.lifecycleStateAfterExecution);
    addDiagnostic(result, std::string("cleanup attempted: ") + (result.cleanupAttempted ? "true" : "false"));
    addDiagnostic(result, "cleaned window count: " + std::to_string(result.cleanedWindowCount));
    addDiagnostic(result, "remaining owned window count: " + std::to_string(result.remainingOwnedWindowCount));
    if (!result.failureReason.empty()) addDiagnostic(result, "failure reason: " + result.failureReason);
    addDiagnostic(result, "Host log call count: " + std::to_string(result.hostLogCallCount));
    if (!result.lastHostLogMessage.empty()) addDiagnostic(result, "Last host log message: " + result.lastHostLogMessage);
    addDiagnostic(result, "API version returned: " + std::to_string(result.apiVersionReturned));
    addDiagnostic(result, "request_window call count: " + std::to_string(result.requestWindowCallCount));
    addDiagnostic(result, "last window id: " + std::to_string(result.lastWindowId));
    if (!result.lastWindowTitle.empty()) addDiagnostic(result, "last window title: " + result.lastWindowTitle);
    addDiagnostic(result, "request_window result: " + std::to_string(result.requestWindowResult));
    addDiagnostic(result, "drawText call count: " + std::to_string(result.drawTextCallCount));
    addDiagnostic(result, "last drawText window: " + std::to_string(result.lastDrawTextWindow));
    if (!result.lastDrawText.empty()) addDiagnostic(result, "last drawText: " + result.lastDrawText);
    addDiagnostic(result, "last drawText result: " + std::to_string(result.lastDrawTextResult));
    addDiagnostic(result, "drawRect call count: " + std::to_string(result.drawRectCallCount));
    addDiagnostic(result, "last drawRect window: " + std::to_string(result.lastDrawRectWindow));
    addDiagnostic(result, "last drawRect width: " + std::to_string(result.lastDrawRectWidth));
    addDiagnostic(result, "last drawRect height: " + std::to_string(result.lastDrawRectHeight));
    addDiagnostic(result, "last drawRect color: " + std::to_string(result.lastDrawRectColor));
    addDiagnostic(result, "last drawRect result: " + std::to_string(result.lastDrawRectResult));
    addDiagnostic(result, "waitForClose call count: " + std::to_string(result.waitForCloseCallCount));
    addDiagnostic(result, "last wait window: " + std::to_string(result.lastWaitWindow));
    addDiagnostic(result, "last wait timeoutMs: " + std::to_string(result.lastWaitTimeoutMs));
    addDiagnostic(result, "last wait result: " + std::to_string(result.lastWaitResult));
    addDiagnostic(result, "pollEvent call count: " + std::to_string(result.pollEventCallCount));
    addDiagnostic(result, "last event type: " + std::to_string(static_cast<uint32_t>(result.lastEventType)));
    addDiagnostic(result, "last event window: " + std::to_string(result.lastEventWindow));
    addDiagnostic(result, "last pollEvent result: " + std::to_string(result.lastPollEventResult));
    addDiagnostic(result, "paint event count: " + std::to_string(result.paintEventCount));
    addDiagnostic(result, "last paint window: " + std::to_string(result.lastPaintWindow));
    addDiagnostic(result, "last paint width: " + std::to_string(result.lastPaintWidth));
    addDiagnostic(result, "last paint height: " + std::to_string(result.lastPaintHeight));
    addDiagnostic(result, "key event count: " + std::to_string(result.keyEventCount));
    addDiagnostic(result, "last key window: " + std::to_string(result.lastKeyWindow));
    addDiagnostic(result, "last key code: " + std::to_string(result.lastKeyCode));
    addDiagnostic(result, "last key action: " + std::to_string(result.lastKeyAction));
    addDiagnostic(result, "last key modifiers: " + std::to_string(result.lastKeyModifiers));
    addDiagnostic(result, "mouse event count: " + std::to_string(result.mouseEventCount));
    addDiagnostic(result, "last mouse window: " + std::to_string(result.lastMouseWindow));
    addDiagnostic(result, "last mouse x: " + std::to_string(result.lastMouseX));
    addDiagnostic(result, "last mouse y: " + std::to_string(result.lastMouseY));
    addDiagnostic(result, "last mouse packed button action: " + std::to_string(result.lastMousePackedButtonAction));
    addDiagnostic(result, "last mouse modifiers: " + std::to_string(result.lastMouseModifiers));
    addDiagnostic(result, "fileRead call count: " + std::to_string(result.fileReadCallCount));
    addDiagnostic(result, "fileExists call count: " + std::to_string(result.fileExistsCallCount));
    if (!result.lastFilePath.empty()) addDiagnostic(result, "last file path: " + result.lastFilePath);
    addDiagnostic(result, "last file read bytes: " + std::to_string(result.lastFileReadBytes));
    addDiagnostic(result, "last file IO result: " + std::to_string(result.lastFileIoResult));
    if (result.lastFileIoResult == GX_ERROR_PERMISSION_DENIED) addDiagnostic(result, "missing permission: file.read");
    if (result.lastFileIoResult == GX_ERROR_FAILED && !result.lastFilePath.empty()) addDiagnostic(result, "missing resource file: " + result.lastFilePath);
    if (result.requestWindowResult == GX_ERROR_PERMISSION_DENIED) addDiagnostic(result, "missing permission: window");
    if (result.drawTextCallCount > 0 && result.lastDrawTextResult == GX_ERROR_PERMISSION_DENIED) addDiagnostic(result, "missing permission: draw/window");
    if (result.drawRectCallCount > 0 && result.lastDrawRectResult == GX_ERROR_PERMISSION_DENIED) addDiagnostic(result, "missing permission: draw/window");
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
