//
// Control Panel - Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "control_panel.h"
#include "desktop_theme.h"
#include "gui_protocol.h"
#include "logger.h"
#include "process.h"
#include "ipc_bus.h"
#include "desktop_service.h"
#include <chrono>
#include <sstream>
#include <algorithm>

namespace {
    constexpr int kWindowW = 640;
    constexpr int kWindowH = 480;
    constexpr int kHeaderX = 20;
    constexpr int kHeaderY = 10;
    constexpr int kPanelX = 12;
    constexpr int kPanelY = 36;
    constexpr int kPanelW = 616;
    constexpr int kPanelH = 432;
    constexpr int kGridTop = 50;

    uint32_t packRgb(int r, int g, int b)
    {
        return 0xFF000000u |
            (static_cast<uint32_t>(r & 0xFF) << 16) |
            (static_cast<uint32_t>(g & 0xFF) << 8) |
            static_cast<uint32_t>(b & 0xFF);
    }

    uint32_t blendColor(uint32_t baseColor, uint32_t overlayColor, int overlayPercent)
    {
        if (overlayPercent <= 0) {
            return baseColor;
        }
        if (overlayPercent >= 100) {
            return overlayColor;
        }

        const int baseR = static_cast<int>((baseColor >> 16) & 0xFF);
        const int baseG = static_cast<int>((baseColor >> 8) & 0xFF);
        const int baseB = static_cast<int>(baseColor & 0xFF);
        const int overR = static_cast<int>((overlayColor >> 16) & 0xFF);
        const int overG = static_cast<int>((overlayColor >> 8) & 0xFF);
        const int overB = static_cast<int>(overlayColor & 0xFF);
        const int keepPercent = 100 - overlayPercent;

        return packRgb(
            (baseR * keepPercent + overR * overlayPercent) / 100,
            (baseG * keepPercent + overG * overlayPercent) / 100,
            (baseB * keepPercent + overB * overlayPercent) / 100);
    }

    bool isSciFiThemeActive()
    {
        return GetCurrentDesktopThemeId() == DesktopThemeId::SciFi;
    }

    const DesktopTheme& controlPanelTheme()
    {
        return GetCurrentDesktopTheme();
    }

    uint32_t ControlPanelBodyColor()
    {
        if (!isSciFiThemeActive()) {
            return packRgb(240, 240, 240);
        }

        const auto& theme = controlPanelTheme();
        return blendColor(theme.taskbarBackground, theme.windowBackground, 18);
    }

    uint32_t ControlPanelPanelColor()
    {
        if (!isSciFiThemeActive()) {
            return packRgb(246, 246, 246);
        }

        const auto& theme = controlPanelTheme();
        return blendColor(theme.windowBackground, theme.taskbarBackground, 10);
    }

    uint32_t ControlPanelCardColor()
    {
        if (!isSciFiThemeActive()) {
            return packRgb(255, 255, 255);
        }

        const auto& theme = controlPanelTheme();
        return blendColor(theme.windowBackground, theme.taskbarBackground, 16);
    }

    uint32_t ControlPanelCardHoverColor()
    {
        if (!isSciFiThemeActive()) {
            return packRgb(220, 220, 220);
        }

        const auto& theme = controlPanelTheme();
        return blendColor(ControlPanelCardColor(), theme.mutedAccent, 8);
    }

    uint32_t ControlPanelCardSelectedColor()
    {
        if (!isSciFiThemeActive()) {
            return packRgb(180, 180, 180);
        }

        const auto& theme = controlPanelTheme();
        return blendColor(ControlPanelCardColor(), theme.accent, 10);
    }

    uint32_t ControlPanelBorderColor(bool selected, bool hover)
    {
        if (!isSciFiThemeActive()) {
            if (selected) {
                return packRgb(128, 128, 128);
            }
            if (hover) {
                return packRgb(200, 200, 200);
            }
            return packRgb(160, 160, 160);
        }

        const auto& theme = controlPanelTheme();
        if (selected) {
            return blendColor(theme.windowBorder, theme.accent, 42);
        }
        if (hover) {
            return blendColor(theme.windowBorder, theme.mutedAccent, 30);
        }
        return blendColor(theme.windowBorder, theme.taskbarBorder, 24);
    }

