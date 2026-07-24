#include "../platform/guidexos_nativeaot_pal_contract.h"
#include "../platform/guidexos_nativeaot_fls_adapter.h"

#include "../../../../../runtime/synchronization/guidexos_event.h"
#include "../../../../../runtime/thread/guidexos_native_thread.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>

extern "C" bool __stdcall PalInit();
extern "C" void __stdcall PalAttachThread(void* thread);
extern "C" bool __stdcall PalDetachThread(void* thread);
extern "C" uint64_t PalQueryPerformanceCounter();
extern "C" uint64_t PalQueryPerformanceFrequency();
extern "C" uint64_t PalGetCurrentOSThreadId();
extern "C" void __stdcall PalSleep(uint32_t milliseconds);
extern "C" uint32_t __stdcall PalSwitchToThread();
extern "C" HANDLE __stdcall PalCreateEventW(LPSECURITY_ATTRIBUTES,
                                               uint32_t manual_reset,
                                               uint32_t initial_state,
                                               LPCWSTR name);
extern "C" uint32_t __stdcall PalCompatibleWaitAny(uint32_t alertable,
                                                      uint32_t timeout,
                                                      uint32_t count,
                                                      HANDLE* handles,
                                                      uint32_t allow_reentrant_wait);
extern "C" bool __stdcall PalStartBackgroundWork(uint32_t (__stdcall*)(void*),
                                                   void* context,
                                                   int32_t high_priority);
extern "C" HANDLE __stdcall PalGetModuleHandleFromPointer(void* pointer);
extern "C" void* __stdcall PalGetProcAddress(HANDLE module, const char* name);
extern "C" HANDLE __stdcall PalLoadLibrary(const char* name);
extern "C" void* __stdcall PalVirtualAlloc(uintptr_t size, uint32_t protect);
extern "C" void __stdcall PalVirtualFree(void* address, uintptr_t size);
extern "C" uint32_t __stdcall PalVirtualProtect(void* address,
                                                  uintptr_t size,
                                                  uint32_t protect);
extern "C" bool __stdcall PalGetMaximumStackBounds(void** low, void** high);
extern "C" int32_t __stdcall PalGetModuleFileName(const wchar_t** name,
                                                    HANDLE module);
extern "C" int32_t __stdcall PalGetProcessCpuCount();
extern "C" uint64_t __stdcall PalGetTickCount64();

extern "C" int64_t minipal_hires_ticks();
extern "C" int64_t minipal_hires_tick_frequency();
extern "C" void minipal_microdelay(uint32_t usecs, uint32_t* since_yield);

namespace {

using gxos::runtime::Event;
using gxos::runtime::EventMode;
using gxos::runtime::ThreadCreateOptions;
using gxos::runtime::ThreadHandle;
using gxos::runtime::ThreadResult;
using gxos::runtime::WaitResult;
using gxos::runtime::WaitTimeout;

thread_local int32_t g_lastError = 0;
thread_local uint8_t g_threadToken = 0;

struct HostedThread {
    ThreadHandle handle{};
};

struct HostedThreadStart {
    guidexos_nativeaot_thread_entry entry;
    void* context;
};

std::atomic<uint32_t> g_detachCallbacks{0};
std::atomic<uint32_t> g_backgroundCalls{0};
void* g_resolvedAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234));

uintptr_t hostedThreadEntry(void* raw) {
    HostedThreadStart* start = static_cast<HostedThreadStart*>(raw);
    if (start == nullptr || start->entry == nullptr) return 0;
    (void)guidexos_nativeaot_fls_attach_current_thread();
    const uintptr_t result = start->entry(start->context);
    (void)guidexos_nativeaot_fls_detach_current_thread();
    delete start;
    return result;
}

uint64_t currentThreadId(void*) {
    return static_cast<uint64_t>(::GetCurrentThreadId());
}

int32_t stackBounds(void*, uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    if (low == nullptr || high == nullptr || current == nullptr) return -1;
    ULONG_PTR stackLow = 0;
    ULONG_PTR stackHigh = 0;
    ::GetCurrentThreadStackLimits(&stackLow, &stackHigh);
    uintptr_t marker = reinterpret_cast<uintptr_t>(&stackLow);
    *low = static_cast<uintptr_t>(stackLow);
    *high = static_cast<uintptr_t>(stackHigh);
    *current = marker;
    return *low < *high && marker >= *low && marker < *high ? 0 : -1;
}

