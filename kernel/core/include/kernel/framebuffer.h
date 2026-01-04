#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include "types.h"

// Forward declaration for BootInfo
namespace guideXOS {
    struct BootInfo;
}

namespace kernel {
namespace framebuffer {

// Initialize framebuffer from multiboot info (legacy BIOS)
bool init(void* multiboot_info);

// Initialize framebuffer from BootInfo (UEFI)
bool init_from_bootinfo(const guideXOS::BootInfo* bootinfo);

// Get framebuffer dimensions
uint32_t get_width();
uint32_t get_height();
uint32_t get_pitch();
uint8_t get_bpp();

// Get framebuffer pointer
uint32_t* get_buffer();

// Check if framebuffer is available
bool is_available();

// Clear screen to color
void clear(uint32_t color);

// Draw a pixel
void put_pixel(uint32_t x, uint32_t y, uint32_t color);

// Get a pixel
uint32_t get_pixel(uint32_t x, uint32_t y);

// Draw a filled rectangle
void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

// Draw a line
void draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);

// Copy a buffer to screen
void blit(uint32_t* buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

} // namespace framebuffer
} // namespace kernel

#endif // KERNEL_FRAMEBUFFER_H
