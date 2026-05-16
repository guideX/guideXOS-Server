#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_API_VERSION 0u
#define GX_ABI_NAME "guidexos-c-abi-v1"

typedef struct gx_host_calls {
    uint32_t size;
    uint32_t version;
    gx_result (*log)(const char* message);
    uint32_t (*get_api_version)(void);
    gx_result (*request_window)(void);
    gx_result (*exit)(gx_result exitCode);
} gx_host_calls;

#ifdef __cplusplus
}
#endif
