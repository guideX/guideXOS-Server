#include "paint.h"
#include "gui_protocol.h"
#include "vfs.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <queue>

namespace gxos { namespace apps {

// ?? static storage ??????????????????????????????????????????????
uint64_t Paint::s_windowId = 0;
PaintTool Paint::s_tool = PaintTool::Brush;
uint32_t  Paint::s_color = 0xFFF0F0F0;
uint32_t  Paint::s_bgColor = 0xFF222222;
int  Paint::s_brushSize = 5;
int  Paint::s_lastX = -1;
int  Paint::s_lastY = -1;
bool Paint::s_drawing = false;
std::vector<uint32_t> Paint::s_canvas;
int  Paint::s_canvasW = 0;
int  Paint::s_canvasH = 0;
int  Paint::s_lastKeyCode = 0;
bool Paint::s_keyDown = false;

// Colour palette matching legacy C# Paint.cs
const uint32_t Paint::kPalette[] = {
    0xFFC0392B, 0xFFE74C3C, 0xFFAF7AC5, 0xFF8E44AD,
    0xFF2980B9, 0xFF5DADE2, 0xFF1ABC9C, 0xFF45B39D,
    0xFF52BE80, 0xFF27AE60, 0xFFF1C40F, 0xFFE67E22,
    0xFFECF0F1, 0xFFD4AC0D, 0xFFFFFFFF, 0xFF000000
};

const char* Paint::kToolNames[] = {
    "Pencil", "Brush", "Eraser", "Line", "Rect", "Circle", "Fill"
};

// ?? Launch ??????????????????????????????????????????????????????
uint64_t Paint::Launch() {
    s_tool = PaintTool::Brush;
    s_color = 0xFFF0F0F0;
    s_brushSize = 5;
    s_lastX = -1;
    s_lastY = -1;
    s_drawing = false;
    s_lastKeyCode = 0;
    s_keyDown = false;
    ProcessSpec spec{"Paint", &Paint::main};
    return ProcessTable::spawn(spec, {});
}

int Paint::main(int /*argc*/, char** /*argv*/) {
    Logger::write(LogLevel::Info, "Paint starting");

    // Create window
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = "Paint|" + std::to_string(kWinW) + "|" + std::to_string(kWinH);
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

    // Init canvas
    s_canvasW = kWinW;
    s_canvasH = kWinH - kToolbarH;
    s_canvas.assign(static_cast<size_t>(s_canvasW) * s_canvasH, s_bgColor);

    updateDisplay();

    // Event loop
    bool running = true;
    while (running) {
        ipc::Message ev;
        if (ipc::Bus::pop("gui.output", ev, 100)) {
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
                        int id = std::stoi(widgetIdStr);
                        if (winId == s_windowId && event == "click") {
                            // Color buttons: id 100..115
                            if (id >= 100 && id < 100 + kPaletteCount) {
                                s_color = kPalette[id - 100];
                                Logger::write(LogLevel::Info, "Paint color selected");
                            }
                            // Tool buttons: id 200..206
                            else if (id >= 200 && id < 207) {
                                s_tool = static_cast<PaintTool>(id - 200);
                                Logger::write(LogLevel::Info, std::string("Paint tool: ") + kToolNames[id - 200]);
                            }
                            // Size buttons: id 300..304
                            else if (id >= 300 && id < 305) {
                                static const int sizes[] = {1, 3, 5, 10, 20};
                                s_brushSize = sizes[id - 300];
                            }
                            // Clear button: id 400
                            else if (id == 400) {
                                clearCanvas();
                            }
                            updateDisplay();
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

    Logger::write(LogLevel::Info, "Paint exiting");
    return 0;
}

// ?? Drawing helpers ?????????????????????????????????????????????
void Paint::drawPixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= s_canvasW || y >= s_canvasH) return;
    s_canvas[static_cast<size_t>(y) * s_canvasW + x] = color;
}

void Paint::drawLine(int x0, int y0, int x1, int y1, uint32_t color, int size) {
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        for (int oy = -size / 2; oy <= size / 2; ++oy)
            for (int ox = -size / 2; ox <= size / 2; ++ox)
                drawPixel(x0 + ox, y0 + oy, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void Paint::drawRect(int x0, int y0, int x1, int y1, uint32_t color) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) {
        drawPixel(x0, y, color);
        drawPixel(x1, y, color);
    }
    for (int x = x0; x <= x1; ++x) {
        drawPixel(x, y0, color);
        drawPixel(x, y1, color);
    }
}

void Paint::drawCircle(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0, e = 1 - r;
    while (x >= y) {
        drawPixel(cx + x, cy + y, color); drawPixel(cx - x, cy + y, color);
        drawPixel(cx + x, cy - y, color); drawPixel(cx - x, cy - y, color);
        drawPixel(cx + y, cy + x, color); drawPixel(cx - y, cy + x, color);
        drawPixel(cx + y, cy - x, color); drawPixel(cx - y, cy - x, color);
        ++y;
        if (e <= 0) { e += 2 * y + 1; }
        else { --x; e += 2 * (y - x) + 1; }
    }
}

void Paint::floodFill(int x, int y, uint32_t newColor) {
    if (x < 0 || y < 0 || x >= s_canvasW || y >= s_canvasH) return;
    uint32_t target = s_canvas[static_cast<size_t>(y) * s_canvasW + x];
    if (target == newColor) return;
    std::queue<std::pair<int,int>> q;
    q.push({x, y});
    while (!q.empty()) {
        int px = q.front().first;
        int py = q.front().second;
        q.pop();
        if (px < 0 || py < 0 || px >= s_canvasW || py >= s_canvasH) continue;
        if (s_canvas[static_cast<size_t>(py) * s_canvasW + px] != target) continue;
        s_canvas[static_cast<size_t>(py) * s_canvasW + px] = newColor;
        q.push({px + 1, py}); q.push({px - 1, py});
        q.push({px, py + 1}); q.push({px, py - 1});
    }
}

void Paint::clearCanvas() {
    std::fill(s_canvas.begin(), s_canvas.end(), s_bgColor);
    Logger::write(LogLevel::Info, "Paint canvas cleared");
}

// ?? UI ??????????????????????????????????????????????????????????
void Paint::updateDisplay() {
    if (s_windowId == 0) return;
    std::string wid = std::to_string(s_windowId);

    // Toolbar background
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawRect);
        std::ostringstream oss;
        oss << wid << "|0|0|" << kWinW << "|" << kToolbarH << "|48|48|48";
        auto s = oss.str();
        m.data.assign(s.begin(), s.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Color palette buttons (row 1)
    for (int i = 0; i < kPaletteCount; ++i) {
        int x = 10 + i * 30;
        uint32_t c = kPalette[i];
        int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        // Use button id 100+i
        auto payload = gui::packWidgetAdd(s_windowId, 1, 100 + i, x, 10, 24, 20, "");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Clear button
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1, 400, 550, 10, 60, 20, "Clear");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Tool buttons (row 2)
    for (int i = 0; i < 7; ++i) {
        int x = 10 + i * 70;
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1, 200 + i, x, 40, 60, 28, kToolNames[i]);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Brush size buttons
    static const char* sizeLabels[] = {"1px", "3px", "5px", "10px", "20px"};
    for (int i = 0; i < 5; ++i) {
        int x = 550 + i * 44;
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1, 300 + i, x, 40, 40, 28, sizeLabels[i]);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Status bar
    {
        std::string status = std::string("Tool: ") + kToolNames[static_cast<int>(s_tool)] +
                             "  Size: " + std::to_string(s_brushSize) + "px";
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|" + status;
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
}

void Paint::handleKeyPress(int keyCode) {
    switch (keyCode) {
        case 'p': case 'P': s_tool = PaintTool::Pencil; break;
        case 'b': case 'B': s_tool = PaintTool::Brush;  break;
        case 'e': case 'E': s_tool = PaintTool::Eraser; break;
        case 'l': case 'L': s_tool = PaintTool::Line;   break;
        case 'r': case 'R': s_tool = PaintTool::Rect;   break;
        case 'c': case 'C': s_tool = PaintTool::Circle; break;
        case 'f': case 'F': s_tool = PaintTool::Fill;   break;
        case 'x': case 'X': clearCanvas(); break;
        default: return;
    }
    updateDisplay();
}

}} // namespace gxos::apps
