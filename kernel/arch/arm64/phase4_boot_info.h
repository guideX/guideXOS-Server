#pragma once

#include "../../../aarch64/phase2/phase2_platform.h"
#include "../../../kernel/core/include/kernel/boot_info.h"

namespace kernel {
namespace arch {
namespace arm64 {

inline bool populate_phase4_mmio_ranges(
    kernel::boot::CommonBootInfo* info,
    const gxos_aarch64_phase2_platform* platform)
{
    if (!info || !platform) return false;

    kernel::boot::Range ranges[kernel::boot::kMaxMmioRanges] = {};
    uint32_t count = 0;
    const auto append = [&](uint64_t base, uint64_t size) -> bool {
        if (base == 0 || size == 0 || count >= kernel::boot::kMaxMmioRanges) return false;
        ranges[count++] = { base, size };
        return true;
    };

    if (!append(platform->uart_base, platform->uart_size) ||
        !append(platform->gicd_base, platform->gicd_size)) return false;

    if (platform->gic_version == GXOS_AARCH64_GIC_V2) {
        if (!append(platform->gicc_base, platform->gicc_size)) return false;
    } else if (platform->gic_version == GXOS_AARCH64_GIC_V3) {
        if (platform->gic_redistributor_region_count == 0 ||
            platform->gic_redistributor_region_count > GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS) return false;
        for (uint32_t i = 0; i < platform->gic_redistributor_region_count; ++i) {
            if (!append(platform->gic_redistributor_regions[i].base,
                        platform->gic_redistributor_regions[i].size)) return false;
        }
    } else {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) info->mmio_ranges[i] = ranges[i];
    info->mmio_range_count = count;
    return true;
}

} // namespace arm64
} // namespace arch
} // namespace kernel
