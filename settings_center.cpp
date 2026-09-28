#include "settings_center.h"

#include "compositor.h"
#include "background_service.h"
#include "desktop_service.h"
#include "desktop_theme.h"
#include "display_configuration.h"
#include "display_configuration_service.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "network_telemetry.h"
#include "settings_network_service.h"
#include "settings_server_identity.h"
#include "settings_system_information.h"
#include "open_dialog.h"
#include "process.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace gxos {
namespace apps {

using namespace gxos::gui;
using namespace gxos::display;
using namespace gxos::apps::settings;

namespace {
constexpr int kWindowWidth = 980;
constexpr int kWindowHeight = 700;
constexpr int kSidebarX = 14;
constexpr int kSidebarY = 82;
constexpr int kSidebarWidth = 230;
constexpr int kContentX = 256;
constexpr int kContentY = 82;
constexpr int kCategoryX = 20;
constexpr int kCategoryY = 108;
constexpr int kCategoryWidth = 216;
constexpr int kCategoryHeight = 40;
constexpr int kCategoryPitch = 47;
constexpr int kSearchY = 16;
constexpr int kSearchWidth = 310;
constexpr int kSearchHeight = 38;
constexpr uint32_t kKeyBackspace = 0x08;
constexpr uint32_t kKeyTab = 0x09;
constexpr uint32_t kKeyEnter = 0x0D;
constexpr uint32_t kKeyEscape = 0x1B;
constexpr uint32_t kKeySpace = 0x20;
constexpr uint32_t kKeyLeft = 0x25;
constexpr uint32_t kKeyUp = 0x26;
constexpr uint32_t kKeyRight = 0x27;
constexpr uint32_t kKeyDown = 0x28;
constexpr int kModifierShift = 1;

uint32_t packRgb(int r, int g, int b)
{
    return 0xFF000000u |
        (static_cast<uint32_t>(r & 0xFF) << 16) |
        (static_cast<uint32_t>(g & 0xFF) << 8) |
        static_cast<uint32_t>(b & 0xFF);
}

uint32_t blendColor(uint32_t base, uint32_t overlay, int percent)
{
    percent = std::max(0, std::min(100, percent));
    const int keep = 100 - percent;
    return packRgb(
        (static_cast<int>((base >> 16) & 0xFF) * keep + static_cast<int>((overlay >> 16) & 0xFF) * percent) / 100,
        (static_cast<int>((base >> 8) & 0xFF) * keep + static_cast<int>((overlay >> 8) & 0xFF) * percent) / 100,
        (static_cast<int>(base & 0xFF) * keep + static_cast<int>(overlay & 0xFF) * percent) / 100);
}

bool isSciFiTheme()
{
    return GetCurrentDesktopThemeId() == DesktopThemeId::SciFi;
}

uint32_t bodyColor()
{
    if (!isSciFiTheme()) return packRgb(240, 240, 240);
    const auto& theme = GetCurrentDesktopTheme();
    return blendColor(theme.taskbarBackground, theme.windowBackground, 18);
}

uint32_t panelColor()
{
    if (!isSciFiTheme()) return packRgb(246, 246, 246);
    const auto& theme = GetCurrentDesktopTheme();
    return blendColor(theme.windowBackground, theme.taskbarBackground, 10);
}

uint32_t cardColor()
{
    if (!isSciFiTheme()) return packRgb(255, 255, 255);
    const auto& theme = GetCurrentDesktopTheme();
    return blendColor(theme.windowBackground, theme.taskbarBackground, 16);
}

uint32_t textColor()
{
    return isSciFiTheme() ? GetCurrentDesktopTheme().titleBarText : packRgb(20, 20, 20);
}

uint32_t mutedTextColor()
{
    if (!isSciFiTheme()) return packRgb(82, 82, 82);
    const auto& theme = GetCurrentDesktopTheme();
    return blendColor(theme.titleBarText, theme.taskbarBackground, 58);
}

uint32_t accentColor()
{
    return isSciFiTheme() ? GetCurrentDesktopTheme().accent : packRgb(45, 110, 205);
}

uint32_t borderColor()
{
    return isSciFiTheme() ? GetCurrentDesktopTheme().windowBorder : packRgb(190, 190, 190);
}

std::string fitText(const std::string& value, size_t maxChars)
{
    if (value.size() <= maxChars) return value;
    if (maxChars <= 3) return value.substr(0, maxChars);
    return value.substr(0, maxChars - 3) + "...";
}

std::string networkAddressText(const network_settings::IPv4Value& value)
{
    if (!value.available) return "Unavailable";
    char address[16]{};
    return network_settings::formatIPv4(value.value, address) ? std::string(address) : "Unavailable";
}

std::string networkAssignmentText(network_settings::ConfigurationMode mode)
{
    switch (mode) {
    case network_settings::ConfigurationMode::Dhcp: return "Automatic (DHCP)";
    case network_settings::ConfigurationMode::Static: return "Static";
    case network_settings::ConfigurationMode::Unknown: default: return "Unavailable";
    }
}

std::string networkDnsText(const network_settings::NetworkInterfaceInfo& adapter)
{
    std::string value = networkAddressText(adapter.dns);
    if (adapter.dnsSource == network_settings::DnsSource::Dhcp) value += " (DHCP)";
    else if (adapter.dnsSource == network_settings::DnsSource::Static) value += " (static)";
    else if (adapter.dns.available && adapter.dns.value != 0) value += " (source unavailable)";
    return value;
}

uint64_t steadyMilliseconds()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void publish(MsgType type, const std::string& payload)
{
    ipc::Message message;
    message.type = static_cast<uint32_t>(type);
    message.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(message), false);
}

struct FocusItem {
    enum class Kind { None, Search, Category, SearchResult, Control } kind{Kind::None};
    int index{0};
    FocusControl control{FocusControl::None};
};

struct DisplayPageState {
    bool available{false};
    DisplayConfigurationSnapshot active{};
    uint32_t pendingMode{static_cast<uint32_t>(DisplayConfigurationMode::Mirror)};
    bool modeChanged{false};
    bool resolutionChanged{false};
    std::string pendingModeId;
    int pendingWidth{0};
    int pendingHeight{0};
    std::vector<DisplayMode> supportedModes;
    std::string status;

    bool dirty() const { return modeChanged || resolutionChanged; }
};

struct PersonalizationFeedback {
    std::mutex mutex;
    std::string status;
    std::atomic<bool> refreshRequested{false};
};

class SettingsApplication {
public:
    explicit SettingsApplication(SettingsRoute initialRoute, int width = kWindowWidth, int height = kWindowHeight)
        : m_width(width), m_height(height), m_navigation(),
          m_systemInformation(readHostedSystemInformation()),
          m_personalizationFeedback(std::make_shared<PersonalizationFeedback>())
    {
        m_navigation.navigate(initialRoute);
        if (initialRoute.category == CategoryId::Personalization) refreshPersonalization();
        if (initialRoute.category == CategoryId::Display || initialRoute.category == CategoryId::System) refreshDisplay();
        if (initialRoute.category == CategoryId::Network) {
            m_networkRefreshPolicy.setActive(true, steadyMilliseconds());
            refreshNetwork();
        }
        setFocusForRoute(initialRoute);
    }

    void setWindowId(uint64_t id) { m_windowId = id; }
    uint64_t windowId() const { return m_windowId; }

    bool setWindowFocused(bool focused)
    {
        if (m_windowFocused == focused) return false;
        m_windowFocused = focused;
        const bool refreshActive = focused && m_navigation.selectedCategory() == CategoryId::Network;
        m_networkRefreshPolicy.setActive(refreshActive, steadyMilliseconds());
        if (focused && m_navigation.selectedCategory() == CategoryId::Personalization) refreshPersonalization();
        if (refreshActive) {
            refreshNetwork();
        }
        return true;
    }

    bool refreshNetworkIfDue()
    {
        if (m_windowId == 0 || !m_networkRefreshPolicy.due(steadyMilliseconds())) return false;
        return refreshNetwork();
    }

    bool refreshPersonalizationIfRequested()
    {
        if (!m_personalizationFeedback->refreshRequested.exchange(false, std::memory_order_acq_rel)) return false;
        refreshPersonalization();
        render();
        return true;
    }

    void setWindowSize(int width, int height)
    {
        m_width = std::max(640, width);
        m_height = std::max(480, height);
    }

