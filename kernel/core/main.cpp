//
// guideXOS Minimal Kernel - Entry Point
//
// ROLE: Bridge between bootloader and guideXOS Server (user-mode)
//
// RESPONSIBILITIES:
//   - Receive BootInfo from bootloader (firmware-neutral)
//   - Initialize minimal subsystems (memory, interrupts, processes)
//   - Show minimal boot splash (NO desktop rendering)
//   - Load guideXOSServer from ramdisk (TODO: implement ELF loader)
//   - Launch server as PID 1 in user mode (ring 3)
//   - Handle syscalls from user processes
//   - Idle loop when no work to do
//
// CONSTRAINTS:
//   - Keep MINIMAL (no unnecessary features)
//   - Boot splash ONLY (desktop/GUI belongs in guideXOSServer)
//   - NO user services (compositor, desktop, apps belong in server)
//   - Prefer clarity over completeness
//
// ARCHITECTURE:
//   Bootloader ? [kernel_main] ? guideXOSServer ? Applications
//
// Copyright (c) 2024 guideX
//

#include "include/kernel/version.h"
#include "include/kernel/arch.h"
#include "include/kernel/vga.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/multiboot.h"
#include "include/kernel/process.h"

// Forward declarations
void init_boot_splash();
void launch_init_process();

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
        // Legacy BIOS/Multiboot boot
        multiboot_info = boot_environment;
    } else {
        // UEFI boot via guideXOSBootLoader
        bootinfo = static_cast<guideXOS::BootInfo*>(boot_environment);
        
        // Validate BootInfo magic
        if (bootinfo && bootinfo->Magic == guideXOS::GUIDEXOS_BOOTINFO_MAGIC) {
            is_bootinfo = true;
        }
    }
    
    // If neither boot method is valid, halt
    if (!is_multiboot && !is_bootinfo) {
        while(1) kernel::arch::halt();
    }
    
    // Try to initialize framebuffer for graphics mode
    bool has_fb = false;
    
    if (is_bootinfo) {
        // UEFI boot - use BootInfo framebuffer
        has_fb = kernel::framebuffer::init_from_bootinfo(bootinfo);
    } else {
        // Multiboot boot - use Multiboot framebuffer
        has_fb = kernel::framebuffer::init(multiboot_info);
    }
    
    if (has_fb) {
        // === GRAPHICS MODE BOOT ===
        
        // Clear to dark background
        kernel::framebuffer::clear(0x00000000);
        
        // Show boot splash
        init_boot_splash();
        
        // Initialize architecture
        kernel::arch::disable_interrupts();
        kernel::arch::init();
        
        // Initialize kernel subsystems
        kernel::process::init();
        
        // TODO: Initialize other subsystems
        // - GDT, IDT
        // - Memory manager
        // - Interrupts
        // - Keyboard, Mouse, Timer
        // - Filesystem
        
        // Launch guideXOSServer as first user process
        launch_init_process();
        
        // Main kernel loop - just schedule processes
        while (1) {
            kernel::process::schedule();
            kernel::arch::halt();
        }
    }
    else {
        // === TEXT MODE FALLBACK (VGA) ===
        
        // Initialize VGA for text output
        kernel::vga::init();
        
        // Print banner
        kernel::vga::print_colored("guideXOS Kernel v0.1\n", kernel::vga::Color::LightCyan, kernel::vga::Color::Black);
        kernel::vga::print("Copyright (c) 2024 guideX\n\n");
        
        // Show architecture info
        kernel::vga::print("Architecture: ");
        kernel::vga::print(kernel::arch::get_arch_name());
        kernel::vga::print(" (");
        kernel::vga::print_dec(kernel::arch::get_arch_bits());
        kernel::vga::print("-bit)\n\n");
        
        // Disable interrupts during initialization
        kernel::vga::print("[....] Disabling interrupts...\r");
        kernel::arch::disable_interrupts();
        kernel::vga::print_colored("[ OK ]", kernel::vga::Color::LightGreen, kernel::vga::Color::Black);
        kernel::vga::print(" Interrupts disabled\n");
        
        
        // Initialize architecture-specific features
        kernel::vga::print("[....] Initializing architecture...\r");
        kernel::arch::init();
        kernel::vga::print_colored("[ OK ]", kernel::vga::Color::LightGreen, kernel::vga::Color::Black);
        kernel::vga::print(" Architecture initialized\n");
        
        // Initialize kernel subsystems
        kernel::process::init();
        
        // Framebuffer not available warning
        kernel::vga::print("\n");
        kernel::vga::print_colored("WARNING: Framebuffer not available\n", kernel::vga::Color::Yellow, kernel::vga::Color::Black);
        kernel::vga::print("Running in text mode only\n");
        kernel::vga::print("For GUI, boot with: qemu-system-i386 -kernel kernel.elf -m 128M\n");
        
        
        kernel::vga::print("\n");
        kernel::vga::print_colored("[INFO]", kernel::vga::Color::LightBlue, kernel::vga::Color::Black);
        kernel::vga::print(" Kernel initialization complete\n");
        
        kernel::vga::print("\n");
        kernel::vga::print_colored("[TODO]", kernel::vga::Color::Yellow, kernel::vga::Color::Black);
        kernel::vga::print(" Load and launch guideXOSServer as init process\n");
        kernel::vga::print("       (Currently no ELF loader - server.cpp needs to be linked in or loaded)\n");
        
        // Main kernel loop (text mode) - idle loop
        while (1) {
            kernel::arch::halt();
        }
    }
}

