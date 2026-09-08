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

    // Append-only compositor evidence captured before loader teardown.
    bool guiApplicationCreated;
    bool guiWindowCreated;
    bool guiRendered;
    bool guiClosedNormally;
    uint32_t guiWindowId;
    uint32_t guiRenderCount;
    uint32_t guiPumpCount;
    uint64_t guiGeneration;
    uint64_t guiContentHash;
    char guiContent[256];
};

struct NativeElfGuiRuntimeSnapshot {
    bool active;
    bool applicationCreated;
    bool windowCreated;
    bool rendered;
    bool closedNormally;
    uint32_t windowId;
    uint32_t renderCount;
    uint32_t pumpCount;
    uint64_t generation;
    uint64_t contentHash;
    char content[256];
};

bool configure_execution_context(const NativeElfExecutionContext& context);
bool execution_context_configured();

// The Run service binds the ephemeral Phase 27W development identity to the
// NativeElf application instance for the duration of one generation.
bool configure_development_identity(uint64_t generation, const char* applicationId);
void clear_development_identity();

void set_gui_automation_close(bool enabled);
bool native_elf_gui_runtime_snapshot(NativeElfGuiRuntimeSnapshot* output);

// Request the current generation's normal compositor close path.  This is an
// owner-side control operation; it never destroys an application stack.
bool request_native_elf_gui_close(uint64_t generation);

// Loads one validated ET_EXEC NativeElf file into the reusable reserved
// window, invokes its validated gx_main(gx_app_context*) entry on the
// dedicated bootstrap stack, and returns only after control has returned to
// the kernel and teardown has completed.
bool run_file(const char* path, int32_t* returnValue, NativeElfRunReport* report = nullptr);

} // namespace native_elf
} // namespace kernel
