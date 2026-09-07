#include <stdint.h>
#include <stdio.h>

#include "kernel/boot_info.h"
#include "kernel/common_physical_allocator.h"

static void put64(uint8_t* bytes, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}

static kernel::boot::CommonBootInfo make_info(uint8_t* map)
{
    for (uint32_t i = 0; i < 40; ++i) map[i] = 0;
    map[0] = 7;
    put64(map + 8, UINT64_C(0x00100000));
    put64(map + 24, 64);

    kernel::boot::CommonBootInfo info{};
    info.magic = kernel::boot::kCommonBootInfoMagic;
    info.version = kernel::boot::kCommonBootInfoVersion;
    info.size = sizeof(info);
    info.architecture = kernel::boot::kArchitectureArm64;
    info.kernel_base = 0x100000;
    info.kernel_size = 0x2000;
    info.bootstrap_stack_base = 0x110000;
    info.bootstrap_stack_size = 0x2000;
    info.memory_map = (uint64_t)(uintptr_t)map;
    info.memory_map_size = 40;
    info.memory_map_descriptor_size = 40;
    info.memory_map_entry_count = 1;
    info.handoff_base = 0x120000;
    info.handoff_size = 0x1000;
    info.ramdisk_base = 0x130000;
    info.ramdisk_size = 0x1000;
    info.dtb_base = 0x140000;
    info.dtb_size = 0x1000;
    info.flags = kernel::boot::kBootFlagMemoryMap |
                 kernel::boot::kBootFlagRamdisk |
                 kernel::boot::kBootFlagDtb;
    info.memory_range_count = 1;
    info.memory_ranges[0] = { 0x100000, 0x40000 };
    info.mmio_range_count = 1;
    info.mmio_ranges[0] = { 0x08000000, 0x1000 };
    return info;
}

int main()
{
    uint8_t map[40];
    kernel::boot::CommonBootInfo info = make_info(map);
    if (!kernel::boot::validate(&info)) return 1;

    kernel::boot::CommonBootInfo malformed = info;
    malformed.memory_map_descriptor_size = 8;
    if (kernel::boot::validate(&malformed)) return 2;
    malformed = info;
    malformed.ramdisk_base = 0;
    if (kernel::boot::validate(&malformed)) return 3;

    if (!kernel::memory::initialize(info) || !kernel::memory::ready()) return 4;
    if (!kernel::memory::is_reserved(info.ramdisk_base, info.ramdisk_size)) return 5;
    uint64_t first = 0;
    uint64_t second = 0;
    if (!kernel::memory::allocate_pages(2, &first) || !kernel::memory::allocate_pages(3, &second)) return 6;
    if ((first & 0xfff) != 0 || (second & 0xfff) != 0 || first == second ||
        kernel::memory::is_reserved(first, 0x2000) || kernel::memory::is_reserved(second, 0x3000)) return 7;
    if (!kernel::memory::release_pages(first, 2) || kernel::memory::release_pages(first, 2)) return 8;
    if (!kernel::memory::release_pages(second, 3)) return 9;
    uint64_t impossible = 0;
    if (kernel::memory::allocate_pages(1000, &impossible)) return 10;
    printf("aarch64 phase4 host controls: PASS\n");
    return 0;
}
