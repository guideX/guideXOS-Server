#pragma once
//
// Window Rendering Helpers
// Provides GDI helper functions for drawing windows matching guideXOS.Legacy style
//
// Copyright (c) 2026 guideXOS Server
//

#include "ui_settings.h"
#include "window_effects.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gxos {
namespace gui {

/// Helper class for drawing windows with effects matching guideXOS.Legacy
class WindowRenderer {
public:
    /// Draw a rounded rectangle (or regular rectangle if radius is 0)
    static void DrawRoundedRect(HDC dc, int x, int y, int w, int h, COLORREF color, int radius = 0) {
        HBRUSH brush = CreateSolidBrush(color);
        if (radius > 0) {
            HRGN rgn = CreateRoundRectRgn(x, y, x + w + 1, y + h + 1, radius * 2, radius * 2);
            FillRgn(dc, rgn, brush);
            DeleteObject(rgn);
        } else {
            RECT r{ x, y, x + w, y + h };
            FillRect(dc, &r, brush);
        }
        DeleteObject(brush);
    }
    
    /// Draw a window glow/shadow effect
    static void DrawWindowGlow(HDC dc, int x, int y, int w, int h, int titleBarH, bool focused) {
        if (!UISettings::EnableWindowGlow) return;
        
        int glowRadius = UISettings::WindowGlowRadius;
        uint32_t glowColor = focused ? UISettings::WindowGlowFocusedColor : UISettings::WindowGlowColor;
        
        // Extract alpha and RGB from ARGB
        int alpha = (glowColor >> 24) & 0xFF;
        int r = (glowColor >> 16) & 0xFF;
        int g = (glowColor >> 8) & 0xFF;
        int b = glowColor & 0xFF;
        
        // Draw concentric glow layers (outer to inner)
        for (int i = glowRadius; i >= 1; i--) {
            int layerAlpha = alpha * i / glowRadius / 2;
            COLORREF layerColor = RGB(
                r * layerAlpha / 255,
                g * layerAlpha / 255,
                b * layerAlpha / 255
            );
            
            HPEN pen = CreatePen(PS_SOLID, 1, layerColor);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            
            int cornerRadius = UISettings::EnableRoundedCorners ? UISettings::WindowCornerRadius : 0;
            if (cornerRadius > 0) {
                RoundRect(dc, x - i, y - i, x + w + i, y + h + i, cornerRadius * 2, cornerRadius * 2);
            } else {
                Rectangle(dc, x - i, y - i, x + w + i, y + h + i);
            }
            
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
        }
    }
    
    /// Draw window title bar with optional transparency effect
    static void DrawTitleBar(HDC dc, int x, int y, int w, int h, bool focused, bool transparent = false) {
        if (!UISettings::EnableTitleBarBackground) return;
        
        COLORREF color;
        if (focused) {
            color = transparent ? 
                RGB((UISettings::TitleBarFocusedColor >> 16) & 0xFF,
                    (UISettings::TitleBarFocusedColor >> 8) & 0xFF,
                    UISettings::TitleBarFocusedColor & 0xFF) :
                RGB((UISettings::TitleBarFocusedColor >> 16) & 0xFF,
                    (UISettings::TitleBarFocusedColor >> 8) & 0xFF,
                    UISettings::TitleBarFocusedColor & 0xFF);
        } else {
            color = RGB((UISettings::TitleBarColor >> 16) & 0xFF,
                        (UISettings::TitleBarColor >> 8) & 0xFF,
                        UISettings::TitleBarColor & 0xFF);
        }
        
        int cornerRadius = UISettings::EnableRoundedCorners ? UISettings::WindowCornerRadius : 0;
        DrawRoundedRect(dc, x, y, w, h, color, cornerRadius);
    }
    
