#include "image_viewer.h"

#include "open_dialog.h"
#include "gui_protocol.h"
#include "kernel/core/include/kernel/image_adapter.h"
#include "logger.h"
#include "vfs.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <thread>

namespace gxos { namespace apps {

uint64_t ImageViewer::s_windowId = 0;
int ImageViewer::s_windowW = ImageViewer::kWinW;
int ImageViewer::s_windowH = ImageViewer::kWinH;
std::string ImageViewer::s_filePath;
std::string ImageViewer::s_currentDirectory;
std::string ImageViewer::s_fileName;
std::string ImageViewer::s_windowTitle = "Image Viewer";
std::string ImageViewer::s_statusText = "No image loaded";
std::string ImageViewer::s_errorText;
std::string ImageViewer::s_noticeText;
gui::ImagePtr ImageViewer::s_image;
int ImageViewer::s_originalW = 0;
int ImageViewer::s_originalH = 0;
float ImageViewer::s_zoomLevel = 1.0f;
ImageViewer::ZoomMode ImageViewer::s_zoomMode = ImageViewer::ZoomMode::FitToWindow;
int ImageViewer::s_panX = 0;
int ImageViewer::s_panY = 0;
bool ImageViewer::s_hasTransparency = false;
ImageViewer::BackgroundMode ImageViewer::s_backgroundMode = ImageViewer::BackgroundMode::Solid;
std::vector<std::string> ImageViewer::s_folderImages;
int ImageViewer::s_currentImageIndex = -1;
bool ImageViewer::s_leftMouseDown = false;
bool ImageViewer::s_dragPending = false;
bool ImageViewer::s_dragging = false;
int ImageViewer::s_dragStartX = 0;
int ImageViewer::s_dragStartY = 0;
int ImageViewer::s_dragStartPanX = 0;
int ImageViewer::s_dragStartPanY = 0;
int ImageViewer::s_lastKeyCode = 0;
bool ImageViewer::s_keyDown = false;

namespace {

static void publishMessage(gui::MsgType type, const std::string& payload) {
    ipc::Message m;
    m.type = static_cast<uint32_t>(type);
    m.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(m), false);
}

static void publishWindowText(uint64_t windowId, int x, int y, const std::string& text, bool colored = false) {
    if (windowId == 0) return;
    if (colored) {
        publishMessage(gui::MsgType::MT_DrawTextAtColor, gui::packDrawTextAtColor(windowId, x, y, 235, 235, 235, text));
    } else {
        publishMessage(gui::MsgType::MT_DrawTextAt, gui::packDrawTextAt(windowId, x, y, text));
    }
}

static void publishWindowTextColor(uint64_t windowId, int x, int y, uint8_t r, uint8_t g, uint8_t b, const std::string& text) {
    if (windowId == 0) return;
    publishMessage(gui::MsgType::MT_DrawTextAtColor, gui::packDrawTextAtColor(windowId, x, y, r, g, b, text));
}

static void publishWindowRect(uint64_t windowId, int x, int y, int w, int h, int r, int g, int b) {
    if (windowId == 0) return;
    std::ostringstream oss;
    oss << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
    publishMessage(gui::MsgType::MT_DrawRect, oss.str());
}

static int clampInt(int value, int minimum, int maximum) {
    if (maximum < minimum) return minimum;
    return std::max(minimum, std::min(value, maximum));
}

static float clampFloat(float value, float minimum, float maximum) {
    if (maximum < minimum) return minimum;
    return std::max(minimum, std::min(value, maximum));
}

static const char* zoomModeName(ImageViewer::ZoomMode mode) {
    switch (mode) {
    case ImageViewer::ZoomMode::FitToWindow: return "Fit";
    case ImageViewer::ZoomMode::ActualSize: return "Actual";
    case ImageViewer::ZoomMode::Custom: return "Custom";
    default: return "Custom";
    }
}

static std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::string joinStatusText(const std::string& base, const std::string& extra) {
    if (base.empty()) return extra;
    if (extra.empty()) return base;
    return base + " | " + extra;
}

} // namespace

uint64_t ImageViewer::Launch(const std::string& filePath) {
    s_filePath = filePath;
    s_currentDirectory = normalizeFolderPath(filePath);
    s_fileName = displayNameForPath(filePath);
    s_windowId = 0;
    s_windowW = kWinW;
    s_windowH = kWinH;
    s_zoomMode = ZoomMode::FitToWindow;
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    s_hasTransparency = false;
    s_backgroundMode = BackgroundMode::Solid;
    s_folderImages.clear();
    s_currentImageIndex = -1;
    s_leftMouseDown = false;
    s_dragPending = false;
    s_dragging = false;
    s_image.reset();
    s_originalW = 0;
    s_originalH = 0;
    s_errorText.clear();
    s_noticeText.clear();
    s_statusText = s_fileName.empty() ? "No image loaded" : "Loading " + s_fileName + "...";
    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;

    ProcessSpec spec{"ImageViewer", &ImageViewer::main};
    spec.appId = "gxos.builtin.imageviewer";
    std::vector<std::string> args;
    if (!filePath.empty()) args.push_back(filePath);
    return ProcessTable::spawn(spec, args);
}

std::string ImageViewer::displayNameForPath(const std::string& path) {
    if (path.empty()) return std::string();
    std::filesystem::path filePath(path);
    std::string name = filePath.filename().string();
    return name.empty() ? path : name;
}

std::string ImageViewer::normalizeFolderPath(const std::string& path) {
    if (path.empty()) return std::string();
    std::filesystem::path p(path);
    std::filesystem::path folder = p.has_filename() ? p.parent_path() : p;
    if (folder.empty()) return std::string();
    return folder.lexically_normal().generic_string();
}

std::string ImageViewer::normalizeCaseForSort(const std::string& value) {
    return lowerCopy(std::filesystem::path(value).lexically_normal().generic_string());
}

bool ImageViewer::safeEqualsPath(const std::string& a, const std::string& b) {
    return normalizeCaseForSort(a) == normalizeCaseForSort(b);
}

bool ImageViewer::isPngPath(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    return lowerCopy(ext) == ".png";
}

float ImageViewer::fitScaleForClientArea(int clientWidth, int clientHeight) {
    if (!s_image || s_originalW <= 0 || s_originalH <= 0) return 1.0f;
    if (clientWidth <= 0 || clientHeight <= 0) return 1.0f;

    const float scaleX = static_cast<float>(clientWidth) / static_cast<float>(s_originalW);
    const float scaleY = static_cast<float>(clientHeight) / static_cast<float>(s_originalH);
    return std::min(scaleX, scaleY);
}

float ImageViewer::effectiveScaleForCurrentMode() {
    if (!s_image) return 1.0f;

    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    contentMetrics(contentLeft, contentTop, contentWidth, contentHeight);

    const float fitScale = fitScaleForClientArea(contentWidth, contentHeight);
    if (s_zoomMode == ZoomMode::FitToWindow) {
        return fitScale * s_zoomLevel;
    }
    return s_zoomLevel;
}

void ImageViewer::clampZoomForCurrentMode() {
    if (!s_image) return;

    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    contentMetrics(contentLeft, contentTop, contentWidth, contentHeight);

    const float fitScale = fitScaleForClientArea(contentWidth, contentHeight);
    const float minimumScale = kMinZoom;
    const float maximumScale = kMaxZoom;

    if (s_zoomMode == ZoomMode::FitToWindow) {
        const float baseScale = std::max(0.001f, fitScale);
        const float effectiveScale = clampFloat(baseScale * s_zoomLevel, minimumScale, maximumScale);
        s_zoomLevel = effectiveScale / baseScale;
    } else {
        s_zoomLevel = clampFloat(s_zoomLevel, minimumScale, maximumScale);
    }
}

void ImageViewer::clampPanForCurrentImage() {
    if (!s_image) return;

    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    int drawX = 0;
    int drawY = 0;
    int drawW = 1;
    int drawH = 1;
    imageMetrics(drawX, drawY, drawW, drawH, contentLeft, contentTop, contentWidth, contentHeight);
    (void)drawX;
    (void)drawY;

    if (drawW <= contentWidth) {
        s_panX = 0;
    } else {
        s_panX = clampInt(s_panX, contentWidth - drawW, 0);
    }

    if (drawH <= contentHeight) {
        s_panY = 0;
    } else {
        s_panY = clampInt(s_panY, contentHeight - drawH, 0);
    }
}

void ImageViewer::contentMetrics(int& contentLeft, int& contentTop, int& contentWidth, int& contentHeight) {
    const int titleBarH = 32;
    const int statusBarH = 28;
    const int buttonBarH = 44;
    const int margin = 12;

    contentTop = titleBarH + margin;
    const int contentBottom = std::max(contentTop + 1, s_windowH - statusBarH - buttonBarH - margin);
    contentLeft = margin;
    const int contentRight = std::max(contentLeft + 1, s_windowW - margin);
    contentWidth = std::max(1, contentRight - contentLeft);
    contentHeight = std::max(1, contentBottom - contentTop);
}

void ImageViewer::imageMetrics(int& drawX, int& drawY, int& drawW, int& drawH, int& contentLeft, int& contentTop, int& contentWidth, int& contentHeight) {
    contentMetrics(contentLeft, contentTop, contentWidth, contentHeight);

    if (!s_image || s_originalW <= 0 || s_originalH <= 0) {
        drawX = contentLeft;
        drawY = contentTop;
        drawW = 1;
        drawH = 1;
        return;
    }

    const float fitScale = fitScaleForClientArea(contentWidth, contentHeight);
    const float effectiveScale = s_zoomMode == ZoomMode::FitToWindow
        ? fitScale * s_zoomLevel
        : s_zoomLevel;
    const float clampedScale = clampFloat(effectiveScale, kMinZoom, kMaxZoom);

    drawW = std::max(1, static_cast<int>(static_cast<float>(s_originalW) * clampedScale + 0.5f));
    drawH = std::max(1, static_cast<int>(static_cast<float>(s_originalH) * clampedScale + 0.5f));

    const int centeredX = contentLeft + (contentWidth - drawW) / 2;
    const int centeredY = contentTop + (contentHeight - drawH) / 2;

    int panX = s_panX;
    int panY = s_panY;
    if (drawW <= contentWidth) {
        panX = 0;
    } else {
        panX = clampInt(panX, contentWidth - drawW, 0);
    }
    if (drawH <= contentHeight) {
        panY = 0;
    } else {
        panY = clampInt(panY, contentHeight - drawH, 0);
    }

    drawX = centeredX + panX;
    drawY = centeredY + panY;
}

bool ImageViewer::pointInsideCurrentImage(int x, int y) {
    int drawX = 0;
    int drawY = 0;
    int drawW = 1;
    int drawH = 1;
    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    imageMetrics(drawX, drawY, drawW, drawH, contentLeft, contentTop, contentWidth, contentHeight);
    return x >= drawX && x < drawX + drawW && y >= drawY && y < drawY + drawH;
}

bool ImageViewer::detectTransparency(const gui::ImagePtr& image) {
    if (!image || !image->Pixels || image->Width <= 0 || image->Height <= 0 || image->Channels < 4) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(image->Width) * static_cast<size_t>(image->Height);
    const uint8_t* pixels = image->Pixels;
    for (size_t idx = 0; idx < pixelCount; ++idx) {
        if (pixels[idx * 4 + 3] < 255) {
            return true;
        }
    }
    return false;
}

void ImageViewer::drawCheckerboardBackground(int x, int y, int w, int h) {
    if (s_windowId == 0 || w <= 0 || h <= 0) return;

    const int tileSize = 16;
    for (int row = 0; row < h; row += tileSize) {
        for (int col = 0; col < w; col += tileSize) {
            const int tileW = std::min(tileSize, w - col);
            const int tileH = std::min(tileSize, h - row);
            const bool darkTile = (((row / tileSize) + (col / tileSize)) % 2) == 0;
            publishWindowRect(s_windowId, x + col, y + row, tileW, tileH,
                darkTile ? 74 : 104,
                darkTile ? 74 : 104,
                darkTile ? 78 : 108);
        }
    }
}

std::string ImageViewer::currentImagePositionText() {
    if (s_folderImages.size() <= 1 || s_currentImageIndex < 0 || s_currentImageIndex >= static_cast<int>(s_folderImages.size())) {
        return std::string();
    }

    std::ostringstream oss;
    oss << (s_currentImageIndex + 1) << " of " << s_folderImages.size();
    return oss.str();
}

std::string ImageViewer::modeText() {
    switch (s_zoomMode) {
    case ZoomMode::FitToWindow: return "Fit";
    case ZoomMode::ActualSize: return "Actual";
    case ZoomMode::Custom: return "Custom";
    default: return "Custom";
    }
}

std::string ImageViewer::statusText() {
    if (!s_image) {
        if (!s_errorText.empty()) return joinStatusText(s_errorText, s_noticeText);
        if (!s_noticeText.empty()) return s_noticeText;
        return s_statusText.empty() ? "No image loaded" : s_statusText;
    }

    const float effectiveScale = effectiveScaleForCurrentMode();
    const int zoomPct = static_cast<int>((effectiveScale * 100.0f) + 0.5f);
    std::ostringstream oss;
    oss << (s_fileName.empty() ? s_filePath : s_fileName)
        << " | " << s_originalW << "x" << s_originalH
        << " | " << zoomPct << "%"
        << " | " << modeText();

    const std::string position = currentImagePositionText();
    if (!position.empty()) {
        oss << " | " << position;
    }

    if (!s_noticeText.empty()) {
        oss << " | " << s_noticeText;
    }

    if (!s_errorText.empty()) {
        oss << " | error: " << s_errorText;
    }

    return oss.str();
}

void ImageViewer::refreshWindowTitle() {
    if (s_windowId == 0) return;
    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;
    publishMessage(gui::MsgType::MT_SetTitle, std::to_string(s_windowId) + "|" + s_windowTitle);
}

void ImageViewer::setNoticeText(const std::string& text) {
    s_noticeText = text;
}

void ImageViewer::showUnsupportedFormat(const std::string& path) {
    const bool hadImage = static_cast<bool>(s_image);
    s_errorText = "Unsupported image format: only PNG is supported in this version";
    s_noticeText.clear();

    if (!hadImage) {
        s_filePath = path;
        s_currentDirectory = normalizeFolderPath(path);
        s_fileName = displayNameForPath(path);
        s_image.reset();
        s_originalW = 0;
        s_originalH = 0;
        s_zoomMode = ZoomMode::FitToWindow;
        s_zoomLevel = 1.0f;
        s_panX = 0;
        s_panY = 0;
        s_hasTransparency = false;
        s_backgroundMode = BackgroundMode::Solid;
        refreshFolderImageList(path);
        s_statusText = s_errorText;
        refreshWindowTitle();
        updateImageStatus();
        updateDisplay();
        return;
    }

    updateDisplayImage();
}

bool ImageViewer::refreshFolderImageList(const std::string& path) {
    s_folderImages.clear();
    s_currentImageIndex = -1;
    s_currentDirectory = normalizeFolderPath(path);
    if (s_currentDirectory.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path folderPath(s_currentDirectory);
    if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(folderPath, ec)) {
        if (ec) break;
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc) continue;
        const std::string candidate = entry.path().string();
        if (!isPngPath(candidate)) continue;
        s_folderImages.push_back(candidate);
    }

