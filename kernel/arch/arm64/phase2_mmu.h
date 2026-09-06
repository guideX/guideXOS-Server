#pragma once

#include <stdint.h>

#include "../../../aarch64/phase2/phase2_platform.h"

#define GXOS_AARCH64_PHASE2_MMU_GRANULE UINT64_C(4096)
#define GXOS_AARCH64_PHASE2_MMU_VA_BITS 48
#define GXOS_AARCH64_PHASE2_MMU_PA_BITS 40

uint8_t phase2_mmu_build(const gxos_aarch64_phase2_platform* platform,
                         uint64_t kernel_base, uint64_t kernel_size);
void phase2_mmu_enable();
uint64_t phase2_mmu_tables_start();
uint64_t phase2_mmu_tables_end();
uint64_t phase2_mmu_root();
uint64_t phase2_mmu_read_mair();
uint64_t phase2_mmu_read_tcr();
uint64_t phase2_mmu_read_ttbr0();
uint64_t phase2_mmu_read_sctlr();
uint64_t phase2_mmu_descriptor_for(uint64_t virtual_address);
