#pragma once

// AARCH64-6 firmware-to-kernel contract.  The first fields intentionally
// retain the AARCH64-4 handoff layout so the common loader choreography and
// the Phase-5 kernel foundation remain unchanged.  The framebuffer fields are
// captured before ExitBootServices and are the only GOP data consumed by the
// kernel after firmware services have stopped.

#include <stdint.h>

#define GXOS_AARCH64_PHASE6_HANDOFF_MAGIC UINT64_C(0x36414c4c484f5347)
#define GXOS_AARCH64_PHASE6_HANDOFF_VERSION UINT32_C(6)
#define GXOS_AARCH64_PHASE6_KERNEL_LOAD_ADDRESS UINT64_C(0x40000000)
#define GXOS_AARCH64_PHASE6_UART_FALLBACK UINT64_C(0x09000000)

#define GXOS_AARCH64_PHASE6_FLAG_EBS_COMPLETE UINT32_C(1 << 0)
#define GXOS_AARCH64_PHASE6_FLAG_IDENTITY_LOAD UINT32_C(1 << 1)
#define GXOS_AARCH64_PHASE6_FLAG_MMU_OFF_ON_ENTRY UINT32_C(1 << 2)
#define GXOS_AARCH64_PHASE6_FLAG_STACK_ALLOCATED UINT32_C(1 << 3)
#define GXOS_AARCH64_PHASE6_FLAG_MEMORY_MAP_VALID UINT32_C(1 << 4)
#define GXOS_AARCH64_PHASE6_FLAG_DTB_VALID UINT32_C(1 << 5)
#define GXOS_AARCH64_PHASE6_FLAG_DTB_COPIED UINT32_C(1 << 6)
#define GXOS_AARCH64_PHASE6_FLAG_RAMDISK_VALID UINT32_C(1 << 7)
#define GXOS_AARCH64_PHASE6_FLAG_FRAMEBUFFER_VALID UINT32_C(1 << 8)

#define GXOS_AARCH64_PHASE6_PIXEL_FORMAT_UNKNOWN UINT32_C(0)
#define GXOS_AARCH64_PHASE6_PIXEL_FORMAT_R8G8B8A8 UINT32_C(1)
#define GXOS_AARCH64_PHASE6_PIXEL_FORMAT_B8G8R8A8 UINT32_C(2)

#pragma pack(push, 1)
typedef struct gxos_aarch64_phase6_handoff {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t flags;
    uint32_t reserved;
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t kernel_entry;
    uint64_t stack_base;
    uint64_t stack_size;
    uint64_t stack_top;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_descriptor_size;
    uint64_t memory_map_entry_count;
    uint64_t initial_current_el;
    uint64_t loader_sctlr_el1;
    uint64_t uart_base;
    uint64_t dtb_base;
    uint64_t dtb_size;
    uint64_t ramdisk_base;
    uint64_t ramdisk_size;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    uint32_t framebuffer_format;
    uint32_t framebuffer_red_mask;
    uint32_t framebuffer_green_mask;
    uint32_t framebuffer_blue_mask;
    uint32_t framebuffer_reserved_mask;
} gxos_aarch64_phase6_handoff;
#pragma pack(pop)
