#include "include/kernel/image_adapter.h"

#include "include/kernel/framebuffer.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) do { (void)sizeof(x); } while (0)
static void* gxos_kernel_stbi_malloc(size_t size)
{
    size_t total = size + sizeof(size_t);
    uint8_t* raw = new uint8_t[total];
    if (!raw) return nullptr;
    *reinterpret_cast<size_t*>(raw) = size;
    return raw + sizeof(size_t);
}

static void gxos_kernel_stbi_free(void*)
{
    // Kernel delete/free is intentionally a no-op today. Navigator prepares a
    // bounded number of per-page images, so this follows the existing kernel
    // bump-allocation model without adding a second allocator.
}

static void* gxos_kernel_stbi_realloc_sized(void* ptr, size_t oldSize, size_t newSize)
{
    if (!ptr) return gxos_kernel_stbi_malloc(newSize);
    void* newPtr = gxos_kernel_stbi_malloc(newSize);
    if (!newPtr) return nullptr;
    size_t copyBytes = oldSize < newSize ? oldSize : newSize;
    uint8_t* dst = static_cast<uint8_t*>(newPtr);
    const uint8_t* src = static_cast<const uint8_t*>(ptr);
    for (size_t i = 0; i < copyBytes; ++i) dst[i] = src[i];
    return newPtr;
}

static void* gxos_kernel_stbi_realloc(void* ptr, size_t newSize)
{
    if (!ptr) return gxos_kernel_stbi_malloc(newSize);
    uint8_t* raw = static_cast<uint8_t*>(ptr) - sizeof(size_t);
    size_t oldSize = *reinterpret_cast<size_t*>(raw);
    return gxos_kernel_stbi_realloc_sized(ptr, oldSize, newSize);
}

#define STBI_MALLOC(sz) gxos_kernel_stbi_malloc(sz)
#define STBI_REALLOC(p, sz) gxos_kernel_stbi_realloc(p, sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) gxos_kernel_stbi_realloc_sized(p, oldsz, newsz)
#define STBI_FREE(p) gxos_kernel_stbi_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"

