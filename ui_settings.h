#pragma once
#include <cstdint>

namespace gxos {
namespace gui {

/// Global UI settings for animations and visual effects.
/// Ported from guideXOS.Legacy UISettings.cs to match the windowing system style.
struct UISettings {
    // ===== ANIMATION SETTINGS =====
    // Fading animations (open/close) 
    static constexpr bool EnableFadeAnimations = true;
    static constexpr int FadeInDurationMs = 180;
    static constexpr int FadeOutDurationMs = 180;
    static constexpr bool UpdateAnimations = true;

    // Special window effects (Tron-style and more)
    static constexpr bool EnableSpecialWindowEffects = true;
    static constexpr int SpecialEffectDurationMs = 400;

    // Effect-specific settings
    static constexpr int DigitizeScanlineCount = 25;
    static constexpr int BurnParticleCount = 150;
    static constexpr int SmokeParticleCount = 100;
    static constexpr int GlitchIntensity = 15;
    static constexpr int RippleWaveCount = 8;
    static constexpr int ExplodeParticleCount = 200;

    // Window slide animations (minimize/restore)
    static constexpr bool EnableWindowSlideAnimations = true;
    static constexpr int WindowSlideDurationMs = 220;

    // Animation overlay effects
    static constexpr bool EnableFadeOverlay = true;
    static constexpr bool EnableAnimationUpdates = true;

    // ===== VISUAL EFFECTS SETTINGS =====
    // Major visual effects
    static constexpr bool EnableBlurredTitleBars = true;
    static constexpr bool EnableTransparentWindows = true;
    static constexpr bool EnableWindowGlow = true;
    static constexpr bool EnableRoundedCorners = true;
    static constexpr bool EnableButtonHoverEffects = true;
    static constexpr bool EnableResizeGrip = true;

    // ===== WINDOW RENDERING SETTINGS =====
    // Title bar and window chrome
    static constexpr bool EnableWindowTitles = true;
    static constexpr bool EnableWindowBorders = true;
    static constexpr bool EnableTitleBarButtons = true;
    static constexpr bool EnableTitleBarBackground = true;
    static constexpr bool EnableWindowContentBackground = true;

    // ===== BUTTON AND ICON SETTINGS =====
    // Title bar button visuals
    static constexpr bool EnableButtonBackgrounds = true;
    static constexpr bool EnableButtonBorders = true;
    static constexpr bool EnableButtonIcons = true;
    static constexpr bool EnableTaskbarIcons = true;

    // Button sizing and layout
    static constexpr int ButtonSizeOffset = 8;
    static constexpr int MinimumButtonSize = 16;
    static constexpr int ButtonSpacing = 6;

    // ===== TEXT RENDERING SETTINGS =====
    static constexpr bool EnableTextRendering = true;
    static constexpr bool EnableTextShadows = true;

    // ===== WINDOW INTERACTION SETTINGS =====
    // Input and interaction
    static constexpr bool EnableWindowDragging = true;
    static constexpr bool EnableWindowResizing = true;
    static constexpr bool EnableButtonClicking = true;
    static constexpr bool EnableWindowFocusing = true;

    // Resize settings
    static constexpr int ResizeGripSize = 16;
    static constexpr int MinimumWindowWidth = 160;
    static constexpr int MinimumWindowHeight = 120;

    // ===== SCREEN CLAMPING SETTINGS =====
    static constexpr bool EnableScreenClamping = true;

    // ===== WINDOW LIFECYCLE SETTINGS =====
    static constexpr bool EnableAutoRegistration = true;
    static constexpr bool EnableAutoFadeIn = true;

    // ===== TOMBSTONE SETTINGS =====
    static constexpr bool EnableTombstoneOverlay = true;
    static constexpr bool EnableTombstoneText = true;
    static constexpr bool EnableTombstoneInputBlocking = true;

    // ===== PERFORMANCE SETTINGS =====
    static constexpr bool EnableDrawCallCaching = true;
    static constexpr bool SkipOffscreenRendering = true;
    static constexpr bool SkipMinimizedRendering = true;
    static constexpr bool SkipInvisibleRendering = true;

