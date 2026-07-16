#include "display_options.h"

#include "clock_time_settings.h"
#include "compositor.h"
#include "desktop_config.h"
#include "display_options_store.h"
#include "display_mode_catalog.h"
#include "display_configuration.h"
#include "display_configuration_service.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "process.h"
#include "wallpaper_registry.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

namespace gxos {
namespace apps {

using namespace gxos::gui;
using namespace gxos::display;

uint64_t DisplayOptions::s_windowId = 0;
int DisplayOptions::s_selectedIndex = 0;
int DisplayOptions::s_appliedIndex = 0;
int DisplayOptions::s_selectedBackgroundIndex = 0;
int DisplayOptions::s_appliedBackgroundIndex = 0;
int DisplayOptions::s_selectedGradientIndex = 0;
int DisplayOptions::s_appliedGradientIndex = 0;
DesktopThemeId DisplayOptions::s_selectedThemeId = DesktopThemeId::Classic;
DesktopThemeId DisplayOptions::s_appliedThemeId = DesktopThemeId::Classic;
int DisplayOptions::s_activeTab = 0;
int DisplayOptions::s_windowW = 800;
int DisplayOptions::s_windowH = 620;
int DisplayOptions::s_backgroundGalleryScrollOffset = 0;
int DisplayOptions::s_gradientGalleryScrollOffset = 0;
int DisplayOptions::s_mouseX = 0;
int DisplayOptions::s_mouseY = 0;
bool DisplayOptions::s_mouseDown = false;
bool DisplayOptions::s_galleryScrollbarDragging = false;
int DisplayOptions::s_galleryScrollbarDragStartY = 0;
int DisplayOptions::s_galleryScrollbarDragStartOffset = 0;
int DisplayOptions::s_selectedTimeZoneIndex = 0;
int DisplayOptions::s_appliedTimeZoneIndex = 0;
bool DisplayOptions::s_use24HourTime = false;
bool DisplayOptions::s_appliedUse24HourTime = false;
bool DisplayOptions::s_showDesktopTrash = true;
bool DisplayOptions::s_showDesktopThisSystem = true;
bool DisplayOptions::s_showDesktopFileManager = true;
bool DisplayOptions::s_showDesktopSystemSettings = false;
bool DisplayOptions::s_smallLiveDesktopFolderIcons = true;
std::string DisplayOptions::s_selectedDisplayMode = "mirror";
std::string DisplayOptions::s_appliedDisplayMode = "mirror";
std::string DisplayOptions::s_displayPrimaryDisplayId = "display-1";
std::string DisplayOptions::s_appliedDisplayPrimaryDisplayId = "display-1";
std::string DisplayOptions::s_displayArrangement = "";
std::string DisplayOptions::s_appliedDisplayArrangement = "";
std::string DisplayOptions::s_displayResolution = "";
std::string DisplayOptions::s_displayStatus = "";
uint64_t DisplayOptions::s_windowGeneration = 0;
uint64_t DisplayOptions::s_displayRequestId = 0;
bool DisplayOptions::s_displayRequestPending = false;

namespace {
    const char* kDisplayOptionsStorePath = "display-options.cfg";
    const int kDefaultWindowW = 800;
    const int kDefaultWindowH = 620;
    const int kTabY = 18;
    const int kTabW = 120;
    const int kTabH = 40;
    const int kThemeTabX = 20;
    const int kBackgroundTabX = 144;
    const int kDesktopIconTabX = 268;
    const int kGradientTabX = 392;
    const int kRegionTimeTabX = 516;
    const int kDisplayTabX = 640;
    const int kGalleryX = 26;
    const int kGalleryY = 100;
    const int kGalleryScrollBarW = 8;
    const int kTileW = 130;
    const int kTileH = 100;
    const int kThumbW = 116;
    const int kThumbH = 62;
    const int kGapX = 18;
    const int kGapY = 12;
    const int kCols = 5;
    const int kSelectButtonX = 26;
    const int kButtonY = 548;
    const int kButtonW = 180;
    const int kButtonH = 36;
    const int kDesktopIconsX = 46;
    const int kDesktopIconsY = 132;
    const int kDesktopIconRowH = 42;
    const int kDesktopIconCheckboxSize = 18;
    const int kThemeOptionX = 46;
    const int kThemeOptionY = 132;
    const int kThemeOptionW = 320;
    const int kThemeOptionH = 98;
    const int kThemeOptionGap = 14;
    const int kRegionTimeZoneX = 46;
    const int kRegionTimeZoneY = 132;
    const int kRegionTimeZoneW = 360;
    const int kRegionTimeZoneH = 36;
    const int kRegionTimeUse24X = 46;
    const int kRegionTimeUse24Y = 194;
    const int kDisplaySectionX = 46;
    const int kDisplaySectionY = 132;
    const int kDisplayModeButtonW = 140;
    const int kDisplayModeButtonH = 36;
    const int kDisplayModeY = 200;
    const int kDisplayCardY = 320;
    const int kDisplayCardW = 220;
    const int kDisplayCardH = 132;
    const int kDisplayCardGapX = 16;
    const int kDisplayCardGapY = 14;
    const int kGalleryMinHeight = 120;
    const int kGalleryFooterReserve = 84;
    const int kGalleryRightMargin = 26;
    const int kGalleryScrollbarGap = 6;
    const int kMinScrollbarThumbH = 18;
    const uint32_t kKeyLeft = 0x25;
    const uint32_t kKeyUp = 0x26;
    const uint32_t kKeyRight = 0x27;
    const uint32_t kKeyDown = 0x28;
    const uint32_t kKeyPageUp = 0x21;
    const uint32_t kKeyPageDown = 0x22;
    const uint32_t kKeyEnd = 0x23;
    const uint32_t kKeyHome = 0x24;
    const uint32_t kKeySpace = 0x20;
    const uint32_t kKeyEnter = 0x0D;
    const uint32_t kKeyEscape = 0x1B;

    void reloadDisplaySelectionFromResponse(const DisplayConfigurationResponse& response);
    bool queryActiveDisplayConfiguration(DisplayConfigurationResponse& response);
    DisplayConfigurationCommand displayConfigurationCommandFromUi(uint64_t requestId, uint32_t flags);
    void closeDisplayOptionsWindow();

    struct GalleryLayout {
        int itemCount{0};
        int rowCount{0};
        int columns{1};
        int visibleRows{1};
        int maxScroll{0};
        int galleryX{kGalleryX};
        int galleryY{kGalleryY};
        int galleryW{1};
        int galleryH{1};
        int contentW{1};
        int contentH{1};
        int scrollbarX{0};
        bool showScrollbar{false};
        int buttonY{0};
    };

    int rowCountForItemCount(int itemCount, int columns)
    {
        if (itemCount <= 0 || columns <= 0) return 0;
        return (itemCount + columns - 1) / columns;
    }

    int visibleRowsForHeight(int galleryH)
    {
        const int rowPitch = kTileH + kGapY;
        const int visible = (galleryH - kTileH) / rowPitch + 1;
        return std::max(1, visible);
    }

    int galleryColumnsForWidth(int galleryW)
    {
        const int stride = kTileW + kGapX;
        if (galleryW <= kTileW) return 1;
        int columns = (galleryW + kGapX) / stride;
        if (columns < 1) columns = 1;
        while (columns > 1) {
            const int contentW = columns * kTileW + (columns - 1) * kGapX;
            if (contentW <= galleryW) break;
            --columns;
        }
        return columns;
    }

    GalleryLayout layoutForWindow(int windowW, int windowH, int itemCount)
    {
        GalleryLayout layout;
        layout.itemCount = std::max(0, itemCount);
        layout.galleryX = kGalleryX;
        layout.galleryY = kGalleryY;
        layout.galleryW = std::max(1, windowW - kGalleryX - kGalleryRightMargin);
        layout.galleryH = std::max(kGalleryMinHeight, windowH - kGalleryY - kGalleryFooterReserve);
        layout.visibleRows = visibleRowsForHeight(layout.galleryH);
        layout.columns = galleryColumnsForWidth(layout.galleryW);
        layout.rowCount = rowCountForItemCount(layout.itemCount, layout.columns);
        layout.showScrollbar = layout.rowCount > layout.visibleRows;
        if (layout.showScrollbar) {
        layout.galleryW = std::max(1, layout.galleryW - (kGalleryScrollBarW + kGalleryScrollbarGap));
            layout.columns = galleryColumnsForWidth(layout.galleryW);
            layout.rowCount = rowCountForItemCount(layout.itemCount, layout.columns);
            layout.showScrollbar = layout.rowCount > layout.visibleRows;
        }
        layout.maxScroll = std::max(0, layout.rowCount - layout.visibleRows);
        layout.contentW = layout.columns * kTileW + std::max(0, layout.columns - 1) * kGapX;
        layout.contentH = layout.rowCount * kTileH + std::max(0, layout.rowCount - 1) * kGapY;
        layout.scrollbarX = windowW - kGalleryRightMargin - kGalleryScrollBarW;
        layout.buttonY = layout.galleryY + layout.galleryH + 12;
        return layout;
    }

    int& backgroundScrollOffset()
    {
        return DisplayOptions::s_backgroundGalleryScrollOffset;
    }

    int& gradientScrollOffset()
    {
        return DisplayOptions::s_gradientGalleryScrollOffset;
    }

    int& activeGalleryScrollOffset()
    {
        return DisplayOptions::s_activeTab == 0 ? backgroundScrollOffset() : gradientScrollOffset();
    }

    int activeGalleryItemCount()
    {
        return DisplayOptions::s_activeTab == 0
            ? static_cast<int>(WallpaperRegistry::BuiltInWallpapers().size())
            : static_cast<int>(WallpaperRegistry::BuiltInGradients().size());
    }

    void syncActiveSelectionMirror()
    {
        if (DisplayOptions::s_activeTab == 0) {
            DisplayOptions::s_selectedIndex = DisplayOptions::s_selectedBackgroundIndex;
        } else if (DisplayOptions::s_activeTab == 1) {
            DisplayOptions::s_selectedIndex = DisplayOptions::s_selectedGradientIndex;
        }
    }

    int activeSelectionIndex()
    {
        return DisplayOptions::s_activeTab == 0 ? DisplayOptions::s_selectedBackgroundIndex : DisplayOptions::s_selectedGradientIndex;
    }

    void setActiveSelectionIndex(int index)
    {
        if (DisplayOptions::s_activeTab == 0) {
            DisplayOptions::s_selectedBackgroundIndex = index;
            DisplayOptions::s_appliedIndex = DisplayOptions::s_appliedBackgroundIndex;
        } else if (DisplayOptions::s_activeTab == 1) {
            DisplayOptions::s_selectedGradientIndex = index;
            DisplayOptions::s_appliedIndex = DisplayOptions::s_appliedGradientIndex;
        }
        syncActiveSelectionMirror();
    }

    void clampSelectionToCurrentTab()
    {
        const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        if (DisplayOptions::s_activeTab == 0) {
            if (wallpapers.empty()) {
                DisplayOptions::s_selectedBackgroundIndex = 0;
                DisplayOptions::s_appliedBackgroundIndex = 0;
            } else {
                DisplayOptions::s_selectedBackgroundIndex = std::max(0, std::min(DisplayOptions::s_selectedBackgroundIndex, static_cast<int>(wallpapers.size()) - 1));
                DisplayOptions::s_appliedBackgroundIndex = std::max(0, std::min(DisplayOptions::s_appliedBackgroundIndex, static_cast<int>(wallpapers.size()) - 1));
            }
            syncActiveSelectionMirror();
        } else if (DisplayOptions::s_activeTab == 1) {
            if (gradients.empty()) {
                DisplayOptions::s_selectedGradientIndex = 0;
                DisplayOptions::s_appliedGradientIndex = 0;
            } else {
                DisplayOptions::s_selectedGradientIndex = std::max(0, std::min(DisplayOptions::s_selectedGradientIndex, static_cast<int>(gradients.size()) - 1));
                DisplayOptions::s_appliedGradientIndex = std::max(0, std::min(DisplayOptions::s_appliedGradientIndex, static_cast<int>(gradients.size()) - 1));
            }
            syncActiveSelectionMirror();
        }
    }

    void clampActiveScrollOffset()
    {
        const int itemCount = activeGalleryItemCount();
        GalleryLayout layout = layoutForWindow(DisplayOptions::s_windowW, DisplayOptions::s_windowH, itemCount);
        int& scroll = activeGalleryScrollOffset();
        scroll = std::max(0, std::min(scroll, layout.maxScroll));
    }