    void render()
    {
        if (m_windowId == 0) return;
        publish(MsgType::MT_DrawText, std::to_string(m_windowId) + "|\f");
        drawRect(0, 0, m_width, m_height, bodyColor());
        drawText(24, 20, "Settings", textColor());
        drawRect(searchX(), kSearchY, searchWidth(), kSearchHeight,
                 m_focusedItem.kind == FocusItem::Kind::Search ? blendColor(cardColor(), accentColor(), 7) : cardColor());
        drawOutline(searchX(), kSearchY, searchWidth(), kSearchHeight,
                    m_focusedItem.kind == FocusItem::Kind::Search ? accentColor() : borderColor());
        drawText(searchX() + 12, kSearchY + 12,
                 m_search.empty() ? "Search settings..." : fitText(m_search, 46),
                 m_search.empty() ? mutedTextColor() : textColor());
        if (m_focusedItem.kind == FocusItem::Kind::Search && !m_search.empty()) {
            const int caretX = std::min(searchX() + searchWidth() - 10, searchX() + 12 + static_cast<int>(m_search.size()) * 8);
            drawRect(caretX, kSearchY + 10, 1, 18, accentColor());
        }

        drawRect(kSidebarX, kSidebarY, kSidebarWidth, std::max(1, m_height - kSidebarY - 12), panelColor());
        drawRect(kContentX, kContentY, contentWidth(), std::max(1, m_height - kContentY - 12), panelColor());
        drawText(kCategoryX, 88, "Categories", mutedTextColor());
        for (size_t i = 0; i < kCategories.size(); ++i) {
            const FocusItem item{ FocusItem::Kind::Category, static_cast<int>(i), FocusControl::None };
            const bool selected = m_navigation.selectedCategory() == kCategories[i].id;
            const bool hover = m_hoverItem.kind == FocusItem::Kind::Category && m_hoverItem.index == static_cast<int>(i);
            const bool focused = sameFocus(item, m_focusedItem);
            drawButton(kCategoryX, kCategoryY + static_cast<int>(i) * categoryPitch(),
                       kCategoryWidth, categoryHeight(), kCategories[i].label, selected, hover, focused, true);
        }

        const CategoryInfo* category = categoryInfo(m_navigation.selectedCategory());
        if (!category) return;
        drawText(kContentX + 26, kContentY + 22, category->label, textColor());
        drawText(kContentX + 26, kContentY + 52, category->description, mutedTextColor());

        if (!m_search.empty()) {
            renderSearchResults();
        } else {
            switch (m_navigation.selectedCategory()) {
            case CategoryId::System: renderSystem(); break;
            case CategoryId::Display: renderDisplay(); break;
            case CategoryId::Network: renderNetwork(); break;
            case CategoryId::Personalization: renderPersonalization(); break;
            case CategoryId::Devices: renderPlaceholder(); break;
            case CategoryId::Storage: renderAdvancedPage("Disk Manager provides the current disk and partition tools.", FocusControl::StorageDiskManager, "Manage disks and partitions"); break;
            case CategoryId::Apps: renderPlaceholder(); break;
            case CategoryId::Users: renderPlaceholder(); break;
            case CategoryId::DateTime: renderAdvancedPage("Clock and time-zone settings remain available in Display Options.", FocusControl::DateTimeAdvanced, "Open date and time options"); break;
            case CategoryId::Accessibility: renderAdvancedPage("Open the built-in on-screen keyboard.", FocusControl::AccessibilityKeyboard, "Open on-screen keyboard"); break;
            case CategoryId::Developer: renderAdvancedPage("Open the guideXOS Console for developer and diagnostic commands.", FocusControl::DeveloperConsole, "Open Console"); break;
            case CategoryId::About: renderAbout(); break;
            case CategoryId::Count: renderPlaceholder(); break;
            }
        }
    }

    void onMouse(const std::string& payload)
    {
        std::istringstream input(payload);
        std::string xs, ys, buttonsText, action, modifiersText;
        std::getline(input, xs, '|');
        std::getline(input, ys, '|');
        std::getline(input, buttonsText, '|');
        std::getline(input, action, '|');
        std::getline(input, modifiersText, '|');
        try {
            const int x = std::stoi(xs);
            const int y = std::stoi(ys);
            const int buttons = std::stoi(buttonsText);
            const FocusItem previousHover = m_hoverItem;
            const bool previousHoverValid = m_hasHover;
            m_hoverItem = hitTest(x, y);
            m_hasHover = true;
            const bool hoverChanged = !previousHoverValid || !sameFocus(previousHover, m_hoverItem);

            const bool pressed = (buttons & 1) != 0;
            const bool downAction = action.empty() || action == "down";
            if (pressed && downAction && !m_mouseDown) {
                m_mouseDown = true;
                m_focusedItem = m_hoverItem;
                if (m_hoverItem.kind != FocusItem::Kind::Search && m_hoverItem.kind != FocusItem::Kind::None) {
                    const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                    const bool duplicateClick = sameFocus(m_hoverItem, m_lastMouseAction) && now - m_lastMouseActionTime < 420;
                    m_lastMouseAction = m_hoverItem;
                    m_lastMouseActionTime = now;
                    if (!duplicateClick) activate(m_hoverItem);
                }
                render();
            } else if (!pressed || action == "up") {
                m_mouseDown = false;
                if (hoverChanged) render();
            }
        } catch (...) {
        }
    }

    void onKey(const std::string& payload)
    {
        std::istringstream input(payload);
        std::string keyText, action, modifiersText;
        std::getline(input, keyText, '|');
        std::getline(input, action, '|');
        std::getline(input, modifiersText, '|');
        try {
            const uint32_t key = static_cast<uint32_t>(std::stoul(keyText));
            const int modifiers = modifiersText.empty() ? 0 : std::stoi(modifiersText);
            if (action == "up") {
                if (key == kKeyEnter) m_enterDown = false;
                if (key == kKeySpace) m_spaceDown = false;
                return;
            }
            if (action != "down") return;
            if (key == kKeyTab) {
                cycleFocus((modifiers & kModifierShift) != 0);
                render();
                return;
            }
            if (key == kKeyEscape) {
                if (!m_search.empty()) {
                    m_search.clear();
                    m_navigation.navigate(SettingsRoute{ m_navigation.selectedCategory(), TargetId::Page });
                    m_focusedItem = FocusItem{ FocusItem::Kind::Search, 0, FocusControl::None };
                    render();
                }
                return;
            }
            if (m_focusedItem.kind == FocusItem::Kind::Search) {
                if (key == kKeyDown) {
                    const SearchResultSet results = searchSettings(m_search);
                    if (results.count > 0) {
                        m_focusedItem = FocusItem{ FocusItem::Kind::SearchResult, 0, FocusControl::None };
                    }
                    render();
                } else if (key == kKeyBackspace) {
                    if (!m_search.empty()) m_search.pop_back();
                    render();
                } else if (key == kKeyEnter) {
                    const SearchResultSet results = searchSettings(m_search);
                    if (results.count > 0) activate(FocusItem{ FocusItem::Kind::SearchResult, 0, FocusControl::None });
                    render();
                } else {
                    const char typed = keyToChar(key, (modifiers & kModifierShift) != 0);
                    if (typed != '\0' && m_search.size() < 40) {
                        m_search.push_back(typed);
                        render();
                    }
                }
                return;
            }
            if (key == kKeyUp || key == kKeyDown) {
                moveFocusWithinList(key == kKeyUp ? -1 : 1);
                render();
                return;
            }
            if (key == kKeyLeft || key == kKeyRight) {
                if (m_focusedItem.kind == FocusItem::Kind::Control &&
                    (m_focusedItem.control == FocusControl::DisplayMode || m_focusedItem.control == FocusControl::DisplayResolution)) {
                    changeDisplayValue(key == kKeyRight ? 1 : -1);
                    render();
                } else if (m_focusedItem.kind == FocusItem::Kind::Control &&
                           m_focusedItem.control == FocusControl::NetworkAdapter &&
                           networkAdapterCount() > 0) {
                    const uint32_t count = networkAdapterCount();
                    const uint32_t next = key == kKeyRight
                        ? (m_selectedNetworkAdapter + 1) % count
                        : (m_selectedNetworkAdapter + count - 1) % count;
                    m_selectedNetworkAdapter = next;
                    m_focusedItem.index = static_cast<int>(next);
                    render();
                }
                return;
            }
            if ((key == kKeyEnter && !m_enterDown) || (key == kKeySpace && !m_spaceDown)) {
                if (key == kKeyEnter) m_enterDown = true;
                if (key == kKeySpace) m_spaceDown = true;
                activate(m_focusedItem);
                render();
            }
        } catch (...) {
        }
    }

private:
    uint64_t m_windowId{0};
    int m_width{kWindowWidth};
    int m_height{kWindowHeight};
    NavigationModel m_navigation;
    std::string m_search;
    DisplayPageState m_display;
    HostedSystemInformation m_systemInformation;
    std::string m_backgroundId;
    std::string m_backgroundName;
    std::string m_backgroundPreviewPath;
    std::string m_themeName;
    bool m_backgroundAvailable{false};
    std::shared_ptr<PersonalizationFeedback> m_personalizationFeedback;
    FocusItem m_focusedItem{ FocusItem::Kind::Search, 0, FocusControl::None };
    FocusItem m_hoverItem{};
    bool m_hasHover{false};
    bool m_mouseDown{false};
    FocusItem m_lastMouseAction{};
    uint64_t m_lastMouseActionTime{0};
    bool m_enterDown{false};
    bool m_spaceDown{false};
    bool m_windowFocused{true};
    uint32_t m_selectedNetworkAdapter{0};
    network_settings::NetworkSnapshot m_networkSnapshot{};
    network_settings::Result m_networkReadResult{network_settings::Result::Unavailable};
    network_settings::RefreshPolicy m_networkRefreshPolicy{};

