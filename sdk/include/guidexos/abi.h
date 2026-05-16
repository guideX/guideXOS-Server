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
    gx_result (*reserved0)(void);
    gx_result (*reserved1)(void);
    gx_result (*reserved2)(void);
    gx_result (*reserved3)(void);
} gx_host_calls;

#ifdef __cplusplus
}
#endif
