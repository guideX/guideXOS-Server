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
#include "include/kernel/process.h"
#include "include/kernel/desktop.h"
#include "include/kernel/interrupts.h"
#include "include/kernel/ps2mouse.h"

#if ARCH_HAS_PIC_8259
#include "include/kernel/multiboot.h"
// Include BootInfo structure from bootloader (x86 / amd64 UEFI only)
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"
#endif

extern "C" void kernel_main(void* boot_environment, uint32_t boot_magic)
{
#if ARCH_HAS_PIC_8259
    // ============================================================
    // x86 / amd64 boot path  —  Multiboot (BIOS) or BootInfo (UEFI)
    // ============================================================

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
        
        // Set up IDT, remap PIC, enable interrupts
        kernel::interrupts::init();
        
        // Initialize PS/2 mouse driver and register IRQ12 handler
        kernel::ps2mouse::init(kernel::framebuffer::get_width(),
                               kernel::framebuffer::get_height());
        kernel::interrupts::register_irq(12, kernel::ps2mouse::irq_handler);
        
        // Draw initial cursor at center of screen
        kernel::desktop::draw_cursor(kernel::ps2mouse::get_x(),
                                     kernel::ps2mouse::get_y());
        
        // Main kernel loop — poll mouse state and redraw cursor
        while (1) {
            if (kernel::ps2mouse::is_dirty()) {
                kernel::ps2mouse::clear_dirty();
                kernel::desktop::handle_mouse(
                    kernel::ps2mouse::get_x(),
                    kernel::ps2mouse::get_y(),
                    kernel::ps2mouse::get_buttons());
            }
            // Halt CPU until next interrupt (saves power)
            kernel::arch::halt();
        }
    }
    else {
        // === TEXT MODE FALLBACK ===
        kernel::vga::init();
        kernel::vga::print_colored("guideXOS Kernel\n", kernel::vga::Color::LightCyan, kernel::vga::Color::Black);
        kernel::vga::print("Framebuffer not available - text mode only\n");
        
        while (1) { }
    }

#else
    // ============================================================
    // Non-x86 boot path  (SPARC, IA-64, ARM, ...)
    // ============================================================
    (void)boot_environment;
    (void)boot_magic;

    // Initialize architecture-specific hardware
    kernel::interrupts::init();

    // TODO: platform-specific framebuffer discovery goes here.
    //       For SPARC Sun4m the framebuffer is memory-mapped at an
    //       address provided by OpenBoot PROM (OBP).

    // Halt loop until drivers are wired up
    while (1) {
        kernel::arch::halt();
    }
#endif // ARCH_HAS_PIC_8259
}


