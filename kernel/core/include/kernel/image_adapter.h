#pragma once

#if defined(GXOS_BARE_METAL)
#include "types.h"
#else
#include "image.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#endif

namespace gxos {
namespace gui {

enum class ImageLoadStatus : uint8_t {
    Ok = 0,
    NotFound,
    UnsupportedFormat,
    DecodeFailed,
    TooLarge,
    OutOfMemory,
};

struct ImageSafetyLimits {
    uint32_t maxBytes;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t maxPixels;
};

inline ImageSafetyLimits DefaultImageSafetyLimits()
{
    ImageSafetyLimits limits{};
    limits.maxBytes = 4u * 1024u * 1024u;
    limits.maxWidth = 4096u;
    limits.maxHeight = 4096u;
    limits.maxPixels = 4096u * 4096u;
    return limits;
}

const char* ImageLoadStatusName(ImageLoadStatus status);

#if defined(GXOS_BARE_METAL)

struct ImageProbe {
    ImageLoadStatus status;
    uint32_t width;
    uint32_t height;
};

struct ImageBitmap {
    ImageLoadStatus status;
    const uint32_t* pixels;
    uint32_t width;
    uint32_t height;
};

class ImageAdapter {
public:
    static ImageProbe ProbeFile(const char* path, const ImageSafetyLimits& limits = DefaultImageSafetyLimits());
    static ImageProbe ProbeBytes(const uint8_t* bytes, uint32_t byteCount, const ImageSafetyLimits& limits = DefaultImageSafetyLimits());
    static ImageBitmap LoadFromFile(const char* path, const ImageSafetyLimits& limits = DefaultImageSafetyLimits());
    static bool DrawToFramebuffer(const ImageBitmap& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
};

#else

struct ImageBitmap {
    ImageLoadStatus status = ImageLoadStatus::NotFound;
    ImagePtr image;
    int width = 0;
    int height = 0;
    std::string source;
};

class ImageAdapter {
public:
    static ImageBitmap LoadFromFile(const std::string& path, const ImageSafetyLimits& limits = DefaultImageSafetyLimits());
    static ImageBitmap LoadFromBytes(const std::vector<uint8_t>& bytes, const std::string& sourceName = "<memory>",
                                     const ImageSafetyLimits& limits = DefaultImageSafetyLimits());
    static ImageBitmap LoadFromBytes(const uint8_t* bytes, size_t byteCount, const std::string& sourceName = "<memory>",
                                     const ImageSafetyLimits& limits = DefaultImageSafetyLimits());

    static void DrawToPixels(uint32_t* targetPixels, int targetWidth, int targetHeight, int targetPitchBytes,
                             const ImageBitmap& image, int x, int y);
    static void DrawToPixels(uint32_t* targetPixels, int targetWidth, int targetHeight, int targetPitchBytes,
                             const ImageBitmap& image, int x, int y, int width, int height);

#if defined(_WIN32)
    static void DrawToHdc(HDC dc, const ImageBitmap& image, int x, int y);
    static void DrawToHdc(HDC dc, const ImageBitmap& image, int x, int y, int width, int height);
#endif
};

#endif

} // namespace gui
} // namespace gxos
