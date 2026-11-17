//
// Bounded single-CPU AMD64 execution contexts for trusted kernel work and
// explicitly managed Native ELF applications. Eligibility is opt-in; these
// contexts do not provide process or address-space isolation.
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

struct PreemptionStats {
    uint64_t timerInterrupts;
    uint64_t preemptionDecisions;
    uint64_t preemptiveSwitches;
    uint64_t deferredPreemptions;
    uint64_t eligibilityRejections;
    uint64_t applicationResumptions;
};

enum class OwnerKind : uint8_t {
    TrustedKernelSystem = 0,
    NativeElfApplication
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
        kStackBytes = 64u * 1024u,
        kMaximumXstateBytes = 16u * 1024u,
        kKernelTaskSlots = 1u,
        kNativeElfTaskSlots = kMaximumContexts - kKernelTaskSlots
    };

    Runtime();

    Result initialize();
    Result createKernelTask(Entry entry, void* argument, Handle* output);
    Result createNativeElfTask(Entry entry, void* argument, Handle* output);
#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
    Result createSelfTestTask(Entry entry, void* argument, Handle* output);
#endif
    Result dispatchOne(bool* dispatched);
    Result yieldCurrent();

    // Timer preemption is restricted to an eligible Native ELF slot. The IRQ
    // dispatcher calls this only after acknowledging the PIC timer.
    void timerInterrupt(bool outermostInterrupt);

    // Host ABI services use these balanced single-CPU guards around shared
    // kernel state. Leaving the outermost guard may service a deferred tick.
    Result enterNonPreemptible();
    Result leaveNonPreemptible();
    Result suspendPreemptionForCooperativeYield();
    Result cancelPreemptionSuspendForCooperativeYield();
    Result setCurrentPreemptionEligible(bool eligible);

    bool query(Handle handle, Snapshot* output) const;
    bool stacksAreSeparate(Handle first, Handle second) const;
    bool executionStackBounds(Handle handle, uintptr_t* low, uintptr_t* top) const;
    bool isCurrent(Handle handle) const;
    uint64_t enabledXstateMask() const;
    uint32_t enabledXstateBytes() const;
    uint64_t trustedKernelDispatchCount() const;
    PreemptionStats preemptionStats() const;
    uint32_t preemptionDisableDepth() const;

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
        OwnerKind ownerKind;
        State state;
        FaultKind faultKind;
        Entry entry;
        void* argument;
        int32_t exitCode;
        bool retired;
        bool savedFromTimerInterrupt;
        bool preemptionEligible;
        uint32_t suspendedPreemptionDisableDepth;
        alignas(64) uint8_t xstate[kMaximumXstateBytes];
    };

    int32_t selectReusableSlot(bool nativeElfApplication,
                               bool allKernelSlots = false) const;
    Result createTask(Entry entry, void* argument, Handle* output,
                      bool nativeElfApplication, bool allKernelSlots = false);
    bool validHandle(Handle handle) const;
    bool validSavedStack(const Context& context) const;
    bool intactStackGuards(const Context& context) const;
    void releaseCompletedStack(Context& context);
    void finishCurrent(Context& context, int32_t exitCode);
    Result switchCurrentToRoot(bool timerInterrupt);
    Result serviceDeferredPreemption();

    Context m_contexts[kMaximumContexts];
    uint64_t m_rootStackPointer;
    alignas(64) uint8_t m_rootXstate[kMaximumXstateBytes];
    uint64_t m_nextRuntimeId;
    uint32_t m_nextSlot;
    uint32_t m_currentSlot;
    bool m_initialized;
    bool m_dispatching;
    bool m_transition;
    bool m_preemptionPending;
    uint32_t m_preemptionDisableDepth;
    PreemptionStats m_preemptionStats;
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
