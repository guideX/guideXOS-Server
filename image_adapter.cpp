#include "kernel/core/include/kernel/image_adapter.h"

#include "image_renderer.h"
#include "logger.h"
#include "png_loader.h"
#include "vfs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace gxos {
namespace gui {
namespace {

static bool endsWithIgnoreCase(std::string value, const std::string& suffix)
{
    if (suffix.size() > value.size()) return false;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.substr(value.size() - suffix.size()) == suffix;
}

static bool readPngSize(const uint8_t* bytes, size_t byteCount, uint32_t& width, uint32_t& height)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (!bytes || byteCount < 24) return false;
    for (int i = 0; i < 8; ++i) {
        if (bytes[static_cast<size_t>(i)] != sig[i]) return false;
    }
    if (bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R') return false;
    auto be32 = [bytes](size_t pos) -> uint32_t {
        return (static_cast<uint32_t>(bytes[pos]) << 24) |
               (static_cast<uint32_t>(bytes[pos + 1]) << 16) |
               (static_cast<uint32_t>(bytes[pos + 2]) << 8) |
               static_cast<uint32_t>(bytes[pos + 3]);
    };
    width = be32(16);
    height = be32(20);
    return width > 0 && height > 0;
}

static bool withinLimits(uint64_t byteCount, uint32_t width, uint32_t height, const ImageSafetyLimits& limits)
{
    if (byteCount > limits.maxBytes) return false;
    if (width == 0 || height == 0) return false;
    if (width > limits.maxWidth || height > limits.maxHeight) return false;
    uint64_t pixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    return pixels <= limits.maxPixels;
}

static std::string runtimeWallpaperPathToHostPath(const std::string& path)
{
    const std::string prefix = "/system/wallpapers/";
    if (path.rfind(prefix, 0) != 0) return path;
    return std::string("assets/Backgrounds/") + path.substr(prefix.size());
}

static std::string relativeHostPath(const std::string& path)
{
    std::string hostPath = runtimeWallpaperPathToHostPath(path);
    if (!hostPath.empty() && (hostPath[0] == '/' || hostPath[0] == '\\')) {
        hostPath = hostPath.substr(1);
    }
#if defined(_WIN32)
    for (char& c : hostPath) {
        if (c == '/') c = '\\';
    }
#endif
    return hostPath;
}

static bool readHostFile(const std::string& path, std::vector<uint8_t>& bytes, const ImageSafetyLimits& limits, ImageLoadStatus& status)
{
    std::string hostPath = relativeHostPath(path);
    std::ifstream file(hostPath, std::ios::binary);
    if (!file) {
        status = ImageLoadStatus::NotFound;
        return false;
    }
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size < 0) {
        status = ImageLoadStatus::DecodeFailed;
        return false;
    }
    if (static_cast<uint64_t>(size) > limits.maxBytes) {
        status = ImageLoadStatus::TooLarge;
        return false;
    }
    file.seekg(0, std::ios::beg);
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (!file.good() && !file.eof()) {
        bytes.clear();
        status = ImageLoadStatus::DecodeFailed;
        return false;
    }
    status = ImageLoadStatus::Ok;
    return true;
}

} // namespace

const char* ImageLoadStatusName(ImageLoadStatus status)
{
    switch (status) {
    case ImageLoadStatus::Ok: return "Ok";
    case ImageLoadStatus::NotFound: return "NotFound";
    case ImageLoadStatus::UnsupportedFormat: return "UnsupportedFormat";
    case ImageLoadStatus::DecodeFailed: return "DecodeFailed";
    case ImageLoadStatus::TooLarge: return "TooLarge";
    case ImageLoadStatus::OutOfMemory: return "OutOfMemory";
    }
    return "Unknown";
}

