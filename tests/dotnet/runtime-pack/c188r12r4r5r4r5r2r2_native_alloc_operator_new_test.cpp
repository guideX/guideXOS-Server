#include <new>
#include <stdint.h>

#include "guidexos_nativeaot_native_alloc_record.h"
#include "guidexos_nativeaot_pal_contract.h"

extern "C" void* memset(void* destination, int value, size_t size) {
    auto* bytes = static_cast<unsigned char*>(destination);
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = static_cast<unsigned char>(value);
    }
    return destination;
}

namespace {

alignas(16) uint8_t g_backing[128] = {};
bool g_rejectAllocation = false;
uintptr_t g_lastSize = 0u;
uint32_t g_lastType = 0u;
uint32_t g_lastProtection = 0u;
uint32_t g_callbackCount = 0u;

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL currentThread(void*) { return 1u; }

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL stackBounds(
    void*, uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    uintptr_t marker = 0u;
    const uintptr_t address = reinterpret_cast<uintptr_t>(&marker);
    if (low != nullptr) *low = address - 16u;
    if (high != nullptr) *high = address + 16u;
    if (current != nullptr) *current = address;
    return 0;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL counter(void*) { return 1u; }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL frequency(void*) { return 1u; }
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL sleepMs(void*, uint32_t) { return 0; }
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL yieldThread(void*) { return 0; }

void* GUIDEXOS_NATIVEAOT_PAL_CALL virtualAlloc(
    void*, void*, uintptr_t size, uint32_t type, uint32_t protection) {
    ++g_callbackCount;
    g_lastSize = size;
    g_lastType = type;
    g_lastProtection = protection;
    return g_rejectAllocation ? nullptr : g_backing;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL virtualFree(
    void*, void*, uintptr_t, uint32_t) { return 0; }

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL virtualProtect(
    void*, void*, uintptr_t, uint32_t, uint32_t* oldProtection) {
    if (oldProtection != nullptr) *oldProtection = 4u;
    return 1;
}

bool installTestPal() {
    guidexos_nativeaot_pal_hooks hooks = {};
    hooks.size = sizeof(hooks);
    hooks.abi_version = GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
    hooks.current_thread_id = currentThread;
    hooks.stack_bounds = stackBounds;
    hooks.counter = counter;
    hooks.frequency = frequency;
    hooks.sleep_milliseconds = sleepMs;
    hooks.yield = yieldThread;
    hooks.virtual_alloc = virtualAlloc;
    hooks.virtual_free = virtualFree;
    hooks.virtual_protect = virtualProtect;
    return guidexos_nativeaot_pal_install_hooks(&hooks) == 0;
}

} // namespace

int main() {
    (void)guidexos_nativeaot_pal_install_hooks(nullptr);

    if (!installTestPal()) return 1;
    GuideXosNativeAotNativeAllocBegin(GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL, 8u, 8u);
    void* const success = ::operator new(static_cast<size_t>(8u), std::nothrow);
    if (success != static_cast<void*>(g_backing + 16u) || g_lastSize != 24u ||
        g_lastType != 0x3000u || g_lastProtection != 0x04u || g_callbackCount != 1u) {
        return 2;
    }
    GuideXosNativeAotNativeAllocConstructorEntered();
    GuideXosNativeAotNativeAllocComplete(success);
    if (GuideXosNativeAotNativeAllocGetAttemptField(
            0u, GX_NATIVE_ALLOC_FIELD_FINAL_POINTER_NON_NULL) != 1u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            0u, GX_NATIVE_ALLOC_FIELD_CONSTRUCTOR_ENTERED) != 1u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            0u, GX_NATIVE_ALLOC_FIELD_PAL_STATE) != GX_NATIVE_ALLOC_PAL_STATE_READY) {
        return 3;
    }

    g_rejectAllocation = true; // Synthetic PAL callback refusal for the hosted probe.
    GuideXosNativeAotNativeAllocBegin(GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL, 8u, 8u);
    void* const failure = ::operator new(static_cast<size_t>(8u), std::nothrow);
    if (failure != nullptr || g_lastSize != 24u || g_callbackCount != 2u) return 4;
    GuideXosNativeAotNativeAllocComplete(nullptr);
    if (GuideXosNativeAotNativeAllocGetAttemptField(
            1u, GX_NATIVE_ALLOC_FIELD_FINAL_POINTER_NON_NULL) != 0u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            1u, GX_NATIVE_ALLOC_FIELD_CONSTRUCTOR_ENTERED) != 0u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            1u, GX_NATIVE_ALLOC_FIELD_FAILURE_BRANCH) != GX_NATIVE_ALLOC_FAILURE_PAL_RETURNED_NULL ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            1u, GX_NATIVE_ALLOC_FIELD_FAILURE_STATUS_SOURCE) != GX_NATIVE_ALLOC_STATUS_UNAVAILABLE) {
        return 5;
    }

    (void)guidexos_nativeaot_pal_install_hooks(nullptr);
    GuideXosNativeAotNativeAllocBegin(GX_NATIVE_ALLOC_SITE_GC_EVENT_IMPL, 8u, 8u);
    void* const uninitialized = ::operator new(static_cast<size_t>(8u), std::nothrow);
    if (uninitialized != nullptr) return 6;
    GuideXosNativeAotNativeAllocComplete(nullptr);
    if (GuideXosNativeAotNativeAllocGetAttemptField(
            2u, GX_NATIVE_ALLOC_FIELD_PAL_STATE) != GX_NATIVE_ALLOC_PAL_STATE_NOT_READY ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            2u, GX_NATIVE_ALLOC_FIELD_PAL_CALLBACK_ENTERED) != 0u ||
        GuideXosNativeAotNativeAllocGetAttemptField(
            2u, GX_NATIVE_ALLOC_FIELD_CONSTRUCTOR_ENTERED) != 0u) {
        return 7;
    }
    return 0;
}
