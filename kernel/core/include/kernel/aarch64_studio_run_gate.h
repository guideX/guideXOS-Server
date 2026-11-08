#pragma once

#include <stdint.h>

#include "../../../../sdk/include/guidexos/development_run.h"

namespace kernel {
namespace studio_abi {
namespace run_gate {

struct Decision {
    bool admitted;
    gx_development_run_error_code error;
    const char* message;
};

// Studio child execution remains disabled until the ARM64 kernel has a real
// EL0 address space, lower-EL exception return, and scheduler-owned lifetime.
static inline Decision studio_child_decision()
{
    return { false, GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE,
        "Studio Run unsupported: isolated EL0 child execution and bounded task ownership are unavailable" };
}

// A rejected request has no child result. Keep exitCode at -1 and use FAILED;
// callers must not turn this state into a successful or completed run.
static inline void write_rejection(gx_development_run_snapshot* snapshot,
                                   const Decision& decision)
{
    if (!snapshot) return;
    snapshot->state = GX_DEVELOPMENT_RUN_FAILED;
    snapshot->errorCode = static_cast<uint32_t>(decision.error);
    snapshot->exitCode = -1;
    snapshot->cleanupComplete = 1;
    uint32_t i = 0;
    if (decision.message) {
        while (i + 1 < sizeof(snapshot->errorMessage) && decision.message[i]) {
            snapshot->errorMessage[i] = decision.message[i];
            ++i;
        }
    }
    snapshot->errorMessage[i] = '\0';
}

} // namespace run_gate
} // namespace studio_abi
} // namespace kernel
