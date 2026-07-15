#define GXOS_BARE_METAL 1

#include "runtime/synchronization/guidexos_scheduler_wait.h"
#include "runtime/synchronization/guidexos_event.h"

#include <iostream>
#include <stdexcept>

namespace {
    using gxos::runtime::Event;
    using gxos::runtime::EventMode;
    using gxos::runtime::EventStatus;
    using gxos::runtime::WaitResult;
    using gxos::runtime::WaitTimeout;
    using namespace gxos::runtime::scheduler_wait;

    struct FakeThread {
        WaitNode node{};
        bool runnable = true;
        unsigned runnableCalls = 0;
    };

    enum class ParkAction {
        None,
        WakeQueue,
        ExpireTimer,
        SignalEvent,
        DestroyEvent
    };

    struct FakeScheduler {
        WaitQueue queue{};
        FakeThread* current = nullptr;
        gxos::runtime::Event* event = nullptr;
        ParkAction action = ParkAction::None;
        gxos_wait_uint64 now = 0;
    };

    void require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    void* enter(void*) { return reinterpret_cast<void*>(1); }
    void leave(void*, void*) {}

    WaitNode* currentNode(void* context) {
        FakeScheduler* scheduler = static_cast<FakeScheduler*>(context);
        return scheduler->current == nullptr ? nullptr : &scheduler->current->node;
    }

    gxos_wait_uint64 nowTicks(void* context) {
        return static_cast<FakeScheduler*>(context)->now;
    }

    gxos_wait_uint64 durationToTicks(void*, gxos_wait_uint64 nanoseconds, bool* valid) {
        if (valid != nullptr) *valid = nanoseconds != 0;
        if (nanoseconds == 0) return 0;
        const gxos_wait_uint64 ticks = (nanoseconds + 999999ULL) / 1000000ULL;
        return ticks == 0 ? 1 : ticks;
    }

    void makeRunnable(void* context, WaitNode* node) {
        (void)context;
        if (node != nullptr && node->owner_thread != nullptr) {
            FakeThread* thread = static_cast<FakeThread*>(node->owner_thread);
            thread->runnable = true;
            ++thread->runnableCalls;
        }
    }

    WakeReason park(void* context, WaitNode* node) {
        FakeScheduler* scheduler = static_cast<FakeScheduler*>(context);
        switch (scheduler->action) {
        case ParkAction::WakeQueue:
            (void)wakeOne(&scheduler->queue, WakeReason::Signaled);
            break;
        case ParkAction::ExpireTimer:
            scheduler->now = node->deadline;
            (void)processExpired(scheduler->now, 8);
            break;
        case ParkAction::SignalEvent:
            require(scheduler->event != nullptr, "signal action has no event");
            require(scheduler->event->signal() == EventStatus::Ok,
                "event signal action failed");
            break;
        case ParkAction::DestroyEvent:
            require(scheduler->event != nullptr, "destroy action has no event");
            require(scheduler->event->close() == EventStatus::Ok,
                "event destroy action failed");
            break;
        case ParkAction::None:
        default:
            break;
        }
        return node != nullptr && node->state == WaitNodeState::Completed
            ? node->reason
            : WakeReason::Interrupted;
    }

    void install(FakeScheduler& scheduler) {
        const SchedulerWaitHooks hooks = {
            &scheduler,
            enter,
            leave,
            currentNode,
            nowTicks,
            durationToTicks,
            park,
            makeRunnable
        };
        installSchedulerWaitHooks(&hooks);
        initializeWaitQueue(&scheduler.queue);
    }

    void resetThread(FakeThread& thread) {
        thread.node = WaitNode{};
        thread.node.owner_thread = &thread;
        thread.runnable = true;
        thread.runnableCalls = 0;
    }

