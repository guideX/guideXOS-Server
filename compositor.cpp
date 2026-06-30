#include "compositor.h"
#include "compositor.h"
#include "allocator.h"
#include "built_in_app_metadata.h"
#include "desktop_state.h"
#include "desktop_service.h"
#include "desktop_folder.h"
#include "file_icon_provider.h"
#include "file_explorer.h"
#include "desktop_theme.h"
#include "shutdown_dialog.h"
#include "icons.h"
#include "right_click_menu.h"
#include "notification_manager.h"
#include "system_tray.h"
#include "desktop_wallpaper.h"
#include "native_app_process_table.h"
#include "native_elf_executor.h"
#include "bitmap_font.h"
#include "kernel/core/include/kernel/system_font.h"
#include "window_renderer.h"
#include "special_effects.h"
#include "window_animator.h"
#include "focus_indicator.h"
#include "image_renderer.h"
#include "kernel/core/include/kernel/image_adapter.h"
#include "icon_theme_manager.h"
#include "wallpaper_registry.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <iomanip>
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
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
        static constexpr int kTaskbarSize = 40;
        static std::unordered_map<std::string, ImageBitmap> g_uiImageCache;

        static ImageBitmap loadCachedUiImage(const std::string& path) {
            auto cached = g_uiImageCache.find(path);
            if (cached != g_uiImageCache.end()) return cached->second;
            ImageBitmap image = ImageAdapter::LoadFromFile(path);
            if (image.status == ImageLoadStatus::Ok) g_uiImageCache.emplace(path, image);
            return image;
        }

        static const ImageBitmap& displayedImage(const DrawImageItem& item) {
            if (item.frames.empty()) return item.image;
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            const auto tick = std::chrono::duration_cast<std::chrono::milliseconds>(now).count() / 100;
            return item.frames[static_cast<size_t>(tick) % item.frames.size()];
        }

        static bool isCalculatorWindow(const WinInfo& winfo) {
            return winfo.title == "Calculator";
        }

        static bool isCalculatorOperatorButton(const Widget& widget) {
            return widget.text == "+" || widget.text == "-" || widget.text == "*" || widget.text == "/";
        }

        static bool isCalculatorFunctionButton(const Widget& widget) {
            return widget.text == "C" || widget.text == "CE";
        }

        static bool isCalculatorEqualsButton(const Widget& widget) {
            return widget.text == "=";
        }

        static uint32_t calculatorClassicWidgetFillColor(const Widget& widget) {
            return widget.pressed ? 0x00285090u : (widget.hover ? 0x00465A78u : 0x005A5A64u);
        }

        static uint32_t calculatorClassicWidgetBorderColor(const Widget&) {
            return 0x00FFFFFFu;
        }

        static uint32_t calculatorClassicWidgetTextColor(const Widget&) {
            return 0x00F0F0F0u;
        }

        static uint32_t calculatorSciFiWidgetFillColor(const DesktopTheme& theme, const Widget& widget) {
            uint32_t base = WindowRenderer::BlendThemeColor(theme.windowBackground, theme.taskbarBackground, 18);
            if (isCalculatorOperatorButton(widget)) {
                base = WindowRenderer::BlendThemeColor(theme.windowBorder, theme.accent, 18);
            } else if (isCalculatorEqualsButton(widget)) {
                base = WindowRenderer::BlendThemeColor(theme.accent, theme.windowBackground, 18);
            } else if (isCalculatorFunctionButton(widget)) {
                base = WindowRenderer::BlendThemeColor(theme.taskbarBackground, theme.windowBorder, 16);
            }

            if (widget.pressed) {
                return WindowRenderer::BlendThemeColor(base, theme.accent, 22);
            }
            if (widget.hover) {
                return WindowRenderer::BlendThemeColor(base, theme.mutedAccent, 14);
            }
            return base;
        }

        static uint32_t calculatorSciFiWidgetBorderColor(const DesktopTheme& theme, const Widget& widget) {
            uint32_t border = WindowRenderer::BlendThemeColor(theme.windowBorder, theme.taskbarBorder, 20);
            if (isCalculatorOperatorButton(widget) || isCalculatorEqualsButton(widget)) {
                border = WindowRenderer::BlendThemeColor(theme.windowBorder, theme.accent, 34);
            } else if (isCalculatorFunctionButton(widget)) {
                border = WindowRenderer::BlendThemeColor(theme.windowBorder, theme.mutedAccent, 30);
            }

            if (widget.pressed) {
                return WindowRenderer::BlendThemeColor(border, theme.accent, 18);
            }
            if (widget.hover) {
                return WindowRenderer::BlendThemeColor(border, theme.titleBarText, 12);
            }
            return border;
        }

        static uint32_t calculatorSciFiWidgetTextColor(const DesktopTheme& theme, const Widget&) {
            return theme.titleBarText;
        }

        static uint32_t calculatorWidgetFillColor(const WinInfo& winfo, const Widget& widget, const DesktopTheme& theme) {
            if (!isCalculatorWindow(winfo) || theme.id != DesktopThemeId::SciFi) {
                return calculatorClassicWidgetFillColor(widget);
            }
            return calculatorSciFiWidgetFillColor(theme, widget);
        }

        static uint32_t calculatorWidgetBorderColor(const WinInfo& winfo, const Widget& widget, const DesktopTheme& theme) {
            if (!isCalculatorWindow(winfo) || theme.id != DesktopThemeId::SciFi) {
                return calculatorClassicWidgetBorderColor(widget);
            }
            return calculatorSciFiWidgetBorderColor(theme, widget);
        }

        static uint32_t calculatorWidgetTextColor(const WinInfo& winfo, const Widget& widget, const DesktopTheme& theme) {
            if (!isCalculatorWindow(winfo) || theme.id != DesktopThemeId::SciFi) {
                return calculatorClassicWidgetTextColor(widget);
            }
            return calculatorSciFiWidgetTextColor(theme, widget);
        }

        enum class TaskbarPosition {
            Bottom,
            Top,
            Left,
            Right
        };

        static TaskbarPosition g_taskbarPosition = TaskbarPosition::Bottom;

        static std::string taskbarPositionName(TaskbarPosition position) {
            switch (position) {
            case TaskbarPosition::Top: return "top";
            case TaskbarPosition::Left: return "left";
            case TaskbarPosition::Right: return "right";
            case TaskbarPosition::Bottom:
            default: return "bottom";
            }
        }

        static TaskbarPosition parseTaskbarPosition(const std::string& value) {
            std::string lower;
            lower.reserve(value.size());
            for (char c : value) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lower == "top") return TaskbarPosition::Top;
            if (lower == "left") return TaskbarPosition::Left;
            if (lower == "right") return TaskbarPosition::Right;
            return TaskbarPosition::Bottom;
        }

        static void syncDesktopThemeFromConfig(DesktopConfigData& cfg) {
            DesktopThemeId themeId = DesktopThemeId::Classic;
            TryParseDesktopThemeId(cfg.desktopThemeId.c_str(), &themeId);
            cfg.desktopThemeId = DesktopThemeIdToString(themeId);
            SetCurrentDesktopTheme(themeId);
        }

        struct WorkRect {
            int left{0};
            int top{0};
            int right{0};
            int bottom{0};
        };

        static WorkRect taskbarRectForBounds(int width, int height) {
            switch (g_taskbarPosition) {
            case TaskbarPosition::Top:
                return {0, 0, width, std::min(height, kTaskbarSize)};
            case TaskbarPosition::Left:
                return {0, 0, std::min(width, kTaskbarSize), height};
            case TaskbarPosition::Right:
                return {std::max(0, width - kTaskbarSize), 0, width, height};
            case TaskbarPosition::Bottom:
            default:
                return {0, std::max(0, height - kTaskbarSize), width, height};
            }
        }

        static WorkRect desktopWorkAreaForBounds(int width, int height) {
            switch (g_taskbarPosition) {
            case TaskbarPosition::Top:
                return {0, std::min(height, kTaskbarSize), width, height};
            case TaskbarPosition::Left:
                return {std::min(width, kTaskbarSize), 0, width, height};
            case TaskbarPosition::Right:
                return {0, 0, std::max(0, width - kTaskbarSize), height};
            case TaskbarPosition::Bottom:
            default:
                return {0, 0, width, std::max(0, height - kTaskbarSize)};
            }
        }

        // Static member definitions
        std::atomic<uint64_t> Compositor::s_nextWinId{ 1000 };
        std::unordered_map<uint64_t, WinInfo> Compositor::g_windows; std::vector<uint64_t> Compositor::g_z; std::mutex Compositor::g_lock; uint64_t Compositor::g_focus = 0;
        uint64_t Compositor::g_modalWindow = 0;
        bool Compositor::g_dragActive = false; int Compositor::g_dragOffX = 0; int Compositor::g_dragOffY = 0; uint64_t Compositor::g_dragWin = 0; int Compositor::g_dragStartX = 0; int Compositor::g_dragStartY = 0;
        bool Compositor::g_dragPending = false; uint64_t Compositor::g_dragPendingWin = 0;
        bool Compositor::g_resizeActive = false; int Compositor::g_resizeStartW = 0; int Compositor::g_resizeStartH = 0; int Compositor::g_resizeStartMX = 0; int Compositor::g_resizeStartMY = 0; uint64_t Compositor::g_resizeWin = 0;
        bool Compositor::g_resizePreviewActive = false; int Compositor::g_resizePreviewW = 0; int Compositor::g_resizePreviewH = 0;
        bool Compositor::g_snapPreviewActive = false;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        RECT Compositor::g_snapPreviewRect{ 0,0,0,0 };
        HWND Compositor::g_hwnd = nullptr;
        HBITMAP Compositor::g_startBtnBmp = nullptr;
        HBITMAP Compositor::g_wallpaperBmp = nullptr;
        int Compositor::g_wallpaperW = 0;
        int Compositor::g_wallpaperH = 0;
        bool Compositor::g_startMenuVisible = false;
        RECT Compositor::g_startMenuRect{ 0,0,0,0 };
#else
        Compositor::SnapRect Compositor::g_snapPreviewRect{ 0,0,0,0 };
        bool Compositor::g_needsRedraw = true;
#endif
        std::string Compositor::g_wallpaperPath = "";
        std::string Compositor::g_wallpaperId = "";
        std::string Compositor::g_backgroundScaleMode = "fill";
        ImagePtr Compositor::g_wallpaperImage = nullptr;
        uint32_t Compositor::g_gradientTopColor = 0xFF142850;
        uint32_t Compositor::g_gradientBottomColor = 0xFF0F121C;
        uint32_t Compositor::g_gradientAccentColor = 0xFF192337;
        bool Compositor::g_showDesktopActive = false; std::vector<uint64_t> Compositor::g_showDesktopMinimized; uint64_t Compositor::g_lastClickTicks = 0; uint64_t Compositor::g_lastClickWin = 0;
        bool Compositor::g_altTabOverlayActive = false; uint64_t Compositor::g_altTabOverlayTicks = 0; int Compositor::g_altTabCycleIndex = 0;
        bool Compositor::g_taskbarCycleActive = false; int Compositor::g_taskbarCycleIndex = 0; bool Compositor::g_keyboardMoveActive = false; bool Compositor::g_keyboardSizeActive = false; int Compositor::g_kbOrigX = 0; int Compositor::g_kbOrigY = 0; int Compositor::g_kbOrigW = 0; int Compositor::g_kbOrigH = 0;

        DesktopConfigData Compositor::g_cfg{}; std::vector<DesktopItem> Compositor::g_items; uint64_t Compositor::g_lastItemClickTicks = 0; int Compositor::g_lastItemIndex = -1;
        AppModelDemoWindowState Compositor::g_appModelDemo{};
        std::set<int> Compositor::g_selectedDesktopIconIndices; int Compositor::g_lastSelectedDesktopIconIndex = -1;
        bool Compositor::g_iconDragActive = false; int Compositor::g_iconDragIndex = -1; int Compositor::g_iconDragOffX = 0; int Compositor::g_iconDragOffY = 0; int Compositor::g_iconDragStartX = 0; int Compositor::g_iconDragStartY = 0; bool Compositor::g_iconDragPending = false;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        bool Compositor::g_iconSelectionDragPending = false; bool Compositor::g_iconSelectionDragActive = false; int Compositor::g_iconSelectionStartX = 0; int Compositor::g_iconSelectionStartY = 0; int Compositor::g_iconSelectionCurrentX = 0; int Compositor::g_iconSelectionCurrentY = 0; bool Compositor::g_iconSelectionAdditive = false;
#endif

        // Start menu keyboard/selection state
        int Compositor::g_startMenuSel = 0; int Compositor::g_startMenuScroll = 0;
        bool Compositor::g_startMenuAllProgs = false; // "All Programs" view toggle
        std::vector<std::string> Compositor::g_startMenuAllProgsSorted; // Alphabetically sorted app list
        std::vector<std::string> Compositor::g_startMenuPinnedRecent; // Start menu app pins/recent, independent of desktop files.
        bool Compositor::g_taskbarMenuVisible = false;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        RECT Compositor::g_taskbarMenuRect{ 0,0,0,0 };
#else
        Compositor::SnapRect Compositor::g_taskbarMenuRect{ 0,0,0,0 };
#endif
        int Compositor::g_taskbarMenuSel = 0;

        // Video backend (GDI on Windows, kernel FB on bare-metal)
        VideoBackend* Compositor::g_videoBackend = nullptr;
        std::string Compositor::g_hostedDesktopDirectoryPath = DesktopFolderResolver::VirtualPath();
        std::vector<std::string> Compositor::g_hostedDesktopBackHistory{};

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        static HDC s_hostedPaintDc = nullptr;
        static HBITMAP s_hostedPaintBitmap = nullptr;
        static HGDIOBJ s_hostedPaintOldBitmap = nullptr;
        static uint32_t* s_hostedPaintPixels = nullptr;
        static int s_hostedPaintWidth = 0;
        static int s_hostedPaintHeight = 0;

        static void releaseHostedPaintSurface() {
            if (s_hostedPaintDc) {
                if (s_hostedPaintOldBitmap) {
                    SelectObject(s_hostedPaintDc, s_hostedPaintOldBitmap);
                    s_hostedPaintOldBitmap = nullptr;
                }
                if (s_hostedPaintBitmap) {
                    DeleteObject(s_hostedPaintBitmap);
                    s_hostedPaintBitmap = nullptr;
                }
                DeleteDC(s_hostedPaintDc);
                s_hostedPaintDc = nullptr;
            }
            s_hostedPaintPixels = nullptr;
            s_hostedPaintWidth = 0;
            s_hostedPaintHeight = 0;
        }

        static bool ensureHostedPaintSurface(HDC referenceDc, int width, int height) {
            if (width <= 0 || height <= 0 || !referenceDc) return false;
            if (s_hostedPaintDc && s_hostedPaintBitmap && s_hostedPaintPixels &&
                s_hostedPaintWidth == width && s_hostedPaintHeight == height) {
                return true;
            }

            if (!s_hostedPaintDc) {
                s_hostedPaintDc = CreateCompatibleDC(referenceDc);
                if (!s_hostedPaintDc) {
                    Logger::write(LogLevel::Error, "Hosted compositor: CreateCompatibleDC failed");
                    return false;
                }
            }

            if (s_hostedPaintBitmap) {
                if (s_hostedPaintOldBitmap) {
                    SelectObject(s_hostedPaintDc, s_hostedPaintOldBitmap);
                }
                DeleteObject(s_hostedPaintBitmap);
                s_hostedPaintBitmap = nullptr;
                s_hostedPaintOldBitmap = nullptr;
                s_hostedPaintPixels = nullptr;
            }

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(s_hostedPaintDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bitmap || !bits) {
                Logger::write(LogLevel::Error, "Hosted compositor: CreateDIBSection failed");
                if (bitmap) DeleteObject(bitmap);
                return false;
            }

            HGDIOBJ oldBitmap = SelectObject(s_hostedPaintDc, bitmap);
            if (!oldBitmap) {
                DeleteObject(bitmap);
                return false;
            }

            s_hostedPaintBitmap = bitmap;
            s_hostedPaintOldBitmap = oldBitmap;
            s_hostedPaintPixels = static_cast<uint32_t*>(bits);
            s_hostedPaintWidth = width;
            s_hostedPaintHeight = height;
            return true;
        }
#endif

        void Compositor::initVideoBackend( ) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
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

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        namespace {
            constexpr UINT_PTR kHostedFreezeDiagTimerId = 0x47585031u;
            constexpr uint64_t kHostedFreezeDiagHeartbeatMs = 5000;
            constexpr uint64_t kHostedFreezeDiagLongOpMs = 250;

            struct HostedFreezeDiagnosticsState {
                bool enabled{ false };
                bool timerInstalled{ false };
                uint64_t pumpCount{ 0 };
                uint64_t messageCount{ 0 };
                uint64_t repaintRequestCount{ 0 };
                uint64_t paintCount{ 0 };
                uint64_t timerCount{ 0 };
                uint64_t lastPumpMs{ 0 };
                uint64_t lastMessageMs{ 0 };
                uint64_t lastRepaintRequestMs{ 0 };
                uint64_t lastPaintBeginMs{ 0 };
                uint64_t lastPaintEndMs{ 0 };
                uint64_t lastTimerMs{ 0 };
                uint64_t lastPaintDurationMs{ 0 };
                uint64_t maxPaintDurationMs{ 0 };
                uint64_t lastVncDurationMs{ 0 };
                uint64_t maxVncDurationMs{ 0 };
                uint64_t lastMessageDurationMs{ 0 };
                uint64_t maxMessageDurationMs{ 0 };
                uint64_t lastHeartbeatLogMs{ 0 };
                uint32_t lastMessageType{ 0 };
            };

            static HostedFreezeDiagnosticsState g_hostedFreezeDiag;

            static bool hostedFreezeDiagnosticsEnabled( ) {
                static const bool enabled = [] ( ) {
                    const char* value = std::getenv("GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS");
                    if (!value || !*value) return false;
                    std::string lower;
                    lower.reserve(std::strlen(value));
                    for (const char* p = value; *p; ++p) {
                        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
                    }
                    return !(lower == "0" || lower == "false" || lower == "off" || lower == "no");
                }( );
                return enabled;
            }

            static std::string hostedFreezeDiagnosticsResourceCounts( ) {
                DWORD gdiObjects = GetGuiResources(GetCurrentProcess( ), GR_GDIOBJECTS);
                DWORD userObjects = GetGuiResources(GetCurrentProcess( ), GR_USEROBJECTS);
                return std::string("gdiObjects=") + std::to_string(gdiObjects) +
                    " userObjects=" + std::to_string(userObjects);
            }

            static std::string hostedFreezeDiagnosticsCompactSummary( ) {
                if (!g_hostedFreezeDiag.enabled) return "";
                const uint64_t now = nowMs( );
                std::ostringstream oss;
                oss << "diag="
                    << "pumps=" << g_hostedFreezeDiag.pumpCount
                    << ",messages=" << g_hostedFreezeDiag.messageCount
                    << ",paints=" << g_hostedFreezeDiag.paintCount
                    << ",timers=" << g_hostedFreezeDiag.timerCount
                    << ",repaints=" << g_hostedFreezeDiag.repaintRequestCount
                    << ",lastPumpAgeMs=" << (g_hostedFreezeDiag.lastPumpMs ? (now - g_hostedFreezeDiag.lastPumpMs) : 0)
                    << ",lastMessageAgeMs=" << (g_hostedFreezeDiag.lastMessageMs ? (now - g_hostedFreezeDiag.lastMessageMs) : 0)
                    << ",lastRepaintAgeMs=" << (g_hostedFreezeDiag.lastRepaintRequestMs ? (now - g_hostedFreezeDiag.lastRepaintRequestMs) : 0)
                    << ",lastPaintAgeMs=" << (g_hostedFreezeDiag.lastPaintEndMs ? (now - g_hostedFreezeDiag.lastPaintEndMs) : 0)
                    << ",lastTimerAgeMs=" << (g_hostedFreezeDiag.lastTimerMs ? (now - g_hostedFreezeDiag.lastTimerMs) : 0)
                    << ",lastPaintDurationMs=" << g_hostedFreezeDiag.lastPaintDurationMs
                    << ",lastVncDurationMs=" << g_hostedFreezeDiag.lastVncDurationMs
                    << ",lastMessageDurationMs=" << g_hostedFreezeDiag.lastMessageDurationMs
                    << ",lastMessageType=" << g_hostedFreezeDiag.lastMessageType
                    << ",vncClients=" << (vnc::VncServer::IsRunning( ) ? vnc::VncServer::GetClientCount( ) : 0)
                    << "," << hostedFreezeDiagnosticsResourceCounts( );
                return oss.str( );
            }

            static void hostedFreezeDiagnosticsLogHeartbeat(const char* reason, bool force = false) {
                if (!g_hostedFreezeDiag.enabled) return;
                const uint64_t now = nowMs( );
                if (!force && g_hostedFreezeDiag.lastHeartbeatLogMs != 0 &&
                    (now - g_hostedFreezeDiag.lastHeartbeatLogMs) < kHostedFreezeDiagHeartbeatMs) {
                    return;
                }

                g_hostedFreezeDiag.lastHeartbeatLogMs = now;
                std::ostringstream oss;
                oss << "Hosted freeze diag: reason=" << reason
                    << " pumps=" << g_hostedFreezeDiag.pumpCount
                    << " messages=" << g_hostedFreezeDiag.messageCount
                    << " paints=" << g_hostedFreezeDiag.paintCount
                    << " timers=" << g_hostedFreezeDiag.timerCount
                    << " repaints=" << g_hostedFreezeDiag.repaintRequestCount
                    << " lastPumpAgeMs=" << (g_hostedFreezeDiag.lastPumpMs ? (now - g_hostedFreezeDiag.lastPumpMs) : 0)
                    << " lastMessageAgeMs=" << (g_hostedFreezeDiag.lastMessageMs ? (now - g_hostedFreezeDiag.lastMessageMs) : 0)
                    << " lastRepaintAgeMs=" << (g_hostedFreezeDiag.lastRepaintRequestMs ? (now - g_hostedFreezeDiag.lastRepaintRequestMs) : 0)
                    << " lastPaintAgeMs=" << (g_hostedFreezeDiag.lastPaintEndMs ? (now - g_hostedFreezeDiag.lastPaintEndMs) : 0)
                    << " lastTimerAgeMs=" << (g_hostedFreezeDiag.lastTimerMs ? (now - g_hostedFreezeDiag.lastTimerMs) : 0)
                    << " lastPaintDurationMs=" << g_hostedFreezeDiag.lastPaintDurationMs
                    << " maxPaintDurationMs=" << g_hostedFreezeDiag.maxPaintDurationMs
                    << " lastVncDurationMs=" << g_hostedFreezeDiag.lastVncDurationMs
                    << " maxVncDurationMs=" << g_hostedFreezeDiag.maxVncDurationMs
                    << " lastMessageDurationMs=" << g_hostedFreezeDiag.lastMessageDurationMs
                    << " maxMessageDurationMs=" << g_hostedFreezeDiag.maxMessageDurationMs
                    << " lastMessageType=" << g_hostedFreezeDiag.lastMessageType
                    << " vncClients=" << (vnc::VncServer::IsRunning( ) ? vnc::VncServer::GetClientCount( ) : 0)
                    << " " << hostedFreezeDiagnosticsResourceCounts( );
                Logger::write(LogLevel::Info, oss.str( ));
            }

            static void hostedFreezeDiagnosticsOnRequestRepaint( ) {
                if (!g_hostedFreezeDiag.enabled) return;
                ++g_hostedFreezeDiag.repaintRequestCount;
                g_hostedFreezeDiag.lastRepaintRequestMs = nowMs( );
            }

            static void hostedFreezeDiagnosticsOnPump( ) {
                if (!g_hostedFreezeDiag.enabled) return;
                ++g_hostedFreezeDiag.pumpCount;
                g_hostedFreezeDiag.lastPumpMs = nowMs( );
            }

            static void hostedFreezeDiagnosticsOnTimer( ) {
                if (!g_hostedFreezeDiag.enabled) return;
                ++g_hostedFreezeDiag.timerCount;
                g_hostedFreezeDiag.lastTimerMs = nowMs( );
                hostedFreezeDiagnosticsLogHeartbeat("WM_TIMER");
            }

            static void hostedFreezeDiagnosticsOnMessageBegin(uint32_t type) {
                if (!g_hostedFreezeDiag.enabled) return;
                ++g_hostedFreezeDiag.messageCount;
                g_hostedFreezeDiag.lastMessageMs = nowMs( );
                g_hostedFreezeDiag.lastMessageType = type;
            }

            static void hostedFreezeDiagnosticsOnMessageEnd(uint64_t durationMs) {
                if (!g_hostedFreezeDiag.enabled) return;
                g_hostedFreezeDiag.lastMessageDurationMs = durationMs;
                if (durationMs > g_hostedFreezeDiag.maxMessageDurationMs) g_hostedFreezeDiag.maxMessageDurationMs = durationMs;
                if (durationMs >= kHostedFreezeDiagLongOpMs) {
                    std::ostringstream oss;
                    oss << "Hosted freeze diag: slow IPC handler durationMs=" << durationMs
                        << " messageType=" << g_hostedFreezeDiag.lastMessageType
                        << " pumps=" << g_hostedFreezeDiag.pumpCount
                        << " paints=" << g_hostedFreezeDiag.paintCount
                        << " repaints=" << g_hostedFreezeDiag.repaintRequestCount
                        << " " << hostedFreezeDiagnosticsResourceCounts( );
                    Logger::write(LogLevel::Warn, oss.str( ));
                }
                hostedFreezeDiagnosticsLogHeartbeat("message");
            }

            static void hostedFreezeDiagnosticsOnPaintBegin( ) {
                if (!g_hostedFreezeDiag.enabled) return;
                ++g_hostedFreezeDiag.paintCount;
                g_hostedFreezeDiag.lastPaintBeginMs = nowMs( );
            }

            static void hostedFreezeDiagnosticsOnPaintEnd(uint64_t durationMs, uint64_t vncDurationMs) {
                if (!g_hostedFreezeDiag.enabled) return;
                const uint64_t endMs = nowMs( );
                g_hostedFreezeDiag.lastPaintEndMs = endMs;
                g_hostedFreezeDiag.lastPaintDurationMs = durationMs;
                g_hostedFreezeDiag.lastVncDurationMs = vncDurationMs;
                if (durationMs > g_hostedFreezeDiag.maxPaintDurationMs) g_hostedFreezeDiag.maxPaintDurationMs = durationMs;
                if (vncDurationMs > g_hostedFreezeDiag.maxVncDurationMs) g_hostedFreezeDiag.maxVncDurationMs = vncDurationMs;

                if (durationMs >= kHostedFreezeDiagLongOpMs || vncDurationMs >= kHostedFreezeDiagLongOpMs) {
                    std::ostringstream oss;
                    oss << "Hosted freeze diag: slow WM_PAINT durationMs=" << durationMs
                        << " vncDurationMs=" << vncDurationMs
                        << " paints=" << g_hostedFreezeDiag.paintCount
                        << " repaints=" << g_hostedFreezeDiag.repaintRequestCount
                        << " pumps=" << g_hostedFreezeDiag.pumpCount
                        << " " << hostedFreezeDiagnosticsResourceCounts( );
                    Logger::write(LogLevel::Warn, oss.str( ));
                }

                hostedFreezeDiagnosticsLogHeartbeat("WM_PAINT");
            }
        }
#endif

        static void publishOut(MsgType type, const std::string& payload, uint64_t dstPid = 0) { 
            if (type == MsgType::MT_Create) {
                Logger::write(LogLevel::Info, std::string("publishOut MT_Create payload=") + payload + " dstPid=" + std::to_string(dstPid));
            }
            ipc::Message out; out.type = (uint32_t)type; out.dstPid = dstPid; out.data.assign(payload.begin( ), payload.end( )); ipc::Bus::publish(kGuiChanOut, std::move(out), false); 
        }

        struct BackgroundDrawRect {
            int x;
            int y;
            int w;
            int h;
        };

        static BackgroundDrawRect computeBackgroundDrawRect(const ImagePtr& image, int targetW, int targetH, BackgroundScaleMode mode)
        {
            if (!image || image->Width <= 0 || image->Height <= 0 || targetW <= 0 || targetH <= 0) {
                return { 0, 0, targetW, targetH };
            }
            if (mode == BackgroundScaleMode::Stretch) return { 0, 0, targetW, targetH };
            if (mode == BackgroundScaleMode::Center || mode == BackgroundScaleMode::Tile) {
                return { (targetW - image->Width) / 2, (targetH - image->Height) / 2, image->Width, image->Height };
            }

            const double sx = static_cast<double>(targetW) / static_cast<double>(image->Width);
            const double sy = static_cast<double>(targetH) / static_cast<double>(image->Height);
            const double scale = (mode == BackgroundScaleMode::Fit) ? std::min(sx, sy) : std::max(sx, sy);
            int drawW = std::max(1, static_cast<int>(image->Width * scale + 0.5));
            int drawH = std::max(1, static_cast<int>(image->Height * scale + 0.5));
            return { (targetW - drawW) / 2, (targetH - drawH) / 2, drawW, drawH };
        }

        static void drawBackgroundGradientToPixels(uint32_t* pixels, int fbW, int fbH, int pitch, uint32_t topColor, uint32_t bottomColor)
        {
            if (!pixels || fbW <= 0 || fbH <= 0 || pitch <= 0) return;
            const int stride = pitch / 4;
            uint8_t topR = static_cast<uint8_t>((topColor >> 16) & 0xFF);
            uint8_t topG = static_cast<uint8_t>((topColor >> 8) & 0xFF);
            uint8_t topB = static_cast<uint8_t>(topColor & 0xFF);
            uint8_t botR = static_cast<uint8_t>((bottomColor >> 16) & 0xFF);
            uint8_t botG = static_cast<uint8_t>((bottomColor >> 8) & 0xFF);
            uint8_t botB = static_cast<uint8_t>(bottomColor & 0xFF);
            for (int y = 0; y < fbH; ++y) {
                float t = static_cast<float>(y) / static_cast<float>(fbH > 1 ? fbH - 1 : 1);
                uint8_t r = static_cast<uint8_t>(topR + t * (botR - topR));
                uint8_t g = static_cast<uint8_t>(topG + t * (botG - topG));
                uint8_t b = static_cast<uint8_t>(topB + t * (botB - topB));
                uint32_t color = 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
                for (int x = 0; x < fbW; ++x) {
                    pixels[y * stride + x] = color;
                }
            }
        }

        static void drawBackgroundImageToPixels(uint32_t* pixels, int fbW, int fbH, int pitch, const ImagePtr& image, BackgroundScaleMode mode)
        {
            if (!image || !image->isValid()) return;
            if (mode == BackgroundScaleMode::Tile) {
                int stepW = std::max(1, image->Width);
                int stepH = std::max(1, image->Height);
                for (int y = 0; y < fbH; y += stepH) {
                    for (int x = 0; x < fbW; x += stepW) {
                        ImageRenderer::DrawImage(pixels, fbW, fbH, pitch, image, x, y);
                    }
                }
                return;
            }
            BackgroundDrawRect r = computeBackgroundDrawRect(image, fbW, fbH, mode);
            ImageRenderer::DrawImage(pixels, fbW, fbH, pitch, image, r.x, r.y, r.w, r.h);
        }

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        static void drawBackgroundImageToHdc(HDC dc, RECT cr, const ImagePtr& image, BackgroundScaleMode mode)
        {
            if (!image || !image->isValid()) return;
            int w = cr.right - cr.left;
            int h = cr.bottom - cr.top;
            if (mode == BackgroundScaleMode::Tile) {
                int stepW = std::max(1, image->Width);
                int stepH = std::max(1, image->Height);
                for (int y = cr.top; y < cr.bottom; y += stepH) {
                    for (int x = cr.left; x < cr.right; x += stepW) {
                        ImageRenderer::DrawImage(dc, image, x, y);
                    }
                }
                return;
            }
            BackgroundDrawRect r = computeBackgroundDrawRect(image, w, h, mode);
            ImageRenderer::DrawImage(dc, image, cr.left + r.x, cr.top + r.y, r.w, r.h);
        }
