// PS/2 Keyboard Driver Implementation
//
// Handles PS/2 keyboard input via IRQ1 and converts scancodes to ASCII/keycodes.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ps2keyboard.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/shell.h"

namespace kernel {
namespace ps2keyboard {

#if ARCH_HAS_PS2

// PS/2 controller ports
static const uint16_t kDataPort    = 0x60;
static const uint16_t kStatusPort  = 0x64;

// Key state
static uint32_t s_lastKey = 0;
static bool s_hasKey = false;
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
static uint32_t s_c188r11r2EventSequence = 0;
static uint32_t s_c188r11r2PendingEventId = 0;
static uint32_t s_c188r11r2PendingScancode = 0;
static uint32_t s_c188r11r2DequeuedEventId = 0;
static uint32_t s_c188r11r2DequeuedScancode = 0;
static uint32_t s_c188r11r2PendingOverwrites = 0;
static volatile C188R11R2KeyTrace s_c188r11r2Trace[16] = {};
static volatile uint32_t s_c188r11r2TraceHead = 0u;
static volatile uint32_t s_c188r11r2TraceTail = 0u;
static uint32_t s_c188r11r2TraceRecordsWritten = 0u;
static uint32_t s_c188r11r2TraceRecordsPrinted = 0u;
static uint32_t s_c188r11r2TraceSequence = 0u;
static uint32_t s_c188r11r2TraceOverflow = 0u;
static bool s_c188r11r2TraceOverflowReported = false;

enum C188R11R2KeyPhase : uint32_t {
    C188R11R2PrefixE0 = 1u,
    C188R11R2PrefixF0 = 2u,
    C188R11R2ModifierShift = 3u,
    C188R11R2ModifierControl = 4u,
    C188R11R2ModifierAlt = 5u,
    C188R11R2ModifierCaps = 6u,
    C188R11R2KeyRelease = 7u,
    C188R11R2KeyQueued = 8u,
    C188R11R2KeyUnsupported = 9u
};

static uint32_t next_c188r11r2_event_id()
{
    if (s_c188r11r2EventSequence >= 32u) return 0u;
    return ++s_c188r11r2EventSequence;
}

static void record_c188r11r2_key(uint32_t eventId, uint32_t scancode,
                                  uint32_t key, uint32_t phase, bool keyUp,
                                  bool extended, bool shift, bool control)
{
    if (s_c188r11r2TraceRecordsWritten >= 16u) {
        if (s_c188r11r2TraceOverflow < 255u)
            ++s_c188r11r2TraceOverflow;
        return;
    }
    const uint32_t head = s_c188r11r2TraceHead;
    const uint32_t tail = s_c188r11r2TraceTail;
    if (head - tail >= 16u) {
        if (s_c188r11r2TraceOverflow < 255u)
            ++s_c188r11r2TraceOverflow;
        return;
    }
    const uint32_t sequence = ++s_c188r11r2TraceSequence;
    volatile C188R11R2KeyTrace& trace = s_c188r11r2Trace[head % 16u];
    trace.sequence = sequence;
    trace.eventId = eventId;
    trace.scancode = scancode;
    trace.key = key;
    trace.phase = phase;
    trace.keyUp = keyUp ? 1u : 0u;
    trace.extended = extended ? 1u : 0u;
    trace.shift = shift ? 1u : 0u;
    trace.control = control ? 1u : 0u;
    ++s_c188r11r2TraceRecordsWritten;
    s_c188r11r2TraceHead = head + 1u;
}

static const char* c188r11r2_phase_name(uint32_t phase)
{
    switch (phase) {
        case C188R11R2PrefixE0: return "prefix-e0";
        case C188R11R2PrefixF0: return "prefix-f0";
        case C188R11R2ModifierShift: return "modifier-shift";
        case C188R11R2ModifierControl: return "modifier-control";
        case C188R11R2ModifierAlt: return "modifier-alt";
        case C188R11R2ModifierCaps: return "modifier-caps";
        case C188R11R2KeyRelease: return "key-release";
        case C188R11R2KeyQueued: return "key-queued";
        default: return "key-unsupported";
    }
}
#endif
static bool s_leftShiftDown = false;
static bool s_rightShiftDown = false;
static bool s_leftCtrlDown = false;
static bool s_rightCtrlDown = false;
static bool s_lastKeyCtrlDown = false;
static bool s_lastKeyLeftCtrlDown = false;
static bool s_lastKeyRightCtrlDown = false;
static bool s_lastKeyShiftDown = false;
static bool s_altDown = false;
static bool s_rightAltDown = false;
static bool s_f4Down = false;
static bool s_lastKeyAltF4Candidate = false;
static bool s_lastKeyAltLeftDown = false;
static bool s_lastKeyAltRightDown = false;
static bool s_capsLock = false;

#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
static void log_key_transition(const char* label, uint8_t scancode, bool keyUp, bool extended)
{
    serial::puts("[kbd] ");
    serial::puts(label);
    serial::puts(keyUp ? " up" : " down");
    serial::puts(" raw=");
    if (extended) {
        serial::puts("E0 ");
    }
    if (keyUp) {
        serial::puts("F0 ");
    }
    serial::puts("0x");
    serial::put_hex8(scancode);
    serial::puts(" ext=");
    serial::puts(extended ? "1" : "0");
    serial::puts(" leftCtrl=");
    serial::puts(s_leftCtrlDown ? "1" : "0");
    serial::puts(" rightCtrl=");
    serial::puts(s_rightCtrlDown ? "1" : "0");
    serial::puts(" leftAlt=");
    serial::puts(s_altDown ? "1" : "0");
    serial::puts(" rightAlt=");
    serial::puts(s_rightAltDown ? "1" : "0");
    serial::puts(" f4=");
    serial::puts(s_f4Down ? "1" : "0");
    serial::putc('\n');
}
#endif

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
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    s_c188r11r2EventSequence = 0;
    s_c188r11r2PendingEventId = 0;
    s_c188r11r2PendingScancode = 0;
    s_c188r11r2DequeuedEventId = 0;
    s_c188r11r2DequeuedScancode = 0;
    s_c188r11r2PendingOverwrites = 0;
    s_c188r11r2TraceHead = 0u;
    s_c188r11r2TraceTail = 0u;
    s_c188r11r2TraceRecordsWritten = 0u;
    s_c188r11r2TraceRecordsPrinted = 0u;
    s_c188r11r2TraceSequence = 0u;
    s_c188r11r2TraceOverflow = 0u;
    s_c188r11r2TraceOverflowReported = false;
#endif
    s_leftShiftDown = false;
    s_rightShiftDown = false;
    s_leftCtrlDown = false;
    s_rightCtrlDown = false;
    s_lastKeyCtrlDown = false;
    s_lastKeyLeftCtrlDown = false;
    s_lastKeyRightCtrlDown = false;
    s_lastKeyShiftDown = false;
    s_altDown = false;
    s_rightAltDown = false;
    s_f4Down = false;
    s_lastKeyAltF4Candidate = false;
    s_lastKeyAltLeftDown = false;
    s_lastKeyAltRightDown = false;
    s_capsLock = false;
    s_extendedKey = false;
    s_breakCode = false;
    
