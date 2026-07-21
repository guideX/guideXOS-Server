#include "png_codec.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <mutex>

namespace gxos {
namespace gui {
namespace PngCodec {
namespace {

void appendUint16LE(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void appendUint32BE(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
}

uint32_t crc32ForBytes(const uint8_t* data, size_t size)
{
    static uint32_t table[256];
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
    });

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t adler32ForBytes(const uint8_t* data, size_t size)
{
    const uint32_t kMod = 65521u;
    uint32_t a = 1u;
    uint32_t b = 0u;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

void appendPngChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data)
{
    appendUint32BE(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    appendUint32BE(out, crc32ForBytes(out.data() + start, 4u + data.size()));
}

} // namespace

bool EncodeRgba8(const ImagePtr& image, std::vector<uint8_t>& bytes, std::string& error)
{
    bytes.clear();
    error.clear();
    if (!image || !image->isValid() || image->Channels < 4 || image->Width <= 0 || image->Height <= 0) {
        error = "image is not a valid RGBA bitmap";
        return false;
    }

    constexpr size_t kMaxRawBytes = 128u * 1024u * 1024u;
    const size_t width = static_cast<size_t>(image->Width);
    const size_t height = static_cast<size_t>(image->Height);
    if (width > (std::numeric_limits<size_t>::max() - 1u) / 4u) {
        error = "image row stride overflows";
        return false;
    }
    const size_t rowBytes = 1u + width * 4u;
    if (height > std::numeric_limits<size_t>::max() / rowBytes) {
        error = "image output size overflows";
        return false;
    }
    const size_t rawBytes = rowBytes * height;
    if (rawBytes > kMaxRawBytes || rawBytes > std::numeric_limits<uint32_t>::max()) {
        error = "image output exceeds PNG encoder limit";
        return false;
    }

    try {
        bytes.insert(bytes.end(), { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' });
        std::vector<uint8_t> ihdr;
        appendUint32BE(ihdr, static_cast<uint32_t>(image->Width));
        appendUint32BE(ihdr, static_cast<uint32_t>(image->Height));
        ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
        appendPngChunk(bytes, "IHDR", ihdr);

        std::vector<uint8_t> raw;
        raw.reserve(rawBytes);
        for (int y = 0; y < image->Height; ++y) {
            raw.push_back(0); // filter type None
            for (int x = 0; x < image->Width; ++x) {
                const uint8_t* src = image->Pixels + (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * static_cast<size_t>(image->Channels);
                raw.insert(raw.end(), src, src + 4);
            }
        }

        std::vector<uint8_t> zlibData;
        const size_t blockCount = (raw.size() + 65534u) / 65535u;
        if (blockCount > (std::numeric_limits<size_t>::max() - raw.size() - 6u) / 5u) {
            error = "PNG block count overflows";
            bytes.clear();
            return false;
        }
        zlibData.reserve(raw.size() + blockCount * 5u + 6u);
        zlibData.insert(zlibData.end(), { 0x78, 0x01 });
        size_t offset = 0;
        while (offset < raw.size()) {
            const size_t blockLen = std::min<size_t>(65535u, raw.size() - offset);
            const bool finalBlock = offset + blockLen >= raw.size();
            zlibData.push_back(finalBlock ? 0x01 : 0x00);
            appendUint16LE(zlibData, static_cast<uint16_t>(blockLen));
            appendUint16LE(zlibData, static_cast<uint16_t>(~static_cast<uint16_t>(blockLen)));
            zlibData.insert(zlibData.end(), raw.begin() + offset, raw.begin() + offset + blockLen);
            offset += blockLen;
        }
        appendUint32BE(zlibData, adler32ForBytes(raw.data(), raw.size()));
        appendPngChunk(bytes, "IDAT", zlibData);
        appendPngChunk(bytes, "IEND", {});
        return true;
    } catch (const std::exception& exception) {
        bytes.clear();
        error = std::string("PNG encoder allocation failed: ") + exception.what();
        return false;
    }
}

ImagePtr ScaleNearest(const ImagePtr& image, int width, int height)
{
    if (!image || !image->isValid() || image->Width <= 0 || image->Height <= 0 || image->Channels < 4 || width <= 0 || height <= 0) {
        return nullptr;
    }
    if (static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / static_cast<size_t>(height)) return nullptr;
    const size_t outputPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (outputPixels > std::numeric_limits<size_t>::max() / 4u) return nullptr;
    ImagePtr scaled;
    try {
        scaled = std::make_shared<Image>(width, height, 4);
    } catch (...) {
        return nullptr;
    }
    if (!scaled || !scaled->isValid()) return nullptr;
    for (int y = 0; y < height; ++y) {
        const int sourceY = std::min(image->Height - 1, static_cast<int>((static_cast<uint64_t>(y) * static_cast<uint64_t>(image->Height)) / static_cast<uint64_t>(height)));
        for (int x = 0; x < width; ++x) {
            const int sourceX = std::min(image->Width - 1, static_cast<int>((static_cast<uint64_t>(x) * static_cast<uint64_t>(image->Width)) / static_cast<uint64_t>(width)));
            const uint8_t* src = image->Pixels + (static_cast<size_t>(sourceY) * static_cast<size_t>(image->Width) + static_cast<size_t>(sourceX)) * static_cast<size_t>(image->Channels);
            uint8_t* dst = scaled->Pixels + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
            std::copy(src, src + 4, dst);
        }
    }
    return scaled;
}

} // namespace PngCodec
} // namespace gui
} // namespace gxos

