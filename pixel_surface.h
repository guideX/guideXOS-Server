#pragma once

#if defined(GXOS_BARE_METAL)
#include "kernel/core/include/kernel/types.h"
#else
#include <cstdint>
#endif

namespace gxos {
namespace gui {

enum class PixelFormatKind : uint32_t {
    Unknown = 0,
    B8G8R8X8_UNORM = 1,
    B8G8R8A8_UNORM = 2,
    X8R8G8B8_UNORM = 3,
    A8R8G8B8_UNORM = 4,
    R8G8B8A8_UNORM = 5,
    X8B8G8R8_UNORM = 6,
    A8B8G8R8_UNORM = 7,
    R8G8B8X8_UNORM = 8,
};

struct PixelRect {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};

    int width() const
    {
        return right > left ? right - left : 0;
    }

    int height() const
    {
        return bottom > top ? bottom - top : 0;
    }

    bool isValid() const
    {
        return width() > 0 && height() > 0;
    }
};

struct PixelSurface {
    uint32_t* pixelPointer{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitchBytes{0};
    uint8_t bytesPerPixel{0};
    PixelFormatKind pixelFormat{PixelFormatKind::Unknown};
    int viewportOriginX{0};
    int viewportOriginY{0};
    uint32_t targetIndex{0};
    uint32_t monitorId{0};
    uint32_t scanoutId{0};
    bool primary{false};
    bool taskbarVisible{false};
    PixelRect clipRect{};
    uint64_t backingByteCount{0};

    bool isValid() const
    {
        return pixelPointer != nullptr && width > 0u && height > 0u && pitchBytes >= width * (bytesPerPixel == 0u ? 1u : bytesPerPixel);
    }
};

inline const char* pixelFormatKindName(PixelFormatKind format)
{
    switch (format) {
    case PixelFormatKind::B8G8R8X8_UNORM:
        return "B8G8R8X8_UNORM";
    case PixelFormatKind::B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case PixelFormatKind::X8R8G8B8_UNORM:
        return "X8R8G8B8_UNORM";
    case PixelFormatKind::A8R8G8B8_UNORM:
        return "A8R8G8B8_UNORM";
    case PixelFormatKind::R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case PixelFormatKind::X8B8G8R8_UNORM:
        return "X8B8G8R8_UNORM";
    case PixelFormatKind::A8B8G8R8_UNORM:
        return "A8B8G8R8_UNORM";
    case PixelFormatKind::R8G8B8X8_UNORM:
        return "R8G8B8X8_UNORM";
    default:
        return "unknown";
    }
}

inline uint8_t pixelFormatBytesPerPixel(PixelFormatKind format)
{
    switch (format) {
    case PixelFormatKind::B8G8R8X8_UNORM:
    case PixelFormatKind::B8G8R8A8_UNORM:
    case PixelFormatKind::X8R8G8B8_UNORM:
    case PixelFormatKind::A8R8G8B8_UNORM:
    case PixelFormatKind::R8G8B8A8_UNORM:
    case PixelFormatKind::X8B8G8R8_UNORM:
    case PixelFormatKind::A8B8G8R8_UNORM:
    case PixelFormatKind::R8G8B8X8_UNORM:
        return 4u;
    default:
        return 0u;
    }
}

inline bool pixelFormatIsBgrxLike(PixelFormatKind format)
{
    return format == PixelFormatKind::B8G8R8X8_UNORM || format == PixelFormatKind::B8G8R8A8_UNORM;
}

inline PixelRect makePixelRect(int left, int top, int right, int bottom)
{
    return PixelRect{ left, top, right, bottom };
}

inline PixelRect offsetPixelRect(const PixelRect& rect, int dx, int dy)
{
    return PixelRect{ rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy };
}

inline PixelRect intersectPixelRect(const PixelRect& a, const PixelRect& b)
{
    const int left = a.left > b.left ? a.left : b.left;
    const int top = a.top > b.top ? a.top : b.top;
    const int right = a.right < b.right ? a.right : b.right;
    const int bottom = a.bottom < b.bottom ? a.bottom : b.bottom;
    if (right <= left || bottom <= top) {
        return PixelRect{};
    }
    return PixelRect{ left, top, right, bottom };
}

inline uint64_t pixelSurfaceExpectedByteCount(const PixelSurface& surface)
{
    return static_cast<uint64_t>(surface.pitchBytes) * static_cast<uint64_t>(surface.height);
}

inline bool pixelSurfaceCanCoverBacking(const PixelSurface& surface)
{
    if (!surface.isValid()) {
        return false;
    }

    return surface.backingByteCount >= pixelSurfaceExpectedByteCount(surface);
}

inline bool pixelSurfaceRequiresConversion(PixelFormatKind surfaceFormat, PixelFormatKind resourceFormat)
{
    return surfaceFormat != PixelFormatKind::Unknown
        && resourceFormat != PixelFormatKind::Unknown
        && surfaceFormat != resourceFormat;
}

inline uint32_t pixelSurfacePackBgrx(uint8_t red, uint8_t green, uint8_t blue)
{
    return (static_cast<uint32_t>(red) << 16)
        | (static_cast<uint32_t>(green) << 8)
        | static_cast<uint32_t>(blue);
}

inline uint32_t pixelSurfaceConvertBgrxToFormat(uint32_t color, PixelFormatKind format)
{
    if (format == PixelFormatKind::B8G8R8X8_UNORM || format == PixelFormatKind::B8G8R8A8_UNORM || format == PixelFormatKind::Unknown) {
        return color;
    }

    const uint8_t blue = static_cast<uint8_t>(color & 0xFFu);
    const uint8_t green = static_cast<uint8_t>((color >> 8) & 0xFFu);
    const uint8_t red = static_cast<uint8_t>((color >> 16) & 0xFFu);
    const uint8_t alpha = 0x00u;

    switch (format) {
    case PixelFormatKind::X8R8G8B8_UNORM:
    case PixelFormatKind::A8R8G8B8_UNORM:
        return (static_cast<uint32_t>(alpha) << 24)
            | (static_cast<uint32_t>(red) << 16)
            | (static_cast<uint32_t>(green) << 8)
            | static_cast<uint32_t>(blue);
    case PixelFormatKind::R8G8B8A8_UNORM:
    case PixelFormatKind::R8G8B8X8_UNORM:
        return (static_cast<uint32_t>(red) << 24)
            | (static_cast<uint32_t>(green) << 16)
            | (static_cast<uint32_t>(blue) << 8)
            | static_cast<uint32_t>(alpha);
    case PixelFormatKind::X8B8G8R8_UNORM:
    case PixelFormatKind::A8B8G8R8_UNORM:
        return (static_cast<uint32_t>(alpha) << 24)
            | (static_cast<uint32_t>(blue) << 16)
            | (static_cast<uint32_t>(green) << 8)
            | static_cast<uint32_t>(red);
    default:
        return color;
    }
}

inline bool pixelSurfaceLocalRectFromGlobal(
    const PixelSurface& surface,
    int globalX,
    int globalY,
    int width,
    int height,
    PixelRect* localRectOut)
{
    if (localRectOut == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const PixelRect surfaceBounds = makePixelRect(0, 0, static_cast<int>(surface.width), static_cast<int>(surface.height));
    const PixelRect desiredLocal = makePixelRect(
        globalX - surface.viewportOriginX,
        globalY - surface.viewportOriginY,
        globalX - surface.viewportOriginX + width,
        globalY - surface.viewportOriginY + height);
    const PixelRect clipped = intersectPixelRect(desiredLocal, surfaceBounds);
    if (!clipped.isValid()) {
        return false;
    }

    *localRectOut = clipped;
    return true;
}

inline PixelRect pixelSurfaceGlobalBounds(const PixelSurface& surface)
{
    return PixelRect{
        surface.viewportOriginX,
        surface.viewportOriginY,
        surface.viewportOriginX + static_cast<int>(surface.width),
        surface.viewportOriginY + static_cast<int>(surface.height)
    };
}

} // namespace gui
} // namespace gxos
