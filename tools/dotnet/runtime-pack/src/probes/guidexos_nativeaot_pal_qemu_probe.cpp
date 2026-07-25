#include "../platform/guidexos_nativeaot_pal_contract.h"

#include <stdint.h>
#include <stddef.h>
#include <new>

namespace std {
const nothrow_t nothrow = nothrow_t();
}

void* operator new(size_t, const std::nothrow_t&) noexcept { return nullptr; }
void* operator new[](size_t, const std::nothrow_t&) noexcept { return nullptr; }
void operator delete(void*) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete(void*, size_t) noexcept {}
void operator delete[](void*, size_t) noexcept {}

extern "C" void* guidexos_memset(void* destination, int value, size_t count) {
    volatile unsigned char* bytes = static_cast<volatile unsigned char*>(destination);
    for (size_t index = 0; index < count; ++index) {
        bytes[index] = static_cast<unsigned char>(value);
    }
    return destination;
}

struct GuidexosNativeGxAppContext;
typedef int32_t (*GuidexosNativeLog)(GuidexosNativeGxAppContext*, const char*);
struct GuidexosNativeHost {
    uint32_t size;
    uint32_t version;
    GuidexosNativeLog log;
};
struct GuidexosNativeGxAppContext {
    uint32_t size;
    uint32_t apiVersion;
    const GuidexosNativeHost* host;
    void* userData;
};

extern "C" bool __stdcall PalInit();
extern "C" void __stdcall PalAttachThread(void* thread);
extern "C" bool __stdcall PalDetachThread(void* thread);
extern "C" uint64_t PalQueryPerformanceCounter();
extern "C" uint64_t PalQueryPerformanceFrequency();
extern "C" uint64_t __stdcall PalGetCurrentOSThreadId();
extern "C" uint64_t __stdcall PalGetTickCount64();
extern "C" void __stdcall PalSleep(uint32_t milliseconds);
extern "C" uint32_t __stdcall PalSwitchToThread();
extern "C" bool __stdcall PalGetMaximumStackBounds(void** low, void** high);
extern "C" bool __stdcall PalStartBackgroundWork(
    uint32_t (__stdcall* callback)(void*), void* context, int32_t highPriority);

