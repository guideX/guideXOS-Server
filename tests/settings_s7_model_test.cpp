#include "settings_s7_model.h"
#include "desktop_config.h"

#include <iostream>
#include <string>
#include <vector>

using namespace gxos::apps::settings;

namespace {
struct Counts { int passed{0}; int failed{0}; };

void expect(Counts& counts, bool condition, const char* description)
{
    if (condition) ++counts.passed;
    else {
        ++counts.failed;
        std::cerr << "FAIL: " << description << "\n";
    }
}

struct MemoryPreferenceStore {
    bool available{true};
    bool value{false};
    bool failWrite{false};
    int failReadNumber{0};
    int reads{0};
    bool overrideAfterWrite{false};
    bool overrideValue{false};

    bool read(bool& output)
    {
        ++reads;
        if (!available || reads == failReadNumber) return false;
        output = value;
        return true;
    }

    bool write(bool requested)
    {
        if (!available || failWrite) return false;
        value = overrideAfterWrite ? overrideValue : requested;
        return true;
    }
};

AppInventory inventoryWith(std::vector<AppInventoryEntry> entries)
{
    return buildAppInventory(std::move(entries));
}
}

int main()
{
    Counts accessibility;
    Counts developer;
    Counts integration;

    const AccessibilityPreferences defaults{};
    expect(accessibility, !defaults.enhancedFocusIndicator, "enhanced focus has a safe default");
    expect(accessibility, parsePersistedBoolean("true"), "true persisted value is accepted");
    expect(accessibility, parsePersistedBoolean(" ON "), "bounded on value is accepted");
    expect(accessibility, !parsePersistedBoolean("false", true), "false persisted value is accepted");
    expect(accessibility, !parsePersistedBoolean("malformed"), "malformed value falls back off");
    expect(accessibility, parsePersistedBoolean("malformed", true), "malformed value honors explicit fallback");
    expect(accessibility, gxos::gui::parseDesktopEnhancedFocusJsonBoolean(" true \n"),
        "desktop configuration accepts a valid persisted JSON true");
    expect(accessibility, !gxos::gui::parseDesktopEnhancedFocusJsonBoolean("false", true),
        "desktop configuration accepts a valid persisted JSON false");
    expect(accessibility, !gxos::gui::parseDesktopEnhancedFocusJsonBoolean("true-junk"),
        "malformed desktop JSON boolean safely falls back off");

    MemoryPreferenceStore store;
    bool authoritative = false;
    bool applied = false;
    auto apply = [&applied](bool value) { applied = value; return true; };
    expect(accessibility,
        updateEnhancedFocusPreference(store, true, apply, authoritative) == AccessibilityMutationResult::Applied &&
        store.value && authoritative && applied,
        "successful update persists, applies, then rereads the authoritative value");

    MemoryPreferenceStore reopened;
    reopened.value = store.value;
    bool reopenedValue = false;
    expect(accessibility, reopened.read(reopenedValue) && reopenedValue,
        "a reopened Settings provider reads the saved focus preference");

    store.reads = 0;
    applied = true;
    expect(accessibility,
        updateEnhancedFocusPreference(store, false, [&applied](bool) { applied = false; return true; }, authoritative) == AccessibilityMutationResult::Applied &&
        !store.value && !authoritative && !applied,
        "successful update turns the shared preference off");

    MemoryPreferenceStore unavailable;
    unavailable.available = false;
    authoritative = true;
    expect(accessibility,
        updateEnhancedFocusPreference(unavailable, true, [](bool) { return true; }, authoritative) == AccessibilityMutationResult::ReadFailed,
        "missing preference owner blocks mutation");

    MemoryPreferenceStore writeFailure;
    writeFailure.failWrite = true;
    int applyCalls = 0;
    authoritative = true;
    expect(accessibility,
        updateEnhancedFocusPreference(writeFailure, true, [&applyCalls](bool) { ++applyCalls; return true; }, authoritative) == AccessibilityMutationResult::PersistFailed &&
        !writeFailure.value && !authoritative && applyCalls == 0,
        "persistence failure leaves the displayed owner value unchanged");

    MemoryPreferenceStore readbackFailure;
    readbackFailure.failReadNumber = 2;
    authoritative = false;
    expect(accessibility,
        updateEnhancedFocusPreference(readbackFailure, true, [&applyCalls](bool) { ++applyCalls; return true; }, authoritative) == AccessibilityMutationResult::ReadBackFailed &&
        !authoritative && applyCalls == 2,
        "readback failure restores the stored value and production application");

    MemoryPreferenceStore mismatch;
    mismatch.overrideAfterWrite = true;
    mismatch.overrideValue = false;
    authoritative = true;
    expect(accessibility,
        updateEnhancedFocusPreference(mismatch, true, [&applyCalls](bool) { ++applyCalls; return true; }, authoritative) == AccessibilityMutationResult::ReadBackMismatch &&
        !authoritative && applyCalls == 4,
        "readback mismatch restores the authoritative value and production application");

    int failedApplies = 0;
    expect(accessibility,
        updateEnhancedFocusPreference(reopened, false, [&failedApplies](bool) { ++failedApplies; return false; }, reopenedValue) == AccessibilityMutationResult::ApplyFailed &&
        reopenedValue && failedApplies == 2 && reopened.value,
        "failed runtime application restores persistence and the caller state");
    expect(accessibility, maximumSettingsPageScroll(8, 36, 290) == 0,
        "compact Accessibility viewport fits all eight supported and unavailable rows");
    expect(accessibility, clampSettingsPageScroll(40, 0) == 0 && clampSettingsPageScroll(-1, 0) == 0,
        "Accessibility page cannot scroll beyond its compact viewport");
    expect(accessibility, nextSettingsFocusIndex(2, -1, false) == 0 &&
        nextSettingsFocusIndex(2, -1, true) == 1,
        "keyboard Tab enters the first control and reverse Tab enters the last");
    expect(accessibility, nextSettingsFocusIndex(2, 1, false) == 0 &&
        nextSettingsFocusIndex(2, 0, true) == 1,
        "keyboard focus wraps across implemented accessibility controls");

    AppInventory sample = inventoryWith({
        {"com.guidexos.developerstudio", "Developer Studio", "1", "studio", "Developer Studio", "NativeElf", "SystemApps", 0, true},
        {"gxos.builtin.console", "Console", "", "console", "Console", "BuiltIn", "BuiltIn", 0, true},
        {"org.example.native", "Example Native", "1", "native", "Example Native", "NativeElf", "UserApps", 0, false}
    });
    const DeveloperAppModelSnapshot developerSnapshot = buildDeveloperAppModelSnapshot(sample);
    expect(developer, developerSnapshot.registeredCount == sample.count &&
        developerSnapshot.registryTotalCount == sample.totalCount && !developerSnapshot.truncated,
        "App Model count uses the bounded authoritative inventory");
    expect(developer, developerSnapshot.nativeElfCount == 2,
        "Native ELF registration count comes from registered app kinds");
    expect(developer, developerSnapshot.developerStudio.registered && developerSnapshot.developerStudio.openSupported,
        "registered Developer Studio exposes its normal open capability");
    expect(developer, developerSnapshot.console.registered && developerSnapshot.console.openSupported,
        "registered Console exposes its normal open capability");
    expect(developer, developerToolActionEnabled(developerSnapshot.developerStudio),
        "Developer Studio action is enabled only when open is supported");

    AppInventory absent = inventoryWith({
        {"org.example.other", "Other", "1", "other", "Other", "BuiltIn", "BuiltIn", 0, true}
    });
    const DeveloperAppModelSnapshot absentSnapshot = buildDeveloperAppModelSnapshot(absent);
    expect(developer, !absentSnapshot.developerStudio.registered && !developerToolActionEnabled(absentSnapshot.developerStudio),
        "absent Developer Studio is represented as unavailable");
    expect(developer, !absentSnapshot.console.registered && !developerToolActionEnabled(absentSnapshot.console),
        "absent Console is represented as unavailable");

    AppInventory unsupported = inventoryWith({
        {"com.guidexos.developerstudio", "Developer Studio", "1", "studio", "Developer Studio", "NativeElf", "SystemApps", 0, false}
    });
    expect(developer, unsupported.count == 1 && !findDeveloperTool(unsupported, kDeveloperStudioAppId).openSupported,
        "registration does not imply launch readiness");

    std::vector<AppInventoryEntry> many;
    for (int i = 0; i < static_cast<int>(kMaxSettingsApps) + 2; ++i) {
        many.push_back({"org.example." + std::to_string(i), "App " + std::to_string(i), "1", "app", "App", "BuiltIn", "BuiltIn", 0, true});
    }
    const AppInventory bounded = inventoryWith(std::move(many));
    const DeveloperAppModelSnapshot boundedSnapshot = buildDeveloperAppModelSnapshot(bounded);
    expect(developer, boundedSnapshot.registeredCount == kMaxSettingsApps &&
        boundedSnapshot.registryTotalCount == kMaxSettingsApps + 2 && boundedSnapshot.truncated,
        "truncation and total count are reported independently");

    uint64_t count = 0;
    const std::string preview = "summary: totalRecords=19 ready=5 unresolved=12 highRisk=12 printed=19 truncated=0\n";
    expect(developer, diagnosticFieldValue(preview, "highRisk") == "12",
        "diagnostic summary fields are read from runtime text");
    expect(developer, parseDiagnosticCount(preview, "totalRecords", count) && count == 19,
        "bounded diagnostic count parses a valid value");
    expect(developer, parseDiagnosticCount(preview, "unresolved", count) && count == 12,
        "unresolved preview count is exposed");
    expect(developer, !parseDiagnosticCount("summary: unresolved=12x", "unresolved", count),
        "malformed diagnostic count is rejected");
    expect(developer, !parseDiagnosticCount("summary: unresolved=18446744073709551616", "unresolved", count),
        "overflowing diagnostic count is rejected");
    expect(developer, !parseDiagnosticCount(preview, "missing", count),
        "missing diagnostic count is unavailable");
    expect(developer, diagnosticFieldValue("state=abcdefghijklmnopqrstuvwxyz", "state", 8) == "abcdefgh",
        "diagnostic values are bounded before display");
    expect(developer, !developerToolActionEnabled(DeveloperToolStatus{true, false, "registered"}),
        "registered tools without normal open support stay noninteractive");

    using gxos::system_service::ClientResult;
    expect(developer, serviceAvailability(ClientResult::Ok) == ServiceAvailability::Available,
        "successful service response is available");
    expect(developer, serviceAvailability(ClientResult::Unavailable) == ServiceAvailability::Unavailable &&
        serviceAvailability(ClientResult::Timeout) == ServiceAvailability::Unavailable,
        "service failures are not reported as available");
    expect(developer, anyServiceAvailable(ServiceAvailability::Unavailable,
        ServiceAvailability::Available, ServiceAvailability::Unavailable),
        "overall service summary reflects at least one actual response");
    expect(developer, !anyServiceAvailable(ServiceAvailability::Unavailable,
        ServiceAvailability::Unavailable, ServiceAvailability::Unavailable),
        "overall service summary reports no available snapshots when none answered");
    expect(developer, maximumSettingsPageScroll(9, 36, 290) == 34 &&
        maximumSettingsPageScroll(11, 36, 290) == 106,
        "compact Developer diagnostics use a scrollable viewport");
    expect(developer, clampSettingsPageScroll(200, 106) == 106,
        "Developer scroll position clamps before rows can leave the viewport");

    using namespace gxos::apps::settings;
    SettingsRoute route{};
    expect(integration, parseSettingsRoute("settings://accessibility", route) &&
        route.category == CategoryId::Accessibility && route.target == TargetId::Page,
        "Accessibility page deep link");
    expect(integration, parseSettingsRoute("settings://accessibility/focus", route) &&
        route.target == TargetId::AccessibilityFocus,
        "focus preference deep link");
    expect(integration, parseSettingsRoute("settings://accessibility/keyboard", route) &&
        route.target == TargetId::AccessibilityKeyboard,
        "on-screen keyboard deep link");
    expect(integration, parseSettingsRoute("settings://developer", route) &&
        route.category == CategoryId::Developer && route.target == TargetId::Page,
        "Developer page deep link");
    expect(integration, parseSettingsRoute("settings://developer/apps", route) &&
        route.target == TargetId::DeveloperApps,
        "Developer App Model deep link");
    expect(integration, parseSettingsRoute("settings://developer/services", route) &&
        route.target == TargetId::DeveloperServices,
        "Developer services deep link");
    expect(integration, parseSettingsRoute("settings://developer/diagnostics", route) &&
        route.target == TargetId::DeveloperDiagnostics,
        "Developer diagnostics deep link");
    expect(integration, !parseSettingsRoute("settings://developer/arbitrary", route),
        "unsupported Developer destination is rejected");

    const SearchResultSet focusSearch = searchSettings("enhanced focus");
    expect(integration, focusSearch.count > 0 && focusSearch.values[0].route.category == CategoryId::Accessibility &&
        focusSearch.values[0].route.target == TargetId::AccessibilityFocus,
        "search for focus reaches the implemented accessibility setting");
    const SearchResultSet keyboardSearch = searchSettings("on-screen keyboard");
    expect(integration, keyboardSearch.count > 0 && keyboardSearch.values[0].route.target == TargetId::AccessibilityKeyboard,
        "search for keyboard reaches its registered tool action");
    const SearchResultSet studioSearch = searchSettings("developer studio");
    expect(integration, studioSearch.count > 0 && studioSearch.values[0].route.category == CategoryId::Developer,
        "search for Developer Studio reaches the Developer page");
    const SearchResultSet appModelSearch = searchSettings("app model diagnostics");
    expect(integration, appModelSearch.count > 0 && appModelSearch.values[0].route.target == TargetId::DeveloperApps,
        "search for App Model reaches registry diagnostics");
    const SearchResultSet serviceSearch = searchSettings("system service");
    expect(integration, serviceSearch.count > 0 && serviceSearch.values[0].route.target == TargetId::DeveloperServices,
        "search for services reaches actual service status");
    const SearchResultSet diagnosticSearch = searchSettings("phase 5b");
    expect(integration, diagnosticSearch.count > 0 && diagnosticSearch.values[0].route.target == TargetId::DeveloperDiagnostics,
        "search for Phase 5B reaches the live App Model diagnostics page");

    std::cout << "Settings S7 Accessibility tests: " << accessibility.passed << "/"
              << (accessibility.passed + accessibility.failed) << " passed\n";
    std::cout << "Settings S7 Developer tests: " << developer.passed << "/"
              << (developer.passed + developer.failed) << " passed\n";
    std::cout << "Settings S7 navigation/search tests: " << integration.passed << "/"
              << (integration.passed + integration.failed) << " passed\n";
    const int failures = accessibility.failed + developer.failed + integration.failed;
    return failures == 0 ? 0 : 1;
}
