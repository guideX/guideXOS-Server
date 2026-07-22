#include "runtime/thread/guidexos_native_stack_bounds.h"
#include "runtime/thread/guidexos_native_thread.h"
#include "runtime/local_storage/guidexos_local_storage.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using gxos::runtime::NativeStackBounds;
using gxos::runtime::StackBoundsResult;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void status(const char* name, bool passed) {
    std::cout << name << ": " << (passed ? "PASS" : "FAIL") << "\n";
    require(passed, name);
}

bool valid(const NativeStackBounds& bounds) {
    return bounds.low < bounds.high &&
        bounds.current >= bounds.low && bounds.current < bounds.high;
}

StackBoundsResult corruptBounds(void*, NativeStackBounds* result) {
    result->low = 0x3000u;
    result->high = 0x2000u;
    result->current = 0x2800u;
    return StackBoundsResult::Success;
}

StackBoundsResult outsideBounds(void*, NativeStackBounds* result) {
    result->low = 0x2000u;
    result->high = 0x3000u;
    result->current = 0x4000u;
    return StackBoundsResult::Success;
}

struct WorkerContext {
    gxos::runtime::Event ready;
    gxos::runtime::Event release;
    NativeStackBounds bounds{};
    gxos::runtime::LocalStorageIndex callbackIndex{};
    bool hold = false;
    bool queryPassed = false;
    bool valueSet = false;

    WorkerContext(bool shouldHold = false)
        : ready(gxos::runtime::EventMode::ManualReset, false),
          release(gxos::runtime::EventMode::ManualReset, false),
          hold(shouldHold) {
    }
};

std::atomic<unsigned> g_callbackCount{0};
NativeStackBounds g_callbackBounds{};
StackBoundsResult g_callbackResult = StackBoundsResult::Unavailable;

void stackDetachCallback(void*) {
    NativeStackBounds bounds{};
    g_callbackResult = gxos::runtime::queryCurrentNativeStackBounds(&bounds);
    g_callbackBounds = bounds;
    g_callbackCount.fetch_add(1, std::memory_order_release);
}

std::uintptr_t worker(void* raw) {
    WorkerContext* context = static_cast<WorkerContext*>(raw);
    context->queryPassed =
        gxos::runtime::queryCurrentNativeStackBounds(&context->bounds) ==
            StackBoundsResult::Success && valid(context->bounds);
    if (context->callbackIndex.isValid()) {
        context->valueSet =
            gxos::runtime::setLocalStorageValue(
                context->callbackIndex, reinterpret_cast<void*>(0xB00Du)) ==
            gxos::runtime::LocalStorageResult::Success;
    }
    (void)context->ready.signal();
    if (context->hold) {
        (void)context->release.wait(gxos::runtime::WaitTimeout::infinite());
    }
    return context->queryPassed ? 1u : 0u;
}

void createAndJoin(WorkerContext* context) {
    gxos::runtime::ThreadHandle handle{};
    gxos::runtime::ThreadCreateOptions options;
    options.debugName = "stack-bounds-test";
    require(gxos::runtime::createThread(worker, context, options, &handle) ==
                gxos::runtime::ThreadResult::Ok,
            "worker creation failed");
    std::uintptr_t result = 0;
    require(gxos::runtime::joinThread(
                handle, gxos::runtime::WaitTimeout::infinite(), &result) ==
                gxos::runtime::WaitResult::Signaled && result == 1u,
            "worker join failed");
}

} // namespace

