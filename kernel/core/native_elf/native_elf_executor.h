//
// Controlled AMD64 NativeElf entry invocation.
//
#pragma once

#include <stdint.h>

namespace kernel {
namespace native_elf {

// The caller must have completed NativeElf validation before using this
// wrapper.  The entry type is explicitly Microsoft x64 ABI on GCC/Clang so
// the bootstrap compiler and kernel agree about RCX and shadow space.
bool invoke_validated_native_entry(uint64_t entryPoint,
                                   void* context,
                                   int32_t* returnValue);

} // namespace native_elf
} // namespace kernel
