//
// AMD64 backend for the bare-metal compiler bootstrap.
//

#pragma once

#include "kernel/types.h"
#include "core/compiler/compiler_ir.h"

namespace kernel {
namespace compiler {
namespace amd64 {

static const uint32_t AMD64_BOOTSTRAP_CODE_BYTES = 6;

bool emit_function(const FunctionIR& function,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize);

} // namespace amd64
} // namespace compiler
} // namespace kernel
