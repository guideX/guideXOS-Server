#pragma once

#include <cstdint>
#include <memory>

namespace gxos {
namespace gui {

class Image {
public:
    Image(int width, int height, int channels);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    bool isValid() const;

    int Width;
    int Height;
    int Channels;
    uint8_t* Pixels;
};

using ImagePtr = std::shared_ptr<Image>;

} // namespace gui
} // namespace gxos
