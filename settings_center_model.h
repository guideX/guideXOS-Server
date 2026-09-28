#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdint>

namespace gxos {
namespace apps {
namespace settings {

enum class CategoryId : unsigned char {
    System,
    Display,
    Network,
    Personalization,
    Devices,
    Storage,
    Apps,
    Users,
    DateTime,
    Accessibility,
    Developer,
    About,
    Count
};

enum class TargetId : unsigned char {
    Page,
    SystemDevice,
    Resolution,
    DisplayMode,
    IPv4,
    DNS,
    Gateway,
    StorageDisks,
    AboutVersion,
    PersonalizationBackground
};

struct CategoryInfo {
    CategoryId id;
    const char* slug;
    const char* label;
    const char* description;
};

inline constexpr std::array<CategoryInfo, static_cast<size_t>(CategoryId::Count)> kCategories = {{
    { CategoryId::System, "system", "System", "Your computer and everyday system links." },
    { CategoryId::Display, "display", "Display", "Display state and layout." },
    { CategoryId::Network, "network", "Network & Internet", "Connection information and network services." },
    { CategoryId::Personalization, "personalization", "Personalization", "Desktop background and appearance." },
    { CategoryId::Devices, "devices", "Devices", "Connected devices and input." },
    { CategoryId::Storage, "storage", "Storage", "Disks and storage tools." },
    { CategoryId::Apps, "apps", "Apps", "Installed and built-in applications." },
    { CategoryId::Users, "users", "Users", "User accounts and sign-in." },
    { CategoryId::DateTime, "date-time", "Date & Time", "Clock and time-zone settings." },
    { CategoryId::Accessibility, "accessibility", "Accessibility", "Accessibility tools." },
    { CategoryId::Developer, "developer", "Developer", "Development and diagnostic tools." },
    { CategoryId::About, "about", "About", "guideXOS Server information." }
}};

struct SettingsRoute {
    CategoryId category{CategoryId::System};
    TargetId target{TargetId::Page};
};

inline bool operator==(const SettingsRoute& left, const SettingsRoute& right)
{
    return left.category == right.category && left.target == right.target;
}

inline bool validCategory(CategoryId id)
{
    return static_cast<size_t>(id) < kCategories.size();
}

inline const CategoryInfo* categoryInfo(CategoryId id)
{
    return validCategory(id) ? &kCategories[static_cast<size_t>(id)] : nullptr;
}

inline std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline std::string trimAscii(const std::string& value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

inline bool parseSettingsRoute(const std::string& uri, SettingsRoute& route)
{
    const std::string normalized = lowerAscii(trimAscii(uri));
    constexpr const char* prefix = "settings://";
    constexpr size_t prefixLength = 11;
    if (normalized.compare(0, prefixLength, prefix) != 0) return false;
    const std::string path = normalized.substr(prefixLength);
    const size_t slash = path.find('/');
    const std::string slug = path.substr(0, slash);
    const std::string target = slash == std::string::npos ? std::string() : path.substr(slash + 1);

    CategoryId category = CategoryId::System;
    if (!slug.empty()) {
        bool found = false;
        for (const CategoryInfo& info : kCategories) {
            if (slug == info.slug || (slug == "network-internet" && info.id == CategoryId::Network) ||
                (slug == "dateandtime" && info.id == CategoryId::DateTime)) {
                category = info.id;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    TargetId targetId = TargetId::Page;
    if (!target.empty()) {
        if (target == "device" || target == "computer") targetId = TargetId::SystemDevice;
        else if (target == "background" || target == "wallpaper") targetId = TargetId::PersonalizationBackground;
        else if (target == "resolution") targetId = TargetId::Resolution;
        else if (target == "display-mode") targetId = TargetId::DisplayMode;
        else if (target == "ipv4") targetId = TargetId::IPv4;
        else if (target == "dns") targetId = TargetId::DNS;
        else if (target == "gateway") targetId = TargetId::Gateway;
        else if (target == "disks" || target == "partitions") targetId = TargetId::StorageDisks;
        else if (target == "version") targetId = TargetId::AboutVersion;
        else return false;
    }

    const bool validTarget = targetId == TargetId::Page ||
        (targetId == TargetId::SystemDevice && category == CategoryId::System) ||
        (targetId == TargetId::PersonalizationBackground && category == CategoryId::Personalization) ||
        ((targetId == TargetId::Resolution || targetId == TargetId::DisplayMode) && category == CategoryId::Display) ||
        ((targetId == TargetId::IPv4 || targetId == TargetId::DNS || targetId == TargetId::Gateway) && category == CategoryId::Network) ||
        (targetId == TargetId::StorageDisks && category == CategoryId::Storage) ||
        (targetId == TargetId::AboutVersion && category == CategoryId::About);
    if (!validTarget) return false;
    route = SettingsRoute{ category, targetId };
    return true;
}

struct SearchEntry {
    const char* label;
    const char* keywords;
    SettingsRoute route;
};

inline constexpr std::array<SearchEntry, 21> kSearchEntries = {{
    { "System overview", "system computer hardware device hostname processor cpu memory ram architecture", { CategoryId::System, TargetId::SystemDevice } },
    { "Resolution", "resolution screen monitor display size", { CategoryId::Display, TargetId::Resolution } },
    { "Display mode", "display mirror extend monitor layout", { CategoryId::Display, TargetId::DisplayMode } },
    { "Network connection", "network internet ethernet adapter connection", { CategoryId::Network, TargetId::Page } },
    { "IP assignment", "ip ipv4 address dhcp static assignment subnet mask prefix", { CategoryId::Network, TargetId::IPv4 } },
    { "IPv4 address", "ip address ipv4 adapter", { CategoryId::Network, TargetId::IPv4 } },
    { "DNS", "dns nameserver name server network", { CategoryId::Network, TargetId::DNS } },
    { "Gateway", "gateway router network ipv4", { CategoryId::Network, TargetId::Gateway } },
    { "Desktop background", "wallpaper background image personalize", { CategoryId::Personalization, TargetId::PersonalizationBackground } },
    { "Theme and appearance", "theme color appearance personalization desktop", { CategoryId::Personalization, TargetId::Page } },
    { "Input devices", "devices mouse keyboard hardware", { CategoryId::Devices, TargetId::Page } },
    { "Disks and partitions", "storage disk disks partition partitions drive", { CategoryId::Storage, TargetId::StorageDisks } },
    { "Applications", "apps app installed application programs", { CategoryId::Apps, TargetId::Page } },
    { "User accounts", "users user account sign in", { CategoryId::Users, TargetId::Page } },
    { "Clock and time zone", "date time clock timezone time zone", { CategoryId::DateTime, TargetId::Page } },
    { "Accessibility tools", "accessibility keyboard assistive", { CategoryId::Accessibility, TargetId::Page } },
    { "Developer tools", "developer diagnostics debug console", { CategoryId::Developer, TargetId::Page } },
    { "Version and platform information", "about version build guidexos server firmware uefi platform", { CategoryId::About, TargetId::AboutVersion } },
    { "Control Panel", "advanced administrative control panel", { CategoryId::System, TargetId::Page } },
    { "Display Options", "advanced display settings wallpaper", { CategoryId::Display, TargetId::Page } },
    { "Disk Manager", "manage disks partitions storage", { CategoryId::Storage, TargetId::StorageDisks } }
}};

struct SearchResult {
    const char* label{nullptr};
    SettingsRoute route{};
};

struct SearchResultSet {
    static constexpr size_t kCapacity = 6;
    std::array<SearchResult, kCapacity> values{};
    size_t count{0};
};

inline SearchResultSet searchSettings(const std::string& query)
{
    SearchResultSet results;
    const std::string normalized = lowerAscii(trimAscii(query));
    if (normalized.empty()) return results;
    for (const SearchEntry& entry : kSearchEntries) {
        const std::string label = lowerAscii(entry.label);
        const std::string keywords = lowerAscii(entry.keywords);
        if (label.find(normalized) == std::string::npos && keywords.find(normalized) == std::string::npos) continue;
        if (results.count == results.kCapacity) break;
        results.values[results.count++] = SearchResult{ entry.label, entry.route };
    }
    return results;
}

enum class FocusControl : unsigned char {
    None,
    SystemDisplay,
    SystemNetwork,
    SystemStorage,
    SystemAbout,
    NetworkAdapter,
    NetworkAdvanced,
    DisplayResolution,
    DisplayMode,
    DisplayApply,
    DisplayCancel,
    DisplayAdvanced,
    PersonalizationAdvanced,
    PersonalizationChooseBackground,
    DateTimeAdvanced,
    StorageDiskManager,
    SystemControlPanel,
    DeveloperConsole,
    AccessibilityKeyboard
};

inline CategoryId focusControlCategory(FocusControl control)
{
    switch (control) {
    case FocusControl::SystemDisplay:
    case FocusControl::SystemNetwork:
    case FocusControl::SystemStorage:
    case FocusControl::SystemAbout: return CategoryId::System;
    case FocusControl::NetworkAdapter:
    case FocusControl::NetworkAdvanced: return CategoryId::Network;
    case FocusControl::DisplayResolution:
    case FocusControl::DisplayMode:
    case FocusControl::DisplayApply:
    case FocusControl::DisplayCancel:
    case FocusControl::DisplayAdvanced: return CategoryId::Display;
    case FocusControl::PersonalizationAdvanced: return CategoryId::Personalization;
    case FocusControl::PersonalizationChooseBackground: return CategoryId::Personalization;
    case FocusControl::DateTimeAdvanced: return CategoryId::DateTime;
    case FocusControl::StorageDiskManager: return CategoryId::Storage;
    case FocusControl::SystemControlPanel: return CategoryId::System;
    case FocusControl::DeveloperConsole: return CategoryId::Developer;
    case FocusControl::AccessibilityKeyboard: return CategoryId::Accessibility;
    case FocusControl::None: return CategoryId::Count;
    }
    return CategoryId::Count;
}

class NavigationModel {
public:
    CategoryId selectedCategory() const { return m_route.category; }
    SettingsRoute route() const { return m_route; }

    void selectCategory(CategoryId category)
    {
        if (validCategory(category)) m_route = SettingsRoute{ category, TargetId::Page };
    }

    bool navigate(const SettingsRoute& route)
    {
        if (!validCategory(route.category)) return false;
        const bool targetValid = route.target == TargetId::Page ||
            (route.target == TargetId::SystemDevice && route.category == CategoryId::System) ||
            (route.target == TargetId::PersonalizationBackground && route.category == CategoryId::Personalization) ||
            ((route.target == TargetId::Resolution || route.target == TargetId::DisplayMode) && route.category == CategoryId::Display) ||
            ((route.target == TargetId::IPv4 || route.target == TargetId::DNS || route.target == TargetId::Gateway) && route.category == CategoryId::Network) ||
            (route.target == TargetId::StorageDisks && route.category == CategoryId::Storage) ||
            (route.target == TargetId::AboutVersion && route.category == CategoryId::About);
        if (!targetValid) return false;
        m_route = route;
        return true;
    }

    bool canFocus(FocusControl control) const
    {
        return control != FocusControl::None && focusControlCategory(control) == m_route.category;
    }

private:
    SettingsRoute m_route{};
};

inline std::string formatDisplayResolution(int width, int height)
{
    return width > 0 && height > 0
        ? std::to_string(width) + " x " + std::to_string(height)
        : std::string("Resolution unavailable");
}

inline std::string boundedSettingValue(const std::string& value, size_t maxCharacters,
                                       const char* unavailable = "Unavailable")
{
    const std::string trimmed = trimAscii(value);
    if (trimmed.empty()) return unavailable;
    if (trimmed.size() <= maxCharacters) return trimmed;
    if (maxCharacters <= 3) return trimmed.substr(0, maxCharacters);
    return trimmed.substr(0, maxCharacters - 3) + "...";
}

inline std::string formatProcessorName(const std::string& value, size_t maxCharacters = 56)
{
    return boundedSettingValue(value, maxCharacters, "Processor unavailable");
}

inline std::string formatComputerName(const std::string& value, size_t maxCharacters = 48)
{
    return boundedSettingValue(value, maxCharacters, "Unavailable");
}

inline std::string formatArchitecture(const std::string& identifier)
{
    std::string value = lowerAscii(trimAscii(identifier));
    if (value == "amd64" || value == "x86_64") return "AMD64";
    if (value == "x86" || value == "i386" || value == "i686") return "x86";
    if (value == "arm64" || value == "aarch64") return "ARM64";
    if (value == "arm") return "ARM";
    if (value == "ia64" || value == "ia-64") return "IA-64";
    if (value == "loongarch64") return "LoongArch64";
    if (value == "mips64") return "MIPS64";
    if (value == "ppc64" || value == "powerpc64") return "PPC64";
    if (value == "sparc64") return "SPARC64";
    if (value == "sparc") return "SPARC";
    if (value == "riscv64") return "RISC-V 64";
    if (value == "s390x") return "s390x";
    return "Unavailable";
}

inline std::string formatByteSize(uint64_t bytes)
{
    if (bytes == 0) return "Unavailable";
    static constexpr uint64_t kKiB = 1024ull;
    static constexpr uint64_t kMiB = kKiB * 1024ull;
    static constexpr uint64_t kGiB = kMiB * 1024ull;
    static constexpr uint64_t kTiB = kGiB * 1024ull;
    const uint64_t unit = bytes >= kTiB ? kTiB : bytes >= kGiB ? kGiB : bytes >= kMiB ? kMiB : bytes >= kKiB ? kKiB : 1ull;
    const char* suffix = unit == kTiB ? "TiB" : unit == kGiB ? "GiB" : unit == kMiB ? "MiB" : unit == kKiB ? "KiB" : "B";
    if (unit == 1ull) return std::to_string(bytes) + " B";
    const uint64_t whole = bytes / unit;
    const uint64_t remainder = bytes % unit;
    uint64_t tenths = (remainder * 10ull + unit / 2ull) / unit;
    uint64_t roundedWhole = whole;
    if (tenths == 10ull) {
        ++roundedWhole;
        tenths = 0;
    }
    return std::to_string(roundedWhole) + "." + std::to_string(tenths) + " " + suffix;
}

inline std::string formatMemoryBytes(uint64_t bytes)
{
    if (bytes == 0) return "Unavailable";
    return formatByteSize(bytes);
}

inline std::string formatBootEnvironment(const std::string& identifier)
{
    const std::string value = lowerAscii(trimAscii(identifier));
    if (value == "uefi") return "UEFI";
    if (value == "bios" || value == "legacy bios") return "Legacy BIOS";
    if (value == "unknown" || value.empty()) return "Unavailable";
    return boundedSettingValue(identifier, 32);
}

enum class IpAssignment : unsigned char { Unavailable, DHCP, Static };

inline const char* formatIpAssignment(IpAssignment assignment)
{
    switch (assignment) {
    case IpAssignment::DHCP: return "DHCP";
    case IpAssignment::Static: return "Static";
    case IpAssignment::Unavailable: default: return "Unavailable";
    }
}

inline std::string formatNetworkAddress(const std::string& address)
{
    return address.empty() ? std::string("Address unavailable") : address;
}

inline const char* formatNetworkConnection(bool linkKnown, bool linkUp, bool hasAddress)
{
    if (!linkKnown) return "Status unavailable";
    if (!linkUp) return "Disconnected";
    return hasAddress ? "Connected" : "Link up, no lease";
}

inline bool builtInAdvancedTarget(CategoryId category, const char*& launchName)
{
    launchName = nullptr;
    switch (category) {
    case CategoryId::Display: launchName = "DisplayOptions"; return true;
    case CategoryId::Personalization: launchName = "DisplayOptions"; return true;
    case CategoryId::DateTime: launchName = "DisplayOptions"; return true;
    case CategoryId::Storage: launchName = "DiskManager"; return true;
    case CategoryId::System: launchName = "ControlPanel"; return true;
    case CategoryId::Developer: launchName = "Console"; return true;
    case CategoryId::Accessibility: launchName = "OnScreenKeyboard"; return true;
    case CategoryId::Network:
    case CategoryId::Apps:
    case CategoryId::Users:
    case CategoryId::About:
    case CategoryId::Count:
    default: return false;
    }
}

inline bool registryIntegrity()
{
    if (kCategories.size() != static_cast<size_t>(CategoryId::Count)) return false;
    for (size_t i = 0; i < kCategories.size(); ++i) {
        if (static_cast<size_t>(kCategories[i].id) != i || !kCategories[i].slug || !kCategories[i].label) return false;
        for (size_t j = i + 1; j < kCategories.size(); ++j) {
            if (std::string(kCategories[i].slug) == kCategories[j].slug) return false;
        }
    }
    return true;
}

} // namespace settings
} // namespace apps
} // namespace gxos
