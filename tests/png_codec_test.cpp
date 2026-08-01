#include "kernel/core/include/kernel/image_adapter.h"
#include "png_codec.h"

#include <iostream>
#include <memory>
#include <vector>

using gxos::gui::Image;
using gxos::gui::ImageAdapter;
using gxos::gui::ImageLoadStatus;
using gxos::gui::ImagePtr;

namespace {

bool expect(bool condition, const char* label)
{
    if (!condition) std::cerr << "FAIL: " << label << "\n";
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    ImagePtr image = std::make_shared<Image>(320, 240, 4);
    ok &= expect(image && image->isValid(), "source image allocation");
    if (image && image->isValid()) {
        for (int y = 0; y < image->Height; ++y) {
            for (int x = 0; x < image->Width; ++x) {
                uint8_t* pixel = image->Pixels + (static_cast<size_t>(y) * image->Width + x) * 4u;
                pixel[0] = static_cast<uint8_t>(x & 0xFF);
                pixel[1] = static_cast<uint8_t>(y & 0xFF);
                pixel[2] = 0xA5;
                pixel[3] = static_cast<uint8_t>((x + y) & 0xFF);
            }
        }
    }

    std::vector<uint8_t> encoded;
    std::string error;
    ok &= expect(gxos::gui::PngCodec::EncodeRgba8(image, encoded, error), "RGBA PNG encoding");
    const auto decoded = ImageAdapter::LoadFromBytes(encoded, "codec-test.png");
    ok &= expect(decoded.status == ImageLoadStatus::Ok && decoded.width == 320 && decoded.height == 240,
                 "encoded PNG decodes with original dimensions");
    if (decoded.image && decoded.image->isValid()) {
        const uint8_t* sourcePixel = image->Pixels + (17u * static_cast<size_t>(image->Width) + 23u) * 4u;
        const uint8_t* decodedPixel = decoded.image->Pixels + (17u * static_cast<size_t>(decoded.width) + 23u) * 4u;
        ok &= expect(sourcePixel[3] == decodedPixel[3], "PNG alpha preserved");
    }

    const ImagePtr thumbnail = gxos::gui::PngCodec::ScaleNearest(decoded.image, 160, 120);
    ok &= expect(thumbnail && thumbnail->isValid() && thumbnail->Width == 160 && thumbnail->Height == 120,
                 "nearest thumbnail is bounded");
    std::vector<uint8_t> thumbnailBytes;
    ok &= expect(gxos::gui::PngCodec::EncodeRgba8(thumbnail, thumbnailBytes, error), "thumbnail encoding");
    const auto decodedThumbnail = ImageAdapter::LoadFromBytes(thumbnailBytes, "codec-test-thumb.png");
    ok &= expect(decodedThumbnail.status == ImageLoadStatus::Ok && decodedThumbnail.width == 160 && decodedThumbnail.height == 120,
                 "thumbnail decodes with bounded dimensions");

    ImagePtr invalid;
    ok &= expect(!gxos::gui::PngCodec::EncodeRgba8(invalid, encoded, error), "invalid image rejected");
    std::cout << (ok ? "PngCodec tests PASS\n" : "PngCodec tests FAIL\n");
    return ok ? 0 : 1;
}

