#include "app_manifest_loader.h"
#include "app_manifest_validator.h"
#include "app_registry.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <utility>

namespace {

int failures = 0;
int associationChecks = 0;
int ownershipChecks = 0;
int associationFailures = 0;
int ownershipFailures = 0;

void check(bool condition, const char* group, const char* name) {
    if (std::string(group) == "association") ++associationChecks;
    else ++ownershipChecks;
    if (condition) {
        std::cout << "PASS: " << name << "\n";
        return;
    }
    std::cout << "FAIL: " << name << "\n";
    ++failures;
    if (std::string(group) == "association") ++associationFailures;
    else ++ownershipFailures;
}

gxos::apps::RegisteredApp temporaryApp(const std::string& id,
                                       const std::string& displayName,
                                       uint64_t owner,
                                       uint64_t generation,
                                       std::string extension,
                                       bool supportsDocuments = true,
                                       bool backendAvailable = true) {
    gxos::apps::RegisteredApp app;
    app.sourceKind = gxos::apps::AppSourceKind::DevelopmentTemporary;
    app.temporaryDevelopment = true;
    app.temporaryOwnerRuntimeId = owner;
    app.temporaryGeneration = generation;
    app.documentActivationBackendAvailable = backendAvailable;
    app.manifest.schemaVersion = gxos::apps::kSupportedAppManifestSchemaVersion;
    app.manifest.id = id;
    app.manifest.displayName = displayName;
    app.manifest.version = "1.0.0";
    app.manifest.kind = gxos::apps::AppKind::Service;
    app.manifest.supportsDocumentActivation = supportsDocuments;
    if (!extension.empty()) app.manifest.fileAssociations.push_back({ std::move(extension), "text/plain", "test document" });
    return app;
}

bool registerApp(gxos::apps::AppRegistry& registry, gxos::apps::RegisteredApp app) {
    std::string error;
    const bool result = registry.RegisterTemporaryDevelopmentApp(app, error);
    if (!result) std::cout << "registration error: " << error << "\n";
    return result;
}

} // namespace

