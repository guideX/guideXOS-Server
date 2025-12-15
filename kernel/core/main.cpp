//
// Kernel entry point
//
// Copyright (c) 2024 guideX
//

#include <kernel/version.h>
#include <kernel/arch.h>
#include <kernel/vga.h>
#include <kernel/framebuffer.h>
#include <kernel/multiboot.h>

// Forward declarations
void init_boot_splash();
void show_desktop();

extern "C" void kernel_main(void* multiboot_info, uint32_t multiboot_magic)
{
    // Validate Multiboot magic number
    if (multiboot_magic != 0x2BADB002) {
        // Invalid multiboot, halt
        while(1) kernel::arch::halt();
    }
    
    // Parse Multiboot info
    auto* mb_info = reinterpret_cast<kernel::multiboot::Info*>(multiboot_info);
    
    // Try to initialize framebuffer for graphics mode
    bool has_fb = kernel::framebuffer::init(multiboot_info);
    
    if (has_fb) {
        // === GRAPHICS MODE BOOT (Like C# guideXOS) ===
        
        // Clear to dark background
        kernel::framebuffer::clear(0x00000000);
        
        // Show boot splash
        init_boot_splash();
        
        // Initialize architecture
        kernel::arch::disable_interrupts();
        kernel::arch::init();
        
        // TODO: Initialize subsystems
        // - GDT, IDT
        // - Memory manager
        // - Interrupts
        // - Keyboard, Mouse, Timer
        // - Filesystem
        
        // Transition to desktop
        show_desktop();
        
        // Main kernel loop
        while (1) {
            // TODO: Process events
            // TODO: Update GUI
            // TODO: Handle input
            
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
        
        // Framebuffer not available warning
        kernel::vga::print("\n");
        kernel::vga::print_colored("WARNING: Framebuffer not available\n", kernel::vga::Color::Yellow, kernel::vga::Color::Black);
        kernel::vga::print("Running in text mode only\n");
        kernel::vga::print("For GUI, boot with: qemu-system-i386 -kernel kernel.elf -m 128M\n");
        
        kernel::vga::print("\n");
        kernel::vga::print_colored("[INFO]", kernel::vga::Color::LightBlue, kernel::vga::Color::Black);
        kernel::vga::print(" Kernel initialization complete\n");
        
        // Main kernel loop (text mode)
        while (1) {
            kernel::arch::halt();
        }
    }
}

// Initialize boot splash screen
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
    
    // Animate progress (simulate loading)
    for (uint32_t progress = 0; progress < bar_w - 4; progress += 10) {
        // Fill progress
        kernel::framebuffer::fill_rect(bar_x + 2, bar_y + 2, progress, bar_h - 4, 0xFF00AAFF);
        
        // Small delay (TODO: use proper timer)
        for (volatile uint32_t i = 0; i < 1000000; i++);
    }
}

// Show desktop environment
void show_desktop()
{
    uint32_t w = kernel::framebuffer::get_width();
    uint32_t h = kernel::framebuffer::get_height();
    
    // Clear to desktop background (teal gradient like C# version)
    for (uint32_t y = 0; y < h; y++) {
        // Teal gradient: light at top, dark at bottom
        uint8_t r = 0x5F - (y * 0x52 / h);
        uint8_t g = 0xD4 - (y * 0x57 / h);
        uint8_t b = 0xC4 - (y * 0x4D / h);
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        kernel::framebuffer::fill_rect(0, y, w, 1, color);
    }
    
    // Draw taskbar (bottom)
    uint32_t taskbar_h = 32;
    kernel::framebuffer::fill_rect(0, h - taskbar_h, w, taskbar_h, 0xFF2A2A2A);
    
    // Draw taskbar border (top edge)
    kernel::framebuffer::draw_line(0, h - taskbar_h, w, h - taskbar_h, 0xFF444444);
    
    // Draw start button
    uint32_t start_btn_w = 100;
    kernel::framebuffer::fill_rect(5, h - taskbar_h + 4, start_btn_w, taskbar_h - 8, 0xFF3C3C3C);
    kernel::framebuffer::draw_line(5, h - taskbar_h + 4, 5 + start_btn_w, h - taskbar_h + 4, 0xFF555555);
    kernel::framebuffer::draw_line(5, h - 4, 5 + start_btn_w, h - 4, 0xFF555555);
    kernel::framebuffer::draw_line(5, h - taskbar_h + 4, 5, h - 4, 0xFF555555);
    kernel::framebuffer::draw_line(5 + start_btn_w, h - taskbar_h + 4, 5 + start_btn_w, h - 4, 0xFF555555);
    
    // TODO: Draw "Start" text (need font rendering)
    
    // Show system info window (like Welcome window in C#)
    uint32_t win_w = 400;
    uint32_t win_h = 300;
    uint32_t win_x = (w - win_w) / 2;
    uint32_t win_y = (h - win_h) / 2;
    
    // Window background
    kernel::framebuffer::fill_rect(win_x, win_y, win_w, win_h, 0xFF3C3C3C);
    
    // Window border
    kernel::framebuffer::draw_line(win_x, win_y, win_x + win_w, win_y, 0xFF666666);
    kernel::framebuffer::draw_line(win_x, win_y + win_h, win_x + win_w, win_y + win_h, 0xFF666666);
    kernel::framebuffer::draw_line(win_x, win_y, win_x, win_y + win_h, 0xFF666666);
    kernel::framebuffer::draw_line(win_x + win_w, win_y, win_x + win_w, win_y + win_h, 0xFF666666);
    
    // Title bar
    kernel::framebuffer::fill_rect(win_x, win_y, win_w, 24, 0xFF1E90FF);
    kernel::framebuffer::draw_line(win_x, win_y + 24, win_x + win_w, win_y + 24, 0xFF666666);
    
    // TODO: Draw window title "guideXOS" (need font rendering)
    
    // Close button
    kernel::framebuffer::fill_rect(win_x + win_w - 24, win_y + 2, 20, 20, 0xFFFF0000);
}
