#include "image_viewer.h"

#include "gui_protocol.h"
#include "kernel/core/include/kernel/image_adapter.h"
#include "logger.h"
#include "vfs.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>

namespace gxos { namespace apps {

uint64_t ImageViewer::s_windowId = 0;
int ImageViewer::s_windowW = ImageViewer::kWinW;
int ImageViewer::s_windowH = ImageViewer::kWinH;
std::string ImageViewer::s_filePath;
std::string ImageViewer::s_fileName;
std::string ImageViewer::s_windowTitle = "Image Viewer";
std::string ImageViewer::s_statusText = "No image loaded";
gui::ImagePtr ImageViewer::s_image;
int ImageViewer::s_originalW = 0;
int ImageViewer::s_originalH = 0;
float ImageViewer::s_zoomLevel = 1.0f;
int ImageViewer::s_panX = 0;
int ImageViewer::s_panY = 0;
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

static void publishWindowRect(uint64_t windowId, int x, int y, int w, int h, int r, int g, int b) {
    if (windowId == 0) return;
    std::ostringstream oss;
    oss << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
    publishMessage(gui::MsgType::MT_DrawRect, oss.str());
}

} // namespace

uint64_t ImageViewer::Launch(const std::string& filePath) {
    s_filePath = filePath;
    s_fileName = displayNameForPath(filePath);
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    s_image.reset();
    s_originalW = 0;
    s_originalH = 0;
    s_lastKeyCode = 0;
    s_keyDown = false;
    s_windowId = 0;
    s_windowW = kWinW;
    s_windowH = kWinH;
    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;
    s_statusText = s_fileName.empty() ? "No image loaded" : s_fileName;

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

std::string ImageViewer::statusText() {
    if (!s_filePath.empty() && !s_image) {
        return s_statusText;
    }

    if (!s_image) {
        return "No image loaded";
    }

    const int zoomPct = static_cast<int>((s_zoomLevel * 100.0f) + 0.5f);
    std::ostringstream oss;
    oss << (s_fileName.empty() ? s_filePath : s_fileName)
        << "  " << s_originalW << "x" << s_originalH
        << "  zoom " << zoomPct << "%";
    return oss.str();
}

float ImageViewer::fitScaleForClientArea(int clientWidth, int clientHeight) {
    if (!s_image || s_originalW <= 0 || s_originalH <= 0) return 1.0f;
    if (clientWidth <= 0 || clientHeight <= 0) return 1.0f;

    const float scaleX = static_cast<float>(clientWidth) / static_cast<float>(s_originalW);
    const float scaleY = static_cast<float>(clientHeight) / static_cast<float>(s_originalH);
    const float scale = std::min(scaleX, scaleY);
    return std::min(1.0f, scale);
}

void ImageViewer::refreshWindowTitle() {
    if (s_windowId == 0) return;
    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;
    publishMessage(gui::MsgType::MT_SetTitle, std::to_string(s_windowId) + "|" + s_windowTitle);
}

void ImageViewer::handleWindowResize(int width, int height) {
    if (width > 0) s_windowW = width;
    if (height > 0) s_windowH = height;
    updateDisplay();
}

int ImageViewer::main(int argc, char** argv) {
    Logger::write(LogLevel::Info, "ImageViewer starting");

    if (argc > 1 && argv[1]) {
        s_filePath = argv[1];
        s_fileName = displayNameForPath(s_filePath);
    }

    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;
    s_statusText = s_fileName.empty() ? "No image loaded" : s_fileName;

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
            try { s_windowId = std::stoull(s); } catch (...) { s_windowId = 0; }
        }
    }

    if (!s_filePath.empty()) {
        gui::ImageBitmap loaded = gui::ImageAdapter::LoadFromFile(s_filePath);
        s_image = loaded.image;
        if (s_image) {
            s_originalW = s_image->Width;
            s_originalH = s_image->Height;
            s_statusText.clear();
            Logger::write(LogLevel::Info, "ImageViewer loaded PNG: " + s_filePath +
                " (" + std::to_string(s_originalW) + "x" + std::to_string(s_originalH) + ")");
            refreshWindowTitle();
        } else {
            s_statusText = (s_fileName.empty() ? s_filePath : s_fileName) + "  load failed (" + gui::ImageLoadStatusName(loaded.status) + ")";
            Logger::write(LogLevel::Warn, "ImageViewer: image load failed: " + s_filePath +
                " status=" + gui::ImageLoadStatusName(loaded.status));
        }
    }

