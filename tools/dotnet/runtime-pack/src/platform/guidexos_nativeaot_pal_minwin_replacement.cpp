#include "guidexos_nativeaot_pal_contract.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <windows.h>

// PalInit is the NativeAOT GC platform initialization boundary.  These are
// the exact locked source declarations used by the stock Windows PAL; the
// implementations are supplied by the adapted guideXOS gcenv object in the
// startup-only link image.
#if defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
#include "gcenv.h"
#include "gcenv.ee.h"
#include "gcconfig.h"
#endif

#define GUIDEXOS_NATIVEAOT_PAL_EXPORT extern "C"
#define GUIDEXOS_NATIVEAOT_PAL_API __stdcall

#if defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
extern "C" uint32_t g_guidexos_nativeaot_gc_startup_pal_stage = 0;
#endif

namespace {

constexpr uint32_t kFlsOutOfIndexes = 0xFFFFFFFFu;
constexpr uint32_t kWaitFailed = 0xFFFFFFFFu;
constexpr uint32_t kDuplicateSameAccess = 0x2u;

uint32_t g_flsIndex = kFlsOutOfIndexes;
void (*g_hijackCallback)(void*, void*) = nullptr;

struct BackgroundWork {
    uint32_t (GUIDEXOS_NATIVEAOT_PAL_API* callback)(void*);
    void* context;
};

#if defined(GUIDEXOS_NATIVEAOT_PAL_QEMU_PROBE)
BackgroundWork g_qemuBackgroundWork = {};
bool g_qemuBackgroundActive = false;
#endif

#if defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
// RhInitialize starts the NativeAOT finalizer helper.  The locked PAL API has
// no shutdown operation, so the startup dry run keeps this worker parked until
// its disposable QEMU process exits instead of joining it from this call.
guidexos_nativeaot_pal_opaque_handle g_qemuStartupBackgroundHandle = 0;
bool g_qemuStartupBackgroundActive = false;
#endif

uintptr_t GUIDEXOS_NATIVEAOT_PAL_CALL backgroundEntry(void* raw) {
    BackgroundWork* work = static_cast<BackgroundWork*>(raw);
    if (work == nullptr || work->callback == nullptr) return 0;
    const uint32_t result = work->callback(work->context);
#if defined(GUIDEXOS_NATIVEAOT_PAL_QEMU_PROBE)
    // The exact QEMU artifact has no CRT/heap.  The probe permits one
    // bounded background worker and the hook-side close operation joins it
    // before this slot is reused.
    extern bool g_qemuBackgroundActive;
    g_qemuBackgroundActive = false;
#else
    delete work;
#endif
    return result;
}

} // namespace

// This global is part of the locked object ABI.  It is intentionally null in
// the guideXOS configuration because context restoration is not a permitted
// operation in the bounded PAL probe.
using GuidexosInitializeContext2 = int (__cdecl*)(void*, unsigned long, CONTEXT**,
                                                  unsigned long*, unsigned __int64);
GuidexosInitializeContext2 pfnInitializeContext2 = nullptr;

void __cdecl FiberDetachCallback(void* value) {
    // The guideXOS local-storage manager has already cleared the cell before
    // invoking this callback.  The real ThreadStore shutdown is outside this
    // pass; retaining the exact callback symbol preserves the PAL ABI without
    // inventing a C++ Thread object boundary.
    (void)value;
}

void __cdecl InitializeCurrentProcessCpuCount() {
    extern uint32_t g_RhNumberOfProcessors;
    g_RhNumberOfProcessors = 1u;
}

