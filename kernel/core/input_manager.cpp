//
// Input Manager - Implementation
//
// Unified input abstraction layer with automatic fallback between
// input sources based on priority and availability.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/input_manager.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"

// Include available input backends
#if ARCH_HAS_PS2
#include "include/kernel/ps2mouse.h"
#endif

#if ARCH_HAS_USB && defined(KERNEL_HAS_USB_HID)
#include "include/kernel/usb_hid.h"
#endif

// VirtIO input is optional
#if defined(KERNEL_HAS_VIRTIO_INPUT)
#include "include/kernel/virtio_input.h"
#endif

namespace kernel {
namespace input {

// ================================================================
// Internal state
// ================================================================

static MouseState    s_mouse;
static KeyboardState s_keyboard;

static int32_t s_screenWidth  = 1024;
static int32_t s_screenHeight = 768;

// Available sources bitmask
static uint8_t s_availableSources = 0;

// Active sources
static InputSource s_activeMouseSource    = InputSource::None;
static InputSource s_activeKeyboardSource = InputSource::None;

// Preferred source override (0 = auto)
static InputSource s_preferredSource = InputSource::None;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ================================================================
// Source detection and initialization
// ================================================================

static void detect_sources()
{
    s_availableSources = 0;

#if ARCH_HAS_USB && defined(KERNEL_HAS_USB_HID)
    // Check USB HID
    if (usb_hid::has_mouse()) {
        s_availableSources |= (1 << static_cast<uint8_t>(InputSource::USB_HID));
        serial::puts("[INPUT] USB HID mouse detected\n");
    }
#endif

#if ARCH_HAS_PS2
    // PS/2 is always available on x86 if compiled in
    s_availableSources |= (1 << static_cast<uint8_t>(InputSource::PS2));
    serial::puts("[INPUT] PS/2 mouse available\n");
#endif

#if defined(KERNEL_HAS_VIRTIO_INPUT)
    if (virtio_input::has_mouse()) {
        s_availableSources |= (1 << static_cast<uint8_t>(InputSource::VirtIO));
        serial::puts("[INPUT] VirtIO input detected\n");
    }
#endif

    // Platform-specific sources would be detected here
    // (ADB on Mac, Sun mouse on SPARC, etc.)
}

static void select_best_source()
{
    // Select mouse source based on priority
    // Priority: USB_HID > PS2 > VirtIO > Platform
    
    InputSource candidates[] = {
        InputSource::USB_HID,
        InputSource::PS2,
        InputSource::VirtIO,
        InputSource::Platform,
    };

    // Use preferred source if set and available
    if (s_preferredSource != InputSource::None) {
        if (is_source_available(s_preferredSource)) {
            s_activeMouseSource = s_preferredSource;
            s_activeKeyboardSource = s_preferredSource;
            serial::puts("[INPUT] Using preferred source: ");
            serial::puts(source_name(s_preferredSource));
            serial::putc('\n');
            return;
        }
    }

    // Auto-select best available source
    s_activeMouseSource = InputSource::None;
    for (auto src : candidates) {
        if (is_source_available(src)) {
            s_activeMouseSource = src;
            break;
        }
    }

    // For keyboard, use same logic (could be different source in future)
    s_activeKeyboardSource = s_activeMouseSource;

    if (s_activeMouseSource != InputSource::None) {
        serial::puts("[INPUT] Active mouse source: ");
        serial::puts(source_name(s_activeMouseSource));
        serial::putc('\n');
    } else {
        serial::puts("[INPUT] WARNING: No mouse input source available\n");
    }
}

// ================================================================
// Poll handlers for each source
// ================================================================

#if ARCH_HAS_PS2
static void poll_ps2_mouse()
{
    // PS/2 uses interrupt-driven updates, so we just read current state
    // The IRQ handler has already updated the internal state
    
    int32_t x = ps2mouse::get_x();
    int32_t y = ps2mouse::get_y();
    uint8_t buttons = ps2mouse::get_buttons();
    int8_t scroll = ps2mouse::get_scroll_delta();
    
    // Check if anything changed
    if (x != s_mouse.x || y != s_mouse.y || 
        buttons != s_mouse.buttons || scroll != 0) {
        s_mouse.dirty = true;
    }
    
    s_mouse.x = x;
    s_mouse.y = y;
    s_mouse.buttons = buttons;
    s_mouse.scrollY = scroll;
    s_mouse.scrollX = 0;
    s_mouse.mode = PositionMode::Relative;
    s_mouse.source = InputSource::PS2;
}
#endif

#if ARCH_HAS_USB && defined(KERNEL_HAS_USB_HID)
static void poll_usb_hid_mouse()
{
    // Poll USB HID subsystem
    usb_hid::poll();
    
    const usb_hid::MouseState* usbMouse = usb_hid::get_mouse_state();
    if (!usbMouse) return;
    
    // USB HID boot mouse reports relative deltas
    // USB tablet reports absolute coordinates
    // For now, handle both as relative (boot protocol)
    
    int32_t dx = usbMouse->dx;
    int32_t dy = usbMouse->dy;
    
    // Apply deltas to current position
    int32_t newX = clamp(s_mouse.x + dx, 0, s_screenWidth - 1);
    int32_t newY = clamp(s_mouse.y + dy, 0, s_screenHeight - 1);
    
    // Check if anything changed
    if (newX != s_mouse.x || newY != s_mouse.y ||
        usbMouse->buttons != s_mouse.buttons || usbMouse->wheel != 0) {
        s_mouse.dirty = true;
    }
    
    s_mouse.x = newX;
    s_mouse.y = newY;
    s_mouse.buttons = usbMouse->buttons;
    s_mouse.scrollY = usbMouse->wheel;
    s_mouse.scrollX = 0;
    s_mouse.mode = PositionMode::Relative;
    s_mouse.source = InputSource::USB_HID;
}
#endif // ARCH_HAS_USB && KERNEL_HAS_USB_HID

#if defined(KERNEL_HAS_VIRTIO_INPUT)
static void poll_virtio_input()
{
    virtio_input::poll();
    
    const virtio_input::MouseState* vioMouse = virtio_input::get_mouse_state();
    if (!vioMouse) return;
    
    // VirtIO input typically provides absolute coordinates
    int32_t x = static_cast<int32_t>(vioMouse->x);
    int32_t y = static_cast<int32_t>(vioMouse->y);
    
    // Scale to screen dimensions if needed
    // VirtIO tablet usually reports in 0-32767 range
    if (vioMouse->is_absolute) {
        x = (x * s_screenWidth) / 32768;
        y = (y * s_screenHeight) / 32768;
    }
    
    x = clamp(x, 0, s_screenWidth - 1);
    y = clamp(y, 0, s_screenHeight - 1);
    
    if (x != s_mouse.x || y != s_mouse.y ||
        vioMouse->buttons != s_mouse.buttons) {
        s_mouse.dirty = true;
    }
    
    s_mouse.x = x;
    s_mouse.y = y;
    s_mouse.buttons = vioMouse->buttons;
    s_mouse.scrollY = vioMouse->wheel;
    s_mouse.scrollX = 0;
    s_mouse.mode = vioMouse->is_absolute ? PositionMode::Absolute : PositionMode::Relative;
    s_mouse.source = InputSource::VirtIO;
}
#endif

// ================================================================
// Public API implementation
// ================================================================

void init(uint32_t screen_width, uint32_t screen_height)
{
    serial::puts("[INPUT] Initializing input manager\n");
    
    s_screenWidth = static_cast<int32_t>(screen_width);
    s_screenHeight = static_cast<int32_t>(screen_height);
    
    // Initialize state
    memzero(&s_mouse, sizeof(s_mouse));
    memzero(&s_keyboard, sizeof(s_keyboard));
    
    // Center mouse on screen
    s_mouse.x = s_screenWidth / 2;
    s_mouse.y = s_screenHeight / 2;
    
#if ARCH_HAS_USB && defined(KERNEL_HAS_USB_HID)
    // Initialize USB HID subsystem
    usb_hid::init();
#endif
    
#if ARCH_HAS_PS2
    // PS/2 is initialized separately in main.cpp for now
    // (needs IRQ handler registration)
#endif

#if defined(KERNEL_HAS_VIRTIO_INPUT)
    virtio_input::init(screen_width, screen_height);
#endif
    
    // Detect available sources
    detect_sources();
    
    // Select best source
    select_best_source();
    
    serial::puts("[INPUT] Input manager initialized\n");
}

void poll()
{
    // Poll based on active source
    switch (s_activeMouseSource) {
#if ARCH_HAS_USB && defined(KERNEL_HAS_USB_HID)
        case InputSource::USB_HID:
            poll_usb_hid_mouse();
            break;
#endif
            
#if ARCH_HAS_PS2
        case InputSource::PS2:
            poll_ps2_mouse();
            break;
#endif

#if defined(KERNEL_HAS_VIRTIO_INPUT)
        case InputSource::VirtIO:
            poll_virtio_input();
            break;
#endif

        case InputSource::Platform:
            // Platform-specific polling would go here
            break;
            
        case InputSource::None:
        default:
            break;
    }
    
    // Periodically re-check for new devices (e.g., USB hotplug)
    // This could be optimized with a counter to avoid checking every poll
    static uint32_t pollCount = 0;
    if (++pollCount >= 1000) {
        pollCount = 0;
        
        InputSource oldSource = s_activeMouseSource;
        detect_sources();
        select_best_source();
        
        if (s_activeMouseSource != oldSource) {
            serial::puts("[INPUT] Input source changed to: ");
            serial::puts(source_name(s_activeMouseSource));
            serial::putc('\n');
        }
    }
}

// ----------------------------------------------------------------
// Mouse accessors
// ----------------------------------------------------------------

int32_t mouse_x()       { return s_mouse.x; }
int32_t mouse_y()       { return s_mouse.y; }
uint8_t mouse_buttons() { return s_mouse.buttons; }

int8_t mouse_scroll_y()
{
    int8_t scroll = s_mouse.scrollY;
    s_mouse.scrollY = 0;
    return scroll;
}

int8_t mouse_scroll_x()
{
    int8_t scroll = s_mouse.scrollX;
    s_mouse.scrollX = 0;
    return scroll;
}

bool mouse_dirty()       { return s_mouse.dirty; }
void mouse_clear_dirty() { s_mouse.dirty = false; }

InputSource mouse_source()         { return s_activeMouseSource; }
PositionMode mouse_position_mode() { return s_mouse.mode; }

const MouseState* get_mouse_state() { return &s_mouse; }

// ----------------------------------------------------------------
// Keyboard accessors
// ----------------------------------------------------------------

bool keyboard_dirty()       { return s_keyboard.dirty; }
void keyboard_clear_dirty() { s_keyboard.dirty = false; }

InputSource keyboard_source()           { return s_activeKeyboardSource; }
const KeyboardState* get_keyboard_state() { return &s_keyboard; }

// ----------------------------------------------------------------
// Source management
// ----------------------------------------------------------------

bool set_preferred_source(InputSource source)
{
    if (source == InputSource::None || is_source_available(source)) {
        s_preferredSource = source;
        select_best_source();
        return true;
    }
    return false;
}

uint8_t get_available_sources()
{
    return s_availableSources;
}

bool is_source_available(InputSource source)
{
    return (s_availableSources & (1 << static_cast<uint8_t>(source))) != 0;
}

const char* source_name(InputSource source)
{
    switch (source) {
        case InputSource::None:     return "None";
        case InputSource::USB_HID:  return "USB HID";
        case InputSource::PS2:      return "PS/2";
        case InputSource::VirtIO:   return "VirtIO";
        case InputSource::Platform: return "Platform";
        default:                    return "Unknown";
    }
}

} // namespace input
} // namespace kernel
