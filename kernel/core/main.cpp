//
// guideXOS Minimal Kernel - Entry Point
//
// ROLE: Bridge between bootloader and guideXOS Server (user-mode)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/version.h"
#include "include/kernel/arch.h"
#include "include/kernel/vga.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/process.h"
#include "include/kernel/desktop.h"
#include "include/kernel/interrupts.h"
#include "include/kernel/ps2mouse.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"

// Storage subsystem
#include "include/kernel/block_device.h"
#include "include/kernel/ata.h"
#include "include/kernel/nvme.h"
#include "include/kernel/ramdisk.h"
#include "include/kernel/vfs.h"
#include "include/kernel/fs_fat.h"
#include "include/kernel/fs_ext4.h"

#if ARCH_HAS_PIC_8259
#include "include/kernel/multiboot.h"
// Include BootInfo structure from bootloader (x86 / amd64 UEFI only)
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"
#endif

#if defined(ARCH_SPARC)
#include <arch/zs_serial.h>
#endif

#if defined(ARCH_SPARC64)
#include <arch/zs_serial.h>
#endif

#if defined(ARCH_IA64)
#include <arch/ski_console.h>
#endif

#if defined(ARCH_RISCV64)
#include <arch/sbi_console.h>
#include <arch/graphics.h>
#endif

