// Generic bare-metal present_frame validation (MC7).
//
// Root cause it replaces: kernel/core/native_elf_baremetal.cpp
// host_present_frame used to accept exactly one geometry,
//   width == 448 && height == 553 && strideBytes == 1792,
// which is the Pac-Man retained-frame size (pinned in
// tests/native_abi_layout_test.cpp as kPacManFrame*). Anything else --
// including Missile Command's 480x360 -- failed validation before any
// compositor, framebuffer, or staging-buffer code ran.
//
// Classification (audited, not assumed):
//   - NOT an ABI restriction: the present_frame signature already carries
//     (x, y, width, height, strideBytes, pixelFormat, pixels, pixelBytes)
//     and the ABI slot/offset is unchanged by MC7.
//   - NOT a compositor restriction: the hosted compositor
//     (compositor.cpp MT_FramePresent) already validates generically
//     (1..4096 per axis, stride >= width*4, bytes == stride*height,
//     <= 16 MiB) and clips per row against the framebuffer.
//   - NOT a staging-buffer restriction: NativeWindowOwner::present already
//     allocates stride*height bytes generically (<= 16 MiB) with guard
//     words, and ::draw already centers smaller frames and clips larger
//     ones against the window client area.
//   - NOT a framebuffer/stride restriction: stride is bytes-per-row with
//     4-byte pixels; no hardware stride requirement forces 1792.
//   - It was purely a hard-coded ABI-entry assumption in
//     host_present_frame. The 448x553 constant appeared nowhere else on
//     the bare-metal presentation path.
//
// This header is the single source of truth for the generic contract.
// Freestanding (no STL, no exceptions, <stdint.h> only via types.h) so
// both the kernel and host unit tests compile it. Host tests include it
// by quoted relative path (repo convention, cf. app_audio_stream_test).
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_NATIVE_PRESENT_VALIDATION_H
#define KERNEL_NATIVE_PRESENT_VALIDATION_H

#include "types.h"