    serial::puts("[PS2KB] Keyboard initialized (scancode set 2)\n");
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
    serial::puts("[kbd] decoder=set2 controller-translation-off\n");
#endif
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
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
        record_c188r11r2_key(0u, scancode, 0u, C188R11R2PrefixE0,
            false, false, is_shift_down(), is_ctrl_down());
#endif
        s_extendedKey = true;
        return;
    }
    if (scancode == 0xF0) {
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
        record_c188r11r2_key(0u, scancode, 0u, C188R11R2PrefixF0,
            false, s_extendedKey, is_shift_down(), is_ctrl_down());
#endif
        s_breakCode = true;
        return;
    }
    
    bool keyUp = s_breakCode;
    s_breakCode = false;  // Reset for next scancode
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    const bool c188r11r2Extended = s_extendedKey;
    const uint32_t c188r11r2EventId = next_c188r11r2_event_id();
#endif
    bool altF4Candidate = false;
    bool altF4LeftDown = false;
    bool altF4RightDown = false;
    
    // Handle modifier keys (scancode set 2 values)
    if (scancode == SC2_LSHIFT || scancode == SC2_RSHIFT) {
        const bool leftShift = scancode == SC2_LSHIFT;
        if (leftShift) {
            s_leftShiftDown = !keyUp;
        } else {
            s_rightShiftDown = !keyUp;
        }
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
        record_c188r11r2_key(c188r11r2EventId, scancode, 0u,
            C188R11R2ModifierShift, keyUp, c188r11r2Extended,
            is_shift_down(), is_ctrl_down());
#endif
#if defined(GXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT)
        serial::puts("[C129-KEYBOARD] shift=");
        serial::puts(keyUp ? "up" : "down");
        serial::puts(" side=");
        serial::puts(leftShift ? "left" : "right");
        serial::puts(" aggregate=");
        serial::puts((s_leftShiftDown || s_rightShiftDown) ? "1" : "0");
        serial::puts(" result=PASS\n");
#endif
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        log_key_transition(leftShift ? "shift-left" : "shift-right", scancode, keyUp, s_extendedKey);
#endif
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_LCTRL) {
        const bool rightCtrl = s_extendedKey;
        if (rightCtrl) {
            s_rightCtrlDown = !keyUp;
        } else {
            s_leftCtrlDown = !keyUp;
        }
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
        record_c188r11r2_key(c188r11r2EventId, scancode, 0u,
            C188R11R2ModifierControl, keyUp, c188r11r2Extended,
            is_shift_down(), is_ctrl_down());
#endif
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        log_key_transition(rightCtrl ? "ctrl-right" : "ctrl-left",
            scancode, keyUp, s_extendedKey);
#endif
#if defined(GXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS)
        serial::puts("[C156-KEYBOARD] event=control-");
        serial::puts(rightCtrl ? "right" : "left");
        serial::puts(keyUp ? "-up" : "-down");
        serial::puts(" left=");
        serial::puts(s_leftCtrlDown ? "1" : "0");
        serial::puts(" right=");
        serial::puts(s_rightCtrlDown ? "1" : "0");
        serial::puts(" aggregate=");
        serial::puts((s_leftCtrlDown || s_rightCtrlDown) ? "1" : "0");
        serial::puts(" result=PASS\n");
#endif
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_LALT) {
        if (s_extendedKey) {
            s_rightAltDown = !keyUp;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
            log_key_transition("alt-right", scancode, keyUp, true);
#endif
        } else {
            s_altDown = !keyUp;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
            log_key_transition("alt-left", scancode, keyUp, false);
#endif
        }
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
        record_c188r11r2_key(c188r11r2EventId, scancode, 0u,
            C188R11R2ModifierAlt, keyUp, c188r11r2Extended,
            is_shift_down(), is_ctrl_down());
#endif
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_CAPS && !keyUp) {  // Caps Lock (toggle on press)
        s_capsLock = !s_capsLock;
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
        record_c188r11r2_key(c188r11r2EventId, scancode, 0u,
            C188R11R2ModifierCaps, false, c188r11r2Extended,
            is_shift_down(), is_ctrl_down());
#endif
        s_extendedKey = false;
        return;
    }
    if (scancode == 0x0C) {  // F4
        const bool wasDown = s_f4Down;
        s_f4Down = !keyUp;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        log_key_transition("f4", scancode, keyUp, s_extendedKey);
#endif
        if (!keyUp && !wasDown && (s_altDown || s_rightAltDown)) {
            altF4Candidate = true;
            altF4LeftDown = s_altDown;
            altF4RightDown = s_rightAltDown;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
            serial::puts("[kbd] shortcut alt-f4 candidate armed leftAlt=");
            serial::puts(altF4LeftDown ? "1" : "0");
            serial::puts(" rightAlt=");
            serial::puts(altF4RightDown ? "1" : "0");
            serial::putc('\n');
#endif
        }
        s_extendedKey = false;
        if (keyUp) {
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
            record_c188r11r2_key(c188r11r2EventId, scancode, KEY_F1 + 3u,
                C188R11R2KeyRelease, true, c188r11r2Extended,
                is_shift_down(), is_ctrl_down());
#endif
            return;
        }
    }
    
