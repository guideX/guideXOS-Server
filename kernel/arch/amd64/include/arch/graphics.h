// AMD64 Graphics Backend
//
// Provides platform-specific graphics initialisation for AMD64:
//   - Bochs Graphics Adapter (BGA) detection and mode setting
//   - PCI VGA BAR0 linear framebuffer discovery
//   - VGA register programming (palette, DAC, CRTC)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace arch {
namespace amd64 {
namespace graphics {

// Initialise the graphics subsystem.
// Detects BGA, scans PCI for VGA, and configures the best available
// video mode for the kernel framebuffer.
bool init();

// Return true if a Bochs VBE (BGA) adapter was detected.
bool has_bga();

// Return true if any PCI VGA device was found.
bool has_pci_vga();

// Get the physical address of the linear framebuffer.
uint64_t get_lfb_address();

// Set a graphics mode using BGA registers.
// Returns true on success, false if BGA is not available.
bool set_mode(uint16_t width, uint16_t height, uint8_t bpp);

// ================================================================
// VGA DAC / palette helpers (port I/O at 0x3C6-0x3C9)
// ================================================================

// Write a single VGA DAC palette entry (index 0-255).
// r, g, b are 6-bit values (0-63).
void set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

// Load a full 256-entry palette.
// 'data' points to 768 bytes (256 * 3, each 6-bit).
void load_palette(const uint8_t* data);

} // namespace graphics
} // namespace amd64
} // namespace arch
} // namespace kernel
