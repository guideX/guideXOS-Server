#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_API_VERSION 0u
#define GX_ABI_NAME "guidexos-c-abi-v1"

typedef struct gx_app_context gx_app_context;

typedef struct gx_host_calls {
    uint32_t size;
    uint32_t version;
    gx_result (*log)(gx_app_context* ctx, const char* message);
    uint32_t (*get_api_version)(gx_app_context* ctx);
    gx_result (*request_window)(gx_app_context* ctx, const char* title, int width, int height, gx_handle* outWindow);
    gx_result (*draw_text)(gx_app_context* ctx, gx_handle window, int x, int y, const char* text);
    gx_result (*exit)(gx_app_context* ctx, gx_result exitCode);
} gx_host_calls;

#ifdef __cplusplus
}
#endif
