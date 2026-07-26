#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t gx_result;
typedef uint32_t gx_flags;
typedef uint64_t gx_handle;

/* Pixels are stored as little-endian 0x00RRGGBB words (B, G, R, X bytes). */
enum {
    GX_PIXEL_FORMAT_XRGB8888 = 1
};

enum {
    GX_WINDOW_FLAG_RESIZABLE = 1u << 0,
    GX_WINDOW_FLAG_FIXED_SIZE = 0u,
    GX_WINDOW_FLAG_CENTERED = 1u << 1
};

enum {
    GX_OK = 0,
    GX_ERROR_NOT_IMPLEMENTED = -1,
    GX_ERROR_INVALID_ARGUMENT = -2,
    GX_ERROR_UNSUPPORTED = -3,
    GX_ERROR_FAILED = -4,
    GX_ERROR_PERMISSION_DENIED = -5,
    GX_ERROR_INTERNAL = -6,
    GX_ERROR_TIMEOUT = -7,
    GX_ERROR_BUSY = -8,
    GX_ERR_NOT_IMPLEMENTED = GX_ERROR_NOT_IMPLEMENTED,
    GX_ERR_INVALID_ARGUMENT = GX_ERROR_INVALID_ARGUMENT,
    GX_ERR_UNSUPPORTED = GX_ERROR_UNSUPPORTED,
    GX_ERR_FAILED = GX_ERROR_FAILED,
    GX_ERR_PERMISSION_DENIED = GX_ERROR_PERMISSION_DENIED,
    GX_ERR_INTERNAL = GX_ERROR_INTERNAL,
    GX_ERR_TIMEOUT = GX_ERROR_TIMEOUT,
    GX_ERR_BUSY = GX_ERROR_BUSY
};

#ifdef __cplusplus
}
#endif
