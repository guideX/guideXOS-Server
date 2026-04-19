//
// Control Panel - Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "control_panel.h"
#include "gui_protocol.h"
#include "logger.h"
#include "process.h"
#include "ipc_bus.h"
#include "desktop_service.h"
#include <sstream>
#include <algorithm>

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
        "Display Settings",
        "Adjust screen resolution",
        "monitor",
        "DisplaySettings"
    ));
    
    s_items.push_back(PanelItem(
        "Desktop Background",
        "Change wallpaper",
        "image",
        "Wallpaper"
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

int ControlPanel::main(int argc, char** argv) {
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
        oss << "Control Panel|640|480";
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
                    
                    case MsgType::MT_Paint: {
                        render();
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
                                    s_selectedIndex == lastClickIndex && 
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
    
    // Clear background
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawClear;
    std::ostringstream oss;
    oss << s_windowId << "|240|240|240";  // Light gray background
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    // Title
    ipc::Message titleMsg;
    titleMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream titleOss;
    titleOss << s_windowId << "|20|10|Control Panel|0|0|0";
    std::string titlePayload = titleOss.str();
    titleMsg.data.assign(titlePayload.begin(), titlePayload.end());
    ipc::Bus::publish("gui.input", std::move(titleMsg), false);
    
    // Draw items in grid
    int cols = 3;
    int x = PAD;
    int y = 50;
    
    for (size_t i = 0; i < s_items.size(); i++) {
        int col = i % cols;
        int row = i / cols;
        
        int itemX = PAD + col * (ITEM_W + GAP);
        int itemY = 50 + row * (ITEM_H + GAP);
        
        bool hover = hit(s_mouseX, s_mouseY, itemX, itemY, ITEM_W, ITEM_H);
        bool selected = (static_cast<int>(i) == s_selectedIndex);
        
        drawItem(itemX, itemY, s_items[i], hover, selected);
    }
    
    // Request paint
    ipc::Message paintMsg;
    paintMsg.type = (uint32_t)MsgType::MT_Paint;
    std::string paintPayload = std::to_string(s_windowId);
    paintMsg.data.assign(paintPayload.begin(), paintPayload.end());
    ipc::Bus::publish("gui.input", std::move(paintMsg), false);
}

void ControlPanel::drawItem(int x, int y, const PanelItem& item, bool hover, bool selected) {
    // Background
    uint8_t r, g, b;
    if (selected) {
        r = g = b = 180;  // Selected: medium gray
    } else if (hover) {
        r = g = b = 220;  // Hover: light gray
    } else {
        r = g = b = 255;  // Normal: white
    }
    
    ipc::Message bgMsg;
    bgMsg.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream bgOss;
    bgOss << s_windowId << "|" << x << "|" << y << "|" << ITEM_W << "|" << ITEM_H 
          << "|" << static_cast<int>(r) << "|" << static_cast<int>(g) << "|" << static_cast<int>(b);
    std::string bgPayload = bgOss.str();
    bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
    ipc::Bus::publish("gui.input", std::move(bgMsg), false);
    
    // Border
    ipc::Message borderMsg;
    borderMsg.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream borderOss;
    borderOss << s_windowId << "|" << x << "|" << y << "|" << ITEM_W << "|1|128|128|128";  // Top
    std::string borderPayload = borderOss.str();
    borderMsg.data.assign(borderPayload.begin(), borderPayload.end());
    ipc::Bus::publish("gui.input", std::move(borderMsg), false);
    
    // Icon placeholder (centered)
    int iconX = x + (ITEM_W - ICON_SIZE) / 2;
    int iconY = y + 10;
    ipc::Message iconMsg;
    iconMsg.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream iconOss;
    iconOss << s_windowId << "|" << iconX << "|" << iconY << "|" << ICON_SIZE << "|" << ICON_SIZE << "|76|139|245";
    std::string iconPayload = iconOss.str();
    iconMsg.data.assign(iconPayload.begin(), iconPayload.end());
    ipc::Bus::publish("gui.input", std::move(iconMsg), false);
    
    // Name (centered)
    ipc::Message nameMsg;
    nameMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream nameOss;
    nameOss << s_windowId << "|" << (x + 10) << "|" << (iconY + ICON_SIZE + 6) 
            << "|" << item.name << "|0|0|0";
    std::string namePayload = nameOss.str();
    nameMsg.data.assign(namePayload.begin(), namePayload.end());
    ipc::Bus::publish("gui.input", std::move(nameMsg), false);
    
    // Description
    ipc::Message descMsg;
    descMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream descOss;
    descOss << s_windowId << "|" << (x + 10) << "|" << (iconY + ICON_SIZE + 22) 
            << "|" << item.description << "|64|64|64";
    std::string descPayload = descOss.str();
    descMsg.data.assign(descPayload.begin(), descPayload.end());
    ipc::Bus::publish("gui.input", std::move(descMsg), false);
}

void ControlPanel::handleMouseMove(int mx, int my) {
    render();  // Update hover states
}

void ControlPanel::handleMouseDown(int mx, int my) {
    // Check which item was clicked
    int cols = 3;
    
    for (size_t i = 0; i < s_items.size(); i++) {
        int col = i % cols;
        int row = i / cols;
        
        int itemX = PAD + col * (ITEM_W + GAP);
        int itemY = 50 + row * (ITEM_H + GAP);
        
        if (hit(mx, my, itemX, itemY, ITEM_W, ITEM_H)) {
            s_selectedIndex = i;
            render();
            return;
        }
    }
    
    s_selectedIndex = -1;
    render();
}

void ControlPanel::handleMouseUp(int mx, int my) {
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
