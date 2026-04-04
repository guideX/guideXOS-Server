#include "image_viewer.h"
#include "gui_protocol.h"
#include "vfs.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>

namespace gxos { namespace apps {

uint64_t ImageViewer::s_windowId = 0;
std::string ImageViewer::s_filePath;
int ImageViewer::s_originalW = 0;
int ImageViewer::s_originalH = 0;
float ImageViewer::s_zoomLevel = 1.0f;
int ImageViewer::s_panX = 0;
int ImageViewer::s_panY = 0;
int ImageViewer::s_lastKeyCode = 0;
bool ImageViewer::s_keyDown = false;

uint64_t ImageViewer::Launch(const std::string& filePath) {
    s_filePath = filePath;
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    s_lastKeyCode = 0;
    s_keyDown = false;
    ProcessSpec spec{"ImageViewer", &ImageViewer::main};
    std::vector<std::string> args;
    if (!filePath.empty()) args.push_back(filePath);
    return ProcessTable::spawn(spec, args);
}

int ImageViewer::main(int argc, char** argv) {
    Logger::write(LogLevel::Info, "ImageViewer starting");

    std::string title = "Image Viewer";
    if (argc > 1 && argv[1]) {
        s_filePath = argv[1];
        title = std::string("Image Viewer - ") + s_filePath;
    }

    // Create window
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = title + "|" + std::to_string(kWinW) + "|" + std::to_string(kWinH);
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

    // Try to "load" image metadata from VFS
    if (!s_filePath.empty()) {
        std::vector<uint8_t> content;
        if (Vfs::instance().readFile(s_filePath, content)) {
            // In the real kernel, this would be raw pixel data.
            // For the user-mode server stub, record that a file was loaded.
            s_originalW = 400;
            s_originalH = 300;
            Logger::write(LogLevel::Info, "ImageViewer loaded: " + s_filePath +
                          " (" + std::to_string(content.size()) + " bytes)");
        } else {
            Logger::write(LogLevel::Info, "ImageViewer: file not found: " + s_filePath);
        }
    }

    updateDisplay();

    // Event loop
    bool running = true;
    while (running) {
        ipc::Message ev;
        if (ipc::Bus::pop("gui.output", ev, 150)) {
            if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_Close)) {
                running = false;
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_InputKey)) {
                std::string payload(ev.data.begin(), ev.data.end());
                int keyCode = 0;
                try { keyCode = std::stoi(payload); } catch (...) {}
                handleKeyPress(keyCode);
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_WidgetEvt)) {
                std::string payload(ev.data.begin(), ev.data.end());
                // Widget events: "winId|widgetId|event|value"
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
                            if (widgetId == 1) { zoomIn(); updateDisplay(); }
                            else if (widgetId == 2) { zoomOut(); updateDisplay(); }
                            else if (widgetId == 3) { resetZoom(); updateDisplay(); }
                        }
                    } catch (...) {}
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    if (s_windowId != 0) {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Close);
        std::string id = std::to_string(s_windowId);
        m.data.assign(id.begin(), id.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
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
    // In full kernel mode, this would re-scale pixel data.
    // For the user-mode server, update compositor metadata.
    Logger::write(LogLevel::Info, "ImageViewer zoom=" + std::to_string(static_cast<int>(s_zoomLevel * 100)) + "%");
}

void ImageViewer::updateDisplay() {
    if (s_windowId == 0) return;
    std::string wid = std::to_string(s_windowId);

    // Dark background
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawRect);
        std::ostringstream oss;
        oss << wid << "|0|0|" << kWinW << "|" << kWinH << "|30|30|30";
        auto s = oss.str();
        m.data.assign(s.begin(), s.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Status text with filename and zoom
    {
        int zoomPct = static_cast<int>(s_zoomLevel * 100);
        std::string info = s_filePath.empty() ? "No image loaded" :
                           s_filePath + "  " + std::to_string(zoomPct) + "%";
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|" + info;
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Toolbar buttons: Zoom In, Zoom Out, Reset
    int btnY = kWinH - 40;
    int btnW = 80, btnH = 28, gap = 8;
    int x = 10;

    auto addBtn = [&](int id, const std::string& label) {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1, id, x, btnY, btnW, btnH, label);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
        x += btnW + gap;
    };

    addBtn(1, "Zoom +");
    addBtn(2, "Zoom -");
    addBtn(3, "Reset");
}

void ImageViewer::handleKeyPress(int keyCode) {
    if (keyCode == '+' || keyCode == '=') { zoomIn(); updateDisplay(); }
    else if (keyCode == '-') { zoomOut(); updateDisplay(); }
    else if (keyCode == '0') { resetZoom(); updateDisplay(); }
}

}} // namespace gxos::apps