int main() {
    try {
        NativeStackBounds initial{};
        status("Initial-thread query",
               gxos::runtime::queryCurrentNativeStackBounds(&initial) ==
                   StackBoundsResult::Success);
        status("Initial RSP inside bounds", valid(initial));
        status("Low/high ordering", initial.low < initial.high);
        status("Initial stack page alignment",
               (initial.low & 0xFFFu) == 0 && (initial.high & 0xFFFu) == 0);
        status("Invalid output pointer",
               gxos::runtime::queryCurrentNativeStackBounds(nullptr) ==
                   StackBoundsResult::InvalidOutput);

        {
            const gxos::runtime::NativeStackBoundsPlatformHooks hooks = {
                nullptr, corruptBounds
            };
            gxos::runtime::installNativeStackBoundsPlatformHooks(&hooks);
            status("Corrupt bounds rejected",
                   gxos::runtime::queryCurrentNativeStackBounds(&initial) ==
                       StackBoundsResult::InvalidBounds);
        }
        {
            const gxos::runtime::NativeStackBoundsPlatformHooks hooks = {
                nullptr, outsideBounds
            };
            gxos::runtime::installNativeStackBoundsPlatformHooks(&hooks);
            status("Current pointer outside bounds rejected",
                   gxos::runtime::queryCurrentNativeStackBounds(&initial) ==
                       StackBoundsResult::CurrentPointerOutsideBounds);
        }
        gxos::runtime::installNativeStackBoundsPlatformHooks(nullptr);

        require(gxos::runtime::initializeLocalStorage() ==
                    gxos::runtime::LocalStorageResult::Success &&
                    gxos::runtime::attachLocalStorage() ==
                    gxos::runtime::LocalStorageResult::Success,
                "local storage setup failed");
        gxos::runtime::LocalStorageIndex callbackIndex{};
        require(gxos::runtime::allocateLocalStorageIndex(
                    stackDetachCallback, &callbackIndex) ==
                    gxos::runtime::LocalStorageResult::Success,
                "callback index allocation failed");

        WorkerContext single;
        single.callbackIndex = callbackIndex;
        createAndJoin(&single);
        status("Worker-thread query", single.queryPassed);
        status("Worker RSP inside bounds", valid(single.bounds));
        status("Minimum expected worker size",
               single.bounds.high - single.bounds.low >= 4096u);
        status("Bounds valid during detach callback",
               g_callbackCount.load(std::memory_order_acquire) == 1 &&
                   g_callbackResult == StackBoundsResult::Success &&
                   valid(g_callbackBounds));

        WorkerContext first(true);
        WorkerContext second(true);
        gxos::runtime::ThreadHandle firstHandle{};
        gxos::runtime::ThreadHandle secondHandle{};
        gxos::runtime::ThreadCreateOptions options;
        options.debugName = "stack-bounds-distinct-a";
        require(gxos::runtime::createThread(worker, &first, options, &firstHandle) ==
                    gxos::runtime::ThreadResult::Ok &&
                    first.ready.wait(gxos::runtime::WaitTimeout::infinite()) ==
                    gxos::runtime::WaitResult::Signaled,
                "first held worker failed");
        options.debugName = "stack-bounds-distinct-b";
        require(gxos::runtime::createThread(worker, &second, options, &secondHandle) ==
                    gxos::runtime::ThreadResult::Ok &&
                    second.ready.wait(gxos::runtime::WaitTimeout::infinite()) ==
                    gxos::runtime::WaitResult::Signaled,
                "second held worker failed");
        status("Distinct worker stacks", first.bounds.low != second.bounds.low &&
                   first.bounds.high != second.bounds.high);
        (void)first.release.signal();
        (void)second.release.signal();
        std::uintptr_t result = 0;
        require(gxos::runtime::joinThread(firstHandle,
                    gxos::runtime::WaitTimeout::infinite(), &result) ==
                    gxos::runtime::WaitResult::Signaled && result == 1u &&
                gxos::runtime::joinThread(secondHandle,
                    gxos::runtime::WaitTimeout::infinite(), &result) ==
                    gxos::runtime::WaitResult::Signaled && result == 1u,
                "held worker join failed");

        bool reusePassed = true;
        NativeStackBounds previous{};
        for (unsigned i = 0; i < 20; ++i) {
            WorkerContext context;
            context.callbackIndex = callbackIndex;
            createAndJoin(&context);
            reusePassed = reusePassed && valid(context.bounds) &&
                context.valueSet &&
                (i == 0 || context.bounds.low != 0 || previous.low != 0);
            previous = context.bounds;
        }
        status("Repeated TCB/host-slot reuse bounds", reusePassed);
        NativeStackBounds afterWorker{};
        const StackBoundsResult afterResult =
            gxos::runtime::queryCurrentNativeStackBounds(&afterWorker);
        status("Query after worker state unavailable",
               afterResult == StackBoundsResult::Success && valid(afterWorker) &&
                   afterWorker.current != single.bounds.current);

        require(gxos::runtime::releaseLocalStorageIndex(callbackIndex) ==
                    gxos::runtime::LocalStorageResult::Success,
                "callback index release failed");
        require(gxos::runtime::detachLocalStorage() ==
                    gxos::runtime::LocalStorageResult::Success &&
                gxos::runtime::shutdownLocalStorage() ==
                    gxos::runtime::LocalStorageResult::Success,
                "local storage teardown failed");
        status("Local-storage detach and teardown", true);
        std::cout << "Hosted stack-bound tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "native stack bounds test failure: " << error.what() << "\n";
        return 1;
    }
}
