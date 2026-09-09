//
// Pure ABI-aware policy for the bounded NativeElf source Step Out operation.
//
// The compiler owns a fixed AMD64 frame shape in every framed multi-function
// translation unit.  Keep the arithmetic and provenance checks independent of
// the run service so host tests can exercise the safety boundary without
// executing kernel code.
//
#pragma once

#include <stdint.h>

namespace kernel {
namespace native_elf {

static const uint32_t kNativeElfStepOutInstructionLimit = 128U;
static const uint32_t kNativeElfStepOutMaxFrameDistance = 4096U;
static const uint32_t kNativeElfStepOutFrameLinkBytes = 16U;

inline bool step_out_stack_range_contains(uint64_t stackLow, uint64_t stackHigh,
                                          uint64_t address, uint64_t bytes)
{
    return stackHigh > stackLow && address >= stackLow && address < stackHigh &&
        bytes <= stackHigh - address;
}

inline bool step_out_frame_shape_valid(uint64_t rsp, uint64_t rbp,
                                       uint64_t stackLow, uint64_t stackHigh)
{
    return (rbp & 7ULL) == 0 &&
        step_out_stack_range_contains(stackLow, stackHigh, rsp, 1) &&
        step_out_stack_range_contains(stackLow, stackHigh, rbp,
                                       kNativeElfStepOutFrameLinkBytes) &&
        rbp >= rsp && rbp - rsp <= kNativeElfStepOutMaxFrameDistance;
}

inline bool step_out_caller_link_valid(uint64_t currentRbp, uint64_t savedCallerRbp,
                                       uint64_t returnAddress,
                                       uint64_t stackLow, uint64_t stackHigh,
                                       uint64_t imageBase, uint64_t imageSize)
{
    if (stackHigh <= stackLow) return false;
    if (savedCallerRbp == 0 || (savedCallerRbp & 7ULL) != 0 ||
        savedCallerRbp <= currentRbp ||
        !step_out_stack_range_contains(stackLow, stackHigh, savedCallerRbp,
                                        kNativeElfStepOutFrameLinkBytes) ||
        returnAddress == 0 || imageSize == 0 ||
        imageBase > ~static_cast<uint64_t>(0) - imageSize) return false;
    return returnAddress >= imageBase && returnAddress < imageBase + imageSize;
}

inline bool step_out_caller_frame_matches(uint64_t rsp, uint64_t rbp,
                                          uint64_t expectedRbp,
                                          uint64_t stackLow, uint64_t stackHigh)
{
    return rbp == expectedRbp &&
        step_out_frame_shape_valid(rsp, rbp, stackLow, stackHigh);
}

} // namespace native_elf
} // namespace kernel
