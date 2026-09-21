#pragma once

// Missile Command MC4 city artwork: original build1.gif as a GXIM resource.
//
// The VB6 original draws every alive city by stretching the whole
// build1.gif (153x121, single city-skyline tile) into each alive target
// slot (Module1.bas ResetTargets + BitBlt: AspectX = p.Width / n preserves
// the aspect via AspectY; destroyed slots are simply not drawn). One image
// holds ONE city state -- there are no per-state tiles and no hard-coded
// sprite coordinates; placement is bottom-anchored and evenly spaced.
//
// The port converts build1.gif once, at build time, with
// scripts/convert-missilecommand-city.ps1 into resources/city.gximg using
// the app-visible GXIM layout (28-byte "GXIM" header, same as
// D:\dev\pacman\guidexos\tools\convert_bmp_to_gximg.ps1 produces for the
// PacMan sprites and sdk PacMan bitmap_loader.cpp consumes):
//
//   offset  0: "GXIM" magic
//   offset  4: uint32 version = 1
//   offset  8: uint32 width            (153)
//   offset 12: uint32 height           (121)
//   offset 16: uint32 strideBytes      (width * 4)
//   offset 20: uint32 pixelFormat = 1  (GX_PIXEL_FORMAT_XRGB8888)
//   offset 24: uint32 payloadBytes     (stride * height)
//   offset 28: pixels top-down, B,G,R,0 per pixel (LE uint32 0x00RRGGBB)
//
// Pure logic only (same contract as missilecommand_state.h): no guideXOS
// includes, no allocation, no libc. Shared by main.cpp rendering and the
// host unit test. Simulation hitboxes are untouched: gameplay still uses
// the MC1 rectangle slots (mc_city_x / mc_city_y); this header only maps
// the artwork INTO those slots (uniform scale to the slot width,
// bottom-anchored at the ground line, like the original's bottom-anchored
// BitBlt). Source black (0x000000) is transparent so the playfield shows
// through; destroyed cities keep the rubble marker (original leaves them
// empty black -- documented divergence for readability).

#include "missilecommand_state.h"

static const int kMcCityArtWidth = 153;
static const int kMcCityArtHeight = 121;
static const uint32_t kMcCityArtHeaderBytes = 28u;
static const uint32_t kMcCityArtVersion = 1u;
static const uint32_t kMcCityArtPixelFormat = 1u;  // GX_PIXEL_FORMAT_XRGB8888
static const uint32_t kMcCityArtFileBytes =
    28u + (uint32_t)kMcCityArtWidth * (uint32_t)kMcCityArtHeight * 4u;
static const uint32_t kMcCityArtTransparent = 0x00000000u;

// Destination footprint in window units: uniform scale of the 153x121 tile
// to the 28-wide city slot (28/153), bottom-anchored at the ground top.
// Height truncates (121*28/153 = 22.14 -> 22); aspect error < 1%.
static const int kMcCityArtDestW = kMcCityWidth;  // 28
static const int kMcCityArtDestH =
    (kMcCityArtHeight * kMcCityWidth) / kMcCityArtWidth;  // 22

struct McCityArtImage {
    const unsigned char* pixels;  // tightly packed payload, stride = w*4
    uint32_t width;
    uint32_t height;
};

struct McCityArtRect {
    int x;
    int y;
    int w;
    int h;
};

