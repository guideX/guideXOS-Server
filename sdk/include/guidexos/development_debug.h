#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_DEVELOPMENT_DEBUG_API_VERSION 1u
#define GX_DEVELOPMENT_DEBUG_MAX_ERROR_BYTES 128u
#define GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES 64u
#define GX_DEVELOPMENT_DEBUG_MAX_SOURCE_PATH_BYTES 160u

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
    GX_DEVELOPMENT_DEBUG_STEP_INTERNAL_TRAP = 13,
    GX_DEVELOPMENT_DEBUG_STEP_OUT_RETURN = 14,
    /* Phase 27Z: one bare-metal entry breakpoint control. */
    GX_DEVELOPMENT_DEBUG_RESUME = 15,
    /* Phase 28C: repeatedly execute instructions until a new source location. */
    GX_DEVELOPMENT_DEBUG_STEP_SOURCE_INTO = 16,
    /* Phase 28D: execute one source operation without entering a direct user call. */
    GX_DEVELOPMENT_DEBUG_STEP_SOURCE_OVER = 17,
    /* Phase 28E: execute the current user frame until its caller resumes. */
    GX_DEVELOPMENT_DEBUG_STEP_SOURCE_OUT = 18,
    /* Phase 28F: inspect the bounded user frame chain without resuming it. */
    GX_DEVELOPMENT_DEBUG_CALL_STACK = 19,
    /* Phase 28G: inspect validated top-frame arguments and locals. */
    GX_DEVELOPMENT_DEBUG_INSPECT_VARIABLES = 20
} gx_development_debug_command;

#define GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES 16u

typedef enum gx_development_debug_call_stack_status {
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_NONE = 0,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_SUCCESS = 1,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_TRUNCATED = 2,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_REJECTED = 3,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_NO_PAUSED_CONTEXT = 4,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_STALE = 5,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_FRAME = 6,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_INVALID_RETURN_ADDRESS = 7,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_STATUS_UNSUPPORTED_FRAME = 8
} gx_development_debug_call_stack_status;

enum {
    GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_VALIDATED = 1u,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_FUNCTION_RESOLVED = 2u,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_SOURCE_MAPPED = 4u,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_CALL_SITE_MAPPED = 8u,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_ROOT = 16u,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_RETURN_ADDRESS_VALID = 32u,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_POINTER_VALID = 64u,
    GX_DEVELOPMENT_DEBUG_CALL_STACK_FRAME_SOURCE_UNAVAILABLE = 128u
};

typedef struct gx_development_debug_call_stack_frame {
    uint32_t depth;
    uint32_t flags;
    uint64_t instructionPointer;
    uint64_t stackPointer;
    uint64_t framePointer;
    uint64_t returnAddress;
    uint32_t sourceLine;
    uint32_t sourceColumn;
    char functionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    char sourcePath[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_PATH_BYTES];
} gx_development_debug_call_stack_frame;

typedef struct gx_development_debug_call_stack {
    uint32_t size;
    uint32_t version;
    uint32_t status;
    uint32_t frameCount;
    uint32_t truncated;
    uint32_t reserved;
    uint64_t handle;
    uint64_t processId;
    uint64_t nativeRuntimeId;
    uint64_t threadId;
    uint64_t sessionGeneration;
    uint64_t stopGeneration;
    uint64_t stackLow;
    uint64_t stackHigh;
    char errorMessage[GX_DEVELOPMENT_DEBUG_MAX_ERROR_BYTES];
    gx_development_debug_call_stack_frame frames[GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES];
} gx_development_debug_call_stack;

#define GX_DEVELOPMENT_DEBUG_MAX_VARIABLES 32u
#define GX_DEVELOPMENT_DEBUG_MAX_VARIABLE_NAME_BYTES 64u

typedef enum gx_development_debug_variables_status {
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NONE = 0,
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_SUCCESS = 1,
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_TRUNCATED = 2,
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_REJECTED = 3,
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NO_PAUSED_CONTEXT = 4,
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_STALE = 5,
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME = 6,
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_LOCATION = 7,
    GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_UNSUPPORTED = 8
} gx_development_debug_variables_status;

