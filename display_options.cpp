#include "display_options.h"

#include "desktop_config.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "process.h"
#include "wallpaper_registry.h"
#include <chrono>
#include <sstream>

namespace gxos {
namespace apps {

using namespace gxos::gui;

uint64_t DisplayOptions::s_windowId = 0;
int DisplayOptions::s_selectedIndex = 0;
int DisplayOptions::s_appliedIndex = 0;
int DisplayOptions::s_selectedGradientIndex = 0;
int DisplayOptions::s_appliedGradientIndex = 0;
int DisplayOptions::s_activeTab = 0;
int DisplayOptions::s_mouseX = 0;
int DisplayOptions::s_mouseY = 0;
bool DisplayOptions::s_mouseDown = false;

namespace {
    const int kWindowW = 800;
    const int kWindowH = 560;
    const int kTabY = 18;
    const int kTabW = 220;
    const int kTabH = 40;
    const int kGalleryX = 26;
    const int kGalleryY = 100;
    const int kTileW = 130;
    const int kTileH = 112;
    const int kThumbW = 116;
    const int kThumbH = 72;
    const int kGapX = 18;
    const int kGapY = 22;
    const int kCols = 5;
    const int kSelectButtonX = 26;
    const int kButtonY = 486;
    const int kButtonW = 180;
    const int kButtonH = 36;

    void publish(MsgType type, const std::string& payload)
    {
        ipc::Message msg;
        msg.type = static_cast<uint32_t>(type);
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
    }

    void drawRect(uint64_t windowId, int x, int y, int w, int h, int r, int g, int b)
    {
        std::ostringstream oss;
        oss << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
        publish(MsgType::MT_DrawRect, oss.str());
    }

    void drawText(uint64_t windowId, int x, int y, const std::string& text)
    {
        std::ostringstream oss;
        oss << windowId << "|" << x << "|" << y << "|" << text;
        publish(MsgType::MT_DrawTextAt, oss.str());
    }

    void drawImage(uint64_t windowId, int x, int y, int w, int h, const std::string& path)
    {
        publish(MsgType::MT_DrawImage, packDrawImage(windowId, x, y, w, h, path));
    }

