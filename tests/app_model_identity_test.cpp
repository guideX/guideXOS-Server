#include "app_manifest_loader.h"
#include "app_manifest_validator.h"
#include "app_registry.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (condition) {
        std::cout << "PASS: " << name << "\n";
        return;
    }
    std::cout << "FAIL: " << name << "\n";
    ++failures;
}

gxos::apps::RegisteredApp temporaryApp(const std::string& id,
                                       const std::string& displayName,
                                       uint64_t owner,
                                       uint64_t generation) {
    gxos::apps::RegisteredApp app;
    app.sourceKind = gxos::apps::AppSourceKind::DevelopmentTemporary;
    app.temporaryDevelopment = true;
    app.temporaryOwnerRuntimeId = owner;
    app.temporaryGeneration = generation;
    app.manifest.schemaVersion = gxos::apps::kSupportedAppManifestSchemaVersion;
    app.manifest.id = id;
    app.manifest.displayName = displayName;
    app.manifest.version = "1.0.0";
    app.manifest.kind = gxos::apps::AppKind::Service;
    return app;
}

bool writeManifest(const std::filesystem::path& path,
                   const std::string& id,
                   const std::string& displayName) {
    std::ofstream file(path, std::ios::binary);
    file << "{\r\n\"schemaVersion\":1,\r\n\"id\":\"" << id
         << "\",\r\n\"displayName\":\"" << displayName
         << "\",\r\n\"version\":\"1.0.0\",\r\n\"kind\":\"service\"\r\n}";
    return file.good();
}

} // namespace

