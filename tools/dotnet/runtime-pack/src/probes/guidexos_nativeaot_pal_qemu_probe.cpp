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
    for (size_t index = 0; index < count; ++index) bytes[index] = static_cast<unsigned char>(value);
    return destination;
}

// This entry is intentionally C-compatible and contains no C++ runtime or
// guideXOS class.  It is the PE-side half of the deliberate Win64 bridge.
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
extern "C" int64_t minipal_hires_ticks();
extern "C" int64_t minipal_hires_tick_frequency();
extern "C" void* __stdcall PalGetModuleHandleFromPointer(void* pointer);
extern "C" void* __stdcall PalGetProcAddress(void* module, const char* name);
extern "C" void __stdcall PalSleep(uint32_t milliseconds);
extern "C" uint32_t __stdcall PalSwitchToThread();

namespace {

constexpr uint32_t kCapacity = 8u;
constexpr uint32_t kInvalid = 0xFFFFFFFFu;
void* g_values[kCapacity] = {};
bool g_active[kCapacity] = {};
guidexos_nativeaot_fls_detach_callback g_callbacks[kCapacity] = {};
uint64_t g_counter = 1000u;
uint8_t g_threadToken = 0;
uint8_t g_resolvedByte = 0x5Au;

uint32_t flsAlloc(void*, guidexos_nativeaot_fls_detach_callback callback) {
    for (uint32_t index = 0; index < kCapacity; ++index) {
        if (!g_active[index]) {
            g_active[index] = true;
            g_callbacks[index] = callback;
            g_values[index] = nullptr;
            return index;
        }
    }
    return kInvalid;
}

int32_t flsFree(void*, uint32_t index) {
    if (index >= kCapacity || !g_active[index]) return -1;
    if (g_values[index] != nullptr && g_callbacks[index] != nullptr) {
        g_callbacks[index](g_values[index]);
    }
    g_values[index] = nullptr;
    g_callbacks[index] = nullptr;
    g_active[index] = false;
    return 0;
}

void* flsGet(void*, uint32_t index) {
    return index < kCapacity && g_active[index] ? g_values[index] : nullptr;
}

int32_t flsSet(void*, uint32_t index, void* value) {
    if (index >= kCapacity || !g_active[index]) return -1;
    g_values[index] = value;
    return 0;
}

uint64_t currentThreadId(void*) { return 1u; }

int32_t stackBounds(void*, uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    if (low == nullptr || high == nullptr || current == nullptr) return -1;
    const uintptr_t marker = reinterpret_cast<uintptr_t>(&g_threadToken);
    *low = marker - 4096u;
    *high = marker + 4096u;
    *current = marker;
    return 0;
}

uint64_t counter(void*) { return ++g_counter; }
uint64_t frequency(void*) { return 1000000u; }
int32_t sleepMilliseconds(void*, uint32_t) { return 0; }
int32_t yield(void*) { return 0; }
int32_t getLastError(void*) { return 0; }
void setLastError(void*, int32_t) {}
void* currentProcess(void*) { return reinterpret_cast<void*>(1u); }
void* currentThread(void*) { return &g_threadToken; }
int32_t duplicateHandle(void*, void*, void*, void*, void**, uint32_t, int32_t, uint32_t) { return -1; }
void* staticModule(void*, const void*) { return reinterpret_cast<void*>(0xBEEFu); }
void* staticResolve(void*, void*, const char* name) {
    return name != nullptr && name[0] == 'P' && name[1] == 'r' ? &g_resolvedByte : nullptr;
}
int32_t createThread(void*, guidexos_nativeaot_thread_entry, void*, uint32_t, int32_t,
                     guidexos_nativeaot_pal_opaque_handle*) { return -1; }
int32_t joinThread(void*, guidexos_nativeaot_pal_opaque_handle, uint32_t, uintptr_t*) { return -1; }
int32_t closeThread(void*, guidexos_nativeaot_pal_opaque_handle) { return -1; }
void* createEvent(void*, int32_t, int32_t) { return nullptr; }
int32_t waitAny(void*, uint32_t, uint32_t, void* const*, int32_t) { return -1; }
int32_t closeEvent(void*, void*) { return -1; }
[[noreturn]] void failFast(void*, uint32_t) { for (;;) {} }
void* virtualAlloc(void*, void* preferred, uintptr_t, uint32_t, uint32_t) { return preferred; }
int32_t virtualFree(void*, void*, uintptr_t, uint32_t) { return 0; }
int32_t virtualProtect(void*, void*, uintptr_t, uint32_t, uint32_t* oldProtection) {
    if (oldProtection != nullptr) *oldProtection = 0;
    return 0;
}

void detachValue(void*) {}

} // namespace

extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotPalProbeMain(GuidexosNativeGxAppContext* context) {
    if (context == nullptr || context->host == nullptr || context->host->log == nullptr) return -2;

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
    hooks.get_last_error = getLastError;
    hooks.set_last_error = setLastError;
    hooks.current_process = currentProcess;
    hooks.current_thread = currentThread;
    hooks.duplicate_handle = duplicateHandle;
    hooks.static_module_from_pointer = staticModule;
    hooks.static_resolve = staticResolve;
    hooks.create_thread = createThread;
    hooks.join_thread = joinThread;
    hooks.close_thread = closeThread;
    hooks.create_event = createEvent;
    hooks.wait_any = waitAny;
    hooks.close_event = closeEvent;
    hooks.fail_fast = failFast;
    hooks.virtual_alloc = virtualAlloc;
    hooks.virtual_free = virtualFree;
    hooks.virtual_protect = virtualProtect;
    if (guidexos_nativeaot_pal_install_fls_hooks(&fls) != 0 ||
        guidexos_nativeaot_pal_install_hooks(&hooks) != 0) return -3;

    if (!PalInit()) return -4;
    PalAttachThread(&g_threadToken);
    const uint32_t index = guidexos_nativeaot_pal_fls_alloc(detachValue);
    void* value = reinterpret_cast<void*>(0x1111u);
    if (index == kInvalid || guidexos_nativeaot_pal_fls_set(index, value) != 0 ||
        guidexos_nativeaot_pal_fls_get(index) != value ||
        guidexos_nativeaot_pal_fls_free(index) != 0) return -5;

    const uint64_t before = PalQueryPerformanceCounter();
    PalSleep(0u);
    const uint64_t after = PalQueryPerformanceCounter();
    const uint64_t frequencyValue = PalQueryPerformanceFrequency();
    if (after < before || frequencyValue == 0u || PalGetCurrentOSThreadId() != 1u ||
        PalGetTickCount64() == 0u || minipal_hires_ticks() == 0 ||
        minipal_hires_tick_frequency() == 0 || PalSwitchToThread() == 2u) return -6;

    void* module = PalGetModuleHandleFromPointer(&GuideXosNativeAotPalProbeMain);
    if (module == nullptr || PalGetProcAddress(module, "ProbeResolved") == nullptr) return -7;

    if (!PalDetachThread(&g_threadToken)) return -8;
    (void)guidexos_nativeaot_pal_fls_free(0u);
    (void)guidexos_nativeaot_pal_install_hooks(nullptr);
    (void)guidexos_nativeaot_pal_install_fls_hooks(nullptr);
    return context->host->log(context, "NativeAOT PAL PE bridge probe completed");
}
