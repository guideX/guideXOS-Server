#include "runtime/synchronization/guidexos_event.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using gxos::runtime::Event;
using gxos::runtime::EventMode;
using gxos::runtime::EventStatus;
using gxos::runtime::WaitResult;
using gxos::runtime::WaitTimeout;

extern "C" gxos_event_uint64 gxos_event_live_state_count_for_test();

namespace {
    using Clock = std::chrono::steady_clock;

    void require(bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    bool waitFor(const std::atomic<unsigned>& value, unsigned expected, unsigned timeoutMs = 1000) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (value.load(std::memory_order_acquire) < expected && Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return value.load(std::memory_order_acquire) >= expected;
    }

    unsigned countResult(const std::vector<WaitResult>& results, WaitResult wanted) {
        unsigned count = 0;
        for (WaitResult result : results) {
            if (result == wanted) {
                ++count;
            }
        }
        return count;
    }

    void manualTests() {
        Event initiallyNonsignaled(EventMode::ManualReset, false);
        require(initiallyNonsignaled.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "manual nonsignaled poll did not time out");

        Event initiallySignaled(EventMode::ManualReset, true);
        require(initiallySignaled.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "manual initial signal was not observed");

        require(initiallyNonsignaled.signal() == EventStatus::Ok, "manual signal failed");
        require(initiallyNonsignaled.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "manual signal-before-wait failed");
        require(initiallyNonsignaled.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "manual signal did not persist");
        require(initiallyNonsignaled.reset() == EventStatus::Ok, "manual reset failed");
        require(initiallyNonsignaled.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "manual reset did not restore nonsignaled state");

        Event multiple(EventMode::ManualReset, false);
        std::atomic<unsigned> started{0};
        std::vector<WaitResult> results(4, WaitResult::Invalid);
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < results.size(); ++i) {
            threads.emplace_back([&multiple, &started, &results, i] {
                started.fetch_add(1, std::memory_order_release);
                results[i] = multiple.wait(WaitTimeout::finiteMilliseconds(500));
            });
        }
        require(waitFor(started, 4), "manual multiple waiters did not start");
        require(multiple.signal() == EventStatus::Ok, "manual multi-waiter signal failed");
        for (auto& thread : threads) {
            thread.join();
        }
        require(countResult(results, WaitResult::Signaled) == 4,
            "manual signal did not release all waiters");

        Event reusable(EventMode::ManualReset, false);
        for (unsigned i = 0; i < 32; ++i) {
            require(reusable.signal() == EventStatus::Ok, "manual reuse signal failed");
            require(reusable.wait(WaitTimeout::zero()) == WaitResult::Signaled,
                "manual reuse wait failed");
            require(reusable.wait(WaitTimeout::zero()) == WaitResult::Signaled,
                "manual reuse persistence failed");
            require(reusable.reset() == EventStatus::Ok, "manual reuse reset failed");
            require(reusable.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
                "manual reuse reset state failed");
        }

        std::cout << "Manual initial-state tests: PASS\n";
        std::cout << "Manual signal/reset/reuse: PASS\n";
        std::cout << "Manual multi-waiter release: PASS\n";
    }

    void autoTests() {
        Event initiallyNonsignaled(EventMode::AutoReset, false);
        require(initiallyNonsignaled.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "auto nonsignaled poll did not time out");

        Event initiallySignaled(EventMode::AutoReset, true);
        require(initiallySignaled.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "auto initial signal was not observed");
        require(initiallySignaled.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "auto initial signal was not consumed");

        require(initiallyNonsignaled.signal() == EventStatus::Ok, "auto pending signal failed");
        require(initiallyNonsignaled.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "auto pending signal was not retained");
        require(initiallyNonsignaled.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "auto pending signal was counted twice");

        Event multiple(EventMode::AutoReset, false);
        std::atomic<unsigned> started{0};
        std::atomic<unsigned> successes{0};
        std::vector<WaitResult> results(4, WaitResult::Invalid);
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < results.size(); ++i) {
            threads.emplace_back([&multiple, &started, &successes, &results, i] {
                started.fetch_add(1, std::memory_order_release);
                results[i] = multiple.wait(WaitTimeout::finiteMilliseconds(500));
                if (results[i] == WaitResult::Signaled) {
                    successes.fetch_add(1, std::memory_order_release);
                }
            });
        }
        require(waitFor(started, 4), "auto multiple waiters did not start");
        require(multiple.signal() == EventStatus::Ok, "auto first waiter signal failed");
        require(waitFor(successes, 1), "auto first signal released no waiter");
        require(multiple.signal() == EventStatus::Ok, "auto second waiter signal failed");
        for (auto& thread : threads) {
            thread.join();
        }
        require(countResult(results, WaitResult::Signaled) == 2,
            "auto signals did not release exactly one waiter each");
        require(countResult(results, WaitResult::TimedOut) == 2,
            "auto waiters unexpectedly succeeded more than once per signal");

        Event repeated(EventMode::AutoReset, false);
        require(repeated.signal() == EventStatus::Ok, "auto repeated first signal failed");
        require(repeated.signal() == EventStatus::Ok, "auto repeated second signal failed");
        require(repeated.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "auto repeated signal was not retained");
        require(repeated.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "auto repeated signal became an unintended semaphore");

        Event reusable(EventMode::AutoReset, false);
        for (unsigned i = 0; i < 32; ++i) {
            require(reusable.signal() == EventStatus::Ok, "auto reuse signal failed");
            require(reusable.wait(WaitTimeout::zero()) == WaitResult::Signaled,
                "auto reuse wait failed");
            require(reusable.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
                "auto reuse did not consume signal");
        }