    void ensureActiveSelectionVisible()
    {
        const int itemCount = activeGalleryItemCount();
        if (itemCount <= 0) {
            activeGalleryScrollOffset() = 0;
            return;
        }

        GalleryLayout layout = layoutForWindow(DisplayOptions::s_windowW, DisplayOptions::s_windowH, itemCount);
        int& scroll = activeGalleryScrollOffset();
        const int selected = std::max(0, std::min(activeSelectionIndex(), itemCount - 1));
        const int row = layout.columns > 0 ? (selected / layout.columns) : 0;
        if (row < scroll) {
            scroll = row;
        } else if (row >= scroll + layout.visibleRows) {
            scroll = row - layout.visibleRows + 1;
        }
        scroll = std::max(0, std::min(scroll, layout.maxScroll));
    }

    bool isGalleryKey(uint32_t key)
    {
        return key == kKeyLeft || key == kKeyRight || key == kKeyUp || key == kKeyDown ||
            key == kKeyHome || key == kKeyEnd || key == kKeyPageUp || key == kKeyPageDown ||
            key == kKeySpace || key == kKeyEnter || key == kKeyEscape;
    }

    bool handleGalleryKey(uint32_t key)
    {
        if (DisplayOptions::s_activeTab != 0 && DisplayOptions::s_activeTab != 1) return false;

        const int itemCount = activeGalleryItemCount();
        if (itemCount <= 0) return false;

        GalleryLayout layout = layoutForWindow(DisplayOptions::s_windowW, DisplayOptions::s_windowH, itemCount);
        int selected = activeSelectionIndex();
        if (selected < 0) selected = 0;
        if (selected >= itemCount) selected = itemCount - 1;

        auto moveSelection = [&](int nextIndex) {
            nextIndex = std::max(0, std::min(nextIndex, itemCount - 1));
            if (DisplayOptions::s_activeTab == 0) {
                DisplayOptions::s_selectedBackgroundIndex = nextIndex;
            } else {
                DisplayOptions::s_selectedGradientIndex = nextIndex;
            }
            syncActiveSelectionMirror();
            ensureActiveSelectionVisible();
            DisplayOptions::render();
            return true;
        };

        auto scrollByRows = [&](int rows) {
            if (rows == 0) return false;
            int& scroll = activeGalleryScrollOffset();
            const int nextOffset = std::max(0, std::min(scroll + rows, layout.maxScroll));
            if (nextOffset == scroll) return false;
            scroll = nextOffset;
            DisplayOptions::render();
            return true;
        };

        if (key == kKeyLeft) {
            const int rowStart = (selected / layout.columns) * layout.columns;
            return moveSelection(std::max(rowStart, selected - 1));
        }
        if (key == kKeyRight) {
            const int rowStart = (selected / layout.columns) * layout.columns;
            const int rowEnd = std::min(rowStart + layout.columns - 1, itemCount - 1);
            return moveSelection(std::min(rowEnd, selected + 1));
        }
        if (key == kKeyUp) {
            return moveSelection(selected - layout.columns);
        }
        if (key == kKeyDown) {
            return moveSelection(selected + layout.columns);
        }
        if (key == kKeyHome) {
            return moveSelection(0);
        }
        if (key == kKeyEnd) {
            return moveSelection(itemCount - 1);
        }
        if (key == kKeyPageUp) {
            return scrollByRows(-layout.visibleRows);
        }
        if (key == kKeyPageDown) {
            return scrollByRows(layout.visibleRows);
        }
        if (key == kKeyEnter || key == kKeySpace) {
            if (DisplayOptions::s_activeTab == 0) {
                DisplayOptions::applySelectedBackground();
            } else {
                DisplayOptions::applySelectedGradient();
            }
            DisplayOptions::render();
            return true;
        }
        if (key == kKeyEscape) {
            return true;
        }
        return false;
    }

    int hitTestGalleryScrollbar(int mx, int my, const GalleryLayout& layout)
    {
        if (!layout.showScrollbar) return 0;
        if (mx < layout.scrollbarX || mx >= layout.scrollbarX + kGalleryScrollBarW) return 0;
        if (my < layout.galleryY || my >= layout.galleryY + layout.galleryH) return 0;

        const int trackTop = layout.galleryY;
        const int trackH = layout.galleryH;
        const int thumbH = std::max(kMinScrollbarThumbH, (layout.visibleRows * trackH) / std::max(1, layout.rowCount));
        const int thumbTravel = std::max(1, trackH - thumbH);
        const int scroll = std::max(0, std::min(activeGalleryScrollOffset(), layout.maxScroll));
        const int thumbY = trackTop + ((thumbTravel * scroll) / std::max(1, layout.maxScroll));
        if (my >= thumbY && my < thumbY + thumbH) return 1;
        if (my < thumbY) return 2;
        return 3;
    }

    int hitTestActiveGalleryTile(int mx, int my, const GalleryLayout& layout)
    {
        if (DisplayOptions::s_activeTab != 0 && DisplayOptions::s_activeTab != 1) return -1;
        if (mx < layout.galleryX || mx >= layout.galleryX + layout.galleryW) return -1;
        if (my < layout.galleryY || my >= layout.galleryY + layout.galleryH) return -1;

        const int itemCount = activeGalleryItemCount();
        const int scroll = std::max(0, std::min(activeGalleryScrollOffset(), layout.maxScroll));
        const int relX = mx - layout.galleryX;
        const int relY = my - layout.galleryY;
        const int colStride = kTileW + kGapX;
        const int rowStride = kTileH + kGapY;
        int col = relX / colStride;
        int row = relY / rowStride + scroll;
        if (col < 0 || col >= layout.columns) return -1;
        if (row < 0 || row >= layout.rowCount) return -1;
        const int index = row * layout.columns + col;
        if (index < 0 || index >= itemCount) return -1;
        const int tileX = layout.galleryX + col * colStride;
        const int tileY = layout.galleryY + (row - scroll) * rowStride;
        if (mx >= tileX && mx < tileX + kTileW && my >= tileY && my < tileY + kTileH) return index;
        return -1;
    }

    void setActiveTabAndClamp(int tab)
    {
        if (tab < 0 || tab > 5) return;
        DisplayOptions::s_activeTab = tab;
        if (tab == 0 || tab == 1) {
            clampSelectionToCurrentTab();
            clampActiveScrollOffset();
            ensureActiveSelectionVisible();
        } else {
            clampSelectionToCurrentTab();
        }
        DisplayOptions::s_galleryScrollbarDragging = false;
    }

    int clockTimeZoneIndexFromId(const std::string& id)
    {
        return static_cast<int>(gxos::clocktime::TimeZoneIndexFromId(id));
    }

    std::string clockTimeZoneLabelAt(int index)
    {
        return gxos::clocktime::TimeZoneOptionAt(static_cast<size_t>(std::max(0, index))).displayName;
    }

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

    void drawColorRect(uint64_t windowId, int x, int y, int w, int h, uint32_t color)
    {
        drawRect(windowId, x, y, w, h,
            static_cast<int>((color >> 16) & 0xFF),
            static_cast<int>((color >> 8) & 0xFF),
            static_cast<int>(color & 0xFF));
    }

    void drawText(uint64_t windowId, int x, int y, const std::string& text, uint32_t color = 0xFFDCDCDC)
    {
        if (color == 0xFFDCDCDCu) {
            std::ostringstream oss;
            oss << windowId << "|" << x << "|" << y << "|" << text;
            publish(MsgType::MT_DrawTextAt, oss.str());
            return;
        }

        std::ostringstream oss;
        oss << windowId << "|" << x << "|" << y << "|"
            << static_cast<int>((color >> 16) & 0xFF) << "|"
            << static_cast<int>((color >> 8) & 0xFF) << "|"
            << static_cast<int>(color & 0xFF) << "|" << text;
        publish(MsgType::MT_DrawTextAtColor, oss.str());
    }

    void drawImage(uint64_t windowId, int x, int y, int w, int h, const std::string& path)
    {
        publish(MsgType::MT_DrawImage, packDrawImage(windowId, x, y, w, h, path));
    }

    bool loadPersistedDisplayOptions(DisplayOptionsStoreData& out, std::string& err);

    std::string selectedWallpaperIdFromConfig()
    {
        std::string err;
        DisplayOptionsStoreData store;
        if (!loadPersistedDisplayOptions(store, err)) return WallpaperRegistry::DefaultWallpaper().id;
        if (!store.wallpaperId.empty()) return WallpaperRegistry::ResolveIdOrDefault(store.wallpaperId);
        return WallpaperRegistry::DefaultWallpaper().id;
    }

    DesktopThemeId selectedThemeIdFromConfig()
    {
        std::string err;
        DesktopThemeId themeId = DesktopThemeId::Classic;
        DisplayOptionsStoreData store;
        if (loadPersistedDisplayOptions(store, err)) {
            TryParseDesktopThemeId(store.desktopThemeId.c_str(), &themeId);
        }
        return themeId;
    }

    uint32_t packRgb(int r, int g, int b)
    {
        return 0xFF000000u |
            (static_cast<uint32_t>(r & 0xFF) << 16) |
            (static_cast<uint32_t>(g & 0xFF) << 8) |
            static_cast<uint32_t>(b & 0xFF);
    }

    uint32_t blendColor(uint32_t baseColor, uint32_t overlayColor, int overlayPercent)
    {
        if (overlayPercent <= 0) return baseColor;
        if (overlayPercent >= 100) return overlayColor;

        const int baseR = static_cast<int>((baseColor >> 16) & 0xFF);
        const int baseG = static_cast<int>((baseColor >> 8) & 0xFF);
        const int baseB = static_cast<int>(baseColor & 0xFF);
        const int overR = static_cast<int>((overlayColor >> 16) & 0xFF);
        const int overG = static_cast<int>((overlayColor >> 8) & 0xFF);
        const int overB = static_cast<int>(overlayColor & 0xFF);
        const int basePercent = 100 - overlayPercent;

        return packRgb(
            (baseR * basePercent + overR * overlayPercent) / 100,
            (baseG * basePercent + overG * overlayPercent) / 100,
            (baseB * basePercent + overB * overlayPercent) / 100);
    }

    bool IsSciFiThemeActive()
    {
        return GetCurrentDesktopThemeId() == DesktopThemeId::SciFi;
    }

    const DesktopTheme& DisplayOptionsTheme()
    {
        return GetCurrentDesktopTheme();
    }

    uint32_t DisplayOptionsBodyColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(27, 31, 40);
        const auto& theme = DisplayOptionsTheme();
        return blendColor(theme.taskbarBackground, theme.windowBackground, 18);
    }

    uint32_t DisplayOptionsPanelColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(22, 22, 24);
        const auto& theme = DisplayOptionsTheme();
        return blendColor(theme.windowBackground, theme.taskbarBackground, 10);
    }

