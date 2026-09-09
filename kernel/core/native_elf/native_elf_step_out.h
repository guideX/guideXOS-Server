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

#include "native_elf_frame_policy.h"

namespace kernel {
namespace native_elf {

static const uint32_t kNativeElfStepOutInstructionLimit = 128U;
static const uint32_t kNativeElfStepOutMaxFrameDistance = kNativeElfMaxFrameDistance;
static const uint32_t kNativeElfStepOutFrameLinkBytes = kNativeElfFrameLinkBytes;

inline bool step_out_stack_range_contains(uint64_t stackLow, uint64_t stackHigh,
                                          uint64_t address, uint64_t bytes)
{
    return native_elf_stack_range_contains(stackLow, stackHigh, address, bytes);
}

inline bool step_out_frame_shape_valid(uint64_t rsp, uint64_t rbp,
                                       uint64_t stackLow, uint64_t stackHigh)
{
    return native_elf_frame_shape_valid(rsp, rbp, stackLow, stackHigh);
}

inline bool step_out_caller_link_valid(uint64_t currentRbp, uint64_t savedCallerRbp,
                                       uint64_t returnAddress,
                                       uint64_t stackLow, uint64_t stackHigh,
                                       uint64_t imageBase, uint64_t imageSize)
{
    return native_elf_caller_link_valid(currentRbp, savedCallerRbp, returnAddress,
                                        stackLow, stackHigh, imageBase, imageSize);
}

inline bool step_out_caller_frame_matches(uint64_t rsp, uint64_t rbp,
                                          uint64_t expectedRbp,
                                          uint64_t stackLow, uint64_t stackHigh)
{
    return native_elf_caller_frame_matches(rsp, rbp, expectedRbp, stackLow, stackHigh);
}

} // namespace native_elf
} // namespace kernel