    uint32_t ControlPanelTextColor()
    {
        if (!isSciFiThemeActive()) {
            return packRgb(0, 0, 0);
        }

        return controlPanelTheme().titleBarText;
    }

    uint32_t ControlPanelMutedTextColor()
    {
        if (!isSciFiThemeActive()) {
            return packRgb(64, 64, 64);
        }

        const auto& theme = controlPanelTheme();
        return blendColor(theme.titleBarText, theme.taskbarBackground, 58);
    }

    uint32_t ControlPanelAccentColor()
    {
        if (!isSciFiThemeActive()) {
            return packRgb(76, 139, 245);
        }

        return controlPanelTheme().accent;
    }
}

namespace gxos {
namespace apps {

using namespace gxos::gui;

// Static member initialization
uint64_t ControlPanel::s_windowId = 0;
std::vector<ControlPanel::PanelItem> ControlPanel::s_items;
int ControlPanel::s_selectedIndex = -1;
int ControlPanel::s_mouseX = 0;
int ControlPanel::s_mouseY = 0;
bool ControlPanel::s_mouseDown = false;

uint64_t ControlPanel::Launch() {
    ProcessSpec spec{"controlpanel", ControlPanel::main};
    spec.appId = "gxos.builtin.controlpanel";
    return ProcessTable::spawn(spec, {"controlpanel"});
}

void ControlPanel::initItems() {
    s_items.clear();
    
    // System & Security
    s_items.push_back(PanelItem(
        "Disk Management",
        "Manage disks and partitions",
        "harddisk",
        "DiskManager"
    ));
    
    s_items.push_back(PanelItem(
        "Task Manager",
        "View running processes",
        "applications",
        "TaskManager"
    ));
    
    s_items.push_back(PanelItem(
        "System Info",
        "View system information",
        "info",
        "SystemInfo"
    ));
    
    // Appearance & Personalization
    s_items.push_back(PanelItem(
        "Display Options",
        "Backgrounds and display",
        "monitor",
        "DisplayOptions"
    ));
    
    s_items.push_back(PanelItem(
        "Desktop Background",
        "Change wallpaper",
        "image",
        "DisplayOptions"
    ));
    
    // Hardware & Sound
    s_items.push_back(PanelItem(
        "Device Manager",
        "Manage hardware devices",
        "device",
        "DeviceManager"
    ));
    
    // Network & Internet
    s_items.push_back(PanelItem(
        "Network Settings",
        "Configure network",
        "network",
        "NetworkSettings"
    ));
    
    // User Accounts
    s_items.push_back(PanelItem(
        "User Accounts",
        "Manage user accounts",
        "user",
        "UserAccounts"
    ));
}

int ControlPanel::main(int, char**) {
    try {
        Logger::write(LogLevel::Info, "ControlPanel starting...");
        
        // Initialize
        s_windowId = 0;
        s_selectedIndex = -1;
        s_mouseX = 0;
        s_mouseY = 0;
        s_mouseDown = false;
        
        initItems();
        
        // Subscribe to IPC
        const char* kGuiChanIn = "gui.input";
        const char* kGuiChanOut = "gui.output";
        ipc::Bus::ensure(kGuiChanIn);
        ipc::Bus::ensure(kGuiChanOut);
        
        // Create window (640x480)
        ipc::Message createMsg;
        createMsg.type = (uint32_t)MsgType::MT_Create;
        std::ostringstream oss;
        oss << "Control Panel|" << kWindowW << "|" << kWindowH;
        std::string payload = oss.str();
        createMsg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
        
        // Event loop
        bool running = true;
        uint64_t lastClickTime = 0;
        uint64_t lastClickIndex = -1;
        
        while (running) {
            ipc::Message msg;
            if (ipc::Bus::pop(kGuiChanOut, msg, 100)) {
                MsgType msgType = (MsgType)msg.type;
                std::string payload(msg.data.begin(), msg.data.end());
                
                switch (msgType) {
                    case MsgType::MT_Create: {
                        size_t sep = payload.find('|');
                        if (sep != std::string::npos && sep > 0) {
                            try {
                                std::string idStr = payload.substr(0, sep);
                                s_windowId = std::stoull(idStr);
                                Logger::write(LogLevel::Info, std::string("ControlPanel window created: ") + std::to_string(s_windowId));
                                render();
                            } catch (...) {
                                Logger::write(LogLevel::Error, "Failed to parse window ID");
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
                            
                            s_mouseX = x;
                            s_mouseY = y;
                            bool wasDown = s_mouseDown;
                            s_mouseDown = (buttons & 1) != 0;
                            
                            handleMouseMove(x, y);
                            
                            if (s_mouseDown && !wasDown) {
                                handleMouseDown(x, y);
                                
                                // Check for double-click
                                uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch()).count();
                                if (s_selectedIndex >= 0 &&
                                    static_cast<uint64_t>(s_selectedIndex) == lastClickIndex &&
                                    (now - lastClickTime) < 500) {
                                    // Double-click!
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
                        } catch (...) {}
                        break;
                    }
                    
                    case MsgType::MT_Close: {
                        Logger::write(LogLevel::Info, "ControlPanel closing");
                        running = false;
                        break;
                    }
                    
                    default:
                        break;
                }
            }
        }
        
        Logger::write(LogLevel::Info, "ControlPanel terminated");
        return 0;
        
    } catch (const std::exception& e) {
        Logger::write(LogLevel::Error, std::string("ControlPanel exception: ") + e.what());
        return 1;
    }
}

void ControlPanel::render() {
    if (s_windowId == 0) return;
    
    // Clear previous draw commands
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::string payload = std::to_string(s_windowId) + "|\f";
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);

    auto drawRect = [&](int x, int y, int w, int h, uint32_t color) {
        ipc::Message rectMsg;
        rectMsg.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream rectOss;
        rectOss << s_windowId << "|" << x << "|" << y << "|" << w << "|" << h
                << "|" << static_cast<int>((color >> 16) & 0xFF)
                << "|" << static_cast<int>((color >> 8) & 0xFF)
                << "|" << static_cast<int>(color & 0xFF);
        std::string rectPayload = rectOss.str();
        rectMsg.data.assign(rectPayload.begin(), rectPayload.end());
        ipc::Bus::publish("gui.input", std::move(rectMsg), false);
    };

    // Theme-aware shell background and content panel
    drawRect(0, 0, kWindowW, kWindowH, ControlPanelBodyColor());
    drawRect(kPanelX, kPanelY, kPanelW, kPanelH, ControlPanelPanelColor());
    drawRect(kPanelX, kPanelY, kPanelW, 1, ControlPanelBorderColor(false, false));
    drawRect(kPanelX, kPanelY + kPanelH - 1, kPanelW, 1, ControlPanelBorderColor(false, false));
    drawRect(kPanelX, kPanelY, 1, kPanelH, ControlPanelBorderColor(false, false));
    drawRect(kPanelX + kPanelW - 1, kPanelY, 1, kPanelH, ControlPanelBorderColor(false, false));
    
    // Title
    ipc::Message titleMsg;
    titleMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream titleOss;
    const uint32_t titleColor = ControlPanelTextColor();
    titleOss << s_windowId << "|" << kHeaderX << "|" << kHeaderY << "|Control Panel|"
             << static_cast<int>((titleColor >> 16) & 0xFF) << "|"
             << static_cast<int>((titleColor >> 8) & 0xFF) << "|"
             << static_cast<int>(titleColor & 0xFF);
    std::string titlePayload = titleOss.str();
    titleMsg.data.assign(titlePayload.begin(), titlePayload.end());
    ipc::Bus::publish("gui.input", std::move(titleMsg), false);
    
    // Draw items in grid
    int cols = 3;
    
    for (size_t i = 0; i < s_items.size(); i++) {
        int col = i % cols;
        int row = i / cols;
        
        int itemX = PAD + col * (ITEM_W + GAP);
        int itemY = kGridTop + row * (ITEM_H + GAP);
        
        bool hover = hit(s_mouseX, s_mouseY, itemX, itemY, ITEM_W, ITEM_H);
        bool selected = (static_cast<int>(i) == s_selectedIndex);
        
        drawItem(itemX, itemY, s_items[i], hover, selected);
    }
}

void ControlPanel::drawItem(int x, int y, const PanelItem& item, bool hover, bool selected) {
    const uint32_t cardColor = selected ? ControlPanelCardSelectedColor() : (hover ? ControlPanelCardHoverColor() : ControlPanelCardColor());
    const uint32_t borderColor = ControlPanelBorderColor(selected, hover);
    const uint32_t textColor = ControlPanelTextColor();
    const uint32_t mutedTextColor = ControlPanelMutedTextColor();
    const uint32_t accentColor = ControlPanelAccentColor();
    const uint32_t iconColor = isSciFiThemeActive()
        ? (selected ? blendColor(accentColor, controlPanelTheme().windowBorder, 18)
                    : (hover ? blendColor(accentColor, controlPanelTheme().mutedAccent, 20)
                             : blendColor(accentColor, controlPanelTheme().windowBackground, 26)))
        : accentColor;

    auto drawRect = [&](int rx, int ry, int rw, int rh, uint32_t color) {
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream oss;
        oss << s_windowId << "|" << rx << "|" << ry << "|" << rw << "|" << rh
            << "|" << static_cast<int>((color >> 16) & 0xFF)
            << "|" << static_cast<int>((color >> 8) & 0xFF)
            << "|" << static_cast<int>(color & 0xFF);
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
    };

    auto drawText = [&](int tx, int ty, const std::string& text, uint32_t color) {
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream oss;
        oss << s_windowId << "|" << tx << "|" << ty << "|" << text
            << "|" << static_cast<int>((color >> 16) & 0xFF)
            << "|" << static_cast<int>((color >> 8) & 0xFF)
            << "|" << static_cast<int>(color & 0xFF);
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
    };

    drawRect(x, y, ITEM_W, ITEM_H, cardColor);
    drawRect(x, y, ITEM_W, 1, blendColor(borderColor, ControlPanelTextColor(), 10));
    drawRect(x, y + ITEM_H - 1, ITEM_W, 1, blendColor(borderColor, ControlPanelPanelColor(), 18));
    drawRect(x, y, 1, ITEM_H, blendColor(borderColor, ControlPanelTextColor(), 10));
    drawRect(x + ITEM_W - 1, y, 1, ITEM_H, blendColor(borderColor, ControlPanelPanelColor(), 18));

    if (selected && isSciFiThemeActive()) {
        drawRect(x, y, 4, ITEM_H, accentColor);
    } else if (hover && isSciFiThemeActive()) {
        drawRect(x, y, ITEM_W, 2, blendColor(accentColor, controlPanelTheme().titleBarText, 12));
    }

    // Icon placeholder (centered)
    int iconX = x + (ITEM_W - ICON_SIZE) / 2;
    int iconY = y + 10;
    drawRect(iconX, iconY, ICON_SIZE, ICON_SIZE, iconColor);
    drawRect(iconX, iconY, ICON_SIZE, 1, blendColor(borderColor, iconColor, 18));
    drawRect(iconX, iconY + ICON_SIZE - 1, ICON_SIZE, 1, blendColor(borderColor, ControlPanelPanelColor(), 22));
    
    // Name (centered)
    drawText(x + 10, iconY + ICON_SIZE + 6, item.name, (selected && isSciFiThemeActive()) ? accentColor : textColor);
    
    // Description
    drawText(x + 10, iconY + ICON_SIZE + 22, item.description, mutedTextColor);
}

void ControlPanel::handleMouseMove(int, int) {
    render();  // Update hover states
}

void ControlPanel::handleMouseDown(int mx, int my) {
    // Check which item was clicked
    int cols = 3;
    
    for (size_t i = 0; i < s_items.size(); i++) {
        int col = i % cols;
        int row = i / cols;
        
        int itemX = PAD + col * (ITEM_W + GAP);
        int itemY = kGridTop + row * (ITEM_H + GAP);
        
        if (hit(mx, my, itemX, itemY, ITEM_W, ITEM_H)) {
            s_selectedIndex = i;
            render();
            return;
        }
    }
    
    s_selectedIndex = -1;
    render();
}

void ControlPanel::handleMouseUp(int, int) {
    // Nothing special needed
}

void ControlPanel::handleDoubleClick(int mx, int my) {
    if (s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_items.size())) {
        launchItem(s_items[s_selectedIndex].action);
    }
}

bool ControlPanel::hit(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

void ControlPanel::launchItem(const std::string& action) {
    Logger::write(LogLevel::Info, std::string("ControlPanel launching: ") + action);
    
    std::string error;
    if (!DesktopService::LaunchApp(action, error)) {
        Logger::write(LogLevel::Warn, std::string("Failed to launch ") + action + ": " + error);
    }
}

} // namespace apps
} // namespace gxos
