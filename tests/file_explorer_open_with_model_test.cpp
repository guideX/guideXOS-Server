#include "app_registry.h"
#include "file_explorer_open_with_model.h"

#include <iostream>
#include <string>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else { ++failures; std::cout << "FAIL: " << name << "\n"; }
}

gxos::apps::RegisteredApp temporaryApp(const std::string& id,
                                       const std::string& name,
                                       uint64_t owner,
                                       uint64_t generation,
                                       bool available = true) {
    using namespace gxos::apps;
    RegisteredApp app;
    app.sourceKind = AppSourceKind::DevelopmentTemporary;
    app.temporaryDevelopment = true;
    app.temporaryOwnerRuntimeId = owner;
    app.temporaryGeneration = generation;
    app.documentActivationBackendAvailable = available;
    app.manifest.id = id;
    app.manifest.displayName = name;
    app.manifest.version = "1.0.0";
    app.manifest.kind = AppKind::Service;
    app.manifest.supportsDocumentActivation = true;
    app.manifest.fileAssociations.push_back({ ".menu", "text/plain", "menu fixture" });
    return app;
}

bool registerApp(gxos::apps::AppRegistry& registry, const gxos::apps::RegisteredApp& app) {
    std::string error;
    const bool ok = registry.RegisterTemporaryDevelopmentApp(app, error);
    if (!ok) std::cout << "registration error: " << error << "\n";
    return ok;
}
}

