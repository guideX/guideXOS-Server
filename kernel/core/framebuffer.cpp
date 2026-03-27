//
// Framebuffer driver implementation
//
// Supports both Multiboot (legacy BIOS) and BootInfo (UEFI) initialization
//
// Copyright (c) 2024 guideX
//

#include "include/kernel/framebuffer.h"
#include "include/kernel/arch.h"

#if ARCH_HAS_PIC_8259
#include "include/kernel/multiboot.h"
// Include BootInfo for UEFI boot support
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"
#endif

namespace kernel {
namespace framebuffer {

static uint32_t* g_buffer = nullptr;
static uint32_t g_width = 0;
static uint32_t g_height = 0;
static uint32_t g_pitch = 0;
static uint8_t g_bpp = 0;
static bool g_available = false;

#if ARCH_HAS_PIC_8259

bool init(void* multiboot_info_ptr)
{
    auto* info = reinterpret_cast<multiboot::Info*>(multiboot_info_ptr);
    
    // Check if framebuffer info is available
    if (!(info->flags & multiboot::INFO_FRAMEBUFFER)) {
        return false;
    }
    
    // Check framebuffer type (we want RGB)
    if (info->framebuffer_type != multiboot::FRAMEBUFFER_TYPE_RGB) {
        return false;
    }
    
    // Get framebuffer info
    g_buffer = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(info->framebuffer_addr));
    g_width = info->framebuffer_width;
    g_height = info->framebuffer_height;
    g_pitch = info->framebuffer_pitch;
    g_bpp = info->framebuffer_bpp;
    
    // Validate
    if (!g_buffer || g_width == 0 || g_height == 0) {
        return false;
    }
    
    g_available = true;
    return true;
}

bool init_from_bootinfo(const guideXOS::BootInfo* bootinfo)
{
    if (!bootinfo) {
        return false;
    }
    
    // Check if framebuffer is valid in BootInfo
    if (!(bootinfo->Flags & (1u << 1))) {
        return false;  // Framebuffer not available
    }
    
    // Validate framebuffer data
    if (bootinfo->FramebufferBase == 0 || bootinfo->FramebufferSize == 0) {
        return false;
    }
    
    if (bootinfo->FramebufferWidth == 0 || bootinfo->FramebufferHeight == 0) {
        return false;
    }
    
    // Initialize framebuffer from BootInfo
    g_buffer = reinterpret_cast<uint32_t*>(bootinfo->FramebufferBase);
    g_width = bootinfo->FramebufferWidth;
    g_height = bootinfo->FramebufferHeight;
    g_pitch = bootinfo->FramebufferPitch;
    g_bpp = 32;  // BootInfo always uses 32-bit format
    
    g_available = true;
    return true;
}

#else // !ARCH_HAS_PIC_8259

bool init(void*) { return false; }
bool init_from_bootinfo(const guideXOS::BootInfo*) { return false; }

#endif // ARCH_HAS_PIC_8259

uint32_t get_width()
{
    return g_width;
}

uint32_t get_height()
{
    return g_height;
}

uint32_t get_pitch()
{
    return g_pitch;
}

uint8_t get_bpp()
{
    return g_bpp;
}

uint32_t* get_buffer()
{
    return g_buffer;
}

bool is_available()
{
    return g_available;
}

void clear(uint32_t color)
{
    if (!g_available) return;
    
    uint32_t pixels = (g_pitch / 4) * g_height;
    for (uint32_t i = 0; i < pixels; i++) {
        g_buffer[i] = color;
    }
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_available || x >= g_width || y >= g_height) return;
    
    uint32_t offset = y * (g_pitch / 4) + x;
    g_buffer[offset] = color;
}

uint32_t get_pixel(uint32_t x, uint32_t y)
{
    if (!g_available || x >= g_width || y >= g_height) return 0;
    
    uint32_t offset = y * (g_pitch / 4) + x;
    return g_buffer[offset];
}

void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (!g_available) return;
    
    for (uint32_t dy = 0; dy < height; dy++) {
        for (uint32_t dx = 0; dx < width; dx++) {
            put_pixel(x + dx, y + dy, color);
        }
    }
}

void draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color)
{
    if (!g_available) return;
    
    // Bresenham's line algorithm
    int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        put_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void blit(uint32_t* buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!g_available || !buffer) return;
    
    for (uint32_t dy = 0; dy < height; dy++) {
        for (uint32_t dx = 0; dx < width; dx++) {
            uint32_t color = buffer[dy * width + dx];
            put_pixel(x + dx, y + dy, color);
        }
    }
}

} // namespace framebuffer
} // namespace kernel
