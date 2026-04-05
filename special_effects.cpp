//
// Special Effects - Window Animation Effects Implementation
// Ported from guideXOS.Legacy SpecialEffects.cs
//
// Copyright (c) 2026 guideXOS Server
//

#include "special_effects.h"
#include <algorithm>

namespace gxos {
namespace gui {

// Static member initialization
uint32_t SpecialEffects::s_randState = 12345;

// Pre-computed sine table (0-359 degrees, scaled by 1000)
const int SpecialEffects::s_sinTable[360] = {
    0,17,35,52,70,87,105,122,139,156,174,191,208,225,242,259,276,292,309,326,
    342,358,375,391,407,423,438,454,469,485,500,515,530,545,559,574,588,602,616,629,
    643,656,669,682,695,707,719,731,743,755,766,777,788,799,809,819,829,839,848,857,
    866,875,883,891,899,906,914,921,927,934,940,946,951,956,961,966,970,974,978,982,
    985,988,990,993,995,996,998,999,999,1000,1000,999,999,998,996,995,993,990,988,985,
    982,978,974,970,966,961,956,951,946,940,934,927,921,914,906,899,891,883,875,866,
    857,848,839,829,819,809,799,788,777,766,755,743,731,719,707,695,682,669,656,643,
    629,616,602,588,574,559,545,530,515,500,485,469,454,438,423,407,391,375,358,342,
    326,309,292,276,259,242,225,208,191,174,156,139,122,105,87,70,52,35,17,0,
    -17,-35,-52,-70,-87,-105,-122,-139,-156,-174,-191,-208,-225,-242,-259,-276,-292,-309,-326,-342,
    -358,-375,-391,-407,-423,-438,-454,-469,-485,-500,-515,-530,-545,-559,-574,-588,-602,-616,-629,-643,
    -656,-669,-682,-695,-707,-719,-731,-743,-755,-766,-777,-788,-799,-809,-819,-829,-839,-848,-857,-866,
    -875,-883,-891,-899,-906,-914,-921,-927,-934,-940,-946,-951,-956,-961,-966,-970,-974,-978,-982,-985,
    -988,-990,-993,-995,-996,-998,-999,-999,-1000,-1000,-999,-999,-998,-996,-995,-993,-990,-988,-985,-982,
    -978,-974,-970,-966,-961,-956,-951,-946,-940,-934,-927,-921,-914,-906,-899,-891,-883,-875,-866,-857,
    -848,-839,-829,-819,-809,-799,-788,-777,-766,-755,-743,-731,-719,-707,-695,-682,-669,-656,-643,-629,
    -616,-602,-588,-574,-559,-545,-530,-515,-500,-485,-469,-454,-438,-423,-407,-391,-375,-358,-342,-326,
    -309,-292,-276,-259,-242,-225,-208,-191,-174,-156,-139,-122,-105,-87,-70,-52,-35,-17
};

// Pre-computed cosine table (0-359 degrees, scaled by 1000)
const int SpecialEffects::s_cosTable[360] = {
    1000,1000,999,999,998,996,995,993,990,988,985,982,978,974,970,966,961,956,951,946,
    940,934,927,921,914,906,899,891,883,875,866,857,848,839,829,819,809,799,788,777,
    766,755,743,731,719,707,695,682,669,656,643,629,616,602,588,574,559,545,530,515,
    500,485,469,454,438,423,407,391,375,358,342,326,309,292,276,259,242,225,208,191,
    174,156,139,122,105,87,70,52,35,17,0,-17,-35,-52,-70,-87,-105,-122,-139,-156,
    -174,-191,-208,-225,-242,-259,-276,-292,-309,-326,-342,-358,-375,-391,-407,-423,-438,-454,-469,-485,
    -500,-515,-530,-545,-559,-574,-588,-602,-616,-629,-643,-656,-669,-682,-695,-707,-719,-731,-743,-755,
    -766,-777,-788,-799,-809,-819,-829,-839,-848,-857,-866,-875,-883,-891,-899,-906,-914,-921,-927,-934,
    -940,-946,-951,-956,-961,-966,-970,-974,-978,-982,-985,-988,-990,-993,-995,-996,-998,-999,-999,-1000,
    -1000,-999,-999,-998,-996,-995,-993,-990,-988,-985,-982,-978,-974,-970,-966,-961,-956,-951,-946,-940,
    -934,-927,-921,-914,-906,-899,-891,-883,-875,-866,-857,-848,-839,-829,-819,-809,-799,-788,-777,-766,
    -755,-743,-731,-719,-707,-695,-682,-669,-656,-643,-629,-616,-602,-588,-574,-559,-545,-530,-515,-500,
    -485,-469,-454,-438,-423,-407,-391,-375,-358,-342,-326,-309,-292,-276,-259,-242,-225,-208,-191,-174,
    -156,-139,-122,-105,-87,-70,-52,-35,-17,0,17,35,52,70,87,105,122,139,156,174,
    191,208,225,242,259,276,292,309,326,342,358,375,391,407,423,438,454,469,485,500,
    515,530,545,559,574,588,602,616,629,643,656,669,682,695,707,719,731,743,755,766,
    777,788,799,809,819,829,839,848,857,866,875,883,891,899,906,914,921,927,934,940,
    946,951,956,961,966,970,974,978,982,985,988,990,993,995,996,998,999,999
};

// ============================================================
// Helper functions
// ============================================================

int SpecialEffects::NextRandom(int max) {
    if (max <= 0) return 0;
    s_randState = s_randState * 1103515245 + 12345;
    return static_cast<int>((s_randState >> 16) % static_cast<uint32_t>(max));
}

int SpecialEffects::FastSin(int degrees) {
    degrees = degrees % 360;
    if (degrees < 0) degrees += 360;
    return s_sinTable[degrees];
}

int SpecialEffects::FastCos(int degrees) {
    degrees = degrees % 360;
    if (degrees < 0) degrees += 360;
    return s_cosTable[degrees];
}

int SpecialEffects::IntSqrt(int n) {
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

int SpecialEffects::IntAbs(int x) {
    return x < 0 ? -x : x;
}

void SpecialEffects::BlendPixel(uint32_t* pixels, int screenW, int screenH, int pitch,
                                 int px, int py, uint32_t color) {
    if (px < 0 || px >= screenW || py < 0 || py >= screenH) return;
    
    uint8_t alpha = static_cast<uint8_t>((color >> 24) & 0xFF);
    if (alpha == 0) return;
    
    int idx = py * (pitch / 4) + px;
    uint32_t dst = pixels[idx];
    
    if (alpha == 255) {
        pixels[idx] = color | 0xFF000000;
        return;
    }
    
    // Alpha blend
    uint8_t srcR = static_cast<uint8_t>((color >> 16) & 0xFF);
    uint8_t srcG = static_cast<uint8_t>((color >> 8) & 0xFF);
    uint8_t srcB = static_cast<uint8_t>(color & 0xFF);
    
    uint8_t dstR = static_cast<uint8_t>((dst >> 16) & 0xFF);
    uint8_t dstG = static_cast<uint8_t>((dst >> 8) & 0xFF);
    uint8_t dstB = static_cast<uint8_t>(dst & 0xFF);
    
    uint8_t outR = static_cast<uint8_t>((srcR * alpha + dstR * (255 - alpha)) / 255);
    uint8_t outG = static_cast<uint8_t>((srcG * alpha + dstG * (255 - alpha)) / 255);
    uint8_t outB = static_cast<uint8_t>((srcB * alpha + dstB * (255 - alpha)) / 255);
    
    pixels[idx] = 0xFF000000 | (static_cast<uint32_t>(outR) << 16) |
                  (static_cast<uint32_t>(outG) << 8) | outB;
}

void SpecialEffects::FillRectAlpha(uint32_t* pixels, int screenW, int screenH, int pitch,
                                    int x, int y, int w, int h, uint32_t color) {
    int x1 = std::max(0, x);
    int y1 = std::max(0, y);
    int x2 = std::min(screenW, x + w);
    int y2 = std::min(screenH, y + h);
    
    uint8_t alpha = static_cast<uint8_t>((color >> 24) & 0xFF);
    if (alpha == 0) return;
    
    for (int py = y1; py < y2; py++) {
        for (int px = x1; px < x2; px++) {
            BlendPixel(pixels, screenW, screenH, pitch, px, py, color);
        }
    }
}

void SpecialEffects::DrawHLine(uint32_t* pixels, int screenW, int screenH, int pitch,
                                int x1, int x2, int y, uint32_t color) {
    if (y < 0 || y >= screenH) return;
    if (x1 > x2) std::swap(x1, x2);
    x1 = std::max(0, x1);
    x2 = std::min(screenW - 1, x2);
    
    for (int px = x1; px <= x2; px++) {
        BlendPixel(pixels, screenW, screenH, pitch, px, y, color);
    }
}

void SpecialEffects::DrawLine(uint32_t* pixels, int screenW, int screenH, int pitch,
                               int x1, int y1, int x2, int y2, uint32_t color) {
    // Bresenham's line algorithm
    int dx = IntAbs(x2 - x1);
    int dy = IntAbs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        BlendPixel(pixels, screenW, screenH, pitch, x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

// ============================================================
// Main effect dispatcher
// ============================================================

WindowEffectType SpecialEffects::GetRandomEffect() {
    int choice = NextRandom(10);
    switch (choice) {
        case 0: return WindowEffectType::Digitize;
        case 1: return WindowEffectType::Derezz;
        case 2: return WindowEffectType::BurnIn;
        case 3: return WindowEffectType::BurnOut;
        case 4: return WindowEffectType::SmokeIn;
        case 5: return WindowEffectType::SmokeOut;
        case 6: return WindowEffectType::Glitch;
        case 7: return WindowEffectType::Ripple;
        case 8: return WindowEffectType::Explode;
        default: return WindowEffectType::Implode;
    }
}

void SpecialEffects::RenderEffect(uint32_t* pixels, int screenW, int screenH, int pitch,
                                   WindowEffectType effect,
                                   int x, int y, int width, int height, int barHeight,
                                   float progress) {
    if (!pixels) return;
    if (width <= 0 || height <= 0 || barHeight < 0) return;
    if ((height + barHeight) <= 0) return;
    
    // Convert float progress (0.0-1.0) to integer (0-1000)
    int progressInt = static_cast<int>(progress * 1000);
    if (progressInt < 0) progressInt = 0;
    if (progressInt > 1000) progressInt = 1000;
    
    switch (effect) {
        case WindowEffectType::Digitize:
            RenderDigitize(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::Derezz:
            RenderDerezz(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::BurnIn:
            RenderBurnIn(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::BurnOut:
            RenderBurnOut(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::SmokeIn:
            RenderSmokeIn(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::SmokeOut:
            RenderSmokeOut(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::Glitch:
            RenderGlitch(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::Ripple:
            RenderRipple(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::Explode:
            RenderExplode(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        case WindowEffectType::Implode:
            RenderImplode(pixels, screenW, screenH, pitch, x, y, width, height, barHeight, progressInt);
            break;
        default:
            break;
    }
}

// ============================================================
// Individual effect implementations
// ============================================================

void SpecialEffects::RenderDigitize(uint32_t* pixels, int screenW, int screenH, int pitch,
                                     int x, int y, int width, int height, int barHeight, int progressInt) {
    int totalHeight = height + barHeight;
    if (totalHeight <= 0 || width <= 0) return;
    
    int scanlineCount = UISettings::DigitizeScanlineCount;
    if (scanlineCount <= 0) return;
    uint32_t color = UISettings::DigitizeColor;
    
    // Scanlines appear from top to bottom
    int visibleScanlines = (scanlineCount * progressInt) / 1000;
    
    for (int i = 0; i < visibleScanlines; i++) {
        int scanY = y - barHeight + (i * totalHeight / scanlineCount);
        
        // Main scanline
        DrawHLine(pixels, screenW, screenH, pitch, x, x + width, scanY, color);
        
        // Glow effect around scanline
        if (visibleScanlines > 0) {
            uint8_t alpha = static_cast<uint8_t>(100 * (1000 - (i * 1000 / visibleScanlines)) / 1000);
            uint32_t glowColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
            if (scanY > 0) DrawHLine(pixels, screenW, screenH, pitch, x, x + width, scanY - 1, glowColor);
            if (scanY < screenH - 1) DrawHLine(pixels, screenW, screenH, pitch, x, x + width, scanY + 1, glowColor);
        }
        
        // Random pixel sparkles along the scanline
        for (int j = 0; j < width / 20; j++) {
            int sparkX = x + NextRandom(width);
            FillRectAlpha(pixels, screenW, screenH, pitch, sparkX, scanY - 1, 2, 3, color);
        }
    }
    
    // Progressive reveal - draw darker overlay on areas not yet digitized
    if (progressInt < 1000) {
        int revealHeight = (totalHeight * progressInt) / 1000;
        int darkY = y - barHeight + revealHeight;
        int darkHeight = totalHeight - revealHeight;
        if (darkHeight > 0) {
            uint32_t darkOverlay = 0xDD000000;
            FillRectAlpha(pixels, screenW, screenH, pitch, x, darkY, width, darkHeight, darkOverlay);
        }
    }
}

void SpecialEffects::RenderDerezz(uint32_t* pixels, int screenW, int screenH, int pitch,
                                   int x, int y, int width, int height, int barHeight, int progressInt) {
    int totalHeight = height + barHeight;
    if (totalHeight <= 0 || width <= 0) return;
    uint32_t color = UISettings::DerezzColor;
    
    // Grid of breaking pixels
    int gridSize = 8;
    int gridW = width / gridSize;
    int gridH = totalHeight / gridSize;
    
    for (int gy = 0; gy < gridH; gy++) {
        for (int gx = 0; gx < gridW; gx++) {
            // Stagger the breakdown based on position
            int cellProgressInt = progressInt + (NextRandom(100) * 1000 / 500);
            if (cellProgressInt > 1000) cellProgressInt = 1000;
            
            int cellX = x + (gx * gridSize);
            int cellY = (y - barHeight) + (gy * gridSize);
            
            if (cellProgressInt > 300) {
                uint8_t alpha = static_cast<uint8_t>(((1000 - cellProgressInt) * 255) / 1000);
                uint32_t cellColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
                
                // Draw breaking cell outline
                DrawHLine(pixels, screenW, screenH, pitch, cellX, cellX + gridSize, cellY, cellColor);
                DrawHLine(pixels, screenW, screenH, pitch, cellX, cellX + gridSize, cellY + gridSize, cellColor);
                DrawLine(pixels, screenW, screenH, pitch, cellX, cellY, cellX, cellY + gridSize, cellColor);
                DrawLine(pixels, screenW, screenH, pitch, cellX + gridSize, cellY, cellX + gridSize, cellY + gridSize, cellColor);
                
                // Add falling effect
                if (cellProgressInt > 600) {
                    int fallDist = ((cellProgressInt - 600) * 20) / 1000;
                    DrawHLine(pixels, screenW, screenH, pitch, cellX, cellX + gridSize, cellY + fallDist, cellColor);
                }
            }
        }
    }
    
    // Fade overlay
    uint8_t fadeAlpha = static_cast<uint8_t>((progressInt * 200) / 1000);
    uint32_t fadeColor = static_cast<uint32_t>(fadeAlpha) << 24;
    FillRectAlpha(pixels, screenW, screenH, pitch, x, y - barHeight, width, totalHeight, fadeColor);
}

void SpecialEffects::RenderBurnIn(uint32_t* pixels, int screenW, int screenH, int pitch,
                                   int x, int y, int width, int height, int barHeight, int progressInt) {
    int particleCount = UISettings::BurnParticleCount;
    if (particleCount <= 0) return;
    uint32_t color = UISettings::BurnColor;
    
    int centerX = x + width / 2;
    int centerY = y - barHeight + (height + barHeight) / 2;
    
    for (int i = 0; i < particleCount; i++) {
        int angleDeg = (i * 137) % 360;
        int radius = ((i * 17) % 200) + 50;
        
        // Particles move inward as progress increases
        int currentRadius = (radius * (1000 - progressInt)) / 1000;
        
        int px = centerX + (currentRadius * FastCos(angleDeg)) / 1000;
        int py = centerY + (currentRadius * FastSin(angleDeg)) / 1000;
        
        uint8_t alpha = static_cast<uint8_t>((progressInt * 255) / 1000);
        uint32_t pColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
        
        FillRectAlpha(pixels, screenW, screenH, pitch, px, py, 3, 3, pColor);
        
        // Trailing glow
        if (currentRadius > 5) {
            int trailRadius = currentRadius + 10;
            int trailX = centerX + (trailRadius * FastCos(angleDeg)) / 1000;
            int trailY = centerY + (trailRadius * FastSin(angleDeg)) / 1000;
            uint32_t trailColor = (static_cast<uint32_t>(alpha / 2) << 24) | (color & 0x00FFFFFF);
            FillRectAlpha(pixels, screenW, screenH, pitch, trailX, trailY, 2, 2, trailColor);
        }
    }
    
    // Fade in overlay
    uint8_t fadeAlpha = static_cast<uint8_t>(((1000 - progressInt) * 200) / 1000);
    uint32_t fadeColor = static_cast<uint32_t>(fadeAlpha) << 24;
    FillRectAlpha(pixels, screenW, screenH, pitch, x, y - barHeight, width, height + barHeight, fadeColor);
}

void SpecialEffects::RenderBurnOut(uint32_t* pixels, int screenW, int screenH, int pitch,
                                    int x, int y, int width, int height, int barHeight, int progressInt) {
    int particleCount = UISettings::BurnParticleCount;
    if (particleCount <= 0) return;
    uint32_t color = UISettings::BurnColor;
    
    int centerX = x + width / 2;
    int centerY = y - barHeight + (height + barHeight) / 2;
    
    for (int i = 0; i < particleCount; i++) {
        int angleDeg = (i * 137) % 360;
        int radius = ((i * 17) % 150) + 30;
        
        // Particles move outward as progress increases
        int currentRadius = (radius * progressInt) / 1000;
        
        int px = centerX + (currentRadius * FastCos(angleDeg)) / 1000;
        int py = centerY + (currentRadius * FastSin(angleDeg)) / 1000;
        
        uint8_t alpha = static_cast<uint8_t>(((1000 - progressInt) * 255) / 1000);
        uint32_t pColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
        
        FillRectAlpha(pixels, screenW, screenH, pitch, px, py, 3, 3, pColor);
    }
}

void SpecialEffects::RenderSmokeIn(uint32_t* pixels, int screenW, int screenH, int pitch,
                                    int x, int y, int width, int height, int barHeight, int progressInt) {
    int particleCount = UISettings::SmokeParticleCount;
    if (particleCount <= 0) return;
    uint32_t color = UISettings::SmokeColor;
    
    int totalHeight = height + barHeight;
    if (width <= 0 || totalHeight <= 0) return;
    
    for (int i = 0; i < particleCount; i++) {
        int posX = (i * 37) % width;
        int posY = (i * 53) % totalHeight;
        
        int driftX = ((i * 11) % 40) - 20;
        int driftY = -((i * 7) % 30);
        
        int px = x + posX + (driftX * (1000 - progressInt) / 1000);
        int py = (y - barHeight) + posY + (driftY * (1000 - progressInt) / 1000);
        
        uint8_t alpha = static_cast<uint8_t>((progressInt * 100) / 1000);
        uint32_t pColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
        
        FillRectAlpha(pixels, screenW, screenH, pitch, px, py, 4, 4, pColor);
    }
    
    // Fade in
    uint8_t fadeAlpha = static_cast<uint8_t>(((1000 - progressInt) * 150) / 1000);
    uint32_t fadeColor = static_cast<uint32_t>(fadeAlpha) << 24;
    FillRectAlpha(pixels, screenW, screenH, pitch, x, y - barHeight, width, height + barHeight, fadeColor);
}

void SpecialEffects::RenderSmokeOut(uint32_t* pixels, int screenW, int screenH, int pitch,
                                     int x, int y, int width, int height, int barHeight, int progressInt) {
    int particleCount = UISettings::SmokeParticleCount;
    if (particleCount <= 0) return;
    uint32_t color = UISettings::SmokeColor;
    
    int totalHeight = height + barHeight;
    if (width <= 0 || totalHeight <= 0) return;
    
    for (int i = 0; i < particleCount; i++) {
        int posX = (i * 37) % width;
        int posY = (i * 53) % totalHeight;
        
        int driftX = ((i * 11) % 40) - 20;
        int driftY = -((i * 7) % 30);
        
        int px = x + posX + (driftX * progressInt / 1000);
        int py = (y - barHeight) + posY + (driftY * progressInt / 1000);
        
        uint8_t alpha = static_cast<uint8_t>(((1000 - progressInt) * 100) / 1000);
        uint32_t pColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
        
        int pSize = 4 + (progressInt * 2 / 1000);
        FillRectAlpha(pixels, screenW, screenH, pitch, px, py, pSize, pSize, pColor);
    }
    
    // Fade out
    uint8_t fadeAlpha = static_cast<uint8_t>((progressInt * 200) / 1000);
    uint32_t fadeColor = static_cast<uint32_t>(fadeAlpha) << 24;
    FillRectAlpha(pixels, screenW, screenH, pitch, x, y - barHeight, width, height + barHeight, fadeColor);
}

void SpecialEffects::RenderGlitch(uint32_t* pixels, int screenW, int screenH, int pitch,
                                   int x, int y, int width, int height, int barHeight, int progressInt) {
    uint32_t color = UISettings::GlitchColor;
    int intensity = UISettings::GlitchIntensity;
    if (intensity <= 0) intensity = 1;
    
    int totalHeight = height + barHeight;
    if (totalHeight <= 0 || width <= 0) return;
    
    // More glitches at start and end
    int glitchAmount = 1000 - (IntAbs(progressInt - 500) * 2);
    int glitchCount = (glitchAmount * 15) / 1000;
    
    for (int i = 0; i < glitchCount; i++) {
        int glitchY = (y - barHeight) + ((i * 71) % totalHeight);
        int glitchH = 2 + ((i * 13) % 8);
        int offset = ((i % 2) == 0 ? 1 : -1) * ((i * 7) % intensity);
        
        uint8_t alpha = static_cast<uint8_t>((glitchAmount * 180) / 1000);
        uint32_t glitchColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
        FillRectAlpha(pixels, screenW, screenH, pitch, x + offset, glitchY, width, glitchH, glitchColor);
        
        // RGB separation effect
        uint32_t redShift = (static_cast<uint32_t>(alpha / 2) << 24) | 0x00FF0000;
        uint32_t blueShift = (static_cast<uint32_t>(alpha / 2) << 24) | 0x000000FF;
        DrawHLine(pixels, screenW, screenH, pitch, x + offset - 2, x + width + offset - 2, glitchY, redShift);
        DrawHLine(pixels, screenW, screenH, pitch, x + offset + 2, x + width + offset + 2, glitchY, blueShift);
    }
    
    // Fade based on effect direction
    uint8_t fadeAlpha;
    if (progressInt < 500) {
        fadeAlpha = static_cast<uint8_t>(((1000 - progressInt * 2) * 200) / 1000);
    } else {
        fadeAlpha = static_cast<uint8_t>(((progressInt - 500) * 2 * 200) / 1000);
    }
    uint32_t fadeColor = static_cast<uint32_t>(fadeAlpha) << 24;
    FillRectAlpha(pixels, screenW, screenH, pitch, x, y - barHeight, width, height + barHeight, fadeColor);
}

void SpecialEffects::RenderRipple(uint32_t* pixels, int screenW, int screenH, int pitch,
                                   int x, int y, int width, int height, int barHeight, int progressInt) {
    int waveCount = UISettings::RippleWaveCount;
    if (waveCount <= 0) return;
    uint32_t color = UISettings::RippleColor;
    
    int centerX = x + width / 2;
    int centerY = y - barHeight + (height + barHeight) / 2;
    
    int totalH = height + barHeight;
    if (totalH <= 0 || width <= 0) return;
    int maxRadius = IntSqrt(width * width + totalH * totalH) / 2;
    
    for (int i = 0; i < waveCount; i++) {
        int waveProgressInt = progressInt - (i * 100);
        if (waveProgressInt < 0) continue;
        if (waveProgressInt > 1000) waveProgressInt = 1000;
        
        int radius = (maxRadius * waveProgressInt) / 1000;
        uint8_t alpha = static_cast<uint8_t>(((1000 - waveProgressInt) * 180) / 1000);
        uint32_t waveColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
        
        // Draw circle with line segments
        int segments = 36;
        int degStep = 360 / segments;
        for (int s = 0; s < segments; s++) {
            int deg1 = s * degStep;
            int deg2 = (s + 1) * degStep;
            
            int x1 = centerX + (radius * FastCos(deg1)) / 1000;
            int y1 = centerY + (radius * FastSin(deg1)) / 1000;
            int x2 = centerX + (radius * FastCos(deg2)) / 1000;
            int y2 = centerY + (radius * FastSin(deg2)) / 1000;
            
            DrawLine(pixels, screenW, screenH, pitch, x1, y1, x2, y2, waveColor);
        }
    }
    
    // Fade in/out based on direction
    uint8_t fadeAlpha;
    if (progressInt < 500) {
        fadeAlpha = static_cast<uint8_t>(((1000 - progressInt * 2) * 150) / 1000);
    } else {
        fadeAlpha = static_cast<uint8_t>(((progressInt - 500) * 2 * 150) / 1000);
    }
    uint32_t fadeColor = static_cast<uint32_t>(fadeAlpha) << 24;
    FillRectAlpha(pixels, screenW, screenH, pitch, x, y - barHeight, width, height + barHeight, fadeColor);
}

void SpecialEffects::RenderExplode(uint32_t* pixels, int screenW, int screenH, int pitch,
                                    int x, int y, int width, int height, int barHeight, int progressInt) {
    int particleCount = UISettings::ExplodeParticleCount;
    if (particleCount <= 0) return;
    uint32_t color = UISettings::ExplodeColor;
    
    int centerX = x + width / 2;
    int centerY = y - barHeight + (height + barHeight) / 2;
    
    for (int i = 0; i < particleCount; i++) {
        int angleDeg = (i * 137) % 360;
        int speed = ((i * 23) % 100) + 50;
        
        int dist = (speed * progressInt) / 1000;
        int px = centerX + (dist * FastCos(angleDeg)) / 1000;
        int py = centerY + (dist * FastSin(angleDeg)) / 1000;
        
        uint8_t alpha = static_cast<uint8_t>(((1000 - progressInt) * 255) / 1000);
        uint32_t pColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
        
        int pSize = 2 + (progressInt * 2 / 1000);
        FillRectAlpha(pixels, screenW, screenH, pitch, px, py, pSize, pSize, pColor);
        
        // Trail
        if (dist > 10) {
            int trailDist = dist - 10;
            int trailX = centerX + (trailDist * FastCos(angleDeg)) / 1000;
            int trailY = centerY + (trailDist * FastSin(angleDeg)) / 1000;
            uint32_t trailColor = (static_cast<uint32_t>(alpha / 2) << 24) | (color & 0x00FFFFFF);
            FillRectAlpha(pixels, screenW, screenH, pitch, trailX, trailY, 2, 2, trailColor);
        }
    }
}

void SpecialEffects::RenderImplode(uint32_t* pixels, int screenW, int screenH, int pitch,
                                    int x, int y, int width, int height, int barHeight, int progressInt) {
    int particleCount = UISettings::ExplodeParticleCount;
    if (particleCount <= 0) return;
    uint32_t color = UISettings::ExplodeColor;
    
    int centerX = x + width / 2;
    int centerY = y - barHeight + (height + barHeight) / 2;
    
    for (int i = 0; i < particleCount; i++) {
        int angleDeg = (i * 137) % 360;
        int speed = ((i * 23) % 100) + 50;
        
        int dist = (speed * (1000 - progressInt)) / 1000;
        int px = centerX + (dist * FastCos(angleDeg)) / 1000;
        int py = centerY + (dist * FastSin(angleDeg)) / 1000;
        
        uint8_t alpha = static_cast<uint8_t>((progressInt * 255) / 1000);
        uint32_t pColor = (static_cast<uint32_t>(alpha) << 24) | (color & 0x00FFFFFF);
        
        int pSize = 2 + ((1000 - progressInt) * 2 / 1000);
        FillRectAlpha(pixels, screenW, screenH, pitch, px, py, pSize, pSize, pColor);
    }
    
    // Fade in
    uint8_t fadeAlpha = static_cast<uint8_t>(((1000 - progressInt) * 150) / 1000);
    uint32_t fadeColor = static_cast<uint32_t>(fadeAlpha) << 24;
    FillRectAlpha(pixels, screenW, screenH, pitch, x, y - barHeight, width, height + barHeight, fadeColor);
}

} // namespace gui
} // namespace gxos
