//
// Controlled AMD64 NativeElf entry invocation.
//
#pragma once

#include <stdint.h>

#include "native_elf_runtime.h"

namespace kernel {
namespace native_elf {

// The caller must have completed NativeElf validation before using this
// wrapper.  The entry type is explicitly Microsoft x64 ABI on GCC/Clang so
// the bootstrap compiler and kernel agree about RCX and shadow space.
bool invoke_validated_native_entry(uint64_t entryPoint,
                                   void* context,
                                   int32_t* returnValue);

// Explicit Microsoft x64 ABI stack-switching trampoline.  The caller passes
// entry in RCX, context in RDX, application stack top in R8, and result in R9.
// The NASM implementation saves/restores all Microsoft nonvolatile GPRs and
// XMM6-XMM15, supplies the target's 32-byte shadow space plus alignment, and
// returns only after restoring the caller's original RSP.
extern "C" bool invoke_native_entry_on_stack(uint64_t entryPoint,
                                               void* context,
                                               uint64_t applicationStackTop,
                                               NativeElfTrampolineResult* result);

} // namespace native_elf
} // namespace kernel
