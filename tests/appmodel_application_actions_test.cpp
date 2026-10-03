#include "app_manifest_loader.h"
#include "app_manifest_validator.h"
#include "app_registry.h"
#include "built_in_document_dispatcher.h"

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
    check(!production.EnumerateAppActions("missing.app").appFound,
        "unknown applications enumerate no actions");

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
    const AppActionInfo ownedSnapshot = actions.actions[0];
    actions.actions[0].label = "caller mutation";
    check(registry.IsAppActionCurrent(ownedSnapshot) && ownedSnapshot.label == "Duplicate",
        "enumeration values are owned and caller mutation cannot affect registry declarations");

    AppRegistry bounded;
    std::vector<AppActionDeclaration> maximumActions;
    for (size_t i = 0; i < kAppModelMaxActionsPerApp; ++i)
        maximumActions.push_back({ "item-" + std::to_string(i), "Item " + std::to_string(i) });
    RegisteredApp maximumApp = actionApp("test.phase17.maximum", maximumActions);
    check(registerDurable(bounded, maximumApp) &&
        bounded.EnumerateAppActions(maximumApp.manifest.id).count == kAppModelMaxActionsPerApp,
        "exact per-app action maximum registers and enumerates without truncation");
    maximumActions.push_back({ "overflow", "Overflow" });
    check(!registerDurable(bounded, actionApp("test.phase17.overflow", maximumActions)),
        "AppRegistry rejects per-app maximum plus one");

    AppRegistry backend;
    const RegisteredApp backendApp = actionApp("test.phase17.backend", { { "run", "Run" } }, false);
    check(registerDurable(backend, backendApp) &&
        backend.EnumerateAppActions("test.phase17.backend").count == 0 &&
        backend.ResolveAppAction("test.phase17.backend", "run").status == AppActionResolutionStatus::HandlerUnavailable,
        "declarations with no available application backend are not exposed as invocable actions");
    check(backend.SetTestAppActionBackend("test.phase17.backend", true) &&
        backend.EnumerateAppActions("test.phase17.backend").count == 1,
        "current backend availability reveals its statically declared action");
    const AppActionInfo backendSnapshot = backend.EnumerateAppActions("test.phase17.backend").actions[0];
    check(backend.SetTestAppActionBackend("test.phase17.backend", false) &&
        backend.SetTestAppActionBackend("test.phase17.backend", true) &&
        !backend.IsAppActionCurrent(backendSnapshot),
        "backend capability mutation changes registration generation and invalidates stale snapshots");

    BuiltInAppActionDispatcher dispatcher;
    check(dispatcher.RegisterHandler(synthetic.manifest.id, &captureAction),
        "generic action dispatcher registers one application-owned handler by canonical ID");
    std::string error;
    bool launched = false;
    dispatched = 0;
    check(dispatcher.Dispatch(registry, ownedSnapshot, launched, error) && dispatched == 1 &&
        dispatchedAppId == synthetic.manifest.id && dispatchedActionId == "alpha" && !launched,
        "generic dispatcher delivers only canonical app ID and declared action ID to the app handler");

    check(registry.ResolveAppAction("missing.app", "open-home").status == AppActionResolutionStatus::UnknownApp &&
        registry.ResolveAppAction(synthetic.manifest.id, "unknown").status == AppActionResolutionStatus::ActionNotDeclared &&
        registry.ResolveAppAction(synthetic.manifest.id, "bad_ID").status == AppActionResolutionStatus::InvalidActionId &&
        registry.ResolveAppAction(synthetic.manifest.id, "").status == AppActionResolutionStatus::InvalidActionId &&
        registry.ResolveAppAction(synthetic.manifest.id, std::string(kAppModelMaxActionIdBytes + 1, 'a')).status == AppActionResolutionStatus::InvalidActionId,
        "unknown application, unknown action, empty, malformed, and overlong identities fail closed");
    check(dispatched == 1,
        "failed resolutions do not reach the application action implementation");

    AppActionInfo staleSnapshot;
    RegisteredApp temporary = actionApp("test.phase17.reusable", { { "open-home", "Home" } });
    temporary.sourceKind = AppSourceKind::DevelopmentTemporary;
    temporary.temporaryDevelopment = true;
    temporary.temporaryOwnerRuntimeId = 77;
    temporary.temporaryGeneration = 1;
    check(registry.RegisterTemporaryDevelopmentApp(temporary, error),
        "temporary action registration enters the same bounded registry");
    staleSnapshot = registry.EnumerateAppActions(temporary.manifest.id).actions[0];
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
