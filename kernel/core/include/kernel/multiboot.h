#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include "types.h"

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif

namespace kernel {
namespace multiboot {

// Multiboot header flags
constexpr uint32_t HEADER_MAGIC = 0x1BADB002;
constexpr uint32_t HEADER_FLAGS = 0x00000007; // Request memory map, modules, and framebuffer
constexpr uint32_t HEADER_CHECKSUM = 0u - (HEADER_MAGIC + HEADER_FLAGS);

// Multiboot info flags
constexpr uint32_t INFO_MEMORY = 0x00000001;
constexpr uint32_t INFO_BOOTDEV = 0x00000002;
constexpr uint32_t INFO_CMDLINE = 0x00000004;
constexpr uint32_t INFO_MODS = 0x00000008;
constexpr uint32_t INFO_FRAMEBUFFER = 0x00001000;

// Framebuffer types
constexpr uint8_t FRAMEBUFFER_TYPE_INDEXED = 0;
constexpr uint8_t FRAMEBUFFER_TYPE_RGB = 1;
constexpr uint8_t FRAMEBUFFER_TYPE_EGA_TEXT = 2;

// Multiboot information structure
struct Info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    union {
        struct {
            uint32_t palette_addr;
            uint16_t palette_num_colors;
        };
        struct {
            uint8_t red_field_position;
            uint8_t red_mask_size;
            uint8_t green_field_position;
            uint8_t green_mask_size;
            uint8_t blue_field_position;
            uint8_t blue_mask_size;
        };
    };
} 
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
;

// Module structure
struct Module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t reserved;
} 
#if !defined(_MSC_VER)
__attribute__((packed))
#endif
;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif

} // namespace multiboot
} // namespace kernel

#endif // KERNEL_MULTIBOOT_H
