#include "png_loader.h"
#include "logger.h"
#include "vfs.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <new>
#include <vector>

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
#include <fstream>
#include <iterator>
#endif

static void* gxos_stbi_malloc(size_t size)
{
    size_t total = size + sizeof(size_t);
    unsigned char* raw = new (std::nothrow) unsigned char[total];
    if (!raw) return nullptr;
    *reinterpret_cast<size_t*>(raw) = size;
    return raw + sizeof(size_t);
}

static void gxos_stbi_free(void* ptr)
{
    if (!ptr) return;
    unsigned char* raw = static_cast<unsigned char*>(ptr) - sizeof(size_t);
    delete[] raw;
}

static void* gxos_stbi_realloc_sized(void* ptr, size_t oldSize, size_t newSize)
{
    if (!ptr) return gxos_stbi_malloc(newSize);
    void* newPtr = gxos_stbi_malloc(newSize);
    if (!newPtr) return nullptr;
    std::memcpy(newPtr, ptr, oldSize < newSize ? oldSize : newSize);
    gxos_stbi_free(ptr);
    return newPtr;
}

static void* gxos_stbi_realloc(void* ptr, size_t newSize)
{
    if (!ptr) return gxos_stbi_malloc(newSize);
    unsigned char* raw = static_cast<unsigned char*>(ptr) - sizeof(size_t);
    size_t oldSize = *reinterpret_cast<size_t*>(raw);
    return gxos_stbi_realloc_sized(ptr, oldSize, newSize);
}

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_FAILURE_USERMSG
#define STBI_NO_THREAD_LOCALS
#define STBI_MALLOC(sz) gxos_stbi_malloc(sz)
#define STBI_REALLOC(p, sz) gxos_stbi_realloc(p, sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) gxos_stbi_realloc_sized(p, oldsz, newsz)
#define STBI_FREE(p) gxos_stbi_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

namespace gxos {
namespace gui {

ImagePtr PngLoader::LoadFromMemory(const uint8_t* bytes, size_t byteCount, const std::string& sourceName)
{
    if (!bytes || byteCount == 0) {
        Logger::write(LogLevel::Warn, std::string("PngLoader: empty PNG data: ") + sourceName);
        return nullptr;
    }

    if (byteCount > static_cast<size_t>(INT_MAX)) {
        Logger::write(LogLevel::Warn, std::string("PngLoader: PNG too large: ") + sourceName);
        return nullptr;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    const int requestedChannels = 4;
    stbi_uc* decoded = stbi_load_from_memory(bytes, static_cast<int>(byteCount), &width, &height, &sourceChannels, requestedChannels);
    if (!decoded) {
        Logger::write(LogLevel::Warn, std::string("PngLoader: decode failed for ") + sourceName + ": " + stbi_failure_reason());
        return nullptr;
    }

    std::shared_ptr<Image> image(new (std::nothrow) Image(width, height, requestedChannels));
    if (!image || !image->isValid()) {
        stbi_image_free(decoded);
        Logger::write(LogLevel::Error, std::string("PngLoader: allocation failed for ") + sourceName);
        return nullptr;
    }

    const size_t decodedByteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(requestedChannels);
    std::copy(decoded, decoded + decodedByteCount, image->Pixels);
    stbi_image_free(decoded);

    Logger::write(LogLevel::Info,
        std::string("PngLoader: loaded ") + sourceName + " " +
        std::to_string(width) + "x" + std::to_string(height) +
        " sourceChannels=" + std::to_string(sourceChannels));
    return image;
}

ImagePtr PngLoader::LoadFromMemory(const std::vector<uint8_t>& bytes, const std::string& sourceName)
{
    return LoadFromMemory(bytes.empty() ? nullptr : bytes.data(), bytes.size(), sourceName);
}

ImagePtr PngLoader::LoadFromFile(const std::string& path)
{
    if (path.empty()) {
        Logger::write(LogLevel::Warn, "PngLoader: empty path");
        return nullptr;
    }

    std::vector<uint8_t> encoded;
    if (Vfs::instance().readFile(path, encoded)) {
        return LoadFromMemory(encoded, path);
    }

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
    std::ifstream file(path, std::ios::binary);
    if (file) {
        encoded.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return LoadFromMemory(encoded, path);
    }
#endif

    Logger::write(LogLevel::Warn, std::string("PngLoader: file not found in VFS: ") + path);
    return nullptr;
}

} // namespace gui
} // namespace gxos
