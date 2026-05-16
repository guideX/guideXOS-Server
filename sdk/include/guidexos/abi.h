#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_API_VERSION 0u
#define GX_ABI_NAME "guidexos-c-abi-v1"

typedef struct gx_app_context gx_app_context;

typedef enum gx_event_type {
    GX_EVENT_NONE = 0,
    GX_EVENT_WINDOW_CLOSE = 1,
    GX_EVENT_WINDOW_FOCUS = 2,
    GX_EVENT_WINDOW_BLUR = 3,
    GX_EVENT_KEY = 4,
    GX_EVENT_MOUSE = 5,
    GX_EVENT_WINDOW_PAINT = 6
} gx_event_type;

enum {
    GX_KEY_ACTION_UP = 0,
    GX_KEY_ACTION_DOWN = 1,
    GX_KEY_MOD_SHIFT = 1,
    GX_KEY_MOD_CTRL = 2,
    GX_KEY_MOD_ALT = 4
};

enum {
    GX_MOUSE_ACTION_MOVE = 0,
    GX_MOUSE_ACTION_DOWN = 1,
    GX_MOUSE_ACTION_UP = 2,
    GX_MOUSE_ACTION_DOUBLE_CLICK = 3,
    GX_MOUSE_BUTTON_NONE = 0,
    GX_MOUSE_BUTTON_LEFT = 1,
    GX_MOUSE_BUTTON_RIGHT = 2,
    GX_MOUSE_BUTTON_MIDDLE = 3
};

#define GX_MOUSE_PACK(button, action) ((((button) & 0xFFFF) << 16) | ((action) & 0xFFFF))
#define GX_MOUSE_ACTION(value) ((value) & 0xFFFF)
#define GX_MOUSE_BUTTON(value) (((value) >> 16) & 0xFFFF)

typedef struct gx_event {
    uint32_t size;
    gx_event_type type;
    gx_handle window;
    int param1;
    int param2;
    int param3;
    int param4;
} gx_event;

typedef struct gx_host_calls {
    uint32_t size;
    uint32_t version;
    gx_result (*log)(gx_app_context* ctx, const char* message);
    uint32_t (*get_api_version)(gx_app_context* ctx);
    gx_result (*request_window)(gx_app_context* ctx, const char* title, int width, int height, gx_handle* outWindow);
    gx_result (*draw_text)(gx_app_context* ctx, gx_handle window, int x, int y, const char* text);
    gx_result (*draw_rect)(gx_app_context* ctx, gx_handle window, int x, int y, int width, int height, uint32_t color);
    gx_result (*wait_for_close)(gx_app_context* ctx, gx_handle window, int timeoutMs);
    gx_result (*poll_event)(gx_app_context* ctx, gx_event* outEvent, int timeoutMs);
    gx_result (*exit)(gx_app_context* ctx, gx_result exitCode);
    gx_result (*file_read_all)(gx_app_context* ctx, const char* path, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead);
    gx_result (*file_exists)(gx_app_context* ctx, const char* path, uint32_t* outExists);
} gx_host_calls;

#ifdef __cplusplus
}
#endif
