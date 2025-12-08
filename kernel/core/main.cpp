//
// Kernel entry point
//
// Copyright (c) 2024 guideX
//

#include <kernel/version.h>
#include <kernel/arch.h>
#include <kernel/vga.h>
#include <kernel/framebuffer.h>

extern "C" void kernel_main(void* multiboot_info, uint32_t multiboot_magic)
{
    // Try to initialize framebuffer first
    bool has_fb = kernel::framebuffer::init(multiboot_info);
    
    if (has_fb) {
        // Clear to dark blue
        kernel::framebuffer::clear(0x00001020);
        
        // Draw a simple test pattern
        kernel::framebuffer::fill_rect(50, 50, 200, 100, 0x00FF0000); // Red rectangle
        kernel::framebuffer::fill_rect(300, 50, 200, 100, 0x0000FF00); // Green rectangle
        kernel::framebuffer::fill_rect(550, 50, 200, 100, 0x000000FF); // Blue rectangle
        
        // Draw border
        uint32_t w = kernel::framebuffer::get_width();
        uint32_t h = kernel::framebuffer::get_height();
        kernel::framebuffer::fill_rect(0, 0, w, 5, 0x00FFFFFF); // Top
        kernel::framebuffer::fill_rect(0, h-5, w, 5, 0x00FFFFFF); // Bottom
        kernel::framebuffer::fill_rect(0, 0, 5, h, 0x00FFFFFF); // Left
        kernel::framebuffer::fill_rect(w-5, 0, 5, h, 0x00FFFFFF); // Right
    }
    
    // Initialize VGA for text output (fallback or overlay)
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
    
    // TODO: Initialize kernel subsystems
    kernel::vga::print("\n");
    kernel::vga::print_colored("TODO: Initialize kernel subsystems\n", kernel::vga::Color::Yellow, kernel::vga::Color::Black);
    kernel::vga::print("  - GDT, IDT (x86/amd64)\n");
    kernel::vga::print("  - Memory manager (PMM, VMM)\n");
    kernel::vga::print("  - Scheduler\n");
    kernel::vga::print("  - Drivers\n");
    
    // Don't enable interrupts yet (no IDT configured)
    kernel::vga::print("\n");
    kernel::vga::print_colored("[INFO]", kernel::vga::Color::LightBlue, kernel::vga::Color::Black);
    kernel::vga::print(" Kernel initialization complete\n");
    kernel::vga::print_colored("[INFO]", kernel::vga::Color::LightBlue, kernel::vga::Color::Black);
    kernel::vga::print(" Entering idle loop (interrupts disabled)\n");
    
    // Main kernel loop
    while (1)
    {
        // Halt the CPU until next interrupt
        kernel::arch::halt();
    }
}
