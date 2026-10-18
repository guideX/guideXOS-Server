// PS/2 Keyboard Driver
//
// Handles PS/2 keyboard input (IRQ1) and provides scancodes to the input manager.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_PS2KEYBOARD_H
#define KERNEL_PS2KEYBOARD_H

#include "kernel/types.h"

namespace kernel {
namespace ps2keyboard {

// Stable values used by both the legacy desktop dispatcher and the Native
// ELF input bridge.  The decoder's private names retain their historical
// spelling in the implementation.
static const uint32_t KEY_EVENT_UP = 0x100;
static const uint32_t KEY_EVENT_DOWN = 0x101;
static const uint32_t KEY_EVENT_LEFT = 0x102;
static const uint32_t KEY_EVENT_RIGHT = 0x103;

// Initialize the PS/2 keyboard driver
void init();

// IRQ1 handler - called by interrupt dispatcher
void irq_handler();

// Check if there's a new key available
bool has_key();

// Get the last key pressed (ASCII or special code)
// Returns 0 if no key available
uint32_t get_key();
// Returns the same legacy key value and its modifier snapshot from key-down.
// This avoids polling modifier state after it may have changed.
uint32_t get_key_with_modifiers(uint32_t* outModifiers);

enum class KeyAction : uint8_t {
    Up = 0,
    Down = 1
};

enum : uint32_t {
    KEY_MOD_SHIFT = 1u << 0,
    KEY_MOD_CTRL  = 1u << 1,
    KEY_MOD_ALT   = 1u << 2
};

// Set-2 left and right Shift keys have independent make/break state. Keeping
// this small value type in the production header lets the IRQ decoder and its
// executable regression exercise the same transition rules.
struct ShiftState {
    ShiftState() : leftDown(false), rightDown(false) {}

    void setLeftDown(bool down) { leftDown = down; }
    void setRightDown(bool down) { rightDown = down; }
    bool isDown() const { return leftDown || rightDown; }

    bool leftDown;
    bool rightDown;
};

struct KeyEvent {
    uint32_t key;
    KeyAction action;
    uint32_t modifiers;
};

// Consume the lossless key transition queue used by Native ELF apps.
bool has_event();
bool get_event(KeyEvent* outEvent);

// Modifier state accessors
bool is_ctrl_down();
bool is_shift_down();
bool is_alt_down();
bool is_left_alt_down();
bool is_right_alt_down();
bool is_f4_down();
bool was_alt_f4_shortcut_candidate();
bool last_key_alt_left_down();
bool last_key_alt_right_down();

// Clear the key buffer
void clear();

} // namespace ps2keyboard
} // namespace kernel

#endif // KERNEL_PS2KEYBOARD_H