namespace gxos {
namespace gui {
namespace {

// Keep the kernel-side preview path bounded, but large enough for wallpaper-sized PNGs.
static const uint32_t kKernelImageDefaultMaxBytes = 8u * 1024u * 1024u;
static const uint32_t kKernelImageFileScratchBytes = kKernelImageDefaultMaxBytes;
static uint8_t s_kernelImageFileScratch[kKernelImageFileScratchBytes];

#if defined(GXOS_BARE_METAL)
extern "C" size_t gxos_kernel_heap_free_bytes();
#endif

static bool ends_with_png(const char* path)
{
    if (!path) return false;
    uint32_t len = 0;
    while (path[len]) ++len;
    if (len < 4) return false;
    const char* ext = path + len - 4;
    return (ext[0] == '.') &&
           (ext[1] == 'p' || ext[1] == 'P') &&
           (ext[2] == 'n' || ext[2] == 'N') &&
           (ext[3] == 'g' || ext[3] == 'G');
}

static uint32_t be32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static bool png_header_size(const uint8_t* bytes, uint32_t byteCount, uint32_t& width, uint32_t& height)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (!bytes || byteCount < 24) return false;
    for (uint32_t i = 0; i < 8; ++i) {
        if (bytes[i] != sig[i]) return false;
    }
    if (bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R') return false;
    width = be32(bytes + 16);
    height = be32(bytes + 20);
    return width > 0 && height > 0;
}

static bool dimensions_within_limits(uint32_t width, uint32_t height, const ImageSafetyLimits& limits)
{
    if (width == 0 || height == 0) return false;
    if (width > limits.maxWidth || height > limits.maxHeight) return false;
    uint64_t pixels = (uint64_t)width * (uint64_t)height;
    return pixels <= limits.maxPixels;
}

static bool decoded_image_fits_kernel_heap(uint32_t width, uint32_t height)
{
#if defined(GXOS_BARE_METAL)
    if (width == 0 || height == 0) return false;

    const uint64_t decodedBytes = (uint64_t)width * (uint64_t)height * sizeof(uint32_t);
    const uint64_t heapFree = gxos_kernel_heap_free_bytes();

    // The bare-metal loader currently keeps one decoded ARGB buffer and a
    // second converted pixel buffer alive during the load path, so we need to
    // leave room for both plus a little allocator slack.
    const uint64_t requiredBytes = decodedBytes * 2u + 64u * 1024u;
    return requiredBytes <= heapFree;
#else
    (void)width;
    (void)height;
    return true;
#endif
}

static bool is_default_limits(const ImageSafetyLimits& limits)
{
    ImageSafetyLimits defaultLimits = DefaultImageSafetyLimits();
    return limits.maxBytes == defaultLimits.maxBytes &&
           limits.maxWidth == defaultLimits.maxWidth &&
           limits.maxHeight == defaultLimits.maxHeight &&
           limits.maxPixels == defaultLimits.maxPixels;
}

static ImageSafetyLimits resolve_bare_metal_limits(const ImageSafetyLimits& limits)
{
    if (!is_default_limits(limits)) return limits;
    ImageSafetyLimits resolved = limits;
    resolved.maxBytes = kKernelImageDefaultMaxBytes;
    return resolved;
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

ImageProbe ImageAdapter::ProbeBytes(const uint8_t* bytes, uint32_t byteCount, const ImageSafetyLimits& limits)
{
    ImageSafetyLimits effectiveLimits = resolve_bare_metal_limits(limits);
    ImageProbe probe{};
    probe.status = ImageLoadStatus::UnsupportedFormat;
    probe.width = 0;
    probe.height = 0;

    if (!bytes || byteCount == 0) {
        probe.status = ImageLoadStatus::NotFound;
        return probe;
    }
    if (byteCount > effectiveLimits.maxBytes) {
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!png_header_size(bytes, byteCount, width, height)) {
        probe.status = ImageLoadStatus::UnsupportedFormat;
        return probe;
    }

    probe.width = width;
    probe.height = height;
    if (!dimensions_within_limits(width, height, effectiveLimits)) {
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }
    if (!decoded_image_fits_kernel_heap(width, height)) {
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }

    probe.status = ImageLoadStatus::Ok;
    return probe;
}

ImageProbe ImageAdapter::ProbeFile(const char* path, const ImageSafetyLimits& limits)
{
    ImageSafetyLimits effectiveLimits = resolve_bare_metal_limits(limits);
    ImageProbe probe{};
    probe.status = ImageLoadStatus::NotFound;
    probe.width = 0;
    probe.height = 0;

    if (!path || !path[0]) return probe;
    if (!ends_with_png(path)) {
        probe.status = ImageLoadStatus::UnsupportedFormat;
        return probe;
    }

#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] probe file start path=");
    serial::puts(path);
    serial::puts("\n");
#endif
    kernel::vfs::FileInfo info{};
    if (kernel::vfs::stat(path, &info) != kernel::vfs::VFS_OK) {
        probe.status = ImageLoadStatus::NotFound;
        return probe;
    }
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] probe file stat sizeBytes=");
    serial::put_hex32(info.size);
    serial::puts("\n");
#endif
    if (info.size > effectiveLimits.maxBytes) {
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }

    uint8_t header[32];
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] probe file read header start\n");
#endif
    int32_t read = kernel::vfs::read_file(path, header, sizeof(header));
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] probe file read header done bytes=");
    serial::put_hex32(read < 0 ? 0u : (uint32_t)read);
    serial::puts("\n");
#endif
    if (read < 24) {
        probe.status = read < 0 ? ImageLoadStatus::NotFound : ImageLoadStatus::DecodeFailed;
        return probe;
    }

