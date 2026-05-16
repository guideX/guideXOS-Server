#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t gx_result;
typedef uint32_t gx_flags;
typedef uint64_t gx_handle;

enum {
    GX_OK = 0,
    GX_ERROR_NOT_IMPLEMENTED = -1,
    GX_ERROR_INVALID_ARGUMENT = -2,
    GX_ERROR_UNSUPPORTED = -3,
    GX_ERROR_FAILED = -4,
    GX_ERROR_PERMISSION_DENIED = -5,
    GX_ERROR_INTERNAL = -6,
    GX_ERR_NOT_IMPLEMENTED = GX_ERROR_NOT_IMPLEMENTED,
    GX_ERR_INVALID_ARGUMENT = GX_ERROR_INVALID_ARGUMENT,
    GX_ERR_UNSUPPORTED = GX_ERROR_UNSUPPORTED,
    GX_ERR_FAILED = GX_ERROR_FAILED,
    GX_ERR_PERMISSION_DENIED = GX_ERROR_PERMISSION_DENIED,
    GX_ERR_INTERNAL = GX_ERROR_INTERNAL
};

#ifdef __cplusplus
}
#endif
