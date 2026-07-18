#include "../synchronization/guidexos_mutex.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
    int g_failures = 0;

    void check(bool condition, const char* label) {
        if (condition) {
            std::cout << "[mutex-test] " << label << ": PASS\n";
        }
        else {
            std::cout << "[mutex-test] " << label << ": FAIL\n";
            ++g_failures;
        }
    }

    void basicAndOwnershipTests() {
        gxos::runtime::Mutex mutex;
        check(!mutex.isInitialized(), "Uninitialized state");
        check(mutex.lock() == gxos::runtime::MutexResult::Invalid,
              "Uninitialized lock rejected");
        check(mutex.initialize(gxos::runtime::MutexMode::NonRecursive),
              "Nonrecursive initialize");
        check(mutex.lock() == gxos::runtime::MutexResult::Acquired,
              "Basic acquire");
        check(mutex.lock() == gxos::runtime::MutexResult::AlreadyOwned,
              "Nonrecursive self-lock rejected");
        check(mutex.tryLock() == gxos::runtime::MutexResult::AlreadyOwned,
              "Nonrecursive self-try-lock rejected");
        check(mutex.unlock() == gxos::runtime::MutexResult::Released,
              "Basic release");
        check(mutex.unlock() == gxos::runtime::MutexResult::NotOwner,
              "Double release rejected");
        check(mutex.tryLock() == gxos::runtime::MutexResult::Acquired,
              "Try-lock after release");
        check(mutex.unlock() == gxos::runtime::MutexResult::Released,
              "Try-lock release");
        check(mutex.destroy() == gxos::runtime::MutexStatus::Ok,
              "Quiescent destroy");
        check(mutex.lock() == gxos::runtime::MutexResult::Destroyed,
              "Destroyed lock rejected");
        check(mutex.destroy() == gxos::runtime::MutexStatus::Destroyed,
              "Repeated destroy reported");
    }

    void recursiveTests() {
        gxos::runtime::Mutex mutex(gxos::runtime::MutexMode::Recursive);
        check(mutex.isInitialized(), "Recursive initialize");
        check(mutex.lock() == gxos::runtime::MutexResult::Acquired,
              "Recursive first acquire");
        for (gxos_mutex_uint32 i = 1; i < gxos::runtime::kMutexMaximumRecursion; ++i) {
            if (mutex.lock() != gxos::runtime::MutexResult::Acquired) {
                ++g_failures;
                break;
            }
        }
        check(mutex.lock() == gxos::runtime::MutexResult::RecursionLimit,
              "Recursion overflow bounded");
        bool released = true;
        for (gxos_mutex_uint32 i = 0; i < gxos::runtime::kMutexMaximumRecursion; ++i) {
            released = released && mutex.unlock() == gxos::runtime::MutexResult::Released;
        }
        check(released, "Recursive releases");
        check(mutex.destroy() == gxos::runtime::MutexStatus::Ok,
              "Recursive destroy");
    }

    void fifoWaiter(gxos::runtime::Mutex* mutex,
                    int id,
                    std::atomic<bool>* gate,
                    std::atomic<bool>* requested,
                    std::array<int, 4>* order,
                    std::atomic<int>* resultCount) {
        while (!gate->load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        requested->store(true, std::memory_order_release);
        const gxos::runtime::MutexResult result = mutex->lock();
        if (result == gxos::runtime::MutexResult::Acquired) {
            const int position = resultCount->fetch_add(1, std::memory_order_acq_rel);
            (*order)[static_cast<std::size_t>(position)] = id;
            (void)mutex->unlock();
        }
    }

    void contentionTests() {
        gxos::runtime::Mutex mutex(gxos::runtime::MutexMode::NonRecursive);
        check(mutex.lock() == gxos::runtime::MutexResult::Acquired,
              "Contention owner acquire");

        std::array<std::thread, 4> waiters;
        std::array<std::atomic<bool>, 4> gates;
        std::array<std::atomic<bool>, 4> requested;
        std::array<int, 4> order = { -1, -1, -1, -1 };
        std::atomic<int> resultCount{0};
        for (int i = 0; i < 4; ++i) {
            gates[static_cast<std::size_t>(i)].store(false);
            requested[static_cast<std::size_t>(i)].store(false);
        }
        // Each waiter is admitted to lock() in order while the owner keeps
        // the mutex held.  The small yield window lets the ticket be
        // published before the next waiter is admitted.
        for (int i = 0; i < 4; ++i) {
            waiters[static_cast<std::size_t>(i)] = std::thread(
                fifoWaiter, &mutex, i, &gates[static_cast<std::size_t>(i)],
                &requested[static_cast<std::size_t>(i)], &order,
                &resultCount);
            gates[static_cast<std::size_t>(i)].store(true, std::memory_order_release);
            while (!requested[static_cast<std::size_t>(i)].load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        std::atomic<int> tryResult{-1};
        std::thread tryContender([&]() {
            tryResult.store(static_cast<int>(mutex.tryLock()), std::memory_order_release);
        });
        tryContender.join();
        check(tryResult.load(std::memory_order_acquire) ==
                  static_cast<int>(gxos::runtime::MutexResult::WouldBlock),
              "Try-lock contended without parking");
        check(mutex.destroy() == gxos::runtime::MutexStatus::Busy,
              "Destroy with waiters rejected");
        check(mutex.unlock() == gxos::runtime::MutexResult::Released,
              "Contention owner release");
        for (std::thread& waiter : waiters) {
            waiter.join();
        }
        check(resultCount.load() == 4, "All waiters acquired");
        bool fifo = resultCount.load() == 4;
        for (int i = 0; i < 4 && fifo; ++i) {
            fifo = order[static_cast<std::size_t>(i)] == i;
        }
        check(fifo, "FIFO waiter order");
        check(mutex.destroy() == gxos::runtime::MutexStatus::Ok,
              "Contention destroy after quiescence");
    }

    void nonOwnerUnlock(gxos::runtime::Mutex* mutex,
                        std::atomic<int>* result) {
        *result = static_cast<int>(mutex->unlock());
    }

    void exitWhileOwned(gxos::runtime::Mutex* mutex,
                        std::atomic<int>* notification) {
        if (mutex->lock() == gxos::runtime::MutexResult::Acquired) {
            *notification = static_cast<int>(mutex->notifyOwnerExit());
        }
    }

    void lifetimeAndExitTests() {
        gxos::runtime::Mutex mutex(gxos::runtime::MutexMode::NonRecursive);
        check(mutex.lock() == gxos::runtime::MutexResult::Acquired,
              "Ownership error owner acquire");
        std::atomic<int> nonOwnerResult{-1};
        std::thread nonOwner(nonOwnerUnlock, &mutex, &nonOwnerResult);
        nonOwner.join();
        check(nonOwnerResult.load() == static_cast<int>(gxos::runtime::MutexResult::NotOwner),
              "Non-owner release rejected");
        check(mutex.destroy() == gxos::runtime::MutexStatus::Busy,
              "Destroy while owned rejected");
        check(mutex.unlock() == gxos::runtime::MutexResult::Released,
              "Ownership error release");
        check(mutex.destroy() == gxos::runtime::MutexStatus::Ok,
              "Ownership error destroy");

        // This static object intentionally remains orphaned until process
        // teardown.  The worker reports its exit while owning it; the mutex
        // stays locked and cannot be destroyed, proving there is no silent
        // abandoned-owner release.
        static gxos::runtime::Mutex orphan(gxos::runtime::MutexMode::NonRecursive);
        std::atomic<int> notification{-1};
        std::thread exiting(exitWhileOwned, &orphan, &notification);
        exiting.join();
        check(notification.load() == static_cast<int>(gxos::runtime::MutexStatus::OwnerExitViolation),
              "Owner-exit violation detected");
        check(orphan.tryLock() == gxos::runtime::MutexResult::WouldBlock,
              "Orphaned owner is not silently released");
        check(orphan.destroy() == gxos::runtime::MutexStatus::Busy,
              "Orphaned mutex remains busy");
    }
}

int main() {
    basicAndOwnershipTests();
    recursiveTests();
    contentionTests();
    lifetimeAndExitTests();
    std::cout << "[mutex-test] ALL_PASS: " << (g_failures == 0 ? "PASS" : "FAIL") << "\n";
    return g_failures == 0 ? 0 : 1;
}