#endif

        static bool hostedSciFiTheme(const DesktopTheme& theme) {
            return theme.id == DesktopThemeId::SciFi;
        }

        static uint32_t hostedDesktopTopColor(const DesktopTheme& theme) {
            return hostedSciFiTheme(theme) ? theme.desktopBackground : 0xFF142850u;
        }

        static uint32_t hostedDesktopBottomColor(const DesktopTheme& theme) {
            return hostedSciFiTheme(theme)
                ? WindowRenderer::BlendThemeColor(theme.desktopBackground, theme.windowBackground, 12)
                : 0xFF0F121Cu;
        }

        static uint32_t hostedDesktopAccentColor(const DesktopTheme& theme) {
            return hostedSciFiTheme(theme)
                ? WindowRenderer::BlendThemeColor(theme.desktopBackground, theme.accent, 10)
                : 0xFF192337u;
        }

        static uint32_t hostedTaskbarSurfaceColor(const DesktopTheme& theme) {
            return hostedSciFiTheme(theme)
                ? WindowRenderer::BlendThemeColor(theme.taskbarBackground, theme.windowBackground, 8)
                : theme.taskbarBackground;
        }

        static uint32_t hostedTaskbarHighlightColor(const DesktopTheme& theme) {
            return hostedSciFiTheme(theme)
                ? WindowRenderer::BlendThemeColor(theme.taskbarBorder, theme.accent, 52)
                : theme.taskbarBorder;
        }

        static uint32_t hostedTaskbarBorderColor(const DesktopTheme& theme) {
            return hostedSciFiTheme(theme)
                ? WindowRenderer::BlendThemeColor(theme.taskbarBorder, theme.windowBackground, 16)
                : theme.taskbarBorder;
        }

        static uint32_t hostedPanelSurfaceColor(const DesktopTheme& theme) {
            return hostedSciFiTheme(theme)
                ? WindowRenderer::BlendThemeColor(theme.taskbarBackground, theme.windowBackground, 12)
                : RGB(45, 45, 55);
        }

        static uint32_t hostedPanelBorderColor(const DesktopTheme& theme) {
            return hostedSciFiTheme(theme)
                ? WindowRenderer::BlendThemeColor(theme.taskbarBorder, theme.accent, 40)
                : RGB(255, 255, 255);
        }

        static uint32_t hostedTaskbarItemFillColor(const DesktopTheme& theme, bool focused, bool hover, bool minimized, bool tombstoned) {
            if (!hostedSciFiTheme(theme)) {
                return hover ? theme.accent : (focused ? theme.mutedAccent : (minimized ? 0xFF28303Bu : (tombstoned ? 0xFF533F2Cu : 0xFF373A46u)));
            }

            const uint32_t base = hostedTaskbarSurfaceColor(theme);
            const uint32_t idleColor = minimized
                ? WindowRenderer::BlendThemeColor(base, theme.windowBorder, 26)
                : (tombstoned
                    ? WindowRenderer::BlendThemeColor(base, theme.accent, 12)
                    : WindowRenderer::BlendThemeColor(base, theme.windowBorder, 14));
            const uint32_t activeColor = WindowRenderer::BlendThemeColor(base, theme.mutedAccent, 22);
            const uint32_t hoverColor = WindowRenderer::BlendThemeColor(base, theme.accent, 22);
            return hover ? hoverColor : (focused ? activeColor : idleColor);
        }

        static uint32_t hostedTaskbarItemBorderColor(const DesktopTheme& theme, bool focused, bool hover) {
            if (!hostedSciFiTheme(theme)) {
                return theme.taskbarBorder;
            }

            return hover
                ? WindowRenderer::BlendThemeColor(theme.taskbarBorder, theme.mutedAccent, 30)
                : (focused
                    ? WindowRenderer::BlendThemeColor(theme.taskbarBorder, theme.accent, 35)
                    : theme.taskbarBorder);
        }

        static uint32_t hostedStartButtonFillColor(const DesktopTheme& theme, bool hover, bool open) {
            if (!hostedSciFiTheme(theme)) {
                return open ? theme.accent : theme.mutedAccent;
            }

            const uint32_t base = hostedTaskbarSurfaceColor(theme);
            if (open) {
                return WindowRenderer::BlendThemeColor(base, theme.accent, 26);
            }
            if (hover) {
                return WindowRenderer::BlendThemeColor(base, theme.mutedAccent, 22);
            }
            return WindowRenderer::BlendThemeColor(base, theme.windowBorder, 18);
        }

        static uint32_t hostedStartButtonBorderColor(const DesktopTheme& theme, bool hover, bool open) {
            if (!hostedSciFiTheme(theme)) {
                return RGB(255, 255, 255);
            }

            return open
                ? WindowRenderer::BlendThemeColor(theme.taskbarBorder, theme.accent, 42)
                : (hover
                    ? WindowRenderer::BlendThemeColor(theme.taskbarBorder, theme.mutedAccent, 34)
                    : theme.taskbarBorder);
        }

        static RECT hostedStartButtonRect(const DesktopTheme& theme, const RECT& tb) {
            return RECT{ tb.left + theme.taskbarPadding, tb.top + 6, tb.left + theme.taskbarPadding + 32, tb.top + 34 };
        }

        static RECT hostedStartButtonRect(const DesktopTheme& theme, const WorkRect& tb) {
            return RECT{ tb.left + theme.taskbarPadding, tb.top + 6, tb.left + theme.taskbarPadding + 32, tb.top + 34 };
        }

        static const bool kEnableStartMenuIcons = true;
        static const int kStartMenuIconSize = 16;
        static const int kStartMenuRowH = 22;
        static const int kStartMenuRowGap = 4;
        static std::string s_lastLaunchAction;
        static uint64_t s_lastLaunchTicks = 0;

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
#if defined(GXOS_SYSTEM_FONT_DEMO)
        static void drawSystemFontDemo(HDC dc, RECT cr)
        {
            const int panelX = 16;
            const int panelY = 16;
            const int panelW = cr.right > 520 ? 520 : (cr.right - 24);
            const int panelH = 108;
            if (panelW <= 0 || panelH <= 0) return;

            RECT panel{ panelX, panelY, panelX + panelW, panelY + panelH };
            HBRUSH panelBrush = CreateSolidBrush(RGB(22, 26, 36));
            FillRect(dc, &panel, panelBrush);
            DeleteObject(panelBrush);
            FrameRect(dc, &panel, (HBRUSH)GetStockObject(WHITE_BRUSH));

            static const char* kLine1 = "Roboto 12 regular: The quick brown fox jumps over lazy glyphs: g j p q y";
            static const char* kLine2 = "Roboto 12 bold: The quick brown fox jumps over lazy glyphs: g j p q y";
            static const char* kLine3 = "Roboto 12 italic: The quick brown fox jumps over lazy glyphs: g j p q y";
            static const char* kLine4 = "Roboto 9 regular: The quick brown fox jumps over lazy glyphs: g j p q y";

            SystemFont::DrawText(dc, panelX + 10, panelY + 10, kLine1, -1, RGB(236, 240, 248), FontRole::Default);
            SystemFont::DrawText(dc, panelX + 10, panelY + 34, kLine2, -1, RGB(255, 224, 170), FontRole::Title);
            SystemFont::DrawText(dc, panelX + 10, panelY + 58, kLine3, -1, RGB(196, 220, 255), FontRole::Emphasis);
            SystemFont::DrawText(dc, panelX + 10, panelY + 82, kLine4, -1, RGB(210, 214, 222), FontRole::Small);
        }
#endif

        static int measureUiText(const char* text, int len = -1, FontRole role = FontRole::Default)
        {
            return SystemFont::MeasureWidth(role, text, len);
        }

        static int uiTextHeight(FontRole role = FontRole::Default)
        {
            return SystemFont::MeasureHeight(role);
        }

        static int centeredUiTextY(int top, int height, FontRole role = FontRole::Default)
        {
            int lineH = uiTextHeight(role);
            return top + (height > lineH ? (height - lineH) / 2 : 0);
        }

        static void drawUiText(HDC dc, int x, int y, const char* text, int len, COLORREF color, FontRole role = FontRole::Default)
        {
            SystemFont::DrawText(dc, x, y, text, len, color, role);
        }

        static void drawUiText(HDC dc, int x, int y, const std::string& text, COLORREF color, FontRole role = FontRole::Default)
        {
            drawUiText(dc, x, y, text.c_str(), static_cast<int>(text.size()), color, role);
        }

        static std::vector<std::string> wrapUiTextToWidth(const std::string& text, int maxWidth, FontRole role, size_t maxLines = 3)
        {
            std::vector<std::string> lines;
            if (text.empty() || maxWidth <= 0 || maxLines == 0) return lines;

            size_t pos = 0;
            while (pos < text.size() && lines.size() < maxLines) {
                while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
                if (pos >= text.size()) break;

                std::string line;
                while (pos < text.size()) {
                    size_t wordStart = pos;
                    while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
                    std::string word = text.substr(wordStart, pos - wordStart);
                    std::string candidate = line.empty() ? word : line + " " + word;
                    if (measureUiText(candidate.c_str(), static_cast<int>(candidate.size()), role) <= maxWidth) {
                        line = candidate;
                    } else {
                        if (line.empty()) {
                            std::string piece;
                            for (char ch : word) {
                                std::string next = piece + ch;
                                if (!piece.empty() && measureUiText(next.c_str(), static_cast<int>(next.size()), role) > maxWidth) {
                                    lines.push_back(piece);
                                    piece.clear();
                                    if (lines.size() >= maxLines) return lines;
                                }
                                piece.push_back(ch);
                            }
                            line = piece;
                        } else {
                            pos = wordStart;
                        }
                        break;
                    }
                    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
                    if (pos >= text.size()) break;
                }
                if (!line.empty()) lines.push_back(line);
            }

            return lines;
        }

        static int wrappedUiTextHeight(const std::vector<std::string>& lines, FontRole role)
        {
            if (lines.empty()) return 0;
            return static_cast<int>(lines.size()) * uiTextHeight(role);
        }
