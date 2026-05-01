#include "compositor.h"
#include "compositor.h"
#include "allocator.h"
#include "desktop_state.h"
#include "desktop_service.h"
#include "shutdown_dialog.h"
#include "icons.h"
#include "right_click_menu.h"
#include "notification_manager.h"
#include "system_tray.h"
#include "desktop_wallpaper.h"
#include "bitmap_font.h"
#include "window_renderer.h"
#include "special_effects.h"
#include "window_animator.h"
#include "focus_indicator.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace gxos {
    namespace gui {
        using namespace gxos;
        static const char* kGuiChanIn = "gui.input";
        static const char* kGuiChanOut = "gui.output";

        // Static member definitions
        std::atomic<uint64_t> Compositor::s_nextWinId{ 1000 };
        std::unordered_map<uint64_t, WinInfo> Compositor::g_windows; std::vector<uint64_t> Compositor::g_z; std::mutex Compositor::g_lock; uint64_t Compositor::g_focus = 0;
        bool Compositor::g_dragActive = false; int Compositor::g_dragOffX = 0; int Compositor::g_dragOffY = 0; uint64_t Compositor::g_dragWin = 0; int Compositor::g_dragStartX = 0; int Compositor::g_dragStartY = 0;
        bool Compositor::g_dragPending = false; uint64_t Compositor::g_dragPendingWin = 0;
        bool Compositor::g_resizeActive = false; int Compositor::g_resizeStartW = 0; int Compositor::g_resizeStartH = 0; int Compositor::g_resizeStartMX = 0; int Compositor::g_resizeStartMY = 0; uint64_t Compositor::g_resizeWin = 0;
        bool Compositor::g_resizePreviewActive = false; int Compositor::g_resizePreviewW = 0; int Compositor::g_resizePreviewH = 0;
        bool Compositor::g_snapPreviewActive = false;
#ifdef _WIN32
        RECT Compositor::g_snapPreviewRect{ 0,0,0,0 };
        HWND Compositor::g_hwnd = nullptr;
        HBITMAP Compositor::g_startBtnBmp = nullptr;
        HBITMAP Compositor::g_wallpaperBmp = nullptr;
        int Compositor::g_wallpaperW = 0;
        int Compositor::g_wallpaperH = 0;
        std::string Compositor::g_wallpaperPath = "";
        bool Compositor::g_startMenuVisible = false;
        RECT Compositor::g_startMenuRect{ 0,0,0,0 };
#else
        Compositor::SnapRect Compositor::g_snapPreviewRect{ 0,0,0,0 };
        bool Compositor::g_needsRedraw = true;
#endif
        bool Compositor::g_showDesktopActive = false; std::vector<uint64_t> Compositor::g_showDesktopMinimized; uint64_t Compositor::g_lastClickTicks = 0; uint64_t Compositor::g_lastClickWin = 0;
        bool Compositor::g_altTabOverlayActive = false; uint64_t Compositor::g_altTabOverlayTicks = 0; int Compositor::g_altTabCycleIndex = 0;
        bool Compositor::g_taskbarCycleActive = false; int Compositor::g_taskbarCycleIndex = 0; bool Compositor::g_keyboardMoveActive = false; bool Compositor::g_keyboardSizeActive = false; int Compositor::g_kbOrigX = 0; int Compositor::g_kbOrigY = 0; int Compositor::g_kbOrigW = 0; int Compositor::g_kbOrigH = 0;

        DesktopConfigData Compositor::g_cfg{}; std::vector<DesktopItem> Compositor::g_items; uint64_t Compositor::g_lastItemClickTicks = 0; int Compositor::g_lastItemIndex = -1;
        bool Compositor::g_iconDragActive = false; int Compositor::g_iconDragIndex = -1; int Compositor::g_iconDragOffX = 0; int Compositor::g_iconDragOffY = 0; int Compositor::g_iconDragStartX = 0; int Compositor::g_iconDragStartY = 0; bool Compositor::g_iconDragPending = false;

        // Start menu keyboard/selection state
        int Compositor::g_startMenuSel = 0; int Compositor::g_startMenuScroll = 0;
        bool Compositor::g_startMenuAllProgs = false; // "All Programs" view toggle
        std::vector<std::string> Compositor::g_startMenuAllProgsSorted; // Alphabetically sorted app list
        bool Compositor::g_taskbarMenuVisible = false;
#ifdef _WIN32
        RECT Compositor::g_taskbarMenuRect{ 0,0,0,0 };
#else
        Compositor::SnapRect Compositor::g_taskbarMenuRect{ 0,0,0,0 };
#endif
        int Compositor::g_taskbarMenuSel = 0;

        // Video backend (GDI on Windows, kernel FB on bare-metal)
        VideoBackend* Compositor::g_videoBackend = nullptr;

        void Compositor::initVideoBackend( ) {
#ifdef _WIN32
            // On Windows host, use GDI backend (current rendering path).
            // The GDI backend is created but the compositor still paints
            // through GDI calls directly.  This is the first step towards
            // migrating to a pixel-buffer renderer.
            static GdiVideoBackend s_gdiBackend;
            if (s_gdiBackend.init(1024, 768)) {
                g_videoBackend = &s_gdiBackend;
                Logger::write(LogLevel::Info, "VideoBackend: GDI backend active");
            }
#else
            // On bare-metal, use kernel framebuffer backend
            static KernelFbVideoBackend s_kfbBackend;
            if (s_kfbBackend.init(1024, 768)) {
                g_videoBackend = &s_kfbBackend;
            }
#endif
        }

        void Compositor::feedVncFromBackend( ) {
            if (!vnc::VncServer::IsRunning( )) return;
            if (!g_videoBackend) return;
            uint32_t* pixels = g_videoBackend->getPixels( );
            if (!pixels) return;
            int w = g_videoBackend->getWidth( );
            int h = g_videoBackend->getHeight( );
            int stride = g_videoBackend->getPitch( );
            vnc::VncServer::UpdateFramebuffer(
                reinterpret_cast<const uint8_t*>(pixels), w, h, stride);
        }

        static uint64_t nowMs( ) { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now( ).time_since_epoch( )).count( ); }
        static void publishOut(MsgType type, const std::string& payload, uint64_t dstPid = 0) { 
            if (type == MsgType::MT_Create) {
                Logger::write(LogLevel::Info, std::string("publishOut MT_Create payload=") + payload + " dstPid=" + std::to_string(dstPid));
            }
            ipc::Message out; out.type = (uint32_t)type; out.dstPid = dstPid; out.data.assign(payload.begin( ), payload.end( )); ipc::Bus::publish(kGuiChanOut, std::move(out), false); 
        }

        void Compositor::refreshDesktopItems( ) {
            g_items.clear( ); // pinned first
            for (const auto& p : g_cfg.pinned) { DesktopItem di; di.label = p; di.action = p; di.pinned = true; di.ix = -1; di.iy = -1; g_items.push_back(di); } for (const auto& r : g_cfg.recent) { if (std::find(g_cfg.pinned.begin( ), g_cfg.pinned.end( ), r) != g_cfg.pinned.end( )) continue; DesktopItem di; di.label = r; di.action = r; di.pinned = false; di.ix = -1; di.iy = -1; g_items.push_back(di); }
            // Apply saved positions from config
            for (auto& item : g_items) { for (const auto& ip : g_cfg.iconPositions) { if (ip.name == item.label) { item.ix = ip.x; item.iy = ip.y; break; } } }
            // Assign default grid positions to any items that don't have saved positions
            const int margin = 20; const int iconW = 56; const int iconH = 56; const int cellW = iconW + 28; const int cellH = iconH + 38;
            int defIdx = 0; for (auto& item : g_items) { if (item.ix < 0 || item.iy < 0) { item.ix = margin + (defIdx % 8) * cellW; item.iy = margin + (defIdx / 8) * cellH; } defIdx++; }
        }

        void Compositor::refreshAllProgramsList( ) {
            // Load all registered apps from DesktopService and sort alphabetically
            g_startMenuAllProgsSorted.clear( );
            // For now, use a default list matching C# implementation
            g_startMenuAllProgsSorted.push_back("Calculator");
            g_startMenuAllProgsSorted.push_back("Clock");
            g_startMenuAllProgsSorted.push_back("Console");
            g_startMenuAllProgsSorted.push_back("ControlPanel");
            g_startMenuAllProgsSorted.push_back("DiskManager");
            g_startMenuAllProgsSorted.push_back("Notepad");
            g_startMenuAllProgsSorted.push_back("Paint");
            g_startMenuAllProgsSorted.push_back("TaskManager");
            // Sort alphabetically (case-insensitive)
            std::sort(g_startMenuAllProgsSorted.begin( ), g_startMenuAllProgsSorted.end( ),
                [] (const std::string& a, const std::string& b) {
                    std::string al = a, bl = b;
                    std::transform(al.begin( ), al.end( ), al.begin( ), ::tolower);
                    std::transform(bl.begin( ), bl.end( ), bl.begin( ), ::tolower);
                    return al < bl;
                });
        }

        void Compositor::saveDesktopConfig( ) { std::string err; DesktopConfig::Save("desktop.json", g_cfg, err); }
        void Compositor::addRecent(const std::string& act) { auto it = std::find(g_cfg.recent.begin( ), g_cfg.recent.end( ), act); if (it != g_cfg.recent.end( )) g_cfg.recent.erase(it); g_cfg.recent.insert(g_cfg.recent.begin( ), act); if (g_cfg.recent.size( ) > 20) g_cfg.recent.pop_back( ); refreshDesktopItems( ); saveDesktopConfig( ); }
        void Compositor::pinAction(const std::string& act) { if (act.empty( )) return; if (std::find(g_cfg.pinned.begin( ), g_cfg.pinned.end( ), act) == g_cfg.pinned.end( )) { g_cfg.pinned.push_back(act); refreshDesktopItems( ); saveDesktopConfig( ); } }
        void Compositor::unpinAction(const std::string& act) { auto it = std::find(g_cfg.pinned.begin( ), g_cfg.pinned.end( ), act); if (it != g_cfg.pinned.end( )) { g_cfg.pinned.erase(it); refreshDesktopItems( ); saveDesktopConfig( ); } }
        void Compositor::launchAction(const std::string& act) {
            Logger::write(LogLevel::Info, std::string("Desktop launch: ") + act);
            addRecent(act);
            // Actually launch the application
            std::string err;
            if (!DesktopService::LaunchApp(act, err)) {
                Logger::write(LogLevel::Error, std::string("Failed to launch app: ") + act + " - " + err);
            }
        }

        WinInfo* Compositor::hitWindowAt(int mx, int my) { for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { uint64_t wid = g_z[idx]; auto it = g_windows.find(wid); if (it == g_windows.end( )) continue; WinInfo& w = it->second; if (w.minimized || w.tombstoned) continue; if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h) return &w; } return nullptr; }
