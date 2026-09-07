#include "kernel/framebuffer_contract.h"

#include <cstdint>
#include <iostream>
#include <limits>

using kernel::framebuffer::ClippedRect;
using kernel::framebuffer::Geometry;

static bool expect(bool condition, int code)
{
    if (!condition) {
        std::cerr << "Phase 6 host control failed: " << code << "\n";
        return false;
    }
    return true;
}

int main()
{
    Geometry good{0x100000, 0x4000, 64, 32, 256, 32,
                  kernel::framebuffer::kPixelFormatB8G8R8A8};
    if (!expect(kernel::framebuffer::validate_geometry(good), 1)) return 1;

    Geometry badPitch = good;
    badPitch.pitch = 64 * 4 - 1;
    if (!expect(!kernel::framebuffer::validate_geometry(badPitch), 2)) return 2;

    Geometry badSize = good;
    badSize.size = 256 * 32 - 1;
    if (!expect(!kernel::framebuffer::validate_geometry(badSize), 3)) return 3;

    Geometry badFormat = good;
    badFormat.format = kernel::framebuffer::kPixelFormatUnknown;
    if (!expect(!kernel::framebuffer::validate_geometry(badFormat), 4)) return 4;

    Geometry badAlignment = good;
    badAlignment.base = 0x100002;
    if (!expect(!kernel::framebuffer::validate_geometry(badAlignment), 5)) return 5;

    Geometry overflowingBase = good;
    overflowingBase.base = std::numeric_limits<uint64_t>::max() - 0x1000;
    if (!expect(!kernel::framebuffer::validate_geometry(overflowingBase), 6)) return 6;

    ClippedRect rect{};
    if (!expect(kernel::framebuffer::clip_rect(-10, -5, 30, 20, 100, 50, &rect), 7) ||
        !expect(rect.x == 0 && rect.y == 0 && rect.width == 20 && rect.height == 15, 7)) return 7;
    if (!expect(!kernel::framebuffer::clip_rect(100, 0, 1, 1, 100, 50, &rect), 8)) return 8;
    if (!expect(!kernel::framebuffer::clip_rect(0, 50, 1, 1, 100, 50, &rect), 9)) return 9;
    if (!expect(kernel::framebuffer::clip_rect(std::numeric_limits<int64_t>::max() - 2, 0,
                                                std::numeric_limits<uint64_t>::max(), 1,
                                                100, 50, &rect) == false, 10)) return 10;
    if (!expect(kernel::framebuffer::clip_rect(90, 40, 40, 30, 100, 50, &rect), 11) ||
        !expect(rect.x == 90 && rect.y == 40 && rect.width == 10 && rect.height == 10, 12)) return 11;

    std::cout << "Phase 6 host controls: PASS (geometry, overflow, format, clipping)\n";
    return 0;
}
