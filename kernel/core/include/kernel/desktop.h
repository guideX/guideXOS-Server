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
// Copyright (c) 2024 guideX
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

} // namespace desktop
} // namespace kernel

#endif // KERNEL_DESKTOP_H