    uint32_t DisplayOptionsCardColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(34, 36, 42);
        const auto& theme = DisplayOptionsTheme();
        return blendColor(theme.windowBackground, theme.taskbarBackground, 18);
    }

    uint32_t DisplayOptionsSelectedBorderColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(72, 110, 180);
        const auto& theme = DisplayOptionsTheme();
        return blendColor(theme.accent, theme.windowBorder, 28);
    }

    uint32_t DisplayOptionsHoverBorderColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(55, 65, 85);
        const auto& theme = DisplayOptionsTheme();
        return blendColor(theme.mutedAccent, theme.windowBorder, 34);
    }

    uint32_t DisplayOptionsNeutralBorderColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(84, 90, 105);
        const auto& theme = DisplayOptionsTheme();
        return blendColor(theme.taskbarBorder, theme.windowBorder, 22);
    }

    uint32_t DisplayOptionsButtonFillColor(bool active, bool enabled)
    {
        if (!IsSciFiThemeActive()) {
            if (active) return packRgb(58, 58, 58);
            if (enabled) return packRgb(48, 48, 52);
            return packRgb(34, 34, 38);
        }

        const auto& theme = DisplayOptionsTheme();
        if (active) return blendColor(theme.windowBorder, theme.accent, 18);
        if (enabled) return blendColor(theme.taskbarBackground, theme.windowBackground, 18);
        return blendColor(theme.taskbarBackground, theme.windowBackground, 8);
    }

    uint32_t DisplayOptionsButtonBorderColor(bool active, bool enabled)
    {
        if (!IsSciFiThemeActive()) {
            if (active) return packRgb(90, 118, 175);
            if (enabled) return packRgb(85, 85, 90);
            return packRgb(58, 58, 62);
        }

        const auto& theme = DisplayOptionsTheme();
        if (active) return blendColor(theme.accent, theme.windowBorder, 34);
        if (enabled) return blendColor(theme.taskbarBorder, theme.windowBorder, 28);
        return blendColor(theme.windowBorder, theme.taskbarBackground, 28);
    }

    uint32_t DisplayOptionsTextColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(220, 220, 220);
        return DisplayOptionsTheme().titleBarText;
    }

    uint32_t DisplayOptionsMutedTextColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(186, 190, 196);
        const auto& theme = DisplayOptionsTheme();
        return blendColor(theme.titleBarText, theme.taskbarBackground, 54);
    }

    uint32_t DisplayOptionsAccentColor()
    {
        if (!IsSciFiThemeActive()) return packRgb(120, 168, 230);
        return DisplayOptionsTheme().accent;
    }

    const char* desktopIconSettingName(int index)
    {
    switch (index) {
    case 0: return "Trash";
    case 1: return "File Explorer";
    case 2: return "System Settings";
    case 3: return "Use smaller folder icons";
    default: return "";
    }
}

    bool loadPersistedDisplayOptions(DisplayOptionsStoreData& out, std::string& err);

    DisplayOptionsStoreData displayOptionsFromDesktopConfig(const DesktopConfigData& cfg)
    {
    DisplayOptionsStoreData out;
    out.wallpaperId = !cfg.wallpaperId.empty()
        ? cfg.wallpaperId
        : WallpaperRegistry::IdForAssetPath(cfg.wallpaperPath);
    out.backgroundScaleMode = cfg.backgroundScaleMode.empty() ? "fill" : cfg.backgroundScaleMode;
    out.desktopThemeId = cfg.desktopThemeId.empty() ? "classic" : cfg.desktopThemeId;
    out.taskbarPosition = cfg.taskbarPosition.empty() ? "bottom" : cfg.taskbarPosition;
    out.displayMode = normalizeDisplayModeName(cfg.displayMode);
    out.displayPrimaryDisplayId = cfg.displayPrimaryDisplayId.empty() ? "display-1" : cfg.displayPrimaryDisplayId;
    out.displayArrangement = cfg.displayArrangement;
    out.displayResolution = cfg.displayResolution;
    out.timeZoneId = gxos::clocktime::NormalizeTimeZoneId(cfg.timeZoneId);
    out.use24HourTime = cfg.use24HourTime;
    out.showDesktopTrash = cfg.showDesktopTrash;
    out.showDesktopThisSystem = cfg.showDesktopThisSystem;
    out.showDesktopFileManager = cfg.showDesktopFileManager;
    out.showDesktopSystemSettings = cfg.showDesktopSystemSettings;
    out.smallLiveDesktopFolderIcons = cfg.smallLiveDesktopFolderIcons;
    out.autoArrangeDesktopIcons = cfg.autoArrangeDesktopIcons;
    return out;
    }

    void applyDisplayOptionsToDesktopConfig(const DisplayOptionsStoreData& store, DesktopConfigData& cfg)
    {
        cfg.wallpaperId = store.wallpaperId;
    cfg.backgroundScaleMode = store.backgroundScaleMode.empty() ? "fill" : store.backgroundScaleMode;
    cfg.desktopThemeId = store.desktopThemeId.empty() ? "classic" : store.desktopThemeId;
    cfg.taskbarPosition = store.taskbarPosition.empty() ? "bottom" : store.taskbarPosition;
    cfg.displayMode = normalizeDisplayModeName(store.displayMode);
    cfg.displayPrimaryDisplayId = store.displayPrimaryDisplayId.empty() ? "display-1" : store.displayPrimaryDisplayId;
    cfg.displayArrangement = store.displayArrangement;
    cfg.displayResolution = store.displayResolution;
    cfg.timeZoneId = gxos::clocktime::NormalizeTimeZoneId(store.timeZoneId);
    cfg.use24HourTime = store.use24HourTime;
    cfg.showDesktopTrash = store.showDesktopTrash;
    cfg.showDesktopThisSystem = store.showDesktopThisSystem;
    cfg.showDesktopFileManager = store.showDesktopFileManager;
    cfg.showDesktopSystemSettings = store.showDesktopSystemSettings;
    cfg.smallLiveDesktopFolderIcons = store.smallLiveDesktopFolderIcons;
    cfg.autoArrangeDesktopIcons = store.autoArrangeDesktopIcons;
    }

    bool loadPersistedDisplayOptions(DisplayOptionsStoreData& out, std::string& err)
    {
        if (DisplayOptionsStore::Load(kDisplayOptionsStorePath, out, err)) {
            return true;
        }

        DesktopConfigData cfg;
        std::string legacyErr;
        if (DesktopConfig::Load("desktop.json", cfg, legacyErr)) {
            out = displayOptionsFromDesktopConfig(cfg);
            err.clear();
            return true;
        }

        err = legacyErr;
        return false;
    }
}

uint64_t DisplayOptions::Launch()
{
    ProcessSpec spec{"displayoptions", DisplayOptions::main};
    spec.appId = "gxos.builtin.displayoptions";
    return ProcessTable::spawn(spec, {"displayoptions"});
}

void DisplayOptions::loadSelection()
{
    std::string selectedId = selectedWallpaperIdFromConfig();
    Logger::write(LogLevel::Info, std::string("DisplayOptions loaded saved background id=") + selectedId);
    const bool syntheticDualMonitor = hostedSyntheticDualMonitorEnabled();
    if (syntheticDualMonitor) {
        Logger::write(LogLevel::Info, "DisplayOptions synthetic dual-monitor preview active via GXOS_SYNTHETIC_DUAL_MONITOR=1");
    }
    const auto& gradients = WallpaperRegistry::BuiltInGradients();
    const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
    s_selectedIndex = 0;
    s_appliedIndex = 0;
    s_selectedBackgroundIndex = 0;
    s_appliedBackgroundIndex = 0;
    s_selectedGradientIndex = 0;
    s_appliedGradientIndex = 0;
    s_backgroundGalleryScrollOffset = 0;
    s_gradientGalleryScrollOffset = 0;
    s_activeTab = WallpaperRegistry::IsGradientId(selectedId) ? 1 : 0;
    s_galleryScrollbarDragging = false;
    s_galleryScrollbarDragStartY = 0;
    s_galleryScrollbarDragStartOffset = 0;
    DisplayOptionsStoreData store;
    std::string storeErr;
    if (loadPersistedDisplayOptions(store, storeErr)) {
        s_showDesktopTrash = store.showDesktopTrash;
        s_showDesktopThisSystem = store.showDesktopThisSystem;
        s_showDesktopFileManager = store.showDesktopFileManager;
        s_showDesktopSystemSettings = store.showDesktopSystemSettings;
        s_smallLiveDesktopFolderIcons = store.smallLiveDesktopFolderIcons;
        s_selectedDisplayMode = normalizeDisplayModeName(store.displayMode);
        s_appliedDisplayMode = s_selectedDisplayMode;
        s_displayPrimaryDisplayId = store.displayPrimaryDisplayId.empty() ? "display-1" : store.displayPrimaryDisplayId;
        s_appliedDisplayPrimaryDisplayId = s_displayPrimaryDisplayId;
        s_displayArrangement = store.displayArrangement;
        s_appliedDisplayArrangement = s_displayArrangement;
        s_displayResolution = store.displayResolution;
        s_selectedTimeZoneIndex = clockTimeZoneIndexFromId(store.timeZoneId);
        s_appliedTimeZoneIndex = s_selectedTimeZoneIndex;
        s_use24HourTime = store.use24HourTime;
        s_appliedUse24HourTime = s_use24HourTime;
        Logger::write(LogLevel::Info, "DisplayOptions loaded display settings");
    } else {
        s_showDesktopTrash = true;
        s_showDesktopThisSystem = true;
        s_showDesktopFileManager = true;
        s_showDesktopSystemSettings = false;
        s_smallLiveDesktopFolderIcons = true;
        s_selectedDisplayMode = syntheticDualMonitor ? "extend" : "mirror";
        s_appliedDisplayMode = s_selectedDisplayMode;
        s_displayPrimaryDisplayId = "display-1";
        s_appliedDisplayPrimaryDisplayId = "display-1";
        s_displayArrangement.clear();
        s_appliedDisplayArrangement.clear();
        s_displayResolution.clear();
        s_selectedTimeZoneIndex = 0;
        s_appliedTimeZoneIndex = 0;
        s_use24HourTime = false;
        s_appliedUse24HourTime = false;
        Logger::write(LogLevel::Info, "DisplayOptions display settings defaulted");
    }
    if (s_displayResolution.empty()) {
        s_displayResolution = syntheticDualMonitor
            ? std::to_string(kSyntheticTestMonitorWidth) + "x" + std::to_string(kSyntheticTestMonitorHeight)
            : "1024x768";
    }
    if (syntheticDualMonitor && !hasSyntheticDualMonitorArrangement(parseDisplayArrangement(s_displayArrangement))) {
        s_selectedDisplayMode = "extend";
        s_appliedDisplayMode = s_selectedDisplayMode;
        s_displayArrangement = serializeDisplayArrangement(
            makeSyntheticDualMonitorDesktop(nullptr, kSyntheticTestMonitorWidth, kSyntheticTestMonitorHeight, 0).monitors);
        s_appliedDisplayArrangement = s_displayArrangement;
    } else if (s_displayArrangement.empty()) {
        int displayW = 1024;
        int displayH = 768;
        const size_t sep = s_displayResolution.find('x');
        if (sep != std::string::npos) {
            try {
                displayW = std::max(1, std::stoi(s_displayResolution.substr(0, sep)));
                displayH = std::max(1, std::stoi(s_displayResolution.substr(sep + 1)));
            } catch (...) {
                displayW = 1024;
                displayH = 768;
            }
        }
        std::vector<DisplayMonitorDescriptor> monitors;
        monitors.push_back(makeDisplayMonitor(
            s_displayPrimaryDisplayId.empty() ? "display-1" : s_displayPrimaryDisplayId,
            syntheticDualMonitor ? "Synthetic Display 1" : "Display 1",
            0,
            0,
            displayW,
            displayH,
            nullptr,
            0,
            true,
            true));
        if (normalizeDisplayModeName(s_selectedDisplayMode) == "extend") {
            monitors.push_back(makeDisplayMonitor(
                "display-2",
                syntheticDualMonitor ? "Synthetic Display 2" : "Display 2",
                displayW,
                0,
                displayW,
                displayH,
                nullptr,
                0,
                true,
                false));
        }
        s_displayArrangement = serializeDisplayArrangement(monitors);
    }
    s_appliedDisplayPrimaryDisplayId = s_displayPrimaryDisplayId;
    s_appliedDisplayArrangement = s_displayArrangement;
    s_displayStatus.clear();
    DisplayConfigurationResponse actualResponse{};
    if (queryActiveDisplayConfiguration(actualResponse)) {
        reloadDisplaySelectionFromResponse(actualResponse);
        Logger::write(LogLevel::Info, "DisplayOptions loaded active state through DisplayConfigurationService");
    }
    s_selectedThemeId = selectedThemeIdFromConfig();
    s_appliedThemeId = s_selectedThemeId;
    for (size_t i = 0; i < wallpapers.size(); ++i) {
        if (wallpapers[i].id == selectedId) {
            s_selectedBackgroundIndex = static_cast<int>(i);
            s_appliedBackgroundIndex = static_cast<int>(i);
            break;
        }
    }
    for (size_t i = 0; i < gradients.size(); ++i) {
        if (gradients[i].id == selectedId) {
            s_selectedGradientIndex = static_cast<int>(i);
            s_appliedGradientIndex = static_cast<int>(i);
            break;
        }
    }
    clampSelectionToCurrentTab();
    clampActiveScrollOffset();
}

