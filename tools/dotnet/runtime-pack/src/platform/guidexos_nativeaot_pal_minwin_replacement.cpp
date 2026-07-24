#include "guidexos_nativeaot_pal_contract.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <windows.h>

#define GUIDEXOS_NATIVEAOT_PAL_EXPORT extern "C"
#define GUIDEXOS_NATIVEAOT_PAL_API __stdcall

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

uintptr_t GUIDEXOS_NATIVEAOT_PAL_CALL backgroundEntry(void* raw) {
    BackgroundWork* work = static_cast<BackgroundWork*>(raw);
    if (work == nullptr || work->callback == nullptr) return 0;
    const uint32_t result = work->callback(work->context);
    delete work;
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
    if (g_flsIndex != kFlsOutOfIndexes) {
        (void)guidexos_nativeaot_pal_fls_free(g_flsIndex);
        g_flsIndex = kFlsOutOfIndexes;
    }
    g_flsIndex = guidexos_nativeaot_pal_fls_alloc(FiberDetachCallback);
    if (g_flsIndex == kFlsOutOfIndexes) return false;
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
    BackgroundWork* work = new (std::nothrow) BackgroundWork{ callback, callback_context };
    if (work == nullptr) return false;
    guidexos_nativeaot_pal_opaque_handle handle = 0;
    const int32_t created = guidexos_nativeaot_pal_create_thread(
        backgroundEntry, work, 8192u, high_priority != 0, &handle);
    if (created != 0) {
        delete work;
        return false;
    }
    // This API intentionally has no joinable result in its source contract;
    // the guideXOS hook transfers ownership to the detached helper path.
    (void)guidexos_nativeaot_pal_close_thread(handle);
    return true;
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