#endif

        static bool isAppModelDemoAppLabel(const std::string& label) {
            return label == "App Model Demo" ||
                label == "AppModel" ||
                label == "Native App Debug Viewer" ||
                label == "Hello World" ||
                label == "Resource Viewer" ||
                label == "HelloWorld ELF" ||
                label == "Future GXApp Package";
        }

        static void logCompositorList(const char* label, const std::vector<std::string>& items) {
            Logger::write(LogLevel::Info, std::string("Compositor ") + label + " count=" + std::to_string(items.size()));
            for (const auto& item : items) {
                Logger::write(LogLevel::Info, std::string("  ") + label + ": " + item);
            }
        }

        static bool hasListItem(const std::vector<std::string>& items, const std::string& value) {
            return std::find(items.begin(), items.end(), value) != items.end();
        }

        static bool namesEquivalent(const std::string& a, const std::string& b) {
            if (a == b) return true;
            return (a == "AppModel" && b == "App Model Demo") || (a == "App Model Demo" && b == "AppModel");
        }

        static bool hasEquivalentListItem(const std::vector<std::string>& items, const std::string& value) {
            for (const auto& item : items) {
                if (namesEquivalent(item, value)) return true;
            }
            return false;
        }

        static bool startsWithText(const std::string& value, const char* prefix) {
            if (!prefix) return false;
            size_t prefixLen = std::strlen(prefix);
            return value.size() >= prefixLen && value.compare(0, prefixLen, prefix) == 0;
        }

        static std::string desktopLayoutKey(const DesktopItem& item) {
            if (item.kind == DesktopItemKind::SystemObject) return item.action;
            if (item.kind == DesktopItemKind::FilesystemEntry) return std::string(item.isDirectory ? "desktop-folder:" : "desktop-file:") + item.path;
            if (item.kind == DesktopItemKind::Shortcut && item.shortcutType == "App" && !item.targetAppId.empty()) return std::string("shortcut:app:") + item.targetAppId;
            if (item.kind == DesktopItemKind::Shortcut && (item.shortcutType == "File" || item.shortcutType == "Folder") && !item.path.empty()) return std::string("shortcut:") + (item.shortcutType == "Folder" ? "folder:" : "file:") + item.path;
            return item.action;
        }

        static std::string appShortcutLayoutKey(const std::string& appId) {
            return std::string("shortcut:app:") + appId;
        }

        static std::string filesystemShortcutLayoutKey(const std::string& shortcutType, const std::string& targetPath) {
            return std::string("shortcut:") + (shortcutType == "Folder" ? "folder:" : "file:") + targetPath;
        }

        static bool isDesktopFolderRootPath(const std::string& path);

        struct DesktopGridMetrics {
            int margin{20};
            int iconW{56};
            int iconH{56};
            int cellW{84};
            int cellH{94};
            int labelMaxLines{3};
            int workX{0};
            int workY{0};
            int workW{1024};
            int workH{728};
        };

        struct DesktopCellRect {
            int left;
            int top;
            int right;
            int bottom;
        };

        static DesktopGridMetrics desktopGridMetrics() {
            DesktopGridMetrics metrics;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            if (Compositor::hostedDesktopUsesCompactIconLayout()) {
                metrics.iconW = 40;
                metrics.iconH = 40;
                metrics.cellW = 72;
                metrics.cellH = 82;
                metrics.labelMaxLines = 2;
            }
            RECT cr{0, 0, 1024, 768};
            if (Compositor::g_hwnd) GetClientRect(Compositor::g_hwnd, &cr);
            WorkRect work = desktopWorkAreaForBounds(static_cast<int>(cr.right - cr.left), static_cast<int>(cr.bottom - cr.top));
            metrics.workX = work.left;
            metrics.workY = work.top;
            metrics.workW = std::max(1, work.right - work.left);
            metrics.workH = std::max(1, work.bottom - work.top);
#else
            if (Compositor::g_videoBackend) {
                WorkRect work = desktopWorkAreaForBounds(Compositor::g_videoBackend->getWidth(), Compositor::g_videoBackend->getHeight());
                metrics.workX = work.left;
                metrics.workY = work.top;
                metrics.workW = std::max(1, work.right - work.left);
                metrics.workH = std::max(1, work.bottom - work.top);
            }
#endif
            return metrics;
        }

        static DesktopCellRect desktopCellRect(int x, int y, const DesktopGridMetrics& metrics) {
            return { x, y, x + metrics.cellW, y + metrics.cellH };
        }

        static int desktopIconTopPadding(const DesktopGridMetrics& metrics) {
            return metrics.iconH <= 40 ? 4 : 6;
        }

        static int desktopIconLabelPadding(const DesktopGridMetrics& metrics) {
            return metrics.iconH <= 40 ? 4 : 5;
        }

        static int desktopIconCellHeightForItem(const DesktopItem& item, const DesktopGridMetrics& metrics) {
            const int labelMaxW = std::max(1, metrics.cellW - 8);
            const auto labelLines = wrapUiTextToWidth(item.label, labelMaxW, FontRole::Small, static_cast<size_t>(std::max(1, metrics.labelMaxLines)));
            const int labelH = std::max(uiTextHeight(FontRole::Small), wrappedUiTextHeight(labelLines, FontRole::Small));
            return desktopIconTopPadding(metrics) + metrics.iconH + desktopIconLabelPadding(metrics) + labelH;
        }

        static bool desktopRectsOverlap(const DesktopCellRect& a, const DesktopCellRect& b) {
            return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
        }

        static bool findNextAvailableDesktopIconPosition(const std::vector<DesktopItem>& items, const std::string& ignoreKey, int& outX, int& outY) {
            const DesktopGridMetrics metrics = desktopGridMetrics();
            std::vector<DesktopCellRect> occupied;
            for (const auto& item : items) {
                if (item.ix < 0 || item.iy < 0) continue;
                if (!ignoreKey.empty() && desktopLayoutKey(item) == ignoreKey) continue;
                occupied.push_back(desktopCellRect(item.ix, item.iy, metrics));
            }

            const int columns = std::max(1, (metrics.workW - metrics.margin) / metrics.cellW);
            const int rows = std::max(1, (metrics.workH - metrics.margin) / metrics.cellH);
            Logger::write(LogLevel::Info, "Desktop icon slot allocation: occupied=" + std::to_string(occupied.size()) +
                " columns=" + std::to_string(columns) + " rows=" + std::to_string(rows));

            for (int row = 0; row < rows; ++row) {
                for (int col = 0; col < columns; ++col) {
                    int x = metrics.workX + metrics.margin + col * metrics.cellW;
                    int y = metrics.workY + metrics.margin + row * metrics.cellH;
                    DesktopCellRect candidate = desktopCellRect(x, y, metrics);
                    bool collides = false;
                    for (const auto& rect : occupied) {
                        if (desktopRectsOverlap(candidate, rect)) {
                            collides = true;
                            break;
                        }
                    }
                    if (!collides) {
                        outX = x;
                        outY = y;
                        Logger::write(LogLevel::Info, "Desktop icon slot selected: x=" + std::to_string(outX) + " y=" + std::to_string(outY));
                        return true;
                    }
                }
            }

            outX = metrics.workX + metrics.margin;
            outY = metrics.workY + metrics.margin;
            Logger::write(LogLevel::Warn, "Desktop icon slot allocation fallback: no free grid slot");
            return false;
        }

        static bool clampDesktopIconPosition(int& x, int& y) {
            const DesktopGridMetrics metrics = desktopGridMetrics();
            int minX = metrics.workX + metrics.margin;
            int minY = metrics.workY + metrics.margin;
            int maxX = metrics.workX + metrics.workW - metrics.cellW;
            int maxY = metrics.workY + metrics.workH - metrics.cellH;
            if (maxX < minX) maxX = metrics.workX;
            if (maxY < minY) maxY = metrics.workY;
            int oldX = x;
            int oldY = y;
            x = std::max(minX, std::min(x, maxX));
            y = std::max(minY, std::min(y, maxY));
            return x != oldX || y != oldY;
        }

        static void upsertDesktopIconPosition(std::vector<DesktopIconPos>& positions, const std::string& key, int x, int y) {
            for (auto& pos : positions) {
                if (pos.name == key) {
                    pos.x = x;
                    pos.y = y;
                    return;
                }
            }
            DesktopIconPos pos;
            pos.name = key;
            pos.x = x;
            pos.y = y;
            positions.push_back(pos);
        }

        static void arrangeDesktopIconsInGrid(std::vector<DesktopItem>& items) {
            for (auto& item : items) {
                item.ix = -1;
                item.iy = -1;
            }
            for (auto& item : items) {
                int x = 20;
                int y = 20;
                findNextAvailableDesktopIconPosition(items, desktopLayoutKey(item), x, y);
                item.ix = x;
                item.iy = y;
            }
        }

        static const RegisteredDesktopApp* findDesktopAppByNameOrId(const std::string& value) {
            for (const auto& app : DesktopService::GetRegisteredApps()) {
                if (app.id == value || app.displayName == value || app.launchName == value || namesEquivalent(app.displayName, value)) return &app;
            }
            return nullptr;
        }

        static DesktopItem makeSystemDesktopItem(DesktopSystemObjectKind kind, const char* label, const char* action, const char* iconName) {
            DesktopItem item;
            item.kind = DesktopItemKind::SystemObject;
            item.systemObject = kind;
            item.label = label;
            item.action = action;
            item.iconName = iconName;
            item.removable = false;
            item.pinned = true;
            Logger::write(LogLevel::Info, std::string("Desktop system icon created: ") + item.action + " label=" + item.label + " icon=" + item.iconName);
            return item;
        }

        static std::string desktopEntryIconName(const DesktopFolderEntry& entry) {
            apps::ExplorerFileEntry explorerEntry;
            explorerEntry.name = entry.name;
            explorerEntry.fullPath = entry.virtualPath;
            explorerEntry.kind = entry.isDirectory ? apps::ExplorerEntryKind::Directory : apps::ExplorerEntryKind::File;
            explorerEntry.size = entry.size;
            return apps::FileIconProvider::logicalIconNameForEntry(explorerEntry);
        }

        static DesktopItem makeFilesystemDesktopItem(const DesktopFolderEntry& entry) {
            DesktopItem item;
            item.kind = DesktopItemKind::FilesystemEntry;
            item.label = entry.name;
            item.path = entry.virtualPath;
            item.action = std::string(entry.isDirectory ? "desktop-folder:" : "desktop-file:") + entry.virtualPath;
            item.iconName = desktopEntryIconName(entry);
            item.isDirectory = entry.isDirectory;
            item.removable = true;
            Logger::write(LogLevel::Info, std::string("Desktop filesystem icon created: ") + item.action + " icon=" + item.iconName);
            return item;
        }

        static void appendSystemDesktopItemIfEnabled(std::vector<DesktopItem>& items, bool enabled, DesktopSystemObjectKind kind, const char* label, const char* action, const char* iconName) {
            Logger::write(LogLevel::Info, std::string("Desktop system icon visibility: ") + action + " enabled=" + (enabled ? "true" : "false"));
            if (!enabled) {
                Logger::write(LogLevel::Info, std::string("Desktop system icon skipped by setting: ") + action);
                return;
            }
            items.push_back(makeSystemDesktopItem(kind, label, action, iconName));
        }

        static void refreshStartMenuPinnedRecentFromConfig(const DesktopConfigData& cfg, std::vector<std::string>& items) {
            items.clear();
            for (const auto& pinned : cfg.pinned) {
                if (!pinned.empty() && !hasEquivalentListItem(items, pinned)) items.push_back(pinned);
            }
            for (const auto& recent : cfg.recent) {
                if (!recent.empty() && !hasEquivalentListItem(items, recent)) items.push_back(recent);
            }
            if (items.size() > 20) items.resize(20);
            logCompositorList("start menu pinned/recent", items);
        }

        static const char* kHostedTrashPath = "/Trash";
        static const char* kHostedTrashInfoSuffix = ".trashinfo";

        static bool endsWithText(const std::string& value, const char* suffix) {
            if (!suffix) return false;
            size_t suffixLen = std::strlen(suffix);
            return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
        }

        static std::string normalizeHostedVirtualPath(const std::string& path) {
            std::string normalized = path.empty() ? "/" : path;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            std::filesystem::path virtualPath(normalized);
            if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
            std::string out = virtualPath.lexically_normal().generic_string();
            if (out.empty()) out = "/";
            if (out.front() != '/') out.insert(out.begin(), '/');
            return out;
        }

        static bool isDesktopFolderPath(const std::string& path) {
            const std::string normalized = normalizeHostedVirtualPath(path);
            return normalized == "/Desktop" || startsWithText(normalized, "/Desktop/");
        }

        static bool isDesktopFolderRootPath(const std::string& path) {
            return normalizeHostedVirtualPath(path) == normalizeHostedVirtualPath(DesktopFolderResolver::VirtualPath());
        }

        static std::string hostedDesktopRootPath() {
            return normalizeHostedVirtualPath(DesktopFolderResolver::VirtualPath());
        }

        static bool hostedDesktopIsWithinLiveRoot(const std::string& path) {
            const std::string normalized = normalizeHostedVirtualPath(path);
            const std::string root = hostedDesktopRootPath();
            const std::string rootPrefix = root + "/";
            return normalized == root || startsWithText(normalized, rootPrefix.c_str());
        }

        static std::string basenameForVirtualPath(const std::string& path) {
            const std::string normalized = normalizeHostedVirtualPath(path);
            if (normalized == "/") return "Root";
            size_t slash = normalized.find_last_of('/');
            if (slash == std::string::npos || slash + 1 >= normalized.size()) return normalized;
            return normalized.substr(slash + 1);
        }

        static std::filesystem::path hostedRootPath() {
            return std::filesystem::current_path();
        }

        static std::filesystem::path hostedPathForVirtual(const std::string& path) {
            std::string normalized = normalizeHostedVirtualPath(path);
            if (normalized == "/") return hostedRootPath();
            return hostedRootPath() / std::filesystem::path(normalized.substr(1));
        }

        static size_t hostedTrashItemCount() {
            std::error_code error;
            std::filesystem::path trashPath = hostedPathForVirtual(kHostedTrashPath);
            if (!std::filesystem::exists(trashPath, error) || error) return 0;
            size_t count = 0;
            for (const auto& entry : std::filesystem::directory_iterator(trashPath, error)) {
                if (error) break;
                std::string name = entry.path().filename().generic_string();
                if (endsWithText(name, kHostedTrashInfoSuffix)) continue;
                ++count;
            }
            return count;
        }

        static std::string hostedTrashIconLogicalName() {
            return hostedTrashItemCount() > 0 ? "trash.full" : "trash.empty";
        }

        static void mergeVisibleAppEntry(std::vector<std::string>& target, const std::string& value, const char* sourceLabel, bool visiblePreferred) {
            if (value.empty()) {
                Logger::write(LogLevel::Info, std::string("Compositor skip empty visible entry from ") + sourceLabel);
                return;
            }
            if (hasEquivalentListItem(target, value)) {
                Logger::write(LogLevel::Info, std::string("Compositor skip already present from ") + sourceLabel + ": " + value);
                return;
            }
            target.push_back(value);
            Logger::write(LogLevel::Info, std::string("Compositor include ") + (visiblePreferred ? "desktop/start " : "list ") + "entry from " + sourceLabel + ": " + value);
        }

        static const apps::BuiltInAppMetadata* hostedBuiltInMetadataForIdentity(const std::string& identity) {
            const apps::BuiltInAppMetadata* metadata = apps::FindBuiltInAppMetadataByIdentity(identity.c_str());
            if (!metadata || !apps::IsBuiltInAppAvailableInHosted(*metadata)) return nullptr;
            return metadata;
        }

        static const apps::BuiltInAppMetadata* hostedBuiltInMetadataForRegisteredApp(const RegisteredDesktopApp* app) {
            if (!app) return nullptr;
            const apps::BuiltInAppMetadata* metadata = hostedBuiltInMetadataForIdentity(app->id);
            if (metadata) return metadata;
            metadata = hostedBuiltInMetadataForIdentity(app->displayName);
            if (metadata) return metadata;
            return hostedBuiltInMetadataForIdentity(app->launchName);
        }

        static const apps::BuiltInAppMetadata* hostedBuiltInMetadataForLabel(const std::string& label) {
            // Shared built-in metadata is display/identity-only here; launch still
            // goes through DesktopService and the existing dispatch branches.
            const RegisteredDesktopApp* app = findDesktopAppByNameOrId(label);
            const apps::BuiltInAppMetadata* metadata = hostedBuiltInMetadataForRegisteredApp(app);
            if (metadata) return metadata;
            return hostedBuiltInMetadataForIdentity(label);
        }

        static std::string hostedBuiltInIconKeyForLabel(const std::string& label) {
            const apps::BuiltInAppMetadata* metadata = hostedBuiltInMetadataForLabel(label);
            if (metadata && metadata->iconKey) return metadata->iconKey;
            return std::string();
        }

        static std::string startMenuLogicalIconName(const std::string& label) {
            if (label == "Trash") return hostedTrashIconLogicalName();
            const std::string builtInIcon = hostedBuiltInIconKeyForLabel(label);
            if (!builtInIcon.empty()) return builtInIcon;
            if (label == "Files" || label == "File Explorer" || label == "FileManager" || label == "File Manager") return "app.files";
            if (label == "Computer" || label == "This System" || label == "Computer Files" || label == "ComputerFiles") return "place.computer";
            if (label == "Documents" || label == "Recent Docs") return "place.documents";
            if (label == "Pictures") return "place.pictures";
            if (label == "Music") return "place.music";
            if (label == "Network") return "place.network";
            if (label == "Task Manager") return "app.taskmanager";
            if (label == "Control Panel" || label == "System Settings") return "app.controlpanel";
            if (label == "Display Options" || label == "Display Settings" || label == "Desktop Background" || label == "Wallpaper" || label == "Settings") return "app.settings";
            if (isAppModelDemoAppLabel(label)) return "app.generic";
            return "app.generic";
        }

        static COLORREF startMenuFallbackIconColor(const std::string& label) {
            const apps::BuiltInAppMetadata* metadata = hostedBuiltInMetadataForLabel(label);
            if (metadata && metadata->kernelIconColor != 0) {
                return RGB((metadata->kernelIconColor >> 16) & 0xFF, (metadata->kernelIconColor >> 8) & 0xFF, metadata->kernelIconColor & 0xFF);
            }
            if (label == "Calculator" || label == "Clock") return RGB(70, 140, 200);
            if (label == "Notepad" || label == "Console") return RGB(120, 180, 80);
            if (label == "Trash") return RGB(150, 150, 160);
            if (label == "Paint") return RGB(200, 120, 60);
            if (label == "ImageViewer" || label == "Image Viewer") return RGB(200, 120, 60);
            if (label == "guideXOS Navigator") return RGB(70, 120, 190);
            if (label == "TaskManager" || label == "Task Manager") return RGB(180, 70, 70);
            if (label == "DiskManager" || label == "ControlPanel" || label == "Control Panel" || label == "Settings") return RGB(140, 90, 180);
            if (isAppModelDemoAppLabel(label)) return RGB(85, 135, 210);
            if (label == "Files" || label == "File Explorer" || label == "FileExplorer" || label == "Computer" || label == "Computer Files" || label == "ComputerFiles" || label == "Documents" || label == "Recent Docs") return RGB(200, 180, 60);
            if (label == "Network") return RGB(80, 150, 180);
            return RGB(90, 100, 120);
        }

        static uint32_t startMenuFallbackIconColor32(const std::string& label) {
            COLORREF color = startMenuFallbackIconColor(label);
            return (static_cast<uint32_t>(GetRValue(color)) << 16) |
                (static_cast<uint32_t>(GetGValue(color)) << 8) |
                static_cast<uint32_t>(GetBValue(color));
        }

        static void drawStartMenuFallbackIcon(HDC dc, const RECT& iconRect, const std::string& label) {
            HBRUSH iconBrush = CreateSolidBrush(startMenuFallbackIconColor(label));
            FillRect(dc, &iconRect, iconBrush);
            DeleteObject(iconBrush);
            FrameRect(dc, &iconRect, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }

        static bool drawStartMenuIcon(HDC dc, const RECT& rowRect, const std::string& label, int& textX) {
            RECT iconRect{ rowRect.left + 4, rowRect.top + ((rowRect.bottom - rowRect.top) - kStartMenuIconSize) / 2,
                rowRect.left + 4 + kStartMenuIconSize, rowRect.top + ((rowRect.bottom - rowRect.top) - kStartMenuIconSize) / 2 + kStartMenuIconSize };
            textX = iconRect.right + 6;

            if (!kEnableStartMenuIcons) {
                drawStartMenuFallbackIcon(dc, iconRect, label);
                return false;
            }

            ImagePtr icon;
            try {
                icon = IconThemeManager::Instance().LoadIcon(startMenuLogicalIconName(label), kStartMenuIconSize);
            }
            catch (...) {
                icon.reset();
            }

            if (!icon || !icon->isValid()) {
                drawStartMenuFallbackIcon(dc, iconRect, label);
                return false;
            }

            ImageRenderer::DrawImage(dc, icon, iconRect.left, iconRect.top, kStartMenuIconSize, kStartMenuIconSize);
            return true;
        }

        static bool drawDesktopThemedIcon(HDC dc, const RECT& iconRect, const DesktopItem& item) {
            try {
                const std::string logicalIcon = item.iconName.empty() ? startMenuLogicalIconName(item.label) : item.iconName;
                ImagePtr icon = IconThemeManager::Instance().LoadIcon(logicalIcon, iconRect.right - iconRect.left);
                if (!icon || !icon->isValid()) return false;
                ImageRenderer::DrawImage(dc, icon, iconRect.left, iconRect.top, iconRect.right - iconRect.left, iconRect.bottom - iconRect.top);
                return true;
            }
            catch (...) {
                return false;
            }
        }

        static const apps::BuiltInAppMetadata* hostedBuiltInMetadataForShortcut(const DesktopShortcutRec& shortcut, const RegisteredDesktopApp* app) {
            const apps::BuiltInAppMetadata* metadata = hostedBuiltInMetadataForRegisteredApp(app);
            if (metadata) return metadata;
            metadata = hostedBuiltInMetadataForIdentity(shortcut.targetAppId);
            if (metadata) return metadata;
            return hostedBuiltInMetadataForIdentity(shortcut.label);
        }

        static DesktopItem makeAppShortcutDesktopItem(const DesktopShortcutRec& shortcut) {
            DesktopItem item;
            item.kind = DesktopItemKind::Shortcut;
            item.shortcutType = shortcut.shortcutType.empty() ? "App" : shortcut.shortcutType;
            item.targetAppId = shortcut.targetAppId;
            item.action = appShortcutLayoutKey(shortcut.targetAppId);
            const RegisteredDesktopApp* app = findDesktopAppByNameOrId(shortcut.targetAppId);
            const apps::BuiltInAppMetadata* metadata = hostedBuiltInMetadataForShortcut(shortcut, app);
            item.label = app && !app->displayName.empty() ? app->displayName : shortcut.label;
            if (item.label.empty() && metadata && metadata->displayName) item.label = metadata->displayName;
            if (item.label.empty()) item.label = shortcut.targetAppId;
            item.iconName = app && !app->icon.empty() ? app->icon : shortcut.iconName;
            if (item.iconName.empty() && metadata && metadata->iconKey) item.iconName = metadata->iconKey;
            if (item.iconName.empty()) item.iconName = startMenuLogicalIconName(item.label);
            item.removable = true;
            item.pinned = true;
            Logger::write(LogLevel::Info, std::string("Desktop shortcut loaded: type=") + item.shortcutType +
                " targetAppId=" + item.targetAppId + " label=" + item.label + " icon=" + item.iconName);
            return item;
        }

        static DesktopItem makeFilesystemShortcutDesktopItem(const DesktopShortcutRec& shortcut) {
            DesktopItem item;
            item.kind = DesktopItemKind::Shortcut;
            item.shortcutType = shortcut.shortcutType == "Folder" ? "Folder" : "File";
            item.path = normalizeHostedVirtualPath(shortcut.targetPath);
            item.isDirectory = item.shortcutType == "Folder";
            item.action = filesystemShortcutLayoutKey(item.shortcutType, item.path);
            item.label = shortcut.label.empty() ? basenameForVirtualPath(item.path) : shortcut.label;
            item.iconName = shortcut.iconName;
            if (item.iconName.empty()) {
                apps::ExplorerFileEntry entry;
                entry.name = item.label;
                entry.fullPath = item.path;
                entry.kind = item.isDirectory ? apps::ExplorerEntryKind::Directory : apps::ExplorerEntryKind::File;
                item.iconName = apps::FileIconProvider::logicalIconNameForEntry(entry);
            }
            item.removable = true;
            item.pinned = true;
            Logger::write(LogLevel::Info, "Desktop shortcut loaded: type=" + item.shortcutType +
                " targetPath=" + item.path + " label=" + item.label + " icon=" + item.iconName);
            return item;
        }

        static std::string packMousePayload(int x, int y, int button, const std::string& action, uint64_t ownerPid, uint64_t windowId = 0) {
            std::string payload = std::to_string(x) + "|" + std::to_string(y) + "|" + std::to_string(button) + "|" + action;
#ifdef GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
            if (windowId != 0 && gxos::apps::NativeAppProcessTable::IsNativeProcessId(ownerPid)) {
                payload += "|0|" + std::to_string(windowId);
            }
#else
            (void)ownerPid;
            (void)windowId;
#endif
            return payload;
        }

        std::string Compositor::packMousePayloadForTarget(int x, int y, int button, const std::string& action, uint64_t ownerPid, uint64_t windowId) {
            int appX = x;
            int appY = y;
            if (windowId != 0) {
                std::lock_guard<std::mutex> lk(g_lock);
                auto it = g_windows.find(windowId);
                if (it != g_windows.end()) {
                    const DesktopTheme& theme = GetCurrentDesktopTheme();
                    const int titleBarH = theme.titleBarHeight;
                    appX = x - it->second.x;
                    appY = y - it->second.y - titleBarH;
                }
            }
            return packMousePayload(appX, appY, button, action, ownerPid, windowId);
        }

        void Compositor::refreshDesktopItems( ) {
            Logger::write(LogLevel::Info, std::string("Compositor refreshDesktopItems input pinned=") + std::to_string(g_cfg.pinned.size()) + " recent=" + std::to_string(g_cfg.recent.size()));
            std::set<std::string> selectedActions;
            for (int idx : g_selectedDesktopIconIndices) {
                if (idx >= 0 && idx < (int)g_items.size( )) selectedActions.insert(g_items[idx].action);
            }

            refreshStartMenuPinnedRecentFromConfig(g_cfg, g_startMenuPinnedRecent);

            g_items.clear( );
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            const std::string hostedRoot = hostedDesktopRootPath();
            if (g_hostedDesktopDirectoryPath.empty()) g_hostedDesktopDirectoryPath = hostedRoot;
            g_hostedDesktopDirectoryPath = normalizeHostedVirtualPath(g_hostedDesktopDirectoryPath);
            if (!hostedDesktopIsWithinLiveRoot(g_hostedDesktopDirectoryPath)) {
                Logger::write(LogLevel::Warn, "Hosted desktop current path escaped live root; resetting to root");
                g_hostedDesktopDirectoryPath = hostedRoot;
            }
            if (!isDesktopFolderRootPath(g_hostedDesktopDirectoryPath)) {
                g_items.push_back(makeSystemDesktopItem(DesktopSystemObjectKind::DesktopBack, "Back", "desktop-nav:back", ""));
                g_items.push_back(makeSystemDesktopItem(DesktopSystemObjectKind::DesktopHome, "Go to Desktop", "desktop-nav:home", ""));
            }
            const bool showFileExplorer = g_cfg.showDesktopThisSystem || g_cfg.showDesktopFileManager;
            appendSystemDesktopItemIfEnabled(g_items, g_cfg.showDesktopTrash, DesktopSystemObjectKind::Trash, "Trash", "system:Trash", hostedTrashIconLogicalName().c_str());
            appendSystemDesktopItemIfEnabled(g_items, showFileExplorer, DesktopSystemObjectKind::FileExplorer, "File Explorer", "system:FileExplorer", "app.files");
            appendSystemDesktopItemIfEnabled(g_items, g_cfg.showDesktopSystemSettings, DesktopSystemObjectKind::SystemSettings, "System Settings", "system:SystemSettings", "app.settings");
            std::vector<DesktopFolderEntry> desktopEntries = DesktopFolderResolver::Enumerate(g_hostedDesktopDirectoryPath);
#else
            const bool showFileExplorer = g_cfg.showDesktopThisSystem || g_cfg.showDesktopFileManager;
            appendSystemDesktopItemIfEnabled(g_items, g_cfg.showDesktopTrash, DesktopSystemObjectKind::Trash, "Trash", "system:Trash", hostedTrashIconLogicalName().c_str());
            appendSystemDesktopItemIfEnabled(g_items, showFileExplorer, DesktopSystemObjectKind::FileExplorer, "File Explorer", "system:FileExplorer", "app.files");
            appendSystemDesktopItemIfEnabled(g_items, g_cfg.showDesktopSystemSettings, DesktopSystemObjectKind::SystemSettings, "System Settings", "system:SystemSettings", "app.settings");

            std::vector<DesktopFolderEntry> desktopEntries = DesktopFolderResolver::Enumerate();
#endif
            for (const auto& entry : desktopEntries) {
                g_items.push_back(makeFilesystemDesktopItem(entry));
            }

            for (const auto& shortcut : g_cfg.desktopShortcuts) {
                std::string shortcutType = shortcut.shortcutType.empty() ? "App" : shortcut.shortcutType;
                if (shortcutType == "App" && shortcut.targetAppId.empty()) {
                    Logger::write(LogLevel::Warn, "Desktop shortcut skipped: missing targetAppId");
                    continue;
                }
                if ((shortcutType == "File" || shortcutType == "Folder") && shortcut.targetPath.empty()) {
                    Logger::write(LogLevel::Warn, "Desktop shortcut skipped: missing targetPath");
                    continue;
                }
                DesktopItem shortcutItem = shortcutType == "App"
                    ? makeAppShortcutDesktopItem(shortcut)
                    : makeFilesystemShortcutDesktopItem(shortcut);
                g_items.push_back(shortcutItem);
                Logger::write(LogLevel::Info, "Desktop shortcut rendered: " + desktopLayoutKey(shortcutItem));
            }

            // Apply saved positions from config
            bool migratedIconPositions = false;
            for (auto& item : g_items) {
                const std::string key = desktopLayoutKey(item);
                for (const auto& ip : g_cfg.iconPositions) {
                    if (ip.name == key || ip.name == item.label) {
                        item.ix = ip.x;
                        item.iy = ip.y;
                        if (clampDesktopIconPosition(item.ix, item.iy)) {
                            migratedIconPositions = true;
                            Logger::write(LogLevel::Info, "Desktop icon saved position clamped into work area: " + key +
                                " x=" + std::to_string(item.ix) + " y=" + std::to_string(item.iy));
                        }
                        break;
                    }
                }
            }
            if (g_cfg.autoArrangeDesktopIcons) {
                arrangeDesktopIconsInGrid(g_items);
            }
            // Assign collision-aware grid positions to any items that don't have saved positions.
            for (auto& item : g_items) {
                if (item.ix >= 0 && item.iy >= 0) continue;
                int x = 20;
                int y = 20;
                findNextAvailableDesktopIconPosition(g_items, desktopLayoutKey(item), x, y);
                item.ix = x;
                item.iy = y;
                Logger::write(LogLevel::Info, "Desktop icon auto-positioned: " + desktopLayoutKey(item) +
                    " x=" + std::to_string(item.ix) + " y=" + std::to_string(item.iy));
            }
            if (!g_cfg.autoArrangeDesktopIcons && migratedIconPositions) {
                g_cfg.iconPositions.clear();
                for (const auto& di : g_items) {
                    DesktopIconPos ip;
                    ip.name = desktopLayoutKey(di);
                    ip.x = di.ix;
                    ip.y = di.iy;
                    g_cfg.iconPositions.push_back(ip);
                }
            }
            g_selectedDesktopIconIndices.clear( );
            for (int i = 0; i < (int)g_items.size( ); ++i) {
                bool selected = selectedActions.find(g_items[i].action) != selectedActions.end( );
                g_items[i].selected = selected;
                if (selected) g_selectedDesktopIconIndices.insert(i);
            }
            if (g_lastSelectedDesktopIconIndex >= (int)g_items.size( )) g_lastSelectedDesktopIconIndex = g_items.empty( ) ? -1 : (int)g_items.size( ) - 1;
            std::vector<std::string> finalDesktop;
            for (const auto& item : g_items) finalDesktop.push_back(item.label);
            logCompositorList("desktop item", finalDesktop);
        }

        void Compositor::refreshAllProgramsList( ) {
            g_startMenuAllProgsSorted.clear( );
            for (const auto& app : DesktopService::GetRegisteredApps()) {
                if (app.displayName.empty()) {
                    Logger::write(LogLevel::Info, "Compositor start menu skipped empty app display name");
                    continue;
                }
                if (hasEquivalentListItem(g_startMenuAllProgsSorted, app.displayName)) {
                    Logger::write(LogLevel::Info, std::string("Compositor start menu skipped duplicate app: ") + app.displayName + " id=" + app.id + " source=" + app.source);
                    continue;
                }
                Logger::write(LogLevel::Info, std::string("Compositor start menu include: ") + app.displayName + " id=" + app.id + " source=" + app.source);
                g_startMenuAllProgsSorted.push_back(app.displayName);
            }
            // Sort alphabetically (case-insensitive)
            std::sort(g_startMenuAllProgsSorted.begin( ), g_startMenuAllProgsSorted.end( ),
                [] (const std::string& a, const std::string& b) {
                    std::string al = a, bl = b;
                    std::transform(al.begin( ), al.end( ), al.begin( ), ::tolower);
                    std::transform(bl.begin( ), bl.end( ), bl.begin( ), ::tolower);
                    return al < bl;
                });
            logCompositorList("start menu app", g_startMenuAllProgsSorted);
        }

        void Compositor::saveDesktopConfig( ) { g_cfg.taskbarPosition = taskbarPositionName(g_taskbarPosition); g_cfg.desktopThemeId = DesktopThemeIdToString(GetCurrentDesktopThemeId()); std::string err; if (!DesktopConfig::Save("desktop.json", g_cfg, err)) Logger::write(LogLevel::Error, "Shortcut persistence failure: " + err); else Logger::write(LogLevel::Info, "Desktop config persisted"); }
        void Compositor::addRecent(const std::string& act) { auto it = std::find(g_cfg.recent.begin( ), g_cfg.recent.end( ), act); if (it != g_cfg.recent.end( )) g_cfg.recent.erase(it); g_cfg.recent.insert(g_cfg.recent.begin( ), act); if (g_cfg.recent.size( ) > 20) g_cfg.recent.pop_back( ); refreshDesktopItems( ); saveDesktopConfig( ); }
        void Compositor::pinAction(const std::string& act) { if (act.empty( )) return; if (std::find(g_cfg.pinned.begin( ), g_cfg.pinned.end( ), act) == g_cfg.pinned.end( )) { g_cfg.pinned.push_back(act); refreshDesktopItems( ); saveDesktopConfig( ); } }
        void Compositor::unpinAction(const std::string& act) { auto it = std::find(g_cfg.pinned.begin( ), g_cfg.pinned.end( ), act); if (it != g_cfg.pinned.end( )) { g_cfg.pinned.erase(it); refreshDesktopItems( ); saveDesktopConfig( ); } }
        static std::string hostedLaunchStatus(const RegisteredDesktopApp& app) {
            if (app.displayName == "App Model Demo" || app.launchName == "App Model Demo") return "launchable viewer";
            if (app.kind == apps::AppKind::BuiltIn) return "launchable if built-in handler exists";
            if (app.kind == apps::AppKind::NativeElf) return gxos::apps::NativeElfExecutor::ExperimentalExecutionEnabled() ? "experimental native path" : "disabled: native execution off";
            if (app.kind == apps::AppKind::GXAppPackage) return "disabled: GXApp launch not implemented";
            return "unknown";
        }

        static void appendAppModelSummaryLines(std::vector<std::string>& lines, const std::string& summaryText) {
            std::istringstream summary(summaryText);
            std::string line;
            while (std::getline(summary, line)) {
                if (line.size() > 112) line = line.substr(0, 109) + "...";
                lines.push_back("  " + line);
            }
        }

        static std::string appModelSummaryLineValue(const std::string& summaryText, const std::string& key) {
            std::istringstream summary(summaryText);
            std::string line;
            const std::string prefix = key + ":";
            while (std::getline(summary, line)) {
                if (line.rfind(prefix, 0) != 0) continue;
                std::string value = line.substr(prefix.size());
                while (!value.empty() && value.front() == ' ') value.erase(value.begin());
                return value;
            }
            return "not reported";
        }

        static void populateAppModelDemoViewerWindow(WinInfo& wi, const std::vector<RegisteredDesktopApp>& demoApps, const std::string& summaryText, const std::string& status) {
            wi.w = 920;
            wi.h = 620;
            wi.rects.clear();
            wi.images.clear();
            wi.widgets.clear();
            wi.positionedTexts.clear();
            wi.texts.clear();
            wi.rects.push_back(DrawRectItem{0, 0, wi.w, 28, 32, 40, 58});
            wi.rects.push_back(DrawRectItem{0, 250, wi.w, 2, 64, 80, 110});
            wi.rects.push_back(DrawRectItem{0, wi.h - 54, wi.w, 24, 32, 40, 58});

            wi.texts.push_back("App Model Demo");
            wi.texts.push_back("Runtime:");
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            wi.texts.push_back("  Mode: Windows compositor/test harness");
#else
            wi.texts.push_back("  Mode: compositor / unknown");
#endif
            wi.texts.push_back(std::string("  Native execution: ") + (gxos::apps::NativeElfExecutor::ExperimentalExecutionEnabled() ? "enabled" : "disabled"));
            wi.texts.push_back("  Discovered app-model demo apps: " + std::to_string(demoApps.size()));
            wi.texts.push_back("  Launch Target Comparison: " + appModelSummaryLineValue(summaryText, "launchTargetComparison"));
            wi.texts.push_back("");
            wi.texts.push_back("App Model Summary:");
            appendAppModelSummaryLines(wi.texts, summaryText);
            wi.texts.push_back("");
            wi.texts.push_back("Apps:");
            wi.texts.push_back("  Display name                 App ID/name                         Type         Status");
            for (const auto& app : demoApps) {
                std::ostringstream row;
                row << "  " << app.displayName;
                if (app.displayName.size() < 29) row << std::string(29 - app.displayName.size(), ' ');
                row << app.id;
                if (app.id.size() < 36) row << std::string(36 - app.id.size(), ' ');
                row << apps::ToString(app.kind);
                std::string kind = apps::ToString(app.kind);
                if (kind.size() < 13) row << std::string(13 - kind.size(), ' ');
                row << hostedLaunchStatus(app);
                wi.texts.push_back(row.str());
            }
            wi.texts.push_back("");
            wi.texts.push_back("Actions: R/F5 refresh app-model diagnostics, close window when done.");
            if (!status.empty()) wi.texts.push_back("Status: " + status);
        }

        void Compositor::openAppModelDemoViewerWindow() {
            std::vector<RegisteredDesktopApp> demoApps = DesktopService::GetAppModelDemoApps();
            bool hasDemo = false;
            for (const auto& app : demoApps) {
                if (app.displayName == "App Model Demo" || app.launchName == "App Model Demo") hasDemo = true;
            }
            if (!hasDemo) {
                RegisteredDesktopApp demo;
                demo.id = "gxos.builtin.appmodeldemo";
                demo.displayName = "App Model Demo";
                demo.kind = apps::AppKind::BuiltIn;
                demo.launchName = "App Model Demo";
                demo.source = "BuiltIn";
                demoApps.push_back(demo);
            }

            uint64_t id = s_nextWinId.fetch_add(1);
            WinInfo wi{};
            wi.id = id;
            wi.title = "App Model Demo";
            wi.w = 920;
            wi.h = 620;
            wi.x = 82;
            wi.y = 70;
            wi.visible = true;
            wi.dirty = true;
            wi.ownerPid = 0;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            wi.taskbarIcon = Icons::TaskbarIcon(16);
#endif
            const std::string summaryText = DesktopService::AppModelSummaryDiagnostic();
            populateAppModelDemoViewerWindow(wi, demoApps, summaryText, "snapshot refreshed");
            {
                std::lock_guard<std::mutex> lk(g_lock);
                g_windows[id] = wi;
                g_z.push_back(id);
                g_focus = id;
                g_appModelDemo.windowId = id;
                g_appModelDemo.apps = demoApps;
                g_appModelDemo.status = "snapshot refreshed";
            }
            invalidate(id);
        }

        void Compositor::updateAppModelDemoViewerWindow(const std::string& status) {
            std::vector<RegisteredDesktopApp> demoApps = DesktopService::GetAppModelDemoApps();
            const std::string summaryText = DesktopService::AppModelSummaryDiagnostic();
            std::lock_guard<std::mutex> lk(g_lock);
            auto it = g_windows.find(g_appModelDemo.windowId);
            if (it == g_windows.end()) return;
            g_appModelDemo.apps = demoApps;
            g_appModelDemo.status = status;
            populateAppModelDemoViewerWindow(it->second, demoApps, summaryText, status);
            it->second.dirty = true;
            invalidate(it->second.id);
        }

        bool Compositor::handleAppModelDemoKey(int key) {
            if (key == 82 || key == 114 || key == 116) {
                updateAppModelDemoViewerWindow("snapshot refreshed");
                return true;
            }
            return false;
        }

        static std::string buildLaunchTargetShadowDiagnosticLine(const std::string& source, const std::string& uiLabel, const std::string& shortcutTarget, const std::string& dispatchName, std::string* aliasFallbackLine = nullptr) {
            const TypedDispatchCandidateResult candidate = DesktopService::ComputeTypedDispatchCandidateForUiLaunch(source, uiLabel, shortcutTarget, dispatchName);
            const std::string adapterComparison = DesktopService::RecordLaunchTargetShadowObservation(source, candidate.target, dispatchName, candidate.typedDispatchCandidate);
            std::ostringstream oss;
            oss << "[LaunchTargetShadow] source=" << source
                << " uiLabel=" << uiLabel;
            if (!shortcutTarget.empty()) oss << " shortcutTarget=" << shortcutTarget;
            oss << " actualDispatch=" << dispatchName
                << " resolutionInput=" << candidate.resolutionInput
                << " resolvedType=" << apps::ToString(candidate.target.type)
                << " appId=" << candidate.target.appId
                << " resolvedDispatch=" << candidate.target.dispatchLaunchName
                << " typedDispatchCandidate=" << candidate.typedDispatchCandidate
                << " typedDispatchCandidateMatchesActual=" << (candidate.typedDispatchCandidateMatchesActual ? "true" : "false")
                << " typedDispatchCandidateComparison=" << candidate.typedDispatchCandidateComparison
                << " typedDispatchCandidateStatus=" << candidate.typedDispatchCandidateStatus
                << " typedDispatchCandidateReason=" << candidate.typedDispatchCandidateReason
                << " adapterComparison=" << adapterComparison
                << " status=" << candidate.target.diagnosticStatus
                << " reason=" << candidate.target.diagnosticReason;

            if (!candidate.target.dispatchLaunchName.empty() && candidate.target.dispatchLaunchName != dispatchName) {
                if (aliasFallbackLine) {
                    *aliasFallbackLine = "[LaunchTargetShadow] nonFatalAliasOrFallback source=" + source +
                        " actualDispatch=" + dispatchName +
                        " resolvedDispatch=" + candidate.target.dispatchLaunchName;
                }
            }

            return oss.str();
        }

        static std::vector<std::string> collectLaunchTargetShadowDiagnosticLines(const std::string& source, const std::string& uiLabel, const std::string& shortcutTarget, const std::string& dispatchName, bool emitLogs) {
            std::vector<std::string> lines;
            std::string aliasFallbackLine;
            lines.push_back(buildLaunchTargetShadowDiagnosticLine(source, uiLabel, shortcutTarget, dispatchName, &aliasFallbackLine));
            if (!aliasFallbackLine.empty()) lines.push_back(aliasFallbackLine);
            const LaunchDispatchDecision dispatchDecision = DesktopService::SelectLaunchDispatch(dispatchName);
            DesktopService::RecordLaunchDispatchDecision("HostedLaunchShadowSmoke", dispatchDecision);
            std::ostringstream dispatchLine;
            dispatchLine << "[LaunchDispatch] source=" << source
                << " target=" << dispatchDecision.originalDispatch
                << " resolvedType=" << apps::ToString(dispatchDecision.target.type)
                << " appId=" << dispatchDecision.target.appId
                << " usage=" << apps::ToString(dispatchDecision.usage)
                << " selectedDispatch=" << dispatchDecision.selectedDispatch
                << " behaviorPreserved=true"
                << " smokeSelectionOnly=true"
                << " reason=" << dispatchDecision.reason;
            lines.push_back(dispatchLine.str());
            if (emitLogs) {
                for (const std::string& line : lines) Logger::write(LogLevel::Info, line);
            }
            return lines;
        }

        static void logLaunchTargetShadowDiagnostic(const std::string& source, const std::string& uiLabel, const std::string& shortcutTarget, const std::string& dispatchName) {
            collectLaunchTargetShadowDiagnosticLines(source, uiLabel, shortcutTarget, dispatchName, true);
        }

        static void logStartMenuLaunchTargetShadowDiagnostic(const std::string& originalLegacyDispatch) {
#if defined(GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY) && defined(_WIN32) && !defined(GXOS_BARE_METAL)
            // SHADOW_ONLY computes and records the typed candidate for Start Menu launches only.
            // This helper intentionally returns no dispatch string; callers must still launch
            // with originalLegacyDispatch so legacy dispatch remains authoritative.
            logLaunchTargetShadowDiagnostic("StartMenu", originalLegacyDispatch, "", originalLegacyDispatch);
#else
            logLaunchTargetShadowDiagnostic("StartMenu", originalLegacyDispatch, "", originalLegacyDispatch);
#endif
        }

        static void logDesktopShortcutLaunchTargetShadowDiagnostic(const std::string& uiLabel, const std::string& shortcutTarget, const std::string& originalLegacyDispatch) {
#if defined(GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY) && defined(_WIN32) && !defined(GXOS_BARE_METAL)
            // SHADOW_ONLY computes and records the typed candidate for hosted app shortcuts only.
            // This helper intentionally returns no dispatch string; callers must still launch
            // with originalLegacyDispatch so legacy desktop shortcut dispatch remains authoritative.
            logLaunchTargetShadowDiagnostic("DesktopShortcut", uiLabel, shortcutTarget, originalLegacyDispatch);
#else
            logLaunchTargetShadowDiagnostic("DesktopShortcut", uiLabel, shortcutTarget, originalLegacyDispatch);
#endif
        }

        std::string Compositor::RunLaunchShadowSmokeDiagnostic() {
#if !defined(_WIN32) || defined(GXOS_BARE_METAL)
            return "[LaunchTargetShadowSmoke]\n"
                "command: gui.smoke.launchshadow\n"
                "mode: unavailable\n"
                "reason: hosted-only smoke/test harness path\n"
                "nonFatal: true\n";
#else
            struct SmokeCase {
                const char* source;
                const char* uiLabel;
                const char* shortcutTarget;
                const char* actualDispatch;
            };

            static const SmokeCase kSmokeCases[] = {
                { "StartMenu", "Notepad", "", "Notepad" },
                { "StartMenu", "ComputerFiles", "", "ComputerFiles" },
                { "DesktopShortcut", "Notepad", "gxos.builtin.notepad", "Notepad" },
                { "DesktopShortcut", "FileExplorer", "gxos.builtin.fileexplorer", "FileExplorer" },
                { "StartMenu", "FakeLaunchShadowApp", "", "FakeLaunchShadowApp" }
            };

            std::ostringstream oss;
            oss << "[LaunchTargetShadowSmoke]\n";
            oss << "command: gui.smoke.launchshadow\n";
            oss << "mode: diagnostic-only\n";
            oss << "launchesApps: false\n";
            oss << "usesExistingUiShadowHelper: true\n";
            oss << "cases:\n";
            for (const SmokeCase& smokeCase : kSmokeCases) {
                std::vector<std::string> lines = collectLaunchTargetShadowDiagnosticLines(
                    smokeCase.source,
                    smokeCase.uiLabel,
                    smokeCase.shortcutTarget,
                    smokeCase.actualDispatch,
                    true);
                for (const std::string& line : lines) oss << "  " << line << "\n";
            }
            oss << "summary:\n" << DesktopService::AppModelSummaryDiagnostic();
            oss << "dispatchUsage:\n" << DesktopService::LaunchDispatchUsageDiagnostic();
            oss << "coverage:\n" << DesktopService::BuiltInAppMetadataCoverageDiagnostic();
            oss << "runtimeLaunchBehaviorChanged: false\n";
            std::string evidenceError;
            const bool evidenceWritten = DesktopService::WriteTypedDispatchHostedSmokeEvidence(evidenceError);
            oss << "evidencePath: logs/appmodel-typed-dispatch-gate-hosted.evidence.txt\n";
            oss << "evidenceWritten: " << (evidenceWritten ? "true" : "false") << "\n";
            if (!evidenceWritten) oss << "evidenceError: " << evidenceError << "\n";
            oss << "nonFatal: true\n";
            return oss.str();
#endif
        }

        void Compositor::launchAction(const std::string& act) {
            uint64_t now = nowMs();
            if (!act.empty() && act == s_lastLaunchAction && (now - s_lastLaunchTicks) < 350) {
                Logger::write(LogLevel::Info, std::string("Desktop launch skipped duplicate click: ") + act);
                return;
            }
            s_lastLaunchAction = act;
            s_lastLaunchTicks = now;
            Logger::write(LogLevel::Info, std::string("Desktop launch: ") + act);
            addRecent(act);
            if (act == "App Model Demo" || act == "AppModel") {
                const LaunchDispatchDecision dispatchDecision = DesktopService::SelectLaunchDispatch(act);
                DesktopService::RecordLaunchDispatchDecision("HostedCompositorEmbeddedAction", dispatchDecision);
                openAppModelDemoViewerWindow();
                return;
            }
            // Actually launch the application
            std::string err;
            if (!DesktopService::LaunchApp(act, err)) {
                Logger::write(LogLevel::Error, std::string("Failed to launch app: ") + act + " - " + err);
                NotificationManager::Add(err.empty() ? std::string("Failed to launch app: ") + act : err, NotificationLevel::Error);
            }
        }

        void Compositor::openStartMenuApp(const std::string& appName) {
            Logger::write(LogLevel::Info, "Start Menu context Open selected: " + appName);
            logStartMenuLaunchTargetShadowDiagnostic(appName);
            // SHADOW_ONLY observation above is diagnostic-only; launchAction still receives
            // the original legacy Start Menu dispatch string.
            launchAction(appName);
            g_startMenuVisible = false;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            requestRepaint();
#else
            g_needsRedraw = true;
#endif
        }

        bool Compositor::isStartMenuAppPinnedToDesktop(const std::string& appName) {
            const RegisteredDesktopApp* app = findDesktopAppByNameOrId(appName);
            const std::string targetId = app ? app->id : appName;
            for (const auto& shortcut : g_cfg.desktopShortcuts) {
                if ((shortcut.shortcutType.empty() || shortcut.shortcutType == "App") && shortcut.targetAppId == targetId) return true;
            }
            return false;
        }

        bool Compositor::isFilesystemEntryPinnedToDesktop(const std::string& path, bool isDirectory) {
            const std::string normalized = normalizeHostedVirtualPath(path);
            const std::string shortcutType = isDirectory ? "Folder" : "File";
            for (const auto& shortcut : g_cfg.desktopShortcuts) {
                if (shortcut.shortcutType == shortcutType && normalizeHostedVirtualPath(shortcut.targetPath) == normalized) return true;
            }
            return false;
        }

        bool Compositor::pinStartMenuAppToDesktop(const std::string& appName) {
            Logger::write(LogLevel::Info, "Pin to Desktop selected for Start Menu app: " + appName);
            const RegisteredDesktopApp* app = findDesktopAppByNameOrId(appName);
            if (!app) {
                Logger::write(LogLevel::Warn, "Pin to Desktop failed: app id resolved target not found for " + appName);
                NotificationManager::Add("Shortcut target not found", NotificationLevel::Error);
                return false;
            }
            Logger::write(LogLevel::Info, "Pin to Desktop app ID resolved: " + app->id);
            for (const auto& shortcut : g_cfg.desktopShortcuts) {
                if ((shortcut.shortcutType.empty() || shortcut.shortcutType == "App") && shortcut.targetAppId == app->id) {
                    Logger::write(LogLevel::Info, "Desktop shortcut already exists: " + appShortcutLayoutKey(app->id));
                    NotificationManager::Add("Shortcut already exists on desktop", NotificationLevel::Info);
                    return false;
                }
            }

            DesktopShortcutRec shortcut;
            shortcut.shortcutType = "App";
            shortcut.targetAppId = app->id;
            shortcut.label = app->displayName;
            const apps::BuiltInAppMetadata* metadata = hostedBuiltInMetadataForRegisteredApp(app);
            shortcut.iconName = app->icon;
            if (shortcut.iconName.empty() && metadata && metadata->iconKey) shortcut.iconName = metadata->iconKey;
            if (shortcut.iconName.empty()) shortcut.iconName = startMenuLogicalIconName(app->displayName);
            g_cfg.desktopShortcuts.push_back(shortcut);
            Logger::write(LogLevel::Info, "Desktop shortcut created: " + appShortcutLayoutKey(app->id));
            refreshDesktopItems();
            const std::string shortcutKey = appShortcutLayoutKey(app->id);
            for (const auto& item : g_items) {
                if (desktopLayoutKey(item) == shortcutKey) {
                    upsertDesktopIconPosition(g_cfg.iconPositions, shortcutKey, item.ix, item.iy);
                    Logger::write(LogLevel::Info, "Desktop shortcut position persisted: " + shortcutKey +
                        " x=" + std::to_string(item.ix) + " y=" + std::to_string(item.iy));
                    break;
                }
            }
            saveDesktopConfig();
            Logger::write(LogLevel::Info, "Desktop shortcut persisted: " + appShortcutLayoutKey(app->id));
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            requestRepaint();
#else
            g_needsRedraw = true;
#endif
            return true;
        }

        bool Compositor::pinFilesystemEntryToDesktop(const std::string& path, bool isDirectory, const std::string& label, const std::string& iconName) {
            const std::string normalized = normalizeHostedVirtualPath(path);
            std::string shortcutType = isDirectory ? "Folder" : "File";
            Logger::write(LogLevel::Info, "Pin to Desktop selected for filesystem entry: " + normalized +
                " type=" + shortcutType);
            Logger::write(LogLevel::Info, "Pin to Desktop target path resolved: " + normalized);

            if (isDesktopFolderPath(normalized)) {
                Logger::write(LogLevel::Info, "Pin to Desktop skipped: item is already on desktop: " + normalized);
                NotificationManager::Add("Item is already on desktop", NotificationLevel::Info);
                return false;
            }

            std::error_code ec;
            std::filesystem::path hostPath = hostedPathForVirtual(normalized);
            if (!std::filesystem::exists(hostPath, ec) || ec) {
                Logger::write(LogLevel::Warn, "Pin to Desktop failed: target path missing " + normalized);
                NotificationManager::Add("Shortcut target missing", NotificationLevel::Error);
                return false;
            }
            const bool actualIsDirectory = std::filesystem::is_directory(hostPath, ec) && !ec;
            if (actualIsDirectory != isDirectory) {
                isDirectory = actualIsDirectory;
                shortcutType = isDirectory ? "Folder" : "File";
                Logger::write(LogLevel::Info, "Pin to Desktop target kind resolved from filesystem: " + shortcutType);
            }
            const std::string shortcutKey = filesystemShortcutLayoutKey(shortcutType, normalized);

            if (isFilesystemEntryPinnedToDesktop(normalized, isDirectory)) {
                Logger::write(LogLevel::Info, "Desktop shortcut already exists: " + shortcutKey);
                NotificationManager::Add("Shortcut already exists on desktop", NotificationLevel::Info);
                return false;
            }

            DesktopShortcutRec shortcut;
            shortcut.shortcutType = shortcutType;
            shortcut.targetPath = normalized;
            shortcut.label = label.empty() ? basenameForVirtualPath(normalized) : label;
            shortcut.iconName = iconName;
            if (shortcut.iconName.empty()) {
                apps::ExplorerFileEntry entry;
                entry.name = shortcut.label;
                entry.fullPath = normalized;
                entry.kind = isDirectory ? apps::ExplorerEntryKind::Directory : apps::ExplorerEntryKind::File;
                shortcut.iconName = apps::FileIconProvider::logicalIconNameForEntry(entry);
            }
            g_cfg.desktopShortcuts.push_back(shortcut);
            Logger::write(LogLevel::Info, "Desktop shortcut created: " + shortcutKey);

            refreshDesktopItems();
            for (const auto& item : g_items) {
                if (desktopLayoutKey(item) == shortcutKey) {
                    upsertDesktopIconPosition(g_cfg.iconPositions, shortcutKey, item.ix, item.iy);
                    Logger::write(LogLevel::Info, "Desktop shortcut position persisted: " + shortcutKey +
                        " x=" + std::to_string(item.ix) + " y=" + std::to_string(item.iy));
                    break;
                }
            }
            saveDesktopConfig();
            Logger::write(LogLevel::Info, "Desktop shortcut persisted: " + shortcutKey);
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            requestRepaint();
#else
            g_needsRedraw = true;
#endif
            return true;
        }

        bool Compositor::unpinStartMenuAppFromDesktop(const std::string& appName) {
            const RegisteredDesktopApp* app = findDesktopAppByNameOrId(appName);
            const std::string targetId = app ? app->id : appName;
            for (auto it = g_cfg.desktopShortcuts.begin(); it != g_cfg.desktopShortcuts.end(); ++it) {
                if ((it->shortcutType.empty() || it->shortcutType == "App") && it->targetAppId == targetId) {
                    Logger::write(LogLevel::Info, "Desktop shortcut removed: " + appShortcutLayoutKey(targetId));
                    g_cfg.desktopShortcuts.erase(it);
                    refreshDesktopItems();
                    saveDesktopConfig();
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                    requestRepaint();
#else
                    g_needsRedraw = true;
#endif
                    return true;
                }
            }
            Logger::write(LogLevel::Info, "Desktop shortcut remove skipped, not pinned: " + targetId);
            return false;
        }

        bool Compositor::removeDesktopShortcut(int index) {
            if (index < 0 || index >= (int)g_items.size()) return false;
            const DesktopItem item = g_items[index];
            if (item.kind != DesktopItemKind::Shortcut) {
                Logger::write(LogLevel::Warn, "Remove from Desktop ignored for non-shortcut desktop item");
                return false;
            }
            for (auto it = g_cfg.desktopShortcuts.begin(); it != g_cfg.desktopShortcuts.end(); ++it) {
                const std::string recType = it->shortcutType.empty() ? "App" : it->shortcutType;
                const bool matchesApp = recType == "App" && item.shortcutType == "App" && it->targetAppId == item.targetAppId;
                const bool matchesFile = (recType == "File" || recType == "Folder") && recType == item.shortcutType &&
                    normalizeHostedVirtualPath(it->targetPath) == normalizeHostedVirtualPath(item.path);
                if (matchesApp || matchesFile) {
                    Logger::write(LogLevel::Info, "Shortcut removed from desktop: " + desktopLayoutKey(item));
                    g_cfg.desktopShortcuts.erase(it);
                    refreshDesktopItems();
                    saveDesktopConfig();
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                    requestRepaint();
#else
                    g_needsRedraw = true;
#endif
                    return true;
                }
            }
            Logger::write(LogLevel::Warn, "Shortcut removal failed; record not found: " + desktopLayoutKey(item));
            return false;
        }

        bool Compositor::openDesktopShortcutTargetLocation(int index) {
            if (index < 0 || index >= (int)g_items.size()) return false;
            const DesktopItem item = g_items[index];
            if (item.kind != DesktopItemKind::Shortcut || (item.shortcutType != "File" && item.shortcutType != "Folder")) {
                Logger::write(LogLevel::Warn, "Open Target Location ignored for non-filesystem shortcut");
                return false;
            }
            const std::string normalized = normalizeHostedVirtualPath(item.path);
            std::string targetLocation = normalized;
            if (item.shortcutType == "File") {
                size_t slash = normalized.find_last_of('/');
                targetLocation = slash == 0 ? "/" : normalized.substr(0, slash);
            }
            Logger::write(LogLevel::Info, "Desktop shortcut Open Target Location selected: " + desktopLayoutKey(item) +
                " location=" + targetLocation);
            apps::FileExplorer::Launch(targetLocation);
            return true;
        }

        bool Compositor::hostedDesktopCanNavigateTo(const std::string& path) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            const std::string normalized = normalizeHostedVirtualPath(path);
            if (!hostedDesktopIsWithinLiveRoot(normalized)) {
                Logger::write(LogLevel::Warn, "Hosted desktop navigation rejected outside live root: " + normalized);
                return false;
            }

            std::string ensureError;
            const bool createIfMissing = isDesktopFolderRootPath(normalized);
            if (!DesktopFolderResolver::EnsureExists(normalized, ensureError, createIfMissing)) {
                Logger::write(LogLevel::Warn, "Hosted desktop navigation target unavailable: " + ensureError);
                return false;
            }

            return true;
#else
            (void)path;
            return false;
#endif
        }

        bool Compositor::hostedDesktopSetCurrentPath(const std::string& path, bool pushHistory) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            const std::string normalized = normalizeHostedVirtualPath(path);
            if (!hostedDesktopCanNavigateTo(normalized)) return false;
            const std::string current = normalizeHostedVirtualPath(g_hostedDesktopDirectoryPath);
            if (normalized == current) {
                Logger::write(LogLevel::Info, "Hosted desktop navigation already at path=" + normalized);
                return true;
            }

            if (pushHistory && !current.empty() && current != normalized) {
                g_hostedDesktopBackHistory.push_back(current);
            }
            g_hostedDesktopDirectoryPath = normalized;
            Logger::write(LogLevel::Info, "Hosted desktop current path=" + g_hostedDesktopDirectoryPath);
            refreshDesktopItems();
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            requestRepaint();
#endif
            return true;
