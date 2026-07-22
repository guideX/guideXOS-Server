#include "include/kernel/native_thread_qemu_test.h"

#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)

#include "include/kernel/process.h"
#include "include/kernel/serial_debug.h"
#include "runtime/synchronization/guidexos_event.h"
#include "runtime/local_storage/guidexos_local_storage.h"
#include "runtime/thread/guidexos_native_stack_bounds.h"
#include "runtime/thread/guidexos_native_thread.h"

namespace kernel {
namespace native_thread_qemu_test {
namespace {

using gxos::runtime::Event;
using gxos::runtime::EventMode;
using gxos::runtime::EventStatus;
using gxos::runtime::ThreadCreateOptions;
using gxos::runtime::ThreadHandle;
using gxos::runtime::ThreadResult;
using gxos::runtime::WaitResult;
using gxos::runtime::WaitTimeout;

constexpr uint32_t kContextMagic = 0x4E544D47U;
constexpr uint32_t kContextVersion = 1U;
constexpr uintptr_t kSingleResult = 0x1234U;

struct WorkerContext {
    Event started;
    Event request;
    Event done;
    Event* request_source;
    uint32_t magic;
    uint32_t version;
    uint32_t calls;
    uint32_t result_field;
    uint32_t context_ok;
    uint32_t stack_ok;
    uint32_t abi_ok;
    uint32_t nonvolatile_ok;
    uint32_t waiting;
    WaitResult wait_result;
    uintptr_t result_value;
    bool waits_for_request;
    bool finite_wait;
    gxos::runtime::LocalStorageIndex stack_callback_index;
    bool stack_callback_value_set;
    kernel::process::NativeThreadTestSnapshot snapshot;

