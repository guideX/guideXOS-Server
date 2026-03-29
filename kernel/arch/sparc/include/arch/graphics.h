// SPARC v8 (Sun4m) Graphics Backend
//
// Provides platform-specific graphics for SPARC v8 Sun4m machines:
//
//   1. TCX framebuffer (primary — already in framebuffer.cpp)
//      The 8-bit / 24-bit TCX is the standard Sun4m display.
//      QEMU SS-5 maps the 24-bit plane at 0x50800000.
//
//   2. CG3 framebuffer (GX — 8-bit, 1152x900)
//      Older SBus framebuffer at 0x4000000 + slot offset.
//      8-bit indexed colour with BT458 RAMDAC.
//
//   3. CG6 framebuffer (GX / Turbo-GX — 8-bit accelerated)
//      SBus framebuffer with hardware acceleration.
//
// All Sun4m framebuffers are memory-mapped via SBus.  There is
// no VGA text mode, no PCI, and no port I/O.  The colour lookup
// table (CLUT) is programmed through the RAMDAC registers.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc {
namespace graphics {

// ================================================================
// Sun4m framebuffer types
// ================================================================

enum FramebufferType : uint8_t {
    FB_NONE = 0,
    FB_TCX  = 1,     // TCX 8/24-bit (QEMU default)
    FB_CG3  = 2,     // CG3 8-bit
    FB_CG6  = 3,     // CG6 8-bit accelerated
};

// ================================================================
// Well-known Sun4m MMIO addresses
//
// These are the standard SBus slot addresses for Sun4m.
// QEMU -machine SS-5 uses these addresses.
// ================================================================

// TCX 24-bit direct-colour plane
static const uint32_t TCX_FB24_BASE  = 0x50800000u;
// TCX 8-bit indexed plane
static const uint32_t TCX_FB8_BASE   = 0x50000000u;
// TCX TEC (transform engine control)
static const uint32_t TCX_TEC_BASE   = 0x50700000u;
// TCX THC (TEC hardware cursor / control)
static const uint32_t TCX_THC_BASE   = 0x50701000u;

// CG3 framebuffer (SBus slot 3 default)
static const uint32_t CG3_FB_BASE    = 0x54800000u;
// CG3 RAMDAC (Brooktree BT458)
static const uint32_t CG3_DAC_BASE   = 0x54200000u;

// ================================================================
// BT458 RAMDAC registers (used by CG3 / CG6)
// ================================================================

static const uint32_t BT458_ADDR_WRITE  = 0x00;
static const uint32_t BT458_COLOR_DATA  = 0x04;
static const uint32_t BT458_PIXEL_MASK  = 0x08;
static const uint32_t BT458_ADDR_READ   = 0x0C;

// ================================================================
// Public API
// ================================================================

// Probe for available Sun4m framebuffers.
// Returns the type of framebuffer detected, or FB_NONE.
FramebufferType probe();

// Initialise the detected framebuffer (set default resolution,
// program CLUT for 8-bit modes).
bool init();

// Return the detected framebuffer type.
FramebufferType get_type();

// Return true if a framebuffer is available.
bool is_available();

// Get framebuffer properties.
uint32_t get_lfb_address();
uint32_t get_width();
uint32_t get_height();
uint32_t get_pitch();
uint8_t  get_bpp();

// ================================================================
// CLUT / palette for 8-bit indexed modes (CG3, CG6, TCX 8-bit)
// ================================================================

// Set a single CLUT entry.  r, g, b are 8-bit (0-255).
void set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

// Load a full 256-entry CLUT (768 bytes: 256 * RGB).
void load_palette(const uint8_t* data);

// Load the default VGA-like 256-colour palette.
void load_default_palette();

} // namespace graphics
} // namespace sparc
} // namespace arch
} // namespace kernel