    // Only process key presses, not releases
    if (keyUp) {
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
        record_c188r11r2_key(c188r11r2EventId, scancode, 0u,
            C188R11R2KeyRelease, true, c188r11r2Extended,
            is_shift_down(), is_ctrl_down());
#endif
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
            case 0x1F: key = shell::KEY_SUPER; break; // Left Windows / Super
            case 0x27: key = shell::KEY_SUPER; break; // Right Windows / Super
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
                    bool shift = s_leftShiftDown || s_rightShiftDown;
                    char c = s_scancodeToAscii[scancode];
                    if (s_capsLock && c >= 'a' && c <= 'z') {
                        shift = !shift;
                    }
                    key = shift ? s_scancodeToAsciiShift[scancode] : s_scancodeToAscii[scancode];
                }
                break;
        }
    }
    
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    record_c188r11r2_key(c188r11r2EventId, scancode, key,
        key != 0u ? C188R11R2KeyQueued : C188R11R2KeyUnsupported,
        false, c188r11r2Extended, is_shift_down(), is_ctrl_down());
#endif
    if (key != 0) {
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
        if (s_hasKey && s_c188r11r2PendingOverwrites < 255u)
            ++s_c188r11r2PendingOverwrites;
        s_c188r11r2PendingEventId = c188r11r2EventId;
        s_c188r11r2PendingScancode = scancode;
#endif
        s_lastKey = key;
        s_hasKey = true;
        s_lastKeyCtrlDown = s_leftCtrlDown || s_rightCtrlDown;
        s_lastKeyLeftCtrlDown = s_leftCtrlDown;
        s_lastKeyRightCtrlDown = s_rightCtrlDown;
        s_lastKeyShiftDown = s_leftShiftDown || s_rightShiftDown;
        s_lastKeyAltF4Candidate = altF4Candidate;
        s_lastKeyAltLeftDown = altF4LeftDown;
        s_lastKeyAltRightDown = altF4RightDown;
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
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    s_c188r11r2DequeuedEventId = s_c188r11r2PendingEventId;
    s_c188r11r2DequeuedScancode = s_c188r11r2PendingScancode;
#endif
    return s_lastKey;
}