int main() {
    using namespace gxos::apps;

    AppRegistry builtIns;
    builtIns.RegisterBuiltInAppsAsManifests({ "Notepad" });
    const AppManifestLoadResult manifestCapability = AppManifestLoader::LoadFromString(
        "{\"schemaVersion\":1,\"id\":\"com.guidexos.tests.loader\",\"displayName\":\"Loader Test\","
        "\"version\":\"1.0.0\",\"kind\":\"service\",\"supportsDocumentActivation\":true,"
        "\"fileAssociations\":[{\"extension\":\".md\",\"contentType\":\"text/markdown\",\"description\":\"Markdown\"}]}");
    check(manifestCapability.valid && manifestCapability.manifest.supportsDocumentActivation &&
        manifestCapability.manifest.fileAssociations.size() == 1 &&
        manifestCapability.manifest.fileAssociations.front().extension == ".md",
        "association", "manifest loader retains explicit activation capability and extension declarations");
    check(builtIns.GetFileAssociations().size() == 4, "association", "built-in Notepad publishes exactly four bounded associations");
    check(builtIns.ResolveFileAssociation("C:\\docs\\archive.tar.TXT").launchable() &&
        builtIns.ResolveFileAssociation("C:\\docs\\archive.tar.TXT").extension == ".txt",
        "association", "extension matching is ASCII case-insensitive and uses the final suffix");
    check(builtIns.ResolveFileAssociation("/docs/settings.INI").appId == "gxos.builtin.notepad",
        "association", "slash paths resolve through the canonical built-in App ID");
    check(builtIns.ResolveFileAssociation("C:\\docs\\settings.cfg").launchable(),
        "association", "backslash paths resolve through the AppRegistry index");
    const std::string traversalPath = "C:\\docs\\..\\safe.txt";
    const FileAssociationResolution traversalResolution = builtIns.ResolveFileAssociation(traversalPath);
    check(traversalResolution.launchable() && traversalResolution.activation.documentPath == traversalPath,
        "ownership", "existing path traversal spelling is preserved exactly for the VFS to interpret");
    check(builtIns.ResolveFileAssociation("readme.md").status == FileAssociationResolutionStatus::NoAssociation,
        "association", "unknown extension does not acquire an incidental handler");
    check(builtIns.ResolveFileAssociation("README").status == FileAssociationResolutionStatus::NoExtension,
        "association", "extensionless names remain unresolved");
    check(builtIns.ResolveFileAssociation(".txt").status == FileAssociationResolutionStatus::NoExtension,
        "association", "dotfiles without a suffix remain extensionless");
    check(builtIns.ResolveFileAssociation("trailing.").status == FileAssociationResolutionStatus::InvalidExtension,
        "association", "trailing dot is rejected as a malformed extension");
    check(builtIns.ResolveFileAssociation("bad name.txt").launchable(),
        "association", "spaces in the filename do not prevent a registered final suffix from resolving");
    check(builtIns.ResolveFileAssociation("name." + std::string(kAppModelMaxFileExtensionBytes, 'x')).status == FileAssociationResolutionStatus::InvalidExtension,
        "association", "overlong suffix is rejected");
    check(builtIns.ResolveFileAssociation("").status == FileAssociationResolutionStatus::InvalidPath,
        "association", "empty paths are rejected before extension lookup");
    const std::string maximumExtension = "." + std::string(kAppModelMaxFileExtensionBytes - 1, 'x');
    AppManifest maximumExtensionManifest = temporaryApp("com.guidexos.tests.maxextension", "Maximum Extension", 1, 1, maximumExtension).manifest;
    check(maximumExtension.size() == kAppModelMaxFileExtensionBytes && AppManifestValidator::Validate(maximumExtensionManifest).valid,
        "association", "extension exactly at the configured byte limit is accepted");
    check(builtIns.ResolveFileAssociation(std::string(kAppModelMaxDocumentPathBytes + 1, 'x') + ".txt").status == FileAssociationResolutionStatus::InvalidPath,
        "association", "path longer than the configured bound is rejected before routing");
    check(builtIns.ResolveFileAssociation(std::string("bad\nname.txt")).status == FileAssociationResolutionStatus::InvalidPath,
        "association", "control characters are rejected from document paths");

    AppManifest associationManifest = temporaryApp("com.guidexos.tests.extension", "Extension Test", 1, 1, ".doc_1-x").manifest;
    check(AppManifestValidator::Validate(associationManifest).valid,
        "association", "documented extension alphabet accepts ASCII letters, digits, underscore, and hyphen");
    associationManifest.fileAssociations.front().extension = ".bad.suffix";
    check(!AppManifestValidator::Validate(associationManifest).valid,
        "association", "manifest validator rejects multi-dot association declarations");
    associationManifest.fileAssociations.front().extension = ".txt";
    associationManifest.fileAssociations.clear();
    for (size_t i = 0; i < kAppModelMaxFileAssociationsPerApp; ++i) {
        char extension[16];
        std::snprintf(extension, sizeof(extension), ".a%03u", static_cast<unsigned>(i));
        associationManifest.fileAssociations.push_back({ extension, "", "" });
    }
    check(AppManifestValidator::Validate(associationManifest).valid,
        "association", "per-app association count accepts its configured maximum");
    associationManifest.fileAssociations.push_back({ ".extra", "", "" });
    check(!AppManifestValidator::Validate(associationManifest).valid,
        "association", "per-app association count rejects one over the configured maximum");

    AppRegistry duplicateRegistry;
    check(registerApp(duplicateRegistry, temporaryApp("com.guidexos.tests.duplicatea", "Duplicate A", 10, 1, ".DUP")) &&
        registerApp(duplicateRegistry, temporaryApp("com.guidexos.tests.duplicateb", "Duplicate B", 11, 1, ".dup")),
        "association", "duplicate association fixtures register with case normalization");
    check(duplicateRegistry.ResolveFileAssociation("item.DuP").status == FileAssociationResolutionStatus::Ambiguous,
        "association", "duplicate declarations become ambiguous without ranking by ID or source order");
    AppRegistry unsupportedRegistry;
    check(registerApp(unsupportedRegistry, temporaryApp("com.guidexos.tests.nodoc", "No Document Capability", 12, 1, ".nodoc", false, true)) &&
        unsupportedRegistry.ResolveFileAssociation("item.nodoc").status == FileAssociationResolutionStatus::HandlerDoesNotSupportDocuments,
        "association", "handler without declared document capability is not launchable");
    AppRegistry unavailableRegistry;
    check(registerApp(unavailableRegistry, temporaryApp("com.guidexos.tests.nobackend", "No Backend", 13, 1, ".noback", true, false)) &&
        unavailableRegistry.ResolveFileAssociation("item.noback").status == FileAssociationResolutionStatus::HandlerUnavailable,
        "association", "handler without a current runtime backend is not launchable");

    AppRegistry capacityRegistry;
    bool capacityRegistrationsSucceeded = true;
    for (size_t appIndex = 0; appIndex < kAppModelMaxFileAssociationRecords / kAppModelMaxFileAssociationsPerApp; ++appIndex) {
        RegisteredApp app = temporaryApp("com.guidexos.capacity.app" + std::to_string(appIndex),
            "Association Capacity " + std::to_string(appIndex), 90 + appIndex, appIndex + 1, "", true, true);
        for (size_t extensionIndex = 0; extensionIndex < kAppModelMaxFileAssociationsPerApp; ++extensionIndex) {
            const size_t number = appIndex * kAppModelMaxFileAssociationsPerApp + extensionIndex;
            char suffix[16];
            std::snprintf(suffix, sizeof(suffix), ".e%03u", static_cast<unsigned>(number));
            app.manifest.fileAssociations.push_back({ suffix, "application/test", "capacity fixture" });
        }
        if (!registerApp(capacityRegistry, std::move(app))) capacityRegistrationsSucceeded = false;
    }
    check(capacityRegistrationsSucceeded && capacityRegistry.GetFileAssociations().size() == kAppModelMaxFileAssociationRecords,
        "association", "association index accepts exactly its configured record capacity");
    RegisteredApp overflowApp = temporaryApp("com.guidexos.capacity.overflow", "Overflow", 999, 999, ".e256");
    check(registerApp(capacityRegistry, std::move(overflowApp)) && capacityRegistry.FileAssociationCapacityExceeded() &&
        capacityRegistry.GetFileAssociations().size() == kAppModelMaxFileAssociationRecords,
        "association", "association index caps retained records and reports overflow");
    check(capacityRegistry.ResolveFileAssociation("overflow.e256").status == FileAssociationResolutionStatus::RegistryCapacityExceeded,
        "association", "extension omitted by capacity cap fails closed with explicit status");

    AppRegistry ownershipRegistry;
    RegisteredApp first = temporaryApp("com.guidexos.tests.document", "Document Handler One", 77, 1, ".owned");
    check(registerApp(ownershipRegistry, first), "ownership", "owned activation handler registers");
    std::string callerPath = "C:\\users\\default\\notes.owned";
    FileAssociationResolution resolved = ownershipRegistry.ResolveFileAssociation(callerPath);
    check(resolved.launchable() && resolved.activation.kind == AppActivationKind::Document &&
        resolved.activation.appId == first.manifest.id && resolved.activation.documentPath == callerPath &&
        resolved.activation.registrationOwner == 77 && resolved.activation.registrationGeneration == 1,
        "ownership", "resolved target owns the exact path, canonical ID, owner, and generation");
    callerPath.assign("caller buffer changed after resolution");
    check(resolved.activation.documentPath == "C:\\users\\default\\notes.owned" &&
        ownershipRegistry.IsDocumentActivationCurrent(resolved.activation),
        "ownership", "activation path is an owned copy and validates while its registration is current");
    FileAssociationResolution destroyedSourceResolution;
    {
        std::string temporarySource = "C:\\users\\default\\temporary.owned";
        destroyedSourceResolution = ownershipRegistry.ResolveFileAssociation(temporarySource);
    }
    check(destroyedSourceResolution.launchable() &&
        destroyedSourceResolution.activation.documentPath == "C:\\users\\default\\temporary.owned",
        "ownership", "activation remains intact after its temporary source string is destroyed");
    check(!ownershipRegistry.UnregisterTemporaryDevelopmentApp(first.manifest.id, 78, 1) &&
        !ownershipRegistry.UnregisterTemporaryDevelopmentApp(first.manifest.id, 77, 2),
        "ownership", "wrong owner or generation cannot remove the association handler");
    check(ownershipRegistry.UnregisterTemporaryDevelopmentApp(first.manifest.id, 77, 1),
        "ownership", "matching owner and generation can unregister the handler");
    check(ownershipRegistry.ResolveFileAssociation("C:\\users\\default\\notes.owned").status == FileAssociationResolutionStatus::NoAssociation,
        "ownership", "unregister removes the association instead of leaving a dangling handler ID");
    RegisteredApp second = temporaryApp("com.guidexos.tests.document", "Document Handler Two", 77, 2, ".owned");
    check(registerApp(ownershipRegistry, second), "ownership", "same App ID can be reused with a fresh generation");
    check(!ownershipRegistry.IsDocumentActivationCurrent(resolved.activation),
        "ownership", "old owned activation cannot survive unregister and same-ID generation reuse");
    FileAssociationResolution fresh = ownershipRegistry.ResolveFileAssociation("C:\\users\\default\\notes.owned");
    check(fresh.launchable() && fresh.activation.registrationGeneration == 2 &&
        fresh.displayName == "Document Handler Two",
        "ownership", "fresh resolution selects only the current registration generation");

    std::string maxPath = std::string(kAppModelMaxDocumentPathBytes - 4, 'p') + ".txt";
    check(maxPath.size() == kAppModelMaxDocumentPathBytes && IsValidDocumentActivationPath(maxPath),
        "ownership", "maximum-length document path is accepted at the value boundary");
    check(builtIns.ResolveFileAssociation(maxPath).launchable(),
        "ownership", "maximum-length associated path resolves without truncation");
    maxPath.push_back('x');
    check(!IsValidDocumentActivationPath(maxPath),
        "ownership", "one byte over the document path bound is rejected");
    AppActivationContext malformed;
    malformed.kind = AppActivationKind::Document;
    malformed.appId = "com.guidexos.tests.document";
    malformed.documentPath = "bad\rpath.owned";
    check(!ownershipRegistry.IsDocumentActivationCurrent(malformed),
        "ownership", "malformed document activation context is never current");

    std::cout << "associationRegistryChecks=" << (associationChecks - associationFailures) << "/" << associationChecks << "\n";
    std::cout << "documentTargetOwnershipChecks=" << (ownershipChecks - ownershipFailures) << "/" << ownershipChecks << "\n";
    std::cout << "appModelFileActivationChecks=" << (associationChecks + ownershipChecks - failures)
        << "/" << (associationChecks + ownershipChecks) << "\n";
    return failures == 0 ? 0 : 1;
}
