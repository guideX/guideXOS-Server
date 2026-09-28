#include "settings_center_model.h"
#include "built_in_app_metadata.h"

#include <iostream>
#include <string>

using namespace gxos::apps::settings;
using namespace gxos::apps;

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* name)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}
}

int main()
{
    check(registryIntegrity(), "category registry has stable unique identifiers");
    check(kCategories.size() == 12, "all twelve Settings categories are registered");
    const BuiltInAppMetadata* settingsMetadata = FindBuiltInAppMetadataByAppId("gxos.builtin.settings");
    const BuiltInAppMetadata* controlPanelMetadata = FindBuiltInAppMetadataByAppId("gxos.builtin.controlpanel");
    const BuiltInAppMetadata* displayOptionsMetadata = FindBuiltInAppMetadataByAppId("gxos.builtin.displayoptions");
    check(settingsMetadata && IsBuiltInAppAvailableInHosted(*settingsMetadata), "Settings is registered as a hosted built-in app");
    check(controlPanelMetadata && IsBuiltInAppAvailableInHosted(*controlPanelMetadata), "Control Panel remains a hosted built-in app");
    check(displayOptionsMetadata && IsBuiltInAppAvailableInHosted(*displayOptionsMetadata), "Display Options remains registered for advanced settings");

    SettingsRoute route;
    check(parseSettingsRoute("settings://display", route) && route.category == CategoryId::Display && route.target == TargetId::Page,
          "category deep link resolves");
    check(parseSettingsRoute("settings://display/resolution", route) && route.target == TargetId::Resolution,
          "display resolution deep link resolves");
    check(parseSettingsRoute(" SETTINGS://NETWORK/IPv4 ", route) && route.category == CategoryId::Network && route.target == TargetId::IPv4,
          "network IPv4 deep link is case and whitespace tolerant");
    check(parseSettingsRoute("settings://network/dns", route) && route.target == TargetId::DNS,
          "network DNS deep link resolves");
    check(parseSettingsRoute("settings://network", route) && route.category == CategoryId::Network && route.target == TargetId::Page,
          "Network & Internet category deep link resolves");
    check(parseSettingsRoute("settings://storage", route) && route.category == CategoryId::Storage,
          "storage deep link resolves");
    check(parseSettingsRoute("settings://about/version", route) && route.target == TargetId::AboutVersion,
          "about version deep link resolves");
    check(!parseSettingsRoute("settings://unknown", route), "unknown category deep link is rejected");
    check(!parseSettingsRoute("settings://storage/resolution", route), "target from another category is rejected");

    SearchResultSet results = searchSettings("resolution");
    check(results.count > 0 && results.values[0].route.category == CategoryId::Display && results.values[0].route.target == TargetId::Resolution,
          "resolution keyword maps to Display resolution");
    results = searchSettings("dns");
    check(results.count > 0 && results.values[0].route.category == CategoryId::Network && results.values[0].route.target == TargetId::DNS,
          "DNS keyword maps to Network DNS");
    results = searchSettings("adapter");
    check(results.count > 0 && results.values[0].route.category == CategoryId::Network,
          "adapter keyword maps to Network & Internet");
    results = searchSettings("ethernet");
    check(results.count > 0 && results.values[0].route.category == CategoryId::Network,
          "ethernet keyword maps to Network & Internet");
    results = searchSettings("ipv4");
    check(results.count > 0 && results.values[0].route.category == CategoryId::Network && results.values[0].route.target == TargetId::IPv4,
          "IPv4 keyword maps to IP assignment");
    results = searchSettings("ip address");
    check(results.count > 0 && results.values[0].route.category == CategoryId::Network && results.values[0].route.target == TargetId::IPv4,
          "IP address phrase maps to IPv4 details");
    results = searchSettings("dhcp");
    check(results.count > 0 && results.values[0].route.target == TargetId::IPv4,
          "DHCP keyword maps to IP assignment");
    results = searchSettings("gateway");
    check(results.count > 0 && results.values[0].route.target == TargetId::Gateway,
          "gateway keyword maps to Gateway");
    results = searchSettings("subnet");
    check(results.count > 0 && results.values[0].route.target == TargetId::IPv4,
          "subnet keyword maps to IPv4 details");
    results = searchSettings("disk");
    check(results.count > 0 && results.values[0].route.category == CategoryId::Storage,
          "disk keyword maps to Storage");
    check(searchSettings("").count == 0, "empty search has no results");
    check(searchSettings("no-such-setting").count == 0, "unknown search has no fabricated results");
    check(searchSettings("settings").count <= SearchResultSet::kCapacity, "search result count is bounded");

    NavigationModel navigation;
    check(navigation.selectedCategory() == CategoryId::System, "navigation starts on System");
    check(navigation.navigate(SettingsRoute{ CategoryId::Display, TargetId::Resolution }), "navigation accepts valid deep links");
    check(navigation.canFocus(FocusControl::DisplayResolution), "selected Display page can focus its resolution row");
    check(!navigation.canFocus(FocusControl::StorageDiskManager), "hidden Storage action cannot receive focus on Display");
    navigation.selectCategory(CategoryId::Storage);
    check(navigation.route() == SettingsRoute{ CategoryId::Storage, TargetId::Page }, "category selection clears the prior deep target");
    check(navigation.canFocus(FocusControl::StorageDiskManager), "selected Storage page can focus its action");
    check(!navigation.navigate(SettingsRoute{ CategoryId::Count, TargetId::Page }), "invalid category navigation is rejected");
    check(!navigation.canFocus(FocusControl::DisplayAdvanced), "Display action is excluded after navigating away");
    navigation.selectCategory(CategoryId::Network);
    check(navigation.canFocus(FocusControl::NetworkAdvanced), "Network page can focus its advanced diagnostics action");
    check(navigation.canFocus(FocusControl::NetworkAdapter), "Network page can focus adapter rows");
    navigation.selectCategory(CategoryId::Storage);
    check(!navigation.canFocus(FocusControl::NetworkAdvanced), "Network advanced action is excluded after navigating away");
    for (int i = 0; i < 100; ++i) {
        navigation.selectCategory(static_cast<CategoryId>(i % static_cast<int>(CategoryId::Count)));
    }
    check(navigation.selectedCategory() == CategoryId::Personalization, "repeated category switching has deterministic selection");

    check(formatDisplayResolution(1920, 1080) == "1920 x 1080", "display state binding formats a real resolution");
    check(formatDisplayResolution(0, 0) == "Resolution unavailable", "missing display state remains unavailable");
    check(std::string(formatIpAssignment(IpAssignment::DHCP)) == "DHCP", "DHCP assignment is represented truthfully");
    check(std::string(formatIpAssignment(IpAssignment::Static)) == "Static", "static assignment is represented truthfully");
    check(std::string(formatIpAssignment(IpAssignment::Unavailable)) == "Unavailable", "unknown assignment is not guessed");
    check(formatNetworkAddress("") == "Address unavailable", "empty IPv4 address is unavailable");
    check(formatNetworkAddress("10.1.9.42") == "10.1.9.42", "provided IPv4 address is preserved");
    check(std::string(formatNetworkConnection(true, true, true)) == "Connected", "link with address formats Connected");
    check(std::string(formatNetworkConnection(true, true, false)) == "Link up, no lease", "link without address formats no lease");
    check(std::string(formatNetworkConnection(true, false, false)) == "Disconnected", "down link formats Disconnected");
    check(std::string(formatNetworkConnection(false, false, false)) == "Status unavailable", "unknown link state stays unavailable");

    const char* advanced = nullptr;
    check(builtInAdvancedTarget(CategoryId::Display, advanced) && std::string(advanced) == "DisplayOptions",
          "Display advanced action routes to Display Options");
    check(builtInAdvancedTarget(CategoryId::Storage, advanced) && std::string(advanced) == "DiskManager",
          "Storage action routes to Disk Manager");
    check(!builtInAdvancedTarget(CategoryId::Network, advanced), "Network does not link to a nonexistent configuration tool");

    std::cout << "Settings Center model tests: " << (checks - failures) << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