int main() {
    using namespace gxos::apps;

    AppRegistry emptyRegistry;
    DocumentHandlerList empty = emptyRegistry.EnumerateCapableHandlers(".menu");
    check(!BuildFileExplorerOpenWithMenu(empty, false, "/docs/file.menu").visible,
        "unknown or unassociated file has no Open With menu");

    AppRegistry imageViewerRegistry;
    imageViewerRegistry.RegisterBuiltInAppsAsManifests({ "Image Viewer" });
    check(BuildFileExplorerOpenWithMenu(imageViewerRegistry.EnumerateCapableHandlers(".png"), false,
            "/images/supported.png").visible &&
        !BuildFileExplorerOpenWithMenu(imageViewerRegistry.EnumerateCapableHandlers(".bmp"), false,
            "/images/unsupported.bmp").visible &&
        !BuildFileExplorerOpenWithMenu(imageViewerRegistry.EnumerateCapableHandlers(".gif"), false,
            "/images/unsupported.gif").visible,
        "Open With offers ImageViewer for supported PNG but no fabricated BMP or GIF choice");

    AppRegistry oneRegistry;
    check(registerApp(oneRegistry, temporaryApp("app.test.notepad", "Notepad", 31, 1)),
        "one-handler fixture registers");
    const DocumentHandlerList one = oneRegistry.EnumerateCapableHandlers(".MENU");
    FileExplorerOpenWithMenuSnapshot oneMenu = BuildFileExplorerOpenWithMenu(one, false, "/docs/nested/ReadMe.MENU");
    check(oneMenu.visible && oneMenu.count == 1 && oneMenu.items[0].label == "Notepad",
        "one capable handler appears as a single Open With choice");
    DocumentHandlerInfo selected;
    std::string selectedPath;
    check(oneMenu.Select(0, selected, selectedPath) && selected.appId == "app.test.notepad" &&
        selectedPath == "/docs/nested/ReadMe.MENU",
        "menu selection returns canonical identity and an owned exact document path");
    check(!BuildFileExplorerOpenWithMenu(one, true, "/docs/folder.menu").visible,
        "directory entries never create document Open With choices");
    check(!BuildFileExplorerOpenWithMenu(one, false, "").visible,
        "empty targets fail closed when building the menu snapshot");

    AppRegistry duplicateRegistry;
    check(registerApp(duplicateRegistry, temporaryApp("app.test.alpha", "Editor", 32, 1)) &&
        registerApp(duplicateRegistry, temporaryApp("app.test.beta", "Editor", 33, 1)),
        "duplicate-label menu fixtures register");
    const DocumentHandlerList duplicateHandlers = duplicateRegistry.EnumerateCapableHandlers(".menu");
    FileExplorerOpenWithMenuSnapshot duplicateMenu = BuildFileExplorerOpenWithMenu(
        duplicateHandlers, false, "C:\\docs\\duplicate.menu");
    check(duplicateMenu.visible && duplicateMenu.count == 2 &&
        duplicateMenu.items[0].label == "Editor (1)" && duplicateMenu.items[1].label == "Editor (2)" &&
        duplicateMenu.items[0].handler.appId == "app.test.alpha" &&
        duplicateMenu.items[1].handler.appId == "app.test.beta",
        "duplicate display names get distinct presentation rows while canonical IDs remain separate");
    check(duplicateMenu.Select(1, selected, selectedPath) && selected.appId == "app.test.beta" &&
        duplicateRegistry.ResolveDocumentActivation(selected, selectedPath).appId == "app.test.beta",
        "choosing the second identically named row dispatches its canonical application");

    AppRegistry longNameRegistry;
    const std::string longName(120, 'L');
    check(registerApp(longNameRegistry, temporaryApp("app.test.long", longName, 34, 1)),
        "long-label menu fixture registers");
    const FileExplorerOpenWithMenuSnapshot longMenu = BuildFileExplorerOpenWithMenu(
        longNameRegistry.EnumerateCapableHandlers(".menu"), false, "/docs/long.menu");
    check(longMenu.count == 1 && longMenu.items[0].label.size() <= kFileExplorerOpenWithLabelMaxChars &&
        longMenu.items[0].handler.appId == "app.test.long",
        "long display labels are bounded independently from canonical IDs");

    AppRegistry unavailableRegistry;
    check(registerApp(unavailableRegistry, temporaryApp("app.test.available", "Available", 35, 1, true)) &&
        registerApp(unavailableRegistry, temporaryApp("app.test.offline", "Offline", 36, 1, false)),
        "available and unavailable menu fixtures register");
    const DocumentHandlerList mixed = unavailableRegistry.EnumerateCapableHandlers(".menu");
    const FileExplorerOpenWithMenuSnapshot mixedMenu = BuildFileExplorerOpenWithMenu(mixed, false, "/docs/mixed.menu");
    check(mixedMenu.count == 1 && mixedMenu.items[0].handler.appId == "app.test.available",
        "backend-unavailable handlers are not offered as usable menu choices");

    FileExplorerOpenWithMenuSnapshot staleMenu = BuildFileExplorerOpenWithMenu(one, false, "/docs/stale.menu");
    check(oneRegistry.UnregisterTemporaryDevelopmentApp("app.test.notepad", 31, 1) &&
        registerApp(oneRegistry, temporaryApp("app.test.notepad", "Notepad Reused", 31, 2)),
        "old menu registration can be removed and its canonical ID re-registered");
    staleMenu.Select(0, selected, selectedPath);
    check(oneRegistry.ResolveDocumentActivation(selected, selectedPath).status == FileAssociationResolutionStatus::HandlerStale,
        "old Open With menu snapshot rejects same-ID registration generation reuse");

    bool lifecycleOk = true;
    for (size_t i = 0; i < 100; ++i) {
        FileExplorerOpenWithMenuSnapshot cycle = BuildFileExplorerOpenWithMenu(
            duplicateRegistry.EnumerateCapableHandlers(".menu"), false,
            "/docs/cycle-" + std::to_string(i) + ".menu");
        DocumentHandlerInfo cycleHandler;
        std::string cyclePath;
        if (!cycle.Select(1, cycleHandler, cyclePath) || cycleHandler.appId != "app.test.beta" ||
            cyclePath != "/docs/cycle-" + std::to_string(i) + ".menu") lifecycleOk = false;
        cycle.Reset();
        if (cycle.visible || cycle.count != 0 || !cycle.targetPath.empty()) lifecycleOk = false;
    }
    check(lifecycleOk, "100 menu create, select, and destroy cycles retain no stale target or identity");

    std::cout << "fileExplorerMenuChecks=" << (checks - failures) << "/" << checks << "\n";
    std::cout << "menuLifecycleCycles=100/100\n";
    return failures == 0 ? 0 : 1;
}