    int searchX() const { return std::max(278, m_width - kSearchWidth - 16); }
    int searchWidth() const { return std::min(kSearchWidth, std::max(180, m_width - searchX() - 16)); }
    int categoryPitch() const { return m_height < 540 ? 30 : m_height < 650 ? 36 : kCategoryPitch; }
    int categoryHeight() const { return std::min(kCategoryHeight, categoryPitch() - 4); }
    int pageX() const { return kContentX + 28; }
    int pageWidth() const { return std::max(1, std::min(750, m_width - pageX() - 16)); }
    int contentWidth() const { return std::max(1, m_width - kContentX - 14); }
    bool compactDisplayLayout() const { return m_height < 680; }
    bool smallDisplayLayout() const { return m_height < 540; }
    int displaySettingsY() const { return kContentY + (smallDisplayLayout() ? 180 : compactDisplayLayout() ? 230 : 280); }
    int displayResolutionY() const { return displaySettingsY() + (smallDisplayLayout() ? 34 : 42); }
    int displayModeY() const { return displayResolutionY() + (smallDisplayLayout() ? 40 : 48); }
    bool compactNetworkLayout() const { return m_height < 680; }
    int networkAdaptersY() const { return kContentY + (m_height < 540 ? 62 : compactNetworkLayout() ? 88 : 104); }
    int networkAdapterRowY(uint32_t index) const { return networkAdaptersY() + 48 + static_cast<int>(index) * 26; }
    int networkAdapterCardHeight() const
    {
        const uint32_t count = std::min<uint32_t>(m_networkSnapshot.adapterCount,
            static_cast<uint32_t>(network_settings::kMaxAdapters));
        const uint32_t shown = networkAdapterDisplayCount();
        return count == 0 ? (m_height < 540 ? 84 : 102) : 52 + static_cast<int>(shown) * 26;
    }
    uint32_t networkAdapterCount() const
    {
        return std::min<uint32_t>(m_networkSnapshot.adapterCount,
            static_cast<uint32_t>(network_settings::kMaxAdapters));
    }
    uint32_t networkAdapterDisplayCount() const
    {
        const uint32_t count = networkAdapterCount();
        const int actionHeight = smallSettingsLayout() ? 30 : 40;
        const int availableCardHeight = m_height - 12 - networkAdaptersY() - networkDetailsHeight() - 20 - actionHeight;
        const int visibleRows = std::max(0, (availableCardHeight - 52) / 26);
        return std::min<uint32_t>(count, static_cast<uint32_t>(visibleRows));
    }
    uint32_t networkAdapterDisplayStart() const
    {
        const uint32_t shown = networkAdapterDisplayCount();
        const uint32_t count = networkAdapterCount();
        if (shown == 0 || count <= shown || m_selectedNetworkAdapter < shown) return 0;
        return std::min<uint32_t>(m_selectedNetworkAdapter - shown + 1, count - shown);
    }
    int networkDetailsY() const { return networkAdaptersY() + networkAdapterCardHeight() + 10; }
    int networkDetailsHeight() const { return m_height < 540 ? 152 : compactNetworkLayout() ? 216 : 238; }
    int networkAdvancedY() const { return networkDetailsY() + networkDetailsHeight() + 10; }
    bool smallSettingsLayout() const { return m_height < 540; }
    int systemInformationY() const { return kContentY + (smallSettingsLayout() ? 66 : 82); }
    int systemInformationHeight() const { return smallSettingsLayout() ? 180 : 210; }
    int systemLinksY() const { return kContentY + (smallSettingsLayout() ? 254 : 296); }
    int systemLinkY(int index) const { return systemLinksY() + (smallSettingsLayout() ? 26 : 30) + (index / 2) * (smallSettingsLayout() ? 29 : 36); }
    int systemLinkHeight() const { return smallSettingsLayout() ? 26 : 32; }
    int systemLinksHeight() const { return smallSettingsLayout() ? 132 : 150; }
    int systemLinkX(int index) const { return pageX() + 12 + (index % 2) * ((pageWidth() - 28) / 2 + 8); }
    int systemLinkWidth() const { return (pageWidth() - 28) / 2; }
    int personalizationBackgroundY() const { return kContentY + (smallSettingsLayout() ? 66 : 82); }
    int personalizationChooseY() const { return kContentY + (smallSettingsLayout() ? 178 : 218); }
    int personalizationAdvancedY() const { return kContentY + (smallSettingsLayout() ? 214 : 264); }
    int personalizationAppearanceY() const { return kContentY + (smallSettingsLayout() ? 252 : 312); }
    int personalizationActionHeight() const { return smallSettingsLayout() ? 32 : 40; }
    int personalizationBackgroundHeight() const { return smallSettingsLayout() ? 104 : 128; }
    int searchResultPitch() const { return smallSettingsLayout() ? 42 : 50; }
    size_t visibleSearchResultCount(size_t count) const { return count; }

    static bool sameFocus(const FocusItem& left, const FocusItem& right)
    {
        return left.kind == right.kind && left.index == right.index && left.control == right.control;
    }

    static FocusControl initialFocusFor(const SettingsRoute& route)
    {
        switch (route.target) {
        case TargetId::PersonalizationBackground: return FocusControl::PersonalizationChooseBackground;
        case TargetId::Resolution: return FocusControl::DisplayResolution;
        case TargetId::DisplayMode: return FocusControl::DisplayMode;
        default: return FocusControl::None;
        }
    }

    void drawRect(int x, int y, int width, int height, uint32_t color) const
    {
        std::ostringstream payload;
        payload << m_windowId << "|" << x << "|" << y << "|" << width << "|" << height
                << "|" << static_cast<int>((color >> 16) & 0xFF)
                << "|" << static_cast<int>((color >> 8) & 0xFF)
                << "|" << static_cast<int>(color & 0xFF);
        publish(MsgType::MT_DrawRect, payload.str());
    }

    void drawText(int x, int y, const std::string& value, uint32_t color = textColor()) const
    {
        std::ostringstream payload;
        payload << m_windowId << "|" << x << "|" << y << "|"
                << static_cast<int>((color >> 16) & 0xFF) << "|"
                << static_cast<int>((color >> 8) & 0xFF) << "|"
                << static_cast<int>(color & 0xFF) << "|" << fitText(value, 96);
        publish(MsgType::MT_DrawTextAtColor, payload.str());
    }

    void drawOutline(int x, int y, int width, int height, uint32_t color) const
    {
        drawRect(x, y, width, 1, color);
        drawRect(x, y + height - 1, width, 1, color);
        drawRect(x, y, 1, height, color);
        drawRect(x + width - 1, y, 1, height, color);
    }

    void drawCard(int x, int y, int width, int height, const std::string& title) const
    {
        drawRect(x, y, width, height, cardColor());
        drawOutline(x, y, width, height, borderColor());
        if (!title.empty()) drawText(x + 18, y + 14, title, textColor());
    }

    void drawButton(int x, int y, int width, int height, const std::string& label,
                    bool selected, bool hover, bool focused, bool enabled) const
    {
        uint32_t fill = selected ? blendColor(cardColor(), accentColor(), isSciFiTheme() ? 15 : 11) : cardColor();
        if (hover && enabled) fill = blendColor(fill, accentColor(), isSciFiTheme() ? 16 : 10);
        if (!enabled) fill = blendColor(panelColor(), cardColor(), 55);
        const uint32_t edge = !enabled ? borderColor() : ((hover || focused || selected) ? accentColor() : borderColor());
        drawRect(x, y, width, height, fill);
        drawOutline(x, y, width, height, edge);
        if (selected && !isSciFiTheme()) drawRect(x, y, 4, height, accentColor());
        if (focused && !hover) drawRect(x + 2, y + 2, width - 4, 1, accentColor());
        drawText(x + 12, y + std::max(8, (height - 16) / 2), fitText(label, static_cast<size_t>(std::max(1, (width - 24) / 8))), enabled ? textColor() : mutedTextColor());
    }

    void drawSettingRow(int x, int y, int width, const std::string& label,
                        const std::string& value, FocusControl control,
                        bool enabled, bool highlighted = false, int rowHeight = 44)
    {
        const bool hover = m_hoverItem.kind == FocusItem::Kind::Control && m_hoverItem.control == control;
        const bool focused = sameFocus(m_focusedItem, FocusItem{ FocusItem::Kind::Control, 0, control });
        const uint32_t fill = hover && enabled ? blendColor(cardColor(), accentColor(), 8) :
            (highlighted ? blendColor(cardColor(), accentColor(), 12) : cardColor());
        drawRect(x, y, width, rowHeight, fill);
        if (focused || hover || highlighted) drawOutline(x, y, width, rowHeight, accentColor());
        drawText(x + 14, y + 6, label, enabled ? textColor() : mutedTextColor());
        const int valueX = x + width / 2;
        const size_t valueLimit = static_cast<size_t>(std::max(8, std::min(30, (width / 2 - 24) / 8)));
        drawText(valueX, y + 6, fitText(value, valueLimit), enabled ? mutedTextColor() : mutedTextColor());
        if (enabled) drawText(x + width - 24, y + 6, ">", accentColor());
    }

