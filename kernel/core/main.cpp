//
// guideXOS Minimal Kernel - Entry Point
//
// ROLE: Bridge between bootloader and guideXOS Server (user-mode)
//
// Copyright (c) 2024 guideX
//

#include "include/kernel/version.h"
#include "include/kernel/arch.h"
#include "include/kernel/vga.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/multiboot.h"
#include "include/kernel/process.h"
#include "include/kernel/desktop.h"

// Include BootInfo structure from bootloader
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"

extern "C" void kernel_main(void* boot_environment, uint32_t boot_magic)
{
    // Support both Multiboot (legacy) and BootInfo (UEFI) boot
    bool is_multiboot = (boot_magic == 0x2BADB002);
    bool is_bootinfo = false;
    
    guideXOS::BootInfo* bootinfo = nullptr;
    void* multiboot_info = nullptr;
    
    if (is_multiboot) {
        multiboot_info = boot_environment;
    } else {
        bootinfo = static_cast<guideXOS::BootInfo*>(boot_environment);
        if (bootinfo && bootinfo->Magic == guideXOS::GUIDEXOS_BOOTINFO_MAGIC) {
            is_bootinfo = true;
        }
    }
    
    // If neither boot method is valid, halt
    if (!is_multiboot && !is_bootinfo) {
        while(1) { }
    }
    
    // Initialize framebuffer for graphics mode
    bool has_fb = false;
    
    if (is_bootinfo) {
        has_fb = kernel::framebuffer::init_from_bootinfo(bootinfo);
    } else {
        has_fb = kernel::framebuffer::init(multiboot_info);
    }
    
    if (has_fb) {
        // === GRAPHICS MODE BOOT ===
        
        // Clear screen to dark color
        kernel::framebuffer::clear(0xFF101828);
        
        // Initialize desktop and draw immediately (skip boot splash)
        kernel::desktop::init();
        kernel::desktop::draw();
        
        // Main kernel loop - idle
        while (1) {
            // Halt CPU until next interrupt (saves power)
            #if defined(_MSC_VER)
            __nop();
            #else
            asm volatile ("hlt");
            #endif
        }
    }
    else {
        // === TEXT MODE FALLBACK ===
        kernel::vga::init();
        kernel::vga::print_colored("guideXOS Kernel\n", kernel::vga::Color::LightCyan, kernel::vga::Color::Black);
        kernel::vga::print("Framebuffer not available - text mode only\n");
        
        while (1) { }
    }
}

