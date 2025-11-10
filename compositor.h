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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gxos { namespace gui {
    struct DrawRectItem { int x; int y; int w; int h; uint8_t r; uint8_t g; uint8_t b; };
    enum class WidgetType { Button=1 };
    struct Widget { WidgetType type; int id; int x; int y; int w; int h; std::string text; bool hover=false; bool pressed=false; };
    // SnapState: 0=none,1=left,2=right,3=tl,4=tr,5=bl,6=br
    struct WinInfo { uint64_t id; std::string title; int x; int y; int w; int h; std::vector<std::string> texts; std::vector<DrawRectItem> rects; std::vector<Widget> widgets; bool minimized{false}; bool maximized{false}; int prevX{0}; int prevY{0}; int prevW{0}; int prevH{0}; bool dirty{true}; int snapState{0}; bool tombstoned{false}; HBITMAP taskbarIcon{nullptr}; };

    class Compositor {
    public:
        static uint64_t start();
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
#ifdef _WIN32
        static uint64_t hitTestTaskbarButton(int mx, int my, RECT cr, int taskbarH);
        static void initWindow();
        static void shutdownWindow();
        static void requestRepaint();
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        static HWND g_hwnd;
        // Start button bitmap (optional)
        static HBITMAP g_startBtnBmp;
        // Monitors list for snapping (updated on each paint or resize)
        static std::vector<RECT> g_monitors;
#endif
        static std::atomic<uint64_t> s_nextWinId;
        static std::unordered_map<uint64_t, WinInfo> g_windows;
        static std::vector<uint64_t> g_z; // front-most at back
        static std::mutex g_lock;
        static uint64_t g_focus;
        // Drag state
        static bool g_dragActive; static int g_dragOffX; static int g_dragOffY; static uint64_t g_dragWin; static int g_dragStartX; static int g_dragStartY; // for threshold
        // Resize state
        static bool g_resizeActive; static int g_resizeStartW; static int g_resizeStartH; static int g_resizeStartMX; static int g_resizeStartMY; static uint64_t g_resizeWin;
        // Resize preview (rubber-band)
        static bool g_resizePreviewActive; static int g_resizePreviewW; static int g_resizePreviewH;
        // Snap preview highlight
        static bool g_snapPreviewActive; 
#ifdef _WIN32
        static RECT g_snapPreviewRect;
#else
        struct SnapRect { int l; int t; int r; int b; }; static SnapRect g_snapPreviewRect; 
#endif
        // Show desktop state
        static bool g_showDesktopActive; static std::vector<uint64_t> g_showDesktopMinimized;
        // Double-click tracking
        static uint64_t g_lastClickTicks; static uint64_t g_lastClickWin;
        // Alt+Tab overlay
        static bool g_altTabOverlayActive; static uint64_t g_altTabOverlayTicks;
        // Taskbar cycling (Win+T)
        static bool g_taskbarCycleActive; static int g_taskbarCycleIndex;
        // Keyboard move/size after system menu
        static bool g_keyboardMoveActive; static bool g_keyboardSizeActive; static int g_kbOrigX; static int g_kbOrigY; static int g_kbOrigW; static int g_kbOrigH;
    };
} }