    WorkerContext(bool waits = false,
                  bool finite = false,
                  uintptr_t result = 0)
        : started(EventMode::ManualReset, false),
          request(EventMode::ManualReset, false),
          done(EventMode::ManualReset, false),
          request_source(nullptr),
          magic(kContextMagic),
          version(kContextVersion),
          calls(0),
          result_field(0),
          context_ok(0),
          stack_ok(0),
          abi_ok(0),
          nonvolatile_ok(0),
          waiting(0),
          wait_result(WaitResult::Invalid),
          result_value(result),
          waits_for_request(waits),
          finite_wait(finite),
          stack_callback_index{},
          stack_callback_value_set(false),
          snapshot{} {
    }
};

bool g_all_passed = true;
gxos::runtime::LocalStorageIndex g_stack_callback_index{};
bool g_stack_callback_bounds_valid = false;
uint32_t g_stack_callback_count = 0;

void stack_detach_callback(void*) {
    gxos::runtime::NativeStackBounds bounds{};
    g_stack_callback_bounds_valid =
        gxos::runtime::queryCurrentNativeStackBounds(&bounds) ==
            gxos::runtime::StackBoundsResult::Success &&
        bounds.low < bounds.high && bounds.current >= bounds.low &&
        bounds.current < bounds.high;
    ++g_stack_callback_count;
}

void status(const char* name, bool passed) {
    kernel::serial::puts("[native-thread-test] ");
    kernel::serial::puts(name);
    kernel::serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) {
        g_all_passed = false;
    }
}

void print_handle(const char* label, const ThreadHandle& handle) {
    kernel::serial::puts("[native-thread-test] ");
    kernel::serial::puts(label);
    kernel::serial::puts(" slot=");
    kernel::serial::put_hex32(handle.slot);
    kernel::serial::puts(" generation=");
    kernel::serial::put_hex32(handle.generation);
    kernel::serial::putc('\n');
}

uintptr_t worker_entry(void* raw) {
    WorkerContext* context = static_cast<WorkerContext*>(raw);
    if (context == nullptr) {
        return 0;
    }
    ++context->calls;
    context->context_ok = context->magic == kContextMagic &&
        context->version == kContextVersion;
    context->stack_ok = kernel::process::native_thread_test_snapshot_current(
        &context->snapshot) &&
        context->snapshot.stack_pointer >= context->snapshot.stack_base &&
        context->snapshot.stack_pointer < context->snapshot.stack_limit;
    if (context->stack_callback_index.isValid()) {
        context->stack_callback_value_set =
            gxos::runtime::setLocalStorageValue(
                context->stack_callback_index,
                reinterpret_cast<void*>(0xB00Du)) ==
            gxos::runtime::LocalStorageResult::Success;
    }
    context->abi_ok = context->snapshot.initial_instruction_pointer != 0 &&
        (context->snapshot.initial_stack_pointer & 0x0FU) == 8U;
    gxos::runtime::NativeStackBounds apiBounds{};
    const bool apiResult =
        gxos::runtime::queryCurrentNativeStackBounds(&apiBounds) ==
            gxos::runtime::StackBoundsResult::Success;
    context->stack_ok = context->stack_ok && apiResult &&
        apiBounds.low == context->snapshot.stack_base &&
        apiBounds.high == context->snapshot.stack_limit &&
        apiBounds.current >= apiBounds.low &&
        apiBounds.current < apiBounds.high;

    (void)context->started.signal();

    if (context->waits_for_request) {
        context->waiting = 1;
        Event* request = context->request_source == nullptr
            ? &context->request : context->request_source;
        context->wait_result = request->wait(
            context->finite_wait ? WaitTimeout::finiteMilliseconds(50)
                                 : WaitTimeout::infinite());
        context->waiting = 0;
        kernel::process::NativeThreadTestSnapshot afterWait{};
        if (kernel::process::native_thread_test_snapshot_current(&afterWait)) {
            context->snapshot.wait_queue_linked = afterWait.wait_queue_linked;
            context->snapshot.timer_linked = afterWait.timer_linked;
        }
    }

    context->nonvolatile_ok =
        context->snapshot.initial_rbx == 0 &&
        context->snapshot.initial_r12 != 0 &&
        context->snapshot.initial_r13 != 0 &&
        context->snapshot.initial_r14 == 0 &&
        context->snapshot.initial_r15 == 0;
    context->result_field = 0xA5000000U |
        (static_cast<uint32_t>(context->result_value) & 0x0000FFFFU);
    (void)context->done.signal();
    return context->result_value;
}

bool createWorker(WorkerContext* context,
                  bool detached,
                  ThreadHandle* handle,
                  const char* name) {
    ThreadCreateOptions options;
    options.stackSize = gxos::runtime::kNativeThreadDefaultStackSize;
    options.debugName = name;
    options.detached = detached;
    return gxos::runtime::createThread(worker_entry, context, options, handle) ==
        ThreadResult::Ok;
}

bool liveOnlyBootstrap() {
    return kernel::process::native_thread_test_live_count() == 1U;
}

bool runSingleWorker() {
    WorkerContext context(false, false, kSingleResult);
    ThreadHandle handle{};
    const bool created = createWorker(&context, false, &handle, "qemu-single");
    print_handle("single created", handle);
    if (!created || !handle.isValid()) {
        status("Single worker", false);
        return false;
    }
    uintptr_t result = 0;
    const WaitResult joined = gxos::runtime::joinThread(
        handle, WaitTimeout::infinite(), &result);
    const bool lifecycle = joined == WaitResult::Signaled &&
        result == kSingleResult && context.calls == 1U &&
        context.result_field == (0xA5000000U |
            static_cast<uint32_t>(kSingleResult)) && liveOnlyBootstrap();
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
    if (context.stack_ok == 0 || context.abi_ok == 0 || context.nonvolatile_ok == 0) {
        kernel::serial::puts("[native-thread-test] stack diag sp=");
        kernel::serial::put_hex64(context.snapshot.stack_pointer);
        kernel::serial::puts(" low=");
        kernel::serial::put_hex64(context.snapshot.stack_base);
        kernel::serial::puts(" high=");
        kernel::serial::put_hex64(context.snapshot.stack_limit);
        kernel::serial::puts(" ip=");
        kernel::serial::put_hex64(context.snapshot.initial_instruction_pointer);
        kernel::serial::puts(" initial_sp=");
        kernel::serial::put_hex64(context.snapshot.initial_stack_pointer);
        kernel::serial::puts(" rbx=");
        kernel::serial::put_hex64(context.snapshot.initial_rbx);
        kernel::serial::puts(" r12=");
        kernel::serial::put_hex64(context.snapshot.initial_r12);
        kernel::serial::puts(" r13=");
        kernel::serial::put_hex64(context.snapshot.initial_r13);
        kernel::serial::puts(" r14=");
        kernel::serial::put_hex64(context.snapshot.initial_r14);
        kernel::serial::puts(" r15=");
        kernel::serial::put_hex64(context.snapshot.initial_r15);
        kernel::serial::putc('\n');
    }
#endif
    status("Context delivery", context.context_ok != 0);
    status("Result capture", result == kSingleResult);
    status("Stack/ABI", context.stack_ok != 0 && context.abi_ok != 0 &&
        context.nonvolatile_ok != 0);
    status("Single worker", lifecycle);
    return lifecycle;
}

bool runStackBounds() {
    kernel::process::NativeThreadTestSnapshot bootstrap{};
    gxos::runtime::NativeStackBounds bootstrapBounds{};
    const bool bootstrapSnapshot =
        kernel::process::native_thread_test_snapshot_current(&bootstrap);
    const bool bootstrapQuery =
        gxos::runtime::queryCurrentNativeStackBounds(&bootstrapBounds) ==
            gxos::runtime::StackBoundsResult::Success;
    const bool bootstrapInside = bootstrapQuery &&
        bootstrapBounds.current >= bootstrapBounds.low &&
        bootstrapBounds.current < bootstrapBounds.high;
    const bool bootstrapExact = bootstrapSnapshot && bootstrapQuery &&
        bootstrap.stack_bounds_exact_source &&
        bootstrap.stack_base == bootstrapBounds.low &&
        bootstrap.stack_limit == bootstrapBounds.high;
    status("Bootstrap bounds available", bootstrapQuery);
    status("Bootstrap RSP inside bounds", bootstrapInside);
    status("Bootstrap bounds exact-source validation", bootstrapExact);
    status("Initial-thread exact stack bounds", bootstrapExact);
    status("Initial RSP validation", bootstrapInside);

    const bool initialized =
        gxos::runtime::initializeLocalStorage() ==
            gxos::runtime::LocalStorageResult::Success &&
        gxos::runtime::attachLocalStorage() ==
            gxos::runtime::LocalStorageResult::Success;
    gxos::runtime::LocalStorageIndex callbackIndex{};
    const bool allocated = initialized &&
        gxos::runtime::allocateLocalStorageIndex(
            stack_detach_callback, &callbackIndex) ==
        gxos::runtime::LocalStorageResult::Success;
    g_stack_callback_index = callbackIndex;
    g_stack_callback_count = 0;
    g_stack_callback_bounds_valid = false;

    WorkerContext workerContext(false, false, 0xB00Du);
    workerContext.stack_callback_index = callbackIndex;
    ThreadHandle handle{};
    const bool created = allocated && createWorker(
        &workerContext, false, &handle, "qemu-stack-bounds");
    uintptr_t result = 0;
    const bool joined = created &&
        gxos::runtime::joinThread(handle, WaitTimeout::infinite(), &result) ==
            WaitResult::Signaled;
    const bool workerBounds = joined && workerContext.stack_ok != 0 &&
        workerContext.stack_callback_value_set;
    status("Worker bounds available", workerBounds);
    status("Worker RSP inside bounds", workerContext.stack_ok != 0);
    status("Bounds valid during local-storage detach",
           g_stack_callback_count == 1 && g_stack_callback_bounds_valid);

    WorkerContext reused(false, false, 0xB00Eu);
    ThreadHandle reusedHandle{};
    const bool reusedCreated = createWorker(
        &reused, false, &reusedHandle, "qemu-stack-reuse");
    const bool oldInvalidated = reusedCreated && !kernel::process::native_thread_test_slot_matches(
        handle.slot, handle.generation);
    uintptr_t reusedResult = 0;
    const bool reusedJoined = reusedCreated &&
        gxos::runtime::joinThread(reusedHandle, WaitTimeout::infinite(),
                                  &reusedResult) == WaitResult::Signaled;
    status("Bounds invalidated before TCB reuse", oldInvalidated);
    status("TCB reuse receives new valid bounds", reusedJoined &&
           reused.stack_ok != 0 && reused.snapshot.stack_bounds_exact_source);

    const bool releaseIndex = allocated &&
        gxos::runtime::releaseLocalStorageIndex(callbackIndex) ==
            gxos::runtime::LocalStorageResult::Success;
    const bool detached = releaseIndex &&
        gxos::runtime::detachLocalStorage() ==
            gxos::runtime::LocalStorageResult::Success;
    const bool shutdown = detached &&
        gxos::runtime::shutdownLocalStorage() ==
            gxos::runtime::LocalStorageResult::Success;
    status("Stack-bound local-storage teardown", shutdown);
    return bootstrapExact && bootstrapInside && workerBounds &&
        g_stack_callback_bounds_valid && oldInvalidated && reusedJoined &&
        reused.stack_ok != 0 && shutdown;
}

bool runJoinTiming() {
    WorkerContext context(true, false, 0xBEEFU);
    ThreadHandle handle{};
    if (!createWorker(&context, false, &handle, "qemu-join-before")) {
        status("Join-before-exit", false);
        return false;
    }
    if (context.started.wait(WaitTimeout::finiteMilliseconds(1000)) !=
        WaitResult::Signaled || context.waiting == 0) {
        status("Join-before-exit", false);
        return false;
    }
    const bool zero = gxos::runtime::joinThread(
        handle, WaitTimeout::zero(), nullptr) == WaitResult::TimedOut;
    const bool finite = gxos::runtime::joinThread(
        handle, WaitTimeout::finiteMilliseconds(20), nullptr) ==
        WaitResult::TimedOut;
    const bool retained = kernel::process::native_thread_test_slot_matches(
        handle.slot, handle.generation);
    const bool signaled = context.request.signal() == EventStatus::Ok;
    uintptr_t result = 0;
    const bool retry = gxos::runtime::joinThread(
        handle, WaitTimeout::infinite(), &result) == WaitResult::Signaled &&
        result == context.result_value && context.wait_result == WaitResult::Signaled;
    status("Zero-timeout join", zero);
    status("Finite-timeout join", finite);
    status("Join retry", retry);
    status("Timed join retains target", retained);
    const bool before = signaled && retry && finite && zero && liveOnlyBootstrap();
    status("Join-before-exit", before);

    WorkerContext after(false, false, 0xCAFEU);
    ThreadHandle afterHandle{};
    const bool created = createWorker(&after, false, &afterHandle, "qemu-join-after");
    const bool finished = created && after.done.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    uintptr_t afterResult = 0;
    const bool joined = finished && gxos::runtime::joinThread(
        afterHandle, WaitTimeout::zero(), &afterResult) == WaitResult::Signaled &&
        afterResult == after.result_value;
    const bool second = gxos::runtime::joinThread(
        afterHandle, WaitTimeout::zero(), nullptr) == WaitResult::Invalid;
    const bool stale = !kernel::process::native_thread_test_slot_matches(
        afterHandle.slot, afterHandle.generation);
    status("Join-after-exit", joined);
    status("Second-join rejection", second);
    status("Stale-handle rejection", stale);
    return before && joined && second && stale && liveOnlyBootstrap();
}

bool runReuse() {
    ThreadHandle previous{};
    bool slotReused = true;
    bool generationChanged = true;
    bool staleRejected = true;
    for (uint32_t i = 0; i < 20U; ++i) {
        WorkerContext context(false, false, 0x2000U + i);
        ThreadHandle handle{};
        if (!createWorker(&context, false, &handle, "qemu-reuse")) {
            return false;
        }
        uintptr_t result = 0;
        if (gxos::runtime::joinThread(handle, WaitTimeout::infinite(), &result) !=
            WaitResult::Signaled || result != context.result_value) {
            return false;
        }
        if (i != 0U) {
            slotReused = slotReused && handle.slot == previous.slot;
            generationChanged = generationChanged &&
                handle.generation != previous.generation;
            staleRejected = staleRejected &&
                gxos::runtime::joinThread(previous, WaitTimeout::zero(), nullptr) ==
                WaitResult::Invalid;
        }
        previous = handle;
    }
    const bool passed = slotReused && generationChanged && staleRejected &&
        liveOnlyBootstrap();
    status("TCB reuse", passed);
    status("Generation change", generationChanged);
    status("Stale-handle rejection", staleRejected);
    return passed;
}

bool runDetach() {
    WorkerContext before(true, false, 0x4444U);
    ThreadHandle beforeHandle{};
    const bool createdBefore = createWorker(&before, false, &beforeHandle,
                                            "qemu-detach-before");
    const bool startedBefore = createdBefore && before.started.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    const bool detachedBefore = startedBefore &&
        gxos::runtime::detachThread(beforeHandle) == ThreadResult::Ok;
    const bool rejectedBefore = gxos::runtime::joinThread(
        beforeHandle, WaitTimeout::zero(), nullptr) == WaitResult::Invalid;
    const bool releasedBefore = before.request.signal() == EventStatus::Ok &&
        before.done.wait(WaitTimeout::finiteMilliseconds(1000)) ==
        WaitResult::Signaled;
    const bool reclaimedBefore = releasedBefore && liveOnlyBootstrap();
    status("Detach before exit", detachedBefore && rejectedBefore && reclaimedBefore);

    WorkerContext after(false, false, 0x5555U);
    ThreadHandle afterHandle{};
    const bool createdAfter = createWorker(&after, false, &afterHandle,
                                           "qemu-detach-after");
    const bool exitedAfter = createdAfter && after.done.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    const bool detachedAfter = exitedAfter &&
        gxos::runtime::detachThread(afterHandle) == ThreadResult::Ok;
    const bool rejectedAfter = gxos::runtime::joinThread(
        afterHandle, WaitTimeout::zero(), nullptr) == WaitResult::Invalid;
    const bool reclaimedAfter = detachedAfter && rejectedAfter && liveOnlyBootstrap();
    status("Detach after exit", reclaimedAfter);
    status("Stack/TCB leak check", reclaimedBefore && reclaimedAfter);
    return detachedBefore && rejectedBefore && reclaimedBefore && reclaimedAfter;
}

bool runMultipleWorkers() {
    WorkerContext contexts[4];
    Event sharedRequest(EventMode::ManualReset, false);
    ThreadHandle handles[4]{};
    for (uint32_t i = 0; i < 4U; ++i) {
        contexts[i].waits_for_request = true;
        contexts[i].result_value = 0x3000U + i;
        contexts[i].request_source = &sharedRequest;
    }
    bool created = true;
    for (uint32_t i = 0; i < 4U; ++i) {
        created = created && createWorker(&contexts[i], false, &handles[i],
                                          "qemu-multiple");
        if (!created || contexts[i].started.wait(
                WaitTimeout::finiteMilliseconds(1000)) != WaitResult::Signaled) {
            created = false;
            break;
        }
    }
    const bool signaled = created && sharedRequest.signal() == EventStatus::Ok;
    bool joined = created;
    for (uint32_t i = 0; i < 4U; ++i) {
        uintptr_t result = 0;
        joined = joined && gxos::runtime::joinThread(
            handles[i], WaitTimeout::infinite(), &result) == WaitResult::Signaled &&
            result == contexts[i].result_value && contexts[i].calls == 1U;
    }
    const bool isolated = contexts[0].result_field != contexts[1].result_field &&
        contexts[1].result_field != contexts[2].result_field &&
        contexts[2].result_field != contexts[3].result_field;
    const bool passed = created && signaled && joined && isolated && liveOnlyBootstrap();
    status("Multiple workers", passed);
    return passed;
}

bool runTimedWaits() {
    WorkerContext timeoutWorker(true, true, 0x6001U);
    ThreadHandle timeoutHandle{};
    const bool timeoutCreated = createWorker(&timeoutWorker, false, &timeoutHandle,
                                             "qemu-timeout");
    const bool timeoutStarted = timeoutCreated && timeoutWorker.started.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    const bool timeoutWaited = timeoutStarted && timeoutWorker.done.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    uintptr_t timeoutResult = 0;
    const bool timeoutJoined = timeoutWaited && gxos::runtime::joinThread(
        timeoutHandle, WaitTimeout::infinite(), &timeoutResult) == WaitResult::Signaled;
    const bool timerClean = timeoutWorker.wait_result == WaitResult::TimedOut &&
        !timeoutWorker.snapshot.wait_queue_linked &&
        !timeoutWorker.snapshot.timer_linked;
    status("Timeout before signal", timerClean && timeoutJoined);

    WorkerContext signaledWorker(true, true, 0x6002U);
    ThreadHandle signaledHandle{};
    const bool signalCreated = createWorker(&signaledWorker, false, &signaledHandle,
                                            "qemu-signal-before-timeout");
    const bool signalStarted = signalCreated && signaledWorker.started.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    const bool signalIssued = signalStarted && signaledWorker.request.signal() ==
        EventStatus::Ok;
    const bool signalDone = signalIssued && signaledWorker.done.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    uintptr_t signalResult = 0;
    const bool signalJoined = signalDone && gxos::runtime::joinThread(
        signaledHandle, WaitTimeout::infinite(), &signalResult) == WaitResult::Signaled;
    const bool signalClean = signaledWorker.wait_result == WaitResult::Signaled &&
        !signaledWorker.snapshot.wait_queue_linked &&
        !signaledWorker.snapshot.timer_linked;
    status("Signal before timeout", signalClean && signalJoined);
    status("Wait/timer cleanup", timerClean && signalClean && liveOnlyBootstrap());
    return timeoutJoined && signalJoined && timerClean && signalClean && liveOnlyBootstrap();
}

bool teardownOne(bool detached, bool timed, bool waitForStart) {
    WorkerContext context(true, timed, 0x7000U);
    ThreadHandle handle{};
    kernel::process::native_thread_test_set_current_owner(42U);
    const bool created = createWorker(&context, detached, &handle, "qemu-teardown");
    bool prepared = created;
    if (waitForStart) {
        prepared = prepared && context.started.wait(
            WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
        prepared = prepared && context.waiting != 0;
    }
    kernel::process::native_thread_test_set_current_owner(0U);
    const uint32_t terminated = kernel::process::terminate_process_threads(42U);
    const bool reclaimed = terminated == 1U &&
        !kernel::process::native_thread_test_slot_matches(handle.slot, handle.generation) &&
        liveOnlyBootstrap();
    return prepared && reclaimed;
}

bool runProcessTeardown() {
    const bool runnable = teardownOne(false, false, false);
    const bool blocked = teardownOne(false, false, true);
    const bool timed = teardownOne(false, true, true);
    const bool detached = teardownOne(true, false, true);
    const bool passed = runnable && blocked && timed && detached;
    status("Process teardown", passed);
    return passed;
}

} // namespace

void run() {
    kernel::serial::puts("[native-thread-test] BEGIN\n");
    if (kernel::process::native_thread_test_live_count() != 1U) {
        status("Initial TCB state", false);
    }
    else {
        (void)runSingleWorker();
        (void)runJoinTiming();
        (void)runReuse();
        (void)runDetach();
        (void)runMultipleWorkers();
        (void)runTimedWaits();
        (void)runProcessTeardown();
        (void)runStackBounds();
    }
    status("Stack/TCB leak check", liveOnlyBootstrap());
    kernel::serial::puts(g_all_passed
        ? "[native-thread-test] ALL_PASS\n"
        : "[native-thread-test] ALL_FAIL\n");
}

} // namespace native_thread_qemu_test
} // namespace kernel

#endif
