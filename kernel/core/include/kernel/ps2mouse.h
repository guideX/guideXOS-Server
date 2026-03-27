//
// PS/2 Mouse Driver
//
// Handles PS/2 mouse initialization, interrupt-driven packet parsing,
// and cursor position tracking.
//
// Ported from guideXOS Legacy PS2Mouse.cs
//
// Copyright (c) 2024 guideX
//

#ifndef KERNEL_PS2MOUSE_H
#define KERNEL_PS2MOUSE_H

#include "kernel/types.h"

namespace kernel {
namespace ps2mouse {

// Mouse button state flags
enum MouseButtons : uint8_t {
    None   = 0,
    Left   = 1,
    Right  = 2,
    Middle = 4
};

// Initialize the PS/2 mouse (sends enable commands to controller)
void init(uint32_t screen_width, uint32_t screen_height);

// Called from IRQ12 handler — reads one byte from the PS/2 data port
void irq_handler();

// Get current mouse position
int32_t get_x();
int32_t get_y();

// Get current button state (bitfield of MouseButtons)
uint8_t get_buttons();

// Get scroll wheel delta (reset after read)
int8_t get_scroll_delta();

// Check if position changed since last call to clear_dirty()
bool is_dirty();
void clear_dirty();

} // namespace ps2mouse
} // namespace kernel

#endif // KERNEL_PS2MOUSE_H
