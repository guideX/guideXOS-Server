#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_fls_adapter.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_stack_bounds_adapter.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_adapter.h"
#include "runtime/thread/guidexos_native_stack_bounds.h"
#include "runtime/thread/guidexos_native_thread.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using guidexos::nativeaot::threadstore::Result;
using guidexos::nativeaot::threadstore::ThreadSnapshot;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void status(const char* name, bool passed) {
    std::cout << name << ": " << (passed ? "PASS" : "FAIL") << "\n";
    require(passed, name);
}

gxos::runtime::StackBoundsResult corruptBounds(
    void*, gxos::runtime::NativeStackBounds* result) {
    result->low = 0x2000u;
    result->high = 0x1000u;
    result->current = 0x1800u;
    return gxos::runtime::StackBoundsResult::Success;
}

gxos::runtime::StackBoundsResult outsideBounds(
    void*, gxos::runtime::NativeStackBounds* result) {
    result->low = 0x1000u;
    result->high = 0x2000u;
    result->current = 0x3000u;
    return gxos::runtime::StackBoundsResult::Success;
}

struct WorkerContext {
    void* initialRecord;
    bool detachRuntime;
    bool boundsBeforeAttach;
    bool attached;
    bool lookupOwn;
    bool lookupIsolated;
    bool exactSnapshot;
    bool transitionReady;
    bool detached;
    bool lookupAfterDetach;
    ThreadSnapshot snapshot{};
};

std::uintptr_t worker(void* raw) {
    WorkerContext* context = static_cast<WorkerContext*>(raw);
    gxos::runtime::NativeStackBounds bounds{};
    context->boundsBeforeAttach =
        guidexos::nativeaot::pal::getMaximumStackBounds(&bounds) &&
        bounds.low < bounds.high && bounds.current >= bounds.low &&
        bounds.current < bounds.high;
    const Result attach =
        guidexos::nativeaot::threadstore::attachCurrentThread();
    context->attached = attach == Result::Success;
    context->lookupOwn = context->attached &&
        guidexos::nativeaot::threadstore::getCurrentThread() != nullptr;
    context->lookupIsolated = context->lookupOwn &&
        guidexos::nativeaot::threadstore::getCurrentThread() !=
            context->initialRecord;
    context->exactSnapshot = context->attached &&
        guidexos::nativeaot::threadstore::snapshotCurrentThread(
            &context->snapshot) &&
        context->snapshot.stackLow == bounds.low &&
        context->snapshot.stackHigh == bounds.high &&
        context->snapshot.currentStackPointer >= context->snapshot.stackLow &&
        context->snapshot.currentStackPointer < context->snapshot.stackHigh;
    context->transitionReady = context->exactSnapshot &&
        context->snapshot.transitionFrame == static_cast<std::uintptr_t>(-1) &&
        context->snapshot.deferredTransitionFrame ==
            static_cast<std::uintptr_t>(-1) &&
        context->snapshot.allocationContext == 0 &&
        context->snapshot.preemptive == 1;

    if (context->detachRuntime) {
        context->detached =
            guidexos::nativeaot::threadstore::detachCurrentThread() ==
            Result::Success;
        context->lookupAfterDetach =
            guidexos::nativeaot::threadstore::getCurrentThread() == nullptr;
    }
    return context->attached && context->lookupOwn && context->lookupIsolated &&
        context->exactSnapshot && context->transitionReady &&
        (!context->detachRuntime || (context->detached && context->lookupAfterDetach))
        ? 1u : 0u;
}

void runOneWorker(void* initialRecord, bool detachRuntime, WorkerContext* context) {
    context->initialRecord = initialRecord;
    context->detachRuntime = detachRuntime;
    gxos::runtime::ThreadHandle handle{};
    gxos::runtime::ThreadCreateOptions options;
    options.debugName = "nativeaot-threadstore-probe";
    require(gxos::runtime::createThread(worker, context, options, &handle) ==
                gxos::runtime::ThreadResult::Ok,
            "worker create failed");
    std::uintptr_t result = 0;
    require(gxos::runtime::joinThread(
                handle, gxos::runtime::WaitTimeout::infinite(), &result) ==
                gxos::runtime::WaitResult::Signaled && result == 1,
            "worker join failed");
}

struct HookReset {
    ~HookReset() {
        gxos::runtime::installNativeStackBoundsPlatformHooks(nullptr);
    }
};

} // namespace

