// Itanium (IA-64) Graphics Backend
//
// Provides platform-specific graphics initialisation for IA-64:
//
//   1. EFI GOP (Graphics Output Protocol) framebuffer
//      The EFI firmware / bootloader pre-configures a framebuffer
//      and passes the base address, resolution, and pixel format
//      via EFI system table or a boot parameter block.
//
//   2. PCI VGA MMIO probe
//      On real Itanium servers with PCI VGA cards, the linear
//      framebuffer can be discovered via PCI config space (SAL or
//      MMIO-mapped configuration).
//
//   3. ski simulator fallback
//      The HP ski simulator has no graphics hardware.  In that
//      environment the driver reports no framebuffer and the
//      kernel falls back to the ski serial console.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ia64 {
namespace graphics {

// ================================================================
// EFI GOP framebuffer descriptor
//
// Filled in by the EFI stub bootloader before jumping to the
// kernel.  On the ski simulator all fields are zero.
// ================================================================

struct EfiGopInfo {
    uint64_t framebufferBase;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;             // bytes per scan line
    uint32_t pixelFormat;       // 0 = RGBX, 1 = BGRX, 2 = bitmask
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
};

// Initialise the IA-64 graphics subsystem.
// Attempts to use EFI GOP info, then falls back to PCI VGA probe.
// Returns true if a usable framebuffer was found.
bool init();

// Initialise from an EFI GOP descriptor (passed from bootloader).
bool init_from_gop(const EfiGopInfo* gopInfo);

// Return true if a framebuffer is available.
bool is_available();

// Get framebuffer properties.
uint64_t get_lfb_address();
uint32_t get_width();
uint32_t get_height();
uint32_t get_pitch();
uint8_t  get_bpp();

} // namespace graphics
} // namespace ia64
} // namespace arch
} // namespace kernel
