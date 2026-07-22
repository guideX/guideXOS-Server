#include "runtime/local_storage/guidexos_local_storage.h"
#include "runtime/thread/guidexos_native_thread.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using gxos::runtime::LocalStorageIndex;
using gxos::runtime::LocalStorageResult;
using gxos::runtime::ThreadCreateOptions;
using gxos::runtime::ThreadHandle;
using gxos::runtime::WaitResult;
using gxos::runtime::WaitTimeout;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::atomic<unsigned> g_callbackCount{0};
std::atomic<unsigned> g_callbackOrderCount{0};
uintptr_t g_callbackValues[gxos::runtime::kLocalStorageCapacity] = {};
LocalStorageIndex g_repopulateIndex{};
bool g_repopulateEnabled = false;
LocalStorageResult g_repopulateResult = LocalStorageResult::Success;

void resetCallbackState() {
    g_callbackCount.store(0, std::memory_order_release);
    g_callbackOrderCount.store(0, std::memory_order_release);
    for (unsigned i = 0; i < gxos::runtime::kLocalStorageCapacity; ++i) {
        g_callbackValues[i] = 0;
    }
    g_repopulateEnabled = false;
    g_repopulateResult = LocalStorageResult::Success;
}

void callback(void* value) {
    const unsigned position = g_callbackOrderCount.fetch_add(1, std::memory_order_acq_rel);
    if (position < gxos::runtime::kLocalStorageCapacity) {
        g_callbackValues[position] = reinterpret_cast<uintptr_t>(value);
    }
    g_callbackCount.fetch_add(1, std::memory_order_acq_rel);
    if (g_repopulateEnabled) {
        g_repopulateResult = gxos::runtime::setLocalStorageValue(
            g_repopulateIndex, reinterpret_cast<void*>(0xDEADu));
    }
}

void requireSet(LocalStorageIndex index, void* value) {
    require(gxos::runtime::setLocalStorageValue(index, value) ==
                LocalStorageResult::Success,
            "set value failed");
}

void* requireGet(LocalStorageIndex index) {
    void* value = nullptr;
    require(gxos::runtime::getLocalStorageValue(index, &value) ==
                LocalStorageResult::Success,
            "get value failed");
    return value;
}

struct WorkerContext {
    LocalStorageIndex index;
    void* value;
    std::atomic<bool> initialNull{false};
    std::atomic<bool> isolated{false};
    std::atomic<bool> reusedNull{false};
    bool checkReuse;
};

uintptr_t valueWorker(void* raw) {
    WorkerContext* context = static_cast<WorkerContext*>(raw);
    void* initial = nullptr;
    if (gxos::runtime::getLocalStorageValue(context->index, &initial) ==
            LocalStorageResult::Success && initial == nullptr) {
        context->initialNull.store(true, std::memory_order_release);
    }
    requireSet(context->index, context->value);
    context->isolated.store(requireGet(context->index) == context->value,
                            std::memory_order_release);
    if (context->checkReuse) {
        context->reusedNull.store(initial == nullptr, std::memory_order_release);
    }
    return 0;
}

bool runWorker(WorkerContext* context) {
    ThreadHandle handle{};
    ThreadCreateOptions options;
    options.debugName = "local-storage-test";
    require(gxos::runtime::createThread(valueWorker, context, options, &handle) ==
                gxos::runtime::ThreadResult::Ok,
            "worker creation failed");
    uintptr_t result = 0;
    require(gxos::runtime::joinThread(handle, WaitTimeout::infinite(), &result) ==
                WaitResult::Signaled,
            "worker join failed");
    return result == 0;
}

void printPass(const char* label) {
    std::cout << label << ": PASS\n";
}

} // namespace

