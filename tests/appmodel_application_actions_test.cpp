#include "app_manifest_loader.h"
#include "app_manifest_validator.h"
#include "app_registry.h"
#include "built_in_document_dispatcher.h"
#include "start_menu_action_model.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace gxos::apps;

int checks = 0;
int failures = 0;
int dispatched = 0;
std::string dispatchedAppId;
std::string dispatchedActionId;

void check(bool condition, const std::string& name) {
    ++checks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else { ++failures; std::cout << "FAIL: " << name << "\n"; }
}

RegisteredApp actionApp(const std::string& id,
                        const std::vector<AppActionDeclaration>& actions,
                        bool backendAvailable = true) {
    RegisteredApp app;
    app.sourceKind = AppSourceKind::Package;
    app.appActionBackendAvailable = backendAvailable;
    app.manifest.id = id;
    app.manifest.displayName = id;
    app.manifest.version = "1.0.0";
    app.manifest.publisher = "Phase 17 test fixture";
    app.manifest.kind = AppKind::BuiltIn;
    app.manifest.actions = actions;
    return app;
}

bool captureAction(const AppActionInfo& action, bool&, std::string&) {
    ++dispatched;
    dispatchedAppId = action.appId;
    dispatchedActionId = action.actionId;
    return true;
}

bool registerDurable(AppRegistry& registry, const RegisteredApp& app) {
    std::string error;
    return registry.RegisterTestDurableApp(app, error);
}

AppManifest basicManifest() {
    AppManifest manifest;
    manifest.id = "test.phase17.actions";
    manifest.displayName = "Action Test";
    manifest.version = "1";
    manifest.publisher = "Phase 17";
    manifest.kind = AppKind::BuiltIn;
    return manifest;
}
}