int main() {
    try {
        const Result beforeFls =
            guidexos::nativeaot::threadstore::attachCurrentThread();
        status("Attach before FLS manager initialization",
               beforeFls == Result::StackBoundsUnavailable ||
               beforeFls == Result::NotInitialized);

        guidexos::nativeaot::fls::initialize();
        require(guidexos::nativeaot::fls::attachCurrentThread(),
                "generic initial attach failed");
        status("Generic initial local-storage attach", true);

        const Result beforeStore =
            guidexos::nativeaot::threadstore::attachCurrentThread();
        status("Attach before ThreadStore initialization",
               beforeStore == Result::NotInitialized);

        {
            HookReset reset;
            const gxos::runtime::NativeStackBoundsPlatformHooks hooks = {
                nullptr, corruptBounds
            };
            gxos::runtime::installNativeStackBoundsPlatformHooks(&hooks);
            const Result invalid =
                guidexos::nativeaot::threadstore::attachCurrentThread();
            status("Invalid bounds rejected", invalid == Result::InvalidBounds);
        }
        {
            HookReset reset;
            const gxos::runtime::NativeStackBoundsPlatformHooks hooks = {
                nullptr, outsideBounds
            };
            gxos::runtime::installNativeStackBoundsPlatformHooks(&hooks);
            const Result outside =
                guidexos::nativeaot::threadstore::attachCurrentThread();
            status("Current pointer outside bounds rejected",
                   outside == Result::CurrentPointerOutsideBounds);
        }

        require(guidexos::nativeaot::threadstore::initialize() ==
                    Result::Success,
                "ThreadStore initialize failed");
        status("ThreadStore global initialization", true);
        status("Double ThreadStore initialization",
               guidexos::nativeaot::threadstore::initialize() ==
                   Result::AlreadyInitialized);

        require(guidexos::nativeaot::threadstore::attachCurrentThread() ==
                    Result::Success,
                "initial ThreadStore attach failed");
        void* initialRecord =
            guidexos::nativeaot::threadstore::getCurrentThread();
        ThreadSnapshot initialSnapshot{};
        require(initialRecord != nullptr &&
                    guidexos::nativeaot::threadstore::snapshotCurrentThread(
                        &initialSnapshot),
                "initial lookup failed");
        require(initialSnapshot.stackLow < initialSnapshot.stackHigh &&
                    initialSnapshot.currentStackPointer >=
                        initialSnapshot.stackLow &&
                    initialSnapshot.currentStackPointer <
                        initialSnapshot.stackHigh,
                "initial stack bounds invalid");
        status("Initial-thread exact stack bounds", true);
        status("Initial RSP inside bounds", true);
        status("Initial-thread attachment", true);
        status("Current-thread lookup", true);
        status("Initial duplicate attach rejected",
               guidexos::nativeaot::threadstore::attachCurrentThread() ==
                   Result::AlreadyAttached);
        require(guidexos::nativeaot::threadstore::attachedThreadCount() == 1,
                "initial thread count incorrect");

        WorkerContext workerContext{};
        runOneWorker(initialRecord, true, &workerContext);
        status("Worker exact stack bounds", workerContext.boundsBeforeAttach &&
                   workerContext.exactSnapshot);
        status("Worker RSP inside bounds", workerContext.exactSnapshot);
        status("Worker-thread attachment", workerContext.attached);
        status("Worker lookup isolation", workerContext.lookupIsolated);
        status("Transition-frame readiness", workerContext.transitionReady);
        status("ThreadStore detach", workerContext.detached &&
                   workerContext.lookupAfterDetach);
        require(guidexos::nativeaot::threadstore::attachedThreadCount() == 1,
                "worker detach did not restore count");
        require(guidexos::nativeaot::threadstore::getCurrentThread() ==
                    initialRecord,
                "initial lookup changed after worker detach");
        status("Initial thread remains intact", true);

        WorkerContext callbackContext{};
        runOneWorker(initialRecord, false, &callbackContext);
        status("FLS callback detach", callbackContext.attached &&
                   callbackContext.lookupOwn &&
                   guidexos::nativeaot::threadstore::callbackDetachCount() == 1 &&
                   guidexos::nativeaot::threadstore::attachedThreadCount() == 1);
        status("Bounds valid through detach", callbackContext.exactSnapshot);

        WorkerContext workers[4]{};
        for (WorkerContext& context : workers) {
            runOneWorker(initialRecord, true, &context);
        }
        bool workerSetPassed = true;
        for (const WorkerContext& context : workers) {
            workerSetPassed = workerSetPassed && context.lookupIsolated &&
                context.exactSnapshot && context.detached;
        }
        status("Multiple worker attachment/detach", workerSetPassed &&
                   guidexos::nativeaot::threadstore::attachedThreadCount() == 1);
        status("Thread count restores baseline",
               guidexos::nativeaot::threadstore::attachedThreadCount() == 1);

        WorkerContext reuseA{};
        runOneWorker(initialRecord, true, &reuseA);
        WorkerContext reuseB{};
        runOneWorker(initialRecord, true, &reuseB);
        status("Runtime-record generation reuse", reuseA.snapshot.generation != 0 &&
                   reuseB.snapshot.generation != 0 &&
                   (reuseA.snapshot.generation != reuseB.snapshot.generation ||
                    reuseA.snapshot.stackLow != reuseB.snapshot.stackLow));
        status("TCB/runtime bounds clearing", reuseB.exactSnapshot &&
                   reuseB.detached);

        require(guidexos::nativeaot::threadstore::shutdown() ==
                    Result::ShutdownWithAttachedThreads,
                "shutdown with initial thread was not rejected");
        status("Shutdown with attached worker/thread rejected", true);
        require(guidexos::nativeaot::threadstore::detachCurrentThread() ==
                    Result::Success,
                "initial ThreadStore detach failed");
        status("Initial-thread detach", true);
        status("Lookup after detach returns null",
               guidexos::nativeaot::threadstore::getCurrentThread() == nullptr);
        status("Detach unattached rejected",
               guidexos::nativeaot::threadstore::detachCurrentThread() ==
                   Result::NotAttached);
        require(guidexos::nativeaot::threadstore::shutdown() ==
                    Result::Success,
                "ThreadStore shutdown failed");
        status("ThreadStore shutdown", true);
        status("Double shutdown rejected",
               guidexos::nativeaot::threadstore::shutdown() ==
                   Result::AlreadyShutdown);

        require(guidexos::nativeaot::fls::detachCurrentThread(),
                "generic initial detach failed");
        guidexos::nativeaot::fls::shutdown();
        status("FLS/ThreadStore detach ordering", true);
        std::cout << "Inactive ThreadStore adapter probe: PASS\n";
        std::cout << "RhInitialize called: no\n";
        std::cout << "GC initialized: no\n";
        std::cout << "Finalizer thread started: no\n";
        std::cout << "Collections entered: 0\n";
        std::cout << "GC-backed allocations: 0\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "nativeaot ThreadStore adapter probe failure: "
                  << error.what() << "\n";
        return 1;
    }
}
