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
int DisplayOptions::s_selectedBackgroundIndex = 0;
int DisplayOptions::s_appliedBackgroundIndex = 0;
int DisplayOptions::s_selectedGradientIndex = 0;
int DisplayOptions::s_appliedGradientIndex = 0;
DesktopThemeId DisplayOptions::s_selectedThemeId = DesktopThemeId::Classic;
DesktopThemeId DisplayOptions::s_appliedThemeId = DesktopThemeId::Classic;
int DisplayOptions::s_activeTab = 0;
int DisplayOptions::s_mouseX = 0;
int DisplayOptions::s_mouseY = 0;
bool DisplayOptions::s_mouseDown = false;
bool DisplayOptions::s_showDesktopTrash = true;
bool DisplayOptions::s_showDesktopThisSystem = true;
bool DisplayOptions::s_showDesktopFileManager = true;
bool DisplayOptions::s_showDesktopSystemSettings = false;
bool DisplayOptions::s_smallLiveDesktopFolderIcons = true;

namespace {
    const int kWindowW = 800;
    const int kWindowH = 620;
    const int kTabY = 18;
    const int kTabW = 170;
    const int kTabH = 40;
    const int kThemeTabX = 20;
    const int kBackgroundTabX = 200;
    const int kDesktopIconTabX = 380;
    const int kGradientTabX = 560;
    const int kGalleryX = 26;
    const int kGalleryY = 100;
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

    std::string selectedWallpaperIdFromConfig()
    {
        DesktopConfigData cfg;
        std::string err;
        if (!DesktopConfig::Load("desktop.json", cfg, err)) return WallpaperRegistry::DefaultWallpaper().id;
        if (!cfg.wallpaperId.empty()) return WallpaperRegistry::ResolveIdOrDefault(cfg.wallpaperId);
        std::string id = WallpaperRegistry::IdForAssetPath(cfg.wallpaperPath);
        return id.empty() ? WallpaperRegistry::DefaultBackground().id : id;
    }

    DesktopThemeId selectedThemeIdFromConfig()
    {
        DesktopConfigData cfg;
        std::string err;
        DesktopThemeId themeId = DesktopThemeId::Classic;
        if (!DesktopConfig::Load("desktop.json", cfg, err)) {
            return themeId;
        }
        TryParseDesktopThemeId(cfg.desktopThemeId.c_str(), &themeId);
        return themeId;
    }

    int backgroundIndexForId(const std::string& id)
    {
        const auto& backgrounds = WallpaperRegistry::BuiltInBackgrounds();
        for (size_t i = 0; i < backgrounds.size(); ++i) {
            if (backgrounds[i].id == id) return static_cast<int>(i);
        }
        return 0;
    }

