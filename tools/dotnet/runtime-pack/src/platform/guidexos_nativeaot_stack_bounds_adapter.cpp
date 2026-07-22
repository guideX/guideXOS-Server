#include "guidexos_nativeaot_stack_bounds_adapter.h"

namespace guidexos {
namespace nativeaot {
namespace pal {

bool getMaximumStackBounds(gxos::runtime::NativeStackBounds* result) {
    return gxos::runtime::queryCurrentNativeStackBounds(result) ==
        gxos::runtime::StackBoundsResult::Success;
}

} // namespace pal
} // namespace nativeaot
} // namespace guidexos

extern "C" int guidexos_nativeaot_pal_get_maximum_stack_bounds(
    std::uintptr_t* stackLow,
    std::uintptr_t* stackHigh,
    std::uintptr_t* currentStackPointer) {
    if (stackLow == nullptr || stackHigh == nullptr ||
        currentStackPointer == nullptr) {
        return 0;
    }
    gxos::runtime::NativeStackBounds bounds{};
    if (!guidexos::nativeaot::pal::getMaximumStackBounds(&bounds)) {
        *stackLow = 0;
        *stackHigh = 0;
        *currentStackPointer = 0;
        return 0;
    }
    *stackLow = bounds.low;
    *stackHigh = bounds.high;
    *currentStackPointer = bounds.current;
    return 1;
}