#ifdef _WIN32
        uint64_t Compositor::hitTestTaskbarButton(int mx, int my, RECT cr, int taskbarH) {
            int taskbarTop = cr.bottom - taskbarH; if (my < taskbarTop) return 0; int btnX = 216; // leave space for start button + search box
            for (uint64_t id : g_z) { auto it = g_windows.find(id); if (it == g_windows.end( )) continue; std::string label = it->second.title; SIZE sz; HDC dc = GetDC(g_hwnd); GetTextExtentPoint32A(dc, label.c_str( ), (int)label.size( ), &sz); ReleaseDC(g_hwnd, dc); int bw = sz.cx + 30; int btnTop = taskbarTop + 4; int btnBottom = cr.bottom - 4; if (mx >= btnX && mx <= btnX + bw && my >= btnTop && my <= btnBottom) return id; btnX += bw + 6; } return 0;
        }
        void Compositor::initWindow( ) { WNDCLASSA wc{}; wc.style = CS_OWNDC; wc.lpfnWndProc = Compositor::WndProc; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "GXOS_COMPOSITOR"; RegisterClassA(&wc); g_hwnd = CreateWindowExA(0, wc.lpszClassName, "guideXOSCpp Compositor", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768, nullptr, nullptr, wc.hInstance, nullptr); g_startBtnBmp = (HBITMAP)LoadImageA(nullptr, "assets/start_button.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION); }
        void Compositor::shutdownWindow( ) { if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; } }
        void Compositor::requestRepaint( ) { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }
        void Compositor::loadWallpaper(const std::string& path) { g_wallpaperPath = path; if (g_wallpaperBmp) { DeleteObject(g_wallpaperBmp); g_wallpaperBmp = nullptr; } if (path.empty( )) return; HBITMAP hb = (HBITMAP)LoadImageA(nullptr, path.c_str( ), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION); if (hb) { BITMAP bm{}; GetObject(hb, sizeof(bm), &bm); g_wallpaperBmp = hb; g_wallpaperW = bm.bmWidth; g_wallpaperH = bm.bmHeight; } }
        void Compositor::freeWallpaper( ) { if (g_wallpaperBmp) { DeleteObject(g_wallpaperBmp); g_wallpaperBmp = nullptr; g_wallpaperW = g_wallpaperH = 0; } }

        void Compositor::drawDesktopIcons(HDC dc, RECT cr) {
            const int iconW = 56; const int iconH = 56; const int cellW = iconW + 28; const int cellH = iconH + 38; HFONT font = (HFONT)GetStockObject(ANSI_VAR_FONT); SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); POINT cursor; GetCursorPos(&cursor); ScreenToClient(g_hwnd, &cursor); int idx = 0; for (auto& it : g_items) {
                int x = it.ix; int y = it.iy; RECT cell{ x, y, x + cellW, y + cellH }; bool hover = (cursor.x >= cell.left && cursor.x <= cell.right && cursor.y >= cell.top && cursor.y <= cell.bottom);
                if (it.selected) {
                    HBRUSH sel = CreateSolidBrush(RGB(50, 90, 160)); FillRect(dc, &cell, sel); DeleteObject(sel); 
                    // Draw focus indicator for selected icon
                    FocusIndicator::DrawFocusRect(dc, cell.left, cell.top, cellW, cellH, 4, 2, 3);
                } else if (hover) { HBRUSH hov = CreateSolidBrush(RGB(50, 55, 65)); FillRect(dc, &cell, hov); DeleteObject(hov); HPEN hovP = CreatePen(PS_SOLID, 1, RGB(80, 100, 140)); HGDIOBJ oP = SelectObject(dc, hovP); HGDIOBJ oB = SelectObject(dc, GetStockObject(NULL_BRUSH)); Rectangle(dc, cell.left, cell.top, cell.right, cell.bottom); SelectObject(dc, oP); SelectObject(dc, oB); DeleteObject(hovP); }
                RECT iconR{ x + (cellW - iconW) / 2, y + 6, x + (cellW - iconW) / 2 + iconW, y + 6 + iconH };
                // Color-coded icons based on app name
                COLORREF iconColor = RGB(90, 100, 120); // default
                std::string lbl = it.label;
                if (lbl == "Calculator" || lbl == "Clock") iconColor = RGB(70, 140, 200);
                else if (lbl == "Notepad" || lbl == "Console") iconColor = RGB(120, 180, 80);
                else if (lbl == "Paint" || lbl == "ImageViewer") iconColor = RGB(200, 120, 60);
                else if (lbl == "TaskManager") iconColor = RGB(180, 70, 70);
                else if (lbl == "DiskManager" || lbl == "ControlPanel") iconColor = RGB(140, 90, 180);
                else if (lbl == "Files" || lbl == "ComputerFiles") iconColor = RGB(200, 180, 60);
                else if (it.pinned) iconColor = RGB(90, 140, 220);
                HBRUSH ib = CreateSolidBrush(iconColor); FillRect(dc, &iconR, ib); DeleteObject(ib);
                // Icon inner detail: small symbol
                {
                    int cx = iconR.left + (iconW / 2); int cy = iconR.top + (iconH / 2);
                    HBRUSH inner = CreateSolidBrush(RGB(GetRValue(iconColor) + 40 > 255 ? 255 : GetRValue(iconColor) + 40, GetGValue(iconColor) + 40 > 255 ? 255 : GetGValue(iconColor) + 40, GetBValue(iconColor) + 40 > 255 ? 255 : GetBValue(iconColor) + 40));
                    RECT innerR{ cx - 10, cy - 10, cx + 10, cy + 10 }; FillRect(dc, &innerR, inner); DeleteObject(inner);
                }
                // Rounded-ish frame (just a subtle border)
                HPEN iconFrame = CreatePen(PS_SOLID, 1, RGB(180, 180, 200)); HGDIOBJ oP2 = SelectObject(dc, iconFrame); HGDIOBJ oB2 = SelectObject(dc, GetStockObject(NULL_BRUSH)); Rectangle(dc, iconR.left, iconR.top, iconR.right, iconR.bottom); SelectObject(dc, oP2); SelectObject(dc, oB2); DeleteObject(iconFrame);
                // Label with text shadow
                SetTextColor(dc, RGB(0, 0, 0)); TextOutA(dc, x + 5, iconR.bottom + 5, lbl.c_str( ), (int)lbl.size( ));
                SetTextColor(dc, RGB(230, 230, 240)); TextOutA(dc, x + 4, iconR.bottom + 4, lbl.c_str( ), (int)lbl.size( ));
                // Pin indicator
                if (it.pinned) { SetTextColor(dc, RGB(255, 200, 60)); const char* pin = "*"; TextOutA(dc, iconR.right - 10, iconR.top + 2, pin, 1); }
                idx++;
            }
        }

        // helper: draw a bitmap centered in rect
        static void drawBitmapCentered(HDC dc, HBITMAP hb, RECT r) { if (!hb) return; HDC mem = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(mem, hb); BITMAP bm{}; GetObject(hb, sizeof(bm), &bm); int w = bm.bmWidth, h = bm.bmHeight; int dx = r.left + ((r.right - r.left) - w) / 2; int dy = r.top + ((r.bottom - r.top) - h) / 2; BitBlt(dc, dx, dy, w, h, mem, 0, 0, SRCCOPY); SelectObject(mem, old); DeleteDC(mem); }

        LRESULT CALLBACK Compositor::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
            switch (msg) {
            case WM_CLOSE: PostQuitMessage(0); return 0;
            case WM_SIZE: { RECT cr; GetClientRect(h, &cr); std::lock_guard<std::mutex> lk(g_lock); int taskbarH = 40; for (auto& kv : g_windows) { WinInfo& wi = kv.second; if (wi.maximized) { wi.x = cr.left; wi.y = cr.top; wi.w = cr.right - cr.left; wi.h = cr.bottom - taskbarH; wi.dirty = true; } } requestRepaint( ); return 0; }
            case WM_PAINT: {
                PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT cr; GetClientRect(h, &cr); if (g_wallpaperBmp) { HDC mem = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(mem, g_wallpaperBmp); BITMAP bm{}; GetObject(g_wallpaperBmp, sizeof(bm), &bm); double sx = (double)(cr.right - cr.left) / bm.bmWidth; double sy = (double)(cr.bottom - cr.top) / bm.bmHeight; double s = sx < sy ? sx : sy; int dstW = (int)(bm.bmWidth * s); int dstH = (int)(bm.bmHeight * s); int dx = (cr.right - dstW) / 2; int dy = (cr.bottom - dstH) / 2; HBRUSH bg = CreateSolidBrush(RGB(25, 25, 30)); FillRect(dc, &cr, bg); DeleteObject(bg); SetStretchBltMode(dc, HALFTONE); StretchBlt(dc, dx, dy, dstW, dstH, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY); SelectObject(mem, old); DeleteDC(mem); } else { DesktopWallpaper::DrawGradient(dc, cr); DesktopWallpaper::DrawBranding(dc, cr); } drawDesktopIcons(dc, cr);
                // Draw application windows in Z-order (bottom to top)
                const int titleBarH = UISettings::DefaultBarHeight; 
                HFONT font = (HFONT)GetStockObject(ANSI_VAR_FONT); 
                SelectObject(dc, font); 
                SetBkMode(dc, TRANSPARENT);
                
                for (size_t i = 0; i < g_z.size( ); ++i) {
                    auto it = g_windows.find(g_z[i]); 
                    if (it == g_windows.end( )) continue; 
                    const WinInfo& winfo = it->second; 
                    if (winfo.minimized || !winfo.visible) continue;
                    
                    bool isFocused = (winfo.id == g_focus);
                    RECT wrect{ winfo.x, winfo.y, winfo.x + winfo.w, winfo.y + winfo.h };
                    
                    // Draw window glow/shadow (matching Legacy)
                    WindowRenderer::DrawWindowGlow(dc, winfo.x, winfo.y, winfo.w, winfo.h, titleBarH, isFocused);
                    
                    // Draw window content background
                    WindowRenderer::DrawRoundedRect(dc, winfo.x, winfo.y, winfo.w, winfo.h, 
                        RGB((UISettings::WindowContentColor >> 16) & 0xFF,
                            (UISettings::WindowContentColor >> 8) & 0xFF,
                            UISettings::WindowContentColor & 0xFF),
                        UISettings::EnableRoundedCorners ? UISettings::WindowCornerRadius : 0);
                    
                    // Draw title bar
                    WindowRenderer::DrawTitleBar(dc, winfo.x, winfo.y, winfo.w, titleBarH, isFocused);
                    
                    // Draw window border
                    WindowRenderer::DrawWindowBorder(dc, winfo.x, winfo.y, winfo.w, winfo.h, isFocused);
                    
                    // Draw window title text
                    if (UISettings::EnableWindowTitles) {
                        SetTextColor(dc, RGB(240, 240, 240)); 
                        TextOutA(dc, winfo.x + 10, winfo.y + (titleBarH - 16) / 2, winfo.title.c_str( ), (int)winfo.title.size( ));
                    }

                    // Titlebar buttons (matching Legacy: minimize, maximize, tombstone, close from left to right)
                    // Button layout right-to-left: close, tombstone, maximize, minimize
                    const int btnSize = titleBarH - UISettings::ButtonSizeOffset;
                    const int btnGap = UISettings::ButtonSpacing;
                    int btnY = winfo.y + (titleBarH - btnSize) / 2;
                    
                    // Position buttons from right to left
                    int closeLeft = winfo.x + winfo.w - btnGap - btnSize;
                    int tombLeft = closeLeft - btnGap - btnSize;
                    int maxLeft = tombLeft - btnGap - btnSize;
                    int minLeft = maxLeft - btnGap - btnSize;
                    
                    // Draw buttons using improved renderer (matching Legacy style)
                    // buttonType: 0=close, 1=maximize, 2=minimize, 3=tombstone
                    WindowRenderer::DrawTitleButton(dc, minLeft, btnY, btnSize, 2, 
                        winfo.titleBtnMinHover, winfo.titleBtnMinPressed, isFocused);
                    WindowRenderer::DrawTitleButton(dc, maxLeft, btnY, btnSize, 1, 
                        winfo.titleBtnMaxHover, winfo.titleBtnMaxPressed, isFocused);
                    WindowRenderer::DrawTitleButton(dc, tombLeft, btnY, btnSize, 3, 
                        winfo.titleBtnTombHover, winfo.titleBtnTombPressed, isFocused);
                    WindowRenderer::DrawTitleButton(dc, closeLeft, btnY, btnSize, 0, 
                        winfo.titleBtnCloseHover, winfo.titleBtnClosePressed, isFocused);

                    // Draw resize grip
                    if (!winfo.maximized) {
                        WindowRenderer::DrawResizeGrip(dc, winfo.x, winfo.y, winfo.w, winfo.h);
                    }

                    // Draw window content (rects, widgets, text)
                    for (const auto& ri : winfo.rects) { 
                        RECT rr{ winfo.x + ri.x, winfo.y + titleBarH + ri.y, winfo.x + ri.x + ri.w, winfo.y + titleBarH + ri.h }; 
                        HBRUSH rb = CreateSolidBrush(RGB(ri.r, ri.g, ri.b)); 
                        FillRect(dc, &rr, rb); 
                        DeleteObject(rb); 
                    }
                    for (const auto& wd : winfo.widgets) { 
                        RECT wr{ winfo.x + wd.x, winfo.y + titleBarH + wd.y, winfo.x + wd.x + wd.w, winfo.y + titleBarH + wd.y + wd.h }; 
                        HBRUSH wb = CreateSolidBrush(wd.pressed ? RGB(40, 80, 140) : (wd.hover ? RGB(70, 90, 120) : RGB(90, 90, 100))); 
                        FillRect(dc, &wr, wb); 
                        DeleteObject(wb); 
                        FrameRect(dc, &wr, (HBRUSH)GetStockObject(WHITE_BRUSH)); 
                        SetTextColor(dc, RGB(240, 240, 240)); 
                        TextOutA(dc, wr.left + 6, wr.top + 4, wd.text.c_str( ), (int)wd.text.size( )); 
                    }
                    int ty = winfo.y + titleBarH + 8; 
                    for (const auto& tx : winfo.texts) { 
                        SetTextColor(dc, RGB(220, 220, 220));
                        TextOutA(dc, winfo.x + 8, ty, tx.c_str( ), (int)tx.size( )); 
                        ty += 16; 
                    }
                    
                    // Draw tombstone overlay
                    if (winfo.tombstoned) { 
                        WindowRenderer::DrawTombstoneOverlay(dc, winfo.x, winfo.y, winfo.w, winfo.h);
                    }
                }
                int taskbarH = 40; RECT tb{ cr.left,cr.bottom - taskbarH,cr.right,cr.bottom };
                // Taskbar background with subtle gradient
                for (int ty2 = 0; ty2 < taskbarH; ++ty2) {
                    float gt = (float)ty2 / (float)(taskbarH > 1 ? taskbarH - 1 : 1);
                    int gr = (int)(30 + gt * 10), gg = (int)(30 + gt * 10), gb = (int)(38 + gt * 14);
                    HBRUSH tbLine = CreateSolidBrush(RGB(gr, gg, gb)); RECT tbLn{ cr.left, cr.bottom - taskbarH + ty2, cr.right, cr.bottom - taskbarH + ty2 + 1 }; FillRect(dc, &tbLn, tbLine); DeleteObject(tbLine);
                }
                // Top edge highlight
                { HPEN tbEdge = CreatePen(PS_SOLID, 1, RGB(60, 65, 80)); HGDIOBJ oldP = SelectObject(dc, tbEdge); MoveToEx(dc, cr.left, cr.bottom - taskbarH, nullptr); LineTo(dc, cr.right, cr.bottom - taskbarH); SelectObject(dc, oldP); DeleteObject(tbEdge); }
                RECT startBtn{ 8,cr.bottom - taskbarH + 6,8 + 32,cr.bottom - 6 }; HBRUSH sbg = CreateSolidBrush(g_startMenuVisible ? RGB(80, 110, 160) : RGB(55, 75, 100)); FillRect(dc, &startBtn, sbg); DeleteObject(sbg); FrameRect(dc, &startBtn, (HBRUSH)GetStockObject(WHITE_BRUSH)); drawBitmapCentered(dc, g_startBtnBmp, startBtn);
                // Search box placeholder (after start button)
                drawTaskbarSearchBox(dc, 48, cr.bottom - taskbarH + 8, 160, taskbarH - 16);
                // Taskbar buttons (offset to right of search box)
                POINT cursor; GetCursorPos(&cursor); ScreenToClient(h, &cursor); int btnX = 216; for (uint64_t id : g_z) {
                    auto it = g_windows.find(id); if (it == g_windows.end( )) continue; std::string label = it->second.title; SIZE sz; GetTextExtentPoint32A(dc, label.c_str( ), (int)label.size( ), &sz); int bw = sz.cx + 30; if (bw > 180) bw = 180; RECT br{ btnX, cr.bottom - taskbarH + 6, btnX + bw, cr.bottom - 6 }; bool hover = (cursor.x >= br.left && cursor.x <= br.right && cursor.y >= br.top && cursor.y <= br.bottom); HBRUSH bbg = CreateSolidBrush(hover ? RGB(90, 130, 190) : (id == g_focus ? RGB(70, 100, 150) : (it->second.minimized ? RGB(40, 40, 50) : (it->second.tombstoned ? RGB(85, 65, 35) : RGB(55, 58, 70))))); FillRect(dc, &br, bbg); DeleteObject(bbg);
                    // Active indicator line at bottom for focused window
                    if (id == g_focus) { HBRUSH ind = CreateSolidBrush(RGB(100, 160, 240)); RECT indR{ br.left + 2,br.bottom - 3,br.right - 2,br.bottom - 1 }; FillRect(dc, &indR, ind); DeleteObject(ind); }
                    RECT iconRect{ br.left + 4, br.top + 4, br.left + 20, br.top + 20 }; drawBitmapCentered(dc, it->second.taskbarIcon, iconRect); SetTextColor(dc, RGB(230, 230, 240)); TextOutA(dc, br.left + 24, br.top + 8, label.c_str( ), (int)label.size( )); btnX += bw + 4;
                }
                // System tray area (before clock)
                drawSystemTray(dc, cr, taskbarH);
                // Taskbar clock/date display (right side, matching Legacy Taskbar.cs)
                {
                    std::time_t now = std::time(nullptr);
                    std::tm ltBuf{};
#ifdef _WIN32
                    localtime_s(&ltBuf, &now);
#else
                    std::tm* tmp = std::localtime(&now);
                    if (tmp) ltBuf = *tmp;
#endif
                    char timeBuf[16]; char dateBuf[16];
                    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ltBuf.tm_hour, ltBuf.tm_min);
                    std::snprintf(dateBuf, sizeof(dateBuf), "%d/%d/%d", ltBuf.tm_mon + 1, ltBuf.tm_mday, ltBuf.tm_year + 1900);
                    SIZE timeSz, dateSz;
                    GetTextExtentPoint32A(dc, timeBuf, (int)strlen(timeBuf), &timeSz);
                    GetTextExtentPoint32A(dc, dateBuf, (int)strlen(dateBuf), &dateSz);
                    int clockW = (timeSz.cx > dateSz.cx ? timeSz.cx : dateSz.cx) + 16;
                    int clockX = cr.right - clockW - 12;
                    int timeY = cr.bottom - taskbarH + 6;
                    int dateY = timeY + timeSz.cy + 1;
                    SetTextColor(dc, RGB(200, 200, 210));
                    TextOutA(dc, clockX + (clockW - timeSz.cx) / 2, timeY, timeBuf, (int)strlen(timeBuf));
                    SetTextColor(dc, RGB(150, 150, 165));
                    TextOutA(dc, clockX + (clockW - dateSz.cx) / 2, dateY, dateBuf, (int)strlen(dateBuf));
                }
                // Show Desktop button (thin sliver on far right, matching Legacy)
                {
                    int sdW = 6; int sdX = cr.right - sdW;
                    RECT sdRect{ sdX, cr.bottom - taskbarH, cr.right, cr.bottom };
                    bool hoverSD = (cursor.x >= sdRect.left && cursor.y >= sdRect.top && cursor.x <= sdRect.right && cursor.y <= sdRect.bottom);
                    HBRUSH sdBrush = CreateSolidBrush(hoverSD ? RGB(70, 80, 100) : RGB(50, 50, 60));
                    FillRect(dc, &sdRect, sdBrush); DeleteObject(sdBrush);
                }
                // Taskbar button tooltip (drawn last so it overlaps everything)
                {
                    int tbtnX = 216;
                    for (uint64_t id : g_z) {
                        auto it = g_windows.find(id); if (it == g_windows.end( )) continue;
                        std::string label = it->second.title; SIZE sz;
                        GetTextExtentPoint32A(dc, label.c_str( ), (int)label.size( ), &sz);
                        int bw = sz.cx + 30; if (bw > 180) bw = 180;
                        RECT br2{ tbtnX, cr.bottom - taskbarH + 6, tbtnX + bw, cr.bottom - 6 };
                        bool hov = (cursor.x >= br2.left && cursor.x <= br2.right && cursor.y >= br2.top && cursor.y <= br2.bottom);
                        if (hov) { drawTaskbarTooltip(dc, (br2.left + br2.right) / 2, cr.bottom - taskbarH, label.c_str( )); break; }
                        tbtnX += bw + 4;
                    }
                }
                // Notification toasts (top-right, matching Legacy NotificationManager.cs)
                {
                    uint64_t nowTicks = nowMs( );
                    NotificationManager::Update(nowTicks);
                    auto notes = NotificationManager::Snapshot( );
                    int noteY = 8;
                    for (size_t ni = 0; ni < notes.size( ); ni++) {
                        const auto& n = notes[ni];
                        if (n.dismissed) continue;
                        SIZE nsz; const char* nmsg = n.message.c_str( );
                        GetTextExtentPoint32A(dc, nmsg, (int)n.message.size( ), &nsz);
                        int noteW = nsz.cx + 24; if (noteW < 160) noteW = 160;
                        int noteH = 32;
                        int noteX = cr.right - noteW - 8;
                        RECT noteR{ noteX, noteY, noteX + noteW, noteY + noteH };
                        HBRUSH nb = CreateSolidBrush(n.level == NotificationLevel::Error ? RGB(120, 40, 40) : RGB(40, 55, 80));
                        FillRect(dc, &noteR, nb); DeleteObject(nb);
                        FrameRect(dc, &noteR, (HBRUSH)GetStockObject(WHITE_BRUSH));
                        SetTextColor(dc, RGB(240, 240, 240));
                        TextOutA(dc, noteX + 12, noteY + 8, nmsg, (int)n.message.size( ));
                        noteY += noteH + 4;
                    }
                }
                // Right-click context menu overlay
                RightClickMenu::Draw(dc);
                // Taskbar right-click menu (Task Manager, Reboot, Log Off)
                if (g_taskbarMenuVisible) {
                    const int tmItemH = 28; const int tmMenuW = 180; const int tmPad = 6;
                    static const char* tmLabels[] = { "Task Manager", "Reboot", "Log Off" };
                    const int tmItemCount = 3;
                    int tmH = tmItemH * tmItemCount + tmPad * 2;
                    g_taskbarMenuRect = { g_taskbarMenuRect.left, g_taskbarMenuRect.top,
                        g_taskbarMenuRect.left + tmMenuW, g_taskbarMenuRect.top + tmH };
                    HBRUSH tmBg = CreateSolidBrush(RGB(42, 42, 42));
                    FillRect(dc, &g_taskbarMenuRect, tmBg); DeleteObject(tmBg);
                    HPEN tmBorder = CreatePen(PS_SOLID, 1, RGB(63, 63, 63));
                    HGDIOBJ oldPen2 = SelectObject(dc, tmBorder);
                    HGDIOBJ oldBr2 = SelectObject(dc, GetStockObject(NULL_BRUSH));
                    Rectangle(dc, g_taskbarMenuRect.left, g_taskbarMenuRect.top, g_taskbarMenuRect.right, g_taskbarMenuRect.bottom);
                    SelectObject(dc, oldPen2); SelectObject(dc, oldBr2); DeleteObject(tmBorder);
                    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(220, 220, 220));
                    for (int tmi = 0; tmi < tmItemCount; ++tmi) {
                        int iy = g_taskbarMenuRect.top + tmPad + tmi * tmItemH;
                        RECT itemR{ g_taskbarMenuRect.left + 1, iy, g_taskbarMenuRect.right - 1, iy + tmItemH };
                        bool hov = (cursor.x >= itemR.left && cursor.x <= itemR.right && cursor.y >= itemR.top && cursor.y <= itemR.bottom);
                        if (tmi == g_taskbarMenuSel || hov) { HBRUSH hb = CreateSolidBrush(RGB(60, 80, 120)); FillRect(dc, &itemR, hb); DeleteObject(hb); }
                        TextOutA(dc, itemR.left + 8, iy + (tmItemH / 2) - 7, tmLabels[tmi], (int)strlen(tmLabels[tmi]));
                    }
                }
                // Start menu popup (pinned + recent OR all programs)
                if (g_startMenuVisible) {
                    int smW = 440; // wider to accommodate two columns
                    int maxRows = 14;
                    int rowH = 20;
                    int leftColW = 260; // left list column width
                    int rightColW = 160; // right column for shortcuts
                    int smH = maxRows * rowH + 10;
                    RECT sm{ startBtn.left, startBtn.top - smH - 6, startBtn.left + smW, startBtn.top - 6 };
                    if (sm.top < 0) { sm.top = 4; sm.bottom = sm.top + smH; }
                    g_startMenuRect = sm;
                    HBRUSH mBg = CreateSolidBrush(RGB(45, 45, 55));
                    FillRect(dc, &sm, mBg);
                    DeleteObject(mBg);
                    FrameRect(dc, &sm, (HBRUSH)GetStockObject(WHITE_BRUSH));

                    // Left column - Recent/All Programs list
                    int y = sm.top + 4;
                    HFONT f = (HFONT)GetStockObject(ANSI_VAR_FONT);
                    SelectObject(dc, f);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, RGB(230, 230, 230));
                    int row = 0;
                    int startIndex = g_startMenuScroll;

                    if (g_startMenuAllProgs) {
                        // Show all programs alphabetically
                        for (size_t i = startIndex; i < g_startMenuAllProgsSorted.size( ) && row < maxRows; ++i) {
                            RECT r{ sm.left + 4, y, sm.left + leftColW - 4, y + rowH };
                            bool isSel = ((int)i == g_startMenuSel);
                            bool isHover = (cursor.x >= r.left && cursor.x <= r.right && cursor.y >= r.top && cursor.y <= r.bottom);
                            HBRUSH rb = CreateSolidBrush(isSel ? RGB(80, 100, 150) : (isHover ? RGB(70, 90, 130) : RGB(55, 55, 70)));
                            FillRect(dc, &r, rb);
                            DeleteObject(rb);
                            
                            // Draw focus indicator if selected (keyboard focus)
                            if (isSel && !isHover) {
                                FocusIndicator::DrawFocusRect(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 3, 2, 2);
                            }
                            
                            std::string txt = g_startMenuAllProgsSorted[i];
                            TextOutA(dc, r.left + 4, r.top + 4, txt.c_str( ), (int)txt.size( ));
                            y += rowH; row++;
                        }
                    } else {
                        // Show pinned + recent
                        for (size_t i = startIndex; i < g_items.size( ) && row < maxRows; ++i) {
                            RECT r{ sm.left + 4, y, sm.left + leftColW - 4, y + rowH };
                            bool isSel = ((int)i == g_startMenuSel);
                            bool isHover = (cursor.x >= r.left && cursor.x <= r.right && cursor.y >= r.top && cursor.y <= r.bottom);
                            HBRUSH rb = CreateSolidBrush(isSel ? RGB(80, 100, 150) : (isHover ? RGB(70, 90, 130) : RGB(55, 55, 70)));
                            FillRect(dc, &r, rb);
                            DeleteObject(rb);
                            
                            // Draw focus indicator if selected (keyboard focus)
                            if (isSel && !isHover) {
                                FocusIndicator::DrawFocusRect(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 3, 2, 2);
                            }
                            
                            std::string txt = (g_items[i].pinned ? "* " : "  ") + g_items[i].label;
                            TextOutA(dc, r.left + 4, r.top + 4, txt.c_str( ), (int)txt.size( ));
                            y += rowH; row++;
                        }
                    }

                    // Right column - shortcuts
                    int rcX = sm.left + leftColW + 4;
                    int rcY = sm.top + 6;
                    SetTextColor(dc, RGB(200, 200, 200));

                    // Computer Files shortcut
                    RECT rcComputer{ rcX, rcY, sm.right - 6, rcY + rowH };
                    bool overComp = (cursor.x >= rcComputer.left && cursor.x <= rcComputer.right && cursor.y >= rcComputer.top && cursor.y <= rcComputer.bottom);
                    if (overComp) { HBRUSH hb = CreateSolidBrush(RGB(70, 90, 130)); FillRect(dc, &rcComputer, hb); DeleteObject(hb); }
                    TextOutA(dc, rcComputer.left + 6, rcComputer.top + 4, "Computer Files", 14);
                    rcY += rowH + 4;

                    // Console shortcut
                    RECT rcConsole{ rcX, rcY, sm.right - 6, rcY + rowH };
                    bool overCon = (cursor.x >= rcConsole.left && cursor.x <= rcConsole.right && cursor.y >= rcConsole.top && cursor.y <= rcConsole.bottom);
                    if (overCon) { HBRUSH hb = CreateSolidBrush(RGB(70, 90, 130)); FillRect(dc, &rcConsole, hb); DeleteObject(hb); }
                    TextOutA(dc, rcConsole.left + 6, rcConsole.top + 4, "Console", 7);
                    rcY += rowH + 4;

                    // Recent Documents shortcut
                    RECT rcDocs{ rcX, rcY, sm.right - 6, rcY + rowH };
                    bool overDocs = (cursor.x >= rcDocs.left && cursor.x <= rcDocs.right && cursor.y >= rcDocs.top && cursor.y <= rcDocs.bottom);
                    if (overDocs) { HBRUSH hb = CreateSolidBrush(RGB(70, 90, 130)); FillRect(dc, &rcDocs, hb); DeleteObject(hb); }
                    TextOutA(dc, rcDocs.left + 6, rcDocs.top + 4, "Recent Docs", 11);

                    // Bottom area - "All Programs" toggle button
                    int btnY = sm.bottom - 30;
                    RECT allProgBtn{ sm.left + 6, btnY, sm.left + leftColW - 6, btnY + 24 };
                    bool overAllProg = (cursor.x >= allProgBtn.left && cursor.x <= allProgBtn.right && cursor.y >= allProgBtn.top && cursor.y <= allProgBtn.bottom);
                    HBRUSH apb = CreateSolidBrush(overAllProg ? RGB(70, 80, 100) : RGB(60, 60, 75));
                    FillRect(dc, &allProgBtn, apb); DeleteObject(apb);
                    FrameRect(dc, &allProgBtn, (HBRUSH)GetStockObject(WHITE_BRUSH));
                    const char* btnText = g_startMenuAllProgs ? "< Back" : "All Programs >";
                    TextOutA(dc, allProgBtn.left + 8, allProgBtn.top + 6, btnText, (int)strlen(btnText));

                    // Power menu area (bottom-right)
                    int shutdownBtnW = 80;
                    int shutdownBtnH = 24;
                    RECT shutdownBtn{ sm.right - shutdownBtnW - 30, btnY, sm.right - 30, btnY + shutdownBtnH };
                    bool overShutdown = (cursor.x >= shutdownBtn.left && cursor.x <= shutdownBtn.right && cursor.y >= shutdownBtn.top && cursor.y <= shutdownBtn.bottom);
                    HBRUSH sdb = CreateSolidBrush(overShutdown ? RGB(80, 40, 40) : RGB(60, 60, 75));
                    FillRect(dc, &shutdownBtn, sdb); DeleteObject(sdb);
                    FrameRect(dc, &shutdownBtn, (HBRUSH)GetStockObject(WHITE_BRUSH));
                    TextOutA(dc, shutdownBtn.left + 10, shutdownBtn.top + 6, "Shutdown", 8);
                }

                // Capture framebuffer for VNC if server is running.
                // When using the GDI backend we still capture from the
                // window DC because the compositor paints via GDI calls.
                // Once the compositor migrates to painting into the
                // VideoBackend pixel buffer, feedVncFromBackend() will
                // be the sole path and this block can be removed.
                if (vnc::VncServer::IsRunning( )) {
                    HDC memDC = CreateCompatibleDC(dc);
                    HBITMAP memBitmap = CreateCompatibleBitmap(dc, cr.right - cr.left, cr.bottom - cr.top);
                    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);
                    BitBlt(memDC, 0, 0, cr.right - cr.left, cr.bottom - cr.top, dc, 0, 0, SRCCOPY);

                    BITMAPINFO bmi{};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = cr.right - cr.left;
                    bmi.bmiHeader.biHeight = -(cr.bottom - cr.top); // negative for top-down
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;

                    std::vector<uint8_t> pixels((cr.right - cr.left) * (cr.bottom - cr.top) * 4);
                    GetDIBits(memDC, memBitmap, 0, cr.bottom - cr.top, pixels.data( ), &bmi, DIB_RGB_COLORS);
                    vnc::VncServer::UpdateFramebuffer(pixels.data( ), cr.right - cr.left, cr.bottom - cr.top, (cr.right - cr.left) * 4);

                    // Also copy into the video backend buffer so
                    // feedVncFromBackend() stays in sync for future use.
                    if (g_videoBackend && g_videoBackend->getPixels( )) {
                        uint32_t* dst = g_videoBackend->getPixels( );
                        int bw = g_videoBackend->getWidth( );
                        int bh = g_videoBackend->getHeight( );
                        int cw = cr.right - cr.left;
                        int ch = cr.bottom - cr.top;
                        int copyW = cw < bw ? cw : bw;
                        int copyH = ch < bh ? ch : bh;
                        const uint32_t* src = reinterpret_cast<const uint32_t*>(pixels.data( ));
                        for (int y = 0; y < copyH; ++y)
                            std::memcpy(dst + y * bw, src + y * cw, static_cast<size_t>(copyW) * 4);
                    }

                    SelectObject(memDC, oldBitmap);
                    DeleteObject(memBitmap);
                    DeleteDC(memDC);
                }

                EndPaint(h, &ps); return 0;
            }
            case WM_LBUTTONDOWN: {
                int mx = GET_X_LPARAM(l); int my = GET_Y_LPARAM(l); RECT cr; GetClientRect(h, &cr); int taskbarH = 40;
                // Dismiss right-click menu on any left click
                if (RightClickMenu::IsVisible( )) { RightClickMenu::HandleClick(mx, my); requestRepaint( ); return 0; }
                // Handle taskbar right-click menu click
                if (g_taskbarMenuVisible) {
                    const int tmItemH = 28; const int tmPad = 6;
                    static const char* tmActions[] = { "TaskManager", "Reboot", "LogOff" };
                    const int tmItemCount = 3;
                    if (mx >= g_taskbarMenuRect.left && mx <= g_taskbarMenuRect.right &&
                        my >= g_taskbarMenuRect.top && my <= g_taskbarMenuRect.bottom) {
                        int idx = (my - g_taskbarMenuRect.top - tmPad) / tmItemH;
                        if (idx >= 0 && idx < tmItemCount) {
                            g_taskbarMenuVisible = false;
                            if (idx == 0) { launchAction("TaskManager"); } else if (idx == 1) { publishOut(MsgType::MT_WidgetEvt, "REBOOT"); } else if (idx == 2) { publishOut(MsgType::MT_WidgetEvt, "LOGOFF"); }
                            requestRepaint( ); return 0;
                        }
                    }
                    g_taskbarMenuVisible = false;
                    requestRepaint( ); return 0;
                }
                // Show Desktop button (thin sliver on far right of taskbar)
                { int sdW = 6; int sdX = cr.right - sdW; if (mx >= sdX && my >= cr.bottom - taskbarH && mx <= cr.right && my <= cr.bottom) { ipc::Message sdm; sdm.type = static_cast<uint32_t>(gui::MsgType::MT_ShowDesktopToggle); handleMessage(sdm); requestRepaint( ); return 0; } }
                RECT startBtn{ 8,cr.bottom - taskbarH + 6,8 + 32,cr.bottom - 6 }; // Start button toggle
                if (mx >= startBtn.left && mx <= startBtn.right && my >= startBtn.top && my <= startBtn.bottom) {
                    g_startMenuVisible = !g_startMenuVisible;
                    if (g_startMenuVisible) {
                        g_startMenuSel = 0;
                        g_startMenuScroll = 0;
                        g_startMenuAllProgs = false; // reset to recent view
                        refreshAllProgramsList( ); // ensure sorted list is ready
                    }
                    requestRepaint( );
                    return 0;
                }
                // Start menu click
                if (g_startMenuVisible) {
                    // Check "All Programs" toggle button
                    int smW = 440;
                    int leftColW = 260;
                    int btnY = g_startMenuRect.bottom - 30;
                    RECT allProgBtn{ g_startMenuRect.left + 6, btnY, g_startMenuRect.left + leftColW - 6, btnY + 24 };
                    if (mx >= allProgBtn.left && mx <= allProgBtn.right && my >= allProgBtn.top && my <= allProgBtn.bottom) {
                        g_startMenuAllProgs = !g_startMenuAllProgs;
                        g_startMenuSel = 0;
                        g_startMenuScroll = 0;
                        requestRepaint( );
                        return 0;
                    }

                    // Check Shutdown button
                    int shutdownBtnW = 80;
                    int shutdownBtnH = 24;
                    RECT shutdownBtn{ g_startMenuRect.right - shutdownBtnW - 30, btnY, g_startMenuRect.right - 30, btnY + shutdownBtnH };
                    if (mx >= shutdownBtn.left && mx <= shutdownBtn.right && my >= shutdownBtn.top && my <= shutdownBtn.bottom) {
                        // Launch shutdown confirmation dialog
                        std::cout << "[Compositor] Shutdown button clicked!" << std::endl;
                        Logger::write(LogLevel::Info, "Shutdown requested from Start Menu");
                        apps::ShutdownDialog::Launch( );
                        g_startMenuVisible = false;
                        requestRepaint( );
                        return 0;
                    }

                    // Check right column shortcuts
                    int rcX = g_startMenuRect.left + leftColW + 4;
                    int rcY = g_startMenuRect.top + 6;
                    int rowH = 20;

                    // Computer Files
                    RECT rcComputer{ rcX, rcY, g_startMenuRect.right - 6, rcY + rowH };
                    if (mx >= rcComputer.left && mx <= rcComputer.right && my >= rcComputer.top && my <= rcComputer.bottom) {
                        launchAction("ComputerFiles");
                        g_startMenuVisible = false;
                        requestRepaint( );
                        return 0;
                    }
                    rcY += rowH + 4;

                    // Console
                    RECT rcConsole{ rcX, rcY, g_startMenuRect.right - 6, rcY + rowH };
                    if (mx >= rcConsole.left && mx <= rcConsole.right && my >= rcConsole.top && my <= rcConsole.bottom) {
                        launchAction("Console");
                        g_startMenuVisible = false;
                        requestRepaint( );
                        return 0;
                    }
                    rcY += rowH + 4;

                    // Recent Documents - just close menu for now
                    RECT rcDocs{ rcX, rcY, g_startMenuRect.right - 6, rcY + rowH };
                    if (mx >= rcDocs.left && mx <= rcDocs.right && my >= rcDocs.top && my <= rcDocs.bottom) {
                        Logger::write(LogLevel::Info, "Recent Documents clicked (not implemented)");
                        // Future: show popout with recent documents
                        requestRepaint( );
                        return 0;
                    }

                    // List item click
                    int listTop = g_startMenuRect.top + 4;
                    int listBottom = btnY - 4; // above buttons
                    if (mx >= g_startMenuRect.left && mx <= g_startMenuRect.left + leftColW && my >= listTop && my <= listBottom) {
                        int idx = (my - listTop) / rowH + g_startMenuScroll;
                        int itemCount = g_startMenuAllProgs ? (int)g_startMenuAllProgsSorted.size( ) : (int)g_items.size( );
                        if (idx >= 0 && idx < itemCount) {
                            uint64_t now = nowMs( );
                            if (g_lastItemIndex == idx && (now - g_lastItemClickTicks) < 450) {
                                // Double-click: launch
                                std::string action = g_startMenuAllProgs ? g_startMenuAllProgsSorted[idx] : g_items[idx].action;
                                launchAction(action);
                                g_startMenuVisible = false;
                            } else {
                                // Single click: select
                                g_lastItemIndex = idx;
                                g_lastItemClickTicks = now;
                                g_startMenuSel = idx;
                            }
                            requestRepaint( );
                            return 0;
                        }
                    } else {
                        g_startMenuVisible = false;
                    }
                }
                // Desktop icon click (selection / double / drag initiation)
                // Skip if a visible window is at the click position (windows are above desktop icons)
                { bool windowAtClick = false; { std::lock_guard<std::mutex> lk(g_lock); windowAtClick = (hitWindowAt(mx, my) != nullptr); } if (!windowAtClick) { const int iconW = 56; const int iconH = 56; const int cellW = iconW + 28; const int cellH = iconH + 38; if (my < cr.bottom - taskbarH) { int hitIdx = -1; for (int i = 0; i < (int)g_items.size( ); ++i) { int ix = g_items[i].ix; int iy = g_items[i].iy; if (mx >= ix && mx < ix + cellW && my >= iy && my < iy + cellH) { hitIdx = i; break; } } if (hitIdx >= 0) { uint64_t now = nowMs( ); if (g_lastItemIndex == hitIdx && (now - g_lastItemClickTicks) < 450) { launchAction(g_items[hitIdx].action); g_lastItemIndex = -1; g_lastItemClickTicks = 0; } else { for (auto& di : g_items) di.selected = false; g_items[hitIdx].selected = true; g_lastItemIndex = hitIdx; g_lastItemClickTicks = now; g_iconDragPending = true; g_iconDragIndex = hitIdx; g_iconDragStartX = mx; g_iconDragStartY = my; g_iconDragOffX = mx - g_items[hitIdx].ix; g_iconDragOffY = my - g_items[hitIdx].iy; SetCapture(h); } requestRepaint( ); return 0; } } } }
                // Taskbar button click (minimize/restore/untombstone)
                uint64_t id = hitTestTaskbarButton(mx, my, cr, taskbarH); if (id) { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { WinInfo& w = it->second; if (w.tombstoned) { 
                    // Restore from tombstone (untombstone)
                    w.tombstoned = false; w.visible = true; g_focus = w.id; 
                    for (auto itZ = g_z.begin( ); itZ != g_z.end( ); ++itZ) { if (*itZ == id) { g_z.erase(itZ); break; } } g_z.push_back(id); 
                } else if (w.minimized) { 
                    // Restore from minimized
                    w.minimized = false; g_focus = w.id; 
                    for (auto itZ = g_z.begin( ); itZ != g_z.end( ); ++itZ) { if (*itZ == id) { g_z.erase(itZ); break; } } g_z.push_back(id); 
                } else if (g_focus == id) { 
                    // Currently focused - minimize it
                    w.minimized = true; g_focus = 0; 
                } else { 
                    // Not focused - bring to focus
                    g_focus = w.id; 
                    for (auto itZ = g_z.begin( ); itZ != g_z.end( ); ++itZ) { if (*itZ == id) { g_z.erase(itZ); break; } } g_z.push_back(id); 
                } } requestRepaint( ); return 0; }
                // pass to widget handling and general mouse handling
                uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(g_focus); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; }
                Compositor::handleMouse(mx, my, true, false); publishOut(MsgType::MT_InputMouse, std::to_string(mx) + "|" + std::to_string(my) + "|1|down", ownerPid); return 0;
            }
            case WM_RBUTTONDOWN: {
                int mx = GET_X_LPARAM(l); int my = GET_Y_LPARAM(l); if (g_startMenuVisible && mx >= g_startMenuRect.left && mx <= g_startMenuRect.right && my >= g_startMenuRect.top && my <= g_startMenuRect.bottom) { int idx = (my - (g_startMenuRect.top + 4)) / 20; if (idx >= 0 && idx < (int)g_items.size( )) { if (g_items[idx].pinned) unpinAction(g_items[idx].action); else pinAction(g_items[idx].action); requestRepaint( ); return 0; } }
                // Desktop icon right-click pin/unpin or taskbar right-click menu
                RECT cr; GetClientRect(h, &cr); int taskbarH = 40;
                // Taskbar right-click: show context menu
                if (my >= cr.bottom - taskbarH) {
                    const int tmItemH = 28; const int tmItemCount = 3; const int tmPad = 6;
                    int tmH = tmItemH * tmItemCount + tmPad * 2;
                    int tmW = 180;
                    int tmX = mx; int tmY = cr.bottom - taskbarH - tmH;
                    if (tmX + tmW > cr.right) tmX = cr.right - tmW - 2;
                    if (tmY < 0) tmY = 0;
                    g_taskbarMenuRect = { tmX, tmY, tmX + tmW, tmY + tmH };
                    g_taskbarMenuVisible = true;
                    g_taskbarMenuSel = -1;
                    g_startMenuVisible = false;
                    requestRepaint( ); return 0;
                }
                if (my < cr.bottom - taskbarH) {
                    // Check if right-click is on a window
                    WinInfo* hitWin = nullptr;
                    uint64_t ownerPid = 0;
                    {
                        std::lock_guard<std::mutex> lk(g_lock);
                        hitWin = hitWindowAt(mx, my);
                        if (hitWin) {
                            ownerPid = hitWin->ownerPid;
                            // Set focus to the clicked window
                            if (g_focus != hitWin->id) {
                                g_focus = hitWin->id;
                                auto it2 = std::find(g_z.begin(), g_z.end(), hitWin->id);
                                if (it2 != g_z.end()) {
                                    g_z.erase(it2);
                                    g_z.push_back(hitWin->id);
                                }
                            }
                        }
                    }
                    
                    // If right-click is on a window, forward the event to the application
                    if (hitWin) {
                        publishOut(MsgType::MT_InputMouse, std::to_string(mx) + "|" + std::to_string(my) + "|2|down", ownerPid);
                        requestRepaint();
                        return 0;
                    }
                    
                    // Otherwise, handle desktop icon right-click or show desktop context menu
                    const int iconW = 56; const int cellW = iconW + 28; const int cellH = 56 + 38; int hitIdx = -1; for (int i = 0; i < (int)g_items.size( ); ++i) { int ix = g_items[i].ix; int iy = g_items[i].iy; if (mx >= ix && mx < ix + cellW && my >= iy && my < iy + cellH) { hitIdx = i; break; } } if (hitIdx >= 0) { if (g_items[hitIdx].pinned) unpinAction(g_items[hitIdx].action); else pinAction(g_items[hitIdx].action); requestRepaint( ); return 0; }
                    // Desktop right-click context menu (no icon hit)
                    RightClickMenu::Show(mx, my);
                    requestRepaint( );
                    return 0;
                }
            } break;
            case WM_LBUTTONUP: {
                int mx = GET_X_LPARAM(l); int my = GET_Y_LPARAM(l);
                if (g_iconDragActive || g_iconDragPending) {
                    ReleaseCapture( );
                    if (g_iconDragActive) {
                        // Save icon positions to config
                        g_cfg.iconPositions.clear( ); for (const auto& di : g_items) { DesktopIconPos ip; ip.name = di.label; ip.x = di.ix; ip.y = di.iy; g_cfg.iconPositions.push_back(ip); } saveDesktopConfig( );
                    }
                    g_iconDragActive = false; g_iconDragPending = false; requestRepaint( ); break;
                }
                uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(g_focus); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; }
                Compositor::handleMouse(mx, my, false, true); publishOut(MsgType::MT_InputMouse, std::to_string(mx) + "|" + std::to_string(my) + "|1|up", ownerPid);
            } break;
            case WM_MOUSEMOVE: {
                int mx = GET_X_LPARAM(l); int my = GET_Y_LPARAM(l);
                if (g_iconDragPending && !g_iconDragActive) { if (std::abs(mx - g_iconDragStartX) >= 4 || std::abs(my - g_iconDragStartY) >= 4) { g_iconDragActive = true; } }
                if (g_iconDragActive && g_iconDragIndex >= 0 && g_iconDragIndex < (int)g_items.size( )) { RECT cr2; GetClientRect(h, &cr2); int taskbarH2 = 40; int nx = mx - g_iconDragOffX; int ny = my - g_iconDragOffY; if (nx < 0) nx = 0; if (ny < 0) ny = 0; const int cellW2 = 84; const int cellH2 = 94; if (nx + cellW2 > cr2.right) nx = cr2.right - cellW2; if (ny + cellH2 > cr2.bottom - taskbarH2) ny = cr2.bottom - taskbarH2 - cellH2; g_items[g_iconDragIndex].ix = nx; g_items[g_iconDragIndex].iy = ny; requestRepaint( ); break; }
                if (g_iconDragPending) { break; } // Skip handleMouse while drag is pending
                uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(g_focus); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; }
                Compositor::handleMouse(mx, my, false, false); publishOut(MsgType::MT_InputMouse, std::to_string(mx) + "|" + std::to_string(my) + "|0|move", ownerPid);
            } break;
            case WM_KEYDOWN: case WM_SYSKEYDOWN: {
                int key = (int)w;
                // Taskbar menu keyboard handling
                if (g_taskbarMenuVisible) {
                    if (key == VK_ESCAPE) { g_taskbarMenuVisible = false; requestRepaint( ); return 0; }
                    const int tmItemCount = 3;
                    if (key == VK_UP) { if (g_taskbarMenuSel > 0) g_taskbarMenuSel--; else g_taskbarMenuSel = tmItemCount - 1; requestRepaint( ); return 0; }
                    if (key == VK_DOWN) { if (g_taskbarMenuSel < tmItemCount - 1) g_taskbarMenuSel++; else g_taskbarMenuSel = 0; requestRepaint( ); return 0; }
                    if (key == VK_RETURN && g_taskbarMenuSel >= 0) {
                        int sel = g_taskbarMenuSel; g_taskbarMenuVisible = false;
                        if (sel == 0) launchAction("TaskManager");
                        else if (sel == 1) publishOut(MsgType::MT_WidgetEvt, "REBOOT");
                        else if (sel == 2) publishOut(MsgType::MT_WidgetEvt, "LOGOFF");
                        requestRepaint( ); return 0;
                    }
                }
                // start-menu navigation handled here
                if (g_startMenuVisible) {
                    int maxItems = g_startMenuAllProgs ? (int)g_startMenuAllProgsSorted.size( ) : (int)g_items.size( );
                    if (key == VK_UP) {
                        if (g_startMenuSel > 0) g_startMenuSel--;
                        if (g_startMenuSel < g_startMenuScroll) g_startMenuScroll = g_startMenuSel;
                        requestRepaint( );
                        return 0;
                    }
                    if (key == VK_DOWN) {
                        if (maxItems > 0 && g_startMenuSel < maxItems - 1) g_startMenuSel++;
                        const int maxRows = 14;
                        if (g_startMenuSel >= g_startMenuScroll + maxRows) g_startMenuScroll = g_startMenuSel - maxRows + 1;
                        requestRepaint( );
                        return 0;
                    }
                    if (key == VK_RETURN) {
                        if (g_startMenuSel >= 0 && g_startMenuSel < maxItems) {
                            std::string action = g_startMenuAllProgs ? g_startMenuAllProgsSorted[g_startMenuSel] : g_items[g_startMenuSel].action;
                            launchAction(action);
                            g_startMenuVisible = false;
                            requestRepaint( );
                        }
                        return 0;
                    }
                    if (key == VK_ESCAPE) {
                        g_startMenuVisible = false;
                        requestRepaint( );
                        return 0;
                    }
                    if (key == VK_TAB) {
                        // Toggle between Recent and All Programs
                        g_startMenuAllProgs = !g_startMenuAllProgs;
                        g_startMenuSel = 0;
                        g_startMenuScroll = 0;
                        requestRepaint( );
                        return 0;
                    }
                }

                uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(g_focus); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; }
                publishOut(MsgType::MT_InputKey, std::to_string(key) + "|down", ownerPid);
            } break;
            case WM_KEYUP: { int key = (int)w; uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(g_focus); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; } publishOut(MsgType::MT_InputKey, std::to_string(key) + "|up", ownerPid); } break;
            }
            return DefWindowProcA(h, msg, w, l);
        }