#else
            (void)path;
            (void)pushHistory;
            return false;
#endif
        }

        bool Compositor::hostedDesktopGoBack() {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            if (g_hostedDesktopBackHistory.empty()) {
                Logger::write(LogLevel::Info, "Hosted desktop back navigation ignored: no history");
                return false;
            }

            const std::string target = g_hostedDesktopBackHistory.back();
            g_hostedDesktopBackHistory.pop_back();
            Logger::write(LogLevel::Info, "Hosted desktop navigating back to " + target);
            return hostedDesktopSetCurrentPath(target, false);
#else
            return false;
#endif
        }

        bool Compositor::hostedDesktopGoHome() {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            const std::string root = hostedDesktopRootPath();
            Logger::write(LogLevel::Info, "Hosted desktop navigating home to " + root);
            return hostedDesktopSetCurrentPath(root, true);
#else
            return false;
#endif
        }

        void Compositor::openDesktopItem(int index) {
            if (index < 0 || index >= (int)g_items.size()) return;
            const DesktopItem& item = g_items[index];
            Logger::write(LogLevel::Info, std::string("Desktop item open requested: ") + desktopLayoutKey(item));

            std::string err;
            if (item.kind == DesktopItemKind::SystemObject) {
                switch (item.systemObject) {
                    case DesktopSystemObjectKind::Trash:
                        launchAction("Trash");
                        return;
                    case DesktopSystemObjectKind::ThisSystem:
                        apps::FileExplorer::Launch("/");
                        return;
                    case DesktopSystemObjectKind::FileManager:
                    case DesktopSystemObjectKind::FileExplorer:
                        apps::FileExplorer::Launch();
                        return;
                    case DesktopSystemObjectKind::SystemSettings:
                        launchAction("ControlPanel");
                        return;
                    case DesktopSystemObjectKind::DesktopBack:
                        if (hostedDesktopGoBack()) return;
                        err = "No previous hosted desktop folder";
                        break;
                    case DesktopSystemObjectKind::DesktopHome:
                        if (hostedDesktopGoHome()) return;
                        err = "Unable to return to hosted desktop root";
                        break;
                    default:
                        err = "Unknown system desktop object: " + item.label;
                        break;
                }
            } else if (item.kind == DesktopItemKind::FilesystemEntry) {
                if (item.isDirectory) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                    if (hostedDesktopSetCurrentPath(item.path, true)) return;
#endif
                }
                if (DesktopService::OpenFilesystemEntry(item.path, item.isDirectory, err)) return;
            } else if (item.kind == DesktopItemKind::Shortcut && item.shortcutType == "App") {
                const RegisteredDesktopApp* app = findDesktopAppByNameOrId(item.targetAppId);
                if (!app) {
                    err = "Shortcut target not found";
                } else {
                    Logger::write(LogLevel::Info, "Desktop shortcut launched: " + desktopLayoutKey(item) + " -> " + app->displayName);
                    logDesktopShortcutLaunchTargetShadowDiagnostic(item.label, item.targetAppId, app->displayName);
                    // SHADOW_ONLY observation above is diagnostic-only; launchAction still receives
                    // the original legacy desktop shortcut dispatch string.
                    launchAction(app->displayName);
                    return;
                }
            } else if (item.kind == DesktopItemKind::Shortcut && (item.shortcutType == "File" || item.shortcutType == "Folder")) {
                const std::string normalized = normalizeHostedVirtualPath(item.path);
                std::error_code ec;
                std::filesystem::path hostPath = hostedPathForVirtual(normalized);
                if (!std::filesystem::exists(hostPath, ec) || ec) {
                    err = "Shortcut target missing";
                } else {
                    bool isDirectory = item.shortcutType == "Folder";
                    Logger::write(LogLevel::Info, "Desktop shortcut opened: " + desktopLayoutKey(item));
                    if (DesktopService::OpenFilesystemEntry(normalized, isDirectory, err)) return;
                }
            } else {
                err = "Desktop shortcuts are not implemented yet";
            }

            Logger::write(LogLevel::Warn, "Desktop item open failed: " + err);
            NotificationManager::Add(err.empty() ? "Unable to open desktop item" : err, NotificationLevel::Error);
        }

        void Compositor::requestDesktopRefresh() {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            Logger::write(LogLevel::Info,
                std::string("Desktop icon refresh requested; trash items=") + std::to_string(hostedTrashItemCount()));
            refreshDesktopItems();
            requestRepaint();
#endif
        }

        bool Compositor::showFolderOnHostedDesktop(const std::string& path) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            const std::string normalized = normalizeHostedVirtualPath(path);
            Logger::write(LogLevel::Info, "Hosted desktop show-on-desktop requested path=" + normalized);
            return hostedDesktopSetCurrentPath(normalized, true);
#else
            (void)path;
            return false;
#endif
        }

        bool Compositor::hostedDesktopUsesCompactIconLayout() {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            return !isDesktopFolderRootPath(g_hostedDesktopDirectoryPath) && g_cfg.smallLiveDesktopFolderIcons;
#else
            return false;
#endif
        }

        bool Compositor::hostedDesktopPrefersCompactFolderIcons() {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            return g_cfg.smallLiveDesktopFolderIcons;
#else
            return false;
#endif
        }

        bool Compositor::setHostedDesktopPrefersCompactFolderIcons(bool smallIcons) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            if (g_cfg.smallLiveDesktopFolderIcons == smallIcons) {
                Logger::write(LogLevel::Info,
                    std::string("Hosted desktop folder icon preference unchanged: ") + (smallIcons ? "small" : "normal"));
                return false;
            }
            g_cfg.smallLiveDesktopFolderIcons = smallIcons;
            g_cfg.taskbarPosition = taskbarPositionName(g_taskbarPosition);
            std::string err;
            const bool saved = DesktopConfig::Save("desktop.json", g_cfg, err);
            if (!saved) {
                Logger::write(LogLevel::Error, "Hosted desktop folder icon preference save failed: " + err);
            } else {
                Logger::write(LogLevel::Info, "Hosted desktop folder icon preference persisted");
            }
            refreshDesktopItems();
            invalidate(0);
            Logger::write(LogLevel::Info,
                std::string("Hosted desktop folder icon preference set: ") + (smallIcons ? "small" : "normal"));
            return saved;
#else
            (void)smallIcons;
            return false;
#endif
        }

        bool Compositor::hostedDesktopAutoArrangeIcons() {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            return g_cfg.autoArrangeDesktopIcons;
#else
            return false;
#endif
        }

        bool Compositor::setHostedDesktopAutoArrangeIcons(bool enabled) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            if (g_cfg.autoArrangeDesktopIcons == enabled) {
                Logger::write(LogLevel::Info,
                    std::string("Hosted desktop auto-arrange preference unchanged: ") + (enabled ? "enabled" : "disabled"));
                return false;
            }
            g_cfg.autoArrangeDesktopIcons = enabled;
            g_cfg.taskbarPosition = taskbarPositionName(g_taskbarPosition);
            std::string err;
            const bool saved = DesktopConfig::Save("desktop.json", g_cfg, err);
            if (!saved) {
                Logger::write(LogLevel::Error, "Hosted desktop auto-arrange preference save failed: " + err);
            } else {
                Logger::write(LogLevel::Info, "Hosted desktop auto-arrange preference persisted");
            }
            refreshDesktopItems();
            invalidate(0);
            Logger::write(LogLevel::Info,
                std::string("Hosted desktop auto-arrange preference set: ") + (enabled ? "enabled" : "disabled"));
            return saved;
#else
            (void)enabled;
            return false;
#endif
        }

        void Compositor::arrangeHostedDesktopIcons() {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            arrangeDesktopIconsInGrid(g_items);
            g_cfg.iconPositions.clear();
            for (const auto& item : g_items) {
                DesktopIconPos pos;
                pos.name = desktopLayoutKey(item);
                pos.x = item.ix;
                pos.y = item.iy;
                g_cfg.iconPositions.push_back(pos);
            }
            saveDesktopConfig();
            invalidate(0);
            Logger::write(LogLevel::Info, "Hosted desktop icons arranged into grid");
#endif
        }

        void Compositor::ClearDesktopIconSelection( ) {
            bool hadSelection = !g_selectedDesktopIconIndices.empty( );
            for (int idx : g_selectedDesktopIconIndices) {
                if (idx >= 0 && idx < (int)g_items.size( )) g_items[idx].selected = false;
            }
            for (auto& item : g_items) item.selected = false;
            g_selectedDesktopIconIndices.clear( );
            g_lastSelectedDesktopIconIndex = -1;
            if (hadSelection) Logger::write(LogLevel::Info, "Desktop icon selection cleared");
        }

        void Compositor::SelectDesktopIcon(int index, bool additive) {
            if (index < 0 || index >= (int)g_items.size( )) return;
            if (!additive) ClearDesktopIconSelection( );
            g_items[index].selected = true;
            g_selectedDesktopIconIndices.insert(index);
            g_lastSelectedDesktopIconIndex = index;
            Logger::write(LogLevel::Info, std::string("Desktop icon selected: ") + g_items[index].label + " count=" + std::to_string(g_selectedDesktopIconIndices.size( )));
        }

        void Compositor::ToggleDesktopIconSelection(int index) {
            if (index < 0 || index >= (int)g_items.size( )) return;
            auto it = g_selectedDesktopIconIndices.find(index);
            if (it != g_selectedDesktopIconIndices.end( )) {
                g_selectedDesktopIconIndices.erase(it);
                g_items[index].selected = false;
                if (g_lastSelectedDesktopIconIndex == index) {
                    g_lastSelectedDesktopIconIndex = g_selectedDesktopIconIndices.empty( ) ? -1 : *g_selectedDesktopIconIndices.rbegin( );
                }
                Logger::write(LogLevel::Info, std::string("Desktop icon deselected: ") + g_items[index].label + " count=" + std::to_string(g_selectedDesktopIconIndices.size( )));
            } else {
                g_items[index].selected = true;
                g_selectedDesktopIconIndices.insert(index);
                g_lastSelectedDesktopIconIndex = index;
                Logger::write(LogLevel::Info, std::string("Desktop icon toggled selected: ") + g_items[index].label + " count=" + std::to_string(g_selectedDesktopIconIndices.size( )));
            }
        }

        void Compositor::SelectDesktopIconRange(int startIndex, int endIndex) {
            if (g_items.empty( )) return;
            if (startIndex < 0 || startIndex >= (int)g_items.size( )) startIndex = endIndex;
            if (endIndex < 0 || endIndex >= (int)g_items.size( )) return;
            ClearDesktopIconSelection( );
            int first = std::min(startIndex, endIndex);
            int last = std::max(startIndex, endIndex);
            for (int i = first; i <= last; ++i) {
                g_items[i].selected = true;
                g_selectedDesktopIconIndices.insert(i);
            }
            g_lastSelectedDesktopIconIndex = endIndex;
            Logger::write(LogLevel::Info, "Desktop icon range selected: " + std::to_string(first) + "-" + std::to_string(last) + " count=" + std::to_string(g_selectedDesktopIconIndices.size( )));
        }

        std::vector<int> Compositor::GetSelectedDesktopIconIndices( ) {
            return std::vector<int>(g_selectedDesktopIconIndices.begin( ), g_selectedDesktopIconIndices.end( ));
        }

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        bool Compositor::IsCtrlDown( ) {
            // Hosted compositor receives Win32 mouse messages; GetKeyState is the local modifier source.
            return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        }

        bool Compositor::IsShiftDown( ) {
            // Hosted compositor receives Win32 mouse messages; GetKeyState is the local modifier source.
            return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        }

        RECT Compositor::GetDesktopIconBounds(int index) {
            if (index < 0 || index >= (int)g_items.size( )) return RECT{ 0,0,0,0 };
            const DesktopGridMetrics metrics = desktopGridMetrics();
            const int cellH = desktopIconCellHeightForItem(g_items[index], metrics);
            return RECT{ g_items[index].ix, g_items[index].iy, g_items[index].ix + metrics.cellW, g_items[index].iy + cellH };
        }

        int Compositor::HitTestDesktopIcon(int mouseX, int mouseY) {
            for (int i = 0; i < (int)g_items.size( ); ++i) {
                RECT bounds = GetDesktopIconBounds(i);
                if (mouseX >= bounds.left && mouseX < bounds.right && mouseY >= bounds.top && mouseY < bounds.bottom) return i;
            }
            return -1;
        }

        static RECT normalizedRect(int x1, int y1, int x2, int y2) {
            return RECT{ std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2) };
        }

        void Compositor::SelectIconsInRectangle(const RECT& selectionRect, bool additive) {
            std::set<int> before = g_selectedDesktopIconIndices;
            if (!additive) {
                for (int idx : g_selectedDesktopIconIndices) {
                    if (idx >= 0 && idx < (int)g_items.size( )) g_items[idx].selected = false;
                }
                g_selectedDesktopIconIndices.clear( );
                g_lastSelectedDesktopIconIndex = -1;
            }
            for (int i = 0; i < (int)g_items.size( ); ++i) {
                RECT iconBounds = GetDesktopIconBounds(i);
                RECT intersection{};
                if (IntersectRect(&intersection, &selectionRect, &iconBounds)) {
                    g_items[i].selected = true;
                    g_selectedDesktopIconIndices.insert(i);
                    g_lastSelectedDesktopIconIndex = i;
                }
            }
            if (before != g_selectedDesktopIconIndices) {
                Logger::write(LogLevel::Info, "Desktop drag selection count=" + std::to_string(g_selectedDesktopIconIndices.size( )));
            }
        }
#endif

        WinInfo* Compositor::hitWindowAt(int mx, int my) { for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { uint64_t wid = g_z[idx]; auto it = g_windows.find(wid); if (it == g_windows.end( )) continue; WinInfo& w = it->second; if (w.minimized || w.tombstoned) continue; if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h) return &w; } return nullptr; }
        bool Compositor::isDialogTitle(const std::string& title) { return title == "Save As" || title == "Open" || title == "Unsaved Changes"; }
        bool Compositor::blockInputBehindModal(int mx, int my) { if (g_modalWindow == 0) return false; auto modalIt = g_windows.find(g_modalWindow); if (modalIt == g_windows.end( ) || modalIt->second.minimized || modalIt->second.tombstoned) { g_modalWindow = 0; return false; } WinInfo& modal = modalIt->second; bool inside = mx >= modal.x && mx < modal.x + modal.w && my >= modal.y && my < modal.y + modal.h; if (!inside) { for (auto it = g_z.begin( ); it != g_z.end( ); ++it) { if (*it == modal.id) { g_z.erase(it); break; } } g_z.push_back(modal.id); g_focus = modal.id; return true; } return false; }
        uint64_t Compositor::inputOwnerPid() { uint64_t ownerPid = 0; uint64_t focusId = g_modalWindow ? g_modalWindow : g_focus; auto it = g_windows.find(focusId); if (it != g_windows.end( ) && !it->second.minimized && !it->second.tombstoned) { ownerPid = it->second.ownerPid; } return ownerPid; }
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        uint64_t Compositor::hitTestTaskbarButton(int mx, int my, RECT cr, int taskbarH) {
            (void)taskbarH;
            const DesktopTheme& theme = GetCurrentDesktopTheme();
            WorkRect tb = taskbarRectForBounds(cr.right - cr.left, cr.bottom - cr.top);
            if (mx < tb.left || mx >= tb.right || my < tb.top || my >= tb.bottom) return 0;
            bool vertical = g_taskbarPosition == TaskbarPosition::Left || g_taskbarPosition == TaskbarPosition::Right;
            int btnX = vertical ? tb.left + 4 : 216;
            int btnY = vertical ? tb.top + 48 : tb.top + 6;
            for (uint64_t id : g_z) {
                auto it = g_windows.find(id);
                if (it == g_windows.end( )) continue;
                std::string label = it->second.title;
                int bw = vertical ? (tb.right - tb.left - 8) : measureUiText(label.c_str(), (int)label.size(), FontRole::Small) + theme.taskbarItemPadding * 2 + 12;
                if (!vertical && bw > 180) bw = 180;
                int bh = vertical ? 28 : (tb.bottom - tb.top - 12);
                RECT br{ btnX, btnY, btnX + bw, btnY + bh };
                if (mx >= br.left && mx <= br.right && my >= br.top && my <= br.bottom) return id;
                if (vertical) btnY += bh + 4; else btnX += bw + theme.taskbarItemPadding / 2;
            }
            return 0;
        }
        void Compositor::initWindow( ) { WNDCLASSA wc{}; wc.style = CS_OWNDC; wc.lpfnWndProc = Compositor::WndProc; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "GXOS_COMPOSITOR"; RegisterClassA(&wc); g_hwnd = CreateWindowExA(0, wc.lpszClassName, "guideXOSCpp Compositor", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768, nullptr, nullptr, wc.hInstance, nullptr); g_startBtnBmp = (HBITMAP)LoadImageA(nullptr, "assets/start_button.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            g_hostedFreezeDiag.enabled = hostedFreezeDiagnosticsEnabled( );
            if (g_hostedFreezeDiag.enabled) {
                UINT_PTR timerId = SetTimer(g_hwnd, kHostedFreezeDiagTimerId, 1000, nullptr);
                g_hostedFreezeDiag.timerInstalled = (timerId != 0);
                if (g_hostedFreezeDiag.timerInstalled) {
                    Logger::write(LogLevel::Info, "Hosted freeze diagnostics enabled (GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS=1, timer=1s)");
                } else {
                    Logger::write(LogLevel::Warn, "Hosted freeze diagnostics requested but SetTimer failed");
                }
            }
#endif
        }
        void Compositor::shutdownWindow( ) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            if (g_hostedFreezeDiag.timerInstalled && g_hwnd) {
                KillTimer(g_hwnd, kHostedFreezeDiagTimerId);
                g_hostedFreezeDiag.timerInstalled = false;
            }
#endif
            releaseHostedPaintSurface();
            if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
        }
        void Compositor::requestRepaint( ) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            hostedFreezeDiagnosticsOnRequestRepaint( );
