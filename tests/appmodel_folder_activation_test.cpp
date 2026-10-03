#include "app_registry.h"
#include "app_manifest_validator.h"
#include "built_in_document_dispatcher.h"

#include <iostream>
#include <string>

namespace {
using namespace gxos::apps;

int checks = 0;
int failures = 0;
std::string dispatchedAppId;
std::string dispatchedFolderPath;

void check(bool condition, const std::string& name)
{
    ++checks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else { ++failures; std::cout << "FAIL: " << name << "\n"; }
}

RegisteredApp folderApp(const std::string& appId, const std::string& displayName, AppKind kind)
{
    RegisteredApp app;
    app.sourceKind = AppSourceKind::Package;
    app.folderActivationBackendAvailable = true;
    app.manifest.id = appId;
    app.manifest.displayName = displayName;
    app.manifest.version = "1.0.0";
    app.manifest.publisher = "Phase 16 test fixture";
    app.manifest.kind = kind;
    app.manifest.supportsFolderActivation = true;
    return app;
}

bool captureFolderActivation(const AppActivationContext& activation, std::string&)
{
    dispatchedAppId = activation.appId;
    dispatchedFolderPath = activation.folderPath;
    return true;
}

bool validManifestContract()
{
    AppManifest manifest;
    manifest.id = "test.folder.capability";
    manifest.displayName = "Folder capability";
    manifest.version = "1";
    manifest.kind = AppKind::Service;
    manifest.supportsFolderActivation = true;
    return AppManifestValidator::Validate(manifest).valid;
}
}