#endif

        void Compositor::sendFocus(uint64_t winId) { uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(winId); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; } publishOut(MsgType::MT_SetFocus, std::to_string(winId), ownerPid); }
        void Compositor::invalidate(uint64_t winId) {
#ifdef _WIN32
            requestRepaint( );
#else
            g_needsRedraw = true;
            Logger::write(LogLevel::Info, std::string("invalidate called for window ") + std::to_string(winId) + ", g_needsRedraw=true");
#endif
        }
        void Compositor::emitWidgetEvt(uint64_t winId, int wid, const std::string& evt, const std::string& value) { uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(winId); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; } publishOut(MsgType::MT_WidgetEvt, std::to_string(winId) + "|" + std::to_string(wid) + "|" + evt + "|" + value, ownerPid); }

        void Compositor::handleMouse(int mx, int my, bool down, bool up) {
            std::lock_guard<std::mutex> lk(g_lock); const int titleBarH = 24; const int gripSize = 12; const int taskbarH = 40;
#ifdef _WIN32
            RECT cr{ 0,0,1024,768 };
            if (g_hwnd) GetClientRect(g_hwnd, &cr);
#else
            // On bare-metal, use video backend dimensions
            struct { int left; int top; int right; int bottom; } cr{ 0, 0, 1024, 768 };
            if (g_videoBackend) {
                cr.right = g_videoBackend->getWidth();
                cr.bottom = g_videoBackend->getHeight();
            }
#endif
            // On mouse down, record start position and check if we're in a title bar (pending drag)
            if (down) {
                g_dragStartX = mx; g_dragStartY = my; g_dragPending = false; g_dragPendingWin = 0;
                for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { WinInfo& w = g_windows[g_z[idx]]; if (w.minimized || w.maximized || w.tombstoned) continue; if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + titleBarH) { g_dragPending = true; g_dragPendingWin = w.id; g_dragOffX = mx - w.x; g_dragOffY = my - w.y; break; } }
            }
            // On mouse move with pending drag, check if we moved enough to initiate actual drag
            if (!down && !up && g_dragPending && !g_dragActive) { if (std::abs(mx - g_dragStartX) >= 4 || std::abs(my - g_dragStartY) >= 4) { auto it = g_windows.find(g_dragPendingWin); if (it != g_windows.end( )) { WinInfo& w = it->second; if (!w.minimized && !w.maximized && !w.tombstoned) { g_dragActive = true; g_dragWin = g_dragPendingWin; g_dragPending = false; } } } }
            // On mouse up, clear pending drag state
            if (up) { g_dragPending = false; g_dragPendingWin = 0; }
            // find topmost window under cursor
            WinInfo* topW = nullptr; for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { auto it = g_windows.find(g_z[idx]); if (it == g_windows.end( )) continue; WinInfo& w = it->second; if (w.minimized || w.tombstoned) continue; if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h) { topW = &w; break; } }
            // Titlebar button handling (hover/press/click) - matching Legacy layout
            // Button layout right-to-left: close, tombstone, maximize, minimize
            if (topW) { // compute button rects for this window
                const int btnSize = 16; const int btnGap = 6;
                int closeLeft = topW->x + topW->w - btnGap - btnSize;
                int tombLeft = closeLeft - btnGap - btnSize;
                int maxLeft = tombLeft - btnGap - btnSize;
                int minLeft = maxLeft - btnGap - btnSize;
                bool overClose = (mx >= closeLeft && mx < closeLeft + btnSize && my >= topW->y && my < topW->y + titleBarH);
                bool overTomb = (mx >= tombLeft && mx < tombLeft + btnSize && my >= topW->y && my < topW->y + titleBarH);
                bool overMax = (mx >= maxLeft && mx < maxLeft + btnSize && my >= topW->y && my < topW->y + titleBarH);
                bool overMin = (mx >= minLeft && mx < minLeft + btnSize && my >= topW->y && my < topW->y + titleBarH);
                // mouse move -> update hover
                if (!down && !up) { 
                    if (topW->titleBtnCloseHover != overClose) { topW->titleBtnCloseHover = overClose; invalidate(topW->id); } 
                    if (topW->titleBtnTombHover != overTomb) { topW->titleBtnTombHover = overTomb; invalidate(topW->id); }
                    if (topW->titleBtnMaxHover != overMax) { topW->titleBtnMaxHover = overMax; invalidate(topW->id); } 
                    if (topW->titleBtnMinHover != overMin) { topW->titleBtnMinHover = overMin; invalidate(topW->id); } 
                }
                // mouse down -> set pressed if over
                if (down) { 
                    if (overClose) { topW->titleBtnClosePressed = true; invalidate(topW->id); } 
                    if (overTomb) { topW->titleBtnTombPressed = true; invalidate(topW->id); }
                    if (overMax) { topW->titleBtnMaxPressed = true; invalidate(topW->id); } 
                    if (overMin) { topW->titleBtnMinPressed = true; invalidate(topW->id); } 
                }
                // mouse up -> perform action if pressed
                if (up) {
                    if (topW->titleBtnClosePressed) { // close
                        uint64_t id = topW->id;
                        uint64_t ownerPid = topW->ownerPid;
                        topW->titleBtnClosePressed = false; topW->titleBtnCloseHover = false; invalidate(id);
                        // remove window
                        g_windows.erase(id);
                        for (auto it = g_z.begin( ); it != g_z.end( ); ++it) { if (*it == id) { g_z.erase(it); break; } }
                        if (g_focus == id) g_focus = 0;
                        publishOut(MsgType::MT_Close, std::to_string(id), ownerPid);
                        return;
                    }
                    if (topW->titleBtnTombPressed) { // tombstone (freeze/disable window)
                        uint64_t id = topW->id;
                        topW->titleBtnTombPressed = false; topW->titleBtnTombHover = false;
                        topW->tombstoned = true;
                        topW->visible = false;
                        if (g_focus == id) g_focus = 0;
                        invalidate(id);
                        return;
                    }
                    if (topW->titleBtnMinPressed) { // minimize
                        uint64_t id = topW->id;
                        topW->titleBtnMinPressed = false; topW->titleBtnMinHover = false; topW->minimized = true; if (g_focus == id) g_focus = 0; invalidate(id); return;
                    }
                    if (topW->titleBtnMaxPressed) { // maximize/restore
                        uint64_t id = topW->id;
                        // toggle maximize state
                        if (!topW->maximized) { // maximize
                            topW->prevX = topW->x; topW->prevY = topW->y; topW->prevW = topW->w; topW->prevH = topW->h;
#ifdef _WIN32
                            RECT crL{ 0,0,1024,768 }; if (g_hwnd) GetClientRect(g_hwnd, &crL);
#else
                            RECT crL{ 0,0,1024,768 };
#endif
                            int taskbarY = crL.bottom - taskbarH;
                            topW->x = crL.left; topW->y = crL.top; topW->w = crL.right - crL.left; topW->h = taskbarY - crL.top; topW->maximized = true; topW->snapState = 0; topW->dirty = true;
                        } else { // restore
                            topW->x = topW->prevX; topW->y = topW->prevY; topW->w = topW->prevW; topW->h = topW->prevH; topW->maximized = false; topW->dirty = true;
                        }
                        topW->titleBtnMaxPressed = false; topW->titleBtnMaxHover = false; invalidate(id); return;
                    }
                }
            }

            if (topW) { int wx = mx - topW->x; int wy = my - topW->y - titleBarH; for (auto& wd : topW->widgets) { bool over = (wx >= wd.x && wx < wd.x + wd.w && wy >= wd.y && wy < wd.y + wd.h); if (!down && !up) { if (wd.hover != over) { wd.hover = over; invalidate(topW->id); } } else if (down) { if (over) { wd.pressed = true; wd.hover = true; invalidate(topW->id); } } else if (up) { if (wd.pressed) { if (over) { emitWidgetEvt(topW->id, wd.id, "click", ""); Logger::write(LogLevel::Info, std::string("Widget clicked: ") + std::to_string(topW->id) + "/" + std::to_string(wd.id)); } wd.pressed = false; wd.hover = false; invalidate(topW->id); } } } }
            // move while dragging
            if (g_dragActive && !up) { auto it = g_windows.find(g_dragWin); if (it != g_windows.end( )) { WinInfo& w = it->second; if (!w.maximized && !w.minimized && !w.tombstoned) { int nx = mx - g_dragOffX; int ny = my - g_dragOffY; if (nx < cr.left) nx = cr.left; if (ny < cr.top) ny = cr.top; if (nx + w.w > cr.right) nx = cr.right - w.w; if (ny + w.h > cr.bottom - taskbarH) ny = cr.bottom - taskbarH - w.h; if (nx != w.x || ny != w.y) { w.x = nx; w.y = ny; w.dirty = true; invalidate(w.id); } } } }
            if (down) { uint64_t t = nowMs( ); for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { uint64_t wid = g_z[idx]; WinInfo& w = g_windows[wid]; if (w.minimized || w.tombstoned) continue; if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + titleBarH) { if (g_lastClickWin == w.id && (t - g_lastClickTicks) < 450) { if (!w.minimized) { if (!w.maximized) { w.prevX = w.x; w.prevY = w.y; w.prevW = w.w; w.prevH = w.h; w.x = 0; w.y = 0; w.w = cr.right; w.h = cr.bottom - taskbarH; w.maximized = true; } else { w.x = w.prevX; w.y = w.prevY; w.w = w.prevW; w.h = w.prevH; w.maximized = false; } } g_lastClickWin = 0; g_lastClickTicks = 0; invalidate(w.id); return; } g_lastClickWin = w.id; g_lastClickTicks = t; break; } } }
            if (down) { for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { WinInfo& w = g_windows[g_z[idx]]; if (w.minimized || w.maximized || w.tombstoned) continue; if (mx >= w.x + w.w - gripSize && mx < w.x + w.w && my >= w.y + w.h - gripSize && my < w.y + w.h) { g_resizeActive = true; g_resizeWin = w.id; g_resizeStartW = w.w; g_resizeStartH = w.h; g_resizeStartMX = mx; g_resizeStartMY = my; break; } } }
            if (g_dragActive && up) { auto it = g_windows.find(g_dragWin); if (it != g_windows.end( )) { WinInfo& w = it->second; const int snap = 16; bool nearLeft = mx <= snap, nearRight = mx >= cr.right - snap, nearTop = my <= snap; bool nearBottom = my >= cr.bottom - taskbarH - snap; if (nearTop && !(nearLeft || nearRight)) { w.prevX = w.x; w.prevY = w.y; w.prevW = w.w; w.prevH = w.h; w.x = 0; w.y = 0; w.w = cr.right; w.h = cr.bottom - taskbarH; w.maximized = true; w.snapState = 0; } else if (nearLeft) { w.maximized = false; w.x = 0; w.y = 0; w.w = cr.right / 2; w.h = cr.bottom - taskbarH; w.snapState = 1; } else if (nearRight) { w.maximized = false; w.x = cr.right / 2; w.y = 0; w.w = cr.right / 2; w.h = cr.bottom - taskbarH; w.snapState = 2; } w.dirty = true; } g_dragActive = false; g_dragWin = 0; g_snapPreviewActive = false; invalidate(0); }
            if (g_resizeActive && !up) { auto it = g_windows.find(g_resizeWin); if (it != g_windows.end( )) { int dw = mx - g_resizeStartMX; int dh = my - g_resizeStartMY; int newW = g_resizeStartW + dw; if (newW < 160) newW = 160; int newH = g_resizeStartH + dh; if (newH < 120) newH = 120; g_resizePreviewActive = true; g_resizePreviewW = newW; g_resizePreviewH = newH; } }
            if (g_resizeActive && up) { auto it = g_windows.find(g_resizeWin); if (it != g_windows.end( )) { int dw = mx - g_resizeStartMX; int dh = my - g_resizeStartMY; int newW = g_resizeStartW + dw; if (newW < 160) newW = 160; int newH = g_resizeStartH + dh; if (newH < 120) newH = 120; it->second.w = newW; it->second.h = newH; it->second.dirty = true; } g_resizeActive = false; g_resizeWin = 0; g_resizePreviewActive = false; }
            if (down) { for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { WinInfo& w = g_windows[g_z[idx]]; if (w.minimized || w.tombstoned) continue; if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h) { g_focus = w.id; for (auto itZ = g_z.begin( ); itZ != g_z.end( ); ++itZ) { if (*itZ == w.id) { g_z.erase(itZ); break; } } g_z.push_back(w.id); sendFocus(w.id); break; } } }
        }

        void Compositor::handleMessage(const ipc::Message& m) {
            std::string s(m.data.begin( ), m.data.end( )); switch ((MsgType)m.type) {
            case MsgType::MT_Create: { 
                Logger::write(LogLevel::Info, std::string("Compositor received MT_Create: ") + s + " from pid=" + std::to_string(m.srcPid));
                std::istringstream iss(s); std::string title; std::getline(iss, title, '|'); std::string wS, hS; std::getline(iss, wS, '|'); std::getline(iss, hS, '|'); int w = 320, h = 200; try { w = std::stoi(wS); h = std::stoi(hS); } catch (...) {} uint64_t id = s_nextWinId.fetch_add(1); 
                { 
                    std::lock_guard<std::mutex> lk(g_lock); 
                    int winX = 60 + (int)(id % 7) * 40;
                    int winY = 60 + (int)(id % 7) * 40;
                    WinInfo wi{};
                    wi.id = id;
                    wi.title = title;
                    wi.x = winX;
                    wi.y = winY;
                    wi.w = w;
                    wi.h = h;
                    wi.minimized = false;
                    wi.maximized = false;
                    wi.dirty = true;
                    wi.visible = true;
                    wi.ownerPid = m.srcPid;
#ifdef _WIN32
                    wi.taskbarIcon = Icons::TaskbarIcon(16);
#endif
                    // Initialize animation state - store normal bounds
                    wi.animState.normX = winX;
                    wi.animState.normY = winY;
                    wi.animState.normW = w;
                    wi.animState.normH = h;
                    // Start fade-in animation
                    WindowAnimator::BeginFadeIn(wi.animState, winY);
                    g_windows[id] = wi;
                    g_z.push_back(id); 
                    g_focus = id; 
                } 
                Logger::write(LogLevel::Info, std::string("Compositor created window id=") + std::to_string(id) + " sending ack to pid=" + std::to_string(m.srcPid));
                publishOut(MsgType::MT_Create, std::to_string(id) + "|" + title, m.srcPid); sendFocus(id); invalidate(id); } break;
            case MsgType::MT_DrawText: { std::istringstream iss(s); std::string idS; std::getline(iss, idS, '|'); std::string text; std::getline(iss, text); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { if (text == "\f") { it->second.texts.clear(); it->second.rects.clear(); } else { it->second.texts.push_back(text); } it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_DrawText, std::to_string(id) + "|" + text, ownerPid); invalidate(id); } break;
            case MsgType::MT_Close: { uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(s); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); auto wit = g_windows.find(id); if (wit != g_windows.end( )) ownerPid = wit->second.ownerPid; g_windows.erase(id); auto it = std::find(g_z.begin( ), g_z.end( ), id); if (it != g_z.end( )) g_z.erase(it); if (g_focus == id) g_focus = 0; } publishOut(MsgType::MT_Close, std::to_string(id), ownerPid ? ownerPid : m.srcPid); invalidate(0); } break;
            case MsgType::MT_DrawRect: { std::istringstream iss(s); std::string idS; std::getline(iss, idS, '|'); std::string xs, ys, ws, hs, rs, gs, bs; std::getline(iss, xs, '|'); std::getline(iss, ys, '|'); std::getline(iss, ws, '|'); std::getline(iss, hs, '|'); std::getline(iss, rs, '|'); std::getline(iss, gs, '|'); std::getline(iss, bs, '|'); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} DrawRectItem item{ std::stoi(xs), std::stoi(ys), std::stoi(ws), std::stoi(hs), (uint8_t)std::stoi(rs),(uint8_t)std::stoi(gs),(uint8_t)std::stoi(bs) }; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { it->second.rects.push_back(item); it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_DrawRect, std::to_string(id), ownerPid); invalidate(id); } break;
            case MsgType::MT_SetTitle: { std::istringstream iss(s); std::string idS; std::getline(iss, idS, '|'); std::string title; std::getline(iss, title); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { it->second.title = title; it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_SetTitle, std::to_string(id) + "|" + title, ownerPid); invalidate(id); } break;
            case MsgType::MT_Move: { std::istringstream iss(s); std::string idS, xs, ys; std::getline(iss, idS, '|'); std::getline(iss, xs, '|'); std::getline(iss, ys, '|'); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} int nx = std::stoi(xs), ny = std::stoi(ys); { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( ) && !it->second.maximized) { it->second.x = nx; it->second.y = ny; it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_Move, std::to_string(id) + "|" + xs + "|" + ys, ownerPid); invalidate(id); } break;
            case MsgType::MT_Resize: { std::istringstream iss(s); std::string idS, ws, hs; std::getline(iss, idS, '|'); std::getline(iss, ws, '|'); std::getline(iss, hs, '|'); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} int nw = std::stoi(ws), nh = std::stoi(hs); { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( ) && !it->second.maximized) { it->second.w = nw; it->second.h = nh; it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_Resize, std::to_string(id) + "|" + ws + "|" + hs, ownerPid); invalidate(id); } break;
            case MsgType::MT_WidgetAdd: { // format: <winId>|<type>|<id>|<x>|<y>|<w>|<h>|<text>
                std::istringstream iss(s); std::string winS, typeS, idS, xs, ys, ws2, hs2; std::getline(iss, winS, '|'); std::getline(iss, typeS, '|'); std::getline(iss, idS, '|'); std::getline(iss, xs, '|'); std::getline(iss, ys, '|'); std::getline(iss, ws2, '|'); std::getline(iss, hs2, '|'); std::string rest; std::getline(iss, rest); uint64_t winId = 0; try { winId = std::stoull(winS); } catch (...) {} int wtype = 0; try { wtype = std::stoi(typeS); } catch (...) {} int wid = 0; try { wid = std::stoi(idS); } catch (...) {} int wx = 0, wy = 0, ww = 0, wh = 0; try { wx = std::stoi(xs); wy = std::stoi(ys); ww = std::stoi(ws2); wh = std::stoi(hs2); } catch (...) {}
                Widget wd; wd.type = (WidgetType)wtype; wd.id = wid; wd.x = wx; wd.y = wy; wd.w = ww; wd.h = wh; wd.text = rest;
                uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(winId); if (it != g_windows.end( )) { it->second.widgets.push_back(wd); it->second.dirty = true; ownerPid = it->second.ownerPid; Logger::write(LogLevel::Info, std::string("Widget added to ") + std::to_string(winId) + " id=" + std::to_string(wid)); } }
                publishOut(MsgType::MT_WidgetAdd, std::to_string(winId) + "|" + std::to_string(wid), ownerPid); invalidate(winId);
            } break;
            case MsgType::MT_WindowList: { std::ostringstream oss; bool first = true; { std::lock_guard<std::mutex> lk(g_lock); for (uint64_t id : g_z) { auto it = g_windows.find(id); if (it == g_windows.end( )) continue; if (!first) oss << ";"; first = false; oss << it->first << "|" << it->second.title << "|" << (it->second.minimized ? 1 : 0); } } publishOut(MsgType::MT_WindowList, oss.str( ), m.srcPid); } break;
            case MsgType::MT_Activate: { uint64_t id = 0; try { id = std::stoull(s); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); for (auto it = g_z.begin( ); it != g_z.end( ); ++it) { if (*it == id) { g_z.erase(it); break; } } auto wit = g_windows.find(id); if (wit != g_windows.end( )) { wit->second.minimized = false; wit->second.tombstoned = false; } g_z.push_back(id); g_focus = id; } sendFocus(id); invalidate(id); } break;
            case MsgType::MT_Minimize: { uint64_t id = 0; try { id = std::stoull(s); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); auto wit = g_windows.find(id); if (wit != g_windows.end( )) { wit->second.minimized = true; wit->second.tombstoned = true; if (g_focus == id) g_focus = 0; } } invalidate(id); } break;
            case MsgType::MT_ShowDesktopToggle: { if (!g_showDesktopActive) { g_showDesktopMinimized.clear( ); for (uint64_t id : g_z) { auto it = g_windows.find(id); if (it != g_windows.end( ) && !it->second.minimized) { it->second.minimized = true; it->second.tombstoned = true; g_showDesktopMinimized.push_back(id); } } g_focus = 0; g_showDesktopActive = true; } else { for (uint64_t id : g_showDesktopMinimized) { auto it = g_windows.find(id); if (it != g_windows.end( )) { it->second.minimized = false; it->second.tombstoned = false; } } g_showDesktopMinimized.clear( ); g_showDesktopActive = false; } invalidate(0); } break;
            case MsgType::MT_StateSave: { std::string path = s; std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for (size_t i = 0; i < g_z.size( ); ++i) { uint64_t id = g_z[i]; auto it = g_windows.find(id); if (it == g_windows.end( )) continue; const WinInfo& w = it->second; SavedWindow rec; rec.id = w.id; rec.title = w.title; rec.x = w.x; rec.y = w.y; rec.w = w.w; rec.h = w.h; rec.minimized = w.minimized; rec.maximized = w.maximized; rec.z = (int)i; rec.focused = (g_focus == w.id); rec.snap = w.snapState; sw.push_back(rec); } } std::string err; if (!DesktopState::Save(path, sw, err)) publishOut(MsgType::MT_WidgetEvt, std::string("STATE_SAVE_ERR|") + err); else publishOut(MsgType::MT_WidgetEvt, std::string("STATE_SAVE_OK|") + path); } break;
            case MsgType::MT_StateLoad: { std::string path = s; std::vector<SavedWindow> sw; std::string err; if (!DesktopState::Load(path, sw, err)) { publishOut(MsgType::MT_WidgetEvt, std::string("STATE_LOAD_ERR|") + err); } else { { std::lock_guard<std::mutex> lk(g_lock); g_windows.clear( ); g_z.clear( ); g_focus = 0; std::sort(sw.begin( ), sw.end( ), [] (const SavedWindow& a, const SavedWindow& b) { return a.z < b.z; }); for (auto& w : sw) { uint64_t id = s_nextWinId.fetch_add(1); WinInfo wi{ id, w.title, w.x, w.y, w.w, w.h, {}, {}, {}, w.minimized, w.maximized, 0,0,0,0, true, w.snap }; if (wi.maximized) { RECT crL{ 0,0,1024,768 }; if (g_hwnd) GetClientRect(g_hwnd, &crL); int taskbarY = crL.bottom - 40; wi.x = crL.left; wi.y = crL.top; wi.w = crL.right - crL.left; wi.h = taskbarY - crL.top; } g_windows[id] = wi; g_z.push_back(id); if (w.focused && !wi.minimized) g_focus = id; } } publishOut(MsgType::MT_WidgetEvt, std::string("STATE_LOAD_OK|") + path); invalidate(0); } } break;
            case MsgType::MT_Invalidate: { invalidate(0); } break;
            case MsgType::MT_Ping: { publishOut(MsgType::MT_Ping, s); } break;
            case MsgType::MT_DesktopLaunch: { launchAction(s); } break;
            case MsgType::MT_DesktopPins: { std::istringstream iss(s); std::string tok; while (std::getline(iss, tok, ';')) { if (tok.size( ) < 2) continue; if (tok[0] == '+') pinAction(tok.substr(1)); else if (tok[0] == '-') unpinAction(tok.substr(1)); } } break;
            case MsgType::MT_DesktopWallpaperSet: { loadWallpaper(s); g_cfg.wallpaperPath = s; saveDesktopConfig( ); invalidate(0); } break;
            case MsgType::MT_InputMouse: {
                // Handle mouse input from kernel (bare-metal) or test harness
                // Format: <x>|<y>|<button>|<action>
                std::istringstream iss(s);
                std::string xStr, yStr, buttonStr, action;
                std::getline(iss, xStr, '|');
                std::getline(iss, yStr, '|');
                std::getline(iss, buttonStr, '|');
                std::getline(iss, action);
                
                try {
                    int mx = std::stoi(xStr);
                    int my = std::stoi(yStr);
                    int button = std::stoi(buttonStr);
                    
                    // Handle based on button and action
                    if (button == 1) { // Left button
                        if (action == "down") {
                            handleMouse(mx, my, true, false);
                        } else if (action == "up") {
                            handleMouse(mx, my, false, true);
                        }
                    } else if (button == 2) { // Right button
                        if (action == "down") {
                            // Right-click handling - check if over a window
                            WinInfo* hitWin = nullptr;
                            uint64_t ownerPid = 0;
                            {
                                std::lock_guard<std::mutex> lk(g_lock);
                                hitWin = hitWindowAt(mx, my);
                                if (hitWin) {
                                    ownerPid = hitWin->ownerPid;
                                    // Set focus to the clicked window
                                    if (g_focus != hitWin->id) {
                                        g_focus = hitWin->id;
                                        auto it2 = std::find(g_z.begin(), g_z.end(), hitWin->id);
                                        if (it2 != g_z.end()) {
                                            g_z.erase(it2);
                                            g_z.push_back(hitWin->id);
                                        }
                                    }
                                }
                            }
                            
                            // If right-click is on a window, forward the event to the application
                            if (hitWin) {
                                publishOut(MsgType::MT_InputMouse, std::to_string(mx) + "|" + std::to_string(my) + "|2|down", ownerPid);
                                invalidate(0);
                            } else {
                                // Desktop right-click - show desktop context menu
                                RightClickMenu::Show(mx, my);
                                invalidate(0);
                            }
                        }
                    } else if (button == 0) { // Mouse move
                        if (action == "move") {
                            handleMouse(mx, my, false, false);
                        }
                    }
                } catch (const std::exception& e) {
                    Logger::write(LogLevel::Error, std::string("Compositor: Failed to parse MT_InputMouse: ") + e.what());
                }
            } break;
            case MsgType::MT_InputKey: {
                // Handle keyboard input from kernel (bare-metal) or test harness
                // Format: <keycode>|<action>
                std::istringstream iss(s);
                std::string keyCodeStr, action;
                std::getline(iss, keyCodeStr, '|');
                std::getline(iss, action);
                
                try {
                    int keyCode = std::stoi(keyCodeStr);
                    uint64_t ownerPid = 0;
                    {
                        std::lock_guard<std::mutex> lk(g_lock);
                        auto it = g_windows.find(g_focus);
                        if (it != g_windows.end()) {
                            ownerPid = it->second.ownerPid;
                        }
                    }
                    
                    // Forward to focused window
                    publishOut(MsgType::MT_InputKey, std::to_string(keyCode) + "|" + action, ownerPid);
                } catch (const std::exception& e) {
                    Logger::write(LogLevel::Error, std::string("Compositor: Failed to parse MT_InputKey: ") + e.what());
                }
            } break;
            default: break;
            }
        }

        void Compositor::drawAll( ) {
#ifdef _WIN32
            requestRepaint( );
#else
            if (g_needsRedraw) {
                renderToFramebuffer();
            }
#endif
        }
        void Compositor::pumpEvents( ) {
#ifdef _WIN32
            MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); if (msg.message == WM_QUIT) break; }