    std::vector<FocusItem> focusOrder() const
    {
        std::vector<FocusItem> items;
        items.push_back(FocusItem{ FocusItem::Kind::Search, 0, FocusControl::None });
        for (size_t i = 0; i < kCategories.size(); ++i) items.push_back(FocusItem{ FocusItem::Kind::Category, static_cast<int>(i), FocusControl::None });
        if (!m_search.empty()) {
            const SearchResultSet results = searchSettings(m_search);
            for (size_t i = 0; i < visibleSearchResultCount(results.count); ++i) items.push_back(FocusItem{ FocusItem::Kind::SearchResult, static_cast<int>(i), FocusControl::None });
            return items;
        }
        auto add = [&](FocusControl control) {
            if (m_navigation.canFocus(control)) items.push_back(FocusItem{ FocusItem::Kind::Control, 0, control });
        };
        switch (m_navigation.selectedCategory()) {
        case CategoryId::Network:
            for (uint32_t row = 0; row < networkAdapterDisplayCount(); ++row) {
                const uint32_t index = networkAdapterDisplayStart() + row;
                items.push_back(FocusItem{ FocusItem::Kind::Control, static_cast<int>(index), FocusControl::NetworkAdapter });
            }
            if (!smallSettingsLayout() || networkAdvancedY() + 30 <= m_height - 12) add(FocusControl::NetworkAdvanced);
            break;
        case CategoryId::Display:
            if (m_display.available && m_display.supportedModes.size() > 1 && m_display.active.outputCount == 1) add(FocusControl::DisplayResolution);
            if (m_display.available && m_display.active.outputCount > 1) add(FocusControl::DisplayMode);
            if (m_display.dirty()) { add(FocusControl::DisplayApply); add(FocusControl::DisplayCancel); }
            add(FocusControl::DisplayAdvanced);
            break;
        case CategoryId::System:
            add(FocusControl::SystemDisplay);
            add(FocusControl::SystemNetwork);
            add(FocusControl::SystemStorage);
            add(FocusControl::SystemAbout);
            add(FocusControl::SystemControlPanel);
            break;
        case CategoryId::Personalization:
            add(FocusControl::PersonalizationChooseBackground);
            add(FocusControl::PersonalizationAdvanced);
            break;
        case CategoryId::DateTime: add(FocusControl::DateTimeAdvanced); break;
        case CategoryId::Storage: add(FocusControl::StorageDiskManager); break;
        case CategoryId::Developer: add(FocusControl::DeveloperConsole); break;
        case CategoryId::Accessibility: add(FocusControl::AccessibilityKeyboard); break;
        default: break;
        }
        return items;
    }

    void cycleFocus(bool reverse)
    {
        std::vector<FocusItem> items = focusOrder();
        if (items.empty()) return;
        size_t current = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            if (sameFocus(items[i], m_focusedItem)) { current = i; break; }
        }
        current = reverse ? (current + items.size() - 1) % items.size() : (current + 1) % items.size();
        m_focusedItem = items[current];
    }

    void moveFocusWithinList(int delta)
    {
        if (m_focusedItem.kind == FocusItem::Kind::Category) {
            const int next = (m_focusedItem.index + delta + static_cast<int>(kCategories.size())) % static_cast<int>(kCategories.size());
            m_focusedItem.index = next;
            navigateTo(SettingsRoute{ kCategories[static_cast<size_t>(next)].id, TargetId::Page });
        } else if (m_focusedItem.kind == FocusItem::Kind::SearchResult) {
            const size_t count = visibleSearchResultCount(searchSettings(m_search).count);
            if (count > 0) m_focusedItem.index = (m_focusedItem.index + delta + static_cast<int>(count)) % static_cast<int>(count);
        } else if (m_focusedItem.kind == FocusItem::Kind::Control &&
                   m_focusedItem.control == FocusControl::NetworkAdapter &&
                   networkAdapterCount() > 0) {
            const int count = static_cast<int>(networkAdapterCount());
            const int next = (m_focusedItem.index + delta + count) % count;
            m_focusedItem.index = next;
            m_selectedNetworkAdapter = static_cast<uint32_t>(next);
        }
    }

    FocusItem hitTest(int x, int y) const
    {
        if (x >= searchX() && x <= searchX() + searchWidth() && y >= kSearchY && y <= kSearchY + kSearchHeight)
            return FocusItem{ FocusItem::Kind::Search, 0, FocusControl::None };
        for (size_t i = 0; i < kCategories.size(); ++i) {
            const int cy = kCategoryY + static_cast<int>(i) * categoryPitch();
            if (x >= kCategoryX && x <= kCategoryX + kCategoryWidth && y >= cy && y <= cy + categoryHeight())
                return FocusItem{ FocusItem::Kind::Category, static_cast<int>(i), FocusControl::None };
        }
        if (!m_search.empty()) {
            const SearchResultSet results = searchSettings(m_search);
            for (size_t i = 0; i < visibleSearchResultCount(results.count); ++i) {
                const int ry = kContentY + 132 + static_cast<int>(i) * searchResultPitch();
                if (x >= kContentX + 24 && x <= kContentX + contentWidth() - 24 && y >= ry && y <= ry + 42)
                    return FocusItem{ FocusItem::Kind::SearchResult, static_cast<int>(i), FocusControl::None };
            }
            return FocusItem{};
        }
        if (m_navigation.selectedCategory() == CategoryId::Network) {
            for (uint32_t row = 0; row < networkAdapterDisplayCount(); ++row) {
                const uint32_t i = networkAdapterDisplayStart() + row;
                if (inRect(x, y, pageX() + 12, networkAdapterRowY(row), pageWidth() - 24, 24))
                    return FocusItem{ FocusItem::Kind::Control, static_cast<int>(i), FocusControl::NetworkAdapter };
            }
        }
        for (const FocusItem& item : focusOrder()) {
            if (item.kind != FocusItem::Kind::Control || !m_navigation.canFocus(item.control)) continue;
            if (controlHit(item.control, x, y)) return item;
        }
        return FocusItem{};
    }

    bool controlHit(FocusControl control, int x, int y) const
    {
        switch (control) {
        case FocusControl::NetworkAdvanced: return inRect(x, y, pageX() + 6, networkAdvancedY(), std::min(390, pageWidth() - 12), smallSettingsLayout() ? 30 : 40);
        case FocusControl::NetworkAdapter: return false;
        case FocusControl::DisplayResolution: return inRect(x, y, pageX() + 12, displayResolutionY(), pageWidth() - 24, smallDisplayLayout() ? 36 : 44);
        case FocusControl::DisplayMode: return inRect(x, y, pageX() + 12, displayModeY(), pageWidth() - 24, smallDisplayLayout() ? 36 : 44);
        case FocusControl::DisplayApply: return inRect(x, y, 320, smallDisplayLayout() ? 376 : compactDisplayLayout() ? 449 : 496, 136, smallDisplayLayout() ? 30 : 40);
        case FocusControl::DisplayCancel: return inRect(x, y, 466, smallDisplayLayout() ? 376 : compactDisplayLayout() ? 449 : 496, 136, smallDisplayLayout() ? 30 : 40);
        case FocusControl::DisplayAdvanced: return inRect(x, y, 320, smallDisplayLayout() ? 418 : compactDisplayLayout() ? (m_height < 580 ? 486 : 504) : 580, std::min(390, pageWidth() - 12), smallDisplayLayout() ? 30 : 42);
        case FocusControl::SystemDisplay: return inRect(x, y, systemLinkX(0), systemLinkY(0), systemLinkWidth(), systemLinkHeight());
        case FocusControl::SystemNetwork: return inRect(x, y, systemLinkX(1), systemLinkY(1), systemLinkWidth(), systemLinkHeight());
        case FocusControl::SystemStorage: return inRect(x, y, systemLinkX(2), systemLinkY(2), systemLinkWidth(), systemLinkHeight());
        case FocusControl::SystemAbout: return inRect(x, y, systemLinkX(3), systemLinkY(3), systemLinkWidth(), systemLinkHeight());
        case FocusControl::SystemControlPanel: return inRect(x, y, systemLinkX(4), systemLinkY(4), systemLinkWidth(), systemLinkHeight());
        case FocusControl::PersonalizationChooseBackground: return inRect(x, y, pageX() + 8, personalizationChooseY(), std::min(390, pageWidth() - 16), personalizationActionHeight());
        case FocusControl::PersonalizationAdvanced: return inRect(x, y, pageX() + 8, personalizationAdvancedY(), std::min(390, pageWidth() - 16), personalizationActionHeight());
        case FocusControl::DateTimeAdvanced:
        case FocusControl::StorageDiskManager:
        case FocusControl::DeveloperConsole:
        case FocusControl::AccessibilityKeyboard: return inRect(x, y, pageX() + 6, 210, std::min(390, pageWidth() - 12), 46);
        case FocusControl::None: default: return false;
        }
    }

    static bool inRect(int x, int y, int rx, int ry, int width, int height)
    {
        return x >= rx && x <= rx + width && y >= ry && y <= ry + height;
    }

    static char keyToChar(uint32_t key, bool shifted)
    {
        if (key >= 'A' && key <= 'Z') return static_cast<char>(shifted ? key : key + ('a' - 'A'));
        if (key >= '0' && key <= '9') return static_cast<char>(key);
        if (key == kKeySpace) return ' ';
        if (key == 0xBD || key == 0x6D) return '-';
        if (key == 0xBE || key == 0x6E) return '.';
        return '\0';
    }

    void navigateTo(const SettingsRoute& route)
    {
        const CategoryId previousCategory = m_navigation.selectedCategory();
        if (!m_navigation.navigate(route)) return;
        if (route.category == CategoryId::Display && !m_display.dirty()) refreshDisplay();
        if (route.category == CategoryId::System && !m_display.dirty()) refreshDisplay();
        if (route.category == CategoryId::Personalization) refreshPersonalization();
        const bool networkVisible = m_windowFocused && route.category == CategoryId::Network;
        m_networkRefreshPolicy.setActive(networkVisible, steadyMilliseconds());
        if (networkVisible && previousCategory != CategoryId::Network) refreshNetwork();
        setFocusForRoute(route);
        m_search.clear();
    }

