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
    GX_DEVELOPMENT_DEBUG_CANCEL_EXECUTION = 5,
    GX_DEVELOPMENT_DEBUG_CONTINUE_BREAKPOINT = 6,
    GX_DEVELOPMENT_DEBUG_STEP_INSTRUCTION = 7,
    GX_DEVELOPMENT_DEBUG_RESUME_STEP = 8,
    GX_DEVELOPMENT_DEBUG_READ_MEMORY = 9,
    GX_DEVELOPMENT_DEBUG_REMOVE_BREAKPOINT_OWNER = 10,
    GX_DEVELOPMENT_DEBUG_STEP_OVER_CALL = 11,
    GX_DEVELOPMENT_DEBUG_RESUME_INTERNAL_TRAP = 12,
    GX_DEVELOPMENT_DEBUG_STEP_INTERNAL_TRAP = 13
} gx_development_debug_command;

typedef enum gx_development_debug_status {
    GX_DEVELOPMENT_DEBUG_STATUS_NONE = 0,
    GX_DEVELOPMENT_DEBUG_STATUS_READY = 1,
    GX_DEVELOPMENT_DEBUG_STATUS_BOUND = 2,
    GX_DEVELOPMENT_DEBUG_STATUS_TRAP = 3,
    GX_DEVELOPMENT_DEBUG_STATUS_RESTORED = 4,
    GX_DEVELOPMENT_DEBUG_STATUS_REJECTED = 5,
    GX_DEVELOPMENT_DEBUG_STATUS_SINGLE_STEP_PENDING = 6
} gx_development_debug_status;

#define GX_DEVELOPMENT_DEBUG_FLAG_REINSTALL_BREAKPOINT 1u

typedef enum gx_development_debug_architecture {
    GX_DEVELOPMENT_DEBUG_ARCHITECTURE_UNKNOWN = 0,
    GX_DEVELOPMENT_DEBUG_ARCHITECTURE_AMD64 = 1,
    GX_DEVELOPMENT_DEBUG_ARCHITECTURE_ARM64 = 2,
    GX_DEVELOPMENT_DEBUG_ARCHITECTURE_RISCV64 = 3
} gx_development_debug_architecture;

typedef struct gx_development_debug_request {
    uint32_t size;
    uint32_t version;
    uint32_t command;
    uint32_t flags;
    uint64_t handle;
    uint64_t sessionGeneration;
    uint64_t processId;
    uint64_t nativeRuntimeId;
    uint64_t breakpointId;
    uint64_t targetAddress;
    const char* artifactSha256;
    uint64_t threadId;
    uint64_t stopGeneration;
    uint64_t auxiliaryAddress;
    uint32_t readByteCount;
    uint32_t reserved;
} gx_development_debug_request;

typedef struct gx_development_debug_register_context {
    uint32_t architecture;
    uint32_t valid;
    uint64_t processId;
    uint64_t nativeRuntimeId;
    uint64_t threadId;
    uint64_t sessionGeneration;
    uint64_t stopGeneration;
    uint64_t rip;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} gx_development_debug_register_context;

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
    gx_development_debug_register_context context;
    uint64_t rflagsBeforeStep;
    uint64_t rflagsWithTrapFlag;
    uint64_t rflagsAfterTrapFlagClear;
    uint32_t singleStepKind;
    uint8_t internalBreakpointTrap;
    uint8_t reserved2[3];
    uint64_t internalBreakpointId;
    uint32_t byteCount;
    uint8_t bytes[16];
} gx_development_debug_snapshot;

enum {
    GX_DEVELOPMENT_DEBUG_TRAP_NONE = 0,
    GX_DEVELOPMENT_DEBUG_TRAP_BREAKPOINT = 1,
    GX_DEVELOPMENT_DEBUG_TRAP_SINGLE_STEP = 2
};

enum {
    GX_DEVELOPMENT_DEBUG_SINGLE_STEP_NONE = 0,
    GX_DEVELOPMENT_DEBUG_SINGLE_STEP_INTERNAL_BREAKPOINT = 1,
    GX_DEVELOPMENT_DEBUG_SINGLE_STEP_USER_SOURCE = 2
};

#ifdef __cplusplus
}
#endif