#else
            // On bare-metal, we don't have a native event pump
            // Events come through IPC from the kernel input subsystem
            // Trigger a redraw if needed
            if (g_needsRedraw) {
                renderToFramebuffer();
            }
#endif
        }

        int Compositor::main(int argc, char** argv) {
            Logger::write(LogLevel::Info, "Compositor service started (native window)");
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
#ifdef _WIN32
            initWindow( );
#endif
            initVideoBackend( );
            DesktopConfigData cfg; std::string cfgErr; bool cfgOk = DesktopConfig::Load("desktop.json", cfg, cfgErr);
            g_cfg = cfg; // Store config
            refreshDesktopItems( ); // Populate g_items from pinned/recent
            refreshAllProgramsList( ); // Populate sorted all programs list
#ifdef _WIN32
            if (cfgOk) loadWallpaper(cfg.wallpaperPath);
#endif

            bool legacyLoaded = false; if (!cfgOk || cfg.windows.empty( )) {
                std::vector<SavedWindow> sw; std::string err; if (DesktopState::Load("desktop.state", sw, err)) {
                    std::lock_guard<std::mutex> lk(g_lock); g_windows.clear( ); g_z.clear( ); g_focus = 0; std::sort(sw.begin( ), sw.end( ), [] (const SavedWindow& a, const SavedWindow& b) { return a.z < b.z; }); for (auto& w : sw) {
                        uint64_t id = s_nextWinId.fetch_add(1); WinInfo wi{ id,w.title,w.x,w.y,w.w,w.h, {}, {}, {}, w.minimized, w.maximized, 0,0,0,0, true, w.snap }; if (wi.maximized) {
#ifdef _WIN32
                            RECT crL{ 0,0,1024,768 };
                            if (g_hwnd) GetClientRect(g_hwnd, &crL);
#else
                            struct { int left; int top; int right; int bottom; } crL{ 0, 0, 1024, 768 };
                            if (g_videoBackend) { crL.right = g_videoBackend->getWidth(); crL.bottom = g_videoBackend->getHeight(); }
#endif
                            int taskbarY = crL.bottom - 40; wi.x = crL.left; wi.y = crL.top; wi.w = crL.right - crL.left; wi.h = taskbarY - crL.top;
                        } g_windows[id] = wi; g_z.push_back(id); if (w.focused && !wi.minimized) g_focus = id;
                    } legacyLoaded = true;
                }
            }
            
#if !defined(_WIN32)
            // On bare-metal, trigger initial render
            g_needsRedraw = true;
            renderToFramebuffer();
#endif
            
            bool running = true; while (running) { pumpEvents( ); ipc::Message m; if (ipc::Bus::pop(kGuiChanIn, m, 30)) { if (m.type == (uint32_t)MsgType::MT_Ping && m.data.size( ) == 3 && std::string(m.data.begin( ), m.data.end( )) == "bye") running = false; else handleMessage(m); } }
            DesktopConfigData outCfg = g_cfg; { std::lock_guard<std::mutex> lk(g_lock); outCfg.windows.clear( ); for (size_t i = 0; i < g_z.size( ); ++i) { uint64_t id = g_z[i]; auto it = g_windows.find(id); if (it == g_windows.end( )) continue; const WinInfo& w = it->second; DesktopWindowRec rec; rec.id = w.id; rec.title = w.title; rec.x = w.x; rec.y = w.y; rec.w = w.w; rec.h = w.h; rec.minimized = w.minimized; rec.maximized = w.maximized; rec.z = (int)i; rec.focused = (g_focus == w.id); rec.snap = w.snapState; outCfg.windows.push_back(rec); } }
            std::string cerr; DesktopConfig::Save("desktop.json", outCfg, cerr); if (!legacyLoaded) { std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for (auto& kv : g_windows) { sw.push_back(SavedWindow{ kv.second.id, kv.second.title, kv.second.x, kv.second.y, kv.second.w, kv.second.h, kv.second.minimized, kv.second.maximized }); } } std::string err; DesktopState::Save("desktop.state", sw, err); }