    std::sort(s_folderImages.begin(), s_folderImages.end(), [](const std::string& a, const std::string& b) {
        const std::string left = normalizeCaseForSort(a);
        const std::string right = normalizeCaseForSort(b);
        if (left == right) return a < b;
        return left < right;
    });

    for (size_t i = 0; i < s_folderImages.size(); ++i) {
        if (safeEqualsPath(s_folderImages[i], path)) {
            s_currentImageIndex = static_cast<int>(i);
            break;
        }
    }

    return !s_folderImages.empty();
}

void ImageViewer::updateImageStatus() {
    if (!s_image) {
        if (!s_errorText.empty()) {
            s_statusText = joinStatusText(s_errorText, s_noticeText);
        } else if (!s_noticeText.empty()) {
            s_statusText = s_noticeText;
        } else if (s_statusText.empty()) {
            s_statusText = "No image loaded";
        }
    }
}

bool ImageViewer::loadImagePath(const std::string& path, bool refreshFolderList, bool preserveZoomMode) {
    const bool hadImage = static_cast<bool>(s_image);
    const ZoomMode previousZoomMode = s_zoomMode;
    const float previousZoomLevel = s_zoomLevel;

    gui::ImageBitmap loaded = gui::ImageAdapter::LoadFromFile(path);
    if (!loaded.image) {
        const std::string name = displayNameForPath(path);
        s_errorText = "Failed to load " + (name.empty() ? path : name) + " (" + gui::ImageLoadStatusName(loaded.status) + ")";
        s_noticeText.clear();
        if (!hadImage || !preserveZoomMode) {
            s_filePath = path;
            s_fileName = name;
            s_image.reset();
            s_originalW = 0;
            s_originalH = 0;
            s_zoomMode = ZoomMode::FitToWindow;
            s_zoomLevel = 1.0f;
            s_panX = 0;
            s_panY = 0;
            s_hasTransparency = false;
            s_backgroundMode = BackgroundMode::Solid;
            if (refreshFolderList) {
                refreshFolderImageList(path);
            }
            s_statusText = s_errorText;
            refreshWindowTitle();
            updateImageStatus();
            updateDisplay();
        } else {
            s_statusText = s_errorText;
            updateDisplayImage();
        }
        Logger::write(LogLevel::Warn, "ImageViewer: image load failed: " + path +
            " status=" + gui::ImageLoadStatusName(loaded.status));
        return false;
    }

    s_filePath = path;
    s_fileName = displayNameForPath(path);
    s_image = loaded.image;
    s_originalW = s_image->Width;
    s_originalH = s_image->Height;
    s_hasTransparency = detectTransparency(s_image);
    s_backgroundMode = s_hasTransparency ? BackgroundMode::Checkerboard : BackgroundMode::Solid;
    s_errorText.clear();
    s_noticeText.clear();

    if (refreshFolderList) {
        refreshFolderImageList(path);
    }
    if (s_currentImageIndex < 0 && !s_folderImages.empty()) {
        for (size_t i = 0; i < s_folderImages.size(); ++i) {
            if (safeEqualsPath(s_folderImages[i], path)) {
                s_currentImageIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (preserveZoomMode) {
        s_zoomMode = previousZoomMode;
        s_zoomLevel = previousZoomLevel;
    } else {
        s_zoomMode = ZoomMode::FitToWindow;
        s_zoomLevel = 1.0f;
    }
    s_panX = 0;
    s_panY = 0;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateImageStatus();
    refreshWindowTitle();
    updateDisplay();

    Logger::write(LogLevel::Info, "ImageViewer loaded PNG: " + s_filePath +
        " (" + std::to_string(s_originalW) + "x" + std::to_string(s_originalH) + ")" +
        " transparency=" + (s_hasTransparency ? "true" : "false") +
        " folderImages=" + std::to_string(s_folderImages.size()));
    return true;
}

void ImageViewer::handleWindowResize(int width, int height) {
    if (width > 0) s_windowW = width;
    if (height > 0) s_windowH = height;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplay();
}

void ImageViewer::fitToWindow() {
    if (!s_image) return;
    s_zoomMode = ZoomMode::FitToWindow;
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplayImage();
}

bool ImageViewer::navigateRelative(int delta) {
    if (s_folderImages.size() <= 1) return false;
    if (s_currentImageIndex < 0) return false;

    const int nextIndex = s_currentImageIndex + delta;
    if (nextIndex < 0 || nextIndex >= static_cast<int>(s_folderImages.size())) {
        return false;
    }

    const std::string nextPath = s_folderImages[static_cast<size_t>(nextIndex)];
    return loadImagePath(nextPath, true, true);
}

void ImageViewer::previousImage() {
    (void)navigateRelative(-1);
}

void ImageViewer::nextImage() {
    (void)navigateRelative(1);
}

void ImageViewer::openImageFromDialog() {
    const std::string startPath = s_currentDirectory.empty() ? std::string("/") : s_currentDirectory;
    dialogs::OpenDialog::Show(0, 0, startPath, [](const std::string& path) {
        if (path.empty()) {
            return;
        }
        if (!isPngPath(path)) {
            showUnsupportedFormat(path);
            return;
        }
        (void)loadImagePath(path, true, false);
    });
}

bool ImageViewer::trySetCurrentImageAsWallpaper() {
    if (!s_image || s_filePath.empty()) {
        setNoticeText("Load a PNG first");
        updateDisplayImage();
        return false;
    }

    if (!isPngPath(s_filePath)) {
        showUnsupportedFormat(s_filePath);
        return false;
    }

    ipc::Message msg;
    msg.type = static_cast<uint32_t>(gui::MsgType::MT_DesktopWallpaperSet);
    msg.data.assign(s_filePath.begin(), s_filePath.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    setNoticeText("Wallpaper update requested");
    updateDisplayImage();
    return true;
}

int ImageViewer::main(int argc, char** argv) {
    Logger::write(LogLevel::Info, "ImageViewer starting");

    if (argc > 1 && argv[1]) {
        s_filePath = argv[1];
        s_currentDirectory = normalizeFolderPath(s_filePath);
        s_fileName = displayNameForPath(s_filePath);
    }

    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;
    s_statusText = s_fileName.empty() ? "No image loaded" : "Loading " + s_fileName + "...";
    s_errorText.clear();
    s_noticeText.clear();

    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = s_windowTitle + "|" + std::to_string(kWinW) + "|" + std::to_string(kWinH);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        ipc::Message m;
        if (ipc::Bus::pop("gui.output", m, 200)) {
            std::string s(m.data.begin(), m.data.end());
            try {
                s_windowId = std::stoull(s);
            } catch (...) {
                s_windowId = 0;
            }
        }
    }

    if (!s_filePath.empty()) {
        if (!isPngPath(s_filePath)) {
            showUnsupportedFormat(s_filePath);
        } else {
            loadImagePath(s_filePath, true, false);
        }
    } else {
        updateImageStatus();
        updateDisplay();
    }

    bool running = true;
    while (running) {
        ipc::Message ev;
        if (ipc::Bus::pop("gui.output", ev, 150)) {
            if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_Close)) {
                running = false;
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_Resize)) {
                std::string payload(ev.data.begin(), ev.data.end());
                std::istringstream iss(payload);
                std::string winIdStr, widthStr, heightStr;
                std::getline(iss, winIdStr, '|');
                std::getline(iss, widthStr, '|');
                std::getline(iss, heightStr, '|');
                try {
                    if (std::stoull(winIdStr) == s_windowId) {
                        handleWindowResize(std::stoi(widthStr), std::stoi(heightStr));
                    }
                } catch (...) {
                }
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_InputKey)) {
                std::string payload(ev.data.begin(), ev.data.end());
                std::istringstream iss(payload);
                std::string keyStr;
                std::string action;
                std::getline(iss, keyStr, '|');
                std::getline(iss, action);

                int keyCode = 0;
                try { keyCode = std::stoi(keyStr); } catch (...) {}
                if (action.empty() || action == "down") {
                    handleKeyPress(keyCode);
                }
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_InputMouse)) {
                std::string payload(ev.data.begin(), ev.data.end());
                std::istringstream iss(payload);
                std::string xStr, yStr, buttonStr, action;
                std::getline(iss, xStr, '|');
                std::getline(iss, yStr, '|');
                std::getline(iss, buttonStr, '|');
                std::getline(iss, action);
                try {
                    handleMouseInput(std::stoi(xStr), std::stoi(yStr), std::stoi(buttonStr), action);
                } catch (...) {
                }
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_WidgetEvt)) {
                std::string payload(ev.data.begin(), ev.data.end());
                std::istringstream iss(payload);
                std::string winIdStr, widgetIdStr, event;
                std::getline(iss, winIdStr, '|');
                std::getline(iss, widgetIdStr, '|');
                std::getline(iss, event, '|');
                if (!winIdStr.empty() && !widgetIdStr.empty()) {
                    try {
                        uint64_t winId = std::stoull(winIdStr);
                        int widgetId = std::stoi(widgetIdStr);
                        if (winId == s_windowId && event == "click") {
                            switch (widgetId) {
                            case 1: openImageFromDialog(); break;
                            case 2: previousImage(); break;
                            case 3: nextImage(); break;
                            case 4: zoomIn(); break;
                            case 5: zoomOut(); break;
                            case 6: fitToWindow(); break;
                            case 7: resetZoom(); break;
                            case 8: (void)trySetCurrentImageAsWallpaper(); break;
                            default: break;
                            }
                        }
                    } catch (...) {
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    if (s_windowId != 0) {
        publishMessage(gui::MsgType::MT_Close, std::to_string(s_windowId));
    }

    Logger::write(LogLevel::Info, "ImageViewer exiting");
    return 0;
}

void ImageViewer::zoomIn() {
    if (!s_image) return;
    if (s_zoomMode == ZoomMode::FitToWindow) {
        s_zoomLevel = effectiveScaleForCurrentMode();
        s_zoomMode = ZoomMode::Custom;
    } else if (s_zoomMode == ZoomMode::ActualSize) {
        s_zoomMode = ZoomMode::Custom;
        s_zoomLevel = 1.0f;
    }

    s_zoomLevel *= 1.25f;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplayImage();
}

void ImageViewer::zoomOut() {
    if (!s_image) return;
    if (s_zoomMode == ZoomMode::FitToWindow) {
        s_zoomLevel = effectiveScaleForCurrentMode();
        s_zoomMode = ZoomMode::Custom;
    } else if (s_zoomMode == ZoomMode::ActualSize) {
        s_zoomMode = ZoomMode::Custom;
        s_zoomLevel = 1.0f;
    }

    s_zoomLevel /= 1.25f;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplayImage();
}

void ImageViewer::resetZoom() {
    s_zoomMode = ZoomMode::ActualSize;
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplayImage();
}

void ImageViewer::updateDisplayImage() {
    Logger::write(LogLevel::Info, "ImageViewer mode=" + std::string(zoomModeName(s_zoomMode)) +
        " scale=" + std::to_string(static_cast<int>((effectiveScaleForCurrentMode() * 100.0f) + 0.5f)) + "%");
    updateDisplay();
}

void ImageViewer::handleMouseInput(int x, int y, int button, const std::string& action) {
    if (!s_image) return;

    if (button == 1 && action == "down") {
        s_leftMouseDown = true;
        s_dragPending = pointInsideCurrentImage(x, y);
        s_dragging = false;
        s_dragStartX = x;
        s_dragStartY = y;
        s_dragStartPanX = s_panX;
        s_dragStartPanY = s_panY;
        return;
    }

    if (button == 1 && action == "up") {
        s_leftMouseDown = false;
        s_dragPending = false;
        s_dragging = false;
        return;
    }

    if (button == 0 && action == "move") {
        if (s_leftMouseDown && s_dragPending) {
            if (std::abs(x - s_dragStartX) >= 3 || std::abs(y - s_dragStartY) >= 3) {
                s_dragging = true;
                s_dragPending = false;
            }
        }

        if (s_dragging) {
            s_panX = s_dragStartPanX + (x - s_dragStartX);
            s_panY = s_dragStartPanY + (y - s_dragStartY);
            clampZoomForCurrentMode();
            clampPanForCurrentImage();
            updateDisplayImage();
        }
    }
}

void ImageViewer::handleKeyPress(int keyCode) {
    if (keyCode == '+' || keyCode == '=') {
        zoomIn();
    } else if (keyCode == '-') {
        zoomOut();
    } else if (keyCode == '0' || keyCode == '1') {
        resetZoom();
    } else if (keyCode == 'f' || keyCode == 'F') {
        fitToWindow();
    } else if (keyCode == 37 || keyCode == 0x102) {
        previousImage();
    } else if (keyCode == 39 || keyCode == 0x103) {
        nextImage();
    }
    s_lastKeyCode = keyCode;
}

void ImageViewer::updateDisplay() {
    if (s_windowId == 0) return;

    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    contentMetrics(contentLeft, contentTop, contentWidth, contentHeight);

    int drawX = contentLeft;
    int drawY = contentTop;
    int drawW = 1;
    int drawH = 1;
    imageMetrics(drawX, drawY, drawW, drawH, contentLeft, contentTop, contentWidth, contentHeight);

    publishMessage(gui::MsgType::MT_DrawText, std::to_string(s_windowId) + "|\f");
    publishWindowRect(s_windowId, 0, 0, s_windowW, s_windowH, 30, 30, 30);
    publishWindowRect(s_windowId, 0, 31, s_windowW, 1, 45, 45, 45);

    if (s_image) {
        if (s_backgroundMode == BackgroundMode::Checkerboard) {
            drawCheckerboardBackground(contentLeft, contentTop, contentWidth, contentHeight);
        }
        publishMessage(gui::MsgType::MT_DrawImage,
            gui::packDrawImage(s_windowId, drawX, drawY, drawW, drawH, s_filePath));
        if (!s_errorText.empty()) {
            publishWindowTextColor(s_windowId, contentLeft + 8, contentTop + 8, 255, 96, 96, s_errorText);
        }
    } else {
        const std::string message = s_statusText.empty() ? "No image loaded" : s_statusText;
        const int approxTextWidth = static_cast<int>(message.size()) * 7;
        const int centeredX = contentLeft + std::max(0, (contentWidth - approxTextWidth) / 2);
        const int centeredY = contentTop + std::max(0, contentHeight / 2);
        publishWindowTextColor(s_windowId, centeredX, centeredY, 235, 235, 235, message);
    }

    const std::string info = statusText();
    publishWindowText(s_windowId, 12, s_windowH - 22, info, true);

    int btnY = s_windowH - 40;
    int btnH = 26;
    int gap = 8;
    int x = 12;

    auto addBtn = [&](int id, const std::string& label) {
        const int btnW = std::max(44, static_cast<int>(label.size()) * 7 + 18);
        publishMessage(gui::MsgType::MT_WidgetAdd, gui::packWidgetAdd(s_windowId, 1, id, x, btnY, btnW, btnH, label));
        x += btnW + gap;
    };

    addBtn(1, "Open");
    addBtn(2, "Previous");
    addBtn(3, "Next");
    addBtn(4, "Zoom In");
    addBtn(5, "Zoom Out");
    addBtn(6, "Fit to Window");
    addBtn(7, "100%");
    addBtn(8, "Set as Wallpaper");
}

}} // namespace gxos::apps
