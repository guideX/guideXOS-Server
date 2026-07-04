#pragma once

#include <array>
#include <cstddef>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace gxos {
namespace clocktime {

struct ClockDisplaySettings {
    std::string timeZoneId{"pacific"};
    bool use24HourTime{false};
};

struct TimeZoneOption {
    const char* id;
    const char* displayName;
    int standardOffsetMinutes;
    bool observesDst;
};

inline constexpr TimeZoneOption kTimeZoneOptions[] = {
    {"pacific", "Pacific Time (US & Canada)", -480, true},
    {"mountain", "Mountain Time (US & Canada)", -420, true},
    {"central", "Central Time (US & Canada)", -360, true},
    {"eastern", "Eastern Time (US & Canada)", -300, true},
    {"utc", "UTC", 0, false},
};

inline constexpr size_t kTimeZoneOptionCount = sizeof(kTimeZoneOptions) / sizeof(kTimeZoneOptions[0]);
inline constexpr const char* kDefaultTimeZoneId = "pacific";

inline std::string normalizedToken(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

inline std::string NormalizeTimeZoneId(const std::string& value)
{
    const std::string token = normalizedToken(value);
    if (token.empty()) {
        return kDefaultTimeZoneId;
    }

    for (const auto& option : kTimeZoneOptions) {
        if (token == normalizedToken(option.id) || token == normalizedToken(option.displayName)) {
            return option.id;
        }
    }

    if (token == "pacifictime" || token == "pacifictimeuscanada") return "pacific";
    if (token == "mountaintime" || token == "mountaintimeuscanada") return "mountain";
    if (token == "centraltime" || token == "centraltimeuscanada") return "central";
    if (token == "easterntime" || token == "easterntimeuscanada") return "eastern";
    if (token == "utc" || token == "gmt") return "utc";

    return kDefaultTimeZoneId;
}

inline size_t TimeZoneIndexFromId(const std::string& value)
{
    const std::string normalized = NormalizeTimeZoneId(value);
    for (size_t i = 0; i < kTimeZoneOptionCount; ++i) {
        if (normalized == kTimeZoneOptions[i].id) {
            return i;
        }
    }
    return 0;
}

inline const TimeZoneOption& TimeZoneOptionAt(size_t index)
{
    if (index >= kTimeZoneOptionCount) {
        return kTimeZoneOptions[0];
    }
    return kTimeZoneOptions[index];
}

inline const TimeZoneOption& TimeZoneOptionForId(const std::string& value)
{
    return TimeZoneOptionAt(TimeZoneIndexFromId(value));
}

inline std::string TimeZoneDisplayName(const std::string& value)
{
    return TimeZoneOptionForId(value).displayName;
}

inline std::string TimeZoneIdAt(size_t index)
{
    return TimeZoneOptionAt(index).id;
}

inline int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153u * static_cast<unsigned>(month > 2 ? month - 3 : month + 9) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline int weekdayFromCivil(int year, unsigned month, unsigned day)
{
    int64_t days = daysFromCivil(year, month, day);
    int weekday = static_cast<int>((days + 4) % 7);
    if (weekday < 0) {
        weekday += 7;
    }
    return weekday;
}

inline int nthWeekdayOfMonth(int year, unsigned month, int weekday, int nth)
{
    const int firstWeekday = weekdayFromCivil(year, month, 1);
    int delta = weekday - firstWeekday;
    if (delta < 0) {
        delta += 7;
    }
    return 1 + delta + (nth - 1) * 7;
}

inline int64_t civilToUnixSeconds(int year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second)
{
    return daysFromCivil(year, month, day) * 86400LL +
        static_cast<int64_t>(hour) * 3600LL +
        static_cast<int64_t>(minute) * 60LL +
        static_cast<int64_t>(second);
}

inline std::tm gmtimePortable(std::time_t value)
{
    std::tm out{};
#if defined(_WIN32)
    gmtime_s(&out, &value);
#else
    gmtime_r(&value, &out);
#endif
    return out;
}

inline bool observesUsDst(const TimeZoneOption& zone, std::time_t utcTime)
{
    if (!zone.observesDst) {
        return false;
    }

    const int64_t standardOffsetSeconds = static_cast<int64_t>(zone.standardOffsetMinutes) * 60LL;
    const std::time_t localStandardProbe = static_cast<std::time_t>(static_cast<int64_t>(utcTime) + standardOffsetSeconds);
    const std::tm probeTm = gmtimePortable(localStandardProbe);
    const int year = 1900 + probeTm.tm_year;
    const int marchSunday = nthWeekdayOfMonth(year, 3u, 0, 2);
    const int novemberSunday = nthWeekdayOfMonth(year, 11u, 0, 1);
    const int64_t dstStartUtc = civilToUnixSeconds(year, 3u, static_cast<unsigned>(marchSunday), 2u, 0u, 0u) - standardOffsetSeconds;
    const int64_t dstEndUtc = civilToUnixSeconds(year, 11u, static_cast<unsigned>(novemberSunday), 2u, 0u, 0u) -
        (standardOffsetSeconds + 3600LL);
    const int64_t utc = static_cast<int64_t>(utcTime);
    return utc >= dstStartUtc && utc < dstEndUtc;
}

inline int utcOffsetMinutesForTimeZone(const TimeZoneOption& zone, std::time_t utcTime)
{
    int offset = zone.standardOffsetMinutes;
    if (observesUsDst(zone, utcTime)) {
        offset += 60;
    }
    return offset;
}

inline std::tm toLocalTm(std::time_t utcTime, const ClockDisplaySettings& settings)
{
    const TimeZoneOption& zone = TimeZoneOptionForId(settings.timeZoneId);
    const int offsetMinutes = utcOffsetMinutesForTimeZone(zone, utcTime);
    const std::time_t localTime = static_cast<std::time_t>(static_cast<int64_t>(utcTime) + static_cast<int64_t>(offsetMinutes) * 60LL);
    return gmtimePortable(localTime);
}

inline std::string formatTimeOfDay(std::time_t utcTime, const ClockDisplaySettings& settings, bool includeSeconds)
{
    const std::tm local = toLocalTm(utcTime, settings);
    std::ostringstream out;
    if (settings.use24HourTime) {
        out << std::setfill('0') << std::setw(2) << local.tm_hour << ":"
            << std::setfill('0') << std::setw(2) << local.tm_min;
        if (includeSeconds) {
            out << ":" << std::setfill('0') << std::setw(2) << local.tm_sec;
        }
        return out.str();
    }

    int hour = local.tm_hour % 12;
    if (hour == 0) {
        hour = 12;
    }
    out << hour << ":" << std::setfill('0') << std::setw(2) << local.tm_min;
    if (includeSeconds) {
        out << ":" << std::setfill('0') << std::setw(2) << local.tm_sec;
    }
    out << (local.tm_hour < 12 ? " AM" : " PM");
    return out.str();
}

inline std::string formatShortDate(std::time_t utcTime, const ClockDisplaySettings& settings)
{
    const std::tm local = toLocalTm(utcTime, settings);
    std::ostringstream out;
    out << (local.tm_mon + 1) << "/" << local.tm_mday << "/" << (1900 + local.tm_year);
    return out.str();
}

inline std::string formatLongDate(std::time_t utcTime, const ClockDisplaySettings& settings)
{
    static const char* weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    static const char* months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    const std::tm local = toLocalTm(utcTime, settings);
    std::ostringstream out;
    out << weekdays[local.tm_wday] << ", "
        << months[local.tm_mon] << " "
        << local.tm_mday << ", "
        << (1900 + local.tm_year);
    return out.str();
}

inline ClockDisplaySettings NormalizeClockDisplaySettings(ClockDisplaySettings settings)
{
    settings.timeZoneId = NormalizeTimeZoneId(settings.timeZoneId);
    settings.use24HourTime = settings.use24HourTime ? true : false;
    return settings;
}

} // namespace clocktime
} // namespace gxos
