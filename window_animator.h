#pragma once
//
// Window Animation Helper
// Handles fade, slide, and special effect animations for windows
// Ported from guideXOS.Legacy Window.cs animation system
//
// Copyright (c) 2026 guideXOS Server
//

#include "window_effects.h"
#include "ui_settings.h"
#include "special_effects.h"
#include <chrono>

namespace gxos {
namespace gui {

/// Helper class to manage window animations
class WindowAnimator {
public:
    /// Get current time in milliseconds
    static uint64_t GetTicksMs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    /// Start fade-in animation for a window
    static void BeginFadeIn(WindowAnimState& state, int windowY) {
        if (UISettings::EnableSpecialWindowEffects) {
            // Use special effect
            state.effectType = SpecialEffects::GetRandomEffect();
            state.animType = WindowAnimationType::FadeIn;
            state.animStartTicks = GetTicksMs();
            state.animDurationMs = UISettings::SpecialEffectDurationMs;
            state.effectProgress = 0.0f;
            return;
        }
        
        if (!UISettings::EnableFadeAnimations) {
            state.animType = WindowAnimationType::None;
            state.overlayAlpha = 0;
            return;
        }
        
        state.animType = WindowAnimationType::FadeIn;
        state.animStartTicks = GetTicksMs();
        state.animDurationMs = UISettings::FadeInDurationMs;
        state.overlayAlpha = 255;
        state.effectType = WindowEffectType::None;
    }

    /// Start fade-out close animation for a window
    static void BeginFadeOutClose(WindowAnimState& state) {
        if (UISettings::EnableSpecialWindowEffects) {
            state.effectType = SpecialEffects::GetRandomEffect();
            state.animType = WindowAnimationType::FadeOutClose;
            state.animStartTicks = GetTicksMs();
            state.animDurationMs = UISettings::SpecialEffectDurationMs;
            state.effectProgress = 0.0f;
            return;
        }
        
        if (!UISettings::EnableFadeAnimations) {
            state.animType = WindowAnimationType::None;
            return;
        }
        
        state.animType = WindowAnimationType::FadeOutClose;
        state.animStartTicks = GetTicksMs();
        state.animDurationMs = UISettings::FadeOutDurationMs;
        state.overlayAlpha = 0;
        state.effectType = WindowEffectType::None;
    }

    /// Start minimize animation for a window
    static void BeginMinimize(WindowAnimState& state, int currentY, int screenHeight) {
        if (!UISettings::EnableWindowSlideAnimations) {
            state.animType = WindowAnimationType::None;
            return;
        }
        
        state.animType = WindowAnimationType::Minimize;
        state.animStartTicks = GetTicksMs();
        state.animDurationMs = UISettings::WindowSlideDurationMs;
        state.animStartY = currentY;
        state.animEndY = screenHeight + 100; // slide below screen
        state.effectType = WindowEffectType::None;
    }

    /// Start restore animation for a window
    static void BeginRestore(WindowAnimState& state, int screenHeight) {
        if (!UISettings::EnableWindowSlideAnimations) {
            state.animType = WindowAnimationType::None;
            return;
        }
        
        state.animType = WindowAnimationType::Restore;
        state.animStartTicks = GetTicksMs();
        state.animDurationMs = UISettings::WindowSlideDurationMs;
        state.animStartY = screenHeight + 100;
        state.animEndY = state.normY;
        state.effectType = WindowEffectType::None;
    }

    /// Update animation state, returns true if animation is complete
    /// Also modifies windowY for slide animations
    static bool UpdateAnimation(WindowAnimState& state, int& windowY, bool& shouldClose) {
        if (!UISettings::UpdateAnimations) return false;
        if (state.animType == WindowAnimationType::None) return false;
        
        uint64_t now = GetTicksMs();
        uint64_t elapsed = (now > state.animStartTicks) ? (now - state.animStartTicks) : 0;
        float t = (state.animDurationMs > 0) ? static_cast<float>(elapsed) / state.animDurationMs : 1.0f;
        if (t > 1.0f) t = 1.0f;
        
        shouldClose = false;

        switch (state.animType) {
            case WindowAnimationType::FadeIn: {
                if (state.effectType != WindowEffectType::None && state.effectType != WindowEffectType::Fade) {
                    state.effectProgress = t;
                    if (t >= 1.0f) {
                        state.animType = WindowAnimationType::None;
                        state.effectType = WindowEffectType::None;
                        state.effectProgress = 0.0f;
                        return true;
                    }
                } else {
                    state.overlayAlpha = static_cast<uint8_t>(255 - static_cast<int>(t * 255.0f));
                    if (t >= 1.0f) {
                        state.overlayAlpha = 0;
                        state.animType = WindowAnimationType::None;
                        return true;
                    }
                }
                break;
            }
            
            case WindowAnimationType::FadeOutClose: {
                if (state.effectType != WindowEffectType::None && state.effectType != WindowEffectType::Fade) {
                    state.effectProgress = t;
                    if (t >= 1.0f) {
                        state.animType = WindowAnimationType::None;
                        state.effectType = WindowEffectType::None;
                        state.effectProgress = 0.0f;
                        shouldClose = true;
                        return true;
                    }
                } else {
                    state.overlayAlpha = static_cast<uint8_t>(static_cast<int>(t * 255.0f));
                    if (t >= 1.0f) {
                        state.overlayAlpha = 0;
                        state.animType = WindowAnimationType::None;
                        shouldClose = true;
                        return true;
                    }
                }
                break;
            }
            
            case WindowAnimationType::Minimize: {
                int newY = state.animStartY + static_cast<int>((state.animEndY - state.animStartY) * t);
                windowY = newY;
                if (t >= 1.0f) {
                    windowY = state.animEndY;
                    state.animType = WindowAnimationType::None;
                    return true;
                }
                break;
            }
            
            case WindowAnimationType::Restore: {
                int newY = state.animStartY + static_cast<int>((state.animEndY - state.animStartY) * t);
                windowY = newY;
                if (t >= 1.0f) {
                    windowY = state.normY;
                    state.animType = WindowAnimationType::None;
                    return true;
                }
                break;
            }
            
            default:
                break;
        }
        
        return false;
    }

    /// Check if window needs fade overlay drawn
    static bool NeedsFadeOverlay(const WindowAnimState& state) {
        if (state.animType == WindowAnimationType::None) return false;
        if (state.effectType != WindowEffectType::None && state.effectType != WindowEffectType::Fade) {
            return false; // special effects handle their own overlay
        }
        return state.overlayAlpha > 0 && UISettings::EnableFadeOverlay;
    }

    /// Get fade overlay alpha (0-255)
    static uint8_t GetFadeOverlayAlpha(const WindowAnimState& state) {
        return state.overlayAlpha;
    }

    /// Check if window needs special effect rendered
    static bool NeedsSpecialEffect(const WindowAnimState& state) {
        return state.effectType != WindowEffectType::None && 
               state.effectType != WindowEffectType::Fade &&
               state.effectProgress > 0.0f;
    }

    /// Get current effect type and progress
    static WindowEffectType GetEffectType(const WindowAnimState& state) {
        return state.effectType;
    }

    static float GetEffectProgress(const WindowAnimState& state) {
        return state.effectProgress;
    }
};

} // namespace gui
} // namespace gxos
