//
// Shared AMD64 frame-chain policy for NativeElf debugger inspection.
//
// This policy is intentionally small and side-effect free.  Source Step Out
// and Call Stack inspection must reject the same malformed frame links and
// must never turn an untrusted RBP into an unbounded memory read.
//
#pragma once

#include <stdint.h>

namespace kernel {
namespace native_elf {

static const uint32_t kNativeElfFrameLinkBytes = 16U;
static const uint32_t kNativeElfMaxFrameDistance = 4096U;

inline bool native_elf_stack_range_contains(uint64_t stackLow, uint64_t stackHigh,
                                            uint64_t address, uint64_t bytes)
{
    return stackHigh > stackLow && address >= stackLow && address < stackHigh &&
        bytes <= stackHigh - address;
}

inline bool native_elf_frame_shape_valid(uint64_t rsp, uint64_t rbp,
                                         uint64_t stackLow, uint64_t stackHigh)
{
    return (rbp & 7ULL) == 0 &&
        native_elf_stack_range_contains(stackLow, stackHigh, rsp, 1) &&
        native_elf_stack_range_contains(stackLow, stackHigh, rbp,
                                        kNativeElfFrameLinkBytes) &&
        rbp >= rsp && rbp - rsp <= kNativeElfMaxFrameDistance;
}

inline bool native_elf_frame_pointer_valid(uint64_t rbp,
                                           uint64_t stackLow, uint64_t stackHigh)
{
    return (rbp & 7ULL) == 0 &&
        native_elf_stack_range_contains(stackLow, stackHigh, rbp,
                                        kNativeElfFrameLinkBytes);
}

inline bool native_elf_caller_link_valid(uint64_t currentRbp, uint64_t savedCallerRbp,
                                         uint64_t returnAddress,
                                         uint64_t stackLow, uint64_t stackHigh,
                                         uint64_t imageBase, uint64_t imageSize)
{
    if (stackHigh <= stackLow) return false;
    if (savedCallerRbp == 0 || (savedCallerRbp & 7ULL) != 0 ||
        savedCallerRbp <= currentRbp ||
        savedCallerRbp - currentRbp > kNativeElfMaxFrameDistance ||
        !native_elf_stack_range_contains(stackLow, stackHigh, savedCallerRbp,
                                         kNativeElfFrameLinkBytes) ||
        returnAddress == 0 || imageSize == 0 ||
        imageBase > ~static_cast<uint64_t>(0) - imageSize) return false;
    return returnAddress >= imageBase && returnAddress < imageBase + imageSize;
}

inline bool native_elf_caller_frame_matches(uint64_t rsp, uint64_t rbp,
                                            uint64_t expectedRbp,
                                            uint64_t stackLow, uint64_t stackHigh)
{
    return rbp == expectedRbp &&
        native_elf_frame_shape_valid(rsp, rbp, stackLow, stackHigh);
}

} // namespace native_elf
} // namespace kernel
