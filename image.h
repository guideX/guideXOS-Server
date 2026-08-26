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

// Test-only deterministic fault injection for the final host-side pixel
// buffer. UINT32_MAX disables injection; zero denies the next Image buffer.
void SetImageAllocationFailureInjection(uint32_t failAfterAllocations);

} // namespace gui
} // namespace gxos