ImageBitmap ImageAdapter::LoadFromFile(const std::string& path, const ImageSafetyLimits& limits)
{
    ImageBitmap result;
    result.source = path;
    if (path.empty()) {
        result.status = ImageLoadStatus::NotFound;
        return result;
    }
    if (!endsWithIgnoreCase(path, ".png")) {
        result.status = ImageLoadStatus::UnsupportedFormat;
        return result;
    }

    std::vector<uint8_t> bytes;
    if (Vfs::instance().readFile(path, bytes)) {
        return LoadFromBytes(bytes, path, limits);
    }

    ImageLoadStatus readStatus = ImageLoadStatus::NotFound;
    if (!readHostFile(path, bytes, limits, readStatus)) {
        result.status = readStatus;
        return result;
    }
    return LoadFromBytes(bytes, path, limits);
}

ImageBitmap ImageAdapter::LoadFromBytes(const std::vector<uint8_t>& bytes, const std::string& sourceName, const ImageSafetyLimits& limits)
{
    return LoadFromBytes(bytes.empty() ? nullptr : bytes.data(), bytes.size(), sourceName, limits);
}

ImageBitmap ImageAdapter::LoadFromBytes(const uint8_t* bytes, size_t byteCount, const std::string& sourceName, const ImageSafetyLimits& limits)
{
    ImageBitmap result;
    result.source = sourceName;
    if (!bytes || byteCount == 0) {
        result.status = ImageLoadStatus::NotFound;
        return result;
    }
    if (byteCount > limits.maxBytes) {
        result.status = ImageLoadStatus::TooLarge;
        return result;
    }

    uint32_t headerW = 0;
    uint32_t headerH = 0;
    if (!readPngSize(bytes, byteCount, headerW, headerH)) {
        result.status = ImageLoadStatus::UnsupportedFormat;
        return result;
    }
    if (!withinLimits(byteCount, headerW, headerH, limits)) {
        result.status = ImageLoadStatus::TooLarge;
        return result;
    }

    ImagePtr decoded = PngLoader::LoadFromMemory(bytes, byteCount, sourceName);
    if (!decoded) {
        result.status = ImageLoadStatus::DecodeFailed;
        return result;
    }
    if (!decoded->isValid()) {
        result.status = ImageLoadStatus::OutOfMemory;
        return result;
    }
    if (!withinLimits(byteCount, static_cast<uint32_t>(decoded->Width), static_cast<uint32_t>(decoded->Height), limits)) {
        result.status = ImageLoadStatus::TooLarge;
        return result;
    }

    result.status = ImageLoadStatus::Ok;
    result.image = decoded;
    result.width = decoded->Width;
    result.height = decoded->Height;
    return result;
}

void ImageAdapter::DrawToPixels(uint32_t* targetPixels, int targetWidth, int targetHeight, int targetPitchBytes,
                                const ImageBitmap& image, int x, int y)
{
    if (image.status != ImageLoadStatus::Ok || !image.image) return;
    ImageRenderer::DrawImage(targetPixels, targetWidth, targetHeight, targetPitchBytes, image.image, x, y);
}

void ImageAdapter::DrawToPixels(uint32_t* targetPixels, int targetWidth, int targetHeight, int targetPitchBytes,
                                const ImageBitmap& image, int x, int y, int width, int height)
{
    if (image.status != ImageLoadStatus::Ok || !image.image) return;
    ImageRenderer::DrawImage(targetPixels, targetWidth, targetHeight, targetPitchBytes, image.image, x, y, width, height);
}

#if defined(_WIN32)
void ImageAdapter::DrawToHdc(HDC dc, const ImageBitmap& image, int x, int y)
{
    if (image.status != ImageLoadStatus::Ok || !image.image) return;
    ImageRenderer::DrawImage(dc, image.image, x, y);
}

void ImageAdapter::DrawToHdc(HDC dc, const ImageBitmap& image, int x, int y, int width, int height)
{
    if (image.status != ImageLoadStatus::Ok || !image.image) return;
    ImageRenderer::DrawImage(dc, image.image, x, y, width, height);
}
#endif

} // namespace gui
} // namespace gxos
