#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_BUILD_API_VERSION 1u
#define GX_BUILD_MAX_OUTPUT_LINES 32u
#define GX_BUILD_MAX_OUTPUT_LINE_BYTES 256u
#define GX_BUILD_MAX_ARTIFACT_PATH_BYTES 160u
#define GX_BUILD_MAX_SHA256_BYTES 65u

typedef uint64_t gx_build_handle;

typedef enum gx_build_state {
    GX_BUILD_IDLE = 0,
    GX_BUILD_VALIDATING = 1,
    GX_BUILD_PREPARING = 2,
    GX_BUILD_RUNNING = 3,
    GX_BUILD_VALIDATING_ARTIFACT = 4,
    GX_BUILD_SUCCEEDED = 5,
    GX_BUILD_FAILED = 6,
    GX_BUILD_CANCELLED = 7
} gx_build_state;

typedef enum gx_build_error_code {
    GX_BUILD_ERROR_NONE = 0,
    GX_BUILD_ERROR_INVALID_REQUEST = 1,
    GX_BUILD_ERROR_BUSY = 2,
    GX_BUILD_ERROR_SDK_NOT_FOUND = 3,
    GX_BUILD_ERROR_TOOLCHAIN_NOT_FOUND = 4,
    GX_BUILD_ERROR_POWERSHELL_NOT_FOUND = 5,
    GX_BUILD_ERROR_BUILD_SCRIPT_MISSING = 6,
    GX_BUILD_ERROR_INVALID_PROJECT_ROOT = 7,
    GX_BUILD_ERROR_PROCESS_START_FAILED = 8,
    GX_BUILD_ERROR_PROCESS_FAILED = 9,
    GX_BUILD_ERROR_BUILD_TIMEOUT = 10,
    GX_BUILD_ERROR_ARTIFACT_MISSING = 11,
    GX_BUILD_ERROR_ARTIFACT_INVALID = 12,
    GX_BUILD_ERROR_ARTIFACT_WRONG_ARCHITECTURE = 13,
    GX_BUILD_ERROR_ENTRY_POINT_MISSING = 14,
    GX_BUILD_ERROR_MANIFEST_ARTIFACT_MISMATCH = 15,
    GX_BUILD_ERROR_OUTPUT_TRUNCATED = 16,
    GX_BUILD_ERROR_INTERNAL = 17
} gx_build_error_code;

typedef struct gx_build_request {
    uint32_t size;
    uint32_t version;
    const char* projectRoot;
    const char* projectId;
    const char* projectKind;
    const char* targetProfile;
    const char* buildSystem;
    const char* buildScript;
    const char* expectedArtifact;
    const char* configuration;
} gx_build_request;

typedef struct gx_build_output_line {
    /* 0 = unknown/legacy, 1 = stdout, 2 = stderr. */
    uint32_t stream;
    char text[GX_BUILD_MAX_OUTPUT_LINE_BYTES];
} gx_build_output_line;

typedef struct gx_build_snapshot {
    uint32_t size;
    uint32_t version;
    gx_build_handle handle;
    uint32_t state;
    int32_t processExitCode;
    uint32_t errorCode;
    uint32_t warningCount;
    uint32_t errorCount;
    uint32_t outputCount;
    uint32_t outputTruncated;
    uint64_t elapsedMilliseconds;
    uint64_t artifactSize;
    uint32_t artifactValid;
    uint32_t artifactEntryPoint;
    char artifactPath[GX_BUILD_MAX_ARTIFACT_PATH_BYTES];
    char artifactSha256[GX_BUILD_MAX_SHA256_BYTES];
    char artifactArchitecture[32];
    char errorMessage[128];
    gx_build_output_line output[GX_BUILD_MAX_OUTPUT_LINES];
} gx_build_snapshot;

#ifdef __cplusplus
}
#endif
