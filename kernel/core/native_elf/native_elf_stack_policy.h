//
// Shared NativeElf/compiler stack-safety policy.
//
// Keep the compiler's conservative activation bound and the runtime's fixed
// application-stack contract in one header.  The runtime guard is deliberately
// derived from these constants; it is not a tuning value chosen independently
// of generated-code accounting.
//
#pragma once

#include <stdint.h>

#include "../compiler/compiler_ir.h"

namespace kernel {
namespace native_elf {

static const uint64_t NATIVE_ELF_APPLICATION_STACK_SIZE = 64ULL * 1024ULL;
static const uint32_t NATIVE_ELF_RUNTIME_SAFETY_RESERVE_BYTES = 8192U;
// The trampoline reserves 32-byte Microsoft x64 shadow space and the target
// entry CALL contributes one return address before gx_main starts.
static const uint32_t NATIVE_ELF_TRAMPOLINE_ENTRY_OVERHEAD_BYTES = 0x28U;
static const uint32_t NATIVE_ELF_RETURN_ADDRESS_BYTES = 8U;
static const uint32_t NATIVE_ELF_SAVED_RBP_BYTES = 8U;
static const uint32_t NATIVE_ELF_MAX_OUTGOING_CALL_RESERVE_BYTES = 40U;

} // namespace native_elf

namespace compiler {

// A binary expression keeps one 8-byte value pushed while its right subtree is
// evaluated.  The parser bounds expression nesting at 16, so this is the
// conservative maximum for the remaining push/pop based expression lowering.
static const uint32_t COMPILER_MAX_TRANSIENT_STACK_BYTES =
    COMPILER_MAX_EXPRESSION_NESTING * 8U;

// frame_size is 40 bytes of fixed compiler frame area plus the largest legal
// parameter/local-array/temporary storage, rounded to the required 16-byte
// boundary. Phase 27Q keeps aggregate local storage at 256 bytes.
static const uint32_t COMPILER_MAX_GENERATED_FRAME_BYTES = 576U;
static const uint32_t COMPILER_MAX_GENERATED_ACTIVATION_STACK_COST =
    native_elf::NATIVE_ELF_RETURN_ADDRESS_BYTES +
    native_elf::NATIVE_ELF_SAVED_RBP_BYTES +
    COMPILER_MAX_GENERATED_FRAME_BYTES +
    COMPILER_MAX_TRANSIENT_STACK_BYTES +
    native_elf::NATIVE_ELF_MAX_OUTGOING_CALL_RESERVE_BYTES;

// gx_main is activation depth 1.  Every source-defined call increments the
// depth before its E8 CALL.  The final subtraction is intentionally integer
// floor division, leaving at least the complete runtime reserve unused.
static const uint32_t COMPILER_MAX_RUNTIME_CALL_DEPTH =
    static_cast<uint32_t>(
        (native_elf::NATIVE_ELF_APPLICATION_STACK_SIZE -
         native_elf::NATIVE_ELF_RUNTIME_SAFETY_RESERVE_BYTES -
         native_elf::NATIVE_ELF_TRAMPOLINE_ENTRY_OVERHEAD_BYTES) /
        COMPILER_MAX_GENERATED_ACTIVATION_STACK_COST);

static_assert(COMPILER_MAX_GENERATED_FRAME_BYTES ==
                  ((40U + COMPILER_MAX_PARAMETERS * 4U +
                    COMPILER_MAX_LOCAL_STORAGE_BYTES + COMPILER_MAX_TEMPORARY_SLOTS * 4U +
                    15U) & ~15U),
              "compiler frame bound must match the legal IR maxima");
static_assert(COMPILER_MAX_RUNTIME_CALL_DEPTH == 75U,
              "review the recursion limit when stack policy changes");
static_assert(COMPILER_MAX_RUNTIME_CALL_DEPTH *
                  COMPILER_MAX_GENERATED_ACTIVATION_STACK_COST +
                  native_elf::NATIVE_ELF_TRAMPOLINE_ENTRY_OVERHEAD_BYTES +
                  native_elf::NATIVE_ELF_RUNTIME_SAFETY_RESERVE_BYTES <=
                  native_elf::NATIVE_ELF_APPLICATION_STACK_SIZE,
              "runtime call-depth policy must fit the NativeElf stack");
static_assert((COMPILER_MAX_RUNTIME_CALL_DEPTH + 1U) *
                  COMPILER_MAX_GENERATED_ACTIVATION_STACK_COST +
                  native_elf::NATIVE_ELF_TRAMPOLINE_ENTRY_OVERHEAD_BYTES +
                  native_elf::NATIVE_ELF_RUNTIME_SAFETY_RESERVE_BYTES >
                  native_elf::NATIVE_ELF_APPLICATION_STACK_SIZE,
              "runtime call-depth policy must reject the next activation");

} // namespace compiler
} // namespace kernel
