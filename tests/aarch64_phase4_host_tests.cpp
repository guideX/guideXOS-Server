#include <stdint.h>
#include <stdio.h>

#include "kernel/boot_info.h"
#include "kernel/common_physical_allocator.h"
#include "../kernel/arch/arm64/phase4_boot_info.h"

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

    gxos_aarch64_phase2_platform gicv3{};
    gicv3.gic_version = GXOS_AARCH64_GIC_V3;
    gicv3.uart_base = 0x09000000;
    gicv3.uart_size = 0x1000;
    gicv3.gicd_base = 0x08000000;
    gicv3.gicd_size = 0x10000;
    gicv3.gic_redistributor_region_count = GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS;
    for (uint32_t i = 0; i < gicv3.gic_redistributor_region_count; ++i) {
        gicv3.gic_redistributor_regions[i] = { UINT64_C(0x080a0000) + i * UINT64_C(0x20000), UINT64_C(0x20000) };
    }
    kernel::boot::CommonBootInfo gicv3Info = make_info(map);
    if (!kernel::arch::arm64::populate_phase4_mmio_ranges(&gicv3Info, &gicv3) ||
        gicv3Info.mmio_range_count != 2 + GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS ||
        gicv3Info.mmio_ranges[0].base != gicv3.uart_base ||
        gicv3Info.mmio_ranges[1].base != gicv3.gicd_base ||
        gicv3Info.mmio_ranges[2].base != gicv3.gic_redistributor_regions[0].base ||
        !kernel::boot::validate(&gicv3Info)) return 11;

    gxos_aarch64_phase2_platform gicv2{};
    gicv2.gic_version = GXOS_AARCH64_GIC_V2;
    gicv2.uart_base = 0x09000000;
    gicv2.uart_size = 0x1000;
    gicv2.gicd_base = 0x08000000;
    gicv2.gicd_size = 0x1000;
    gicv2.gicc_base = 0x08010000;
    gicv2.gicc_size = 0x1000;
    kernel::boot::CommonBootInfo gicv2Info = make_info(map);
    if (!kernel::arch::arm64::populate_phase4_mmio_ranges(&gicv2Info, &gicv2) ||
        gicv2Info.mmio_range_count != 3 ||
        gicv2Info.mmio_ranges[2].base != gicv2.gicc_base ||
        !kernel::boot::validate(&gicv2Info)) return 12;

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
    printf("aarch64 phase4 host controls: PASS (GICv2 GICC and GICv3 GICR resource tables)\n");
    return 0;
}
