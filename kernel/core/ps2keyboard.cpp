// PS/2 Keyboard Driver Implementation
//
// Handles PS/2 keyboard input via IRQ1 and converts scancodes to ASCII/keycodes.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ps2keyboard.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace ps2keyboard {

#if ARCH_HAS_PS2

// PS/2 controller ports
static const uint16_t kDataPort    = 0x60;
static const uint16_t kStatusPort  = 0x64;

// Key state
static uint32_t s_lastKey = 0;
static bool s_hasKey = false;
static bool s_shiftDown = false;
static bool s_ctrlDown = false;
static bool s_altDown = false;
static bool s_capsLock = false;

// Scancode set 1 to ASCII mapping (US keyboard layout)
// Index = scancode, value = ASCII (lowercase)
static const char s_scancodeToAscii[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',  // 0x00-0x0E
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',     // 0x0F-0x1C
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',           // 0x1D-0x29 (0x1D=LCtrl)
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,             // 0x2A-0x36 (0x2A=LShift, 0x36=RShift)
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,              // 0x37-0x43 (0x38=LAlt, 0x39=Space, 0x3A=CapsLock, 0x3B-0x44=F1-F10)
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+',  // 0x44-0x4E (numpad)
    '1', '2', '3', '0', '.',                                                    // 0x4F-0x53
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x54-0x63
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x64-0x73
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0                     // 0x74-0x7F
};

// Shifted characters
static const char s_scancodeToAsciiShift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',  // 0x00-0x0E
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',     // 0x0F-0x1C
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',            // 0x1D-0x29
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,              // 0x2A-0x36
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,              // 0x37-0x43
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+',  // 0x44-0x4E
    '1', '2', '3', '0', '.',                                                    // 0x4F-0x53
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x54-0x63
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x64-0x73
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0                     // 0x74-0x7F
};

// Special key codes (returned for non-ASCII keys)
static const uint32_t KEY_UP     = 0x100;
static const uint32_t KEY_DOWN   = 0x101;
static const uint32_t KEY_LEFT   = 0x102;
static const uint32_t KEY_RIGHT  = 0x103;
static const uint32_t KEY_HOME   = 0x104;
static const uint32_t KEY_END    = 0x105;
static const uint32_t KEY_DELETE = 0x106;
static const uint32_t KEY_PGUP   = 0x108;
static const uint32_t KEY_PGDN   = 0x109;
static const uint32_t KEY_F1     = 0x110;
static const uint32_t KEY_F11    = 0x11A;
static const uint32_t KEY_F12    = 0x11B;

// Extended scancode flag
static bool s_extendedKey = false;

void init()
{
    serial::puts("[PS2KB] Initializing PS/2 keyboard...\n");
    
    // Keyboard should already be initialized by BIOS/UEFI
    // Just clear any pending data
    while (arch::inb(kStatusPort) & 0x01) {
        arch::inb(kDataPort);
    }
    
    s_lastKey = 0;
    s_hasKey = false;
    s_shiftDown = false;
    s_ctrlDown = false;
    s_altDown = false;
    s_capsLock = false;
    s_extendedKey = false;
    
    serial::puts("[PS2KB] Keyboard initialized\n");
}

void irq_handler()
{
    uint8_t status = arch::inb(kStatusPort);
    
    // Check if data is available and it's from keyboard (not mouse)
    if (!(status & 0x01)) return;
    if (status & 0x20) return;  // Bit 5 = mouse data, skip it
    
    uint8_t scancode = arch::inb(kDataPort);
    
    // Handle extended scancode prefix
    if (scancode == 0xE0) {
        s_extendedKey = true;
        return;
    }
    
    bool keyUp = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;
    
    // Handle modifier keys
    if (code == 0x2A || code == 0x36) {  // Left/Right Shift
        s_shiftDown = !keyUp;
        s_extendedKey = false;
        return;
    }
    if (code == 0x1D) {  // Ctrl
        s_ctrlDown = !keyUp;
        s_extendedKey = false;
        return;
    }
    if (code == 0x38) {  // Alt
        s_altDown = !keyUp;
        s_extendedKey = false;
        return;
    }
    if (code == 0x3A && !keyUp) {  // Caps Lock (toggle on press)
        s_capsLock = !s_capsLock;
        s_extendedKey = false;
        return;
    }
    
    // Only process key presses, not releases
    if (keyUp) {
        s_extendedKey = false;
        return;
    }
    
    uint32_t key = 0;
    
    // Handle extended keys (arrow keys, etc.)
    if (s_extendedKey) {
        switch (code) {
            case 0x48: key = KEY_UP; break;
            case 0x50: key = KEY_DOWN; break;
            case 0x4B: key = KEY_LEFT; break;
            case 0x4D: key = KEY_RIGHT; break;
            case 0x47: key = KEY_HOME; break;
            case 0x4F: key = KEY_END; break;
            case 0x53: key = KEY_DELETE; break;
            case 0x49: key = KEY_PGUP; break;
            case 0x51: key = KEY_PGDN; break;
            default: break;
        }
        s_extendedKey = false;
    } else {
        // Handle function keys
        if (code >= 0x3B && code <= 0x44) {
            key = KEY_F1 + (code - 0x3B);  // F1-F10
        } else if (code == 0x57) {
            key = KEY_F11;
        } else if (code == 0x58) {
            key = KEY_F12;
        } else if (code < 128) {
            // Regular ASCII keys
            bool shift = s_shiftDown;
            if (s_capsLock && code >= 0x10 && code <= 0x32) {
                // Caps lock affects letters
                char c = s_scancodeToAscii[code];
                if (c >= 'a' && c <= 'z') {
                    shift = !shift;
                }
            }
            key = shift ? s_scancodeToAsciiShift[code] : s_scancodeToAscii[code];
        }
    }
    
    if (key != 0) {
        s_lastKey = key;
        s_hasKey = true;
    }
}

bool has_key()
{
    return s_hasKey;
}

uint32_t get_key()
{
    if (!s_hasKey) return 0;
    s_hasKey = false;
    return s_lastKey;
}

void clear()
{
    s_hasKey = false;
    s_lastKey = 0;
}

#else
// Stub implementation for non-x86 architectures
void init() {}
void irq_handler() {}
bool has_key() { return false; }
uint32_t get_key() { return 0; }
void clear() {}
#endif

} // namespace ps2keyboard
} // namespace kernel