namespace {

constexpr uint32_t kInvalidFls = 0xFFFFFFFFu;
constexpr uintptr_t kWorkerResult = static_cast<uintptr_t>(0x1234u);

uint32_t g_probeFlsIndex = kInvalidFls;
uint32_t g_probeDetachCount = 0;
void* g_probeDetachValue = nullptr;
uint8_t g_initialThreadToken = 0x11;
uint8_t g_workerThreadToken = 0x22;
uint8_t g_initialFlsValue = 0x31;
uint8_t g_workerFlsValue = 0x32;

struct WorkerProbeState {
    uint64_t initialThreadId;
    uint64_t workerThreadId;
    uintptr_t workerStackLow;
    uintptr_t workerStackHigh;
    uintptr_t workerStackCurrent;
    uintptr_t callbackResult;
    uint32_t callbackCount;
    bool initialFlsNull;
    bool stackValid;
    bool callbackContextValid;
};

WorkerProbeState g_worker = {};

namespace hosted {

constexpr uint32_t kCapacity = 8u;
void* g_values[kCapacity] = {};
bool g_active[kCapacity] = {};
guidexos_nativeaot_fls_detach_callback g_callbacks[kCapacity] = {};
uint64_t g_counter = 1000u;
uint8_t g_threadToken = 0x41;

uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsAlloc(
    void*, guidexos_nativeaot_fls_detach_callback callback) {
    for (uint32_t index = 0; index < kCapacity; ++index) {
        if (!g_active[index]) {
            g_active[index] = true;
            g_values[index] = nullptr;
            g_callbacks[index] = callback;
            return index;
        }
    }
    return kInvalidFls;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsFree(void*, uint32_t index) {
    if (index >= kCapacity || !g_active[index]) return -1;
    if (g_values[index] != nullptr && g_callbacks[index] != nullptr) {
        g_callbacks[index](g_values[index]);
    }
    g_values[index] = nullptr;
    g_callbacks[index] = nullptr;
    g_active[index] = false;
    return 0;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL flsGet(void*, uint32_t index) {
    return index < kCapacity && g_active[index] ? g_values[index] : nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsSet(void*, uint32_t index, void* value) {
    if (index >= kCapacity || !g_active[index]) return -1;
    g_values[index] = value;
    return 0;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL currentThreadId(void*) { return 1u; }

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL stackBounds(
    void*, uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    if (low == nullptr || high == nullptr || current == nullptr) return -1;
    const uintptr_t marker = reinterpret_cast<uintptr_t>(&g_threadToken);
    *low = marker - 4096u;
    *high = marker + 4096u;
    *current = marker;
    return 0;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL counter(void*) { return ++g_counter; }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL frequency(void*) { return 1000000u; }
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL sleepMilliseconds(void*, uint32_t) { return 0; }
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL yield(void*) { return 0; }
void* GUIDEXOS_NATIVEAOT_PAL_CALL virtualAlloc(
    void*, void* preferred, uintptr_t, uint32_t, uint32_t) { return preferred; }
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL virtualFree(
    void*, void*, uintptr_t, uint32_t) { return 0; }
int32_t GUIDEXOS_NATIVEAOT_PAL_CALL virtualProtect(
    void*, void*, uintptr_t, uint32_t, uint32_t* oldProtection) {
    if (oldProtection != nullptr) *oldProtection = 0;
    return 0;
}

void GUIDEXOS_NATIVEAOT_PAL_CALL detach(void*) {}

int32_t run(GuidexosNativeGxAppContext* context) {
    guidexos_nativeaot_fls_hooks fls{};
    fls.size = sizeof(fls);
    fls.abi_version = GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
    fls.alloc = flsAlloc;
    fls.free_index = flsFree;
    fls.get = flsGet;
    fls.set = flsSet;

    guidexos_nativeaot_pal_hooks hooks{};
    hooks.size = sizeof(hooks);
    hooks.abi_version = GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
    hooks.current_thread_id = currentThreadId;
    hooks.stack_bounds = stackBounds;
    hooks.counter = counter;
    hooks.frequency = frequency;
    hooks.sleep_milliseconds = sleepMilliseconds;
    hooks.yield = yield;
    hooks.virtual_alloc = virtualAlloc;
    hooks.virtual_free = virtualFree;
    hooks.virtual_protect = virtualProtect;

    if (guidexos_nativeaot_pal_install_fls_hooks(&fls) != 0 ||
        guidexos_nativeaot_pal_install_hooks(&hooks) != 0) return -30;
    if (!PalInit()) return -31;
    PalAttachThread(&g_threadToken);
    const uint32_t index = guidexos_nativeaot_pal_fls_alloc(detach);
    void* value = reinterpret_cast<void*>(0x5151u);
    void* low = nullptr;
    void* high = nullptr;
    const bool ok = index != kInvalidFls &&
        guidexos_nativeaot_pal_fls_set(index, value) == 0 &&
        guidexos_nativeaot_pal_fls_get(index) == value &&
        PalGetCurrentOSThreadId() == 1u &&
        PalGetMaximumStackBounds(&low, &high) && low < high &&
        PalQueryPerformanceFrequency() != 0 &&
        PalQueryPerformanceCounter() != 0 && PalGetTickCount64() != 0;
    if (index != kInvalidFls) (void)guidexos_nativeaot_pal_fls_free(index);
    const bool detached = PalDetachThread(&g_threadToken);
    (void)guidexos_nativeaot_pal_fls_free(0u);
    (void)guidexos_nativeaot_pal_install_hooks(nullptr);
    (void)guidexos_nativeaot_pal_install_fls_hooks(nullptr);
    if (!ok || !detached) return -32;
    return context->host->log(context, "NativeAOT PAL PE bridge probe completed");
}

} // namespace hosted

void __stdcall probeDetachCallback(void* value) {
    ++g_probeDetachCount;
    g_probeDetachValue = value;
}

uint32_t __stdcall workerCallback(void* raw) {
    WorkerProbeState* state = static_cast<WorkerProbeState*>(raw);
    if (state == nullptr) return 0;
    ++state->callbackCount;
    state->workerThreadId = PalGetCurrentOSThreadId();

    void* low = nullptr;
    void* high = nullptr;
    state->stackValid = PalGetMaximumStackBounds(&low, &high);
    state->workerStackLow = reinterpret_cast<uintptr_t>(low);
    state->workerStackHigh = reinterpret_cast<uintptr_t>(high);
    uintptr_t marker = reinterpret_cast<uintptr_t>(&state);
    state->workerStackCurrent = marker;
    state->stackValid = state->stackValid &&
        state->workerStackLow < state->workerStackHigh &&
        marker >= state->workerStackLow && marker < state->workerStackHigh;

    // A newly-created native worker has a null PAL FLS cell.  PalAttachThread
    // is the actual PAL operation that validates that invariant and publishes
    // the worker token.  The value is intentionally left live so generic
    // local-storage teardown invokes the Win64 detach callback.
    state->initialFlsNull =
        g_probeFlsIndex != kInvalidFls &&
        guidexos_nativeaot_pal_fls_get(g_probeFlsIndex) == nullptr;
    PalAttachThread(&g_workerThreadToken);
    if (g_probeFlsIndex != kInvalidFls) {
        state->callbackContextValid =
            guidexos_nativeaot_pal_fls_set(g_probeFlsIndex, &g_workerFlsValue) == 0;
    }
    state->callbackResult = kWorkerResult;
    return static_cast<uint32_t>(kWorkerResult);
}

} // namespace

extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotPalInstallHooks(
    const guidexos_nativeaot_pal_hook_table_v1* table) {
    return guidexos_nativeaot_pal_install_hook_table(table);
}

extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotPalUninstallHooks() {
    return guidexos_nativeaot_pal_uninstall_hook_table();
}

extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotPalProbeMain(GuidexosNativeGxAppContext* context) {
    if (context != nullptr) {
        if (context->host == nullptr || context->host->log == nullptr) return -2;
        return hosted::run(context);
    }
    g_probeDetachCount = 0;
    g_probeDetachValue = nullptr;
    g_worker = {};

    const bool initialized = PalInit();
    if (!initialized) return -10;

    g_worker.initialThreadId = PalGetCurrentOSThreadId();
    if (g_worker.initialThreadId == 0) return -11;

    void* initialLow = nullptr;
    void* initialHigh = nullptr;
    if (!PalGetMaximumStackBounds(&initialLow, &initialHigh)) return -12;
    const uintptr_t initialMarker = reinterpret_cast<uintptr_t>(&initialLow);
    if (reinterpret_cast<uintptr_t>(initialLow) >= reinterpret_cast<uintptr_t>(initialHigh) ||
        initialMarker < reinterpret_cast<uintptr_t>(initialLow) ||
        initialMarker >= reinterpret_cast<uintptr_t>(initialHigh)) return -13;

    PalAttachThread(&g_initialThreadToken);
    g_probeFlsIndex = guidexos_nativeaot_pal_fls_alloc(probeDetachCallback);
    if (g_probeFlsIndex == kInvalidFls ||
        guidexos_nativeaot_pal_fls_set(g_probeFlsIndex, &g_initialFlsValue) != 0 ||
        guidexos_nativeaot_pal_fls_get(g_probeFlsIndex) != &g_initialFlsValue) return -14;

    const uint64_t counterBefore = PalQueryPerformanceCounter();
    const uint64_t frequency = PalQueryPerformanceFrequency();
    PalSleep(20u);
    (void)PalSwitchToThread();
    const uint64_t counterAfter = PalQueryPerformanceCounter();
    const uint64_t millis = PalGetTickCount64();
    if (frequency == 0 || millis == 0 || counterAfter < counterBefore) return -15;

    if (!PalStartBackgroundWork(workerCallback, &g_worker, 0) ||
        g_worker.callbackCount != 1 ||
        g_worker.workerThreadId == 0 ||
        g_worker.workerThreadId == g_worker.initialThreadId ||
        g_worker.callbackResult != kWorkerResult ||
        !g_worker.initialFlsNull || !g_worker.callbackContextValid ||
        g_probeDetachCount != 1 || g_probeDetachValue != &g_workerFlsValue ||
        guidexos_nativeaot_pal_fls_get(g_probeFlsIndex) != &g_initialFlsValue) return -16;

    if (guidexos_nativeaot_pal_fls_set(g_probeFlsIndex, nullptr) != 0 ||
        guidexos_nativeaot_pal_fls_free(g_probeFlsIndex) != 0) return -17;
    g_probeFlsIndex = kInvalidFls;
    if (!PalDetachThread(&g_initialThreadToken)) return -18;

    // PalInit's internal index is deliberately released through the same
    // exact contract used by the active MinWin replacement.  The QEMU harness
    // initializes ThreadStore first, so the PAL index is the second generic
    // slot but the first PAL-visible index (0).
    if (guidexos_nativeaot_pal_fls_free(0u) != 0) return -19;

    if (context != nullptr && context->host != nullptr && context->host->log != nullptr) {
        return context->host->log(context, "NativeAOT PAL hook-table probe completed");
    }
    return 0;
}
