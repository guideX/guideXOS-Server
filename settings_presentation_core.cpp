#include "settings_presentation_core.h"

namespace gxos {
namespace apps {
namespace settings {

const FixedTable<CategoryInfo, kCategoryCount> kCategories = {{
    { CategoryId::System, "system", "System", "Your computer and everyday system links." },
    { CategoryId::Display, "display", "Display", "Display state and layout." },
    { CategoryId::Network, "network", "Network & Internet", "Connection information and network services." },
    { CategoryId::Personalization, "personalization", "Personalization", "Desktop background and appearance." },
    { CategoryId::Devices, "devices", "Devices", "Connected devices and input." },
    { CategoryId::Storage, "storage", "Storage", "Disks and storage tools." },
    { CategoryId::Apps, "apps", "Apps", "Applications registered with the guideXOS App Model." },
    { CategoryId::Users, "users", "Users", "Identity and session capabilities." },
    { CategoryId::DateTime, "date-time", "Date & Time", "Clock and time-zone settings." },
    { CategoryId::Accessibility, "accessibility", "Accessibility", "Accessibility tools." },
    { CategoryId::Developer, "developer", "Developer", "Development and diagnostic tools." },
    { CategoryId::About, "about", "About", "guideXOS Server information." }
}};

const FixedTable<PageElementDescriptor, kPageElementCount> kPageElements = {{
    { "System overview", "system computer hardware device hostname processor cpu memory ram architecture", { CategoryId::System, TargetId::SystemDevice }, PageElementKind::Page },
    { "Resolution", "resolution screen monitor display size", { CategoryId::Display, TargetId::Resolution }, PageElementKind::Setting },
    { "Display mode", "display mirror extend monitor layout", { CategoryId::Display, TargetId::DisplayMode }, PageElementKind::Setting },
    { "Network connection", "network internet ethernet adapter connection", { CategoryId::Network, TargetId::Page }, PageElementKind::Page },
    { "IP assignment", "ip ipv4 address dhcp static assignment subnet mask prefix", { CategoryId::Network, TargetId::IPv4 }, PageElementKind::Setting },
    { "IPv4 address", "ip address ipv4 adapter", { CategoryId::Network, TargetId::IPv4 }, PageElementKind::Setting },
    { "DNS", "dns nameserver name server network", { CategoryId::Network, TargetId::DNS }, PageElementKind::Setting },
    { "Gateway", "gateway router network ipv4", { CategoryId::Network, TargetId::Gateway }, PageElementKind::Setting },
    { "Desktop background", "wallpaper background image personalize", { CategoryId::Personalization, TargetId::PersonalizationBackground }, PageElementKind::Action },
    { "Theme and appearance", "theme color appearance personalization desktop", { CategoryId::Personalization, TargetId::Page }, PageElementKind::Page },
    { "Devices", "device devices hardware connected", { CategoryId::Devices, TargetId::Page }, PageElementKind::Page },
    { "Input devices", "input mouse keyboard hid", { CategoryId::Devices, TargetId::DevicesInput }, PageElementKind::Setting },
    { "Network adapters", "network adapter ethernet nic", { CategoryId::Devices, TargetId::DevicesNetwork }, PageElementKind::Setting },
    { "Display adapters", "display adapter graphics gpu monitor", { CategoryId::Devices, TargetId::DevicesDisplay }, PageElementKind::Setting },
    { "Audio devices", "audio sound speaker microphone", { CategoryId::Devices, TargetId::DevicesAudio }, PageElementKind::Setting },
    { "USB devices", "usb peripheral", { CategoryId::Devices, TargetId::DevicesUsb }, PageElementKind::Setting },
    { "Disks and partitions", "storage disk disks partition partitions drive filesystem fat32 removable", { CategoryId::Storage, TargetId::StorageDisks }, PageElementKind::Setting },
    { "Volumes", "volume mount mounted filesystem fat32", { CategoryId::Storage, TargetId::StorageVolumes }, PageElementKind::Setting },
    { "Registered apps", "apps app installed application applications programs registered open app", { CategoryId::Apps, TargetId::AppsList }, PageElementKind::Setting },
    { "Default apps", "default defaults default apps default applications file types file associations open with .txt .log .ini .cfg", { CategoryId::Apps, TargetId::AppsDefault }, PageElementKind::Action },
    { "Users and identity", "users user account accounts identity principal", { CategoryId::Users, TargetId::Page }, PageElementKind::Page },
    { "Current session", "session desktop shell login logon sign in signin sign-out logout", { CategoryId::Users, TargetId::UsersSession }, PageElementKind::Setting },
    { "Authentication and passwords", "authentication authenticate password passwords credentials credential login", { CategoryId::Users, TargetId::UsersSecurity }, PageElementKind::Setting },
    { "User profiles", "profile profiles home directory per-user user configuration", { CategoryId::Users, TargetId::Page }, PageElementKind::Page },
    { "User permissions", "permissions permission owner ownership uid gid acl access control", { CategoryId::Users, TargetId::UsersSecurity }, PageElementKind::Setting },
    { "Clock and time zone", "date time clock timezone time zone", { CategoryId::DateTime, TargetId::Page }, PageElementKind::Page },
    { "Current time", "current time date clock calendar rtc", { CategoryId::DateTime, TargetId::DateTimeTime }, PageElementKind::Setting },
    { "Time zone", "timezone time zone utc daylight saving automatic time manual time", { CategoryId::DateTime, TargetId::DateTimeTimeZone }, PageElementKind::Setting },
    { "Accessibility tools", "accessibility keyboard assistive focus contrast", { CategoryId::Accessibility, TargetId::Page }, PageElementKind::Page },
    { "Enhanced focus indicator", "focus indicator focus ring keyboard accessibility outline", { CategoryId::Accessibility, TargetId::AccessibilityFocus }, PageElementKind::Setting },
    { "On-screen keyboard", "on-screen keyboard accessibility assistive input", { CategoryId::Accessibility, TargetId::AccessibilityKeyboard }, PageElementKind::Action },
    { "Developer tools", "developer diagnostics debug console studio", { CategoryId::Developer, TargetId::Page }, PageElementKind::Page },
    { "Developer Studio", "developer studio development editor native elf", { CategoryId::Developer, TargetId::Page }, PageElementKind::Action },
    { "Console", "console terminal command line developer", { CategoryId::Developer, TargetId::Page }, PageElementKind::Action },
    { "App Model diagnostics", "app model registered apps registry developer diagnostic", { CategoryId::Developer, TargetId::DeveloperApps }, PageElementKind::Setting },
    { "System service diagnostics", "system service bridge network device storage developer", { CategoryId::Developer, TargetId::DeveloperServices }, PageElementKind::Setting },
    { "Developer diagnostics", "diagnostics debug phase 5b launch storage developer", { CategoryId::Developer, TargetId::DeveloperDiagnostics }, PageElementKind::Setting },
    { "Version and platform information", "about version build guidexos server firmware uefi platform", { CategoryId::About, TargetId::AboutVersion }, PageElementKind::Page },
    { "Control Panel", "advanced administrative control panel", { CategoryId::System, TargetId::Page }, PageElementKind::Action },
    { "Display Options", "advanced display settings wallpaper", { CategoryId::Display, TargetId::Page }, PageElementKind::Action },
    { "Disk Manager", "manage disks partitions storage", { CategoryId::Storage, TargetId::StorageDisks }, PageElementKind::Action }
}};

namespace {

bool asciiWhitespace(char value)
{
    return value == ' ' || (value >= '\t' && value <= '\r');
}

char asciiLower(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

size_t boundedLength(const char* value, size_t maximum)
{
    if (!value) return maximum + 1;
    size_t length = 0;
    while (length <= maximum && value[length] != '\0') ++length;
    return length;
}

bool spanEquals(const char* value, size_t start, size_t length, const char* literal)
{
    size_t literalLength = 0;
    while (literal[literalLength] != '\0') ++literalLength;
    if (literalLength != length) return false;
    for (size_t i = 0; i < length; ++i) {
        if (asciiLower(value[start + i]) != asciiLower(literal[i])) return false;
    }
    return true;
}

bool containsCaseInsensitive(const char* text, size_t textLength,
                             const char* query, size_t queryLength)
{
    if (queryLength == 0) return true;
    if (textLength < queryLength) return false;
    for (size_t start = 0; start <= textLength - queryLength; ++start) {
        size_t i = 0;
        while (i < queryLength && asciiLower(text[start + i]) == asciiLower(query[i])) ++i;
        if (i == queryLength) return true;
    }
    return false;
}

bool validCapability(CapabilityId id)
{
    return static_cast<size_t>(id) < kCapabilityCount;
}

bool validAvailability(CapabilityAvailability availability)
{
    return availability == CapabilityAvailability::Writable ||
        availability == CapabilityAvailability::ReadOnly ||
        availability == CapabilityAvailability::Unsupported ||
        availability == CapabilityAvailability::Unavailable;
}

bool targetMatchesCategory(TargetId target, CategoryId category)
{
    switch (target) {
    case TargetId::Page: return true;
    case TargetId::SystemDevice: return category == CategoryId::System;
    case TargetId::Resolution:
    case TargetId::DisplayMode: return category == CategoryId::Display;
    case TargetId::IPv4:
    case TargetId::DNS:
    case TargetId::Gateway: return category == CategoryId::Network;
    case TargetId::PersonalizationBackground: return category == CategoryId::Personalization;
    case TargetId::DevicesInput:
    case TargetId::DevicesNetwork:
    case TargetId::DevicesDisplay:
    case TargetId::DevicesStorage:
    case TargetId::DevicesAudio:
    case TargetId::DevicesUsb:
    case TargetId::DevicesOther:
    case TargetId::DeviceDetail: return category == CategoryId::Devices;
    case TargetId::StorageDisks:
    case TargetId::StorageVolumes:
    case TargetId::StorageDiskDetail: return category == CategoryId::Storage;
    case TargetId::AppsList:
    case TargetId::AppsDefault:
    case TargetId::AppDetail: return category == CategoryId::Apps;
    case TargetId::DateTimeTime:
    case TargetId::DateTimeTimeZone: return category == CategoryId::DateTime;
    case TargetId::AccessibilityFocus:
    case TargetId::AccessibilityKeyboard: return category == CategoryId::Accessibility;
    case TargetId::DeveloperApps:
    case TargetId::DeveloperServices:
    case TargetId::DeveloperDiagnostics: return category == CategoryId::Developer;
    case TargetId::UsersSession:
    case TargetId::UsersSecurity: return category == CategoryId::Users;
    case TargetId::AboutVersion: return category == CategoryId::About;
    default: return false;
    }
    return false;
}

TargetId parseTarget(const char* text, size_t start, size_t length)
{
    if (length == 0) return TargetId::Page;
    if (spanEquals(text, start, length, "list")) return TargetId::AppsList;
    if (spanEquals(text, start, length, "default") || spanEquals(text, start, length, "defaults")) return TargetId::AppsDefault;
    if (spanEquals(text, start, length, "device") || spanEquals(text, start, length, "computer")) return TargetId::SystemDevice;
    if (spanEquals(text, start, length, "background") || spanEquals(text, start, length, "wallpaper")) return TargetId::PersonalizationBackground;
    if (spanEquals(text, start, length, "resolution")) return TargetId::Resolution;
    if (spanEquals(text, start, length, "display-mode")) return TargetId::DisplayMode;
    if (spanEquals(text, start, length, "ipv4")) return TargetId::IPv4;
    if (spanEquals(text, start, length, "dns")) return TargetId::DNS;
    if (spanEquals(text, start, length, "gateway")) return TargetId::Gateway;
    if (spanEquals(text, start, length, "disks") || spanEquals(text, start, length, "partitions")) return TargetId::StorageDisks;
    if (spanEquals(text, start, length, "volumes")) return TargetId::StorageVolumes;
    if (spanEquals(text, start, length, "input")) return TargetId::DevicesInput;
    if (spanEquals(text, start, length, "network")) return TargetId::DevicesNetwork;
    if (spanEquals(text, start, length, "display")) return TargetId::DevicesDisplay;
    if (spanEquals(text, start, length, "storage")) return TargetId::DevicesStorage;
    if (spanEquals(text, start, length, "audio") || spanEquals(text, start, length, "sound")) return TargetId::DevicesAudio;
    if (spanEquals(text, start, length, "usb")) return TargetId::DevicesUsb;
    if (spanEquals(text, start, length, "other")) return TargetId::DevicesOther;
    if (spanEquals(text, start, length, "version")) return TargetId::AboutVersion;
    if (spanEquals(text, start, length, "time")) return TargetId::DateTimeTime;
    if (spanEquals(text, start, length, "timezone")) return TargetId::DateTimeTimeZone;
    if (spanEquals(text, start, length, "focus")) return TargetId::AccessibilityFocus;
    if (spanEquals(text, start, length, "keyboard")) return TargetId::AccessibilityKeyboard;
    if (spanEquals(text, start, length, "apps")) return TargetId::DeveloperApps;
    if (spanEquals(text, start, length, "services")) return TargetId::DeveloperServices;
    if (spanEquals(text, start, length, "diagnostics")) return TargetId::DeveloperDiagnostics;
    if (spanEquals(text, start, length, "session")) return TargetId::UsersSession;
    if (spanEquals(text, start, length, "security")) return TargetId::UsersSecurity;
    return static_cast<TargetId>(0xFFu);
}

} // namespace

bool operator==(const SettingsRoute& left, const SettingsRoute& right)
{
    return left.category == right.category && left.target == right.target;
}

bool validCategory(CategoryId id)
{
    return static_cast<size_t>(id) < kCategoryCount;
}

const CategoryInfo* categoryInfo(CategoryId id)
{
    return validCategory(id) ? &kCategories[static_cast<size_t>(id)] : nullptr;
}

bool validRoute(const SettingsRoute& route)
{
    return validCategory(route.category) && targetMatchesCategory(route.target, route.category);
}

bool parseSettingsRoute(const char* uri, SettingsRoute& route)
{
    const size_t length = boundedLength(uri, kSettingsRouteMaxLength);
    if (length == 0 || length > kSettingsRouteMaxLength) return false;

    size_t first = 0;
    size_t last = length;
    while (first < last && asciiWhitespace(uri[first])) ++first;
    while (last > first && asciiWhitespace(uri[last - 1])) --last;
    if (last - first < 11 || !spanEquals(uri, first, 11, "settings://")) return false;

    const size_t pathStart = first + 11;
    size_t slash = pathStart;
    while (slash < last && uri[slash] != '/') ++slash;
    const size_t slugLength = slash - pathStart;

    CategoryId category = CategoryId::System;
    if (slugLength != 0) {
        bool found = false;
        for (const CategoryInfo& info : kCategories) {
            if (spanEquals(uri, pathStart, slugLength, info.slug)) {
                category = info.id;
                found = true;
                break;
            }
        }
        if (!found && spanEquals(uri, pathStart, slugLength, "network-internet")) {
            category = CategoryId::Network;
            found = true;
        }
        if (!found && spanEquals(uri, pathStart, slugLength, "dateandtime")) {
            category = CategoryId::DateTime;
            found = true;
        }
        if (!found) return false;
    }

    const size_t targetStart = slash < last ? slash + 1 : last;
    const size_t targetLength = last - targetStart;
    const TargetId target = parseTarget(uri, targetStart, targetLength);
    const SettingsRoute parsed{ category, target };
    if (!validRoute(parsed)) return false;
    route = parsed;
    return true;
}

SearchResultSet searchSettings(const char* query)
{
    SearchResultSet results;
    results.count = 0;
    results.accepted = false;
    for (size_t i = 0; i < SearchResultSet::kCapacity; ++i) {
        results.values[i].label = nullptr;
        results.values[i].route = SettingsRoute{ CategoryId::System, TargetId::Page };
    }

    const size_t length = boundedLength(query, kSearchQueryMaxLength);
    if (length > kSearchQueryMaxLength) return results;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char character = static_cast<unsigned char>(query[i]);
        if ((character < 0x20u && !asciiWhitespace(query[i])) || character > 0x7Eu) return results;
    }
    results.accepted = true;

