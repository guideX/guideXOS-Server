#pragma once

// Inactive NativeAOT PAL stack-bound adapter.  The public generic API remains
// runtime-neutral; this adapter is the isolated NativeAOT spelling that a
// future PalRedhawk implementation can call.

#if defined(GXOS_BARE_METAL)
#include <stdint.h>
namespace std {
using ::uintptr_t;
}
#else
#include <cstdint>
#endif

#include "../../../../../runtime/thread/guidexos_native_stack_bounds.h"

namespace guidexos {
namespace nativeaot {
namespace pal {

bool getMaximumStackBounds(
    gxos::runtime::NativeStackBounds* result);

} // namespace pal
} // namespace nativeaot
} // namespace guidexos

extern "C" int guidexos_nativeaot_pal_get_maximum_stack_bounds(
    std::uintptr_t* stackLow,
    std::uintptr_t* stackHigh,
    std::uintptr_t* currentStackPointer);
