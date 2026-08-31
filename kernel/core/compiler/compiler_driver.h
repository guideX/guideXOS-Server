//
// Bare-metal compiler bootstrap driver.
//

#pragma once

#include "kernel/types.h"
#include "compiler_diagnostics.h"
#include "compiler_ir.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_MAX_SOURCE_BYTES = 64 * 1024;
static const uint32_t COMPILER_MAX_OUTPUT_BYTES = 98304;
static const uint32_t COMPILER_MAX_DATA_BYTES = COMPILER_MAX_LINKED_DATA_BYTES;
static const uint32_t COMPILER_MAX_DIAGNOSTIC_TOKEN_BYTES = 32;
static const uint32_t COMPILER_MAX_DIAGNOSTIC_PATH_BYTES = COMPILER_MAX_SOURCE_PATH_BYTES;

struct CompileDiagnostic {
    SourceLocation location;
    char message[COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES];
    char tokenKind[COMPILER_MAX_DIAGNOSTIC_TOKEN_BYTES];
    char sourcePath[COMPILER_MAX_DIAGNOSTIC_PATH_BYTES];
};

struct CompileSummary {
    bool success;
    bool reopenedAndValidated;
    bool hasHostLog;
    uint32_t sourceBytes;
    uint32_t tokenCount;
    uint32_t functionCount;
    uint16_t recursiveSccCount;
    bool recursiveFunction[COMPILER_MAX_FUNCTIONS];
    uint32_t entryCodeOffset;
    bool returnConstantValid;
    int32_t returnConstant;
    uint32_t codeBytes;
    uint8_t code[COMPILER_MAX_LINKED_CODE_BYTES];
    uint32_t dataBytes;
    uint8_t data[COMPILER_MAX_DATA_BYTES];
    uint32_t outputBytes;
    uint64_t sourceHash;
    uint64_t outputHash;
    uint64_t reopenedHash;
    uint64_t dataHash;
    uint32_t diagnosticCount;
    bool diagnosticsTruncated;
    CompileDiagnostic diagnostics[COMPILER_MAX_DIAGNOSTICS];
};

bool compile(const char* sourcePath,
             const char* outputPath,
             CompileSummary* summary);

// Compile each source path as an independent translation unit, then link the
// resulting bounded in-memory modules into one NativeElf application.
bool compile_project(const char* const* sourcePaths,
                     uint32_t sourceCount,
                     const char* outputPath,
                     CompileSummary* summary);

// Opt-in startup proof used by the Phase 27B QEMU smoke.  Normal boots do not
// call this routine; NativeElf execution is exercised by the separate
// Phase 27C/27D smoke route.
void run_bootstrap_smoke();

} // namespace compiler
} // namespace kernel
