#pragma once
#pragma once
// system_tray.h - System tray area for the taskbar
// Ported from guideXOS.Legacy Taskbar.cs network indicator and tray area.
// Draws network activity icon, volume icon placeholder, and battery indicator.

#include <cstdint>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gxos { namespace gui {

    class SystemTray {
    public:
        static constexpr int kTrayIconSize = 16;
        static constexpr int kTrayIconGap  = 6;
        static constexpr int kTrayPadding  = 4;

        // Total width needed for system tray icons (network + volume + separator)
        static int Width() { return (kTrayIconSize + kTrayIconGap) * 3 + kTrayPadding * 2; }

        // Call once per frame to advance animations
        static void Update() {
            uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (now - s_lastAnimTick >= 500) {
                s_netAnimPhase = (s_netAnimPhase + 1) % 4;
                s_lastAnimTick = now;
            }
        }

        // Trigger network activity animation
        static void NotifyNetworkActivity() {
            s_netActive = true;
            s_netActivityEnd = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() + 3000;
        }

#ifdef _WIN32
        // Draw the system tray area into the taskbar
        // x,y = top-left of the tray area; barH = taskbar height
        static void Draw(HDC dc, int x, int y, int barH) {
            Update();

            uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (s_netActive && now >= s_netActivityEnd) s_netActive = false;

            int iconY = y + (barH - kTrayIconSize) / 2;
            int cx = x + kTrayPadding;

            // Separator line
            HPEN sep = CreatePen(PS_SOLID, 1, RGB(80, 80, 90));
            HGDIOBJ oldPen = SelectObject(dc, sep);
            MoveToEx(dc, cx - 2, y + 4, nullptr);
            LineTo(dc, cx - 2, y + barH - 4);
            SelectObject(dc, oldPen);
            DeleteObject(sep);

            // Network icon - signal bars
            drawNetworkIcon(dc, cx, iconY);
            cx += kTrayIconSize + kTrayIconGap;

            // Volume icon - speaker shape
            drawVolumeIcon(dc, cx, iconY);
            cx += kTrayIconSize + kTrayIconGap;

            // Battery/power icon
            drawBatteryIcon(dc, cx, iconY);
        }

    private:
        static void drawNetworkIcon(HDC dc, int x, int y) {
            // Draw 4 signal bars, fill based on animation phase when active
            int barCount = s_netActive ? s_netAnimPhase + 1 : 4;
            for (int i = 0; i < 4; ++i) {
                int barH = 4 + i * 3;
                int barW = 3;
                int bx = x + i * 4;
                int by = y + kTrayIconSize - barH;
                COLORREF color = (i < barCount) ? RGB(100, 200, 100) : RGB(60, 60, 70);
                HBRUSH br = CreateSolidBrush(color);
                RECT r = { bx, by, bx + barW, by + barH };
                FillRect(dc, &r, br);
                DeleteObject(br);
            }
        }

        static void drawVolumeIcon(HDC dc, int x, int y) {
            // Simple speaker icon: small rectangle + triangle
            HBRUSH spk = CreateSolidBrush(RGB(180, 180, 190));
            // Speaker body
            RECT body = { x + 2, y + 5, x + 6, y + 11 };
            FillRect(dc, &body, spk);
            // Speaker cone (triangle approximated with expanding rects)
            RECT c1 = { x + 6, y + 4, x + 8, y + 12 };
            FillRect(dc, &c1, spk);
            RECT c2 = { x + 8, y + 3, x + 10, y + 13 };
            FillRect(dc, &c2, spk);
            DeleteObject(spk);
            // Sound waves (small arcs approximated with pixels)
            HPEN wave = CreatePen(PS_SOLID, 1, RGB(140, 140, 150));
            HGDIOBJ oldP = SelectObject(dc, wave);
            // First wave arc
            Arc(dc, x + 10, y + 4, x + 14, y + 12, x + 12, y + 4, x + 12, y + 12);
            SelectObject(dc, oldP);
            DeleteObject(wave);
        }

        static void drawBatteryIcon(HDC dc, int x, int y) {
            // Battery outline
            HPEN outline = CreatePen(PS_SOLID, 1, RGB(160, 160, 170));
            HGDIOBJ oldPen = SelectObject(dc, outline);
            HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, x + 1, y + 4, x + 13, y + 12);
            // Battery tip
            RECT tip = { x + 13, y + 6, x + 15, y + 10 };
            HBRUSH tipBr = CreateSolidBrush(RGB(160, 160, 170));
            FillRect(dc, &tip, tipBr);
            DeleteObject(tipBr);
            SelectObject(dc, oldBr);
            SelectObject(dc, oldPen);
            DeleteObject(outline);
            // Fill level (show ~80%)
            RECT fill = { x + 2, y + 5, x + 11, y + 11 };
            HBRUSH fillBr = CreateSolidBrush(RGB(80, 190, 80));
            FillRect(dc, &fill, fillBr);
            DeleteObject(fillBr);
        }
#endif

        static int s_netAnimPhase;
        static uint64_t s_lastAnimTick;
        static bool s_netActive;
        static uint64_t s_netActivityEnd;
    };

}} // namespace gxos::gui
