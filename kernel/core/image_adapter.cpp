#include "include/kernel/image_adapter.h"

#include "include/kernel/framebuffer.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"
#include "../../jpeg_loader.h"

namespace serial = kernel::serial;

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STB_IMAGE_STATIC
#define STBI_ASSERT(x) do { (void)sizeof(x); } while (0)
static void* gxos_kernel_stbi_malloc(size_t size)
{
    size_t total = size + sizeof(size_t);
    uint8_t* raw = new uint8_t[total];
    if (!raw) return nullptr;
    *reinterpret_cast<size_t*>(raw) = size;
    return raw + sizeof(size_t);
}

static void gxos_kernel_stbi_free(void* ptr);

static void* gxos_kernel_stbi_realloc_sized(void* ptr, size_t oldSize, size_t newSize)
{
    if (!ptr) return gxos_kernel_stbi_malloc(newSize);
    void* newPtr = gxos_kernel_stbi_malloc(newSize);
    if (!newPtr) return nullptr;
    size_t copyBytes = oldSize < newSize ? oldSize : newSize;
    uint8_t* dst = static_cast<uint8_t*>(newPtr);
    const uint8_t* src = static_cast<const uint8_t*>(ptr);
    for (size_t i = 0; i < copyBytes; ++i) dst[i] = src[i];
    gxos_kernel_stbi_free(ptr);
    return newPtr;
}

static void* gxos_kernel_stbi_realloc(void* ptr, size_t newSize)
{
    if (!ptr) return gxos_kernel_stbi_malloc(newSize);
    uint8_t* raw = static_cast<uint8_t*>(ptr) - sizeof(size_t);
    size_t oldSize = *reinterpret_cast<size_t*>(raw);
    return gxos_kernel_stbi_realloc_sized(ptr, oldSize, newSize);
}