extern "C" void kernel_main(void* boot_environment, uint32_t boot_magic)
{
#if ARCH_HAS_PIC_8259
    // ============================================================
    // x86 / amd64 boot path  —  Multiboot (BIOS) or BootInfo (UEFI)
    // ============================================================

    // Initialize serial debug output early
    kernel::serial::init();
    kernel::serial::puts("[KERNEL] guideXOS kernel_main entered\n");

    // Support both Multiboot (legacy) and BootInfo (UEFI) boot
    bool is_multiboot = (boot_magic == 0x2BADB002);
    bool is_bootinfo = false;
    
    guideXOS::BootInfo* bootinfo = nullptr;
    void* multiboot_info = nullptr;
    
    if (is_multiboot) {
        multiboot_info = boot_environment;
        kernel::serial::puts("[KERNEL] Boot method: Multiboot\n");
    } else {
        bootinfo = static_cast<guideXOS::BootInfo*>(boot_environment);
        if (bootinfo && bootinfo->Magic == guideXOS::GUIDEXOS_BOOTINFO_MAGIC) {
            is_bootinfo = true;
            kernel::serial::puts("[KERNEL] Boot method: UEFI BootInfo\n");
        }
    }
    
    // If neither boot method is valid, halt
    if (!is_multiboot && !is_bootinfo) {
        kernel::serial::puts("[KERNEL] ERROR: No valid boot method detected, halting\n");
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
        kernel::serial::puts("[KERNEL] Framebuffer initialized: ");
        kernel::serial::put_hex32(kernel::framebuffer::get_width());
        kernel::serial::putc('x');
        kernel::serial::put_hex32(kernel::framebuffer::get_height());
        kernel::serial::putc('\n');
        
        // Clear screen to dark color
        kernel::framebuffer::clear(0xFF101828);
        
        // Initialize desktop and draw immediately (skip boot splash)
        kernel::desktop::init();
        kernel::desktop::draw();
        kernel::serial::puts("[KERNEL] Desktop drawn\n");
        
        // Set up IDT, remap PIC, enable interrupts
        kernel::interrupts::init();
        kernel::serial::puts("[KERNEL] IDT + PIC initialized, interrupts enabled\n");
        
        // Initialize PIT timer for periodic IRQ0 (100 Hz heartbeat)
        // This ensures the CPU wakes from HLT regularly to poll input.
        kernel::pit::init(100);
        kernel::interrupts::register_irq(0, kernel::pit::irq_handler);
        kernel::serial::puts("[KERNEL] PIT timer initialized, IRQ0 registered\n");
        
        // ============================================================
        // Storage Subsystem Initialization
        // ============================================================
        kernel::serial::puts("[KERNEL] Initializing storage subsystem...\n");
        
        // Initialize block device layer
        kernel::block::init();
        kernel::serial::puts("[KERNEL] Block device layer initialized\n");
        
        // Initialize filesystem drivers
        kernel::fs_fat::init();
        kernel::fs_ext4::init();
        kernel::serial::puts("[KERNEL] Filesystem drivers initialized\n");
        
        // Initialize ATA/SATA driver (scans for IDE and AHCI controllers)
        kernel::ata::init();
        kernel::serial::puts("[KERNEL] ATA/SATA driver initialized, ");
        kernel::serial::put_hex32(kernel::ata::device_count());
        kernel::serial::puts(" drive(s) found\n");
        
        // Initialize NVMe driver (scans for NVMe controllers)
        kernel::nvme::init();
        kernel::serial::puts("[KERNEL] NVMe driver initialized, ");
        kernel::serial::put_hex32(kernel::nvme::device_count());
        kernel::serial::puts(" namespace(s) found\n");
        
        // Initialize RAM disk subsystem
        kernel::ramdisk::init();
        
        // Create a 4MB RAM disk for temporary storage / testing
        uint8_t ramdiskIdx = kernel::ramdisk::create(4 * 1024 * 1024, "ram0");
        if (ramdiskIdx != 0xFF) {
            kernel::serial::puts("[KERNEL] Created 4MB RAM disk 'ram0'\n");
        }
        
        // Initialize VFS layer
        kernel::vfs::init();
        kernel::serial::puts("[KERNEL] VFS layer initialized\n");
        
        // Report total block devices
        kernel::serial::puts("[KERNEL] Total block devices: ");
        kernel::serial::put_hex32(kernel::block::device_count());
        kernel::serial::putc('\n');
        
        // ============================================================
        
        // Initialize PS/2 mouse driver and register IRQ12 handler
        // (PS/2 is used as fallback when USB HID is not available)
        kernel::serial::puts("[KERNEL] Initializing PS/2 mouse...\n");
        kernel::ps2mouse::init(kernel::framebuffer::get_width(),
                               kernel::framebuffer::get_height());
        kernel::serial::puts("[KERNEL] PS/2 mouse init complete\n");
        kernel::interrupts::register_irq(12, kernel::ps2mouse::irq_handler);
        kernel::serial::puts("[KERNEL] IRQ12 handler registered and unmasked\n");
        
        // Initialize input manager (handles USB HID, PS/2, VirtIO fallback)
        kernel::serial::puts("[KERNEL] Initializing input manager...\n");
        kernel::input::init(kernel::framebuffer::get_width(),
                            kernel::framebuffer::get_height());
        kernel::serial::puts("[KERNEL] Input manager initialized\n");
        
        // Draw initial cursor at center of screen
        kernel::desktop::draw_cursor(kernel::input::mouse_x(),
                                     kernel::input::mouse_y());
        
        kernel::serial::puts("[KERNEL] Entering main loop (waiting for input)...\n");
        
        // Main kernel loop — poll input and redraw cursor
        while (1) {
            // Poll input manager for updates (handles USB HID polling)
            kernel::input::poll();
            
            if (kernel::input::mouse_dirty()) {
                kernel::input::mouse_clear_dirty();
                kernel::desktop::handle_mouse(
                    kernel::input::mouse_x(),
                    kernel::input::mouse_y(),
                    kernel::input::mouse_buttons());
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
    kernel::arch::init();

#if defined(ARCH_SPARC)
    // ---- SPARC Sun4m boot path ----

    // Discover the TCX framebuffer at its well-known MMIO address
    bool has_fb = kernel::framebuffer::init_sun4m();

    if (has_fb) {
        // Clear screen to dark colour
        kernel::framebuffer::clear(0xFF101828);

        // Initialize and draw the desktop
        kernel::desktop::init();
        kernel::desktop::draw();

        // Set up interrupts (SLAVIO already initialised in arch::init)
        kernel::interrupts::init();

        // Initialize the Z8530 serial keyboard/mouse driver
        kernel::arch::sparc::zs::init(kernel::framebuffer::get_width(),
                                       kernel::framebuffer::get_height());

        // Register the ZS IRQ handler on SLAVIO IRQ 12 (serial/SBus level 6)
        kernel::interrupts::register_irq(12, kernel::arch::sparc::zs::irq_handler);

        // Draw initial cursor at centre of screen
        kernel::desktop::draw_cursor(kernel::arch::sparc::zs::mouse_x(),
                                     kernel::arch::sparc::zs::mouse_y());

        // Main kernel loop — poll mouse state and redraw cursor
        while (1) {
            if (kernel::arch::sparc::zs::mouse_dirty()) {
                kernel::arch::sparc::zs::mouse_clear_dirty();
                kernel::desktop::handle_mouse(
                    kernel::arch::sparc::zs::mouse_x(),
                    kernel::arch::sparc::zs::mouse_y(),
                    kernel::arch::sparc::zs::mouse_buttons());
            }
            kernel::arch::halt();
        }
    }
#endif // ARCH_SPARC

#if defined(ARCH_SPARC64)
    // ---- SPARC v9 Sun4u boot path ----

    bool has_fb = kernel::framebuffer::init_sun4u();

    if (has_fb) {
        kernel::framebuffer::clear(0xFF101828);

        kernel::desktop::init();
        kernel::desktop::draw();

        kernel::interrupts::init();

        kernel::arch::sparc64::zs::init(kernel::framebuffer::get_width(),
                                         kernel::framebuffer::get_height());

        kernel::interrupts::register_irq(12, kernel::arch::sparc64::zs::irq_handler);

        kernel::desktop::draw_cursor(kernel::arch::sparc64::zs::mouse_x(),
                                     kernel::arch::sparc64::zs::mouse_y());

        while (1) {
            if (kernel::arch::sparc64::zs::mouse_dirty()) {
                kernel::arch::sparc64::zs::mouse_clear_dirty();
                kernel::desktop::handle_mouse(
                    kernel::arch::sparc64::zs::mouse_x(),
                    kernel::arch::sparc64::zs::mouse_y(),
                    kernel::arch::sparc64::zs::mouse_buttons());
            }
            kernel::arch::halt();
        }
    }
#endif // ARCH_SPARC64

#if defined(ARCH_IA64)
    // ---- IA-64 / ski simulator boot path ----

    // arch::init() already set up IVT, RSE, and ski console.
    // Print boot information to the firmware console.
    kernel::arch::ia64::ski_console::puts("\r\n");
    kernel::arch::ia64::ski_console::puts("========================================\r\n");
    kernel::arch::ia64::ski_console::puts("  guideXOS Server - Itanium (IA-64)\r\n");
    kernel::arch::ia64::ski_console::puts("  Running on HP ski simulator\r\n");
    kernel::arch::ia64::ski_console::puts("========================================\r\n");
    kernel::arch::ia64::ski_console::puts("\r\n");
    kernel::arch::ia64::ski_console::puts("Kernel loaded at 1 MB physical\r\n");
    kernel::arch::ia64::ski_console::puts("Architecture: ");
    kernel::arch::ia64::ski_console::puts(kernel::arch::get_arch_name());
    kernel::arch::ia64::ski_console::puts("\r\n");
    kernel::arch::ia64::ski_console::puts("\r\n");
    kernel::arch::ia64::ski_console::puts("Entering idle loop (Ctrl-C in ski to exit)\r\n");

    // Enable interrupts and idle
    kernel::interrupts::init();
    while (1) {
        kernel::arch::halt();
    }
#endif // ARCH_IA64

#if defined(ARCH_RISCV64)
    // ---- RISC-V 64 / QEMU virt boot path ----

    // arch::init() already set up SBI console.
    // Print boot information to the SBI serial console.
    kernel::arch::riscv64::sbi_console::puts("\r\n");
    kernel::arch::riscv64::sbi_console::puts("========================================\r\n");
    kernel::arch::riscv64::sbi_console::puts("  guideXOS Server - RISC-V 64 (RV64IMA)\r\n");
    kernel::arch::riscv64::sbi_console::puts("  Running on QEMU virt + OpenSBI\r\n");
    kernel::arch::riscv64::sbi_console::puts("========================================\r\n");
    kernel::arch::riscv64::sbi_console::puts("\r\n");
    kernel::arch::riscv64::sbi_console::puts("Kernel loaded at 0x80200000\r\n");
    kernel::arch::riscv64::sbi_console::puts("Architecture: ");
    kernel::arch::riscv64::sbi_console::puts(kernel::arch::get_arch_name());
    kernel::arch::riscv64::sbi_console::puts("\r\n");
    kernel::arch::riscv64::sbi_console::puts("\r\n");

    // Try to initialise ramfb / PCI VGA graphics
    kernel::arch::riscv64::sbi_console::puts("Probing graphics...\r\n");
    bool has_fb = kernel::arch::riscv64::graphics::init();

    if (has_fb) {
        kernel::arch::riscv64::sbi_console::puts("Framebuffer found: ");
        kernel::arch::riscv64::sbi_console::put_hex(
            kernel::arch::riscv64::graphics::get_lfb_address());
        kernel::arch::riscv64::sbi_console::puts("\r\n");

        // Register the framebuffer with the core driver
        kernel::framebuffer::init_riscv_ramfb(
            kernel::arch::riscv64::graphics::get_lfb_address(),
            kernel::arch::riscv64::graphics::get_width(),
            kernel::arch::riscv64::graphics::get_height(),
            kernel::arch::riscv64::graphics::get_pitch(),
            kernel::arch::riscv64::graphics::get_bpp());

        kernel::framebuffer::clear(0xFF101828);

        kernel::desktop::init();
        kernel::desktop::draw();

        kernel::arch::riscv64::sbi_console::puts("Desktop drawn\r\n");
    } else {
        kernel::arch::riscv64::sbi_console::puts("No framebuffer detected\r\n");
    }

    kernel::arch::riscv64::sbi_console::puts("Entering idle loop\r\n");

    // Enable interrupts and idle
    kernel::interrupts::init();
    while (1) {
        kernel::arch::halt();
    }
#endif // ARCH_RISCV64

    // Fallback: no framebuffer or unsupported non-x86 platform
    kernel::interrupts::init();
    while (1) {
        kernel::arch::halt();
    }
#endif // ARCH_HAS_PIC_8259
}