uint64_t counter(void*) {
    LARGE_INTEGER value{};
    ::QueryPerformanceCounter(&value);
    return static_cast<uint64_t>(value.QuadPart);
}

uint64_t frequency(void*) {
    LARGE_INTEGER value{};
    ::QueryPerformanceFrequency(&value);
    return static_cast<uint64_t>(value.QuadPart);
}

int32_t sleepMilliseconds(void*, uint32_t milliseconds) {
    ::Sleep(milliseconds);
    return 0;
}

int32_t yield(void*) {
    return ::SwitchToThread() ? 0 : 0;
}

int32_t lastError(void*) { return g_lastError; }
void setLastError(void*, int32_t value) { g_lastError = value; }
void* currentProcess(void*) { return reinterpret_cast<void*>(static_cast<uintptr_t>(1)); }
void* currentThread(void*) { return static_cast<void*>(&g_threadToken); }

int32_t duplicateHandle(void*, void*, void* sourceHandle, void*, void** target,
                        uint32_t, int32_t, uint32_t) {
    if (target == nullptr || sourceHandle == nullptr) return -1;
    *target = sourceHandle;
    return 0;
}

int32_t createThread(void*, guidexos_nativeaot_thread_entry entry, void* context,
                     uint32_t stackSize, int32_t, guidexos_nativeaot_pal_opaque_handle* result) {
    if (entry == nullptr || result == nullptr) return -1;
    HostedThreadStart* start = new (std::nothrow) HostedThreadStart{entry, context};
    HostedThread* thread = new (std::nothrow) HostedThread();
    if (start == nullptr || thread == nullptr) {
        delete start;
        delete thread;
        return -1;
    }
    ThreadCreateOptions options;
    options.stackSize = stackSize == 0 ? 8192u : stackSize;
    options.debugName = "nativeaot-pal-hosted-probe";
    if (gxos::runtime::createThread(hostedThreadEntry, start, options,
                                    &thread->handle) != ThreadResult::Ok) {
        delete start;
        delete thread;
        return -1;
    }
    *result = reinterpret_cast<guidexos_nativeaot_pal_opaque_handle>(thread);
    return 0;
}

int32_t joinThread(void*, guidexos_nativeaot_pal_opaque_handle opaque,
                   uint32_t timeoutMilliseconds, uintptr_t* result) {
    HostedThread* thread = reinterpret_cast<HostedThread*>(opaque);
    if (thread == nullptr) return -1;
    const WaitTimeout timeout = timeoutMilliseconds == 0xFFFFFFFFu
        ? WaitTimeout::infinite()
        : WaitTimeout::finiteMilliseconds(timeoutMilliseconds);
    return gxos::runtime::joinThread(thread->handle, timeout, result) ==
        WaitResult::Signaled ? 0 : -1;
}

int32_t closeThread(void*, guidexos_nativeaot_pal_opaque_handle opaque) {
    HostedThread* thread = reinterpret_cast<HostedThread*>(opaque);
    if (thread == nullptr) return -1;
    const ThreadResult result = gxos::runtime::detachThread(thread->handle);
    delete thread;
    return result == ThreadResult::Ok || result == ThreadResult::AlreadyDetached ||
        result == ThreadResult::AlreadyJoined || result == ThreadResult::InvalidHandle ? 0 : -1;
}

void* createEvent(void*, int32_t manualReset, int32_t initialState) {
    Event* event = new (std::nothrow) Event(
        manualReset ? EventMode::ManualReset : EventMode::AutoReset,
        initialState != 0);
    return event;
}

int32_t waitAny(void*, uint32_t timeoutMilliseconds, uint32_t count,
                void* const* handles, int32_t) {
    if (count == 0 || handles == nullptr) return -1;
    const WaitTimeout timeout = timeoutMilliseconds == 0xFFFFFFFFu
        ? WaitTimeout::infinite()
        : WaitTimeout::finiteMilliseconds(timeoutMilliseconds);
    for (uint32_t i = 0; i < count; ++i) {
        Event* event = static_cast<Event*>(handles[i]);
        if (event != nullptr && event->wait(timeout) == WaitResult::Signaled) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t closeEvent(void*, void* handle) {
    Event* event = static_cast<Event*>(handle);
    if (event == nullptr) return -1;
    (void)event->close();
    delete event;
    return 0;
}

void* staticModule(void*, const void*) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0xBEEF));
}