#ifdef _WIN32
            shutdownWindow( );
#endif
            Logger::write(LogLevel::Info, "Compositor service stopping"); return 0;
        }
        uint64_t Compositor::start( ) { ProcessSpec spec{ "compositor", Compositor::main }; return ProcessTable::spawn(spec, { "compositor" }); }

#ifdef _WIN32
        void Compositor::drawTaskbarSearchBox(HDC dc, int x, int y, int w, int h) {
            // Search box background
            HBRUSH bg = CreateSolidBrush(RGB(50, 52, 62));
            RECT r = { x, y, x + w, y + h };
            FillRect(dc, &r, bg);
            DeleteObject(bg);
            // Border
            HPEN border = CreatePen(PS_SOLID, 1, RGB(75, 78, 90));
            HGDIOBJ oldPen = SelectObject(dc, border);
            HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, x, y, x + w, y + h);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBr);
            DeleteObject(border);
            // Magnifying glass icon (small circle + line)
            int iconX = x + 8;
            int iconY = y + (h / 2);
            HPEN iconPen = CreatePen(PS_SOLID, 1, RGB(140, 145, 160));
            HGDIOBJ oP = SelectObject(dc, iconPen);
            HGDIOBJ oB = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, iconX - 4, iconY - 4, iconX + 4, iconY + 4);
            MoveToEx(dc, iconX + 3, iconY + 3, nullptr);
            LineTo(dc, iconX + 7, iconY + 7);
            SelectObject(dc, oP);
            SelectObject(dc, oB);
            DeleteObject(iconPen);
            // Placeholder text
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(100, 105, 120));
            const char* placeholder = "Search apps...";
            TextOutA(dc, x + 20, y + (h - 14) / 2, placeholder, (int)strlen(placeholder));
        }

        void Compositor::drawSystemTray(HDC dc, RECT cr, int taskbarH) {
            // System tray is drawn to the left of the clock area
            // Calculate clock width first to position tray
            std::time_t now = std::time(nullptr);
            std::tm ltBuf{};
            localtime_s(&ltBuf, &now);
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ltBuf.tm_hour, ltBuf.tm_min);
            SIZE timeSz;
            GetTextExtentPoint32A(dc, timeBuf, (int)strlen(timeBuf), &timeSz);
            int clockW = timeSz.cx + 32; // approximate clock area width
            int trayX = cr.right - clockW - SystemTray::Width( ) - 16;
            int trayY = cr.bottom - taskbarH;
            SystemTray::Draw(dc, trayX, trayY, taskbarH);
        }

        void Compositor::drawTaskbarTooltip(HDC dc, int x, int y, const char* text) {
            if (!text || !text[0]) return;
            SIZE sz;
            GetTextExtentPoint32A(dc, text, (int)strlen(text), &sz);
            int pad = 6;
            int tipW = sz.cx + pad * 2;
            int tipH = sz.cy + pad * 2;
            // Position above the given point
            int tipX = x - tipW / 2;
            int tipY = y - tipH - 4;
            if (tipX < 0) tipX = 0;
            if (tipY < 0) tipY = 0;
            RECT tipR = { tipX, tipY, tipX + tipW, tipY + tipH };
            HBRUSH bg = CreateSolidBrush(RGB(55, 55, 65));
            FillRect(dc, &tipR, bg);
            DeleteObject(bg);
            HPEN border = CreatePen(PS_SOLID, 1, RGB(100, 105, 120));
            HGDIOBJ oldPen = SelectObject(dc, border);
            HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, tipR.left, tipR.top, tipR.right, tipR.bottom);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBr);
            DeleteObject(border);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(220, 220, 230));
            TextOutA(dc, tipX + pad, tipY + pad, text, (int)strlen(text));
        }
