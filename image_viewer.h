#pragma once
#include "process.h"
#include "ipc_bus.h"
#include "image.h"
#include <string>

namespace gxos { namespace apps {

    /// Minimal hosted image viewer for PNG files.
    /// Features: open image from VFS/host path, fit-to-window rendering, zoom, and basic status text.
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
        static void handleWindowResize(int width, int height);
        static void refreshWindowTitle();
        static std::string displayNameForPath(const std::string& path);
        static std::string statusText();
        static float fitScaleForClientArea(int clientWidth, int clientHeight);

        // State
        static uint64_t s_windowId;
        static int s_windowW;
        static int s_windowH;
        static std::string s_filePath;
        static std::string s_fileName;
        static std::string s_windowTitle;
        static std::string s_statusText;
        static gui::ImagePtr s_image;
        static int s_originalW;
        static int s_originalH;
        static float s_zoomLevel;
        static int s_panX;
        static int s_panY;
        static int s_lastKeyCode;
        static bool s_keyDown;

        static constexpr int kWinW = 820;
        static constexpr int kWinH = 620;
        static constexpr float kMaxZoom = 5.0f;
        static constexpr float kMinZoom = 0.1f;
    };

}} // namespace gxos::apps