    /// Draw a title bar button with Legacy-style appearance
    static void DrawTitleButton(HDC dc, int x, int y, int size, int buttonType, 
                                 bool hover, bool pressed, bool focused) {
        if (!UISettings::EnableTitleBarButtons) return;
        
        // Button colors based on type and state
        COLORREF bgColor;
        COLORREF fgColor = RGB(250, 250, 250);
        
        // buttonType: 0=close, 1=maximize, 2=minimize
        if (buttonType == 0) { // Close button - red tint
            if (pressed) {
                bgColor = RGB((UISettings::CloseButtonPressedColor >> 16) & 0xFF,
                              (UISettings::CloseButtonPressedColor >> 8) & 0xFF,
                              UISettings::CloseButtonPressedColor & 0xFF);
            } else if (hover) {
                bgColor = RGB((UISettings::CloseButtonHoverColor >> 16) & 0xFF,
                              (UISettings::CloseButtonHoverColor >> 8) & 0xFF,
                              UISettings::CloseButtonHoverColor & 0xFF);
            } else {
                bgColor = RGB((UISettings::CloseButtonNormalColor >> 16) & 0xFF,
                              (UISettings::CloseButtonNormalColor >> 8) & 0xFF,
                              UISettings::CloseButtonNormalColor & 0xFF);
            }
        } else if (buttonType == 1) { // Maximize - blue tint
            if (pressed) {
                bgColor = RGB(80, 100, 160);
            } else if (hover) {
                bgColor = RGB((UISettings::MaximizeButtonHoverColor >> 16) & 0xFF,
                              (UISettings::MaximizeButtonHoverColor >> 8) & 0xFF,
                              UISettings::MaximizeButtonHoverColor & 0xFF);
            } else {
                bgColor = RGB((UISettings::ButtonNormalColor >> 16) & 0xFF,
                              (UISettings::ButtonNormalColor >> 8) & 0xFF,
                              UISettings::ButtonNormalColor & 0xFF);
            }
        } else { // Minimize - green tint
            if (pressed) {
                bgColor = RGB(80, 120, 80);
            } else if (hover) {
                bgColor = RGB((UISettings::MinimizeButtonHoverColor >> 16) & 0xFF,
                              (UISettings::MinimizeButtonHoverColor >> 8) & 0xFF,
                              UISettings::MinimizeButtonHoverColor & 0xFF);
            } else {
                bgColor = RGB((UISettings::ButtonNormalColor >> 16) & 0xFF,
                              (UISettings::ButtonNormalColor >> 8) & 0xFF,
                              UISettings::ButtonNormalColor & 0xFF);
            }
        }
        
        // Draw hover glow effect (if enabled)
        if (hover && UISettings::EnableButtonHoverEffects) {
            int glowPad = 2;
            uint32_t glowColor = UISettings::ButtonHoverGlowColor;
            COLORREF glow = RGB((glowColor >> 16) & 0xFF, (glowColor >> 8) & 0xFF, glowColor & 0xFF);
            int cornerRadius = UISettings::EnableRoundedCorners ? 6 : 0;
            DrawRoundedRect(dc, x - glowPad, y - glowPad, size + glowPad * 2, size + glowPad * 2, glow, cornerRadius);
        }
        
        // Draw button background
        if (UISettings::EnableButtonBackgrounds) {
            int cornerRadius = UISettings::EnableRoundedCorners ? 4 : 0;
            DrawRoundedRect(dc, x, y, size, size, bgColor, cornerRadius);
        }
        
        // Draw button border
        if (UISettings::EnableButtonBorders) {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB((UISettings::ButtonBorderColor >> 16) & 0xFF,
                                                   (UISettings::ButtonBorderColor >> 8) & 0xFF,
                                                   UISettings::ButtonBorderColor & 0xFF));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            
            int cornerRadius = UISettings::EnableRoundedCorners ? 4 : 0;
            if (cornerRadius > 0) {
                RoundRect(dc, x, y, x + size, y + size, cornerRadius * 2, cornerRadius * 2);
            } else {
                Rectangle(dc, x, y, x + size, y + size);
            }
            
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
        }
        
