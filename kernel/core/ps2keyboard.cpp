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

// Scancode set 2 to ASCII mapping (US QWERTY keyboard layout)
// Many emulators use scancode set 2 (translated mode)
// Index = scancode, value = ASCII (lowercase)
static const char s_scancodeToAscii[256] = {
//   0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,  '\t',  '`',   0,   // 0x00-0x0F
    0,    0,    0,    0,    0,   'q',  '1',   0,    0,    0,   'z',  's',  'a',  'w',  '2',   0,   // 0x10-0x1F
    0,   'c',  'x',  'd',  'e',  '4',  '3',   0,    0,   ' ',  'v',  'f',  't',  'r',  '5',   0,   // 0x20-0x2F
    0,   'n',  'b',  'h',  'g',  'y',  '6',   0,    0,    0,   'm',  'j',  'u',  '7',  '8',   0,   // 0x30-0x3F
    0,   ',',  'k',  'i',  'o',  '0',  '9',   0,    0,   '.',  '/',  'l',  ';',  'p',  '-',   0,   // 0x40-0x4F
    0,    0,  '\'',   0,   '[',  '=',   0,    0,    0,    0,  '\n',  ']',   0,  '\\',   0,    0,   // 0x50-0x5F
    0,    0,    0,    0,    0,    0,  '\b',   0,    0,   '1',   0,   '4',  '7',   0,    0,    0,   // 0x60-0x6F
   '0',  '.',  '2',  '5',  '6',  '8',  27,    0,    0,   '+',  '3',  '-',  '*',  '9',   0,    0,   // 0x70-0x7F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0x80-0x8F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0x90-0x9F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xA0-0xAF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xB0-0xBF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xC0-0xCF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xD0-0xDF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xE0-0xEF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0    // 0xF0-0xFF
};

// Shifted characters (scancode set 2)
static const char s_scancodeToAsciiShift[256] = {
//   0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,  '\t',  '~',   0,   // 0x00-0x0F
    0,    0,    0,    0,    0,   'Q',  '!',   0,    0,    0,   'Z',  'S',  'A',  'W',  '@',   0,   // 0x10-0x1F
    0,   'C',  'X',  'D',  'E',  '$',  '#',   0,    0,   ' ',  'V',  'F',  'T',  'R',  '%',   0,   // 0x20-0x2F
    0,   'N',  'B',  'H',  'G',  'Y',  '^',   0,    0,    0,   'M',  'J',  'U',  '&',  '*',   0,   // 0x30-0x3F
    0,   '<',  'K',  'I',  'O',  ')',  '(',   0,    0,   '>',  '?',  'L',  ':',  'P',  '_',   0,   // 0x40-0x4F
    0,    0,   '"',   0,   '{',  '+',   0,    0,    0,    0,  '\n',  '}',   0,   '|',   0,    0,   // 0x50-0x5F
    0,    0,    0,    0,    0,    0,  '\b',   0,    0,   '1',   0,   '4',  '7',   0,    0,    0,   // 0x60-0x6F
   '0',  '.',  '2',  '5',  '6',  '8',  27,    0,    0,   '+',  '3',  '-',  '*',  '9',   0,    0,   // 0x70-0x7F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0x80-0x8F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0x90-0x9F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xA0-0xAF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xB0-0xBF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xC0-0xCF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xD0-0xDF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xE0-0xEF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0    // 0xF0-0xFF
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

// Scancode set 2 modifier scancodes
static const uint8_t SC2_LSHIFT = 0x12;
static const uint8_t SC2_RSHIFT = 0x59;
static const uint8_t SC2_LCTRL  = 0x14;
static const uint8_t SC2_LALT   = 0x11;
static const uint8_t SC2_CAPS   = 0x58;

// Extended scancode flag and break code flag
static bool s_extendedKey = false;
static bool s_breakCode = false;  // Scancode set 2 uses 0xF0 prefix for key release

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
    s_breakCode = false;
    
    serial::puts("[PS2KB] Keyboard initialized (scancode set 2)\n");
}

void irq_handler()
{
    uint8_t status = arch::inb(kStatusPort);
    
    // Check if data is available and it's from keyboard (not mouse)
    if (!(status & 0x01)) return;
    if (status & 0x20) return;  // Bit 5 = mouse data, skip it
    
    uint8_t scancode = arch::inb(kDataPort);
    
    // Scancode set 2: Handle special prefixes
    // 0xE0 = extended key prefix
    // 0xF0 = break (key release) prefix
    if (scancode == 0xE0) {
        s_extendedKey = true;
        return;
    }
    if (scancode == 0xF0) {
        s_breakCode = true;
        return;
    }
    
    bool keyUp = s_breakCode;
    s_breakCode = false;  // Reset for next scancode
    
    // Handle modifier keys (scancode set 2 values)
    if (scancode == SC2_LSHIFT || scancode == SC2_RSHIFT) {
        s_shiftDown = !keyUp;
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_LCTRL) {
        s_ctrlDown = !keyUp;
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_LALT) {
        s_altDown = !keyUp;
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_CAPS && !keyUp) {  // Caps Lock (toggle on press)
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
    
    // Handle extended keys (arrow keys, etc.) - scancode set 2 values
    if (s_extendedKey) {
        switch (scancode) {
            case 0x75: key = KEY_UP; break;
            case 0x72: key = KEY_DOWN; break;
            case 0x6B: key = KEY_LEFT; break;
            case 0x74: key = KEY_RIGHT; break;
            case 0x6C: key = KEY_HOME; break;
            case 0x69: key = KEY_END; break;
            case 0x71: key = KEY_DELETE; break;
            case 0x7D: key = KEY_PGUP; break;
            case 0x7A: key = KEY_PGDN; break;
            default: break;
        }
        s_extendedKey = false;
    } else {
        // Handle function keys (scancode set 2)
        // F1=0x05, F2=0x06, F3=0x04, F4=0x0C, F5=0x03, F6=0x0B, F7=0x83, F8=0x0A, F9=0x01, F10=0x09
        switch (scancode) {
            case 0x05: key = KEY_F1; break;
            case 0x06: key = KEY_F1 + 1; break;
            case 0x04: key = KEY_F1 + 2; break;
            case 0x0C: key = KEY_F1 + 3; break;
            case 0x03: key = KEY_F1 + 4; break;
            case 0x0B: key = KEY_F1 + 5; break;
            case 0x83: key = KEY_F1 + 6; break;
            case 0x0A: key = KEY_F1 + 7; break;
            case 0x01: key = KEY_F1 + 8; break;
            case 0x09: key = KEY_F1 + 9; break;
            case 0x78: key = KEY_F11; break;
            case 0x07: key = KEY_F12; break;
            default:
                // Regular ASCII keys
                if (scancode < 256) {
                    bool shift = s_shiftDown;
                    char c = s_scancodeToAscii[scancode];
                    if (s_capsLock && c >= 'a' && c <= 'z') {
                        shift = !shift;
                    }
                    key = shift ? s_scancodeToAsciiShift[scancode] : s_scancodeToAscii[scancode];
                }
                break;
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
