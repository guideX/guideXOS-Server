#include "runtime/thread/guidexos_native_thread.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using gxos::runtime::Event;
using gxos::runtime::EventMode;
using gxos::runtime::EventStatus;
using gxos::runtime::NativeThreadEntry;
using gxos::runtime::ThreadCreateOptions;
using gxos::runtime::ThreadHandle;
using gxos::runtime::ThreadResult;
using gxos::runtime::WaitResult;
using gxos::runtime::WaitTimeout;

extern "C" gxos_event_uint64 gxos_event_live_state_count_for_test();
extern "C" gxos_thread_uint32 gxos_native_thread_live_count_for_test();

namespace {
    using Clock = std::chrono::steady_clock;

    void require(bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    bool waitForLiveCount(gxos_thread_uint32 expected, unsigned timeoutMs = 500) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (gxos_native_thread_live_count_for_test() != expected &&
               Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return gxos_native_thread_live_count_for_test() == expected;
    }

    struct GateContext {
        explicit GateContext(bool initiallyReleased, uintptr_t value)
            : started(EventMode::ManualReset, false),
              release(EventMode::ManualReset, initiallyReleased),
              done(EventMode::ManualReset, false),
              calls(0),
              value(value) {
        }

        Event started;
        Event release;
        Event done;
        std::atomic<unsigned> calls;
        uintptr_t value;
    };

    uintptr_t gateEntry(void* raw) {
        GateContext* context = static_cast<GateContext*>(raw);
        context->calls.fetch_add(1, std::memory_order_release);
        (void)context->started.signal();
        if (context->release.wait(WaitTimeout::infinite()) != WaitResult::Signaled) {
            return 0;
        }
        (void)context->done.signal();
        return context->value;
    }

    uintptr_t immediateEntry(void* raw) {
        GateContext* context = static_cast<GateContext*>(raw);
        context->calls.fetch_add(1, std::memory_order_release);
        (void)context->started.signal();
        (void)context->done.signal();
        return context->value;
    }

    void creationAndResult() {
        GateContext context(true, 0x1234u);
        ThreadHandle handle;
        require(gxos::runtime::createThread(gateEntry, &context,
                                             ThreadCreateOptions{}, &handle) == ThreadResult::Ok,
            "single thread creation failed");
        require(context.started.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "worker did not start");
        uintptr_t result = 0;
        require(gxos::runtime::joinThread(handle, WaitTimeout::infinite(), &result) ==
                    WaitResult::Signaled,
            "single thread join failed");
        require(result == context.value, "exit result was not captured");
        require(context.calls.load(std::memory_order_acquire) == 1,
            "entry did not run exactly once");
        std::cout << "Single thread creation: PASS\n";
        std::cout << "Context delivery: PASS\n";
        std::cout << "Exit result: value=" << result << "\n";
    }

    void joinTiming() {
        GateContext beforeExit(false, 0xBEEFu);
        ThreadHandle beforeHandle;
        require(gxos::runtime::createThread(gateEntry, &beforeExit,
                                             ThreadCreateOptions{}, &beforeHandle) == ThreadResult::Ok,
            "join-before-exit creation failed");
        require(beforeExit.started.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "join-before-exit worker did not start");
        std::thread releaser([&beforeExit] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            (void)beforeExit.release.signal();
        });
        uintptr_t beforeResult = 0;
        require(gxos::runtime::joinThread(beforeHandle,
                                          WaitTimeout::finiteMilliseconds(500),
                                          &beforeResult) == WaitResult::Signaled,
            "join-before-exit did not wait for exit");
        releaser.join();
        require(beforeResult == beforeExit.value, "join-before-exit result mismatch");
        std::cout << "Join before exit: PASS\n";

        GateContext afterExit(true, 0xCAFEu);
        ThreadHandle afterHandle;
        require(gxos::runtime::createThread(immediateEntry, &afterExit,
                                             ThreadCreateOptions{}, &afterHandle) == ThreadResult::Ok,
            "join-after-exit creation failed");
        require(afterExit.done.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "join-after-exit worker did not finish");
        uintptr_t afterResult = 0;
        require(gxos::runtime::joinThread(afterHandle, WaitTimeout::zero(), &afterResult) ==
                    WaitResult::Signaled,
            "join-after-exit was not immediate");
        require(afterResult == afterExit.value, "join-after-exit result mismatch");
        std::cout << "Join after exit: PASS\n";

        GateContext zero(false, 0xD00Du);
        ThreadHandle zeroHandle;
        require(gxos::runtime::createThread(gateEntry, &zero,
                                             ThreadCreateOptions{}, &zeroHandle) == ThreadResult::Ok,
            "zero-timeout creation failed");
        require(zero.started.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "zero-timeout worker did not start");
        require(gxos::runtime::joinThread(zeroHandle, WaitTimeout::zero(), nullptr) ==
                    WaitResult::TimedOut,
            "zero-timeout join did not poll");
        std::cout << "Zero-timeout join: PASS\n";

        require(gxos::runtime::joinThread(zeroHandle,
                                          WaitTimeout::finiteMilliseconds(15),
                                          nullptr) == WaitResult::TimedOut,
            "finite join did not time out");
        std::cout << "Finite-timeout join: PASS\n";
        require(zero.release.signal() == EventStatus::Ok, "timeout release failed");
        uintptr_t zeroResult = 0;
        require(gxos::runtime::joinThread(zeroHandle, WaitTimeout::infinite(), &zeroResult) ==
                    WaitResult::Signaled,
            "join after timeout failed");
        require(zeroResult == zero.value, "join after timeout result mismatch");
        std::cout << "Join after timeout: PASS\n";
    }