        std::cout << "Auto initial-state tests: PASS\n";
        std::cout << "Auto one-waiter release: PASS\n";
        std::cout << "Auto pending-signal behavior: PASS\n";
        std::cout << "Auto reuse: PASS\n";
    }

    void timeoutTests() {
        Event event(EventMode::AutoReset, false);

        const auto zeroStart = Clock::now();
        require(event.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
            "zero-timeout poll did not time out");
        require(Clock::now() - zeroStart < std::chrono::milliseconds(100),
            "zero-timeout poll blocked");

        const auto smallStart = Clock::now();
        require(event.wait(WaitTimeout::finiteMilliseconds(20)) == WaitResult::TimedOut,
            "small finite timeout did not time out");
        require(Clock::now() - smallStart >= std::chrono::milliseconds(10),
            "small finite timeout returned too early");

        const auto longerStart = Clock::now();
        require(event.wait(WaitTimeout::finiteMilliseconds(80)) == WaitResult::TimedOut,
            "longer finite timeout did not time out");
        require(Clock::now() - longerStart >= std::chrono::milliseconds(50),
            "longer finite timeout returned too early");

        std::atomic<bool> started{false};
        WaitResult signalResult = WaitResult::Invalid;
        std::thread waiter([&] {
            started.store(true, std::memory_order_release);
            signalResult = event.wait(WaitTimeout::finiteMilliseconds(500));
        });
        while (!started.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(event.signal() == EventStatus::Ok, "signal-before-timeout failed");
        waiter.join();
        require(signalResult == WaitResult::Signaled, "signal-before-timeout did not wake waiter");

        require(event.wait(WaitTimeout::finiteMilliseconds(15)) == WaitResult::TimedOut,
            "reuse-after-signal setup did not time out");
        require(event.signal() == EventStatus::Ok, "reuse-after-timeout signal failed");
        require(event.wait(WaitTimeout::zero()) == WaitResult::Signaled,
            "reuse-after-timeout did not observe signal");

        Event resetEvent(EventMode::ManualReset, false);
        WaitResult resetResult = WaitResult::Invalid;
        std::atomic<bool> resetStarted{false};
        std::thread resetWaiter([&] {
            resetStarted.store(true, std::memory_order_release);
            resetResult = resetEvent.wait(WaitTimeout::finiteMilliseconds(200));
        });
        while (!resetStarted.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(resetEvent.signal() == EventStatus::Ok, "reset race signal failed");
        require(resetEvent.reset() == EventStatus::Ok, "reset race reset failed");
        resetWaiter.join();
        require(resetResult == WaitResult::TimedOut || resetResult == WaitResult::Signaled,
            "reset race returned an invalid result");

        for (unsigned i = 0; i < 24; ++i) {
            Event race(EventMode::AutoReset, false);
            WaitResult result = WaitResult::Invalid;
            std::thread raceWaiter([&] {
                result = race.wait(WaitTimeout::finiteMilliseconds(2));
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            require(race.signal() == EventStatus::Ok, "timeout-boundary signal failed");
            raceWaiter.join();
            if (result == WaitResult::Signaled) {
                require(race.wait(WaitTimeout::zero()) == WaitResult::TimedOut,
                    "auto signal was consumed twice at timeout boundary");
            }
            else if (result == WaitResult::TimedOut) {
                require(race.wait(WaitTimeout::zero()) == WaitResult::Signaled,
                    "auto signal was lost at timeout boundary");
            }
            else {
                throw std::runtime_error("timeout-boundary returned unexpected result");
            }
        }

        std::cout << "Zero-timeout polling: PASS\n";
        std::cout << "Finite timeout: PASS\n";
        std::cout << "Signal-timeout race: PASS\n";
    }

    void cleanupTests() {
        for (unsigned i = 0; i < 512; ++i) {
            Event event((i & 1u) == 0 ? EventMode::ManualReset : EventMode::AutoReset, false);
            require(event.isInitialized(), "repeated event initialization failed");
            require(event.close() == EventStatus::Ok, "repeated event close failed");
            require(event.signal() == EventStatus::Invalid, "closed event accepted signal");
            require(event.close() == EventStatus::Invalid, "closed event closed twice");
        }

        std::unique_ptr<Event> event(new Event(EventMode::ManualReset, false));
        Event* raw = event.get();
        std::atomic<bool> started{false};
        WaitResult result = WaitResult::Invalid;
        std::thread waiter([raw, &started, &result] {
            started.store(true, std::memory_order_release);
            result = raw->wait(WaitTimeout::finiteMilliseconds(500));
        });
        while (!started.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        event.reset();
        waiter.join();
        require(result == WaitResult::Destroyed,
            "active waiter did not receive Destroyed during hosted cleanup");
        require(gxos_event_live_state_count_for_test() == 0,
            "event state remained live after hosted cleanup");

        std::cout << "Cleanup/leak checks: PASS\n";
    }

    bool run(const char* label, void (*test)()) {
        try {
            test();
            return true;
        }
        catch (const std::exception& error) {
            std::cerr << label << ": FAIL: " << error.what() << "\n";
            return false;
        }
    }
}

int main() {
    if (!run("manual", manualTests) ||
        !run("auto", autoTests) ||
        !run("timeout", timeoutTests) ||
        !run("cleanup", cleanupTests)) {
        return 1;
    }
    return 0;
}
