#include "image.h"
#include <algorithm>
#include <new>

namespace gxos {
namespace gui {

Image::Image(int width, int height, int channels)
    : Width(width)
    , Height(height)
    , Channels(channels)
    , Pixels(nullptr)
{
    if (Width <= 0 || Height <= 0 || Channels <= 0) {
        Width = 0;
        Height = 0;
        Channels = 0;
        return;
    }

    const size_t totalBytes = static_cast<size_t>(Width) * static_cast<size_t>(Height) * static_cast<size_t>(Channels);
    Pixels = new (std::nothrow) uint8_t[totalBytes];
    if (!Pixels) {
        Width = 0;
        Height = 0;
        Channels = 0;
    }
}

Image::~Image()
{
    delete[] Pixels;
    Pixels = nullptr;
}

Image::Image(Image&& other) noexcept
    : Width(other.Width)
    , Height(other.Height)
    , Channels(other.Channels)
    , Pixels(other.Pixels)
{
    other.Width = 0;
    other.Height = 0;
    other.Channels = 0;
    other.Pixels = nullptr;
}

Image& Image::operator=(Image&& other) noexcept
{
    if (this != &other) {
        delete[] Pixels;
        Width = other.Width;
        Height = other.Height;
        Channels = other.Channels;
        Pixels = other.Pixels;
        other.Width = 0;
        other.Height = 0;
        other.Channels = 0;
        other.Pixels = nullptr;
    }
    return *this;
}

bool Image::isValid() const
{
    return Width > 0 && Height > 0 && Channels > 0 && Pixels != nullptr;
}

} // namespace gui
} // namespace gxos
