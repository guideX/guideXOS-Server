#pragma once

// Runtime-neutral current native-thread stack-bound reporting.
//
// The returned interval is the maximum stack reservation owned by the current
// native thread.  `low` is inclusive and `high` is exclusive.  The current
// stack pointer is reported independently and must be inside [low, high).
// This interface reports facts only; it does not allocate, grow, or mutate a
// stack and has no language-runtime or scheduler dependency.

#if defined(GXOS_BARE_METAL)
#include <stdint.h>
using gxos_stack_bounds_uintptr = uintptr_t;
#else
#include <cstdint>
using gxos_stack_bounds_uintptr = std::uintptr_t;
#endif

namespace gxos {
namespace runtime {

struct NativeStackBounds {
    gxos_stack_bounds_uintptr low = 0;
    gxos_stack_bounds_uintptr high = 0;
    gxos_stack_bounds_uintptr current = 0;
};

enum class StackBoundsResult {
    Success,
    InvalidOutput,
    NoCurrentThread,
    Unavailable,
    InvalidBounds,
    CurrentPointerOutsideBounds
};

// Bare-metal schedulers and other freestanding hosts provide the current
// execution-context lookup here.  Hosted builds use the private platform
// implementation in guidexos_native_stack_bounds.cpp unless hooks are
// explicitly installed by a test or host adapter.
struct NativeStackBoundsPlatformHooks {
    void* context;
    StackBoundsResult (*query)(void* context, NativeStackBounds* result);
};

void installNativeStackBoundsPlatformHooks(
    const NativeStackBoundsPlatformHooks* hooks);

StackBoundsResult queryCurrentNativeStackBounds(NativeStackBounds* result);

const char* stackBoundsResultName(StackBoundsResult result);

} // namespace runtime
} // namespace gxos
