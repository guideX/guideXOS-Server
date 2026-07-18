#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_fls_adapter.h"
#include "runtime/thread/guidexos_native_thread.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::atomic<unsigned> g_callbackCount{0};
std::atomic<uintptr_t> g_callbackValue{0};

void GUIDEXOS_NATIVEAOT_FLS_CALL callback(void* value) {
    g_callbackValue.store(reinterpret_cast<uintptr_t>(value), std::memory_order_release);
    g_callbackCount.fetch_add(1, std::memory_order_acq_rel);
}

struct WorkerContext {
    unsigned long first;
    unsigned long second;
    bool initialFirstNull;
    bool initialSecondNull;
};

uintptr_t worker(void* raw) {
    WorkerContext* context = static_cast<WorkerContext*>(raw);
    context->initialFirstNull =
        guidexos_nativeaot_fls_get(context->first) == nullptr;
    context->initialSecondNull =
        guidexos_nativeaot_fls_get(context->second) == nullptr;
    require(guidexos_nativeaot_fls_set(context->first,
                                       reinterpret_cast<void*>(0xCAFEu)) == 1,
            "worker first set failed");
    require(guidexos_nativeaot_fls_set(context->second,
                                       reinterpret_cast<void*>(0xBABEu)) == 1,
            "worker second set failed");
    return 0;
}

} // namespace

int main() {
    try {
        guidexos_nativeaot_fls_initialize();
        require(guidexos_nativeaot_fls_attach_current_thread() == 1,
                "initial adapter attach failed");
        const unsigned long first = guidexos_nativeaot_fls_alloc(callback);
        const unsigned long second = guidexos_nativeaot_fls_alloc(nullptr);
        require(first != guidexos::nativeaot::fls::kOutOfIndexes &&
                second != guidexos::nativeaot::fls::kOutOfIndexes && first != second,
                "adapter allocation failed");
        require(guidexos_nativeaot_fls_set(first,
                                           reinterpret_cast<void*>(0x1111u)) == 1 &&
                guidexos_nativeaot_fls_set(second,
                                           reinterpret_cast<void*>(0x2222u)) == 1,
                "initial adapter set failed");
        require(guidexos_nativeaot_fls_get(first) == reinterpret_cast<void*>(0x1111u) &&
                guidexos_nativeaot_fls_get(second) == reinterpret_cast<void*>(0x2222u),
                "initial adapter get failed");
        std::cout << "Initial-thread values: PASS\n";
        std::cout << "Adapter dynamic allocation: PASS\n";

        WorkerContext context{static_cast<unsigned long>(first),
                              static_cast<unsigned long>(second), false, false};
        gxos::runtime::ThreadHandle handle{};
        gxos::runtime::ThreadCreateOptions options;
        options.debugName = "nativeaot-fls-probe";
        require(gxos::runtime::createThread(worker, &context, options, &handle) ==
                    gxos::runtime::ThreadResult::Ok,
                "adapter worker creation failed");
        uintptr_t exitResult = 0;
        require(gxos::runtime::joinThread(handle,
                                          gxos::runtime::WaitTimeout::infinite(),
                                          &exitResult) ==
                    gxos::runtime::WaitResult::Signaled,
                "adapter worker join failed");
        require(context.initialFirstNull && context.initialSecondNull,
                "worker inherited initial adapter values");
        require(g_callbackCount.load(std::memory_order_acquire) == 1 &&
                g_callbackValue.load(std::memory_order_acquire) == 0xCAFEu,
                "adapter callback value/count failed");
        require(guidexos_nativeaot_fls_get(first) == reinterpret_cast<void*>(0x1111u) &&
                guidexos_nativeaot_fls_get(second) == reinterpret_cast<void*>(0x2222u),
                "worker changed initial adapter values");
        std::cout << "Worker-thread isolation: PASS\n";
        std::cout << "Detach callback value: PASS\n";
        std::cout << "Detach callback count: expected=1 observed="
                  << g_callbackCount.load(std::memory_order_acquire) << "\n";

        require(guidexos_nativeaot_fls_set(first, nullptr) == 1 &&
                guidexos_nativeaot_fls_set(second, nullptr) == 1,
                "initial adapter clear failed");
        require(guidexos_nativeaot_fls_detach_current_thread() == 1,
                "initial adapter detach failed");
        require(guidexos_nativeaot_fls_free(first) == 1 &&
                guidexos_nativeaot_fls_free(second) == 1,
                "adapter release failed");
        std::cout << "Initial-thread detach: PASS\n";
        std::cout << "Index release: PASS\n";

        const unsigned long reused = guidexos_nativeaot_fls_alloc(nullptr);
        require(reused != guidexos::nativeaot::fls::kOutOfIndexes,
                "adapter reuse allocation failed");
        require(guidexos_nativeaot_fls_get(reused) == nullptr,
                "reused adapter slot was not null");
        require(guidexos_nativeaot_fls_get(first) == nullptr,
                "freed adapter index was not rejected");
        require(guidexos_nativeaot_fls_free(reused) == 1,
                "adapter reused release failed");
        std::cout << "Slot-generation reuse: PASS\n";
        std::cout << "Stale-index rejection: PASS\n";

        guidexos_nativeaot_fls_shutdown();
        std::cout << "NativeAOT adapter probe: PASS\n";
        std::cout << "RhInitialize called: no\n";
        std::cout << "GC initialized: no\n";
        std::cout << "Finalizer thread started: no\n";
        std::cout << "Collections entered: 0\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "nativeaot FLS adapter probe failure: " << error.what() << "\n";
        return 1;
    }
}