    std::string selectedWallpaperIdFromConfig()
    {
        DesktopConfigData cfg;
        std::string err;
        if (!DesktopConfig::Load("desktop.json", cfg, err)) return WallpaperRegistry::DefaultWallpaper().id;
        if (!cfg.wallpaperId.empty()) return WallpaperRegistry::ResolveIdOrDefault(cfg.wallpaperId);
        std::string id = WallpaperRegistry::IdForAssetPath(cfg.wallpaperPath);
        return id.empty() ? WallpaperRegistry::DefaultWallpaper().id : id;
    }
}

uint64_t DisplayOptions::Launch()
{
    ProcessSpec spec{"displayoptions", DisplayOptions::main};
    return ProcessTable::spawn(spec, {"displayoptions"});
}

void DisplayOptions::loadSelection()
{
    std::string selectedId = selectedWallpaperIdFromConfig();
    const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
    const auto& gradients = WallpaperRegistry::BuiltInGradients();
    s_selectedIndex = 0;
    s_appliedIndex = 0;
    s_selectedGradientIndex = 0;
    s_appliedGradientIndex = 0;
    s_activeTab = WallpaperRegistry::IsGradientId(selectedId) ? 1 : 0;
    for (size_t i = 0; i < gradients.size(); ++i) {
        if (gradients[i].id == selectedId) {
            s_selectedGradientIndex = static_cast<int>(i);
            s_appliedGradientIndex = static_cast<int>(i);
            return;
        }
    }
    for (size_t i = 0; i < wallpapers.size(); ++i) {
        if (wallpapers[i].id == selectedId) {
            s_selectedIndex = static_cast<int>(i);
            s_appliedIndex = static_cast<int>(i);
            break;
        }
    }
}

int DisplayOptions::main(int, char**)
{
    Logger::write(LogLevel::Info, "DisplayOptions starting");
    s_windowId = 0;
    s_mouseX = 0;
    s_mouseY = 0;
    s_mouseDown = false;
    loadSelection();

    ipc::Bus::ensure("gui.input");
    ipc::Bus::ensure("gui.output");

    std::ostringstream create;
    create << "Display Options|" << kWindowW << "|" << kWindowH;
    publish(MsgType::MT_Create, create.str());

    bool running = true;
    uint64_t lastClickTime = 0;
    int lastClickIndex = -1;

    while (running) {
        ipc::Message msg;
        if (!ipc::Bus::pop("gui.output", msg, 100)) continue;

        MsgType msgType = static_cast<MsgType>(msg.type);
        std::string payload(msg.data.begin(), msg.data.end());
        switch (msgType) {
        case MsgType::MT_Create: {
            size_t sep = payload.find('|');
            if (sep != std::string::npos) {
                try {
                    s_windowId = std::stoull(payload.substr(0, sep));
                    render();
                } catch (...) {
                    Logger::write(LogLevel::Warn, "DisplayOptions failed to parse create ack");
                }
            }
            break;
        }
        case MsgType::MT_InputMouse: {
            std::istringstream iss(payload);
            std::string xs, ys, btns;
            std::getline(iss, xs, '|');
            std::getline(iss, ys, '|');
            std::getline(iss, btns, '|');
            try {
                int x = std::stoi(xs);
                int y = std::stoi(ys);
                int buttons = std::stoi(btns);
                bool wasDown = s_mouseDown;
                s_mouseX = x;
                s_mouseY = y;
                s_mouseDown = (buttons & 1) != 0;
                handleMouseMove(x, y);
                if (s_mouseDown && !wasDown) {
                    handleMouseDown(x, y);
                    uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                    if (s_selectedIndex == lastClickIndex && (now - lastClickTime) < 500) {
                        handleDoubleClick(x, y);
                        lastClickTime = 0;
                        lastClickIndex = -1;
                    } else {
                        lastClickTime = now;
                        lastClickIndex = s_selectedIndex;
                    }
                } else if (!s_mouseDown && wasDown) {
                    handleMouseUp(x, y);
                }
            } catch (...) {
            }
            break;
        }
        case MsgType::MT_Close:
            running = false;
            break;
        default:
            break;
        }
    }

    Logger::write(LogLevel::Info, "DisplayOptions terminated");
    return 0;
}

void DisplayOptions::render()
{
    if (s_windowId == 0) return;

    publish(MsgType::MT_DrawText, std::to_string(s_windowId) + "|\f");
    drawRect(s_windowId, 0, 0, kWindowW, kWindowH, 27, 31, 40);

    drawButton(20, kTabY, kTabW, kTabH, "Backgrounds", s_activeTab == 0, true);
    drawButton(250, kTabY, kTabW, kTabH, "Resolution", false, false);
    drawButton(480, kTabY, kTabW, kTabH, "Gradients", s_activeTab == 1, true);
    drawText(s_windowId, 26, 72, s_activeTab == 0 ? "Select a background from the gallery:" : "Select a gradient from the gallery:");
    drawRect(s_windowId, 20, 92, 742, 372, 22, 22, 24);

    if (s_activeTab == 0) {
        const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
        for (size_t i = 0; i < wallpapers.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            int x = kGalleryX + col * (kTileW + kGapX);
            int y = kGalleryY + row * (kTileH + kGapY);
            bool hover = hit(s_mouseX, s_mouseY, x, y, kTileW, kTileH);
            drawWallpaperTile(static_cast<int>(i), x, y, hover, static_cast<int>(i) == s_selectedIndex, static_cast<int>(i) == s_appliedIndex);
        }
    } else {
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        for (size_t i = 0; i < gradients.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            int x = kGalleryX + col * (kTileW + kGapX);
            int y = kGalleryY + row * (kTileH + kGapY);
            bool hover = hit(s_mouseX, s_mouseY, x, y, kTileW, kTileH);
            drawGradientTile(static_cast<int>(i), x, y, hover, static_cast<int>(i) == s_selectedGradientIndex, static_cast<int>(i) == s_appliedGradientIndex);
        }
    }

    drawButton(kSelectButtonX, kButtonY, kButtonW, kButtonH, s_activeTab == 0 ? "Select Background" : "Select Gradient", false, true);
    drawButton(kSelectButtonX + 200, kButtonY, kButtonW, kButtonH, "Choose Color", false, false);
    drawButton(kSelectButtonX + 400, kButtonY, kButtonW, kButtonH, "Visual Effects", false, false);
}

void DisplayOptions::drawButton(int x, int y, int w, int h, const std::string& text, bool active, bool enabled)
{
    if (active) drawRect(s_windowId, x, y, w, h, 58, 58, 58);
    else if (enabled) drawRect(s_windowId, x, y, w, h, 48, 48, 52);
    else drawRect(s_windowId, x, y, w, h, 34, 34, 38);
    drawRect(s_windowId, x, y, w, 1, 90, 90, 95);
    drawRect(s_windowId, x, y + h - 1, w, 1, 85, 85, 90);
    drawRect(s_windowId, x, y, 1, h, 85, 85, 90);
    drawRect(s_windowId, x + w - 1, y, 1, h, 55, 55, 60);
    drawText(s_windowId, x + 16, y + 14, enabled ? text : text + " (soon)");
}

void DisplayOptions::drawWallpaperTile(int index, int x, int y, bool hover, bool selected, bool applied)
{
    const auto& entry = WallpaperRegistry::BuiltInWallpapers()[static_cast<size_t>(index)];
    if (selected) drawRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, 72, 110, 180);
    else if (hover) drawRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, 55, 65, 85);
    drawRect(s_windowId, x, y, kTileW, kTileH, 42, 42, 42);
    drawRect(s_windowId, x + 6, y + 8, kThumbW, kThumbH, 18, 18, 20);
    std::string thumbnailPath = entry.thumbnailPath.empty() ? entry.fullImagePath : entry.thumbnailPath;
    Logger::write(LogLevel::Info, std::string("DisplayOptions thumbnail id=") + entry.id + " path=" + thumbnailPath);
    drawImage(s_windowId, x + 6, y + 8, kThumbW, kThumbH, thumbnailPath);
    drawText(s_windowId, x + 8, y + 88, entry.displayName + (applied ? " *" : ""));
}

