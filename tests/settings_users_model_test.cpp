#include "settings_users_model.h"
#include "settings_s7_model.h"

#include <iostream>
#include <string>

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

bool firstSearchRouteIs(const std::string& query, CategoryId category, TargetId target)
{
    const SearchResultSet results = searchSettings(query);
    return results.count > 0 && results.values[0].route.category == category &&
        results.values[0].route.target == target;
}
}

int main()
{
    Counts model;
    Counts navigation;
    Counts interaction;

    const UsersPageState state = buildUsersPageState("Hosted on Windows");
    expect(model, state.accountModel == UsersInfoState::NotSupported,
        "the page states that account management is not supported");
    expect(model, state.currentIdentity == UsersInfoState::NotDefined &&
        state.sessionIdentity == UsersInfoState::NotDefined,
        "no human identity or stable guideXOS session identity is invented");
    expect(model, state.authentication == UsersInfoState::NotSupported,
        "authentication is reported as unsupported");
    expect(model, state.profiles == UsersInfoState::NotSupported,
        "separate user profiles are reported as unsupported");
    expect(model, state.sessionManager == UsersInfoState::NotSupported &&
        state.sessionActions == UsersInfoState::NotSupported,
        "session manager and user-switching actions are not fabricated");
    expect(model, state.userPermissions == UsersInfoState::NotSupported &&
        state.fileOwnership == UsersInfoState::NotSupported &&
        state.userGrantedAppPermissions == UsersInfoState::NotSupported,
        "user permissions, file ownership, and user-granted app access are unsupported");
    expect(model, state.servicePeerAuthentication == UsersInfoState::Unavailable,
        "unauthenticated COM2 service peer state is kept distinct from unsupported login");
    expect(model, state.hostedRuntime == "Hosted on Windows",
        "the hosted runtime is labeled as environment context");

    const UsersPageState emptyRuntime = buildUsersPageState("");
    const UsersPageState unavailableRuntime = buildUsersPageState("Hosted runtime details unavailable");
    expect(model, emptyRuntime.hostedRuntime == "Unavailable" &&
        unavailableRuntime.hostedRuntime == "Unavailable",
        "missing runtime provider data is reported as unavailable");
    const std::string longRuntime(120, 'R');
    const std::string boundedRuntime = formatUsersHostedRuntime(longRuntime);
    expect(model, boundedRuntime.size() == 44 && boundedRuntime.substr(41) == "...",
        "long hosted runtime strings are bounded with an ellipsis");
    expect(model, usersInfoStateText(UsersInfoState::NotDefined) == std::string("Not defined") &&
        usersInfoStateText(UsersInfoState::NotSupported) == std::string("Not supported") &&
        usersInfoStateText(UsersInfoState::Unavailable) == std::string("Unavailable"),
        "not defined, not supported, and unavailable remain distinct labels");

    SettingsRoute route{};
    expect(navigation, parseSettingsRoute("settings://users", route) &&
        route == SettingsRoute{CategoryId::Users, TargetId::Page},
        "Users top-level deep link resolves to the account model overview");
    expect(navigation, parseSettingsRoute("settings://users/session", route) &&
        route == SettingsRoute{CategoryId::Users, TargetId::UsersSession},
        "session detail deep link resolves to the real session view");
    expect(navigation, parseSettingsRoute("settings://users/security", route) &&
        route == SettingsRoute{CategoryId::Users, TargetId::UsersSecurity},
        "security detail deep link resolves to the real security view");
    expect(navigation, !parseSettingsRoute("settings://users/login", route) &&
        !parseSettingsRoute("settings://users/account-management", route),
        "unsupported login and account-management destinations are rejected");
    expect(navigation, !parseSettingsRoute("settings://users/services", route),
        "developer service diagnostics are not misrepresented as a Users route");

    const char* topLevelRoutes[] = {
        "settings://system", "settings://display", "settings://network",
        "settings://personalization", "settings://devices", "settings://storage",
        "settings://apps", "settings://users", "settings://date-time",
        "settings://accessibility", "settings://developer", "settings://about"
    };
    bool allTopLevelRoutesResolve = true;
    for (const char* value : topLevelRoutes) {
        SettingsRoute topLevel{};
        allTopLevelRoutesResolve = parseSettingsRoute(value, topLevel) &&
            topLevel.target == TargetId::Page && allTopLevelRoutesResolve;
    }
    expect(navigation, allTopLevelRoutesResolve,
        "all twelve top-level Settings deep links resolve without stale pages");

    NavigationModel navigationModel;
    expect(navigation, navigationModel.navigate(SettingsRoute{CategoryId::Users, TargetId::UsersSecurity}) &&
        !navigationModel.navigate(SettingsRoute{CategoryId::Users, TargetId::DeveloperServices}),
        "the navigation model accepts implemented Users views and rejects unrelated targets");
    expect(navigation, focusControlCategory(FocusControl::UsersBack) == CategoryId::Users &&
        focusControlCategory(FocusControl::UsersDeveloperServices) == CategoryId::Users,
        "Users detail actions remain in the Users focus domain");

    expect(interaction, firstSearchRouteIs("accounts", CategoryId::Users, TargetId::Page),
        "account searches lead to the truthful Users overview");
    expect(interaction, firstSearchRouteIs("identity", CategoryId::Users, TargetId::Page),
        "identity searches lead to the Users overview");
    expect(interaction, firstSearchRouteIs("session", CategoryId::Users, TargetId::UsersSession),
        "session searches lead to session details");
    expect(interaction, firstSearchRouteIs("password", CategoryId::Users, TargetId::UsersSecurity),
        "password searches lead to the page that states authentication is unsupported");
    expect(interaction, firstSearchRouteIs("profile", CategoryId::Users, TargetId::Page),
        "profile searches explain that separate profiles are unsupported");
    expect(interaction, firstSearchRouteIs("permissions", CategoryId::Users, TargetId::UsersSecurity),
        "permission searches lead to the real security summary");
    const SearchResultSet repeated = searchSettings("password");
    expect(interaction, repeated.count == 1 &&
        std::string(repeated.values[0].label) == "Authentication and passwords",
        "password search results are bounded and deterministic");

    expect(interaction, usersPageRowCount(TargetId::Page) == 8 &&
        usersPageRowCount(TargetId::UsersSession) == 8 &&
        usersPageRowCount(TargetId::UsersSecurity) == 7,
        "all Users views use bounded row counts");
    expect(interaction, maximumSettingsPageScroll(usersPageRowCount(TargetId::Page), 36, 220) == 68 &&
        clampSettingsPageScroll(100, 68) == 68,
        "compact Users layout has bounded scrolling when the viewport is short");
    expect(interaction, maximumSettingsPageScroll(usersPageRowCount(TargetId::Page), 36, 290) == 0,
        "the Users overview fits the 640 by 480 Settings row viewport");
    expect(interaction, fitSettingsWindowDimension(640, 980, 24, 760) == 640 &&
        fitSettingsWindowDimension(480, 700, 60, 540) == 480,
        "Settings fits the full 640 by 480 desktop when margins are not available");
    expect(interaction, fitSettingsWindowDimension(1920, 980, 24, 760) == 980 &&
        fitSettingsWindowDimension(800, 980, 24, 760) == 776,
        "Settings keeps its preferred size on large displays and uses available width on small displays");
    expect(interaction, nextSettingsFocusIndex(2, -1, false) == 0 &&
        nextSettingsFocusIndex(2, 1, false) == 0 && nextSettingsFocusIndex(2, 0, true) == 1,
        "keyboard traversal enters, wraps, and reverses between Users detail actions");

    std::cout << "Settings S8 Users model tests: " << model.passed << "/"
              << model.passed + model.failed << " passed\n";
    std::cout << "Settings S8 Users navigation tests: " << navigation.passed << "/"
              << navigation.passed + navigation.failed << " passed\n";
    std::cout << "Settings S8 Users interaction/layout tests: " << interaction.passed << "/"
              << interaction.passed + interaction.failed << " passed\n";
    return model.failed + navigation.failed + interaction.failed == 0 ? 0 : 1;
}