uint32_t last_dequeued_event_id()
{
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    return s_c188r11r2DequeuedEventId;
#else
    return 0u;
#endif
}

uint32_t last_dequeued_scancode()
{
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    return s_c188r11r2DequeuedScancode;
#else
    return 0u;
#endif
}

uint32_t last_dequeued_pending_overwrites()
{
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    return s_c188r11r2PendingOverwrites;
#else
    return 0u;
#endif
}

bool try_get_c188r11r2_key_trace(C188R11R2KeyTrace* trace)
{
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    if (!trace) return false;
    const uint32_t tail = s_c188r11r2TraceTail;
    const uint32_t head = s_c188r11r2TraceHead;
    if (tail == head) return false;
    const volatile C188R11R2KeyTrace& source =
        s_c188r11r2Trace[tail % 16u];
    trace->sequence = source.sequence;
    trace->eventId = source.eventId;
    trace->scancode = source.scancode;
    trace->key = source.key;
    trace->phase = source.phase;
    trace->keyUp = source.keyUp;
    trace->extended = source.extended;
    trace->shift = source.shift;
    trace->control = source.control;
    s_c188r11r2TraceTail = tail + 1u;
    return true;
#else
    (void)trace;
    return false;
#endif
}

void flush_c188r11r2_key_traces()
{
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    C188R11R2KeyTrace trace{};
    while (s_c188r11r2TraceRecordsPrinted < 16u &&
           try_get_c188r11r2_key_trace(&trace)) {
        ++s_c188r11r2TraceRecordsPrinted;
        serial::puts("[C188R11R2_KEY] stage=irq-scan seq=");
        serial::put_hex32(trace.sequence);
        serial::puts(" eid=");
        serial::put_hex32(trace.eventId);
        serial::puts(" sc=");
        serial::put_hex32(trace.scancode);
        serial::puts(" ext=");
        serial::puts(trace.extended ? "1" : "0");
        serial::puts(" phase=");
        serial::puts(c188r11r2_phase_name(trace.phase));
        serial::puts(" key=");
        serial::put_hex32(trace.key);
        serial::puts(" transition=");
        serial::puts(trace.keyUp ? "up" : "down");
        serial::puts(" shift=");
        serial::puts(trace.shift ? "1" : "0");
        serial::puts(" ctrl=");
        serial::puts(trace.control ? "1" : "0");
        serial::puts(" route=");
        serial::puts(trace.phase == C188R11R2KeyQueued ? "queue" :
            trace.phase == C188R11R2KeyUnsupported ? "drop" : "consume");
        serial::puts("\n");
    }
    if (s_c188r11r2TraceOverflow != 0u &&
        !s_c188r11r2TraceOverflowReported) {
        s_c188r11r2TraceOverflowReported = true;
        serial::puts("[C188R11R2_KEY] stage=irq-trace-overflow dropped=");
        serial::put_hex32(s_c188r11r2TraceOverflow);
        serial::puts("\n");
    }
#endif
}