    int gradientIndexForId(const std::string& id)
    {
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        for (size_t i = 0; i < gradients.size(); ++i) {
            if (gradients[i].id == id) return static_cast<int>(i);
        }
        return 0;
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
        if (!IsSciFiThemeActive()) return packRgb(42, 42, 42);
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
    case 1: return "This System";
    case 2: return "File Manager";
    case 3: return "System Settings";
    case 4: return "Use smaller folder icons";
    default: return "";
    }
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
    const auto& wallpapers = WallpaperRegistry::BuiltInWallpapers();
    const auto& gradients = WallpaperRegistry::BuiltInGradients();
    const auto& backgrounds = WallpaperRegistry::BuiltInBackgrounds();
    s_selectedIndex = 0;
    s_appliedIndex = 0;
    s_selectedBackgroundIndex = 0;
    s_appliedBackgroundIndex = 0;
    s_selectedGradientIndex = 0;
    s_appliedGradientIndex = 0;
    s_activeTab = WallpaperRegistry::IsGradientId(selectedId) ? 1 : 0;
    DesktopConfigData cfg;
    std::string cfgErr;
    if (DesktopConfig::Load("desktop.json", cfg, cfgErr)) {
        s_showDesktopTrash = cfg.showDesktopTrash;
        s_showDesktopThisSystem = cfg.showDesktopThisSystem;
        s_showDesktopFileManager = cfg.showDesktopFileManager;
        s_showDesktopSystemSettings = cfg.showDesktopSystemSettings;
        s_smallLiveDesktopFolderIcons = cfg.smallLiveDesktopFolderIcons;
        Logger::write(LogLevel::Info, std::string("DisplayOptions Desktop Icons loaded: Trash=") + (s_showDesktopTrash ? "true" : "false") +
            " ThisSystem=" + (s_showDesktopThisSystem ? "true" : "false") +
            " FileManager=" + (s_showDesktopFileManager ? "true" : "false") +
            " SystemSettings=" + (s_showDesktopSystemSettings ? "true" : "false") +
            " FolderIconsSmall=" + (s_smallLiveDesktopFolderIcons ? "true" : "false"));
    } else {
        s_showDesktopTrash = true;
        s_showDesktopThisSystem = true;
        s_showDesktopFileManager = true;
        s_showDesktopSystemSettings = false;
        s_smallLiveDesktopFolderIcons = true;
        Logger::write(LogLevel::Info, "DisplayOptions Desktop Icons defaulted: Trash=true ThisSystem=true FileManager=true SystemSettings=false FolderIconsSmall=true");
    }
    s_selectedThemeId = selectedThemeIdFromConfig();
    s_appliedThemeId = s_selectedThemeId;
    for (size_t i = 0; i < backgrounds.size(); ++i) {
        if (backgrounds[i].id == selectedId) {
            s_selectedBackgroundIndex = static_cast<int>(i);
            s_appliedBackgroundIndex = static_cast<int>(i);
            break;
        }
    }
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
    drawColorRect(s_windowId, 0, 0, kWindowW, kWindowH, DisplayOptionsBodyColor());

    drawButton(kBackgroundTabX, kTabY, kTabW, kTabH, "Backgrounds", s_activeTab == 0, true);
    drawButton(kDesktopIconTabX, kTabY, kTabW, kTabH, "Desktop Icons", s_activeTab == 2, true);
    drawButton(kGradientTabX, kTabY, kTabW, kTabH, "Gradients", s_activeTab == 1, true);
    drawButton(kThemeTabX, kTabY, kTabW, kTabH, "Theme", s_activeTab == 3, true);
    drawText(s_windowId, 26, 72,
        s_activeTab == 2 ? "Choose desktop icons and folder icon size:"
        : (s_activeTab == 0 ? "Select a background from the gallery:"
        : (s_activeTab == 1 ? "Select a gradient from the gallery:"
        : "Choose a desktop theme. Classic is default; Sci Fi is opt-in.")),
        DisplayOptionsTextColor());
    drawColorRect(s_windowId, 20, 92, 742, 456, DisplayOptionsPanelColor());

    if (s_activeTab == 0) {
        const auto& backgrounds = WallpaperRegistry::BuiltInBackgrounds();
        for (size_t i = 0; i < backgrounds.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            int x = kGalleryX + col * (kTileW + kGapX);
            int y = kGalleryY + row * (kTileH + kGapY);
            bool hover = hit(s_mouseX, s_mouseY, x, y, kTileW, kTileH);
            drawBackgroundTile(static_cast<int>(i), x, y, hover, static_cast<int>(i) == s_selectedBackgroundIndex, static_cast<int>(i) == s_appliedBackgroundIndex);
        }
    } else if (s_activeTab == 1) {
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        for (size_t i = 0; i < gradients.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            int x = kGalleryX + col * (kTileW + kGapX);
            int y = kGalleryY + row * (kTileH + kGapY);
            bool hover = hit(s_mouseX, s_mouseY, x, y, kTileW, kTileH);
            drawGradientTile(static_cast<int>(i), x, y, hover, static_cast<int>(i) == s_selectedGradientIndex, static_cast<int>(i) == s_appliedGradientIndex);
        }
    } else if (s_activeTab == 3) {
        drawThemeTab();
    } else {
        drawDesktopIconsTab();
    }

