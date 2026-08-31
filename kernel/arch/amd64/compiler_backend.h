//
// AMD64 lowering for the bounded bootstrap compiler IR.
//

#pragma once

#include "kernel/types.h"
#include "core/compiler/compiler_ir.h"
#include "core/native_elf/native_elf_stack_policy.h"

namespace kernel {
namespace compiler {
namespace amd64 {

static const uint32_t AMD64_BOOTSTRAP_CODE_BYTES = 6;
static const uint32_t AMD64_BOOTSTRAP_MAX_CODE_BYTES = COMPILER_MAX_CODE_BYTES;
static const uint32_t COMPILER_MAX_BRANCH_LABELS = 128;
static const uint32_t COMPILER_MAX_BRANCH_FIXUPS = 128;

struct FrameLayout {
    uint32_t frameBytes;
    int32_t contextDisplacement;
    uint32_t localBytes;
    uint32_t parameterBytes;
    uint32_t temporaryBytes;
    uint32_t variableBytes;
    uint32_t transientBytes;
    uint32_t activationBytes;
    uint16_t temporarySlots;
};

// Calculate an AMD64 signed rel32 displacement without allowing unsigned
// wraparound or silent truncation.  Both inputs are offsets/addresses of the
// branch target and the first byte after the rel32 field.
bool calculate_signed_rel32(uint64_t targetAddress,
                            uint64_t addressAfterBranch,
                            int32_t* displacement);

bool calculate_frame_layout(uint32_t localCount, FrameLayout* output);

bool calculate_frame_layout(uint32_t parameterCount, uint32_t localCount,
                            uint32_t temporarySlots, bool hasContext,
                            FrameLayout* output);

bool calculate_frame_layout(uint32_t parameterCount, uint32_t localCount,
                            uint32_t temporarySlots, bool hasContext,
                            uint32_t transientBytes, FrameLayout* output);

bool emit_translation_unit(const TranslationUnitIR& unit,
                           uint64_t readOnlyDataAddress,
                           uint8_t* output,
                           uint32_t outputCapacity,
                           uint32_t* outputSize,
                           uint32_t* entryCodeOffset);

// Module emission leaves cross-translation-unit calls and data addresses as
// bounded internal relocations. Calls to definitions in the same translation
// unit continue to use local branch fixups.
bool emit_translation_unit_module(const TranslationUnitIR& unit,
                                  uint8_t* output,
                                  uint32_t outputCapacity,
                                  uint32_t* outputSize,
                                  uint32_t* entryCodeOffset,
                                  RelocationRecord* relocations,
                                  uint32_t relocationCapacity,
                                  uint32_t* relocationCount);

bool emit_function(const FunctionIR& function,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize);

bool emit_function(const FunctionIR& function,
                   uint64_t readOnlyDataAddress,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize);

} // namespace amd64
} // namespace compiler
} // namespace kernel
