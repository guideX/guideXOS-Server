#pragma once

#include <stdint.h>

namespace kernel {
namespace framebuffer {

// Common framebuffer pixels are represented as 0xAARRGGBB.  The format
// describes the byte order exposed by the firmware scanout, not the logical
// colour order used by drawing code.
static const uint32_t kPixelFormatUnknown = 0;
static const uint32_t kPixelFormatR8G8B8A8 = 1;
static const uint32_t kPixelFormatB8G8R8A8 = 2;

struct Geometry {
    uint64_t base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    uint32_t format;
};

inline bool add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return *result > left;
}

inline bool validate_geometry(const Geometry& geometry)
{
    if (geometry.base == 0 || (geometry.base & 3u) != 0 || geometry.size == 0 || geometry.width == 0 ||
        geometry.height == 0 || geometry.bits_per_pixel != 32 ||
        (geometry.format != kPixelFormatR8G8B8A8 &&
         geometry.format != kPixelFormatB8G8R8A8)) return false;

    uint64_t row_bytes = static_cast<uint64_t>(geometry.width) * 4u;
    uint64_t image_bytes = static_cast<uint64_t>(geometry.pitch) * geometry.height;
    uint64_t end = 0;
    return geometry.pitch >= row_bytes && geometry.pitch != 0 && image_bytes != 0 &&
           image_bytes <= geometry.size && add_u64(geometry.base, geometry.size, &end);
}

struct ClippedRect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

inline bool clip_rect(int64_t x, int64_t y, uint64_t width, uint64_t height,
                      uint32_t screen_width, uint32_t screen_height,
                      ClippedRect* result)
{
    if (!result || screen_width == 0 || screen_height == 0 || width == 0 || height == 0) return false;
    const int64_t bounded_width = static_cast<int64_t>(width > INT64_MAX ? INT64_MAX : width);
    const int64_t bounded_height = static_cast<int64_t>(height > INT64_MAX ? INT64_MAX : height);
    const int64_t right = x > 0 && static_cast<uint64_t>(x) > static_cast<uint64_t>(INT64_MAX) - bounded_width
                              ? INT64_MAX
                              : x + bounded_width;
    const int64_t bottom = y > 0 && static_cast<uint64_t>(y) > static_cast<uint64_t>(INT64_MAX) - bounded_height
                               ? INT64_MAX
                               : y + bounded_height;
    if (right <= 0 || bottom <= 0 || x >= static_cast<int64_t>(screen_width) ||
        y >= static_cast<int64_t>(screen_height)) return false;
    const int64_t left = x < 0 ? 0 : x;
    const int64_t top = y < 0 ? 0 : y;
    const int64_t clipped_right = right > static_cast<int64_t>(screen_width) ? screen_width : right;
    const int64_t clipped_bottom = bottom > static_cast<int64_t>(screen_height) ? screen_height : bottom;
    if (clipped_right <= left || clipped_bottom <= top) return false;
    result->x = static_cast<uint32_t>(left);
    result->y = static_cast<uint32_t>(top);
    result->width = static_cast<uint32_t>(clipped_right - left);
    result->height = static_cast<uint32_t>(clipped_bottom - top);
    return true;
}

} // namespace framebuffer
} // namespace kernel
