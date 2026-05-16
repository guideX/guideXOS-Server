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
    GX_ERROR_FAILED = -4
};

#ifdef __cplusplus
}
#endif