#endif
            if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
        }
        void Compositor::drawDesktopIcons(HDC dc, RECT cr) {
            const DesktopGridMetrics metrics = desktopGridMetrics();
            HFONT font = (HFONT)GetStockObject(ANSI_VAR_FONT); SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); POINT cursor; GetCursorPos(&cursor); ScreenToClient(g_hwnd, &cursor); int idx = 0; for (auto& it : g_items) {
                int x = it.ix; int y = it.iy; const int cellW = metrics.cellW; const int iconW = metrics.iconW; const int iconH = metrics.iconH; const int cellH = desktopIconCellHeightForItem(it, metrics); const int labelMaxW = cellW - 8; std::vector<std::string> labelLines = wrapUiTextToWidth(it.label, labelMaxW, FontRole::Small, static_cast<size_t>(std::max(1, metrics.labelMaxLines))); RECT cell{ x, y, x + cellW, y + cellH }; bool hover = (cursor.x >= cell.left && cursor.x <= cell.right && cursor.y >= cell.top && cursor.y <= cell.bottom);
                if (it.selected) {
                    HBRUSH sel = CreateSolidBrush(RGB(50, 90, 160)); FillRect(dc, &cell, sel); DeleteObject(sel); 
                    // Draw focus indicator for selected icon
                    FocusIndicator::DrawFocusRect(dc, cell.left, cell.top, cellW, cellH, 4, 2, 3);
                } else if (hover) { HBRUSH hov = CreateSolidBrush(RGB(50, 55, 65)); FillRect(dc, &cell, hov); DeleteObject(hov); HPEN hovP = CreatePen(PS_SOLID, 1, RGB(80, 100, 140)); HGDIOBJ oP = SelectObject(dc, hovP); HGDIOBJ oB = SelectObject(dc, GetStockObject(NULL_BRUSH)); Rectangle(dc, cell.left, cell.top, cell.right, cell.bottom); SelectObject(dc, oP); SelectObject(dc, oB); DeleteObject(hovP); }
                const int iconTopPad = desktopIconTopPadding(metrics);
                const int labelTopPad = desktopIconLabelPadding(metrics);
                RECT iconR{ x + (cellW - iconW) / 2, y + iconTopPad, x + (cellW - iconW) / 2 + iconW, y + iconTopPad + iconH };
                std::string lbl = it.label;
                if (!drawDesktopThemedIcon(dc, iconR, it)) {
                    if (!it.iconName.empty()) Logger::write(LogLevel::Warn, "Desktop icon fallback used for " + it.label + " logical=" + it.iconName);
                    COLORREF iconColor = startMenuFallbackIconColor(lbl);
                    if (lbl == "ImageViewer" || lbl == "Image Viewer") iconColor = RGB(200, 120, 60);
                    else if (lbl == "System Settings") iconColor = RGB(140, 90, 180);
                    else if (lbl == "Files" || lbl == "ComputerFiles" || lbl == "File Manager" || lbl == "This System" || it.isDirectory) iconColor = RGB(200, 180, 60);
                    else if (it.pinned && iconColor == RGB(90, 100, 120)) iconColor = RGB(90, 140, 220);
                    HBRUSH ib = CreateSolidBrush(iconColor); FillRect(dc, &iconR, ib); DeleteObject(ib);
                    {
                        int cx = iconR.left + (iconW / 2); int cy = iconR.top + (iconH / 2);
                        HBRUSH inner = CreateSolidBrush(RGB(GetRValue(iconColor) + 40 > 255 ? 255 : GetRValue(iconColor) + 40, GetGValue(iconColor) + 40 > 255 ? 255 : GetGValue(iconColor) + 40, GetBValue(iconColor) + 40 > 255 ? 255 : GetBValue(iconColor) + 40));
                        RECT innerR{ cx - 10, cy - 10, cx + 10, cy + 10 }; FillRect(dc, &innerR, inner); DeleteObject(inner);
                    }
                    HPEN iconFrame = CreatePen(PS_SOLID, 1, RGB(180, 180, 200)); HGDIOBJ oP2 = SelectObject(dc, iconFrame); HGDIOBJ oB2 = SelectObject(dc, GetStockObject(NULL_BRUSH)); Rectangle(dc, iconR.left, iconR.top, iconR.right, iconR.bottom); SelectObject(dc, oP2); SelectObject(dc, oB2); DeleteObject(iconFrame);
                }
                // Label with text shadow
                int labelY = iconR.bottom + labelTopPad;
                int lineH = uiTextHeight(FontRole::Small);
                for (const std::string& line : labelLines) {
                    int lineW = measureUiText(line.c_str(), static_cast<int>(line.size()), FontRole::Small);
                    int lx = x + (cellW - lineW) / 2;
                    drawUiText(dc, lx + 1, labelY + 1, line, RGB(0, 0, 0), FontRole::Small);
                    drawUiText(dc, lx, labelY, line, RGB(230, 230, 240), FontRole::Small);
                    labelY += lineH;
                }
                // Pin indicator
                if (it.pinned && it.kind == DesktopItemKind::Shortcut) { const char* pin = "*"; drawUiText(dc, iconR.right - 10, iconR.top + 2, pin, 1, RGB(255, 200, 60), FontRole::SmallBold); }
                idx++;
            }
            if (g_iconSelectionDragPending || g_iconSelectionDragActive) {
                RECT selRect = normalizedRect(g_iconSelectionStartX, g_iconSelectionStartY, g_iconSelectionCurrentX, g_iconSelectionCurrentY);
                if (selRect.right > selRect.left && selRect.bottom > selRect.top) {
                    HBRUSH fill = CreateHatchBrush(HS_DIAGCROSS, RGB(70, 130, 220));
                    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, fill);
                    HPEN pen = CreatePen(PS_DOT, 1, RGB(110, 170, 245));
                    HPEN oldPen = (HPEN)SelectObject(dc, pen);
                    SetBkMode(dc, TRANSPARENT);
                    Rectangle(dc, selRect.left, selRect.top, selRect.right, selRect.bottom);
                    SelectObject(dc, oldPen);
                    SelectObject(dc, oldBrush);
                    DeleteObject(pen);
                    DeleteObject(fill);
                }
            }
        }

        // helper: draw a bitmap centered in rect
        static void drawBitmapCentered(HDC dc, HBITMAP hb, RECT r) { if (!hb) return; HDC mem = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(mem, hb); BITMAP bm{}; GetObject(hb, sizeof(bm), &bm); int w = bm.bmWidth, h = bm.bmHeight; int dx = r.left + ((r.right - r.left) - w) / 2; int dy = r.top + ((r.bottom - r.top) - h) / 2; BitBlt(dc, dx, dy, w, h, mem, 0, 0, SRCCOPY); SelectObject(mem, old); DeleteDC(mem); }

        LRESULT CALLBACK Compositor::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
            switch (msg) {
            case WM_CLOSE: PostQuitMessage(0); return 0;
            case WM_SIZE: { RECT cr; GetClientRect(h, &cr); WorkRect work = desktopWorkAreaForBounds(cr.right - cr.left, cr.bottom - cr.top); std::lock_guard<std::mutex> lk(g_lock); for (auto& kv : g_windows) { WinInfo& wi = kv.second; if (wi.maximized) { wi.x = work.left; wi.y = work.top; wi.w = work.right - work.left; wi.h = work.bottom - work.top; wi.dirty = true; } } requestRepaint( ); return 0; }
            case WM_ERASEBKGND: return 1;
            case WM_TIMER:
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                if (w == kHostedFreezeDiagTimerId) {
                    hostedFreezeDiagnosticsOnTimer( );
                    return 0;
                }
#endif
                break;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                const uint64_t paintStartMs = nowMs( );
                uint64_t vncDurationMs = 0;
                HDC visibleDc = BeginPaint(h, &ps);
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                hostedFreezeDiagnosticsOnPaintBegin( );
#endif
                RECT cr;
                GetClientRect(h, &cr);
                const int clientW = cr.right - cr.left;
                const int clientH = cr.bottom - cr.top;
                HDC drawDc = visibleDc;
                const bool hostedOffscreenReady = ensureHostedPaintSurface(visibleDc, clientW, clientH);
                if (hostedOffscreenReady && s_hostedPaintDc) {
                    drawDc = s_hostedPaintDc;
                }
#define dc drawDc
                // Hosted paint lifecycle: redraw the full client frame into the
                // offscreen buffer, then compose the window stack on top.
                const DesktopTheme& theme = GetCurrentDesktopTheme();
                const bool sciFiTheme = hostedSciFiTheme(theme);
                auto colorFromTheme = [](uint32_t value) -> COLORREF {
                    return RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
                };
                const bool themeDesktopSurface = sciFiTheme && (!g_wallpaperImage || !g_wallpaperImage->isValid());
                DesktopWallpaper::DrawGradient(dc, cr,
                    themeDesktopSurface ? hostedDesktopTopColor(theme) : g_gradientTopColor,
                    themeDesktopSurface ? hostedDesktopBottomColor(theme) : g_gradientBottomColor,
                    themeDesktopSurface ? hostedDesktopAccentColor(theme) : g_gradientAccentColor,
                    !g_wallpaperImage);
                if (g_wallpaperImage && g_wallpaperImage->isValid()) { drawBackgroundImageToHdc(dc, cr, g_wallpaperImage, WallpaperRegistry::ParseScaleMode(g_backgroundScaleMode)); } else { DesktopWallpaper::DrawBranding(dc, cr); } drawDesktopIcons(dc, cr);
#if defined(GXOS_SYSTEM_FONT_DEMO)
                drawSystemFontDemo(dc, cr);
#endif
                // Draw application windows in Z-order (bottom to top)
                const int titleBarH = theme.titleBarHeight;
                const int windowCornerRadius = WindowRenderer::GetWindowChromeCornerRadius(theme);
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
                    
                    // Phase 2D: hosted-only Sci Fi soft shadow preview. The
                    // offscreen hosted paint path redraws the full client
                    // frame before windows, so this conservative shadow can
                    // extend slightly beyond the shell without stale pixels.
                    WindowRenderer::DrawWindowShadow(dc, winfo.x, winfo.y, winfo.w, winfo.h, theme);

                    // Draw window glow/shadow (matching Legacy)
                    WindowRenderer::DrawWindowGlow(dc, winfo.x, winfo.y, winfo.w, winfo.h, titleBarH, isFocused);
                    
                    // Draw window content background
                    WindowRenderer::DrawRoundedRect(dc, winfo.x, winfo.y, winfo.w, winfo.h,
                        colorFromTheme(theme.windowBackground),
                        windowCornerRadius);
                    
                    // Draw title bar
                    WindowRenderer::DrawTitleBar(dc, winfo.x, winfo.y, winfo.w, titleBarH, isFocused);
                    
                    // Draw window border
                    WindowRenderer::DrawWindowBorder(dc, winfo.x, winfo.y, winfo.w, winfo.h, isFocused);
                    
                    // Draw window title text
                    if (UISettings::EnableWindowTitles) {
                    SystemFont::DrawText(dc, winfo.x + theme.titleTextInset, centeredUiTextY(winfo.y, titleBarH, FontRole::Title), winfo.title.c_str(), (int)winfo.title.size(), colorFromTheme(theme.titleBarText), FontRole::Title);
                    }

                    // Titlebar buttons (matching Legacy: minimize, maximize, tombstone, close from left to right)
                    // Button layout right-to-left: close, tombstone, maximize, minimize
                    const int btnSize = std::max(12, titleBarH - theme.controlPadding * 2);
                    const int btnGap = theme.titleButtonGap;
                    int btnY = winfo.y + theme.controlPadding;
                    
                    // Position buttons from right to left
                    int closeLeft = winfo.x + winfo.w - theme.controlPadding - btnSize;
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

                    // Draw window content (rects, images, widgets, text)
                    const int contentX = winfo.x + theme.windowPadding;
                    const int contentY = winfo.y + titleBarH + theme.windowPadding;
                    for (const auto& ri : winfo.rects) { 
                        RECT rr{ contentX + ri.x, contentY + ri.y, contentX + ri.x + ri.w, contentY + ri.h };
                        HBRUSH rb = CreateSolidBrush(RGB(ri.r, ri.g, ri.b)); 
                        FillRect(dc, &rr, rb); 
                        DeleteObject(rb); 
                    }
                    for (const auto& img : winfo.images) {
                        const ImageBitmap& shown = displayedImage(img);
                        if (img.w > 0 && img.h > 0) ImageAdapter::DrawToHdc(dc, shown, contentX + img.x, contentY + img.y, img.w, img.h);
                        else ImageAdapter::DrawToHdc(dc, shown, contentX + img.x, contentY + img.y);
                    }
                    for (const auto& wd : winfo.widgets) { 
                        RECT wr{ contentX + wd.x, contentY + wd.y, contentX + wd.x + wd.w, contentY + wd.y + wd.h };
                        const uint32_t fillColor = calculatorWidgetFillColor(winfo, wd, theme);
                        HBRUSH wb = CreateSolidBrush(colorFromTheme(fillColor));
                        FillRect(dc, &wr, wb);
                        DeleteObject(wb);
                        HBRUSH borderBrush = CreateSolidBrush(colorFromTheme(calculatorWidgetBorderColor(winfo, wd, theme)));
                        FrameRect(dc, &wr, borderBrush);
                        DeleteObject(borderBrush);
                        const int textX = wr.left + (wd.icon.status == ImageLoadStatus::Ok ? 24 : 6);
                        if (wd.icon.status == ImageLoadStatus::Ok) ImageAdapter::DrawToHdc(dc, wd.icon, wr.left + 4, wr.top + (wd.h - 16) / 2, 16, 16);
                        const uint32_t textColor = calculatorWidgetTextColor(winfo, wd, theme);
                        drawUiText(dc, textX, centeredUiTextY(wr.top, wd.h), wd.text, colorFromTheme(textColor), FontRole::Default);
                    }
                    for (const auto& tx : winfo.positionedTexts) {
                        SystemFont::DrawText(dc, contentX + tx.x, contentY + tx.y, tx.text.c_str(), (int)tx.text.size(),
                            tx.hasColor ? RGB(tx.r, tx.g, tx.b) : RGB(220, 220, 220), FontRole::Default);
                    }
                    int ty = contentY;
                    for (const auto& tx : winfo.texts) { 
                        drawUiText(dc, contentX, ty, tx, RGB(220, 220, 220), FontRole::Default);
                        ty += uiTextHeight(FontRole::Default);
                    }
                    
                    // Draw tombstone overlay
                    if (winfo.tombstoned) { 
                        WindowRenderer::DrawTombstoneOverlay(dc, winfo.x, winfo.y, winfo.w, winfo.h);
                    }
                }
                int taskbarH = kTaskbarSize; WorkRect tbWork = taskbarRectForBounds(cr.right - cr.left, cr.bottom - cr.top); RECT tb{ tbWork.left,tbWork.top,tbWork.right,tbWork.bottom }; bool taskbarVertical = (g_taskbarPosition == TaskbarPosition::Left || g_taskbarPosition == TaskbarPosition::Right);
                int taskbarSpan = taskbarVertical ? (tb.right - tb.left) : (tb.bottom - tb.top);
                const uint32_t taskbarSurface = hostedTaskbarSurfaceColor(theme);
                if (sciFiTheme) {
                    HBRUSH tbFill = CreateSolidBrush(colorFromTheme(taskbarSurface));
                    FillRect(dc, &tb, tbFill);
                    DeleteObject(tbFill);

                    RECT accentLine = taskbarVertical
                        ? ((g_taskbarPosition == TaskbarPosition::Left)
                            ? RECT{ tb.right - 1, tb.top, tb.right, tb.bottom }
                            : RECT{ tb.left, tb.top, tb.left + 1, tb.bottom })
                        : ((g_taskbarPosition == TaskbarPosition::Top)
                            ? RECT{ tb.left, tb.bottom - 1, tb.right, tb.bottom }
                            : RECT{ tb.left, tb.top, tb.right, tb.top + 1 });
                    HBRUSH accentBrush = CreateSolidBrush(colorFromTheme(hostedTaskbarHighlightColor(theme)));
                    FillRect(dc, &accentLine, accentBrush);
                    DeleteObject(accentBrush);

                    RECT borderLine = taskbarVertical
                        ? ((g_taskbarPosition == TaskbarPosition::Left)
                            ? RECT{ tb.right - 2, tb.top, tb.right - 1, tb.bottom }
                            : RECT{ tb.left + 1, tb.top, tb.left + 2, tb.bottom })
                        : ((g_taskbarPosition == TaskbarPosition::Top)
                            ? RECT{ tb.left, tb.bottom - 2, tb.right, tb.bottom - 1 }
                            : RECT{ tb.left, tb.top + 1, tb.right, tb.top + 2 });
                    HBRUSH borderBrush = CreateSolidBrush(colorFromTheme(hostedTaskbarBorderColor(theme)));
                    FillRect(dc, &borderLine, borderBrush);
                    DeleteObject(borderBrush);
                } else {
                    const uint32_t taskbarStartColor = theme.taskbarBackground;
                    const uint32_t taskbarEndColor = theme.mutedAccent;
                    auto mixChannel = [&](uint32_t start, uint32_t end, float t) -> uint32_t {
                        return static_cast<uint32_t>(static_cast<int>(start) + ((static_cast<int>(end) - static_cast<int>(start)) * t));
                    };
                    for (int ty2 = 0; ty2 < taskbarSpan; ++ty2) {
                        float gt = (float)ty2 / (float)(taskbarSpan > 1 ? taskbarSpan - 1 : 1);
                        uint32_t mixedColor =
                            (mixChannel((taskbarStartColor >> 16) & 0xFF, (taskbarEndColor >> 16) & 0xFF, gt) << 16) |
                            (mixChannel((taskbarStartColor >> 8) & 0xFF, (taskbarEndColor >> 8) & 0xFF, gt) << 8) |
                            mixChannel(taskbarStartColor & 0xFF, taskbarEndColor & 0xFF, gt);
                        HBRUSH tbLine = CreateSolidBrush(colorFromTheme(mixedColor));
                        RECT tbLn = taskbarVertical ? RECT{ tb.left + ty2, tb.top, tb.left + ty2 + 1, tb.bottom } : RECT{ tb.left, tb.top + ty2, tb.right, tb.top + ty2 + 1 };
                        FillRect(dc, &tbLn, tbLine);
                        DeleteObject(tbLine);
                    }
                    HPEN tbEdge = CreatePen(PS_SOLID, 1, colorFromTheme(theme.taskbarBorder));
                    HGDIOBJ oldP = SelectObject(dc, tbEdge);
                    if (taskbarVertical) {
                        int edgeX = (g_taskbarPosition == TaskbarPosition::Left) ? tb.right : tb.left;
                        MoveToEx(dc, edgeX, tb.top, nullptr);
                        LineTo(dc, edgeX, tb.bottom);
                    } else {
                        int edgeY = (g_taskbarPosition == TaskbarPosition::Top) ? tb.bottom : tb.top;
                        MoveToEx(dc, tb.left, edgeY, nullptr);
                        LineTo(dc, tb.right, edgeY);
                    }
                    SelectObject(dc, oldP);
                    DeleteObject(tbEdge);
                }
                POINT cursor; GetCursorPos(&cursor); ScreenToClient(h, &cursor);
                RECT startBtn = hostedStartButtonRect(theme, tb);
                const bool startHover = (cursor.x >= startBtn.left && cursor.x <= startBtn.right && cursor.y >= startBtn.top && cursor.y <= startBtn.bottom);
                HBRUSH sbg = CreateSolidBrush(colorFromTheme(hostedStartButtonFillColor(theme, startHover, g_startMenuVisible)));
                FillRect(dc, &startBtn, sbg);
                DeleteObject(sbg);
                if (sciFiTheme) {
                    HPEN startBorder = CreatePen(PS_SOLID, 1, colorFromTheme(hostedStartButtonBorderColor(theme, startHover, g_startMenuVisible)));
                    HGDIOBJ oldStartPen = SelectObject(dc, startBorder);
                    HGDIOBJ oldStartBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
                    Rectangle(dc, startBtn.left, startBtn.top, startBtn.right, startBtn.bottom);
                    SelectObject(dc, oldStartPen);
                    SelectObject(dc, oldStartBr);
                    DeleteObject(startBorder);
                } else {
                    FrameRect(dc, &startBtn, (HBRUSH)GetStockObject(WHITE_BRUSH));
                }
                drawBitmapCentered(dc, g_startBtnBmp, startBtn);
                // Search box placeholder (after start button)
                if (!taskbarVertical) drawTaskbarSearchBox(dc, tb.left + 48, tb.top + 8, 160, (tb.bottom - tb.top) - 16);
                // Taskbar buttons (offset to right of search box)
                int btnX = taskbarVertical ? tb.left + 4 : tb.left + 216; int btnY = taskbarVertical ? tb.top + 48 : tb.top + 6; for (uint64_t id : g_z) {
                    auto it = g_windows.find(id); if (it == g_windows.end( )) continue; std::string label = it->second.title; int bw = taskbarVertical ? (tb.right - tb.left - 8) : measureUiText(label.c_str(), (int)label.size(), FontRole::Small) + theme.taskbarItemPadding * 2 + 12; if (!taskbarVertical && bw > 180) bw = 180; int bh = taskbarVertical ? 28 : (tb.bottom - tb.top - 12); RECT br{ btnX, btnY, btnX + bw, btnY + bh }; bool hover = (cursor.x >= br.left && cursor.x <= br.right && cursor.y >= br.top && cursor.y <= br.bottom);
                    uint32_t fillColor = hostedTaskbarItemFillColor(theme, id == g_focus, hover, it->second.minimized, it->second.tombstoned);
                    HBRUSH bbg = CreateSolidBrush(colorFromTheme(fillColor)); FillRect(dc, &br, bbg); DeleteObject(bbg);
                    if (sciFiTheme) {
                        const uint32_t outlineColor = hostedTaskbarItemBorderColor(theme, id == g_focus, hover);
                        HPEN outlinePen = CreatePen(PS_SOLID, 1, colorFromTheme(outlineColor));
                        HGDIOBJ oldOutlinePen = SelectObject(dc, outlinePen);
                        HGDIOBJ oldOutlineBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                        Rectangle(dc, br.left, br.top, br.right, br.bottom);
                        SelectObject(dc, oldOutlinePen);
                        SelectObject(dc, oldOutlineBrush);
                        DeleteObject(outlinePen);
                    }
                    // Active indicator line at bottom for focused window
                    if (id == g_focus) { HBRUSH ind = CreateSolidBrush(colorFromTheme(sciFiTheme ? WindowRenderer::BlendThemeColor(theme.accent, theme.mutedAccent, 14) : theme.accent)); RECT indR{ br.left + 2,br.bottom - 3,br.right - 2,br.bottom - 1 }; FillRect(dc, &indR, ind); DeleteObject(ind); }
                    RECT iconRect{ br.left + 4, br.top + 4, br.left + 20, br.top + 20 }; drawBitmapCentered(dc, it->second.taskbarIcon, iconRect); if (!taskbarVertical) drawUiText(dc, br.left + theme.taskbarItemPadding + 12, br.top + 8, label, RGB(230, 230, 240), FontRole::Small); if (taskbarVertical) btnY += bh + 4; else btnX += bw + theme.taskbarItemPadding / 2;
                }
                // System tray area (before clock)
                if (!taskbarVertical) drawSystemTray(dc, cr, taskbarH);
                // Taskbar clock/date display (right side, matching Legacy Taskbar.cs)
                if (!taskbarVertical) {
                    std::time_t now = std::time(nullptr);
                    std::tm ltBuf{};
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                    localtime_s(&ltBuf, &now);
#else
                    std::tm* tmp = std::localtime(&now);
                    if (tmp) ltBuf = *tmp;
#endif
                    char timeBuf[16]; char dateBuf[16];
                    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ltBuf.tm_hour, ltBuf.tm_min);
                    std::snprintf(dateBuf, sizeof(dateBuf), "%d/%d/%d", ltBuf.tm_mon + 1, ltBuf.tm_mday, ltBuf.tm_year + 1900);
                    int timeW = measureUiText(timeBuf, (int)strlen(timeBuf), FontRole::Small);
                    int dateW = measureUiText(dateBuf, (int)strlen(dateBuf), FontRole::Small);
                    int lineH = uiTextHeight(FontRole::Small);
                    int clockW = (timeW > dateW ? timeW : dateW) + 16;
                    int clockX = tb.right - clockW - theme.taskbarPadding;
                    int timeY = tb.top + 6;
                    int dateY = timeY + lineH - 1;
                    drawUiText(dc, clockX + (clockW - timeW) / 2, timeY, timeBuf, (int)strlen(timeBuf), RGB(200, 200, 210), FontRole::Small);
                    drawUiText(dc, clockX + (clockW - dateW) / 2, dateY, dateBuf, (int)strlen(dateBuf), RGB(150, 150, 165), FontRole::Small);
                }
                // Show Desktop button (thin sliver on far right, matching Legacy)
                {
                    int sdW = 6; RECT sdRect = taskbarVertical ? RECT{ tb.left, tb.bottom - sdW, tb.right, tb.bottom } : RECT{ tb.right - sdW, tb.top, tb.right, tb.bottom };
                    bool hoverSD = (cursor.x >= sdRect.left && cursor.y >= sdRect.top && cursor.x <= sdRect.right && cursor.y <= sdRect.bottom);
                    HBRUSH sdBrush = CreateSolidBrush(hoverSD ? RGB(70, 80, 100) : RGB(50, 50, 60));
                    FillRect(dc, &sdRect, sdBrush); DeleteObject(sdBrush);
                }
                // Taskbar button tooltip (drawn last so it overlaps everything)
                if (!taskbarVertical) {
                    int tbtnX = tb.left + 216;
                    for (uint64_t id : g_z) {
                        auto it = g_windows.find(id); if (it == g_windows.end( )) continue;
                        std::string label = it->second.title;
                        int bw = measureUiText(label.c_str(), (int)label.size(), FontRole::Small) + theme.taskbarItemPadding * 2 + 12; if (bw > 180) bw = 180;
                        RECT br2{ tbtnX, tb.top + 6, tbtnX + bw, tb.bottom - 6 };
                        bool hov = (cursor.x >= br2.left && cursor.x <= br2.right && cursor.y >= br2.top && cursor.y <= br2.bottom);
                        if (hov) { drawTaskbarTooltip(dc, (br2.left + br2.right) / 2, tb.top, label.c_str( )); break; }
                        tbtnX += bw + theme.taskbarItemPadding / 2;
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
                        const char* nmsg = n.message.c_str( );
                        int noteW = measureUiText(nmsg, (int)n.message.size(), FontRole::Default) + 24; if (noteW < 160) noteW = 160;
                        int noteH = 32;
                        int noteX = cr.right - noteW - 8;
                        RECT noteR{ noteX, noteY, noteX + noteW, noteY + noteH };
                        HBRUSH nb = CreateSolidBrush(n.level == NotificationLevel::Error ? RGB(120, 40, 40) : RGB(40, 55, 80));
                        FillRect(dc, &noteR, nb); DeleteObject(nb);
                        FrameRect(dc, &noteR, (HBRUSH)GetStockObject(WHITE_BRUSH));
                        drawUiText(dc, noteX + 12, noteY + 8, nmsg, (int)n.message.size(), RGB(240, 240, 240), FontRole::Default);
                        noteY += noteH + 4;
                    }
                }
                // Taskbar right-click menu (Task Manager, Reboot, Log Off)
                if (g_taskbarMenuVisible) {
                    const int tmItemH = 28; const int tmMenuW = 180; const int tmPad = 6;
                    static const char* tmLabels[] = { "Task Manager", "Reboot", "Log Off" };
                    const int tmItemCount = 3;
                    int tmH = tmItemH * tmItemCount + tmPad * 2;
                    g_taskbarMenuRect = { g_taskbarMenuRect.left, g_taskbarMenuRect.top,
                        g_taskbarMenuRect.left + tmMenuW, g_taskbarMenuRect.top + tmH };
                    HBRUSH tmBg = CreateSolidBrush(colorFromTheme(sciFiTheme ? hostedPanelSurfaceColor(theme) : RGB(42, 42, 42)));
                    FillRect(dc, &g_taskbarMenuRect, tmBg); DeleteObject(tmBg);
                    if (sciFiTheme) {
                        HBRUSH accent = CreateSolidBrush(colorFromTheme(hostedTaskbarHighlightColor(theme)));
                        RECT line{ g_taskbarMenuRect.left, g_taskbarMenuRect.top, g_taskbarMenuRect.right, g_taskbarMenuRect.top + 1 };
                        FillRect(dc, &line, accent);
                        DeleteObject(accent);
                    }
                    HPEN tmBorder = CreatePen(PS_SOLID, 1, colorFromTheme(sciFiTheme ? hostedPanelBorderColor(theme) : RGB(63, 63, 63)));
                    HGDIOBJ oldPen2 = SelectObject(dc, tmBorder);
                    HGDIOBJ oldBr2 = SelectObject(dc, GetStockObject(NULL_BRUSH));
                    Rectangle(dc, g_taskbarMenuRect.left, g_taskbarMenuRect.top, g_taskbarMenuRect.right, g_taskbarMenuRect.bottom);
                    SelectObject(dc, oldPen2); SelectObject(dc, oldBr2); DeleteObject(tmBorder);
                    SetBkMode(dc, TRANSPARENT);
                    for (int tmi = 0; tmi < tmItemCount; ++tmi) {
                        int iy = g_taskbarMenuRect.top + tmPad + tmi * tmItemH;
                        RECT itemR{ g_taskbarMenuRect.left + 1, iy, g_taskbarMenuRect.right - 1, iy + tmItemH };
                        bool hov = (cursor.x >= itemR.left && cursor.x <= itemR.right && cursor.y >= itemR.top && cursor.y <= itemR.bottom);
                        if (tmi == g_taskbarMenuSel || hov) {
                            const uint32_t itemColor = sciFiTheme
                                ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), hov ? theme.accent : theme.mutedAccent, hov ? 22 : 16)
                                : RGB(60, 80, 120);
                            HBRUSH hb = CreateSolidBrush(colorFromTheme(itemColor)); FillRect(dc, &itemR, hb); DeleteObject(hb);
                        }
                        drawUiText(dc, itemR.left + 8, iy + (tmItemH - uiTextHeight(FontRole::Default)) / 2, tmLabels[tmi], (int)strlen(tmLabels[tmi]), sciFiTheme ? RGB(232, 236, 246) : RGB(220, 220, 220), FontRole::Default);
                    }
                }
                // Start menu popup (pinned + recent OR all programs)
                if (g_startMenuVisible) {
                    int smW = 440; // wider to accommodate two columns
                    int maxRows = 14;
                    int rowH = kStartMenuRowH;
                    int leftColW = 260; // left list column width
                    int rightColW = 160; // right column for shortcuts
                    int smH = maxRows * rowH + 10;
                    RECT sm = (g_taskbarPosition == TaskbarPosition::Top)
                        ? RECT{ startBtn.left, startBtn.bottom + 6, startBtn.left + smW, startBtn.bottom + 6 + smH }
                        : RECT{ startBtn.left, startBtn.top - smH - 6, startBtn.left + smW, startBtn.top - 6 };
                    if (sm.top < 0) { sm.top = 4; sm.bottom = sm.top + smH; }
                    if (sm.bottom > cr.bottom) { sm.bottom = cr.bottom - 4; sm.top = sm.bottom - smH; }
                    g_startMenuRect = sm;
                    HBRUSH mBg = CreateSolidBrush(colorFromTheme(sciFiTheme ? hostedPanelSurfaceColor(theme) : RGB(45, 45, 55)));
                    FillRect(dc, &sm, mBg);
                    DeleteObject(mBg);
                    if (sciFiTheme) {
                        HBRUSH accent = CreateSolidBrush(colorFromTheme(hostedTaskbarHighlightColor(theme)));
                        RECT line{ sm.left, sm.top, sm.right, sm.top + 1 };
                        FillRect(dc, &line, accent);
                        DeleteObject(accent);
                    }
                    HBRUSH mBorder = CreateSolidBrush(colorFromTheme(sciFiTheme ? hostedPanelBorderColor(theme) : RGB(255, 255, 255)));
                    FrameRect(dc, &sm, mBorder);
                    DeleteObject(mBorder);

                    // Left column - Recent/All Programs list
                    int y = sm.top + 4;
                    HFONT f = (HFONT)GetStockObject(ANSI_VAR_FONT);
                    SelectObject(dc, f);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, RGB(230, 230, 230));
                    int row = 0;
                    int startIndex = g_startMenuScroll;
                    bool freezeStartMenuHover = RightClickMenu::IsStartMenuAppMenuVisible();

                    if (g_startMenuAllProgs) {
                        // Show all programs alphabetically
                        for (size_t i = startIndex; i < g_startMenuAllProgsSorted.size( ) && row < maxRows; ++i) {
                            RECT r{ sm.left + 4, y, sm.left + leftColW - 4, y + rowH };
                            bool isSel = ((int)i == g_startMenuSel);
                            bool isHover = !freezeStartMenuHover && (cursor.x >= r.left && cursor.x <= r.right && cursor.y >= r.top && cursor.y <= r.bottom);
                            const uint32_t rowColor = sciFiTheme
                                ? (isSel
                                    ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.accent, 22)
                                    : (isHover
                                        ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.mutedAccent, 16)
                                        : WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.windowBorder, 8)))
                                : (isSel ? RGB(80, 100, 150) : (isHover ? RGB(70, 90, 130) : RGB(55, 55, 70)));
                            HBRUSH rb = CreateSolidBrush(colorFromTheme(rowColor));
                            FillRect(dc, &r, rb);
                            DeleteObject(rb);
                            
                            // Draw focus indicator if selected (keyboard focus)
                            if (isSel && !isHover) {
                                FocusIndicator::DrawFocusRect(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 3, 2, 2);
                            }
                            
                            std::string txt = g_startMenuAllProgsSorted[i];
                            int textX = r.left + 4;
                            drawStartMenuIcon(dc, r, txt, textX);
                            drawUiText(dc, textX, centeredUiTextY(r.top, rowH), txt, sciFiTheme ? RGB(232, 236, 246) : RGB(230, 230, 230), FontRole::Default);
                            y += rowH; row++;
                        }
                    } else {
                        // Show pinned + recent
                        for (size_t i = startIndex; i < g_startMenuPinnedRecent.size( ) && row < maxRows; ++i) {
                            RECT r{ sm.left + 4, y, sm.left + leftColW - 4, y + rowH };
                            bool isSel = ((int)i == g_startMenuSel);
                            bool isHover = !freezeStartMenuHover && (cursor.x >= r.left && cursor.x <= r.right && cursor.y >= r.top && cursor.y <= r.bottom);
                            const uint32_t rowColor = sciFiTheme
                                ? (isSel
                                    ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.accent, 22)
                                    : (isHover
                                        ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.mutedAccent, 16)
                                        : WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.windowBorder, 8)))
                                : (isSel ? RGB(80, 100, 150) : (isHover ? RGB(70, 90, 130) : RGB(55, 55, 70)));
                            HBRUSH rb = CreateSolidBrush(colorFromTheme(rowColor));
                            FillRect(dc, &r, rb);
                            DeleteObject(rb);
                            
                            // Draw focus indicator if selected (keyboard focus)
                            if (isSel && !isHover) {
                                FocusIndicator::DrawFocusRect(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 3, 2, 2);
                            }
                            
                            std::string txt = g_startMenuPinnedRecent[i];
                            int textX = r.left + 4;
                            drawStartMenuIcon(dc, r, txt, textX);
                            std::string displayText = (hasEquivalentListItem(g_cfg.pinned, txt) ? "* " : "  ") + txt;
                            drawUiText(dc, textX, centeredUiTextY(r.top, rowH), displayText, sciFiTheme ? RGB(232, 236, 246) : RGB(230, 230, 230), FontRole::Default);
                            y += rowH; row++;
                        }
                    }

                    // Right column - shortcuts
                    int rcX = sm.left + leftColW + 4;
                    int rcY = sm.top + 6;
                    SetTextColor(dc, RGB(200, 200, 200));

                    // Computer Files shortcut
                    RECT rcComputer{ rcX, rcY, sm.right - 6, rcY + rowH };
                    bool overComp = !freezeStartMenuHover && (cursor.x >= rcComputer.left && cursor.x <= rcComputer.right && cursor.y >= rcComputer.top && cursor.y <= rcComputer.bottom);
                    if (overComp) { HBRUSH hb = CreateSolidBrush(colorFromTheme(sciFiTheme ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.accent, 18) : RGB(70, 90, 130))); FillRect(dc, &rcComputer, hb); DeleteObject(hb); }
                    int computerTextX = rcComputer.left + 6;
                    drawStartMenuIcon(dc, rcComputer, "Computer Files", computerTextX);
                    drawUiText(dc, computerTextX, centeredUiTextY(rcComputer.top, rowH), "Computer Files", 14, sciFiTheme ? RGB(220, 228, 244) : RGB(200, 200, 200), FontRole::Default);
                    rcY += rowH + kStartMenuRowGap;

                    // Console shortcut
                    RECT rcConsole{ rcX, rcY, sm.right - 6, rcY + rowH };
                    bool overCon = !freezeStartMenuHover && (cursor.x >= rcConsole.left && cursor.x <= rcConsole.right && cursor.y >= rcConsole.top && cursor.y <= rcConsole.bottom);
                    if (overCon) { HBRUSH hb = CreateSolidBrush(colorFromTheme(sciFiTheme ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.accent, 18) : RGB(70, 90, 130))); FillRect(dc, &rcConsole, hb); DeleteObject(hb); }
                    int consoleTextX = rcConsole.left + 6;
                    drawStartMenuIcon(dc, rcConsole, "Console", consoleTextX);
                    drawUiText(dc, consoleTextX, centeredUiTextY(rcConsole.top, rowH), "Console", 7, sciFiTheme ? RGB(220, 228, 244) : RGB(200, 200, 200), FontRole::Default);
                    rcY += rowH + kStartMenuRowGap;

                    // Recent Documents shortcut
                    RECT rcDocs{ rcX, rcY, sm.right - 6, rcY + rowH };
                    bool overDocs = !freezeStartMenuHover && (cursor.x >= rcDocs.left && cursor.x <= rcDocs.right && cursor.y >= rcDocs.top && cursor.y <= rcDocs.bottom);
                    if (overDocs) { HBRUSH hb = CreateSolidBrush(colorFromTheme(sciFiTheme ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.accent, 18) : RGB(70, 90, 130))); FillRect(dc, &rcDocs, hb); DeleteObject(hb); }
                    int docsTextX = rcDocs.left + 6;
                    drawStartMenuIcon(dc, rcDocs, "Recent Docs", docsTextX);
                    drawUiText(dc, docsTextX, centeredUiTextY(rcDocs.top, rowH), "Recent Docs", 11, sciFiTheme ? RGB(220, 228, 244) : RGB(200, 200, 200), FontRole::Default);

                    // Bottom area - "All Programs" toggle button
                    int btnY = sm.bottom - 30;
                    RECT allProgBtn{ sm.left + 6, btnY, sm.left + leftColW - 6, btnY + 24 };
                    bool overAllProg = !freezeStartMenuHover && (cursor.x >= allProgBtn.left && cursor.x <= allProgBtn.right && cursor.y >= allProgBtn.top && cursor.y <= allProgBtn.bottom);
                    HBRUSH apb = CreateSolidBrush(colorFromTheme(sciFiTheme
                        ? (overAllProg
                            ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.mutedAccent, 20)
                            : WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.windowBorder, 12))
                        : (overAllProg ? RGB(70, 80, 100) : RGB(60, 60, 75))));
                    FillRect(dc, &allProgBtn, apb); DeleteObject(apb);
                    if (sciFiTheme) {
                        HBRUSH apbBorder = CreateSolidBrush(colorFromTheme(hostedPanelBorderColor(theme)));
                        FrameRect(dc, &allProgBtn, apbBorder);
                        DeleteObject(apbBorder);
                    } else {
                        FrameRect(dc, &allProgBtn, (HBRUSH)GetStockObject(WHITE_BRUSH));
                    }
                    const char* btnText = g_startMenuAllProgs ? "< Back" : "All Programs >";
                    drawUiText(dc, allProgBtn.left + 8, centeredUiTextY(allProgBtn.top, allProgBtn.bottom - allProgBtn.top), btnText, sciFiTheme ? RGB(232, 236, 246) : RGB(230, 230, 230), FontRole::Default);

                    // Power menu area (bottom-right)
                    int shutdownBtnW = 80;
                    int shutdownBtnH = 24;
                    RECT shutdownBtn{ sm.right - shutdownBtnW - 30, btnY, sm.right - 30, btnY + shutdownBtnH };
                    bool overShutdown = !freezeStartMenuHover && (cursor.x >= shutdownBtn.left && cursor.x <= shutdownBtn.right && cursor.y >= shutdownBtn.top && cursor.y <= shutdownBtn.bottom);
                    HBRUSH sdb = CreateSolidBrush(colorFromTheme(sciFiTheme
                        ? (overShutdown
                            ? WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.accent, 18)
                            : WindowRenderer::BlendThemeColor(hostedPanelSurfaceColor(theme), theme.windowBorder, 12))
                        : (overShutdown ? RGB(80, 40, 40) : RGB(60, 60, 75))));
                    FillRect(dc, &shutdownBtn, sdb); DeleteObject(sdb);
                    if (sciFiTheme) {
                        HBRUSH sbBorder = CreateSolidBrush(colorFromTheme(hostedPanelBorderColor(theme)));
                        FrameRect(dc, &shutdownBtn, sbBorder);
                        DeleteObject(sbBorder);
                    } else {
                        FrameRect(dc, &shutdownBtn, (HBRUSH)GetStockObject(WHITE_BRUSH));
                    }
                    drawUiText(dc, shutdownBtn.left + 10, centeredUiTextY(shutdownBtn.top, shutdownBtn.bottom - shutdownBtn.top), "Shutdown", 8, sciFiTheme ? RGB(232, 236, 246) : RGB(230, 230, 230), FontRole::Default);
                }

                // Right-click context menus are top-level popups over Start and desktop surfaces.
                RightClickMenu::Draw(dc);

                // Capture framebuffer for VNC if server is running.
                // When using the GDI backend we still capture from the
                // window DC because the compositor paints via GDI calls.
                // Once the compositor migrates to painting into the
                // VideoBackend pixel buffer, feedVncFromBackend() will
                // be the sole path and this block can be removed.
                if (vnc::VncServer::IsRunning( )) {
                    const uint64_t vncStartMs = nowMs( );
                    // Capture the completed hosted frame, not the visible DC, so VNC
                    // stays aligned with the atomic offscreen composition result.
                    if (hostedOffscreenReady && s_hostedPaintPixels) {
                        vnc::VncServer::UpdateFramebuffer(
                            reinterpret_cast<const uint8_t*>(s_hostedPaintPixels),
                            clientW,
                            clientH,
                            clientW * 4);

                        // Keep the compatibility backend buffer in sync for callers
                        // that still read from VideoBackend on the hosted path.
                        if (g_videoBackend && g_videoBackend->getPixels( )) {
                            uint32_t* dst = g_videoBackend->getPixels( );
                            int bw = g_videoBackend->getWidth( );
                            int bh = g_videoBackend->getHeight( );
                            int copyW = clientW < bw ? clientW : bw;
                            int copyH = clientH < bh ? clientH : bh;
                            for (int y = 0; y < copyH; ++y) {
                                std::memcpy(dst + y * bw, s_hostedPaintPixels + y * clientW, static_cast<size_t>(copyW) * 4);
                            }
                        }
                    } else {
                        HDC memDC = CreateCompatibleDC(drawDc);
                        HBITMAP memBitmap = CreateCompatibleBitmap(drawDc, clientW, clientH);
                        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);
                        BitBlt(memDC, 0, 0, clientW, clientH, drawDc, 0, 0, SRCCOPY);

                        BITMAPINFO bmi{};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = clientW;
                        bmi.bmiHeader.biHeight = -clientH; // negative for top-down
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;

                        std::vector<uint8_t> pixels(clientW * clientH * 4);
                        GetDIBits(memDC, memBitmap, 0, clientH, pixels.data( ), &bmi, DIB_RGB_COLORS);
                        vnc::VncServer::UpdateFramebuffer(pixels.data( ), clientW, clientH, clientW * 4);

                        // Also copy into the video backend buffer so the legacy
                        // compatibility path remains synchronized.
                        if (g_videoBackend && g_videoBackend->getPixels( )) {
                            uint32_t* dst = g_videoBackend->getPixels( );
                            int bw = g_videoBackend->getWidth( );
                            int bh = g_videoBackend->getHeight( );
                            int copyW = clientW < bw ? clientW : bw;
                            int copyH = clientH < bh ? clientH : bh;
                            const uint32_t* src = reinterpret_cast<const uint32_t*>(pixels.data( ));
                            for (int y = 0; y < copyH; ++y) {
                                std::memcpy(dst + y * bw, src + y * clientW, static_cast<size_t>(copyW) * 4);
                            }
                        }

                        SelectObject(memDC, oldBitmap);
                        DeleteObject(memBitmap);
                        DeleteDC(memDC);
                    }
                    vncDurationMs = nowMs( ) - vncStartMs;
                }

