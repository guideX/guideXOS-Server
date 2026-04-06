// ARM64 Graphics Interface
//
// Provides framebuffer graphics support for ARM64 platforms.
// Supports:
//   - QEMU ramfb (simple framebuffer via fw_cfg)
//   - virtio-gpu (VirtIO GPU device)
//   - PL111 LCD controller (ARM reference platforms)
//   - Device Tree configured framebuffers
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm64 {
namespace graphics {

// ================================================================
// Graphics mode information
// ================================================================

struct ModeInfo {
    uint32_t width;           // Horizontal resolution
    uint32_t height;          // Vertical resolution
    uint32_t pitch;           // Bytes per scanline
    uint32_t bpp;             // Bits per pixel (16, 24, 32)
    uint64_t framebuffer;     // Physical address of framebuffer
    uint32_t redMask;         // Red color mask
    uint32_t greenMask;       // Green color mask
    uint32_t blueMask;        // Blue color mask
    uint32_t redShift;        // Red bit position
    uint32_t greenShift;      // Green bit position
    uint32_t blueShift;       // Blue bit position
};

// ================================================================
// Color formats
// ================================================================

// Common color format: RGB888 (32-bit with padding)
static const uint32_t FORMAT_RGB888  = 0;
// Common color format: BGR888 (32-bit with padding)
static const uint32_t FORMAT_BGR888  = 1;
// RGB565 (16-bit)
static const uint32_t FORMAT_RGB565  = 2;
// RGBA8888 (32-bit with alpha)
static const uint32_t FORMAT_RGBA8888 = 3;
// BGRA8888 (32-bit with alpha)
static const uint32_t FORMAT_BGRA8888 = 4;

// ================================================================
// Graphics functions
// ================================================================

// Initialize graphics subsystem
// Returns true if a display device was found and initialized
bool init();

// Get current mode information
bool get_mode_info(ModeInfo* info);

// Set graphics mode (if supported by device)
bool set_mode(uint32_t width, uint32_t height, uint32_t bpp);

// Get framebuffer base address
uint64_t get_framebuffer();

// Get framebuffer size in bytes
size_t get_framebuffer_size();

// ================================================================
// Drawing primitives
// ================================================================

// Clear screen with specified color
void clear(uint32_t color);

// Set a single pixel
void set_pixel(uint32_t x, uint32_t y, uint32_t color);

// Get a single pixel
uint32_t get_pixel(uint32_t x, uint32_t y);

// Draw a horizontal line
void draw_hline(uint32_t x, uint32_t y, uint32_t width, uint32_t color);

// Draw a vertical line
void draw_vline(uint32_t x, uint32_t y, uint32_t height, uint32_t color);

// Draw a rectangle outline
void draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

// Fill a rectangle
void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

// Copy a region of the framebuffer
void copy_rect(uint32_t srcX, uint32_t srcY, uint32_t dstX, uint32_t dstY,
               uint32_t width, uint32_t height);

// Scroll the display vertically
void scroll(int32_t lines);

// ================================================================
// Text rendering (basic bitmap font)
// ================================================================

// Draw a single character at position
void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);

// Draw a string at position
void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);

// Get character cell dimensions
uint32_t get_char_width();
uint32_t get_char_height();

// ================================================================
// Double buffering (if supported)
// ================================================================

// Enable double buffering
bool enable_double_buffer();

// Swap front and back buffers
void swap_buffers();

// Get back buffer address (for drawing)
uint64_t get_back_buffer();

// ================================================================
// VSync and timing
// ================================================================

// Wait for vertical blank
void wait_vsync();

// Get refresh rate in Hz (0 if unknown)
uint32_t get_refresh_rate();

// ================================================================
// Color utilities
// ================================================================

// Create a color value from RGB components
inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

// Create a color value from RGBA components
inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

// Common colors
static const uint32_t COLOR_BLACK   = 0x000000;
static const uint32_t COLOR_WHITE   = 0xFFFFFF;
static const uint32_t COLOR_RED     = 0xFF0000;
static const uint32_t COLOR_GREEN   = 0x00FF00;
static const uint32_t COLOR_BLUE    = 0x0000FF;
static const uint32_t COLOR_YELLOW  = 0xFFFF00;
static const uint32_t COLOR_CYAN    = 0x00FFFF;
static const uint32_t COLOR_MAGENTA = 0xFF00FF;
static const uint32_t COLOR_GRAY    = 0x808080;
static const uint32_t COLOR_DARK_GRAY = 0x404040;
static const uint32_t COLOR_LIGHT_GRAY = 0xC0C0C0;

} // namespace graphics
} // namespace arm64
} // namespace arch
} // namespace kernel