#endif

#if !defined(_WIN32)
        // ==================================================================
        // Bare-metal framebuffer rendering
        // ==================================================================
        
        // Helper: fill a rectangle in the framebuffer
        static void fbFillRect(uint32_t* pixels, int pitch, int bufW, int bufH,
                               int x, int y, int w, int h, uint32_t color) {
            if (!pixels) return;
            int stride = pitch / 4;
            for (int row = y; row < y + h && row < bufH; ++row) {
                if (row < 0) continue;
                for (int col = x; col < x + w && col < bufW; ++col) {
                    if (col < 0) continue;
                    pixels[row * stride + col] = color;
                }
            }
        }
        
        // Helper: draw a border rectangle
        static void fbDrawRect(uint32_t* pixels, int pitch, int bufW, int bufH,
                               int x, int y, int w, int h, uint32_t color) {
            if (!pixels) return;
            int stride = pitch / 4;
            // Top and bottom edges
            for (int col = x; col < x + w && col < bufW; ++col) {
                if (col < 0) continue;
                if (y >= 0 && y < bufH) pixels[y * stride + col] = color;
                if (y + h - 1 >= 0 && y + h - 1 < bufH) pixels[(y + h - 1) * stride + col] = color;
            }
            // Left and right edges
            for (int row = y; row < y + h && row < bufH; ++row) {
                if (row < 0) continue;
                if (x >= 0 && x < bufW) pixels[row * stride + x] = color;
                if (x + w - 1 >= 0 && x + w - 1 < bufW) pixels[row * stride + x + w - 1] = color;
            }
        }
        
        void Compositor::renderToFramebuffer() {
            if (!g_videoBackend) {
                Logger::write(LogLevel::Error, "renderToFramebuffer: no video backend!");
                return;
            }
            
            uint32_t* pixels = g_videoBackend->getPixels();
            if (!pixels) {
                Logger::write(LogLevel::Error, "renderToFramebuffer: no pixel buffer!");
                return;
            }
            
            int fbW = g_videoBackend->getWidth();
            int fbH = g_videoBackend->getHeight();
            int pitch = g_videoBackend->getPitch();
            
            // Log window count for debugging
            {
                std::lock_guard<std::mutex> lk(g_lock);
                Logger::write(LogLevel::Info, std::string("renderToFramebuffer: ") + 
                    std::to_string(g_windows.size()) + " windows, " +
                    std::to_string(fbW) + "x" + std::to_string(fbH));
            }
            
            const int taskbarH = 40;
            const int titleBarH = 28;
            
            // Clear background with gradient (dark blue)
            for (int y = 0; y < fbH - taskbarH; ++y) {
                float t = (float)y / (float)(fbH - taskbarH);
                uint8_t r = (uint8_t)(20 + t * 15);
                uint8_t g = (uint8_t)(25 + t * 20);
                uint8_t b = (uint8_t)(40 + t * 30);
                uint32_t color = (r << 16) | (g << 8) | b;
                for (int x = 0; x < fbW; ++x) {
                    pixels[y * (pitch/4) + x] = color;
                }
            }
            
            // Draw branding text
            const char* brand = "guideXOS Server - UEFI Mode";
            BitmapFont::DrawStringToBufferScaled(pixels, pitch, fbW, fbH,
                fbW / 2 - BitmapFont::MeasureWidth(brand) * 2 / 2,
                fbH / 2 - 50, brand, -1, 0x00404040, 2);
            BitmapFont::DrawStringToBufferScaled(pixels, pitch, fbW, fbH,
                fbW / 2 - BitmapFont::MeasureWidth(brand) * 2 / 2 - 1,
                fbH / 2 - 51, brand, -1, 0x00808090, 2);
            
            // Draw desktop icons
            const int iconW = 56;
            const int iconH = 56;
            const int cellW = iconW + 28;
            const int cellH = iconH + 38;
            int iconIdx = 0;
            for (const auto& item : g_items) {
                int ix = item.ix >= 0 ? item.ix : 20 + (iconIdx % 8) * cellW;
                int iy = item.iy >= 0 ? item.iy : 20 + (iconIdx / 8) * cellH;
                
                // Icon background
                uint32_t iconColor = 0x005A6478; // default gray-blue
                if (item.label == "Notepad" || item.label == "Console") iconColor = 0x0078B450;
                else if (item.label == "Calculator" || item.label == "Clock") iconColor = 0x00468CC8;
                else if (item.label == "TaskManager") iconColor = 0x00B44646;
                
                int iconX = ix + (cellW - iconW) / 2;
                int iconY = iy + 6;
                fbFillRect(pixels, pitch, fbW, fbH, iconX, iconY, iconW, iconH, iconColor);
                fbDrawRect(pixels, pitch, fbW, fbH, iconX, iconY, iconW, iconH, 0x00B4B4C8);
                
                // Icon label
                const char* label = item.label.c_str();
                int labelW = BitmapFont::MeasureWidth(label);
                BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                    ix + (cellW - labelW) / 2, iconY + iconH + 8, label, -1, 0x00E6E6F0);
                
                if (item.pinned) {
                    BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                        iconX + iconW - 6, iconY + 2, "*", 1, 0x00FFC83C);
                }
                
                iconIdx++;
            }
            
            // Draw windows in Z-order
            {
                std::lock_guard<std::mutex> lk(g_lock);
                for (uint64_t wid : g_z) {
                    auto it = g_windows.find(wid);
                    if (it == g_windows.end()) continue;
                    const WinInfo& w = it->second;
                    if (w.minimized || !w.visible) continue;
                    
                    bool isFocused = (w.id == g_focus);
                    
                    // Window shadow
                    fbFillRect(pixels, pitch, fbW, fbH, w.x + 4, w.y + 4, w.w, w.h, 0x00202020);
                    
                    // Window background
                    fbFillRect(pixels, pitch, fbW, fbH, w.x, w.y, w.w, w.h, 0x00303840);
                    
                    // Title bar
                    uint32_t titleColor = isFocused ? 0x00466496 : 0x00505058;
                    fbFillRect(pixels, pitch, fbW, fbH, w.x, w.y, w.w, titleBarH, titleColor);
                    
                    // Title text
                    BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                        w.x + 10, w.y + (titleBarH - 7) / 2, w.title.c_str(), -1, 0x00F0F0F0);
                    
                    // Close button (X)
                    int btnSize = titleBarH - 8;
                    int closeX = w.x + w.w - btnSize - 4;
                    int closeY = w.y + 4;
                    fbFillRect(pixels, pitch, fbW, fbH, closeX, closeY, btnSize, btnSize, 0x00C83232);
                    BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                        closeX + (btnSize - 5) / 2, closeY + (btnSize - 7) / 2, "X", 1, 0x00FFFFFF);
                    
                    // Window border
                    uint32_t borderColor = isFocused ? 0x006496C8 : 0x00606068;
                    fbDrawRect(pixels, pitch, fbW, fbH, w.x, w.y, w.w, w.h, borderColor);
                    
                    // Draw window content (widgets, text)
                    int contentY = w.y + titleBarH;
                    for (const auto& wd : w.widgets) {
                        int wx = w.x + wd.x;
                        int wy = contentY + wd.y;
                        uint32_t wColor = wd.pressed ? 0x00285090 : (wd.hover ? 0x00465A78 : 0x005A5A64);
                        fbFillRect(pixels, pitch, fbW, fbH, wx, wy, wd.w, wd.h, wColor);
                        fbDrawRect(pixels, pitch, fbW, fbH, wx, wy, wd.w, wd.h, 0x00FFFFFF);
                        BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                            wx + 6, wy + (wd.h - 7) / 2, wd.text.c_str(), -1, 0x00F0F0F0);
                    }
                    
                    // Draw text lines
                    int ty = contentY + 8;
                    for (const auto& tx : w.texts) {
                        BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                            w.x + 8, ty, tx.c_str(), -1, 0x00DCDCDC);
                        ty += 10;
                    }
                    
                    // Tombstone overlay
                    if (w.tombstoned) {
                        fbFillRect(pixels, pitch, fbW, fbH, w.x, w.y, w.w, w.h, 0x40202020);
                        const char* tomb = "TOMBSTONED";
                        int tw = BitmapFont::MeasureWidth(tomb) * 2;
                        BitmapFont::DrawStringToBufferScaled(pixels, pitch, fbW, fbH,
                            w.x + (w.w - tw) / 2, w.y + w.h / 2 - 7, tomb, -1, 0x00FF8080, 2);
                    }
                }
            }
            
            // Draw taskbar
            for (int y = fbH - taskbarH; y < fbH; ++y) {
                float t = (float)(y - (fbH - taskbarH)) / (float)taskbarH;
                uint8_t gray = (uint8_t)(30 + t * 10);
                uint32_t color = (gray << 16) | (gray << 8) | (gray + 8);
                for (int x = 0; x < fbW; ++x) {
                    pixels[y * (pitch/4) + x] = color;
                }
            }
            
            // Taskbar top edge
            for (int x = 0; x < fbW; ++x) {
                pixels[(fbH - taskbarH) * (pitch/4) + x] = 0x003C4150;
            }
            
            // Start button
            fbFillRect(pixels, pitch, fbW, fbH, 8, fbH - taskbarH + 6, 32, taskbarH - 12, 0x00374B64);
            fbDrawRect(pixels, pitch, fbW, fbH, 8, fbH - taskbarH + 6, 32, taskbarH - 12, 0x00FFFFFF);
            BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH, 12, fbH - taskbarH + 14, "S", 1, 0x00FFFFFF);
            
            // Taskbar window buttons
            int btnX = 50;
            {
                std::lock_guard<std::mutex> lk(g_lock);
                for (uint64_t wid : g_z) {
                    auto it = g_windows.find(wid);
                    if (it == g_windows.end()) continue;
                    const WinInfo& w = it->second;
                    
                    int labelLen = (int)w.title.size();
                    if (labelLen > 15) labelLen = 15;
                    int bw = labelLen * 6 + 20;
                    if (bw > 150) bw = 150;
                    
                    uint32_t btnColor = (wid == g_focus) ? 0x00466496 : 
                                        (w.minimized ? 0x00282832 : 
                                        (w.tombstoned ? 0x00554123 : 0x00373A46));
                    fbFillRect(pixels, pitch, fbW, fbH, btnX, fbH - taskbarH + 6, bw, taskbarH - 12, btnColor);
                    
                    // Focus indicator
                    if (wid == g_focus) {
                        fbFillRect(pixels, pitch, fbW, fbH, btnX + 2, fbH - 9, bw - 4, 2, 0x0064A0F0);
                    }
                    
                    BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                        btnX + 8, fbH - taskbarH + 14, w.title.c_str(), labelLen, 0x00E6E6F0);
                    
                    btnX += bw + 4;
                }
            }
            
            // Clock
            std::time_t now = std::time(nullptr);
            std::tm ltBuf{};
            std::tm* tmp = std::localtime(&now);
            if (tmp) ltBuf = *tmp;
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ltBuf.tm_hour, ltBuf.tm_min);
            int clockX = fbW - 60;
            BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                clockX, fbH - taskbarH + 10, timeBuf, -1, 0x00C8C8D2);
            char dateBuf[16];
            std::snprintf(dateBuf, sizeof(dateBuf), "%d/%d", ltBuf.tm_mon + 1, ltBuf.tm_mday);
            BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                clockX, fbH - taskbarH + 22, dateBuf, -1, 0x009696A5);
            
            // Present to hardware framebuffer
            g_videoBackend->present();
            g_needsRedraw = false;
        }
#endif
    }
} // namespace gxos::gui
