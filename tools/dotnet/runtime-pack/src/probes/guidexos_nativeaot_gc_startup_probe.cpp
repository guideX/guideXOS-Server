#include "../platform/guidexos_nativeaot_gc_startup_platform_contract.h"
#include "../platform/guidexos_nativeaot_pal_contract.h"
#include "../platform/guidexos_nativeaot_amd64_unwind_primitive.h"

#include <stddef.h>
#include <stdint.h>
#include <new>

#include <intrin.h>
#include "RhConfig.h"

// Keep the diagnostic surface deliberately smaller than the locked runtime
// headers.  These declarations are only used to read singleton presence and
// never expose or dereference an object across the PE-to-ELF boundary.
class ThreadStore;
class RuntimeInstance {
public:
    ThreadStore* GetThreadStore();
};
RuntimeInstance* GetRuntimeInstance();
class IGCHeap;
extern IGCHeap* g_pGCHeap;

// The startup dry run has no CRT startup and must not use the collector for
// its own bookkeeping.  NativeAOT's platform objects only need a small,
// bounded native metadata arena before the process-lifetime finalizer worker
// is parked.  The arena is deliberately never exposed to managed code.
namespace std {
const nothrow_t nothrow = nothrow_t();
}

namespace {
// Keep the startup-only native bookkeeping bounded below the QEMU test
// loader's established frame budget.  This is not a managed heap.
alignas(16) unsigned char g_nativeStartupArena[1u * 1024u * 1024u] = {};
size_t g_nativeStartupArenaUsed = 0;
uint32_t g_nativeStartupAllocationFailures = 0;
uint32_t g_nativeStartupAllocationCalls = 0;
uint32_t g_nativeStartupLastAllocationSize = 0;
extern "C" uint32_t g_guidexos_nativeaot_gc_startup_diagnostic_stage = 0;

struct WinCriticalState {
    void* storage;
    volatile long held;
    uint64_t owner;
    uint32_t recursion;
};
// The bounded C011EC42 experiment deliberately reaches one natural
// Collection-3 path after the earlier C37-C41 setup.  Keep the test-only PAL
// lock registry fixed-size, but leave enough entries for that additional
// collector lifecycle without changing the production GC.  Open addressing
// keeps lookups bounded without repeatedly scanning the whole registry.
static constexpr size_t kWinCriticalStateCount = 16384u;
static constexpr uintptr_t kWinCriticalStateTombstone = 1u;
WinCriticalState g_winCriticalStates[kWinCriticalStateCount] = {};

void* allocateNative(size_t size) {
    ++g_nativeStartupAllocationCalls;
    g_nativeStartupLastAllocationSize =
        size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size);
    const size_t aligned = (size + 15u) & ~static_cast<size_t>(15u);
    if (aligned > sizeof(g_nativeStartupArena) - g_nativeStartupArenaUsed) {
        ++g_nativeStartupAllocationFailures;
        return nullptr;
    }
    void* result = g_nativeStartupArena + g_nativeStartupArenaUsed;
    g_nativeStartupArenaUsed += aligned;
    return result;
}

uint64_t startupThreadId() {
    uint64_t result = 0;
    return guidexos_nativeaot_pal_current_thread_id(&result) == 0 ? result : 0;
}

WinCriticalState* criticalState(void* storage, bool create) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(storage) >> 4;
    const uintptr_t mixed = value * static_cast<uintptr_t>(0x9E3779B97F4A7C15ull);
    const size_t mask = kWinCriticalStateCount - 1u;
    const size_t start = static_cast<size_t>(mixed) & mask;
    WinCriticalState* tombstone = nullptr;
    for (size_t offset = 0; offset < kWinCriticalStateCount; ++offset) {
        WinCriticalState& state = g_winCriticalStates[(start + offset) & mask];
        if (state.storage == storage) return &state;
        if (state.storage == reinterpret_cast<void*>(kWinCriticalStateTombstone)) {
            if (tombstone == nullptr) tombstone = &state;
            continue;
        }
        if (state.storage == nullptr) {
            if (!create) return nullptr;
            WinCriticalState* result = tombstone == nullptr ? &state : tombstone;
            result->storage = storage;
            result->held = 0;
            result->owner = 0;
            result->recursion = 0;
            return result;
        }
    }
    return nullptr;
}

void startupYield() {
    (void)guidexos_nativeaot_pal_yield();
}

[[noreturn]] void startupFailFast() {
    guidexos_nativeaot_pal_fail_fast(0x47435354u);
}
}

