// USB HID Class Driver — Implementation
//
// Boot keyboard, boot mouse, gamepad, and touchscreen handling.
// Parses HID report descriptors for non-boot devices and polls
// interrupt IN endpoints for input reports.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_hid.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_hid {

// ================================================================
// Internal state
// ================================================================

static HIDDevice s_devices[MAX_HID_DEVICES];
static uint8_t   s_deviceCount = 0;

static KeyboardState    s_keyboard;
static MouseState       s_mouse;
static GamepadState     s_gamepad[4];
static TouchscreenState s_touch;
static uint8_t          s_gamepadCount = 0;

static bool s_hasKeyboard    = false;
static bool s_hasMouse       = false;
static bool s_hasTouchscreen = false;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcopy(void* dst, const void* src, uint32_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < len; ++i) d[i] = s[i];
}

// ================================================================
// HID class-specific control requests
// ================================================================

static usb::TransferStatus hid_set_protocol(uint8_t addr,
                                            uint8_t iface,
                                            HIDProtocolMode mode)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21; // Host-to-device, class, interface
    setup.bRequest      = HID_REQ_SET_PROTOCOL;
    setup.wValue        = mode;
    setup.wIndex        = iface;
    setup.wLength       = 0;
    return usb::control_transfer(addr, &setup, nullptr, 0);
}

static usb::TransferStatus hid_set_idle(uint8_t addr,
                                        uint8_t iface,
                                        uint8_t reportId,
                                        uint8_t durationMs4)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21;
    setup.bRequest      = HID_REQ_SET_IDLE;
    setup.wValue        = static_cast<uint16_t>((durationMs4 << 8) | reportId);
    setup.wIndex        = iface;
    setup.wLength       = 0;
    return usb::control_transfer(addr, &setup, nullptr, 0);
}

static usb::TransferStatus hid_get_report(uint8_t addr,
                                          uint8_t iface,
                                          uint8_t reportType,
                                          uint8_t reportId,
                                          void* buf,
                                          uint16_t len)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1; // Device-to-host, class, interface
    setup.bRequest      = HID_REQ_GET_REPORT;
    setup.wValue        = static_cast<uint16_t>((reportType << 8) | reportId);
    setup.wIndex        = iface;
    setup.wLength       = len;
    return usb::control_transfer(addr, &setup, buf, len);
}

// ================================================================
// HID report descriptor parser (minimal)
//
// Extracts Input fields from a HID report descriptor so we can
// locate axis, button, and coordinate data in incoming reports.
// ================================================================

static void parse_report_descriptor(HIDDevice* dev,
                                    const uint8_t* data,
                                    uint16_t len)
{
    // Global state
    uint16_t usagePage   = 0;
    int32_t  logicalMin  = 0;
    int32_t  logicalMax  = 0;
    uint16_t reportSize  = 0;
    uint16_t reportCount = 0;
    uint8_t  reportId    = 0;

    // Local state
    uint16_t usage       = 0;
    uint16_t usageMin    = 0;
    uint16_t usageMax    = 0;
    (void)usageMin; (void)usageMax;

    dev->fieldCount = 0;

    uint16_t i = 0;
    while (i < len && dev->fieldCount < MAX_REPORT_FIELDS) {
        uint8_t prefix = data[i];
        uint8_t bSize  = prefix & 0x03;
        uint8_t bType  = (prefix >> 2) & 0x03;
        uint8_t bTag   = (prefix >> 4) & 0x0F;

        if (bSize == 3) bSize = 4; // size encoding: 3 means 4 bytes

        int32_t value = 0;
        if (i + 1 < len && bSize >= 1) value = data[i + 1];
        if (i + 2 < len && bSize >= 2) value = static_cast<int16_t>(data[i + 1] | (data[i + 2] << 8));
        if (i + 4 < len && bSize >= 4) {
            value = static_cast<int32_t>(
                data[i + 1] | (data[i + 2] << 8) |
                (data[i + 3] << 16) | (data[i + 4] << 24));
        }

        if (bType == 1) {
            // Global item
            switch (bTag) {
                case 0x00: usagePage   = static_cast<uint16_t>(value); break;
                case 0x01: logicalMin  = value; break;
                case 0x02: logicalMax  = value; break;
                case 0x07: reportSize  = static_cast<uint16_t>(value); break;
                case 0x09: reportCount = static_cast<uint16_t>(value); break;
                case 0x08: reportId    = static_cast<uint8_t>(value); break;
            }
        } else if (bType == 2) {
            // Local item
            switch (bTag) {
                case 0x00: usage    = static_cast<uint16_t>(value); break;
                case 0x01: usageMin = static_cast<uint16_t>(value); break;
                case 0x02: usageMax = static_cast<uint16_t>(value); break;
            }
        } else if (bType == 0) {
            // Main item
            if (bTag == 0x08) {
                // Input
                ReportField& f = dev->fields[dev->fieldCount];
                f.usagePage   = usagePage;
                f.usage       = usage;
                f.logicalMin  = logicalMin;
                f.logicalMax  = logicalMax;
                f.reportSize  = reportSize;
                f.reportCount = reportCount;
                f.reportId    = reportId;
                f.isRelative  = (value & 0x04) != 0;
                f.isVariable  = (value & 0x02) != 0;
                dev->fieldCount++;

                // Reset local items
                usage = 0;
                usageMin = 0;
                usageMax = 0;
            }
        }

        i += static_cast<uint16_t>(1 + bSize);
    }
}

