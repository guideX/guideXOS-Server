#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "display_model.h"
#include "desktop_theme.h"

namespace gxos {
namespace apps {

class DisplayOptions {
public:
    static uint64_t Launch();
    static int main(int argc, char** argv);

public:
    static uint64_t s_windowId;
    static int s_selectedIndex;
    static int s_appliedIndex;
    static int s_selectedBackgroundIndex;
    static int s_appliedBackgroundIndex;
    static int s_selectedGradientIndex;
    static int s_appliedGradientIndex;
    static DesktopThemeId s_selectedThemeId;
    static DesktopThemeId s_appliedThemeId;
    static int s_activeTab;
    static int s_windowW;
    static int s_windowH;
    static int s_backgroundGalleryScrollOffset;
    static int s_gradientGalleryScrollOffset;
    static int s_mouseX;
    static int s_mouseY;
    static bool s_mouseDown;
    static bool s_galleryScrollbarDragging;
    static int s_galleryScrollbarDragStartY;
    static int s_galleryScrollbarDragStartOffset;
    static int s_selectedTimeZoneIndex;
    static int s_appliedTimeZoneIndex;
    static bool s_use24HourTime;
    static bool s_appliedUse24HourTime;
    static bool s_showDesktopTrash;
    static bool s_showDesktopThisSystem;
    static bool s_showDesktopFileManager;
    static bool s_showDesktopSystemSettings;
    static bool s_smallLiveDesktopFolderIcons;
    static std::string s_selectedDisplayMode;
    static std::string s_appliedDisplayMode;
    static std::string s_displayPrimaryDisplayId;
    static std::string s_appliedDisplayPrimaryDisplayId;
    static std::string s_displayArrangement;
    static std::string s_appliedDisplayArrangement;
    static std::string s_displayResolution;
    static std::string s_displayStatus;
    static uint64_t s_windowGeneration;
    static uint64_t s_displayRequestId;
    static bool s_displayRequestPending;

    static void loadSelection();
    static void render();
    static void drawButton(int x, int y, int w, int h, const std::string& text, bool active, bool enabled);
    static void drawCheckbox(int x, int y, const std::string& text, bool checked, bool hover);
    static void drawBackgroundTile(int index, int x, int y, bool hover, bool selected, bool applied);
    static void drawWallpaperTile(int index, int x, int y, bool hover, bool selected, bool applied);
    static void drawGradientTile(int index, int x, int y, bool hover, bool selected, bool applied);
    static void drawThemeTab();
    static void drawDesktopIconsTab();
    static void drawRegionTimeTab();
    static void drawDisplayTab();
    static void handleMouseDown(int mx, int my);
    static void handleMouseUp(int mx, int my);
    static void handleMouseMove(int mx, int my);
    static void handleDoubleClick(int mx, int my);
    static void applySelectedWallpaper();
    static void applySelectedGradient();
    static void applySelectedBackground();
    static void applySelectedTheme();
    static void applySelectedRegionTime();
    static void applySelectedDisplayMode();
    static void applySelectedDisplaySettings();
    static void applySelectedDisplaySettingsAndClose();
    static void cancelSelectedDisplaySettings();
    static bool saveDisplaySettings();
    static bool toggleDesktopIconSetting(int index);
    static void saveDesktopIconSettings();
    static void saveClockSettings();
    static bool hit(int mx, int my, int x, int y, int w, int h);
};

} // namespace apps
} // namespace gxos