    return ProbeBytes(header, (uint32_t)read, effectiveLimits);
}

ImageBitmap ImageAdapter::LoadFromFile(const char* path, const ImageSafetyLimits& limits)
{
    ImageSafetyLimits effectiveLimits = resolve_bare_metal_limits(limits);
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] adapter load file entry path=");
    serial::puts(path ? path : "(null)");
    serial::puts("\n");
#endif
    ImageBitmap bitmap{};
    bitmap.status = ImageLoadStatus::NotFound;
    bitmap.pixels = nullptr;
    bitmap.width = 0;
    bitmap.height = 0;

    if (!path || !path[0]) {
        return bitmap;
    }
    if (!ends_with_png(path)) {
        bitmap.status = ImageLoadStatus::UnsupportedFormat;
        return bitmap;
    }

    kernel::vfs::FileInfo info{};
    if (kernel::vfs::stat(path, &info) != kernel::vfs::VFS_OK) {
        bitmap.status = ImageLoadStatus::NotFound;
        return bitmap;
    }
    if (info.size > effectiveLimits.maxBytes || info.size > kKernelImageFileScratchBytes) {
        bitmap.status = ImageLoadStatus::TooLarge;
        return bitmap;
    }
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] adapter file read start sizeBytes=");
    serial::put_hex32(info.size);
    serial::puts("\n");
#endif
    int32_t read = kernel::vfs::read_file(path, s_kernelImageFileScratch, (uint32_t)info.size);
    if (read < 0 || (uint32_t)read != info.size) {
        bitmap.status = ImageLoadStatus::DecodeFailed;
        return bitmap;
    }
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] adapter decode start byteCount=");
    serial::put_hex32((uint32_t)read);
    serial::puts("\n");
#endif
    return LoadFromBytes(s_kernelImageFileScratch, (uint32_t)read, effectiveLimits);
}

ImageBitmap ImageAdapter::LoadFromBytes(const uint8_t* bytes, uint32_t byteCount, const ImageSafetyLimits& limits)
{
    ImageSafetyLimits effectiveLimits = resolve_bare_metal_limits(limits);
    ImageProbe probe = ProbeBytes(bytes, byteCount, effectiveLimits);
    ImageBitmap bitmap{};
    bitmap.status = probe.status;
    bitmap.pixels = nullptr;
    bitmap.width = probe.width;
    bitmap.height = probe.height;
    if (probe.status != ImageLoadStatus::Ok) return bitmap;
    if (byteCount > 0x7FFFFFFFu) {
        bitmap.status = ImageLoadStatus::TooLarge;
        return bitmap;
    }

#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] adapter stbi begin byteCount=");
    serial::put_hex32(byteCount);
    serial::puts(" dims=");
    serial::put_hex32(bitmap.width);
    serial::putc('x');
    serial::put_hex32(bitmap.height);
    serial::puts("\n");
#endif
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes, (int)byteCount, &width, &height, &sourceChannels, 4);
    if (!decoded || width <= 0 || height <= 0) {
        bitmap.status = ImageLoadStatus::DecodeFailed;
        if (decoded) stbi_image_free(decoded);
        return bitmap;
    }

    if (!dimensions_within_limits((uint32_t)width, (uint32_t)height, effectiveLimits)) {
        bitmap.status = ImageLoadStatus::TooLarge;
        stbi_image_free(decoded);
        return bitmap;
    }

    uint64_t pixelCount64 = (uint64_t)width * (uint64_t)height;
    if (pixelCount64 > effectiveLimits.maxPixels) {
        bitmap.status = ImageLoadStatus::TooLarge;
        stbi_image_free(decoded);
        return bitmap;
    }
    uint64_t maxAllocPixels = (uint64_t)(static_cast<size_t>(-1) / sizeof(uint32_t));
    if (pixelCount64 > maxAllocPixels) {
        bitmap.status = ImageLoadStatus::TooLarge;
        stbi_image_free(decoded);
        return bitmap;
    }
    uint32_t* pixels = new uint32_t[(uint32_t)pixelCount64];
    if (!pixels) {
        bitmap.status = ImageLoadStatus::OutOfMemory;
        stbi_image_free(decoded);
        return bitmap;
    }

    for (uint32_t i = 0; i < (uint32_t)pixelCount64; ++i) {
        const uint8_t* src = decoded + i * 4u;
        pixels[i] = ((uint32_t)src[3] << 24) |
            ((uint32_t)src[0] << 16) |
            ((uint32_t)src[1] << 8) |
            (uint32_t)src[2];
    }
    stbi_image_free(decoded);

    bitmap.status = ImageLoadStatus::Ok;
    bitmap.pixels = pixels;
    bitmap.width = (uint32_t)width;
    bitmap.height = (uint32_t)height;
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] adapter stbi end status=Loaded\n");
#endif
    return bitmap;
}