    void setFocusForRoute(const SettingsRoute& route)
    {
        const FocusControl requested = initialFocusFor(route);
        const bool focusable = requested == FocusControl::PersonalizationChooseBackground
            ? route.category == CategoryId::Personalization
            : requested == FocusControl::DisplayResolution
            ? m_display.available && m_display.supportedModes.size() > 1 && m_display.active.outputCount == 1
            : requested == FocusControl::DisplayMode ? m_display.available && m_display.active.outputCount > 1 : false;
        m_focusedItem = focusable
            ? FocusItem{ FocusItem::Kind::Control, 0, requested }
            : FocusItem{ FocusItem::Kind::Category, static_cast<int>(route.category), FocusControl::None };
    }

    void activate(const FocusItem& item)
    {
        switch (item.kind) {
        case FocusItem::Kind::Category:
            if (item.index >= 0 && item.index < static_cast<int>(kCategories.size())) {
                navigateTo(SettingsRoute{ kCategories[static_cast<size_t>(item.index)].id, TargetId::Page });
            }
            break;
        case FocusItem::Kind::SearchResult: {
            const SearchResultSet results = searchSettings(m_search);
            if (item.index >= 0 && item.index < static_cast<int>(results.count)) navigateTo(results.values[static_cast<size_t>(item.index)].route);
            break;
        }
        case FocusItem::Kind::Control:
            if (!m_navigation.canFocus(item.control)) break;
            switch (item.control) {
            case FocusControl::SystemDisplay: navigateTo(SettingsRoute{ CategoryId::Display, TargetId::Page }); break;
            case FocusControl::SystemNetwork: navigateTo(SettingsRoute{ CategoryId::Network, TargetId::Page }); break;
            case FocusControl::SystemStorage: navigateTo(SettingsRoute{ CategoryId::Storage, TargetId::Page }); break;
            case FocusControl::SystemAbout: navigateTo(SettingsRoute{ CategoryId::About, TargetId::Page }); break;
            case FocusControl::NetworkAdapter:
                if (item.index >= 0 && static_cast<uint32_t>(item.index) < m_networkSnapshot.adapterCount)
                    m_selectedNetworkAdapter = static_cast<uint32_t>(item.index);
                break;
            case FocusControl::NetworkAdvanced: launchAdvanced("Console"); break;
            case FocusControl::DisplayResolution: changeDisplayValue(1); break;
            case FocusControl::DisplayMode: changeDisplayValue(1); break;
            case FocusControl::DisplayApply: applyDisplay(); break;
            case FocusControl::DisplayCancel: cancelDisplay(); break;
            case FocusControl::DisplayAdvanced: launchAdvanced("DisplayOptions"); break;
            case FocusControl::PersonalizationAdvanced: launchAdvanced("DisplayOptions"); break;
            case FocusControl::PersonalizationChooseBackground: chooseBackground(); break;
            case FocusControl::DateTimeAdvanced: launchAdvanced("DisplayOptions"); break;
            case FocusControl::StorageDiskManager: launchAdvanced("DiskManager"); break;
            case FocusControl::SystemControlPanel: launchAdvanced("ControlPanel"); break;
            case FocusControl::DeveloperConsole: launchAdvanced("Console"); break;
            case FocusControl::AccessibilityKeyboard: launchAdvanced("OnScreenKeyboard"); break;
            case FocusControl::None: break;
            }
            break;
        case FocusItem::Kind::Search:
            break;
        case FocusItem::Kind::None:
            break;
        }
    }

    void launchAdvanced(const std::string& launchName)
    {
        std::string error;
        if (!DesktopService::LaunchApp(launchName, error)) {
            Logger::write(LogLevel::Warn, std::string("Settings advanced launch failed: ") + launchName + (error.empty() ? "" : ": " + error));
        }
    }

