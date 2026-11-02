#pragma once

// Small freestanding-safe validation helpers shared by the kernel parser and
// host-side negative tests.  No pointer is dereferenced by an arithmetic
// helper unless its caller has already established the containing byte span.

#include <stdint.h>

typedef struct gxos_aarch64_validation_range {
    uint64_t start;
    uint64_t end;
} gxos_aarch64_validation_range;

typedef enum gxos_aarch64_fdt_header_status {
    GXOS_AARCH64_FDT_HEADER_OK = 0,
    GXOS_AARCH64_FDT_HEADER_NULL_POINTER = 1,
    GXOS_AARCH64_FDT_HEADER_TRUNCATED = 2,
    GXOS_AARCH64_FDT_HEADER_TOO_LARGE = 3,
    GXOS_AARCH64_FDT_HEADER_BAD_MAGIC = 4,
    GXOS_AARCH64_FDT_HEADER_BAD_TOTAL_SIZE = 5,
    GXOS_AARCH64_FDT_HEADER_UNSUPPORTED_VERSION = 6,
    GXOS_AARCH64_FDT_HEADER_BAD_BLOCK_BOUNDS = 7,
    GXOS_AARCH64_FDT_HEADER_BAD_RESERVATION_MAP = 8,
    GXOS_AARCH64_FDT_HEADER_OVERLAPPING_BLOCKS = 9
} gxos_aarch64_fdt_header_status;

typedef struct gxos_aarch64_fdt_header_info {
    uint32_t magic;
    uint32_t total_size;
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t reservation_offset;
    uint32_t version;
    uint32_t last_compatible_version;
    uint32_t boot_cpu_id;
    uint32_t strings_size;
    uint32_t structure_size;
} gxos_aarch64_fdt_header_info;

