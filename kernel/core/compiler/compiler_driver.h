//
// Bare-metal compiler bootstrap driver.
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_MAX_SOURCE_BYTES = 64 * 1024;
static const uint32_t COMPILER_MAX_OUTPUT_BYTES = 12288;
static const uint32_t COMPILER_MAX_CODE_BYTES = 128;
static const uint32_t COMPILER_MAX_DATA_BYTES = 256;

struct CompileSummary {
    bool success;
    bool reopenedAndValidated;
    bool hasHostLog;
    uint32_t sourceBytes;
    uint32_t tokenCount;
    int32_t returnConstant;
    uint32_t codeBytes;
    uint8_t code[COMPILER_MAX_CODE_BYTES];
    uint32_t dataBytes;
    uint8_t data[COMPILER_MAX_DATA_BYTES];
    uint32_t outputBytes;
    uint64_t sourceHash;
    uint64_t outputHash;
    uint64_t reopenedHash;
    uint64_t dataHash;
};

bool compile(const char* sourcePath,
             const char* outputPath,
             CompileSummary* summary);

// Opt-in startup proof used by the Phase 27B QEMU smoke.  Normal boots do not
// call this routine; NativeElf execution is exercised by the separate
// Phase 27C/27D smoke route.
void run_bootstrap_smoke();

} // namespace compiler
} // namespace kernel