int DisplayOptions::main(int, char**)
{
    Logger::write(LogLevel::Info, "DisplayOptions starting");
    ++s_windowGeneration;
    s_displayRequestPending = false;
    s_windowId = 0;
    s_windowW = kDefaultWindowW;
    s_windowH = kDefaultWindowH;
    s_mouseX = 0;
    s_mouseY = 0;
    s_mouseDown = false;
    loadSelection();

    ipc::Bus::ensure("gui.input");
    ipc::Bus::ensure("gui.output");

    std::ostringstream create;
    create << "Display Options|" << s_windowW << "|" << s_windowH;
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
        case MsgType::MT_Resize: {
            std::istringstream iss(payload);
            std::string windowId;
            std::string widthS;
            std::string heightS;
            std::getline(iss, windowId, '|');
            std::getline(iss, widthS, '|');
            std::getline(iss, heightS, '|');
            try {
                if (s_windowId != 0 && std::stoull(windowId) == s_windowId) {
                    s_windowW = std::max(1, std::stoi(widthS));
                    s_windowH = std::max(1, std::stoi(heightS));
                    s_galleryScrollbarDragging = false;
                    clampActiveScrollOffset();
                    if (s_activeTab == 0 || s_activeTab == 1) {
                        ensureActiveSelectionVisible();
                    }
                    render();
                }
            } catch (...) {
            }
            break;
        }
        case MsgType::MT_InputMouse: {
            std::istringstream iss(payload);
            std::string xs, ys, btns;
            std::string action;
            std::string modifiers;
            std::string windowId;
            std::getline(iss, xs, '|');
            std::getline(iss, ys, '|');
            std::getline(iss, btns, '|');
            std::getline(iss, action, '|');
            std::getline(iss, modifiers, '|');
            std::getline(iss, windowId, '|');
            (void)modifiers;
            (void)windowId;
            try {
                int x = std::stoi(xs);
                int y = std::stoi(ys);
                int buttons = std::stoi(btns);

                if (!action.empty() && action.rfind("wheel", 0) == 0) {
                    int wheelDelta = 0;
                    if (action == "wheel" || action == "wheelup") {
                        wheelDelta = 1;
                    } else if (action == "wheeldown") {
                        wheelDelta = -1;
                    } else {
                        size_t colon = action.find(':');
                        if (colon != std::string::npos && colon + 1 < action.size()) {
                            wheelDelta = std::stoi(action.substr(colon + 1));
                        }
                    }

                    if (wheelDelta != 0 && (s_activeTab == 0 || s_activeTab == 1)) {
                        const int itemCount = activeGalleryItemCount();
                        const GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, itemCount);
                        const bool inGallery = hit(x, y, layout.galleryX, layout.galleryY, layout.galleryW, layout.galleryH);
                        const bool onScrollbar = layout.showScrollbar && hit(x, y, layout.scrollbarX, layout.galleryY, kGalleryScrollBarW, layout.galleryH);
                        if (inGallery || onScrollbar) {
                            int& scroll = activeGalleryScrollOffset();
                            const int previousOffset = scroll;
                            scroll = std::max(0, std::min(scroll - wheelDelta, layout.maxScroll));
                            if (scroll != previousOffset) {
                                render();
                            }
                        }
                    }
                    break;
                }

                if (!action.empty() && action == "move" && s_galleryScrollbarDragging && (s_activeTab == 0 || s_activeTab == 1)) {
                    const int itemCount = activeGalleryItemCount();
                    const GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, itemCount);
                    if (layout.showScrollbar) {
                        const int trackTravel = std::max(1, layout.galleryH - std::max(kMinScrollbarThumbH, (layout.visibleRows * layout.galleryH) / std::max(1, layout.rowCount)));
                        int nextOffset = s_galleryScrollbarDragStartOffset + ((y - s_galleryScrollbarDragStartY) * layout.maxScroll) / trackTravel;
                        nextOffset = std::max(0, std::min(nextOffset, layout.maxScroll));
                        int& scroll = activeGalleryScrollOffset();
                        if (nextOffset != scroll) {
                            scroll = nextOffset;
                            render();
                        }
                    }
                    break;
                }

                bool wasDown = s_mouseDown;
                s_mouseX = x;
                s_mouseY = y;
                s_mouseDown = (buttons & 1) != 0;
                handleMouseMove(x, y);
                if (s_mouseDown && !wasDown) {
                    handleMouseDown(x, y);
                    uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                    int clickIndex = s_activeTab == 0 ? s_selectedBackgroundIndex : (s_activeTab == 1 ? (1000 + s_selectedGradientIndex) : -2000);
                    if (clickIndex == lastClickIndex && (now - lastClickTime) < 500) {
                        handleDoubleClick(x, y);
                        lastClickTime = 0;
                        lastClickIndex = -1;
                    } else {
                        lastClickTime = now;
                        lastClickIndex = clickIndex;
                    }
                } else if (!s_mouseDown && wasDown) {
                    handleMouseUp(x, y);
                }
            } catch (...) {
            }
            break;
        }
        case MsgType::MT_InputKey: {
            std::istringstream iss(payload);
            std::string keyS;
            std::string action;
            std::getline(iss, keyS, '|');
            std::getline(iss, action);
            try {
                if (!action.empty() && action == "down") {
                    const uint32_t key = static_cast<uint32_t>(std::stoul(keyS));
                    if (handleGalleryKey(key)) {
                        break;
                    }
                }
            } catch (...) {
            }
            break;
        }
        case MsgType::MT_Close:
            ++s_windowGeneration;
            s_displayRequestPending = false;
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
    drawColorRect(s_windowId, 0, 0, s_windowW, s_windowH, DisplayOptionsBodyColor());

    drawButton(kBackgroundTabX, kTabY, kTabW, kTabH, "Backgrounds", s_activeTab == 0, true);
    drawButton(kDesktopIconTabX, kTabY, kTabW, kTabH, "Desktop Icons", s_activeTab == 2, true);
    drawButton(kGradientTabX, kTabY, kTabW, kTabH, "Gradients", s_activeTab == 1, true);
    drawButton(kThemeTabX, kTabY, kTabW, kTabH, "Theme", s_activeTab == 3, true);
    drawButton(kRegionTimeTabX, kTabY, kTabW, kTabH, "Region/Time", s_activeTab == 4, true);
    drawButton(kDisplayTabX, kTabY, kTabW, kTabH, "Display", s_activeTab == 5, true);
    drawText(s_windowId, 26, 72,
        s_activeTab == 2 ? "Choose desktop icons and folder icon size:"
        : (s_activeTab == 0 ? "Select a background from the gallery:"
        : (s_activeTab == 1 ? "Select a gradient from the gallery:"
        : (s_activeTab == 3 ? "Choose a desktop theme. Classic is default; Sci Fi is opt-in."
        : (s_activeTab == 4 ? "Choose your region and clock format."
        : "Prepare the monitor layout model for future multi-display support.")))),
        DisplayOptionsTextColor());
    drawColorRect(s_windowId, 20, 92, std::max(1, s_windowW - 40), std::max(1, s_windowH - 112), DisplayOptionsPanelColor());

    if (s_activeTab == 0 || s_activeTab == 1) {
        const bool showWallpapers = s_activeTab == 0;
        const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        const int itemCount = showWallpapers ? static_cast<int>(wallpapers.size()) : static_cast<int>(gradients.size());
        GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, itemCount);
        int& scrollOffset = activeGalleryScrollOffset();
        scrollOffset = std::max(0, std::min(scrollOffset, layout.maxScroll));
        const int visibleRows = layout.visibleRows;
        const int startRow = scrollOffset;
        const int endRow = startRow + visibleRows;

        for (int i = 0; i < itemCount; ++i) {
            const int row = layout.columns > 0 ? i / layout.columns : 0;
            if (row < startRow || row >= endRow) continue;
            const int col = layout.columns > 0 ? i % layout.columns : 0;
            const int x = layout.galleryX + col * (kTileW + kGapX);
            const int y = layout.galleryY + (row - startRow) * (kTileH + kGapY);
            const bool hover = hit(s_mouseX, s_mouseY, x, y, kTileW, kTileH);
            if (showWallpapers) {
                drawWallpaperTile(
                    i,
                    x,
                    y,
                    hover,
                    i == s_selectedBackgroundIndex,
                    i == s_appliedBackgroundIndex);
            } else {
                drawGradientTile(
                    i,
                    x,
                    y,
                    hover,
                    i == s_selectedGradientIndex,
                    i == s_appliedGradientIndex);
            }
        }

        if (layout.showScrollbar) {
            drawColorRect(s_windowId, layout.scrollbarX, layout.galleryY, kGalleryScrollBarW, layout.galleryH, DisplayOptionsPanelColor());
            const int thumbH = std::max(kMinScrollbarThumbH, (layout.visibleRows * layout.galleryH) / std::max(1, layout.rowCount));
            const int thumbTravel = std::max(1, layout.galleryH - thumbH);
            const int thumbY = layout.galleryY + ((thumbTravel * scrollOffset) / std::max(1, layout.maxScroll));
            drawColorRect(s_windowId, layout.scrollbarX, thumbY, kGalleryScrollBarW, thumbH, DisplayOptionsAccentColor());
        }
    } else if (s_activeTab == 3) {
        drawThemeTab();
    } else if (s_activeTab == 4) {
        drawRegionTimeTab();
    } else if (s_activeTab == 5) {
        drawDisplayTab();
    } else {
        drawDesktopIconsTab();
    }

    if (s_activeTab == 0 || s_activeTab == 1) {
        GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, activeGalleryItemCount());
        drawButton(kSelectButtonX, layout.buttonY, kButtonW, kButtonH, s_activeTab == 0 ? "Select Background" : "Apply Gradient", false, true);
        drawButton(kSelectButtonX + 200, kButtonY, kButtonW, kButtonH, "Choose Color", false, false);
        drawButton(kSelectButtonX + 400, kButtonY, kButtonW, kButtonH, "Visual Effects", false, false);
    } else if (s_activeTab == 2) {
        drawText(s_windowId, 26, kButtonY + 10, "Changes are saved immediately.", DisplayOptionsMutedTextColor());
    } else if (s_activeTab == 3) {
        drawText(s_windowId, 26, kButtonY + 10, "Selecting a theme saves immediately and reloads the compositor.", DisplayOptionsMutedTextColor());
    } else if (s_activeTab == 4) {
        drawText(s_windowId, 26, kButtonY + 10, "Changes save immediately and apply to the clock display.", DisplayOptionsMutedTextColor());
    } else if (s_activeTab == 5) {
        drawText(s_windowId, 26, kButtonY + 10, "Display layout edits apply transactionally; Cancel keeps the active layout.", DisplayOptionsMutedTextColor());
    }
}

void DisplayOptions::drawButton(int x, int y, int w, int h, const std::string& text, bool active, bool enabled)
{
    const uint32_t fill = DisplayOptionsButtonFillColor(active, enabled);
    const uint32_t border = DisplayOptionsButtonBorderColor(active, enabled);
    const uint32_t topBorder = blendColor(border, DisplayOptionsTextColor(), 8);
    const uint32_t bottomBorder = blendColor(border, DisplayOptionsPanelColor(), 18);

    drawColorRect(s_windowId, x, y, w, h, fill);
    drawColorRect(s_windowId, x, y, w, 1, topBorder);
    drawColorRect(s_windowId, x, y + h - 1, w, 1, bottomBorder);
    drawColorRect(s_windowId, x, y, 1, h, topBorder);
    drawColorRect(s_windowId, x + w - 1, y, 1, h, bottomBorder);
    drawText(s_windowId, x + 16, y + 14, enabled ? text : text + " (soon)", enabled ? DisplayOptionsTextColor() : DisplayOptionsMutedTextColor());
}

void DisplayOptions::drawCheckbox(int x, int y, const std::string& text, bool checked, bool hover)
{
    int box = kDesktopIconCheckboxSize;
    if (hover) {
        const uint32_t hoverFill = IsSciFiThemeActive()
            ? blendColor(DisplayOptionsCardColor(), DisplayOptionsAccentColor(), 10)
            : packRgb(38, 46, 62);
        drawColorRect(s_windowId, x - 8, y - 8, 320, 34, hoverFill);
    }
    const uint32_t boxFill = checked ? DisplayOptionsAccentColor() : (IsSciFiThemeActive() ? blendColor(DisplayOptionsCardColor(), DisplayOptionsNeutralBorderColor(), 24) : packRgb(30, 30, 34));
    const uint32_t boxTop = IsSciFiThemeActive() ? blendColor(DisplayOptionsAccentColor(), DisplayOptionsTextColor(), 16) : packRgb(130, 135, 150);
    const uint32_t boxBottom = IsSciFiThemeActive() ? blendColor(DisplayOptionsPanelColor(), DisplayOptionsNeutralBorderColor(), 30) : packRgb(85, 90, 110);
    drawColorRect(s_windowId, x, y, box, box, boxFill);
    drawColorRect(s_windowId, x, y, box, 1, boxTop);
    drawColorRect(s_windowId, x, y + box - 1, box, 1, boxBottom);
    drawColorRect(s_windowId, x, y, 1, box, boxTop);
    drawColorRect(s_windowId, x + box - 1, y, 1, box, boxBottom);
    if (checked) drawText(s_windowId, x + 4, y + 3, "x", DisplayOptionsTextColor());
    drawText(s_windowId, x + box + 12, y + 4, text, DisplayOptionsTextColor());
}

void DisplayOptions::drawDesktopIconsTab()
{
    const bool states[] = {
        s_showDesktopTrash,
        s_showDesktopThisSystem || s_showDesktopFileManager,
        s_showDesktopSystemSettings,
        s_smallLiveDesktopFolderIcons
    };
    for (int i = 0; i < 4; ++i) {
        int y = kDesktopIconsY + i * kDesktopIconRowH;
        bool hover = hit(s_mouseX, s_mouseY, kDesktopIconsX - 8, y - 8, 320, 34);
        drawCheckbox(kDesktopIconsX, y, desktopIconSettingName(i), states[i], hover);
    }
}