// ================================================================
// Determine device type from interface class/protocol or parsed
// report descriptor usage pages.
// ================================================================

static HIDDeviceType classify_device(const HIDDevice* dev,
                                     const usb::Device* usbDev,
                                     uint8_t ifaceIdx)
{
    // Check boot protocol first
    if (usbDev->interfaceSubClass[ifaceIdx] == usb::HID_SUBCLASS_BOOT) {
        if (usbDev->interfaceProtocol[ifaceIdx] == usb::HID_PROTOCOL_KEYBOARD)
            return HID_TYPE_KEYBOARD;
        if (usbDev->interfaceProtocol[ifaceIdx] == usb::HID_PROTOCOL_MOUSE)
            return HID_TYPE_MOUSE;
    }

    // Classify from report descriptor usage pages
    for (uint8_t f = 0; f < dev->fieldCount; ++f) {
        uint16_t page = dev->fields[f].usagePage;
        uint16_t usg  = dev->fields[f].usage;

        if (page == 0x01) { // Generic Desktop
            if (usg == 0x06) return HID_TYPE_KEYBOARD;
            if (usg == 0x02) return HID_TYPE_MOUSE;
            if (usg == 0x04 || usg == 0x05) return HID_TYPE_GAMEPAD;
        }
        if (page == 0x0D) { // Digitizer
            if (usg == 0x04) return HID_TYPE_TOUCHSCREEN;
        }
    }

    return HID_TYPE_UNKNOWN;
}

// ================================================================
// Process a boot keyboard report (8 bytes)
// ================================================================

static void process_boot_keyboard(HIDDevice* dev, const uint8_t* report, uint16_t len)
{
    if (len < 8) return;

    dev->keyboard.modifiers = report[0];
    dev->keyboard.reserved  = report[1];
    for (int k = 0; k < 6; ++k)
        dev->keyboard.keys[k] = report[k + 2];

    memcopy(&s_keyboard, &dev->keyboard, sizeof(KeyboardState));
}

// ================================================================
// Process a boot mouse report (3-4 bytes)
// ================================================================

static void process_boot_mouse(HIDDevice* dev, const uint8_t* report, uint16_t len)
{
    if (len < 3) return;

    dev->mouse.buttons = report[0] & 0x07;
    dev->mouse.dx      = static_cast<int8_t>(report[1]);
    dev->mouse.dy      = static_cast<int8_t>(report[2]);
    dev->mouse.wheel   = (len >= 4) ? static_cast<int8_t>(report[3]) : 0;

    memcopy(&s_mouse, &dev->mouse, sizeof(MouseState));
}

// ================================================================
// Process a generic gamepad report (non-boot)
//
// Reads the first parsed axis and button fields and maps them to
// the GamepadState structure.  This is intentionally simplistic;
// a full implementation would walk every report field with proper
// bit-level extraction.
// ================================================================

