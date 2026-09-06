#pragma once

#include <stdint.h>

#define GXOS_AARCH64_PHASE2_MAX_RAM_RANGES 8

typedef struct gxos_aarch64_phase2_range {
    uint64_t base;
    uint64_t size;
} gxos_aarch64_phase2_range;

typedef struct gxos_aarch64_phase2_platform {
    uint8_t valid;
    uint8_t gic_version;
    uint8_t address_cells;
    uint8_t size_cells;
    uint32_t timer_irq;
    uint32_t timer_source; // 0=none, 1=virtual, 2=physical
    uint64_t uart_base;
    uint64_t uart_size;
    uint64_t gicd_base;
    uint64_t gicd_size;
    uint64_t gicc_base;
    uint64_t gicc_size;
    gxos_aarch64_phase2_range ram[GXOS_AARCH64_PHASE2_MAX_RAM_RANGES];
    uint32_t ram_count;
} gxos_aarch64_phase2_platform;

#ifdef __cplusplus
extern "C" {
#endif

uint8_t gxos_aarch64_phase2_parse_dtb(const void* blob, uint64_t blob_size,
                                      gxos_aarch64_phase2_platform* platform);

#ifdef __cplusplus
}
#endif