void DisplayOptions::drawGradientTile(int index, int x, int y, bool hover, bool selected, bool applied)
{
    const auto& entry = WallpaperRegistry::BuiltInGradients()[static_cast<size_t>(index)];
    if (selected) drawRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, 72, 110, 180);
    else if (hover) drawRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, 55, 65, 85);
    drawRect(s_windowId, x, y, kTileW, kTileH, 42, 42, 42);
    for (int py = 0; py < kThumbH; ++py) {
        int t = (py * 255) / (kThumbH > 1 ? kThumbH - 1 : 1);
        int r = ((((entry.topColor >> 16) & 0xFF) * (255 - t)) + (((entry.bottomColor >> 16) & 0xFF) * t)) / 255;
        int g = ((((entry.topColor >> 8) & 0xFF) * (255 - t)) + (((entry.bottomColor >> 8) & 0xFF) * t)) / 255;
        int b = (((entry.topColor & 0xFF) * (255 - t)) + ((entry.bottomColor & 0xFF) * t)) / 255;
        drawRect(s_windowId, x + 6, y + 8 + py, kThumbW, 1, r, g, b);
    }
    drawRect(s_windowId, x + 6, y + 8, kThumbW, 1, (entry.accentColor >> 16) & 0xFF, (entry.accentColor >> 8) & 0xFF, entry.accentColor & 0xFF);
    drawText(s_windowId, x + 8, y + 88, entry.displayName + (applied ? " *" : ""));
}

void DisplayOptions::handleMouseMove(int, int)
{
    render();
}

