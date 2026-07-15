//
// Input Manager - Unified Input Abstraction Layer
//
// Provides a single interface for mouse/keyboard input across
// multiple backends with automatic fallback:
//   1. USB HID mouse/keyboard (preferred - absolute positioning)
//   2. PS/2 mouse/keyboard (legacy, relative positioning)
//   3. VirtIO input (virtualization-optimized)
//   4. Platform-specific (ADB, Sun mouse, etc.)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_INPUT_MANAGER_H
#define KERNEL_INPUT_MANAGER_H

#include "kernel/types.h"
#include "kernel/display_input_mapper.h"

namespace kernel {
namespace input {

// ================================================================
// Input source priority (lower = higher priority)
// ================================================================

enum class InputSource : uint8_t {
    None          = 0,
    USB_HID       = 1,   // Highest priority - USB HID mouse/tablet
    PS2           = 2,   // PS/2 mouse (legacy)
    VirtIO        = 3,   // VirtIO input device
    Platform      = 4,   // Platform-specific (ADB, Sun, etc.)
};

// ================================================================
// Mouse button flags (compatible with PS/2 and USB HID)
// ================================================================

enum MouseButton : uint8_t {
    ButtonNone   = 0,
    ButtonLeft   = 1,
    ButtonRight  = 2,
    ButtonMiddle = 4,
};

// ================================================================
// Positioning mode
// ================================================================

enum class PositionMode : uint8_t {
    Relative = 0,   // PS/2 style - delta movements
    Absolute = 1,   // USB tablet style - absolute coordinates
};

// ================================================================
// Unified mouse state
// ================================================================

struct MouseState {
    int32_t      x;          // Current X position (screen coords)
    int32_t      y;          // Current Y position (screen coords)
    uint8_t      buttons;    // Button bitmask (MouseButton flags)
    int8_t       scrollX;    // Horizontal scroll delta
    int8_t       scrollY;    // Vertical scroll delta
    PositionMode mode;       // Current positioning mode
    InputSource  source;     // Active input source
    bool         dirty;      // State changed since last clear
    display_input::DisplayPointerEvent mapping; // Last mapped event
};

// ================================================================
// Unified keyboard state (for future expansion)
// ================================================================

struct KeyboardState {
    uint8_t     modifiers;   // Modifier key bitmask
    uint8_t     keys[6];     // Up to 6 simultaneous keys (USB HID style)
    InputSource source;      // Active input source
    bool        dirty;       // State changed since last clear
};

// ================================================================
// Public API
// ================================================================

// Initialize the input manager with screen dimensions.
// Probes all available input sources and selects the best one.
void init(uint32_t screen_width, uint32_t screen_height);

// Replace the default single-monitor geometry with the active virtual
// desktop. This is called by the QEMU-only display probe after virtio-gpu
// descriptors are known; normal boots retain the single-primary default.
bool configure_display_layout(
    int32_t left, int32_t top, int32_t right, int32_t bottom,
    const display_input::DisplayInputMonitor* monitors, uint8_t monitorCount);

// Diagnostics are bounded and disabled by default for ordinary boots.
void set_mapping_diagnostics(bool enabled, uint32_t eventLimit);

// Poll all active input sources for new data.
// Should be called regularly from the main loop or timer interrupt.
void poll();

// ----------------------------------------------------------------
// Mouse accessors
// ----------------------------------------------------------------

// Get current mouse X position (screen coordinates)
int32_t mouse_x();

// Get current mouse Y position (screen coordinates)
int32_t mouse_y();

// Get current mouse button state
uint8_t mouse_buttons();

// Get vertical scroll delta (reset after read)
int8_t mouse_scroll_y();

// Get horizontal scroll delta (reset after read)
int8_t mouse_scroll_x();

// Check if mouse state changed since last clear
bool mouse_dirty();

// Clear the dirty flag
void mouse_clear_dirty();

// Get the current mouse input source
InputSource mouse_source();

// Get the current positioning mode
PositionMode mouse_position_mode();

// Get the full mouse state structure
const MouseState* get_mouse_state();
const display_input::DisplayPointerEvent* get_last_pointer_event();
const display_input::DisplayInputMapperCounters* get_mapping_counters();
const display_input::DisplayInputMapper* get_display_input_mapper();

// ----------------------------------------------------------------
// Keyboard accessors (for future expansion)
// ----------------------------------------------------------------

// Check if keyboard state changed since last clear
bool keyboard_dirty();

// Clear the keyboard dirty flag
void keyboard_clear_dirty();

// Get the current keyboard input source
InputSource keyboard_source();

// Get the full keyboard state structure
const KeyboardState* get_keyboard_state();

// ----------------------------------------------------------------
// Source management
// ----------------------------------------------------------------

// Force a specific input source (for testing/debugging)
// Returns false if the source is not available
bool set_preferred_source(InputSource source);

// Get list of available input sources
// Returns bitmask of InputSource values
uint8_t get_available_sources();

// Check if a specific source is available
bool is_source_available(InputSource source);

// Get human-readable name for an input source
const char* source_name(InputSource source);

} // namespace input
} // namespace kernel

#endif // KERNEL_INPUT_MANAGER_H
