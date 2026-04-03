#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>
#include <atomic>
#include <vector>
#include <mutex>
#include "ipc_bus.h"
#include "gui_protocol.h"
#include "process.h"
#include "logger.h"
#include "desktop_config.h"
#include "vnc_server.h"
#include "system_tray.h"
#include "desktop_wallpaper.h"
#include "bitmap_font.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gxos { namespace gui {
    struct DrawRectItem { int x; int y; int w; int h; uint8_t r; uint8_t g; uint8_t b; };
    enum class WidgetType { Button=1 };
    struct Widget { WidgetType type; int id; int x; int y; int w; int h; std::string text; bool hover=false; bool pressed=false; };
    struct WinInfo { uint64_t id; std::string title; int x; int y; int w; int h; std::vector<std::string> texts; std::vector<DrawRectItem> rects; std::vector<Widget> widgets; bool minimized{false}; bool maximized{false}; int prevX{0}; int prevY{0}; int prevW{0}; int prevH{0}; bool dirty{true}; int snapState{0}; bool tombstoned{false}; HBITMAP taskbarIcon{nullptr};
        // Titlebar button hover/pressed state
        bool titleBtnCloseHover{false}; bool titleBtnClosePressed{false};
        bool titleBtnMaxHover{false}; bool titleBtnMaxPressed{false};
        bool titleBtnMinHover{false}; bool titleBtnMinPressed{false}; };
    struct DesktopItem { std::string label; std::string action; bool pinned{false}; bool selected{false}; int ix{-1}; int iy{-1}; };

    class Compositor {
    public:
        static uint64_t start();
#ifdef _WIN32
        static HWND g_hwnd; // expose for helper drawing
#endif
        static std::vector<DesktopItem> g_items; // expose for icon renderer
    private:
        static int main(int argc, char** argv);
        static void handleMessage(const ipc::Message& m);
        static void drawAll();
        static void pumpEvents();
        static void invalidate(uint64_t winId);
        static void sendFocus(uint64_t winId);
        static void handleMouse(int mx, int my, bool down, bool up);
        static void emitWidgetEvt(uint64_t winId, int wid, const std::string& evt, const std::string& value);
        static WinInfo* hitWindowAt(int mx, int my);
        static void launchAction(const std::string& act);
        static void pinAction(const std::string& act);
        static void unpinAction(const std::string& act);
        static void saveDesktopConfig();
        static void addRecent(const std::string& act);
        static void refreshDesktopItems();
        static void refreshAllProgramsList();
#ifdef _WIN32
        static uint64_t hitTestTaskbarButton(int mx, int my, RECT cr, int taskbarH);
        static void initWindow();
        static void shutdownWindow();
        static void requestRepaint();
        // Windows UI state (start menu, wallpaper, etc.)
        static HBITMAP g_startBtnBmp;
        static HBITMAP g_wallpaperBmp;
        static int g_wallpaperW;
        static int g_wallpaperH;
        static std::string g_wallpaperPath;
        static bool g_startMenuVisible;
        static RECT g_startMenuRect;
        // Functions implemented in compositor.cpp
        static void loadWallpaper(const std::string& path);
        static void freeWallpaper();
        static void drawDesktopIcons(HDC dc, RECT cr);
        static void drawTaskbarSearchBox(HDC dc, int x, int y, int w, int h);
        static void drawSystemTray(HDC dc, RECT cr, int taskbarH);
        static void drawTaskbarTooltip(HDC dc, int x, int y, const char* text);
        static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l);
#endif
        static std::atomic<uint64_t> s_nextWinId;
        static std::unordered_map<uint64_t, WinInfo> g_windows; static std::vector<uint64_t> g_z; static std::mutex g_lock; static uint64_t g_focus;
        static bool g_dragActive; static int g_dragOffX; static int g_dragOffY; static uint64_t g_dragWin; static int g_dragStartX; static int g_dragStartY;
        static bool g_dragPending; static uint64_t g_dragPendingWin;
        static bool g_resizeActive; static int g_resizeStartW; static int g_resizeStartH; static int g_resizeStartMX; static int g_resizeStartMY; static uint64_t g_resizeWin;
        static bool g_resizePreviewActive; static int g_resizePreviewW; static int g_resizePreviewH;
        static bool g_snapPreviewActive;
#ifdef _WIN32
        static RECT g_snapPreviewRect;
#else
        struct SnapRect { int l; int t; int r; int b; }; static SnapRect g_snapPreviewRect;
#endif
        static bool g_showDesktopActive; static std::vector<uint64_t> g_showDesktopMinimized; static uint64_t g_lastClickTicks; static uint64_t g_lastClickWin;
        static bool g_altTabOverlayActive; static uint64_t g_altTabOverlayTicks; static int g_altTabCycleIndex;
        static bool g_taskbarCycleActive; static int g_taskbarCycleIndex; static bool g_keyboardMoveActive; static bool g_keyboardSizeActive; static int g_kbOrigX; static int g_kbOrigY; static int g_kbOrigW; static int g_kbOrigH;
        static DesktopConfigData g_cfg; static uint64_t g_lastItemClickTicks; static int g_lastItemIndex;
        static bool g_iconDragActive; static int g_iconDragIndex; static int g_iconDragOffX; static int g_iconDragOffY; static int g_iconDragStartX; static int g_iconDragStartY; static bool g_iconDragPending;
        // Start menu keyboard/selection state
        static int g_startMenuSel; static int g_startMenuScroll;
        static bool g_startMenuAllProgs; // "All Programs" view
        static std::vector<std::string> g_startMenuAllProgsSorted; // Sorted app names
        // Taskbar menu state
        static bool g_taskbarMenuVisible;
        static RECT g_taskbarMenuRect;
        static int g_taskbarMenuSel; // selected item index
    };
} }
