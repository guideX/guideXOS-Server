#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>
#include <atomic>
#include <vector>
#include <mutex>
#include <set>
#include "ipc_bus.h"
#include "gui_protocol.h"
#include "process.h"
#include "logger.h"
#include "desktop_config.h"
#include "desktop_service.h"
#include "vnc_server.h"
#include "system_tray.h"
#include "desktop_wallpaper.h"
#include "bitmap_font.h"
#include "video_backend.h"
#include "window_effects.h"
#include "ui_settings.h"
#include "image.h"

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gxos { namespace gui {
    struct DrawRectItem { int x; int y; int w; int h; uint8_t r; uint8_t g; uint8_t b; };
    struct DrawImageItem { int x; int y; int w; int h; std::string path; ImagePtr image; };
    struct DrawTextItem { int x; int y; std::string text; };
    enum class WidgetType { Button=1 };
    struct Widget { WidgetType type; int id; int x; int y; int w; int h; std::string text; bool hover=false; bool pressed=false; };
    struct WinInfo { 
        uint64_t id; 
        std::string title; 
        int x; int y; int w; int h; 
        std::vector<std::string> texts; 
        std::vector<DrawTextItem> positionedTexts;
        std::vector<DrawRectItem> rects; 
        std::vector<DrawImageItem> images;
        std::vector<Widget> widgets; 
        bool minimized{false}; 
        bool maximized{false}; 
        int prevX{0}; int prevY{0}; int prevW{0}; int prevH{0}; 
        bool dirty{true}; 
        int snapState{0}; 
        bool tombstoned{false}; 
        bool modal{false};
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        HBITMAP taskbarIcon{nullptr}; 
#endif
        uint64_t ownerPid{0};
        // Titlebar button hover/pressed state
        bool titleBtnCloseHover{false}; bool titleBtnClosePressed{false};
        bool titleBtnMaxHover{false}; bool titleBtnMaxPressed{false};
        bool titleBtnMinHover{false}; bool titleBtnMinPressed{false};
        bool titleBtnTombHover{false}; bool titleBtnTombPressed{false};
        // Animation state - ported from guideXOS.Legacy Window.cs
        WindowAnimState animState{};
        bool visible{true};
    };
    struct DesktopItem { std::string label; std::string action; bool pinned{false}; bool selected{false}; int ix{-1}; int iy{-1}; };
    struct AppModelDemoWindowState { uint64_t windowId{0}; std::vector<RegisteredDesktopApp> apps; int selectedIndex{0}; std::string status; };

    class Compositor {
    public:
        static uint64_t start();
        static void requestDesktopRefresh();
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        static HWND g_hwnd; // expose for helper drawing
#endif
        static std::vector<DesktopItem> g_items; // expose for icon renderer

        // Active video backend (GDI on Windows, kernel FB on bare-metal).
        // VNC reads from this regardless of which backend is active.
        static VideoBackend* g_videoBackend;
    private:
        static int main(int argc, char** argv);
        static void handleMessage(const ipc::Message& m);
        static void drawAll();
        static void pumpEvents();
        static void invalidate(uint64_t winId);
        static void sendFocus(uint64_t winId);
        static void handleMouse(int mx, int my, bool down, bool up);
        static std::string packMousePayloadForTarget(int x, int y, int button, const std::string& action, uint64_t ownerPid, uint64_t windowId = 0);
        static void emitWidgetEvt(uint64_t winId, int wid, const std::string& evt, const std::string& value);
        static WinInfo* hitWindowAt(int mx, int my);
        static bool isDialogTitle(const std::string& title);
        static bool blockInputBehindModal(int mx, int my);
        static uint64_t inputOwnerPid();
        static void launchAction(const std::string& act);
        static void openAppModelDemoViewerWindow();
        static void updateAppModelDemoViewerWindow(const std::string& status = "");
        static bool handleAppModelDemoKey(int key);
        static void pinAction(const std::string& act);
        static void unpinAction(const std::string& act);
        static void saveDesktopConfig();
        static void addRecent(const std::string& act);
        static void refreshDesktopItems();
        static void refreshAllProgramsList();
        static void ClearDesktopIconSelection();
        static void SelectDesktopIcon(int index, bool additive);
        static void ToggleDesktopIconSelection(int index);
        static void SelectDesktopIconRange(int startIndex, int endIndex);
        static std::vector<int> GetSelectedDesktopIconIndices();
        static void loadWallpaper(const std::string& idOrPath);
        static void freeWallpaper();
        static std::string g_wallpaperPath;
        static std::string g_wallpaperId;
        static ImagePtr g_wallpaperImage;
        static uint32_t g_gradientTopColor;
        static uint32_t g_gradientBottomColor;
        static uint32_t g_gradientAccentColor;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        static int HitTestDesktopIcon(int mouseX, int mouseY);
        static RECT GetDesktopIconBounds(int index);
        static void SelectIconsInRectangle(const RECT& selectionRect, bool additive);
        static bool IsCtrlDown();
        static bool IsShiftDown();
#endif
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        static uint64_t hitTestTaskbarButton(int mx, int my, RECT cr, int taskbarH);
        static void initWindow();
        static void shutdownWindow();
        static void requestRepaint();
        // Windows UI state (start menu, wallpaper, etc.)
        static HBITMAP g_startBtnBmp;
        static HBITMAP g_wallpaperBmp;
        static int g_wallpaperW;
        static int g_wallpaperH;
        static bool g_startMenuVisible;
        static RECT g_startMenuRect;
        // Functions implemented in compositor.cpp
        static void drawDesktopIcons(HDC dc, RECT cr);
        static void drawTaskbarSearchBox(HDC dc, int x, int y, int w, int h);
        static void drawSystemTray(HDC dc, RECT cr, int taskbarH);
        static void drawTaskbarTooltip(HDC dc, int x, int y, const char* text);
        static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l);
#else
        // Bare-metal rendering functions
        static void renderToFramebuffer();
        static bool g_needsRedraw;
#endif
        static std::atomic<uint64_t> s_nextWinId;
        static std::unordered_map<uint64_t, WinInfo> g_windows; static std::vector<uint64_t> g_z; static std::mutex g_lock; static uint64_t g_focus;
        static uint64_t g_modalWindow;
        static bool g_dragActive; static int g_dragOffX; static int g_dragOffY; static uint64_t g_dragWin; static int g_dragStartX; static int g_dragStartY;
        static bool g_dragPending; static uint64_t g_dragPendingWin;
        static bool g_resizeActive; static int g_resizeStartW; static int g_resizeStartH; static int g_resizeStartMX; static int g_resizeStartMY; static uint64_t g_resizeWin;
        static bool g_resizePreviewActive; static int g_resizePreviewW; static int g_resizePreviewH;
        static bool g_snapPreviewActive;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        static RECT g_snapPreviewRect;
#else
        struct SnapRect { int l; int t; int r; int b; }; static SnapRect g_snapPreviewRect;
#endif
        static bool g_showDesktopActive; static std::vector<uint64_t> g_showDesktopMinimized; static uint64_t g_lastClickTicks; static uint64_t g_lastClickWin;
        static bool g_altTabOverlayActive; static uint64_t g_altTabOverlayTicks; static int g_altTabCycleIndex;
        static bool g_taskbarCycleActive; static int g_taskbarCycleIndex; static bool g_keyboardMoveActive; static bool g_keyboardSizeActive; static int g_kbOrigX; static int g_kbOrigY; static int g_kbOrigW; static int g_kbOrigH;
        static DesktopConfigData g_cfg; static uint64_t g_lastItemClickTicks; static int g_lastItemIndex;
        static AppModelDemoWindowState g_appModelDemo;
        static std::set<int> g_selectedDesktopIconIndices; static int g_lastSelectedDesktopIconIndex;
        static bool g_iconDragActive; static int g_iconDragIndex; static int g_iconDragOffX; static int g_iconDragOffY; static int g_iconDragStartX; static int g_iconDragStartY; static bool g_iconDragPending;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        static bool g_iconSelectionDragPending; static bool g_iconSelectionDragActive; static int g_iconSelectionStartX; static int g_iconSelectionStartY; static int g_iconSelectionCurrentX; static int g_iconSelectionCurrentY; static bool g_iconSelectionAdditive;
#endif
        // Start menu keyboard/selection state
        static int g_startMenuSel; static int g_startMenuScroll;
        static bool g_startMenuAllProgs; // "All Programs" view
        static std::vector<std::string> g_startMenuAllProgsSorted; // Sorted app names
        // Taskbar menu state
        static bool g_taskbarMenuVisible;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        static RECT g_taskbarMenuRect;
#else
        static SnapRect g_taskbarMenuRect;
#endif
        static int g_taskbarMenuSel; // selected item index

        // Video backend helpers
        static void initVideoBackend();
        static void feedVncFromBackend();
    };
} }
