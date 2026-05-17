#include "image_renderer.h"
#include "logger.h"
#include <algorithm>
#include <vector>

namespace gxos {
namespace gui {

static uint8_t blendChannel(uint8_t src, uint8_t dst, uint8_t alpha)
{
    return static_cast<uint8_t>((static_cast<unsigned int>(src) * alpha + static_cast<unsigned int>(dst) * (255 - alpha)) / 255);
}

void ImageRenderer::DrawImage(uint32_t* targetPixels, int targetWidth, int targetHeight, int targetPitchBytes, const ImagePtr& image, int x, int y)
{
    if (!targetPixels || targetWidth <= 0 || targetHeight <= 0 || targetPitchBytes <= 0) {
        Logger::write(LogLevel::Warn, "ImageRenderer: invalid target");
        return;
    }
    if (!image || !image->isValid() || image->Channels < 4) {
        Logger::write(LogLevel::Warn, "ImageRenderer: invalid image");
        return;
    }

    int srcStartX = 0;
    int srcStartY = 0;
    int drawX = x;
    int drawY = y;
    int drawW = image->Width;
    int drawH = image->Height;

    if (drawX < 0) {
        srcStartX = -drawX;
        drawW += drawX;
        drawX = 0;
    }
    if (drawY < 0) {
        srcStartY = -drawY;
        drawH += drawY;
        drawY = 0;
    }
    if (drawX + drawW > targetWidth) drawW = targetWidth - drawX;
    if (drawY + drawH > targetHeight) drawH = targetHeight - drawY;
    if (drawW <= 0 || drawH <= 0) return;

    const int targetStride = targetPitchBytes / 4;
    for (int row = 0; row < drawH; ++row) {
        uint32_t* dstRow = targetPixels + (drawY + row) * targetStride + drawX;
        const uint8_t* srcRow = image->Pixels + ((srcStartY + row) * image->Width + srcStartX) * image->Channels;
        for (int col = 0; col < drawW; ++col) {
            const uint8_t* src = srcRow + col * image->Channels;
            const uint8_t sr = src[0];
            const uint8_t sg = src[1];
            const uint8_t sb = src[2];
            const uint8_t sa = src[3];
            if (sa == 0) continue;
            if (sa == 255) {
                dstRow[col] = 0xFF000000u | (static_cast<uint32_t>(sr) << 16) | (static_cast<uint32_t>(sg) << 8) | sb;
            } else {
                uint32_t dst = dstRow[col];
                uint8_t dr = static_cast<uint8_t>((dst >> 16) & 0xFF);
                uint8_t dg = static_cast<uint8_t>((dst >> 8) & 0xFF);
                uint8_t db = static_cast<uint8_t>(dst & 0xFF);
                dstRow[col] = 0xFF000000u |
                    (static_cast<uint32_t>(blendChannel(sr, dr, sa)) << 16) |
                    (static_cast<uint32_t>(blendChannel(sg, dg, sa)) << 8) |
                    blendChannel(sb, db, sa);
            }
        }
    }
}

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
void ImageRenderer::DrawImage(HDC dc, const ImagePtr& image, int x, int y)
{
    if (!image) {
        Logger::write(LogLevel::Warn, "ImageRenderer: invalid image for HDC draw");
        return;
    }
    DrawImage(dc, image, x, y, image->Width, image->Height);
}

void ImageRenderer::DrawImage(HDC dc, const ImagePtr& image, int x, int y, int width, int height)
{
    if (!dc) {
        Logger::write(LogLevel::Warn, "ImageRenderer: invalid HDC");
        return;
    }
    if (!image || !image->isValid() || image->Channels < 4) {
        Logger::write(LogLevel::Warn, "ImageRenderer: invalid image for HDC draw");
        return;
    }
    if (width <= 0 || height <= 0) {
        Logger::write(LogLevel::Warn, "ImageRenderer: invalid HDC draw size");
        return;
    }

    std::vector<uint32_t> bgra(static_cast<size_t>(image->Width) * static_cast<size_t>(image->Height));
    for (int row = 0; row < image->Height; ++row) {
        for (int col = 0; col < image->Width; ++col) {
            const uint8_t* src = image->Pixels + (row * image->Width + col) * image->Channels;
            bgra[static_cast<size_t>(row) * image->Width + col] =
                (static_cast<uint32_t>(src[3]) << 24) |
                (static_cast<uint32_t>(src[2]) << 16) |
                (static_cast<uint32_t>(src[1]) << 8) |
                src[0];
        }
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = image->Width;
    bmi.bmiHeader.biHeight = -image->Height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC mem = CreateCompatibleDC(dc);
    if (!mem) {
        Logger::write(LogLevel::Warn, "ImageRenderer: CreateCompatibleDC failed");
        return;
    }

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        DeleteDC(mem);
        Logger::write(LogLevel::Warn, "ImageRenderer: CreateDIBSection failed");
        return;
    }

    std::copy(bgra.begin(), bgra.end(), static_cast<uint32_t*>(bits));
    HGDIOBJ old = SelectObject(mem, dib);
    AlphaBlend(dc, x, y, width, height, mem, 0, 0, image->Width, image->Height, blend);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
}
#endif

} // namespace gui
} // namespace gxos
