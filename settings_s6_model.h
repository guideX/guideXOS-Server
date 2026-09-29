#pragma once

#include "clock_time_settings.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace gxos {
namespace apps {
namespace settings {

constexpr size_t kMaxSettingsApps = 64;

struct AppInventoryEntry {
    std::string appId;
    std::string displayName;
    std::string version;
    std::string iconKey;
    std::string launchName;
    std::string kind;
    std::string source;
    uint64_t registrationGeneration{0};
    bool openSupported{false};
};

struct AppInventory {
    std::array<AppInventoryEntry, kMaxSettingsApps> entries{};
    size_t count{0};
    size_t totalCount{0};
    bool truncated{false};
};

inline bool appEntryLess(const AppInventoryEntry& left, const AppInventoryEntry& right)
{
    const auto lowercase = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    };
    const std::string leftName = lowercase(left.displayName);
    const std::string rightName = lowercase(right.displayName);
    if (leftName != rightName) return leftName < rightName;
    if (left.appId != right.appId) return left.appId < right.appId;
    return left.registrationGeneration < right.registrationGeneration;
}

inline AppInventory buildAppInventory(std::vector<AppInventoryEntry> entries)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const AppInventoryEntry& entry) {
        return entry.appId.empty() || entry.displayName.empty();
    }), entries.end());
    std::stable_sort(entries.begin(), entries.end(), appEntryLess);

    AppInventory inventory;
    inventory.totalCount = entries.size();
    inventory.count = std::min(entries.size(), kMaxSettingsApps);
    inventory.truncated = entries.size() > kMaxSettingsApps;
    for (size_t i = 0; i < inventory.count; ++i) inventory.entries[i] = std::move(entries[i]);
    return inventory;
}

inline const AppInventoryEntry* findAppInventoryEntry(const AppInventory& inventory,
                                                       const std::string& appId,
                                                       uint64_t registrationGeneration)
{
    for (size_t i = 0; i < inventory.count; ++i) {
        const AppInventoryEntry& entry = inventory.entries[i];
        if (entry.appId == appId && entry.registrationGeneration == registrationGeneration) return &entry;
    }
    return nullptr;
}

inline bool sameAppInventory(const AppInventory& left, const AppInventory& right)
{
    if (left.count != right.count || left.totalCount != right.totalCount || left.truncated != right.truncated)
        return false;
    for (size_t i = 0; i < left.count; ++i) {
        const AppInventoryEntry& a = left.entries[i];
        const AppInventoryEntry& b = right.entries[i];
        if (a.appId != b.appId || a.displayName != b.displayName || a.version != b.version ||
            a.iconKey != b.iconKey || a.launchName != b.launchName || a.kind != b.kind ||
            a.source != b.source || a.registrationGeneration != b.registrationGeneration ||
            a.openSupported != b.openSupported) return false;
    }
    return true;
}

struct DateTimeSnapshot {
    bool available{false};
    bool timeZoneAvailable{false};
    std::time_t utcEpochSeconds{0};
    clocktime::ClockDisplaySettings displaySettings{};
};

inline DateTimeSnapshot makeDateTimeSnapshot(bool available, std::time_t utcEpochSeconds,
                                              clocktime::ClockDisplaySettings settings,
                                              bool timeZoneAvailable = true)
{
    DateTimeSnapshot snapshot;
    snapshot.available = available;
    snapshot.timeZoneAvailable = timeZoneAvailable;
    snapshot.utcEpochSeconds = utcEpochSeconds;
    snapshot.displaySettings = clocktime::NormalizeClockDisplaySettings(std::move(settings));
    return snapshot;
}

inline DateTimeSnapshot readHostedDateTime(clocktime::ClockDisplaySettings settings)
{
    try {
        const auto now = std::chrono::system_clock::now();
        const std::time_t utc = std::chrono::system_clock::to_time_t(now);
        return makeDateTimeSnapshot(true, utc, std::move(settings));
    } catch (...) {
        return makeDateTimeSnapshot(false, 0, std::move(settings), false);
    }
}

inline std::string formatDateTimeDate(const DateTimeSnapshot& snapshot)
{
    return snapshot.available
        ? clocktime::formatLongDate(snapshot.utcEpochSeconds, snapshot.displaySettings)
        : std::string("Unavailable");
}

inline std::string formatDateTimeClock(const DateTimeSnapshot& snapshot)
{
    return snapshot.available
        ? clocktime::formatTimeOfDay(snapshot.utcEpochSeconds, snapshot.displaySettings, true)
        : std::string("Unavailable");
}

inline std::string formatUtcOffset(int offsetMinutes)
{
    const char sign = offsetMinutes < 0 ? '-' : '+';
    const unsigned absoluteMinutes = static_cast<unsigned>(offsetMinutes < 0 ? -offsetMinutes : offsetMinutes);
    const unsigned hours = absoluteMinutes / 60u;
    const unsigned minutes = absoluteMinutes % 60u;
    std::string result = "UTC";
    result.push_back(sign);
    if (hours < 10) result.push_back('0');
    result += std::to_string(hours);
    result.push_back(':');
    if (minutes < 10) result.push_back('0');
    result += std::to_string(minutes);
    return result;
}

inline std::string formatDateTimeTimeZoneName(const DateTimeSnapshot& snapshot)
{
    if (!snapshot.timeZoneAvailable) return "Unavailable";
    return clocktime::TimeZoneDisplayName(snapshot.displaySettings.timeZoneId);
}

inline std::string formatDateTimeTimeZoneOffset(const DateTimeSnapshot& snapshot)
{
    if (!snapshot.timeZoneAvailable || !snapshot.available) return "UTC offset unavailable";

    const clocktime::TimeZoneOption& zone =
        clocktime::TimeZoneOptionForId(snapshot.displaySettings.timeZoneId);
    const int offset = clocktime::utcOffsetMinutesForTimeZone(zone, snapshot.utcEpochSeconds);
    std::string result = formatUtcOffset(offset);
    if (zone.observesDst) {
        result += clocktime::observesUsDst(zone, snapshot.utcEpochSeconds)
            ? " (daylight time)" : " (standard time)";
    }
    return result;
}

inline std::string formatDateTimeTimeZone(const DateTimeSnapshot& snapshot)
{
    if (!snapshot.timeZoneAvailable) return "Unavailable";
    const std::string name = formatDateTimeTimeZoneName(snapshot);
    if (!snapshot.available) return name;
    return name + " · " + formatDateTimeTimeZoneOffset(snapshot);
}

inline bool dateTimeRefreshDue(bool pageVisible, bool windowFocused, bool searchVisible,
                               uint64_t nowMilliseconds, uint64_t nextRefreshMilliseconds)
{
    return pageVisible && windowFocused && !searchVisible && nowMilliseconds >= nextRefreshMilliseconds;
}

} // namespace settings
} // namespace apps
} // namespace gxos
