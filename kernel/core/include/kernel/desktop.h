//
//
// guideXOS Desktop Environment - Kernel Framebuffer Renderer
//
// Draws the complete desktop UI directly to the framebuffer:
//   - Gradient wallpaper with branding
//   - Taskbar with start button, clock, system tray
//   - Desktop icons
//   - Start menu (toggled)
//
// Temporary kernel-mode desktop until guideXOSServer
// can be loaded as a user-mode process (ELF loader TODO).
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_DESKTOP_H
#define KERNEL_DESKTOP_H

#include "kernel/types.h"

namespace kernel {
namespace desktop {

// Initialize desktop state (call once after framebuffer is ready)
void init();

// Draw the full desktop to the framebuffer
void draw();

// Toggle start menu open/closed
void toggle_start_menu();

// Check if start menu is visible
bool is_start_menu_open();

// Toggle right-click context menu at position
void show_context_menu(uint32_t x, uint32_t y);

// Close right-click context menu
void close_context_menu();

// Dismiss notification toast
void dismiss_notification();

// Process mouse input: update cursor position and handle clicks
// Call this from the main loop when the mouse state changes.
void handle_mouse(int32_t mx, int32_t my, uint8_t buttons);

// Process keyboard input: handle key presses
// Special keys use codes from shell.h (KEY_UP, KEY_DOWN, etc.)
void handle_key(uint32_t key);

// Open the terminal/shell
void open_terminal();

// Update tick counter for timing (call from main loop, e.g., every 10ms)
void tick();

// Check if redraw is needed (called from main loop)
bool needs_redraw();

// Draw the mouse cursor at the given position (overlay on framebuffer)
void draw_cursor(int32_t mx, int32_t my);

} // namespace desktop
} // namespace kernel

// External function for keyboard IRQ to request redraw
extern "C" void desktop_request_redraw();

#endif // KERNEL_DESKTOP_H
