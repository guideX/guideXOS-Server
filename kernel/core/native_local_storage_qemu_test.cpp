#include "include/kernel/native_local_storage_qemu_test.h"

#if defined(GXOS_NATIVE_LOCAL_STORAGE_QEMU_TEST)

#include "include/kernel/serial_debug.h"
#include "runtime/local_storage/guidexos_local_storage.h"
#include "runtime/thread/guidexos_native_thread.h"

namespace kernel {
namespace native_local_storage_qemu_test {
namespace {

using gxos::runtime::LocalStorageIndex;
using gxos::runtime::LocalStorageResult;
using gxos::runtime::ThreadCreateOptions;
using gxos::runtime::ThreadHandle;
using gxos::runtime::ThreadResult;
using gxos::runtime::WaitResult;
using gxos::runtime::WaitTimeout;

bool g_all_passed = true;
uint32_t g_callback_count = 0;
uintptr_t g_callback_value = 0;
LocalStorageIndex g_callback_index{};
LocalStorageResult g_callback_set_result = LocalStorageResult::Success;
gxos::runtime::Event g_detachedCallbackDone;

void status(const char* name, bool passed) {
    kernel::serial::puts("[native-local-storage-test] ");
    kernel::serial::puts(name);
    kernel::serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) {
        g_all_passed = false;
    }
}

void callback(void* value) {
    ++g_callback_count;
    g_callback_value = reinterpret_cast<uintptr_t>(value);
    g_callback_set_result = gxos::runtime::setLocalStorageValue(
        g_callback_index, reinterpret_cast<void*>(0xDEADu));
    if (g_callback_value == 0xD00Du) {
        (void)g_detachedCallbackDone.signal();
    }
}

struct WorkerContext {
    LocalStorageIndex index;
    void* value;
    bool initial_null;
    bool isolated;
};

uintptr_t worker_entry(void* raw) {
    WorkerContext* context = static_cast<WorkerContext*>(raw);
    if (context == nullptr) {
        return 0;
    }
    void* initial = nullptr;
    context->initial_null =
        gxos::runtime::getLocalStorageValue(context->index, &initial) ==
            LocalStorageResult::Success && initial == nullptr;
    context->isolated =
        gxos::runtime::setLocalStorageValue(context->index, context->value) ==
            LocalStorageResult::Success;
    if (context->isolated) {
        void* current = nullptr;
        context->isolated =
            gxos::runtime::getLocalStorageValue(context->index, &current) ==
                LocalStorageResult::Success && current == context->value;
    }
    return context->isolated ? 1 : 0;
}

bool runWorker(WorkerContext* context) {
    ThreadHandle handle{};
    ThreadCreateOptions options;
    options.debugName = "qemu-local-storage";
    if (gxos::runtime::createThread(worker_entry, context, options, &handle) !=
        ThreadResult::Ok) {
        return false;
    }
    uintptr_t result = 0;
    return gxos::runtime::joinThread(handle, WaitTimeout::infinite(), &result) ==
        WaitResult::Signaled && result == 1;
}

bool runDetachedWorker(WorkerContext* context) {
    ThreadHandle handle{};
    ThreadCreateOptions options;
    options.debugName = "qemu-local-storage-detached";
    options.detached = true;
    if (gxos::runtime::createThread(worker_entry, context, options, &handle) !=
        ThreadResult::Ok) {
        return false;
    }
    return g_detachedCallbackDone.wait(WaitTimeout::infinite()) ==
        WaitResult::Signaled;
}

} // namespace

void run() {
    kernel::serial::puts("[native-local-storage-test] BEGIN\n");
    g_all_passed = true;
    g_callback_count = 0;
    g_callback_value = 0;
    g_callback_set_result = LocalStorageResult::Success;
    if (!g_detachedCallbackDone.isInitialized()) {
        (void)g_detachedCallbackDone.initialize(
            gxos::runtime::EventMode::ManualReset, false);
    }
    else {
        (void)g_detachedCallbackDone.reset();
    }

    const bool initialized =
        gxos::runtime::initializeLocalStorage() == LocalStorageResult::Success &&
        gxos::runtime::attachLocalStorage() == LocalStorageResult::Success;
    status("Manager initialization", initialized);

    LocalStorageIndex callback_index{};
    LocalStorageIndex ordinary_index{};
    const bool allocated = initialized &&
        gxos::runtime::allocateLocalStorageIndex(callback, &callback_index) ==
            LocalStorageResult::Success &&
        gxos::runtime::allocateLocalStorageIndex(nullptr, &ordinary_index) ==
            LocalStorageResult::Success;
    g_callback_index = callback_index;
    status("Index allocation", allocated);

    bool initial_values = false;
    bool worker_values = false;
    bool worker_callback = false;
    bool detached_cleanup = false;
    bool release_callback = false;
    bool multiple_values = false;
    if (allocated) {
        initial_values =
            gxos::runtime::setLocalStorageValue(callback_index,
                reinterpret_cast<void*>(0x1111u)) == LocalStorageResult::Success &&
            gxos::runtime::setLocalStorageValue(ordinary_index,
                reinterpret_cast<void*>(0x2222u)) == LocalStorageResult::Success;
        WorkerContext context{callback_index, reinterpret_cast<void*>(0xCAFEu),
                              false, false};
        WorkerContext reused_context{callback_index, reinterpret_cast<void*>(0xBEEFu),
                                     false, false};
        void* initial = nullptr;
        worker_values = runWorker(&context) && context.initial_null &&
            context.isolated && runWorker(&reused_context) &&
            reused_context.initial_null && reused_context.isolated;
        worker_callback = g_callback_count == 2 &&
            g_callback_value == 0xBEEFu &&
            g_callback_set_result == LocalStorageResult::Busy;
        WorkerContext detached_context{callback_index,
                                       reinterpret_cast<void*>(0xD00Du),
                                       false, false};
        detached_cleanup = runDetachedWorker(&detached_context) &&
            detached_context.initial_null && detached_context.isolated &&
            g_callback_count == 3 && g_callback_value == 0xD00Du;
        multiple_values =
            gxos::runtime::getLocalStorageValue(ordinary_index, &initial) ==
                LocalStorageResult::Success && initial == reinterpret_cast<void*>(0x2222u);
        const LocalStorageResult release_result =
            gxos::runtime::releaseLocalStorageIndex(callback_index);
        release_callback =
            (release_result == LocalStorageResult::Success ||
             release_result == LocalStorageResult::CallbackFailed) &&
            g_callback_count == 4 && g_callback_value == 0x1111u &&
            g_callback_set_result == LocalStorageResult::Busy;
    }
    status("Initial-thread value", initial_values);
    status("Worker-thread isolation", worker_values);
    status("Multiple-index isolation", multiple_values);
    status("Detach callback value", worker_callback);
    status("Detach callback count", worker_callback);
    status("Callback repopulation policy", worker_callback);
    status("Detached-thread cleanup", detached_cleanup);
    status("Index release callback", release_callback);
    status("TCB reuse clearing", worker_values);

    LocalStorageIndex extra[gxos::runtime::kLocalStorageCapacity] = {};
    uint32_t extra_count = 0;
    if (allocated) {
        (void)gxos::runtime::setLocalStorageValue(ordinary_index, nullptr);
        while (extra_count < gxos::runtime::kLocalStorageCapacity &&
               gxos::runtime::allocateLocalStorageIndex(nullptr,
                   &extra[extra_count]) == LocalStorageResult::Success) {
            ++extra_count;
        }
    }
    LocalStorageIndex exhausted{};
    const bool exhaustedPass = allocated && extra_count ==
        gxos::runtime::kLocalStorageCapacity - 1 &&
        gxos::runtime::allocateLocalStorageIndex(nullptr, &exhausted) ==
            LocalStorageResult::Exhausted;
    status("Index exhaustion", exhaustedPass);

    const LocalStorageIndex stale = extra_count == 0 ? LocalStorageIndex{} : extra[0];
    bool reuse = false;
    if (extra_count != 0) {
        reuse = gxos::runtime::releaseLocalStorageIndex(stale) ==
            LocalStorageResult::Success;
        LocalStorageIndex reused{};
        reuse = reuse &&
            gxos::runtime::allocateLocalStorageIndex(nullptr, &reused) ==
                LocalStorageResult::Success && reused.slot == stale.slot &&
            reused.generation != stale.generation;
        void* value = nullptr;
        reuse = reuse &&
            gxos::runtime::getLocalStorageValue(stale, &value) ==
                LocalStorageResult::StaleIndex;
        (void)gxos::runtime::releaseLocalStorageIndex(reused);
    }
    status("Slot-generation reuse", reuse);
    status("Stale-index rejection", reuse);

    bool detached = false;
    if (allocated) {
        (void)gxos::runtime::releaseLocalStorageIndex(ordinary_index);
        for (uint32_t i = 1; i < extra_count; ++i) {
            (void)gxos::runtime::releaseLocalStorageIndex(extra[i]);
        }
        detached = gxos::runtime::detachLocalStorage() ==
            LocalStorageResult::Success;
        status("Initial-thread detach", detached);
        const bool shutdown = detached &&
            gxos::runtime::shutdownLocalStorage() == LocalStorageResult::Success;
        status("Runtime shutdown cleanup", shutdown);
        status("Process/runtime teardown", shutdown);
        status("Leak check", shutdown);
    }
    else {
        status("Initial-thread detach", false);
        status("Runtime shutdown cleanup", false);
    }

    kernel::serial::puts(g_all_passed
        ? "[native-local-storage-test] ALL_PASS\n"
        : "[native-local-storage-test] ALL_FAIL\n");
}

} // namespace native_local_storage_qemu_test
} // namespace kernel

#endif
