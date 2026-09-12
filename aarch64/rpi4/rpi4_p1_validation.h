#pragma once

#include <stdint.h>

static inline bool gxos_aarch64_rpi4_p1_range_contains(
    uint64_t available_base, uint64_t available_size,
    uint64_t requested_base, uint64_t requested_size)
{
    if (available_size == 0 || requested_size == 0 ||
        requested_base < available_base ||
        available_size > UINT64_MAX - available_base ||
        requested_size > UINT64_MAX - requested_base) return false;
    return requested_base + requested_size <= available_base + available_size;
}

static inline bool gxos_aarch64_rpi4_p1_framebuffer_geometry_valid(
    uint64_t base, uint64_t size, uint32_t width, uint32_t height,
    uint32_t pitch, uint32_t bits_per_pixel, uint32_t format)
{
    uint64_t end = 0;
    uint64_t required_pitch = 0;
    uint64_t required_size = 0;
    if (base == 0 || size == 0 || width == 0 || height == 0 ||
        bits_per_pixel != 32 || (format != 1 && format != 2) ||
        width > UINT64_MAX / 4 ||
        (required_pitch = static_cast<uint64_t>(width) * 4) > pitch ||
        static_cast<uint64_t>(pitch) > UINT64_MAX / height ||
        (required_size = static_cast<uint64_t>(pitch) * height) > size ||
        base > UINT64_MAX - size) return false;
    end = base + size;
    return end > base && required_size != 0;
}
