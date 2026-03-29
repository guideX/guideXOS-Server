// x86 (32-bit) Graphics Backend
//
// Provides platform-specific graphics initialisation for x86:
//   - Bochs Graphics Adapter (BGA) detection and mode setting
//   - PCI VGA BAR0 linear framebuffer discovery
//   - VGA register programming (palette, DAC, CRTC)
//   - VBE mode info passthrough from Multiboot bootloader
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace arch {
namespace x86 {
namespace graphics {

// Initialise the graphics subsystem.
bool init();

// Return true if a Bochs VBE (BGA) adapter was detected.
bool has_bga();

// Return true if any PCI VGA device was found.
bool has_pci_vga();

// Get the physical address of the linear framebuffer.
uint32_t get_lfb_address();

// Set a graphics mode using BGA registers.
bool set_mode(uint16_t width, uint16_t height, uint8_t bpp);

// VGA DAC palette helpers
void set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void load_palette(const uint8_t* data);

// ================================================================
// VGA standard register access
// ================================================================

// Read/write VGA miscellaneous output register (port 0x3CC / 0x3C2)
uint8_t read_misc_output();
void write_misc_output(uint8_t val);

// Read/write VGA sequencer registers
uint8_t read_sequencer(uint8_t index);
void write_sequencer(uint8_t index, uint8_t val);

// Read/write VGA CRTC registers (port 0x3D4/0x3D5)
uint8_t read_crtc(uint8_t index);
void write_crtc(uint8_t index, uint8_t val);

// Read/write VGA graphics controller registers (port 0x3CE/0x3CF)
uint8_t read_graphics(uint8_t index);
void write_graphics(uint8_t index, uint8_t val);

// Read/write VGA attribute controller registers (port 0x3C0)
uint8_t read_attribute(uint8_t index);
void write_attribute(uint8_t index, uint8_t val);

} // namespace graphics
} // namespace x86
} // namespace arch
} // namespace kernel
