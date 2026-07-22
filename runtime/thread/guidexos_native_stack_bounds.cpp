#include "guidexos_native_stack_bounds.h"

#if !defined(GXOS_BARE_METAL)
#if defined(_WIN32)
#include <windows.h>
#include <winternl.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace gxos {
namespace runtime {
namespace {

NativeStackBoundsPlatformHooks g_hooks = { nullptr, nullptr };

#if !defined(GXOS_BARE_METAL)
gxos_stack_bounds_uintptr currentStackPointer() {
#if defined(_MSC_VER)
    // The address of the return-address slot is an address in the current
    // frame and is the closest portable MSVC primitive available here.  The
    // Windows backend validates it against the exact TEB interval below.
    return reinterpret_cast<gxos_stack_bounds_uintptr>(
        _AddressOfReturnAddress());
#elif defined(__x86_64__)
    gxos_stack_bounds_uintptr value = 0;
    asm volatile ("mov %%rsp, %0" : "=r"(value));
    return value;
#elif defined(__aarch64__)
    gxos_stack_bounds_uintptr value = 0;
    asm volatile ("mov %0, sp" : "=r"(value));
    return value;
#elif defined(__i386__)
    gxos_stack_bounds_uintptr value = 0;
    asm volatile ("mov %%esp, %0" : "=r"(value));
    return value;
#else
    // A local object is still an exact address in the active stack frame, but
    // this fallback is intentionally reported as unavailable rather than
    // pretending to know the architectural SP.
    return 0;
#endif
}
#endif

#if !defined(GXOS_BARE_METAL)
StackBoundsResult queryHosted(NativeStackBounds* result) {
    if (result == nullptr) {
        return StackBoundsResult::InvalidOutput;
    }

#if defined(_WIN32)
    // This follows the locked NativeAOT PAL contract in
    // PalRedhawkCommon.cpp: VirtualQuery obtains the reservation's
    // AllocationBase and the TEB NT_TIB StackBase supplies the high endpoint.
    // AllocationBase includes the reserved stack interval, including any
    // guard-page portion; it is not a guessed window around RSP.
    unsigned char marker = 0;
    MEMORY_BASIC_INFORMATION memory = {};
    if (VirtualQuery(&marker, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.AllocationBase == nullptr) {
        return StackBoundsResult::Unavailable;
    }
    NT_TIB* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (tib == nullptr || tib->StackBase == nullptr) {
        return StackBoundsResult::Unavailable;
    }
    result->low = reinterpret_cast<gxos_stack_bounds_uintptr>(
        memory.AllocationBase);
    result->high = reinterpret_cast<gxos_stack_bounds_uintptr>(tib->StackBase);
    result->current = currentStackPointer();
    return StackBoundsResult::Success;
#elif defined(__unix__) || defined(__APPLE__)
    pthread_attr_t attributes;
    if (pthread_getattr_np(pthread_self(), &attributes) != 0) {
        return StackBoundsResult::Unavailable;
    }
    void* stackAddress = nullptr;
    size_t stackSize = 0;
    const int stackResult = pthread_attr_getstack(
        &attributes, &stackAddress, &stackSize);
    (void)pthread_attr_destroy(&attributes);
    if (stackResult != 0 || stackAddress == nullptr || stackSize == 0) {
        return StackBoundsResult::Unavailable;
    }
    result->low = reinterpret_cast<gxos_stack_bounds_uintptr>(stackAddress);
    result->high = result->low + static_cast<gxos_stack_bounds_uintptr>(stackSize);
    if (result->high < result->low) {
        return StackBoundsResult::InvalidBounds;
    }
    result->current = currentStackPointer();
    return StackBoundsResult::Success;
#else
    (void)result;
    return StackBoundsResult::Unavailable;
#endif
}
#endif

StackBoundsResult validate(NativeStackBounds* result,
                           StackBoundsResult resultCode) {
    if (result == nullptr) {
        return StackBoundsResult::InvalidOutput;
    }
    if (resultCode != StackBoundsResult::Success) {
        return resultCode;
    }
    if (result->low >= result->high) {
        return StackBoundsResult::InvalidBounds;
    }
    if (result->current < result->low || result->current >= result->high) {
        return StackBoundsResult::CurrentPointerOutsideBounds;
    }
    return StackBoundsResult::Success;
}

} // namespace

void installNativeStackBoundsPlatformHooks(
    const NativeStackBoundsPlatformHooks* hooks) {
    g_hooks = hooks == nullptr
        ? NativeStackBoundsPlatformHooks{ nullptr, nullptr }
        : *hooks;
}

StackBoundsResult queryCurrentNativeStackBounds(NativeStackBounds* result) {
    if (result == nullptr) {
        return StackBoundsResult::InvalidOutput;
    }
    *result = NativeStackBounds{};

    StackBoundsResult raw = StackBoundsResult::Unavailable;
    if (g_hooks.query != nullptr) {
        raw = g_hooks.query(g_hooks.context, result);
    }
#if defined(GXOS_BARE_METAL)
    else {
        raw = StackBoundsResult::NoCurrentThread;
    }
#else
    else {
        raw = queryHosted(result);
    }
#endif
    return validate(result, raw);
}

const char* stackBoundsResultName(StackBoundsResult result) {
    switch (result) {
        case StackBoundsResult::Success: return "Success";
        case StackBoundsResult::InvalidOutput: return "InvalidOutput";
        case StackBoundsResult::NoCurrentThread: return "NoCurrentThread";
        case StackBoundsResult::Unavailable: return "Unavailable";
        case StackBoundsResult::InvalidBounds: return "InvalidBounds";
        case StackBoundsResult::CurrentPointerOutsideBounds:
            return "CurrentPointerOutsideBounds";
    }
    return "Unknown";
}

} // namespace runtime
} // namespace gxos
