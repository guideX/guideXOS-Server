#pragma once
//
// Special Effects - Window Animation Effects
// Ported from guideXOS.Legacy SpecialEffects.cs
//
// These effects work directly with the video backend's pixel buffer,
// making them compatible with both Windows (GDI) and UEFI bare-metal.
//
// Copyright (c) 2026 guideXOS Server
//

#include "window_effects.h"
#include "ui_settings.h"
#include <cstdint>

namespace gxos {
namespace gui {

/// Special visual effects for window animations (Tron-style digitize, burn, smoke, etc.)
/// Renders directly to a pixel buffer for cross-platform compatibility.
class SpecialEffects {
public:
    /// Render a special effect overlay on a window area
    /// @param pixels The pixel buffer to render into (XRGB format)
    /// @param screenW Screen/buffer width
    /// @param screenH Screen/buffer height
    /// @param pitch   Bytes per scanline (usually screenW * 4)
    /// @param effect  The effect type to render
    /// @param x       Window X position
    /// @param y       Window Y position (content area, not including title bar)
    /// @param width   Window width
    /// @param height  Window height
    /// @param barHeight Title bar height
    /// @param progress Effect progress from 0.0 (start) to 1.0 (complete)
    static void RenderEffect(uint32_t* pixels, int screenW, int screenH, int pitch,
                             WindowEffectType effect,
                             int x, int y, int width, int height, int barHeight,
                             float progress);
    
    /// Get a random effect type (excluding None, Fade, and Random)
    static WindowEffectType GetRandomEffect();

private:
    // Simple pseudo-random number generator state
    static uint32_t s_randState;
    
    // Helper: next pseudo-random number
    static int NextRandom(int max);
    
    // Helper: integer sine (returns value * 1000)
    static int FastSin(int degrees);
    
    // Helper: integer cosine (returns value * 1000)
    static int FastCos(int degrees);
    
    // Helper: integer square root approximation
    static int IntSqrt(int n);
    
    // Helper: integer absolute value
    static int IntAbs(int x);
    
    // Helper: blend a pixel with alpha
    static void BlendPixel(uint32_t* pixels, int screenW, int screenH, int pitch,
                           int px, int py, uint32_t color);
    
    // Helper: draw a filled rectangle with alpha blending
    static void FillRectAlpha(uint32_t* pixels, int screenW, int screenH, int pitch,
                              int x, int y, int w, int h, uint32_t color);
    
    // Helper: draw a horizontal line
    static void DrawHLine(uint32_t* pixels, int screenW, int screenH, int pitch,
                          int x1, int x2, int y, uint32_t color);
    
    // Helper: draw a line (Bresenham)
    static void DrawLine(uint32_t* pixels, int screenW, int screenH, int pitch,
                         int x1, int y1, int x2, int y2, uint32_t color);
    
    // Individual effect renderers
    static void RenderDigitize(uint32_t* pixels, int screenW, int screenH, int pitch,
                               int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderDerezz(uint32_t* pixels, int screenW, int screenH, int pitch,
                             int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderBurnIn(uint32_t* pixels, int screenW, int screenH, int pitch,
                             int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderBurnOut(uint32_t* pixels, int screenW, int screenH, int pitch,
                              int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderSmokeIn(uint32_t* pixels, int screenW, int screenH, int pitch,
                              int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderSmokeOut(uint32_t* pixels, int screenW, int screenH, int pitch,
                               int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderGlitch(uint32_t* pixels, int screenW, int screenH, int pitch,
                             int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderRipple(uint32_t* pixels, int screenW, int screenH, int pitch,
                             int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderExplode(uint32_t* pixels, int screenW, int screenH, int pitch,
                              int x, int y, int width, int height, int barHeight, int progressInt);
    
    static void RenderImplode(uint32_t* pixels, int screenW, int screenH, int pitch,
                              int x, int y, int width, int height, int barHeight, int progressInt);
    
    // Pre-computed sine/cosine lookup tables (scaled by 1000)
    static const int s_sinTable[360];
    static const int s_cosTable[360];
};

} // namespace gui
} // namespace gxos
