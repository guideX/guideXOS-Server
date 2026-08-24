#pragma once

#include "desktop_theme.h"

// Color-only roles shared by the existing bare-metal paint paths.  These are
// descriptive relationships, not a widget framework: callers still own
// geometry, state, semantics, and Classic fallbacks.
struct BareMetalControlTheme
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
};

inline BareMetalControlTheme GetBareMetalControlTheme(const DesktopTheme& theme)
{
    return {
        theme.windowBackground,
        theme.taskbarBackground,
        BlendDesktopThemeColor(theme.windowBackground, theme.taskbarBackground, 8),
        theme.windowBorder,
        theme.titleBarText,
        BlendDesktopThemeColor(theme.titleBarText, theme.windowBackground, 34),
        BlendDesktopThemeColor(theme.windowBackground, theme.accent, 48),
        BlendDesktopThemeColor(theme.windowBackground, theme.mutedAccent, 42),
        theme.titleBarText
    };
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
