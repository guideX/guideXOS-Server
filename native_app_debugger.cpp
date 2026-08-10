#include "native_app_debugger.h"

#include "allocator.h"
#include "logger.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gxos {
namespace apps {
namespace {

constexpr uint32_t kMaxDebugRuntimes = 16;
constexpr uint32_t kMaxPhysicalBindings = 64;
constexpr uint32_t kMaxLogicalOwners = 8;
constexpr uint32_t kPfX = 1;
constexpr uint32_t kPfW = 2;
constexpr uint32_t kResumeModeRelease = 1;
constexpr uint32_t kResumeModeCancel = 2;
constexpr uint32_t kResumeModeInternalSingleStep = 3;
constexpr uint64_t kAmd64TrapFlag = 0x100ull;

struct DebugSegment {
    uint64_t start = 0;
    uint64_t end = 0;
    uint32_t flags = 0;
};

struct PhysicalBinding {
    bool used = false;
    uint64_t bindingId = 0;
    uint64_t sessionGeneration = 0;
    uint64_t address = 0;
    uint8_t originalByte = 0;
    uint8_t installedByte = 0xCC;
    bool installed = false;
    uint32_t ownerCount = 0;
    uint64_t owners[kMaxLogicalOwners] = {};
};

struct DebugRuntime {
    std::atomic<bool> active{false};
    std::atomic<bool> gateOpen{false};
    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> trapObserved{false};
    std::atomic<uint32_t> resumeMode{0};
    uint64_t runtimeId = 0;
    uint64_t processId = 0;
    uint64_t imageBase = 0;
    uint64_t imageEnd = 0;
    ExecutableMemoryBlock mapping;
    DebugSegment segments[16] = {};
    uint32_t segmentCount = 0;
    uint64_t nextBindingId = 1;
    PhysicalBinding bindings[kMaxPhysicalBindings] = {};
    std::atomic<uint64_t> trapThreadId{0};
    std::atomic<uint64_t> trapInstructionPointer{0};
    std::atomic<uint64_t> trapAddress{0};
    std::atomic<uint64_t> trapBindingId{0};
    std::atomic<uint64_t> trapStopGeneration{0};
    std::atomic<uint64_t> stopGenerationCounter{0};
    std::atomic<bool> singleStepObserved{false};
    std::atomic<bool> singleStepFailed{false};
    std::atomic<bool> singleStepPending{false};
    std::atomic<uint64_t> pendingSessionGeneration{0};
    std::atomic<uint64_t> pendingThreadId{0};
    std::atomic<uint64_t> pendingStopGeneration{0};
    std::atomic<uint64_t> pendingBindingId{0};
    std::atomic<uint64_t> pendingAddress{0};
    std::atomic<uint32_t> pendingReinstall{0};
    std::atomic<uint64_t> pendingRflagsBeforeStep{0};
    std::atomic<uint64_t> pendingRflagsWithTrapFlag{0};
    std::atomic<uint64_t> singleStepRflagsAfterClear{0};
    gx_development_debug_register_context trapContext{};
    gx_development_debug_register_context singleStepContext{};
#ifdef _WIN32
    HANDLE gateEvent = nullptr;
    HANDLE trapEvent = nullptr;
    HANDLE resumeEvent = nullptr;
#endif
};

std::array<DebugRuntime, kMaxDebugRuntimes> g_runtimes;
std::mutex g_mutex;
std::once_flag g_handlerOnce;

void clearSnapshot(gx_development_debug_snapshot* snapshot) {
    if (!snapshot) return;
    *snapshot = gx_development_debug_snapshot{};
    snapshot->size = sizeof(gx_development_debug_snapshot);
    snapshot->version = GX_DEVELOPMENT_DEBUG_API_VERSION;
    snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_NONE;
}

void setError(gx_development_debug_snapshot* snapshot, const char* message) {
    if (!snapshot) return;
    snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_REJECTED;
    if (!message) message = "debug operation rejected";
    std::strncpy(snapshot->errorMessage, message, sizeof(snapshot->errorMessage) - 1);
    snapshot->errorMessage[sizeof(snapshot->errorMessage) - 1] = '\0';
}

ExecutableMemoryProtection originalProtection(uint32_t flags) {
    if ((flags & kPfX) != 0) return ExecutableMemoryProtection::ReadExecute;
    if ((flags & kPfW) != 0) return ExecutableMemoryProtection::ReadWrite;
    return ExecutableMemoryProtection::Read;
}

DebugRuntime* findRuntimeLocked(uint64_t runtimeId, uint64_t processId) {
    for (DebugRuntime& runtime : g_runtimes) {
        if (!runtime.active.load(std::memory_order_acquire)) continue;
        if (runtime.runtimeId != runtimeId || runtime.processId != processId) continue;
        return &runtime;
    }
    return nullptr;
}

DebugRuntime* findRuntimeByProcess(uint64_t processId) {
    if (processId == 0) return nullptr;
    for (DebugRuntime& runtime : g_runtimes) {
        if (!runtime.active.load(std::memory_order_acquire)) continue;
        if (runtime.processId == processId) return &runtime;
    }
    return nullptr;
}

bool executableAddress(const DebugRuntime& runtime, uint64_t address, DebugSegment** outSegment) {
    if (outSegment) *outSegment = nullptr;
    for (uint32_t i = 0; i < runtime.segmentCount; ++i) {
        const DebugSegment& segment = runtime.segments[i];
        if (address < segment.start || address >= segment.end || (segment.flags & kPfX) == 0) continue;
        if (outSegment) *outSegment = const_cast<DebugSegment*>(&segment);
        return true;
    }
    return false;
}

bool flushInstruction(void* address) {
#ifdef _WIN32
    return FlushInstructionCache(GetCurrentProcess(), address, 1) != FALSE;
#else
    __builtin___clear_cache(static_cast<char*>(address), static_cast<char*>(address) + 1);
    return true;
#endif
}

bool writeCodeByte(DebugRuntime& runtime, uint64_t address, uint8_t value, uint32_t flags, std::string& error) {
    if (address < runtime.imageBase || address >= runtime.imageEnd || !runtime.mapping.base) {
        error = "target address is outside the current image";
        return false;
    }
    const uint64_t offset64 = address - runtime.imageBase;
    if (offset64 >= runtime.mapping.size) {
        error = "target address is outside the backing mapping";
        return false;
    }
    const size_t offset = static_cast<size_t>(offset64);
    ExecutableMemoryBlock view = runtime.mapping;
    if (!ExecutableMemory::Protect(view, offset, 1, ExecutableMemoryProtection::ReadWrite, error)) return false;
    volatile uint8_t* byte = reinterpret_cast<volatile uint8_t*>(static_cast<char*>(runtime.mapping.base) + offset);
    *byte = value;
    uint8_t* writableByte = const_cast<uint8_t*>(byte);
    if (!flushInstruction(writableByte)) {
        error = "instruction-cache flush failed";
        return false;
    }
    if (!ExecutableMemory::Protect(view, offset, 1, originalProtection(flags), error)) return false;
    if (*byte != value) {
        error = "patched byte verification failed";
        return false;
    }
    return true;
}

bool restoreBinding(DebugRuntime& runtime, PhysicalBinding& binding, std::string& error) {
    if (!binding.used) return true;
    if (!binding.installed) {
        binding.used = false;
        return true;
    }
    DebugSegment* segment = nullptr;
    if (!executableAddress(runtime, binding.address, &segment)) {
        error = "breakpoint address is no longer executable";
        return false;
    }
    if (!writeCodeByte(runtime, binding.address, binding.originalByte, segment->flags, error)) return false;
    binding.installed = false;
    binding.used = false;
    return true;
}

bool restoreForInternalSingleStep(DebugRuntime& runtime, PhysicalBinding& binding, std::string& error) {
    if (!binding.used || !binding.installed) {
        error = "breakpoint is not installed at the stopped address";
        return false;
    }
    DebugSegment* segment = nullptr;
    if (!executableAddress(runtime, binding.address, &segment)) {
        error = "breakpoint address is no longer executable";
        return false;
    }
    if (!writeCodeByte(runtime, binding.address, binding.originalByte, segment->flags, error)) return false;
    binding.installed = false;
    if (*reinterpret_cast<volatile uint8_t*>(static_cast<char*>(runtime.mapping.base) +
                                             static_cast<size_t>(binding.address - runtime.imageBase)) != binding.originalByte) {
        error = "original breakpoint byte verification failed";
        return false;
    }
    return true;
}

bool reinstallAfterInternalSingleStep(DebugRuntime& runtime, PhysicalBinding& binding, std::string& error) {
    if (!binding.used) return true;
    DebugSegment* segment = nullptr;
    if (!executableAddress(runtime, binding.address, &segment)) {
        error = "breakpoint address is no longer executable";
        return false;
    }
    if (!writeCodeByte(runtime, binding.address, 0xCC, segment->flags, error)) return false;
    binding.installed = true;
    if (*reinterpret_cast<volatile uint8_t*>(static_cast<char*>(runtime.mapping.base) +
                                             static_cast<size_t>(binding.address - runtime.imageBase)) != 0xCC) {
        error = "breakpoint reinstall verification failed";
        return false;
    }
    return true;
}

bool restoreAllLocked(DebugRuntime& runtime, std::string& error) {
    bool restored = true;
    for (PhysicalBinding& binding : runtime.bindings) {
        if (!binding.used) continue;
        std::string bindingError;
        if (!restoreBinding(runtime, binding, bindingError)) {
            restored = false;
            if (error.empty()) error = bindingError;
        }
    }
    return restored;
}

#ifdef _WIN32
void captureWindowsContext(const CONTEXT& source, uint64_t processId, uint64_t runtimeId,
                           uint64_t threadId, uint64_t sessionGeneration, uint64_t stopGeneration,
                           gx_development_debug_register_context& target) {
    target = gx_development_debug_register_context{};
    target.architecture = GX_DEVELOPMENT_DEBUG_ARCHITECTURE_AMD64;
    target.valid = 1;
    target.processId = processId;
    target.nativeRuntimeId = runtimeId;
    target.threadId = threadId;
    target.sessionGeneration = sessionGeneration;
    target.stopGeneration = stopGeneration;
    target.rip = static_cast<uint64_t>(source.Rip);
    target.rflags = static_cast<uint64_t>(source.EFlags);
    target.rsp = static_cast<uint64_t>(source.Rsp);
    target.rbp = static_cast<uint64_t>(source.Rbp);
    target.rax = static_cast<uint64_t>(source.Rax);
    target.rbx = static_cast<uint64_t>(source.Rbx);
    target.rcx = static_cast<uint64_t>(source.Rcx);
    target.rdx = static_cast<uint64_t>(source.Rdx);
    target.rsi = static_cast<uint64_t>(source.Rsi);
    target.rdi = static_cast<uint64_t>(source.Rdi);
    target.r8 = static_cast<uint64_t>(source.R8);
    target.r9 = static_cast<uint64_t>(source.R9);
    target.r10 = static_cast<uint64_t>(source.R10);
    target.r11 = static_cast<uint64_t>(source.R11);
    target.r12 = static_cast<uint64_t>(source.R12);
    target.r13 = static_cast<uint64_t>(source.R13);
    target.r14 = static_cast<uint64_t>(source.R14);
    target.r15 = static_cast<uint64_t>(source.R15);
}

LONG CALLBACK debugVectoredHandler(EXCEPTION_POINTERS* pointers) {
    if (!pointers || !pointers->ExceptionRecord || !pointers->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t processId = Allocator::currentPid();
    DebugRuntime* runtime = findRuntimeByProcess(processId);
    if (!runtime || !runtime->gateOpen.load(std::memory_order_acquire)) return EXCEPTION_CONTINUE_SEARCH;

    if (pointers->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        Logger::write(LogLevel::Info, "[NativeAppDebugger] single-step exception processId=" + std::to_string(processId) +
            " runtime=" + std::to_string(runtime->runtimeId) +
            " pending=" + (runtime->singleStepPending.load(std::memory_order_acquire) ? "true" : "false") +
            " threadId=" + std::to_string(static_cast<uint64_t>(GetCurrentThreadId())));
        if (!runtime->singleStepPending.load(std::memory_order_acquire)) return EXCEPTION_CONTINUE_SEARCH;
        const uint64_t threadId = static_cast<uint64_t>(GetCurrentThreadId());
        if (threadId != runtime->pendingThreadId.load(std::memory_order_acquire)) return EXCEPTION_CONTINUE_SEARCH;
        const uint64_t stopGeneration = runtime->pendingStopGeneration.load(std::memory_order_acquire);
        const uint64_t bindingId = runtime->pendingBindingId.load(std::memory_order_acquire);
        const uint64_t address = runtime->pendingAddress.load(std::memory_order_acquire);
        captureWindowsContext(*pointers->ContextRecord, processId, runtime->runtimeId, threadId,
                              runtime->pendingSessionGeneration.load(std::memory_order_acquire), stopGeneration,
                              runtime->singleStepContext);
        const uint64_t rflagsBeforeClear = static_cast<uint64_t>(pointers->ContextRecord->EFlags);
        pointers->ContextRecord->EFlags = static_cast<DWORD>(rflagsBeforeClear & ~kAmd64TrapFlag);
        runtime->singleStepRflagsAfterClear.store(static_cast<uint64_t>(pointers->ContextRecord->EFlags), std::memory_order_release);

        bool rebound = false;
        if (runtime->cancelRequested.load(std::memory_order_acquire)) {
            for (PhysicalBinding& binding : runtime->bindings) {
                if (binding.used && binding.bindingId == bindingId && binding.address == address) {
                    binding.used = false;
                    binding.installed = false;
                    break;
                }
            }
        } else if (runtime->pendingReinstall.load(std::memory_order_acquire) != 0) {
            for (PhysicalBinding& binding : runtime->bindings) {
                if (!binding.used || binding.bindingId != bindingId || binding.address != address) continue;
                std::string error;
                rebound = reinstallAfterInternalSingleStep(*runtime, binding, error);
                if (!rebound) Logger::write(LogLevel::Warn, "[NativeAppDebugger] breakpoint reinstall failed: " + error);
                break;
            }
        } else {
            for (PhysicalBinding& binding : runtime->bindings) {
                if (binding.used && binding.bindingId == bindingId && binding.address == address) {
                    binding.used = false;
                    binding.installed = false;
                    rebound = true;
                    break;
                }
            }
        }
        if (runtime->pendingReinstall.load(std::memory_order_acquire) != 0 && !rebound &&
            !runtime->cancelRequested.load(std::memory_order_acquire))
            runtime->singleStepFailed.store(true, std::memory_order_release);
        runtime->trapObserved.store(false, std::memory_order_release);
        runtime->singleStepPending.store(false, std::memory_order_release);
        runtime->resumeMode.store(0, std::memory_order_release);
        runtime->singleStepObserved.store(true, std::memory_order_release);
        if (runtime->trapEvent) SetEvent(runtime->trapEvent);
        Logger::write(LogLevel::Info, "[NativeAppDebugger] EXCEPTION_SINGLE_STEP runtimeId=" +
            std::to_string(runtime->runtimeId) + " threadId=" + std::to_string(threadId) +
            " stopGeneration=" + std::to_string(stopGeneration) +
            " rflagsBeforeClear=0x" + [&rflagsBeforeClear]() { std::ostringstream value; value << std::hex << rflagsBeforeClear; return value.str(); }() +
            " rflagsWithTF=0x" + [&runtime]() { std::ostringstream value; value << std::hex << runtime->pendingRflagsWithTrapFlag.load(std::memory_order_acquire); return value.str(); }() +
            " rflagsAfterClear=0x" + [&runtime]() { std::ostringstream value; value << std::hex << runtime->singleStepRflagsAfterClear.load(std::memory_order_acquire); return value.str(); }() +
            " rebound=" + (rebound ? "true" : "false"));
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (pointers->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;

    Logger::write(LogLevel::Info, "[NativeAppDebugger] breakpoint exception processId=" + std::to_string(processId) +
        " runtime=" + std::to_string(runtime->runtimeId) +
        " gate=" + (runtime->gateOpen.load(std::memory_order_acquire) ? "open" : "closed"));
    const uint64_t instructionPointer = static_cast<uint64_t>(pointers->ContextRecord->Rip);
    const uint64_t exceptionAddress = reinterpret_cast<uint64_t>(pointers->ExceptionRecord->ExceptionAddress);
    for (PhysicalBinding& binding : runtime->bindings) {
        if (!binding.used || !binding.installed) continue;
        const bool ripAfterInt3 = instructionPointer == binding.address + 1;
        const bool addressMatches = exceptionAddress == binding.address || instructionPointer == binding.address || ripAfterInt3;
        if (!addressMatches) continue;
        const uint64_t threadId = static_cast<uint64_t>(GetCurrentThreadId());
        const uint64_t stopGeneration = runtime->stopGenerationCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
        runtime->trapThreadId.store(threadId, std::memory_order_release);
        runtime->trapInstructionPointer.store(instructionPointer, std::memory_order_release);
        runtime->trapAddress.store(binding.address, std::memory_order_release);
        runtime->trapBindingId.store(binding.bindingId, std::memory_order_release);
        runtime->trapStopGeneration.store(stopGeneration, std::memory_order_release);
        captureWindowsContext(*pointers->ContextRecord, processId, runtime->runtimeId, threadId,
                              binding.sessionGeneration, stopGeneration, runtime->trapContext);
        runtime->trapObserved.store(true, std::memory_order_release);
        SetEvent(runtime->trapEvent);
        WaitForSingleObject(runtime->resumeEvent, INFINITE);
        const uint32_t resumeMode = runtime->resumeMode.load(std::memory_order_acquire);
        if (resumeMode == kResumeModeInternalSingleStep) {
            if (runtime->pendingThreadId.load(std::memory_order_acquire) != threadId ||
                runtime->pendingBindingId.load(std::memory_order_acquire) != binding.bindingId ||
                runtime->pendingAddress.load(std::memory_order_acquire) != binding.address) {
                runtime->singleStepFailed.store(true, std::memory_order_release);
                runtime->singleStepPending.store(false, std::memory_order_release);
                ResetEvent(runtime->resumeEvent);
                return EXCEPTION_CONTINUE_SEARCH;
            }
            // The borrowed CONTEXT is writable only here. The command thread
            // has already restored the original byte and recorded the pending
            // operation; this callback performs the exact RIP/TF mutation.
            pointers->ContextRecord->Rip = binding.address;
            pointers->ContextRecord->EFlags = static_cast<DWORD>(
                static_cast<uint64_t>(pointers->ContextRecord->EFlags) | kAmd64TrapFlag);
            ResetEvent(runtime->resumeEvent);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (resumeMode == kResumeModeRelease || resumeMode == kResumeModeCancel) {
            pointers->ContextRecord->Rip = binding.address;
            runtime->resumeMode.store(0, std::memory_order_release);
            ResetEvent(runtime->resumeEvent);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void installVectoredHandler() {
    AddVectoredExceptionHandler(1, debugVectoredHandler);
}
#else
void installVectoredHandler() {}
#endif

} // namespace

bool NativeAppDebugger::RegisterRuntime(NativeAppRuntimeContext& context,
                                         const ExecutableMemoryBlock& mapping,
                                         const NativeElfImage& image,
                                         bool gateExecution,
                                         std::string& error) {
    error.clear();
    std::call_once(g_handlerOnce, installVectoredHandler);
    std::lock_guard<std::mutex> lock(g_mutex);
    for (DebugRuntime& runtime : g_runtimes) {
        if (runtime.active.load(std::memory_order_acquire)) continue;
        runtime.active.store(false, std::memory_order_release);
        runtime.gateOpen.store(false, std::memory_order_release);
        runtime.cancelRequested.store(false, std::memory_order_release);
        runtime.trapObserved.store(false, std::memory_order_release);
        runtime.resumeMode.store(0, std::memory_order_release);
        runtime.runtimeId = 0;
        runtime.processId = 0;
        runtime.imageBase = 0;
        runtime.imageEnd = 0;
        runtime.mapping = ExecutableMemoryBlock();
        runtime.segmentCount = 0;
        runtime.nextBindingId = 1;
        for (DebugSegment& segment : runtime.segments) segment = DebugSegment();
        for (PhysicalBinding& binding : runtime.bindings) binding = PhysicalBinding();
        runtime.trapThreadId.store(0, std::memory_order_release);
        runtime.trapInstructionPointer.store(0, std::memory_order_release);
        runtime.trapAddress.store(0, std::memory_order_release);
        runtime.trapBindingId.store(0, std::memory_order_release);
        runtime.trapStopGeneration.store(0, std::memory_order_release);
        runtime.stopGenerationCounter.store(0, std::memory_order_release);
        runtime.singleStepObserved.store(false, std::memory_order_release);
        runtime.singleStepFailed.store(false, std::memory_order_release);
        runtime.singleStepPending.store(false, std::memory_order_release);
        runtime.pendingSessionGeneration.store(0, std::memory_order_release);
        runtime.pendingThreadId.store(0, std::memory_order_release);
        runtime.pendingStopGeneration.store(0, std::memory_order_release);
        runtime.pendingBindingId.store(0, std::memory_order_release);
        runtime.pendingAddress.store(0, std::memory_order_release);
        runtime.pendingReinstall.store(0, std::memory_order_release);
        runtime.pendingRflagsBeforeStep.store(0, std::memory_order_release);
        runtime.pendingRflagsWithTrapFlag.store(0, std::memory_order_release);
        runtime.singleStepRflagsAfterClear.store(0, std::memory_order_release);
        runtime.trapContext = gx_development_debug_register_context{};
        runtime.singleStepContext = gx_development_debug_register_context{};
        runtime.runtimeId = context.runtimeId;
        runtime.processId = context.processId;
        if (image.preferredBaseAddress == 0 || image.imageSize == 0 ||
            image.imageSize > std::numeric_limits<uint64_t>::max() - image.preferredBaseAddress) {
            error = "Native ELF image range is invalid";
            return false;
        }
        runtime.imageBase = image.preferredBaseAddress;
        runtime.imageEnd = image.preferredBaseAddress + image.imageSize;
        runtime.mapping = mapping;
        for (const NativeElfSegment& segment : image.loadedSegments) {
            if ((segment.flags & kPfX) == 0) continue;
            if (runtime.segmentCount >= sizeof(runtime.segments) / sizeof(runtime.segments[0])) {
                error = "executable segment table limit exceeded";
                return false;
            }
            if (segment.memorySize == 0 || segment.virtualAddress > std::numeric_limits<uint64_t>::max() - segment.memorySize) {
                error = "Native ELF executable segment range is invalid";
                return false;
            }
            DebugSegment& target = runtime.segments[runtime.segmentCount++];
            target.start = segment.virtualAddress;
            target.end = segment.virtualAddress + segment.memorySize;
            target.flags = segment.flags;
        }
        if (runtime.segmentCount == 0) {
            error = "Native ELF image has no executable segment";
            return false;
        }
#ifdef _WIN32
        runtime.gateEvent = CreateEventA(nullptr, TRUE, gateExecution ? FALSE : TRUE, nullptr);
        runtime.trapEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        runtime.resumeEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (!runtime.gateEvent || !runtime.trapEvent || !runtime.resumeEvent) {
            if (runtime.gateEvent) CloseHandle(runtime.gateEvent);
            if (runtime.trapEvent) CloseHandle(runtime.trapEvent);
            if (runtime.resumeEvent) CloseHandle(runtime.resumeEvent);
            error = "debug synchronization event creation failed";
            return false;
        }
#endif
        runtime.gateOpen.store(!gateExecution, std::memory_order_release);
        runtime.active.store(true, std::memory_order_release);
        Logger::write(LogLevel::Info, "[NativeAppDebugger] runtime registered runtimeId=" + std::to_string(context.runtimeId) +
            " processId=" + std::to_string(context.processId) + " gate=" + (gateExecution ? "closed" : "open"));
        return true;
    }
    error = "debug runtime table exhausted";
    return false;
}

bool NativeAppDebugger::WaitForExecutionGate(uint64_t runtimeId) {
    DebugRuntime* runtime = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (DebugRuntime& candidate : g_runtimes) if (candidate.active.load() && candidate.runtimeId == runtimeId) { runtime = &candidate; break; }
    }
    if (!runtime) {
        Logger::write(LogLevel::Warn, "[NativeAppDebugger] execution gate wait rejected: runtime not found runtimeId=" + std::to_string(runtimeId));
        return false;
    }
#ifdef _WIN32
    WaitForSingleObject(runtime->gateEvent, INFINITE);
#endif
    const bool open = runtime->gateOpen.load(std::memory_order_acquire);
    const bool cancelled = runtime->cancelRequested.load(std::memory_order_acquire);
    Logger::write(LogLevel::Info, "[NativeAppDebugger] execution gate released runtimeId=" + std::to_string(runtimeId) +
        " open=" + (open ? "true" : "false") + " cancelled=" + (cancelled ? "true" : "false"));
    return open && !cancelled;
}

void NativeAppDebugger::UnregisterRuntime(uint64_t runtimeId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (DebugRuntime& runtime : g_runtimes) {
        if (!runtime.active.load(std::memory_order_acquire) || runtime.runtimeId != runtimeId) continue;
        std::string restoreError;
        if (!restoreAllLocked(runtime, restoreError))
            Logger::write(LogLevel::Warn, "[NativeAppDebugger] teardown restore failed: " + restoreError);
#ifdef _WIN32
        if (runtime.gateEvent) CloseHandle(runtime.gateEvent);
        if (runtime.trapEvent) CloseHandle(runtime.trapEvent);
        if (runtime.resumeEvent) CloseHandle(runtime.resumeEvent);
        runtime.gateEvent = nullptr;
        runtime.trapEvent = nullptr;
        runtime.resumeEvent = nullptr;
#endif
        runtime.active.store(false, std::memory_order_release);
    }
}

gx_result NativeAppDebugger::Command(const gx_development_debug_request& request,
                                     const std::string& expectedArtifactSha256,
                                     gx_development_debug_snapshot* snapshot) {
    if (!snapshot || request.size < sizeof(gx_development_debug_request) || request.version != GX_DEVELOPMENT_DEBUG_API_VERSION ||
        request.handle == 0 || request.sessionGeneration == 0 || request.processId == 0 || request.nativeRuntimeId == 0) return GX_ERROR_INVALID_ARGUMENT;
    clearSnapshot(snapshot);
    if (expectedArtifactSha256.empty() || !request.artifactSha256 || expectedArtifactSha256 != request.artifactSha256) {
        Logger::write(LogLevel::Warn, "[NativeAppDebugger] artifact identity mismatch expected=" + expectedArtifactSha256 +
            " requested=" + (request.artifactSha256 ? request.artifactSha256 : "<null>"));
        setError(snapshot, "artifact identity mismatch");
        return GX_ERROR_FAILED;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    DebugRuntime* runtime = findRuntimeLocked(request.nativeRuntimeId, request.processId);
    if (!runtime) { setError(snapshot, "target runtime is not registered"); return GX_ERROR_FAILED; }
    snapshot->processId = runtime->processId;
    snapshot->nativeRuntimeId = runtime->runtimeId;
    switch (request.command) {
    case GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT: {
        if (request.targetAddress == 0 || request.breakpointId == 0) { setError(snapshot, "breakpoint identity is incomplete"); return GX_ERROR_INVALID_ARGUMENT; }
        PhysicalBinding* existing = nullptr;
        for (PhysicalBinding& binding : runtime->bindings) {
            if (binding.used && binding.sessionGeneration == request.sessionGeneration && binding.address == request.targetAddress) { existing = &binding; break; }
        }
        if (existing) {
            if (existing->ownerCount >= kMaxLogicalOwners) { setError(snapshot, "logical breakpoint owner limit exceeded"); return GX_ERROR_FAILED; }
            for (uint32_t i = 0; i < existing->ownerCount; ++i) if (existing->owners[i] == request.breakpointId) { snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_BOUND; snapshot->bindingId = existing->bindingId; snapshot->originalByte = existing->originalByte; snapshot->installedByte = existing->installedByte; snapshot->originalByteValid = 1; snapshot->bindingInstalled = 1; return GX_OK; }
            existing->owners[existing->ownerCount++] = request.breakpointId;
            snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_BOUND;
            snapshot->bindingId = existing->bindingId;
            snapshot->originalByte = existing->originalByte;
            snapshot->installedByte = existing->installedByte;
            snapshot->originalByteValid = 1;
            snapshot->bindingInstalled = 1;
            snapshot->bindingCount = existing->ownerCount;
            return GX_OK;
        }
        DebugSegment* segment = nullptr;
        if (!executableAddress(*runtime, request.targetAddress, &segment)) { setError(snapshot, "breakpoint address is outside an executable segment"); return GX_ERROR_FAILED; }
        if (runtime->bindings[kMaxPhysicalBindings - 1].used) { setError(snapshot, "physical breakpoint table exhausted"); return GX_ERROR_FAILED; }
        PhysicalBinding* binding = nullptr;
        for (PhysicalBinding& candidate : runtime->bindings) if (!candidate.used) { binding = &candidate; break; }
        const size_t offset = static_cast<size_t>(request.targetAddress - runtime->imageBase);
        volatile uint8_t* byte = reinterpret_cast<volatile uint8_t*>(static_cast<char*>(runtime->mapping.base) + offset);
        const uint8_t original = *byte;
        if (original == 0xCC) { setError(snapshot, "instruction already contains INT3"); return GX_ERROR_FAILED; }
        std::string error;
        if (!writeCodeByte(*runtime, request.targetAddress, 0xCC, segment->flags, error)) { setError(snapshot, error.c_str()); return GX_ERROR_FAILED; }
        const uint8_t installed = *byte;
        binding->used = true;
        binding->bindingId = runtime->nextBindingId++;
        binding->sessionGeneration = request.sessionGeneration;
        binding->address = request.targetAddress;
        binding->originalByte = original;
        binding->installed = true;
        binding->ownerCount = 1;
        binding->owners[0] = request.breakpointId;
        snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_BOUND;
        snapshot->bindingId = binding->bindingId;
        snapshot->originalByte = original;
        snapshot->installedByte = 0xCC;
        snapshot->originalByteValid = 1;
        snapshot->bindingInstalled = 1;
        snapshot->bindingCount = 1;
        Logger::write(LogLevel::Info, "[NativeAppDebugger] breakpoint bound bindingId=" + std::to_string(binding->bindingId) +
            " address=0x" + [&request]() { std::ostringstream value; value << std::hex << request.targetAddress; return value.str(); }() +
            " original=0x" + [&original]() { std::ostringstream value; value << std::hex << static_cast<uint32_t>(original); return value.str(); }() +
            " installed=0x" + [&installed]() { std::ostringstream value; value << std::hex << static_cast<uint32_t>(installed); return value.str(); }() +
            " imageBase=0x" + [&runtime]() { std::ostringstream value; value << std::hex << runtime->imageBase; return value.str(); }() +
            " mappedBase=0x" + [&runtime]() { std::ostringstream value; value << std::hex << reinterpret_cast<uint64_t>(runtime->mapping.base); return value.str(); }());
        return GX_OK;
    }
    case GX_DEVELOPMENT_DEBUG_RELEASE_EXECUTION:
        runtime->gateOpen.store(true, std::memory_order_release);
#ifdef _WIN32
        SetEvent(runtime->gateEvent);
#endif
        snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_READY;
        Logger::write(LogLevel::Info, "[NativeAppDebugger] execution release accepted runtimeId=" + std::to_string(runtime->runtimeId));
        return GX_OK;
    case GX_DEVELOPMENT_DEBUG_POLL:
        if (runtime->singleStepFailed.load(std::memory_order_acquire)) {
            setError(snapshot, "internal single-step completed but breakpoint reinstall failed");
            return GX_ERROR_FAILED;
        } else if (runtime->singleStepObserved.exchange(false, std::memory_order_acq_rel)) {
            snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_TRAP;
            snapshot->trapKind = GX_DEVELOPMENT_DEBUG_TRAP_SINGLE_STEP;
            snapshot->threadId = runtime->singleStepContext.threadId;
            snapshot->instructionPointer = runtime->singleStepContext.rip;
            snapshot->targetAddress = runtime->pendingAddress.load(std::memory_order_acquire);
            snapshot->bindingId = runtime->pendingBindingId.load(std::memory_order_acquire);
            snapshot->context = runtime->singleStepContext;
            snapshot->rflagsBeforeStep = runtime->pendingRflagsBeforeStep.load(std::memory_order_acquire);
            snapshot->rflagsWithTrapFlag = runtime->pendingRflagsWithTrapFlag.load(std::memory_order_acquire);
            snapshot->rflagsAfterTrapFlagClear = runtime->singleStepRflagsAfterClear.load(std::memory_order_acquire);
            Logger::write(LogLevel::Info, "[NativeAppDebugger] internal single-step observed runtimeId=" +
                std::to_string(runtime->runtimeId) + " bindingId=" + std::to_string(snapshot->bindingId));
        } else if (runtime->singleStepPending.load(std::memory_order_acquire)) {
            snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_SINGLE_STEP_PENDING;
            snapshot->threadId = runtime->pendingThreadId.load(std::memory_order_acquire);
            snapshot->targetAddress = runtime->pendingAddress.load(std::memory_order_acquire);
            snapshot->bindingId = runtime->pendingBindingId.load(std::memory_order_acquire);
        } else if (runtime->trapObserved.load(std::memory_order_acquire)) {
            snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_TRAP;
            snapshot->trapKind = GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT;
            snapshot->threadId = runtime->trapThreadId.load(std::memory_order_acquire);
            snapshot->instructionPointer = runtime->trapInstructionPointer.load(std::memory_order_acquire);
            snapshot->targetAddress = runtime->trapAddress.load(std::memory_order_acquire);
            snapshot->bindingId = runtime->trapBindingId.load(std::memory_order_acquire);
            snapshot->context = runtime->trapContext;
            for (const PhysicalBinding& binding : runtime->bindings) if (binding.used && binding.bindingId == snapshot->bindingId) { snapshot->originalByte = binding.originalByte; snapshot->installedByte = binding.installedByte; snapshot->originalByteValid = 1; break; }
            Logger::write(LogLevel::Info, "[NativeAppDebugger] breakpoint trap observed runtimeId=" + std::to_string(runtime->runtimeId) +
                " bindingId=" + std::to_string(snapshot->bindingId));
        } else snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_READY;
        return GX_OK;
    case GX_DEVELOPMENT_DEBUG_CONTINUE_BREAKPOINT:
        {
        if (request.breakpointId == 0 || request.targetAddress == 0 || request.threadId == 0 || request.stopGeneration == 0) {
            setError(snapshot, "stopped breakpoint identity is incomplete");
            return GX_ERROR_INVALID_ARGUMENT;
        }
        if (!runtime->trapObserved.load(std::memory_order_acquire) ||
            runtime->singleStepPending.load(std::memory_order_acquire) ||
            runtime->trapThreadId.load(std::memory_order_acquire) != request.threadId ||
            runtime->trapStopGeneration.load(std::memory_order_acquire) != request.stopGeneration ||
            runtime->trapAddress.load(std::memory_order_acquire) != request.targetAddress) {
            setError(snapshot, "stale or mismatched stopped breakpoint context");
            return GX_ERROR_FAILED;
        }
        const uint64_t bindingId = runtime->trapBindingId.load(std::memory_order_acquire);
        PhysicalBinding* binding = nullptr;
        for (PhysicalBinding& candidate : runtime->bindings) {
            if (!candidate.used || !candidate.installed || candidate.bindingId != bindingId ||
                candidate.sessionGeneration != request.sessionGeneration || candidate.address != request.targetAddress) continue;
            for (uint32_t owner = 0; owner < candidate.ownerCount; ++owner)
                if (candidate.owners[owner] == request.breakpointId) { binding = &candidate; break; }
            if (binding) break;
        }
        if (!binding) {
            setError(snapshot, "stopped breakpoint is not owned by this session");
            return GX_ERROR_FAILED;
        }
        std::string restoreError;
        if (!restoreForInternalSingleStep(*runtime, *binding, restoreError)) {
            setError(snapshot, restoreError.empty() ? "original breakpoint byte restoration failed" : restoreError.c_str());
            return GX_ERROR_FAILED;
        }
        runtime->trapObserved.store(false, std::memory_order_release);
        runtime->singleStepObserved.store(false, std::memory_order_release);
        runtime->singleStepFailed.store(false, std::memory_order_release);
        runtime->pendingSessionGeneration.store(request.sessionGeneration, std::memory_order_release);
        runtime->pendingThreadId.store(request.threadId, std::memory_order_release);
        runtime->pendingStopGeneration.store(request.stopGeneration, std::memory_order_release);
        runtime->pendingBindingId.store(bindingId, std::memory_order_release);
        runtime->pendingAddress.store(request.targetAddress, std::memory_order_release);
        runtime->pendingReinstall.store((request.flags & GX_DEVELOPMENT_DEBUG_FLAG_REINSTALL_BREAKPOINT) != 0 ? 1u : 0u,
                                        std::memory_order_release);
        const uint64_t rflagsBeforeStep = runtime->trapContext.rflags;
        runtime->pendingRflagsBeforeStep.store(rflagsBeforeStep, std::memory_order_release);
        runtime->pendingRflagsWithTrapFlag.store(rflagsBeforeStep | kAmd64TrapFlag, std::memory_order_release);
        runtime->singleStepPending.store(true, std::memory_order_release);
        runtime->resumeMode.store(kResumeModeInternalSingleStep, std::memory_order_release);
#ifdef _WIN32
        SetEvent(runtime->resumeEvent);
#endif
        snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_SINGLE_STEP_PENDING;
        snapshot->bindingId = bindingId;
        snapshot->targetAddress = request.targetAddress;
        snapshot->threadId = request.threadId;
        Logger::write(LogLevel::Info, "[NativeAppDebugger] breakpoint continuation accepted runtimeId=" +
            std::to_string(runtime->runtimeId) + " bindingId=" + std::to_string(bindingId) +
            " stopGeneration=" + std::to_string(request.stopGeneration) +
            " reinstall=" + ((request.flags & GX_DEVELOPMENT_DEBUG_FLAG_REINSTALL_BREAKPOINT) ? "true" : "false"));
        return GX_OK;
        }
    case GX_DEVELOPMENT_DEBUG_RESTORE_ALL:
        {
        std::string restoreError;
        const bool restored = restoreAllLocked(*runtime, restoreError);
        runtime->resumeMode.store(kResumeModeRelease, std::memory_order_release);
#ifdef _WIN32
        SetEvent(runtime->resumeEvent);
#endif
        if (!restored) {
            setError(snapshot, restoreError.empty() ? "breakpoint restoration failed" : restoreError.c_str());
            return GX_ERROR_FAILED;
        }
        snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_RESTORED;
        return GX_OK;
        }
    case GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION:
        {
        std::string restoreError;
        const bool restored = restoreAllLocked(*runtime, restoreError);
        runtime->cancelRequested.store(true, std::memory_order_release);
        runtime->gateOpen.store(true, std::memory_order_release);
        runtime->pendingReinstall.store(0, std::memory_order_release);
        runtime->resumeMode.store(kResumeModeCancel, std::memory_order_release);
#ifdef _WIN32
        SetEvent(runtime->gateEvent);
        SetEvent(runtime->resumeEvent);
#endif
        if (!restored) {
            setError(snapshot, restoreError.empty() ? "breakpoint cancellation restoration failed" : restoreError.c_str());
            return GX_ERROR_FAILED;
        }
        snapshot->status = GX_DEVELOPMENT_DEBUG_STATUS_RESTORED;
        return GX_OK;
        }
    default:
        setError(snapshot, "unknown debug command");
        return GX_ERROR_INVALID_ARGUMENT;
    }
}

void NativeAppDebugger::CancelProcess(uint64_t processId) {
    if (processId == 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    DebugRuntime* runtime = findRuntimeByProcess(processId);
    if (!runtime) return;
    std::string restoreError;
    if (!restoreAllLocked(*runtime, restoreError))
        Logger::write(LogLevel::Warn, "[NativeAppDebugger] process cancellation restore failed: " + restoreError);
    runtime->cancelRequested.store(true, std::memory_order_release);
    runtime->gateOpen.store(true, std::memory_order_release);
    runtime->pendingReinstall.store(0, std::memory_order_release);
    runtime->resumeMode.store(kResumeModeCancel, std::memory_order_release);
#ifdef _WIN32
    SetEvent(runtime->gateEvent);
    SetEvent(runtime->resumeEvent);
#endif
}

} // namespace apps
} // namespace gxos