void DisplayOptions::drawWallpaperTile(int index, int x, int y, bool hover, bool selected, bool applied)
{
    const auto& entry = WallpaperRegistry::BuiltInWallpapers()[static_cast<size_t>(index)];
    if (selected) drawColorRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, DisplayOptionsSelectedBorderColor());
    else if (hover) drawColorRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, DisplayOptionsHoverBorderColor());
    drawColorRect(s_windowId, x, y, kTileW, kTileH, DisplayOptionsCardColor());
    drawColorRect(s_windowId, x + 6, y + 8, kThumbW, kThumbH, IsSciFiThemeActive() ? blendColor(DisplayOptionsCardColor(), DisplayOptionsPanelColor(), 20) : packRgb(18, 18, 20));
    std::string thumbnailPath = entry.thumbnailPath.empty() ? entry.fullImagePath : entry.thumbnailPath;
    Logger::write(LogLevel::Info, std::string("DisplayOptions thumbnail id=") + entry.id + " path=" + thumbnailPath);
    drawImage(s_windowId, x + 6, y + 8, kThumbW, kThumbH, thumbnailPath);
    drawText(s_windowId, x + 8, y + 78, entry.displayName + (applied ? " *" : ""), (applied && IsSciFiThemeActive()) ? DisplayOptionsAccentColor() : DisplayOptionsTextColor());
}

void DisplayOptions::drawThemeTab()
{
    auto drawThemeOption = [&](int x, int y, DesktopThemeId id, const DesktopTheme& theme, const char* description, const char* feature1, const char* feature2, const char* feature3) {
        const bool selected = (s_selectedThemeId == id);
        const bool applied = (s_appliedThemeId == id);
        if (selected) drawColorRect(s_windowId, x - 4, y - 4, kThemeOptionW + 8, kThemeOptionH + 8, DisplayOptionsSelectedBorderColor());
        drawColorRect(s_windowId, x, y, kThemeOptionW, kThemeOptionH, DisplayOptionsCardColor());
        drawColorRect(s_windowId, x, y, kThemeOptionW, 1, IsSciFiThemeActive() ? blendColor(DisplayOptionsNeutralBorderColor(), DisplayOptionsTextColor(), 10) : packRgb(84, 90, 105));
        drawColorRect(s_windowId, x, y + kThemeOptionH - 1, kThemeOptionW, 1, IsSciFiThemeActive() ? blendColor(DisplayOptionsNeutralBorderColor(), DisplayOptionsPanelColor(), 24) : packRgb(64, 68, 78));
        drawColorRect(s_windowId, x, y, 1, kThemeOptionH, IsSciFiThemeActive() ? blendColor(DisplayOptionsNeutralBorderColor(), DisplayOptionsTextColor(), 8) : packRgb(84, 90, 105));
        drawColorRect(s_windowId, x + kThemeOptionW - 1, y, 1, kThemeOptionH, IsSciFiThemeActive() ? blendColor(DisplayOptionsNeutralBorderColor(), DisplayOptionsPanelColor(), 32) : packRgb(48, 52, 60));
        drawColorRect(s_windowId, x + 12, y + 12, 18, 18, theme.accent);
        if (selected) drawText(s_windowId, x + 16, y + 10, "x", IsSciFiThemeActive() ? DisplayOptionsAccentColor() : DisplayOptionsTextColor());
        drawText(s_windowId, x + 42, y + 10, std::string(theme.displayName) + (applied ? " *" : ""), (applied && IsSciFiThemeActive()) ? DisplayOptionsAccentColor() : DisplayOptionsTextColor());
        drawText(s_windowId, x + 42, y + 28, description, DisplayOptionsMutedTextColor());
        drawText(s_windowId, x + 42, y + 46, feature1, DisplayOptionsMutedTextColor());
        drawText(s_windowId, x + 42, y + 60, feature2, DisplayOptionsMutedTextColor());
        drawText(s_windowId, x + 42, y + 74, feature3, DisplayOptionsMutedTextColor());
    };

    drawThemeOption(
        kThemeOptionX,
        kThemeOptionY,
        DesktopThemeId::Classic,
        GetDesktopTheme(DesktopThemeId::Classic),
        "Current guideXOS look.",
        "Legacy rectangular chrome",
        "Classic taskbar styling",
        "Minimal effects");
    drawThemeOption(
        kThemeOptionX,
        kThemeOptionY + kThemeOptionH + kThemeOptionGap,
        DesktopThemeId::SciFi,
        GetDesktopTheme(DesktopThemeId::SciFi),
        "Dark futuristic hosted UI.",
        "Rounded hosted chrome",
        "Accent highlights, shadows",
        "Dark taskbar surfaces");
}

void DisplayOptions::drawRegionTimeTab()
{
    const std::string timeZoneLabel = clockTimeZoneLabelAt(s_selectedTimeZoneIndex);
    drawText(s_windowId, 46, 116, "Region and clock format:", DisplayOptionsTextColor());
    drawButton(kRegionTimeZoneX, kRegionTimeZoneY, kRegionTimeZoneW, kRegionTimeZoneH,
        std::string("Time Zone: ") + timeZoneLabel, false, true);
    drawText(s_windowId, 46, 174, "Click the time zone button to cycle supported zones.", DisplayOptionsMutedTextColor());
    drawCheckbox(kRegionTimeUse24X, kRegionTimeUse24Y, "Use 24-hour time", s_use24HourTime, hit(s_mouseX, s_mouseY, kRegionTimeUse24X - 8, kRegionTimeUse24Y - 8, 320, 34));
    drawText(s_windowId, 46, 238,
        s_use24HourTime ? "Current format: 24-hour clock, for example 14:44." : "Current format: 12-hour clock, for example 2:44 PM.",
        DisplayOptionsMutedTextColor());
    drawText(s_windowId, 46, 266, "Pacific Time is the safe fallback if the setting is missing or invalid.", DisplayOptionsMutedTextColor());
}

namespace {
    bool parseResolutionText(const std::string& text, int& outW, int& outH)
    {
        outW = 1024;
        outH = 768;
        if (text.empty()) {
            return false;
        }

        const size_t sep = text.find('x');
        if (sep == std::string::npos) {
            return false;
        }

        try {
            outW = std::max(1, std::stoi(text.substr(0, sep)));
            outH = std::max(1, std::stoi(text.substr(sep + 1)));
            return true;
        } catch (...) {
            outW = 1024;
            outH = 768;
            return false;
        }
    }

    RequestedDisplayConfiguration requestedDisplayConfigurationFromUi()
    {
        RequestedDisplayConfiguration requested;
        requested.mode = parseDisplayModeKind(DisplayOptions::s_selectedDisplayMode);
        requested.primaryOutputId = DisplayOptions::s_displayPrimaryDisplayId.empty()
            ? "display-1" : DisplayOptions::s_displayPrimaryDisplayId;
        requested.arrangement = parseDisplayArrangement(DisplayOptions::s_displayArrangement);
        for (const auto& monitor : requested.arrangement) {
            if (monitor.enabled) requested.enabledOutputIds.push_back(monitor.id);
        }
        return requested;
    }

    void copyDisplayContractText(char* destination, size_t capacity, const std::string& value)
    {
        if (destination == nullptr || capacity == 0) return;
        std::memset(destination, 0, capacity);
        const size_t count = std::min(capacity - 1, value.size());
        if (count != 0) std::memcpy(destination, value.data(), count);
    }

    DisplayConfigurationCommand displayConfigurationCommandFromUi(uint64_t requestId, uint32_t flags)
    {
        DisplayConfigurationCommand command{};
        command.version = kDisplayConfigurationContractVersion;
        command.structureSize = sizeof(DisplayConfigurationCommand);
        command.requestId = requestId;
        command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
        command.flags = flags;
        const RequestedDisplayConfiguration requested = requestedDisplayConfigurationFromUi();
        command.requestedConfiguration.mode = requested.mode == DisplayModeKind::Extend
            ? static_cast<uint32_t>(DisplayConfigurationMode::Extend)
            : static_cast<uint32_t>(DisplayConfigurationMode::Mirror);
        copyDisplayContractText(command.requestedConfiguration.primaryOutputId,
                                sizeof(command.requestedConfiguration.primaryOutputId),
                                requested.primaryOutputId);

        std::vector<DisplayMonitorDescriptor> monitors = requested.arrangement;
        if (monitors.empty()) {
            const DetectedDisplayInventory detected = Compositor::detectedDisplayInventory();
            monitors = detected.monitors;
        }
        command.requestedConfiguration.outputCount = static_cast<uint32_t>(std::min<size_t>(
            kDisplayConfigurationMaxOutputs, monitors.size()));
        for (uint32_t i = 0; i < command.requestedConfiguration.outputCount; ++i) {
            const DisplayMonitorDescriptor& monitor = monitors[i];
            DisplayConfigurationOutput& output = command.requestedConfiguration.outputs[i];
            copyDisplayContractText(output.stableId, sizeof(output.stableId), monitor.id);
            copyDisplayContractText(output.modeId, sizeof(output.modeId), monitor.modeId);
            if (output.modeId[0] == '\0' && monitor.width > 0 && monitor.height > 0) {
                const DetectedDisplayInventory detected = Compositor::detectedDisplayInventory();
                const std::vector<DisplayMode> catalog = displayModeCatalogForInventory(detected);
                if (const DisplayMode* mode = findDisplayModeByDimensions(catalog, monitor.width, monitor.height)) {
                    copyDisplayContractText(output.modeId, sizeof(output.modeId), mode->id);
                }
            }
            copyDisplayContractText(output.backendType, sizeof(output.backendType), monitor.sourceType.empty() ? "virtio-gpu" : monitor.sourceType);
            copyDisplayContractText(output.backendDeviceId, sizeof(output.backendDeviceId), monitor.backendId);
            output.scanoutId = monitor.scanoutId;
            output.logicalOrdinal = i + 1u;
            copyDisplayContractText(output.stableName, sizeof(output.stableName), monitor.name);
            output.virtualX = monitor.virtualX;
            output.virtualY = monitor.virtualY;
            output.width = monitor.width;
            output.height = monitor.height;
            output.enabled = monitor.enabled ? 1u : 0u;
            output.primary = monitor.id == requested.primaryOutputId ? 1u : (monitor.primary ? 1u : 0u);
        }
        return command;
    }

