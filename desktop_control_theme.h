#pragma once

#include "desktop_theme.h"

// Color-only roles shared by hosted and bare-metal application paint paths.
// This is a token view over DesktopTheme, not a second theme-selection or
// widget system: callers still own geometry, state, semantics, and Classic
// fallbacks.
struct DesktopControlTheme
{
    uint32_t panelBackground;
    uint32_t raisedPanel;
    uint32_t recessedField;
    uint32_t border;
    uint32_t primaryText;
    uint32_t secondaryText;
    uint32_t selectionActive;
    uint32_t selectionInactive;
    uint32_t selectionText;

    // Shared button/control states.
    uint32_t controlBackground;
    uint32_t controlHover;
    uint32_t controlPressed;
    uint32_t controlDisabled;
    uint32_t controlBorder;
    uint32_t controlHoverBorder;
    uint32_t controlPressedBorder;
    uint32_t controlFocusBorder;
    uint32_t controlDisabledBorder;
    uint32_t controlText;
    uint32_t controlDisabledText;

    // Shared text/edit-field roles.
    uint32_t inputBackground;
    uint32_t inputBorder;
    uint32_t inputFocusBorder;
    uint32_t inputText;
    uint32_t inputDisabledText;

    // Shared separators and scrollbar states.
    uint32_t separator;
    uint32_t scrollbarTrack;
    uint32_t scrollbarThumb;
    uint32_t scrollbarHover;
    uint32_t scrollbarPressed;
    uint32_t scrollbarDisabled;
};

// Existing bare-metal callers retain their original type spelling while the
// underlying role set is now reusable by both compositor implementations.
using BareMetalControlTheme = DesktopControlTheme;

inline DesktopControlTheme GetDesktopControlTheme(const DesktopTheme& theme)
{
    DesktopControlTheme roles{};

    // Preserve the Phase 8C/8D relationships used by existing bare-metal
    // applications.  The additional roles below build on the same palette.
    roles.panelBackground = theme.windowBackground;
    roles.raisedPanel = theme.taskbarBackground;
    roles.recessedField = BlendDesktopThemeColor(theme.windowBackground, theme.taskbarBackground, 8);
    roles.border = theme.windowBorder;
    roles.primaryText = theme.titleBarText;
    roles.secondaryText = BlendDesktopThemeColor(theme.titleBarText, theme.windowBackground, 34);
    roles.selectionActive = BlendDesktopThemeColor(theme.windowBackground, theme.accent, 48);
    roles.selectionInactive = BlendDesktopThemeColor(theme.windowBackground, theme.mutedAccent, 42);
    roles.selectionText = theme.titleBarText;

    roles.controlBackground = BlendDesktopThemeColor(theme.windowBackground, theme.taskbarBackground, 18);
    roles.controlHover = BlendDesktopThemeColor(roles.controlBackground, theme.mutedAccent, 16);
    roles.controlPressed = BlendDesktopThemeColor(roles.controlBackground, theme.accent, 24);
    roles.controlDisabled = BlendDesktopThemeColor(theme.windowBackground, theme.taskbarBackground, 8);
    roles.controlBorder = BlendDesktopThemeColor(theme.windowBorder, theme.taskbarBorder, 20);
    roles.controlHoverBorder = BlendDesktopThemeColor(theme.windowBorder, theme.mutedAccent, 30);
    roles.controlPressedBorder = BlendDesktopThemeColor(theme.windowBorder, theme.accent, 36);
    roles.controlFocusBorder = BlendDesktopThemeColor(theme.windowBorder, theme.accent, 56);
    roles.controlDisabledBorder = BlendDesktopThemeColor(theme.windowBorder, theme.taskbarBackground, 28);
    roles.controlText = theme.titleBarText;
    roles.controlDisabledText = BlendDesktopThemeColor(theme.titleBarText, theme.windowBackground, 54);

    roles.inputBackground = roles.recessedField;
    roles.inputBorder = BlendDesktopThemeColor(theme.windowBorder, theme.taskbarBorder, 28);
    roles.inputFocusBorder = BlendDesktopThemeColor(theme.windowBorder, theme.accent, 42);
    roles.inputText = roles.primaryText;
    roles.inputDisabledText = roles.controlDisabledText;

    roles.separator = BlendDesktopThemeColor(theme.windowBorder, theme.taskbarBackground, 24);
    roles.scrollbarTrack = BlendDesktopThemeColor(theme.taskbarBackground, theme.windowBackground, 28);
    roles.scrollbarThumb = BlendDesktopThemeColor(theme.windowBorder, theme.accent, 30);
    roles.scrollbarHover = BlendDesktopThemeColor(roles.scrollbarThumb, theme.titleBarText, 12);
    roles.scrollbarPressed = BlendDesktopThemeColor(roles.scrollbarThumb, theme.accent, 22);
    roles.scrollbarDisabled = BlendDesktopThemeColor(theme.windowBorder, theme.windowBackground, 20);

    return roles;
}

