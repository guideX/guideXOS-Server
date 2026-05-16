#pragma once

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GX_KEY_ESCAPE
#define GX_KEY_ESCAPE 27
#endif

typedef struct gx_rect {
    int x;
    int y;
    int width;
    int height;
} gx_rect;

typedef struct gx_point {
    int x;
    int y;
} gx_point;

typedef uint32_t gx_color;

static inline int gx_rect_contains(gx_rect rect, int x, int y) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

static inline gx_result gx_draw_panel(gx_app_context* ctx, gx_handle window, gx_rect rect, gx_color color) {
    if (!ctx || !ctx->host || !ctx->host->draw_rect) return GX_ERROR_UNSUPPORTED;
    return ctx->host->draw_rect(ctx, window, rect.x, rect.y, rect.width, rect.height, color);
}

static inline gx_result gx_draw_label(gx_app_context* ctx, gx_handle window, int x, int y, const char* text) {
    if (!ctx || !ctx->host || !ctx->host->draw_text) return GX_ERROR_UNSUPPORTED;
    return ctx->host->draw_text(ctx, window, x, y, text);
}

static inline gx_result gx_draw_button(gx_app_context* ctx, gx_handle window, gx_rect rect, const char* label, int pressed) {
    gx_result result = gx_draw_panel(ctx, window, rect, pressed ? 0x606060u : 0x404040u);
    if (result != GX_OK) return result;
    return gx_draw_label(ctx, window, rect.x + 15, rect.y + 25, label);
}

static inline int gx_mouse_is_left_down(const gx_event* event) {
    if (!event || event->type != GX_EVENT_MOUSE) return 0;
    return GX_MOUSE_BUTTON(event->param3) == GX_MOUSE_BUTTON_LEFT && GX_MOUSE_ACTION(event->param3) == GX_MOUSE_ACTION_DOWN;
}

static inline int gx_event_is_close(const gx_event* event) {
    return event && event->type == GX_EVENT_WINDOW_CLOSE;
}

static inline int gx_event_is_paint(const gx_event* event) {
    return event && event->type == GX_EVENT_WINDOW_PAINT;
}

static inline int gx_event_is_escape_down(const gx_event* event) {
    return event && event->type == GX_EVENT_KEY && event->param1 == GX_KEY_ESCAPE && event->param2 == GX_KEY_ACTION_DOWN;
}

#ifdef __cplusplus
}
#endif