    void reloadDisplaySelectionFromResponse(const DisplayConfigurationResponse& response)
    {
        const DisplayConfigurationSnapshot& active = response.activeConfiguration;
        DisplayOptions::s_displayStatus.clear();
        DisplayOptions::s_selectedDisplayMode = active.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend)
            ? "extend" : "mirror";
        DisplayOptions::s_appliedDisplayMode = DisplayOptions::s_selectedDisplayMode;
        std::string primary(active.primaryOutputId);
        if (primary.empty()) primary = "display-1";
        DisplayOptions::s_displayPrimaryDisplayId = primary;
        DisplayOptions::s_appliedDisplayPrimaryDisplayId = primary;
        std::vector<DisplayMonitorDescriptor> monitors;
        const uint32_t outputCount = std::min(active.outputCount, kDisplayConfigurationMaxOutputs);
        for (uint32_t i = 0; i < outputCount; ++i) {
            const DisplayConfigurationOutput& source = active.outputs[i];
            DisplayMonitorDescriptor monitor = makeDisplayMonitor(
                std::string(source.stableId),
                std::string(source.stableId),
                source.virtualX,
                source.virtualY,
                source.width,
                source.height,
                nullptr,
                0,
                source.enabled != 0,
                source.primary != 0);
            monitor.modeId = std::string(source.modeId);
            monitors.push_back(monitor);
        }
        if (!monitors.empty()) {
            DisplayOptions::s_displayArrangement = serializeDisplayArrangement(monitors);
            DisplayOptions::s_appliedDisplayArrangement = DisplayOptions::s_displayArrangement;
        }
        if (active.virtualDesktopWidth > 0 && active.virtualDesktopHeight > 0) {
            DisplayOptions::s_displayResolution = std::to_string(active.virtualDesktopWidth) + "x" + std::to_string(active.virtualDesktopHeight);
        }
    }

    bool queryActiveDisplayConfiguration(DisplayConfigurationResponse& response)
    {
        DisplayConfigurationCommand command{};
        command.version = kDisplayConfigurationContractVersion;
        command.structureSize = sizeof(DisplayConfigurationCommand);
        command.requestId = DisplayConfigurationService::nextRequestId();
        command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::QueryActiveConfiguration);
        if (!DisplayConfigurationService::submit(command, response) ||
            response.requestId != command.requestId || response.commandType != command.commandType) {
            return false;
        }
        return response.success != 0u;
    }

    void closeDisplayOptionsWindow()
    {
        if (DisplayOptions::s_windowId == 0) return;
        ipc::Message close;
        close.type = static_cast<uint32_t>(MsgType::MT_Close);
        const std::string windowId = std::to_string(DisplayOptions::s_windowId);
        close.data.assign(windowId.begin(), windowId.end());
        ipc::Bus::publish("gui.input", std::move(close), false);
        ++DisplayOptions::s_windowGeneration;
        DisplayOptions::s_displayRequestPending = false;
    }

    std::vector<DisplayMonitorDescriptor> displayPreviewMonitors()
    {
        const DetectedDisplayInventory detected = Compositor::detectedDisplayInventory();
        if (detected.backend == "virtio-gpu" && detected.operationalOutputCount() >= 2) {
            const RequestedDisplayConfiguration requested = requestedDisplayConfigurationFromUi();
            std::string reason;
            const ActiveDisplayConfiguration active = buildActiveDisplayConfiguration(detected, requested, reason);
            if (active.valid()) {
                return active.monitors;
            }
            Logger::write(LogLevel::Warn, "DisplayOptions real output preview rejected: " + reason);
            return detected.monitors;
        }
        const bool syntheticDualMonitor = hostedSyntheticDualMonitorEnabled();

        if (syntheticDualMonitor) {
            DisplayVirtualDesktop desktop = makeSyntheticDualMonitorDesktopForMode(
                parseDisplayModeKind(DisplayOptions::s_selectedDisplayMode),
                nullptr,
                kSyntheticTestMonitorWidth,
                kSyntheticTestMonitorHeight,
                0);
            setDisplayVirtualDesktopPrimary(desktop, DisplayOptions::s_displayPrimaryDisplayId);
            return desktop.monitors;
        }

        std::vector<DisplayMonitorDescriptor> monitors = parseDisplayArrangement(DisplayOptions::s_displayArrangement);
        monitors.erase(
            std::remove_if(monitors.begin(), monitors.end(), [](const DisplayMonitorDescriptor& monitor) {
                return !monitor.isActive();
            }),
            monitors.end());
        bool hasPrimary = false;
        for (auto& monitor : monitors) {
            if (!DisplayOptions::s_displayPrimaryDisplayId.empty()) {
                monitor.primary = monitor.id == DisplayOptions::s_displayPrimaryDisplayId;
                if (monitor.primary) hasPrimary = true;
            } else if (monitor.primary) {
                hasPrimary = true;
            }
        }
        if (!monitors.empty()) {
            if (!hasPrimary && !monitors.empty()) {
                monitors.front().primary = true;
            }
            return monitors;
        }

        const bool extendSelected = normalizeDisplayModeName(DisplayOptions::s_selectedDisplayMode) == "extend";
        int monitorW = syntheticDualMonitor ? kSyntheticTestMonitorWidth : 1024;
        int monitorH = syntheticDualMonitor ? kSyntheticTestMonitorHeight : 768;
        parseResolutionText(DisplayOptions::s_displayResolution, monitorW, monitorH);

        DisplayMonitorDescriptor primary = makeDisplayMonitor(
            DisplayOptions::s_displayPrimaryDisplayId.empty() ? "display-1" : DisplayOptions::s_displayPrimaryDisplayId,
            syntheticDualMonitor ? "Synthetic Display 1" : "Display 1",
            0,
            0,
            monitorW,
            monitorH,
            nullptr,
            0,
            true,
            true);
        monitors.push_back(primary);

        if (extendSelected) {
            DisplayMonitorDescriptor secondary = makeDisplayMonitor(
                "display-2",
                syntheticDualMonitor ? "Synthetic Display 2" : "Display 2",
                monitorW,
                0,
                monitorW,
                monitorH,
                nullptr,
                0,
                true,
                false);
            monitors.push_back(secondary);
        }

        return monitors;
    }

    bool cycleVirtioGpuResolution(size_t index)
    {
        const DetectedDisplayInventory detected = Compositor::detectedDisplayInventory();
        if (detected.backend != "virtio-gpu" || !detected.qemuOnly || !detected.backendGateActive) return false;
        std::vector<DisplayMonitorDescriptor> arrangement = parseDisplayArrangement(DisplayOptions::s_displayArrangement);
        if (index >= arrangement.size()) return false;
        const std::vector<DisplayMode> catalog = displayModeCatalogForInventory(detected);
        if (catalog.empty()) return false;

        size_t current = 0;
        for (size_t i = 0; i < catalog.size(); ++i) {
            if ((!arrangement[index].modeId.empty() && catalog[i].id == arrangement[index].modeId) ||
                (arrangement[index].modeId.empty() && catalog[i].width == arrangement[index].width && catalog[i].height == arrangement[index].height)) {
                current = i;
                break;
            }
        }
        const DisplayMode& next = catalog[(current + 1u) % catalog.size()];
        arrangement[index].modeId = next.id;
        arrangement[index].width = next.width;
        arrangement[index].height = next.height;
        arrangement[index].assignedWidth = next.width;
        arrangement[index].assignedHeight = next.height;
        DisplayOptions::s_displayArrangement = serializeDisplayArrangement(arrangement);
        DisplayOptions::s_displayResolution = std::to_string(next.width) + "x" + std::to_string(next.height);
        DisplayOptions::s_displayStatus = std::string("Requested logical resolution ") + next.label() + ". Click Apply to rebuild resources.";
        return true;
    }

    void drawDisplayMonitorCard(int index, const DisplayMonitorDescriptor& monitor, int x, int y, bool primary, bool selected)
    {
        const uint64_t windowId = DisplayOptions::s_windowId;
        const bool enabled = monitor.operational && monitor.width > 0 && monitor.height > 0;
        const uint32_t cardFill = enabled ? DisplayOptionsCardColor() : blendColor(DisplayOptionsCardColor(), DisplayOptionsPanelColor(), 42);
        const uint32_t borderColor = primary ? DisplayOptionsSelectedBorderColor()
            : selected ? DisplayOptionsHoverBorderColor()
            : DisplayOptionsNeutralBorderColor();
        drawColorRect(windowId, x - 3, y - 3, kDisplayCardW + 6, kDisplayCardH + 6, borderColor);
        drawColorRect(windowId, x, y, kDisplayCardW, kDisplayCardH, cardFill);
        drawText(windowId, x + 12, y + 12, std::string("[") + std::to_string(index + 1) + "] " + (monitor.name.empty() ? monitor.id : monitor.name), DisplayOptionsTextColor());
        drawText(windowId, x + 12, y + 40, std::string("Resolution: ") + std::to_string(monitor.width) + " x " + std::to_string(monitor.height), DisplayOptionsAccentColor());
        drawText(windowId, x + 12, y + 58, std::string("Output: ") + (monitor.outputId.empty() ? monitor.id : monitor.outputId), DisplayOptionsMutedTextColor());
        drawText(windowId, x + 12, y + 76, primary ? "Primary display" : "Secondary display", primary ? DisplayOptionsAccentColor() : DisplayOptionsMutedTextColor());
        drawText(windowId, x + 12, y + 94, enabled ? "Operational" : "Unavailable", enabled ? DisplayOptionsAccentColor() : DisplayOptionsMutedTextColor());
        drawText(windowId, x + 12, y + 112,
            monitor.sourceType == "virtio-gpu" ? "Click Resolution to cycle modes" :
            (monitor.connectorEnabled ? "Connector state enabled" : "Connector state unavailable/disabled"),
            monitor.connectorEnabled ? DisplayOptionsMutedTextColor() : DisplayOptionsAccentColor());
    }
}

void DisplayOptions::drawDisplayTab()
{
    const std::vector<DisplayMonitorDescriptor> monitors = displayPreviewMonitors();
    const DetectedDisplayInventory detected = Compositor::detectedDisplayInventory();
    DisplayVirtualDesktop desktop;
    desktop.mode = parseDisplayModeKind(s_selectedDisplayMode);
    desktop.monitors = monitors;
    desktop.recomputeBounds();

    drawText(s_windowId, 46, 116, "Display mode and monitor layout:", DisplayOptionsTextColor());
    if (detected.backend == "virtio-gpu") {
        drawText(s_windowId, 46, 140,
            std::string("Backend: VirtIO-GPU  Outputs detected: ") + std::to_string(detected.detectedOutputCount())
                + "  operational: " + std::to_string(detected.operationalOutputCount()),
            DisplayOptionsAccentColor());
        drawText(s_windowId, 46, 158,
            std::string("Connector state is separate from usability; confirmed outputs: ")
                + std::to_string(detected.presentationConfirmedCount()),
            DisplayOptionsMutedTextColor());
    } else if (hostedSyntheticDualMonitorEnabled()) {
        const DisplayViewport viewport = makeHostedDisplayViewport(desktop, Compositor::hostedDisplayViewportIndex(), kSyntheticTestMonitorWidth, kSyntheticTestMonitorHeight);
        drawText(s_windowId, 46, 158, "Synthetic dual-monitor test mode is active; desktop.display.viewport 1/2 selects the visible hosted viewport.", DisplayOptionsMutedTextColor());
        drawText(s_windowId, 46, 176, std::string("Viewing ") + viewport.summary() + "  (one framebuffer; secondary cursor/window content is model-only unless dual-window output is enabled)", DisplayOptionsMutedTextColor());
    } else {
        drawText(s_windowId, 46, 140, "Backend: framebuffer  Outputs detected: 1  operational: 1", DisplayOptionsMutedTextColor());
    }

    const bool mirrorSelected = normalizeDisplayModeName(s_selectedDisplayMode) == "mirror";
    const bool extendSelected = normalizeDisplayModeName(s_selectedDisplayMode) == "extend";
    drawButton(kDisplaySectionX, kDisplayModeY, kDisplayModeButtonW, kDisplayModeButtonH, "Mirror Displays", mirrorSelected, true);
    drawButton(kDisplaySectionX + kDisplayModeButtonW + 12, kDisplayModeY, kDisplayModeButtonW, kDisplayModeButtonH, "Extend Displays", extendSelected, true);

    const std::string summary = std::string("Mode: ") + displayModeName(desktop.mode) +
        "  Primary: " + (desktop.primaryMonitor() ? desktop.primaryMonitor()->id : (s_displayPrimaryDisplayId.empty() ? "display-1" : s_displayPrimaryDisplayId)) +
        "  Virtual desktop: " + desktop.resolutionString();
    drawText(s_windowId, 46, 224, summary, DisplayOptionsMutedTextColor());
    drawText(s_windowId, 46, 244, desktop.detailedSummary(), DisplayOptionsMutedTextColor());
    if (detected.backend == "virtio-gpu" && detected.qemuOnly && detected.backendGateActive) {
        bool mirrorCompatible = monitors.size() < 2;
        if (monitors.size() >= 2) {
            mirrorCompatible = monitors[0].width == monitors[1].width && monitors[0].height == monitors[1].height;
        }
        drawText(s_windowId, 46, 264,
            std::string("QEMU logical scanout modes: ") + (mirrorCompatible ? "Mirror-compatible" : "Mirror requires matching resolutions"),
            mirrorCompatible ? DisplayOptionsAccentColor() : DisplayOptionsAccentColor());
        drawText(s_windowId, 46, 282, "Refresh rate: Not available    Rotation: Not available", DisplayOptionsMutedTextColor());
    }
    if (!s_displayArrangement.empty()) {
        drawText(s_windowId, 46, detected.backend == "virtio-gpu" && detected.qemuOnly && detected.backendGateActive ? 300 : 264,
            std::string("Arrangement: ") + s_displayArrangement, DisplayOptionsMutedTextColor());
    }

    const int cardY = kDisplayCardY;
    const int cardGapX = kDisplayCardGapX;
    const int cardGapY = kDisplayCardGapY;
    const int maxColumns = 2;
    for (size_t i = 0; i < monitors.size(); ++i) {
        const int column = static_cast<int>(i % maxColumns);
        const int row = static_cast<int>(i / maxColumns);
        const int cardX = kDisplaySectionX + column * (kDisplayCardW + cardGapX);
        const int cardTop = cardY + row * (kDisplayCardH + cardGapY);
        const bool primary = !monitors.empty() && (monitors[i].primary || monitors[i].id == s_displayPrimaryDisplayId);
        const bool selected = hit(s_mouseX, s_mouseY, cardX, cardTop, kDisplayCardW, kDisplayCardH);
        drawDisplayMonitorCard(static_cast<int>(i), monitors[i], cardX, cardTop, primary, selected);
    }

    drawButton(kDisplaySectionX, kButtonY, kButtonW, kButtonH, "Apply", false, true);
    drawButton(kDisplaySectionX + kButtonW + 18, kButtonY, kButtonW, kButtonH, "Cancel", false, true);
    drawButton(kDisplaySectionX + 2 * (kButtonW + 18), kButtonY, kButtonW, kButtonH, "OK", false, true);
    const std::string status = s_displayStatus.empty()
        ? (extendSelected ? "Extend uses persisted virtual origins." : "Mirror uses one logical viewport on every output.")
        : s_displayStatus;
    drawText(s_windowId, 46, kButtonY + 44, status, s_displayStatus.empty() ? DisplayOptionsMutedTextColor() : DisplayOptionsAccentColor());
}