namespace kernel {
namespace native_present {

// Presentation contract (mirrors the hosted compositor limits in
// compositor.cpp MT_FramePresent, plus the bare-metal retained-buffer
// alignment requirement from NativeWindowOwner::present).
static const int32_t kMinDimension = 1;
static const int32_t kMaxDimension = 4096;
static const uint32_t kBytesPerPixel = 4u;
static const uint32_t kPixelFormatXrgb8888 = 1u;
static const uint64_t kMaxFrameBytes = 16ull * 1024ull * 1024ull;

// Legacy Pac-Man geometry (still accepted; pinned by
// tests/native_abi_layout_test.cpp). Listed here only so tests can
// name the regression case without re-hard-coding it elsewhere.
static const int32_t kLegacyWidth = 448;
static const int32_t kLegacyHeight = 553;
static const uint32_t kLegacyStride = 1792u;
static const uint64_t kLegacyBytes = 990976ull;

// Missile Command geometry (must work; not special-cased in code).
static const int32_t kMissileCommandWidth = 480;
static const int32_t kMissileCommandHeight = 360;

enum class ValidationResult : uint32_t {
    Ok = 0,
    NullPixels = 1,
    InvalidOffset = 2,
    InvalidGeometry = 3,
    InvalidStride = 4,
    InvalidFormat = 5,
    TooLarge = 6,
    ByteCountMismatch = 7,
};

struct ValidationOutcome {
    ValidationResult result;
    uint64_t requiredBytes;
};

inline const char* validation_result_name(ValidationResult result) {
    switch (result) {
        case ValidationResult::Ok: return "Ok";
        case ValidationResult::NullPixels: return "NullPixels";
        case ValidationResult::InvalidOffset: return "InvalidOffset";
        case ValidationResult::InvalidGeometry: return "InvalidGeometry";
        case ValidationResult::InvalidStride: return "InvalidStride";
        case ValidationResult::InvalidFormat: return "InvalidFormat";
        case ValidationResult::TooLarge: return "TooLarge";
        case ValidationResult::ByteCountMismatch: return "ByteCountMismatch";
        default: return "Unknown";
    }
}

// Overflow-safe validation. All multiplication runs in 64-bit; no
// `width * height * bytesPerPixel` or `width * 4` int-width arithmetic
// exists on this path. hasPixels/pixelBytes are the raw ABI arguments
// (pixels pointer nullness + declared byte count); ownership, window
// identity, and actual buffer containment are checked by the caller.
inline ValidationOutcome validate_frame(int x, int y, int width, int height,
                                        uint32_t strideBytes, uint32_t pixelFormat,
                                        bool hasPixels, uint32_t pixelBytes) {
    ValidationOutcome out;
    out.result = ValidationResult::Ok;
    out.requiredBytes = 0;
    if (!hasPixels) {
        out.result = ValidationResult::NullPixels;
        return out;
    }
    // Bare-metal offset contract (unchanged from the old path): frames are
    // presented at the client-area origin. Non-zero origins fail closed
    // rather than rendering at an unchecked offset. The hosted compositor
    // accepts arbitrary x/y; unifying the offset semantics is follow-up
    // work, not needed for 480x360 (which presents at 0,0).
    if (x != 0 || y != 0) {
        out.result = ValidationResult::InvalidOffset;
        return out;
    }
    if (width < kMinDimension || height < kMinDimension ||
        width > kMaxDimension || height > kMaxDimension) {
        out.result = ValidationResult::InvalidGeometry;
        return out;
    }
    if (pixelFormat != kPixelFormatXrgb8888) {
        out.result = ValidationResult::InvalidFormat;
        return out;
    }
    // Stride: 4-byte aligned XRGB8888 rows, at least width*4 bytes.
    // The alignment requirement comes from the retained uint32_t staging
    // buffer (NativeWindowOwner::present); the hosted path does not
    // enforce it, but the bare-metal copy does.
    const uint64_t minStride =
        static_cast<uint64_t>(static_cast<uint32_t>(width)) * kBytesPerPixel;
    if ((strideBytes & 3u) != 0u ||
        static_cast<uint64_t>(strideBytes) < minStride) {
        out.result = ValidationResult::InvalidStride;
        return out;
    }
    const uint64_t required =
        static_cast<uint64_t>(strideBytes) * static_cast<uint64_t>(static_cast<uint32_t>(height));
    out.requiredBytes = required;
    if (required > kMaxFrameBytes) {
        out.result = ValidationResult::TooLarge;
        return out;
    }
    // Exact byte count (matches hosted native_app_runtime + compositor):
    // the frame must carry exactly stride*height bytes. Over- or
    // under-sized buffers fail closed instead of over-reading or
    // silently ignoring a caller bug.
    if (static_cast<uint64_t>(pixelBytes) != required) {
        out.result = ValidationResult::ByteCountMismatch;
        return out;
    }
    out.result = ValidationResult::Ok;
    return out;
}

// Destination mapping for NativeWindowOwner::draw semantics:
//   - frame smaller than the client area: centered (letterboxed with the
//     window background; no scaling, no aspect distortion).
//   - frame larger than the client area: top-left-clipped to the visible
//     portion (no scaling, no distortion).
//   - equal sizes: direct copy.
// All arithmetic is bounded (dimensions <= 4096), so no overflow.
struct ClientClip {
    bool visible;
    uint32_t dstX;
    uint32_t dstY;
    uint32_t visibleW;
    uint32_t visibleH;
};

inline ClientClip clip_to_client(uint32_t frameW, uint32_t frameH,
                                 uint32_t clientW, uint32_t clientH) {
    ClientClip clip;
    clip.visible = false;
    clip.dstX = 0;
    clip.dstY = 0;
    clip.visibleW = 0;
    clip.visibleH = 0;
    if (frameW == 0 || frameH == 0 || clientW == 0 || clientH == 0) return clip;
    clip.dstX = clientW > frameW ? (clientW - frameW) / 2u : 0u;
    clip.dstY = clientH > frameH ? (clientH - frameH) / 2u : 0u;
    clip.visibleW = clientW < frameW ? clientW : frameW;
    clip.visibleH = clientH < frameH ? clientH : frameH;
    clip.visible = clip.visibleW > 0 && clip.visibleH > 0;
    return clip;
}

// Per-row framebuffer clip (mirrors the compositor fb path in
// compositor.cpp DrawWindowContent: rows outside [0, fbH) are skipped,
// columns outside [0, fbW) are trimmed from the copy width).
struct RowClip {
    bool visible;
    uint32_t srcOffset;
    uint32_t dstOffset;
    uint32_t copyWidth;
};

inline RowClip clip_row(int frameLeft, uint32_t frameW, int fbW) {
    RowClip clip;
    clip.visible = false;
    clip.srcOffset = 0;
    clip.dstOffset = 0;
    clip.copyWidth = 0;
    if (fbW <= 0 || frameW == 0) return clip;
    const int srcLeft = frameLeft < 0 ? -frameLeft : 0;
    if (srcLeft < 0 || static_cast<uint64_t>(srcLeft) >= frameW) return clip;
    const int dstLeft = frameLeft + srcLeft;
    if (dstLeft < 0 || dstLeft >= fbW) return clip;
    uint32_t roomRight = static_cast<uint32_t>(fbW - dstLeft);
    uint32_t roomFrame = frameW - static_cast<uint32_t>(srcLeft);
    uint32_t width = roomRight < roomFrame ? roomRight : roomFrame;
    if (width == 0) return clip;
    clip.visible = true;
    clip.srcOffset = static_cast<uint32_t>(srcLeft);
    clip.dstOffset = static_cast<uint32_t>(dstLeft);
    clip.copyWidth = width;
    return clip;
}

}  // namespace native_present
}  // namespace kernel

#endif  // KERNEL_NATIVE_PRESENT_VALIDATION_H
