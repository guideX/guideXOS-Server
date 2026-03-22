#pragma once
#pragma once
// desktop_wallpaper.h - Procedural desktop wallpaper / gradient background
// Draws a nice gradient background when no wallpaper BMP is loaded.
// Provides the blue-to-dark gradient reminiscent of classic OS desktops.

#include <cstdint>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gxos { namespace gui {

    class DesktopWallpaper {
    public:
#ifdef _WIN32
        // Draw a vertical gradient background across the given rect
        // Top color: deep blue, Bottom color: dark navy/charcoal
        static void DrawGradient(HDC dc, RECT cr) {
            int w = cr.right - cr.left;
            int h = cr.bottom - cr.top;
            if (w <= 0 || h <= 0) return;

            // Gradient: top (20, 40, 80) -> bottom (15, 18, 28)
            for (int y = 0; y < h; ++y) {
                float t = (float)y / (float)(h > 1 ? h - 1 : 1);
                int r = (int)(20 + (15 - 20) * t);
                int g = (int)(40 + (18 - 40) * t);
                int b = (int)(80 + (28 - 80) * t);
                // Clamp
                r = r < 0 ? 0 : (r > 255 ? 255 : r);
                g = g < 0 ? 0 : (g > 255 ? 255 : g);
                b = b < 0 ? 0 : (b > 255 ? 255 : b);
                HBRUSH br = CreateSolidBrush(RGB(r, g, b));
                RECT line = { cr.left, cr.top + y, cr.right, cr.top + y + 1 };
                FillRect(dc, &line, br);
                DeleteObject(br);
            }

            // Subtle grid pattern overlay (like a desktop grid)
            HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(25, 35, 55));
            HGDIOBJ oldPen = SelectObject(dc, gridPen);
            // Vertical lines every 100px (very subtle)
            for (int x = cr.left + 100; x < cr.right; x += 100) {
                MoveToEx(dc, x, cr.top, nullptr);
                LineTo(dc, x, cr.bottom);
            }
            // Horizontal lines every 100px
            for (int y = cr.top + 100; y < cr.bottom; y += 100) {
                MoveToEx(dc, cr.left, y, nullptr);
                LineTo(dc, cr.right, y);
            }
            SelectObject(dc, oldPen);
            DeleteObject(gridPen);
        }

        // Draw a guideXOS watermark/branding in the center of the desktop
        static void DrawBranding(HDC dc, RECT cr) {
            const char* brand = "guideXOS";
            HFONT bigFont = CreateFontA(
                48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            if (!bigFont) bigFont = (HFONT)GetStockObject(ANSI_VAR_FONT);
            HGDIOBJ oldFont = SelectObject(dc, bigFont);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(35, 50, 75)); // very subtle
            SIZE sz;
            GetTextExtentPoint32A(dc, brand, (int)strlen(brand), &sz);
            int cx = (cr.right + cr.left - sz.cx) / 2;
            int cy = (cr.bottom + cr.top - sz.cy) / 2 - 60; // slightly above center
            TextOutA(dc, cx, cy, brand, (int)strlen(brand));

            // Version subtitle
            const char* ver = "Server Edition";
            HFONT subFont = CreateFontA(
                18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            if (!subFont) subFont = (HFONT)GetStockObject(ANSI_VAR_FONT);
            SelectObject(dc, subFont);
            SetTextColor(dc, RGB(40, 55, 80));
            SIZE subSz;
            GetTextExtentPoint32A(dc, ver, (int)strlen(ver), &subSz);
            int sx = (cr.right + cr.left - subSz.cx) / 2;
            TextOutA(dc, sx, cy + sz.cy + 4, ver, (int)strlen(ver));

            SelectObject(dc, oldFont);
            if (bigFont != (HFONT)GetStockObject(ANSI_VAR_FONT)) DeleteObject(bigFont);
            if (subFont != (HFONT)GetStockObject(ANSI_VAR_FONT)) DeleteObject(subFont);
        }
#endif
    };

}} // namespace gxos::gui