    bool queryActiveDisplay(DisplayConfigurationResponse& response)
    {
        DisplayConfigurationCommand command{};
        command.version = kDisplayConfigurationContractVersion;
        command.structureSize = sizeof(DisplayConfigurationCommand);
        command.requestId = DisplayConfigurationService::nextRequestId();
        command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::QueryActiveConfiguration);
        return DisplayConfigurationService::submit(command, response) &&
            response.requestId == command.requestId && response.commandType == command.commandType && response.success != 0;
    }

    bool refreshNetwork()
    {
        const network_settings::NetworkSnapshot previous = m_networkSnapshot;
        network_settings::NetworkSnapshot current{};
        m_networkReadResult = readSettingsNetworkSnapshot(&current);
        if (current.version != network_settings::kContractVersion) {
            current = network_settings::NetworkSnapshot{};
            current.backend = network_settings::Backend::Unavailable;
            current.state = network_settings::SnapshotState::Unavailable;
            m_networkReadResult = network_settings::Result::Unavailable;
        }
        m_networkSnapshot = current;
        if (m_networkSnapshot.adapterCount == 0) m_selectedNetworkAdapter = 0;
        else if (m_selectedNetworkAdapter >= m_networkSnapshot.adapterCount)
            m_selectedNetworkAdapter = m_networkSnapshot.adapterCount - 1;
        m_networkRefreshPolicy.completed(steadyMilliseconds());
        return !network_settings::sameSnapshot(previous, m_networkSnapshot);
    }

    void refreshDisplay()
    {
        DisplayConfigurationResponse response{};
        m_display.available = queryActiveDisplay(response);
        m_display.supportedModes.clear();
        m_display.status.clear();
        if (!m_display.available) {
            m_display.status = "Active display configuration is unavailable.";
            m_display.active = DisplayConfigurationSnapshot{};
            m_display.modeChanged = false;
            m_display.resolutionChanged = false;
            return;
        }
        m_display.active = response.activeConfiguration;
        m_display.pendingMode = m_display.active.mode;
        m_display.modeChanged = false;
        m_display.resolutionChanged = false;
        if (m_display.active.outputCount > 0) {
            const DisplayConfigurationOutput& primary = primaryOutput(m_display.active);
            m_display.pendingModeId = primary.modeId;
            m_display.pendingWidth = primary.width;
            m_display.pendingHeight = primary.height;
        }
        const DetectedDisplayInventory detected = Compositor::detectedDisplayInventory();
        if (detected.backend == "virtio-gpu" && detected.qemuOnly && detected.backendGateActive)
            m_display.supportedModes = displayModeCatalogForInventory(detected);
    }

    static const DisplayConfigurationOutput& primaryOutput(const DisplayConfigurationSnapshot& snapshot)
    {
        for (uint32_t i = 0; i < std::min(snapshot.outputCount, kDisplayConfigurationMaxOutputs); ++i) {
            if (snapshot.outputs[i].primary) return snapshot.outputs[i];
        }
        return snapshot.outputs[0];
    }

    void changeDisplayValue(int direction)
    {
        if (!m_display.available) return;
        if (m_focusedItem.control == FocusControl::DisplayResolution &&
            m_display.supportedModes.size() > 1 && m_display.active.outputCount == 1) {
            size_t current = 0;
            for (size_t i = 0; i < m_display.supportedModes.size(); ++i) {
                if ((!m_display.pendingModeId.empty() && m_display.supportedModes[i].id == m_display.pendingModeId) ||
                    (m_display.pendingModeId.empty() && m_display.supportedModes[i].width == m_display.pendingWidth && m_display.supportedModes[i].height == m_display.pendingHeight)) {
                    current = i;
                    break;
                }
            }
            const size_t count = m_display.supportedModes.size();
            const size_t next = direction >= 0 ? (current + 1) % count : (current + count - 1) % count;
            const DisplayMode& mode = m_display.supportedModes[next];
            m_display.pendingModeId = mode.id;
            m_display.pendingWidth = mode.width;
            m_display.pendingHeight = mode.height;
            m_display.resolutionChanged = true;
            m_display.status = "Resolution change is pending. Apply to rebuild display resources.";
            return;
        }
        if (m_focusedItem.control == FocusControl::DisplayMode && m_display.active.outputCount > 1) {
            m_display.pendingMode = m_display.pendingMode == static_cast<uint32_t>(DisplayConfigurationMode::Extend)
                ? static_cast<uint32_t>(DisplayConfigurationMode::Mirror)
                : static_cast<uint32_t>(DisplayConfigurationMode::Extend);
            m_display.modeChanged = m_display.pendingMode != m_display.active.mode;
            m_display.status = "Display mode change is pending. Apply to save it.";
        }
    }

    void applyDisplay()
    {
        if (!m_display.dirty()) return;
        DisplayConfigurationResponse latest{};
        if (!queryActiveDisplay(latest)) {
            m_display.status = "Apply failed: active display configuration is unavailable.";
            return;
        }
        DisplayConfigurationCommand command{};
        command.version = kDisplayConfigurationContractVersion;
        command.structureSize = sizeof(DisplayConfigurationCommand);
        command.requestId = DisplayConfigurationService::nextRequestId();
        command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
        command.flags = DisplayConfigurationFlagCommitPersistence;
        command.origin = static_cast<uint32_t>(DisplayConfigurationRequestOrigin::UserApply);
        command.requestedConfiguration.mode = m_display.modeChanged ? m_display.pendingMode : latest.activeConfiguration.mode;
        command.requestedConfiguration.outputCount = std::min(latest.activeConfiguration.outputCount, kDisplayConfigurationMaxOutputs);
        std::copy(std::begin(latest.activeConfiguration.primaryOutputId), std::end(latest.activeConfiguration.primaryOutputId), std::begin(command.requestedConfiguration.primaryOutputId));
        for (uint32_t i = 0; i < command.requestedConfiguration.outputCount; ++i)
            command.requestedConfiguration.outputs[i] = latest.activeConfiguration.outputs[i];

        if (m_display.resolutionChanged && command.requestedConfiguration.outputCount == 1) {
            DisplayConfigurationOutput& output = command.requestedConfiguration.outputs[0];
            const size_t modeIdLength = std::min(m_display.pendingModeId.size(), sizeof(output.modeId) - 1);
            std::copy_n(m_display.pendingModeId.begin(), modeIdLength, output.modeId);
            output.modeId[modeIdLength] = '\0';
            output.width = m_display.pendingWidth;
            output.height = m_display.pendingHeight;
        }

        DisplayConfigurationResponse response{};
        const bool submitted = DisplayConfigurationService::submit(command, response);
        if (submitted && response.requestId == command.requestId && response.success) {
            m_display.active = response.activeConfiguration;
            m_display.modeChanged = false;
            m_display.resolutionChanged = false;
            m_display.pendingMode = m_display.active.mode;
            if (m_display.active.outputCount > 0) {
                const DisplayConfigurationOutput& output = primaryOutput(m_display.active);
                m_display.pendingModeId = output.modeId;
                m_display.pendingWidth = output.width;
                m_display.pendingHeight = output.height;
            }

            m_display.status = "Display configuration applied successfully.";
            m_focusedItem = FocusItem{ FocusItem::Kind::Control, 0, FocusControl::DisplayMode };
        } else {
            m_display.status = std::string("Apply failed: ") + (response.diagnostic[0] ? response.diagnostic : "display configuration rejected");
        }
    }

    void cancelDisplay()
    {
        refreshDisplay();
        if (m_display.available) m_display.status = "Pending display changes were discarded.";
        m_focusedItem = m_display.available
            ? FocusItem{ FocusItem::Kind::Control, 0, FocusControl::DisplayMode }
            : FocusItem{ FocusItem::Kind::Control, 0, FocusControl::DisplayAdvanced };
    }

    void refreshPersonalization()
    {
        std::string error;
        m_backgroundAvailable = gui::DesktopBackgroundService::ReadCurrentBackground(
            m_backgroundId, m_backgroundName, m_backgroundPreviewPath, error);
        if (!m_backgroundAvailable) {
            m_backgroundId.clear();
            m_backgroundName = "Background unavailable";
            m_backgroundPreviewPath.clear();
        }
        m_themeName = GetCurrentDesktopTheme().displayName
            ? GetCurrentDesktopTheme().displayName : "Unavailable";
    }

    std::string personalizationStatus() const
    {
        std::lock_guard<std::mutex> lock(m_personalizationFeedback->mutex);
        return m_personalizationFeedback->status;
    }

    void chooseBackground()
    {
        std::weak_ptr<PersonalizationFeedback> feedback = m_personalizationFeedback;
        dialogs::OpenDialog::Show(0, 0, "/", [feedback](const std::string& path) {
            std::string error;
            const bool changed = gui::DesktopBackgroundService::ImportAndSetDesktopBackground(path, error);
            if (std::shared_ptr<PersonalizationFeedback> state = feedback.lock()) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->status = changed ? "Desktop background changed." :
                    (error.empty() ? "That PNG could not be used. The current background was kept." :
                     "Background unchanged: " + error);
                state->refreshRequested.store(true, std::memory_order_release);
            }
        });
    }

    void renderSearchResults()
    {
        const SearchResultSet results = searchSettings(m_search);
        drawText(kContentX + 26, kContentY + 98,
                 results.count ? "Search results" : "No matching settings", mutedTextColor());
        for (size_t i = 0; i < visibleSearchResultCount(results.count); ++i) {
            const int y = kContentY + 132 + static_cast<int>(i) * searchResultPitch();
            const FocusItem item{ FocusItem::Kind::SearchResult, static_cast<int>(i), FocusControl::None };
            const bool focused = sameFocus(item, m_focusedItem);
            const bool hover = m_hoverItem.kind == item.kind && m_hoverItem.index == item.index;
            const CategoryInfo* resultCategory = categoryInfo(results.values[i].route.category);
            std::string summary = resultCategory ? resultCategory->label : "Settings";
            if (results.values[i].route.target != TargetId::Page) {
                const char* detail = results.values[i].route.target == TargetId::SystemDevice ? "Device information" :
                    results.values[i].route.target == TargetId::PersonalizationBackground ? "Background" :
                    results.values[i].route.target == TargetId::Resolution ? "Resolution" :
                    results.values[i].route.target == TargetId::DisplayMode ? "Display mode" :
                    results.values[i].route.target == TargetId::IPv4 ? "IP assignment" :
                    results.values[i].route.target == TargetId::DNS ? "DNS" :
                    results.values[i].route.target == TargetId::Gateway ? "Gateway" :
                    results.values[i].route.target == TargetId::StorageDisks ? "Disks and partitions" : "Version";
                summary += " > ";
                summary += detail;
            }
            drawButton(kContentX + 24, y, contentWidth() - 48, 42,
                       std::string(results.values[i].label) + "    " + summary,
                       false, hover, focused, true);
        }
    }

    void renderSystem()
    {
        const int x = pageX();
        const int informationY = systemInformationY();
        const int informationHeight = systemInformationHeight();
        const bool compact = smallSettingsLayout();
        drawCard(x, informationY, pageWidth(), informationHeight, "Hosted computer");
        if (m_navigation.route().target == TargetId::SystemDevice)
            drawOutline(x, informationY, pageWidth(), informationHeight, accentColor());
        const size_t valueLimit = static_cast<size_t>(std::max(10, (pageWidth() / 2 - 28) / 8));
        const int rowPitch = compact ? 18 : 22;
        auto row = [&](int index, const char* label, const std::string& value) {
            const int y = informationY + (compact ? 32 : 39) + index * rowPitch;
            drawText(x + 18, y, label, mutedTextColor());
            drawText(x + pageWidth() / 2, y, fitText(value, valueLimit), textColor());
        };
        const std::string cpuName = formatProcessorName(m_systemInformation.processorName, valueLimit);
        const std::string logicalCount = m_systemInformation.logicalProcessorCount
            ? std::to_string(m_systemInformation.logicalProcessorCount) : "Unavailable";
        row(0, "Computer name", formatComputerName(m_systemInformation.computerName, valueLimit));
        row(1, "Processor", cpuName);
        row(2, "Logical processors", logicalCount);
        row(3, "Installed memory", formatMemoryBytes(m_systemInformation.installedMemoryBytes));
        row(4, "Architecture", formatArchitecture(m_systemInformation.architecture));
        row(5, "Boot firmware", formatBootEnvironment(m_systemInformation.firmware));
        row(6, "Display", m_display.available && m_display.active.outputCount > 0
            ? formatDisplayResolution(primaryOutput(m_display.active).width, primaryOutput(m_display.active).height)
            : "Unavailable");
        drawText(x + 18, informationY + (compact ? 159 : 190),
                 fitText("guideXOS does not currently store a separate computer name.",
                         static_cast<size_t>(std::max(12, pageWidth() / 8))), mutedTextColor());

        drawCard(x, systemLinksY(), pageWidth(), systemLinksHeight(), "Related settings");
        const FocusControl controls[] = {
            FocusControl::SystemDisplay, FocusControl::SystemNetwork,
            FocusControl::SystemStorage, FocusControl::SystemAbout,
            FocusControl::SystemControlPanel
        };
        const char* labels[] = { "Display", "Network & Internet", "Storage", "About", "Control Panel" };
        for (int i = 0; i < 5; ++i) {
            const FocusItem item{ FocusItem::Kind::Control, 0, controls[i] };
            drawButton(systemLinkX(i), systemLinkY(i), systemLinkWidth(), systemLinkHeight(), labels[i], false,
                m_hoverItem.kind == item.kind && m_hoverItem.control == item.control,
                sameFocus(m_focusedItem, item), true);
        }
    }

    void renderPersonalization()
    {
        const int x = pageX();
        const bool highlighted = m_navigation.route().target == TargetId::PersonalizationBackground;
        const bool compact = smallSettingsLayout();
        const int backgroundY = personalizationBackgroundY();
        const int backgroundHeight = personalizationBackgroundHeight();
        const int previewX = x + 18;
        const int previewY = kContentY + (compact ? 102 : 126);
        const int previewWidth = compact ? 74 : 86;
        const int previewHeight = compact ? 46 : 54;
        const int detailsX = x + (compact ? 104 : 120);
        drawCard(x, backgroundY, pageWidth(), backgroundHeight, "Desktop background");
        if (highlighted) drawOutline(x, backgroundY, pageWidth(), backgroundHeight, accentColor());
        drawRect(previewX, previewY, previewWidth, previewHeight, blendColor(cardColor(), accentColor(), 14));
        if (!m_backgroundPreviewPath.empty()) {
            publish(MsgType::MT_DrawImage,
                gui::packDrawImage(m_windowId, previewX, previewY, previewWidth, previewHeight, m_backgroundPreviewPath));
        }
        drawText(detailsX, kContentY + (compact ? 106 : 130), "Current background", mutedTextColor());
        drawText(detailsX, kContentY + (compact ? 128 : 157),
                 fitText(m_backgroundAvailable ? m_backgroundName : "Background unavailable",
                         static_cast<size_t>(std::max(12, (pageWidth() - (compact ? 132 : 150)) / 8))), textColor());

        const FocusItem chooseItem{ FocusItem::Kind::Control, 0, FocusControl::PersonalizationChooseBackground };
        drawButton(x + 8, personalizationChooseY(), std::min(390, pageWidth() - 16), personalizationActionHeight(),
            "Choose a PNG background image", false,
            sameFocus(m_hoverItem, chooseItem), sameFocus(m_focusedItem, chooseItem), true);
        const FocusItem advancedItem{ FocusItem::Kind::Control, 0, FocusControl::PersonalizationAdvanced };
        drawButton(x + 8, personalizationAdvancedY(), std::min(390, pageWidth() - 16), personalizationActionHeight(),
            "More wallpaper and theme options", false,
            sameFocus(m_hoverItem, advancedItem), sameFocus(m_focusedItem, advancedItem), true);

        const int appearanceY = personalizationAppearanceY();
        const int appearanceHeight = compact ? 64 : 72;
        const int appearanceRowY = appearanceY + (compact ? 40 : 42);
        drawCard(x, appearanceY, pageWidth(), appearanceHeight, "Appearance");
        drawText(x + 20, appearanceRowY, "Desktop theme", mutedTextColor());
        drawText(x + pageWidth() / 2, appearanceRowY, m_themeName, textColor());
        const std::string status = personalizationStatus();
        if (!status.empty()) drawText(x + 8, kContentY + (compact ? 324 : 392),
            fitText(status, static_cast<size_t>(std::max(12, pageWidth() / 8))), mutedTextColor());
        drawText(x + 8, kContentY + (compact ? 344 : 416),
            fitText("Only validated PNG files are accepted; a failed choice keeps the current background.",
                    static_cast<size_t>(std::max(12, pageWidth() / 8))), mutedTextColor());
    }

    void renderDisplay()
    {
        const int x = pageX();
        const bool compact = compactDisplayLayout();
        const bool tiny = smallDisplayLayout();
        const int activeCardY = kContentY + (tiny ? 62 : compact ? 88 : 104);
        const int activeCardHeight = tiny ? 116 : compact ? 128 : 160;
        const int settingsCardY = displaySettingsY();
        const int settingsCardHeight = tiny ? 190 : compact ? 168 : 204;
        const int activeRowsY = activeCardY + (tiny ? 40 : 48);
        drawCard(x, activeCardY, pageWidth(), activeCardHeight, "Active display");
        if (!m_display.available) {
            drawText(x + 20, activeRowsY, "Display configuration is unavailable.", mutedTextColor());
        } else {
            const uint32_t count = std::min(m_display.active.outputCount, kDisplayConfigurationMaxOutputs);
            drawText(x + 20, activeRowsY,
                     std::string("Backend: ") + fitText(m_display.active.backend, 40), mutedTextColor());
            if (count == 0) {
                drawText(x + 20, activeRowsY + 30, "No active display output is reported.", mutedTextColor());
            } else {
                const DisplayConfigurationOutput& output = primaryOutput(m_display.active);
                std::string outputName = output.stableName[0] ? output.stableName : output.stableId;
                drawText(x + 20, activeRowsY + 28, "Display", mutedTextColor());
                drawText(x + pageWidth() / 2, activeRowsY + 28, fitText(outputName, static_cast<size_t>(std::max(12, pageWidth() / 18))), textColor());
                drawText(x + 20, activeRowsY + 58, "Current resolution", mutedTextColor());
                drawText(x + pageWidth() / 2, activeRowsY + 58, formatDisplayResolution(output.width, output.height), accentColor());
            }
        }

        drawCard(x, settingsCardY, pageWidth(), settingsCardHeight, "Display settings");
        const bool canChangeResolution = m_display.available && m_display.supportedModes.size() > 1 && m_display.active.outputCount == 1;
        const std::string resolution = canChangeResolution
            ? formatDisplayResolution(m_display.pendingWidth, m_display.pendingHeight) + "  (QEMU supported modes)"
            : "Use Advanced display settings";
        const int resolutionY = displayResolutionY();
        const int modeY = displayModeY();
        drawSettingRow(x + 12, resolutionY, pageWidth() - 24, "Resolution", resolution,
                       FocusControl::DisplayResolution, canChangeResolution,
                       m_navigation.route().target == TargetId::Resolution, tiny ? 36 : 44);
        const uint32_t activeOutputCount = std::min(m_display.active.outputCount, kDisplayConfigurationMaxOutputs);
        const std::string mode = !m_display.available ? "Unavailable" : activeOutputCount < 2 ? "Single display" :
            (m_display.pendingMode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
        drawSettingRow(x + 12, modeY, pageWidth() - 24, "Display mode", mode,
                       FocusControl::DisplayMode, m_display.available && activeOutputCount > 1,
                       m_navigation.route().target == TargetId::DisplayMode, tiny ? 36 : 44);
        if (m_display.dirty()) {
            const int applyY = tiny ? 376 : compact ? (m_height < 580 ? 446 : 449) : 496;
            drawButton(320, applyY, 136, tiny ? 30 : 40, "Apply", false,
                       m_hoverItem.control == FocusControl::DisplayApply,
                       sameFocus(m_focusedItem, FocusItem{ FocusItem::Kind::Control, 0, FocusControl::DisplayApply }), true);
            drawButton(466, applyY, 136, tiny ? 30 : 40, "Cancel", false,
                       m_hoverItem.control == FocusControl::DisplayCancel,
                       sameFocus(m_focusedItem, FocusItem{ FocusItem::Kind::Control, 0, FocusControl::DisplayCancel }), true);
        }
        const int advancedY = tiny ? 418 : compact ? (m_height < 580 ? 486 : 504) : 580;
        drawButton(320, advancedY, std::min(390, pageWidth() - 12), tiny ? 30 : 42, "Advanced display settings", false,
                   m_hoverItem.control == FocusControl::DisplayAdvanced,
                   sameFocus(m_focusedItem, FocusItem{ FocusItem::Kind::Control, 0, FocusControl::DisplayAdvanced }), true);
        if (!m_display.status.empty() && m_height >= 580) drawText(x + 16, compact ? 552 : 644, fitText(m_display.status, 72), mutedTextColor());
    }

    void renderNetwork()
    {
        const int x = pageX();
        const uint32_t count = std::min<uint32_t>(m_networkSnapshot.adapterCount,
            static_cast<uint32_t>(network_settings::kMaxAdapters));
        const int adaptersY = networkAdaptersY();
        const uint32_t displayCount = networkAdapterDisplayCount();
        const uint32_t displayStart = networkAdapterDisplayStart();
        const std::string adapterTitle = smallSettingsLayout() && count > displayCount
            ? "Network adapters  " + std::to_string(m_selectedNetworkAdapter + 1) + " of " + std::to_string(count)
            : "Network adapters";
        drawCard(x, adaptersY, pageWidth(), networkAdapterCardHeight(), adapterTitle);
        if (m_networkSnapshot.state == network_settings::SnapshotState::Unavailable) {
            const char* unavailableText = m_networkSnapshot.backend == network_settings::Backend::Kernel
                ? "The kernel network adapter is unavailable."
                : "guideXOS network service unavailable.";
            drawText(x + 20, adaptersY + 50, unavailableText, mutedTextColor());
        } else if (count == 0) {
            drawText(x + 20, adaptersY + 50, "No supported network adapter was detected.", mutedTextColor());
        } else {
            for (uint32_t row = 0; row < displayCount; ++row) {
                const uint32_t i = displayStart + row;
                const network_settings::NetworkInterfaceInfo& item = m_networkSnapshot.adapters[i];
                const std::string name = item.name[0] ? item.name : item.stableId;
                const std::string status = network_settings::connectionStateText(m_networkSnapshot, &item);
                drawButton(x + 12, networkAdapterRowY(row), pageWidth() - 24, 24,
                    name + "    " + status, m_selectedNetworkAdapter == i,
                    m_hoverItem.kind == FocusItem::Kind::Control &&
                        m_hoverItem.control == FocusControl::NetworkAdapter && m_hoverItem.index == static_cast<int>(i),
                    sameFocus(m_focusedItem, FocusItem{ FocusItem::Kind::Control, static_cast<int>(i), FocusControl::NetworkAdapter }),
                    true);
            }
        }

        const network_settings::NetworkInterfaceInfo* adapter = count > 0 && m_selectedNetworkAdapter < count
            ? &m_networkSnapshot.adapters[m_selectedNetworkAdapter] : nullptr;
        const int detailsY = networkDetailsY();
        const int detailsHeight = networkDetailsHeight();
        drawCard(x, detailsY, pageWidth(), detailsHeight, "Network connection");
        auto infoRow = [&](int offset, const char* label, const std::string& value, TargetId target) {
            const bool compact = smallSettingsLayout();
            const int y = detailsY + (compact ? 36 : 42) + offset * (compact ? 19 : 25);
            const int rowHeight = compact ? 18 : 23;
            const bool highlighted = m_navigation.route().target == target && target != TargetId::Page;
            drawRect(x + 12, y, pageWidth() - 24, rowHeight,
                     highlighted ? blendColor(cardColor(), accentColor(), 12) : cardColor());
            if (highlighted) drawOutline(x + 12, y, pageWidth() - 24, rowHeight, accentColor());
            drawText(x + 22, y + 3, label, mutedTextColor());
            drawText(x + pageWidth() / 2, y + 3, fitText(value, static_cast<size_t>(std::max(12, pageWidth() / 16))), textColor());
        };
        const std::string status = network_settings::connectionStateText(m_networkSnapshot, adapter);
        infoRow(0, "Status", status, TargetId::Page);
        infoRow(1, "IP assignment", adapter ? networkAssignmentText(adapter->configurationMode) : "Unavailable", TargetId::IPv4);
        infoRow(2, "IPv4 address", adapter ? networkAddressText(adapter->ipv4Address) : "Unavailable", TargetId::IPv4);
        infoRow(3, "Subnet mask", adapter ? networkAddressText(adapter->subnetMask) : "Unavailable", TargetId::IPv4);
        infoRow(4, "Gateway", adapter ? networkAddressText(adapter->gateway) : "Unavailable", TargetId::Gateway);
        infoRow(5, "DNS", adapter ? networkDnsText(*adapter) : "Unavailable", TargetId::DNS);

        const net::NetworkTelemetrySnapshot telemetry = net::networkTelemetrySnapshot();
        const int activityY = detailsY + detailsHeight - 23;
        std::string activity = "Hosted socket telemetry unavailable";
        if (telemetry.available) {
            activity = "Hosted sockets: " + std::to_string(telemetry.bytesReceivedTotal / 1024) +
                " KB received, " + std::to_string(telemetry.bytesSentTotal / 1024) + " KB sent";
            if (telemetry.ratesAvailable)
                activity += " | " + std::to_string(telemetry.receiveKBps) + "/" +
                    std::to_string(telemetry.sendKBps) + " KB/s";
        }
        if (!smallSettingsLayout())
            drawText(x + 20, activityY, fitText(activity, static_cast<size_t>(std::max(20, pageWidth() / 8))), mutedTextColor());

        const int advancedY = networkAdvancedY();
        drawButton(x + 6, advancedY, std::min(390, pageWidth() - 12), smallSettingsLayout() ? 30 : 40,
            "Open network diagnostics", false,
            m_hoverItem.control == FocusControl::NetworkAdvanced,
            sameFocus(m_focusedItem, FocusItem{ FocusItem::Kind::Control, 0, FocusControl::NetworkAdvanced }), true);
        if (!smallSettingsLayout())
            drawText(x + 410, advancedY + 12, "Configuration is read-only in this Settings view.", mutedTextColor());
    }

    void renderAdvancedPage(const std::string& description, FocusControl control, const std::string& action)
    {
        const int x = pageX();
        drawCard(x, kContentY + 104, pageWidth(), 176, "Available tools");
        drawText(x + 20, kContentY + 152, description, mutedTextColor());
        drawButton(x + 6, 210, std::min(390, pageWidth() - 12), 46, action, false,
                   m_hoverItem.control == control,
                   sameFocus(m_focusedItem, FocusItem{ FocusItem::Kind::Control, 0, control }), true);
    }

    void renderAbout()
    {
        const int x = pageX();
        const bool compact = smallSettingsLayout();
        const int cardY = kContentY + (compact ? 66 : 82);
        drawCard(x, cardY, pageWidth(), compact ? 220 : 266, gxos::identity::kGuideXosServerProduct);
        const size_t valueLimit = static_cast<size_t>(std::max(12, (pageWidth() / 2 - 24) / 8));
        const std::string buildDate = std::string(gxos::identity::kGuideXosServerBuildDate) + " " +
            gxos::identity::kGuideXosServerBuildTime;
        const std::string logicalCount = m_systemInformation.logicalProcessorCount
            ? std::to_string(m_systemInformation.logicalProcessorCount) : "Unavailable";
        const std::string rows[][2] = {
            { "Product", gxos::identity::kGuideXosServerProduct },
            { "Version", gxos::identity::kGuideXosServerVersion },
            { "Build date", buildDate },
            { "Runtime", m_systemInformation.runtime },
            { "Architecture", formatArchitecture(m_systemInformation.architecture) },
            { "Processor", formatProcessorName(m_systemInformation.processorName, valueLimit) },
            { "Logical processors", logicalCount },
            { "Installed memory", formatMemoryBytes(m_systemInformation.installedMemoryBytes) },
            { "Boot firmware", formatBootEnvironment(m_systemInformation.firmware) }
        };
        for (int i = 0; i < 9; ++i) {
            const int y = cardY + (compact ? 32 : 39) + i * (compact ? 18 : 22);
            if (i == 1 && m_navigation.route().target == TargetId::AboutVersion) {
                drawRect(x + 10, y - 3, pageWidth() - 20, compact ? 20 : 24,
                         blendColor(cardColor(), accentColor(), 12));
                drawOutline(x + 10, y - 3, pageWidth() - 20, compact ? 20 : 24, accentColor());
            }
            drawText(x + 18, y, rows[i][0], mutedTextColor());
            drawText(x + pageWidth() / 2, y, fitText(rows[i][1], valueLimit), textColor());
        }
        drawText(x + 8, cardY + (compact ? 224 : 280),
            fitText(gxos::identity::kGuideXosServerVersionNote,
                    static_cast<size_t>(std::max(12, pageWidth() / 8))), mutedTextColor());
        drawText(x + 8, cardY + (compact ? 244 : 302),
            fitText("Hardware values describe the hosted machine.",
                    static_cast<size_t>(std::max(12, pageWidth() / 8))), mutedTextColor());
    }

    void renderPlaceholder()
    {
        const int x = pageX();
        drawCard(x, kContentY + 104, pageWidth(), 136, "More settings");
        drawText(x + 20, kContentY + 160, "More settings will be available in a future guideXOS release.", mutedTextColor());
    }
};

} // namespace

