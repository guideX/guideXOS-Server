#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_DEVELOPMENT_DEBUG_API_VERSION 1u
#define GX_DEVELOPMENT_DEBUG_MAX_ERROR_BYTES 128u

typedef enum gx_development_debug_command {
    GX_DEVELOPMENT_DEBUG_BIND_SOFTWARE_BREAKPOINT = 1,
    GX_DEVELOPMENT_DEBUG_RELEASE_EXECUTION = 2,
    GX_DEVELOPMENT_DEBUG_POLL = 3,
    GX_DEVELOPMENT_DEBUG_RESTORE_ALL = 4,
    GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION = 5
} gx_development_debug_command;

typedef enum gx_development_debug_status {
    GX_DEVELOPMENT_DEBUG_STATUS_NONE = 0,
    GX_DEVELOPMENT_DEBUG_STATUS_READY = 1,
    GX_DEVELOPMENT_DEBUG_STATUS_BOUND = 2,
    GX_DEVELOPMENT_DEBUG_STATUS_TRAP = 3,
    GX_DEVELOPMENT_DEBUG_STATUS_RESTORED = 4,
    GX_DEVELOPMENT_DEBUG_STATUS_REJECTED = 5
} gx_development_debug_status;

typedef struct gx_development_debug_request {
    uint32_t size;
    uint32_t version;
    uint32_t command;
    uint32_t reserved;
    uint64_t handle;
    uint64_t sessionGeneration;
    uint64_t processId;
    uint64_t nativeRuntimeId;
    uint64_t breakpointId;
    uint64_t targetAddress;
    const char* artifactSha256;
} gx_development_debug_request;

typedef struct gx_development_debug_snapshot {
    uint32_t size;
    uint32_t version;
    uint32_t status;
    uint32_t trapKind;
    uint64_t bindingId;
    uint64_t processId;
    uint64_t nativeRuntimeId;
    uint64_t threadId;
    uint64_t instructionPointer;
    uint64_t targetAddress;
    uint8_t originalByte;
    uint8_t installedByte;
    uint8_t originalByteValid;
    uint8_t bindingInstalled;
    uint32_t bindingCount;
    char errorMessage[GX_DEVELOPMENT_DEBUG_MAX_ERROR_BYTES];
} gx_development_debug_snapshot;

enum {
    GX_DEVELOPMENT_DEBUG_TRAP_NONE = 0,
    GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT = 1,
    GX_DEVELOPMENT_DEBUG_TRAP_SINGLE_STEP = 2
};

#ifdef __cplusplus
}
#endif
