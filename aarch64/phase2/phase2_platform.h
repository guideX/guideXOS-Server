#pragma once

#include <stdint.h>
#include "phase2_validation.h"

#define GXOS_AARCH64_PHASE2_MAX_RAM_RANGES 8
#define GXOS_AARCH64_PHASE2_MAX_VIRTIO_MMIO 32
#define GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS 8
#define GXOS_AARCH64_PHASE2_MAX_CPU_AFFINITIES 8

#define GXOS_AARCH64_PLATFORM_UNKNOWN 0
#define GXOS_AARCH64_PLATFORM_QEMU_VIRT 1
#define GXOS_AARCH64_PLATFORM_RASPBERRY_PI4 2

#define GXOS_AARCH64_UART_UNKNOWN 0
#define GXOS_AARCH64_UART_PL011 1

#define GXOS_AARCH64_GIC_UNSUPPORTED 0
#define GXOS_AARCH64_GIC_V2 2
#define GXOS_AARCH64_GIC_V3 3

#define GXOS_AARCH64_GICV3_MIN_DISTRIBUTOR_SIZE UINT64_C(0x10000)
#define GXOS_AARCH64_GICV3_MIN_REDISTRIBUTOR_SIZE UINT64_C(0x20000)

typedef enum gxos_aarch64_dtb_status {
    GXOS_AARCH64_DTB_OK = 0,
    GXOS_AARCH64_DTB_INVALID_ARGUMENT = 1,
    GXOS_AARCH64_DTB_MALFORMED = 2,
    GXOS_AARCH64_DTB_UNSUPPORTED_CONTROLLER = 3,
    GXOS_AARCH64_DTB_DEVICE_UNAVAILABLE = 4,
    GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT = 5,
    GXOS_AARCH64_DTB_TIMER_UNAVAILABLE = 6
} gxos_aarch64_dtb_status;

typedef enum gxos_aarch64_dtb_parse_stage {
    GXOS_AARCH64_DTB_PARSE_STAGE_NONE = 0,
    GXOS_AARCH64_DTB_PARSE_STAGE_HEADER = 1,
    GXOS_AARCH64_DTB_PARSE_STAGE_STRUCTURE = 2,
    GXOS_AARCH64_DTB_PARSE_STAGE_PLATFORM = 3,
    GXOS_AARCH64_DTB_PARSE_STAGE_COMPLETE = 4
} gxos_aarch64_dtb_parse_stage;

typedef enum gxos_aarch64_dtb_parse_error {
    GXOS_AARCH64_DTB_PARSE_ERROR_NONE = 0,
    GXOS_AARCH64_DTB_PARSE_ERROR_TRUNCATED_TOKEN = 1,
    GXOS_AARCH64_DTB_PARSE_ERROR_NESTING = 2,
    GXOS_AARCH64_DTB_PARSE_ERROR_NODE_NAME = 3,
    GXOS_AARCH64_DTB_PARSE_ERROR_END_NODE = 4,
    GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_HEADER = 5,
    GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_VALUE = 6,
    GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_NAME = 7,
    GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_VALUE_FORMAT = 8,
    GXOS_AARCH64_DTB_PARSE_ERROR_REG_RANGE = 9,
    GXOS_AARCH64_DTB_PARSE_ERROR_CPU_AFFINITY = 10,
    GXOS_AARCH64_DTB_PARSE_ERROR_STRUCTURE_END = 11,
    GXOS_AARCH64_DTB_PARSE_ERROR_UNKNOWN_TOKEN = 12,
    GXOS_AARCH64_DTB_PARSE_ERROR_REQUIRED_PLATFORM = 13,
    GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER = 14,
    GXOS_AARCH64_DTB_PARSE_ERROR_TIMER = 15,
    GXOS_AARCH64_DTB_PARSE_ERROR_STRUCTURE = 16
} gxos_aarch64_dtb_parse_error;

typedef struct gxos_aarch64_phase2_range {
    uint64_t base;
    uint64_t size;
} gxos_aarch64_phase2_range;

typedef struct gxos_aarch64_phase2_virtio_mmio {
    uint64_t base;
    uint64_t size;
    uint32_t irq;
    uint32_t reserved;
} gxos_aarch64_phase2_virtio_mmio;

typedef struct gxos_aarch64_gicv3_region {
    uint64_t base;
    uint64_t size;
} gxos_aarch64_gicv3_region;

typedef struct gxos_aarch64_phase2_platform {
    uint8_t valid;
    uint8_t gic_version;
    uint8_t address_cells;
    uint8_t size_cells;
    uint8_t platform_kind;
    uint8_t uart_kind;
    uint8_t ranges_translated;
    uint8_t parser_last_token;
    uint8_t gic_node_seen;
    uint8_t gic_node_available;
    uint8_t unsupported_interrupt_controller_seen;
    uint8_t gic_has_redistributor_stride;
    uint8_t gic_has_redistributor_regions;
    uint8_t parser_offset_low[3]; // low 24 bits of the last FDT structure offset
    uint32_t parser_stage;
    uint32_t parser_error;
    uint32_t parser_offset;
    gxos_aarch64_fdt_header_status header_status;
    gxos_aarch64_fdt_header_info header;
    uint32_t diagnostic;
    uint32_t timer_irq;
    uint32_t timer_source; // 0=none, 1=virtual, 2=physical
    uint32_t gic_interrupt_cells;
    uint32_t gic_redistributor_stride;
    uint32_t gic_redistributor_region_count;
    uint32_t gic_cpu_affinity_count;
    uint64_t gic_cpu_affinity[GXOS_AARCH64_PHASE2_MAX_CPU_AFFINITIES];
    uint64_t uart_base;
    uint64_t uart_size;
    uint64_t gicd_base;
    uint64_t gicd_size;
    uint64_t gicc_base;
    uint64_t gicc_size;
    gxos_aarch64_gicv3_region gic_redistributor_regions[GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS];
    gxos_aarch64_phase2_range ram[GXOS_AARCH64_PHASE2_MAX_RAM_RANGES];
    uint32_t ram_count;
    gxos_aarch64_phase2_virtio_mmio virtio_mmio[GXOS_AARCH64_PHASE2_MAX_VIRTIO_MMIO];
    uint32_t virtio_mmio_count;
} gxos_aarch64_phase2_platform;

#ifdef __cplusplus
extern "C" {
#endif

uint8_t gxos_aarch64_phase2_parse_dtb(const void* blob, uint64_t blob_size,
                                      gxos_aarch64_phase2_platform* platform);
gxos_aarch64_dtb_status gxos_aarch64_phase2_parse_dtb_ex(
    const void* blob, uint64_t blob_size, gxos_aarch64_phase2_platform* platform);

#ifdef __cplusplus
}
#endif
