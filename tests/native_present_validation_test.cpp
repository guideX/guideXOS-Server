// MC7 host-side tests for generic bare-metal present_frame validation.
//
// Covers the single-source-of-truth contract in
// kernel/core/include/kernel/native_present_validation.h (the same header
// the bare-metal ABI entry compiles):
//   legacy 448x553, Missile Command 480x360, small/odd/max dims,
//   zero/negative/invalid geometry, stride rules, format, offsets,
//   undersized/oversized buffers, multiplication-overflow safety,
//   client-area clipping on every edge, row clipping incl. fully
//   off-screen frames, repeated presentation, and stateless relaunch.
//
// Build: g++ -std=c++17 tests/native_present_validation_test.cpp -o out/...exe
// (see scripts/run-native-present-validation-test.ps1).

#include "../kernel/core/include/kernel/native_present_validation.h"

#include <cstdint>
#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

using kernel::native_present::clip_row;
using kernel::native_present::clip_to_client;
using kernel::native_present::validate_frame;
using kernel::native_present::ValidationResult;

bool isOk(int x, int y, int w, int h, uint32_t stride, uint32_t format,
          bool hasPixels, uint32_t bytes, uint64_t* requiredOut = nullptr) {
    const kernel::native_present::ValidationOutcome out =
        validate_frame(x, y, w, h, stride, format, hasPixels, bytes);
    if (requiredOut) *requiredOut = out.requiredBytes;
    return out.result == ValidationResult::Ok;
}

ValidationResult validateResult(int x, int y, int w, int h, uint32_t stride,
                                uint32_t format, bool hasPixels, uint32_t bytes) {
    return validate_frame(x, y, w, h, stride, format, hasPixels, bytes).result;
}

const uint32_t kFormat =
    kernel::native_present::kPixelFormatXrgb8888;

}  // namespace

