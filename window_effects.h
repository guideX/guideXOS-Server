#pragma once
#include <cstdint>

namespace gxos {
namespace gui {

/// Window animation type - ported from guideXOS.Legacy WindowAnimationTypeEnum.cs
enum class WindowAnimationType {
    None = 0,
    FadeIn,
    FadeOutClose,
    Minimize,
    Restore
};

/// Window effect types for open/close animations.
/// Ported from guideXOS.Legacy WindowEffectType enum.
enum class WindowEffectType {
    None = 0,       // No effect (instant)
    Fade,           // Simple fade (default)
    Digitize,       // Tron-style scanline materialization
    Derezz,         // Tron-style de-resolution
    BurnIn,         // Fire particles coalescing
    BurnOut,        // Fire particles dispersing
    SmokeIn,        // Smoke particles forming
    SmokeOut,       // Smoke particles dissipating
    Glitch,         // Digital corruption
    Ripple,         // Ripple wave effect
    Explode,        // Particle explosion
    Implode,        // Particle implosion
    Random          // Random effect each time
};

/// Animation state for a window - tracks current animation progress.
struct WindowAnimState {
    WindowAnimationType animType = WindowAnimationType::None;
    WindowEffectType effectType = WindowEffectType::None;
    uint64_t animStartTicks = 0;
    int animDurationMs = 0;
    int animStartY = 0;
    int animEndY = 0;
    uint8_t overlayAlpha = 0;       // 0..255 for fade
    float effectProgress = 0.0f;    // 0.0 to 1.0 for special effects

    // Cached normal bounds for restore after minimize/maximize
    int normX = 0;
    int normY = 0;
    int normW = 0;
    int normH = 0;
    
    bool isAnimating() const {
        return animType != WindowAnimationType::None;
    }
};

/// Forward declaration for effect rendering
class SpecialEffects;

} // namespace gui
} // namespace gxos