    size_t first = 0;
    size_t last = length;
    while (first < last && asciiWhitespace(query[first])) ++first;
    while (last > first && asciiWhitespace(query[last - 1])) --last;
    const size_t normalizedLength = last - first;
    if (normalizedLength == 0) return results;

    for (const PageElementDescriptor& element : kPageElements) {
        size_t labelLength = 0;
        while (element.label[labelLength] != '\0') ++labelLength;
        size_t keywordLength = 0;
        while (element.keywords[keywordLength] != '\0') ++keywordLength;
        if (!containsCaseInsensitive(element.label, labelLength, query + first, normalizedLength) &&
            !containsCaseInsensitive(element.keywords, keywordLength, query + first, normalizedLength)) continue;
        if (results.count == SearchResultSet::kCapacity) break;
        results.values[results.count].label = element.label;
        results.values[results.count].route = element.route;
        ++results.count;
    }
    return results;
}

CategoryId focusControlCategory(FocusControl control)
{
    switch (control) {
    case FocusControl::SystemDisplay:
    case FocusControl::SystemNetwork:
    case FocusControl::SystemStorage:
    case FocusControl::SystemAbout:
    case FocusControl::SystemDevices:
    case FocusControl::SystemControlPanel: return CategoryId::System;
    case FocusControl::NetworkAdapter:
    case FocusControl::NetworkAdvanced: return CategoryId::Network;
    case FocusControl::DisplayResolution:
    case FocusControl::DisplayMode:
    case FocusControl::DisplayApply:
    case FocusControl::DisplayCancel:
    case FocusControl::DisplayAdvanced: return CategoryId::Display;
    case FocusControl::PersonalizationAdvanced:
    case FocusControl::PersonalizationChooseBackground:
    case FocusControl::PersonalizationApply:
    case FocusControl::PersonalizationCancel: return CategoryId::Personalization;
    case FocusControl::DateTimeAdvanced: return CategoryId::DateTime;
    case FocusControl::StorageDiskManager:
    case FocusControl::StorageDiskEntry:
    case FocusControl::StorageVolumeEntry:
    case FocusControl::StorageDetailBack: return CategoryId::Storage;
    case FocusControl::DeviceEntry:
    case FocusControl::DeviceDetailBack:
    case FocusControl::DeviceDetailNetwork:
    case FocusControl::DeviceDetailDisplay:
    case FocusControl::DeviceDetailStorage: return CategoryId::Devices;
    case FocusControl::DeveloperConsole:
    case FocusControl::DeveloperStudio:
    case FocusControl::DeveloperApps:
    case FocusControl::DeveloperServices:
    case FocusControl::DeveloperDiagnostics:
    case FocusControl::DeveloperRefresh:
    case FocusControl::DeveloperBack: return CategoryId::Developer;
    case FocusControl::AccessibilityKeyboard:
    case FocusControl::AccessibilityEnhancedFocus: return CategoryId::Accessibility;
    case FocusControl::AppEntry:
    case FocusControl::AppsInstalledTab:
    case FocusControl::AppsDefaultTab:
    case FocusControl::DefaultAppEntry:
    case FocusControl::DefaultAppHandler:
    case FocusControl::DefaultAppRestore:
    case FocusControl::DefaultAppCancel:
    case FocusControl::AppsDetailBack:
    case FocusControl::AppsOpen: return CategoryId::Apps;
    case FocusControl::UsersSessionDetails:
    case FocusControl::UsersSecurityDetails:
    case FocusControl::UsersDeveloperServices:
    case FocusControl::UsersBack: return CategoryId::Users;
    case FocusControl::None: return CategoryId::Count;
    default: return CategoryId::Count;
    }
}

SettingsPresentationCore::SettingsPresentationCore()
    : m_nextRequestId(1)
{
    reset();
}

void SettingsPresentationCore::reset()
{
    m_route = SettingsRoute{ CategoryId::System, TargetId::Page };
    m_focusedControl = FocusControl::None;
    m_searchQuery[0] = '\0';
    m_searchQueryLength = 0;
    for (size_t i = 0; i < kCapabilityCount; ++i) {
        m_capabilities[i].availability = CapabilityAvailability::Unknown;
        m_capabilities[i].status = PresentationStatus::None;
        m_capabilities[i].revision = 0;
        m_capabilities[i].activeRequestId = 0;
    }
}

CategoryId SettingsPresentationCore::selectedCategory() const
{
    return m_route.category;
}

SettingsRoute SettingsPresentationCore::route() const
{
    return m_route;
}

bool SettingsPresentationCore::selectCategory(CategoryId category)
{
    return navigate(SettingsRoute{ category, TargetId::Page });
}

bool SettingsPresentationCore::nextCategory()
{
    const size_t index = (static_cast<size_t>(m_route.category) + 1) % kCategoryCount;
    return selectCategory(static_cast<CategoryId>(index));
}

bool SettingsPresentationCore::previousCategory()
{
    const size_t current = static_cast<size_t>(m_route.category);
    const size_t index = current == 0 ? kCategoryCount - 1 : current - 1;
    return selectCategory(static_cast<CategoryId>(index));
}

bool SettingsPresentationCore::navigate(const SettingsRoute& route)
{
    if (!validRoute(route)) return false;
    m_route = route;
    m_focusedControl = FocusControl::None;
    clearSearch();
    return true;
}

bool SettingsPresentationCore::canFocus(FocusControl control) const
{
    return control != FocusControl::None &&
        focusControlCategory(control) == m_route.category;
}

bool SettingsPresentationCore::setFocusedControl(FocusControl control)
{
    if (control == FocusControl::None) {
        m_focusedControl = FocusControl::None;
        return true;
    }
    if (!canFocus(control)) return false;
    m_focusedControl = control;
    return true;
}

FocusControl SettingsPresentationCore::focusedControl() const
{
    return m_focusedControl;
}

bool SettingsPresentationCore::setSearchQuery(const char* query)
{
    const size_t length = boundedLength(query, kSearchQueryMaxLength);
    if (length > kSearchQueryMaxLength) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char character = static_cast<unsigned char>(query[i]);
        if (character < 0x20u || character > 0x7Eu) return false;
    }
    for (size_t i = 0; i < length; ++i) m_searchQuery[i] = query[i];
    m_searchQuery[length] = '\0';
    m_searchQueryLength = length;
    return true;
}

bool SettingsPresentationCore::appendSearchCharacter(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    if (value < 0x20u || value > 0x7Eu || m_searchQueryLength >= kSearchQueryMaxLength) return false;
    m_searchQuery[m_searchQueryLength++] = character;
    m_searchQuery[m_searchQueryLength] = '\0';
    return true;
}

bool SettingsPresentationCore::backspaceSearch()
{
    if (m_searchQueryLength == 0) return false;
    m_searchQuery[--m_searchQueryLength] = '\0';
    return true;
}

void SettingsPresentationCore::clearSearch()
{
    m_searchQuery[0] = '\0';
    m_searchQueryLength = 0;
}

const char* SettingsPresentationCore::searchQuery() const
{
    return m_searchQuery;
}

size_t SettingsPresentationCore::searchQueryLength() const
{
    return m_searchQueryLength;
}

bool SettingsPresentationCore::hasSearchQuery() const
{
    return m_searchQueryLength != 0;
}

SearchResultSet SettingsPresentationCore::searchResults() const
{
    return searchSettings(m_searchQuery);
}

bool SettingsPresentationCore::applyCapabilitySnapshot(const CapabilitySnapshot& snapshot)
{
    if (!validCapability(snapshot.id) || !validAvailability(snapshot.availability)) return false;
    CapabilityPresentation& current = m_capabilities[static_cast<size_t>(snapshot.id)];
    if (snapshot.id == CapabilityId::Display && current.revision != 0 &&
        snapshot.revision != current.revision) {
        // Native Display revisions encode a 32-bit active-configuration
        // generation as generation + 1. Use half-range serial arithmetic so
        // a legitimate source-generation wrap can advance the capability.
        const uint32_t candidateGeneration =
            static_cast<uint32_t>(snapshot.revision - 1u);
        const uint32_t currentGeneration =
            static_cast<uint32_t>(current.revision - 1u);
        const uint32_t difference = candidateGeneration - currentGeneration;
        if (difference == 0u || difference >= 0x80000000u) return false;
    } else if (snapshot.revision < current.revision) {
        return false;
    }
    current.availability = snapshot.availability;
    current.revision = snapshot.revision;
    return true;
}

const CapabilityPresentation* SettingsPresentationCore::capability(CapabilityId id) const
{
    return validCapability(id) ? &m_capabilities[static_cast<size_t>(id)] : nullptr;
}

bool SettingsPresentationCore::beginOperation(CapabilityId id, uint16_t operationCode,
                                              OperationRequest& request,
                                              const uint8_t* payload,
                                              size_t payloadLength)
{
    if (!validCapability(id) || operationCode == 0 ||
        payloadLength > kOperationPayloadCapacity ||
        (payloadLength != 0 && !payload)) return false;
    CapabilityPresentation& state = m_capabilities[static_cast<size_t>(id)];
    if (state.availability != CapabilityAvailability::Writable ||
        state.status == PresentationStatus::Pending) return false;

    uint8_t* requestBytes = reinterpret_cast<uint8_t*>(&request);
    for (size_t i = 0; i < sizeof(request); ++i) requestBytes[i] = 0;
    uint32_t requestId = m_nextRequestId++;
    if (requestId == 0) requestId = m_nextRequestId++;
    if (m_nextRequestId == 0) ++m_nextRequestId;
    state.status = PresentationStatus::Pending;
    state.activeRequestId = requestId;
    request.requestId = requestId;
    request.capability = id;
    request.operationCode = operationCode;
    request.baseRevision = state.revision;
    request.payloadLength = payloadLength;
    for (size_t i = 0; i < payloadLength; ++i) request.payload[i] = payload[i];
    return true;
}

bool SettingsPresentationCore::completeOperation(const OperationResult& result)
{
    if (result.requestId == 0 ||
        (result.completion != OperationCompletion::Success &&
         result.completion != OperationCompletion::Failure)) return false;
    for (size_t i = 0; i < kCapabilityCount; ++i) {
        CapabilityPresentation& state = m_capabilities[i];
        if (state.status != PresentationStatus::Pending ||
            state.activeRequestId != result.requestId) continue;
        state.status = result.completion == OperationCompletion::Success
            ? PresentationStatus::Success : PresentationStatus::Failure;
        state.activeRequestId = 0;
        return true;
    }
    return false;
}

} // namespace settings
} // namespace apps
} // namespace gxos
