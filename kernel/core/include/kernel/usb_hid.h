// USB HID (Human Interface Device) Class Driver
//
// Supports:
//   - Boot keyboard protocol (6KRO, modifier bitmask)
//   - Boot mouse protocol (3-button + scroll via report)
//   - Gamepad / joystick reports (basic axis + button mapping)
//   - Touchscreen single-point contact reports
//   - Full HID report descriptor parsing for non-boot devices
//
// Reference: USB HID 1.11, HID Usage Tables 1.12
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_HID_H
#define KERNEL_USB_HID_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_hid {

// ================================================================
// HID class-specific requests
// ================================================================

enum HIDRequest : uint8_t {
    HID_REQ_GET_REPORT   = 0x01,
    HID_REQ_GET_IDLE     = 0x02,
    HID_REQ_GET_PROTOCOL = 0x03,
    HID_REQ_SET_REPORT   = 0x09,
    HID_REQ_SET_IDLE     = 0x0A,
    HID_REQ_SET_PROTOCOL = 0x0B,
};

// Protocol values for SET_PROTOCOL / GET_PROTOCOL
enum HIDProtocolMode : uint8_t {
    PROTOCOL_BOOT   = 0,
    PROTOCOL_REPORT = 1,
};

// ================================================================
// Keyboard state (boot protocol: 8 bytes)
//
//  Byte 0 : modifier keys bitmask
//  Byte 1 : reserved (OEM)
//  Bytes 2-7 : up to 6 simultaneous key scancodes
// ================================================================

struct KeyboardState {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
};

// Modifier bitmask constants
enum KeyboardModifier : uint8_t {
    MOD_LEFT_CTRL   = 0x01,
    MOD_LEFT_SHIFT  = 0x02,
    MOD_LEFT_ALT    = 0x04,
    MOD_LEFT_GUI    = 0x08,
    MOD_RIGHT_CTRL  = 0x10,
    MOD_RIGHT_SHIFT = 0x20,
    MOD_RIGHT_ALT   = 0x40,
    MOD_RIGHT_GUI   = 0x80,
};

// ================================================================
// Mouse state (boot protocol: 3-4 bytes)
//
//  Byte 0 : buttons bitmask (bit0=left, bit1=right, bit2=middle)
//  Byte 1 : X displacement (signed)
//  Byte 2 : Y displacement (signed)
//  Byte 3 : wheel (optional, signed)
// ================================================================

struct MouseState {
    uint8_t buttons;
    int8_t  dx;
    int8_t  dy;
    int8_t  wheel;
};

// ================================================================
// Gamepad state (generic mapping)
// ================================================================

static const uint8_t MAX_GAMEPAD_AXES    = 6;
static const uint8_t MAX_GAMEPAD_BUTTONS = 16;

struct GamepadState {
    int16_t axes[MAX_GAMEPAD_AXES];       // normalised to [-32768, 32767]
    uint16_t buttons;                      // bitmask, up to 16 buttons
    uint8_t  hatSwitch;                    // D-pad: 0-7 directions, 0xFF = centred
};

// ================================================================
// Touchscreen state (single contact)
// ================================================================

struct TouchscreenState {
    bool     contact;       // finger touching
    uint16_t x;             // absolute X
    uint16_t y;             // absolute Y
    uint16_t pressure;      // contact pressure (0 = no data)
};

// ================================================================
// HID report field descriptor (parsed from report descriptor)
// ================================================================

struct ReportField {
    uint16_t usagePage;
    uint16_t usage;
    int32_t  logicalMin;
    int32_t  logicalMax;
    uint16_t reportSize;    // bits per field
    uint16_t reportCount;
    uint8_t  reportId;
    bool     isRelative;    // true = relative, false = absolute
    bool     isVariable;    // true = variable, false = array
};

static const uint8_t MAX_REPORT_FIELDS = 32;

// ================================================================
// HID device instance
// ================================================================

enum HIDDeviceType : uint8_t {
    HID_TYPE_UNKNOWN     = 0,
    HID_TYPE_KEYBOARD    = 1,
    HID_TYPE_MOUSE       = 2,
    HID_TYPE_GAMEPAD     = 3,
    HID_TYPE_TOUCHSCREEN = 4,
};

struct HIDDevice {
    bool          active;
    uint8_t       usbAddress;
    uint8_t       interfaceNum;
    uint8_t       inEndpoint;         // interrupt IN endpoint address
    uint16_t      inMaxPacket;
    uint8_t       inInterval;
    HIDDeviceType type;
    bool          bootProtocol;       // true if using boot protocol

    // Parsed report descriptor fields
    ReportField   fields[MAX_REPORT_FIELDS];
    uint8_t       fieldCount;

    // Current state (union-style, selected by type)
    KeyboardState    keyboard;
    MouseState       mouse;
    GamepadState     gamepad;
    TouchscreenState touch;
};

static const uint8_t MAX_HID_DEVICES = 8;

// ================================================================
// Public API
// ================================================================

// Initialise the HID class driver.
void init();

// Probe a USB device; if it has HID interfaces, claim them.
// Returns true if at least one HID interface was claimed.
bool probe(uint8_t usbAddress);

// Poll all active HID devices for new reports.
void poll();

// Release all HID interfaces on a device (on detach).
void release(uint8_t usbAddress);

// ----------------------------------------------------------------
// Keyboard accessors
// ----------------------------------------------------------------

// Return the most recently received keyboard state.
const KeyboardState* get_keyboard_state();

// Return true if a specific key scancode is currently pressed.
bool is_key_pressed(uint8_t scancode);

// Return true if any keyboard is present.
bool has_keyboard();

// ----------------------------------------------------------------
// Mouse accessors
// ----------------------------------------------------------------

// Return the most recently received mouse report.
const MouseState* get_mouse_state();

// Return true if any USB mouse is present.
bool has_mouse();

// ----------------------------------------------------------------
// Gamepad accessors
// ----------------------------------------------------------------

const GamepadState* get_gamepad_state(uint8_t index);
uint8_t gamepad_count();

// ----------------------------------------------------------------
// Touchscreen accessors
// ----------------------------------------------------------------

const TouchscreenState* get_touchscreen_state();
bool has_touchscreen();

} // namespace usb_hid
} // namespace kernel

#endif // KERNEL_USB_HID_H