static void process_gamepad(HIDDevice* dev, const uint8_t* report, uint16_t len)
{
    if (len < 2) return;

    uint8_t axisIdx   = 0;
    uint16_t buttons  = 0;
    uint8_t  hat      = 0xFF;

    for (uint8_t f = 0; f < dev->fieldCount && axisIdx < MAX_GAMEPAD_AXES; ++f) {
        const ReportField& fld = dev->fields[f];

        if (fld.usagePage == 0x01) { // Generic Desktop
            // Axes (X, Y, Z, Rx, Ry, Rz)
            if (fld.usage >= 0x30 && fld.usage <= 0x35 && fld.isVariable) {
                // Simplified: read first byte as 8-bit axis
                uint8_t byteOff = static_cast<uint8_t>(2 + axisIdx);
                if (byteOff < len) {
                    int32_t raw = static_cast<int32_t>(report[byteOff]);
                    // Normalise to signed 16-bit
                    int32_t range = fld.logicalMax - fld.logicalMin;
                    if (range > 0) {
                        raw = ((raw - fld.logicalMin) * 65535 / range) - 32768;
                    }
                    dev->gamepad.axes[axisIdx] = static_cast<int16_t>(raw);
                    axisIdx++;
                }
            }
            // Hat switch
            if (fld.usage == 0x39 && fld.isVariable) {
                uint8_t byteOff = static_cast<uint8_t>(2 + axisIdx);
                if (byteOff < len) {
                    hat = report[byteOff] & 0x0F;
                    if (hat > 7) hat = 0xFF; // centred
                }
            }
        }

        if (fld.usagePage == 0x09) { // Button page
            // Simplified: read up to 2 bytes of button bits
            if (len >= 2) {
                buttons = report[len - 2] | (static_cast<uint16_t>(report[len - 1]) << 8);
            }
        }
    }

    dev->gamepad.buttons   = buttons;
    dev->gamepad.hatSwitch = hat;

    // Copy to shared state for first gamepad
    for (uint8_t g = 0; g < 4; ++g) {
        if (s_devices[g].active && s_devices[g].type == HID_TYPE_GAMEPAD &&
            &s_devices[g] == dev) {
            memcopy(&s_gamepad[g], &dev->gamepad, sizeof(GamepadState));
            break;
        }
    }
}

// ================================================================
// Process a touchscreen report
//
// Expects Digitizer usage page fields for Tip Switch (contact),
// X, Y, and optionally Tip Pressure.
// ================================================================

static void process_touchscreen(HIDDevice* dev, const uint8_t* report, uint16_t len)
{
    if (len < 4) return;

    // Very simplified: first byte = contact + padding, then X (2 bytes), Y (2 bytes)
    dev->touch.contact  = (report[0] & 0x01) != 0;
    if (len >= 5) {
        dev->touch.x = static_cast<uint16_t>(report[1] | (report[2] << 8));
        dev->touch.y = static_cast<uint16_t>(report[3] | (report[4] << 8));
    }
    if (len >= 7) {
        dev->touch.pressure = static_cast<uint16_t>(report[5] | (report[6] << 8));
    }

    memcopy(&s_touch, &dev->touch, sizeof(TouchscreenState));
}

// ================================================================
// Dispatch an incoming report to the correct handler
// ================================================================

static void dispatch_report(HIDDevice* dev, const uint8_t* report, uint16_t len)
{
    switch (dev->type) {
        case HID_TYPE_KEYBOARD:
            process_boot_keyboard(dev, report, len);
            break;
        case HID_TYPE_MOUSE:
            process_boot_mouse(dev, report, len);
            break;
        case HID_TYPE_GAMEPAD:
            process_gamepad(dev, report, len);
            break;
        case HID_TYPE_TOUCHSCREEN:
            process_touchscreen(dev, report, len);
            break;
        default:
            break;
    }
}

// ================================================================
// Find an interrupt IN endpoint for a HID interface
// ================================================================

static bool find_interrupt_in(const usb::Device* usbDev,
                              uint8_t* epAddr,
                              uint16_t* maxPkt,
                              uint8_t* interval)
{
    for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i) {
        const usb::Endpoint& ep = usbDev->endpoints[i];
        if (ep.active &&
            ep.type == usb::TRANSFER_INTERRUPT &&
            ep.dir == usb::DIR_DEVICE_TO_HOST) {
            *epAddr  = ep.address;
            *maxPkt  = ep.maxPacketSize;
            *interval = ep.interval;
            return true;
        }
    }
    return false;
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    memzero(&s_keyboard, sizeof(s_keyboard));
    memzero(&s_mouse, sizeof(s_mouse));
    memzero(s_gamepad, sizeof(s_gamepad));
    memzero(&s_touch, sizeof(s_touch));
    s_deviceCount    = 0;
    s_gamepadCount   = 0;
    s_hasKeyboard    = false;
    s_hasMouse       = false;
    s_hasTouchscreen = false;
}

