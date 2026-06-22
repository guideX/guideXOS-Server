#pragma once

#include <cstdint>

enum class DesktopThemeId
{
    Classic = 0,
    SciFi = 1
};

struct DesktopTheme
{
    DesktopThemeId id;
    const char* displayName;

    uint32_t desktopBackground;
    uint32_t windowBackground;
    uint32_t windowBorder;
    uint32_t titleBarBackground;
    uint32_t titleBarText;
    uint32_t accent;
    uint32_t mutedAccent;
    uint32_t taskbarBackground;
    uint32_t taskbarBorder;

    int windowCornerRadius;
    int windowPadding;
    int controlPadding;
    int titleBarHeight;
    int windowBorderThickness;
    int titleTextInset;
    int titleButtonGap;
    int taskbarPadding;
    int taskbarItemPadding;

    bool roundedWindows;
    bool softShadowIntent;
    bool glassIntent;
    bool animationsIntent;
};

DesktopThemeId GetCurrentDesktopThemeId();
const DesktopTheme& GetCurrentDesktopTheme();
const DesktopTheme& GetDesktopTheme(DesktopThemeId id);
bool SetCurrentDesktopTheme(DesktopThemeId id);
const char* DesktopThemeIdToString(DesktopThemeId id);
bool TryParseDesktopThemeId(const char* text, DesktopThemeId* out);
