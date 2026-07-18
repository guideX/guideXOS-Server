#include "include/kernel/native_mutex_qemu_test.h"

#if defined(GXOS_NATIVE_MUTEX_QEMU_TEST)

#include "include/kernel/process.h"
#include "include/kernel/serial_debug.h"
#include "runtime/synchronization/guidexos_event.h"
#include "runtime/synchronization/guidexos_mutex.h"
#include "runtime/thread/guidexos_native_thread.h"

namespace kernel {
namespace native_mutex_qemu_test {
namespace {

using gxos::runtime::Event;
using gxos::runtime::EventMode;
using gxos::runtime::EventStatus;
using gxos::runtime::Mutex;
using gxos::runtime::MutexMode;
using gxos::runtime::MutexResult;
using gxos::runtime::MutexStatus;
using gxos::runtime::ThreadCreateOptions;
using gxos::runtime::ThreadHandle;
using gxos::runtime::ThreadResult;
using gxos::runtime::WaitResult;
using gxos::runtime::WaitTimeout;

constexpr uint32_t kWaiterCount = 3;
bool g_all_passed = true;

void status(const char* name, bool passed) {
    kernel::serial::puts("[native-mutex-test] ");
    kernel::serial::puts(name);
    kernel::serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) {
        g_all_passed = false;
    }
}

struct WorkerContext {
    Mutex* mutex;
    Event started;
    Event done;
    Event* release;
    MutexResult lock_result;
    MutexResult unlock_result;
    MutexStatus owner_exit_result;
    uint32_t id;
    uint32_t* order;
    uint32_t* order_count;
    uint32_t* protected_counter;
    bool try_only;
    bool unlock_only;
    bool notify_exit;

