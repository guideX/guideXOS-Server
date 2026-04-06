//
// LoongArch 64-bit Graphics Backend
//
// Provides platform-specific graphics initialization for LoongArch64:
//
//   1. QEMU ramfb (RAM framebuffer)
//      A simple firmware-provided framebuffer device via fw_cfg.
//      The guest writes geometry and a physical address, and QEMU
//      displays the contents.
//      Enabled with: -device ramfb
//
//   2. PCI VGA MMIO probe
//      On LoongArch virt with a PCI VGA/bochs-display device, the
//      linear framebuffer can be discovered via PCI config space
//      (ECAM MMIO on QEMU virt).
//
//   3. Loongson integrated GPU (future)
//      Real Loongson hardware has integrated display controllers
//      (DC) that require specific driver support.
//
//   4. VirtIO GPU (future)
//      QEMU supports virtio-gpu-device for virtualized graphics.
//
// All LoongArch framebuffers are memory-mapped. There is no VGA
// text mode and no x86-style port I/O.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace graphics {

// ================================================================
// LoongArch display device types
// ================================================================

enum FramebufferType : uint8_t {
    FB_NONE      = 0,
    FB_RAMFB    = 1,    // QEMU ramfb (fw_cfg device)
    FB_PCI_VGA  = 2,    // PCI VGA / bochs-display
    FB_LOONGSON = 3,    // Loongson integrated display controller
};

// ================================================================
// QEMU fw_cfg interface addresses (LoongArch virt machine)
//
// fw_cfg is a simple QEMU paravirtualised configuration device.
// On the LoongArch virt machine, the MMIO interface is typically
// at 0x1e020000 (may vary by QEMU version/config).
//
// ramfb configuration is written via fw_cfg DMA.
// ================================================================

// QEMU LoongArch virt fw_cfg addresses (check device tree for actual values)
static const uint64_t FW_CFG_BASE     = 0x1E020000ULL;
static const uint64_t FW_CFG_DATA     = FW_CFG_BASE + 0x00;
static const uint64_t FW_CFG_SEL      = FW_CFG_BASE + 0x08;
static const uint64_t FW_CFG_DMA      = FW_CFG_BASE + 0x10;

// fw_cfg selector keys
static const uint16_t FW_CFG_SIGNATURE = 0x0000;
static const uint16_t FW_CFG_FILE_DIR  = 0x0019;

// ================================================================
// PCI ECAM base address (QEMU LoongArch virt machine)
//
// PCI configuration space on LoongArch virt uses ECAM (Enhanced
// Configuration Access Mechanism) with MMIO access.
// ================================================================

// QEMU LoongArch virt PCI ECAM (check device tree for actual base)
static const uint64_t PCI_ECAM_BASE = 0x20000000ULL;

// PCI MMIO region for BARs
static const uint64_t PCI_MMIO_BASE = 0x40000000ULL;
static const uint64_t PCI_MMIO_SIZE = 0x40000000ULL;

// ================================================================
// Loongson Display Controller (DC) registers
//
// On real Loongson hardware (3A5000, 3A6000), the integrated
// display controller provides framebuffer output.
// ================================================================

// Loongson DC base address (varies by SoC, typical value)
static const uint64_t LOONGSON_DC_BASE = 0x0ULL;  // TODO: Get from device tree

// DC register offsets (example, actual values depend on SoC)
static const uint32_t DC_FB_BASE_LO  = 0x00;
static const uint32_t DC_FB_BASE_HI  = 0x04;
static const uint32_t DC_WIDTH       = 0x08;
static const uint32_t DC_HEIGHT      = 0x0C;
static const uint32_t DC_PITCH       = 0x10;
static const uint32_t DC_FORMAT      = 0x14;
static const uint32_t DC_ENABLE      = 0x18;

// ================================================================
// ramfb configuration structure (big-endian on wire)
// ================================================================

struct RamfbConfig {
    uint64_t addr;      // Physical address of framebuffer (BE)
    uint32_t fourcc;    // Pixel format fourcc code (BE)
    uint32_t flags;     // Flags (BE)
    uint32_t width;     // Width in pixels (BE)
    uint32_t height;    // Height in pixels (BE)
    uint32_t stride;    // Bytes per scan line (BE)
};

// ================================================================
// Public API
// ================================================================

// Probe for available LoongArch display devices.
// Returns the type of framebuffer detected, or FB_NONE.
FramebufferType probe();

// Initialize the detected display device and set a default mode.
// Returns true if a usable framebuffer was found.
bool init();

// Return the detected framebuffer type.
FramebufferType get_type();

// Return true if a framebuffer is available.
bool is_available();

// Get framebuffer properties.
uint64_t get_lfb_address();
uint32_t get_width();
uint32_t get_height();
uint32_t get_pitch();
uint8_t  get_bpp();

// Draw a single pixel (for testing/debugging).
void put_pixel(uint32_t x, uint32_t y, uint32_t color);

// Fill rectangle with color.
void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

// Clear screen to color.
void clear_screen(uint32_t color);

} // namespace graphics
} // namespace loongarch64
} // namespace arch
} // namespace kernel
