// AAPCS64 backend for the bounded Developer Studio bootstrap compiler.
#pragma once

#include "kernel/types.h"
#include "core/compiler/compiler_ir.h"

namespace kernel {
namespace compiler {
namespace arm64 {

static const uint32_t ARM64_MAX_BOOTSTRAP_BYTES = 512;

bool emit_function(const FunctionIR& function,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize);

} // namespace arm64
} // namespace compiler
} // namespace kernel
