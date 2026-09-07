#pragma once

// AARCH64-4 handoff.  The prefix intentionally preserves the Phase-2
// firmware fields so the architecture foundation and its validation helpers
// remain usable during the migration to the common BootInfo contract.

#include <stdint.h>

#define GXOS_AARCH64_PHASE4_HANDOFF_MAGIC UINT64_C(0x34414c4c484f5347)
#define GXOS_AARCH64_PHASE4_HANDOFF_VERSION UINT32_C(4)
#define GXOS_AARCH64_PHASE4_KERNEL_LOAD_ADDRESS UINT64_C(0x40000000)
#define GXOS_AARCH64_PHASE4_UART_FALLBACK UINT64_C(0x09000000)

#define GXOS_AARCH64_PHASE4_FLAG_EBS_COMPLETE UINT32_C(1 << 0)
#define GXOS_AARCH64_PHASE4_FLAG_IDENTITY_LOAD UINT32_C(1 << 1)
#define GXOS_AARCH64_PHASE4_FLAG_MMU_OFF_ON_ENTRY UINT32_C(1 << 2)
#define GXOS_AARCH64_PHASE4_FLAG_STACK_ALLOCATED UINT32_C(1 << 3)
#define GXOS_AARCH64_PHASE4_FLAG_MEMORY_MAP_VALID UINT32_C(1 << 4)
#define GXOS_AARCH64_PHASE4_FLAG_DTB_VALID UINT32_C(1 << 5)
#define GXOS_AARCH64_PHASE4_FLAG_DTB_COPIED UINT32_C(1 << 6)
#define GXOS_AARCH64_PHASE4_FLAG_RAMDISK_VALID UINT32_C(1 << 7)

#pragma pack(push, 1)
typedef struct gxos_aarch64_phase4_handoff {
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
} gxos_aarch64_phase4_handoff;
#pragma pack(pop)