void* operator new(size_t size) { return allocateNative(size); }
void* operator new[](size_t size) { return allocateNative(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return allocateNative(size);
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    return allocateNative(size);
}
void operator delete(void*) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete(void*, size_t) noexcept {}
void operator delete[](void*, size_t) noexcept {}

// Narrow NativeAOT startup shims. These are not a Win32 compatibility layer:
// each operation is used only by the locked startup/ThreadStore objects and
// is mapped to the installed guideXOS C hooks.
extern "C" void* CreateEventExW(void*, const wchar_t*, uint32_t flags, uint32_t) {
    return guidexos_nativeaot_gc_create_event(
        (flags & 1u) != 0 ? 1u : 0u, (flags & 2u) != 0 ? 1u : 0u);
}
extern "C" int32_t SetEvent(void* handle) {
    return guidexos_nativeaot_gc_set_event(handle) == 0 ? 1 : 0;
}
extern "C" int32_t ResetEvent(void* handle) {
    return guidexos_nativeaot_gc_reset_event(handle) == 0 ? 1 : 0;
}
extern "C" uint32_t WaitForSingleObjectEx(void* handle, uint32_t timeout, int32_t) {
    const int32_t result = guidexos_nativeaot_gc_wait_event(handle, timeout);
    return result == 0 ? 0u : (result == 258 ? 258u : 0xFFFFFFFFu);
}
extern "C" int32_t CloseHandle(void* handle) {
    return guidexos_nativeaot_gc_close_event(handle) == 0 ? 1 : 0;
}
extern "C" void FlushProcessWriteBuffers() {}
extern "C" uint32_t GetCurrentThreadId() {
    return static_cast<uint32_t>(startupThreadId());
}
extern "C" void Sleep(uint32_t milliseconds) {
    (void)guidexos_nativeaot_pal_sleep(milliseconds);
}
extern "C" uint64_t GetTickCount64() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_counter(&value) == 0 ? value : 0;
}
extern "C" int32_t QueryPerformanceCounter(int64_t* result) {
    uint64_t value = 0;
    if (result == nullptr || guidexos_nativeaot_pal_counter(&value) != 0) return 0;
    *result = static_cast<int64_t>(value);
    return 1;
}
extern "C" int32_t QueryPerformanceFrequency(int64_t* result) {
    uint64_t value = 0;
    if (result == nullptr || guidexos_nativeaot_pal_frequency(&value) != 0) return 0;
    *result = static_cast<int64_t>(value);
    return 1;
}
extern "C" uint32_t GetEnvironmentVariableW(const wchar_t*, wchar_t*, uint32_t) {
    return 0;
}
extern "C" void GetSystemTimeAsFileTime(void* result) {
    if (result != nullptr) {
        static_cast<uint32_t*>(result)[0] = 0;
        static_cast<uint32_t*>(result)[1] = 0;
    }
}
extern "C" void* VirtualAlloc(void* preferred, size_t size, uint32_t type, uint32_t) {
    if ((type & 0x2000u) != 0 && (type & 0x1000u) == 0) {
        return guidexos_nativeaot_gc_reserve(size, 4096u, 0u, 0xFFFFu);
    }
    void* result = guidexos_nativeaot_gc_reserve(size, 4096u, 0u, 0xFFFFu);
    (void)preferred;
    if (result != nullptr && (type & 0x1000u) != 0 &&
        guidexos_nativeaot_gc_commit(
            result, (size + 4095u) & ~static_cast<size_t>(4095u), 0xFFFFu) != 0) {
        (void)guidexos_nativeaot_gc_release(result, size);
        return nullptr;
    }
    return result;
}
extern "C" int32_t VirtualFree(void* address, size_t size, uint32_t type) {
    return type == 0x4000u
        ? (guidexos_nativeaot_gc_release(address, size) == 0 ? 1 : 0)
        : (guidexos_nativeaot_gc_decommit(address, size) == 0 ? 1 : 0);
}
extern "C" int32_t InitializeCriticalSectionEx(void* storage, uint32_t, uint32_t) {
    return criticalState(storage, true) != nullptr ? 1 : 0;
}
extern "C" void EnterCriticalSection(void* storage) {
    WinCriticalState* state = criticalState(storage, true);
    if (state == nullptr) startupFailFast();
    const uint64_t owner = startupThreadId();
    if (state->owner == owner) {
        ++state->recursion;
        return;
    }
    while (_InterlockedCompareExchange(&state->held, 1, 0) != 0) startupYield();
    state->owner = owner;
    state->recursion = 1;
}
extern "C" void LeaveCriticalSection(void* storage) {
    WinCriticalState* state = criticalState(storage, false);
    if (state == nullptr || state->owner != startupThreadId() || state->recursion == 0) {
        startupFailFast();
    }
    if (--state->recursion == 0) {
        state->owner = 0;
        _InterlockedExchange(&state->held, 0);
        // This is a single-thread QEMU startup shim.  Once the lock is
        // released, its identity is no longer needed; recycle the bounded
        // slot so repeated runtime setup does not exhaust the registry.
        state->storage = reinterpret_cast<void*>(kWinCriticalStateTombstone);
    }
}
extern "C" void DeleteCriticalSection(void* storage) {
    WinCriticalState* state = criticalState(storage, false);
    if (state != nullptr && state->held == 0) {
        state->storage = reinterpret_cast<void*>(kWinCriticalStateTombstone);
        state->owner = 0;
        state->recursion = 0;
    }
}

