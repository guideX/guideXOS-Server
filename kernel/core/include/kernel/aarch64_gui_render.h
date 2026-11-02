#pragma once

#include <stdint.h>

#include "guidexos/app.h"

namespace kernel {
namespace gui_render {

struct Surface {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stridePixels;
    uint64_t storagePixels;
};

class SurfaceRegistry {
public:
    static const uint32_t kCapacity = 3;

    SurfaceRegistry();
    ~SurfaceRegistry();
    bool create(gx_handle window, uint32_t width, uint32_t height);
    bool destroy(gx_handle window);
    bool resolve(gx_handle window, Surface* outSurface) const;
    bool guards_intact(gx_handle window) const;

private:
    struct Entry {
        gx_handle window;
        uint32_t* allocation;
        Surface surface;
        uint64_t pixelCount;
    };

    Entry m_entries[kCapacity];
    SurfaceRegistry(const SurfaceRegistry&);
    SurfaceRegistry& operator=(const SurfaceRegistry&);
};

typedef bool (*ResolveSurface)(void* owner, gx_handle window, Surface* outSurface);
typedef void (*InvalidateSurface)(void* owner, gx_handle window);

/* gx_app_context::userData points at this runtime-owned binding while the
 * application is active. The callbacks never retain a Surface pointer. */
struct HostBinding {
    void* runtime;
    gx_app_context* context;
    gx_host_calls* host;
    void* owner;
    ResolveSurface resolveSurface;
    InvalidateSurface invalidateSurface;
    uint32_t active;
};

bool surface_valid(const Surface& surface);
bool binding_valid(const HostBinding* binding, const gx_app_context* context);

gx_result draw_rect(Surface* surface, int x, int y, int width, int height, uint32_t color);
gx_result draw_text(Surface* surface, int x, int y, const char* text);

gx_result GX_CALL host_draw_rect(gx_app_context* context, gx_handle window,
                                 int x, int y, int width, int height, uint32_t color);
gx_result GX_CALL host_draw_text(gx_app_context* context, gx_handle window,
                                 int x, int y, const char* text);

/* Publishes the genuine AARCH64 SDK callbacks only for a live matching context,
 * without touching any neighboring ABI fields. */
bool publish_host_callbacks(gx_host_calls* host, const HostBinding* binding,
                            const gx_app_context* context);

} // namespace gui_render
} // namespace kernel