uint64_t SettingsCenter::Launch(const std::string& route)
{
    ProcessSpec spec;
    spec.name = "settings";
    spec.entry = SettingsCenter::main;
    spec.appId = "gxos.builtin.settings";
    std::vector<std::string> arguments{ "settings" };
    if (!route.empty()) arguments.push_back(route);
    return ProcessTable::spawn(spec, arguments);
}

int SettingsCenter::main(int argc, char** argv)
{
    SettingsRoute initialRoute{};
    if (argc > 1 && argv && argv[1]) {
        SettingsRoute requested{};
        if (parseSettingsRoute(argv[1], requested)) initialRoute = requested;
    }
    int windowWidth = kWindowWidth;
    int windowHeight = kWindowHeight;
    const DetectedDisplayInventory inventory = Compositor::detectedDisplayInventory();
    const int desktopWidth = inventory.currentDesktop.width();
    const int desktopHeight = inventory.currentDesktop.height();
    if (desktopWidth > 0) windowWidth = std::min(kWindowWidth, std::max(760, desktopWidth - 24));
    if (desktopHeight > 0) windowHeight = std::min(kWindowHeight, std::max(540, desktopHeight - 60));
    SettingsApplication application(initialRoute, windowWidth, windowHeight);
    Logger::write(LogLevel::Info, "Settings Center starting");
    ipc::Bus::ensure("gui.input");
    ipc::Bus::ensure("gui.output");

    std::ostringstream create;
    create << "Settings|" << windowWidth << "|" << windowHeight;
    publish(MsgType::MT_Create, create.str());

    bool running = true;
    while (running) {
        ipc::Message message;
        if (!ipc::Bus::pop("gui.output", message, 100)) {
            application.refreshPersonalizationIfRequested();
            if (application.refreshNetworkIfDue()) application.render();
            continue;
        }
        application.refreshPersonalizationIfRequested();
        const MsgType type = static_cast<MsgType>(message.type);
        const std::string payload(message.data.begin(), message.data.end());
        switch (type) {
        case MsgType::MT_Create: {
            const size_t separator = payload.find('|');
            if (separator != std::string::npos) {
                try {
                    application.setWindowId(std::stoull(payload.substr(0, separator)));
                    application.render();
                } catch (...) {
                    Logger::write(LogLevel::Warn, "Settings Center could not parse its window id");
                }
            }
            break;
        }
        case MsgType::MT_Resize: {
            std::istringstream resize(payload);
            std::string windowId, widthText, heightText;
            std::getline(resize, windowId, '|');
            std::getline(resize, widthText, '|');
            std::getline(resize, heightText, '|');
            try {
                if (application.windowId() != 0 && std::stoull(windowId) == application.windowId()) {
                    application.setWindowSize(std::stoi(widthText), std::stoi(heightText));
                    application.render();
                }
            } catch (...) {
            }
            break;
        }
        case MsgType::MT_InputMouse:
            application.onMouse(payload);
            break;
        case MsgType::MT_InputKey:
            application.onKey(payload);
            break;
        case MsgType::MT_SetFocus:
        case MsgType::MT_ClearFocus: {
            try {
                const uint64_t focusedWindow = std::stoull(payload);
                if (focusedWindow == application.windowId() &&
                    application.setWindowFocused(type == MsgType::MT_SetFocus)) application.render();
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

    Logger::write(LogLevel::Info, "Settings Center terminated");
    return 0;
}

} // namespace apps
} // namespace gxos
