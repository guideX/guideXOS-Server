#pragma once

// Small freestanding-safe validation helpers shared by the kernel parser and
// host-side negative tests.  No pointer is dereferenced by an arithmetic
// helper unless its caller has already established the containing byte span.

#include <stdint.h>

typedef struct gxos_aarch64_validation_range {
    uint64_t start;
    uint64_t end;
} gxos_aarch64_validation_range;

static inline uint32_t gxos_aarch64_be32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static inline bool gxos_aarch64_add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static inline bool gxos_aarch64_mul_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || (left != 0 && right > UINT64_MAX / left)) return false;
    *result = left * right;
    return true;
}

static inline bool gxos_aarch64_span_inside(uint64_t offset, uint64_t size, uint64_t limit)
{
    uint64_t end = 0;
    return gxos_aarch64_add_u64(offset, size, &end) && end <= limit;
}

static inline bool gxos_aarch64_range_overlaps(gxos_aarch64_validation_range left,
                                                gxos_aarch64_validation_range right)
{
    return left.start < left.end && right.start < right.end &&
           left.start < right.end && right.start < left.end;
}

static inline bool gxos_aarch64_range_is_page_aligned(uint64_t start, uint64_t end)
{
    return start < end && (start & UINT64_C(0xfff)) == 0 && (end & UINT64_C(0xfff)) == 0;
}

static inline bool gxos_aarch64_fdt_header_bounds(const uint8_t* blob, uint64_t blob_size,
                                                   uint32_t* total_size, uint32_t* struct_offset,
                                                   uint32_t* struct_size, uint32_t* strings_offset,
                                                   uint32_t* strings_size)
{
    if (!blob || blob_size < 40 || blob_size > UINT64_C(16) * 1024 * 1024) return false;
    if (gxos_aarch64_be32(blob) != UINT32_C(0xd00dfeed)) return false;

    const uint32_t total = gxos_aarch64_be32(blob + 4);
    const uint32_t structOff = gxos_aarch64_be32(blob + 8);
    const uint32_t stringsOff = gxos_aarch64_be32(blob + 12);
    const uint32_t reserveOff = gxos_aarch64_be32(blob + 16);
    const uint32_t version = gxos_aarch64_be32(blob + 20);
    const uint32_t lastCompatible = gxos_aarch64_be32(blob + 24);
    const uint32_t stringsBytes = gxos_aarch64_be32(blob + 32);
    const uint32_t structBytes = gxos_aarch64_be32(blob + 36);

    if (total < 40 || total > blob_size || total > UINT32_MAX) return false;
    if (version < 16 || version > 19 || lastCompatible < 16 || lastCompatible > version) return false;
    if ((reserveOff & 7u) != 0 || reserveOff > total || !gxos_aarch64_span_inside(reserveOff, 16, total)) {
        return false;
    }
    if ((structOff & 3u) != 0 || (stringsOff & 3u) != 0 ||
        !gxos_aarch64_span_inside(structOff, structBytes, total) ||
        !gxos_aarch64_span_inside(stringsOff, stringsBytes, total)) return false;

    if (total_size) *total_size = total;
    if (struct_offset) *struct_offset = structOff;
    if (struct_size) *struct_size = structBytes;
    if (strings_offset) *strings_offset = stringsOff;
    if (strings_size) *strings_size = stringsBytes;
    return true;
}

static inline bool gxos_aarch64_memory_map_layout_valid(uint64_t map, uint64_t map_size,
                                                         uint64_t descriptor_size,
                                                         uint64_t entry_count)
{
    uint64_t expected_size = 0;
    return map != 0 && map_size != 0 && descriptor_size >= 40 &&
           descriptor_size <= UINT64_C(4096) && entry_count != 0 &&
           entry_count <= UINT64_C(65536) &&
           gxos_aarch64_mul_u64(descriptor_size, entry_count, &expected_size) &&
           expected_size == map_size;
}

static inline bool gxos_aarch64_page_descriptor_valid(uint64_t physical, uint64_t pages,
                                                       uint64_t* end)
{
    uint64_t bytes = 0;
    if ((physical & UINT64_C(0xfff)) != 0 || pages == 0 ||
        !gxos_aarch64_mul_u64(pages, UINT64_C(0x1000), &bytes) ||
        !gxos_aarch64_add_u64(physical, bytes, end)) return false;
    return *end > physical;
}
