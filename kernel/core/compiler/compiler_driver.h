//
// Bare-metal compiler bootstrap driver.
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_MAX_SOURCE_BYTES = 64 * 1024;
static const uint32_t COMPILER_MAX_OUTPUT_BYTES = 8192;

struct CompileSummary {
    bool success;
    bool reopenedAndValidated;
    uint32_t sourceBytes;
    uint32_t tokenCount;
    int32_t returnConstant;
    uint32_t codeBytes;
    uint8_t code[16];
    uint32_t outputBytes;
    uint64_t sourceHash;
    uint64_t outputHash;
    uint64_t reopenedHash;
};

bool compile(const char* sourcePath,
             const char* outputPath,
             CompileSummary* summary);

// Opt-in startup proof used by the Phase 27B QEMU smoke.  Normal boots do not
// call this routine and generated applications are never executed here.
void run_bootstrap_smoke();

} // namespace compiler
} // namespace kernel