void __cdecl InitHijackingAPIs() {
    // Hijacking is deliberately unsupported until the NativeAOT context ABI
    // is separately proven.  No Windows resolver or APC API is touched.
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API PalInit() {
#if defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
    g_guidexos_nativeaot_gc_startup_pal_stage = 1u;
#endif
    if (g_flsIndex != kFlsOutOfIndexes) {
        (void)guidexos_nativeaot_pal_fls_free(g_flsIndex);
        g_flsIndex = kFlsOutOfIndexes;
    }
    g_flsIndex = guidexos_nativeaot_pal_fls_alloc(FiberDetachCallback);
    if (g_flsIndex == kFlsOutOfIndexes) return false;
#if defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
    g_guidexos_nativeaot_gc_startup_pal_stage = 2u;
#endif

#if defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
    GCConfig::Initialize();
    g_guidexos_nativeaot_gc_startup_pal_stage = 3u;
    if (!GCToOSInterface::Initialize()) {
        return false;
    }
    g_guidexos_nativeaot_gc_startup_pal_stage = 4u;
#endif

    InitializeCurrentProcessCpuCount();
    return true;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void GUIDEXOS_NATIVEAOT_PAL_API
PalAttachThread(void* thread) {
    if (g_flsIndex == kFlsOutOfIndexes) {
        guidexos_nativeaot_pal_fail_fast(0x50414C01u);
    }
    void* existing = guidexos_nativeaot_pal_fls_get(g_flsIndex);
    if (existing != nullptr && existing != thread) {
        guidexos_nativeaot_pal_fail_fast(0x50414C02u);
    }
    if (guidexos_nativeaot_pal_fls_set(g_flsIndex, thread) != 0) {
        guidexos_nativeaot_pal_fail_fast(0x50414C03u);
    }
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API
PalDetachThread(void* thread) {
    if (g_flsIndex == kFlsOutOfIndexes) return false;
    void* existing = guidexos_nativeaot_pal_fls_get(g_flsIndex);
    if (existing == nullptr) return false;
    if (existing != thread) {
        guidexos_nativeaot_pal_fail_fast(0x50414C04u);
    }
    return guidexos_nativeaot_pal_fls_set(g_flsIndex, nullptr) == 0;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint64_t PalQueryPerformanceCounter() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_counter(&value) == 0 ? value : 0;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint64_t PalQueryPerformanceFrequency() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_frequency(&value) == 0 ? value : 0;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint64_t PalGetCurrentOSThreadId() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_current_thread_id(&value) == 0 ? value : 0;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint32_t GUIDEXOS_NATIVEAOT_PAL_API
PalMarkThunksAsValidCallTargets(void*, int, int, int, int) {
    return 1u;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint32_t GUIDEXOS_NATIVEAOT_PAL_API
PalCompatibleWaitAny(uint32_t alertable,
                     uint32_t timeout,
                     uint32_t count,
                     HANDLE* handles,
                     uint32_t allow_reentrant_wait) {
    (void)allow_reentrant_wait;
    if (count == 0 || handles == nullptr) return kWaitFailed;
    return guidexos_nativeaot_pal_wait_any(
        timeout, count, reinterpret_cast<void* const*>(handles),
        alertable != 0);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT HANDLE PalCreateLowMemoryResourceNotification() {
    return nullptr;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void GUIDEXOS_NATIVEAOT_PAL_API
PalSleep(uint32_t milliseconds) {
    (void)guidexos_nativeaot_pal_sleep(milliseconds);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint32_t GUIDEXOS_NATIVEAOT_PAL_API
PalSwitchToThread() {
    return guidexos_nativeaot_pal_yield() == 0 ? 1u : 0u;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT HANDLE GUIDEXOS_NATIVEAOT_PAL_API
PalCreateEventW(LPSECURITY_ATTRIBUTES attributes,
                uint32_t manual_reset,
                uint32_t initial_state,
                LPCWSTR name) {
    (void)attributes;
    (void)name;
    return static_cast<HANDLE>(guidexos_nativeaot_pal_create_event(
        manual_reset != 0, initial_state != 0));
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint32_t GUIDEXOS_NATIVEAOT_PAL_API
PalAreShadowStacksEnabled() {
    return 0u;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT CONTEXT*
PalAllocateCompleteOSContext(uint8_t** context_buffer) {
    if (context_buffer == nullptr) return nullptr;
    *context_buffer = nullptr;
    return nullptr;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API
PalGetCompleteThreadContext(HANDLE, CONTEXT*) {
    return false;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API
PalSetThreadContext(HANDLE, CONTEXT*) {
    return false;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void GUIDEXOS_NATIVEAOT_PAL_API
PalRestoreContext(CONTEXT*) {
    guidexos_nativeaot_pal_fail_fast(0x50414C05u);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void GUIDEXOS_NATIVEAOT_PAL_API
PopulateControlSegmentRegisters(CONTEXT* context) {
    if (context != nullptr) {
        context->SegCs = 0;
        context->SegSs = 0;
    }
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint32_t GUIDEXOS_NATIVEAOT_PAL_API
PalRegisterHijackCallback(void (*callback)(void*, void*)) {
    g_hijackCallback = callback;
    return 1u;
}

void InitHijackingAPIs();

GUIDEXOS_NATIVEAOT_PAL_EXPORT void GUIDEXOS_NATIVEAOT_PAL_API
PalHijack(HANDLE, void*) {
    // Context hijacking is not part of the bounded no-collection contract.
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void* GUIDEXOS_NATIVEAOT_PAL_API
PalGetHijackTarget(void* default_target) {
    return default_target;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API
PalStartBackgroundWork(uint32_t (__stdcall* callback)(void*),
                       void* callback_context,
                       int32_t high_priority) {
    if (callback == nullptr) return false;
    BackgroundWork* work = nullptr;
#if defined(GUIDEXOS_NATIVEAOT_PAL_QEMU_PROBE)
    if (g_qemuBackgroundActive) return false;
    g_qemuBackgroundActive = true;
    g_qemuBackgroundWork = BackgroundWork{ callback, callback_context };
    work = &g_qemuBackgroundWork;
#else
    work = new (std::nothrow) BackgroundWork{ callback, callback_context };
    if (work == nullptr) return false;
#endif
    guidexos_nativeaot_pal_opaque_handle handle = 0;
    const int32_t created = guidexos_nativeaot_pal_create_thread(
        backgroundEntry, work, 8192u, high_priority != 0, &handle);
    if (created != 0) {
#if defined(GUIDEXOS_NATIVEAOT_PAL_QEMU_PROBE)
        g_qemuBackgroundActive = false;
#else
        delete work;
#endif
#if defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
        g_qemuStartupBackgroundActive = false;
        g_guidexos_nativeaot_gc_startup_pal_stage = 0x0Fu;
#endif
        return false;
    }
#if defined(GUIDEXOS_NATIVEAOT_GC_STARTUP)
    g_qemuStartupBackgroundHandle = handle;
    g_qemuStartupBackgroundActive = true;
    g_guidexos_nativeaot_gc_startup_pal_stage = 0x02u;
    // There is no RhShutdown in the locked NativeAOT source contract.  The
    // process-lifetime QEMU harness kills the disposable process after the
    // startup marker, while the artifact and callback targets remain mapped.
    return true;
#else
    // This API intentionally has no joinable result in its source contract;
    // the guideXOS hook transfers ownership to the detached helper path.
    (void)guidexos_nativeaot_pal_close_thread(handle);
    return true;
#endif
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API
PalStartBackgroundGCThread(uint32_t (__stdcall* callback)(void*), void* context) {
    return PalStartBackgroundWork(callback, context, 0);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API
PalStartFinalizerThread(uint32_t (__stdcall* callback)(void*), void* context) {
    return PalStartBackgroundWork(callback, context, 1);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API
PalStartEventPipeHelperThread(uint32_t (__stdcall* callback)(void*), void* context) {
    return PalStartBackgroundWork(callback, context, 0);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT HANDLE GUIDEXOS_NATIVEAOT_PAL_API
PalGetModuleHandleFromPointer(void* pointer) {
    return static_cast<HANDLE>(guidexos_nativeaot_pal_static_module_from_pointer(pointer));
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void PalPrintFatalError(const char* message) {
    (void)message;
    guidexos_nativeaot_pal_fail_fast(0x50414C06u);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT char* PalCopyTCharAsChar(const wchar_t* source) {
    if (source == nullptr) return nullptr;
    size_t length = 0;
    while (source[length] != L'\0') ++length;
    char* result = new (std::nothrow) char[length + 1u];
    if (result == nullptr) return nullptr;
    for (size_t i = 0; i < length; ++i) {
        result[i] = source[i] <= 0x7Fu ? static_cast<char>(source[i]) : '?';
    }
    result[length] = '\0';
    return result;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT HANDLE PalLoadLibrary(const char*) {
    // Dynamic loading is deliberately unsupported.
    return nullptr;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void* PalGetProcAddress(HANDLE module,
                                                       const char* function_name) {
    if (module == nullptr || function_name == nullptr) return nullptr;
    return guidexos_nativeaot_pal_static_resolve(module, function_name);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void* GUIDEXOS_NATIVEAOT_PAL_API
PalVirtualAlloc(uintptr_t size, uint32_t protect) {
    return guidexos_nativeaot_pal_virtual_alloc(
        nullptr, size, MEM_RESERVE | MEM_COMMIT, protect);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void GUIDEXOS_NATIVEAOT_PAL_API
PalVirtualFree(void* address, uintptr_t size) {
    (void)guidexos_nativeaot_pal_virtual_free(address, size, MEM_RELEASE);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint32_t GUIDEXOS_NATIVEAOT_PAL_API
PalVirtualProtect(void* address, uintptr_t size, uint32_t protect) {
    uint32_t old_protect = 0;
    return guidexos_nativeaot_pal_virtual_protect(
        address, size, protect, &old_protect) == 0 ? 1u : 0u;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT void
PalFlushInstructionCache(void*, size_t) {
}