void DisplayOptions::drawBackgroundTile(int index, int x, int y, bool hover, bool selected, bool applied)
{
    const auto& entry = WallpaperRegistry::BuiltInBackgrounds()[static_cast<size_t>(index)];
    if (selected) drawColorRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, DisplayOptionsSelectedBorderColor());
    else if (hover) drawColorRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, DisplayOptionsHoverBorderColor());
    drawColorRect(s_windowId, x, y, kTileW, kTileH, DisplayOptionsCardColor());

    if (entry.kind == BackgroundKind::Image) {
        drawColorRect(s_windowId, x + 6, y + 8, kThumbW, kThumbH, IsSciFiThemeActive() ? blendColor(DisplayOptionsCardColor(), DisplayOptionsPanelColor(), 20) : packRgb(18, 18, 20));
        std::string thumbnailPath = entry.thumbnailPath.empty() ? entry.fullImagePath : entry.thumbnailPath;
        Logger::write(LogLevel::Info, std::string("DisplayOptions thumbnail id=") + entry.id + " path=" + thumbnailPath);
        drawImage(s_windowId, x + 6, y + 8, kThumbW, kThumbH, thumbnailPath);
    } else {
        uint32_t top = entry.kind == BackgroundKind::SolidColor ? entry.solidColor : entry.topColor;
        uint32_t bottom = entry.kind == BackgroundKind::SolidColor ? entry.solidColor : entry.bottomColor;
        for (int py = 0; py < kThumbH; ++py) {
            int t = (py * 255) / (kThumbH > 1 ? kThumbH - 1 : 1);
            int r = ((((top >> 16) & 0xFF) * (255 - t)) + (((bottom >> 16) & 0xFF) * t)) / 255;
            int g = ((((top >> 8) & 0xFF) * (255 - t)) + (((bottom >> 8) & 0xFF) * t)) / 255;
            int b = (((top & 0xFF) * (255 - t)) + ((bottom & 0xFF) * t)) / 255;
            drawRect(s_windowId, x + 6, y + 8 + py, kThumbW, 1, r, g, b);
        }
        drawColorRect(s_windowId, x + 6, y + 8, kThumbW, 1, entry.accentColor);
    }

    drawText(s_windowId, x + 8, y + 78, entry.displayName + (applied ? " *" : ""), (applied && IsSciFiThemeActive()) ? DisplayOptionsAccentColor() : DisplayOptionsTextColor());
}

void DisplayOptions::drawGradientTile(int index, int x, int y, bool hover, bool selected, bool applied)
{
    const auto& entry = WallpaperRegistry::BuiltInGradients()[static_cast<size_t>(index)];
    if (selected) drawColorRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, DisplayOptionsSelectedBorderColor());
    else if (hover) drawColorRect(s_windowId, x - 4, y - 4, kTileW + 8, kTileH + 8, DisplayOptionsHoverBorderColor());
    drawColorRect(s_windowId, x, y, kTileW, kTileH, DisplayOptionsCardColor());
    for (int py = 0; py < kThumbH; ++py) {
        int t = (py * 255) / (kThumbH > 1 ? kThumbH - 1 : 1);
        int r = ((((entry.topColor >> 16) & 0xFF) * (255 - t)) + (((entry.bottomColor >> 16) & 0xFF) * t)) / 255;
        int g = ((((entry.topColor >> 8) & 0xFF) * (255 - t)) + (((entry.bottomColor >> 8) & 0xFF) * t)) / 255;
        int b = (((entry.topColor & 0xFF) * (255 - t)) + ((entry.bottomColor & 0xFF) * t)) / 255;
        drawRect(s_windowId, x + 6, y + 8 + py, kThumbW, 1, r, g, b);
    }
    drawColorRect(s_windowId, x + 6, y + 8, kThumbW, 1, entry.accentColor);
    drawText(s_windowId, x + 8, y + 78, entry.displayName + (applied ? " *" : ""), (applied && IsSciFiThemeActive()) ? DisplayOptionsAccentColor() : DisplayOptionsTextColor());
}

void DisplayOptions::handleMouseMove(int, int)
{
    if (s_galleryScrollbarDragging && (s_activeTab == 0 || s_activeTab == 1)) {
        const int itemCount = activeGalleryItemCount();
        const GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, itemCount);
        if (layout.showScrollbar) {
            const int thumbH = std::max(kMinScrollbarThumbH, (layout.visibleRows * layout.galleryH) / std::max(1, layout.rowCount));
            const int trackTravel = std::max(1, layout.galleryH - thumbH);
            int nextOffset = s_galleryScrollbarDragStartOffset + ((s_mouseY - s_galleryScrollbarDragStartY) * layout.maxScroll) / trackTravel;
            nextOffset = std::max(0, std::min(nextOffset, layout.maxScroll));
            int& scroll = activeGalleryScrollOffset();
            if (nextOffset != scroll) {
                scroll = nextOffset;
            }
        }
    }
    render();
}

void DisplayOptions::handleMouseDown(int mx, int my)
{
    if (hit(mx, my, kBackgroundTabX, kTabY, kTabW, kTabH)) {
        setActiveTabAndClamp(0);
        render();
        return;
    }
    if (hit(mx, my, kDesktopIconTabX, kTabY, kTabW, kTabH)) {
        setActiveTabAndClamp(2);
        Logger::write(LogLevel::Info, "DisplayOptions Desktop Icons tab selected");
        render();
        return;
    }
    if (hit(mx, my, kGradientTabX, kTabY, kTabW, kTabH)) {
        setActiveTabAndClamp(1);
        render();
        return;
    }
    if (hit(mx, my, kThemeTabX, kTabY, kTabW, kTabH)) {
        setActiveTabAndClamp(3);
        Logger::write(LogLevel::Info, "DisplayOptions Theme tab selected");
        render();
        return;
    }
    if (hit(mx, my, kRegionTimeTabX, kTabY, kTabW, kTabH)) {
        setActiveTabAndClamp(4);
        Logger::write(LogLevel::Info, "DisplayOptions Region/Time tab selected");
        render();
        return;
    }
    if (hit(mx, my, kDisplayTabX, kTabY, kTabW, kTabH)) {
        setActiveTabAndClamp(5);
        Logger::write(LogLevel::Info, "DisplayOptions Display tab selected");
        render();
        return;
    }

    if (s_activeTab == 2) {
        for (int i = 0; i < 4; ++i) {
            int rowY = kDesktopIconsY + i * kDesktopIconRowH;
            if (hit(mx, my, kDesktopIconsX - 8, rowY - 8, 320, 34)) {
                if (toggleDesktopIconSetting(i)) {
                    saveDesktopIconSettings();
                    render();
                }
                return;
            }
        }
        return;
    }

    if (s_activeTab == 4) {
        if (hit(mx, my, kRegionTimeZoneX, kRegionTimeZoneY, kRegionTimeZoneW, kRegionTimeZoneH)) {
            s_selectedTimeZoneIndex = (s_selectedTimeZoneIndex + 1) % static_cast<int>(gxos::clocktime::kTimeZoneOptionCount);
            applySelectedRegionTime();
            render();
            return;
        }
        if (hit(mx, my, kRegionTimeUse24X, kRegionTimeUse24Y, 320, 34)) {
            s_use24HourTime = !s_use24HourTime;
            applySelectedRegionTime();
            render();
            return;
        }
        return;
    }

    if (s_activeTab == 5) {
        if (hit(mx, my, kDisplaySectionX, kDisplayModeY, kDisplayModeButtonW, kDisplayModeButtonH)) {
            s_selectedDisplayMode = "mirror";
            render();
            return;
        }
        if (hit(mx, my, kDisplaySectionX + kDisplayModeButtonW + 12, kDisplayModeY, kDisplayModeButtonW, kDisplayModeButtonH)) {
            s_selectedDisplayMode = "extend";
            render();
            return;
        }

        const std::vector<DisplayMonitorDescriptor> monitors = displayPreviewMonitors();
        if (hit(mx, my, kDisplaySectionX, kButtonY, kButtonW, kButtonH)) {
            applySelectedDisplaySettings();
            render();
            return;
        }
        if (hit(mx, my, kDisplaySectionX + kButtonW + 18, kButtonY, kButtonW, kButtonH)) {
            cancelSelectedDisplaySettings();
            render();
            return;
        }
        if (hit(mx, my, kDisplaySectionX + 2 * (kButtonW + 18), kButtonY, kButtonW, kButtonH)) {
            applySelectedDisplaySettingsAndClose();
            return;
        }
        const int cardY = kDisplayCardY;
        for (size_t i = 0; i < monitors.size(); ++i) {
            const int column = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            const int cardX = kDisplaySectionX + column * (kDisplayCardW + kDisplayCardGapX);
            const int cardTop = cardY + row * (kDisplayCardH + kDisplayCardGapY);
            if (!hit(mx, my, cardX, cardTop, kDisplayCardW, kDisplayCardH)) {
                continue;
            }
            if (hit(mx, my, cardX + 8, cardTop + 32, kDisplayCardW - 16, 24) &&
                cycleVirtioGpuResolution(i)) {
                render();
                return;
            }
            if (!monitors[i].id.empty()) {
                s_displayPrimaryDisplayId = monitors[i].id;
                render();
            }
            return;
        }
        return;
    }

    if (s_activeTab == 3) {
        if (hit(mx, my, kThemeOptionX, kThemeOptionY, kThemeOptionW, kThemeOptionH)) {
            s_selectedThemeId = DesktopThemeId::Classic;
            applySelectedTheme();
            render();
            return;
        }
        if (hit(mx, my, kThemeOptionX, kThemeOptionY + kThemeOptionH + kThemeOptionGap, kThemeOptionW, kThemeOptionH)) {
            s_selectedThemeId = DesktopThemeId::SciFi;
            applySelectedTheme();
            render();
            return;
        }
        return;
    }

    if (s_activeTab == 1) {
        const GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, static_cast<int>(WallpaperRegistry::BuiltInGradients().size()));
        const int scrollbarHit = hitTestGalleryScrollbar(mx, my, layout);
        if (scrollbarHit == 1) {
            s_galleryScrollbarDragging = true;
            s_galleryScrollbarDragStartY = my;
            s_galleryScrollbarDragStartOffset = s_gradientGalleryScrollOffset;
            return;
        }
        if (scrollbarHit == 2 || scrollbarHit == 3) {
            int& scroll = activeGalleryScrollOffset();
            const int nextOffset = scroll + (scrollbarHit == 2 ? -layout.visibleRows : layout.visibleRows);
            scroll = std::max(0, std::min(nextOffset, layout.maxScroll));
            render();
            return;
        }
        const int hitIndex = hitTestActiveGalleryTile(mx, my, layout);
        if (hitIndex >= 0) {
            setActiveSelectionIndex(hitIndex);
            render();
        }
        return;
    }

    if (s_activeTab == 0) {
        const GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, static_cast<int>(WallpaperRegistry::BuiltInWallpapers().size()));
        const int scrollbarHit = hitTestGalleryScrollbar(mx, my, layout);
        if (scrollbarHit == 1) {
            s_galleryScrollbarDragging = true;
            s_galleryScrollbarDragStartY = my;
            s_galleryScrollbarDragStartOffset = s_backgroundGalleryScrollOffset;
            return;
        }
        if (scrollbarHit == 2 || scrollbarHit == 3) {
            int& scroll = activeGalleryScrollOffset();
            const int nextOffset = scroll + (scrollbarHit == 2 ? -layout.visibleRows : layout.visibleRows);
            scroll = std::max(0, std::min(nextOffset, layout.maxScroll));
            render();
            return;
        }
        const int hitIndex = hitTestActiveGalleryTile(mx, my, layout);
        if (hitIndex >= 0) {
            setActiveSelectionIndex(hitIndex);
            render();
        }
        return;
    }
}

void DisplayOptions::handleMouseUp(int, int)
{
    s_galleryScrollbarDragging = false;
}

void DisplayOptions::handleDoubleClick(int mx, int my)
{
    if (s_activeTab == 2 || s_activeTab == 3) return;
    if (s_activeTab == 1) {
        const GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, static_cast<int>(WallpaperRegistry::BuiltInGradients().size()));
        const int hitIndex = hitTestActiveGalleryTile(mx, my, layout);
        if (hitIndex >= 0) {
            setActiveSelectionIndex(hitIndex);
            applySelectedGradient();
            render();
        }
        return;
    }

    if (s_activeTab == 0) {
        const GalleryLayout layout = layoutForWindow(s_windowW, s_windowH, static_cast<int>(WallpaperRegistry::BuiltInWallpapers().size()));
        const int hitIndex = hitTestActiveGalleryTile(mx, my, layout);
        if (hitIndex >= 0) {
            setActiveSelectionIndex(hitIndex);
            applySelectedBackground();
            render();
        }
    }
}

