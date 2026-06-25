#pragma once
//
// Window Rendering Helpers
// Provides GDI helper functions for drawing windows matching guideXOS.Legacy style
//
// Copyright (c) 2026 guideXOS Server
//

#include "ui_settings.h"
#include "desktop_theme.h"
#include "window_effects.h"

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
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
    static COLORREF ToColorRef(uint32_t value) {
        return RGB((value >> 16) & 0xFF,
                   (value >> 8) & 0xFF,
                   value & 0xFF);
    }

    static uint32_t BlendThemeColor(uint32_t baseColor, uint32_t overlayColor, int overlayPercent) {
        if (overlayPercent <= 0) {
            return baseColor;
        }
        if (overlayPercent >= 100) {
            return overlayColor;
        }

        const int baseR = static_cast<int>((baseColor >> 16) & 0xFF);
        const int baseG = static_cast<int>((baseColor >> 8) & 0xFF);
        const int baseB = static_cast<int>(baseColor & 0xFF);
        const int overR = static_cast<int>((overlayColor >> 16) & 0xFF);
        const int overG = static_cast<int>((overlayColor >> 8) & 0xFF);
        const int overB = static_cast<int>(overlayColor & 0xFF);
        const int keepPercent = 100 - overlayPercent;

        return 0xFF000000u |
            (static_cast<uint32_t>((baseR * keepPercent + overR * overlayPercent) / 100) << 16) |
            (static_cast<uint32_t>((baseG * keepPercent + overG * overlayPercent) / 100) << 8) |
            static_cast<uint32_t>((baseB * keepPercent + overB * overlayPercent) / 100);
    }

    /// Phase 2B keeps rounded chrome conservative: only enable it when the
    /// theme explicitly opts in and the theme radius is positive.
    static bool ShouldUseRoundedWindowChrome(const DesktopTheme& theme) {
        return theme.roundedWindows && theme.windowCornerRadius > 0;
    }

    static int GetWindowChromeCornerRadius(const DesktopTheme& theme) {
        return ShouldUseRoundedWindowChrome(theme) ? theme.windowCornerRadius : 0;
    }

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
        const DesktopTheme& theme = GetCurrentDesktopTheme();
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
            
            int cornerRadius = GetWindowChromeCornerRadius(theme);
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
    
    /// Draw window title bar
    static void DrawTitleBar(HDC dc, int x, int y, int w, int h, bool focused) {
        if (!UISettings::EnableTitleBarBackground) return;

        const DesktopTheme& theme = GetCurrentDesktopTheme();
        const bool sciFiTheme = theme.id == DesktopThemeId::SciFi;
        const uint32_t themeColor = focused ? theme.titleBarBackground : theme.taskbarBackground;
        const COLORREF color = ToColorRef(themeColor);
        int cornerRadius = GetWindowChromeCornerRadius(theme);
        DrawRoundedRect(dc, x, y, w, h, color, cornerRadius);

        if (sciFiTheme && h > 1) {
            const uint32_t highlightColor = focused
                ? BlendThemeColor(theme.titleBarBackground, theme.accent, 48)
                : BlendThemeColor(theme.taskbarBackground, theme.mutedAccent, 28);
            HBRUSH highlight = CreateSolidBrush(ToColorRef(highlightColor));
            RECT line{ x, y, x + w, y + 1 };
            FillRect(dc, &line, highlight);
            DeleteObject(highlight);
        }
    }
    
    /// Draw a title bar button with Legacy-style appearance
    /// buttonType: 0=close, 1=maximize, 2=minimize, 3=tombstone
    static void DrawTitleButton(HDC dc, int x, int y, int size, int buttonType, 
                                 bool hover, bool pressed, bool focused) {
        if (!UISettings::EnableTitleBarButtons) return;
        const DesktopTheme& theme = GetCurrentDesktopTheme();
        const bool sciFiTheme = theme.id == DesktopThemeId::SciFi;
        
        // Legacy-style button colors: dark semi-transparent backgrounds
        // Based on guideXOS.Legacy Window.cs DrawTitleButton
        COLORREF bgColor;
        COLORREF fgColor = pressed ? RGB(238, 238, 238) : RGB(250, 250, 250);

        if (sciFiTheme) {
            const uint32_t chromeBase = focused ? theme.titleBarBackground : theme.taskbarBackground;
            const uint32_t idleFill = BlendThemeColor(chromeBase, theme.windowBorder, 18);
            const uint32_t hoverFill = BlendThemeColor(chromeBase, theme.mutedAccent, 26);
            const uint32_t pressedFill = BlendThemeColor(chromeBase, theme.accent, 22);
            const uint32_t closeHoverFill = BlendThemeColor(chromeBase, theme.accent, 14);

            if (buttonType == 0) {
                bgColor = pressed ? pressedFill : (hover ? closeHoverFill : idleFill);
            } else {
                bgColor = pressed ? pressedFill : (hover ? hoverFill : idleFill);
            }
        } else {
            // Base fill colors matching Legacy: baseFill = pressed ? 0xFF2A2A2A : (hover ? 0xFF343434 : 0xFF2E2E2E)
            if (buttonType == 0) { // Close button - red tinted
                if (pressed) {
                    bgColor = RGB(0x2A, 0x2A, 0x2A);
                } else if (hover) {
                    bgColor = RGB(0x44, 0x34, 0x34); // slight red tint on hover
                } else {
                    bgColor = RGB(0x2E, 0x2E, 0x2E);
                }
            } else if (buttonType == 1) { // Maximize - subtle blue
                if (pressed) {
                    bgColor = RGB(0x2A, 0x2A, 0x2A);
                } else if (hover) {
                    bgColor = RGB(0x34, 0x34, 0x3E); // slight blue tint
                } else {
                    bgColor = RGB(0x2E, 0x2E, 0x2E);
                }
            } else if (buttonType == 2) { // Minimize - subtle green
                if (pressed) {
                    bgColor = RGB(0x2A, 0x2A, 0x2A);
                } else if (hover) {
                    bgColor = RGB(0x34, 0x3E, 0x34); // slight green tint
                } else {
                    bgColor = RGB(0x2E, 0x2E, 0x2E);
                }
            } else { // Tombstone - amber tint
                if (pressed) {
                    bgColor = RGB(0x2A, 0x2A, 0x2A);
                } else if (hover) {
                    bgColor = RGB(0x3E, 0x38, 0x34); // amber tint
                } else {
                    bgColor = RGB(0x2E, 0x2E, 0x2E);
                }
            }
        }
        
        // Draw hover glow halo effect (matching Legacy: 0x332E89FF)
        if (hover && UISettings::EnableButtonHoverEffects) {
            int glowPad = 2;
            COLORREF glowColor = sciFiTheme ? ToColorRef(BlendThemeColor(theme.accent, theme.mutedAccent, 28)) : RGB(0x2E, 0x89, 0xFF);
            int cornerRadius = GetWindowChromeCornerRadius(theme) / 2;
            DrawRoundedRect(dc, x - glowPad, y - glowPad, size + glowPad * 2, size + glowPad * 2, glowColor, cornerRadius);
        }
        
        // Draw button background with rounded corners (matching Legacy)
        if (UISettings::EnableButtonBackgrounds) {
            int cornerRadius = GetWindowChromeCornerRadius(theme) / 2;
            DrawRoundedRect(dc, x, y, size, size, bgColor, cornerRadius);
        }
        
        // Draw button border (matching Legacy: 0xFF505050)
        if (UISettings::EnableButtonBorders) {
            COLORREF borderColor = sciFiTheme
                ? ToColorRef(pressed ? BlendThemeColor(theme.windowBorder, theme.accent, 36)
                                     : (hover ? BlendThemeColor(theme.windowBorder, theme.mutedAccent, 30)
                                              : BlendThemeColor(theme.windowBorder, theme.titleBarBackground, 22)))
                : RGB(0x50, 0x50, 0x50);
            HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            
            int cornerRadius = GetWindowChromeCornerRadius(theme) / 2;
            if (cornerRadius > 0) {
                RoundRect(dc, x, y, x + size, y + size, cornerRadius * 2, cornerRadius * 2);
            } else {
                Rectangle(dc, x, y, x + size, y + size);
            }
            
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
        }
        
        // Draw button icon (matching Legacy icons)
        if (UISettings::EnableButtonIcons) {
            int cx = x + size / 2;
            int cy = y + size / 2;
            int iconR = size / 3;  // icon radius
            int lineWidth = 2;
            
            if (buttonType == 0) { 
                // Close - X icon (matching Legacy: two diagonal lines)
                HPEN iconPen = CreatePen(PS_SOLID, 1, fgColor);
                HGDIOBJ oldPen = SelectObject(dc, iconPen);
                // Draw thicker X by drawing two lines for each stroke
                MoveToEx(dc, cx - iconR, cy - iconR, nullptr);
                LineTo(dc, cx + iconR, cy + iconR);
                MoveToEx(dc, cx - iconR + 1, cy - iconR, nullptr);
                LineTo(dc, cx + iconR + 1, cy + iconR);
                MoveToEx(dc, cx + iconR, cy - iconR, nullptr);
                LineTo(dc, cx - iconR, cy + iconR);
                MoveToEx(dc, cx + iconR + 1, cy - iconR, nullptr);
                LineTo(dc, cx - iconR + 1, cy + iconR);
                SelectObject(dc, oldPen);
                DeleteObject(iconPen);
            } else if (buttonType == 1) { 
                // Maximize - square outline (matching Legacy: DrawRectangle)
                HPEN iconPen = CreatePen(PS_SOLID, lineWidth, fgColor);
                HGDIOBJ oldPen = SelectObject(dc, iconPen);
                HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                int s = size / 2;
                int rx = cx - s / 2;
                int ry = cy - s / 2;
                Rectangle(dc, rx, ry, rx + s, ry + s);
                SelectObject(dc, oldBrush);
                SelectObject(dc, oldPen);
                DeleteObject(iconPen);
            } else if (buttonType == 2) { 
                // Minimize - horizontal bar (matching Legacy: FillRectangle at bottom)
                HBRUSH iconBrush = CreateSolidBrush(fgColor);
                int barW = size / 2;
                int barH = lineWidth;
                int hx = cx - barW / 2;
                int hy = y + size - 8;
                RECT barRect{ hx, hy, hx + barW, hy + barH };
                FillRect(dc, &barRect, iconBrush);
                DeleteObject(iconBrush);
            } else { 
                // Tombstone - rounded rect with base (matching Legacy tombstone icon)
                HPEN iconPen = CreatePen(PS_SOLID, 1, fgColor);
                HGDIOBJ oldPen = SelectObject(dc, iconPen);
                HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                int w = size / 2;
                int rx = cx - w / 2;
                int ry = cy - w / 2;
                // Draw tombstone shape (rounded rect + base)
                RoundRect(dc, rx, ry, rx + w, ry + w + 3, 6, 6);
                SelectObject(dc, oldBrush);
                SelectObject(dc, oldPen);
                DeleteObject(iconPen);
                // Draw base line
                HBRUSH baseBrush = CreateSolidBrush(fgColor);
                RECT baseRect{ rx - 2, ry + w + 4, rx + w + 2, ry + w + 6 };
                FillRect(dc, &baseRect, baseBrush);
                DeleteObject(baseBrush);
            }
        }
    }
    
    /// Draw window border
    static void DrawWindowBorder(HDC dc, int x, int y, int w, int h, bool focused) {
        if (!UISettings::EnableWindowBorders) return;

        const DesktopTheme& theme = GetCurrentDesktopTheme();
        const bool sciFiTheme = theme.id == DesktopThemeId::SciFi;
        uint32_t borderColor = focused ? theme.accent : theme.windowBorder;
        if (sciFiTheme) {
            borderColor = focused
                ? BlendThemeColor(theme.windowBorder, theme.accent, 72)
                : BlendThemeColor(theme.windowBorder, theme.mutedAccent, 20);
        }
        int borderThickness = theme.windowBorderThickness > 0 ? theme.windowBorderThickness : 1;
        HPEN pen = CreatePen(PS_SOLID, borderThickness, ToColorRef(borderColor));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        
        int cornerRadius = GetWindowChromeCornerRadius(theme);
        if (cornerRadius > 0) {
            RoundRect(dc, x, y, x + w, y + h, cornerRadius * 2, cornerRadius * 2);
        } else {
            Rectangle(dc, x, y, x + w, y + h);
        }
        
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);

        if (sciFiTheme && focused && w > 4 && h > 4) {
            const uint32_t innerColor = BlendThemeColor(theme.windowBackground, theme.accent, 18);
            HPEN innerPen = CreatePen(PS_SOLID, 1, ToColorRef(innerColor));
            HGDIOBJ innerOldPen = SelectObject(dc, innerPen);
            HGDIOBJ innerOldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

            if (cornerRadius > 0) {
                RoundRect(dc, x + 1, y + 1, x + w - 1, y + h - 1, cornerRadius * 2, cornerRadius * 2);
            } else {
                Rectangle(dc, x + 1, y + 1, x + w - 1, y + h - 1);
            }

            SelectObject(dc, innerOldPen);
            SelectObject(dc, innerOldBrush);
            DeleteObject(innerPen);
        }
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

#endif // defined(_WIN32) && !defined(GXOS_BARE_METAL)