inline DesktopControlTheme GetBareMetalControlTheme(const DesktopTheme& theme)
{
    return GetDesktopControlTheme(theme);
}

enum class DesktopControlState
{
    Normal,
    Hover,
    Pressed,
    Focused,
    Disabled
};

inline uint32_t DesktopControlFillColor(const DesktopControlTheme& roles,
                                        DesktopControlState state)
{
    switch (state) {
    case DesktopControlState::Hover: return roles.controlHover;
    case DesktopControlState::Pressed: return roles.controlPressed;
    case DesktopControlState::Disabled: return roles.controlDisabled;
    case DesktopControlState::Focused:
        return BlendDesktopThemeColor(roles.controlBackground, roles.controlFocusBorder, 14);
    case DesktopControlState::Normal:
    default:
        return roles.controlBackground;
    }
}

inline uint32_t DesktopControlBorderColor(const DesktopControlTheme& roles,
                                          DesktopControlState state)
{
    switch (state) {
    case DesktopControlState::Hover: return roles.controlHoverBorder;
    case DesktopControlState::Pressed: return roles.controlPressedBorder;
    case DesktopControlState::Focused: return roles.controlFocusBorder;
    case DesktopControlState::Disabled: return roles.controlDisabledBorder;
    case DesktopControlState::Normal:
    default:
        return roles.controlBorder;
    }
}

inline uint32_t DesktopControlTextColor(const DesktopControlTheme& roles,
                                        DesktopControlState state)
{
    return state == DesktopControlState::Disabled ? roles.controlDisabledText : roles.controlText;
}

// Selection surfaces may use an application-owned base (Notepad's editor is
// one example), so keep the same active/inactive relationship available
// without forcing all controls onto one surface.
inline uint32_t BareMetalSelectionFillColor(const DesktopTheme& theme,
                                            uint32_t surface,
                                            bool active)
{
    return BlendDesktopThemeColor(
        surface,
        active ? theme.accent : theme.mutedAccent,
        active ? 48 : 42);
}

inline uint32_t DesktopSelectionColor(const DesktopControlTheme& roles, bool active)
{
    return active ? roles.selectionActive : roles.selectionInactive;
}

struct BareMetalButtonSurfaceRoles
{
    uint32_t normal;
    uint32_t hover;
    uint32_t pressed;
    uint32_t text;
};

// The surface base remains application/semantic-specific.  This helper only
// centralizes the repeated Sci-Fi state transitions used by existing buttons.
inline BareMetalButtonSurfaceRoles GetBareMetalButtonSurfaceRoles(
    const DesktopTheme& theme,
    uint32_t normalSurface,
    int hoverBlendPercent,
    int pressedBlendPercent)
{
    return {
        normalSurface,
        BlendDesktopThemeColor(normalSurface, theme.mutedAccent, hoverBlendPercent),
        BlendDesktopThemeColor(normalSurface, theme.accent, pressedBlendPercent),
        theme.titleBarText
    };
}
