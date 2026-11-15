//
// Bounded cooperative AMD64 execution contexts for trusted kernel work.
//
// These contexts are not application processes and do not provide isolation.
// Native ELF continues to use its separate synchronous managed-execution path.
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace native_scheduler {

enum class State : uint8_t {
    Empty = 0,
    Ready,
    Running,
    Completed,
    Faulted
};

enum class Result : uint8_t {
    Ok = 0,
    InvalidArgument,
    NotInitialized,
    CapacityExhausted,
    AllocationFailed,
    InvalidHandle,
    InvalidState,
    UnsafeContext,
    Unsupported
};

enum class OwnerKind : uint8_t {
    TrustedKernelSystem = 0
};

enum class FaultKind : uint8_t {
    None = 0,
    StackGuardCorruption,
    SavedContextInvalid,
    SchedulerInvariant
};

// runtimeId identifies a scheduler context, not a process or Native ELF
// managed-execution record. Slot and generation reject stale references after
// a completed slot is reused.
struct Handle {
    uint64_t runtimeId;
    uint32_t slot;
    uint32_t generation;
};

typedef int32_t (*Entry)(void* argument);

struct Snapshot {
    Handle handle;
    State state;
    OwnerKind ownerKind;
    FaultKind faultKind;
    uint32_t configuredStackBytes;
    uint64_t dispatchCount;
    int32_t exitCode;
    bool stackAllocated;
    bool hasApplicationIdentity;
    bool hasSessionAttribution;
};

class Runtime {
public:
    enum : uint32_t {
        kMaximumContexts = 8u,
        kStackBytes = 64u * 1024u
    };

    Runtime();

    Result initialize();
    Result createKernelTask(Entry entry, void* argument, Handle* output);
    Result dispatchOne(bool* dispatched);
    Result yieldCurrent();

    bool query(Handle handle, Snapshot* output) const;
    bool stacksAreSeparate(Handle first, Handle second) const;

    // Termination is deliberately unsupported. A cooperative context can
    // only finish by returning from its entry function.
    Result requestTermination(Handle handle) const;

#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
    void failNextAllocationForTest();
    bool corruptStackGuardForTest(Handle handle);
#endif

    // Called only by the AMD64 entry trampoline. It never returns.
    void enterFromTrampoline(void* context);

private:
    struct Context {
        uint8_t* allocation;
        uintptr_t stackLow;
        uintptr_t stackTop;
        uint64_t savedStackPointer;
        uint64_t runtimeId;
        uint64_t dispatchCount;
        uint32_t generation;
        uint32_t configuredStackBytes;
        State state;
        FaultKind faultKind;
        Entry entry;
        void* argument;
        int32_t exitCode;
        bool retired;
    };

    int32_t selectReusableSlot() const;
    bool validHandle(Handle handle) const;
    bool validSavedStack(const Context& context) const;
    bool intactStackGuards(const Context& context) const;
    void releaseCompletedStack(Context& context);
    void finishCurrent(Context& context, int32_t exitCode);

    Context m_contexts[kMaximumContexts];
    uint64_t m_rootStackPointer;
    uint64_t m_nextRuntimeId;
    uint32_t m_nextSlot;
    uint32_t m_currentSlot;
    bool m_initialized;
    bool m_dispatching;
#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
    bool m_failNextAllocation;
#endif
};

Runtime& runtime();
const char* resultName(Result result);
const char* stateName(State state);

#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE) && defined(ARCH_AMD64)
bool runSelfTest();
#endif

} // namespace native_scheduler
} // namespace kernel