extern "C" void* GetModuleHandleW(const wchar_t*) { return nullptr; }
extern "C" void* GetProcAddress(void*, const char*) { return nullptr; }
extern "C" void* AddVectoredExceptionHandler(uint32_t, void*) { return nullptr; }
extern "C" [[noreturn]] void RaiseFailFastException(void*, uint32_t) { startupFailFast(); }
extern "C" uint64_t GetEnabledXStateFeatures() { return 0; }
extern "C" void* SetUnhandledExceptionFilter(void*) { return nullptr; }
extern "C" void GetStartupInfoW(void*) {}

extern "C" int atexit(void (*)(void)) { return 0; }
extern "C" unsigned long long strtoull(const char* text, char** end, int base) {
    unsigned long long value = 0;
    if (end != nullptr) *end = const_cast<char*>(text);
    if (text == nullptr) return 0;
    while (*text >= '0' && *text <= '9') {
        value = value * static_cast<unsigned long long>(base == 0 ? 10 : base) +
                static_cast<unsigned long long>(*text - '0');
        if (end != nullptr) *end = const_cast<char*>(text + 1);
        ++text;
    }
    return value;
}
extern "C" unsigned long strtoul(const char* text, char** end, int base) {
    return static_cast<unsigned long>(strtoull(text, end, base));
}
extern "C" int _stricmp(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return left == right ? 0 : (left == nullptr ? -1 : 1);
    while (*left != '\0' && *right != '\0') {
        const char a = *left >= 'A' && *left <= 'Z' ? static_cast<char>(*left + ('a' - 'A')) : *left;
        const char b = *right >= 'A' && *right <= 'Z' ? static_cast<char>(*right + ('a' - 'A')) : *right;
        if (a != b) return a < b ? -1 : 1;
        ++left;
        ++right;
    }
    return *left == *right ? 0 : (*left == '\0' ? -1 : 1);
}
extern "C" [[noreturn]] void abort() { startupFailFast(); }
extern "C" [[noreturn]] void _wassert(const wchar_t*, const wchar_t*, unsigned) {
    startupFailFast();
}

// The locked Windows object files were compiled with dllimport declarations.
// Bind those import-pointer symbols to the narrow local shims above so the
// converted image has no mandatory Windows import directory.
extern "C" void* __imp_AddVectoredExceptionHandler =
    reinterpret_cast<void*>(&AddVectoredExceptionHandler);
extern "C" void* __imp_RaiseFailFastException =
    reinterpret_cast<void*>(&RaiseFailFastException);
extern "C" void* __imp_GetModuleHandleW = reinterpret_cast<void*>(&GetModuleHandleW);
extern "C" void* __imp_GetProcAddress = reinterpret_cast<void*>(&GetProcAddress);
extern "C" void* __imp_GetEnabledXStateFeatures =
    reinterpret_cast<void*>(&GetEnabledXStateFeatures);
extern "C" void* __imp_SetUnhandledExceptionFilter =
    reinterpret_cast<void*>(&SetUnhandledExceptionFilter);
extern "C" void* __imp_GetStartupInfoW = reinterpret_cast<void*>(&GetStartupInfoW);
extern "C" void* __imp_RtlVirtualUnwind =
    reinterpret_cast<void*>(&guideXosRtlVirtualUnwind);