bool ImageAdapter::DrawToFramebuffer(const ImageBitmap& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (image.status != ImageLoadStatus::Ok || !image.pixels || image.width == 0 || image.height == 0 || width == 0 || height == 0) {
        return false;
    }

    uint32_t screenW = kernel::framebuffer::get_width();
    uint32_t screenH = kernel::framebuffer::get_height();
    if (x >= screenW || y >= screenH) {
        return false;
    }
    if (x + width > screenW) width = screenW - x;
    if (y + height > screenH) height = screenH - y;

    if (width == 0 || height == 0) {
        return false;
    }

    if (kernel::framebuffer::get_bpp() == 32) {
        uint32_t* target = kernel::framebuffer::is_double_buffered()
            ? kernel::framebuffer::get_back_buffer()
            : kernel::framebuffer::get_buffer();
        if (!target) return false;

        const uint32_t stride = kernel::framebuffer::is_double_buffered()
            ? kernel::framebuffer::get_width()
            : (kernel::framebuffer::get_pitch() / 4);

        for (uint32_t dy = 0; dy < height; ++dy) {
            uint32_t sy = static_cast<uint32_t>((static_cast<uint64_t>(dy) * image.height) / height);
            uint32_t* dstRow = target + static_cast<uint64_t>(y + dy) * stride + x;
            const uint32_t* srcRow = image.pixels + static_cast<uint64_t>(sy) * image.width;

            for (uint32_t dx = 0; dx < width; ++dx) {
                uint32_t sx = static_cast<uint32_t>((static_cast<uint64_t>(dx) * image.width) / width);
                uint32_t src = srcRow[sx];
                uint8_t a = static_cast<uint8_t>(src >> 24);
                if (a == 0) continue;
                if (a == 255) {
                    dstRow[dx] = src;
                } else {
                    uint32_t dst = dstRow[dx];
                    uint8_t sr = static_cast<uint8_t>((src >> 16) & 0xFF);
                    uint8_t sg = static_cast<uint8_t>((src >> 8) & 0xFF);
                    uint8_t sb = static_cast<uint8_t>(src & 0xFF);
                    uint8_t dr = static_cast<uint8_t>((dst >> 16) & 0xFF);
                    uint8_t dg = static_cast<uint8_t>((dst >> 8) & 0xFF);
                    uint8_t db = static_cast<uint8_t>(dst & 0xFF);
                    uint8_t or_ = static_cast<uint8_t>((sr * a + dr * (255 - a)) / 255);
                    uint8_t og = static_cast<uint8_t>((sg * a + dg * (255 - a)) / 255);
                    uint8_t ob = static_cast<uint8_t>((sb * a + db * (255 - a)) / 255);
                    dstRow[dx] = 0xFF000000u |
                        (static_cast<uint32_t>(or_) << 16) |
                        (static_cast<uint32_t>(og) << 8) |
                        static_cast<uint32_t>(ob);
                }
            }
        }
        return true;
    }

    // TODO: replace nearest-neighbor scaling with a higher-quality kernel-safe scaler.
    for (uint32_t dy = 0; dy < height; ++dy) {
        uint32_t sy = ((uint64_t)dy * image.height) / height;
        for (uint32_t dx = 0; dx < width; ++dx) {
            uint32_t sx = ((uint64_t)dx * image.width) / width;
            kernel::framebuffer::put_pixel(x + dx, y + dy, image.pixels[sy * image.width + sx]);
        }
    }
    return true;
}

} // namespace gui
} // namespace gxos