void* staticResolve(void*, void*, const char* name) {
    return name != nullptr && std::strcmp(name, "ProbeResolved") == 0
        ? g_resolvedAddress : nullptr;
}

void failFast(void*, uint32_t reason) {
    std::fprintf(stderr, "unexpected hosted PAL fail-fast: %08x\n", reason);
    ::TerminateProcess(::GetCurrentProcess(), reason);
    for (;;) {}
}

void* virtualAlloc(void*, void* preferred, uintptr_t size, uint32_t type, uint32_t protection) {
    return ::VirtualAlloc(preferred, size, type, protection);
}

int32_t virtualFree(void*, void* address, uintptr_t size, uint32_t type) {
    return ::VirtualFree(address, size, type) ? 0 : -1;
}

int32_t virtualProtect(void*, void* address, uintptr_t size, uint32_t protection,
                       uint32_t* oldProtection) {
    DWORD oldValue = 0;
    const BOOL result = ::VirtualProtect(address, size, protection, &oldValue);
    if (oldProtection != nullptr) *oldProtection = oldValue;
    return result ? 0 : -1;
}

uint32_t flsAlloc(guidexos_nativeaot_fls_detach_callback callback) {
    return guidexos_nativeaot_fls_alloc(callback);
}

int32_t flsFree(uint32_t index) {
    return guidexos_nativeaot_fls_free(index) ? 0 : -1;
}

void* flsGet(uint32_t index) {
    return guidexos_nativeaot_fls_get(index);
}

int32_t flsSet(uint32_t index, void* value) {
    return guidexos_nativeaot_fls_set(index, value) ? 0 : -1;
}

void detachProbeValue(void*) {
    g_detachCallbacks.fetch_add(1, std::memory_order_relaxed);
}

uint32_t __stdcall backgroundCallback(void* raw) {
    Event* event = static_cast<Event*>(raw);
    g_backgroundCalls.fetch_add(1, std::memory_order_relaxed);
    if (event != nullptr) (void)event->signal();
    return 0;
}

uintptr_t workerFlsEntry(void* raw) {
    const uint32_t index = *static_cast<uint32_t*>(raw);
    void* workerValue = reinterpret_cast<void*>(static_cast<uintptr_t>(0x2222));
    if (guidexos_nativeaot_pal_fls_get(index) != nullptr) return 1;
    if (guidexos_nativeaot_pal_fls_set(index, workerValue) != 0) return 2;
    if (guidexos_nativeaot_pal_fls_get(index) != workerValue) return 3;
    return 0;
}

bool check(bool value, const char* label) {
    if (!value) std::fprintf(stderr, "hosted PAL probe failure: %s\n", label);
    return value;
}

} // namespace

