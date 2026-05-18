#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

class DisplayOptions {
public:
    static uint64_t Launch();
    static int main(int argc, char** argv);

private:
    static uint64_t s_windowId;
    static int s_selectedIndex;
    static int s_appliedIndex;
    static int s_mouseX;
    static int s_mouseY;
    static bool s_mouseDown;

    static void loadSelection();
    static void render();
    static void drawButton(int x, int y, int w, int h, const std::string& text, bool active, bool enabled);
    static void drawWallpaperTile(int index, int x, int y, bool hover, bool selected, bool applied);
    static void handleMouseDown(int mx, int my);
    static void handleMouseUp(int mx, int my);
    static void handleMouseMove(int mx, int my);
    static void handleDoubleClick(int mx, int my);
    static void applySelectedWallpaper();
    static bool hit(int mx, int my, int x, int y, int w, int h);
};

} // namespace apps
} // namespace gxos
