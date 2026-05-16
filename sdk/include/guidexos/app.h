#pragma once

#include "abi.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gx_app_context {
    uint32_t size;
    uint32_t apiVersion;
    const gx_host_calls* host;
    void* userData;
};

typedef gx_result (*gx_main_fn)(gx_app_context* ctx);

gx_result GX_CALL gx_main(gx_app_context* ctx);

#ifdef __cplusplus
}
#endif