static inline uint32_t gxos_aarch64_be32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static inline uint32_t gxos_aarch64_le32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline uint64_t gxos_aarch64_be64(const uint8_t* bytes)
{
    return ((uint64_t)gxos_aarch64_be32(bytes) << 32) | gxos_aarch64_be32(bytes + 4);
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

static inline gxos_aarch64_fdt_header_status gxos_aarch64_fdt_header_validate(
    const uint8_t* blob, uint64_t blob_size, gxos_aarch64_fdt_header_info* info)
{
    if (info) {
        info->magic = 0;
        info->total_size = 0;
        info->structure_offset = 0;
        info->strings_offset = 0;
        info->reservation_offset = 0;
        info->version = 0;
        info->last_compatible_version = 0;
        info->boot_cpu_id = 0;
        info->strings_size = 0;
        info->structure_size = 0;
    }
    if (!blob) return GXOS_AARCH64_FDT_HEADER_NULL_POINTER;
    if (blob_size < 40) return GXOS_AARCH64_FDT_HEADER_TRUNCATED;
    if (blob_size > UINT64_C(16) * 1024 * 1024) return GXOS_AARCH64_FDT_HEADER_TOO_LARGE;

    const uint32_t total = gxos_aarch64_be32(blob + 4);
    const uint32_t structOff = gxos_aarch64_be32(blob + 8);
    const uint32_t stringsOff = gxos_aarch64_be32(blob + 12);
    const uint32_t reserveOff = gxos_aarch64_be32(blob + 16);
    const uint32_t version = gxos_aarch64_be32(blob + 20);
    const uint32_t lastCompatible = gxos_aarch64_be32(blob + 24);
    const uint32_t stringsBytes = gxos_aarch64_be32(blob + 32);
    const uint32_t structBytes = gxos_aarch64_be32(blob + 36);

    if (info) {
        info->magic = gxos_aarch64_be32(blob);
        info->total_size = total;
        info->structure_offset = structOff;
        info->strings_offset = stringsOff;
        info->reservation_offset = reserveOff;
        info->version = version;
        info->last_compatible_version = lastCompatible;
        info->boot_cpu_id = gxos_aarch64_be32(blob + 28);
        info->strings_size = stringsBytes;
        info->structure_size = structBytes;
    }

    if (gxos_aarch64_be32(blob) != UINT32_C(0xd00dfeed)) return GXOS_AARCH64_FDT_HEADER_BAD_MAGIC;
    if (total < 40 || total > blob_size) return GXOS_AARCH64_FDT_HEADER_BAD_TOTAL_SIZE;
    if (version < 16 || version > 19 || lastCompatible < 16 || lastCompatible > version) {
        return GXOS_AARCH64_FDT_HEADER_UNSUPPORTED_VERSION;
    }
    if ((reserveOff & 7u) != 0 || reserveOff < 40 ||
        (structOff & 3u) != 0 || (stringsOff & 3u) != 0 ||
        structOff < 40 || stringsOff < 40 ||
        !gxos_aarch64_span_inside(structOff, structBytes, total) ||
        !gxos_aarch64_span_inside(stringsOff, stringsBytes, total)) {
        return GXOS_AARCH64_FDT_HEADER_BAD_BLOCK_BOUNDS;
    }

    uint64_t reserveCursor = reserveOff;
    bool reserveTerminatorFound = false;
    while (gxos_aarch64_span_inside(reserveCursor, 16, total)) {
        const uint64_t address = gxos_aarch64_be64(blob + reserveCursor);
        const uint64_t size = gxos_aarch64_be64(blob + reserveCursor + 8);
        reserveCursor += 16;
        if (address == 0 && size == 0) {
            reserveTerminatorFound = true;
            break;
        }
    }
    if (!reserveTerminatorFound) return GXOS_AARCH64_FDT_HEADER_BAD_RESERVATION_MAP;

    const gxos_aarch64_validation_range structure = {
        structOff, (uint64_t)structOff + structBytes};
    const gxos_aarch64_validation_range strings = {
        stringsOff, (uint64_t)stringsOff + stringsBytes};
    const gxos_aarch64_validation_range reserveMap = {reserveOff, reserveCursor};
    if (gxos_aarch64_range_overlaps(structure, strings) ||
        gxos_aarch64_range_overlaps(structure, reserveMap) ||
        gxos_aarch64_range_overlaps(strings, reserveMap)) {
        return GXOS_AARCH64_FDT_HEADER_OVERLAPPING_BLOCKS;
    }

    return GXOS_AARCH64_FDT_HEADER_OK;
}

static inline bool gxos_aarch64_fdt_header_bounds(const uint8_t* blob, uint64_t blob_size,
                                                   uint32_t* total_size, uint32_t* struct_offset,
                                                   uint32_t* struct_size, uint32_t* strings_offset,
                                                   uint32_t* strings_size)
{
    gxos_aarch64_fdt_header_info info = {};
    if (gxos_aarch64_fdt_header_validate(blob, blob_size, &info) != GXOS_AARCH64_FDT_HEADER_OK) {
        return false;
    }

    if (total_size) *total_size = info.total_size;
    if (struct_offset) *struct_offset = info.structure_offset;
    if (struct_size) *struct_size = info.structure_size;
    if (strings_offset) *strings_offset = info.strings_offset;
    if (strings_size) *strings_size = info.strings_size;
    return true;
}

static inline bool gxos_aarch64_dtb_allocation_bounds(uint64_t physical_base,
                                                        uint64_t dtb_size,
                                                        uint64_t* allocation_end)
{
    if (!allocation_end || physical_base == 0 || (physical_base & UINT64_C(0xfff)) != 0 ||
        dtb_size < 40 || dtb_size > UINT64_C(16) * 1024 * 1024) return false;
    const uint64_t allocation_size = (dtb_size + UINT64_C(0xfff)) & ~UINT64_C(0xfff);
    return gxos_aarch64_add_u64(physical_base, allocation_size, allocation_end) &&
           *allocation_end > physical_base;
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

static inline bool gxos_aarch64_memory_map_range_covered(
    const uint8_t* map, uint64_t map_address, uint64_t map_size,
    uint64_t descriptor_size, uint64_t entry_count,
    uint64_t range_start, uint64_t range_size, uint32_t required_type)
{
    if (!map || range_size == 0 ||
        !gxos_aarch64_memory_map_layout_valid(map_address, map_size,
                                               descriptor_size, entry_count)) return false;
    uint64_t range_end = 0;
    if (!gxos_aarch64_add_u64(range_start, range_size, &range_end) || range_end <= range_start) {
        return false;
    }

    for (uint64_t index = 0; index < entry_count; ++index) {
        const uint8_t* descriptor = map + index * descriptor_size;
        const uint32_t type = gxos_aarch64_le32(descriptor);
        if (type != required_type) continue;
        const uint64_t physical_start = (uint64_t)gxos_aarch64_le32(descriptor + 8) |
            ((uint64_t)gxos_aarch64_le32(descriptor + 12) << 32);
        const uint64_t pages = (uint64_t)gxos_aarch64_le32(descriptor + 24) |
            ((uint64_t)gxos_aarch64_le32(descriptor + 28) << 32);
        uint64_t bytes = 0;
        uint64_t descriptor_end = 0;
        if (!gxos_aarch64_mul_u64(pages, UINT64_C(0x1000), &bytes) || bytes == 0 ||
            !gxos_aarch64_add_u64(physical_start, bytes, &descriptor_end)) return false;
        if (range_start >= physical_start && range_end <= descriptor_end) return true;
    }
    return false;
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