void DisplayOptions::handleMouseDown(int mx, int my)
{
    if (hit(mx, my, 20, kTabY, kTabW, kTabH)) {
        s_activeTab = 0;
        render();
        return;
    }
    if (hit(mx, my, 480, kTabY, kTabW, kTabH)) {
        s_activeTab = 1;
        render();
        return;
    }

    if (s_activeTab == 1) {
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        for (size_t i = 0; i < gradients.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            int x = kGalleryX + col * (kTileW + kGapX);
            int y = kGalleryY + row * (kTileH + kGapY);
            if (hit(mx, my, x, y, kTileW, kTileH)) {
                s_selectedGradientIndex = static_cast<int>(i);
                render();
                return;
            }
        }

        if (hit(mx, my, kSelectButtonX, kButtonY, kButtonW, kButtonH)) {
            applySelectedGradient();
            render();
        }
        return;
    }

    const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
    for (size_t i = 0; i < wallpapers.size(); ++i) {
        int col = static_cast<int>(i) % kCols;
        int row = static_cast<int>(i) / kCols;
        int x = kGalleryX + col * (kTileW + kGapX);
        int y = kGalleryY + row * (kTileH + kGapY);
        if (hit(mx, my, x, y, kTileW, kTileH)) {
            s_selectedIndex = static_cast<int>(i);
            render();
            return;
        }
    }

    if (hit(mx, my, kSelectButtonX, kButtonY, kButtonW, kButtonH)) {
        applySelectedWallpaper();
        render();
    }
}

void DisplayOptions::handleMouseUp(int, int)
{
}

void DisplayOptions::handleDoubleClick(int mx, int my)
{
    if (s_activeTab == 1) {
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        for (size_t i = 0; i < gradients.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            int x = kGalleryX + col * (kTileW + kGapX);
            int y = kGalleryY + row * (kTileH + kGapY);
            if (hit(mx, my, x, y, kTileW, kTileH)) {
                s_selectedGradientIndex = static_cast<int>(i);
                applySelectedGradient();
                render();
                return;
            }
        }
        return;
    }

    const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
    for (size_t i = 0; i < wallpapers.size(); ++i) {
        int col = static_cast<int>(i) % kCols;
        int row = static_cast<int>(i) / kCols;
        int x = kGalleryX + col * (kTileW + kGapX);
        int y = kGalleryY + row * (kTileH + kGapY);
        if (hit(mx, my, x, y, kTileW, kTileH)) {
            s_selectedIndex = static_cast<int>(i);
            applySelectedWallpaper();
            render();
            return;
        }
    }
}

void DisplayOptions::applySelectedGradient()
{
    const auto& gradients = WallpaperRegistry::BuiltInGradients();
    if (s_selectedGradientIndex < 0 || s_selectedGradientIndex >= static_cast<int>(gradients.size())) return;
    const GradientEntry& selected = gradients[static_cast<size_t>(s_selectedGradientIndex)];
    ipc::Message msg;
    msg.type = static_cast<uint32_t>(MsgType::MT_DesktopWallpaperSet);
    msg.data.assign(selected.id.begin(), selected.id.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    s_appliedGradientIndex = s_selectedGradientIndex;
    Logger::write(LogLevel::Info, std::string("DisplayOptions applied gradient id=") + selected.id);
}

void DisplayOptions::applySelectedWallpaper()
{
    const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
    if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(wallpapers.size())) return;
    const WallpaperEntry& selected = wallpapers[static_cast<size_t>(s_selectedIndex)];
    ipc::Message msg;
    msg.type = static_cast<uint32_t>(MsgType::MT_DesktopWallpaperSet);
    msg.data.assign(selected.id.begin(), selected.id.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    s_appliedIndex = s_selectedIndex;
    Logger::write(LogLevel::Info, std::string("DisplayOptions applied wallpaper id=") + selected.id + " full=" + selected.fullImagePath + " thumb=" + selected.thumbnailPath);
}

bool DisplayOptions::hit(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

} // namespace apps
} // namespace gxos