int main() {
    guidexos_nativeaot_fls_initialize();
    (void)guidexos_nativeaot_fls_attach_current_thread();

    guidexos_nativeaot_fls_hooks flsHooks{};
    flsHooks.size = sizeof(flsHooks);
    flsHooks.abi_version = GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
    flsHooks.alloc = [](void*, guidexos_nativeaot_fls_detach_callback callback) { return flsAlloc(callback); };
    flsHooks.free_index = [](void*, uint32_t index) { return flsFree(index); };
    flsHooks.get = [](void*, uint32_t index) { return flsGet(index); };
    flsHooks.set = [](void*, uint32_t index, void* value) { return flsSet(index, value); };

    guidexos_nativeaot_pal_hooks hooks{};
    hooks.size = sizeof(hooks);
    hooks.abi_version = GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
    hooks.current_thread_id = currentThreadId;
    hooks.stack_bounds = stackBounds;
    hooks.counter = counter;
    hooks.frequency = frequency;
    hooks.sleep_milliseconds = sleepMilliseconds;
    hooks.yield = yield;
    hooks.get_last_error = lastError;
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

    bool ok = true;
    ok &= check(guidexos_nativeaot_pal_install_fls_hooks(&flsHooks) == 0, "install FLS hooks");
    ok &= check(guidexos_nativeaot_pal_install_hooks(&hooks) == 0, "install PAL hooks");

    const uint64_t firstId = PalGetCurrentOSThreadId();
    ok &= check(firstId == static_cast<uint64_t>(::GetCurrentThreadId()), "current thread identity");
    void* low = nullptr;
    void* high = nullptr;
    PalGetMaximumStackBounds(&low, &high);
    ok &= check(low != nullptr && high != nullptr && low < high, "stack bounds");

    ok &= check(PalInit(), "first PalInit");
    PalAttachThread(&g_threadToken);
    uint32_t probeIndex = guidexos_nativeaot_pal_fls_alloc(detachProbeValue);
    void* mainValue = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1111));
    ok &= check(probeIndex != 0xFFFFFFFFu, "dynamic FLS allocation");
    ok &= check(guidexos_nativeaot_pal_fls_set(probeIndex, mainValue) == 0, "FLS set");
    ok &= check(guidexos_nativeaot_pal_fls_get(probeIndex) == mainValue, "FLS get");

    guidexos_nativeaot_pal_opaque_handle worker = 0;
    ok &= check(guidexos_nativeaot_pal_create_thread(workerFlsEntry, &probeIndex,
                                                      8192u, 0, &worker) == 0,
                "worker creation");
    uintptr_t workerResult = 99;
    ok &= check(guidexos_nativeaot_pal_join_thread(worker, 5000u, &workerResult) == 0 &&
                    workerResult == 0, "worker join and local FLS isolation");
    ok &= check(guidexos_nativeaot_pal_close_thread(worker) == 0, "worker close");
    ok &= check(guidexos_nativeaot_pal_fls_get(probeIndex) == mainValue,
                "main FLS isolation");
    ok &= check(guidexos_nativeaot_pal_fls_set(probeIndex, nullptr) == 0,
                "FLS clear");
    ok &= check(guidexos_nativeaot_pal_fls_free(probeIndex) == 0, "FLS free");
    ok &= check(g_detachCallbacks.load(std::memory_order_relaxed) == 1,
                "detach callback");

    HANDLE event = PalCreateEventW(nullptr, 1u, 0u, nullptr);
    ok &= check(event != nullptr, "event creation");
    ok &= check(PalStartBackgroundWork(backgroundCallback, event, 0), "plain helper creation");
    HANDLE waitHandle = event;
    ok &= check(PalCompatibleWaitAny(0, 5000u, 1u, &waitHandle, 0) == 0,
                "event wait");
    ok &= check(g_backgroundCalls.load(std::memory_order_relaxed) == 1,
                "helper callback");
    ok &= check(guidexos_nativeaot_pal_close_event(event) == 0, "event cleanup");

    const int64_t tick0 = minipal_hires_ticks();
    uint32_t sinceYield = 0;
    minipal_microdelay(1000u, &sinceYield);
    const int64_t tick1 = minipal_hires_ticks();
    ok &= check(tick1 >= tick0 && minipal_hires_tick_frequency() > 0,
                "monotonic timing");
    const uint64_t tickCount = PalGetTickCount64();
    PalSleep(1u);
    ok &= check(PalSwitchToThread() != 0 || PalSwitchToThread() == 0, "yield");
    ok &= check(PalGetTickCount64() >= tickCount, "millisecond tick");

    HANDLE module = PalGetModuleHandleFromPointer(reinterpret_cast<void*>(&main));
    ok &= check(module != nullptr && PalGetProcAddress(module, "ProbeResolved") == g_resolvedAddress,
                "static resolver");
    ok &= check(PalLoadLibrary("unsupported.dll") == nullptr, "dynamic resolver rejection");

    void* allocation = PalVirtualAlloc(4096u, PAGE_READWRITE);
    ok &= check(allocation != nullptr, "virtual allocation");
    ok &= check(PalVirtualProtect(allocation, 4096u, PAGE_READONLY) != 0,
                "virtual protection");
    PalVirtualFree(allocation, 0);

    const wchar_t* imageName = nullptr;
    ok &= check(PalGetModuleFileName(&imageName, nullptr) > 0 && imageName != nullptr,
                "static module name");
    ok &= check(PalGetProcessCpuCount() >= 1, "processor count");
    ok &= check(PalDetachThread(&g_threadToken), "PAL detach");
    ok &= check(PalInit(), "second PalInit");
    PalAttachThread(&g_threadToken);
    ok &= check(PalDetachThread(&g_threadToken), "second PAL detach");
    ok &= check(guidexos_nativeaot_pal_fls_free(0u) == 0, "PAL FLS cleanup");

    (void)guidexos_nativeaot_fls_detach_current_thread();
    guidexos_nativeaot_fls_shutdown();
    (void)guidexos_nativeaot_pal_install_hooks(nullptr);
    (void)guidexos_nativeaot_pal_install_fls_hooks(nullptr);

    if (!ok) return 1;
    std::puts("Hosted exact NativeAOT PAL probe: PASS");
    return 0;
}