int main()
{
    AppRegistry registry;
    registry.RegisterBuiltInAppsAsManifests();
    check(validManifestContract(), "folder capability is an explicit manifest boolean and needs no fake extension");

    const RegisteredApp* fileExplorer = registry.FindById("gxos.builtin.fileexplorer");
    check(fileExplorer && fileExplorer->manifest.displayName == "File Explorer" &&
        fileExplorer->manifest.supportsFolderActivation && fileExplorer->folderActivationBackendAvailable,
        "canonical File Explorer registration truthfully owns the built-in folder capability");

    FolderHandlerList handlers = registry.EnumerateCapableFolderHandlers();
    check(handlers.count == 1 && handlers.declaredHandlerCount == 1 && handlers.availableHandlerCount == 1 &&
        !handlers.truncated && handlers.handlers[0].appId == "gxos.builtin.fileexplorer" &&
        handlers.handlers[0].isDefault && handlers.handlers[0].available,
        "folder capable-handler enumeration returns File Explorer as the available built-in default");

    std::string nested = "/projects/example.test/reports/photo.jpg";
    FolderActivationResolution root = registry.ResolveFolderActivation("/");
    FolderActivationResolution nestedResult = registry.ResolveFolderActivation(nested);
    check(root.launchable() && root.activation.kind == AppActivationKind::Folder &&
        root.activation.folderPath == "/" && root.activation.appId == "gxos.builtin.fileexplorer",
        "root activation has its own Folder kind and the canonical File Explorer identity");
    check(nestedResult.launchable() && nestedResult.activation.folderPath == nested &&
        nestedResult.activation.appId == "gxos.builtin.fileexplorer",
        "nested dotted and file-like folder names remain owned folder paths");
    nested = "/mutated/after-resolution";
    check(nestedResult.activation.folderPath == "/projects/example.test/reports/photo.jpg",
        "resolved folder path is value-owned by the activation context");

    check(registry.ResolveFolderActivation(std::string(kAppModelMaxFolderPathBytes, 'a')).launchable(),
        "folder path at the 4096-byte App Model limit is accepted by the bounded model");
    check(registry.ResolveFolderActivation("").status == FolderActivationResolutionStatus::InvalidPath &&
        registry.ResolveFolderActivation(std::string(kAppModelMaxFolderPathBytes + 1, 'a')).status == FolderActivationResolutionStatus::InvalidPath &&
        registry.ResolveFolderActivation(std::string("/bad\npath")).status == FolderActivationResolutionStatus::InvalidPath,
        "empty, control-containing, and overlong folder paths fail closed");
    check(registry.IsFolderActivationCurrent(root.activation), "current folder activation passes AppRegistry identity and capability revalidation");

    std::string error;
    RegisteredApp alternate = folderApp("test.folder.viewer", "Synthetic Folder Viewer", AppKind::BuiltIn);
    check(registry.RegisterTestDurableApp(alternate, error), "synthetic alternate folder handler registers without shipping a fake application");
    handlers = registry.EnumerateCapableFolderHandlers();
    check(handlers.count == 2 && handlers.availableHandlerCount == 2 &&
        handlers.handlers[0].appId == "gxos.builtin.fileexplorer" && handlers.handlers[0].isDefault &&
        handlers.handlers[1].appId == "test.folder.viewer" && !handlers.handlers[1].isDefault,
        "enumeration includes both handlers while the unconfigured built-in default remains File Explorer");

    FolderActivationResolution selectedAlternate = registry.ResolveFolderActivation(handlers.handlers[1], "/tmp/phase16 alternate");
    BuiltInActivationDispatcher dispatcher;
    check(dispatcher.RegisterHandler("test.folder.viewer", &captureFolderActivation),
        "generic activation dispatcher registers the synthetic canonical handler");
    error.clear();
    const bool alternateDispatched = selectedAlternate.launchable() &&
        dispatcher.Dispatch(registry, selectedAlternate.activation, error);
    check(alternateDispatched && dispatchedAppId == "test.folder.viewer" &&
        dispatchedFolderPath == "/tmp/phase16 alternate",
        "explicit one-time folder activation dispatches by AppRegistry-selected canonical ID");

    const AppActivationContext defaultActivation = registry.ResolveFolderActivation("/home/documents").activation;
    check(defaultActivation.appId == "gxos.builtin.fileexplorer" &&
        defaultActivation.kind == AppActivationKind::Folder,
        "ordinary folder activation continues using the built-in effective default");

    check(registry.UnregisterTemporaryDevelopmentApp("missing", 1, 1) == false,
        "unregistering an unrelated temporary identity does not alter folder handlers");
    check(!registry.ResolveFolderActivation(FolderHandlerInfo{}, "/tmp").launchable(),
        "an empty or unknown handler identity cannot redirect a folder activation");

    RegisteredApp temporary = folderApp("test.folder.temporary", "Temporary Folder Viewer", AppKind::BuiltIn);
    temporary.sourceKind = AppSourceKind::DevelopmentTemporary;
    temporary.temporaryDevelopment = true;
    temporary.temporaryOwnerRuntimeId = 10;
    temporary.temporaryGeneration = 20;
    check(registry.RegisterTemporaryDevelopmentApp(temporary, error), "temporary folder-capable handler registers with owner and generation");
    FolderHandlerList withTemporary = registry.EnumerateCapableFolderHandlers();
    FolderHandlerInfo temporaryIdentity;
    bool foundTemporary = false;
    for (size_t i = 0; i < withTemporary.count; ++i) {
        if (withTemporary.handlers[i].appId == "test.folder.temporary") {
            temporaryIdentity = withTemporary.handlers[i];
            foundTemporary = true;
        }
    }
    FolderActivationResolution temporaryActivation = registry.ResolveFolderActivation(temporaryIdentity, "/tmp/temporary");
    const AppActivationContext staleTemporaryActivation = temporaryActivation.activation;
    check(foundTemporary && temporaryActivation.launchable() && registry.IsFolderActivationCurrent(temporaryActivation.activation),
        "temporary handler resolves only while its enumerated registration generation is current");
    check(registry.UnregisterTemporaryDevelopmentApp("test.folder.temporary", 10, 20),
        "temporary folder handler unregisters using its matching owner and generation");
    temporary.temporaryOwnerRuntimeId = 11;
    temporary.temporaryGeneration = 21;
    check(registry.RegisterTemporaryDevelopmentApp(temporary, error), "replacement temporary handler reuses the ID with a new generation");
    check(registry.ResolveFolderActivation(temporaryIdentity, "/tmp/stale").status == FolderActivationResolutionStatus::HandlerStale &&
        !registry.IsFolderActivationCurrent(staleTemporaryActivation),
        "stale folder-handler identity and activation cannot redirect to a reused registration slot");
    check(dispatcher.RegisterHandler("test.folder.temporary", &captureFolderActivation),
        "generic dispatcher accepts the temporary canonical handler for lifecycle validation");
    error.clear();
    check(!dispatcher.Dispatch(registry, staleTemporaryActivation, error),
        "generic dispatcher revalidates the selected handler before dispatch");

    AppRegistry emptyRegistry;
    check(emptyRegistry.ResolveFolderActivation("/").status == FolderActivationResolutionStatus::NoHandler,
        "folder activation fails safely when no capable handler exists");

    int cycles = 0;
    bool cyclesPassed = true;
    for (int i = 0; i < 100; ++i) {
        const FolderActivationResolution activation = registry.ResolveFolderActivation("/stress/folder " + std::to_string(i));
        cyclesPassed = cyclesPassed && activation.launchable() && registry.IsFolderActivationCurrent(activation.activation);
        ++cycles;
    }
    check(cycles == 100 && cyclesPassed, "100 folder handler resolution and activation-context revalidation cycles pass");
    cycles = 0;
    bool invalidCyclesPassed = true;
    for (int i = 0; i < 100; ++i) {
        invalidCyclesPassed = invalidCyclesPassed &&
            registry.ResolveFolderActivation("/bad\tpath").status == FolderActivationResolutionStatus::InvalidPath &&
            emptyRegistry.ResolveFolderActivation("/no-handler").status == FolderActivationResolutionStatus::NoHandler &&
            registry.ResolveFolderActivation(temporaryIdentity, "/stale").status == FolderActivationResolutionStatus::HandlerStale;
        ++cycles;
    }
    check(cycles == 100 && invalidCyclesPassed,
        "100 invalid-path, no-handler, and stale-generation failure cycles pass without activation");

    std::cout << "Folder activation assertions: " << checks << "/" << checks - failures << " passed; failures=" << failures << "\n";
    return failures == 0 ? 0 : 1;
}