int main() {
    // ---- Legacy / MC dimensions -------------------------------------------
    check(kernel::native_present::kLegacyBytes == 1792ull * 553ull,
          "legacy byte constant matches stride*height");
    check(isOk(0, 0, 448, 553, 1792u, kFormat, true, 990976u),
          "legacy 448x553 accepted");
    check(isOk(0, 0, 480, 360, 1920u, kFormat, true, 691200u),
          "missile command 480x360 accepted");
    {
        uint64_t required = 0;
        check(isOk(0, 0, 480, 360, 1920u, kFormat, true, 691200u, &required) &&
                  required == 691200ull,
              "480x360 required bytes exact");
    }
    {
        // Padded stride (row pitch wider than the tight minimum) is valid.
        uint64_t required = 0;
        check(isOk(0, 0, 480, 360, 2048u, kFormat, true, 737280u, &required) &&
                  required == 2048ull * 360ull,
              "padded stride accepted");
    }

    // ---- Small / odd dimensions -------------------------------------------
    check(isOk(0, 0, 1, 1, 4u, kFormat, true, 4u), "1x1 accepted");
    check(isOk(0, 0, 64, 64, 256u, kFormat, true, 16384u), "64x64 accepted");
    check(isOk(0, 0, 3, 7, 12u, kFormat, true, 84u), "odd 3x7 accepted");
    check(isOk(0, 0, 321, 239, 1284u, kFormat, true, 321u * 4u * 239u),
          "odd 321x239 accepted");

    // ---- Maximum supported safe dimensions --------------------------------
    check(isOk(0, 0, 2048, 2048, 8192u, kFormat, true, 16777216u),
          "2048x2048 at 16MiB cap accepted");
    check(isOk(0, 0, 4096, 1024, 16384u, kFormat, true, 16777216u),
          "4096x1024 at 16MiB cap accepted");
    check(validateResult(0, 0, 4096, 1025, 16384u, kFormat, true, 16793600u) ==
              ValidationResult::TooLarge,
          "one row over 16MiB rejected");
    check(validateResult(0, 0, 4097, 360, 16388u, kFormat, true, 5903280u) ==
              ValidationResult::InvalidGeometry,
          "width 4097 rejected");
    check(validateResult(0, 0, 480, 4097, 1920u, kFormat, true, 7866240u) ==
              ValidationResult::InvalidGeometry,
          "height 4097 rejected");

    // ---- Zero / negative / invalid geometry --------------------------------
    check(validateResult(0, 0, 0, 360, 4u, kFormat, true, 0u) ==
              ValidationResult::InvalidGeometry,
          "zero width rejected");
    check(validateResult(0, 0, 480, 0, 1920u, kFormat, true, 0u) ==
              ValidationResult::InvalidGeometry,
          "zero height rejected");
    check(validateResult(0, 0, -480, 360, 1920u, kFormat, true, 691200u) ==
              ValidationResult::InvalidGeometry,
          "negative width rejected");
    check(validateResult(0, 0, 480, -360, 1920u, kFormat, true, 691200u) ==
              ValidationResult::InvalidGeometry,
          "negative height rejected");

    // ---- Offsets fail closed -----------------------------------------------
    check(validateResult(1, 0, 480, 360, 1920u, kFormat, true, 691200u) ==
              ValidationResult::InvalidOffset,
          "x=1 rejected");
    check(validateResult(0, 1, 480, 360, 1920u, kFormat, true, 691200u) ==
              ValidationResult::InvalidOffset,
          "y=1 rejected");
    check(validateResult(-1, 0, 480, 360, 1920u, kFormat, true, 691200u) ==
              ValidationResult::InvalidOffset,
          "x=-1 rejected");

    // ---- Format / null pixels -----------------------------------------------
    check(validateResult(0, 0, 480, 360, 1920u, 0u, true, 691200u) ==
              ValidationResult::InvalidFormat,
          "format 0 rejected");
    check(validateResult(0, 0, 480, 360, 1920u, 2u, true, 691200u) ==
              ValidationResult::InvalidFormat,
          "format 2 rejected");
    check(validateResult(0, 0, 480, 360, 1920u, kFormat, false, 691200u) ==
              ValidationResult::NullPixels,
          "null pixels rejected");

    // ---- Stride rules --------------------------------------------------------
    check(validateResult(0, 0, 480, 360, 1916u, kFormat, true, 689760u) ==
              ValidationResult::InvalidStride,
          "short stride rejected");
    check(validateResult(0, 0, 480, 360, 1921u, kFormat, true, 691560u) ==
              ValidationResult::InvalidStride,
          "unaligned stride rejected");
    check(validateResult(0, 0, 480, 360, 0u, kFormat, true, 0u) ==
              ValidationResult::InvalidStride,
          "zero stride rejected");

    // ---- Buffer-size validation ----------------------------------------------
    check(validateResult(0, 0, 480, 360, 1920u, kFormat, true, 691196u) ==
              ValidationResult::ByteCountMismatch,
          "undersized buffer rejected");
    check(validateResult(0, 0, 480, 360, 1920u, kFormat, true, 691204u) ==
              ValidationResult::ByteCountMismatch,
          "oversized buffer rejected");
    check(validateResult(0, 0, 480, 360, 1920u, kFormat, true, 0u) ==
              ValidationResult::ByteCountMismatch,
          "zero buffer rejected");

    // ---- Multiplication-overflow safety --------------------------------------
    // width*height*4 in 32-bit would wrap for large inputs; the validator
    // must never accept via a wrapped comparison. A giant aligned stride
    // still yields required > 16MiB (TooLarge), never Ok, never wrap.
    check(validateResult(0, 0, 64, 64, 0x40000000u, kFormat, true, 0u) ==
              ValidationResult::TooLarge,
          "huge stride fails as TooLarge not wrap");
    check(validateResult(0, 0, 2147483647, 2147483647, 0xFFFFFFFCu, kFormat,
                         true, 0u) == ValidationResult::InvalidGeometry,
          "INT_MAX dims rejected as geometry");
    check(validateResult(0, 0, 4096, 4096, 16384u, kFormat, true, 0u) ==
              ValidationResult::TooLarge,
          "4096x4096 fails as TooLarge");

    // ---- Repeated presentation -----------------------------------------------
    {
        bool allOk = true;
        for (int i = 0; i < 120; ++i) {
            if (!isOk(0, 0, 480, 360, 1920u, kFormat, true, 691200u)) {
                allOk = false;
                break;
            }
        }
        check(allOk, "120 repeated 480x360 validations pass");
    }
    {
        // Alternating sizes (relaunch/exit sequences reuse the validator
        // with no retained state): legacy, MC, small, max.
        check(isOk(0, 0, 448, 553, 1792u, kFormat, true, 990976u) &&
                  isOk(0, 0, 480, 360, 1920u, kFormat, true, 691200u) &&
                  isOk(0, 0, 1, 1, 4u, kFormat, true, 4u) &&
                  isOk(0, 0, 2048, 2048, 8192u, kFormat, true, 16777216u),
              "stateless relaunch sequence passes");
    }

    // ---- Client-area clipping --------------------------------------------------
    {
        // Smaller than client: centered, fully visible.
        const kernel::native_present::ClientClip c = clip_to_client(480, 360, 800, 600);
        check(c.visible && c.dstX == 160 && c.dstY == 120 && c.visibleW == 480 &&
                  c.visibleH == 360,
              "smaller frame centered");
    }
    {
        // Equal sizes: direct copy at origin.
        const kernel::native_present::ClientClip c = clip_to_client(448, 553, 448, 553);
        check(c.visible && c.dstX == 0 && c.dstY == 0 && c.visibleW == 448 &&
                  c.visibleH == 553,
              "equal frame direct copy");
    }
    {
        // Larger than client: clipped to the client, top-left anchored.
        const kernel::native_present::ClientClip c = clip_to_client(800, 600, 640, 480);
        check(c.visible && c.dstX == 0 && c.dstY == 0 && c.visibleW == 640 &&
                  c.visibleH == 480,
              "larger frame clipped");
    }
    {
        // One axis each way.
        const kernel::native_present::ClientClip wide = clip_to_client(800, 100, 640, 480);
        check(wide.visible && wide.dstX == 0 && wide.visibleW == 640 &&
                  wide.dstY == 190 && wide.visibleH == 100,
              "wide frame clips horizontally centers vertically");
        const kernel::native_present::ClientClip tall = clip_to_client(100, 600, 640, 480);
        check(tall.visible && tall.dstX == 270 && tall.visibleW == 100 &&
                  tall.dstY == 0 && tall.visibleH == 480,
              "tall frame clips vertically centers horizontally");
    }
    {
        // Degenerate inputs never produce a visible region.
        check(!clip_to_client(0, 360, 800, 600).visible, "zero frameW invisible");
        check(!clip_to_client(480, 0, 800, 600).visible, "zero frameH invisible");
        check(!clip_to_client(480, 360, 0, 600).visible, "zero clientW invisible");
        check(!clip_to_client(480, 360, 800, 0).visible, "zero clientH invisible");
    }

    // ---- Row clipping (every edge) ----------------------------------------------
    {
        // Fully inside.
        const kernel::native_present::RowClip r = clip_row(10, 100, 800);
        check(r.visible && r.srcOffset == 0 && r.dstOffset == 10 && r.copyWidth == 100,
              "row inside");
    }
    {
        // Left edge: negative origin trims the source.
        const kernel::native_present::RowClip r = clip_row(-20, 100, 800);
        check(r.visible && r.srcOffset == 20 && r.dstOffset == 0 && r.copyWidth == 80,
              "row left clip");
    }
    {
        // Right edge: copy trimmed to the framebuffer.
        const kernel::native_present::RowClip r = clip_row(750, 100, 800);
        check(r.visible && r.srcOffset == 0 && r.dstOffset == 750 && r.copyWidth == 50,
              "row right clip");
    }
    {
        // Both edges: wide frame over a narrow screen.
        const kernel::native_present::RowClip r = clip_row(-50, 1000, 800);
        check(r.visible && r.srcOffset == 50 && r.dstOffset == 0 && r.copyWidth == 800,
              "row both edges clip");
    }
    {
        // Fully invisible: off the left, off the right, zero widths.
        check(!clip_row(-200, 100, 800).visible, "row fully left invisible");
        check(!clip_row(800, 100, 800).visible, "row fully right invisible");
        check(!clip_row(900, 100, 800).visible, "row beyond right invisible");
        check(!clip_row(10, 0, 800).visible, "row zero frameW invisible");
        check(!clip_row(10, 100, 0).visible, "row zero fbW invisible");
    }

    if (g_failures == 0) {
        std::printf("Native present validation test PASS\n");
        return 0;
    }
    std::printf("Native present validation test FAIL (%d)\n", g_failures);
    return 1;
}
