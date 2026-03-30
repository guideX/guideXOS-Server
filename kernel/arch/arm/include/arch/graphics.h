//
// Provides platform-specific graphics initialisation for ARM:
//
//   1. PL111 CLCD (Colour LCD Controller)
//      The PL111 is the standard display controller on ARM
//      Versatile, RealView, and Integrator boards.
//      QEMU versatilepb maps it at 0x10120000.
//      Supports 1-24 bpp, hardware cursor, and DMA.
//
//   2. HDLCD (High Definition LCD Controller)
//      Found on ARM Juno and some Cortex-A platforms.
//      Supports higher resolutions and 32-bit colour.
//
//   3. Versatile/Express static framebuffer
//      Some board configurations provide a pre-configured
//      framebuffer at a well-known MMIO address.
//
// All ARM framebuffers are memory-mapped.  There is no VGA text
// mode and no port I/O.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm {
namespace graphics {

// ================================================================
// ARM display controller types
// ================================================================

enum FramebufferType : uint8_t {
    FB_NONE   = 0,
    FB_PL111  = 1,     // PL111 CLCD (Versatile/RealView)
    FB_HDLCD  = 2,     // HDLCD (Juno/newer)
};

// ================================================================
// PL111 CLCD well-known MMIO addresses
//
// QEMU versatilepb maps the PL111 at 0x10120000.
// The framebuffer memory is at a configurable DMA address.
// ================================================================

static const uint32_t PL111_BASE       = 0x10120000u;
static const uint32_t PL111_SIZE       = 0x00001000u;

// PL111 register offsets
static const uint32_t CLCD_TIM0        = 0x000;  // Horizontal timing
static const uint32_t CLCD_TIM1        = 0x004;  // Vertical timing
static const uint32_t CLCD_TIM2        = 0x008;  // Clock and signal polarity
static const uint32_t CLCD_TIM3        = 0x00C;  // Line end control
static const uint32_t CLCD_UPBASE      = 0x010;  // Upper panel base address
static const uint32_t CLCD_LPBASE      = 0x014;  // Lower panel base address
static const uint32_t CLCD_CTRL        = 0x018;  // Control register
static const uint32_t CLCD_IMSC        = 0x01C;  // Interrupt mask
static const uint32_t CLCD_RIS         = 0x020;  // Raw interrupt status
static const uint32_t CLCD_MIS         = 0x024;  // Masked interrupt status
static const uint32_t CLCD_ICR         = 0x028;  // Interrupt clear
static const uint32_t CLCD_UCUR        = 0x02C;  // Upper panel current addr
static const uint32_t CLCD_LCUR        = 0x030;  // Lower panel current addr
static const uint32_t CLCD_PERIPHID0   = 0xFE0;  // Peripheral ID 0

// CLCD_CTRL bits
static const uint32_t CLCD_CTRL_EN     = (1u << 0);   // LCD enable
static const uint32_t CLCD_CTRL_BPP1   = (0u << 1);   // 1 bpp
static const uint32_t CLCD_CTRL_BPP2   = (1u << 1);   // 2 bpp
static const uint32_t CLCD_CTRL_BPP4   = (2u << 1);   // 4 bpp
static const uint32_t CLCD_CTRL_BPP8   = (3u << 1);   // 8 bpp
static const uint32_t CLCD_CTRL_BPP16  = (4u << 1);   // 16 bpp (5:6:5)
static const uint32_t CLCD_CTRL_BPP24  = (5u << 1);   // 24 bpp (packed)
static const uint32_t CLCD_CTRL_TFT    = (1u << 5);   // TFT panel select
static const uint32_t CLCD_CTRL_BGR    = (1u << 8);   // BGR pixel order
static const uint32_t CLCD_CTRL_POWER  = (1u << 11);  // LCD power enable
static const uint32_t CLCD_CTRL_WATERMARK = (1u << 16); // DMA FIFO watermark

// ================================================================
// Versatile system registers (for PL111 configuration)
// ================================================================

static const uint32_t VERSATILE_SYS_BASE   = 0x10000000u;
static const uint32_t SYS_CLCD             = 0x50;  // CLCD control register

// ================================================================
// Default framebuffer DMA address (QEMU versatilepb)
// ================================================================

static const uint32_t PL111_FB_DMA_BASE = 0x01000000u;

// ================================================================
// Public API
// ================================================================

// Probe for available ARM display controllers.
// Returns the type of framebuffer detected, or FB_NONE.
FramebufferType probe();

// Initialise the detected display controller and set a default
// graphics mode.
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

// Set a specific display mode.
bool set_mode(uint16_t width, uint16_t height, uint8_t bpp);

// ================================================================
// CLUT / palette for 8-bit indexed mode
// ================================================================

// Set a single palette entry.  r, g, b are 8-bit (0-255).
void set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

// Load a full 256-entry palette (768 bytes: 256 * RGB).
void load_palette(const uint8_t* data);

// Load the default VGA-like 256-colour palette.
void load_default_palette();

} // namespace graphics
} // namespace arm
} // namespace arch
} // namespace kernel
