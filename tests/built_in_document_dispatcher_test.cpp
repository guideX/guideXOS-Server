#include "app_registry.h"
#include "built_in_document_dispatcher.h"

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int checks = 0;
int failures = 0;
int dispatchCalls = 0;
gxos::apps::AppActivationContext receivedActivation;

void check(bool condition, const char* name) {
    ++checks;
    if (condition) {
        std::cout << "PASS: " << name << "\n";
    } else {
        ++failures;
        std::cout << "FAIL: " << name << "\n";
    }
}

bool captureActivation(const gxos::apps::AppActivationContext& activation, std::string&) {
    ++dispatchCalls;
    receivedActivation = activation;
    return true;
}

} // namespace

int main() {
    using namespace gxos::apps;

    const std::filesystem::path storePath = std::filesystem::temp_directory_path() /
        ("guidexos-built-in-dispatcher-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg");
    AppRegistry registry(false, storePath);
    registry.RegisterBuiltInAppsAsManifests({ "Notepad", "Image Viewer" });

    check(registry.GetFileAssociations().size() == 5,
        "built-in registry adds one PNG capability beside the four Notepad capabilities");

    const FileAssociationResolution lower = registry.ResolveFileAssociation("/images/nested/picture.png");
    const FileAssociationResolution upper = registry.ResolveFileAssociation("/images/nested/picture.PNG");
    const FileAssociationResolution mixed = registry.ResolveFileAssociation("/images/nested/picture.Png");
    check(lower.launchable() && upper.launchable() && mixed.launchable() &&
        lower.appId == "gxos.builtin.imageviewer" && upper.appId == lower.appId && mixed.appId == lower.appId,
        "PNG extension lookup is case-insensitive and returns the canonical ImageViewer ID");
    check(lower.activation.kind == AppActivationKind::Document &&
        lower.activation.documentPath == "/images/nested/picture.png" &&
        lower.activation.registrationOwner == 0 && lower.activation.registrationGeneration == 0,
        "resolved built-in activation owns the exact nested VFS path and built-in lifecycle identity");

    const DocumentHandlerList handlers = registry.EnumerateCapableHandlers(".PNG");
    const DefaultHandlerInfo defaults = registry.GetDefaultHandlerInfo(".Png");
    check(handlers.validExtension && handlers.count == 1 && handlers.handlers[0].available &&
        handlers.handlers[0].isDefault && handlers.handlers[0].appId == "gxos.builtin.imageviewer",
        "capable-handler enumeration discovers ImageViewer as the only effective PNG handler");
    check(defaults.builtInDefaultAppId == "gxos.builtin.imageviewer" &&
        defaults.configuredOverrideAppId.empty() && defaults.effectiveDefaultAppId == "gxos.builtin.imageviewer" &&
        defaults.effectiveDefaultAvailable,
        "PNG policy reports ImageViewer as built-in/effective default without a configured override");
    check(registry.ResolveFileAssociation("/images/picture.xyz").status == FileAssociationResolutionStatus::NoAssociation &&
        registry.ResolveFileAssociation("/images/picture.image").status == FileAssociationResolutionStatus::NoAssociation,
        "unregistered image-like extensions remain unassociated");

    BuiltInDocumentDispatcher dispatcher;
    check(dispatcher.RegisterHandler("gxos.builtin.imageviewer", &captureActivation) &&
        !dispatcher.RegisterHandler("gxos.builtin.imageviewer", &captureActivation),
        "generic built-in dispatcher accepts a canonical handler once and rejects duplicate registration");

    std::string callerPath = "/images/nested/exact path.PnG";
    FileAssociationResolution ownedResolution = registry.ResolveFileAssociation(callerPath);
    callerPath.assign("caller storage overwritten");
    std::string error;
    check(ownedResolution.launchable() && dispatcher.Dispatch(registry, ownedResolution.activation, error) &&
        receivedActivation.appId == "gxos.builtin.imageviewer" &&
        receivedActivation.documentPath == "/images/nested/exact path.PnG" &&
        receivedActivation.registrationOwner == ownedResolution.activation.registrationOwner &&
        receivedActivation.registrationGeneration == ownedResolution.activation.registrationGeneration,
        "generic dispatcher delivers the owned exact path and target lifecycle metadata");

    bool lifecycleCyclesSucceeded = true;
    for (size_t i = 0; i < 100; ++i) {
        const std::string path = "/images/nested/cycle-" + std::to_string(i) + ".png";
        const FileAssociationResolution cycle = registry.ResolveFileAssociation(path);
        if (!cycle.launchable() || !dispatcher.Dispatch(registry, cycle.activation, error) ||
            receivedActivation.documentPath != path) {
            lifecycleCyclesSucceeded = false;
            break;
        }
    }
    check(lifecycleCyclesSucceeded && dispatchCalls == 101,
        "100 model/dispatcher activation cycles deliver distinct owned paths");

    AppActivationContext unsupportedKind = ownedResolution.activation;
    unsupportedKind.kind = AppActivationKind::Application;
    check(!dispatcher.Dispatch(registry, unsupportedKind, error) &&
        error == "Built-in document dispatcher rejects non-document activation",
        "generic dispatcher rejects unsupported activation kinds");

    AppActivationContext stale = ownedResolution.activation;
    ++stale.registrationGeneration;
    check(!dispatcher.Dispatch(registry, stale, error) &&
        error == "Built-in document activation target is stale or unavailable",
        "generic dispatcher rejects a stale registration target");

    AppRegistry missingRegistry(false, storePath.string() + ".missing");
    missingRegistry.RegisterBuiltInAppsAsManifests({ "Notepad" });
    check(!dispatcher.Dispatch(missingRegistry, ownedResolution.activation, error) &&
        error == "Built-in document activation target application is missing",
        "generic dispatcher rejects a target application missing from AppRegistry");

    AppActivationContext overlong = ownedResolution.activation;
    overlong.documentPath.assign(kAppModelMaxDocumentPathBytes + 1, 'x');
    check(!dispatcher.Dispatch(registry, overlong, error) &&
        error == "Built-in document activation target is invalid or overlong",
        "generic dispatcher rejects a document path beyond the shared 4096-byte bound");

    BuiltInDocumentDispatcher noHandler;
    check(!noHandler.Dispatch(registry, ownedResolution.activation, error) &&
        error == "No built-in document activation dispatcher is registered for this application",
        "generic dispatcher fails closed when a current built-in has no launch adapter");

    std::error_code ignored;
    std::filesystem::remove(storePath, ignored);
    std::filesystem::remove(storePath.string() + ".missing", ignored);
    std::cout << "builtInDocumentDispatcherChecks=" << (checks - failures) << "/" << checks << "\n";
    std::cout << "builtInDocumentDispatcherLifecycleCycles=" << (dispatchCalls - 1) << "/100\n";
    return failures == 0 ? 0 : 1;
}
