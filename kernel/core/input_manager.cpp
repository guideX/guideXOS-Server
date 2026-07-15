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

static display_input::DisplayInputMapper s_displayMapper;
static bool s_mappingDiagnosticsEnabled = false;
static uint32_t s_mappingDiagnosticLimit = 0;
static uint32_t s_mappingDiagnosticCount = 0;
static int32_t s_lastPs2X = 0;
static int32_t s_lastPs2Y = 0;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void put_decimal(int32_t value)
{
    char buffer[12];
    uint32_t index = sizeof(buffer) - 1u;
    buffer[index] = '\0';
    bool negative = value < 0;
    uint32_t magnitude = negative
        ? static_cast<uint32_t>(-(static_cast<int64_t>(value)))
        : static_cast<uint32_t>(value);
    do {
        buffer[--index] = static_cast<char>('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0u && index > 0u);
    if (negative && index > 0u) buffer[--index] = '-';
    serial::puts(&buffer[index]);
}

static void log_mapping_event(const display_input::DisplayPointerEvent& event)
{
    if (!s_mappingDiagnosticsEnabled ||
        s_mappingDiagnosticCount >= s_mappingDiagnosticLimit) return;
    ++s_mappingDiagnosticCount;
    serial::puts("[INPUT-MAP] seq=");
    serial::put_hex32(event.sequence);
    serial::puts(" source=");
    serial::puts(display_input::pointerSourceName(event.sourceType));
    serial::puts(" mode=");
    serial::puts(display_input::pointerCoordinateModeName(event.coordinateMode));
    serial::puts(" head=");
    put_decimal(event.sourceHead);
    serial::puts(" monitor=");
    put_decimal(event.sourceMonitor);
    serial::puts(" raw=");
    put_decimal(event.rawX);
    serial::puts(",");
    put_decimal(event.rawY);
    serial::puts(" local=");
    put_decimal(event.localX);
    serial::puts(",");
    put_decimal(event.localY);
    serial::puts(" virtual=");
    put_decimal(event.virtualX);
    serial::puts(",");
    put_decimal(event.virtualY);
    serial::puts(" dxdy=");
    put_decimal(event.relativeDx);
    serial::puts(",");
    put_decimal(event.relativeDy);
    serial::puts(" buttons=");
    serial::put_hex8(event.buttonMask);
    serial::puts(" wheel=");
    put_decimal(event.wheelDelta);
    serial::puts(" fallback=");
    serial::puts(event.fallbackUsed ? "yes" : "no");
    serial::puts(" clamped=");
    serial::puts(event.clamped ? "yes" : "no");
    serial::puts(" capture=external mappingReason=");
    serial::puts(event.mappingReason);
    serial::puts(" valid=");
    serial::puts(event.valid ? "yes\n" : "no\n");
}

static void apply_mapping_event(const display_input::DisplayPointerEvent& event,
                                InputSource source)
{
    s_mouse.mapping = event;
    s_mouse.x = event.virtualX;
    s_mouse.y = event.virtualY;
    s_mouse.mode = event.coordinateMode == display_input::PointerCoordinateMode::Relative
        ? PositionMode::Relative : PositionMode::Absolute;
    s_mouse.source = source;
    s_mouse.buttons = event.buttonMask;
    s_mouse.scrollY = static_cast<int8_t>(event.wheelDelta);
    s_mouse.scrollX = 0;
    log_mapping_event(event);
}

// ================================================================
// Source detection and initialization
// ================================================================

static void detect_sources(bool verbose = true)
{
    s_availableSources = 0;

#if ARCH_HAS_USB && defined(KERNEL_HAS_USB_HID)
    // Check USB HID
    if (usb_hid::has_mouse()) {
        s_availableSources |= (1 << static_cast<uint8_t>(InputSource::USB_HID));
        if (verbose) serial::puts("[INPUT] USB HID mouse detected\n");
    }
#endif

#if ARCH_HAS_PS2
    // PS/2 is always available on x86 if compiled in
    s_availableSources |= (1 << static_cast<uint8_t>(InputSource::PS2));
    if (verbose) serial::puts("[INPUT] PS/2 mouse available\n");
#endif

#if defined(KERNEL_HAS_VIRTIO_INPUT)
    if (virtio_input::has_mouse()) {
        s_availableSources |= (1 << static_cast<uint8_t>(InputSource::VirtIO));
        if (verbose) serial::puts("[INPUT] VirtIO input detected\n");
    }
#endif

    // Platform-specific sources would be detected here
    // (ADB on Mac, Sun mouse on SPARC, etc.)
}

static void select_best_source(bool verbose = true)
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
            if (verbose) {
                serial::puts("[INPUT] Using preferred source: ");
                serial::puts(source_name(s_preferredSource));
                serial::putc('\n');
            }
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

    if (verbose) {
        if (s_activeMouseSource != InputSource::None) {
            serial::puts("[INPUT] Active mouse source: ");
            serial::puts(source_name(s_activeMouseSource));
            serial::putc('\n');
        } else {
            serial::puts("[INPUT] WARNING: No mouse input source available\n");
        }
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
    int32_t dx = x - s_lastPs2X;
    int32_t dy = y - s_lastPs2Y;
    s_lastPs2X = x;
    s_lastPs2Y = y;
    uint8_t buttons = ps2mouse::get_buttons();
    int8_t scroll = ps2mouse::get_scroll_delta();

    display_input::DisplayPointerEvent event = s_displayMapper.mapRelativePointer(
        display_input::PointerSourceType::Ps2Relative, dx, dy, buttons, scroll);
    // Check if anything changed
    if (event.virtualX != s_mouse.x || event.virtualY != s_mouse.y ||
        buttons != s_mouse.buttons || scroll != 0) {
        s_mouse.dirty = true;
    }
    apply_mapping_event(event, InputSource::PS2);
}
#endif

#if ARCH_HAS_USB && defined(KERNEL_HAS_USB_HID)
static void poll_usb_hid_mouse()
{
    // Poll USB HID subsystem
    usb_hid::poll();
    
    const usb_hid::MouseState* usbMouse = usb_hid::get_mouse_state();
    if (!usbMouse) return;
    
    int32_t dx = usbMouse->dx;
    int32_t dy = usbMouse->dy;
    display_input::DisplayPointerEvent event = s_displayMapper.mapRelativePointer(
        display_input::PointerSourceType::UsbRelative, dx, dy,
        usbMouse->buttons, usbMouse->wheel);
    // Check if anything changed
    if (event.virtualX != s_mouse.x || event.virtualY != s_mouse.y ||
        usbMouse->buttons != s_mouse.buttons || usbMouse->wheel != 0) {
        s_mouse.dirty = true;
    }
    apply_mapping_event(event, InputSource::USB_HID);
}
#endif // ARCH_HAS_USB && KERNEL_HAS_USB_HID

#if defined(KERNEL_HAS_VIRTIO_INPUT)
static void poll_virtio_input()
{
    virtio_input::poll();
    
    const virtio_input::MouseState* vioMouse = virtio_input::get_mouse_state();
    if (!vioMouse) return;
    
    // VirtIO input typically provides absolute coordinates
    display_input::DisplayPointerEvent event;
    if (vioMouse->is_absolute) {
        event = s_displayMapper.mapUnknownHeadAbsolute(
            display_input::PointerSourceType::VirtioInputAbsolute,
            vioMouse->x, vioMouse->y, 0, 32767, 0, 32767,
            s_screenWidth, s_screenHeight, vioMouse->buttons, vioMouse->wheel);
    } else {
        event = s_displayMapper.mapRelativePointer(
            display_input::PointerSourceType::VirtioInputRelative,
            vioMouse->x, vioMouse->y, vioMouse->buttons, vioMouse->wheel);
    }
    if (event.virtualX != s_mouse.x || event.virtualY != s_mouse.y ||
        vioMouse->buttons != s_mouse.buttons || vioMouse->wheel != 0) {
        s_mouse.dirty = true;
    }
    apply_mapping_event(event, InputSource::VirtIO);
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
    s_displayMapper.reset();
    s_displayMapper.configureVirtualDesktop(0, 0, s_screenWidth, s_screenHeight);
    s_displayMapper.setMonitor(0, 1, 0, 0, s_screenWidth, s_screenHeight, true);
    s_displayMapper.setCursor(s_mouse.x, s_mouse.y);
    s_lastPs2X = s_mouse.x;
    s_lastPs2Y = s_mouse.y;
    s_mappingDiagnosticCount = 0;
    
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

bool configure_display_layout(
    int32_t left, int32_t top, int32_t right, int32_t bottom,
    const display_input::DisplayInputMonitor* monitors, uint8_t monitorCount)
{
    if (monitors == nullptr || monitorCount == 0 ||
        monitorCount > display_input::kDisplayInputMaxMonitors) return false;
    s_displayMapper.reset();
    s_displayMapper.configureVirtualDesktop(left, top, right, bottom);
    for (uint8_t i = 0; i < monitorCount; ++i) {
        const display_input::DisplayInputMonitor& monitor = monitors[i];
        if (!s_displayMapper.setMonitor(
                i, monitor.id, monitor.virtualX, monitor.virtualY,
                monitor.width, monitor.height, monitor.primary,
                monitor.enabled, monitor.assignedX, monitor.assignedY,
                monitor.assignedWidth, monitor.assignedHeight)) {
            return false;
        }
    }
    s_displayMapper.configureVirtualDesktop(left, top, right, bottom);
    s_displayMapper.setCursor(s_mouse.x, s_mouse.y);
#if ARCH_HAS_PS2
    s_lastPs2X = ps2mouse::get_x();
    s_lastPs2Y = ps2mouse::get_y();
#else
    s_lastPs2X = s_mouse.x;
    s_lastPs2Y = s_mouse.y;
#endif
    return true;
}

void set_mapping_diagnostics(bool enabled, uint32_t eventLimit)
{
    s_mappingDiagnosticsEnabled = enabled;
    s_mappingDiagnosticLimit = eventLimit;
    s_mappingDiagnosticCount = 0;
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
        detect_sources(false);
        select_best_source(false);
        
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
const display_input::DisplayPointerEvent* get_last_pointer_event() { return &s_mouse.mapping; }
const display_input::DisplayInputMapperCounters* get_mapping_counters() { return &s_displayMapper.counters(); }
const display_input::DisplayInputMapper* get_display_input_mapper() { return &s_displayMapper; }

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
