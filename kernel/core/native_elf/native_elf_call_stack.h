//
// Bounded NativeElf Call Stack inspection policy.
//
// The service implementation supplies the paused register context and reads
// memory through its validated NativeElf address window.  These helpers keep
// the depth and cycle guards available to both kernel code and host tests.
//
#pragma once

#include <stdint.h>

#include "../../../sdk/include/guidexos/development_debug.h"
#include "native_elf_frame_policy.h"

namespace kernel {
namespace native_elf {

static const uint32_t kNativeElfCallStackMaxFrames =
    GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES;

inline bool native_elf_call_stack_frame_pointer_seen(const uint64_t* framePointers,
                                                     uint32_t frameCount,
                                                     uint64_t candidate)
{
    if (!framePointers || candidate == 0) return false;
    for (uint32_t index = 0; index < frameCount; ++index) {
        if (framePointers[index] == candidate) return true;
    }
    return false;
}

inline bool native_elf_call_stack_can_append(uint32_t frameCount)
{
    return frameCount < kNativeElfCallStackMaxFrames;
}

} // namespace native_elf
} // namespace kernel
