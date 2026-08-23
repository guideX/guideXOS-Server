#include "jpeg_loader.h"

#if !defined(GXOS_BARE_METAL)
#include <cstddef>
#include <cstdint>
#endif

#if !defined(GXOS_BARE_METAL)
#include <new>
#endif

// Keep the JPEG translation unit format-specific.  The PNG translation units
// continue to use STBI_ONLY_PNG, so enabling this path does not silently bring
// BMP/GIF/WebP-like formats into Navigator.
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_STATIC
#define STBI_ASSERT(x) do { (void)sizeof(x); } while (0)

namespace {

struct JpegAllocationHeader {
    size_t size;
};

struct JpegAllocationContext {
    size_t activeBytes;
    size_t limitBytes;
    size_t peakBytes;
    uint32_t allocationAttempts;
    uint32_t failAfterAllocations;
    bool allocationDenied;
};

static JpegAllocationContext* g_jpegAllocationContext = nullptr;
static uint32_t g_jpegFailAfterAllocations = 0xFFFFFFFFu;

static bool addWouldOverflow(size_t left, size_t right)
{
    return left > static_cast<size_t>(-1) - right;
}

static uint8_t* allocateRaw(size_t payloadBytes)
{
    if (g_jpegAllocationContext) {
        const uint32_t attempt = g_jpegAllocationContext->allocationAttempts++;
        if (g_jpegAllocationContext->failAfterAllocations != 0xFFFFFFFFu &&
            attempt >= g_jpegAllocationContext->failAfterAllocations) {
            g_jpegAllocationContext->allocationDenied = true;
            return nullptr;
        }
    }
    const size_t actualPayloadBytes = payloadBytes == 0 ? 1 : payloadBytes;
    if (addWouldOverflow(sizeof(JpegAllocationHeader), actualPayloadBytes)) {
        if (g_jpegAllocationContext) g_jpegAllocationContext->allocationDenied = true;
        return nullptr;
    }

    const size_t totalBytes = sizeof(JpegAllocationHeader) + actualPayloadBytes;
    if (g_jpegAllocationContext) {
        if (g_jpegAllocationContext->activeBytes > g_jpegAllocationContext->limitBytes) {
            g_jpegAllocationContext->allocationDenied = true;
            return nullptr;
        }
        if (payloadBytes > g_jpegAllocationContext->limitBytes - g_jpegAllocationContext->activeBytes) {
            g_jpegAllocationContext->allocationDenied = true;
            return nullptr;
        }
    }

#if defined(GXOS_BARE_METAL)
    uint8_t* raw = new uint8_t[totalBytes];
#else
    uint8_t* raw = new (std::nothrow) uint8_t[totalBytes];
#endif
    if (!raw) {
        if (g_jpegAllocationContext) g_jpegAllocationContext->allocationDenied = true;
        return nullptr;
    }

    JpegAllocationHeader* header = reinterpret_cast<JpegAllocationHeader*>(raw);
    header->size = payloadBytes;
    if (g_jpegAllocationContext) {
        g_jpegAllocationContext->activeBytes += payloadBytes;
        if (g_jpegAllocationContext->activeBytes > g_jpegAllocationContext->peakBytes)
            g_jpegAllocationContext->peakBytes = g_jpegAllocationContext->activeBytes;
    }
    return raw;
}

static void freeRaw(void* ptr)
{
    if (!ptr) return;
    uint8_t* raw = static_cast<uint8_t*>(ptr);
    JpegAllocationHeader* header = reinterpret_cast<JpegAllocationHeader*>(raw);
    const size_t payloadBytes = header->size;
    if (g_jpegAllocationContext) {
        if (g_jpegAllocationContext->activeBytes >= payloadBytes)
            g_jpegAllocationContext->activeBytes -= payloadBytes;
        else
            g_jpegAllocationContext->activeBytes = 0;
    }
    delete[] raw;
}

static void* gxos_jpeg_malloc(size_t size)
{
    uint8_t* raw = allocateRaw(size);
    return raw ? raw + sizeof(JpegAllocationHeader) : nullptr;
}

static void gxos_jpeg_free(void* ptr)
{
    if (!ptr) return;
    freeRaw(static_cast<uint8_t*>(ptr) - sizeof(JpegAllocationHeader));
}

static void* gxos_jpeg_realloc_sized(void* ptr, size_t oldSize, size_t newSize)
{
    if (!ptr) return gxos_jpeg_malloc(newSize);

    uint8_t* oldRaw = static_cast<uint8_t*>(ptr) - sizeof(JpegAllocationHeader);
    JpegAllocationHeader* oldHeader = reinterpret_cast<JpegAllocationHeader*>(oldRaw);
    const size_t actualOldSize = oldHeader->size;
    size_t activeWithoutOld = 0;
    if (g_jpegAllocationContext) {
        activeWithoutOld = g_jpegAllocationContext->activeBytes >= actualOldSize
            ? g_jpegAllocationContext->activeBytes - actualOldSize : 0;
        if (activeWithoutOld > g_jpegAllocationContext->limitBytes ||
            newSize > g_jpegAllocationContext->limitBytes - activeWithoutOld) {
            g_jpegAllocationContext->allocationDenied = true;
            return nullptr;
        }
        if (activeWithoutOld <= g_jpegAllocationContext->limitBytes - newSize &&
            activeWithoutOld + newSize > g_jpegAllocationContext->peakBytes) {
            g_jpegAllocationContext->peakBytes = activeWithoutOld + newSize;
        }
    }

    if (g_jpegAllocationContext) g_jpegAllocationContext->activeBytes = activeWithoutOld;
    uint8_t* newRaw = allocateRaw(newSize);
    if (!newRaw) {
        if (g_jpegAllocationContext) g_jpegAllocationContext->activeBytes += actualOldSize;
        return nullptr;
    }
    const size_t copyBytes = actualOldSize < newSize ? actualOldSize : newSize;
    for (size_t i = 0; i < copyBytes; ++i) newRaw[sizeof(JpegAllocationHeader) + i] = oldRaw[sizeof(JpegAllocationHeader) + i];

    // The old allocation was removed from active accounting before the new
    // allocation was made; the peak still records the true realloc high-water
    // mark if the allocator had to hold both buffers at once.
    (void)oldSize;
    delete[] oldRaw;
    return newRaw + sizeof(JpegAllocationHeader);
}

static void* gxos_jpeg_realloc(void* ptr, size_t newSize)
{
    if (!ptr) return gxos_jpeg_malloc(newSize);
    uint8_t* raw = static_cast<uint8_t*>(ptr) - sizeof(JpegAllocationHeader);
    JpegAllocationHeader* header = reinterpret_cast<JpegAllocationHeader*>(raw);
    return gxos_jpeg_realloc_sized(ptr, header->size, newSize);
}

#define STBI_MALLOC(sz) gxos_jpeg_malloc(sz)
#define STBI_REALLOC(p, sz) gxos_jpeg_realloc(p, sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) gxos_jpeg_realloc_sized(p, oldsz, newsz)
#define STBI_FREE(p) gxos_jpeg_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

static uint16_t readBe16(const uint8_t* bytes)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

static bool isStandaloneMarker(uint8_t marker)
{
    return marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0xD8;
}

static bool isSofMarker(uint8_t marker)
{
    return (marker >= 0xC0 && marker <= 0xC3) ||
           (marker >= 0xC5 && marker <= 0xC7) ||
           (marker >= 0xC9 && marker <= 0xCB) ||
           (marker >= 0xCD && marker <= 0xCF);
}

static bool isSupportedSof(uint8_t marker)
{
    // C0 is baseline sequential, C1 is extended sequential, and C2 is
    // progressive. stb_image v2.30 supports all three at 8 bits/sample.
    return marker == 0xC0 || marker == 0xC1 || marker == 0xC2;
}

} // namespace

