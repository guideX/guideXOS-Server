//
// Target-neutral IR for the bare-metal compiler bootstrap.
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_FUNCTION_NAME_CAPACITY = 16;

struct FunctionIR {
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    int32_t returnConstant;
};

} // namespace compiler
} // namespace kernel
