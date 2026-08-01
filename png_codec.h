#pragma once

#include "image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace gui {
namespace PngCodec {

// Small RGBA8 encoder shared by hosted Image Viewer and user backgrounds.
bool EncodeRgba8(const ImagePtr& image, std::vector<uint8_t>& bytes, std::string& error);

// Nearest-neighbor RGBA scaling used for bounded background thumbnails.
ImagePtr ScaleNearest(const ImagePtr& image, int width, int height);

} // namespace PngCodec
} // namespace gui
} // namespace gxos

