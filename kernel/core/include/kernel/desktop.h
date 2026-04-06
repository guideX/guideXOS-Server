//
//
// guideXOS Desktop Environment - Kernel Framebuffer Renderer
//
// Draws the complete desktop UI directly to the framebuffer:
//   - Gradient wallpaper with branding
//   - Taskbar with start button, clock, system tray
//   - Desktop icons
//   - Start menu (toggled)
//   - GUI applications via kernel compositor
//
// Supports full GUI app functionality in UEFI/bare-metal mode
// without requiring the user-mode server (guideXOSServer).
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_DESKTOP_H
#define KERNEL_DESKTOP_H

#include "kernel/types.h"

namespace kernel {
namespace desktop {

// ================================================================
// Initialization & Core Loop
// ================================================================

// Initialize desktop state (call once after framebuffer is ready)
// This also initializes the kernel app framework, IPC, and compositor
void init();

// Draw the full desktop to the framebuffer
// Draws background, icons, taskbar, menus, and all GUI app windows
void draw();

// Update tick counter for timing (call from main loop, e.g., every 10ms)
// Also updates IPC, running apps, and taskbar
void tick();

// Check if redraw is needed (called from main loop)
bool needs_redraw();

// ================================================================
// Input Handling
// ================================================================

// Process mouse input: update cursor position and handle clicks
// Routes input to GUI apps, shell, or desktop as appropriate
void handle_mouse(int32_t mx, int32_t my, uint8_t buttons);

// Process keyboard input: handle key presses
// Routes to focused GUI app, shell, or desktop shortcuts
// Special keys use codes from shell.h (KEY_UP, KEY_DOWN, etc.)
void handle_key(uint32_t key);

// Draw the mouse cursor at the given position (overlay on framebuffer)
void draw_cursor(int32_t mx, int32_t my);

// ================================================================
// UI Controls
// ================================================================

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

// ================================================================
// Application Management
// ================================================================

// Open the terminal/shell
void open_terminal();

// Launch an application by name
// In bare-metal mode: uses kernel app framework if available
// Returns true if app was launched or is already running
bool launch_app(const char* appName);

// Check if running in bare-metal/UEFI mode (vs hosted mode with server)
bool is_bare_metal_mode();

// Get count of running GUI apps (excludes shell)
int get_running_app_count();

// ================================================================
// Testing & Diagnostics
// ================================================================

// Run test mode - attempts to launch Notepad, Calculator, TaskManager
// and logs results to kernel console. Call from shell or at startup.
void run_test_mode();

// Check if compositor and IPC are available for GUI apps
bool is_compositor_available();

} // namespace desktop
} // namespace kernel

// External function for keyboard IRQ to request redraw
extern "C" void desktop_request_redraw();

#endif // KERNEL_DESKTOP_H