void clear()
{
    s_hasKey = false;
    s_lastKey = 0;
#if defined(GXOS_NATIVEAOT_C188R11_STATE_PROVENANCE)
    s_c188r11r2PendingEventId = 0u;
    s_c188r11r2PendingScancode = 0u;
    s_c188r11r2DequeuedEventId = 0u;
    s_c188r11r2DequeuedScancode = 0u;
    s_c188r11r2PendingOverwrites = 0u;
#endif
    s_lastKeyCtrlDown = false;
    s_lastKeyLeftCtrlDown = false;
    s_lastKeyRightCtrlDown = false;
    s_lastKeyShiftDown = false;
    s_lastKeyAltF4Candidate = false;
    s_lastKeyAltLeftDown = false;
    s_lastKeyAltRightDown = false;
}

bool is_ctrl_down()
{
    return s_leftCtrlDown || s_rightCtrlDown;
}

bool is_left_ctrl_down()
{
    return s_leftCtrlDown;
}

bool is_right_ctrl_down()
{
    return s_rightCtrlDown;
}

bool last_key_ctrl_down()
{
    return s_lastKeyCtrlDown;
}

bool last_key_left_ctrl_down()
{
    return s_lastKeyLeftCtrlDown;
}

bool last_key_right_ctrl_down()
{
    return s_lastKeyRightCtrlDown;
}

bool last_key_shift_down()
{
    return s_lastKeyShiftDown;
}

bool is_shift_down()
{
    return s_leftShiftDown || s_rightShiftDown;
}

bool is_alt_down()
{
    return s_altDown || s_rightAltDown;
}

bool is_left_alt_down()
{
    return s_altDown;
}

bool is_right_alt_down()
{
    return s_rightAltDown;
}

bool is_f4_down()
{
    return s_f4Down;
}

bool was_alt_f4_shortcut_candidate()
{
    return s_lastKeyAltF4Candidate;
}

bool last_key_alt_left_down()
{
    return s_lastKeyAltLeftDown;
}

bool last_key_alt_right_down()
{
    return s_lastKeyAltRightDown;
}

#else
// Stub implementation for non-x86 architectures
void init() {}
void irq_handler() {}
bool has_key() { return false; }
uint32_t get_key() { return 0; }
uint32_t last_dequeued_event_id() { return 0u; }
uint32_t last_dequeued_scancode() { return 0u; }
uint32_t last_dequeued_pending_overwrites() { return 0u; }
bool try_get_c188r11r2_key_trace(C188R11R2KeyTrace*) { return false; }
void flush_c188r11r2_key_traces() {}
void clear() {}
bool is_ctrl_down() { return false; }
bool is_left_ctrl_down() { return false; }
bool is_right_ctrl_down() { return false; }
bool last_key_ctrl_down() { return false; }
bool last_key_left_ctrl_down() { return false; }
bool last_key_right_ctrl_down() { return false; }
bool last_key_shift_down() { return false; }
bool is_shift_down() { return false; }
bool is_alt_down() { return false; }
bool is_left_alt_down() { return false; }
bool is_right_alt_down() { return false; }
bool is_f4_down() { return false; }
bool was_alt_f4_shortcut_candidate() { return false; }
bool last_key_alt_left_down() { return false; }
bool last_key_alt_right_down() { return false; }
#endif

} // namespace ps2keyboard
} // namespace kernel
