#pragma once

#include "image.h"
#include <cstdint>

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gxos {
namespace gui {

class ImageRenderer {
public:
    static void DrawImage(uint32_t* targetPixels, int targetWidth, int targetHeight, int targetPitchBytes, const ImagePtr& image, int x, int y);
    static void DrawImage(uint32_t* targetPixels, int targetWidth, int targetHeight, int targetPitchBytes, const ImagePtr& image, int x, int y, int width, int height);

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
    static void DrawImage(HDC dc, const ImagePtr& image, int x, int y);
    static void DrawImage(HDC dc, const ImagePtr& image, int x, int y, int width, int height);
#endif
};

} // namespace gui
} // namespace gxos
