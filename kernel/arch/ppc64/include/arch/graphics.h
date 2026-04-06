//
// PowerPC64 Graphics Interface
//
// Provides platform-specific graphics initialisation for PowerPC64:
//
//   1. PCI VGA MMIO probe
//      On QEMU pseries with a PCI VGA/bochs-display device the
//      linear framebuffer can be discovered via PCI config space.
//
//   2. VirtIO GPU (future)
//      The pseries machine supports virtio-gpu-pci.  This requires
//      a VirtIO transport driver and is not yet implemented.
//
// All PowerPC64 framebuffers are memory-mapped.  There is no VGA
// text mode in the traditional sense on pseries.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ppc64 {
namespace graphics {

// ================================================================
// PowerPC64 display device types
// ================================================================

enum FramebufferType : uint8_t {
    FB_NONE    = 0,
    FB_PCI_VGA = 1,    // PCI VGA / bochs-display
    FB_VIRTIO  = 2,    // VirtIO GPU (future)
};

// ================================================================
// Framebuffer information
// ================================================================

bool is_available();
FramebufferType get_type();
uint64_t get_lfb_address();
uint32_t get_width();
uint32_t get_height();
uint32_t get_pitch();
uint8_t  get_bpp();

// ================================================================
// Initialization and mode setting
// ================================================================

void init();
bool set_mode(uint32_t width, uint32_t height, uint8_t bpp);

// ================================================================
// Pixel operations
// ================================================================

void put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t get_pixel(uint32_t x, uint32_t y);
void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void clear_screen(uint32_t color);

} // namespace graphics
} // namespace ppc64
} // namespace arch
} // namespace kernel
