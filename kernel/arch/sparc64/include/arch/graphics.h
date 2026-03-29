// SPARC v9 (UltraSPARC / Sun4u) Graphics Backend
//
// Provides platform-specific graphics for SPARC v9 Sun4u machines:
//
//   1. PCI VGA / ATI PGX framebuffer
//      QEMU sun4u with -vga std provides a standard PCI VGA device.
//      The linear framebuffer is at PCI BAR0 (typically 0x80000000).
//      QEMU also supports the ATI Rage PGX card.
//
//   2. Creator3D / Elite3D (UPA bus) — future
//      High-end Sun framebuffers connected via UPA.
//      Not emulated by QEMU; stub only.
//
// PCI configuration is accessed through the psycho/sabre PCI host
// bridge MMIO registers at 0x1FE00000000.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc64 {
namespace graphics {

// ================================================================
// Sun4u PCI VGA device descriptor
// ================================================================

struct PciVgaInfo {
    bool     found;
    uint16_t vendorId;
    uint16_t deviceId;
    uint64_t bar0;           // linear framebuffer
    uint64_t bar2;           // MMIO registers (if applicable)
};

// ================================================================
// Well-known Sun4u PCI addresses
// ================================================================

// Psycho/Sabre PCI configuration space base
static const uint64_t PCI_CONFIG_BASE = 0x1FE01000000ULL;

// Default framebuffer address (QEMU sun4u -vga std BAR0)
static const uint64_t DEFAULT_LFB     = 0x80000000ULL;

// ================================================================
// Public API
// ================================================================

// Initialise the SPARC v9 graphics subsystem.
// Probes PCI for a VGA-class device.
bool init();

// Return true if a framebuffer is available.
bool is_available();

// Probe PCI for a VGA device.
bool probe_pci_vga(PciVgaInfo* out);

// Get framebuffer properties.
uint64_t get_lfb_address();
uint32_t get_width();
uint32_t get_height();
uint32_t get_pitch();
uint8_t  get_bpp();

// Set a video mode (only possible on BGA-like PCI VGA adapters).
bool set_mode(uint16_t width, uint16_t height, uint8_t bpp);

} // namespace graphics
} // namespace sparc64
} // namespace arch
} // namespace kernel
