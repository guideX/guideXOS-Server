//
// Target-neutral IR for the bare-metal compiler bootstrap.
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_FUNCTION_NAME_CAPACITY = 16;
static const uint32_t COMPILER_PARAMETER_NAME_CAPACITY = 32;
static const uint32_t COMPILER_MAX_STRING_LITERAL_BYTES = 255;

struct FunctionIR {
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    bool usesAppContext;
    bool hasHostLog;
    uint32_t logMessageBytes;
    char logMessage[COMPILER_MAX_STRING_LITERAL_BYTES + 1];
    int32_t returnConstant;
};

} // namespace compiler
} // namespace kernel
