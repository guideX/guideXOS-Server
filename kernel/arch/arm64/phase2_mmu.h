#pragma once

#include <stdint.h>

#include "../../../aarch64/phase2/phase2_platform.h"

#define GXOS_AARCH64_PHASE2_MMU_GRANULE UINT64_C(4096)
#define GXOS_AARCH64_PHASE2_MMU_VA_BITS 48
#define GXOS_AARCH64_PHASE2_MMU_PA_BITS 40

uint8_t phase2_mmu_build(const gxos_aarch64_phase2_platform* platform,
                         uint64_t kernel_base, uint64_t kernel_size);
#if defined(GXOS_AARCH64_PHASE6)
uint8_t phase2_mmu_build_with_framebuffer(const gxos_aarch64_phase2_platform* platform,
                                          uint64_t kernel_base, uint64_t kernel_size,
                                          uint64_t framebuffer_base,
                                          uint64_t framebuffer_size);
#endif
void phase2_mmu_enable();
uint64_t phase2_mmu_tables_start();
uint64_t phase2_mmu_tables_end();
uint64_t phase2_mmu_root();
uint64_t phase2_mmu_read_mair();
uint64_t phase2_mmu_read_tcr();
uint64_t phase2_mmu_read_ttbr0();
uint64_t phase2_mmu_read_sctlr();
uint64_t phase2_mmu_descriptor_for(uint64_t virtual_address);

// Change permissions on identity-mapped application pages after the loader
// has copied the PT_LOAD contents.  The Phase-2 table remains the common
// identity map; only the leaf AP/PXN/UXN bits are changed here.
#ifdef __cplusplus
extern "C" {
#endif
uint8_t phase2_mmu_set_range_permissions(uint64_t base, uint64_t size,
                                         uint8_t executable, uint8_t writable);
#ifdef __cplusplus
}
#endif