int main() {
    AppRegistry production;
    production.RegisterBuiltInAppsAsManifests();
    const AppActionList navigator = production.EnumerateAppActions("guidexos.navigator");
    check(navigator.appFound && navigator.declaredActionCount == 1 && navigator.count == 1 &&
        navigator.actions[0].appId == "guidexos.navigator" && navigator.actions[0].actionId == "open-home" &&
        navigator.actions[0].label == "Home" && navigator.actions[0].available,
        "production Navigator registration owns one available value-owned Home action");
    const gxos::gui::StartMenuAppContextMenu navigatorMenu =
        gxos::gui::BuildStartMenuAppContextMenu(navigator, true, true);
    check(navigatorMenu.rows.size() == 4 &&
        navigatorMenu.rows[3].kind == gxos::gui::StartMenuAppContextRowKind::AppAction &&
        navigatorMenu.rows[3].label == navigator.actions[0].label &&
        navigatorMenu.rows[3].action.appId == "guidexos.navigator" &&
        navigatorMenu.rows[3].action.actionId == navigator.actions[0].actionId,
        "Start context menu copies the production declaration label and canonical action identity");
    const gxos::gui::StartMenuAppContextMenu navigatorRecentMenu =
        gxos::gui::BuildStartMenuAppContextMenu(navigator, false, true);
    check(navigatorRecentMenu.rows.size() == 6 &&
        navigatorRecentMenu.rows.back().kind == gxos::gui::StartMenuAppContextRowKind::RemoveRecent,
        "Recent Programs menu keeps its existing generic commands around declared action rows");
    const AppActionResolution navigatorAction = production.ResolveAppAction("guidexos.navigator", "open-home");
    check(navigatorAction.invocable() && production.IsAppActionCurrent(navigatorAction.action),
        "canonical app ID plus action ID resolves and revalidates the production declaration");

    bool zeroActionApps = true;
    for (const std::string id : { "gxos.builtin.notepad", "gxos.builtin.fileexplorer",
                                  "gxos.builtin.imageviewer", "com.guidexos.developerstudio" }) {
        const AppActionList list = production.EnumerateAppActions(id);
        if (list.appFound) zeroActionApps = zeroActionApps && list.count == 0 && list.availableActionCount == 0;
    }
    check(zeroActionApps && production.EnumerateAppActions("gxos.builtin.notepad").appFound &&
        production.EnumerateAppActions("gxos.builtin.notepad").count == 0,
        "Notepad, File Explorer, ImageViewer, and registered Developer Studio expose no speculative actions");
    const AppActionList zeroActions = production.EnumerateAppActions("gxos.builtin.notepad");
    const gxos::gui::StartMenuAppContextMenu zeroActionMenu =
        gxos::gui::BuildStartMenuAppContextMenu(zeroActions, false, false);
    check(zeroActionMenu.rows.size() == 4 &&
        zeroActionMenu.rows[0].kind == gxos::gui::StartMenuAppContextRowKind::Open &&
        zeroActionMenu.rows[1].kind == gxos::gui::StartMenuAppContextRowKind::PinToggle &&
        zeroActionMenu.rows[2].kind == gxos::gui::StartMenuAppContextRowKind::Separator &&
        zeroActionMenu.rows[3].kind == gxos::gui::StartMenuAppContextRowKind::RemoveRecent,
        "zero-action app keeps ordinary Start commands and receives no empty action group");
    check(!production.EnumerateAppActions("missing.app").appFound,
        "unknown applications enumerate no actions");
    const AppActionList unknownSnapshot = production.EnumerateAppActions("missing.app");
    check(gxos::gui::BuildStartMenuAppActionRows(unknownSnapshot).empty(),
        "unknown application identity constructs no action rows");

    AppManifest manifest = basicManifest();
    check(IsValidAppActionId("open-home") && IsValidAppActionId("a") &&
        IsValidAppActionId(std::string(kAppModelMaxActionIdBytes, 'a')),
        "lowercase ASCII action IDs accept alphanumerics, interior hyphens, and the 48-byte limit");
    check(!IsValidAppActionId("") && !IsValidAppActionId("Open-Home") &&
        !IsValidAppActionId("-open") && !IsValidAppActionId("open-") &&
        !IsValidAppActionId("open--home") && !IsValidAppActionId("open_home") &&
        !IsValidAppActionId("open/home") &&
        !IsValidAppActionId(std::string(kAppModelMaxActionIdBytes + 1, 'a')),
        "empty, noncanonical, malformed, and overlong action IDs fail grammar validation");

    for (size_t i = 0; i < kAppModelMaxActionsPerApp; ++i)
        manifest.actions.push_back({ "action-" + std::to_string(kAppModelMaxActionsPerApp - i), "Same label" });
    manifest.actions[0].label = std::string(kAppModelMaxActionLabelBytes, 'L');
    check(AppManifestValidator::Validate(manifest).valid,
        "manifest accepts exactly 12 actions, a 48-byte ID, a 64-byte label, and duplicate labels");
    manifest.actions.push_back({ "one-too-many", "Overflow" });
    check(!AppManifestValidator::Validate(manifest).valid,
        "manifest rejects action bound plus one");
    manifest.actions.pop_back();
    manifest.actions[1].id = manifest.actions[0].id;
    check(!AppManifestValidator::Validate(manifest).valid,
        "manifest rejects duplicate action IDs deterministically");
    manifest.actions[1].id = "action-11";
    manifest.actions[1].label = std::string(kAppModelMaxActionLabelBytes + 1, 'L');
    check(!AppManifestValidator::Validate(manifest).valid,
        "manifest rejects labels over the 64-byte bound");
    manifest.actions[1].label = "Same label";
    manifest.actions[2].id = "bad_ID";
    check(!AppManifestValidator::Validate(manifest).valid,
        "manifest rejects malformed action identity even when its label is valid");

    AppManifestLoadResult loaded;
    const std::filesystem::path fixture = std::filesystem::temp_directory_path() /
        "guidexos-phase17-action-manifest.json";
    {
        std::ofstream file(fixture, std::ios::binary | std::ios::trunc);
        file << R"({"schemaVersion":1,"id":"test.phase17.loader","displayName":"Loader Test","version":"1","kind":"BuiltIn","actions":[{"id":"open-home","label":"Home"}]})";
    }
    loaded = AppManifestLoader::LoadFromFile(fixture);
    std::error_code removeError;
    std::filesystem::remove(fixture, removeError);
    check(loaded.valid && loaded.manifest.actions.size() == 1 &&
        loaded.manifest.actions[0].id == "open-home" && loaded.manifest.actions[0].label == "Home",
        "manifest loader preserves typed action declarations as owned manifest metadata");

    AppRegistry registry;
    RegisteredApp synthetic = actionApp("test.phase17.synthetic", {
        { "zeta", "Duplicate" }, { "alpha", "Duplicate" }, { "middle-2", "Middle" }
    });
    check(registerDurable(registry, synthetic), "synthetic multi-action manifest registers through AppRegistry");
    AppActionList actions = registry.EnumerateAppActions(synthetic.manifest.id);
    check(actions.count == 3 && actions.availableActionCount == 3 && !actions.truncated &&
        actions.actions[0].actionId == "alpha" && actions.actions[1].actionId == "middle-2" &&
        actions.actions[2].actionId == "zeta",
        "multi-action enumeration is deterministically sorted by canonical action ID");
    check(actions.actions[0].label == actions.actions[2].label,
        "equal presentation labels remain distinct because action IDs are authoritative");
    const gxos::gui::StartMenuAppContextMenu syntheticMenu =
        gxos::gui::BuildStartMenuAppContextMenu(actions, true, false);
    check(syntheticMenu.rows.size() == 6 &&
        syntheticMenu.rows[3].label == "Duplicate" && syntheticMenu.rows[4].label == "Middle" &&
        syntheticMenu.rows[5].label == "Duplicate" &&
        syntheticMenu.rows[3].action.actionId == "alpha" &&
        syntheticMenu.rows[4].action.actionId == "middle-2" &&
        syntheticMenu.rows[5].action.actionId == "zeta",
        "Start rows preserve deterministic enumeration order and duplicate labels keep distinct action IDs");
    const AppActionInfo ownedSnapshot = syntheticMenu.rows[3].action;
    actions.actions[0].label = "caller mutation";
    check(registry.IsAppActionCurrent(ownedSnapshot) && ownedSnapshot.label == "Duplicate",
        "menu-held action snapshot owns its declaration label after source mutation");

    AppRegistry bounded;
    std::vector<AppActionDeclaration> maximumActions;
    for (size_t i = 0; i < kAppModelMaxActionsPerApp; ++i)
        maximumActions.push_back({ "item-" + std::to_string(i), "Item " + std::to_string(i) });
    RegisteredApp maximumApp = actionApp("test.phase17.maximum", maximumActions);
    check(registerDurable(bounded, maximumApp) &&
        bounded.EnumerateAppActions(maximumApp.manifest.id).count == kAppModelMaxActionsPerApp,
        "exact per-app action maximum registers and enumerates without truncation");
    const AppActionList maximumSnapshot = bounded.EnumerateAppActions(maximumApp.manifest.id);
    const auto maximumAllProgramsMenu = gxos::gui::BuildStartMenuAppContextMenu(maximumSnapshot, true, false);
    const auto maximumRecentMenu = gxos::gui::BuildStartMenuAppContextMenu(maximumSnapshot, false, true);
    check(maximumAllProgramsMenu.rows.size() == 15 && maximumRecentMenu.rows.size() == 17 &&
        maximumRecentMenu.rows[14].kind == gxos::gui::StartMenuAppContextRowKind::AppAction &&
        maximumRecentMenu.rows[16].kind == gxos::gui::StartMenuAppContextRowKind::RemoveRecent,
        "maximum 12-action presentation remains bounded in All Programs and Recent Programs menus");
    const int maximumRecentMenuHeight = static_cast<int>(maximumRecentMenu.rows.size()) * 28;
    const int menuX = gxos::gui::ClampStartMenuContextOrigin(550, 640, 220);
    const int menuY = gxos::gui::ClampStartMenuContextOrigin(400, 480, maximumRecentMenuHeight);
    check(menuX == 420 && menuY == 4 && menuX + 220 <= 640 && menuY + maximumRecentMenuHeight <= 480,
        "maximum Recent Programs menu stays fully inside a 640x480 viewport");
    AppActionList longLabelSnapshot;
    longLabelSnapshot.appId = "test.phase18.long-label";
    longLabelSnapshot.count = 1;
    longLabelSnapshot.actions[0].appId = longLabelSnapshot.appId;
    longLabelSnapshot.actions[0].actionId = "long-label";
    longLabelSnapshot.actions[0].label = std::string(kAppModelMaxActionLabelBytes, 'L');
    longLabelSnapshot.actions[0].registrationGeneration = 7;
    longLabelSnapshot.actions[0].registrationCurrent = true;
    longLabelSnapshot.actions[0].backendAvailable = true;
    longLabelSnapshot.actions[0].available = true;
    const auto longLabelRows = gxos::gui::BuildStartMenuAppActionRows(longLabelSnapshot);
    check(longLabelRows.size() == 1 && longLabelRows[0].label.size() == kAppModelMaxActionLabelBytes &&
        longLabelRows[0].action.actionId == "long-label",
        "near-maximum 64-byte action label is retained as an owned bounded menu row");
    AppActionList malformedCountSnapshot;
    malformedCountSnapshot.appId = "test.phase18.bounded-count";
    malformedCountSnapshot.count = 1000;
    check(gxos::gui::BuildStartMenuAppActionRows(malformedCountSnapshot).empty(),
        "menu construction clamps a malformed snapshot count to its fixed action array");
    maximumActions.push_back({ "overflow", "Overflow" });
    check(!registerDurable(bounded, actionApp("test.phase17.overflow", maximumActions)),
        "AppRegistry rejects per-app maximum plus one");

    AppRegistry backend;
    const RegisteredApp backendApp = actionApp("test.phase17.backend", { { "run", "Run" } }, false);
    check(registerDurable(backend, backendApp),
        "app with an unavailable backend registers its bounded declaration");
    const AppActionList unavailableActionSnapshot = backend.EnumerateAppActions("test.phase17.backend");
    check(unavailableActionSnapshot.count == 0 &&
        gxos::gui::BuildStartMenuAppActionRows(unavailableActionSnapshot).empty() &&
        backend.ResolveAppAction("test.phase17.backend", "run").status == AppActionResolutionStatus::HandlerUnavailable,
        "declarations with no available application backend are not exposed as invocable actions");
    check(backend.SetTestAppActionBackend("test.phase17.backend", true) &&
        backend.EnumerateAppActions("test.phase17.backend").count == 1,
        "current backend availability reveals its statically declared action");
    const AppActionInfo backendSnapshot = backend.EnumerateAppActions("test.phase17.backend").actions[0];
    const auto backendMenuRows = gxos::gui::BuildStartMenuAppActionRows(
        backend.EnumerateAppActions("test.phase17.backend"));
    check(backend.SetTestAppActionBackend("test.phase17.backend", false) &&
        backend.SetTestAppActionBackend("test.phase17.backend", true) &&
        !backend.IsAppActionCurrent(backendSnapshot),
        "backend capability mutation changes registration generation and invalidates stale snapshots");
    check(backendMenuRows.size() == 1 && backendMenuRows[0].action.registrationGeneration ==
        backendSnapshot.registrationGeneration,
        "menu row retains the enumerated generation across later backend changes");

    BuiltInAppActionDispatcher dispatcher;
    check(dispatcher.RegisterHandler(synthetic.manifest.id, &captureAction),
        "generic action dispatcher registers one application-owned handler by canonical ID");
    std::string error;
    bool launched = false;
    check(dispatcher.RegisterHandler("test.phase17.backend", &captureAction) &&
        !dispatcher.Dispatch(backend, backendMenuRows[0].action, launched, error),
        "displayed menu action fails closed after backend change and re-registration");
    dispatched = 0;
    check(dispatcher.Dispatch(registry, ownedSnapshot, launched, error) && dispatched == 1 &&
        dispatchedAppId == synthetic.manifest.id && dispatchedActionId == "alpha" && !launched,
        "generic dispatcher delivers only canonical app ID and declared action ID to the app handler");

    const int beforeMenuLifecycleCycles = dispatched;
    int menuLifecycleCycles = 0;
    bool menuLifecyclePassed = true;
    for (int i = 0; i < 50; ++i) {
        const AppActionList displayedActions = registry.EnumerateAppActions(synthetic.manifest.id);
        gxos::gui::StartMenuAppContextMenu menuCycle =
            gxos::gui::BuildStartMenuAppContextMenu(displayedActions, true, false);
        const bool selectedDuplicateLabelAction = menuCycle.rows.size() == 6 &&
            menuCycle.rows[5].kind == gxos::gui::StartMenuAppContextRowKind::AppAction &&
            menuCycle.rows[5].label == "Duplicate" && menuCycle.rows[5].action.actionId == "zeta";
        const AppActionInfo selectedAction = selectedDuplicateLabelAction
            ? menuCycle.rows[5].action : AppActionInfo{};
        menuLifecyclePassed = menuLifecyclePassed && selectedDuplicateLabelAction &&
            dispatcher.Dispatch(registry, selectedAction, launched, error);
        menuCycle.rows.clear();
        menuLifecyclePassed = menuLifecyclePassed && menuCycle.rows.empty();
        ++menuLifecycleCycles;
    }
    check(menuLifecycleCycles == 50 && menuLifecyclePassed &&
        dispatched == beforeMenuLifecycleCycles + 50 && dispatchedActionId == "zeta",
        "50 Start menu model build, duplicate-label selection, dispatch, and close cycles stay identity-safe");

    check(registry.ResolveAppAction("missing.app", "open-home").status == AppActionResolutionStatus::UnknownApp &&
        registry.ResolveAppAction(synthetic.manifest.id, "unknown").status == AppActionResolutionStatus::ActionNotDeclared &&
        registry.ResolveAppAction(synthetic.manifest.id, "bad_ID").status == AppActionResolutionStatus::InvalidActionId &&
        registry.ResolveAppAction(synthetic.manifest.id, "").status == AppActionResolutionStatus::InvalidActionId &&
        registry.ResolveAppAction(synthetic.manifest.id, std::string(kAppModelMaxActionIdBytes + 1, 'a')).status == AppActionResolutionStatus::InvalidActionId,
        "unknown application, unknown action, empty, malformed, and overlong identities fail closed");
    check(dispatched == beforeMenuLifecycleCycles + 50,
        "failed resolutions do not reach the application action implementation");

    AppActionInfo staleSnapshot;
    RegisteredApp temporary = actionApp("test.phase17.reusable", { { "open-home", "Home" } });
    temporary.sourceKind = AppSourceKind::DevelopmentTemporary;
    temporary.temporaryDevelopment = true;
    temporary.temporaryOwnerRuntimeId = 77;
    temporary.temporaryGeneration = 1;
    check(registry.RegisterTemporaryDevelopmentApp(temporary, error),
        "temporary action registration enters the same bounded registry");
    const auto displayedTemporaryActionRows = gxos::gui::BuildStartMenuAppActionRows(
        registry.EnumerateAppActions(temporary.manifest.id));
    check(displayedTemporaryActionRows.size() == 1 &&
        displayedTemporaryActionRows[0].action.actionId == "open-home",
        "menu model retains a value-owned row from a temporary registration");
    staleSnapshot = displayedTemporaryActionRows[0].action;
    check(registry.UnregisterTemporaryDevelopmentApp(temporary.manifest.id, 77, 1),
        "unregister removes action authority with the registration");
    const std::string oldTemporaryAppId = temporary.manifest.id;
    temporary.manifest.id = "test.phase17.slot-reuse";
    temporary.temporaryGeneration = 1;
    check(registry.RegisterTemporaryDevelopmentApp(temporary, error) &&
        registry.ResolveAppAction(oldTemporaryAppId, "open-home").status == AppActionResolutionStatus::UnknownApp &&
        !dispatcher.Dispatch(registry, staleSnapshot, launched, error),
        "a different app reusing the unregistered storage slot cannot inherit stale action authority");
    check(registry.UnregisterTemporaryDevelopmentApp(temporary.manifest.id, 77, 1),
        "slot-reuse test registration unregisters cleanly");
    temporary.manifest.id = oldTemporaryAppId;
    temporary.temporaryGeneration = 2;
    check(registry.RegisterTemporaryDevelopmentApp(temporary, error),
        "replacement reuses the canonical registration slot with a new generation");
    check(!registry.IsAppActionCurrent(staleSnapshot) &&
        registry.ResolveAppAction(staleSnapshot.appId, staleSnapshot.actionId,
            staleSnapshot.registrationGeneration).status == AppActionResolutionStatus::RegistrationStale,
        "stale generation cannot invoke a same-ID replacement registration");
    const AppActionInfo replacement = registry.EnumerateAppActions(temporary.manifest.id).actions[0];
    check(replacement.registrationGeneration != staleSnapshot.registrationGeneration &&
        dispatcher.RegisterHandler(temporary.manifest.id, &captureAction),
        "replacement action carries fresh generation authority");
    const int beforeStaleDispatch = dispatched;
    check(!dispatcher.Dispatch(registry, staleSnapshot, launched, error) && dispatched == beforeStaleDispatch,
        "generic dispatcher revalidates generation immediately before callback");

    RegisteredApp removedActionApp = actionApp("test.phase18.removed-action", { { "run", "Run" } });
    removedActionApp.sourceKind = AppSourceKind::DevelopmentTemporary;
    removedActionApp.temporaryDevelopment = true;
    removedActionApp.temporaryOwnerRuntimeId = 88;
    removedActionApp.temporaryGeneration = 1;
    check(registry.RegisterTemporaryDevelopmentApp(removedActionApp, error),
        "removed-action fixture registers with one declared action");
    const auto removedActionRows = gxos::gui::BuildStartMenuAppActionRows(
        registry.EnumerateAppActions(removedActionApp.manifest.id));
    const AppActionInfo removedActionSnapshot = removedActionRows[0].action;
    check(registry.UnregisterTemporaryDevelopmentApp(removedActionApp.manifest.id, 88, 1),
        "removed-action fixture unregisters after the menu snapshot is captured");
    removedActionApp.manifest.actions.clear();
    removedActionApp.temporaryGeneration = 2;
    check(registry.RegisterTemporaryDevelopmentApp(removedActionApp, error),
        "same canonical app re-registers without the displayed action");
    check(gxos::gui::BuildStartMenuAppActionRows(
        registry.EnumerateAppActions(removedActionApp.manifest.id)).empty() &&
        !dispatcher.Dispatch(registry, removedActionSnapshot, launched, error),
        "removed action disappears from the new menu and old displayed row fails closed");
    check(registry.UnregisterTemporaryDevelopmentApp(removedActionApp.manifest.id, 88, 2),
        "removed-action replacement unregisters cleanly");

    AppRegistry empty;
    check(empty.ResolveAppAction("test.missing", "open-home").status == AppActionResolutionStatus::UnknownApp,
        "empty registry fails unknown application actions without side effects");
    const int beforeValidCycles = dispatched;
    int validCycles = 0;
    bool validCyclesPassed = true;
    for (int i = 0; i < 100; ++i) {
        const AppActionResolution current = registry.ResolveAppAction(temporary.manifest.id, "open-home");
        validCyclesPassed = validCyclesPassed && current.invocable() && registry.IsAppActionCurrent(current.action) &&
            dispatcher.Dispatch(registry, current.action, launched, error);
        ++validCycles;
    }
    check(validCycles == 100 && validCyclesPassed && dispatched == beforeValidCycles + 100,
        "100 AppRegistry action resolution, revalidation, and generic invocation cycles reach the app handler");

    const int beforeInvalidCycles = dispatched;
    int invalidCycles = 0;
    bool invalidCyclesPassed = true;
    for (int i = 0; i < 100; ++i) {
        const AppActionResolution failure = (i % 2 == 0)
            ? registry.ResolveAppAction(temporary.manifest.id, "unknown-action")
            : registry.ResolveAppAction("missing.app", "open-home");
        invalidCyclesPassed = invalidCyclesPassed && !failure.invocable() &&
            !dispatcher.Dispatch(registry, staleSnapshot, launched, error);
        ++invalidCycles;
    }
    check(invalidCycles == 100 && invalidCyclesPassed && dispatched == beforeInvalidCycles,
        "100 stale and unknown-action dispatch cycles fail without application callback");

    std::cout << "Application action assertions: " << checks << "/" << checks - failures
              << " passed; failures=" << failures << "\n";
    return failures == 0 ? 0 : 1;
}