static void gxos_kernel_stbi_free(void* ptr)
{
    if (!ptr) return;
    // STBI hands back the original pointer from gxos_kernel_stbi_malloc().
    // Delete the array header pointer so the kernel free-list allocator can
    // reclaim the decoded bitmap when the viewer closes or reloads.
    uint8_t* raw = static_cast<uint8_t*>(ptr) - sizeof(size_t);
    delete[] raw;
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

// Keep the kernel-side preview path bounded, but large enough for the
// wallpaper pack. All current background PNGs stay under 4 MB encoded.
static const uint32_t kKernelImageDefaultMaxBytes = 4u * 1024u * 1024u;
static const uint32_t kKernelImageFileScratchBytes = kKernelImageDefaultMaxBytes;
static const uint64_t kKernelImageHeapSlackBytes = 64u * 1024u;
static uint8_t s_kernelImageFileScratch[kKernelImageFileScratchBytes];

#if defined(GXOS_BARE_METAL)
extern "C" size_t gxos_kernel_heap_free_bytes();
extern "C" size_t gxos_kernel_heap_largest_free_bytes();
extern "C" size_t gxos_kernel_heap_total_bytes();
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

static bool ends_with_jpeg(const char* path)
{
    if (!path) return false;
    uint32_t len = 0;
    while (path[len]) ++len;
    if (len >= 5) {
        const char* ext = path + len - 5;
        if (ext[0] == '.' &&
            (ext[1] == 'j' || ext[1] == 'J') &&
            (ext[2] == 'p' || ext[2] == 'P') &&
            (ext[3] == 'e' || ext[3] == 'E') &&
            (ext[4] == 'g' || ext[4] == 'G')) return true;
    }
    if (len < 4) return false;
    const char* ext = path + len - 4;
    return ext[0] == '.' &&
           (ext[1] == 'j' || ext[1] == 'J') &&
           (ext[2] == 'p' || ext[2] == 'P') &&
           (ext[3] == 'g' || ext[3] == 'G');
}

static bool ends_with_supported_image(const char* path)
{
    return ends_with_png(path) || ends_with_jpeg(path);
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
    if (pixels == 0 || pixels > limits.maxPixels || pixels > (uint64_t)(~0ULL / sizeof(uint32_t))) return false;
    return pixels * sizeof(uint32_t) <= limits.maxDecodedBytes;
}

static bool estimate_image_memory(uint32_t width, uint32_t height, uint64_t& decodedBytes, uint64_t& requiredBytes)
{
    decodedBytes = 0;
    requiredBytes = 0;
    if (width == 0 || height == 0) return false;

    const uint64_t pixels = (uint64_t)width * (uint64_t)height;
    if (pixels == 0 || pixels > (uint64_t)(~0ULL / sizeof(uint32_t))) {
        return false;
    }

    decodedBytes = pixels * sizeof(uint32_t);
    if (decodedBytes > 0xFFFFFFFFULL) return false;
    if (decodedBytes > (uint64_t)(~0ULL - kKernelImageHeapSlackBytes)) {
        return false;
    }

    requiredBytes = decodedBytes + kKernelImageHeapSlackBytes;
    return true;
}

static void log_image_rejection(const char* path, ImageLoadStatus status, uint64_t encodedBytes, uint32_t width, uint32_t height, uint64_t requiredBytes, uint64_t heapFreeBytes, uint64_t heapLargestFreeBytes, uint64_t heapTotalBytes)
{
#if defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] reject path=");
    serial::puts(path ? path : "(none)");
    serial::puts(" status=");
    serial::puts(ImageLoadStatusName(status));
    serial::puts(" encodedBytes=0x");
    serial::put_hex64(encodedBytes);
    serial::puts(" dims=0x");
    serial::put_hex32(width);
    serial::putc('x');
    serial::put_hex32(height);
    serial::puts(" requiredBytes=0x");
    serial::put_hex64(requiredBytes);
    serial::puts(" heapFree=0x");
    serial::put_hex64(heapFreeBytes);
    serial::puts(" heapLargestFree=0x");
    serial::put_hex64(heapLargestFreeBytes);
    serial::puts(" heapTotal=0x");
    serial::put_hex64(heapTotalBytes);
    serial::puts("\n");
#else
    (void)path;
    (void)status;
    (void)encodedBytes;
    (void)width;
    (void)height;
    (void)requiredBytes;
    (void)heapFreeBytes;
    (void)heapLargestFreeBytes;
    (void)heapTotalBytes;
#endif
}

static ImageSafetyLimits resolve_bare_metal_limits(const ImageSafetyLimits& limits)
{
    return limits;
}

enum class KernelImageKind : uint8_t {
    Png = 0,
    Jpeg,
};

static ImageProbe probe_bytes_impl(const uint8_t* bytes, uint32_t byteCount, const ImageSafetyLimits& limits, const char* path)
{
    ImageProbe probe{};
    probe.status = ImageLoadStatus::UnsupportedFormat;
    probe.width = 0;
    probe.height = 0;

    if (!bytes || byteCount == 0) {
        probe.status = ImageLoadStatus::NotFound;
        return probe;
    }

    if (byteCount > limits.maxBytes || byteCount > kKernelImageFileScratchBytes) {
#if defined(GXOS_BARE_METAL)
        log_image_rejection(path, ImageLoadStatus::TooLarge, byteCount, 0, 0, 0, gxos_kernel_heap_free_bytes(), gxos_kernel_heap_largest_free_bytes(), gxos_kernel_heap_total_bytes());
#endif
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    KernelImageKind kind = KernelImageKind::Png;
    JpegHeaderInfo jpegInfo{};
    if (png_header_size(bytes, byteCount, width, height)) {
        kind = KernelImageKind::Png;
    } else if (IsJpegSignature(bytes, byteCount)) {
        kind = KernelImageKind::Jpeg;
        const JpegProbeStatus jpegProbe = InspectJpeg(bytes, byteCount, jpegInfo);
        if (jpegProbe == JpegProbeStatus::Malformed) {
            probe.status = ImageLoadStatus::DecodeFailed;
            return probe;
        }
        width = jpegInfo.width;
        height = jpegInfo.height;
        if (jpegProbe == JpegProbeStatus::Unsupported) {
            probe.width = width;
            probe.height = height;
            probe.status = ImageLoadStatus::UnsupportedFormat;
            return probe;
        }
        if (jpegProbe != JpegProbeStatus::Valid) {
            probe.status = ImageLoadStatus::UnsupportedFormat;
            return probe;
        }
    } else {
        probe.status = ImageLoadStatus::UnsupportedFormat;
        return probe;
    }

    probe.width = width;
    probe.height = height;
    if (!dimensions_within_limits(width, height, limits)) {
#if defined(GXOS_BARE_METAL)
        uint64_t decodedBytes = 0;
        uint64_t requiredBytes = 0;
        const bool estimated = kind == KernelImageKind::Jpeg
            ? EstimateJpegAllocation(jpegInfo, limits, requiredBytes, decodedBytes)
            : estimate_image_memory(width, height, decodedBytes, requiredBytes);
        if (!estimated) {
            requiredBytes = 0;
        }
        log_image_rejection(path, ImageLoadStatus::TooLarge, byteCount, width, height, requiredBytes, gxos_kernel_heap_free_bytes(), gxos_kernel_heap_largest_free_bytes(), gxos_kernel_heap_total_bytes());
#endif
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }

#if defined(GXOS_BARE_METAL)
    uint64_t decodedBytes = 0;
    uint64_t requiredBytes = 0;
    bool estimated = false;
    if (kind == KernelImageKind::Jpeg) {
        estimated = EstimateJpegAllocation(jpegInfo, limits, requiredBytes, decodedBytes);
    } else {
        estimated = estimate_image_memory(width, height, decodedBytes, requiredBytes);
    }
    if (!estimated) {
        log_image_rejection(path, ImageLoadStatus::TooLarge, byteCount, width, height, 0, gxos_kernel_heap_free_bytes(), gxos_kernel_heap_largest_free_bytes(), gxos_kernel_heap_total_bytes());
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }

    const uint64_t heapFree = gxos_kernel_heap_free_bytes();
    const uint64_t heapLargestFree = gxos_kernel_heap_largest_free_bytes();
    if (requiredBytes > heapLargestFree) {
        log_image_rejection(path, ImageLoadStatus::TooLarge, byteCount, width, height, requiredBytes, heapFree, heapLargestFree, gxos_kernel_heap_total_bytes());
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }
#endif

    probe.status = ImageLoadStatus::Ok;
    return probe;
}

static ImageBitmap load_bytes_impl(const uint8_t* bytes, uint32_t byteCount, const ImageSafetyLimits& limits, const char* path)
{
    ImageProbe probe = probe_bytes_impl(bytes, byteCount, limits, path);
    ImageBitmap bitmap{};
    bitmap.status = probe.status;
    bitmap.pixels = nullptr;
    bitmap.width = probe.width;
    bitmap.height = probe.height;
    bitmap.format = ImageFormat::Unknown;
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
    // JPEG uses its own STBI_ONLY_JPEG translation unit so the existing PNG
    // decoder remains format-isolated and its allocation/release path stays
    // unchanged.
    if (IsJpegSignature(bytes, byteCount)) {
        JpegDecodedBuffer jpeg = DecodeJpegRgba(bytes, byteCount, limits);
        if (jpeg.status != JpegDecodeStatus::Ok || !jpeg.pixels) {
            if (jpeg.status == JpegDecodeStatus::TooLarge)
                bitmap.status = ImageLoadStatus::TooLarge;
            else if (jpeg.status == JpegDecodeStatus::Unsupported)
                bitmap.status = ImageLoadStatus::UnsupportedFormat;
            else if (jpeg.status == JpegDecodeStatus::OutOfMemory)
                bitmap.status = ImageLoadStatus::OutOfMemory;
            else
                bitmap.status = ImageLoadStatus::DecodeFailed;
#if defined(GXOS_BARE_METAL)
            uint64_t decodedBytes = 0;
            uint64_t requiredBytes = 0;
            JpegHeaderInfo jpegInfo{};
            if (InspectJpeg(bytes, byteCount, jpegInfo) == JpegProbeStatus::Valid)
                EstimateJpegAllocation(jpegInfo, limits, requiredBytes, decodedBytes);
            log_image_rejection(path, bitmap.status, byteCount, bitmap.width, bitmap.height, requiredBytes,
                gxos_kernel_heap_free_bytes(), gxos_kernel_heap_largest_free_bytes(), gxos_kernel_heap_total_bytes());
#endif
            return bitmap;
        }

        uint8_t* pixelBytes = const_cast<uint8_t*>(jpeg.pixels);
        const uint64_t totalPixels = static_cast<uint64_t>(jpeg.width) * static_cast<uint64_t>(jpeg.height);
        for (uint64_t i = 0; i < totalPixels; ++i) {
            uint8_t* px = pixelBytes + i * 4u;
            uint8_t tmp = px[0];
            px[0] = px[2];
            px[2] = tmp;
        }
        bitmap.status = ImageLoadStatus::Ok;
        bitmap.pixels = reinterpret_cast<const uint32_t*>(jpeg.pixels);
        bitmap.width = jpeg.width;
        bitmap.height = jpeg.height;
        bitmap.format = ImageFormat::Jpeg;
        return bitmap;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes, (int)byteCount, &width, &height, &sourceChannels, 4);
    if (!decoded || width <= 0 || height <= 0) {
        bitmap.status = ImageLoadStatus::OutOfMemory;
        if (decoded) stbi_image_free(decoded);
#if defined(GXOS_BARE_METAL)
        uint64_t decodedBytes = 0;
        uint64_t requiredBytes = 0;
        if (!estimate_image_memory(probe.width, probe.height, decodedBytes, requiredBytes)) {
            requiredBytes = 0;
        }
        log_image_rejection(path, bitmap.status, byteCount, probe.width, probe.height, requiredBytes, gxos_kernel_heap_free_bytes(), gxos_kernel_heap_largest_free_bytes(), gxos_kernel_heap_total_bytes());
#endif
        return bitmap;
    }

    if (!dimensions_within_limits((uint32_t)width, (uint32_t)height, limits)) {
        bitmap.status = ImageLoadStatus::TooLarge;
        stbi_image_free(decoded);
#if defined(GXOS_BARE_METAL)
        uint64_t decodedBytes = 0;
        uint64_t requiredBytes = 0;
        if (estimate_image_memory((uint32_t)width, (uint32_t)height, decodedBytes, requiredBytes)) {
            log_image_rejection(path, bitmap.status, byteCount, (uint32_t)width, (uint32_t)height, requiredBytes, gxos_kernel_heap_free_bytes(), gxos_kernel_heap_largest_free_bytes(), gxos_kernel_heap_total_bytes());
        }
#endif
        return bitmap;
    }

    uint64_t pixelCount64 = (uint64_t)width * (uint64_t)height;
    if (pixelCount64 > (uint64_t)(~0ULL / sizeof(uint32_t))) {
        bitmap.status = ImageLoadStatus::TooLarge;
        stbi_image_free(decoded);
        return bitmap;
    }

    // Reuse STBI's decoded RGBA buffer as the final image storage. Swapping the
    // red and blue bytes in place avoids allocating and copying a second full
    // pixel buffer on bare metal.
    uint8_t* pixelBytes = reinterpret_cast<uint8_t*>(decoded);
    const uint64_t totalPixels = pixelCount64;
    for (uint64_t i = 0; i < totalPixels; ++i) {
        uint8_t* px = pixelBytes + i * 4u;
        uint8_t tmp = px[0];
        px[0] = px[2];
        px[2] = tmp;
    }

    bitmap.status = ImageLoadStatus::Ok;
    bitmap.pixels = reinterpret_cast<const uint32_t*>(decoded);
    bitmap.width = (uint32_t)width;
    bitmap.height = (uint32_t)height;
    bitmap.format = ImageFormat::Png;
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] adapter stbi end status=Loaded\n");
#endif
    return bitmap;
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

void ImageAdapter::Release(ImageBitmap& bitmap)
{
#if defined(GXOS_BARE_METAL)
    if (bitmap.status == ImageLoadStatus::Ok && bitmap.pixels) {
        if (bitmap.format == ImageFormat::Jpeg)
            ReleaseJpegBuffer(reinterpret_cast<const uint8_t*>(bitmap.pixels));
        else
            stbi_image_free(const_cast<uint32_t*>(bitmap.pixels));
    }
    bitmap.status = ImageLoadStatus::NotFound;
    bitmap.pixels = nullptr;
    bitmap.width = 0;
    bitmap.height = 0;
    bitmap.format = ImageFormat::Unknown;
#else
    bitmap.image.reset();
    bitmap.status = ImageLoadStatus::NotFound;
    bitmap.width = 0;
    bitmap.height = 0;
    bitmap.format = ImageFormat::Unknown;
    bitmap.source.clear();
#endif
}

ImageProbe ImageAdapter::ProbeBytes(const uint8_t* bytes, uint32_t byteCount, const ImageSafetyLimits& limits)
{
    ImageSafetyLimits effectiveLimits = resolve_bare_metal_limits(limits);
    return probe_bytes_impl(bytes, byteCount, effectiveLimits, nullptr);
}

ImageProbe ImageAdapter::ProbeFile(const char* path, const ImageSafetyLimits& limits)
{
    ImageSafetyLimits effectiveLimits = resolve_bare_metal_limits(limits);
    ImageProbe probe{};
    probe.status = ImageLoadStatus::NotFound;
    probe.width = 0;
    probe.height = 0;

    if (!path || !path[0]) return probe;
    if (!ends_with_supported_image(path)) {
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
        log_image_rejection(path, ImageLoadStatus::TooLarge, info.size, 0, 0, 0, gxos_kernel_heap_free_bytes(), gxos_kernel_heap_largest_free_bytes(), gxos_kernel_heap_total_bytes());
        probe.status = ImageLoadStatus::TooLarge;
        return probe;
    }

    static uint8_t header[16u * 1024u];
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] probe file read header start\n");
#endif
    // JPEG dimension inspection validates the complete marker stream through
    // EOI.  Read a bounded JPEG into the same 4 MiB scratch storage used by
    // LoadFromFile so larger-than-header JPEGs do not fail before decoding.
    const bool jpegPath = ends_with_jpeg(path);
    uint8_t* probeBuffer = jpegPath ? s_kernelImageFileScratch : header;
    uint32_t headerBytes = jpegPath
        ? info.size
        : (info.size < sizeof(header) ? info.size : static_cast<uint32_t>(sizeof(header)));
    int32_t read = kernel::vfs::read_file(path, probeBuffer, headerBytes);
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] probe file read header done bytes=");
    serial::put_hex32(read < 0 ? 0u : (uint32_t)read);
    serial::puts("\n");
#endif
    if (read <= 0) {
        probe.status = read < 0 ? ImageLoadStatus::NotFound : ImageLoadStatus::DecodeFailed;
        return probe;
    }

    return probe_bytes_impl(probeBuffer, (uint32_t)read, effectiveLimits, path);
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
    if (!ends_with_supported_image(path)) {
        bitmap.status = ImageLoadStatus::UnsupportedFormat;
        return bitmap;
    }

    kernel::vfs::FileInfo info{};
    if (kernel::vfs::stat(path, &info) != kernel::vfs::VFS_OK) {
        bitmap.status = ImageLoadStatus::NotFound;
        return bitmap;
    }
    if (info.size > effectiveLimits.maxBytes || info.size > kKernelImageFileScratchBytes) {
        log_image_rejection(path, ImageLoadStatus::TooLarge, info.size, 0, 0, 0, gxos_kernel_heap_free_bytes(), gxos_kernel_heap_largest_free_bytes(), gxos_kernel_heap_total_bytes());
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
    return load_bytes_impl(s_kernelImageFileScratch, (uint32_t)read, effectiveLimits, path);
}

ImageBitmap ImageAdapter::LoadFromBytes(const uint8_t* bytes, uint32_t byteCount, const ImageSafetyLimits& limits)
{
    ImageSafetyLimits effectiveLimits = resolve_bare_metal_limits(limits);
    return load_bytes_impl(bytes, byteCount, effectiveLimits, nullptr);
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
