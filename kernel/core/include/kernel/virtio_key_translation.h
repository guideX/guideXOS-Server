#pragma once

#include <stdint.h>

namespace kernel {
namespace virtio_input {

// Linux input key codes are translated to the virtual-key values consumed by
// Developer Studio's existing key mapper. Letters are always uppercase here;
// the modifier mask carries shift/caps state separately.
inline uint32_t native_key_for_linux_code(uint16_t code)
{
    if (code >= 16 && code <= 25) {
        static const char keys[] = "QWERTYUIOP";
        return static_cast<uint32_t>(keys[code - 16]);
    }
    if (code >= 30 && code <= 38) {
        static const char keys[] = "ASDFGHJKL";
        return static_cast<uint32_t>(keys[code - 30]);
    }
    if (code >= 44 && code <= 50) {
        static const char keys[] = "ZXCVBNM";
        return static_cast<uint32_t>(keys[code - 44]);
    }
    if (code >= 2 && code <= 11) {
        static const char keys[] = "1234567890";
        return static_cast<uint32_t>(keys[code - 2]);
    }
    if (code >= 59 && code <= 68) return 112u + code - 59u; // F1-F10
    switch (code) {
    case 1: return 27;   // Escape
    case 12: return 189; // '-'
    case 13: return 187; // '='
    case 14: return 8;   // Backspace
    case 15: return 9;   // Tab
    case 26: return 219; // '['
    case 27: return 221; // ']'
    case 28: return 13;  // Enter
    case 29: case 97: return 17; // Ctrl
    case 39: return 186; // ';'
    case 40: return 222; // apostrophe
    case 41: return 192; // '`'
    case 42: case 54: return 16; // Shift
    case 43: return 220; // backslash
    case 51: return 188; // ','
    case 52: return 190; // '.'
    case 53: return 191; // '/'
    case 56: case 100: return 18; // Alt
    case 57: return 32;  // Space
    case 58: return 20;  // Caps Lock
    case 87: return 122; // F11
    case 88: return 123; // F12
    case 102: return 36; // Home
    case 103: return 38; // Up
    case 104: return 33; // Page Up
    case 105: return 37; // Left
    case 106: return 39; // Right
    case 107: return 35; // End
    case 108: return 40; // Down
    case 109: return 34; // Page Down
    case 110: return 45; // Insert
    case 111: return 46; // Delete
    case 125: return 91; // Left Meta
    case 126: return 92; // Right Meta
    default: return 0;
    }
}

} // namespace virtio_input
} // namespace kernel