// The NativeAOT managed image already supplies these image-owned globals and
// fail-fast entrypoints. The startup-only dry-run has no managed image, so it
// keeps the local copies; the first-allocation image reuses its generated
// copies to avoid two competing startup surfaces in one link.
#if !defined(GUIDEXOS_NATIVEAOT_MANAGED_IMAGE)
extern "C" int g_requiredCpuFeatures = 0;
extern "C" uint32_t _tls_index = 0;
struct StartupEmbeddedConfig {
    uint32_t count;
    char* slots[2];
};
extern "C" StartupEmbeddedConfig g_compilerEmbeddedSettingsBlob = {};
extern "C" StartupEmbeddedConfig g_compilerEmbeddedKnobsBlob = {};

extern "C" [[noreturn]] void ProcessFinalizers() { startupFailFast(); }
extern "C" int32_t RhpCalculateStackTraceWorker(void*, uint32_t, void*) { return -1; }
extern "C" void* RhpCidResolve(void*, void*) { return nullptr; }
extern "C" [[noreturn]] void RhpFailFastForPInvokeExceptionPreemp(uintptr_t, void*, void*) { startupFailFast(); }
extern "C" [[noreturn]] void RhpFailFastForPInvokeExceptionCoop(uintptr_t, void*, void*) { startupFailFast(); }
extern "C" [[noreturn]] void RhExceptionHandling_FailedAllocation() { startupFailFast(); }
extern "C" [[noreturn]] void RhThrowHwEx() { startupFailFast(); }
extern "C" [[noreturn]] void RhThrowEx() { startupFailFast(); }
extern "C" [[noreturn]] void RhRethrow() { startupFailFast(); }
#endif

extern "C" bool RhInitialize(bool isDll);

extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotGcStartupInstallPalHooks(
    const guidexos_nativeaot_pal_hooks* hooks) {
    return guidexos_nativeaot_pal_install_hooks(hooks);
}

extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotGcStartupInstallHookTable(
    const guidexos_nativeaot_pal_hook_table_v1* table) {
    return guidexos_nativeaot_pal_install_hook_table(table);
}

extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotGcStartupInstallPlatformHooks(
    const guidexos_nativeaot_gc_startup_platform_table_v1* table) {
    return guidexos_nativeaot_gc_install_startup_platform_hooks(table);
}

extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotGcStartupMain() {
    // This is the sole call to RhInitialize in the experiment.  No managed
    // entry point, allocation helper, collection request, finalizer event, or
    // shutdown routine is called from this wrapper.
    return RhInitialize(false) ? 0 : -1;
}

extern "C" __declspec(dllexport) uint32_t __stdcall
GuideXosNativeAotGcStartupGetState() {
    extern uint32_t g_guidexos_nativeaot_gc_startup_pal_stage;
    extern uint32_t g_guidexos_nativeaot_gc_startup_stage;
    return (g_guidexos_nativeaot_gc_startup_pal_stage << 24) |
           (g_guidexos_nativeaot_gc_startup_stage & 0x00FFFFFFu) |
           ((g_nativeStartupAllocationFailures & 0xFFu) << 16);
}

// Pointer-free diagnostics for the one-shot startup dry run.  The returned
// bits intentionally expose only whether bounded runtime/GC singletons were
// published; no C++ object address crosses the PE-to-ELF boundary.
extern "C" __declspec(dllexport) uint32_t __stdcall
GuideXosNativeAotGcStartupGetPreGcState() {
    uint32_t result = 0;
    RuntimeInstance* runtime = GetRuntimeInstance();
    if (runtime != nullptr) result |= 1u << 0;
    if (runtime != nullptr && runtime->GetThreadStore() != nullptr) result |= 1u << 1;
    if (g_pGCHeap != nullptr) result |= 1u << 2;
    if (g_pRhConfig != nullptr) result |= 1u << 3;
    result |= (g_nativeStartupAllocationCalls & 0xFFu) << 8;
    result |= (g_nativeStartupAllocationFailures & 0xFFu) << 16;
    return result;
}

extern "C" __declspec(dllexport) uint32_t __stdcall
GuideXosNativeAotGcStartupGetAllocationCount() {
    return g_nativeStartupAllocationCalls;
}

extern "C" __declspec(dllexport) uint32_t __stdcall
GuideXosNativeAotGcStartupGetLastAllocationSize() {
    return g_nativeStartupLastAllocationSize;
}

extern "C" __declspec(dllexport) uint32_t __stdcall
GuideXosNativeAotGcStartupGetDiagnosticStage() {
    return g_guidexos_nativeaot_gc_startup_diagnostic_stage;
}
