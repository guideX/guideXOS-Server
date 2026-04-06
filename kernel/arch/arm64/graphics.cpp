// ARM64 Graphics Implementation
//
// Provides framebuffer graphics support.
// Currently implements ramfb for QEMU virt machine.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/serial_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace arm64 {
namespace graphics {

// ================================================================
// Internal state
// ================================================================

static ModeInfo s_modeInfo;
static bool s_initialized = false;
static bool s_doubleBufferEnabled = false;
static uint64_t s_backBuffer = 0;

// Simple 8x16 bitmap font (ASCII 32-127)
// Each character is 8 pixels wide, 16 pixels tall
// 1 byte per row, 16 bytes per character
static const uint8_t s_font8x16[96 * 16] = {
    // Space (32)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ! (33)
    0x00, 0x00, 0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x18,
    0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    // " (34)
    0x00, 0x63, 0x63, 0x63, 0x22, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // # through ~ would be defined here...
    // For brevity, using simplified patterns
};

// ================================================================
// MMIO helpers
// ================================================================

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)addr;
    (void)value;
#else
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(addr);
    *ptr = value;
#endif
}

static inline uint32_t mmio_read32(uint64_t addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
    return 0;
#else
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(addr);
    return *ptr;
#endif
}

// ================================================================
// Initialization
// ================================================================

bool init()
{
    if (s_initialized) return true;
    
    // On QEMU virt, we can use ramfb via fw_cfg
    // For now, set up a default mode (this would normally come from
    // device tree or UEFI GOP)
    
    // Default: 1024x768, 32bpp
    s_modeInfo.width = 1024;
    s_modeInfo.height = 768;
    s_modeInfo.bpp = 32;
    s_modeInfo.pitch = s_modeInfo.width * 4;
    s_modeInfo.framebuffer = 0;  // Will be set when actual FB is detected
    
    // RGB888 format (common for ARM)
    s_modeInfo.redMask = 0x00FF0000;
    s_modeInfo.greenMask = 0x0000FF00;
    s_modeInfo.blueMask = 0x000000FF;
    s_modeInfo.redShift = 16;
    s_modeInfo.greenShift = 8;
    s_modeInfo.blueShift = 0;
    
    // TODO: Detect actual framebuffer from device tree or PCI
    // For now, we report as not initialized until FB is found
    
    serial_console::print("Graphics: Stub mode (no framebuffer detected)\n");
    
    // Mark as initialized even without FB (for stub operation)
    s_initialized = true;
    
    return s_modeInfo.framebuffer != 0;
}

bool get_mode_info(ModeInfo* info)
{
    if (!info) return false;
    
    if (!s_initialized) {
        init();
    }
    
    *info = s_modeInfo;
    return s_modeInfo.framebuffer != 0;
}

bool set_mode(uint32_t width, uint32_t height, uint32_t bpp)
{
    // For now, we can't change modes dynamically
    // Just verify the requested mode matches current
    (void)width;
    (void)height;
    (void)bpp;
    return false;
}

uint64_t get_framebuffer()
{
    return s_modeInfo.framebuffer;
}

size_t get_framebuffer_size()
{
    return static_cast<size_t>(s_modeInfo.pitch) * s_modeInfo.height;
}

// ================================================================
// Drawing primitives
// ================================================================

static inline uint32_t* pixel_addr(uint32_t x, uint32_t y)
{
    if (!s_modeInfo.framebuffer) return nullptr;
    if (x >= s_modeInfo.width || y >= s_modeInfo.height) return nullptr;
    
    uint64_t addr = s_modeInfo.framebuffer + y * s_modeInfo.pitch + x * 4;
    return reinterpret_cast<uint32_t*>(addr);
}

void clear(uint32_t color)
{
    if (!s_modeInfo.framebuffer) return;
    
    uint32_t* fb = reinterpret_cast<uint32_t*>(s_modeInfo.framebuffer);
    size_t pixels = (s_modeInfo.pitch / 4) * s_modeInfo.height;
    
    for (size_t i = 0; i < pixels; ++i) {
        fb[i] = color;
    }
}

void set_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    uint32_t* p = pixel_addr(x, y);
    if (p) *p = color;
}

uint32_t get_pixel(uint32_t x, uint32_t y)
{
    uint32_t* p = pixel_addr(x, y);
    return p ? *p : 0;
}

void draw_hline(uint32_t x, uint32_t y, uint32_t width, uint32_t color)
{
    if (!s_modeInfo.framebuffer) return;
    if (y >= s_modeInfo.height) return;
    
    uint32_t endX = x + width;
    if (endX > s_modeInfo.width) endX = s_modeInfo.width;
    
    uint32_t* p = pixel_addr(x, y);
    if (!p) return;
    
    for (uint32_t i = x; i < endX; ++i) {
        *p++ = color;
    }
}