    struct EventWorkerContext {
        Event request;
        Event done;
        uintptr_t value;

        EventWorkerContext()
            : request(EventMode::AutoReset, false),
              done(EventMode::ManualReset, false),
              value(0) {
        }
    };

    uintptr_t eventWorker(void* raw) {
        EventWorkerContext* context = static_cast<EventWorkerContext*>(raw);
        if (context->request.wait(WaitTimeout::infinite()) != WaitResult::Signaled) {
            return 0;
        }
        (void)context->done.signal();
        return context->value;
    }

    void eventCoordination() {
        EventWorkerContext context;
        context.value = 0xE11E;
        ThreadHandle handle;
        require(gxos::runtime::createThread(eventWorker, &context,
                                             ThreadCreateOptions{}, &handle) == ThreadResult::Ok,
            "event worker creation failed");
        require(context.request.signal() == EventStatus::Ok, "request signal failed");
        require(context.done.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "done event was not signaled");
        uintptr_t result = 0;
        require(gxos::runtime::joinThread(handle, WaitTimeout::infinite(), &result) ==
                    WaitResult::Signaled && result == context.value,
            "event worker join failed");
        std::cout << "Request/done Event coordination: PASS\n";
    }

    struct MultipleContext {
        Event started;
        std::atomic<unsigned> calls;
        unsigned index;

        explicit MultipleContext(unsigned value)
            : started(EventMode::ManualReset, false), calls(0), index(value) {
        }
    };

    uintptr_t multipleEntry(void* raw) {
        MultipleContext* context = static_cast<MultipleContext*>(raw);
        context->calls.fetch_add(1, std::memory_order_release);
        (void)context->started.signal();
        return static_cast<uintptr_t>(0x1000u + context->index);
    }

    void multipleThreads() {
        std::vector<std::unique_ptr<MultipleContext>> contexts;
        std::vector<ThreadHandle> handles;
        for (unsigned i = 0; i < 8; ++i) {
            contexts.emplace_back(new MultipleContext(i));
            ThreadHandle handle;
            require(gxos::runtime::createThread(multipleEntry, contexts.back().get(),
                                                ThreadCreateOptions{}, &handle) == ThreadResult::Ok,
                "multiple thread creation failed");
            handles.push_back(handle);
        }
        for (const auto& context : contexts) {
            require(context->started.wait(WaitTimeout::finiteMilliseconds(500)) ==
                        WaitResult::Signaled,
                "multiple thread did not run");
        }
        for (unsigned i = 0; i < handles.size(); ++i) {
            uintptr_t result = 0;
            require(gxos::runtime::joinThread(handles[i], WaitTimeout::infinite(), &result) ==
                        WaitResult::Signaled && result == 0x1000u + i,
                "multiple thread result mismatch");
            require(contexts[i]->calls.load(std::memory_order_acquire) == 1,
                "multiple entry ran more than once");
        }
        std::cout << "Multiple bounded threads: PASS\n";
    }

    struct SelfJoinContext {
        Event started;
        Event ready;
        ThreadHandle handle;

        SelfJoinContext()
            : started(EventMode::ManualReset, false),
              ready(EventMode::ManualReset, false),
              handle{} {
        }
    };

    uintptr_t selfJoinEntry(void* raw) {
        SelfJoinContext* context = static_cast<SelfJoinContext*>(raw);
        (void)context->started.signal();
        if (context->ready.wait(WaitTimeout::infinite()) != WaitResult::Signaled) {
            return 0;
        }
        return gxos::runtime::joinThread(context->handle, WaitTimeout::zero(), nullptr) ==
                WaitResult::Invalid ? 1u : 0u;
    }