    updateDisplay();

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
                int keyCode = 0;
                try { keyCode = std::stoi(payload); } catch (...) {}
                handleKeyPress(keyCode);
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
                            if (widgetId == 1) { zoomIn(); }
                            else if (widgetId == 2) { zoomOut(); }
                            else if (widgetId == 3) { resetZoom(); }
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
    s_zoomLevel *= 1.25f;
    if (s_zoomLevel > kMaxZoom) s_zoomLevel = kMaxZoom;
    updateDisplayImage();
}

void ImageViewer::zoomOut() {
    s_zoomLevel /= 1.25f;
    if (s_zoomLevel < kMinZoom) s_zoomLevel = kMinZoom;
    updateDisplayImage();
}

void ImageViewer::resetZoom() {
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    updateDisplayImage();
}

void ImageViewer::updateDisplayImage() {
    Logger::write(LogLevel::Info, "ImageViewer zoom=" + std::to_string(static_cast<int>(s_zoomLevel * 100)) + "%");
    updateDisplay();
}

void ImageViewer::updateDisplay() {
    if (s_windowId == 0) return;

    const int titleBarH = 32;
    const int statusBarH = 28;
    const int buttonBarH = 44;
    const int margin = 12;
    const int contentTop = titleBarH + margin;
    const int contentBottom = std::max(contentTop + 1, s_windowH - statusBarH - buttonBarH - margin);
    const int contentLeft = margin;
    const int contentRight = std::max(contentLeft + 1, s_windowW - margin);
    const int contentWidth = std::max(1, contentRight - contentLeft);
    const int contentHeight = std::max(1, contentBottom - contentTop);

    publishMessage(gui::MsgType::MT_DrawText, std::to_string(s_windowId) + "|\f");
    publishWindowRect(s_windowId, 0, 0, s_windowW, s_windowH, 30, 30, 30);
    publishWindowRect(s_windowId, 0, titleBarH - 1, s_windowW, 1, 45, 45, 45);

    if (s_image) {
        const float fitScale = fitScaleForClientArea(contentWidth, contentHeight);
        const float displayScale = fitScale * s_zoomLevel;
        const int drawW = std::max(1, static_cast<int>(static_cast<float>(s_originalW) * displayScale + 0.5f));
        const int drawH = std::max(1, static_cast<int>(static_cast<float>(s_originalH) * displayScale + 0.5f));
        const int drawX = contentLeft + std::max(0, (contentWidth - drawW) / 2) + s_panX;
        const int drawY = contentTop + std::max(0, (contentHeight - drawH) / 2) + s_panY;
        publishMessage(gui::MsgType::MT_DrawImage,
            gui::packDrawImage(s_windowId, drawX, drawY, drawW, drawH, s_filePath));
    } else {
        const std::string message = s_statusText.empty() ? "No image loaded" : s_statusText;
        publishWindowText(s_windowId, contentLeft, contentTop + 20, message, true);
    }

    const std::string info = statusText();
    publishWindowText(s_windowId, margin, s_windowH - 22, info, true);

    int btnY = s_windowH - 40;
    int btnW = 80;
    int btnH = 26;
    int gap = 8;
    int x = 12;

    auto addBtn = [&](int id, const std::string& label) {
        publishMessage(gui::MsgType::MT_WidgetAdd, gui::packWidgetAdd(s_windowId, 1, id, x, btnY, btnW, btnH, label));
        x += btnW + gap;
    };

    addBtn(1, "Zoom +");
    addBtn(2, "Zoom -");
    addBtn(3, "Reset");
}

void ImageViewer::handleKeyPress(int keyCode) {
    if (keyCode == '+' || keyCode == '=') {
        zoomIn();
    } else if (keyCode == '-') {
        zoomOut();
    } else if (keyCode == '0') {
        resetZoom();
    }
    s_lastKeyCode = keyCode;
}

}} // namespace gxos::apps
