#include "settings_center_model.h"
#include "settings_s6_model.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
int s_passed = 0;
int s_failed = 0;

void expect(bool condition, const char* name)
{
    if (condition) {
        ++s_passed;
        std::cout << "PASS " << name << "\n";
    } else {
        ++s_failed;
        std::cout << "FAIL " << name << "\n";
    }
}

gxos::apps::settings::AppInventoryEntry app(const std::string& id,
                                             const std::string& name,
                                             const std::string& source = "BuiltIn",
                                             uint64_t generation = 0,
                                             bool open = false)
{
    gxos::apps::settings::AppInventoryEntry item;
    item.appId = id;
    item.displayName = name;
    item.kind = source == "BuiltIn" ? "BuiltIn" : "NativeElf";
    item.source = source;
    item.registrationGeneration = generation;
    item.openSupported = open;
    return item;
}
}

int main()
{
    using namespace gxos::apps::settings;
    using namespace gxos;

    expect(buildAppInventory({}).count == 0, "empty app registry");

    AppInventory one = buildAppInventory({ app("gxos.builtin.notepad", "Notepad", "BuiltIn", 0, true) });
    expect(one.count == 1 && one.totalCount == 1 && !one.truncated, "single registered app");
    expect(one.entries[0].source == "BuiltIn", "built-in source preserved");
    expect(one.entries[0].openSupported, "normal Open capability preserved");

    AppInventory multiple = buildAppInventory({
        app("com.guidexos.zeta", "zeta", "UserApps"),
        app("gxos.builtin.calculator", "Calculator", "BuiltIn", 0, true),
        app("com.guidexos.alpha", "Alpha", "SystemApps")
    });
    expect(multiple.count == 3 && multiple.totalCount == 3, "multiple registered apps");
    expect(multiple.entries[0].displayName == "Alpha" && multiple.entries[1].displayName == "Calculator" &&
        multiple.entries[2].displayName == "zeta", "stable display-name ordering");
    expect(multiple.entries[0].source == "SystemApps", "external registration source preserved");

    AppInventoryEntry missingVersion = app("com.guidexos.versionless", "Versionless", "UserApps");
    expect(missingVersion.version.empty(), "missing version remains unavailable");
    AppInventoryEntry longValues = app(std::string(180, 'x'), std::string(220, 'N'), "UserApps");
    AppInventory longInventory = buildAppInventory({ longValues });
    expect(longInventory.entries[0].appId.size() == 180 && longInventory.entries[0].displayName.size() == 220,
        "long app identity remains intact in the model");

    expect(app("id", "launchable", "BuiltIn", 0, true).openSupported, "launchable app action");
    expect(!app("id", "non-launchable", "UserApps", 0, false).openSupported, "non-launchable app has no Open action");

    AppInventoryEntry upperName = app("id.z", "same", "UserApps");
    AppInventoryEntry lowerName = app("id.a", "Same", "UserApps");
    AppInventory tied = buildAppInventory({ upperName, lowerName });
    expect(tied.entries[0].appId == "id.a" && tied.entries[1].appId == "id.z",
        "app ID breaks display-name ties deterministically");

    std::vector<AppInventoryEntry> many;
    for (size_t i = 0; i < kMaxSettingsApps + 7; ++i) {
        many.push_back(app("com.guidexos.app." + std::to_string(i), "App " + std::to_string(i), "UserApps"));
    }
    AppInventory full = buildAppInventory(many);
    expect(full.count == kMaxSettingsApps, "app inventory capacity");
    expect(full.totalCount == kMaxSettingsApps + 7 && full.truncated, "app inventory truncation reported");
    bool boundedOrderStable = full.entries[0].displayName == "App 0" &&
        full.entries[1].displayName == "App 1" && full.entries[2].displayName == "App 10";
    for (size_t i = 1; i < full.count; ++i)
        boundedOrderStable = boundedOrderStable && !appEntryLess(full.entries[i], full.entries[i - 1]);
    expect(boundedOrderStable, "bounded app inventory keeps deterministic first entries");

    AppInventoryEntry temporary = app("com.guidexos.temp", "Temporary", "DevelopmentTemporary", 17);
    AppInventory present = buildAppInventory({ temporary });
    AppInventory removed = buildAppInventory({});
    AppInventory reused = buildAppInventory({ app("com.guidexos.temp", "Replacement", "DevelopmentTemporary", 18) });
    expect(findAppInventoryEntry(present, temporary.appId, 17) != nullptr, "selected app identity resolves while registered");
    expect(findAppInventoryEntry(removed, temporary.appId, 17) == nullptr, "selection clears when app disappears");
    expect(findAppInventoryEntry(reused, temporary.appId, 17) == nullptr, "registration generation prevents stale selection reuse");

    clocktime::ClockDisplaySettings pacific;
    pacific.timeZoneId = "pacific";
    DateTimeSnapshot winter = makeDateTimeSnapshot(true, static_cast<std::time_t>(1704069000), pacific);
    expect(formatDateTimeDate(winter) == "Sunday, December 31, 2023", "date formatting crosses year and month boundary");
    expect(formatDateTimeClock(winter) == "4:30:00 PM", "12-hour wall-clock formatting");
    expect(formatDateTimeTimeZone(winter).find("UTC-08:00 (standard time)") != std::string::npos,
        "Pacific standard UTC offset");
    DateTimeSnapshot midnight = makeDateTimeSnapshot(true, static_cast<std::time_t>(1704096000), pacific);
    expect(formatDateTimeDate(midnight) == "Monday, January 1, 2024" &&
        formatDateTimeClock(midnight) == "12:00:00 AM", "local midnight boundary formatting");

    clocktime::ClockDisplaySettings utc;
    utc.timeZoneId = "utc";
    DateTimeSnapshot leap = makeDateTimeSnapshot(true, static_cast<std::time_t>(1709208000), utc);
    expect(formatDateTimeDate(leap) == "Thursday, February 29, 2024", "leap-day date formatting");
    expect(formatDateTimeClock(leap) == "12:00:00 PM", "leap-day noon formatting");
    expect(formatDateTimeTimeZone(leap) == "UTC · UTC+00:00", "UTC time-zone display");

    DateTimeSnapshot summer = makeDateTimeSnapshot(true, static_cast<std::time_t>(1719835200), pacific);
    expect(formatDateTimeTimeZone(summer).find("UTC-07:00 (daylight time)") != std::string::npos,
        "Pacific daylight-saving offset");
    pacific.use24HourTime = true;
    DateTimeSnapshot twentyFourHour = makeDateTimeSnapshot(true, static_cast<std::time_t>(1704069000), pacific);
    expect(formatDateTimeClock(twentyFourHour) == "16:30:00", "24-hour formatting preference");

    DateTimeSnapshot unavailable = makeDateTimeSnapshot(false, 0, utc, false);
    expect(formatDateTimeDate(unavailable) == "Unavailable" && formatDateTimeClock(unavailable) == "Unavailable",
        "unavailable wall clock state");
    expect(formatDateTimeTimeZone(unavailable) == "Unavailable", "unknown time-zone state");
    clocktime::ClockDisplaySettings unknown;
    unknown.timeZoneId = "not-a-zone";
    DateTimeSnapshot normalized = makeDateTimeSnapshot(true, static_cast<std::time_t>(1704069000), unknown);
    expect(normalized.displaySettings.timeZoneId == clocktime::kDefaultTimeZoneId,
        "unknown saved zone follows the shared clock fallback");
    expect(formatUtcOffset(330) == "UTC+05:30" && formatUtcOffset(-480) == "UTC-08:00",
        "UTC offset format includes minutes and sign");
    expect(dateTimeRefreshDue(true, true, false, 1000, 1000), "visible date page refresh becomes due");
    expect(!dateTimeRefreshDue(false, true, false, 1000, 1000) &&
        !dateTimeRefreshDue(true, false, false, 1000, 1000) &&
        !dateTimeRefreshDue(true, true, true, 1000, 1000), "hidden, unfocused, and searched pages do not refresh");

    const std::time_t ownerBefore = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const DateTimeSnapshot hosted = readHostedDateTime(utc);
    const std::time_t ownerAfter = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    expect(hosted.available && hosted.utcEpochSeconds >= ownerBefore - 1 &&
        hosted.utcEpochSeconds <= ownerAfter + 1, "hosted provider reads the runtime wall clock");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const DateTimeSnapshot hostedLater = readHostedDateTime(utc);
    expect(hostedLater.available && hostedLater.utcEpochSeconds > hosted.utcEpochSeconds,
        "repeated hosted clock reads advance without provider regression");

    SettingsRoute route{};
    expect(parseSettingsRoute("settings://apps", route) && route.category == CategoryId::Apps,
        "apps page deep link");
    expect(parseSettingsRoute("settings://apps/list", route) && route.target == TargetId::AppsList,
        "apps list deep link");
    expect(parseSettingsRoute("settings://date-time/time", route) && route.target == TargetId::DateTimeTime,
        "current time deep link");
    expect(parseSettingsRoute("settings://date-time/timezone", route) && route.target == TargetId::DateTimeTimeZone,
        "time-zone deep link");
    expect(!parseSettingsRoute("settings://apps/app/unsafe-id", route), "arbitrary app URI is rejected");
    const SearchResultSet appSearch = searchSettings("registered apps");
    expect(appSearch.count > 0 && appSearch.values[0].route.category == CategoryId::Apps,
        "global Settings search reaches registered apps");
    const SearchResultSet timeSearch = searchSettings("automatic time");
    expect(timeSearch.count > 0 && timeSearch.values[0].route.category == CategoryId::DateTime,
        "global Settings search reaches Date and Time");

    std::cout << "Settings S6 model tests: " << s_passed << "/" << (s_passed + s_failed) << " passed\n";
    return s_failed == 0 ? 0 : 1;
}
