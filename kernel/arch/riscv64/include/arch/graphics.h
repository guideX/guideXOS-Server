//
// Provides platform-specific graphics initialisation for RISC-V 64:
//
//   1. QEMU ramfb (RAM framebuffer)
//      A simple firmware-provided framebuffer device.  QEMU
//      presents it as an fw_cfg item; the guest writes geometry
//      and a physical address, and QEMU displays the contents.
//      Enabled with: -device ramfb
//
//   2. PCI VGA MMIO probe
//      On RISC-V virt with a PCI VGA/bochs-display device the
//      linear framebuffer can be discovered via PCI config space
//      (ECAM MMIO at 0x30000000 on QEMU virt).
//
//   3. VirtIO GPU (future)
//      The QEMU virt machine supports virtio-gpu-device.  This
//      requires a VirtIO transport driver and is not yet
//      implemented.
//
// All RISC-V framebuffers are memory-mapped.  There is no VGA
// text mode and no port I/O.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace riscv64 {
namespace graphics {

// ================================================================
// RISC-V display device types
// ================================================================

enum FramebufferType : uint8_t {
    FB_NONE    = 0,
    FB_RAMFB   = 1,    // QEMU ramfb (fw_cfg device)
    FB_PCI_VGA = 2,    // PCI VGA / bochs-display
};

// ================================================================
// QEMU fw_cfg interface addresses (RISC-V virt machine)
//
// fw_cfg is a simple QEMU paravirtualised configuration device.
// On the virt machine the MMIO interface is at 0x10100000.
//
// ramfb configuration is written via fw_cfg DMA.
// ================================================================

static const uint64_t FW_CFG_BASE     = 0x10100000ULL;
static const uint64_t FW_CFG_DATA     = FW_CFG_BASE + 0x00;
static const uint64_t FW_CFG_SEL      = FW_CFG_BASE + 0x08;
static const uint64_t FW_CFG_DMA      = FW_CFG_BASE + 0x10;

// fw_cfg selector keys
static const uint16_t FW_CFG_SIGNATURE = 0x0000;
static const uint16_t FW_CFG_FILE_DIR  = 0x0019;

// ================================================================
// PCI ECAM base address (QEMU RISC-V virt machine)
// ================================================================

static const uint64_t PCI_ECAM_BASE = 0x30000000ULL;

// ================================================================
// ramfb configuration structure (big-endian on wire)
// ================================================================

struct RamfbConfig {
    uint64_t addr;      // physical address of framebuffer (BE)
    uint32_t fourcc;    // pixel format fourcc code (BE)
    uint32_t flags;     // flags (BE)
    uint32_t width;     // width in pixels (BE)
    uint32_t height;    // height in pixels (BE)
    uint32_t stride;    // bytes per scan line (BE)
};

// ================================================================
// Public API
// ================================================================

// Probe for available RISC-V display devices.
// Returns the type of framebuffer detected, or FB_NONE.
FramebufferType probe();

// Initialise the detected display device and set a default mode.
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

} // namespace graphics
} // namespace riscv64
} // namespace arch
} // namespace kernel