// Initialize boot splash screen (minimal, kernel-only)
void init_boot_splash()
{
    uint32_t w = kernel::framebuffer::get_width();
    uint32_t h = kernel::framebuffer::get_height();
    
    // Draw background gradient (dark blue to black)
    for (uint32_t y = 0; y < h; y++) {
        uint32_t color = 0x00000020 - (y * 0x20 / h);
        kernel::framebuffer::fill_rect(0, y, w, 1, color);
    }
    
    // Draw title area
    uint32_t title_w = 400;
    uint32_t title_h = 100;
    uint32_t title_x = (w - title_w) / 2;
    uint32_t title_y = h / 3;
    
    // Title background (semi-transparent dark)
    kernel::framebuffer::fill_rect(title_x, title_y, title_w, title_h, 0x80000000);
    
    // Title border (cyan)
    kernel::framebuffer::draw_line(title_x, title_y, title_x + title_w, title_y, 0xFF00FFFF);
    kernel::framebuffer::draw_line(title_x, title_y + title_h, title_x + title_w, title_y + title_h, 0xFF00FFFF);
    kernel::framebuffer::draw_line(title_x, title_y, title_x, title_y + title_h, 0xFF00FFFF);
    kernel::framebuffer::draw_line(title_x + title_w, title_y, title_x + title_w, title_y + title_h, 0xFF00FFFF);
    
    // TODO: Draw "guideXOS" text (need font rendering)
    
    // Progress bar
    uint32_t bar_w = 300;
    uint32_t bar_h = 20;
    uint32_t bar_x = (w - bar_w) / 2;
    uint32_t bar_y = title_y + title_h + 50;
    
    // Progress bar background
    kernel::framebuffer::fill_rect(bar_x, bar_y, bar_w, bar_h, 0xFF333333);
    
    // Progress bar border
    kernel::framebuffer::draw_line(bar_x, bar_y, bar_x + bar_w, bar_y, 0xFF666666);
    kernel::framebuffer::draw_line(bar_x, bar_y + bar_h, bar_x + bar_w, bar_y + bar_h, 0xFF666666);
    kernel::framebuffer::draw_line(bar_x, bar_y, bar_x, bar_y + bar_h, 0xFF666666);
    kernel::framebuffer::draw_line(bar_x + bar_w, bar_y, bar_x + bar_w, bar_y + bar_h, 0xFF666666);
    
    // Animate progress (simulate kernel initialization)
    for (uint32_t progress = 0; progress < bar_w - 4; progress += 10) {
        // Fill progress
        kernel::framebuffer::fill_rect(bar_x + 2, bar_y + 2, progress, bar_h - 4, 0xFF00AAFF);
        
        // Small delay (TODO: use proper timer)
        for (volatile uint32_t i = 0; i < 1000000; i++);
    }
}

// Launch the init process (guideXOSServer)
//
// IMPORTANT: guideXOSServer is a USER-MODE PROCESS, not kernel code
// - The server will be loaded from ramdisk as an ELF binary
// - The server will execute in user mode (ring 3)
// - The server has NO access to BootInfo or firmware structures
// - The server requests hardware via syscalls only
//
// In a complete system, this would:
// 1. Load guideXOSServer ELF from ramdisk/filesystem
// 2. Set up user-mode page tables
// 3. Create process structure (PID 1)
// 4. Map necessary resources (framebuffer, etc.)
// 5. Jump to user mode (ring 3) at server entry point
void launch_init_process()
{
    // TODO: Implement ELF loader
    // TODO: Load /sbin/guideXOSServer from ramdisk
    // TODO: Set up user-mode memory mapping
    // TODO: Create process with user privileges (ring 3)
    // TODO: Map framebuffer to user address space
    // TODO: Jump to user mode at server entry point
    
    // For now, just display a message indicating we would launch it
    // The actual desktop UI (compositor, taskbar, windows) belongs in
    // guideXOSServer (user mode), NOT the kernel
    
    // Stub: Clear screen and show "waiting for init" message
    uint32_t w = kernel::framebuffer::get_width();
    uint32_t h = kernel::framebuffer::get_height();
    
    kernel::framebuffer::clear(0xFF001020); // Dark blue-ish background
    
    // Center message area
    uint32_t msg_w = 500;
    uint32_t msg_h = 200;
    uint32_t msg_x = (w - msg_w) / 2;
    uint32_t msg_y = (h - msg_h) / 2;
    
    // Draw message box
    kernel::framebuffer::fill_rect(msg_x, msg_y, msg_w, msg_h, 0xFF2A2A3A);
    kernel::framebuffer::draw_line(msg_x, msg_y, msg_x + msg_w, msg_y, 0xFF5A5A7A);
    kernel::framebuffer::draw_line(msg_x, msg_y + msg_h, msg_x + msg_w, msg_y + msg_h, 0xFF5A5A7A);
    kernel::framebuffer::draw_line(msg_x, msg_y, msg_x, msg_y + msg_h, 0xFF5A5A7A);
    kernel::framebuffer::draw_line(msg_x + msg_w, msg_y, msg_x + msg_w, msg_y + msg_h, 0xFF5A5A7A);
    
    // TODO: Draw text "Kernel ready - waiting for init process (guideXOSServer)"
    // TODO: Draw text "ELF loader not yet implemented"
    
    // Note: The actual GUI (desktop, taskbar, windows) belongs in guideXOSServer
    // The kernel only provides this minimal boot UI
}