int main() {
    using namespace gxos::apps;

    const std::string validJson =
        "{\"schemaVersion\":1,\"id\":\"test.identity\",\"displayName\":\"Identity Test\","
        "\"version\":\"1.0.0\",\"kind\":\"service\"}";
    check(AppManifestLoader::LoadFromString(validJson).valid, "valid manifest loads");
    check(!AppManifestLoader::LoadFromString(
        std::string(kAppModelMaxManifestBytes + 1, ' ')).valid,
        "over-limit manifest text is rejected");
    AppManifest pathBound = temporaryApp("com.guidexos.tests.pathbound", "Path Bound", 1, 1).manifest;
    AppEntry pathEntry;
    pathEntry.path.assign(kAppModelMaxEntryPathBytes, 'p');
    pathBound.entries.push_back(pathEntry);
    check(AppManifestValidator::Validate(pathBound).valid,
        "maximum-length manifest entry path is accepted");
    pathBound.entries.front().path.push_back('p');
    check(!AppManifestValidator::Validate(pathBound).valid,
        "overlength manifest entry path is rejected");
    AppManifest entryBound = temporaryApp("com.guidexos.tests.entrybound", "Entry Bound", 1, 1).manifest;
    entryBound.entries.resize(kAppModelMaxEntriesPerManifest);
    check(AppManifestValidator::Validate(entryBound).valid,
        "maximum manifest entry count is accepted");
    entryBound.entries.push_back(AppEntry{});
    check(!AppManifestValidator::Validate(entryBound).valid,
        "over-capacity manifest entry count is rejected");

    AppRegistry registry;
    RegisteredApp source = temporaryApp("com.guidexos.tests.copy", "Copied Identity", 41, 1);
    std::string error;
    check(registry.RegisterTemporaryDevelopmentApp(source, error), "temporary registration accepted");
    source.manifest.id = "com.guidexos.tests.mutated";
    source.manifest.displayName = "Mutated Source";
    const RegisteredApp* copied = registry.FindById("com.guidexos.tests.copy");
    check(copied && copied->manifest.displayName == "Copied Identity",
        "registry owns a copy of registration identity strings");

    RegisteredApp duplicate = temporaryApp("com.guidexos.tests.copy", "Different App", 99, 2);
    error.clear();
    check(!registry.RegisterTemporaryDevelopmentApp(duplicate, error) &&
        error == "APPLICATION_ID_IN_USE",
        "exact duplicate ID is rejected with a deterministic error");
    copied = registry.FindById("com.guidexos.tests.copy");
    check(copied && copied->manifest.displayName == "Copied Identity",
        "duplicate rejection preserves the existing registration");

    const std::string maxId(kAppModelMaxAppIdBytes, 'i');
    RegisteredApp maxIdApp = temporaryApp(maxId, "Max ID", 41, 2);
    check(registry.RegisterTemporaryDevelopmentApp(maxIdApp, error),
        "maximum-length App Model ID is accepted");
    RegisteredApp longIdApp = temporaryApp(maxId + "x", "Long ID", 41, 3);
    error.clear();
    check(!registry.RegisterTemporaryDevelopmentApp(longIdApp, error),
        "overlength App Model ID is rejected");

    RegisteredApp upper = temporaryApp("com.guidexos.tests.Case", "Upper Case ID", 41, 4);
    RegisteredApp lower = temporaryApp("com.guidexos.tests.case", "Lower Case ID", 41, 5);
    check(registry.RegisterTemporaryDevelopmentApp(upper, error) &&
        registry.RegisterTemporaryDevelopmentApp(lower, error),
        "ID matching preserves the existing case-sensitive contract");

    RegisteredApp generationOne = temporaryApp("com.guidexos.tests.generation", "Generation One", 77, 1);
    check(registry.RegisterTemporaryDevelopmentApp(generationOne, error),
        "generation test registration accepted");
    check(!registry.UnregisterTemporaryDevelopmentApp(generationOne.manifest.id, 77, 2),
        "wrong generation cannot unregister a live registration");
    check(registry.UnregisterTemporaryDevelopmentApp(generationOne.manifest.id, 77, 1),
        "matching owner and generation unregister the registration");
    RegisteredApp generationTwo = temporaryApp("com.guidexos.tests.generation", "Generation Two", 77, 2);
    check(registry.RegisterTemporaryDevelopmentApp(generationTwo, error),
        "same ID can be registered again under a new generation");
    check(!registry.UnregisterTemporaryDevelopmentApp(generationTwo.manifest.id, 77, 1),
        "stale generation cannot remove the reused ID");
    const RegisteredApp* currentGeneration = registry.FindById(generationTwo.manifest.id);
    check(currentGeneration && currentGeneration->temporaryGeneration == 2 &&
        currentGeneration->manifest.displayName == "Generation Two",
        "reused ID still resolves only to the current generation");

    AppRegistry capacityRegistry;
    bool filled = true;
    for (size_t i = 0; i < kAppModelMaxRegistryApps; ++i) {
        RegisteredApp app = temporaryApp("com.guidexos.capacity." + std::to_string(i),
            "Capacity " + std::to_string(i), 8, i + 1);
        if (!capacityRegistry.RegisterTemporaryDevelopmentApp(app, error)) {
            filled = false;
            break;
        }
    }
    check(filled && capacityRegistry.GetAllApps().size() == kAppModelMaxRegistryApps,
        "registry accepts exactly its configured capacity");
    RegisteredApp overflow = temporaryApp("com.guidexos.capacity.overflow", "Overflow", 8, 600);
    error.clear();
    check(!capacityRegistry.RegisterTemporaryDevelopmentApp(overflow, error) &&
        error == "APP_REGISTRY_FULL",
        "registry capacity overflow fails without replacing an entry");

    const auto root = std::filesystem::temp_directory_path() /
        ("gxos-app-model-identity-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto firstPath = root / "a" / "app.json";
    const auto secondPath = root / "z" / "app.json";
    std::filesystem::create_directories(firstPath.parent_path());
    std::filesystem::create_directories(secondPath.parent_path());
    const bool manifestsWritten =
        writeManifest(firstPath, "com.guidexos.tests.duplicate", "Lexically First") &&
        writeManifest(secondPath, "com.guidexos.tests.duplicate", "Lexically Last");
    check(manifestsWritten, "duplicate manifest fixtures written");
    check(AppManifestLoader::LoadFromFile(firstPath).valid,
        "CRLF manifest file is read using its full byte length");
    AppRegistry scanRegistry;
    const AppScanResult scan = scanRegistry.Scan({
        { AppSourceKind::Package, root }
    });
    check(scan.duplicateApps.size() == 1 && scan.registeredApps.size() == 1,
        "same-source duplicate ID is reported rather than silently ignored");
    check(scan.registeredApps.size() == 1 &&
        scan.registeredApps.front().manifest.displayName == "Lexically First",
        "same-source duplicate winner is independent of directory iteration order");
    std::filesystem::remove_all(root);

    std::cout << "appModelIdentityChecks=" << (25 - failures) << "/25\n";
    return failures == 0 ? 0 : 1;
}