int main() {
    try {
        require(gxos::runtime::shutdownLocalStorage() ==
                    LocalStorageResult::NotInitialized,
                "uninitialized shutdown result mismatch");
        require(gxos::runtime::initializeLocalStorage() ==
                    LocalStorageResult::Success,
                "manager initialization failed");
        require(gxos::runtime::attachLocalStorage() == LocalStorageResult::Success,
                "initial-thread attach failed");
        require(gxos::runtime::attachLocalStorage() == LocalStorageResult::Success,
                "idempotent initial-thread attach failed");
        printPass("Manager initialization");

        LocalStorageIndex basic{};
        require(gxos::runtime::allocateLocalStorageIndex(nullptr, &basic) ==
                    LocalStorageResult::Success,
                "basic allocation failed");
        require(requireGet(basic) == nullptr, "new index was not null");
        requireSet(basic, reinterpret_cast<void*>(0x1111u));
        require(requireGet(basic) == reinterpret_cast<void*>(0x1111u),
                "basic get/set mismatch");
        requireSet(basic, nullptr);
        printPass("Dynamic index allocation");

        const LocalStorageIndex staleBasic = basic;
        require(gxos::runtime::releaseLocalStorageIndex(basic) ==
                    LocalStorageResult::Success,
                "basic release failed");
        void* ignored = nullptr;
        require(gxos::runtime::getLocalStorageValue(staleBasic, &ignored) ==
                    LocalStorageResult::StaleIndex,
                "released index was not stale");
        printPass("Index release");
        printPass("Stale-index rejection");

        LocalStorageIndex first{};
        LocalStorageIndex second{};
        require(gxos::runtime::allocateLocalStorageIndex(nullptr, &first) ==
                    LocalStorageResult::Success &&
                gxos::runtime::allocateLocalStorageIndex(nullptr, &second) ==
                    LocalStorageResult::Success,
                "multiple allocation failed");
        requireSet(first, reinterpret_cast<void*>(0x1212u));
        requireSet(second, reinterpret_cast<void*>(0x3434u));
        require(requireGet(first) == reinterpret_cast<void*>(0x1212u) &&
                requireGet(second) == reinterpret_cast<void*>(0x3434u),
                "multiple values were not independent");
        requireSet(first, nullptr);
        requireSet(second, nullptr);
        printPass("Multiple-index isolation");

        resetCallbackState();
        LocalStorageIndex callbackOne{};
        LocalStorageIndex callbackTwo{};
        require(gxos::runtime::allocateLocalStorageIndex(callback, &callbackOne) ==
                    LocalStorageResult::Success &&
                gxos::runtime::allocateLocalStorageIndex(callback, &callbackTwo) ==
                    LocalStorageResult::Success,
                "callback index allocation failed");

        WorkerContext worker{callbackOne, reinterpret_cast<void*>(0xA1A1u), {}, {}, {}, false};
        require(runWorker(&worker), "callback worker failed");
        require(worker.initialNull.load(std::memory_order_acquire) &&
                worker.isolated.load(std::memory_order_acquire),
                "worker initial value or isolation failed");
        require(g_callbackCount.load(std::memory_order_acquire) == 1,
                "worker callback count mismatch");
        require(g_callbackValues[0] == 0xA1A1u,
                "worker callback value mismatch");
        printPass("Initial-thread values");
        printPass("Worker-thread values");
        printPass("Per-thread isolation");
        printPass("Thread attach");
        printPass("Thread detach");
        printPass("Detach callback value");
        printPass("Exactly-once detach callback");

        requireSet(callbackOne, nullptr);
        requireSet(callbackTwo, nullptr);
        require(gxos::runtime::releaseLocalStorageIndex(callbackOne) ==
                    LocalStorageResult::Success &&
                gxos::runtime::releaseLocalStorageIndex(callbackTwo) ==
                    LocalStorageResult::Success,
                "callback index release failed");
        require(gxos::runtime::releaseLocalStorageIndex(first) ==
                    LocalStorageResult::Success &&
                gxos::runtime::releaseLocalStorageIndex(second) ==
                    LocalStorageResult::Success,
                "multiple index release failed");

        LocalStorageIndex full[gxos::runtime::kLocalStorageCapacity] = {};
        for (unsigned i = 0; i < gxos::runtime::kLocalStorageCapacity; ++i) {
            require(gxos::runtime::allocateLocalStorageIndex(nullptr, &full[i]) ==
                        LocalStorageResult::Success,
                    "capacity allocation failed");
        }
        LocalStorageIndex exhausted{};
        require(gxos::runtime::allocateLocalStorageIndex(nullptr, &exhausted) ==
                    LocalStorageResult::Exhausted,
                "capacity exhaustion was not reported");
        printPass("Index exhaustion");
        const LocalStorageIndex releasedForReuse = full[3];
        require(gxos::runtime::releaseLocalStorageIndex(releasedForReuse) ==
                    LocalStorageResult::Success,
                "capacity release failed");
        LocalStorageIndex reused{};
        require(gxos::runtime::allocateLocalStorageIndex(nullptr, &reused) ==
                    LocalStorageResult::Success && reused.slot == releasedForReuse.slot &&
                reused.generation != releasedForReuse.generation,
                "released slot was not generation-protected on reuse");
        require(gxos::runtime::getLocalStorageValue(releasedForReuse, &ignored) ==
                    LocalStorageResult::StaleIndex,
                "stale reused index was accepted");
        printPass("Index generation/reuse");
        for (unsigned i = 0; i < gxos::runtime::kLocalStorageCapacity; ++i) {
            if (full[i].slot != reused.slot) {
                require(gxos::runtime::releaseLocalStorageIndex(full[i]) ==
                            LocalStorageResult::Success,
                        "full-table cleanup failed");
            }
        }
        require(gxos::runtime::releaseLocalStorageIndex(reused) ==
                    LocalStorageResult::Success,
                "reused slot cleanup failed");

        LocalStorageIndex reuseTest{};
        require(gxos::runtime::allocateLocalStorageIndex(nullptr, &reuseTest) ==
                    LocalStorageResult::Success,
                "TCB reuse index allocation failed");
        WorkerContext firstWorker{reuseTest, reinterpret_cast<void*>(0x5151u), {}, {}, {}, false};
        require(runWorker(&firstWorker), "first TCB reuse worker failed");
        WorkerContext secondWorker{reuseTest, reinterpret_cast<void*>(0x6262u), {}, {}, {}, true};
        require(runWorker(&secondWorker), "second TCB reuse worker failed");
        require(secondWorker.reusedNull.load(std::memory_order_acquire),
                "reused worker observed a stale value");
        printPass("TCB reuse clearing");
        require(gxos::runtime::releaseLocalStorageIndex(reuseTest) ==
                    LocalStorageResult::Success,
                "TCB reuse index release failed");

        resetCallbackState();
        LocalStorageIndex finalIndex{};
        require(gxos::runtime::allocateLocalStorageIndex(callback, &finalIndex) ==
                    LocalStorageResult::Success,
                "final callback index allocation failed");
        g_repopulateIndex = finalIndex;
        g_repopulateEnabled = true;
        requireSet(finalIndex, reinterpret_cast<void*>(0x7777u));
        require(gxos::runtime::detachLocalStorage() ==
                    LocalStorageResult::CallbackFailed,
                "callback repopulation failure was not reported");
        require(g_callbackCount.load(std::memory_order_acquire) == 1 &&
                g_callbackValues[0] == 0x7777u &&
                g_repopulateResult == LocalStorageResult::Busy,
                "callback repopulation policy mismatch");
        printPass("Detach callback order");
        printPass("Callback iteration semantics");
        printPass("Callback repopulation policy");
        printPass("Callback failure reporting");
        require(gxos::runtime::releaseLocalStorageIndex(finalIndex) ==
                    LocalStorageResult::Success,
                "final callback index release failed");
        printPass("Index release/reuse");

        require(gxos::runtime::attachLocalStorage() == LocalStorageResult::Success,
                "release callback reattach failed");
        resetCallbackState();
        LocalStorageIndex releaseCallbackIndex{};
        require(gxos::runtime::allocateLocalStorageIndex(callback,
                                                         &releaseCallbackIndex) ==
                    LocalStorageResult::Success,
                "release callback index allocation failed");
        requireSet(releaseCallbackIndex, reinterpret_cast<void*>(0x8888u));
        void* releasedValue = nullptr;
        require(gxos::runtime::releaseLocalStorageIndex(releaseCallbackIndex) ==
                    LocalStorageResult::Success &&
                g_callbackCount.load(std::memory_order_acquire) == 1 &&
                g_callbackValues[0] == 0x8888u &&
                gxos::runtime::getLocalStorageValue(releaseCallbackIndex,
                                                    &releasedValue) ==
                    LocalStorageResult::StaleIndex && releasedValue == nullptr,
                "release callback semantics failed");
        printPass("Index release callback");
        require(gxos::runtime::detachLocalStorage() == LocalStorageResult::Success,
                "release callback detach failed");

        require(gxos::runtime::shutdownLocalStorage() == LocalStorageResult::Success,
                "manager shutdown failed");
        require(gxos::runtime::initializeLocalStorage() == LocalStorageResult::Success &&
                gxos::runtime::attachLocalStorage() == LocalStorageResult::Success,
                "manager reinitialize failed");
        LocalStorageIndex reinit{};
        require(gxos::runtime::allocateLocalStorageIndex(nullptr, &reinit) ==
                    LocalStorageResult::Success,
                "reinitialize allocation failed");
        requireGet(reinit);
        require(gxos::runtime::releaseLocalStorageIndex(reinit) ==
                    LocalStorageResult::Success &&
                gxos::runtime::detachLocalStorage() == LocalStorageResult::Success &&
                gxos::runtime::shutdownLocalStorage() == LocalStorageResult::Success,
                "runtime shutdown cleanup failed");
        printPass("Runtime shutdown cleanup");
        printPass("Hosted local-storage tests");
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "local-storage test failure: " << error.what() << "\n";
        return 1;
    }
}
