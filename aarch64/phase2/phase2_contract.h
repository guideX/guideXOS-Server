#pragma once

// AARCH64-2 firmware-to-kernel contract.  The common Phase 1 fields retain
// their fixed-width layout and meaning; the appended fields make the DTB
// lifetime explicit after ExitBootServices.

#include <stdint.h>

#define GXOS_AARCH64_PHASE2_HANDOFF_MAGIC UINT64_C(0x32414C4C484F5347)
#define GXOS_AARCH64_PHASE2_HANDOFF_VERSION UINT32_C(2)
#define GXOS_AARCH64_PHASE2_KERNEL_LOAD_ADDRESS UINT64_C(0x40000000)
#define GXOS_AARCH64_PHASE2_UART_FALLBACK UINT64_C(0x09000000)

#define GXOS_AARCH64_PHASE2_FLAG_EBS_COMPLETE UINT32_C(1 << 0)
#define GXOS_AARCH64_PHASE2_FLAG_IDENTITY_LOAD UINT32_C(1 << 1)
#define GXOS_AARCH64_PHASE2_FLAG_MMU_OFF_ON_ENTRY UINT32_C(1 << 2)
#define GXOS_AARCH64_PHASE2_FLAG_STACK_ALLOCATED UINT32_C(1 << 3)
#define GXOS_AARCH64_PHASE2_FLAG_MEMORY_MAP_VALID UINT32_C(1 << 4)
#define GXOS_AARCH64_PHASE2_FLAG_DTB_VALID UINT32_C(1 << 5)
#define GXOS_AARCH64_PHASE2_FLAG_DTB_COPIED UINT32_C(1 << 6)

#pragma pack(push, 1)
typedef struct gxos_aarch64_phase2_handoff {
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
} gxos_aarch64_phase2_handoff;
#pragma pack(pop)
