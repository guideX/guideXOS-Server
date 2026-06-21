#pragma once
#include "process.h"
#include "ipc_bus.h"
#include "image.h"

#include <string>
#include <vector>

namespace gxos { namespace apps {

    /// Minimal hosted image viewer for PNG files.
    /// Features: open image from VFS/host path, fit-to-window rendering, zoom, pan, folder navigation,
    /// transparent backgrounds, and basic status text.
    class ImageViewer {
    public:
        struct HistorySnapshot {
            int width = 0;
            int height = 0;
            int channels = 0;
            std::vector<uint8_t> pixels;
        };

        enum class ZoomMode {
            FitToWindow = 0,
            ActualSize,
            Custom
        };

        enum class BackgroundMode {
            Solid = 0,
            Checkerboard,
            Dark,
            Light
        };

        /// Launch with optional file path
        static uint64_t Launch(const std::string& filePath = "");

    private:
        static int main(int argc, char** argv);

        // Zoom / Pan / navigation
        static void zoomIn();
        static void zoomOut();
        static void resetZoom();
        static void fitToWindow();
        static void previousImage();
        static void nextImage();
        static void openImageFromDialog();
        static bool trySetCurrentImageAsWallpaper();
        static void RotateCurrentImageLeft();
        static void RotateCurrentImageRight();
        static void FlipCurrentImageHorizontal();
        static void FlipCurrentImageVertical();
        static void UndoEdit();
        static void RedoEdit();
        static void DiscardChanges();
        static void MarkModified();
        static void UpdateModifiedTitleStatus();
        static void SaveCurrentImageAsCopy();
        static void updateDisplayImage();
        static void updateDisplay();
        static void handleMouseInput(int x, int y, int button, const std::string& action);
        static bool navigateRelative(int delta);

        // UI
        static void handleKeyPress(int keyCode);
        static void handleWindowResize(int width, int height);
        static void refreshWindowTitle();
        static std::string displayNameForPath(const std::string& path);
        static std::string statusText();
        static float fitScaleForClientArea(int clientWidth, int clientHeight);
        static float effectiveScaleForCurrentMode();
        static void clampZoomForCurrentMode();
        static void clampPanForCurrentImage();
        static bool loadImagePath(const std::string& path, bool refreshFolderList, bool preserveZoomMode);
        static void showUnsupportedFormat(const std::string& path);
        static void setNoticeText(const std::string& text);
        static bool refreshFolderImageList(const std::string& path);
        static bool detectTransparency(const gui::ImagePtr& image);
        static bool commitEditedImage(const gui::ImagePtr& image, const std::string& notice);
        static void CaptureHistoryBeforeEdit();
        static bool RestoreHistorySnapshot(const struct HistorySnapshot& snapshot);
        static void ClearEditHistory();
        static bool CanNavigateAwayFromDirtyDocument(const std::string& actionName);
        static void updateImageStatus();
        static void drawCheckerboardBackground(int x, int y, int w, int h);
        static void contentMetrics(int& contentLeft, int& contentTop, int& contentWidth, int& contentHeight);
        static void imageMetrics(int& drawX, int& drawY, int& drawW, int& drawH, int& contentLeft, int& contentTop, int& contentWidth, int& contentHeight);
        static bool pointInsideCurrentImage(int x, int y);
        static std::string currentImagePositionText();
        static std::string modeText();
        static bool isPngPath(const std::string& path);
        static bool safeEqualsPath(const std::string& a, const std::string& b);
        static std::string normalizeFolderPath(const std::string& path);
        static std::string normalizeCaseForSort(const std::string& value);

        // State
        static uint64_t s_windowId;
        static int s_windowW;
        static int s_windowH;
        static std::string s_filePath;
        static std::string s_originalPath;
        static std::string s_displayPath;
        static std::string s_currentDirectory;
        static std::string s_fileName;
        static std::string s_windowTitle;
        static std::string s_statusText;
        static std::string s_errorText;
        static std::string s_noticeText;
        static gui::ImagePtr s_image;
        static int s_originalW;
        static int s_originalH;
        static bool s_isDirty;
        static float s_zoomLevel;
        static ZoomMode s_zoomMode;
        static int s_panX;
        static int s_panY;
        static bool s_hasTransparency;
        static BackgroundMode s_backgroundMode;
        static HistorySnapshot s_originalSnapshot;
        static bool s_hasOriginalSnapshot;
        static std::vector<HistorySnapshot> s_undoStack;
        static std::vector<HistorySnapshot> s_redoStack;
        static std::vector<std::string> s_folderImages;
        static int s_currentImageIndex;
        static bool s_leftMouseDown;
        static bool s_dragPending;
        static bool s_dragging;
        static int s_dragStartX;
        static int s_dragStartY;
        static int s_dragStartPanX;
        static int s_dragStartPanY;
        static int s_lastKeyCode;
        static bool s_keyDown;

        static constexpr int kWinW = 820;
        static constexpr int kWinH = 620;
        static constexpr float kMaxZoom = 8.0f;
        static constexpr float kMinZoom = 0.1f;
        static constexpr size_t kHistoryLimit = 10;
    };

}} // namespace gxos::apps
