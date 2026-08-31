//
// Bare-metal NativeElf loader and diagnostic execution route.
//
#pragma once

#include <stdint.h>

#include "native_elf_validator.h"
#include "native_elf_runtime.h"

namespace kernel {
namespace native_elf {

struct NativeElfExecutionContext {
    uint64_t pageTableRoot;
    uint64_t regionBase;
    uint64_t regionSize;
};

struct NativeElfRunReport {
    bool success;
    int32_t returnValue;
    uint64_t imageBase;
    uint64_t entryPoint;
    uint64_t mappedBytes;
    bool nxEnabled;
    bool dedicatedStackUsed;
    bool appContextValid;
    bool hostLogObserved;
    bool teardownComplete;
    uint64_t kernelRspBefore;
    uint64_t applicationStackBase;
    uint64_t applicationStackTop;
    uint64_t applicationRsp;
    uint64_t kernelRspAfter;
    uint64_t readOnlyDataBase;
    uint64_t readOnlyDataBytes;
    uint32_t hostLogBytes;
    NativeAppExecutionState finalState;
    uint32_t hostLogCount;
    bool hostLogTruncated;
    char hostLog[NATIVE_APP_MAX_LOG_LINES][NATIVE_APP_MAX_LOG_LINE_BYTES];
    const char* error;

    // Append-only runtime diagnostics.  Existing report offsets remain stable.
    NativeRuntimeStatus runtimeStatus;
    uint32_t runtimeCallDepth;
};

bool configure_execution_context(const NativeElfExecutionContext& context);
bool execution_context_configured();

// Loads one validated ET_EXEC NativeElf file into the reusable reserved
// window, invokes its validated gx_main(gx_app_context*) entry on the
// dedicated bootstrap stack, and returns only after control has returned to
// the kernel and teardown has completed.
bool run_file(const char* path, int32_t* returnValue, NativeElfRunReport* report = nullptr);

} // namespace native_elf
} // namespace kernel