    WorkerContext()
        : mutex(nullptr), started(EventMode::ManualReset, false),
          done(EventMode::ManualReset, false), release(nullptr),
          lock_result(MutexResult::Invalid),
          unlock_result(MutexResult::Invalid),
          owner_exit_result(MutexStatus::Invalid), id(0), order(nullptr),
          order_count(nullptr), protected_counter(nullptr), try_only(false),
          unlock_only(false), notify_exit(false) {
    }
};

uintptr_t worker_entry(void* raw) {
    WorkerContext* context = static_cast<WorkerContext*>(raw);
    if (context == nullptr || context->mutex == nullptr) {
        return 0;
    }
    (void)context->started.signal();
    if (context->unlock_only) {
        context->unlock_result = context->mutex->unlock();
        (void)context->done.signal();
        return 0;
    }
    context->lock_result = context->try_only
        ? context->mutex->tryLock()
        : context->mutex->lock();
    if (context->lock_result == MutexResult::Acquired) {
        if (context->notify_exit) {
            context->owner_exit_result = context->mutex->notifyOwnerExit();
        }
        else {
            if (context->order != nullptr && context->order_count != nullptr) {
                const uint32_t position = (*context->order_count)++;
                if (position < kWaiterCount) {
                    context->order[position] = context->id;
                }
            }
            if (context->protected_counter != nullptr) {
                ++(*context->protected_counter);
            }
            if (context->release != nullptr) {
                (void)context->release->wait(WaitTimeout::infinite());
            }
            context->unlock_result = context->mutex->unlock();
        }
    }
    (void)context->done.signal();
    return 0;
}

bool createWorker(WorkerContext* context, ThreadHandle* handle, const char* name) {
    ThreadCreateOptions options;
    options.stackSize = gxos::runtime::kNativeThreadDefaultStackSize;
    options.debugName = name;
    options.detached = false;
    return gxos::runtime::createThread(worker_entry, context, options, handle) ==
        ThreadResult::Ok;
}

bool joinWorker(WorkerContext* context, const ThreadHandle& handle) {
    uintptr_t result = 0;
    return context != nullptr &&
        gxos::runtime::joinThread(handle, WaitTimeout::infinite(), &result) ==
        WaitResult::Signaled && context->done.isInitialized();
}

void runBasic() {
    Mutex mutex(MutexMode::NonRecursive);
    const bool acquired = mutex.lock() == MutexResult::Acquired;
    const bool selfRejected = mutex.lock() == MutexResult::AlreadyOwned;
    const bool released = mutex.unlock() == MutexResult::Released;
    const bool secondReleased = mutex.unlock() == MutexResult::NotOwner;
    const bool destroyed = mutex.destroy() == MutexStatus::Ok;
    status("Basic acquire/release", acquired && released && secondReleased);
    status("Nonrecursive self-lock", selfRejected);
    status("Mutex lifecycle destroy", destroyed);

    Mutex recursive(MutexMode::Recursive);
    const bool recursivePass = recursive.lock() == MutexResult::Acquired &&
        recursive.lock() == MutexResult::Acquired &&
        recursive.tryLock() == MutexResult::Acquired &&
        recursive.unlock() == MutexResult::Released &&
        recursive.unlock() == MutexResult::Released &&
        recursive.unlock() == MutexResult::Released &&
        recursive.destroy() == MutexStatus::Ok;
    status("Recursive acquire/release", recursivePass);
}

void runTryLock() {
    Mutex mutex(MutexMode::NonRecursive);
    const bool owner = mutex.lock() == MutexResult::Acquired;
    WorkerContext contender;
    contender.mutex = &mutex;
    contender.try_only = true;
    ThreadHandle handle{};
    const bool created = createWorker(&contender, &handle, "mutex-try");
    const bool started = created && contender.started.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    const bool joined = created && joinWorker(&contender, handle);
    status("Try-lock free/contended", owner && started && joined &&
        contender.lock_result == MutexResult::WouldBlock &&
        mutex.unlock() == MutexResult::Released &&
        mutex.destroy() == MutexStatus::Ok);
}

void runSingleWaiter() {
    Mutex mutex(MutexMode::NonRecursive);
    WorkerContext waiter;
    waiter.mutex = &mutex;
    ThreadHandle handle{};
    const bool owner = mutex.lock() == MutexResult::Acquired;
    const bool created = owner && createWorker(&waiter, &handle, "mutex-single");
    const bool started = created && waiter.started.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    const bool busyDestroy = started && mutex.destroy() == MutexStatus::Busy;
    const bool released = owner && mutex.unlock() == MutexResult::Released;
    const bool joined = created && joinWorker(&waiter, handle);
    status("Destroy with waiters", busyDestroy);
    status("Single contended waiter", owner && created && started && busyDestroy &&
        released && joined && waiter.lock_result == MutexResult::Acquired &&
        waiter.unlock_result == MutexResult::Released &&
        mutex.destroy() == MutexStatus::Ok);
}

void runFifoWaiters() {
    Mutex mutex(MutexMode::NonRecursive);
    WorkerContext contexts[kWaiterCount];
    ThreadHandle handles[kWaiterCount]{};
    uint32_t order[kWaiterCount] = { 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU };
    uint32_t orderCount = 0;
    uint32_t protectedCounter = 0;
    bool passed = mutex.lock() == MutexResult::Acquired;

    for (uint32_t i = 0; i < kWaiterCount; ++i) {
        contexts[i].mutex = &mutex;
        contexts[i].id = i;
        contexts[i].order = order;
        contexts[i].order_count = &orderCount;
        contexts[i].protected_counter = &protectedCounter;
        passed = passed && createWorker(&contexts[i], &handles[i], "mutex-fifo");
        passed = passed && contexts[i].started.wait(
            WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    }

    const bool busyDestroy = mutex.destroy() == MutexStatus::Busy;
    passed = passed && mutex.unlock() == MutexResult::Released;
    for (uint32_t i = 0; i < kWaiterCount; ++i) {
        passed = passed && joinWorker(&contexts[i], handles[i]);
    }
    bool fifo = orderCount == kWaiterCount;
    for (uint32_t i = 0; i < kWaiterCount && fifo; ++i) {
        fifo = order[i] == i && contexts[i].lock_result == MutexResult::Acquired &&
            contexts[i].unlock_result == MutexResult::Released;
    }
    const bool destroyed = mutex.destroy() == MutexStatus::Ok;
    status("FIFO waiters", passed && busyDestroy && fifo);
    status("Protected counter", protectedCounter == kWaiterCount);
    kernel::serial::puts("[native-mutex-test] Protected counter: expected=3 observed=");
    kernel::serial::put_hex32(protectedCounter);
    kernel::serial::putc('\n');
    status("Wait-node cleanup", passed && destroyed);
    status("Mutex leak check", destroyed);
}

void runOwnershipAndExit() {
    Mutex mutex(MutexMode::NonRecursive);
    WorkerContext nonOwner;
    nonOwner.mutex = &mutex;
    nonOwner.unlock_only = true;
    ThreadHandle nonOwnerHandle{};
    const bool owner = mutex.lock() == MutexResult::Acquired;
    const bool created = owner && createWorker(&nonOwner, &nonOwnerHandle, "mutex-owner");
    const bool started = created && nonOwner.started.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    const bool joined = created && joinWorker(&nonOwner, nonOwnerHandle);
    const bool nonOwnerUnlock = started && joined &&
        nonOwner.unlock_result == MutexResult::NotOwner;
    const bool busy = mutex.destroy() == MutexStatus::Busy;
    const bool released = mutex.unlock() == MutexResult::Released;
    const bool destroyed = mutex.destroy() == MutexStatus::Ok;
    status("Non-owner unlock", nonOwnerUnlock);
    status("Destroy owned", busy && released && destroyed);

    static Mutex orphan(MutexMode::NonRecursive);
    WorkerContext exiting;
    exiting.mutex = &orphan;
    exiting.notify_exit = true;
    ThreadHandle exitHandle{};
    const bool exitCreated = createWorker(&exiting, &exitHandle, "mutex-exit");
    const bool exitStarted = exitCreated && exiting.started.wait(
        WaitTimeout::finiteMilliseconds(1000)) == WaitResult::Signaled;
    const bool exitJoined = exitCreated && joinWorker(&exiting, exitHandle);
    const bool detected = exitStarted && exitJoined &&
        exiting.owner_exit_result == MutexStatus::OwnerExitViolation;
    const bool retained = orphan.tryLock() == MutexResult::WouldBlock &&
        orphan.destroy() == MutexStatus::Busy;
    status("Owner-exit diagnostic", detected && retained);
}

} // namespace

void run() {
    g_all_passed = true;
    runBasic();
    runTryLock();
    runSingleWaiter();
    runFifoWaiters();
    runOwnershipAndExit();
    kernel::serial::puts("[native-mutex-test] ALL_PASS: ");
    kernel::serial::puts(g_all_passed ? "PASS\n" : "FAIL\n");
}

} // namespace native_mutex_qemu_test
} // namespace kernel

#endif // GXOS_NATIVE_MUTEX_QEMU_TEST
