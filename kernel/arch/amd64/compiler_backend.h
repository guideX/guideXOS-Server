//
// AMD64 lowering for the bounded bootstrap compiler IR.
//

#pragma once

#include "kernel/types.h"
#include "core/compiler/compiler_ir.h"

namespace kernel {
namespace compiler {
namespace amd64 {

static const uint32_t AMD64_BOOTSTRAP_CODE_BYTES = 6;
static const uint32_t AMD64_BOOTSTRAP_MAX_CODE_BYTES = COMPILER_MAX_CODE_BYTES;

struct FrameLayout {
    uint32_t frameBytes;
    int32_t contextDisplacement;
    uint32_t localBytes;
};

bool calculate_frame_layout(uint32_t localCount, FrameLayout* output);

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
