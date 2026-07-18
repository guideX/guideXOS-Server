#include "../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_critical_section_adapter.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace {
    using gxos::runtime::MutexResult;
    using gxos::runtime::MutexStatus;

    bool expect(bool value) { return value; }
}

int main() {
    using namespace guidexos::nativeaot;

    CriticalSectionHandle* handle = initializeCriticalSection();
    if (!expect(handle != nullptr) ||
        !expect(enterCriticalSection(handle) == MutexResult::Acquired) ||
        !expect(enterCriticalSection(handle) == MutexResult::Acquired) ||
        !expect(tryEnterCriticalSection(handle) == MutexResult::Acquired)) {
        return 1;
    }

    std::atomic<bool> started{false};
    std::atomic<int> workerResult{-1};
    std::thread worker([&]() {
        started.store(true, std::memory_order_release);
        workerResult.store(static_cast<int>(enterCriticalSection(handle)),
                           std::memory_order_release);
        if (workerResult.load(std::memory_order_acquire) ==
            static_cast<int>(MutexResult::Acquired)) {
            (void)leaveCriticalSection(handle);
        }
    });
    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (!expect(tryEnterCriticalSection(handle) == MutexResult::Acquired) ||
        !expect(leaveCriticalSection(handle) == MutexResult::Released) ||
        !expect(leaveCriticalSection(handle) == MutexResult::Released) ||
        !expect(leaveCriticalSection(handle) == MutexResult::Released) ||
        !expect(leaveCriticalSection(handle) == MutexResult::Released)) {
        worker.join();
        return 2;
    }
    worker.join();
    if (!expect(workerResult.load(std::memory_order_acquire) ==
                static_cast<int>(MutexResult::Acquired)) ||
        !expect(deleteCriticalSection(handle) == MutexStatus::Ok)) {
        return 3;
    }

    // This probe constructs only the inactive adapter.  It does not call any
    // runtime startup, finalizer, managed allocation, or collector entry.
    return 0;
}
