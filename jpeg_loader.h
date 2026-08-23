#pragma once

#include "kernel/core/include/kernel/image_adapter.h"

#if !defined(GXOS_BARE_METAL)
#include <cstddef>
#include <cstdint>
#endif

namespace gxos {
namespace gui {

enum class JpegProbeStatus : uint8_t {
    NotJpeg = 0,
    Malformed,
    Unsupported,
    Valid,
};

struct JpegHeaderInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t components = 0;
    uint8_t maxHorizontalSampling = 0;
    uint8_t maxVerticalSampling = 0;
    bool progressive = false;
};

enum class JpegDecodeStatus : uint8_t {
    Ok = 0,
    NotJpeg,
    Malformed,
    Unsupported,
    TooLarge,
    OutOfMemory,
};

struct JpegDecodedBuffer {
    JpegDecodeStatus status = JpegDecodeStatus::Malformed;
    const uint8_t* pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t pixelBytes = 0;
    uint64_t peakAllocationBytes = 0;
};

bool IsJpegSignature(const uint8_t* bytes, size_t byteCount);
JpegProbeStatus InspectJpeg(const uint8_t* bytes, size_t byteCount, JpegHeaderInfo& info);

// Returns the bounded total live stb allocation budget, including the final
// RGBA output and the decoder's component/coefficient workspace.  It is a
// safety estimate, not a promise that the image will fit the kernel heap.
bool EstimateJpegAllocation(const JpegHeaderInfo& info,
                            const ImageSafetyLimits& limits,
                            uint64_t& requiredBytes,
                            uint64_t& decodedPixelBytes);

JpegDecodedBuffer DecodeJpegRgba(const uint8_t* bytes, size_t byteCount,
                                 const ImageSafetyLimits& limits);
void ReleaseJpegBuffer(const uint8_t* pixels);

// Test-only deterministic fault injection. UINT32_MAX disables injection;
// zero denies the first decoder allocation, and larger values deny the
// corresponding allocation attempt. Production callers never need this.
void SetJpegAllocationFailureInjection(uint32_t failAfterAllocations);

} // namespace gui
} // namespace gxos
