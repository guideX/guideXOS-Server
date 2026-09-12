#pragma once

// Samsung Galaxy Tab A8 platform-side vocabulary only.  This header deliberately
// contains no MMIO constants: those must come from the verified stock DTB/DTBO
// selected for a particular SM-X200 firmware build.

#include <stdint.h>

#define GXOS_AARCH64_TAB_A8_PLATFORM_NAME "SamsungGalaxyTabA8"
#define GXOS_AARCH64_TAB_A8_EXPECTED_MODEL "SM-X200"
#define GXOS_AARCH64_TAB_A8_EXPECTED_DEVICE "gta8wifi"
#define GXOS_AARCH64_TAB_A8_EXPECTED_SOC "UMS512 / T618 / sharkl5pro"

typedef enum gxos_aarch64_tab_a8_evidence_class {
    GXOS_AARCH64_TAB_A8_EVIDENCE_UNKNOWN = 0,
    GXOS_AARCH64_TAB_A8_EVIDENCE_STOCK_PROVEN = 1,
    GXOS_AARCH64_TAB_A8_EVIDENCE_SOURCE_PROVEN = 2,
    GXOS_AARCH64_TAB_A8_EVIDENCE_COMMUNITY_DERIVED = 3,
    GXOS_AARCH64_TAB_A8_EVIDENCE_SOC_FAMILY_INFERENCE = 4
} gxos_aarch64_tab_a8_evidence_class;

typedef enum gxos_aarch64_tab_a8_value_state {
    GXOS_AARCH64_TAB_A8_VALUE_UNKNOWN = 0,
    GXOS_AARCH64_TAB_A8_VALUE_PRESENT = 1,
    GXOS_AARCH64_TAB_A8_VALUE_CONFLICTING = 2
} gxos_aarch64_tab_a8_value_state;

// A loader will eventually populate this from an Android-compatible handoff.
// The structure is intentionally board-neutral at the field level and carries
// only evidence-backed addresses supplied by the selected device tree.
typedef struct gxos_aarch64_tab_a8_platform_map {
    uint32_t size;
    uint32_t version;
    uint32_t evidence;
    uint32_t value_state;
    uint64_t dtb_base;
    uint64_t dtb_size;
    uint64_t ram_base;
    uint64_t ram_size;
    uint64_t gicd_base;
    uint64_t gicr_base;
    uint64_t uart_base;
    uint32_t uart_irq;
    uint32_t timer_physical_irq;
    uint32_t timer_virtual_irq;
    uint32_t timer_frequency_hz;
    uint64_t internal_storage_base;
    uint32_t internal_storage_irq;
    uint64_t usb_base;
    uint32_t usb_irq;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t flags;
} gxos_aarch64_tab_a8_platform_map;

#define GXOS_AARCH64_TAB_A8_MAP_FLAG_DTB_VALID (1u << 0)
#define GXOS_AARCH64_TAB_A8_MAP_FLAG_RAM_VALID (1u << 1)
#define GXOS_AARCH64_TAB_A8_MAP_FLAG_GIC_VALID (1u << 2)
#define GXOS_AARCH64_TAB_A8_MAP_FLAG_UART_VALID (1u << 3)
#define GXOS_AARCH64_TAB_A8_MAP_FLAG_TIMER_VALID (1u << 4)
#define GXOS_AARCH64_TAB_A8_MAP_FLAG_STORAGE_VALID (1u << 5)
#define GXOS_AARCH64_TAB_A8_MAP_FLAG_USB_VALID (1u << 6)
#define GXOS_AARCH64_TAB_A8_MAP_FLAG_FRAMEBUFFER_VALID (1u << 7)

static inline uint8_t gxos_aarch64_tab_a8_identity_matches(const char* model,
                                                            const char* device)
{
    if (!model || !device) return 0;
    const char* expectedModel = GXOS_AARCH64_TAB_A8_EXPECTED_MODEL;
    const char* expectedDevice = GXOS_AARCH64_TAB_A8_EXPECTED_DEVICE;
    uint32_t i = 0;
    for (; expectedModel[i] != 0 || model[i] != 0; ++i) {
        if (model[i] != expectedModel[i]) return 0;
    }
    for (i = 0; expectedDevice[i] != 0 || device[i] != 0; ++i) {
        if (device[i] != expectedDevice[i]) return 0;
    }
    return 1;
}