#undef dc
                // Present the fully composed hosted frame in one blit.
                if (hostedOffscreenReady && drawDc != visibleDc) {
                    BitBlt(visibleDc, 0, 0, clientW, clientH, drawDc, 0, 0, SRCCOPY);
                }

                EndPaint(h, &ps);
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                hostedFreezeDiagnosticsOnPaintEnd(nowMs( ) - paintStartMs, vncDurationMs);
#endif
                return 0;
            }
            case WM_LBUTTONDOWN: {
                int mx = GET_X_LPARAM(l); int my = GET_Y_LPARAM(l); RECT cr; GetClientRect(h, &cr); int taskbarH = 40;
                { std::lock_guard<std::mutex> lk(g_lock); if (blockInputBehindModal(mx, my)) { requestRepaint( ); return 0; } }
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
                WorkRect tbWork = taskbarRectForBounds(cr.right - cr.left, cr.bottom - cr.top); bool taskbarVertical = (g_taskbarPosition == TaskbarPosition::Left || g_taskbarPosition == TaskbarPosition::Right);
                const DesktopTheme& theme = GetCurrentDesktopTheme();
                { int sdW = 6; RECT sdRect = taskbarVertical ? RECT{ tbWork.left, tbWork.bottom - sdW, tbWork.right, tbWork.bottom } : RECT{ tbWork.right - sdW, tbWork.top, tbWork.right, tbWork.bottom }; if (mx >= sdRect.left && my >= sdRect.top && mx <= sdRect.right && my <= sdRect.bottom) { ipc::Message sdm; sdm.type = static_cast<uint32_t>(gui::MsgType::MT_ShowDesktopToggle); handleMessage(sdm); requestRepaint( ); return 0; } }
                RECT startBtn = hostedStartButtonRect(theme, tbWork); // Start button toggle
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
                    int rowH = kStartMenuRowH;

                    // Computer Files
                    RECT rcComputer{ rcX, rcY, g_startMenuRect.right - 6, rcY + rowH };
                    if (mx >= rcComputer.left && mx <= rcComputer.right && my >= rcComputer.top && my <= rcComputer.bottom) {
                        logStartMenuLaunchTargetShadowDiagnostic("ComputerFiles");
                        // SHADOW_ONLY observation above is diagnostic-only; launchAction still receives
                        // the original legacy Start Menu dispatch string.
                        launchAction("ComputerFiles");
                        g_startMenuVisible = false;
                        requestRepaint( );
                        return 0;
                    }
                    rcY += rowH + kStartMenuRowGap;

                    // Console
                    RECT rcConsole{ rcX, rcY, g_startMenuRect.right - 6, rcY + rowH };
                    if (mx >= rcConsole.left && mx <= rcConsole.right && my >= rcConsole.top && my <= rcConsole.bottom) {
                        logStartMenuLaunchTargetShadowDiagnostic("Console");
                        // SHADOW_ONLY observation above is diagnostic-only; launchAction still receives
                        // the original legacy Start Menu dispatch string.
                        launchAction("Console");
                        g_startMenuVisible = false;
                        requestRepaint( );
                        return 0;
                    }
                    rcY += rowH + kStartMenuRowGap;

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
                        int itemCount = g_startMenuAllProgs ? (int)g_startMenuAllProgsSorted.size( ) : (int)g_startMenuPinnedRecent.size( );
                        if (idx >= 0 && idx < itemCount) {
                            uint64_t now = nowMs( );
                            if (g_lastItemIndex == idx && (now - g_lastItemClickTicks) < 450) {
                                // Double-click: launch
                                std::string action = g_startMenuAllProgs ? g_startMenuAllProgsSorted[idx] : g_startMenuPinnedRecent[idx];
                                logStartMenuLaunchTargetShadowDiagnostic(action);
                                // SHADOW_ONLY observation above is diagnostic-only; launchAction still receives
                                // the original legacy Start Menu dispatch string.
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
                {
                    bool windowAtClick = false;
                    { std::lock_guard<std::mutex> lk(g_lock); windowAtClick = (hitWindowAt(mx, my) != nullptr); }
                    WorkRect work = desktopWorkAreaForBounds(cr.right - cr.left, cr.bottom - cr.top);
                    if (!windowAtClick && mx >= work.left && mx < work.right && my >= work.top && my < work.bottom) {
                        int hitIdx = HitTestDesktopIcon(mx, my);
                        bool ctrlDown = IsCtrlDown( );
                        bool shiftDown = IsShiftDown( );
                        if (hitIdx >= 0) {
                            uint64_t now = nowMs( );
                            if (g_lastItemIndex == hitIdx && (now - g_lastItemClickTicks) < 450) {
                                openDesktopItem(hitIdx);
                                g_lastItemIndex = -1;
                                g_lastItemClickTicks = 0;
                            } else {
                                if (shiftDown) SelectDesktopIconRange(g_lastSelectedDesktopIconIndex, hitIdx);
                                else if (ctrlDown) ToggleDesktopIconSelection(hitIdx);
                                else SelectDesktopIcon(hitIdx, false);
                                g_lastItemIndex = hitIdx;
                                g_lastItemClickTicks = now;
                                if (g_items[hitIdx].selected) {
                                    g_iconDragPending = true;
                                    g_iconDragIndex = hitIdx;
                                    g_iconDragStartX = mx;
                                    g_iconDragStartY = my;
                                    g_iconDragOffX = mx - g_items[hitIdx].ix;
                                    g_iconDragOffY = my - g_items[hitIdx].iy;
                                    SetCapture(h);
                                }
                            }
                            requestRepaint( );
                            return 0;
                        }

                        if (!ctrlDown) ClearDesktopIconSelection( );
                        g_iconSelectionDragPending = true;
                        g_iconSelectionDragActive = false;
                        g_iconSelectionStartX = mx;
                        g_iconSelectionStartY = my;
                        g_iconSelectionCurrentX = mx;
                        g_iconSelectionCurrentY = my;
                        g_iconSelectionAdditive = ctrlDown;
                        SetCapture(h);
                        requestRepaint( );
                        return 0;
                    }
                }
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
                uint64_t ownerPid = 0; uint64_t targetWindow = 0; { std::lock_guard<std::mutex> lk(g_lock); WinInfo* hitWin = hitWindowAt(mx, my); if (hitWin) { ownerPid = hitWin->ownerPid; targetWindow = hitWin->id; } else { ownerPid = inputOwnerPid( ); targetWindow = g_modalWindow ? g_modalWindow : g_focus; } }
                Compositor::handleMouse(mx, my, true, false); publishOut(MsgType::MT_InputMouse, Compositor::packMousePayloadForTarget(mx, my, 1, "down", ownerPid, targetWindow), ownerPid); return 0;
            }
            case WM_RBUTTONDOWN: {
                int mx = GET_X_LPARAM(l); int my = GET_Y_LPARAM(l); { std::lock_guard<std::mutex> lk(g_lock); if (blockInputBehindModal(mx, my)) { requestRepaint( ); return 0; } } if (g_startMenuVisible && mx >= g_startMenuRect.left && mx <= g_startMenuRect.right && my >= g_startMenuRect.top && my <= g_startMenuRect.bottom) {
                    const int leftColW = 260;
                    const int rowH = kStartMenuRowH;
                    const int listTop = g_startMenuRect.top + 4;
                    const int btnY = g_startMenuRect.bottom - 30;
                    if (mx >= g_startMenuRect.left && mx <= g_startMenuRect.left + leftColW && my >= listTop && my <= btnY - 4) {
                        int idx = (my - listTop) / rowH + g_startMenuScroll;
                        int itemCount = g_startMenuAllProgs ? (int)g_startMenuAllProgsSorted.size() : (int)g_startMenuPinnedRecent.size();
                        if (idx >= 0 && idx < itemCount) {
                            std::string action = g_startMenuAllProgs ? g_startMenuAllProgsSorted[idx] : g_startMenuPinnedRecent[idx];
                            Logger::write(LogLevel::Info, "Start Menu context menu creation requested for app: " + action);
                            g_startMenuSel = idx;
                            RightClickMenu::ShowForStartMenuApp(mx, my, action);
                            requestRepaint();
                            return 0;
                        }
                    }
                }
                // Desktop icon right-click pin/unpin or taskbar right-click menu
                RECT cr; GetClientRect(h, &cr);
                WorkRect tbWork = taskbarRectForBounds(cr.right - cr.left, cr.bottom - cr.top);
                WorkRect work = desktopWorkAreaForBounds(cr.right - cr.left, cr.bottom - cr.top);
                bool inTaskbar = mx >= tbWork.left && mx < tbWork.right && my >= tbWork.top && my < tbWork.bottom;
                bool inWorkArea = mx >= work.left && mx < work.right && my >= work.top && my < work.bottom;
                // Taskbar right-click: show context menu
                if (inTaskbar) {
                    const int tmItemH = 28; const int tmItemCount = 3; const int tmPad = 6;
                    int tmH = tmItemH * tmItemCount + tmPad * 2;
                    int tmW = 180;
                    int tmX = mx; int tmY = (g_taskbarPosition == TaskbarPosition::Top) ? tbWork.bottom + 4 : tbWork.top - tmH;
                    if (tmX + tmW > cr.right) tmX = cr.right - tmW - 2;
                    if (tmY + tmH > cr.bottom) tmY = cr.bottom - tmH - 2;
                    if (tmY < 0) tmY = 0;
                    g_taskbarMenuRect = { tmX, tmY, tmX + tmW, tmY + tmH };
                    g_taskbarMenuVisible = true;
                    g_taskbarMenuSel = -1;
                    g_startMenuVisible = false;
                    requestRepaint( ); return 0;
                }
                if (inWorkArea) {
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
                        publishOut(MsgType::MT_InputMouse, Compositor::packMousePayloadForTarget(mx, my, 2, "down", ownerPid, hitWin->id), ownerPid);
                        requestRepaint();
                        return 0;
                    }
                    
                    // Otherwise, handle desktop icon right-click or show desktop context menu
                    int hitIdx = HitTestDesktopIcon(mx, my); if (hitIdx >= 0) { SelectDesktopIcon(hitIdx, false); Logger::write(LogLevel::Info, std::string("Desktop item context requested: ") + desktopLayoutKey(g_items[hitIdx])); RightClickMenu::ShowForDesktopItem(mx, my, hitIdx); requestRepaint( ); return 0; }
                    // Desktop right-click context menu (no icon hit)
                    RightClickMenu::Show(mx, my);
                    requestRepaint( );
                    return 0;
                }
            } break;
            case WM_LBUTTONUP: {
                int mx = GET_X_LPARAM(l); int my = GET_Y_LPARAM(l);
                if (g_iconSelectionDragActive || g_iconSelectionDragPending) {
                    if (g_iconSelectionDragActive) {
                        RECT selectionRect = normalizedRect(g_iconSelectionStartX, g_iconSelectionStartY, mx, my);
                        SelectIconsInRectangle(selectionRect, g_iconSelectionAdditive);
                    }
                    g_iconSelectionDragActive = false;
                    g_iconSelectionDragPending = false;
                    ReleaseCapture( );
                    requestRepaint( );
                    break;
                }
                if (g_iconDragActive || g_iconDragPending) {
                    ReleaseCapture( );
                    if (g_iconDragActive) {
                        // Save icon positions to config
                        g_cfg.iconPositions.clear( ); for (const auto& di : g_items) { DesktopIconPos ip; ip.name = desktopLayoutKey(di); ip.x = di.ix; ip.y = di.iy; g_cfg.iconPositions.push_back(ip); } saveDesktopConfig( );
                    }
                    g_iconDragActive = false; g_iconDragPending = false; requestRepaint( ); break;
                }
                uint64_t ownerPid = 0; uint64_t targetWindow = 0; { std::lock_guard<std::mutex> lk(g_lock); ownerPid = inputOwnerPid( ); targetWindow = g_modalWindow ? g_modalWindow : g_focus; }
                Compositor::handleMouse(mx, my, false, true); publishOut(MsgType::MT_InputMouse, Compositor::packMousePayloadForTarget(mx, my, 1, "up", ownerPid, targetWindow), ownerPid);
            } break;
            case WM_MOUSEMOVE: {
                int mx = GET_X_LPARAM(l); int my = GET_Y_LPARAM(l);
                { std::lock_guard<std::mutex> lk(g_lock); if (blockInputBehindModal(mx, my)) { requestRepaint( ); return 0; } }
                if (RightClickMenu::IsVisible()) {
                    if (RightClickMenu::ContainsPoint(mx, my) || RightClickMenu::IsStartMenuAppMenuVisible()) {
                        requestRepaint();
                        return 0;
                    }
                }
                if (g_iconSelectionDragPending || g_iconSelectionDragActive) {
                    g_iconSelectionCurrentX = mx;
                    g_iconSelectionCurrentY = my;
                    if (!g_iconSelectionDragActive) {
                        if (std::abs(mx - g_iconSelectionStartX) >= 4 || std::abs(my - g_iconSelectionStartY) >= 4) {
                            g_iconSelectionDragActive = true;
                        }
                    }
                    if (g_iconSelectionDragActive) {
                        RECT selectionRect = normalizedRect(g_iconSelectionStartX, g_iconSelectionStartY, mx, my);
                        SelectIconsInRectangle(selectionRect, g_iconSelectionAdditive);
                    }
                    requestRepaint( );
                    break;
                }
                if (g_iconDragPending && !g_iconDragActive) { if (std::abs(mx - g_iconDragStartX) >= 4 || std::abs(my - g_iconDragStartY) >= 4) { g_iconDragActive = true; } }
                if (g_iconDragActive && g_iconDragIndex >= 0 && g_iconDragIndex < (int)g_items.size( )) { int nx = mx - g_iconDragOffX; int ny = my - g_iconDragOffY; clampDesktopIconPosition(nx, ny); g_items[g_iconDragIndex].ix = nx; g_items[g_iconDragIndex].iy = ny; requestRepaint( ); break; }
                if (g_iconDragPending) { break; } // Skip handleMouse while drag is pending
                uint64_t ownerPid = 0; uint64_t targetWindow = 0; { std::lock_guard<std::mutex> lk(g_lock); WinInfo* hitWin = hitWindowAt(mx, my); if (hitWin) { ownerPid = hitWin->ownerPid; targetWindow = hitWin->id; } else { ownerPid = inputOwnerPid( ); targetWindow = g_modalWindow ? g_modalWindow : g_focus; } }
                Compositor::handleMouse(mx, my, false, false); publishOut(MsgType::MT_InputMouse, Compositor::packMousePayloadForTarget(mx, my, 0, "move", ownerPid, targetWindow), ownerPid);
            } break;
            case WM_MOUSEWHEEL: {
                int mx = GET_X_LPARAM(l);
                int my = GET_Y_LPARAM(l);
                short wheelDelta = GET_WHEEL_DELTA_WPARAM(w);
                if (wheelDelta == 0) return 0;
                {
                    std::lock_guard<std::mutex> lk(g_lock);
                    if (blockInputBehindModal(mx, my)) {
                        requestRepaint( );
                        return 0;
                    }
                }

                int wheelSteps = wheelDelta / WHEEL_DELTA;
                if (wheelSteps == 0) wheelSteps = wheelDelta > 0 ? 1 : -1;

                uint64_t ownerPid = 0;
                uint64_t targetWindow = 0;
                {
                    std::lock_guard<std::mutex> lk(g_lock);
                    WinInfo* hitWin = hitWindowAt(mx, my);
                    if (hitWin) {
                        ownerPid = hitWin->ownerPid;
                        targetWindow = hitWin->id;
                    } else {
                        ownerPid = inputOwnerPid( );
                        targetWindow = g_modalWindow ? g_modalWindow : g_focus;
                    }
                }

                if (targetWindow != 0) {
                    Compositor::handleMouse(mx, my, false, false);
                    publishOut(MsgType::MT_InputMouse,
                        Compositor::packMousePayloadForTarget(mx, my, 0, std::string("wheel:") + std::to_string(wheelSteps), ownerPid, targetWindow),
                        ownerPid);
                }
                return 0;
            }
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
                    int maxItems = g_startMenuAllProgs ? (int)g_startMenuAllProgsSorted.size( ) : (int)g_startMenuPinnedRecent.size( );
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
                            std::string action = g_startMenuAllProgs ? g_startMenuAllProgsSorted[g_startMenuSel] : g_startMenuPinnedRecent[g_startMenuSel];
                            logStartMenuLaunchTargetShadowDiagnostic(action);
                            // SHADOW_ONLY observation above is diagnostic-only; launchAction still receives
                            // the original legacy Start Menu dispatch string.
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

                bool appModelDemoFocused = false;
                {
                    std::lock_guard<std::mutex> lk(g_lock);
                    auto it = g_windows.find(g_focus);
                    appModelDemoFocused = it != g_windows.end() && it->second.title == "App Model Demo";
                }
                if (appModelDemoFocused && handleAppModelDemoKey(key)) {
                    requestRepaint();
                    return 0;
                }

                uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); ownerPid = inputOwnerPid( ); }
                publishOut(MsgType::MT_InputKey, std::to_string(key) + "|down", ownerPid);
            } break;
            case WM_KEYUP: { int key = (int)w; uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); ownerPid = inputOwnerPid( ); } publishOut(MsgType::MT_InputKey, std::to_string(key) + "|up", ownerPid); } break;
            }
            return DefWindowProcA(h, msg, w, l);
        }