namespace gxos {
namespace gui {

bool IsJpegSignature(const uint8_t* bytes, size_t byteCount)
{
    return bytes && byteCount >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
}

JpegProbeStatus InspectJpeg(const uint8_t* bytes, size_t byteCount, JpegHeaderInfo& info)
{
    info = JpegHeaderInfo{};
    if (!IsJpegSignature(bytes, byteCount)) return JpegProbeStatus::NotJpeg;
    if (byteCount < 4) return JpegProbeStatus::Malformed;

    size_t pos = 2;
    bool sawFrame = false;
    bool sawUnsupportedFrame = false;
    while (pos < byteCount) {
        if (bytes[pos++] != 0xFF) return JpegProbeStatus::Malformed;
        while (pos < byteCount && bytes[pos] == 0xFF) ++pos;
        if (pos >= byteCount) return JpegProbeStatus::Malformed;
        const uint8_t marker = bytes[pos++];
        if (marker == 0x00) return JpegProbeStatus::Malformed;

        if (marker == 0xD9) return JpegProbeStatus::Malformed;
        if (marker == 0xDA) {
            if (!sawFrame || pos + 2 > byteCount) return JpegProbeStatus::Malformed;
            const uint16_t segmentLength = readBe16(bytes + pos);
            if (segmentLength < 2 || static_cast<size_t>(segmentLength) > byteCount - pos)
                return JpegProbeStatus::Malformed;
            const size_t entropyStart = pos + static_cast<size_t>(segmentLength);
            bool sawEoi = false;
            for (size_t i = entropyStart; i + 1 < byteCount; ++i) {
                if (bytes[i] == 0xFF && bytes[i + 1] == 0xD9) {
                    sawEoi = true;
                    break;
                }
            }
            if (!sawEoi) return JpegProbeStatus::Malformed;
            return sawUnsupportedFrame ? JpegProbeStatus::Unsupported : JpegProbeStatus::Valid;
        }
        if (isStandaloneMarker(marker)) continue;
        if (pos + 2 > byteCount) return JpegProbeStatus::Malformed;

        const uint16_t segmentLength = readBe16(bytes + pos);
        if (segmentLength < 2 || static_cast<size_t>(segmentLength) > byteCount - pos)
            return JpegProbeStatus::Malformed;
        const size_t segmentData = pos + 2;
        const size_t segmentDataBytes = static_cast<size_t>(segmentLength) - 2;

        if (isSofMarker(marker)) {
            if (sawFrame || segmentDataBytes < 6) return JpegProbeStatus::Malformed;
            const uint8_t precision = bytes[segmentData];
            const uint16_t height = readBe16(bytes + segmentData + 1);
            const uint16_t width = readBe16(bytes + segmentData + 3);
            const uint8_t components = bytes[segmentData + 5];
            if (precision != 8 || width == 0 || height == 0 || components == 0)
                return JpegProbeStatus::Malformed;
            if (segmentDataBytes != static_cast<size_t>(6 + 3u * components))
                return JpegProbeStatus::Malformed;

            uint8_t maxH = 0;
            uint8_t maxV = 0;
            for (uint8_t i = 0; i < components; ++i) {
                const uint8_t sampling = bytes[segmentData + 7 + 3u * i];
                const uint8_t h = static_cast<uint8_t>(sampling >> 4);
                const uint8_t v = static_cast<uint8_t>(sampling & 0x0F);
                if (h == 0 || v == 0) return JpegProbeStatus::Malformed;
                if (h > 4 || v > 4) sawUnsupportedFrame = true;
                if (h > maxH) maxH = h;
                if (v > maxV) maxV = v;
            }

            info.width = width;
            info.height = height;
            info.components = components;
            info.maxHorizontalSampling = maxH;
            info.maxVerticalSampling = maxV;
            info.progressive = marker == 0xC2;
            sawFrame = true;
            if (!isSupportedSof(marker) || (components != 1 && components != 3))
                sawUnsupportedFrame = true;
        }

        // pos currently points at the two-byte segment length.  Advance over
        // both the length field and the validated payload.
        pos += static_cast<size_t>(segmentLength);
    }
    return JpegProbeStatus::Malformed;
}

bool EstimateJpegAllocation(const JpegHeaderInfo& info,
                            const ImageSafetyLimits& limits,
                            uint64_t& requiredBytes,
                            uint64_t& decodedPixelBytes)
{
    requiredBytes = 0;
    decodedPixelBytes = 0;
    if (info.width == 0 || info.height == 0 ||
        (info.components != 1 && info.components != 3) ||
        info.width > limits.maxWidth || info.height > limits.maxHeight)
        return false;

    const uint64_t pixels = static_cast<uint64_t>(info.width) * static_cast<uint64_t>(info.height);
    if (pixels == 0 || pixels > limits.maxPixels || pixels > static_cast<uint64_t>(-1) / 4u)
        return false;
    decodedPixelBytes = pixels * 4u;
    if (decodedPixelBytes > limits.maxDecodedBytes)
        return false;

    // stb_image keeps complete component planes for baseline JPEG and
    // complete coefficient planes for progressive JPEG.  These are generous
    // bounded estimates for the three-component worst case, plus the fixed
    // JPEG state/Huffman tables.  They keep compressed input from bypassing
    // the decoded-pixel memory policy.
    const uint64_t workspaceMultiplier = info.progressive ? 6u : 3u;
    if (pixels > static_cast<uint64_t>(-1) / workspaceMultiplier) return false;
    const uint64_t workspace = pixels * workspaceMultiplier + 256u * 1024u;
    if (decodedPixelBytes > static_cast<uint64_t>(-1) - workspace) return false;
    requiredBytes = decodedPixelBytes + workspace;
    return true;
}

JpegDecodedBuffer DecodeJpegRgba(const uint8_t* bytes, size_t byteCount,
                                 const ImageSafetyLimits& limits)
{
    JpegDecodedBuffer result;
    if (!bytes || byteCount == 0) {
        result.status = JpegDecodeStatus::NotJpeg;
        return result;
    }
    if (byteCount > limits.maxBytes) {
        result.status = JpegDecodeStatus::TooLarge;
        return result;
    }

    JpegHeaderInfo info;
    const JpegProbeStatus probe = InspectJpeg(bytes, byteCount, info);
    if (probe == JpegProbeStatus::NotJpeg) {
        result.status = JpegDecodeStatus::NotJpeg;
        return result;
    }
    if (probe == JpegProbeStatus::Malformed) {
        result.status = JpegDecodeStatus::Malformed;
        return result;
    }
    if (probe == JpegProbeStatus::Unsupported) {
        result.status = JpegDecodeStatus::Unsupported;
        result.width = info.width;
        result.height = info.height;
        return result;
    }

    uint64_t allocationBudget = 0;
    uint64_t decodedPixelBytes = 0;
    if (!EstimateJpegAllocation(info, limits, allocationBudget, decodedPixelBytes)) {
        result.status = JpegDecodeStatus::TooLarge;
        result.width = info.width;
        result.height = info.height;
        return result;
    }
    if (byteCount > static_cast<size_t>(0x7FFFFFFF)) {
        result.status = JpegDecodeStatus::TooLarge;
        return result;
    }

    JpegAllocationContext allocationContext{};
    allocationContext.limitBytes = static_cast<size_t>(allocationBudget);
    allocationContext.failAfterAllocations = g_jpegFailAfterAllocations;
    g_jpegAllocationContext = &allocationContext;
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes, static_cast<int>(byteCount),
                                             &width, &height, &sourceChannels, 4);
    g_jpegAllocationContext = nullptr;

