#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>

namespace gxos { namespace apps {

    /// ImageViewer — ported from guideXOS.Legacy ImageViewer.cs
    /// Features: open image from VFS, zoom (+/-), pan (drag), reset (right-click)
    class ImageViewer {
    public:
        /// Launch with optional file path
        static uint64_t Launch(const std::string& filePath = "");

    private:
        static int main(int argc, char** argv);

        // Zoom / Pan
        static void zoomIn();
        static void zoomOut();
        static void resetZoom();
        static void updateDisplayImage();

        // UI
        static void updateDisplay();
        static void handleKeyPress(int keyCode);

        // State
        static uint64_t s_windowId;
        static std::string s_filePath;
        static int s_originalW;
        static int s_originalH;
        static float s_zoomLevel;
        static int s_panX;
        static int s_panY;
        static int s_lastKeyCode;
        static bool s_keyDown;

        static constexpr int kWinW = 600;
        static constexpr int kWinH = 450;
        static constexpr float kMaxZoom = 5.0f;
        static constexpr float kMinZoom = 0.1f;
    };

}} // namespace gxos::apps
