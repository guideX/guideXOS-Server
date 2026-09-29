#include "settings_center.h"

#include "compositor.h"
#include "background_service.h"
#include "clock_time_settings.h"
#include "desktop_config.h"
#include "desktop_service.h"
#include "desktop_theme.h"
#include "display_configuration.h"
#include "display_configuration_service.h"
#include "display_options_store.h"
#include "focus_indicator.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "network_telemetry.h"
#include "settings_network_service.h"
#include "settings_inventory_service.h"
#include "settings_server_identity.h"
#include "settings_system_information.h"
#include "settings_s7_model.h"
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
namespace inventory = gxos::settings_inventory;

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

clocktime::ClockDisplaySettings readHostedClockDisplaySettings()
{
    clocktime::ClockDisplaySettings settings;
    DisplayOptionsStoreData store;
    std::string error;
    if (DisplayOptionsStore::Load("display-options.cfg", store, error)) {
        settings.timeZoneId = store.timeZoneId;
        settings.use24HourTime = store.use24HourTime;
        return clocktime::NormalizeClockDisplaySettings(std::move(settings));
    }

    DesktopConfigData config;
    if (DesktopConfig::Load("desktop.json", config, error)) {
        if (!config.timeZoneId.empty()) settings.timeZoneId = config.timeZoneId;
        settings.use24HourTime = config.use24HourTime;
    }
    return clocktime::NormalizeClockDisplaySettings(std::move(settings));
}

const char* appKindLabel(const std::string& kind)
{
    if (kind == "BuiltIn") return "Built-in application";
    if (kind == "NativeElf") return "Native ELF application";
    if (kind == "GXAppPackage") return "GXApp package";
    if (kind == "Service") return "Service";
    if (kind == "HypervisorGuest") return "Hypervisor guest";
    if (kind == "Script") return "Script";
    return "Application type unavailable";
}

const char* appSourceLabel(const std::string& source)
{
    if (source == "BuiltIn") return "Built-in registration";
    if (source == "SystemApps") return "System app registration";
    if (source == "UserApps") return "User app registration";
    if (source == "Package") return "Package registration";
    if (source == "DevelopmentTemporary") return "Temporary development registration";
    return "Registration source unavailable";
}

std::string formatPciId(uint16_t vendor, uint16_t device)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string value(9, '0');
    value[4] = ':';
    for (int i = 0; i < 4; ++i) {
        value[i] = kHex[(vendor >> ((3 - i) * 4)) & 0xFu];
        value[5 + i] = kHex[(device >> ((3 - i) * 4)) & 0xFu];
    }
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

class DesktopFocusPreferenceStore {
public:
    bool read(bool& enabled)
    {
        DesktopConfigData config;
        std::string error;
        if (!DesktopConfig::Load("desktop.json", config, error)) return false;
        enabled = config.enhancedFocusIndicator;
        return true;
    }