    void schedulerTests() {
        FakeScheduler scheduler;
        FakeThread first;
        FakeThread second;
        resetThread(first);
        resetThread(second);
        install(scheduler);

        scheduler.current = &first;
        WaitNode* firstNode = nullptr;
        require(prepareWait(&scheduler.queue, { true, 0 }, &firstNode),
            "prepare wait failed");
        require(wakeOne(&scheduler.queue, WakeReason::Signaled),
            "wake-one did not select waiter");
        require(waitQueueEmpty(&scheduler.queue), "wake-one left queue linked");
        require(parkWait(firstNode) == WakeReason::Signaled,
            "wake-one completion reason changed");
        require(first.node.state == WaitNodeState::Idle,
            "wait record was not reusable after wake-one");
        std::cout << "Scheduler park/wake tests: PASS\n";
        std::cout << "Wake-one: PASS\n";

        scheduler.current = &first;
        require(prepareWait(&scheduler.queue, { true, 0 }, &firstNode),
            "first multi-waiter prepare failed");
        scheduler.current = &second;
        WaitNode* secondNode = nullptr;
        require(prepareWait(&scheduler.queue, { true, 0 }, &secondNode),
            "second multi-waiter prepare failed");
        require(wakeAll(&scheduler.queue, WakeReason::Signaled) == 2,
            "wake-all count was not exact");
        scheduler.current = &first;
        require(parkWait(firstNode) == WakeReason::Signaled,
            "first wake-all result failed");
        scheduler.current = &second;
        require(parkWait(secondNode) == WakeReason::Signaled,
            "second wake-all result failed");
        require(waitQueueEmpty(&scheduler.queue), "wake-all left queue linked");
        require(first.runnableCalls == 2 && second.runnableCalls == 1,
            "runnable insertion count was not deterministic");
        std::cout << "Wake-all: PASS\n";
        std::cout << "Multi-waiter runtime tests: PASS\n";

        scheduler.current = &first;
        scheduler.now = 10;
        require(prepareWait(&scheduler.queue, { false, 3000000 }, &firstNode),
            "finite wait prepare failed");
        require(waitNodeTimerRegistered(firstNode), "timer was not registered");
        require(processExpired(12, 8) == 0, "timer expired too early");
        require(processExpired(13, 8) == 1, "timer did not expire");
        require(parkWait(firstNode) == WakeReason::TimedOut,
            "timer completion reason failed");
        require(!waitNodeTimerRegistered(firstNode), "timer was not cancelled");
        std::cout << "Zero timeout: PASS\n";
        std::cout << "Finite timeout: PASS\n";

        scheduler.current = &first;
        scheduler.now = 20;
        require(prepareWait(&scheduler.queue, { false, 5000000 }, &firstNode),
            "signal race prepare failed");
        require(wakeOne(&scheduler.queue, WakeReason::Signaled),
            "signal-before-timeout wake failed");
        require(processExpired(30, 8) == 0,
            "cancelled timer expired after signal");
        require(parkWait(firstNode) == WakeReason::Signaled,
            "signal-before-timeout result failed");

        scheduler.now = 40;
        require(prepareWait(&scheduler.queue, { false, 1000000 }, &firstNode),
            "timeout race prepare failed");
        require(processExpired(41, 8) == 1, "timeout-before-signal did not win");
        require(!wakeOne(&scheduler.queue, WakeReason::Signaled),
            "signal completed a timed-out waiter");
        require(parkWait(firstNode) == WakeReason::TimedOut,
            "timeout-before-signal result failed");
        std::cout << "Signal-before-timeout: PASS\n";
        std::cout << "Timeout-before-signal: PASS\n";
        std::cout << "Signal-timeout race: PASS\n";

        scheduler.current = &first;
        require(prepareWait(&scheduler.queue, { true, 0 }, &firstNode),
            "cancellation prepare failed");
        require(cancelWait(firstNode, WakeReason::Cancelled),
            "cancellation did not complete node");
        require(parkWait(firstNode) == WakeReason::Cancelled,
            "cancellation reason changed");
        require(waitQueueEmpty(&scheduler.queue), "cancelled node remained queued");

        require(prepareWait(&scheduler.queue, { false, 2000000 }, &firstNode),
            "teardown prepare failed");
        const unsigned runnableBeforeTeardown = first.runnableCalls;
        abandonWait(firstNode);
        require(waitQueueEmpty(&scheduler.queue), "teardown left wait queue entry");
        require(!waitNodeTimerRegistered(firstNode), "teardown left timer entry");
        require(first.runnableCalls == runnableBeforeTeardown,
            "teardown reinserted terminated thread");
        std::cout << "Wait cancellation: PASS\n";
        std::cout << "Thread teardown cleanup: PASS\n";
        std::cout << "Timer cancellation: PASS\n";
        std::cout << "Reuse: PASS\n";
    }

    void eventTests() {
        FakeScheduler scheduler;
        FakeThread thread;
        resetThread(thread);
        install(scheduler);
        scheduler.current = &thread;

        Event autoEvent(EventMode::AutoReset, false);
        scheduler.event = &autoEvent;
        scheduler.action = ParkAction::SignalEvent;
        require(autoEvent.wait(WaitTimeout::finiteMilliseconds(5)) == WaitResult::Signaled,
            "bare-metal auto wait did not wake");
        scheduler.action = ParkAction::None;
        require(autoEvent.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "auto wake was not consumed");
        require(autoEvent.signal() == EventStatus::Ok &&
                autoEvent.signal() == EventStatus::Ok,
            "auto repeated signal failed");
        require(autoEvent.wait(WaitTimeout::zero()) == WaitResult::Signaled &&
                autoEvent.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "auto pending signal was counted more than once");
        require(autoEvent.close() == EventStatus::Ok, "auto close failed");

        resetThread(thread);
        scheduler.current = &thread;
        Event manualEvent(EventMode::ManualReset, false);
        scheduler.event = &manualEvent;
        scheduler.action = ParkAction::SignalEvent;
        require(manualEvent.wait(WaitTimeout::finiteMilliseconds(5)) == WaitResult::Signaled,
            "bare-metal manual wait did not wake");
        scheduler.action = ParkAction::None;
        require(manualEvent.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "manual signal did not persist");
        require(manualEvent.reset() == EventStatus::Ok &&
                manualEvent.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "manual reset failed");

        resetThread(thread);
        scheduler.current = &thread;
        Event timeoutEvent(EventMode::AutoReset, false);
        scheduler.event = &timeoutEvent;
        scheduler.action = ParkAction::ExpireTimer;
        require(timeoutEvent.wait(WaitTimeout::finiteMilliseconds(5)) == WaitResult::TimedOut,
            "bare-metal finite timeout failed");
        scheduler.action = ParkAction::None;
        require(timeoutEvent.signal() == EventStatus::Ok &&
                timeoutEvent.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "signal after timeout was lost");

        resetThread(thread);
        scheduler.current = &thread;
        Event destroyEvent(EventMode::ManualReset, false);
        scheduler.event = &destroyEvent;
        scheduler.action = ParkAction::DestroyEvent;
        require(destroyEvent.wait(WaitTimeout::finiteMilliseconds(5)) == WaitResult::Destroyed,
            "destroy did not complete waiter");
        std::cout << "Manual-reset runtime tests: PASS\n";
        std::cout << "Auto-reset runtime tests: PASS\n";
        std::cout << "Bare-metal event runtime tests: PASS\n";
        std::cout << "Cleanup/leak checks: PASS\n";
    }
}

int main() {
    try {
        schedulerTests();
        eventTests();
        installSchedulerWaitHooks(nullptr);
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "scheduler/event: FAIL: " << error.what() << "\n";
        installSchedulerWaitHooks(nullptr);
        return 1;
    }
}

