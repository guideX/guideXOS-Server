#pragma once

#if defined(GXOS_BARE_METAL)
#include <kernel/types.h>
#else
#include <cstdint>
#endif

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

// Portable color blending for framebuffer and hosted callers.  The alpha byte
// is kept opaque because the existing theme colors are used as XRGB/ARGB
// surface colors rather than translucent paint instructions.
inline uint32_t BlendDesktopThemeColor(uint32_t baseColor, uint32_t overlayColor, int overlayPercent)
{
    if (overlayPercent <= 0) {
        return baseColor;
    }
    if (overlayPercent >= 100) {
        return overlayColor;
    }

    const int baseR = static_cast<int>((baseColor >> 16) & 0xFF);
    const int baseG = static_cast<int>((baseColor >> 8) & 0xFF);
    const int baseB = static_cast<int>(baseColor & 0xFF);
    const int overlayR = static_cast<int>((overlayColor >> 16) & 0xFF);
    const int overlayG = static_cast<int>((overlayColor >> 8) & 0xFF);
    const int overlayB = static_cast<int>(overlayColor & 0xFF);
    const int keepPercent = 100 - overlayPercent;

    return 0xFF000000u |
        (static_cast<uint32_t>((baseR * keepPercent + overlayR * overlayPercent) / 100) << 16) |
        (static_cast<uint32_t>((baseG * keepPercent + overlayG * overlayPercent) / 100) << 8) |
        static_cast<uint32_t>((baseB * keepPercent + overlayB * overlayPercent) / 100);
}

DesktopThemeId GetCurrentDesktopThemeId();
const DesktopTheme& GetCurrentDesktopTheme();
const DesktopTheme& GetDesktopTheme(DesktopThemeId id);
bool SetCurrentDesktopTheme(DesktopThemeId id);
const char* DesktopThemeIdToString(DesktopThemeId id);
bool TryParseDesktopThemeId(const char* text, DesktopThemeId* out);
