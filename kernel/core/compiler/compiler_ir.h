//
// Target-neutral IR for the bare-metal compiler bootstrap.
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_FUNCTION_NAME_CAPACITY = 16;
static const uint32_t COMPILER_LOG_MESSAGE_CAPACITY = 128;
static const uint32_t COMPILER_MAX_LOG_CALLS = 4;

struct FunctionIR {
    char name[COMPILER_FUNCTION_NAME_CAPACITY];
    int32_t returnConstant;
    bool hasLogCall;
    char logMessage[COMPILER_LOG_MESSAGE_CAPACITY];
    uint32_t logCount;
    char logMessages[COMPILER_MAX_LOG_CALLS][COMPILER_LOG_MESSAGE_CAPACITY];
};

} // namespace compiler
} // namespace kernel
