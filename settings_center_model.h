#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include "settings_inventory_contract.h"
#include "settings_presentation_core.h"

namespace gxos {
namespace apps {
namespace settings {

using NavigationModel = SettingsPresentationCore;

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

inline int fitSettingsWindowDimension(int desktopSize, int preferredSize,
                                      int margin, int preferredMinimum)
{
    if (desktopSize <= 0) return preferredSize;
    const int minimum = std::min(preferredMinimum, desktopSize);
    const int withMargin = desktopSize > margin ? desktopSize - margin : desktopSize;
    return std::min(preferredSize, std::max(minimum, withMargin));
}

inline bool parseSettingsRoute(const std::string& uri, SettingsRoute& route)
{
    return parseSettingsRoute(uri.c_str(), route);
}

inline SearchResultSet searchSettings(const std::string& query)
{
    return searchSettings(query.c_str());
}
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

inline const char* deviceCategoryName(settings_inventory::DeviceCategory category)
{
    using settings_inventory::DeviceCategory;
    switch (category) {
    case DeviceCategory::Input: return "Input";
    case DeviceCategory::Network: return "Network";
    case DeviceCategory::Display: return "Display";
    case DeviceCategory::Storage: return "Storage";
    case DeviceCategory::Audio: return "Audio";
    case DeviceCategory::Usb: return "USB";
    case DeviceCategory::Other: default: return "Other device";
    }
}

inline const char* deviceStatusName(settings_inventory::DeviceStatus status)
{
    using settings_inventory::DeviceStatus;
    switch (status) {
    case DeviceStatus::DriverLoaded: return "Driver loaded";
    case DeviceStatus::NoDriver: return "No driver";
    case DeviceStatus::Disconnected: return "Disconnected";
    case DeviceStatus::Unavailable: return "Unavailable";
    case DeviceStatus::Unknown: default: return "Status unavailable";
    }
}

inline const char* diskTransportName(settings_inventory::DiskTransport transport)
{
    using settings_inventory::DiskTransport;
    switch (transport) {
    case DiskTransport::AtaPio: return "ATA PIO";
    case DiskTransport::Ahci: return "AHCI";
    case DiskTransport::Nvme: return "NVMe";
    case DiskTransport::UsbMassStorage: return "USB mass storage";
    case DiskTransport::Unknown: default: return "Transport unavailable";
    }
}

inline const char* partitionTableName(settings_inventory::PartitionTableState state)
{
    using settings_inventory::PartitionTableState;
    switch (state) {
    case PartitionTableState::Unreadable: return "Partition table unreadable";
    case PartitionTableState::NoMbr: return "No MBR signature";
    case PartitionTableState::ValidMbr: return "MBR";
    case PartitionTableState::InvalidMbr: return "Invalid MBR";
    case PartitionTableState::GptUnsupported: return "GPT detected; details unavailable";
    case PartitionTableState::Unknown: default: return "Partition table unavailable";
    }
}

inline const char* fileSystemName(settings_inventory::FileSystem fileSystem)
{
    using settings_inventory::FileSystem;
    switch (fileSystem) {
    case FileSystem::Fat32: return "FAT32";
    case FileSystem::ExFat: return "exFAT";
    case FileSystem::Ext2: return "ext2";
    case FileSystem::Ext4: return "ext4";
    case FileSystem::Ufs: return "UFS";
    case FileSystem::Iso9660: return "ISO9660";
    case FileSystem::RamDisk: return "RamDisk";
    case FileSystem::Unknown: default: return "Filesystem unavailable";
    }
}

inline bool deviceMatchesFilter(settings_inventory::DeviceCategory category, TargetId target)
{
    using settings_inventory::DeviceCategory;
    switch (target) {
    case TargetId::DevicesInput: return category == DeviceCategory::Input;
    case TargetId::DevicesNetwork: return category == DeviceCategory::Network;
    case TargetId::DevicesDisplay: return category == DeviceCategory::Display;
    case TargetId::DevicesStorage: return category == DeviceCategory::Storage;
    case TargetId::DevicesAudio: return category == DeviceCategory::Audio;
    case TargetId::DevicesUsb: return category == DeviceCategory::Usb;
    case TargetId::DevicesOther: return category == DeviceCategory::Other;
    default: return true;
    }
}

inline bool selectionGenerationMatches(uint64_t selectedGeneration,
                                       uint64_t currentGeneration)
{
    return selectedGeneration != 0 && selectedGeneration == currentGeneration;
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