    void ownershipAndReuse() {
        GateContext first(true, 0x1111);
        ThreadHandle oldHandle;
        require(gxos::runtime::createThread(immediateEntry, &first,
                                             ThreadCreateOptions{}, &oldHandle) == ThreadResult::Ok,
            "reuse first creation failed");
        require(first.done.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "reuse first worker did not finish");
        require(gxos::runtime::joinThread(oldHandle, WaitTimeout::infinite(), nullptr) ==
                    WaitResult::Signaled,
            "reuse first join failed");

        GateContext second(true, 0x2222);
        ThreadHandle newHandle;
        require(gxos::runtime::createThread(immediateEntry, &second,
                                             ThreadCreateOptions{}, &newHandle) == ThreadResult::Ok,
            "reuse second creation failed");
        require(newHandle.slot == oldHandle.slot &&
                    newHandle.generation != oldHandle.generation,
            "TCB slot was not reused with a new generation");
        require(gxos::runtime::joinThread(oldHandle, WaitTimeout::zero(), nullptr) ==
                    WaitResult::Invalid,
            "stale handle targeted reused slot");
        require(gxos::runtime::joinThread(newHandle, WaitTimeout::infinite(), nullptr) ==
                    WaitResult::Signaled,
            "new generation join failed");
        std::cout << "TCB reuse: PASS\n";
        std::cout << "Stale-handle rejection: PASS\n";

        GateContext doubleJoin(true, 0x3333);
        ThreadHandle doubleHandle;
        require(gxos::runtime::createThread(immediateEntry, &doubleJoin,
                                             ThreadCreateOptions{}, &doubleHandle) == ThreadResult::Ok,
            "double-join creation failed");
        require(doubleJoin.done.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "double-join worker did not finish");
        require(gxos::runtime::joinThread(doubleHandle, WaitTimeout::infinite(), nullptr) ==
                    WaitResult::Signaled &&
                gxos::runtime::joinThread(doubleHandle, WaitTimeout::zero(), nullptr) ==
                    WaitResult::Invalid,
            "double join was accepted");
        std::cout << "Double-join rejection: PASS\n";

        SelfJoinContext self;
        ThreadHandle selfHandle;
        require(gxos::runtime::createThread(selfJoinEntry, &self,
                                             ThreadCreateOptions{}, &selfHandle) == ThreadResult::Ok,
            "self-join creation failed");
        require(self.started.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "self-join worker did not start");
        self.handle = selfHandle;
        require(self.ready.signal() == EventStatus::Ok, "self-join release failed");
        uintptr_t selfResult = 0;
        require(gxos::runtime::joinThread(selfHandle, WaitTimeout::infinite(), &selfResult) ==
                    WaitResult::Signaled && selfResult == 1,
            "self join was not rejected by target");
        std::cout << "Self-join rejection: PASS\n";
    }

    void detachAndInvalid() {
        GateContext detached(false, 0x4444);
        ThreadHandle handle;
        require(gxos::runtime::createThread(gateEntry, &detached,
                                             ThreadCreateOptions{}, &handle) == ThreadResult::Ok,
            "detach creation failed");
        require(gxos::runtime::detachThread(handle) == ThreadResult::Ok,
            "detach before exit failed");
        require(gxos::runtime::joinThread(handle, WaitTimeout::zero(), nullptr) ==
                    WaitResult::Invalid,
            "detached thread was joinable");
        require(detached.release.signal() == EventStatus::Ok, "detached release failed");
        require(detached.done.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "detached worker did not finish");
        require(waitForLiveCount(0), "detached slot was not reclaimed");

        GateContext exited(false, 0x5555);
        ThreadHandle exitedHandle;
        require(gxos::runtime::createThread(immediateEntry, &exited,
                                             ThreadCreateOptions{}, &exitedHandle) == ThreadResult::Ok,
            "post-exit detach creation failed");
        require(exited.done.wait(WaitTimeout::finiteMilliseconds(500)) == WaitResult::Signaled,
            "post-exit detach worker did not finish");
        require(gxos::runtime::detachThread(exitedHandle) == ThreadResult::Ok,
            "detach after exit failed");
        require(gxos::runtime::joinThread(exitedHandle, WaitTimeout::zero(), nullptr) ==
                    WaitResult::Invalid,
            "post-exit detached thread remained joinable");
        require(waitForLiveCount(0), "post-exit detached slot was not reclaimed");
        std::cout << "Detach semantics: PASS\n";

        ThreadHandle invalid;
        require(gxos::runtime::joinThread(invalid, WaitTimeout::zero(), nullptr) ==
                    WaitResult::Invalid &&
                gxos::runtime::detachThread(invalid) == ThreadResult::InvalidHandle,
            "invalid handle was accepted");
        ThreadCreateOptions bad;
        bad.stackSize = 1024;
        ThreadHandle unused;
        require(gxos::runtime::createThread(immediateEntry, nullptr, bad, &unused) ==
                    ThreadResult::InvalidStackSize,
            "invalid stack size was accepted");
    }
}

int main() {
    try {
        creationAndResult();
        joinTiming();
        eventCoordination();
        multipleThreads();
        ownershipAndReuse();
        detachAndInvalid();
        require(waitForLiveCount(0), "thread slots remained live");
        require(gxos_event_live_state_count_for_test() == 0,
            "thread or test Events remained live");
        std::cout << "Cleanup/leak checks: PASS\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "native-thread: FAIL: " << error.what() << "\n";
        return 1;
    }
}
