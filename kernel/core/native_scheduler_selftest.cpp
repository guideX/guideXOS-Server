//
// Opt-in freestanding proof for the production cooperative AMD64 scheduler.
// These tasks run through the same stack creation, switch, yield, completion,
// and reaping code used by kernel services.
//

#include "include/kernel/arch.h"
#include "include/kernel/native_scheduler.h"
#include "include/kernel/serial_debug.h"

#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE) && defined(ARCH_AMD64)

extern "C" uint32_t gxos_scheduler_selftest_probe(void (*yieldCallback)());
extern "C" uint32_t gxos_scheduler_selftest_xstate_probe(void (*yieldCallback)());
extern "C" uint32_t gxos_scheduler_selftest_stack_abi_probe(void (*yieldCallback)());

namespace kernel {
namespace native_scheduler {
namespace {

struct YieldProof {
    uint64_t stackLocalAddress[2];
    uint64_t localState[2];
    uint32_t runOrder[8];
    uint32_t runCount;
    uint32_t failures;
};

struct YieldTaskArgument {
    YieldProof* proof;
    uint32_t index;
};

struct CycleArgument {
    uint32_t yields;
};

static YieldProof s_yieldProof = {};
static YieldTaskArgument s_yieldArguments[2] = {};
static CycleArgument s_cycleArgument = {};
static uint32_t s_registerMask = 0u;
static uint32_t s_xstateMask = 0u;
static uint32_t s_stackAbiMask = 0u;

static void register_probe_yield()
{
    if (runtime().yieldCurrent() != Result::Ok) ++s_yieldProof.failures;
}

static int32_t yielding_proof_task(void* opaque)
{
    YieldTaskArgument* argument = static_cast<YieldTaskArgument*>(opaque);
    if (!argument || !argument->proof || argument->index >= 2u) return -1;

    const uint32_t index = argument->index;
    const uint64_t seed = 0xA550000000000000ull + index * 0x1000ull;
    volatile uint64_t localState = seed;
    argument->proof->stackLocalAddress[index] =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&localState));

    for (uint32_t phase = 0u; phase < 4u; ++phase) {
        if (localState != seed + phase) ++argument->proof->failures;
        if (argument->proof->runCount < 8u) {
            argument->proof->runOrder[argument->proof->runCount++] = index;
        } else {
            ++argument->proof->failures;
        }
        argument->proof->localState[index] = localState;

        if (phase != 3u) {
            localState = seed + phase + 1u;
            if (runtime().yieldCurrent() != Result::Ok) {
                ++argument->proof->failures;
                return -2;
            }
        }
    }
    return static_cast<int32_t>(100u + index);
}

static int32_t capacity_noop_task(void*)
{
    return 0;
}

static int32_t register_probe_task(void*)
{
    s_registerMask = gxos_scheduler_selftest_probe(register_probe_yield);
    return s_registerMask == 0xFFu ? 9 : -9;
}

static int32_t xstate_probe_task(void*)
{
    s_xstateMask = gxos_scheduler_selftest_xstate_probe(register_probe_yield);
    const uint32_t expected = (runtime().enabledXstateMask() & (1ull << 2u))
        ? 0x7FFFFFu : 0x1FFFu;
    return s_xstateMask == expected ? 10 : -10;
}

static int32_t stack_abi_probe_task(void*)
{
    s_stackAbiMask = gxos_scheduler_selftest_stack_abi_probe(register_probe_yield);
    return s_stackAbiMask == 0xFu ? 11 : -11;
}

static int32_t repeated_cycle_task(void* opaque)
{
    CycleArgument* argument = static_cast<CycleArgument*>(opaque);
    if (!argument) return -1;
    volatile uint32_t preserved = 0x35u;
    for (uint32_t i = 0u; i < 2u; ++i) {
        ++argument->yields;
        if (runtime().yieldCurrent() != Result::Ok) return -2;
        if (preserved != 0x35u) return -3;
    }
    return 7;
}

static bool is_state(Handle handle, State expected, bool expectStack)
{
    Snapshot snapshot{};
    return runtime().query(handle, &snapshot) && snapshot.state == expected &&
           snapshot.stackAllocated == expectStack;
}

static bool run_until_complete(Handle handle, uint32_t maximumDispatches)
{
    for (uint32_t i = 0u; i < maximumDispatches; ++i) {
        if (is_state(handle, State::Completed, false)) return true;
        bool dispatched = false;
        if (runtime().dispatchOne(&dispatched) != Result::Ok || !dispatched)
            return false;
    }
    return is_state(handle, State::Completed, false);
}

} // namespace