enum {
    GX_DEVELOPMENT_DEBUG_VARIABLE_VALIDATED = 1u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_LIVE = 2u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_INITIALIZED = 4u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_STABLE_FRAME_SLOT = 8u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_VALUE_VALID = 16u
};

enum {
    GX_DEVELOPMENT_DEBUG_VARIABLE_KIND_ARGUMENT = 1u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_KIND_LOCAL = 2u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32 = 1u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_POINTER = 2u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_LOCATION_RBP_RELATIVE = 1u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_AVAILABILITY_AVAILABLE = 1u,
    GX_DEVELOPMENT_DEBUG_VARIABLE_AVAILABILITY_UNAVAILABLE = 2u
};

typedef struct gx_development_debug_variable {
    char name[GX_DEVELOPMENT_DEBUG_MAX_VARIABLE_NAME_BYTES];
    uint32_t kind;
    uint32_t type;
    uint32_t location;
    uint32_t flags;
    uint32_t sizeBytes;
    uint32_t declarationLine;
    uint32_t declarationColumn;
    int32_t frameOffset;
    uint32_t availability;
    uint64_t rawValue;
    int64_t signedValue;
    uint64_t unsignedValue;
} gx_development_debug_variable;

typedef struct gx_development_debug_variables {
    uint32_t size;
    uint32_t version;
    uint32_t status;
    uint32_t variableCount;
    uint32_t truncated;
    uint32_t reserved;
    uint64_t handle;
    uint64_t processId;
    uint64_t nativeRuntimeId;
    uint64_t threadId;
    uint64_t sessionGeneration;
    uint64_t stopGeneration;
    uint64_t instructionPointer;
    uint64_t framePointer;
    uint64_t stackLow;
    uint64_t stackHigh;
    char functionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    char sourcePath[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_PATH_BYTES];
    char errorMessage[GX_DEVELOPMENT_DEBUG_MAX_ERROR_BYTES];
    gx_development_debug_variable variables[GX_DEVELOPMENT_DEBUG_MAX_VARIABLES];
} gx_development_debug_variables;

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

/* For GX_DEVELOPMENT_DEBUG_INSPECT_VARIABLES, auxiliaryAddress is a
   query-local validated Call Stack frame index. Zero preserves the Phase 28G
   top-frame behavior. It is never interpreted as a caller-supplied address. */

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
    uint32_t internalBreakpointPurpose;
    uint32_t byteCount;
    uint8_t bytes[16];
    /* Appended in Phase 7. These are validated bounds for the stopped
       Native ELF thread stack; they are not host-process memory bounds. */
    uint64_t stackLow;
    uint64_t stackHigh;
    /* Append-only Phase 27Z entry-stop identity. */
    uint64_t sessionGeneration;
    uint32_t pauseReason;
    uint32_t reserved3;
    char functionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    /* Append-only Phase 28A source-breakpoint identity. */
    uint64_t rawTrapRip;
    char sourcePath[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_PATH_BYTES];
    uint32_t sourceLine;
    uint32_t sourceColumn;
    uint32_t sourceMappingValid;
    uint32_t reserved4;
    /* Append-only Phase 28C source-aware Step Into result and provenance. */
    uint32_t sourceStepResult;
    uint32_t sourceStepInstructionCount;
    uint32_t sourceStepInstructionLimit;
    uint32_t sourceStepStartingMappingValid;
    uint64_t sourceStepStartRip;
    uint64_t sourceStepFinalRip;
    uint32_t sourceStepStartLine;
    uint32_t sourceStepStartColumn;
    char sourceStepStartPath[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_PATH_BYTES];
    char sourceStepStartFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    /* Append-only Phase 28D direct-call Step Over evidence. */
    uint32_t sourceStepOverResult;
    uint32_t sourceStepOverInstructionCount;
    uint32_t sourceStepOverInstructionLimit;
    uint32_t sourceStepOverCallDetected;
    uint32_t sourceStepOverNestedReturnCount;
    uint32_t sourceStepOverTemporaryBreakpointCount;
    uint64_t sourceStepOverCallRip;
    uint64_t sourceStepOverCallTargetAddress;
    uint64_t sourceStepOverReturnAddress;
    uint64_t sourceStepOverStartingRsp;
    uint64_t sourceStepOverStartingRbp;
    uint64_t sourceStepOverReturnTrapRip;
    uint64_t sourceStepOverFinalRsp;
    uint64_t sourceStepOverFinalRbp;
    uint32_t sourceStepOverInternalMachineStepCount;
    uint32_t sourceStepOverCallerFrameVerified;
    char sourceStepOverCalleeFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    uint8_t sourceStepOverOriginalReturnByte;
    uint8_t sourceStepOverOriginalReturnByteValid;
    uint8_t sourceStepOverReserved[2];
    /* Append-only Phase 28E source-aware Step Out evidence. */
    uint32_t sourceStepOutResult;
    uint32_t sourceStepOutInstructionCount;
    uint32_t sourceStepOutInstructionLimit;
    uint32_t sourceStepOutCurrentFrameValid;
    uint64_t sourceStepOutStartRip;
    uint64_t sourceStepOutStartRsp;
    uint64_t sourceStepOutStartRbp;
    uint64_t sourceStepOutSavedCallerRbp;
    uint64_t sourceStepOutReturnAddress;
    uint64_t sourceStepOutReturnTrapRip;
    uint64_t sourceStepOutFinalRip;
    uint64_t sourceStepOutFinalRsp;
    uint64_t sourceStepOutFinalRbp;
    uint32_t sourceStepOutTemporaryBreakpointCount;
    uint32_t sourceStepOutInternalMachineStepCount;
    uint32_t sourceStepOutCallerFrameVerified;
    uint32_t sourceStepOutRemainingCalleeExecuted;
    uint32_t sourceStepOutIntermediatePauseCount;
    char sourceStepOutCalleeFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    char sourceStepOutCallerFunctionName[GX_DEVELOPMENT_DEBUG_MAX_FUNCTION_NAME_BYTES];
    char sourceStepOutCallerSourcePath[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_PATH_BYTES];
    uint8_t sourceStepOutOriginalReturnByte;
    uint8_t sourceStepOutOriginalReturnByteValid;
    uint8_t sourceStepOutReserved[6];
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