inline uint32_t mc_city_art_read_u32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// Bounds-checked GXIM parse. Accepts any sane dims (1..512, stride = w*4,
// payload present); the caller gates on the exact 153x121 art when it
// needs the shipped tile. Never reads past `bytes`.
inline bool mc_city_art_parse(const unsigned char* buffer, uint32_t bytes,
                              McCityArtImage* out) {
    if (!buffer || !out) return false;
    out->pixels = 0;
    out->width = 0;
    out->height = 0;
    if (bytes < kMcCityArtHeaderBytes) return false;
    if (!(buffer[0] == 'G' && buffer[1] == 'X' && buffer[2] == 'I' &&
          buffer[3] == 'M'))
        return false;
    const uint32_t version = mc_city_art_read_u32(buffer + 4);
    const uint32_t width = mc_city_art_read_u32(buffer + 8);
    const uint32_t height = mc_city_art_read_u32(buffer + 12);
    const uint32_t stride = mc_city_art_read_u32(buffer + 16);
    const uint32_t format = mc_city_art_read_u32(buffer + 20);
    const uint32_t payload = mc_city_art_read_u32(buffer + 24);
    if (version != kMcCityArtVersion) return false;
    if (format != kMcCityArtPixelFormat) return false;
    if (width == 0 || width > 512 || height == 0 || height > 512) return false;
    if (stride != width * 4u) return false;
    if (payload != stride * height) return false;
    if (payload > bytes - kMcCityArtHeaderBytes) return false;
    out->pixels = buffer + kMcCityArtHeaderBytes;
    out->width = width;
    out->height = height;
    return true;
}

// True for the shipped tile (exact original dimensions).
inline bool mc_city_art_is_shipped_tile(const McCityArtImage* image) {
    return image && image->pixels != 0 &&
           image->width == (uint32_t)kMcCityArtWidth &&
           image->height == (uint32_t)kMcCityArtHeight;
}

// Nearest-neighbor source coordinate for a destination pixel (clamped).
// Shared by the renderer and the host test so the mapping is pinned.
inline int mc_city_art_src_x(int dx) {
    if (dx < 0) dx = 0;
    if (dx >= kMcCityArtDestW) dx = kMcCityArtDestW - 1;
    return (dx * kMcCityArtWidth) / kMcCityArtDestW;
}

inline int mc_city_art_src_y(int dy) {
    if (dy < 0) dy = 0;
    if (dy >= kMcCityArtDestH) dy = kMcCityArtDestH - 1;
    return (dy * kMcCityArtHeight) / kMcCityArtDestH;
}

// Sample one payload pixel (tightly packed, stride = w*4), clamped.
inline uint32_t mc_city_art_sample(const McCityArtImage* image, int sx,
                                   int sy) {
    if (!image || !image->pixels) return kMcCityArtTransparent;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if ((uint32_t)sx >= image->width) sx = (int)image->width - 1;
    if ((uint32_t)sy >= image->height) sy = (int)image->height - 1;
    const unsigned char* p =
        image->pixels + ((uint32_t)sy * image->width + (uint32_t)sx) * 4u;
    return mc_city_art_read_u32(p);
}

inline bool mc_city_art_is_opaque(uint32_t pixel) {
    return pixel != kMcCityArtTransparent;
}

// Destination rect for a 0-based city slot: same left edge as the logical
// slot, bottom-anchored at the ground line (original ResetTargets anchors
// cities at the picture-box bottom).
inline McCityArtRect mc_city_art_dest(int slotIndex) {
    McCityArtRect rect;
    rect.w = kMcCityArtDestW;
    rect.h = kMcCityArtDestH;
    rect.x = mc_city_x(slotIndex);
    rect.y = kMcGroundTop - kMcCityArtDestH;
    return rect;
}

// Slot visual selection (mirrors main.cpp render_scene so the rule is
// host-testable): alive + valid art -> blit the GXIM tile; alive without
// art -> MC3 yellow rectangle fallback; destroyed -> rubble marker
// (original leaves destroyed slots empty black; the port keeps a small
// rubble marker for readability -- documented divergence).
static const int kMcCityVisualArt = 0;
static const int kMcCityVisualRect = 1;
static const int kMcCityVisualRubble = 2;

inline int mc_city_slot_visual(bool alive, bool artValid) {
    if (alive) return artValid ? kMcCityVisualArt : kMcCityVisualRect;
    return kMcCityVisualRubble;
}