bool probe(uint8_t usbAddress)
{
    const usb::Device* usbDev = usb::get_device(usbAddress);
    if (!usbDev) return false;

    bool claimed = false;

    for (uint8_t iface = 0; iface < usbDev->numInterfaces; ++iface) {
        if (usbDev->interfaceClass[iface] != usb::CLASS_HID) continue;
        if (s_deviceCount >= MAX_HID_DEVICES) break;

        HIDDevice* dev = &s_devices[s_deviceCount];
        memzero(dev, sizeof(HIDDevice));

        dev->usbAddress   = usbAddress;
        dev->interfaceNum = iface;

        // Find interrupt IN endpoint
        if (!find_interrupt_in(usbDev, &dev->inEndpoint,
                               &dev->inMaxPacket, &dev->inInterval)) {
            continue; // no interrupt endpoint — skip
        }

        // Determine if we should use boot protocol
        bool isBoot = (usbDev->interfaceSubClass[iface] == usb::HID_SUBCLASS_BOOT);

        if (isBoot) {
            // Switch to boot protocol for simplicity
            hid_set_protocol(usbAddress, iface, PROTOCOL_BOOT);
            dev->bootProtocol = true;
        } else {
            // Fetch and parse the HID report descriptor
            uint8_t reportBuf[256];
            memzero(reportBuf, sizeof(reportBuf));

            usb::TransferStatus st = usb::get_descriptor(
                usbAddress, usb::DESC_HID_REPORT, 0, iface,
                reportBuf, sizeof(reportBuf));

            if (st == usb::XFER_SUCCESS) {
                parse_report_descriptor(dev, reportBuf, sizeof(reportBuf));
            }
            dev->bootProtocol = false;
        }

        // Set idle rate to 0 (only send reports when data changes)
        hid_set_idle(usbAddress, iface, 0, 0);

        // Classify
        dev->type = classify_device(dev, usbDev, iface);

        dev->active = true;
        s_deviceCount++;
        claimed = true;

        switch (dev->type) {
            case HID_TYPE_KEYBOARD:  s_hasKeyboard = true; break;
            case HID_TYPE_MOUSE:     s_hasMouse = true; break;
            case HID_TYPE_TOUCHSCREEN: s_hasTouchscreen = true; break;
            case HID_TYPE_GAMEPAD:
                if (s_gamepadCount < 4) s_gamepadCount++;
                break;
            default:
                break;
        }
    }

    return claimed;
}

void poll()
{
    for (uint8_t i = 0; i < MAX_HID_DEVICES; ++i) {
        HIDDevice* dev = &s_devices[i];
        if (!dev->active) continue;

        uint8_t reportBuf[64];
        uint16_t bytesRead = 0;

        usb::TransferStatus st = usb::hci::interrupt_transfer(
            dev->usbAddress, dev->inEndpoint,
            reportBuf, dev->inMaxPacket, &bytesRead);

        if (st == usb::XFER_SUCCESS && bytesRead > 0) {
            dispatch_report(dev, reportBuf, bytesRead);
        }
        // NAK is expected when there's no new data — not an error
    }
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_HID_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            s_devices[i].active = false;
            s_deviceCount--;
        }
    }

    // Recompute capability flags
    s_hasKeyboard    = false;
    s_hasMouse       = false;
    s_hasTouchscreen = false;
    s_gamepadCount   = 0;
    for (uint8_t i = 0; i < MAX_HID_DEVICES; ++i) {
        if (!s_devices[i].active) continue;
        switch (s_devices[i].type) {
            case HID_TYPE_KEYBOARD:    s_hasKeyboard = true; break;
            case HID_TYPE_MOUSE:       s_hasMouse = true; break;
            case HID_TYPE_TOUCHSCREEN: s_hasTouchscreen = true; break;
            case HID_TYPE_GAMEPAD:     s_gamepadCount++; break;
            default: break;
        }
    }
}

// ----------------------------------------------------------------
// Keyboard accessors
// ----------------------------------------------------------------

const KeyboardState* get_keyboard_state() { return &s_keyboard; }

bool is_key_pressed(uint8_t scancode)
{
    for (int k = 0; k < 6; ++k) {
        if (s_keyboard.keys[k] == scancode) return true;
    }
    return false;
}

bool has_keyboard() { return s_hasKeyboard; }

// ----------------------------------------------------------------
// Mouse accessors
// ----------------------------------------------------------------

const MouseState* get_mouse_state() { return &s_mouse; }
bool has_mouse() { return s_hasMouse; }

// ----------------------------------------------------------------
// Gamepad accessors
// ----------------------------------------------------------------

const GamepadState* get_gamepad_state(uint8_t index)
{
    if (index >= 4) return nullptr;
    return &s_gamepad[index];
}

uint8_t gamepad_count() { return s_gamepadCount; }

// ----------------------------------------------------------------
// Touchscreen accessors
// ----------------------------------------------------------------

const TouchscreenState* get_touchscreen_state() { return &s_touch; }
bool has_touchscreen() { return s_hasTouchscreen; }

} // namespace usb_hid
} // namespace kernel
