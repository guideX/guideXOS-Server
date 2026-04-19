//
// Control Panel - System Settings and Tools
//
// Provides access to system configuration and administrative tools
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gxos {
namespace apps {

class ControlPanel {
public:
    static uint64_t Launch();
    static int main(int argc, char** argv);
    
private:
    // Panel categories
    struct PanelItem {
        std::string name;
        std::string description;
        std::string icon;
        std::string action;
        
        PanelItem(const std::string& n, const std::string& d, const std::string& i, const std::string& a)
            : name(n), description(d), icon(i), action(a) {}
    };
    
    // State
    static uint64_t s_windowId;
    static std::vector<PanelItem> s_items;
    static int s_selectedIndex;
    static int s_mouseX, s_mouseY;
    static bool s_mouseDown;
    
    // Layout constants
    static const int ITEM_W = 180;
    static const int ITEM_H = 100;
    static const int ICON_SIZE = 48;
    static const int PAD = 20;
    static const int GAP = 16;
    
    // Initialization
    static void initItems();
    
    // Rendering
    static void render();
    static void drawItem(int x, int y, const PanelItem& item, bool hover, bool selected);
    
    // Input handling
    static void handleMouseMove(int mx, int my);
    static void handleMouseDown(int mx, int my);
    static void handleMouseUp(int mx, int my);
    static void handleDoubleClick(int mx, int my);
    static bool hit(int mx, int my, int x, int y, int w, int h);
    
    // Actions
    static void launchItem(const std::string& action);
};

} // namespace apps
} // namespace gxos
