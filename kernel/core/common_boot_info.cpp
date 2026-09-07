#include "include/kernel/boot_info.h"

namespace kernel {
namespace boot {

static bool add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return *result > left;
}

static bool valid_range(const Range& range)
{
    uint64_t end = 0;
    return range.size != 0 && add_u64(range.base, range.size, &end);
}

bool validate(const CommonBootInfo* info)
{
    if (!info || info->magic != kCommonBootInfoMagic ||
        info->version != kCommonBootInfoVersion || info->size != sizeof(*info) ||
        info->architecture == 0 || info->kernel_size == 0 ||
        info->bootstrap_stack_size == 0 || info->memory_map == 0 ||
        info->memory_map_size == 0 || info->memory_map_descriptor_size < 40 ||
        info->memory_map_entry_count == 0 ||
        info->memory_range_count == 0 || info->memory_range_count > kMaxMemoryRanges ||
        info->mmio_range_count > kMaxMmioRanges) return false;

    uint64_t ignored = 0;
    if (!add_u64(info->kernel_base, info->kernel_size, &ignored) ||
        !add_u64(info->bootstrap_stack_base, info->bootstrap_stack_size, &ignored) ||
        !add_u64(info->memory_map, info->memory_map_size, &ignored)) return false;
    if ((info->memory_map_size / info->memory_map_descriptor_size) < info->memory_map_entry_count ||
        info->memory_map_entry_count > UINT64_MAX / info->memory_map_descriptor_size) return false;
    for (uint32_t i = 0; i < info->memory_range_count; ++i) {
        if (!valid_range(info->memory_ranges[i])) return false;
    }
    for (uint32_t i = 0; i < info->mmio_range_count; ++i) {
        if (!valid_range(info->mmio_ranges[i])) return false;
    }
    if ((info->flags & kBootFlagRamdisk) != 0 &&
        (info->ramdisk_base == 0 || info->ramdisk_size == 0 ||
         !add_u64(info->ramdisk_base, info->ramdisk_size, &ignored))) return false;
    if ((info->flags & kBootFlagDtb) != 0 &&
        (info->dtb_base == 0 || info->dtb_size == 0 ||
         !add_u64(info->dtb_base, info->dtb_size, &ignored))) return false;
    return true;
}

} // namespace boot
} // namespace kernel