        // Draw button icon
        if (UISettings::EnableButtonIcons) {
            HPEN iconPen = CreatePen(PS_SOLID, 2, fgColor);
            HGDIOBJ oldPen = SelectObject(dc, iconPen);
            
            int cx = x + size / 2;
            int cy = y + size / 2;
            int iconSize = size / 3;
            
            if (buttonType == 0) { // Close - X
                MoveToEx(dc, cx - iconSize, cy - iconSize, nullptr);
                LineTo(dc, cx + iconSize, cy + iconSize);
                MoveToEx(dc, cx + iconSize, cy - iconSize, nullptr);
                LineTo(dc, cx - iconSize, cy + iconSize);
            } else if (buttonType == 1) { // Maximize - square
                HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, cx - iconSize, cy - iconSize, cx + iconSize, cy + iconSize);
                SelectObject(dc, oldBrush);
            } else { // Minimize - underscore
                MoveToEx(dc, cx - iconSize, cy + iconSize - 2, nullptr);
                LineTo(dc, cx + iconSize, cy + iconSize - 2);
            }
            
            SelectObject(dc, oldPen);
            DeleteObject(iconPen);
        }
    }
    
    /// Draw window border
    static void DrawWindowBorder(HDC dc, int x, int y, int w, int h, bool focused) {
        if (!UISettings::EnableWindowBorders) return;
        
        uint32_t borderColor = focused ? UISettings::WindowBorderFocusedColor : UISettings::WindowBorderColor;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB((borderColor >> 16) & 0xFF,
                                               (borderColor >> 8) & 0xFF,
                                               borderColor & 0xFF));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        
        int cornerRadius = UISettings::EnableRoundedCorners ? UISettings::WindowCornerRadius : 0;
        if (cornerRadius > 0) {
            RoundRect(dc, x, y, x + w, y + h, cornerRadius * 2, cornerRadius * 2);
        } else {
            Rectangle(dc, x, y, x + w, y + h);
        }
        
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
    }
    
    /// Draw resize grip in bottom-right corner
    static void DrawResizeGrip(HDC dc, int x, int y, int w, int h) {
        if (!UISettings::EnableResizeGrip) return;
        
        int gripSize = UISettings::ResizeGripSize;
        int gripX = x + w - gripSize;
        int gripY = y + h - gripSize;
        
        COLORREF lineColor = RGB((UISettings::ResizeGripLineColor >> 16) & 0xFF,
                                  (UISettings::ResizeGripLineColor >> 8) & 0xFF,
                                  UISettings::ResizeGripLineColor & 0xFF);
        
        HPEN pen = CreatePen(PS_SOLID, 1, lineColor);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        
        // Draw diagonal grip lines
        for (int i = 0; i < 3; i++) {
            int offset = 4 + i * 4;
            MoveToEx(dc, gripX + gripSize - offset, gripY + gripSize - 2, nullptr);
            LineTo(dc, gripX + gripSize - 2, gripY + gripSize - offset);
        }
        
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    
    /// Draw tombstone overlay on window
    static void DrawTombstoneOverlay(HDC dc, int x, int y, int w, int h) {
        if (!UISettings::EnableTombstoneOverlay) return;
        
        // Semi-transparent overlay
        uint32_t overlayColor = UISettings::TombstoneOverlayColor;
        COLORREF color = RGB((overlayColor >> 16) & 0xFF,
                              (overlayColor >> 8) & 0xFF,
                              overlayColor & 0xFF);
        
        HBRUSH brush = CreateSolidBrush(color);
        RECT r{ x, y, x + w, y + h };
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        
        if (UISettings::EnableTombstoneText) {
            uint32_t textColor = UISettings::TombstoneTextColor;
            SetTextColor(dc, RGB((textColor >> 16) & 0xFF,
                                  (textColor >> 8) & 0xFF,
                                  textColor & 0xFF));
            
            const char* text = "Tombstoned";
            SIZE ts;
            GetTextExtentPoint32A(dc, text, (int)strlen(text), &ts);
            TextOutA(dc, x + (w - ts.cx) / 2, y + (h - ts.cy) / 2, text, (int)strlen(text));
        }
    }
};

} // namespace gui
} // namespace gxos

#endif // _WIN32
