#pragma once

#include <stdint.h>

namespace kernel {
namespace app {

// Keep window sizing arithmetic in a wider type until the per-window and
// compositor minimums have been applied. Pointer coordinates are signed and
// can span the complete int32 range.
static inline int32_t ClampWindowDimensionToMinimum(int64_t requested,
    int32_t windowMinimum, int32_t compositorMinimum)
{
    const int32_t minimum = windowMinimum > compositorMinimum
        ? windowMinimum : compositorMinimum;
    if (requested < minimum) return minimum;
    if (requested > 0x7fffffffLL) return static_cast<int32_t>(0x7fffffff);
    return static_cast<int32_t>(requested);
}

static inline int32_t WindowDimensionAfterResize(int32_t startDimension,
    int32_t pointerPosition, int32_t pointerStart,
    int32_t windowMinimum, int32_t compositorMinimum)
{
    const int64_t requested = static_cast<int64_t>(startDimension) +
        static_cast<int64_t>(pointerPosition) - static_cast<int64_t>(pointerStart);
    return ClampWindowDimensionToMinimum(requested, windowMinimum, compositorMinimum);
}

} // namespace app
} // namespace kernel
