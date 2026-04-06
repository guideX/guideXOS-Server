//
// MIPS64 Graphics Interface
//
// Provides platform-specific graphics initialization for MIPS64:
//
//   1. Cirrus Logic GD5446 (QEMU malta default VGA)
//      The malta board emulates a Cirrus Logic VGA card.
//      Linear framebuffer at PCI BAR0.
//
//   2. VirtIO GPU (QEMU virt machine)
//      Requires VirtIO transport driver (not yet implemented).
//
// MIPS framebuffers are memory-mapped. There is no VGA text mode
// port I/O on MIPS platforms.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace mips64 {
namespace graphics {

// ================================================================
// MIPS64 display device types
// ================================================================

enum FramebufferType : uint8_t {
    FB_NONE       = 0,
    FB_CIRRUS     = 1,    // Cirrus Logic GD5446 (QEMU malta)
    FB_VIRTIO_GPU = 2,    // VirtIO GPU
    FB_BOCHS      = 3,    // Bochs/QEMU standard VGA
};

// ================================================================
// PCI configuration space addresses
//
// QEMU malta PCI MMIO:
//   PCI config space at 0x1BE00000 (type 0)
//   PCI I/O space at 0x18000000
//   PCI memory space at 0x10000000
// ================================================================

static const uint64_t PCI_CONFIG_BASE_MALTA = 0x1BE00000ULL;
static const uint64_t PCI_IO_BASE_MALTA     = 0x18000000ULL;
static const uint64_t PCI_MEM_BASE_MALTA    = 0x10000000ULL;

// ================================================================
// Framebuffer information
// ================================================================

struct FramebufferInfo {
    uint64_t        base;       // Physical base address of framebuffer
    uint32_t        width;      // Width in pixels
    uint32_t        height;     // Height in pixels
    uint32_t        pitch;      // Bytes per scanline
    uint8_t         bpp;        // Bits per pixel
    FramebufferType type;       // Device type
    bool            available;  // Is framebuffer available?
};

// ================================================================
// Graphics functions
// ================================================================

// Initialize graphics subsystem
void init();

// Check if framebuffer is available
bool is_available();

// Get framebuffer information
const FramebufferInfo* get_info();

// Get pointer to linear framebuffer
void* get_framebuffer();

// Set a pixel color
void set_pixel(uint32_t x, uint32_t y, uint32_t color);

// Fill rectangle with color
void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

// Clear screen with color
void clear_screen(uint32_t color);

// Draw character at position (using built-in font)
void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);

// Draw string at position
void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);

} // namespace graphics
} // namespace mips64
} // namespace arch
} // namespace kernel