bool runSelfTest()
{
    Runtime& scheduler = runtime();
    bool passed = true;
#define GXOS_SELFTEST_CHECKPOINT(name) do { if (!passed) { \
    serial::puts("[NATIVE-SCHEDULER-SELFTEST] failure_stage="); \
    serial::puts(name); serial::puts("\n"); return false; } } while (0)
    s_yieldProof = {};
    s_yieldArguments[0] = { &s_yieldProof, 0u };
    s_yieldArguments[1] = { &s_yieldProof, 1u };

    Handle first{};
    Handle second{};
    const Result firstCreate = scheduler.createSelfTestTask(
        yielding_proof_task, &s_yieldArguments[0], &first);
    serial::puts("[NATIVE-SCHEDULER-SELFTEST] create_first=");
    serial::puts(resultName(firstCreate));
    serial::puts("\n");
    passed = passed && firstCreate == Result::Ok;
    GXOS_SELFTEST_CHECKPOINT("first-context-creation");
    const Result secondCreate = scheduler.createSelfTestTask(
        yielding_proof_task, &s_yieldArguments[1], &second);
    serial::puts("[NATIVE-SCHEDULER-SELFTEST] create_second=");
    serial::puts(resultName(secondCreate));
    serial::puts("\n");
    passed = passed && secondCreate == Result::Ok;
    GXOS_SELFTEST_CHECKPOINT("second-context-creation");
    passed = passed && first.slot != second.slot && first.runtimeId != second.runtimeId;
    GXOS_SELFTEST_CHECKPOINT("independent-handles");
    passed = passed && scheduler.stacksAreSeparate(first, second);
    GXOS_SELFTEST_CHECKPOINT("independent-stacks");
    passed = passed && is_state(first, State::Ready, true) &&
        is_state(second, State::Ready, true);
    GXOS_SELFTEST_CHECKPOINT("ready-state");
    passed = passed && scheduler.requestTermination(first) == Result::Unsupported;
    passed = passed && is_state(first, State::Ready, true);
    GXOS_SELFTEST_CHECKPOINT("unsupported-termination");

    Handle invalid{ 0u, Runtime::kMaximumContexts, 0u };
    Snapshot invalidSnapshot{};
    passed = passed && !scheduler.query(invalid, &invalidSnapshot);
    GXOS_SELFTEST_CHECKPOINT("invalid-handle-query");
    passed = passed && scheduler.requestTermination(invalid) == Result::InvalidHandle;
    GXOS_SELFTEST_CHECKPOINT("invalid-handle-termination");
    passed = passed && scheduler.yieldCurrent() == Result::InvalidState;
    GXOS_SELFTEST_CHECKPOINT("setup-and-contracts");

    for (uint32_t i = 0u; i < 16u &&
         (!is_state(first, State::Completed, false) ||
          !is_state(second, State::Completed, false)); ++i) {
        bool dispatched = false;
        if (scheduler.dispatchOne(&dispatched) != Result::Ok || !dispatched) {
            passed = false;
            break;
        }
    }

    Snapshot firstSnapshot{};
    Snapshot secondSnapshot{};
    passed = passed && scheduler.query(first, &firstSnapshot) &&
        scheduler.query(second, &secondSnapshot);
    passed = passed && firstSnapshot.state == State::Completed &&
        secondSnapshot.state == State::Completed &&
        firstSnapshot.exitCode == 100 && secondSnapshot.exitCode == 101 &&
        firstSnapshot.dispatchCount == 4u && secondSnapshot.dispatchCount == 4u &&
        !firstSnapshot.stackAllocated && !secondSnapshot.stackAllocated;
    passed = passed && s_yieldProof.runCount == 8u &&
        s_yieldProof.stackLocalAddress[0] != 0u &&
        s_yieldProof.stackLocalAddress[1] != 0u &&
        s_yieldProof.stackLocalAddress[0] != s_yieldProof.stackLocalAddress[1] &&
        s_yieldProof.failures == 0u;
    for (uint32_t i = 0u; i < 8u; ++i) {
        if (s_yieldProof.runOrder[i] != (i & 1u)) passed = false;
    }
    GXOS_SELFTEST_CHECKPOINT("round-robin-yield-resume-and-reap");

    Handle registerProbe{};
    Handle registerPartner{};
    s_registerMask = 0u;
    passed = passed && scheduler.createSelfTestTask(
        register_probe_task, nullptr, &registerProbe) == Result::Ok;
    passed = passed && scheduler.createSelfTestTask(
        capacity_noop_task, nullptr, &registerPartner) == Result::Ok;
    for (uint32_t i = 0u; i < 4u &&
         (!is_state(registerProbe, State::Completed, false) ||
          !is_state(registerPartner, State::Completed, false)); ++i) {
        bool dispatched = false;
        if (scheduler.dispatchOne(&dispatched) != Result::Ok || !dispatched) {
            passed = false;
            break;
        }
    }
    Snapshot registerSnapshot{};
    passed = passed && scheduler.query(registerProbe, &registerSnapshot) &&
        registerSnapshot.state == State::Completed && registerSnapshot.exitCode == 9 &&
        registerSnapshot.dispatchCount == 2u && s_registerMask == 0xFFu &&
        is_state(registerPartner, State::Completed, false);
    GXOS_SELFTEST_CHECKPOINT("nonvolatile-gpr-preservation");

    Handle xstateProbe{};
    Handle xstatePartner{};
    s_xstateMask = 0u;
    passed = passed && scheduler.createSelfTestTask(
        xstate_probe_task, nullptr, &xstateProbe) == Result::Ok;
    passed = passed && scheduler.createSelfTestTask(
        capacity_noop_task, nullptr, &xstatePartner) == Result::Ok;
    for (uint32_t i = 0u; i < 6u &&
         (!is_state(xstateProbe, State::Completed, false) ||
          !is_state(xstatePartner, State::Completed, false)); ++i) {
        bool dispatched = false;
        if (scheduler.dispatchOne(&dispatched) != Result::Ok || !dispatched) {
            passed = false;
            break;
        }
    }
    Snapshot xstateSnapshot{};
    const uint32_t expectedXstateMask =
        (scheduler.enabledXstateMask() & (1ull << 2u)) ? 0x7FFFFFu : 0x1FFFu;
    passed = passed && scheduler.query(xstateProbe, &xstateSnapshot) &&
        xstateSnapshot.state == State::Completed && xstateSnapshot.exitCode == 10 &&
        xstateSnapshot.dispatchCount == 2u && s_xstateMask == expectedXstateMask &&
        is_state(xstatePartner, State::Completed, false);
    GXOS_SELFTEST_CHECKPOINT("xmm-x87-mxcsr-and-enabled-avx-state-preservation");

    Handle stackAbi{};
    s_stackAbiMask = 0u;
    passed = passed && scheduler.createSelfTestTask(
        stack_abi_probe_task, nullptr, &stackAbi) == Result::Ok;
    passed = passed && run_until_complete(stackAbi, 2u);
    Snapshot stackAbiSnapshot{};
    passed = passed && scheduler.query(stackAbi, &stackAbiSnapshot) &&
        stackAbiSnapshot.state == State::Completed && stackAbiSnapshot.exitCode == 11 &&
        stackAbiSnapshot.dispatchCount == 2u && s_stackAbiMask == 0xFu;
    GXOS_SELFTEST_CHECKPOINT("stack-alignment-and-shadow-space-after-resume");

    scheduler.failNextAllocationForTest();
    Handle allocationFailure{};
    passed = passed && scheduler.createSelfTestTask(
        capacity_noop_task, nullptr, &allocationFailure) == Result::AllocationFailed;
    GXOS_SELFTEST_CHECKPOINT("allocation-failure");

    Handle capacityHandles[Runtime::kMaximumContexts] = {};
    for (uint32_t i = 0u; i < Runtime::kMaximumContexts; ++i) {
        if (scheduler.createSelfTestTask(capacity_noop_task, nullptr,
                                       &capacityHandles[i]) != Result::Ok) {
            passed = false;
            break;
        }
    }
    Handle overCapacity{};
    passed = passed && scheduler.createSelfTestTask(
        capacity_noop_task, nullptr, &overCapacity) == Result::CapacityExhausted;

    bool staleFirstRejected = false;
    for (uint32_t i = 0u; i < Runtime::kMaximumContexts; ++i) {
        if (capacityHandles[i].slot == first.slot) {
            staleFirstRejected = !scheduler.query(first, &invalidSnapshot) &&
                capacityHandles[i].generation != first.generation;
        }
    }
    passed = passed && staleFirstRejected;
    GXOS_SELFTEST_CHECKPOINT("capacity-and-generation");

    for (uint32_t i = 0u; i < Runtime::kMaximumContexts; ++i) {
        if (!run_until_complete(capacityHandles[i], 2u)) passed = false;
        Snapshot snapshot{};
        if (!scheduler.query(capacityHandles[i], &snapshot) ||
            snapshot.state != State::Completed || snapshot.exitCode != 0 ||
            snapshot.dispatchCount != 1u || snapshot.stackAllocated) {
            passed = false;
        }
    }
    GXOS_SELFTEST_CHECKPOINT("capacity-completion-and-reaping");

    Handle damagedStack{};
    passed = passed && scheduler.createSelfTestTask(
        capacity_noop_task, nullptr, &damagedStack) == Result::Ok;
    passed = passed && scheduler.corruptStackGuardForTest(damagedStack);
    bool damagedDispatched = true;
    const Result damagedResult = scheduler.dispatchOne(&damagedDispatched);
    Snapshot damagedSnapshot{};
    passed = passed && damagedResult == Result::InvalidState && !damagedDispatched &&
        scheduler.query(damagedStack, &damagedSnapshot) &&
        damagedSnapshot.state == State::Faulted &&
        damagedSnapshot.faultKind == FaultKind::StackGuardCorruption &&
        !damagedSnapshot.stackAllocated;
    GXOS_SELFTEST_CHECKPOINT("stack-guard-fault");

    for (uint32_t cycle = 0u; cycle < 3u; ++cycle) {
        s_cycleArgument.yields = 0u;
        Handle repeated{};
        if (scheduler.createSelfTestTask(
                repeated_cycle_task, &s_cycleArgument, &repeated) != Result::Ok ||
            !run_until_complete(repeated, 4u)) {
            passed = false;
            continue;
        }
        Snapshot snapshot{};
        if (!scheduler.query(repeated, &snapshot) ||
            snapshot.state != State::Completed || snapshot.exitCode != 7 ||
            snapshot.dispatchCount != 3u || snapshot.stackAllocated ||
            s_cycleArgument.yields != 2u) {
            passed = false;
        }
    }
    GXOS_SELFTEST_CHECKPOINT("repeated-cycles");

    Handle applicationCapacity[Runtime::kNativeElfTaskSlots] = {};
    bool applicationSlotSeen[Runtime::kMaximumContexts] = {};
    for (uint32_t i = 0u; i < Runtime::kNativeElfTaskSlots; ++i) {
        if (scheduler.createNativeElfTask(capacity_noop_task, nullptr,
                                          &applicationCapacity[i]) != Result::Ok) {
            passed = false;
            break;
        }
        if (applicationCapacity[i].slot == 0u ||
            applicationCapacity[i].slot >= Runtime::kMaximumContexts ||
            applicationSlotSeen[applicationCapacity[i].slot]) {
            passed = false;
        } else {
            applicationSlotSeen[applicationCapacity[i].slot] = true;
        }
        Snapshot applicationSnapshot{};
        if (!scheduler.query(applicationCapacity[i], &applicationSnapshot) ||
            applicationSnapshot.ownerKind != OwnerKind::NativeElfApplication ||
            applicationSnapshot.state != State::Ready ||
            !applicationSnapshot.stackAllocated) passed = false;
    }
    Handle excessApplication{};
    passed = passed && scheduler.createNativeElfTask(
        capacity_noop_task, nullptr, &excessApplication) == Result::CapacityExhausted;
    Handle reservedKernel{};
    passed = passed && scheduler.createKernelTask(
        capacity_noop_task, nullptr, &reservedKernel) == Result::Ok &&
        reservedKernel.slot == 0u;
    GXOS_SELFTEST_CHECKPOINT("reserved-system-capacity-and-application-slot-exhaustion");

    for (uint32_t i = 0u; i < 16u; ++i) {
        bool anyReady = false;
        for (uint32_t j = 0u; j < Runtime::kNativeElfTaskSlots; ++j) {
            Snapshot snapshot{};
            if (!scheduler.query(applicationCapacity[j], &snapshot)) {
                passed = false;
            } else if (snapshot.state == State::Ready ||
                       snapshot.state == State::Running) {
                anyReady = true;
            }
        }
        Snapshot systemSnapshot{};
        if (!scheduler.query(reservedKernel, &systemSnapshot)) passed = false;
        else if (systemSnapshot.state == State::Ready ||
                 systemSnapshot.state == State::Running) anyReady = true;
        if (!anyReady) break;
        bool dispatched = false;
        if (scheduler.dispatchOne(&dispatched) != Result::Ok || !dispatched) {
            passed = false;
            break;
        }
    }
    for (uint32_t i = 0u; i < Runtime::kNativeElfTaskSlots; ++i) {
        Snapshot snapshot{};
        if (!scheduler.query(applicationCapacity[i], &snapshot) ||
            snapshot.state != State::Completed || snapshot.exitCode != 0 ||
            snapshot.stackAllocated) passed = false;
    }
    Snapshot reservedKernelSnapshot{};
    passed = passed && scheduler.query(reservedKernel, &reservedKernelSnapshot) &&
        reservedKernelSnapshot.state == State::Completed &&
        !reservedKernelSnapshot.stackAllocated;
    GXOS_SELFTEST_CHECKPOINT("application-slot-recovery-and-root-reaping");

    Handle applicationReused[Runtime::kNativeElfTaskSlots] = {};
    bool reusedSlotSeen[Runtime::kMaximumContexts] = {};
    for (uint32_t i = 0u; i < Runtime::kNativeElfTaskSlots; ++i) {
        if (scheduler.createNativeElfTask(capacity_noop_task, nullptr,
                                          &applicationReused[i]) != Result::Ok) {
            passed = false;
            break;
        }
        const uint32_t slot = applicationReused[i].slot;
        if (slot == 0u || slot >= Runtime::kMaximumContexts || reusedSlotSeen[slot]) {
            passed = false;
            continue;
        }
        reusedSlotSeen[slot] = true;
        bool matchedPrior = false;
        for (uint32_t prior = 0u; prior < Runtime::kNativeElfTaskSlots; ++prior) {
            if (applicationCapacity[prior].slot != slot) continue;
            matchedPrior = true;
            Snapshot staleSnapshot{};
            if (applicationReused[i].generation == applicationCapacity[prior].generation ||
                applicationReused[i].runtimeId == applicationCapacity[prior].runtimeId ||
                scheduler.query(applicationCapacity[prior], &staleSnapshot)) passed = false;
            break;
        }
        if (!matchedPrior) passed = false;
    }
    for (uint32_t i = 0u; i < Runtime::kNativeElfTaskSlots; ++i) {
        Snapshot snapshot{};
        if (!scheduler.query(applicationReused[i], &snapshot) ||
            snapshot.state != State::Ready || !snapshot.stackAllocated) passed = false;
    }
    for (uint32_t i = 0u; i < 16u; ++i) {
        bool anyReady = false;
        for (uint32_t j = 0u; j < Runtime::kNativeElfTaskSlots; ++j) {
            Snapshot snapshot{};
            if (!scheduler.query(applicationReused[j], &snapshot)) passed = false;
            else if (snapshot.state == State::Ready || snapshot.state == State::Running)
                anyReady = true;
        }
        if (!anyReady) break;
        bool dispatched = false;
        if (scheduler.dispatchOne(&dispatched) != Result::Ok || !dispatched) {
            passed = false;
            break;
        }
    }
    for (uint32_t i = 0u; i < Runtime::kNativeElfTaskSlots; ++i) {
        Snapshot snapshot{};
        if (!scheduler.query(applicationReused[i], &snapshot) ||
            snapshot.state != State::Completed || snapshot.exitCode != 0 ||
            snapshot.stackAllocated) passed = false;
    }
    GXOS_SELFTEST_CHECKPOINT("application-slot-round-robin-generation-reuse");

    serial::puts("[NATIVE-SCHEDULER-SELFTEST] result=");
    serial::puts(passed ? "PASS" : "FAIL");
    serial::puts(" proof=independent-stacks round-robin yield-resume local-state gpr-preservation xmm-x87-mxcsr-avx-state stack-alignment shadow-space completion reaping generation reserved-system-capacity application-exhaustion allocation-failure unsupported-termination repeated-cycles\n");
#undef GXOS_SELFTEST_CHECKPOINT
    return passed;
}

} // namespace native_scheduler
} // namespace kernel

#endif