    // ===== Z-ORDER AND FOCUS SETTINGS =====
    static constexpr bool EnableWindowOrdering = true;
    static constexpr bool EnableFocusOnClick = true;
    static constexpr bool EnableFocusOnDrag = true;

    // ===== WINDOW STATE SETTINGS =====
    static constexpr bool EnableMinimize = true;
    static constexpr bool EnableMaximize = true;
    static constexpr bool EnableRestore = true;
    static constexpr bool EnableStateMemory = true;

    // ===== DEFAULT VALUES AND CONSTANTS =====
    static constexpr int DefaultBarHeight = 24;
    static constexpr int DefaultTaskbarHeight = 40;

    // ===== COLOR CUSTOMIZATION =====
    // Title bar colors (ARGB format)
    static constexpr uint32_t TitleBarColor = 0xFF111111;
    static constexpr uint32_t TitleBarFocusedColor = 0xFF2B506F;
    static constexpr uint32_t TitleBarTransparentColor = 0x99111111;
    static constexpr uint32_t TitleBarOpaqueColor = 0xCC111111;

    // Window content colors
    static constexpr uint32_t WindowContentColor = 0xFF222222;
    static constexpr uint32_t WindowContentTransparentColor = 0xCC222222;

    // Button colors
    static constexpr uint32_t ButtonNormalColor = 0xFF2E2E2E;
    static constexpr uint32_t ButtonHoverColor = 0xFF444444;
    static constexpr uint32_t ButtonPressedColor = 0xFF1E1E1E;
    static constexpr uint32_t ButtonBorderColor = 0xFF505050;
    static constexpr uint32_t ButtonHoverGlowColor = 0x442E89FF;

    // Close button colors (red tint)
    static constexpr uint32_t CloseButtonNormalColor = 0xFF782828;
    static constexpr uint32_t CloseButtonHoverColor = 0xFFAA4040;
    static constexpr uint32_t CloseButtonPressedColor = 0xFFB84C4C;

    // Minimize button colors (green tint)
    static constexpr uint32_t MinimizeButtonHoverColor = 0xFF446644;

    // Maximize button colors (blue tint)
    static constexpr uint32_t MaximizeButtonHoverColor = 0xFF446688;

    // Tombstone button colors (amber tint)
    static constexpr uint32_t TombstoneButtonHoverColor = 0xFF886644;
    static constexpr uint32_t TombstoneButtonPressedColor = 0xFFAA8855;

    // Border and grip colors
    static constexpr uint32_t WindowBorderColor = 0xFF333333;
    static constexpr uint32_t WindowBorderFocusedColor = 0xFF5588AA;
    static constexpr uint32_t ResizeGripColor = 0x442F2F2F;
    static constexpr uint32_t ResizeGripLineColor = 0xFF777777;
    static constexpr uint32_t ResizeGripBorderColor = 0xFF444444;

    // Glow colors
    static constexpr uint32_t WindowGlowColor = 0x331E90FF;
    static constexpr uint32_t WindowGlowFocusedColor = 0x441E90FF;

    // Tombstone colors
    static constexpr uint32_t TombstoneOverlayColor = 0x88111111;
    static constexpr uint32_t TombstoneTextColor = 0xFFC86464;

    // Special effect colors
    static constexpr uint32_t DigitizeColor = 0xFF00FFFF;
    static constexpr uint32_t DerezzColor = 0xFFFF6600;
    static constexpr uint32_t BurnColor = 0xFFFF4400;
    static constexpr uint32_t SmokeColor = 0x88888888;
    static constexpr uint32_t GlitchColor = 0xFF00FF00;
    static constexpr uint32_t RippleColor = 0xFF0088FF;
    static constexpr uint32_t ExplodeColor = 0xFFFFFF00;

    // Shadow/glow settings
    static constexpr int WindowGlowRadius = 8;
    static constexpr int WindowCornerRadius = 6;
};

} // namespace gui
} // namespace gxos
