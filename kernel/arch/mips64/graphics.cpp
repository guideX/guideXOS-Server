//
// MIPS64 Graphics Implementation (Stub)
//
// Provides framebuffer graphics for MIPS64 platforms.
// Currently a stub implementation for initial kernel compile.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/serial_console.h"

namespace kernel {
namespace arch {
namespace mips64 {
namespace graphics {

namespace {

// Global framebuffer state
static FramebufferInfo s_info = {
    0,          // base
    0,          // width
    0,          // height
    0,          // pitch
    0,          // bpp
    FB_NONE,    // type
    false       // available
};

} // anonymous namespace

// ================================================================
// Public interface
// ================================================================

void init()
{
    serial_console::puts("[Graphics] MIPS64 graphics init (stub)\n");
    
    // TODO: Probe for Cirrus Logic VGA on PCI bus
    // For now, just mark as unavailable
    s_info.available = false;
    s_info.type = FB_NONE;
    
    serial_console::puts("[Graphics] No framebuffer detected\n");
}

bool is_available()
{
    return s_info.available;
}

const FramebufferInfo* get_info()
{
    return &s_info;
}

void* get_framebuffer()
{
    if (!s_info.available) return nullptr;
    
    // Convert physical address to virtual (kseg1 uncached)
    // 0xFFFFFFFF_A0000000 | phys_addr
    return reinterpret_cast<void*>(0xFFFFFFFFA0000000ULL | s_info.base);
}

void set_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!s_info.available) return;
    if (x >= s_info.width || y >= s_info.height) return;
    
    uint8_t* fb = static_cast<uint8_t*>(get_framebuffer());
    if (!fb) return;
    
    uint64_t offset = y * s_info.pitch + x * (s_info.bpp / 8);
    
    switch (s_info.bpp) {
        case 32:
            *reinterpret_cast<uint32_t*>(fb + offset) = color;
            break;
        case 24:
            fb[offset + 0] = color & 0xFF;         // Blue
            fb[offset + 1] = (color >> 8) & 0xFF;  // Green
            fb[offset + 2] = (color >> 16) & 0xFF; // Red
            break;
        case 16:
            *reinterpret_cast<uint16_t*>(fb + offset) = static_cast<uint16_t>(color);
            break;
        default:
            break;
    }
}

void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (!s_info.available) return;
    
    for (uint32_t dy = 0; dy < height; ++dy) {
        for (uint32_t dx = 0; dx < width; ++dx) {
            set_pixel(x + dx, y + dy, color);
        }
    }
}

void clear_screen(uint32_t color)
{
    if (!s_info.available) return;
    fill_rect(0, 0, s_info.width, s_info.height, color);
}

void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg)
{
    (void)x; (void)y; (void)c; (void)fg; (void)bg;
    // Stub - no built-in font yet
}

void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg)
{
    (void)x; (void)y; (void)str; (void)fg; (void)bg;
    // Stub - no built-in font yet
}

} // namespace graphics
} // namespace mips64
} // namespace arch
} // namespace kernel