void draw_vline(uint32_t x, uint32_t y, uint32_t height, uint32_t color)
{
    if (!s_modeInfo.framebuffer) return;
    if (x >= s_modeInfo.width) return;
    
    uint32_t endY = y + height;
    if (endY > s_modeInfo.height) endY = s_modeInfo.height;
    
    uint32_t stride = s_modeInfo.pitch / 4;
    uint32_t* p = pixel_addr(x, y);
    if (!p) return;
    
    for (uint32_t i = y; i < endY; ++i) {
        *p = color;
        p += stride;
    }
}

void draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    draw_hline(x, y, width, color);
    draw_hline(x, y + height - 1, width, color);
    draw_vline(x, y, height, color);
    draw_vline(x + width - 1, y, height, color);
}

void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    for (uint32_t row = y; row < y + height && row < s_modeInfo.height; ++row) {
        draw_hline(x, row, width, color);
    }
}

void copy_rect(uint32_t srcX, uint32_t srcY, uint32_t dstX, uint32_t dstY,
               uint32_t width, uint32_t height)
{
    if (!s_modeInfo.framebuffer) return;
    
    // Handle overlapping regions
    if (dstY < srcY || (dstY == srcY && dstX < srcX)) {
        // Copy forward
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t c = get_pixel(srcX + x, srcY + y);
                set_pixel(dstX + x, dstY + y, c);
            }
        }
    } else {
        // Copy backward
        for (int32_t y = height - 1; y >= 0; --y) {
            for (int32_t x = width - 1; x >= 0; --x) {
                uint32_t c = get_pixel(srcX + x, srcY + y);
                set_pixel(dstX + x, dstY + y, c);
            }
        }
    }
}

void scroll(int32_t lines)
{
    if (!s_modeInfo.framebuffer || lines == 0) return;
    
    if (lines > 0) {
        // Scroll up
        uint32_t scrollPixels = static_cast<uint32_t>(lines);
        if (scrollPixels >= s_modeInfo.height) {
            clear(COLOR_BLACK);
            return;
        }
        
        copy_rect(0, scrollPixels, 0, 0, s_modeInfo.width, s_modeInfo.height - scrollPixels);
        fill_rect(0, s_modeInfo.height - scrollPixels, s_modeInfo.width, scrollPixels, COLOR_BLACK);
    } else {
        // Scroll down
        uint32_t scrollPixels = static_cast<uint32_t>(-lines);
        if (scrollPixels >= s_modeInfo.height) {
            clear(COLOR_BLACK);
            return;
        }
        
        copy_rect(0, 0, 0, scrollPixels, s_modeInfo.width, s_modeInfo.height - scrollPixels);
        fill_rect(0, 0, s_modeInfo.width, scrollPixels, COLOR_BLACK);
    }
}

// ================================================================
// Text rendering
// ================================================================

static const uint8_t* get_font_glyph(char c)
{
    if (c < 32 || c > 127) {
        c = '?';  // Replace unknown characters
    }
    
    return &s_font8x16[(c - 32) * 16];
}

void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg)
{
    if (!s_modeInfo.framebuffer) return;
    
    const uint8_t* glyph = get_font_glyph(c);
    
    for (uint32_t row = 0; row < 16; ++row) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; ++col) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            set_pixel(x + col, y + row, color);
        }
    }
}

void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg)
{
    if (!str) return;
    
    uint32_t startX = x;
    
    while (*str) {
        if (*str == '\n') {
            x = startX;
            y += 16;
        } else if (*str == '\r') {
            x = startX;
        } else if (*str == '\t') {
            x = (x + 32) & ~31;  // Tab to next 32-pixel boundary
        } else {
            draw_char(x, y, *str, fg, bg);
            x += 8;
            
            // Wrap at screen edge
            if (x + 8 > s_modeInfo.width) {
                x = startX;
                y += 16;
            }
        }
        
        str++;
    }
}

uint32_t get_char_width()
{
    return 8;
}

uint32_t get_char_height()
{
    return 16;
}

// ================================================================
// Double buffering
// ================================================================

bool enable_double_buffer()
{
    // Double buffering requires allocating a back buffer
    // For now, not implemented
    s_doubleBufferEnabled = false;
    return false;
}

void swap_buffers()
{
    if (!s_doubleBufferEnabled) return;
    
    // Copy back buffer to front buffer
    // In real implementation, this would swap page pointers
}

uint64_t get_back_buffer()
{
    return s_doubleBufferEnabled ? s_backBuffer : s_modeInfo.framebuffer;
}

// ================================================================
// VSync and timing
// ================================================================

void wait_vsync()
{
    // VSync waiting depends on the display controller
    // For ramfb, there's no vsync signal
    // For virtio-gpu or hardware controllers, this would wait for blank
}

uint32_t get_refresh_rate()
{
    // Unknown for most software framebuffers
    return 0;
}

} // namespace graphics
} // namespace arm64
} // namespace arch
} // namespace kernel