    result.peakAllocationBytes = allocationContext.peakBytes;
    if (!decoded) {
        result.status = allocationContext.allocationDenied
            ? JpegDecodeStatus::OutOfMemory : JpegDecodeStatus::Malformed;
        return result;
    }
    if (width <= 0 || height <= 0 || (sourceChannels != 1 && sourceChannels != 3)) {
        stbi_image_free(decoded);
        result.status = JpegDecodeStatus::Unsupported;
        return result;
    }

    const uint64_t actualPixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (actualPixels == 0 || actualPixels > static_cast<uint64_t>(-1) / 4u) {
        stbi_image_free(decoded);
        result.status = JpegDecodeStatus::TooLarge;
        return result;
    }
    const uint64_t actualBytes = actualPixels * 4u;
    if (actualBytes > limits.maxDecodedBytes ||
        static_cast<uint32_t>(width) > limits.maxWidth ||
        static_cast<uint32_t>(height) > limits.maxHeight ||
        actualPixels > limits.maxPixels) {
        stbi_image_free(decoded);
        result.status = JpegDecodeStatus::TooLarge;
        return result;
    }

    result.status = JpegDecodeStatus::Ok;
    result.pixels = decoded;
    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.pixelBytes = actualBytes;
    return result;
}

void ReleaseJpegBuffer(const uint8_t* pixels)
{
    if (pixels) stbi_image_free(const_cast<uint8_t*>(pixels));
}

void SetJpegAllocationFailureInjection(uint32_t failAfterAllocations)
{
    g_jpegFailAfterAllocations = failAfterAllocations;
}

} // namespace gui
} // namespace gxos