enum {
    GX_DEVELOPMENT_DEBUG_INTERNAL_BREAKPOINT_NONE = 0,
    GX_DEVELOPMENT_DEBUG_INTERNAL_BREAKPOINT_STEP_OVER = 1,
    GX_DEVELOPMENT_DEBUG_INTERNAL_BREAKPOINT_STEP_OUT = 2
};

enum {
    GX_DEVELOPMENT_DEBUG_PAUSE_REASON_NONE = 0,
    GX_DEVELOPMENT_DEBUG_PAUSE_REASON_ENTRY_BREAKPOINT = 1,
    GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_BREAKPOINT = 2,
    GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SINGLE_STEP = 3,
    GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP = 4,
    GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OVER = 5,
    GX_DEVELOPMENT_DEBUG_PAUSE_REASON_SOURCE_STEP_OUT = 6
};

/* A source step is a bounded composite of the Phase 28B instruction step. */
enum {
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NONE = 0,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_PENDING = 1,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_COMPLETED = 2,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_LIMIT = 3,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_COMPLETED = 4,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_TARGET_FAILED = 5,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_CANCELLED = 6,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_NO_SOURCE_MAPPING = 7,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_INVALID_SOURCE_MAP = 8,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_UNSAFE_RUNTIME_BOUNDARY = 9,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_RESULT_STALE = 10
};

/* Step Out has its own result channel so a rejected caller/frame shape is
   never confused with a completed Source Step Into/Over operation. */
enum {
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_NONE = 0,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_PENDING = 1,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_COMPLETED = 2,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_NO_CALLER_FRAME = 3,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_UNSUPPORTED_FRAME = 4,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_FRAME = 5,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_RETURN_ADDRESS = 6,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_LIMIT = 7,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_COMPLETED = 8,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_TARGET_FAILED = 9,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_CANCELLED = 10,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_INVALID_SOURCE_MAP = 11,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_UNSAFE_RUNTIME_BOUNDARY = 12,
    GX_DEVELOPMENT_DEBUG_SOURCE_STEP_OUT_RESULT_STALE = 13
};

#define GX_DEVELOPMENT_DEBUG_SOURCE_STEP_MAX_INSTRUCTIONS 128u

#ifdef __cplusplus
}
#endif