    bool write(bool enabled)
    {
        DesktopConfigData config;
        std::string error;
        if (!DesktopConfig::Load("desktop.json", config, error)) return false;
        config.enhancedFocusIndicator = enabled;
        return DesktopConfig::Save("desktop.json", config, error);
    }
};

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
        DesktopFocusPreferenceStore focusPreferenceStore;
        m_accessibilityPreferenceAvailable = focusPreferenceStore.read(
            m_accessibilityPreferences.enhancedFocusIndicator);
        if (m_accessibilityPreferenceAvailable) {
            FocusIndicator::SetEnhancedFocusEnabled(m_accessibilityPreferences.enhancedFocusIndicator);
        }
        if (initialRoute.category == CategoryId::Personalization) refreshPersonalization();
        if (initialRoute.category == CategoryId::Display || initialRoute.category == CategoryId::System) refreshDisplay();
        if (initialRoute.category == CategoryId::Network) {
            m_networkRefreshPolicy.setActive(true, steadyMilliseconds());
            refreshNetwork();
        }
        if (inventoryCategory(initialRoute.category)) {
            m_inventoryRefreshPolicy.setActive(true, steadyMilliseconds());
            refreshInventory();
        }
        if (initialRoute.category == CategoryId::Apps) refreshApps();
        if (initialRoute.category == CategoryId::DateTime) refreshDateTime();
        if (initialRoute.category == CategoryId::Accessibility || initialRoute.category == CategoryId::Developer)
            refreshDeveloperInventory();
        if (initialRoute.category == CategoryId::Developer && initialRoute.target == TargetId::DeveloperServices)
            refreshDeveloperServices();
        if (initialRoute.category == CategoryId::Developer && initialRoute.target == TargetId::DeveloperDiagnostics)
            refreshDeveloperDiagnostics();
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
        const bool inventoryActive = focused && inventoryCategory(m_navigation.selectedCategory());
        m_inventoryRefreshPolicy.setActive(inventoryActive, steadyMilliseconds());
        if (focused && m_navigation.selectedCategory() == CategoryId::Apps) refreshApps();
        if (focused && m_navigation.selectedCategory() == CategoryId::DateTime) refreshDateTime();
        if (focused && m_navigation.selectedCategory() == CategoryId::Personalization) refreshPersonalization();
        if (refreshActive) {
            refreshNetwork();
        }
        if (inventoryActive) refreshInventory();
        return true;
    }

    bool refreshNetworkIfDue()
    {
        if (m_windowId == 0 || !m_networkRefreshPolicy.due(steadyMilliseconds())) return false;
        return refreshNetwork();
    }

    bool refreshInventoryIfDue()
    {
        if (m_windowId == 0 || !m_inventoryRefreshPolicy.due(steadyMilliseconds())) return false;
        return refreshInventory();
    }

    bool refreshAppsIfDue()
    {
        const uint64_t now = steadyMilliseconds();
        if (m_windowId == 0 || !m_windowFocused || !m_search.empty() ||
            m_navigation.selectedCategory() != CategoryId::Apps || now < m_nextAppsRefreshMs) return false;
        return refreshApps();
    }

    bool refreshDateTimeClockIfDue()
    {
        const uint64_t now = steadyMilliseconds();
        if (m_windowId == 0 || !dateTimeRefreshDue(
                m_navigation.selectedCategory() == CategoryId::DateTime,
                m_windowFocused, !m_search.empty(), now, m_nextDateTimeRefreshMs)) return false;
        m_nextDateTimeRefreshMs = now + 1000;
        m_dateTimeSnapshot = readHostedDateTime(m_clockDisplaySettings);
        renderDateTimeClockText();
        return true;
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
        m_s7PageScroll = clampSettingsPageScroll(m_s7PageScroll, s7MaximumScroll());
        ensureS7FocusVisible();
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
            case CategoryId::Devices:
                if (m_navigation.route().target == TargetId::DeviceDetail) renderDeviceDetail();
                else renderDevices();
                break;
            case CategoryId::Storage:
                if (m_navigation.route().target == TargetId::StorageDiskDetail) renderStorageDiskDetail();
                else renderStorage();
                break;
            case CategoryId::Apps:
                if (m_navigation.route().target == TargetId::AppDetail) renderAppDetails();
                else renderApps();
                break;
            case CategoryId::Users: renderPlaceholder(); break;
            case CategoryId::DateTime: renderDateTimePage(); break;
            case CategoryId::Accessibility: renderAccessibilityPage(); break;
            case CategoryId::Developer: renderDeveloperPage(); break;
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
            if (action.rfind("wheel", 0) == 0) {
                int wheelSteps = 0;
                if (action == "wheel" || action == "wheelup") wheelSteps = 1;
                else if (action == "wheeldown") wheelSteps = -1;
                else {
                    const size_t colon = action.find(':');
                    if (colon != std::string::npos) wheelSteps = std::stoi(action.substr(colon + 1));
                }
                const bool s7PageVisible = m_navigation.selectedCategory() == CategoryId::Accessibility ||
                    m_navigation.selectedCategory() == CategoryId::Developer;
                if (s7PageVisible && wheelSteps != 0 && x >= pageX() && x <= pageX() + pageWidth() &&
                    y >= s7ContentTop() && y <= s7ViewportBottom() && s7MaximumScroll() > 0) {
                    m_s7PageScroll = clampSettingsPageScroll(
                        m_s7PageScroll - wheelSteps * s7RowPitch() * 3, s7MaximumScroll());
                    if (m_focusedItem.kind == FocusItem::Kind::Control) {
                        const int focusedRow = s7FocusRow(m_focusedItem.control);
                        if (focusedRow >= 0 && !s7RowFullyVisible(focusedRow)) {
                            int categoryIndex = 0;
                            for (size_t i = 0; i < kCategories.size(); ++i) {
                                if (kCategories[i].id == m_navigation.selectedCategory()) {
                                    categoryIndex = static_cast<int>(i);
                                    break;
                                }
                            }
                            m_focusedItem = FocusItem{ FocusItem::Kind::Category, categoryIndex, FocusControl::None };
                        }
                    }
                    m_hoverItem = hitTest(x, y);
                    m_hasHover = true;
                    render();
                    return;
                }
                const bool insideList = x >= pageX() && x <= pageX() + pageWidth() &&
                    y >= inventoryListTop() && y <= inventoryListBottom();
                if (insideList && wheelSteps != 0) {
                    if (m_navigation.selectedCategory() == CategoryId::Apps &&
                        m_navigation.route().target != TargetId::AppDetail)
                        m_appScroll -= wheelSteps * 3;
                    else if (m_navigation.selectedCategory() == CategoryId::Devices &&
                        m_navigation.route().target != TargetId::DeviceDetail)
                        m_deviceScroll -= wheelSteps * 3;
                    else if (m_navigation.selectedCategory() == CategoryId::Storage &&
                             m_navigation.route().target != TargetId::StorageDiskDetail)
                        m_storageScroll -= wheelSteps * 3;
                    clampInventoryScroll();
                    m_hoverItem = hitTest(x, y);
                    m_hasHover = true;
                    render();
                    return;
                }
            }
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
    inventory::DeviceSnapshot m_deviceSnapshot{};
    inventory::StorageSnapshot m_storageSnapshot{};
    system_service::ClientResult m_deviceReadResult{system_service::ClientResult::Unavailable};
    system_service::ClientResult m_storageReadResult{system_service::ClientResult::Unavailable};
    network_settings::RefreshPolicy m_inventoryRefreshPolicy{};
    int m_deviceScroll{0};
    int m_storageScroll{0};
    std::string m_selectedDeviceStableId;
    std::string m_selectedDiskStableId;
    uint64_t m_selectedDeviceGeneration{0};
    uint64_t m_selectedDiskGeneration{0};
    AppInventory m_appInventory{};
    DeveloperAppModelSnapshot m_developerAppModel{};
    AccessibilityPreferences m_accessibilityPreferences{};
    bool m_accessibilityPreferenceAvailable{false};
    bool m_accessibilityKeyboardAvailable{false};
    bool m_developerServiceNetworkAvailable{false};
    bool m_developerServiceDevicesAvailable{false};
    bool m_developerServiceStorageAvailable{false};
    bool m_developerServicesChecked{false};
    std::string m_developerAppModelStatus{"Unavailable"};
    std::string m_developerAppModelStatusSource{"Unavailable"};
    bool m_developerPreviewAvailable{false};
    uint64_t m_developerPreviewTotal{0};
    uint64_t m_developerPreviewUnresolved{0};
    uint64_t m_developerPreviewHighRisk{0};
    std::string m_selectedAppId;
    uint64_t m_selectedAppGeneration{0};
    bool m_selectedAppMissing{false};
    std::string m_appStatus;
    uint64_t m_nextAppsRefreshMs{0};
    int m_appScroll{0};
    int m_s7PageScroll{0};
    clocktime::ClockDisplaySettings m_clockDisplaySettings{};
    DateTimeSnapshot m_dateTimeSnapshot{};
    uint64_t m_nextDateTimeRefreshMs{0};

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
    int s7ContentTop() const { return kContentY + (smallSettingsLayout() ? 76 : 84); }
    int s7RowsTop() const { return s7ContentTop() + 16; }
    int s7RowPitch() const { return 36; }
    int s7ViewportBottom() const { return std::max(s7ContentTop() + 1, m_height - 16); }
    int s7ViewportHeight() const { return s7ViewportBottom() - s7ContentTop(); }
    int s7RowCount() const
    {
        if (m_navigation.selectedCategory() == CategoryId::Accessibility) return 8;
        if (m_navigation.selectedCategory() != CategoryId::Developer) return 0;
        switch (m_navigation.route().target) {
        case TargetId::Page: return 9;
        case TargetId::DeveloperApps: return 9;
        case TargetId::DeveloperServices: return 8;
        case TargetId::DeveloperDiagnostics: return 11;
        default: return 0;
        }
    }
    int s7MaximumScroll() const
    {
        return maximumSettingsPageScroll(s7RowCount(), s7RowPitch(),
            std::max(1, s7ViewportHeight() - 16));
    }
    int s7RowY(int index) const
    {
        return s7RowsTop() + std::max(0, index) * s7RowPitch() - m_s7PageScroll;
    }
    int s7CardHeight(int rowCount) const
    {
        return std::min(rowCount * s7RowPitch() + 20,
            std::max(1, m_height - 16 - (s7ContentTop() - 5)));
    }
    bool s7RowFullyVisible(int row) const
    {
        const int y = s7RowY(row);
        return y >= s7ContentTop() && y + s7RowPitch() - 3 <= s7ViewportBottom();
    }
    size_t visibleSearchResultCount(size_t count) const { return count; }

    static bool sameFocus(const FocusItem& left, const FocusItem& right)
    {
        return left.kind == right.kind && left.index == right.index && left.control == right.control;
    }

    static FocusControl initialFocusFor(const SettingsRoute& route)
    {
        switch (route.target) {
        case TargetId::AppDetail: return FocusControl::AppsDetailBack;
        case TargetId::DateTimeTimeZone: return FocusControl::DateTimeAdvanced;
        case TargetId::PersonalizationBackground: return FocusControl::PersonalizationChooseBackground;
        case TargetId::Resolution: return FocusControl::DisplayResolution;
        case TargetId::DisplayMode: return FocusControl::DisplayMode;
        case TargetId::AccessibilityFocus: return FocusControl::AccessibilityEnhancedFocus;
        case TargetId::AccessibilityKeyboard: return FocusControl::AccessibilityKeyboard;
        case TargetId::DeveloperApps:
        case TargetId::DeveloperServices:
        case TargetId::DeveloperDiagnostics: return FocusControl::DeveloperBack;
        case TargetId::DeviceDetail: return FocusControl::DeviceDetailBack;
        case TargetId::StorageDiskDetail: return FocusControl::StorageDetailBack;
        default: return FocusControl::None;
        }
    }

    static bool inventoryCategory(CategoryId category)
    {
        return category == CategoryId::Devices || category == CategoryId::Storage;
    }

    bool refreshApps()
    {
        const AppInventory previous = m_appInventory;
        std::string focusedId;
        uint64_t focusedGeneration = 0;
        if (m_focusedItem.kind == FocusItem::Kind::Control &&
            m_focusedItem.control == FocusControl::AppEntry && m_focusedItem.index >= 0 &&
            static_cast<size_t>(m_focusedItem.index) < m_appInventory.count) {
            const AppInventoryEntry& focused = m_appInventory.entries[static_cast<size_t>(m_focusedItem.index)];
            focusedId = focused.appId;
            focusedGeneration = focused.registrationGeneration;
        }

        m_appInventory = DesktopService::GetSettingsAppModelInventory();
        const bool inventoryChanged = !sameAppInventory(previous, m_appInventory);
        const bool oldMissing = m_selectedAppMissing;
        if (m_navigation.route().target == TargetId::AppDetail && !m_selectedAppId.empty()) {
            m_selectedAppMissing = findAppInventoryEntry(
                m_appInventory, m_selectedAppId, m_selectedAppGeneration) == nullptr;
            if (m_selectedAppMissing) {
                m_focusedItem = FocusItem{ FocusItem::Kind::Control, 0, FocusControl::AppsDetailBack };
            }
        }

        if (!focusedId.empty() && m_navigation.route().target != TargetId::AppDetail) {
            bool restoredFocus = false;
            for (size_t i = 0; i < m_appInventory.count; ++i) {
                const AppInventoryEntry& entry = m_appInventory.entries[i];
                if (entry.appId == focusedId && entry.registrationGeneration == focusedGeneration) {
                    m_focusedItem.index = static_cast<int>(i);
                    restoredFocus = true;
                    break;
                }
            }
            if (!restoredFocus) {
                m_focusedItem = m_appInventory.count != 0
                    ? FocusItem{ FocusItem::Kind::Control, 0, FocusControl::AppEntry }
                    : FocusItem{ FocusItem::Kind::Category, static_cast<int>(CategoryId::Apps), FocusControl::None };
            }
        }
        clampInventoryScroll();
        ensureInventoryFocusVisible();
        m_nextAppsRefreshMs = steadyMilliseconds() + 2000;
        return inventoryChanged || oldMissing != m_selectedAppMissing;
    }

    void refreshDeveloperInventory()
    {
        m_appInventory = DesktopService::GetSettingsAppModelInventory();
        m_developerAppModel = buildDeveloperAppModelSnapshot(m_appInventory);
        const DeveloperToolStatus keyboard = findDeveloperTool(
            m_appInventory, "gxos.builtin.onscreenkeyboard");
        m_accessibilityKeyboardAvailable = developerToolActionEnabled(keyboard);
    }

    void refreshDeveloperServices()
    {
        network_settings::NetworkSnapshot network{};
        const network_settings::Result networkResult = readSettingsNetworkSnapshot(&network);
        inventory::DeviceSnapshot devices{};
        const system_service::ClientResult deviceResult = readSettingsDeviceSnapshot(&devices);
        inventory::StorageSnapshot storage{};
        const system_service::ClientResult storageResult = readSettingsStorageSnapshot(&storage);
        m_developerServiceNetworkAvailable = networkResult == network_settings::Result::Ok;
        m_developerServiceDevicesAvailable = deviceResult == system_service::ClientResult::Ok;
        m_developerServiceStorageAvailable = storageResult == system_service::ClientResult::Ok;
        m_developerServicesChecked = true;
    }

    void refreshDeveloperDiagnostics()
    {
        m_developerAppModelStatus = "Unavailable";
        m_developerAppModelStatusSource = "Unavailable";
        const std::string appModel = DesktopService::AppModelSummaryDiagnostic();
        const std::string status = diagnosticFieldValue(appModel, "appModelV1Status");
        const std::string source = diagnosticFieldValue(appModel, "appModelV1StatusSource");
        if (!status.empty()) m_developerAppModelStatus = status;
        if (!source.empty()) m_developerAppModelStatusSource = source;

        m_developerPreviewAvailable = false;
        m_developerPreviewTotal = 0;
        m_developerPreviewUnresolved = 0;
        m_developerPreviewHighRisk = 0;
        const std::string preview = DesktopService::LaunchStoragePreviewDiagnostic();
        m_developerPreviewAvailable =
            parseDiagnosticCount(preview, "totalRecords", m_developerPreviewTotal) &&
            parseDiagnosticCount(preview, "unresolved", m_developerPreviewUnresolved) &&
            parseDiagnosticCount(preview, "highRisk", m_developerPreviewHighRisk);
    }

    static std::string registeredToolText(const DeveloperToolStatus& tool)
    {
        if (!tool.registered) return "Not registered";
        if (!tool.openSupported) return "Open unavailable";
        return "Available";
    }

    void launchDeveloperTool(const DeveloperToolStatus& tool, const char* appId)
    {
        if (!developerToolActionEnabled(tool) || !appId) return;
        std::string error;
        if (!DesktopService::LaunchApp(appId, error))
            Logger::write(LogLevel::Warn, std::string("Settings App Model tool launch failed: ") + appId +
                (error.empty() ? std::string() : ": " + error));
    }

    void toggleEnhancedFocusIndicator()
    {
        DesktopFocusPreferenceStore store;
        bool authoritative = m_accessibilityPreferences.enhancedFocusIndicator;
        const bool requested = !authoritative;
        const AccessibilityMutationResult result = updateEnhancedFocusPreference(
            store, requested, [](bool enabled) {
                FocusIndicator::SetEnhancedFocusEnabled(enabled);
                publish(MsgType::MT_DesktopConfigReload, std::string());
                return FocusIndicator::EnhancedFocusEnabled() == enabled;
            }, authoritative);
        m_accessibilityPreferences.enhancedFocusIndicator = authoritative;
        if (result != AccessibilityMutationResult::Applied) {
            Logger::write(LogLevel::Warn, "Settings could not persist, reread, or apply the enhanced focus preference.");
        }
    }

    void refreshDateTime()
    {
        m_clockDisplaySettings = readHostedClockDisplaySettings();
        m_dateTimeSnapshot = readHostedDateTime(m_clockDisplaySettings);
        m_nextDateTimeRefreshMs = steadyMilliseconds() + 1000;
    }

    bool refreshInventory()
    {
        bool changed = false;
        if (m_navigation.selectedCategory() == CategoryId::Devices) {
            const uint64_t oldGeneration = m_deviceSnapshot.generation;
            const inventory::SnapshotState oldState = m_deviceSnapshot.state;
            const uint16_t oldCount = m_deviceSnapshot.deviceCount;
            const auto oldResult = m_deviceReadResult;
            inventory::DeviceSnapshot latest{};
            m_deviceReadResult = readSettingsDeviceSnapshot(&latest);
            m_deviceSnapshot = latest;
            changed = oldGeneration != latest.generation || oldState != latest.state ||
                oldCount != latest.deviceCount || oldResult != m_deviceReadResult;
        } else if (m_navigation.selectedCategory() == CategoryId::Storage) {
            const uint64_t oldGeneration = m_storageSnapshot.generation;
            const inventory::SnapshotState oldState = m_storageSnapshot.state;
            const uint16_t oldDisks = m_storageSnapshot.diskCount;
            const uint16_t oldVolumes = m_storageSnapshot.volumeCount;
            const auto oldResult = m_storageReadResult;
            inventory::StorageSnapshot latest{};
            m_storageReadResult = readSettingsStorageSnapshot(&latest);
            m_storageSnapshot = latest;
            changed = oldGeneration != latest.generation || oldState != latest.state ||
                oldDisks != latest.diskCount || oldVolumes != latest.volumeCount ||
                oldResult != m_storageReadResult;
        }
        m_inventoryRefreshPolicy.completed(steadyMilliseconds());
        return changed;
    }

    std::vector<int> deviceDisplayIndices() const
    {
        std::vector<int> indices;
        const size_t count = std::min<size_t>(m_deviceSnapshot.deviceCount, inventory::kMaxDevices);
        for (size_t i = 0; i < count; ++i) {
            if (deviceMatchesFilter(m_deviceSnapshot.devices[i].category, m_navigation.route().target))
                indices.push_back(static_cast<int>(i));
        }
        return indices;
    }

    struct StorageListEntry { bool volume{false}; int index{0}; };

    std::vector<StorageListEntry> storageDisplayEntries() const
    {
        std::vector<StorageListEntry> entries;
        const TargetId target = m_navigation.route().target;
        if (target != TargetId::StorageVolumes) {
            for (uint16_t i = 0; i < m_storageSnapshot.diskCount && i < inventory::kMaxDisks; ++i)
                entries.push_back(StorageListEntry{ false, static_cast<int>(i) });
        }
        if (target != TargetId::StorageDisks) {
            for (uint16_t i = 0; i < m_storageSnapshot.volumeCount && i < inventory::kMaxVolumes; ++i)
                entries.push_back(StorageListEntry{ true, static_cast<int>(i) });
        }
        return entries;
    }

    int inventoryListTop() const { return kContentY + 96; }
    int inventoryListBottom() const
    {
        const int reserved = m_navigation.selectedCategory() == CategoryId::Storage
            ? (smallSettingsLayout() ? 66 : 78) : 24;
        const int actionReserved = m_navigation.selectedCategory() == CategoryId::Apps &&
            m_navigation.route().target == TargetId::AppDetail
            ? (smallSettingsLayout() ? 66 : 78) : reserved;
        return std::max(inventoryListTop() + 48, m_height - actionReserved);
    }
    int inventoryRowPitch() const { return smallSettingsLayout() ? 48 : 56; }
    int inventoryActionY() const { return m_height - (smallSettingsLayout() ? 54 : 62); }
    int inventoryVisibleRows() const
    {
        return std::max(1, (inventoryListBottom() - inventoryListTop()) / inventoryRowPitch());
    }
    int dateTimeTop() const { return inventoryListTop(); }
    int dateTimeClockCardHeight() const { return smallSettingsLayout() ? 112 : 144; }
    int dateTimeZoneCardY() const { return dateTimeTop() + dateTimeClockCardHeight() + 10; }
    int dateTimeZoneCardHeight() const { return smallSettingsLayout() ? 168 : 184; }
    int dateTimeActionY() const { return dateTimeZoneCardY() + (smallSettingsLayout() ? 134 : 142); }
    int dateTimeActionHeight() const { return smallSettingsLayout() ? 30 : 36; }

    void clampInventoryScroll()
    {
        const int deviceMax = std::max(0, static_cast<int>(deviceDisplayIndices().size()) - inventoryVisibleRows());
        const int storageMax = std::max(0, static_cast<int>(storageDisplayEntries().size()) - inventoryVisibleRows());
        const int appMax = std::max(0, static_cast<int>(m_appInventory.count) - inventoryVisibleRows());
        m_deviceScroll = std::max(0, std::min(m_deviceScroll, deviceMax));
        m_storageScroll = std::max(0, std::min(m_storageScroll, storageMax));
        m_appScroll = std::max(0, std::min(m_appScroll, appMax));
    }

    void ensureInventoryFocusVisible()
    {
        if (m_focusedItem.kind != FocusItem::Kind::Control) return;
        if (m_focusedItem.control == FocusControl::DeviceEntry) {
            const std::vector<int> indices = deviceDisplayIndices();
            for (size_t i = 0; i < indices.size(); ++i) {
                if (indices[i] != m_focusedItem.index) continue;
                if (static_cast<int>(i) < m_deviceScroll) m_deviceScroll = static_cast<int>(i);
                else if (static_cast<int>(i) >= m_deviceScroll + inventoryVisibleRows())
                    m_deviceScroll = static_cast<int>(i) - inventoryVisibleRows() + 1;
                break;
            }
        } else if (m_focusedItem.control == FocusControl::StorageDiskEntry ||
                   m_focusedItem.control == FocusControl::StorageVolumeEntry) {
            const std::vector<StorageListEntry> entries = storageDisplayEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].index != m_focusedItem.index ||
                    entries[i].volume != (m_focusedItem.control == FocusControl::StorageVolumeEntry)) continue;
                if (static_cast<int>(i) < m_storageScroll) m_storageScroll = static_cast<int>(i);
                else if (static_cast<int>(i) >= m_storageScroll + inventoryVisibleRows())
                    m_storageScroll = static_cast<int>(i) - inventoryVisibleRows() + 1;
                break;
            }
        } else if (m_focusedItem.control == FocusControl::AppEntry && m_focusedItem.index >= 0 &&
                   static_cast<size_t>(m_focusedItem.index) < m_appInventory.count) {
            if (m_focusedItem.index < m_appScroll) m_appScroll = m_focusedItem.index;
            else if (m_focusedItem.index >= m_appScroll + inventoryVisibleRows())
                m_appScroll = m_focusedItem.index - inventoryVisibleRows() + 1;
        }
        clampInventoryScroll();
    }

    const inventory::DeviceInfo* selectedDevice() const
    {
        if (!selectionGenerationMatches(m_selectedDeviceGeneration, m_deviceSnapshot.generation)) return nullptr;
        for (uint16_t i = 0; i < m_deviceSnapshot.deviceCount && i < inventory::kMaxDevices; ++i) {
            if (m_selectedDeviceStableId == m_deviceSnapshot.devices[i].stableId)
                return &m_deviceSnapshot.devices[i];
        }
        return nullptr;
    }

    const inventory::DiskInfo* selectedDisk() const
    {
        if (!selectionGenerationMatches(m_selectedDiskGeneration, m_storageSnapshot.generation)) return nullptr;
        for (uint16_t i = 0; i < m_storageSnapshot.diskCount && i < inventory::kMaxDisks; ++i) {
            if (m_selectedDiskStableId == m_storageSnapshot.disks[i].stableId)
                return &m_storageSnapshot.disks[i];
        }
        return nullptr;
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

    void drawFocusOutline(int x, int y, int width, int height) const
    {
        if (!FocusIndicator::EnhancedFocusEnabled()) {
            drawOutline(x, y, width, height, accentColor());
            return;
        }
        drawOutline(x, y, width, height, packRgb(255, 255, 255));
        if (width > 4 && height > 4)
            drawOutline(x + 2, y + 2, width - 4, height - 4, packRgb(0, 0, 0));
        if (width > 8 && height > 8)
            drawOutline(x + 4, y + 4, width - 8, height - 8, accentColor());
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
        if (focused && enabled) drawFocusOutline(x, y, width, height);
        else if (focused && !hover) drawRect(x + 2, y + 2, width - 4, 1, accentColor());
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
        if (focused) drawFocusOutline(x, y, width, rowHeight);
        else if (hover || highlighted) drawOutline(x, y, width, rowHeight, accentColor());
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
            add(FocusControl::SystemDevices);
            add(FocusControl::SystemAbout);
            add(FocusControl::SystemControlPanel);
            break;
        case CategoryId::Personalization:
            add(FocusControl::PersonalizationChooseBackground);
            add(FocusControl::PersonalizationAdvanced);
            break;
        case CategoryId::DateTime: add(FocusControl::DateTimeAdvanced); break;
        case CategoryId::Apps:
            if (m_navigation.route().target == TargetId::AppDetail) {
                add(FocusControl::AppsDetailBack);
                const AppInventoryEntry* selected = findAppInventoryEntry(
                    m_appInventory, m_selectedAppId, m_selectedAppGeneration);
                if (selected && selected->openSupported) add(FocusControl::AppsOpen);
            } else {
                for (size_t i = 0; i < m_appInventory.count; ++i)
                    items.push_back(FocusItem{ FocusItem::Kind::Control, static_cast<int>(i), FocusControl::AppEntry });
            }
            break;
        case CategoryId::Devices:
            if (m_navigation.route().target == TargetId::DeviceDetail) {
                add(FocusControl::DeviceDetailBack);
                const inventory::DeviceInfo* device = selectedDevice();
                if (device && device->category == inventory::DeviceCategory::Network) add(FocusControl::DeviceDetailNetwork);
                if (device && device->category == inventory::DeviceCategory::Display) add(FocusControl::DeviceDetailDisplay);
                if (device && device->category == inventory::DeviceCategory::Storage) add(FocusControl::DeviceDetailStorage);
            } else {
                for (int index : deviceDisplayIndices())
                    items.push_back(FocusItem{ FocusItem::Kind::Control, index, FocusControl::DeviceEntry });
            }
            break;
        case CategoryId::Storage:
            if (m_navigation.route().target == TargetId::StorageDiskDetail) {
                add(FocusControl::StorageDetailBack);
                add(FocusControl::StorageDiskManager);
            } else {
                for (const StorageListEntry& entry : storageDisplayEntries()) {
                    const FocusControl control = entry.volume ? FocusControl::StorageVolumeEntry : FocusControl::StorageDiskEntry;
                    items.push_back(FocusItem{ FocusItem::Kind::Control, entry.index, control });
                }
                add(FocusControl::StorageDiskManager);
            }
            break;
        case CategoryId::Developer:
            if (m_navigation.route().target == TargetId::Page) {
                if (developerToolActionEnabled(m_developerAppModel.developerStudio)) add(FocusControl::DeveloperStudio);
                if (developerToolActionEnabled(m_developerAppModel.console)) add(FocusControl::DeveloperConsole);
                add(FocusControl::DeveloperApps);
                add(FocusControl::DeveloperServices);
                add(FocusControl::DeveloperDiagnostics);
            } else {
                add(FocusControl::DeveloperBack);
                if (m_navigation.route().target == TargetId::DeveloperServices ||
                    m_navigation.route().target == TargetId::DeveloperDiagnostics)
                    add(FocusControl::DeveloperRefresh);
            }
            break;
        case CategoryId::Accessibility:
            add(FocusControl::AccessibilityEnhancedFocus);
            if (m_accessibilityKeyboardAvailable) add(FocusControl::AccessibilityKeyboard);
            break;
        default: break;
        }
        return items;
    }

    void cycleFocus(bool reverse)
    {
        std::vector<FocusItem> items = focusOrder();
        if (items.empty()) return;
        int current = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            if (sameFocus(items[i], m_focusedItem)) { current = static_cast<int>(i); break; }
        }
        const size_t next = nextSettingsFocusIndex(items.size(), current, reverse);
        m_focusedItem = items[next];
        ensureInventoryFocusVisible();
        ensureS7FocusVisible();
    }

    int s7FocusRow(FocusControl control) const
    {
        switch (control) {
        case FocusControl::AccessibilityEnhancedFocus: return 0;
        case FocusControl::AccessibilityKeyboard: return 1;
        case FocusControl::DeveloperStudio: return 0;
        case FocusControl::DeveloperConsole: return 1;
        case FocusControl::DeveloperApps: return 2;
        case FocusControl::DeveloperServices: return 3;
        case FocusControl::DeveloperDiagnostics: return 4;
        case FocusControl::DeveloperRefresh:
            return m_navigation.route().target == TargetId::DeveloperServices ? 6 : 9;
        case FocusControl::DeveloperBack:
            return m_navigation.route().target == TargetId::DeveloperApps ? 8 :
                m_navigation.route().target == TargetId::DeveloperServices ? 7 : 10;
        default: return -1;
        }
    }

    void ensureS7FocusVisible()
    {
        if (m_focusedItem.kind != FocusItem::Kind::Control) return;
        const int row = s7FocusRow(m_focusedItem.control);
        if (row < 0) return;
        int y = s7RowY(row);
        const int rowHeight = s7RowPitch() - 3;
        if (y < s7ContentTop()) m_s7PageScroll -= s7ContentTop() - y;
        else if (y + rowHeight > s7ViewportBottom())
            m_s7PageScroll += y + rowHeight - s7ViewportBottom();
        m_s7PageScroll = clampSettingsPageScroll(m_s7PageScroll, s7MaximumScroll());
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
        } else if (m_focusedItem.kind == FocusItem::Kind::Control &&
                   m_focusedItem.control == FocusControl::DeviceEntry) {
            const std::vector<int> indices = deviceDisplayIndices();
            if (!indices.empty()) {
                size_t current = 0;
                for (size_t i = 0; i < indices.size(); ++i)
                    if (indices[i] == m_focusedItem.index) { current = i; break; }
                const int next = (static_cast<int>(current) + delta + static_cast<int>(indices.size())) % static_cast<int>(indices.size());
                m_focusedItem.index = indices[static_cast<size_t>(next)];
                ensureInventoryFocusVisible();
            }
        } else if (m_focusedItem.kind == FocusItem::Kind::Control &&
                   (m_focusedItem.control == FocusControl::StorageDiskEntry ||
                    m_focusedItem.control == FocusControl::StorageVolumeEntry)) {
            const std::vector<StorageListEntry> entries = storageDisplayEntries();
            if (!entries.empty()) {
                size_t current = 0;
                for (size_t i = 0; i < entries.size(); ++i)
                    if (entries[i].volume == (m_focusedItem.control == FocusControl::StorageVolumeEntry) &&
                        entries[i].index == m_focusedItem.index) { current = i; break; }
                const int next = std::max(0, std::min(static_cast<int>(entries.size()) - 1,
                    static_cast<int>(current) + delta));
                m_focusedItem.index = entries[static_cast<size_t>(next)].index;
                m_focusedItem.control = entries[static_cast<size_t>(next)].volume
                    ? FocusControl::StorageVolumeEntry : FocusControl::StorageDiskEntry;
                ensureInventoryFocusVisible();
            }
        } else if (m_focusedItem.kind == FocusItem::Kind::Control &&
                   m_focusedItem.control == FocusControl::AppEntry && m_appInventory.count != 0) {
            const int count = static_cast<int>(m_appInventory.count);
            const int next = (m_focusedItem.index + delta + count) % count;
            m_focusedItem.index = next;
            ensureInventoryFocusVisible();
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
            if (controlHit(item, x, y)) return item;
        }
        return FocusItem{};
    }

    bool controlHit(const FocusItem& focus, int x, int y) const
    {
        const FocusControl control = focus.control;
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
        case FocusControl::SystemDevices: return inRect(x, y, systemLinkX(3), systemLinkY(3), systemLinkWidth(), systemLinkHeight());
        case FocusControl::SystemAbout: return inRect(x, y, systemLinkX(4), systemLinkY(4), systemLinkWidth(), systemLinkHeight());
        case FocusControl::SystemControlPanel: return inRect(x, y, systemLinkX(5), systemLinkY(5), systemLinkWidth(), systemLinkHeight());
        case FocusControl::PersonalizationChooseBackground: return inRect(x, y, pageX() + 8, personalizationChooseY(), std::min(390, pageWidth() - 16), personalizationActionHeight());
        case FocusControl::PersonalizationAdvanced: return inRect(x, y, pageX() + 8, personalizationAdvancedY(), std::min(390, pageWidth() - 16), personalizationActionHeight());
        case FocusControl::DateTimeAdvanced: return inRect(x, y, pageX() + 8, dateTimeActionY(),
            std::min(390, pageWidth() - 16), dateTimeActionHeight());
        case FocusControl::AccessibilityEnhancedFocus:
            return m_accessibilityPreferenceAvailable && s7RowFullyVisible(0) &&
                inRect(x, y, pageX() + 8, s7RowY(0), pageWidth() - 16, s7RowPitch() - 2);
        case FocusControl::AccessibilityKeyboard:
            return m_accessibilityKeyboardAvailable && s7RowFullyVisible(1) &&
                inRect(x, y, pageX() + 8, s7RowY(1), pageWidth() - 16, s7RowPitch() - 2);
        case FocusControl::DeveloperStudio:
            return m_navigation.route().target == TargetId::Page && developerToolActionEnabled(m_developerAppModel.developerStudio) &&
                s7RowFullyVisible(0) &&
                inRect(x, y, pageX() + 8, s7RowY(0), pageWidth() - 16, s7RowPitch() - 2);
        case FocusControl::DeveloperConsole:
            return m_navigation.route().target == TargetId::Page && developerToolActionEnabled(m_developerAppModel.console) &&
                s7RowFullyVisible(1) &&
                inRect(x, y, pageX() + 8, s7RowY(1), pageWidth() - 16, s7RowPitch() - 2);
        case FocusControl::DeveloperApps:
            return m_navigation.route().target == TargetId::Page &&
                s7RowFullyVisible(2) &&
                inRect(x, y, pageX() + 8, s7RowY(2), pageWidth() - 16, s7RowPitch() - 2);
        case FocusControl::DeveloperServices:
            return m_navigation.route().target == TargetId::Page &&
                s7RowFullyVisible(3) &&
                inRect(x, y, pageX() + 8, s7RowY(3), pageWidth() - 16, s7RowPitch() - 2);
        case FocusControl::DeveloperDiagnostics:
            return m_navigation.route().target == TargetId::Page &&
                s7RowFullyVisible(4) &&
                inRect(x, y, pageX() + 8, s7RowY(4), pageWidth() - 16, s7RowPitch() - 2);
        case FocusControl::DeveloperRefresh: {
            const int row = m_navigation.route().target == TargetId::DeveloperServices ? 6 : 9;
            return s7RowFullyVisible(row) &&
                inRect(x, y, pageX() + 8, s7RowY(row), pageWidth() - 16, s7RowPitch() - 2);
        }
        case FocusControl::DeveloperBack: {
            int row = 7;
            if (m_navigation.route().target == TargetId::DeveloperApps) row = 8;
            else if (m_navigation.route().target == TargetId::DeveloperDiagnostics) row = 10;
            return s7RowFullyVisible(row) &&
                inRect(x, y, pageX() + 8, s7RowY(row), pageWidth() - 16, s7RowPitch() - 2);
        }
        case FocusControl::StorageDiskManager: return inRect(x, y, pageX() + 6, inventoryActionY(), std::min(390, pageWidth() - 12), smallSettingsLayout() ? 34 : 42);
        case FocusControl::DeviceEntry: {
            const std::vector<int> indices = deviceDisplayIndices();
            for (size_t i = 0; i < indices.size(); ++i) {
                if (indices[i] != focus.index) continue;
                const int ordinal = static_cast<int>(i);
                if (ordinal < m_deviceScroll || ordinal >= m_deviceScroll + inventoryVisibleRows()) continue;
                const int rowY = inventoryListTop() + (ordinal - m_deviceScroll) * inventoryRowPitch();
                if (inRect(x, y, pageX() + 8, rowY, pageWidth() - 16, inventoryRowPitch() - 4)) return true;
            }
            return false;
        }
        case FocusControl::AppEntry: {
            if (m_navigation.route().target == TargetId::AppDetail || focus.index < m_appScroll ||
                focus.index >= m_appScroll + inventoryVisibleRows() || focus.index < 0 ||
                static_cast<size_t>(focus.index) >= m_appInventory.count) return false;
            const int rowY = inventoryListTop() + (focus.index - m_appScroll) * inventoryRowPitch();
            return inRect(x, y, pageX() + 8, rowY, pageWidth() - 16, inventoryRowPitch() - 4);
        }
        case FocusControl::StorageDiskEntry:
        case FocusControl::StorageVolumeEntry: {
            const std::vector<StorageListEntry> entries = storageDisplayEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].index != focus.index ||
                    entries[i].volume != (control == FocusControl::StorageVolumeEntry)) continue;
                const int ordinal = static_cast<int>(i);
                if (ordinal < m_storageScroll || ordinal >= m_storageScroll + inventoryVisibleRows()) return false;
                const int rowY = inventoryListTop() + (ordinal - m_storageScroll) * inventoryRowPitch();
                return inRect(x, y, pageX() + 8, rowY, pageWidth() - 16, inventoryRowPitch() - 4);
            }
            return false;
        }
        case FocusControl::DeviceDetailBack:
        case FocusControl::StorageDetailBack:
        case FocusControl::AppsDetailBack: return inRect(x, y, pageX() + 8, inventoryListTop(), std::min(300, pageWidth() - 16), smallSettingsLayout() ? 34 : 42);
        case FocusControl::AppsOpen: return inRect(x, y, pageX() + 8, inventoryActionY(), std::min(390, pageWidth() - 16), smallSettingsLayout() ? 34 : 42);
        case FocusControl::DeviceDetailNetwork:
        case FocusControl::DeviceDetailDisplay:
        case FocusControl::DeviceDetailStorage: return inRect(x, y, pageX() + 8, inventoryActionY(), std::min(390, pageWidth() - 16), smallSettingsLayout() ? 34 : 42);
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
        if (route.category == CategoryId::Apps) refreshApps();
        if (route.category == CategoryId::DateTime) refreshDateTime();
        if (route.category == CategoryId::Accessibility || route.category == CategoryId::Developer)
            refreshDeveloperInventory();
        if (route.category == CategoryId::Developer && route.target == TargetId::DeveloperServices)
            refreshDeveloperServices();
        if (route.category == CategoryId::Developer && route.target == TargetId::DeveloperDiagnostics)
            refreshDeveloperDiagnostics();
        const bool networkVisible = m_windowFocused && route.category == CategoryId::Network;
        m_networkRefreshPolicy.setActive(networkVisible, steadyMilliseconds());
        if (networkVisible && previousCategory != CategoryId::Network) refreshNetwork();
        const bool inventoryVisible = m_windowFocused && inventoryCategory(route.category);
        m_inventoryRefreshPolicy.setActive(inventoryVisible, steadyMilliseconds());
        if (inventoryVisible && (previousCategory != route.category || !inventoryCategory(previousCategory)))
            refreshInventory();
        setFocusForRoute(route);
        m_search.clear();
        m_s7PageScroll = 0;
        ensureS7FocusVisible();
    }

    void setFocusForRoute(const SettingsRoute& route)
    {
        const FocusControl requested = initialFocusFor(route);
        const bool focusable = requested == FocusControl::PersonalizationChooseBackground
            ? route.category == CategoryId::Personalization
            : requested == FocusControl::DeviceDetailBack
            ? route.category == CategoryId::Devices && route.target == TargetId::DeviceDetail
            : requested == FocusControl::StorageDetailBack
            ? route.category == CategoryId::Storage && route.target == TargetId::StorageDiskDetail
            : requested == FocusControl::AppsDetailBack
            ? route.category == CategoryId::Apps && route.target == TargetId::AppDetail
            : requested == FocusControl::DateTimeAdvanced
            ? route.category == CategoryId::DateTime && route.target == TargetId::DateTimeTimeZone
            : requested == FocusControl::AccessibilityEnhancedFocus
            ? route.category == CategoryId::Accessibility && m_accessibilityPreferenceAvailable
            : requested == FocusControl::AccessibilityKeyboard
            ? route.category == CategoryId::Accessibility && m_accessibilityKeyboardAvailable
            : requested == FocusControl::DeveloperBack
            ? route.category == CategoryId::Developer && route.target != TargetId::Page
            : requested == FocusControl::DeveloperRefresh
            ? route.category == CategoryId::Developer && route.target != TargetId::Page
            : requested == FocusControl::DisplayResolution
            ? m_display.available && m_display.supportedModes.size() > 1 && m_display.active.outputCount == 1
            : requested == FocusControl::DisplayMode ? m_display.available && m_display.active.outputCount > 1 : false;
        m_focusedItem = focusable
            ? FocusItem{ FocusItem::Kind::Control, 0, requested }
            : FocusItem{ FocusItem::Kind::Category, static_cast<int>(route.category), FocusControl::None };
        ensureS7FocusVisible();
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
            case FocusControl::SystemDevices: navigateTo(SettingsRoute{ CategoryId::Devices, TargetId::Page }); break;
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
            case FocusControl::AccessibilityEnhancedFocus: toggleEnhancedFocusIndicator(); break;
            case FocusControl::AccessibilityKeyboard: {
                refreshDeveloperInventory();
                const DeveloperToolStatus keyboard = findDeveloperTool(m_appInventory, "gxos.builtin.onscreenkeyboard");
                if (!developerToolActionEnabled(keyboard)) {
                    Logger::write(LogLevel::Warn, "Settings on-screen keyboard action is unavailable: App Model registration missing or unsupported.");
                    break;
                }
                std::string error;
                if (!DesktopService::LaunchApp("gxos.builtin.onscreenkeyboard", error))
                    Logger::write(LogLevel::Warn, std::string("Settings on-screen keyboard launch failed through App Model") +
                        (error.empty() ? std::string() : ": " + error));
                break;
            }
            case FocusControl::DeveloperStudio:
                refreshDeveloperInventory();
                launchDeveloperTool(m_developerAppModel.developerStudio, kDeveloperStudioAppId);
                break;
            case FocusControl::DeveloperConsole:
                refreshDeveloperInventory();
                launchDeveloperTool(m_developerAppModel.console, kConsoleAppId);
                break;
            case FocusControl::DeveloperApps:
                navigateTo(SettingsRoute{ CategoryId::Developer, TargetId::DeveloperApps });
                break;
            case FocusControl::DeveloperServices:
                navigateTo(SettingsRoute{ CategoryId::Developer, TargetId::DeveloperServices });
                break;
            case FocusControl::DeveloperDiagnostics:
                navigateTo(SettingsRoute{ CategoryId::Developer, TargetId::DeveloperDiagnostics });
                break;
            case FocusControl::DeveloperRefresh:
                if (m_navigation.route().target == TargetId::DeveloperServices) refreshDeveloperServices();
                else if (m_navigation.route().target == TargetId::DeveloperDiagnostics) refreshDeveloperDiagnostics();
                break;
            case FocusControl::DeveloperBack:
                navigateTo(SettingsRoute{ CategoryId::Developer, TargetId::Page });
                break;
            case FocusControl::AppEntry:
                if (item.index >= 0 && static_cast<size_t>(item.index) < m_appInventory.count) {
                    const AppInventoryEntry& selected = m_appInventory.entries[static_cast<size_t>(item.index)];
                    m_selectedAppId = selected.appId;
                    m_selectedAppGeneration = selected.registrationGeneration;
                    m_selectedAppMissing = false;
                    m_appStatus.clear();
                    navigateTo(SettingsRoute{ CategoryId::Apps, TargetId::AppDetail });
                }
                break;
            case FocusControl::AppsDetailBack: {
                for (size_t i = 0; i < m_appInventory.count; ++i) {
                    const AppInventoryEntry& entry = m_appInventory.entries[i];
                    if (entry.appId == m_selectedAppId && entry.registrationGeneration == m_selectedAppGeneration) {
                        m_focusedItem = FocusItem{ FocusItem::Kind::Control, static_cast<int>(i), FocusControl::AppEntry };
                        break;
                    }
                }
                m_selectedAppId.clear();
                m_selectedAppGeneration = 0;
                m_selectedAppMissing = false;
                m_appStatus.clear();
                navigateTo(SettingsRoute{ CategoryId::Apps, TargetId::AppsList });
                break;
            }
            case FocusControl::AppsOpen: {
                const AppInventoryEntry* selected = findAppInventoryEntry(
                    m_appInventory, m_selectedAppId, m_selectedAppGeneration);
                if (!selected || !selected->openSupported) {
                    m_appStatus = "This app is no longer available to open.";
                    break;
                }
                std::string error;
                if (DesktopService::LaunchApp(selected->appId, error)) {
                    m_appStatus = "Open request sent through the App Model.";
                } else {
                    m_appStatus = error.empty() ? "The App Model could not open this app." : error;
                }
                break;
            }
            case FocusControl::StorageDiskManager: launchAdvanced("DiskManager"); break;
            case FocusControl::DeviceEntry:
                if (item.index >= 0 && item.index < static_cast<int>(m_deviceSnapshot.deviceCount) &&
                    static_cast<size_t>(item.index) < inventory::kMaxDevices) {
                    m_selectedDeviceStableId = m_deviceSnapshot.devices[item.index].stableId;
                    m_selectedDeviceGeneration = m_deviceSnapshot.generation;
                    navigateTo(SettingsRoute{ CategoryId::Devices, TargetId::DeviceDetail });
                }
                break;
            case FocusControl::DeviceDetailBack:
                m_selectedDeviceStableId.clear();
                navigateTo(SettingsRoute{ CategoryId::Devices, TargetId::Page });
                break;
            case FocusControl::DeviceDetailNetwork: navigateTo(SettingsRoute{ CategoryId::Network, TargetId::Page }); break;
            case FocusControl::DeviceDetailDisplay: navigateTo(SettingsRoute{ CategoryId::Display, TargetId::Page }); break;
            case FocusControl::DeviceDetailStorage: navigateTo(SettingsRoute{ CategoryId::Storage, TargetId::Page }); break;
            case FocusControl::StorageDiskEntry:
                if (item.index >= 0 && item.index < static_cast<int>(m_storageSnapshot.diskCount) &&
                    static_cast<size_t>(item.index) < inventory::kMaxDisks) {
                    m_storageScroll = 0;
                    m_selectedDiskStableId = m_storageSnapshot.disks[item.index].stableId;
                    m_selectedDiskGeneration = m_storageSnapshot.generation;
                    navigateTo(SettingsRoute{ CategoryId::Storage, TargetId::StorageDiskDetail });
                }
                break;
            case FocusControl::StorageVolumeEntry:
                if (item.index >= 0 && item.index < static_cast<int>(m_storageSnapshot.volumeCount) &&
                    static_cast<size_t>(item.index) < inventory::kMaxVolumes) {
                    const inventory::VolumeInfo& volume = m_storageSnapshot.volumes[item.index];
                    for (uint16_t i = 0; i < m_storageSnapshot.diskCount; ++i) {
                        if (volume.diskStableId[0] &&
                            std::string(m_storageSnapshot.disks[i].stableId) == volume.diskStableId) {
                            m_storageScroll = 0;
                            m_selectedDiskStableId = m_storageSnapshot.disks[i].stableId;
                            m_selectedDiskGeneration = m_storageSnapshot.generation;
                            navigateTo(SettingsRoute{ CategoryId::Storage, TargetId::StorageDiskDetail });
                            break;
                        }
                    }
                }
                break;
            case FocusControl::StorageDetailBack:
                m_selectedDiskStableId.clear();
                m_storageScroll = 0;
                navigateTo(SettingsRoute{ CategoryId::Storage, TargetId::Page });
                break;
            case FocusControl::SystemControlPanel: launchAdvanced("ControlPanel"); break;
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
                const TargetId target = results.values[i].route.target;
                const char* detail = target == TargetId::SystemDevice ? "Device information" :
                    target == TargetId::PersonalizationBackground ? "Background" :
                    target == TargetId::Resolution ? "Resolution" :
                    target == TargetId::DisplayMode ? "Display mode" :
                    target == TargetId::IPv4 ? "IP assignment" :
                    target == TargetId::DNS ? "DNS" :
                    target == TargetId::Gateway ? "Gateway" :
                    target == TargetId::StorageDisks ? "Disks and partitions" :
                    target == TargetId::StorageVolumes ? "Volumes" :
                    target == TargetId::DevicesInput ? "Input devices" :
                    target == TargetId::DevicesNetwork ? "Network adapters" :
                    target == TargetId::DevicesDisplay ? "Display adapters" :
                    target == TargetId::DevicesStorage ? "Storage devices" :
                    target == TargetId::DevicesAudio ? "Audio devices" :
                    target == TargetId::DevicesUsb ? "USB devices" : "Version";
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
            FocusControl::SystemStorage, FocusControl::SystemDevices, FocusControl::SystemAbout,
            FocusControl::SystemControlPanel
        };
        const char* labels[] = { "Display", "Network & Internet", "Storage", "Devices", "About", "Control Panel" };
        for (int i = 0; i < 6; ++i) {
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

    void renderDevices()
    {
        const int x = pageX();
        const int width = pageWidth();
        const int top = inventoryListTop();
        const int bottom = inventoryListBottom();
        std::vector<int> indices = deviceDisplayIndices();
        clampInventoryScroll();
        std::string countText;
        if (m_navigation.route().target == TargetId::Page) {
            countText = std::to_string(m_deviceSnapshot.deviceCount) + " devices shown";
            if (m_deviceSnapshot.truncated) countText += " · additional devices not displayed";
        } else {
            countText = std::to_string(indices.size()) + " matching devices";
            if (m_deviceSnapshot.truncated) countText += " · inventory truncated";
        }
        drawText(x + 8, top - 28, countText, mutedTextColor());
        const char* deviceSource = m_deviceSnapshot.backend == inventory::Backend::Kernel
            ? "Source: guideXOS kernel inventory"
            : m_deviceSnapshot.backend == inventory::Backend::HostedTest
                ? "Source: deterministic test inventory, not kernel hardware"
                : "Source: guideXOS device provider unavailable";
        drawText(x + 8, top - 12, deviceSource, mutedTextColor());

        drawRect(x + 4, top, width - 8, bottom - top, cardColor());
        drawOutline(x + 4, top, width - 8, bottom - top, borderColor());
        if (m_deviceReadResult != system_service::ClientResult::Ok ||
            m_deviceSnapshot.backend == inventory::Backend::Unavailable ||
            m_deviceSnapshot.state == inventory::SnapshotState::Unavailable) {
            drawText(x + 20, top + 24, "guideXOS device inventory unavailable", mutedTextColor());
            drawText(x + 20, top + 48, "Hosted Windows devices are not substituted.", mutedTextColor());
            return;
        }
        if (m_deviceSnapshot.state == inventory::SnapshotState::Empty) {
            drawText(x + 20, top + 24, "No devices available", mutedTextColor());
            return;
        }
        if (indices.empty()) {
            const char* filterName = m_navigation.route().target == TargetId::DevicesInput ? "input" :
                m_navigation.route().target == TargetId::DevicesNetwork ? "network" :
                m_navigation.route().target == TargetId::DevicesDisplay ? "display" :
                m_navigation.route().target == TargetId::DevicesStorage ? "storage" :
                m_navigation.route().target == TargetId::DevicesAudio ? "audio" :
                m_navigation.route().target == TargetId::DevicesUsb ? "USB" : "other";
            drawText(x + 20, top + 24, std::string("No ") + filterName + " devices reported by guideXOS.", mutedTextColor());
            return;
        }

        const size_t first = static_cast<size_t>(m_deviceScroll);
        const size_t end = std::min(indices.size(), first + static_cast<size_t>(inventoryVisibleRows()));
        for (size_t ordinal = first; ordinal < end; ++ordinal) {
            const int index = indices[ordinal];
            const inventory::DeviceInfo& device = m_deviceSnapshot.devices[index];
            const int rowY = top + static_cast<int>(ordinal - first) * inventoryRowPitch();
            const FocusItem row{ FocusItem::Kind::Control, index, FocusControl::DeviceEntry };
            const bool focused = sameFocus(row, m_focusedItem);
            const bool hovered = sameFocus(row, m_hoverItem);
            const uint32_t fill = hovered ? blendColor(cardColor(), accentColor(), 8) :
                focused ? blendColor(cardColor(), accentColor(), 5) : cardColor();
            drawRect(x + 10, rowY + 2, width - 28, inventoryRowPitch() - 6, fill);
            if (focused || hovered) drawOutline(x + 10, rowY + 2, width - 28, inventoryRowPitch() - 6, accentColor());
            const std::string name = device.name[0] ? device.name : "Device name unavailable";
            drawText(x + 20, rowY + 7, fitText(name, static_cast<size_t>(std::max(12, (width - 72) / 8))), textColor());
            const std::string subline = std::string(deviceCategoryName(device.category)) + " · " + deviceStatusName(device.status);
            drawText(x + 20, rowY + (smallSettingsLayout() ? 27 : 30),
                fitText(subline, static_cast<size_t>(std::max(12, (width - 72) / 8))), mutedTextColor());
            drawText(x + width - 34, rowY + 16, ">", accentColor());
        }
        if (indices.size() > static_cast<size_t>(inventoryVisibleRows()))
            drawText(x + 10, bottom - 17, "Use the mouse wheel or arrow keys to browse.", mutedTextColor());
    }

    void renderDeviceDetail()
    {
        const int x = pageX();
        const int width = pageWidth();
        const int top = inventoryListTop();
        const FocusItem back{ FocusItem::Kind::Control, 0, FocusControl::DeviceDetailBack };
        drawButton(x + 8, top, std::min(300, width - 16), smallSettingsLayout() ? 34 : 42,
            "Back to devices", false, sameFocus(m_hoverItem, back), sameFocus(m_focusedItem, back), true);
        const inventory::DeviceInfo* device = selectedDevice();
        const int cardY = top + (smallSettingsLayout() ? 40 : 48);
        const int cardBottom = inventoryListBottom();
        drawRect(x + 4, cardY, width - 8, cardBottom - cardY, cardColor());
        drawOutline(x + 4, cardY, width - 8, cardBottom - cardY, borderColor());
        if (!device) {
            const char* message = m_deviceReadResult != system_service::ClientResult::Ok
                ? "Device details unavailable."
                : selectionGenerationMatches(m_selectedDeviceGeneration, m_deviceSnapshot.generation)
                    ? "This device is no longer available."
                    : "The inventory changed. Return and select the device again.";
            drawText(x + 20, cardY + 24, message, mutedTextColor());
            return;
        }
        drawText(x + 18, cardY + 12,
            fitText(device->name[0] ? device->name : "Device name unavailable",
                static_cast<size_t>(std::max(12, (width - 40) / 8))), textColor());
        const std::string rows[][2] = {
            { "Type", deviceCategoryName(device->category) },
            { "Driver", boundedSettingValue(device->driver, 42) },
            { "Vendor/device ID", (device->flags & inventory::kDeviceFlagPciIdentity)
                ? formatPciId(device->vendorId, device->deviceId) : "Unavailable" },
            { "Location", boundedSettingValue(device->location, 48) },
            { "Status", deviceStatusName(device->status) },
            { "Identity", boundedSettingValue(device->stableId, 48) }
        };
        const int rowPitch = smallSettingsLayout() ? 26 : 34;
        for (int i = 0; i < 6; ++i) {
            const int rowY = cardY + (smallSettingsLayout() ? 38 : 44) + i * rowPitch;
            drawText(x + 18, rowY, rows[i][0], mutedTextColor());
            drawText(x + width / 2, rowY,
                fitText(rows[i][1], static_cast<size_t>(std::max(12, width / 16))), textColor());
        }
        if (device->flags & inventory::kDeviceFlagTextTruncated)
            drawText(x + 18, cardBottom - 18, "A device name was shortened to fit the bounded inventory.", mutedTextColor());

        FocusControl link = device->category == inventory::DeviceCategory::Network ? FocusControl::DeviceDetailNetwork :
            device->category == inventory::DeviceCategory::Display ? FocusControl::DeviceDetailDisplay :
            device->category == inventory::DeviceCategory::Storage ? FocusControl::DeviceDetailStorage : FocusControl::None;
        if (link != FocusControl::None) {
            const char* label = link == FocusControl::DeviceDetailNetwork ? "Open Network & Internet" :
                link == FocusControl::DeviceDetailDisplay ? "Open Display settings" : "Open Storage";
            const FocusItem item{ FocusItem::Kind::Control, 0, link };
            drawButton(x + 8, inventoryActionY(), std::min(390, width - 16), smallSettingsLayout() ? 34 : 42,
                label, false, sameFocus(m_hoverItem, item), sameFocus(m_focusedItem, item), true);
        }
    }

    std::vector<std::pair<std::string, std::string>> diskDetailRows(const inventory::DiskInfo& disk) const
    {
        std::vector<std::pair<std::string, std::string>> rows;
        rows.push_back({ "Capacity", (disk.flags & inventory::kDiskFlagCapacityAvailable)
            ? formatByteSize(disk.capacityBytes) : "Unavailable" });
        rows.push_back({ "Logical sector size", (disk.flags & inventory::kDiskFlagSectorSizeAvailable)
            ? std::to_string(disk.sectorSize) + " bytes" : "Unavailable" });
        rows.push_back({ "Transport", diskTransportName(disk.transport) });
        rows.push_back({ "Block writes", (disk.flags & inventory::kDiskFlagWritable)
            ? "Available at the block layer" : "Read-only at the block layer" });
        rows.push_back({ "Partition table", partitionTableName(disk.partitionTable) });
        rows.push_back({ "Partitions", std::to_string(disk.totalPartitionCount) });
        for (uint8_t i = 0; i < disk.partitionCount; ++i) {
            const inventory::PartitionInfo& part = disk.partitions[i];
            const uint64_t bytes = part.sectorCount <= UINT64_MAX / std::max<uint32_t>(1, disk.sectorSize)
                ? part.sectorCount * disk.sectorSize : 0;
            std::ostringstream label;
            label << "Partition " << static_cast<unsigned>(part.number) << " · MBR type 0x"
                  << std::hex << std::uppercase << static_cast<unsigned>(part.typeCode);
            std::ostringstream value;
            value << "LBA " << std::dec << part.startSector << " · "
                  << (bytes ? formatByteSize(bytes) : std::string("Size unavailable"))
                  << " · filesystem not probed · mount state unavailable";
            rows.push_back({ label.str(), value.str() });
        }
        if (disk.totalPartitionCount > disk.partitionCount) {
            rows.push_back({ "Additional partitions",
                std::to_string(disk.totalPartitionCount - disk.partitionCount) + " not shown in the bounded inventory" });
        }
        for (uint16_t i = 0; i < m_storageSnapshot.volumeCount && i < inventory::kMaxVolumes; ++i) {
            const inventory::VolumeInfo& volume = m_storageSnapshot.volumes[i];
            if (volume.diskStableId[0] && std::string(volume.diskStableId) == disk.stableId) {
                rows.push_back({ "Mounted volume", std::string(volume.mountPath) + " · " + fileSystemName(volume.fileSystem) });
                rows.push_back({ "Volume free space", "Unavailable from the current filesystem provider" });
            }
        }
        if (disk.partitionTable == inventory::PartitionTableState::ValidMbr && disk.partitionCount == 0)
            rows.push_back({ "Partition details", "No MBR partitions reported" });
        if (disk.partitionTable == inventory::PartitionTableState::GptUnsupported)
            rows.push_back({ "Partition details", "GPT is detected; partition details are not available" });
        return rows;
    }

    void renderStorage()
    {
        const int x = pageX();
        const int width = pageWidth();
        const int top = inventoryListTop();
        const int bottom = inventoryListBottom();
        const std::vector<StorageListEntry> entries = storageDisplayEntries();
        clampInventoryScroll();
        std::string countText = std::to_string(m_storageSnapshot.diskCount) + " disks · " +
            std::to_string(m_storageSnapshot.volumeCount) + " mounted volumes";
        if (m_storageSnapshot.truncated) countText += " · additional items not displayed";
        drawText(x + 8, top - 28, countText, mutedTextColor());
        const char* storageSource = m_storageSnapshot.backend == inventory::Backend::Kernel
            ? "Source: guideXOS kernel inventory · filesystem free space unavailable"
            : m_storageSnapshot.backend == inventory::Backend::HostedTest
                ? "Source: deterministic test inventory, not kernel disks · free space unavailable"
                : "Source: guideXOS storage provider unavailable";
        drawText(x + 8, top - 12, storageSource, mutedTextColor());
        drawRect(x + 4, top, width - 8, bottom - top, cardColor());
        drawOutline(x + 4, top, width - 8, bottom - top, borderColor());
        if (m_storageReadResult != system_service::ClientResult::Ok ||
            m_storageSnapshot.backend == inventory::Backend::Unavailable ||
            m_storageSnapshot.state == inventory::SnapshotState::Unavailable) {
            drawText(x + 20, top + 24, "guideXOS storage information unavailable", mutedTextColor());
            drawText(x + 20, top + 48, "Hosted Windows disks are not substituted.", mutedTextColor());
        } else if (m_storageSnapshot.state == inventory::SnapshotState::Empty || entries.empty()) {
            const char* message = m_navigation.route().target == TargetId::StorageVolumes
                ? "No mounted volumes available" : "No storage devices detected";
            drawText(x + 20, top + 24, message, mutedTextColor());
        } else {
            const size_t first = static_cast<size_t>(m_storageScroll);
            const size_t end = std::min(entries.size(), first + static_cast<size_t>(inventoryVisibleRows()));
            for (size_t ordinal = first; ordinal < end; ++ordinal) {
                const StorageListEntry& entry = entries[ordinal];
                std::string primary;
                std::string secondary;
                FocusControl control = FocusControl::StorageDiskEntry;
                if (!entry.volume) {
                    const inventory::DiskInfo& disk = m_storageSnapshot.disks[entry.index];
                    primary = std::string("Disk · ") + (disk.name[0] ? disk.name : "Disk name unavailable");
                    secondary = (disk.flags & inventory::kDiskFlagCapacityAvailable)
                        ? formatByteSize(disk.capacityBytes) : "Capacity unavailable";
                    secondary += std::string(" · ") + diskTransportName(disk.transport);
                    secondary += (disk.flags & inventory::kDiskFlagWritable) ? " · write support available" : " · read-only";
                    control = FocusControl::StorageDiskEntry;
                } else {
                    const inventory::VolumeInfo& volume = m_storageSnapshot.volumes[entry.index];
                    primary = std::string("Volume · ") + (volume.mountPath[0] ? volume.mountPath : "Mount path unavailable");
                    secondary = fileSystemName(volume.fileSystem);
                    secondary += " · Mounted · free space unavailable";
                    if (volume.flags & inventory::kVolumeFlagReadOnly) secondary += " · read-only";
                    control = FocusControl::StorageVolumeEntry;
                }
                const int rowY = top + static_cast<int>(ordinal - first) * inventoryRowPitch();
                const FocusItem row{ FocusItem::Kind::Control, entry.index, control };
                const bool focused = sameFocus(row, m_focusedItem);
                const bool hovered = sameFocus(row, m_hoverItem);
                const uint32_t fill = hovered ? blendColor(cardColor(), accentColor(), 8) :
                    focused ? blendColor(cardColor(), accentColor(), 5) : cardColor();
                drawRect(x + 10, rowY + 2, width - 28, inventoryRowPitch() - 6, fill);
                if (focused || hovered) drawOutline(x + 10, rowY + 2, width - 28, inventoryRowPitch() - 6, accentColor());
                const size_t charLimit = static_cast<size_t>(std::max(12, (width - 72) / 8));
                drawText(x + 20, rowY + 7, fitText(primary, charLimit), textColor());
                drawText(x + 20, rowY + (smallSettingsLayout() ? 27 : 30), fitText(secondary, charLimit), mutedTextColor());
                drawText(x + width - 34, rowY + 16, ">", accentColor());
            }
            if (entries.size() > static_cast<size_t>(inventoryVisibleRows()))
                drawText(x + 10, bottom - 17, "Use the mouse wheel or arrow keys to browse.", mutedTextColor());
        }

        const FocusItem manager{ FocusItem::Kind::Control, 0, FocusControl::StorageDiskManager };
        drawButton(x + 8, inventoryActionY(), std::min(390, width - 16), smallSettingsLayout() ? 34 : 42,
            "Open Disk Manager", false, sameFocus(m_hoverItem, manager), sameFocus(m_focusedItem, manager), true);
        if (!smallSettingsLayout())
            drawText(x + 410, inventoryActionY() + 12, "Advanced disk operations remain in Disk Manager.", mutedTextColor());
    }

    void renderStorageDiskDetail()
    {
        const int x = pageX();
        const int width = pageWidth();
        const int top = inventoryListTop();
        const FocusItem back{ FocusItem::Kind::Control, 0, FocusControl::StorageDetailBack };
        drawButton(x + 8, top, std::min(300, width - 16), smallSettingsLayout() ? 34 : 42,
            "Back to Storage", false, sameFocus(m_hoverItem, back), sameFocus(m_focusedItem, back), true);
        const int cardY = top + (smallSettingsLayout() ? 40 : 48);
        const int cardBottom = inventoryListBottom();
        drawRect(x + 4, cardY, width - 8, cardBottom - cardY, cardColor());
        drawOutline(x + 4, cardY, width - 8, cardBottom - cardY, borderColor());
        const inventory::DiskInfo* disk = selectedDisk();
        if (m_storageReadResult != system_service::ClientResult::Ok || !disk) {
            const char* message = m_storageReadResult != system_service::ClientResult::Ok
                ? "Disk details unavailable."
                : selectionGenerationMatches(m_selectedDiskGeneration, m_storageSnapshot.generation)
                    ? "This disk is no longer available."
                    : "The storage inventory changed. Return and select the disk again.";
            drawText(x + 20, cardY + 24, message, mutedTextColor());
        } else {
            drawText(x + 18, cardY + 12,
                fitText(disk->name[0] ? disk->name : "Disk name unavailable",
                    static_cast<size_t>(std::max(12, (width - 40) / 8))), textColor());
            const std::vector<std::pair<std::string, std::string>> rows = diskDetailRows(*disk);
            const int areaTop = cardY + (smallSettingsLayout() ? 38 : 44);
            const int areaBottom = cardBottom - 4;
            const int rowPitch = inventoryRowPitch();
            const int visible = std::max(1, (areaBottom - areaTop) / rowPitch);
            m_storageScroll = std::max(0, std::min(m_storageScroll,
                std::max(0, static_cast<int>(rows.size()) - visible)));
            const size_t first = static_cast<size_t>(m_storageScroll);
            const size_t end = std::min(rows.size(), first + static_cast<size_t>(visible));
            for (size_t i = first; i < end; ++i) {
                const int rowY = areaTop + static_cast<int>(i - first) * rowPitch;
                drawText(x + 18, rowY + 4, fitText(rows[i].first,
                    static_cast<size_t>(std::max(12, width / 18))), mutedTextColor());
                drawText(x + width / 2, rowY + 4, fitText(rows[i].second,
                    static_cast<size_t>(std::max(12, width / 16))), textColor());
            }
            if (rows.size() > static_cast<size_t>(visible))
                drawText(x + 18, cardBottom - 17, "Use the mouse wheel to view more disk and volume details.", mutedTextColor());
        }
        const FocusItem manager{ FocusItem::Kind::Control, 0, FocusControl::StorageDiskManager };
        drawButton(x + 8, inventoryActionY(), std::min(390, width - 16), smallSettingsLayout() ? 34 : 42,
            "Open Disk Manager", false, sameFocus(m_hoverItem, manager), sameFocus(m_focusedItem, manager), true);
    }

    void renderApps()
    {
        const int x = pageX();
        const int width = pageWidth();
        const int top = inventoryListTop();
        const int bottom = inventoryListBottom();
        std::string count = std::to_string(m_appInventory.count);
        if (m_appInventory.truncated) count += " of " + std::to_string(m_appInventory.totalCount);
        count += m_appInventory.count == 1 ? " registered app" : " registered apps";
        if (m_appInventory.truncated) count += " · inventory truncated";
        drawText(x + 8, top - 28, count, mutedTextColor());
        drawText(x + 8, top - 12, "Source: guideXOS App Model registry", mutedTextColor());

        drawRect(x + 4, top, width - 8, bottom - top, cardColor());
        drawOutline(x + 4, top, width - 8, bottom - top, borderColor());
        if (m_appInventory.count == 0) {
            drawText(x + 20, top + 24, "No applications are currently registered with the App Model.", mutedTextColor());
            return;
        }

        const size_t first = static_cast<size_t>(m_appScroll);
        const size_t end = std::min(m_appInventory.count, first + static_cast<size_t>(inventoryVisibleRows()));
        for (size_t index = first; index < end; ++index) {
            const AppInventoryEntry& app = m_appInventory.entries[index];
            const int rowY = top + static_cast<int>(index - first) * inventoryRowPitch();
            const FocusItem row{ FocusItem::Kind::Control, static_cast<int>(index), FocusControl::AppEntry };
            const bool focused = sameFocus(row, m_focusedItem);
            const bool hovered = sameFocus(row, m_hoverItem);
            const uint32_t fill = hovered ? blendColor(cardColor(), accentColor(), 8) :
                focused ? blendColor(cardColor(), accentColor(), 5) : cardColor();
            drawRect(x + 10, rowY + 2, width - 28, inventoryRowPitch() - 6, fill);
            if (focused || hovered) drawOutline(x + 10, rowY + 2, width - 28, inventoryRowPitch() - 6, accentColor());
            const size_t charLimit = static_cast<size_t>(std::max(12, (width - 72) / 8));
            drawText(x + 20, rowY + 7, fitText(app.displayName, charLimit), textColor());
            const std::string secondary = std::string(appSourceLabel(app.source)) + " · " + appKindLabel(app.kind);
            drawText(x + 20, rowY + (smallSettingsLayout() ? 27 : 30), fitText(secondary, charLimit), mutedTextColor());
            drawText(x + width - 34, rowY + 16, ">", accentColor());
        }
        if (m_appInventory.count > static_cast<size_t>(inventoryVisibleRows()))
            drawText(x + 10, bottom - 17, "Use the mouse wheel or arrow keys to browse.", mutedTextColor());
    }

    void renderAppDetails()
    {
        const int x = pageX();
        const int width = pageWidth();
        const int top = inventoryListTop();
        const int bottom = inventoryListBottom();
        const FocusItem back{ FocusItem::Kind::Control, 0, FocusControl::AppsDetailBack };
        drawButton(x + 8, top, std::min(300, width - 16), smallSettingsLayout() ? 34 : 42,
            "Back to Apps", false, sameFocus(m_hoverItem, back), sameFocus(m_focusedItem, back), true);
        const int cardY = top + (smallSettingsLayout() ? 40 : 48);
        drawRect(x + 4, cardY, width - 8, bottom - cardY, cardColor());
        drawOutline(x + 4, cardY, width - 8, bottom - cardY, borderColor());

        const AppInventoryEntry* app = findAppInventoryEntry(
            m_appInventory, m_selectedAppId, m_selectedAppGeneration);
        if (!app || m_selectedAppMissing) {
            drawText(x + 20, cardY + 24, "This app registration is no longer available.", mutedTextColor());
            drawText(x + 20, cardY + 48, "Return to Apps and select a current registration.", mutedTextColor());
            return;
        }

        drawText(x + 18, cardY + 12,
            fitText(app->displayName, static_cast<size_t>(std::max(12, (width - 40) / 8))), textColor());
        std::vector<std::pair<std::string, std::string>> rows;
        rows.push_back({ "Application ID", app->appId });
        rows.push_back({ "Type", appKindLabel(app->kind) });
        rows.push_back({ "Registration", appSourceLabel(app->source) });
        if (!app->version.empty()) rows.push_back({ "Version", app->version });
        if (!app->launchName.empty()) rows.push_back({ "Launch identity", app->launchName });

        const size_t fullWidthLimit = static_cast<size_t>(std::max(12, (width - 40) / 8));
        const size_t valueLimit = static_cast<size_t>(std::max(8, std::min(30, (width / 2 - 24) / 8)));
        int rowY = cardY + (smallSettingsLayout() ? 38 : 44);
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i == 0) {
                drawText(x + 18, rowY, rows[i].first, mutedTextColor());
                drawText(x + 18, rowY + 16, fitText(rows[i].second, fullWidthLimit), textColor());
                rowY += smallSettingsLayout() ? 38 : 42;
            } else {
                drawText(x + 18, rowY, rows[i].first, mutedTextColor());
                drawText(x + width / 2, rowY, fitText(rows[i].second, valueLimit), textColor());
                rowY += smallSettingsLayout() ? 26 : 32;
            }
        }
        if (!app->openSupported) {
            drawText(x + 18, std::min(bottom - 18, rowY + 2), "Open is unavailable in this runtime.", mutedTextColor());
        }
        if (!m_appStatus.empty())
            drawText(x + 18, bottom - 18, fitText(m_appStatus, fullWidthLimit), mutedTextColor());

        if (app->openSupported) {
            const FocusItem open{ FocusItem::Kind::Control, 0, FocusControl::AppsOpen };
            drawButton(x + 8, inventoryActionY(), std::min(390, width - 16), smallSettingsLayout() ? 34 : 42,
                "Open", false, sameFocus(m_hoverItem, open), sameFocus(m_focusedItem, open), true);
        }
    }

    void renderDateTimeClockText()
    {
        if (m_windowId == 0 || m_search.size() != 0 ||
            m_navigation.selectedCategory() != CategoryId::DateTime) return;
        const int x = pageX();
        const int width = pageWidth();
        const int top = dateTimeTop();
        drawRect(x + 12, top + 38, width - 24, 58, cardColor());
        const size_t textLimit = static_cast<size_t>(std::max(12, (width - 40) / 8));
        drawText(x + 18, top + 42, fitText(formatDateTimeDate(m_dateTimeSnapshot), textLimit), textColor());
        drawText(x + 18, top + 68, formatDateTimeClock(m_dateTimeSnapshot), accentColor());
    }

    void renderDateTimePage()
    {
        const int x = pageX();
        const int width = pageWidth();
        const int top = dateTimeTop();
        const int clockHeight = dateTimeClockCardHeight();
        const bool clockHighlighted = m_navigation.route().target == TargetId::DateTimeTime;
        drawCard(x + 4, top, width - 8, clockHeight, "Current date and time");
        if (clockHighlighted) drawOutline(x + 4, top, width - 8, clockHeight, accentColor());
        renderDateTimeClockText();
        drawText(x + 18, top + (smallSettingsLayout() ? 96 : 112),
            "Source: hosted runtime wall clock", mutedTextColor());

        const int zoneY = dateTimeZoneCardY();
        const int zoneHeight = dateTimeZoneCardHeight();
        const bool zoneHighlighted = m_navigation.route().target == TargetId::DateTimeTimeZone;
        drawCard(x + 4, zoneY, width - 8, zoneHeight, "Time zone");
        if (zoneHighlighted) drawOutline(x + 4, zoneY, width - 8, zoneHeight, accentColor());
        const size_t textLimit = static_cast<size_t>(std::max(12, (width - 40) / 8));
        drawText(x + 18, zoneY + 40,
            fitText(formatDateTimeTimeZoneName(m_dateTimeSnapshot), textLimit), textColor());
        drawText(x + 18, zoneY + 58,
            fitText(formatDateTimeTimeZoneOffset(m_dateTimeSnapshot), textLimit), textColor());
        drawText(x + 18, zoneY + 82, "U.S. DST rules; no IANA database.", mutedTextColor());
        drawText(x + 18, zoneY + 100, "Automatic time is unavailable.", mutedTextColor());
        drawText(x + 18, zoneY + 118, "Manual time/RTC writes unavailable.", mutedTextColor());
        const FocusItem zoneAction{ FocusItem::Kind::Control, 0, FocusControl::DateTimeAdvanced };
        drawButton(x + 8, dateTimeActionY(), std::min(390, width - 16), dateTimeActionHeight(),
            "Open time-zone settings", false, sameFocus(m_hoverItem, zoneAction),
            sameFocus(m_focusedItem, zoneAction), true);
    }

    void drawS7Row(int row, const std::string& label, const std::string& value,
                   FocusControl control = FocusControl::None, bool enabled = false)
    {
        if (!s7RowFullyVisible(row)) return;
        const int x = pageX() + 8;
        const int width = pageWidth() - 16;
        const int y = s7RowY(row);
        const int height = s7RowPitch() - 3;
        const FocusItem item{ FocusItem::Kind::Control, 0, control };
        const bool interactive = control != FocusControl::None;
        const bool hovered = interactive && sameFocus(m_hoverItem, item);
        const bool focused = interactive && sameFocus(m_focusedItem, item);
        const uint32_t fill = hovered && enabled
            ? blendColor(cardColor(), accentColor(), 9) : cardColor();
        drawRect(x, y, width, height, fill);
        if (focused) drawFocusOutline(x, y, width, height);
        else if (hovered && enabled) drawOutline(x, y, width, height, accentColor());

        const int valueX = x + width / 2;
        const int arrowRoom = interactive && enabled ? 26 : 8;
        const size_t labelLimit = static_cast<size_t>(std::max(8, (width / 2 - 18) / 8));
        const size_t valueLimit = static_cast<size_t>(std::max(8, (width / 2 - arrowRoom) / 8));
        drawText(x + 10, y + 4, fitText(label, labelLimit), enabled || !interactive ? textColor() : mutedTextColor());
        drawText(valueX, y + 4, fitText(value, valueLimit), interactive && !enabled ? mutedTextColor() : textColor());
        if (interactive && enabled) drawText(x + width - 18, y + 4, ">", accentColor());
    }

    void renderAccessibilityPage()
    {
        const int x = pageX();
        const int rowCount = 8;
        drawCard(x, s7ContentTop() - 5, pageWidth(), s7CardHeight(rowCount), "");
        drawS7Row(0, "Enhanced focus indicator",
            m_accessibilityPreferenceAvailable
                ? (m_accessibilityPreferences.enhancedFocusIndicator ? "On" : "Off")
                : "Unavailable",
            FocusControl::AccessibilityEnhancedFocus, m_accessibilityPreferenceAvailable);
        drawS7Row(1, "On-screen keyboard",
            m_accessibilityKeyboardAvailable ? "Available" : "Not registered",
            FocusControl::AccessibilityKeyboard, m_accessibilityKeyboardAvailable);
        drawS7Row(2, "System-wide text and UI size", "Unavailable");
        drawS7Row(3, "High contrast palette", "Unavailable");
        drawS7Row(4, "Pointer size and visibility", "Unavailable");
        drawS7Row(5, "Reduced motion", "Unavailable");
        drawS7Row(6, "Screen reader and speech", "Unavailable");
        drawS7Row(7, "Magnifier, color filters, input timing", "Unavailable");
    }

    void renderDeveloperPage()
    {
        const int x = pageX();
        const TargetId target = m_navigation.route().target;
        if (target == TargetId::Page) {
            drawCard(x, s7ContentTop() - 5, pageWidth(), s7CardHeight(9), "");
            drawS7Row(0, "Developer Studio", registeredToolText(m_developerAppModel.developerStudio),
                FocusControl::DeveloperStudio, developerToolActionEnabled(m_developerAppModel.developerStudio));
            drawS7Row(1, "Console", registeredToolText(m_developerAppModel.console),
                FocusControl::DeveloperConsole, developerToolActionEnabled(m_developerAppModel.console));
            drawS7Row(2, "App Model", std::to_string(m_developerAppModel.registryTotalCount) + " registered",
                FocusControl::DeveloperApps, true);
            const int availableServices = static_cast<int>(m_developerServiceNetworkAvailable) +
                static_cast<int>(m_developerServiceDevicesAvailable) +
                static_cast<int>(m_developerServiceStorageAvailable);
            drawS7Row(3, "System services", m_developerServicesChecked
                ? std::to_string(availableServices) + " of 3 available" : "Open to query",
                FocusControl::DeveloperServices, true);
            drawS7Row(4, "App Model and launch diagnostics", "Open details",
                FocusControl::DeveloperDiagnostics, true);
            drawS7Row(5, "Build", gxos::identity::kGuideXosServerVersion);
            drawS7Row(6, "Architecture", formatArchitecture(m_systemInformation.architecture));
            drawS7Row(7, "Runtime", boundedSettingValue(m_systemInformation.runtime, 44, "Unavailable"));
            drawS7Row(8, "Shared focus renderer",
                FocusIndicator::EnhancedFocusEnabled() ? "Enhanced" : "Default");
            return;
        }

        if (target == TargetId::DeveloperApps) {
            drawCard(x, s7ContentTop() - 5, pageWidth(), s7CardHeight(9), "");
            drawS7Row(0, "Registered applications", std::to_string(m_developerAppModel.registryTotalCount));
            drawS7Row(1, "Settings snapshot", std::to_string(m_developerAppModel.registeredCount) +
                (m_developerAppModel.truncated ? "; truncated" : "; complete"));
            drawS7Row(2, "Snapshot capacity", std::to_string(kMaxSettingsApps));
            drawS7Row(3, "Native ELF registrations", std::to_string(m_developerAppModel.nativeElfCount));
            drawS7Row(4, "Developer Studio", registeredToolText(m_developerAppModel.developerStudio));
            drawS7Row(5, "Console", registeredToolText(m_developerAppModel.console));
            drawS7Row(6, "Launch dispatch", "Normal App Model route");
            drawS7Row(7, "Launch readiness", "Not inferred from registration");
            drawS7Row(8, "Back to Developer", "Return", FocusControl::DeveloperBack, true);
            return;
        }

        if (target == TargetId::DeveloperServices) {
            drawCard(x, s7ContentTop() - 5, pageWidth(), s7CardHeight(8), "");
            const int availableServices = static_cast<int>(m_developerServiceNetworkAvailable) +
                static_cast<int>(m_developerServiceDevicesAvailable) +
                static_cast<int>(m_developerServiceStorageAvailable);
            drawS7Row(0, "Available snapshots", std::to_string(availableServices) + " of 3");
            drawS7Row(1, "Network service", m_developerServiceNetworkAvailable ? "Available" : "Unavailable");
            drawS7Row(2, "Device service", m_developerServiceDevicesAvailable ? "Available" : "Unavailable");
            drawS7Row(3, "Storage service", m_developerServiceStorageAvailable ? "Available" : "Unavailable");
            drawS7Row(4, "COM2 peer authentication", "Unavailable; peer is unauthenticated");
            drawS7Row(5, "Network configuration mutation", "Rejected by service bridge");
            drawS7Row(6, "Refresh service status", "Refresh", FocusControl::DeveloperRefresh, true);
            drawS7Row(7, "Back to Developer", "Return", FocusControl::DeveloperBack, true);
            return;
        }

        if (target == TargetId::DeveloperDiagnostics) {
            drawCard(x, s7ContentTop() - 5, pageWidth(), s7CardHeight(11), "");
            drawS7Row(0, "App Model V1 status", m_developerAppModelStatus);
            drawS7Row(1, "Status source", m_developerAppModelStatusSource);
            drawS7Row(2, "Phase 5B storage preview", m_developerPreviewAvailable ? "Nonfatal; read only" : "Unavailable");
            drawS7Row(3, "Preview records", m_developerPreviewAvailable ? std::to_string(m_developerPreviewTotal) : "Unavailable");
            drawS7Row(4, "Phase 5B unresolved", m_developerPreviewAvailable ? std::to_string(m_developerPreviewUnresolved) : "Unavailable");
            drawS7Row(5, "Phase 5B high-risk", m_developerPreviewAvailable ? std::to_string(m_developerPreviewHighRisk) : "Unavailable");
            drawS7Row(6, "Preview storage writes", "None; diagnostic only");
            drawS7Row(7, "Shared focus renderer",
                FocusIndicator::EnhancedFocusEnabled() ? "Enhanced" : "Default");
            drawS7Row(8, "Registry source", "Live AppRegistry snapshot");
            drawS7Row(9, "Refresh diagnostics", "Refresh", FocusControl::DeveloperRefresh, true);
            drawS7Row(10, "Back to Developer", "Return", FocusControl::DeveloperBack, true);
        }
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
            if (application.refreshInventoryIfDue()) application.render();
            if (application.refreshAppsIfDue()) application.render();
            application.refreshDateTimeClockIfDue();
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