    if (s_activeTab != 2 && s_activeTab != 3) {
        drawButton(kSelectButtonX, kButtonY, kButtonW, kButtonH, s_activeTab == 0 ? "Select Background" : "Select Gradient", false, true);
        drawButton(kSelectButtonX + 200, kButtonY, kButtonW, kButtonH, "Choose Color", false, false);
        drawButton(kSelectButtonX + 400, kButtonY, kButtonW, kButtonH, "Visual Effects", false, false);
    } else if (s_activeTab == 2) {
        drawText(s_windowId, 26, kButtonY + 10, "Changes are saved immediately.", DisplayOptionsMutedTextColor());
    } else {
        drawText(s_windowId, 26, kButtonY + 10, "Selecting a theme saves immediately and reloads the compositor.", DisplayOptionsMutedTextColor());
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
        s_showDesktopThisSystem,
        s_showDesktopFileManager,
        s_showDesktopSystemSettings,
        s_smallLiveDesktopFolderIcons
    };
    for (int i = 0; i < 5; ++i) {
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
    render();
}

void DisplayOptions::handleMouseDown(int mx, int my)
{
    if (hit(mx, my, kBackgroundTabX, kTabY, kTabW, kTabH)) {
        s_activeTab = 0;
        render();
        return;
    }
    if (hit(mx, my, kDesktopIconTabX, kTabY, kTabW, kTabH)) {
        s_activeTab = 2;
        Logger::write(LogLevel::Info, "DisplayOptions Desktop Icons tab selected");
        render();
        return;
    }
    if (hit(mx, my, kGradientTabX, kTabY, kTabW, kTabH)) {
        s_activeTab = 1;
        render();
        return;
    }
    if (hit(mx, my, kThemeTabX, kTabY, kTabW, kTabH)) {
        s_activeTab = 3;
        Logger::write(LogLevel::Info, "DisplayOptions Theme tab selected");
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
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        for (size_t i = 0; i < gradients.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            int x = kGalleryX + col * (kTileW + kGapX);
            int y = kGalleryY + row * (kTileH + kGapY);
            if (hit(mx, my, x, y, kTileW, kTileH)) {
                s_selectedGradientIndex = static_cast<int>(i);
                s_selectedBackgroundIndex = backgroundIndexForId(gradients[i].id);
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

    const auto& backgrounds = WallpaperRegistry::BuiltInBackgrounds();
    for (size_t i = 0; i < backgrounds.size(); ++i) {
        int col = static_cast<int>(i) % kCols;
        int row = static_cast<int>(i) / kCols;
        int x = kGalleryX + col * (kTileW + kGapX);
        int y = kGalleryY + row * (kTileH + kGapY);
        if (hit(mx, my, x, y, kTileW, kTileH)) {
            s_selectedBackgroundIndex = static_cast<int>(i);
            if (backgrounds[i].kind == BackgroundKind::Gradient) {
                s_selectedGradientIndex = gradientIndexForId(backgrounds[i].id);
            }
            render();
            return;
        }
    }

    if (hit(mx, my, kSelectButtonX, kButtonY, kButtonW, kButtonH)) {
        applySelectedBackground();
        render();
    }
}

void DisplayOptions::handleMouseUp(int, int)
{
}

void DisplayOptions::handleDoubleClick(int mx, int my)
{
    if (s_activeTab == 2 || s_activeTab == 3) return;
    if (s_activeTab == 1) {
        const auto& gradients = WallpaperRegistry::BuiltInGradients();
        for (size_t i = 0; i < gradients.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            int x = kGalleryX + col * (kTileW + kGapX);
            int y = kGalleryY + row * (kTileH + kGapY);
            if (hit(mx, my, x, y, kTileW, kTileH)) {
                s_selectedGradientIndex = static_cast<int>(i);
                s_selectedBackgroundIndex = backgroundIndexForId(gradients[i].id);
                applySelectedGradient();
                render();
                return;
            }
        }
        return;
    }

    const auto& backgrounds = WallpaperRegistry::BuiltInBackgrounds();
    for (size_t i = 0; i < backgrounds.size(); ++i) {
        int col = static_cast<int>(i) % kCols;
        int row = static_cast<int>(i) / kCols;
        int x = kGalleryX + col * (kTileW + kGapX);
        int y = kGalleryY + row * (kTileH + kGapY);
        if (hit(mx, my, x, y, kTileW, kTileH)) {
            s_selectedBackgroundIndex = static_cast<int>(i);
            applySelectedBackground();
            render();
            return;
        }
    }
}

bool DisplayOptions::toggleDesktopIconSetting(int index)
{
    bool* setting = nullptr;
    switch (index) {
    case 0: setting = &s_showDesktopTrash; break;
    case 1: setting = &s_showDesktopThisSystem; break;
    case 2: setting = &s_showDesktopFileManager; break;
    case 3: setting = &s_showDesktopSystemSettings; break;
    case 4: setting = &s_smallLiveDesktopFolderIcons; break;
    default: return false;
    }
    *setting = !*setting;
    Logger::write(LogLevel::Info, std::string("DisplayOptions Desktop Icons checkbox changed: ") + desktopIconSettingName(index) + "=" + (*setting ? "true" : "false"));
    return true;
}

void DisplayOptions::saveDesktopIconSettings()
{
    DesktopConfigData cfg;
    std::string err;
    if (!DesktopConfig::Load("desktop.json", cfg, err)) {
        Logger::write(LogLevel::Info, "DisplayOptions Desktop Icons save using default config because load failed: " + err);
    }
    cfg.showDesktopTrash = s_showDesktopTrash;
    cfg.showDesktopThisSystem = s_showDesktopThisSystem;
    cfg.showDesktopFileManager = s_showDesktopFileManager;
    cfg.showDesktopSystemSettings = s_showDesktopSystemSettings;
    cfg.smallLiveDesktopFolderIcons = s_smallLiveDesktopFolderIcons;
    cfg.desktopThemeId = DesktopThemeIdToString(s_appliedThemeId);
    if (DesktopConfig::Save("desktop.json", cfg, err)) {
        Logger::write(LogLevel::Info, "DisplayOptions Desktop Icons settings saved");
        publish(MsgType::MT_DesktopConfigReload, "");
    } else {
        Logger::write(LogLevel::Warn, "DisplayOptions Desktop Icons settings save failed: " + err);
    }
}

void DisplayOptions::applySelectedTheme()
{
    DesktopConfigData cfg;
    std::string err;
    if (!DesktopConfig::Load("desktop.json", cfg, err)) {
        Logger::write(LogLevel::Info, "DisplayOptions theme save using default config because load failed: " + err);
    }

    cfg.desktopThemeId = DesktopThemeIdToString(s_selectedThemeId);
    if (DesktopConfig::Save("desktop.json", cfg, err)) {
        s_appliedThemeId = s_selectedThemeId;
        Logger::write(LogLevel::Info, std::string("DisplayOptions applied theme id=") + DesktopThemeIdToString(s_appliedThemeId));
        publish(MsgType::MT_DesktopConfigReload, "");
    } else {
        Logger::write(LogLevel::Warn, "DisplayOptions theme save failed: " + err);
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
    s_appliedBackgroundIndex = backgroundIndexForId(selected.id);
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
    const auto& backgrounds = WallpaperRegistry::BuiltInBackgrounds();
    if (s_selectedBackgroundIndex < 0 || s_selectedBackgroundIndex >= static_cast<int>(backgrounds.size())) return;
    const BackgroundEntry& selected = backgrounds[static_cast<size_t>(s_selectedBackgroundIndex)];
    ipc::Message msg;
    msg.type = static_cast<uint32_t>(MsgType::MT_DesktopWallpaperSet);
    msg.data.assign(selected.id.begin(), selected.id.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    s_appliedBackgroundIndex = s_selectedBackgroundIndex;
    if (selected.kind == BackgroundKind::Gradient) {
        s_selectedGradientIndex = gradientIndexForId(selected.id);
        s_appliedGradientIndex = s_selectedGradientIndex;
    }
    Logger::write(LogLevel::Info, std::string("DisplayOptions applied background id=") + selected.id +
        " kind=" + WallpaperRegistry::KindName(selected.kind) +
        " full=" + selected.fullImagePath + " thumb=" + selected.thumbnailPath);
}

bool DisplayOptions::hit(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

} // namespace apps
} // namespace gxos