#endif

        void Compositor::sendFocus(uint64_t winId) { uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(winId); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; } publishOut(MsgType::MT_SetFocus, std::to_string(winId), ownerPid); }
        void Compositor::loadWallpaper(const std::string& idOrPath) {
            const BackgroundEntry* entry = WallpaperRegistry::FindBackgroundById(idOrPath);
            if (!entry && !idOrPath.empty()) {
                std::string mappedId = WallpaperRegistry::IdForAssetPath(idOrPath);
                if (!mappedId.empty()) {
                    entry = WallpaperRegistry::FindBackgroundById(mappedId);
                }
            }
            if (!entry) {
                Logger::write(LogLevel::Warn, std::string("Compositor background fallback: invalid id/path '") + idOrPath + "'");
                entry = &WallpaperRegistry::DefaultBackground();
            }

            Logger::write(LogLevel::Info, std::string("Compositor background select id=") + entry->id +
                " kind=" + WallpaperRegistry::KindName(entry->kind) +
                " scale=" + WallpaperRegistry::NormalizeScaleModeOrDefault(g_backgroundScaleMode));

            if (entry->kind == BackgroundKind::Gradient) {
                g_wallpaperId = entry->id;
                g_wallpaperPath.clear();
                g_wallpaperImage.reset();
                g_gradientTopColor = entry->topColor;
                g_gradientBottomColor = entry->bottomColor;
                g_gradientAccentColor = entry->accentColor;
                Logger::write(LogLevel::Info, std::string("Compositor gradient select id=") + g_wallpaperId);
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                if (g_wallpaperBmp) { DeleteObject(g_wallpaperBmp); g_wallpaperBmp = nullptr; }
                g_wallpaperW = 0;
                g_wallpaperH = 0;
#endif
                return;
            }

            if (entry->kind == BackgroundKind::SolidColor) {
                g_wallpaperId = entry->id;
                g_wallpaperPath.clear();
                g_wallpaperImage.reset();
                g_gradientTopColor = entry->solidColor;
                g_gradientBottomColor = entry->solidColor;
                g_gradientAccentColor = entry->accentColor;
                Logger::write(LogLevel::Info, std::string("Compositor solid background select id=") + g_wallpaperId);
                return;
            }

            g_wallpaperId = entry->id;
            g_wallpaperPath = entry->fullImagePath;
            g_gradientTopColor = entry->topColor;
            g_gradientBottomColor = entry->bottomColor;
            g_gradientAccentColor = entry->accentColor;
            Logger::write(LogLevel::Info, std::string("Compositor wallpaper select id=") + g_wallpaperId + " full=" + g_wallpaperPath + " thumb=" + entry->thumbnailPath);
            g_wallpaperImage = ImageAdapter::LoadFromFile(g_wallpaperPath).image;
            if (!g_wallpaperImage) {
                Logger::write(LogLevel::Warn, std::string("Compositor wallpaper decode failed, trying default: ") + g_wallpaperPath);
                const BackgroundEntry& fallback = WallpaperRegistry::DefaultBackground();
                if (entry->id != fallback.id && fallback.kind == BackgroundKind::Image) {
                    Logger::write(LogLevel::Warn, std::string("Compositor wallpaper retrying default id=") + fallback.id + " full=" + fallback.fullImagePath);
                    g_wallpaperId = fallback.id;
                    g_wallpaperPath = fallback.fullImagePath;
                    g_gradientTopColor = fallback.topColor;
                    g_gradientBottomColor = fallback.bottomColor;
                    g_gradientAccentColor = fallback.accentColor;
                    g_wallpaperImage = ImageAdapter::LoadFromFile(g_wallpaperPath).image;
                    if (g_wallpaperImage) {
                        Logger::write(LogLevel::Info, std::string("Compositor wallpaper default decode succeeded id=") + g_wallpaperId + " full=" + g_wallpaperPath);
                    }
                }
                if (!g_wallpaperImage) {
                    Logger::write(LogLevel::Warn, std::string("Compositor wallpaper default failed, using gradient fallback for id=") + g_wallpaperId);
                }
            } else {
                Logger::write(LogLevel::Info, std::string("Compositor wallpaper decode succeeded id=") + g_wallpaperId + " full=" + g_wallpaperPath);
            }
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            if (g_wallpaperBmp) { DeleteObject(g_wallpaperBmp); g_wallpaperBmp = nullptr; }
            g_wallpaperW = 0;
            g_wallpaperH = 0;
#endif
        }

        void Compositor::freeWallpaper() {
            g_wallpaperImage.reset();
            g_wallpaperPath.clear();
            g_wallpaperId.clear();
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            if (g_wallpaperBmp) { DeleteObject(g_wallpaperBmp); g_wallpaperBmp = nullptr; }
            g_wallpaperW = 0;
            g_wallpaperH = 0;
#endif
        }

        void Compositor::invalidate(uint64_t winId) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            requestRepaint( );
#else
            g_needsRedraw = true;
            Logger::write(LogLevel::Info, std::string("invalidate called for window ") + std::to_string(winId) + ", g_needsRedraw=true");
