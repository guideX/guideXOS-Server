#pragma once

#include <stdint.h>

namespace kernel {
namespace boot {

static const uint32_t kCommonBootInfoMagic = UINT32_C(0x49425847); // GXBI
static const uint16_t kCommonBootInfoVersion = 1;
static const uint32_t kMaxMemoryRanges = 8;
static const uint32_t kMaxMmioRanges = 8;

struct Range {
    uint64_t base;
    uint64_t size;
};

enum CommonFramebufferFormat : uint32_t {
    kFramebufferFormatUnknown = 0,
    kFramebufferFormatR8G8B8A8 = 1,
    kFramebufferFormatB8G8R8A8 = 2,
};

struct CommonFramebufferInfo {
    uint64_t base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    uint32_t format;
#if defined(GXOS_AARCH64_PHASE6)
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
#endif
};

// Architecture-neutral data consumed after the architecture handoff has
// been validated.  All pointers are physical addresses while the early ARM64
// identity map is active; the common layer never depends on UEFI types.
struct CommonBootInfo {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t flags;
    uint32_t architecture;
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t bootstrap_stack_base;
    uint64_t bootstrap_stack_size;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_descriptor_size;
    uint64_t memory_map_entry_count;
    uint64_t handoff_base;
    uint64_t handoff_size;
    uint64_t ramdisk_base;
    uint64_t ramdisk_size;
    uint64_t dtb_base;
    uint64_t dtb_size;
    uint64_t acpi_rsdp;
    Range memory_ranges[kMaxMemoryRanges];
    uint32_t memory_range_count;
    Range mmio_ranges[kMaxMmioRanges];
    uint32_t mmio_range_count;
    CommonFramebufferInfo framebuffer;
};

static const uint32_t kArchitectureArm64 = 1;
static const uint32_t kBootFlagMemoryMap = 1u << 0;
static const uint32_t kBootFlagRamdisk = 1u << 1;
static const uint32_t kBootFlagDtb = 1u << 2;
static const uint32_t kBootFlagFramebuffer = 1u << 3;

bool validate(const CommonBootInfo* info);

} // namespace boot
} // namespace kernel