bool DisplayOptions::toggleDesktopIconSetting(int index)
{
    bool* setting = nullptr;
    switch (index) {
    case 0: setting = &s_showDesktopTrash; break;
    case 1: {
        const bool enabled = !(s_showDesktopThisSystem || s_showDesktopFileManager);
        s_showDesktopThisSystem = enabled;
        s_showDesktopFileManager = enabled;
        Logger::write(LogLevel::Info, std::string("DisplayOptions Desktop Icons checkbox changed: ") + desktopIconSettingName(index) + "=" + (enabled ? "true" : "false"));
        return true;
    }
    case 2: setting = &s_showDesktopSystemSettings; break;
    case 3: setting = &s_smallLiveDesktopFolderIcons; break;
    default: return false;
    }
    *setting = !*setting;
    Logger::write(LogLevel::Info, std::string("DisplayOptions Desktop Icons checkbox changed: ") + desktopIconSettingName(index) + "=" + (*setting ? "true" : "false"));
    return true;
}

void DisplayOptions::saveDesktopIconSettings()
{
    DisplayOptionsStoreData store;
    std::string err;
    if (!loadPersistedDisplayOptions(store, err)) {
        Logger::write(LogLevel::Info, "DisplayOptions Desktop Icons save using default display settings because load failed: " + err);
    }

    store.showDesktopTrash = s_showDesktopTrash;
    const bool showFileExplorer = s_showDesktopThisSystem || s_showDesktopFileManager;
    store.showDesktopThisSystem = showFileExplorer;
    store.showDesktopFileManager = showFileExplorer;
    store.showDesktopSystemSettings = s_showDesktopSystemSettings;
    store.smallLiveDesktopFolderIcons = s_smallLiveDesktopFolderIcons;
    store.desktopThemeId = DesktopThemeIdToString(s_appliedThemeId);

    const bool storeSaved = DisplayOptionsStore::Save(kDisplayOptionsStorePath, store, err);
    if (!storeSaved) {
        Logger::write(LogLevel::Warn, "DisplayOptions display settings save failed: " + err);
    }

    DesktopConfigData cfg;
    std::string legacyErr;
    bool legacySaved = false;
    if (DesktopConfig::Load("desktop.json", cfg, legacyErr)) {
        applyDisplayOptionsToDesktopConfig(store, cfg);
        if (!DesktopConfig::Save("desktop.json", cfg, legacyErr)) {
            Logger::write(LogLevel::Warn, "DisplayOptions legacy desktop.json save failed: " + legacyErr);
        } else {
            legacySaved = true;
        }
    }

    if (storeSaved || legacySaved) {
        Logger::write(LogLevel::Info, "DisplayOptions Desktop Icons settings saved");
        publish(MsgType::MT_DesktopConfigReload, "");
    }
}

void DisplayOptions::saveClockSettings()
{
    DisplayOptionsStoreData store;
    std::string err;
    if (!loadPersistedDisplayOptions(store, err)) {
        Logger::write(LogLevel::Info, "DisplayOptions clock save using default display settings because load failed: " + err);
    }

    store.timeZoneId = gxos::clocktime::TimeZoneIdAt(static_cast<size_t>(s_selectedTimeZoneIndex));
    store.use24HourTime = s_use24HourTime;

    const bool storeSaved = DisplayOptionsStore::Save(kDisplayOptionsStorePath, store, err);
    if (!storeSaved) {
        Logger::write(LogLevel::Warn, "DisplayOptions clock settings save failed: " + err);
    }

    DesktopConfigData cfg;
    std::string legacyErr;
    bool legacySaved = false;
    if (DesktopConfig::Load("desktop.json", cfg, legacyErr)) {
        applyDisplayOptionsToDesktopConfig(store, cfg);
        if (!DesktopConfig::Save("desktop.json", cfg, legacyErr)) {
            Logger::write(LogLevel::Warn, "DisplayOptions legacy desktop.json clock save failed: " + legacyErr);
        } else {
            legacySaved = true;
        }
    }

    if (storeSaved || legacySaved) {
        s_appliedTimeZoneIndex = s_selectedTimeZoneIndex;
        s_appliedUse24HourTime = s_use24HourTime;
        Logger::write(LogLevel::Info, std::string("DisplayOptions applied clock settings timeZoneId=") +
            gxos::clocktime::TimeZoneIdAt(static_cast<size_t>(s_appliedTimeZoneIndex)) +
            " use24HourTime=" + (s_appliedUse24HourTime ? "true" : "false"));
        publish(MsgType::MT_DesktopConfigReload, "");
    }
}

void DisplayOptions::applySelectedRegionTime()
{
    saveClockSettings();
}

void DisplayOptions::applySelectedTheme()
{
    DisplayOptionsStoreData store;
    std::string err;
    if (!loadPersistedDisplayOptions(store, err)) {
        Logger::write(LogLevel::Info, "DisplayOptions theme save using default display settings because load failed: " + err);
    }

    store.desktopThemeId = DesktopThemeIdToString(s_selectedThemeId);

    const bool storeSaved = DisplayOptionsStore::Save(kDisplayOptionsStorePath, store, err);
    if (!storeSaved) {
        Logger::write(LogLevel::Warn, "DisplayOptions theme save failed: " + err);
    }

    DesktopConfigData cfg;
    std::string legacyErr;
    bool legacySaved = false;
    if (DesktopConfig::Load("desktop.json", cfg, legacyErr)) {
        applyDisplayOptionsToDesktopConfig(store, cfg);
        if (!DesktopConfig::Save("desktop.json", cfg, legacyErr)) {
            Logger::write(LogLevel::Warn, "DisplayOptions legacy desktop.json theme save failed: " + legacyErr);
        } else {
            legacySaved = true;
        }
    }

    if (storeSaved || legacySaved) {
        s_appliedThemeId = s_selectedThemeId;
        Logger::write(LogLevel::Info, std::string("DisplayOptions applied theme id=") + DesktopThemeIdToString(s_appliedThemeId));
        publish(MsgType::MT_DesktopConfigReload, "");
    }
}

bool DisplayOptions::saveDisplaySettings()
{
    DisplayOptionsStoreData store;
    std::string err;
    DisplayOptionsStoreData previousStore;
    std::string previousStoreErr;
    const bool hadPreviousStore = loadPersistedDisplayOptions(previousStore, previousStoreErr);
    if (!hadPreviousStore) {
        Logger::write(LogLevel::Info, "DisplayOptions display save using default display settings because load failed: " + previousStoreErr);
        store = DisplayOptionsStoreData{};
    } else {
        store = previousStore;
    }

    store.displayMode = normalizeDisplayModeName(s_selectedDisplayMode);
    store.displayPrimaryDisplayId = s_displayPrimaryDisplayId.empty() ? "display-1" : s_displayPrimaryDisplayId;
    store.displayArrangement = s_displayArrangement;
    store.displayResolution = s_displayResolution;

    const bool storeSaved = DisplayOptionsStore::Save(kDisplayOptionsStorePath, store, err);
    if (!storeSaved) {
        Logger::write(LogLevel::Warn, "DisplayOptions display settings save failed: " + err);
    }

    DesktopConfigData cfg;
    std::string legacyErr;
    bool legacySaved = false;
    DesktopConfigData previousCfg;
    const bool legacyRequired = DesktopConfig::Load("desktop.json", previousCfg, legacyErr);
    if (legacyRequired) {
        cfg = previousCfg;
        applyDisplayOptionsToDesktopConfig(store, cfg);
        if (!DesktopConfig::Save("desktop.json", cfg, legacyErr)) {
            Logger::write(LogLevel::Warn, "DisplayOptions legacy desktop.json display save failed: " + legacyErr);
        } else {
            legacySaved = true;
        }
    }

    const bool committed = storeSaved && (!legacyRequired || legacySaved);
    if (!committed) {
        if (storeSaved && hadPreviousStore) {
            std::string restoreStoreErr;
            DisplayOptionsStore::Save(kDisplayOptionsStorePath, previousStore, restoreStoreErr);
        }
        if (legacyRequired && legacySaved) {
            std::string restoreLegacyErr;
            DesktopConfig::Save("desktop.json", previousCfg, restoreLegacyErr);
        }
        return false;
    }

    if (committed) {
        s_appliedDisplayMode = s_selectedDisplayMode;
        Logger::write(LogLevel::Info, std::string("DisplayOptions applied display mode=") + s_appliedDisplayMode +
            " primaryDisplayId=" + s_displayPrimaryDisplayId +
            " arrangement=" + (s_displayArrangement.empty() ? "(empty)" : s_displayArrangement) +
            " resolution=" + (s_displayResolution.empty() ? "(empty)" : s_displayResolution));
        return true;
    }
    return false;
}

void DisplayOptions::applySelectedDisplayMode()
{
    applySelectedDisplaySettings();
}

void DisplayOptions::applySelectedDisplaySettings()
{
    if (hostedSyntheticDualMonitorEnabled()) {
        // Keep the persisted compact arrangement aligned with the selected
        // model-level mode. This records Mirror as two overlapping logical
        // descriptors without implying a second framebuffer.
        DisplayVirtualDesktop selectedDesktop = makeSyntheticDualMonitorDesktopForMode(
            parseDisplayModeKind(s_selectedDisplayMode),
            nullptr,
            kSyntheticTestMonitorWidth,
            kSyntheticTestMonitorHeight,
            0);
        setDisplayVirtualDesktopPrimary(selectedDesktop, s_displayPrimaryDisplayId);
        s_displayArrangement = serializeDisplayArrangement(selectedDesktop.monitors);
    }
    s_displayStatus = "Applying display configuration...";
    const uint64_t requestId = DisplayConfigurationService::nextRequestId();
    const uint64_t windowGeneration = s_windowGeneration;
    const DisplayConfigurationCommand command = displayConfigurationCommandFromUi(
        requestId,
        DisplayConfigurationFlagCommitPersistence);
    s_displayRequestId = requestId;
    s_displayRequestPending = true;
    DisplayConfigurationResponse response{};
    const bool submitted = DisplayConfigurationService::submit(command, response);
    if (!submitted && response.requestId != requestId) {
        s_displayRequestPending = false;
        s_displayStatus = "Apply failed: stale display configuration response";
        Logger::write(LogLevel::Warn, "DisplayOptions ignored stale display configuration response");
        return;
    }
    s_displayRequestPending = false;
    if (response.requestId != requestId || response.commandType != command.commandType || windowGeneration != s_windowGeneration) {
        s_displayStatus = "Apply response ignored: Display Options window is stale";
        return;
    }
    if (!response.success) {
        s_displayStatus = std::string("Apply failed [") +
            DisplayConfigurationService::resultCodeName(response.resultCode) + "]: " + response.diagnostic;
        Logger::write(LogLevel::Warn, std::string("DisplayOptions display apply failed request=") +
            std::to_string(requestId) + " code=" + DisplayConfigurationService::resultCodeName(response.resultCode));
        return;
    }
    reloadDisplaySelectionFromResponse(response);
    s_displayStatus = "Applied successfully";
    Logger::write(LogLevel::Info, std::string("DisplayOptions display apply request=") + std::to_string(requestId) + " result=success");
}

void DisplayOptions::applySelectedDisplaySettingsAndClose()
{
    applySelectedDisplaySettings();
    const DisplayConfigurationResponse result = DisplayConfigurationService::lastResult();
    if (!s_displayRequestPending && result.requestId == s_displayRequestId && result.success != 0u) {
        closeDisplayOptionsWindow();
    }
}

void DisplayOptions::cancelSelectedDisplaySettings()
{
    DisplayConfigurationResponse response{};
    if (!queryActiveDisplayConfiguration(response)) {
        s_selectedDisplayMode = s_appliedDisplayMode;
        s_displayPrimaryDisplayId = s_appliedDisplayPrimaryDisplayId;
        s_displayArrangement = s_appliedDisplayArrangement;
    } else {
        reloadDisplaySelectionFromResponse(response);
    }
    s_displayStatus = "Unapplied display changes canceled";
    closeDisplayOptionsWindow();
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

void DisplayOptions::applySelectedBackground()
{
    const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
    if (s_selectedBackgroundIndex < 0 || s_selectedBackgroundIndex >= static_cast<int>(wallpapers.size())) return;
    const WallpaperEntry& selected = wallpapers[static_cast<size_t>(s_selectedBackgroundIndex)];
    ipc::Message msg;
    msg.type = static_cast<uint32_t>(MsgType::MT_DesktopWallpaperSet);
    msg.data.assign(selected.id.begin(), selected.id.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    s_appliedBackgroundIndex = s_selectedBackgroundIndex;
    Logger::write(LogLevel::Info, std::string("DisplayOptions applied wallpaper id=") + selected.id +
        " full=" + selected.fullImagePath + " thumb=" + selected.thumbnailPath);
}

bool DisplayOptions::hit(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

} // namespace apps
} // namespace gxos