#endif
        }
        void Compositor::emitWidgetEvt(uint64_t winId, int wid, const std::string& evt, const std::string& value) { uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(winId); if (it != g_windows.end( )) ownerPid = it->second.ownerPid; } publishOut(MsgType::MT_WidgetEvt, std::to_string(winId) + "|" + std::to_string(wid) + "|" + evt + "|" + value, ownerPid); }

        void Compositor::handleMouse(int mx, int my, bool down, bool up) {
            std::lock_guard<std::mutex> lk(g_lock); const DesktopTheme& theme = GetCurrentDesktopTheme(); const int titleBarH = theme.titleBarHeight; const int gripSize = 12;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
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
            WorkRect work = desktopWorkAreaForBounds(cr.right - cr.left, cr.bottom - cr.top);
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
                const int btnSize = std::max(12, titleBarH - theme.controlPadding * 2);
                const int btnGap = theme.titleButtonGap;
                int btnY = topW->y + theme.controlPadding;
                int closeLeft = topW->x + topW->w - theme.controlPadding - btnSize;
                int tombLeft = closeLeft - btnGap - btnSize;
                int maxLeft = tombLeft - btnGap - btnSize;
                int minLeft = maxLeft - btnGap - btnSize;
                bool overClose = (mx >= closeLeft && mx < closeLeft + btnSize && my >= btnY && my < btnY + btnSize);
                bool overTomb = (mx >= tombLeft && mx < tombLeft + btnSize && my >= btnY && my < btnY + btnSize);
                bool overMax = (mx >= maxLeft && mx < maxLeft + btnSize && my >= btnY && my < btnY + btnSize);
                bool overMin = (mx >= minLeft && mx < minLeft + btnSize && my >= btnY && my < btnY + btnSize);
                bool overAnyButton = overClose || overTomb || overMax || overMin;
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
                if (down && overAnyButton) {
                    g_dragPending = false;
                    g_dragPendingWin = 0;
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
                        if (g_modalWindow == id) g_modalWindow = 0;
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
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
                            RECT crL{ 0,0,1024,768 }; if (g_hwnd) GetClientRect(g_hwnd, &crL);
#else
                            RECT crL{ 0,0,1024,768 };
#endif
                            WorkRect maxWork = desktopWorkAreaForBounds(crL.right - crL.left, crL.bottom - crL.top);
                            topW->x = maxWork.left; topW->y = maxWork.top; topW->w = maxWork.right - maxWork.left; topW->h = maxWork.bottom - maxWork.top; topW->maximized = true; topW->snapState = 0; topW->dirty = true;
                        } else { // restore
                            topW->x = topW->prevX; topW->y = topW->prevY; topW->w = topW->prevW; topW->h = topW->prevH; topW->maximized = false; topW->dirty = true;
                        }
                        topW->titleBtnMaxPressed = false; topW->titleBtnMaxHover = false; invalidate(id); return;
                    }
                }
            }

            if (topW) { int wx = mx - topW->x - theme.windowPadding; int wy = my - topW->y - titleBarH - theme.windowPadding; for (auto& wd : topW->widgets) { bool over = (wx >= wd.x && wx < wd.x + wd.w && wy >= wd.y && wy < wd.y + wd.h); if (!down && !up) { if (wd.hover != over) { wd.hover = over; invalidate(topW->id); } } else if (down) { if (over) { wd.pressed = true; wd.hover = true; invalidate(topW->id); } } else if (up) { if (wd.pressed) { if (over) { emitWidgetEvt(topW->id, wd.id, "click", ""); Logger::write(LogLevel::Info, std::string("Widget clicked: ") + std::to_string(topW->id) + "/" + std::to_string(wd.id)); } wd.pressed = false; wd.hover = false; invalidate(topW->id); } } } }
            // move while dragging
            if (g_dragActive && !up) { auto it = g_windows.find(g_dragWin); if (it != g_windows.end( )) { WinInfo& w = it->second; if (!w.maximized && !w.minimized && !w.tombstoned) { int nx = mx - g_dragOffX; int ny = my - g_dragOffY; if (nx < work.left) nx = work.left; if (ny < work.top) ny = work.top; if (nx + w.w > work.right) nx = work.right - w.w; if (ny + w.h > work.bottom) ny = work.bottom - w.h; if (nx != w.x || ny != w.y) { w.x = nx; w.y = ny; w.dirty = true; invalidate(w.id); } } } }
            if (down) { uint64_t t = nowMs( ); for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { uint64_t wid = g_z[idx]; WinInfo& w = g_windows[wid]; if (w.minimized || w.tombstoned) continue; if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + titleBarH) { if (g_lastClickWin == w.id && (t - g_lastClickTicks) < 450) { if (!w.minimized) { if (!w.maximized) { w.prevX = w.x; w.prevY = w.y; w.prevW = w.w; w.prevH = w.h; w.x = work.left; w.y = work.top; w.w = work.right - work.left; w.h = work.bottom - work.top; w.maximized = true; } else { w.x = w.prevX; w.y = w.prevY; w.w = w.prevW; w.h = w.prevH; w.maximized = false; } } g_lastClickWin = 0; g_lastClickTicks = 0; invalidate(w.id); return; } g_lastClickWin = w.id; g_lastClickTicks = t; break; } } }
            if (down) { for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { WinInfo& w = g_windows[g_z[idx]]; if (w.minimized || w.maximized || w.tombstoned) continue; if (mx >= w.x + w.w - gripSize && mx < w.x + w.w && my >= w.y + w.h - gripSize && my < w.y + w.h) { g_resizeActive = true; g_resizeWin = w.id; g_resizeStartW = w.w; g_resizeStartH = w.h; g_resizeStartMX = mx; g_resizeStartMY = my; break; } } }
            if (g_dragActive && up) { auto it = g_windows.find(g_dragWin); if (it != g_windows.end( )) { WinInfo& w = it->second; const int snap = 16; bool nearLeft = mx <= snap, nearRight = mx >= cr.right - snap, nearTop = my <= snap; if (nearTop && !(nearLeft || nearRight)) { w.prevX = w.x; w.prevY = w.y; w.prevW = w.w; w.prevH = w.h; w.x = work.left; w.y = work.top; w.w = work.right - work.left; w.h = work.bottom - work.top; w.maximized = true; w.snapState = 0; } else if (nearLeft) { w.maximized = false; w.x = work.left; w.y = work.top; w.w = (work.right - work.left) / 2; w.h = work.bottom - work.top; w.snapState = 1; } else if (nearRight) { w.maximized = false; w.x = work.left + (work.right - work.left) / 2; w.y = work.top; w.w = (work.right - work.left) / 2; w.h = work.bottom - work.top; w.snapState = 2; } w.dirty = true; } g_dragActive = false; g_dragWin = 0; g_snapPreviewActive = false; invalidate(0); }
            if (g_resizeActive && !up) { auto it = g_windows.find(g_resizeWin); if (it != g_windows.end( )) { int dw = mx - g_resizeStartMX; int dh = my - g_resizeStartMY; int newW = g_resizeStartW + dw; if (newW < 160) newW = 160; int newH = g_resizeStartH + dh; if (newH < 120) newH = 120; g_resizePreviewActive = true; g_resizePreviewW = newW; g_resizePreviewH = newH; } }
            if (g_resizeActive && up) { auto it = g_windows.find(g_resizeWin); if (it != g_windows.end( )) { int dw = mx - g_resizeStartMX; int dh = my - g_resizeStartMY; int newW = g_resizeStartW + dw; if (newW < 160) newW = 160; int newH = g_resizeStartH + dh; if (newH < 120) newH = 120; it->second.w = newW; it->second.h = newH; it->second.dirty = true; } g_resizeActive = false; g_resizeWin = 0; g_resizePreviewActive = false; }
            if (down) { for (int idx = (int)g_z.size( ) - 1; idx >= 0; --idx) { WinInfo& w = g_windows[g_z[idx]]; if (w.minimized || w.tombstoned) continue; if (g_modalWindow != 0 && w.id != g_modalWindow) continue; if (mx >= w.x && mx < w.x + w.w && my >= w.y && my < w.y + w.h) { g_focus = w.id; for (auto itZ = g_z.begin( ); itZ != g_z.end( ); ++itZ) { if (*itZ == w.id) { g_z.erase(itZ); break; } } g_z.push_back(w.id); sendFocus(w.id); break; } } }
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
                    wi.modal = isDialogTitle(title);
                    wi.ownerPid = m.srcPid;
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
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
                    if (wi.modal) g_modalWindow = id;
                } 
                Logger::write(LogLevel::Info, std::string("Compositor created window id=") + std::to_string(id) + " sending ack to pid=" + std::to_string(m.srcPid));
                publishOut(MsgType::MT_Create, std::to_string(id) + "|" + title, m.srcPid); sendFocus(id); invalidate(id); } break;
            case MsgType::MT_RequestFrame: { uint64_t id = 0; uint64_t ownerPid = 0; int width = 0; int height = 0; try { id = std::stoull(s); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( ) && it->second.ownerPid == m.srcPid) { ownerPid = it->second.ownerPid; width = it->second.w; height = it->second.h; it->second.dirty = true; } } if (ownerPid != 0) publishOut(MsgType::MT_RequestFrame, std::to_string(id) + "|" + std::to_string(width) + "|" + std::to_string(height), ownerPid); invalidate(id); } break;
            case MsgType::MT_DrawText: { std::istringstream iss(s); std::string idS; std::getline(iss, idS, '|'); std::string text; std::getline(iss, text); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { if (text == "\f") { it->second.texts.clear(); it->second.positionedTexts.clear(); it->second.rects.clear(); it->second.images.clear(); } else { it->second.texts.push_back(text); } it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_DrawText, std::to_string(id) + "|" + text, ownerPid); invalidate(id); } break;
            case MsgType::MT_DrawTextAt: { std::istringstream iss(s); std::string idS, xs, ys; std::getline(iss, idS, '|'); std::getline(iss, xs, '|'); std::getline(iss, ys, '|'); std::string text; std::getline(iss, text); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} DrawTextItem item{ std::stoi(xs), std::stoi(ys), text }; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { it->second.positionedTexts.push_back(item); it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_DrawTextAt, std::to_string(id), ownerPid); invalidate(id); } break;
            case MsgType::MT_DrawTextAtColor: { std::istringstream iss(s); std::string idS, xs, ys, rs, gs, bs; std::getline(iss, idS, '|'); std::getline(iss, xs, '|'); std::getline(iss, ys, '|'); std::getline(iss, rs, '|'); std::getline(iss, gs, '|'); std::getline(iss, bs, '|'); std::string text; std::getline(iss, text); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} DrawTextItem item{ std::stoi(xs), std::stoi(ys), text, true, (uint8_t)std::stoi(rs), (uint8_t)std::stoi(gs), (uint8_t)std::stoi(bs) }; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { it->second.positionedTexts.push_back(item); it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_DrawTextAtColor, std::to_string(id), ownerPid); invalidate(id); } break;
            case MsgType::MT_Close: { uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(s); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); auto wit = g_windows.find(id); if (wit != g_windows.end( )) ownerPid = wit->second.ownerPid; g_windows.erase(id); auto it = std::find(g_z.begin( ), g_z.end( ), id); if (it != g_z.end( )) g_z.erase(it); if (g_modalWindow == id) g_modalWindow = 0; if (g_focus == id) g_focus = 0; } publishOut(MsgType::MT_Close, std::to_string(id), ownerPid ? ownerPid : m.srcPid); invalidate(0); } break;
            case MsgType::MT_DrawRect: { std::istringstream iss(s); std::string idS; std::getline(iss, idS, '|'); std::string xs, ys, ws, hs, rs, gs, bs; std::getline(iss, xs, '|'); std::getline(iss, ys, '|'); std::getline(iss, ws, '|'); std::getline(iss, hs, '|'); std::getline(iss, rs, '|'); std::getline(iss, gs, '|'); std::getline(iss, bs, '|'); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} DrawRectItem item{ std::stoi(xs), std::stoi(ys), std::stoi(ws), std::stoi(hs), (uint8_t)std::stoi(rs),(uint8_t)std::stoi(gs),(uint8_t)std::stoi(bs) }; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { it->second.rects.push_back(item); it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_DrawRect, std::to_string(id), ownerPid); invalidate(id); } break;
            case MsgType::MT_DrawImage:
            case MsgType::MT_DrawImageAnimated: { DrawImageSpec spec{}; uint64_t ownerPid = 0; if (!unpackDrawImage(s, spec)) break; ImageBitmap image{}; std::vector<ImageBitmap> frames; if ((MsgType)m.type == MsgType::MT_DrawImageAnimated) { const size_t marker = spec.path.find("{frame}"); if (marker != std::string::npos) { for (int frame = 0; frame < 100; ++frame) { std::ostringstream frameName; frameName << std::setw(2) << std::setfill('0') << frame; std::string path = spec.path; path.replace(marker, 7, frameName.str()); ImageBitmap candidate = loadCachedUiImage(path); if (candidate.status != ImageLoadStatus::Ok) break; frames.push_back(candidate); } } if (!frames.empty()) image = frames.front(); } else { image = ImageAdapter::LoadFromFile(spec.path); } if (image.status != ImageLoadStatus::Ok) { Logger::write(LogLevel::Warn, std::string("Compositor: DrawImage skipped, image load failed: ") + spec.path + " status=" + ImageLoadStatusName(image.status)); break; } DrawImageItem item{ spec.x, spec.y, spec.w, spec.h, spec.path, image, std::move(frames) }; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(spec.winId); if (it != g_windows.end( )) { it->second.images.push_back(std::move(item)); it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut((MsgType)m.type, std::to_string(spec.winId), ownerPid); invalidate(spec.winId); } break;
            case MsgType::MT_SetTitle: { std::istringstream iss(s); std::string idS; std::getline(iss, idS, '|'); std::string title; std::getline(iss, title); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( )) { it->second.title = title; it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_SetTitle, std::to_string(id) + "|" + title, ownerPid); invalidate(id); } break;
            case MsgType::MT_Move: { std::istringstream iss(s); std::string idS, xs, ys; std::getline(iss, idS, '|'); std::getline(iss, xs, '|'); std::getline(iss, ys, '|'); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} int nx = std::stoi(xs), ny = std::stoi(ys); { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( ) && !it->second.maximized) { it->second.x = nx; it->second.y = ny; it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_Move, std::to_string(id) + "|" + xs + "|" + ys, ownerPid); invalidate(id); } break;
            case MsgType::MT_Resize: { std::istringstream iss(s); std::string idS, ws, hs; std::getline(iss, idS, '|'); std::getline(iss, ws, '|'); std::getline(iss, hs, '|'); uint64_t id = 0; uint64_t ownerPid = 0; try { id = std::stoull(idS); } catch (...) {} int nw = std::stoi(ws), nh = std::stoi(hs); { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(id); if (it != g_windows.end( ) && !it->second.maximized) { it->second.w = nw; it->second.h = nh; it->second.dirty = true; ownerPid = it->second.ownerPid; } } publishOut(MsgType::MT_Resize, std::to_string(id) + "|" + ws + "|" + hs, ownerPid); invalidate(id); } break;
            case MsgType::MT_WidgetAdd: { // format: <winId>|<type>|<id>|<x>|<y>|<w>|<h>|<text>
                std::istringstream iss(s); std::string winS, typeS, idS, xs, ys, ws2, hs2; std::getline(iss, winS, '|'); std::getline(iss, typeS, '|'); std::getline(iss, idS, '|'); std::getline(iss, xs, '|'); std::getline(iss, ys, '|'); std::getline(iss, ws2, '|'); std::getline(iss, hs2, '|'); std::string rest; std::getline(iss, rest); uint64_t winId = 0; try { winId = std::stoull(winS); } catch (...) {} int wtype = 0; try { wtype = std::stoi(typeS); } catch (...) {} int wid = 0; try { wid = std::stoi(idS); } catch (...) {} int wx = 0, wy = 0, ww = 0, wh = 0; try { wx = std::stoi(xs); wy = std::stoi(ys); ww = std::stoi(ws2); wh = std::stoi(hs2); } catch (...) {}
                Widget wd; wd.type = (WidgetType)wtype; wd.id = wid; wd.x = wx; wd.y = wy; wd.w = ww; wd.h = wh; wd.text = rest;
                uint64_t ownerPid = 0; { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(winId); if (it != g_windows.end( )) { auto existing = std::find_if(it->second.widgets.begin(), it->second.widgets.end(), [wid](const Widget& widget) { return widget.id == wid; }); if (existing != it->second.widgets.end()) { *existing = wd; } else { it->second.widgets.push_back(wd); } it->second.dirty = true; ownerPid = it->second.ownerPid; Logger::write(LogLevel::Info, std::string("Widget added to ") + std::to_string(winId) + " id=" + std::to_string(wid)); } }
                publishOut(MsgType::MT_WidgetAdd, std::to_string(winId) + "|" + std::to_string(wid), ownerPid); invalidate(winId);
            } break;
            case MsgType::MT_WidgetSetIcon: {
                std::istringstream iss(s); std::string winS, idS, path; std::getline(iss, winS, '|'); std::getline(iss, idS, '|'); std::getline(iss, path);
                uint64_t winId = 0; int wid = -1; try { winId = std::stoull(winS); wid = std::stoi(idS); } catch (...) {}
                ImageBitmap icon = loadCachedUiImage(path); uint64_t ownerPid = 0;
                if (icon.status == ImageLoadStatus::Ok) { std::lock_guard<std::mutex> lk(g_lock); auto it = g_windows.find(winId); if (it != g_windows.end()) { auto widget = std::find_if(it->second.widgets.begin(), it->second.widgets.end(), [wid](const Widget& item) { return item.id == wid; }); if (widget != it->second.widgets.end()) { widget->iconPath = path; widget->icon = icon; it->second.dirty = true; ownerPid = it->second.ownerPid; } } }
                publishOut(MsgType::MT_WidgetSetIcon, std::to_string(winId) + "|" + std::to_string(wid), ownerPid); invalidate(winId);
            } break;
            case MsgType::MT_WindowList: { std::ostringstream oss; bool first = true; { std::lock_guard<std::mutex> lk(g_lock); for (uint64_t id : g_z) { auto it = g_windows.find(id); if (it == g_windows.end( )) continue; if (!first) oss << ";"; first = false; oss << it->first << "|" << it->second.title << "|" << (it->second.minimized ? 1 : 0); } } const std::string diag = hostedFreezeDiagnosticsCompactSummary( ); if (!diag.empty()) { if (!first) oss << ";"; oss << diag; } publishOut(MsgType::MT_WindowList, oss.str( ), m.srcPid); } break;
            case MsgType::MT_Activate: { uint64_t id = 0; try { id = std::stoull(s); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); if (g_modalWindow != 0 && id != g_modalWindow) id = g_modalWindow; for (auto it = g_z.begin( ); it != g_z.end( ); ++it) { if (*it == id) { g_z.erase(it); break; } } auto wit = g_windows.find(id); if (wit != g_windows.end( )) { wit->second.minimized = false; wit->second.tombstoned = false; } g_z.push_back(id); g_focus = id; } sendFocus(id); invalidate(id); } break;
            case MsgType::MT_Minimize: { uint64_t id = 0; try { id = std::stoull(s); } catch (...) {} { std::lock_guard<std::mutex> lk(g_lock); if (g_modalWindow != 0 && id != g_modalWindow) break; auto wit = g_windows.find(id); if (wit != g_windows.end( )) { wit->second.minimized = true; wit->second.tombstoned = true; if (g_modalWindow == id) g_modalWindow = 0; if (g_focus == id) g_focus = 0; } } invalidate(id); } break;
            case MsgType::MT_ShowDesktopToggle: { { std::lock_guard<std::mutex> lk(g_lock); if (g_modalWindow != 0) { for (auto it = g_z.begin( ); it != g_z.end( ); ++it) { if (*it == g_modalWindow) { g_z.erase(it); break; } } g_z.push_back(g_modalWindow); g_focus = g_modalWindow; invalidate(g_modalWindow); break; } } if (!g_showDesktopActive) { g_showDesktopMinimized.clear( ); for (uint64_t id : g_z) { auto it = g_windows.find(id); if (it != g_windows.end( ) && !it->second.minimized) { it->second.minimized = true; it->second.tombstoned = true; g_showDesktopMinimized.push_back(id); } } g_focus = 0; g_showDesktopActive = true; } else { for (uint64_t id : g_showDesktopMinimized) { auto it = g_windows.find(id); if (it != g_windows.end( )) { it->second.minimized = false; it->second.tombstoned = false; } } g_showDesktopMinimized.clear( ); g_showDesktopActive = false; } invalidate(0); } break;
            case MsgType::MT_StateSave: { std::string path = s; std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for (size_t i = 0; i < g_z.size( ); ++i) { uint64_t id = g_z[i]; auto it = g_windows.find(id); if (it == g_windows.end( )) continue; const WinInfo& w = it->second; SavedWindow rec; rec.id = w.id; rec.title = w.title; rec.x = w.x; rec.y = w.y; rec.w = w.w; rec.h = w.h; rec.minimized = w.minimized; rec.maximized = w.maximized; rec.z = (int)i; rec.focused = (g_focus == w.id); rec.snap = w.snapState; sw.push_back(rec); } } std::string err; if (!DesktopState::Save(path, sw, err)) publishOut(MsgType::MT_WidgetEvt, std::string("STATE_SAVE_ERR|") + err); else publishOut(MsgType::MT_WidgetEvt, std::string("STATE_SAVE_OK|") + path); } break;
            case MsgType::MT_StateLoad: { std::string path = s; std::vector<SavedWindow> sw; std::string err; if (!DesktopState::Load(path, sw, err)) { publishOut(MsgType::MT_WidgetEvt, std::string("STATE_LOAD_ERR|") + err); } else { { std::lock_guard<std::mutex> lk(g_lock); g_windows.clear( ); g_z.clear( ); g_focus = 0; std::sort(sw.begin( ), sw.end( ), [] (const SavedWindow& a, const SavedWindow& b) { return a.z < b.z; }); for (auto& w : sw) { uint64_t id = s_nextWinId.fetch_add(1); WinInfo wi{}; wi.id = id; wi.title = w.title; wi.x = w.x; wi.y = w.y; wi.w = w.w; wi.h = w.h; wi.minimized = w.minimized; wi.maximized = w.maximized; wi.dirty = true; wi.snapState = w.snap; if (wi.maximized) { RECT crL{ 0,0,1024,768 }; if (g_hwnd) GetClientRect(g_hwnd, &crL); int taskbarY = crL.bottom - 40; wi.x = crL.left; wi.y = crL.top; wi.w = crL.right - crL.left; wi.h = taskbarY - crL.top; } g_windows[id] = wi; g_z.push_back(id); if (w.focused && !wi.minimized) g_focus = id; } } publishOut(MsgType::MT_WidgetEvt, std::string("STATE_LOAD_OK|") + path); invalidate(0); } } break;
            case MsgType::MT_Invalidate: { invalidate(0); } break;
            case MsgType::MT_Ping: { publishOut(MsgType::MT_Ping, s); } break;
            case MsgType::MT_DesktopLaunch: { launchAction(s); } break;
            case MsgType::MT_DesktopPins: { std::istringstream iss(s); std::string tok; while (std::getline(iss, tok, ';')) { if (tok.size( ) < 2) continue; if (tok[0] == '+') pinAction(tok.substr(1)); else if (tok[0] == '-') unpinAction(tok.substr(1)); } } break;
            case MsgType::MT_DesktopWallpaperSet: { loadWallpaper(s); g_cfg.wallpaperId = g_wallpaperId; g_cfg.wallpaperPath = g_wallpaperPath; g_cfg.backgroundScaleMode = g_backgroundScaleMode; saveDesktopConfig( ); invalidate(0); } break;
            case MsgType::MT_DesktopConfigReload: {
                DesktopConfigData cfg;
                std::string cfgErr;
                if (DesktopConfig::Load("desktop.json", cfg, cfgErr)) {
                    g_cfg = cfg;
                    syncDesktopThemeFromConfig(g_cfg);
                    g_taskbarPosition = parseTaskbarPosition(g_cfg.taskbarPosition);
                    g_cfg.taskbarPosition = taskbarPositionName(g_taskbarPosition);
                    g_backgroundScaleMode = WallpaperRegistry::NormalizeScaleModeOrDefault(g_cfg.backgroundScaleMode.empty() ? "fill" : g_cfg.backgroundScaleMode);
                    g_cfg.backgroundScaleMode = g_backgroundScaleMode;
                    Logger::write(LogLevel::Info, std::string("Desktop config reloaded for system icon visibility: Trash=") + (g_cfg.showDesktopTrash ? "true" : "false") +
                        " FileExplorer=" + ((g_cfg.showDesktopThisSystem || g_cfg.showDesktopFileManager) ? "true" : "false") +
                        " SystemSettings=" + (g_cfg.showDesktopSystemSettings ? "true" : "false") +
                        " FolderIconsSmall=" + (g_cfg.smallLiveDesktopFolderIcons ? "true" : "false") +
                        " AutoArrange=" + (g_cfg.autoArrangeDesktopIcons ? "true" : "false"));
                    refreshDesktopItems();
                    invalidate(0);
                } else {
                    Logger::write(LogLevel::Warn, "Desktop config reload requested but load failed: " + cfgErr);
                }
            } break;
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
                    bool blockedByModal = false;
                    {
                        std::lock_guard<std::mutex> lk(g_lock);
                        blockedByModal = blockInputBehindModal(mx, my);
                    }
                    if (blockedByModal) {
                        invalidate(0);
                        break;
                    }
                    
                    // Handle based on button and action
                    if (button == 1) { // Left button
                        if (action == "down") {
                            uint64_t ownerPid = 0;
                            uint64_t targetWindow = 0;
                            {
                                std::lock_guard<std::mutex> lk(g_lock);
                                WinInfo* hitWin = hitWindowAt(mx, my);
                                if (hitWin) {
                                    ownerPid = hitWin->ownerPid;
                                    targetWindow = hitWin->id;
                                } else {
                                    ownerPid = inputOwnerPid();
                                    targetWindow = g_modalWindow ? g_modalWindow : g_focus;
                                }
                            }
                            handleMouse(mx, my, true, false);
                            publishOut(MsgType::MT_InputMouse, Compositor::packMousePayloadForTarget(mx, my, 1, "down", ownerPid, targetWindow), ownerPid);
                        } else if (action == "up") {
                            uint64_t ownerPid = 0;
                            uint64_t targetWindow = 0;
                            {
                                std::lock_guard<std::mutex> lk(g_lock);
                                ownerPid = inputOwnerPid();
                                targetWindow = g_modalWindow ? g_modalWindow : g_focus;
                            }
                            handleMouse(mx, my, false, true);
                            publishOut(MsgType::MT_InputMouse, Compositor::packMousePayloadForTarget(mx, my, 1, "up", ownerPid, targetWindow), ownerPid);
                        }
                    } else if (button == 2) { // Right button
                        if (action == "down") {
                            // Right-click handling - check if over a window
                            uint64_t ownerPid = 0;
                            uint64_t targetWindow = 0;
                            bool blockedRightClick = false;
                            {
                                std::lock_guard<std::mutex> lk(g_lock);
                                blockedRightClick = blockInputBehindModal(mx, my);
                                WinInfo* hitWin = hitWindowAt(mx, my);
                                if (hitWin) {
                                    if (g_modalWindow != 0 && hitWin->id != g_modalWindow) {
                                        blockedRightClick = true;
                                    }
                                    if (blockedRightClick) {
                                        targetWindow = 0;
                                    } else {
                                        ownerPid = hitWin->ownerPid;
                                        targetWindow = hitWin->id;
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
                            }
                            if (blockedRightClick) {
                                invalidate(0);
                                break;
                            }
                            
                            // If right-click is on a window, forward the event to the application
                            if (targetWindow != 0) {
                                publishOut(MsgType::MT_InputMouse, Compositor::packMousePayloadForTarget(mx, my, 2, "down", ownerPid, targetWindow), ownerPid);
                                invalidate(0);
                            } else {
                                // Desktop right-click - show desktop context menu
                                RightClickMenu::Show(mx, my);
                                invalidate(0);
                            }
                        }
                    } else if (button == 0) { // Mouse move
                        if (action == "move") {
                            uint64_t ownerPid = 0;
                            uint64_t targetWindow = 0;
                            {
                                std::lock_guard<std::mutex> lk(g_lock);
                                WinInfo* hitWin = hitWindowAt(mx, my);
                                if (hitWin) {
                                    ownerPid = hitWin->ownerPid;
                                    targetWindow = hitWin->id;
                                } else {
                                    ownerPid = inputOwnerPid();
                                    targetWindow = g_modalWindow ? g_modalWindow : g_focus;
                                }
                            }
                            handleMouse(mx, my, false, false);
                            publishOut(MsgType::MT_InputMouse, Compositor::packMousePayloadForTarget(mx, my, 0, "move", ownerPid, targetWindow), ownerPid);
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
                        ownerPid = inputOwnerPid( );
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
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            requestRepaint( );
#else
            if (g_needsRedraw) {
                renderToFramebuffer();
            }
#endif
        }
        void Compositor::pumpEvents( ) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            hostedFreezeDiagnosticsOnPump( );
            MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); if (msg.message == WM_QUIT) break; }
#else
            // On bare-metal, we don't have a native event pump
            // Events come through IPC from the kernel input subsystem
            // Trigger a redraw if needed
            if (g_needsRedraw) {
                renderToFramebuffer();
            }
#endif
            bool hasAnimatedImage = false;
            {
                std::lock_guard<std::mutex> lk(g_lock);
                for (const auto& window : g_windows) {
                    for (const auto& image : window.second.images) {
                        if (!image.frames.empty()) {
                            hasAnimatedImage = true;
                            break;
                        }
                    }
                    if (hasAnimatedImage) break;
                }
            }
            if (hasAnimatedImage) requestRepaint();
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            hostedFreezeDiagnosticsLogHeartbeat("pumpEvents");
#endif
        }

        int Compositor::main(int argc, char** argv) {
            Logger::write(LogLevel::Info, "Compositor service started (native window)");
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            initWindow( );
#endif
            initVideoBackend( );
            DesktopConfigData cfg; std::string cfgErr; bool cfgOk = DesktopConfig::Load("desktop.json", cfg, cfgErr);
            g_cfg = cfg; // Store config
            Logger::write(LogLevel::Info, std::string("Compositor DesktopConfig loaded=") + (cfgOk ? "true" : "false") + " err=" + cfgErr);
            syncDesktopThemeFromConfig(g_cfg);
            g_taskbarPosition = parseTaskbarPosition(g_cfg.taskbarPosition);
            g_cfg.taskbarPosition = taskbarPositionName(g_taskbarPosition);
            Logger::write(LogLevel::Info, std::string("Compositor taskbar position=") + g_cfg.taskbarPosition);
            std::string configuredScale = cfg.backgroundScaleMode.empty() ? "fill" : cfg.backgroundScaleMode;
            g_backgroundScaleMode = WallpaperRegistry::NormalizeScaleModeOrDefault(configuredScale);
            if (configuredScale != g_backgroundScaleMode) {
                Logger::write(LogLevel::Warn, std::string("Compositor unsupported background scale mode '") + configuredScale + "', falling back to fill");
            }
            g_cfg.backgroundScaleMode = g_backgroundScaleMode;
            Logger::write(LogLevel::Info, std::string("Compositor background scale mode=") + g_backgroundScaleMode);
            logCompositorList("config pinned before merge", g_cfg.pinned);
            logCompositorList("config recent before merge", g_cfg.recent);
            for (const auto& pinned : DesktopService::GetPinned()) {
                Logger::write(LogLevel::Info, std::string("Compositor DesktopService pin considered: ") + pinned.name);
                mergeVisibleAppEntry(g_cfg.pinned, pinned.name, "DesktopService pin", true);
            }
            for (const auto& app : DesktopService::GetAppModelDemoApps()) {
                Logger::write(LogLevel::Info, std::string("Compositor app-model app considered: ") + app.displayName + " id=" + app.id + " source=" + app.source);
                mergeVisibleAppEntry(g_cfg.pinned, app.displayName, "AppModel registry", true);
                mergeVisibleAppEntry(g_cfg.recent, app.displayName, "AppModel registry", false);
            }
            if (hasListItem(g_cfg.pinned, "AppModel") && !hasEquivalentListItem(g_cfg.pinned, "App Model Demo")) {
                Logger::write(LogLevel::Info, "Compositor replacing legacy AppModel pin with App Model Demo alias");
                for (auto& pinned : g_cfg.pinned) if (pinned == "AppModel") pinned = "App Model Demo";
            }
            logCompositorList("config pinned after merge", g_cfg.pinned);
            logCompositorList("config recent after merge", g_cfg.recent);
            refreshDesktopItems( ); // Populate g_items from pinned/recent
            refreshAllProgramsList( ); // Populate sorted all programs list
            saveDesktopConfig( );
            if (cfgOk) {
                if (!cfg.wallpaperId.empty()) loadWallpaper(cfg.wallpaperId);
                else if (!cfg.wallpaperPath.empty()) loadWallpaper(cfg.wallpaperPath);
                else loadWallpaper(WallpaperRegistry::DefaultBackground().id);
                g_cfg.wallpaperId = g_wallpaperId;
                g_cfg.wallpaperPath = g_wallpaperPath;
                g_cfg.backgroundScaleMode = g_backgroundScaleMode;
            } else {
                loadWallpaper(WallpaperRegistry::DefaultBackground().id);
                g_cfg.wallpaperId = g_wallpaperId;
                g_cfg.wallpaperPath = g_wallpaperPath;
                g_cfg.backgroundScaleMode = g_backgroundScaleMode;
            }

            bool legacyLoaded = false; if (!cfgOk || cfg.windows.empty( )) {
                std::vector<SavedWindow> sw; std::string err; if (DesktopState::Load("desktop.state", sw, err)) {
                    std::lock_guard<std::mutex> lk(g_lock); g_windows.clear( ); g_z.clear( ); g_focus = 0; std::sort(sw.begin( ), sw.end( ), [] (const SavedWindow& a, const SavedWindow& b) { return a.z < b.z; }); for (auto& w : sw) {
                        uint64_t id = s_nextWinId.fetch_add(1); WinInfo wi{}; wi.id = id; wi.title = w.title; wi.x = w.x; wi.y = w.y; wi.w = w.w; wi.h = w.h; wi.minimized = w.minimized; wi.maximized = w.maximized; wi.dirty = true; wi.snapState = w.snap; if (wi.maximized) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
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
            
            bool running = true; while (running) { pumpEvents( ); ipc::Message m; if (ipc::Bus::pop(kGuiChanIn, m, 30)) { if (m.type == (uint32_t)MsgType::MT_Ping && m.data.size( ) == 3 && std::string(m.data.begin( ), m.data.end( )) == "bye") running = false; else { const uint64_t msgStartMs = nowMs( ); hostedFreezeDiagnosticsOnMessageBegin(m.type); handleMessage(m); hostedFreezeDiagnosticsOnMessageEnd(nowMs( ) - msgStartMs); } } }
            DesktopConfigData outCfg = g_cfg; { std::lock_guard<std::mutex> lk(g_lock); outCfg.windows.clear( ); for (size_t i = 0; i < g_z.size( ); ++i) { uint64_t id = g_z[i]; auto it = g_windows.find(id); if (it == g_windows.end( )) continue; const WinInfo& w = it->second; DesktopWindowRec rec; rec.id = w.id; rec.title = w.title; rec.x = w.x; rec.y = w.y; rec.w = w.w; rec.h = w.h; rec.minimized = w.minimized; rec.maximized = w.maximized; rec.z = (int)i; rec.focused = (g_focus == w.id); rec.snap = w.snapState; outCfg.windows.push_back(rec); } }
            std::string cerr; DesktopConfig::Save("desktop.json", outCfg, cerr); if (!legacyLoaded) { std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for (auto& kv : g_windows) { sw.push_back(SavedWindow{ kv.second.id, kv.second.title, kv.second.x, kv.second.y, kv.second.w, kv.second.h, kv.second.minimized, kv.second.maximized }); } } std::string err; DesktopState::Save("desktop.state", sw, err); }
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            shutdownWindow( );
#endif
            Logger::write(LogLevel::Info, "Compositor service stopping"); return 0;
        }
        std::vector<WindowDebugInfo> Compositor::debugWindowsSnapshot()
        {
            std::vector<WindowDebugInfo> out;
            std::lock_guard<std::mutex> lk(g_lock);
            out.reserve(g_z.size());
            for (uint64_t id : g_z) {
                auto it = g_windows.find(id);
                if (it == g_windows.end()) continue;
                const WinInfo& w = it->second;
                WindowDebugInfo info;
                info.id = w.id;
                info.ownerPid = w.ownerPid;
                info.title = w.title;
                info.x = w.x;
                info.y = w.y;
                info.w = w.w;
                info.h = w.h;
                info.widgetCount = static_cast<int>(w.widgets.size());
                info.minimized = w.minimized;
                info.visible = w.visible && !w.tombstoned;
                out.push_back(info);
            }
            return out;
        }

        uint64_t Compositor::start( ) { ProcessSpec spec{ "compositor", Compositor::main }; spec.appId = "gxos.system.compositor"; return ProcessTable::spawn(spec, { "compositor" }); }

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        void Compositor::drawTaskbarSearchBox(HDC dc, int x, int y, int w, int h) {
            const DesktopTheme& theme = GetCurrentDesktopTheme();
            const bool sciFiTheme = hostedSciFiTheme(theme);
            const uint32_t bgColor = sciFiTheme ? hostedPanelSurfaceColor(theme) : 0xFF32343Eu;
            const uint32_t borderColor = sciFiTheme ? hostedPanelBorderColor(theme) : 0xFF4B4E5Au;
            const uint32_t iconColor = sciFiTheme ? WindowRenderer::BlendThemeColor(theme.accent, theme.mutedAccent, 34) : 0xFF8C91A0u;
            const uint32_t textColor = sciFiTheme ? WindowRenderer::BlendThemeColor(theme.windowBorder, theme.taskbarBackground, 36) : 0xFF646976u;

            HBRUSH bg = CreateSolidBrush(WindowRenderer::ToColorRef(bgColor));
            RECT r = { x, y, x + w, y + h };
            FillRect(dc, &r, bg);
            DeleteObject(bg);
            HPEN border = CreatePen(PS_SOLID, 1, WindowRenderer::ToColorRef(borderColor));
            HGDIOBJ oldPen = SelectObject(dc, border);
            HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, x, y, x + w, y + h);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBr);
            DeleteObject(border);
            if (sciFiTheme && h > 1) {
                HBRUSH accent = CreateSolidBrush(WindowRenderer::ToColorRef(hostedTaskbarHighlightColor(theme)));
                RECT line{ x, y, x + w, y + 1 };
                FillRect(dc, &line, accent);
                DeleteObject(accent);
            }
            // Magnifying glass icon (small circle + line)
            int iconX = x + 8;
            int iconY = y + (h / 2);
            HPEN iconPen = CreatePen(PS_SOLID, 1, WindowRenderer::ToColorRef(iconColor));
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
            const char* placeholder = "Search apps...";
            drawUiText(dc, x + 20, y + (h - uiTextHeight(FontRole::Small)) / 2, placeholder, (int)strlen(placeholder), WindowRenderer::ToColorRef(textColor), FontRole::Small);
        }

        void Compositor::drawSystemTray(HDC dc, RECT cr, int taskbarH) {
            // System tray is drawn to the left of the clock area
            // Calculate clock width first to position tray
            std::time_t now = std::time(nullptr);
            std::tm ltBuf{};
            localtime_s(&ltBuf, &now);
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ltBuf.tm_hour, ltBuf.tm_min);
            int clockW = measureUiText(timeBuf, (int)strlen(timeBuf), FontRole::Small) + 32; // approximate clock area width
            int trayX = cr.right - clockW - SystemTray::Width( ) - 16;
            WorkRect tb = taskbarRectForBounds(cr.right - cr.left, cr.bottom - cr.top);
            int trayY = tb.top;
            SystemTray::Draw(dc, trayX, trayY, taskbarH);
        }

        void Compositor::drawTaskbarTooltip(HDC dc, int x, int y, const char* text) {
            if (!text || !text[0]) return;
            int pad = 6;
            int tipW = measureUiText(text, (int)strlen(text), FontRole::Small) + pad * 2;
            int tipH = uiTextHeight(FontRole::Small) + pad * 2;
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
            drawUiText(dc, tipX + pad, tipY + pad, text, (int)strlen(text), RGB(220, 220, 230), FontRole::Small);
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

        static void fbDrawRectThick(uint32_t* pixels, int pitch, int bufW, int bufH,
                                    int x, int y, int w, int h, int thickness, uint32_t color) {
            if (thickness <= 1) {
                fbDrawRect(pixels, pitch, bufW, bufH, x, y, w, h, color);
                return;
            }
            for (int i = 0; i < thickness; ++i) {
                fbDrawRect(pixels, pitch, bufW, bufH, x + i, y + i, w - (i * 2), h - (i * 2), color);
            }
        }

        static int fbMeasureText(const char* text, int len = -1, FontRole role = FontRole::Default) {
            return SystemFont::MeasureWidth(role, text, len);
        }

        static void fbDrawText(uint32_t* pixels, int pitch, int bufW, int bufH,
                               int x, int y, const char* text, int len, uint32_t color,
                               FontRole role = FontRole::Default) {
            SystemFont::DrawTextToBuffer(pixels, pitch, bufW, bufH, x, y, text, len, color, role);
        }

        static void fbDrawText(uint32_t* pixels, int pitch, int bufW, int bufH,
                               int x, int y, const std::string& text, uint32_t color,
                               FontRole role = FontRole::Default) {
            fbDrawText(pixels, pitch, bufW, bufH, x, y, text.c_str(), static_cast<int>(text.size()), color, role);
        }

        static std::vector<std::string> fbWrapTextToWidth(const std::string& text, int maxWidth, FontRole role, size_t maxLines = 3) {
            std::vector<std::string> lines;
            if (text.empty() || maxWidth <= 0 || maxLines == 0) return lines;
            size_t pos = 0;
            while (pos < text.size() && lines.size() < maxLines) {
                while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
                if (pos >= text.size()) break;
                std::string line;
                while (pos < text.size()) {
                    size_t wordStart = pos;
                    while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
                    std::string word = text.substr(wordStart, pos - wordStart);
                    std::string candidate = line.empty() ? word : line + " " + word;
                    if (fbMeasureText(candidate.c_str(), static_cast<int>(candidate.size()), role) <= maxWidth) {
                        line = candidate;
                    } else {
                        if (line.empty()) {
                            std::string piece;
                            for (char ch : word) {
                                std::string next = piece + ch;
                                if (!piece.empty() && fbMeasureText(next.c_str(), static_cast<int>(next.size()), role) > maxWidth) {
                                    lines.push_back(piece);
                                    piece.clear();
                                    if (lines.size() >= maxLines) return lines;
                                }
                                piece.push_back(ch);
                            }
                            line = piece;
                        } else {
                            pos = wordStart;
                        }
                        break;
                    }
                    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
                    if (pos >= text.size()) break;
                }
                if (!line.empty()) lines.push_back(line);
            }
            return lines;
        }

        static void fbDrawStartMenuIcon(uint32_t* pixels, int pitch, int bufW, int bufH, int rowX, int rowY, int rowH, const std::string& label, int& textX) {
            int iconX = rowX + 4;
            int iconY = rowY + (rowH - kStartMenuIconSize) / 2;
            textX = iconX + kStartMenuIconSize + 6;

            if (kEnableStartMenuIcons) {
                ImagePtr icon;
                try {
                    icon = IconThemeManager::Instance().LoadIcon(startMenuLogicalIconName(label), kStartMenuIconSize);
                }
                catch (...) {
                    icon.reset();
                }

                if (icon && icon->isValid()) {
                    ImageRenderer::DrawImage(pixels, bufW, bufH, pitch, icon, iconX, iconY);
                    return;
                }
            }

            fbFillRect(pixels, pitch, bufW, bufH, iconX, iconY, kStartMenuIconSize, kStartMenuIconSize, startMenuFallbackIconColor32(label));
            fbDrawRect(pixels, pitch, bufW, bufH, iconX, iconY, kStartMenuIconSize, kStartMenuIconSize, 0x00FFFFFF);
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
            const int titleBarH = theme.titleBarHeight;
            
            drawBackgroundGradientToPixels(pixels, fbW, fbH - taskbarH, pitch, g_gradientTopColor, g_gradientBottomColor);
            if (g_wallpaperImage && g_wallpaperImage->isValid()) {
                drawBackgroundImageToPixels(pixels, fbW, fbH - taskbarH, pitch, g_wallpaperImage, WallpaperRegistry::ParseScaleMode(g_backgroundScaleMode));
            } else {

                // Draw branding text
                const char* brand = "guideXOS Server - UEFI Mode";
                BitmapFont::DrawStringToBufferScaled(pixels, pitch, fbW, fbH,
                    fbW / 2 - BitmapFont::MeasureWidth(brand) * 2 / 2,
                    fbH / 2 - 50, brand, -1, 0x00404040, 2);
                BitmapFont::DrawStringToBufferScaled(pixels, pitch, fbW, fbH,
                    fbW / 2 - BitmapFont::MeasureWidth(brand) * 2 / 2 - 1,
                    fbH / 2 - 51, brand, -1, 0x00808090, 2);
            }
            
            // Draw desktop icons
            const int iconW = 56;
            const int iconH = 56;
            const int cellW = iconW + 28;
            const int cellH = iconH + 38;
            int iconIdx = 0;
            for (const auto& item : g_items) {
                int ix = item.ix >= 0 ? item.ix : 20 + (iconIdx % 8) * cellW;
                int iy = item.iy >= 0 ? item.iy : 20 + (iconIdx / 8) * cellH;
                
                // Icon background follows the hosted Start Menu fallback,
                // which now consults shared built-in identity metadata first.
                uint32_t iconColor = startMenuFallbackIconColor32(item.label);
                
                int iconX = ix + (cellW - iconW) / 2;
                int iconY = iy + 6;
                fbFillRect(pixels, pitch, fbW, fbH, iconX, iconY, iconW, iconH, iconColor);
                fbDrawRect(pixels, pitch, fbW, fbH, iconX, iconY, iconW, iconH, 0x00B4B4C8);
                
                // Icon label
                const char* label = item.label.c_str();
                std::vector<std::string> labelLines = fbWrapTextToWidth(label, cellW - 8, FontRole::Small, 3);
                int lineY = iconY + iconH + 8;
                int lineH = SystemFont::MeasureHeight(FontRole::Small);
                for (const std::string& line : labelLines) {
                    int labelW = fbMeasureText(line.c_str(), static_cast<int>(line.size()), FontRole::Small);
                    fbDrawText(pixels, pitch, fbW, fbH,
                        ix + (cellW - labelW) / 2, lineY, line, 0x00E6E6F0, FontRole::Small);
                    lineY += lineH;
                }
                
                if (item.pinned) {
                    fbDrawText(pixels, pitch, fbW, fbH,
                        iconX + iconW - 6, iconY + 2, "*", 1, 0x00FFC83C, FontRole::SmallBold);
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
                    fbDrawText(pixels, pitch, fbW, fbH,
                        w.x + theme.titleTextInset, w.y + (titleBarH - SystemFont::MeasureHeight(FontRole::Title)) / 2, w.title, 0x00F0F0F0, FontRole::Title);
                    
                    // Close button (X)
                    int btnSize = std::max(12, titleBarH - theme.controlPadding * 2);
                    int closeX = w.x + w.w - theme.controlPadding - btnSize;
                    int closeY = w.y + theme.controlPadding;
                    fbFillRect(pixels, pitch, fbW, fbH, closeX, closeY, btnSize, btnSize, 0x00C83232);
                    BitmapFont::DrawStringToBuffer(pixels, pitch, fbW, fbH,
                        closeX + (btnSize - 5) / 2, closeY + (btnSize - 7) / 2, "X", 1, 0x00FFFFFF);
                    
                    // Window border
                    uint32_t borderColor = isFocused ? 0x006496C8 : 0x00606068;
                    fbDrawRectThick(pixels, pitch, fbW, fbH, w.x, w.y, w.w, w.h, theme.windowBorderThickness, borderColor);
                    
                    // Draw window content (images, widgets, text)
                    int contentX = w.x + theme.windowPadding;
                    int contentY = w.y + titleBarH + theme.windowPadding;
                    for (const auto& ri : w.rects) {
                        fbFillRect(pixels, pitch, fbW, fbH, contentX + ri.x, contentY + ri.y, ri.w, ri.h,
                                   (static_cast<uint32_t>(ri.r) << 16) | (static_cast<uint32_t>(ri.g) << 8) | ri.b);
                    }
                    for (const auto& img : w.images) {
                        const ImageBitmap& shown = displayedImage(img);
                        if (img.w > 0 && img.h > 0) ImageAdapter::DrawToPixels(pixels, fbW, fbH, pitch, shown, contentX + img.x, contentY + img.y, img.w, img.h);
                        else ImageAdapter::DrawToPixels(pixels, fbW, fbH, pitch, shown, contentX + img.x, contentY + img.y);
                    }
                    for (const auto& wd : w.widgets) {
                        int wx = contentX + wd.x;
                        int wy = contentY + wd.y;
                        uint32_t wColor = calculatorWidgetFillColor(w, wd, theme);
                        fbFillRect(pixels, pitch, fbW, fbH, wx, wy, wd.w, wd.h, wColor);
                        fbDrawRect(pixels, pitch, fbW, fbH, wx, wy, wd.w, wd.h, calculatorWidgetBorderColor(w, wd, theme));
                        const int textX = wx + (wd.icon.status == ImageLoadStatus::Ok ? 24 : 6);
                        if (wd.icon.status == ImageLoadStatus::Ok) ImageAdapter::DrawToPixels(pixels, fbW, fbH, pitch, wd.icon, wx + 4, wy + (wd.h - 16) / 2, 16, 16);
                        const uint32_t textColor = calculatorWidgetTextColor(w, wd, theme);
                        fbDrawText(pixels, pitch, fbW, fbH,
                            textX, wy + (wd.h - SystemFont::MeasureHeight(FontRole::Default)) / 2, wd.text, textColor, FontRole::Default);
                    }
                    for (const auto& tx : w.positionedTexts) {
                        fbDrawText(pixels, pitch, fbW, fbH,
                            contentX + tx.x, contentY + tx.y, tx.text,
                            tx.hasColor ? ((static_cast<uint32_t>(tx.r) << 16) | (static_cast<uint32_t>(tx.g) << 8) | tx.b) : 0x00DCDCDC,
                            FontRole::Default);
                    }
                    
                    // Draw text lines
                    int ty = contentY;
                    for (const auto& tx : w.texts) {
                        fbDrawText(pixels, pitch, fbW, fbH,
                            contentX, ty, tx, 0x00DCDCDC, FontRole::Default);
                        ty += SystemFont::MeasureHeight(FontRole::Default);
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
            fbFillRect(pixels, pitch, fbW, fbH, theme.taskbarPadding, fbH - taskbarH + 6, 32, taskbarH - 12, 0x00374B64);
            fbDrawRect(pixels, pitch, fbW, fbH, theme.taskbarPadding, fbH - taskbarH + 6, 32, taskbarH - 12, 0x00FFFFFF);
            fbDrawText(pixels, pitch, fbW, fbH, theme.taskbarPadding + 4, fbH - taskbarH + 12, "S", 1, 0x00FFFFFF, FontRole::SmallBold);

            if (g_startMenuVisible) {
                const int smW = 440;
                const int maxRows = 14;
                const int rowH = kStartMenuRowH;
                const int leftColW = 260;
                const int smH = maxRows * rowH + 10;
                int smLeft = 8;
                int smTop = fbH - taskbarH + 6 - smH - 6;
                if (smTop < 0) smTop = 4;
                int smRight = smLeft + smW;
                int smBottom = smTop + smH;

                fbFillRect(pixels, pitch, fbW, fbH, smLeft, smTop, smW, smH, 0x002D2D37);
                fbDrawRect(pixels, pitch, fbW, fbH, smLeft, smTop, smW, smH, 0x00FFFFFF);

                int y = smTop + 4;
                int row = 0;
                int startIndex = g_startMenuScroll;
                if (g_startMenuAllProgs) {
                    for (size_t i = startIndex; i < g_startMenuAllProgsSorted.size() && row < maxRows; ++i) {
                        int rowX = smLeft + 4;
                        int rowW = leftColW - 8;
                        uint32_t rowColor = ((int)i == g_startMenuSel) ? 0x00506496 : 0x00373746;
                        fbFillRect(pixels, pitch, fbW, fbH, rowX, y, rowW, rowH, rowColor);
                        std::string txt = g_startMenuAllProgsSorted[i];
                        int textX = rowX + 4;
                        fbDrawStartMenuIcon(pixels, pitch, fbW, fbH, rowX, y, rowH, txt, textX);
                        fbDrawText(pixels, pitch, fbW, fbH, textX, y + (rowH - SystemFont::MeasureHeight(FontRole::Default)) / 2, txt, 0x00E6E6E6, FontRole::Default);
                        y += rowH;
                        row++;
                    }
                } else {
                    for (size_t i = startIndex; i < g_startMenuPinnedRecent.size() && row < maxRows; ++i) {
                        int rowX = smLeft + 4;
                        int rowW = leftColW - 8;
                        uint32_t rowColor = ((int)i == g_startMenuSel) ? 0x00506496 : 0x00373746;
                        fbFillRect(pixels, pitch, fbW, fbH, rowX, y, rowW, rowH, rowColor);
                        std::string txt = g_startMenuPinnedRecent[i];
                        int textX = rowX + 4;
                        fbDrawStartMenuIcon(pixels, pitch, fbW, fbH, rowX, y, rowH, txt, textX);
                        std::string displayText = (hasEquivalentListItem(g_cfg.pinned, txt) ? "* " : "  ") + txt;
                        fbDrawText(pixels, pitch, fbW, fbH, textX, y + (rowH - SystemFont::MeasureHeight(FontRole::Default)) / 2, displayText, 0x00E6E6E6, FontRole::Default);
                        y += rowH;
                        row++;
                    }
                }

                int rcX = smLeft + leftColW + 4;
                int rcY = smTop + 6;
                int computerTextX = rcX + 4;
                fbDrawStartMenuIcon(pixels, pitch, fbW, fbH, rcX, rcY, rowH, "Computer Files", computerTextX);
                fbDrawText(pixels, pitch, fbW, fbH, computerTextX, rcY + (rowH - SystemFont::MeasureHeight(FontRole::Default)) / 2, "Computer Files", -1, 0x00C8C8C8, FontRole::Default);
                rcY += rowH + kStartMenuRowGap;

                int consoleTextX = rcX + 4;
                fbDrawStartMenuIcon(pixels, pitch, fbW, fbH, rcX, rcY, rowH, "Console", consoleTextX);
                fbDrawText(pixels, pitch, fbW, fbH, consoleTextX, rcY + (rowH - SystemFont::MeasureHeight(FontRole::Default)) / 2, "Console", -1, 0x00C8C8C8, FontRole::Default);
                rcY += rowH + kStartMenuRowGap;

                int docsTextX = rcX + 4;
                fbDrawStartMenuIcon(pixels, pitch, fbW, fbH, rcX, rcY, rowH, "Recent Docs", docsTextX);
                fbDrawText(pixels, pitch, fbW, fbH, docsTextX, rcY + (rowH - SystemFont::MeasureHeight(FontRole::Default)) / 2, "Recent Docs", -1, 0x00C8C8C8, FontRole::Default);

                int btnY = smBottom - 30;
                fbFillRect(pixels, pitch, fbW, fbH, smLeft + 6, btnY, leftColW - 12, 24, 0x003C3C4B);
                fbDrawRect(pixels, pitch, fbW, fbH, smLeft + 6, btnY, leftColW - 12, 24, 0x00FFFFFF);
                const char* btnText = g_startMenuAllProgs ? "< Back" : "All Programs >";
                fbDrawText(pixels, pitch, fbW, fbH, smLeft + 14, btnY + (24 - SystemFont::MeasureHeight(FontRole::Default)) / 2, btnText, -1, 0x00E6E6E6, FontRole::Default);

                int shutdownBtnW = 80;
                fbFillRect(pixels, pitch, fbW, fbH, smRight - shutdownBtnW - 30, btnY, shutdownBtnW, 24, 0x003C3C4B);
                fbDrawRect(pixels, pitch, fbW, fbH, smRight - shutdownBtnW - 30, btnY, shutdownBtnW, 24, 0x00FFFFFF);
                fbDrawText(pixels, pitch, fbW, fbH, smRight - shutdownBtnW - 20, btnY + (24 - SystemFont::MeasureHeight(FontRole::Default)) / 2, "Shutdown", -1, 0x00E6E6E6, FontRole::Default);
            }
            
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
                    int bw = fbMeasureText(w.title.c_str(), labelLen, FontRole::Small) + theme.taskbarItemPadding * 2 + 12;
                    if (bw > 150) bw = 150;
                    
                    uint32_t btnColor = (wid == g_focus) ? 0x00466496 : 
                                        (w.minimized ? 0x00282832 : 
                                        (w.tombstoned ? 0x00554123 : 0x00373A46));
                    fbFillRect(pixels, pitch, fbW, fbH, btnX, fbH - taskbarH + 6, bw, taskbarH - 12, btnColor);
                    
                    // Focus indicator
                    if (wid == g_focus) {
                        fbFillRect(pixels, pitch, fbW, fbH, btnX + 2, fbH - 9, bw - 4, 2, 0x0064A0F0);
                    }
                    
                    fbDrawText(pixels, pitch, fbW, fbH,
                        btnX + theme.taskbarItemPadding + 12, fbH - taskbarH + 12, w.title.c_str(), labelLen, 0x00E6E6F0, FontRole::Small);
                    
                    btnX += bw + theme.taskbarItemPadding / 2;
                }
            }
            
            // Clock
            std::time_t now = std::time(nullptr);
            std::tm ltBuf{};
            std::tm* tmp = std::localtime(&now);
            if (tmp) ltBuf = *tmp;
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ltBuf.tm_hour, ltBuf.tm_min);
            int clockX = fbW - (theme.taskbarPadding + 52);
            fbDrawText(pixels, pitch, fbW, fbH,
                clockX, fbH - taskbarH + 8, timeBuf, -1, 0x00C8C8D2, FontRole::Small);
            char dateBuf[16];
            std::snprintf(dateBuf, sizeof(dateBuf), "%d/%d", ltBuf.tm_mon + 1, ltBuf.tm_mday);
            fbDrawText(pixels, pitch, fbW, fbH,
                clockX, fbH - taskbarH + 22, dateBuf, -1, 0x009696A5, FontRole::Small);
            
            // Present to hardware framebuffer
            g_videoBackend->present();
            g_needsRedraw = false;
        }
#endif
    }
} // namespace gxos::gui
