#include "app_registry.h"
#include "settings_default_apps_model.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
using namespace gxos::apps;

class RegistrySettingsBackend final : public settings::DefaultAppsBackend {
public:
    explicit RegistrySettingsBackend(AppRegistry& value) : registry(value) {}
    bool available() const override { return true; }
    std::vector<std::string> knownDocumentExtensions() override { return registry.GetKnownDocumentExtensions(); }
    DefaultHandlerInfo defaultHandlerInfo(const std::string& extension) override {
        return registry.GetDefaultHandlerInfo(extension);
    }
    DocumentHandlerList capableHandlers(const std::string& extension) override {
        return registry.EnumerateCapableHandlers(extension);
    }
    std::string displayName(const std::string& canonicalAppId) override {
        const RegisteredApp* app = registry.FindById(canonicalAppId);
        return app ? app->manifest.displayName : std::string();
    }
    bool isDurableApp(const std::string& canonicalAppId) override {
        const RegisteredApp* app = registry.FindById(canonicalAppId);
        return app && !app->temporaryDevelopment && app->sourceKind != AppSourceKind::DevelopmentTemporary;
    }
    DefaultHandlerMutationResult setDefaultHandler(
        const std::string& extension, const std::string& canonicalAppId) override {
        return registry.SetDefaultHandler(extension, canonicalAppId);
    }
    DefaultHandlerMutationResult clearDefaultHandler(const std::string& extension) override {
        return registry.ClearDefaultHandler(extension);
    }

private:
    AppRegistry& registry;
};

int checks = 0;
int failures = 0;

void check(bool condition, const std::string& name) {
    ++checks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else { ++failures; std::cout << "FAIL: " << name << "\n"; }
}

uint32_t checksum(const std::string& value) {
    uint32_t hash = 2166136261u;
    for (unsigned char byte : value) { hash ^= byte; hash *= 16777619u; }
    return hash;
}

std::string configText(const std::string& body, size_t lines) {
    std::ostringstream output;
    output << "GXOS-APP-DEFAULTS 1\n" << body << "END " << lines << ' ';
    output.width(8);
    output.fill('0');
    output << std::hex << std::uppercase << checksum(body) << "\n";
    return output.str();
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

RegisteredApp durableHandler(const std::string& id,
                             const std::string& label,
                             const std::vector<std::string>& extensions = { ".txt" },
                             bool supportsDocuments = true,
                             bool backendAvailable = true) {
    RegisteredApp app;
    app.sourceKind = AppSourceKind::Package;
    app.documentActivationBackendAvailable = backendAvailable;
    app.manifest.id = id;
    app.manifest.displayName = label;
    app.manifest.version = "1.0.0";
    app.manifest.publisher = "test fixture";
    app.manifest.kind = AppKind::Service;
    app.manifest.supportsDocumentActivation = supportsDocuments;
    for (const std::string& extension : extensions) {
        app.manifest.fileAssociations.push_back({ extension, "text/plain", "test document" });
    }
    return app;
}

RegisteredApp temporaryHandler() {
    RegisteredApp app = durableHandler("test.handler.temporary", "Temporary", { ".txt" });
    app.sourceKind = AppSourceKind::DevelopmentTemporary;
    app.temporaryDevelopment = true;
    app.temporaryOwnerRuntimeId = 44;
    app.temporaryGeneration = 3;
    return app;
}

bool addBuiltIns(AppRegistry& registry) {
    registry.RegisterBuiltInAppsAsManifests();
    return registry.FindById("gxos.builtin.notepad") != nullptr;
}

bool addDurable(AppRegistry& registry, const RegisteredApp& app) {
    std::string error;
    return registry.RegisterTestDurableApp(app, error);
}

bool dispatchToFixture(const AppActivationContext& activation, std::string& selectedAppId) {
    if (activation.kind != AppActivationKind::Document || activation.appId.empty() ||
        activation.documentPath.empty()) return false;
    selectedAppId = activation.appId;
    return true;
}

} // namespace

int main() {
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() /
        ("guidexos-appmodel-defaults-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempRoot);
    const std::string tempPrefix = std::filesystem::temp_directory_path().string();
    const std::string resolvedRoot = std::filesystem::absolute(tempRoot).string();
    if (resolvedRoot.rfind(tempPrefix, 0) != 0) {
        std::cerr << "Refusing to use a test directory outside the system temporary directory.\n";
        return 2;
    }

    const std::filesystem::path storePath = tempRoot / "appmodel-default-handlers.cfg";
    const std::filesystem::path unrelatedPath = tempRoot / "desktop.json";
    const std::string unrelatedBytes = "{\"wallpaper\":\"keep-me\",\"futureField\":{\"n\":7}}\r\n";
    writeText(unrelatedPath, unrelatedBytes);

    std::string normalized;
    check(NormalizeDocumentExtension(".txt", normalized) && normalized == ".txt" &&
        NormalizeDocumentExtension(".TXT", normalized) && normalized == ".txt" &&
        NormalizeDocumentExtension(".Txt", normalized) && normalized == ".txt",
        "extension normalization shares case-folding contract across .txt variants");
    check(!NormalizeDocumentExtension("txt", normalized) &&
        !NormalizeDocumentExtension(".txt.", normalized) &&
        !NormalizeDocumentExtension(".", normalized) &&
        !NormalizeDocumentExtension("." + std::string(kAppModelMaxFileExtensionBytes, 'x'), normalized),
        "malformed, trailing-dot, extensionless, and overlong extensions are rejected");

    std::string serializationError;
    std::string serialized;
    const std::vector<DefaultHandlerOverride> sample = {
        { ".txt", "gxos.builtin.notepad" }, { ".log", "test.handler.alpha" }
    };
    check(DefaultAppHandlerStore::Serialize(sample, serialized, serializationError),
        "canonical bounded store serialization succeeds");
    std::vector<DefaultHandlerOverride> parsed;
    size_t invalidCount = 0;
    std::string parseError;
    check(DefaultAppHandlerStore::ParseText(serialized, parsed, invalidCount, parseError) &&
        parsed.size() == 2 && parsed[0].extension == ".log" && parsed[1].appId == "gxos.builtin.notepad" && invalidCount == 0,
        "valid store reload preserves normalized extension and canonical App IDs in deterministic order");
    std::vector<DefaultHandlerOverride> maximumSizeRecords;
    for (size_t i = 0; i < kAppModelMaxDefaultHandlerOverrides; ++i) {
        std::string extension = ".x" + std::to_string(i);
        extension.append(kAppModelMaxFileExtensionBytes - extension.size(), 'a');
        std::string appId = "fixture." + std::to_string(i);
        appId.append(kAppModelMaxAppIdBytes - appId.size(), 'b');
        maximumSizeRecords.push_back({ extension, appId });
    }
    check(DefaultAppHandlerStore::Serialize(maximumSizeRecords, serialized, serializationError) &&
        serialized.size() <= kAppModelDefaultHandlerConfigMaxBytes &&
        DefaultAppHandlerStore::ParseText(serialized, parsed, invalidCount, parseError) &&
        parsed.size() == kAppModelMaxDefaultHandlerOverrides && invalidCount == 0,
        "128 maximum-length records fit the total serialized configuration bound");
    std::string truncated = serialized;
    truncated.pop_back();
    check(!DefaultAppHandlerStore::ParseText(truncated, parsed, invalidCount, parseError),
        "truncated configuration fails checksum/footer validation");
    check(!DefaultAppHandlerStore::ParseText(std::string(kAppModelDefaultHandlerConfigMaxBytes + 1, 'x'), parsed, invalidCount, parseError),
        "configuration file byte bound is enforced on reload");

    const std::string malformedBody = ".txt=first.app\n.txt=second.app\n.log=good.app\nfutureField=value\n.txt.=bad.app\n";
    check(DefaultAppHandlerStore::ParseText(configText(malformedBody, 5), parsed, invalidCount, parseError) &&
        parsed.size() == 1 && parsed[0].extension == ".log" && invalidCount >= 4,
        "duplicate keys and malformed or unknown records are ignored without losing unrelated valid records");
    const std::string overlongIdBody = ".txt=" + std::string(kAppModelMaxAppIdBytes + 1, 'a') + "\n";
    check(DefaultAppHandlerStore::ParseText(configText(overlongIdBody, 1), parsed, invalidCount, parseError) &&
        parsed.empty() && invalidCount == 1,
        "overlong stored App ID is ignored as one invalid record");
    const std::string missingIdBody = ".txt=\n";
    check(DefaultAppHandlerStore::ParseText(configText(missingIdBody, 1), parsed, invalidCount, parseError) &&
        parsed.empty() && invalidCount == 1,
        "missing canonical App ID is ignored without selecting a handler");

    {
        DefaultAppHandlerStore emptyStore((tempRoot / "empty.cfg").string());
        check(emptyStore.Diagnostics().loadStatus == DefaultHandlerStoreLoadStatus::Missing && emptyStore.Overrides().empty(),
            "missing store loads as an empty machine-global override set");
        check(emptyStore.Commit(sample, serializationError) && emptyStore.Overrides().size() == 2 &&
            emptyStore.Diagnostics().lastWriteStatus == DefaultHandlerStoreWriteStatus::Succeeded,
            "store commit rereads and verifies its serialized replacement");
        DefaultAppHandlerStore reloaded((tempRoot / "empty.cfg").string());
        check(reloaded.Overrides() == std::vector<DefaultHandlerOverride>({
            { ".log", "test.handler.alpha" }, { ".txt", "gxos.builtin.notepad" }
        }), "a recreated configuration owner reloads canonical records");
    }

    std::vector<DefaultHandlerOverride> atCapacity;
    for (size_t i = 0; i < kAppModelMaxDefaultHandlerOverrides; ++i) {
        atCapacity.push_back({ ".x" + std::to_string(i), "test.handler." + std::to_string(i) });
    }
    const std::filesystem::path capacityPath = tempRoot / "capacity.cfg";
    DefaultAppHandlerStore capacityStore(capacityPath.string());
    check(capacityStore.Commit(atCapacity, serializationError) && capacityStore.Overrides().size() == kAppModelMaxDefaultHandlerOverrides,
        "all 128 override records fit within the bounded store");
    atCapacity.push_back({ ".x128", "test.handler.128" });
    const std::string capacityBytes = readText(capacityPath);
    check(!capacityStore.Commit(atCapacity, serializationError) && readText(capacityPath) == capacityBytes,
        "capacity exhaustion fails deterministically without evicting or replacing an existing default");

    const RegisteredApp appB = durableHandler("test.handler.editor.b", "Same Editor");
    const RegisteredApp appC = durableHandler("test.handler.editor.c", "Same Editor");
    const RegisteredApp incapable = durableHandler("test.handler.incapable", "Incapable", { ".other" });
    const RegisteredApp unavailable = durableHandler("test.handler.unavailable", "Unavailable", { ".txt" }, true, false);
    const RegisteredApp noActivation = durableHandler("test.handler.no-activation", "No Activation", { ".txt" }, false, true);

    {
        const std::filesystem::path studioStorePath = tempRoot / "developer-studio.cfg";
        AppRegistry production(false, studioStorePath);
        production.SetSources({ { AppSourceKind::Package, "Apps/DeveloperStudio" } });
        const AppScanResult scan = production.Scan();
        const RegisteredApp* studio = production.FindById("com.guidexos.developerstudio");
        check(scan.registeredAppCount == 1 && studio && studio->manifest.supportsDocumentActivation &&
            studio->manifest.fileAssociations.size() == 9,
            "production Developer Studio manifest declares its canonical ID and bounded C, C++, and plain-text capabilities");
        const DocumentHandlerList unavailableStudio = production.EnumerateCapableHandlers(".txt");
        check(studio && unavailableStudio.declaredHandlerCount == 1 &&
            unavailableStudio.availableHandlerCount == 0 &&
            production.SetDocumentActivationBackendAvailable(studio->manifest.id, true),
            "production Developer Studio registration can receive a live document backend");
        addBuiltIns(production);
        const DocumentHandlerList sharedText = production.EnumerateCapableHandlers(".TXT");
        const DefaultHandlerInfo textDefault = production.GetDefaultHandlerInfo(".txt");
        check(sharedText.count == 2 && sharedText.availableHandlerCount == 2 && sharedText.handlers[0].isDefault &&
            sharedText.handlers[0].appId == "gxos.builtin.notepad" &&
            sharedText.handlers[1].appId == "com.guidexos.developerstudio" &&
            textDefault.builtInDefaultAppId == "gxos.builtin.notepad" &&
            textDefault.configuredOverrideAppId.empty() && textDefault.effectiveDefaultAppId == "gxos.builtin.notepad",
            "real production .txt competition keeps Notepad as the unchanged built-in default");
        check(production.ResolveFileAssociation("/src/HELLO.CPP").launchable() &&
            production.ResolveFileAssociation("/src/HELLO.CPP").appId == "com.guidexos.developerstudio",
            "a newly declared standalone C++ source uses Developer Studio under the existing single-handler default policy");
        RegistrySettingsBackend settingsBackend(production);
        settings::DefaultAppsModel settingsModel;
        settingsModel.refresh(settingsBackend);
        const int textRowIndex = settings::findDefaultAppsRowIndex(settingsModel.snapshot(), ".txt");
        const settings::DefaultAppsRow* textRow = textRowIndex >= 0
            ? &settingsModel.snapshot().rows[static_cast<size_t>(textRowIndex)] : nullptr;
        const std::vector<size_t> eligibleChoices = textRow
            ? settings::eligibleDefaultAppHandlerIndices(*textRow, settingsBackend) : std::vector<size_t>{};
        size_t studioChoice = eligibleChoices.size();
        for (size_t choice = 0; choice < eligibleChoices.size(); ++choice) {
            if (textRow->handlers.handlers[eligibleChoices[choice]].appId == "com.guidexos.developerstudio") {
                studioChoice = choice;
                break;
            }
        }
        const DefaultHandlerMutationResult selected = studioChoice < eligibleChoices.size()
            ? settingsModel.chooseHandler(settingsBackend, ".txt", studioChoice) : DefaultHandlerMutationResult{};
        const FileAssociationResolution changedDefault = production.ResolveFileAssociation("/docs/one.txt");
        const DocumentHandlerList withOverride = production.EnumerateCapableHandlers(".txt");
        auto notepad = std::find_if(withOverride.handlers.begin(), withOverride.handlers.begin() + withOverride.count,
            [](const DocumentHandlerInfo& handler) { return handler.appId == "gxos.builtin.notepad"; });
        const FileAssociationResolution oneTimeNotepad = notepad == withOverride.handlers.begin() + withOverride.count
            ? FileAssociationResolution{} : production.ResolveDocumentActivation(*notepad, "/docs/once.TXT");
        AppRegistry reloadedProduction(false, studioStorePath);
        reloadedProduction.SetSources({ { AppSourceKind::Package, "Apps/DeveloperStudio" } });
        const AppScanResult reloadedScan = reloadedProduction.Scan();
        const RegisteredApp* reloadedStudio = reloadedProduction.FindById("com.guidexos.developerstudio");
        const bool reloadedBackend = reloadedStudio &&
            reloadedProduction.SetDocumentActivationBackendAvailable(reloadedStudio->manifest.id, true);
        addBuiltIns(reloadedProduction);
        const DefaultHandlerInfo reloadedTextDefault = reloadedProduction.GetDefaultHandlerInfo(".txt");
        const FileAssociationResolution reloadedOrdinaryOpen = reloadedProduction.ResolveFileAssociation("/docs/reloaded.txt");
        const DocumentHandlerList reloadedHandlers = reloadedProduction.EnumerateCapableHandlers(".txt");
        const settings::DefaultAppsRow* configuredRow = settings::findDefaultAppsRow(settingsModel.snapshot(), ".txt");
        check(selected.succeeded() && changedDefault.launchable() &&
            changedDefault.appId == "com.guidexos.developerstudio" && oneTimeNotepad.launchable() &&
            oneTimeNotepad.appId == "gxos.builtin.notepad" &&
            production.GetDefaultHandlerInfo(".txt").effectiveDefaultAppId == "com.guidexos.developerstudio" &&
            configuredRow && configuredRow->policy.configuredOverrideAppId ==
                "com.guidexos.developerstudio",
            "production Default Apps model persists the real Developer Studio handler while one-time Notepad remains non-mutating");
        check(reloadedScan.registeredAppCount == 1 && reloadedBackend &&
            reloadedTextDefault.configuredOverrideAppId == "com.guidexos.developerstudio" &&
            reloadedOrdinaryOpen.launchable() && reloadedOrdinaryOpen.appId == "com.guidexos.developerstudio" &&
            reloadedHandlers.count == 2 && reloadedHandlers.availableHandlerCount == 2,
            "a recreated production AppRegistry owner reloads the persisted Developer Studio canonical ID and ordinary Open target");
        RegistrySettingsBackend reloadedBackendForSettings(reloadedProduction);
        settings::DefaultAppsModel reloadedSettingsModel;
        reloadedSettingsModel.refresh(reloadedBackendForSettings);
        check(reloadedSettingsModel.restoreBuiltInDefault(reloadedBackendForSettings, ".txt").succeeded() &&
            reloadedProduction.ResolveFileAssociation("/docs/restored.txt").appId == "gxos.builtin.notepad" &&
            reloadedProduction.EnumerateCapableHandlers(".txt").availableHandlerCount == 2,
            "production Default Apps Restore selects Notepad and keeps Developer Studio available");
        check(production.ResolveDocumentActivation("com.guidexos.developerstudio", "/docs/unsupported.ini").status ==
            FileAssociationResolutionStatus::HandlerDoesNotSupportDocuments,
            "direct Developer Studio activation rejects an extension outside its manifest capability set");
    }

    {
        AppRegistry registry(false, storePath);
        check(addBuiltIns(registry) && addDurable(registry, appB) && addDurable(registry, appC) &&
            addDurable(registry, incapable) && addDurable(registry, unavailable) && addDurable(registry, noActivation),
            "built-in and isolated durable synthetic registrations are available to the owner");
        check(registry.SetDefaultHandler(".TXT", appB.manifest.id).succeeded(),
            "SetDefaultHandler accepts a current capable durable canonical app ID");
        check(registry.SetDefaultHandler(".Txt", appB.manifest.id).succeeded() &&
            registry.GetDefaultHandlerStoreDiagnostics().overrideCount == 1,
            "equivalent extension forms replace one key rather than creating duplicate records");
        check(registry.SetDefaultHandler(".txt", "test.handler.missing").status == DefaultHandlerMutationStatus::UnknownApplication,
            "unknown canonical application is rejected");
        check(registry.SetDefaultHandler(".txt", "TEST.HANDLER.EDITOR.B").status == DefaultHandlerMutationStatus::UnknownApplication,
            "canonical App ID lookup remains exact and case-sensitive");
        check(registry.SetDefaultHandler(".txt", incapable.manifest.id).status == DefaultHandlerMutationStatus::CapabilityMissing,
            "handler without the requested extension capability is rejected");
        check(registry.SetDefaultHandler(".txt", unavailable.manifest.id).status == DefaultHandlerMutationStatus::HandlerUnavailable,
            "backend-unavailable handler cannot become the effective default");
        check(registry.SetDefaultHandler(".txt", noActivation.manifest.id).status == DefaultHandlerMutationStatus::DocumentActivationUnsupported,
            "handler without document activation support is rejected");
        std::string temporaryError;
        check(registry.RegisterTemporaryDevelopmentApp(temporaryHandler(), temporaryError) &&
            registry.SetDefaultHandler(".txt", "test.handler.temporary").status == DefaultHandlerMutationStatus::NonDurableRegistration,
            "temporary registration is rejected as a durable machine default");
        check(registry.SetDefaultHandler("txt", appB.manifest.id).status == DefaultHandlerMutationStatus::InvalidExtension,
            "malformed extension is rejected before persistence");
    }

    check(readText(unrelatedPath) == unrelatedBytes,
        "dedicated App Model store leaves unrelated desktop configuration byte-for-byte unchanged");

    bool stressOk = true;
    size_t ordinaryDispatches = 0;
    size_t explicitDispatches = 0;
    size_t reloadCycles = 0;
    for (size_t cycle = 0; cycle < 100; ++cycle) {
        AppRegistry recreated(false, storePath);
        if (!addBuiltIns(recreated) || !addDurable(recreated, appB) || !addDurable(recreated, appC)) {
            stressOk = false;
            continue;
        }
        const DefaultHandlerInfo state = recreated.GetDefaultHandlerInfo(".txt");
        if (state.configuredOverrideAppId != appB.manifest.id ||
            state.effectiveDefaultAppId != appB.manifest.id ||
            state.configuredStatus != ConfiguredDefaultHandlerStatus::Available) stressOk = false;
        const FileAssociationResolution ordinary = recreated.ResolveFileAssociation("/docs/restart-proof.TXT");
        std::string ordinaryDispatch;
        if (!ordinary.launchable() || !dispatchToFixture(ordinary.activation, ordinaryDispatch) ||
            ordinaryDispatch != appB.manifest.id) stressOk = false;
        else ++ordinaryDispatches;

        const DocumentHandlerList handlers = recreated.EnumerateCapableHandlers(".TXT");
        auto notepad = std::find_if(handlers.handlers.begin(), handlers.handlers.begin() + handlers.count,
            [](const DocumentHandlerInfo& handler) { return handler.appId == "gxos.builtin.notepad"; });
        if (notepad == handlers.handlers.begin() + handlers.count) stressOk = false;
        else {
            const FileAssociationResolution explicitOpen = recreated.ResolveDocumentActivation(*notepad, "/docs/restart-proof.TXT");
            std::string explicitDispatch;
            if (!explicitOpen.launchable() || !dispatchToFixture(explicitOpen.activation, explicitDispatch) ||
                explicitDispatch != "gxos.builtin.notepad") stressOk = false;
            else ++explicitDispatches;
        }
        const DefaultHandlerInfo afterOpenWith = recreated.GetDefaultHandlerInfo(".txt");
        if (afterOpenWith.effectiveDefaultAppId != appB.manifest.id ||
            afterOpenWith.configuredOverrideAppId != appB.manifest.id) stressOk = false;
        if (readText(unrelatedPath) != unrelatedBytes) stressOk = false;
        ++reloadCycles;
    }
    check(stressOk && reloadCycles == 100 && ordinaryDispatches == 100 && explicitDispatches == 100,
        "100 owner recreation cycles preserve B for ordinary Open and A for one-time Open With");
    check(readText(storePath).find("test.handler.editor.b") != std::string::npos &&
        readText(storePath).find("Same Editor") == std::string::npos,
        "duplicate display labels have no persisted authority; the canonical B ID survives reload");
    check(readText(unrelatedPath) == unrelatedBytes,
        "100 persistence cycles preserve unrelated desktop configuration bytes");

    {
        DefaultAppHandlerStore failureStore(storePath.string());
        const std::string oldBytes = readText(storePath);
        const std::filesystem::path blockedTemporary = storePath.string() + ".tmp";
        std::filesystem::create_directory(blockedTemporary);
        std::vector<DefaultHandlerOverride> replacement = { { ".txt", appC.manifest.id } };
        check(!failureStore.Commit(replacement, serializationError) && readText(storePath) == oldBytes,
            "failed temporary write leaves the previous valid configuration byte-for-byte intact");
        std::filesystem::remove(blockedTemporary);
        std::string reloadError;
        check(failureStore.Reload(reloadError) && failureStore.Find(".txt") &&
            failureStore.Find(".txt")->appId == appB.manifest.id,
            "failed write leaves the last valid in-memory and reloaded snapshot intact");
#ifdef _WIN32
        HANDLE heldTarget = CreateFileA(storePath.string().c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (heldTarget != INVALID_HANDLE_VALUE) {
            check(!failureStore.Commit(replacement, serializationError) &&
                failureStore.Diagnostics().lastWriteStatus == DefaultHandlerStoreWriteStatus::ReplaceFailed &&
                readText(storePath) == oldBytes,
                "failed atomic replace leaves the previous valid file byte-for-byte intact");
            CloseHandle(heldTarget);
        } else {
            check(false, "failed atomic replace fixture can hold the prior file without delete sharing");
        }
#endif
    }

    {
        const std::filesystem::path staleImagePath = tempRoot / "stale-image-overrides.cfg";
        const std::string staleImageBody = ".bmp=gxos.builtin.imageviewer\n.gif=gxos.builtin.imageviewer\n";
        writeText(staleImagePath, configText(staleImageBody, 2));
        const std::string staleImageConfigBefore = readText(staleImagePath);
        AppRegistry staleImages(false, staleImagePath);
        check(addBuiltIns(staleImages), "stale BMP/GIF override fixture loads production handlers");
        const DefaultHandlerInfo bmpState = staleImages.GetDefaultHandlerInfo(".bmp");
        const DefaultHandlerInfo gifState = staleImages.GetDefaultHandlerInfo(".gif");
        const std::vector<std::string> known = staleImages.GetKnownDocumentExtensions();
        RegistrySettingsBackend staleBackend(staleImages);
        settings::DefaultAppsModel staleSettings;
        staleSettings.refresh(staleBackend);
        const settings::DefaultAppsRow* bmpRow = settings::findDefaultAppsRow(staleSettings.snapshot(), ".bmp");
        const settings::DefaultAppsRow* gifRow = settings::findDefaultAppsRow(staleSettings.snapshot(), ".gif");
        check(bmpState.configuredStatus == ConfiguredDefaultHandlerStatus::CapabilityMissing &&
            gifState.configuredStatus == ConfiguredDefaultHandlerStatus::CapabilityMissing &&
            bmpState.effectiveDefaultAppId.empty() && gifState.effectiveDefaultAppId.empty() &&
            staleImages.ResolveFileAssociation("/docs/stale.bmp").status == FileAssociationResolutionStatus::NoAssociation &&
            staleImages.ResolveFileAssociation("/docs/stale.gif").status == FileAssociationResolutionStatus::NoAssociation &&
            staleImages.ResolveDocumentActivation("gxos.builtin.imageviewer", "/docs/explicit.bmp").status ==
                FileAssociationResolutionStatus::HandlerDoesNotSupportDocuments &&
            staleImages.EnumerateCapableHandlers(".bmp").count == 0 &&
            staleImages.EnumerateCapableHandlers(".gif").count == 0 &&
            bmpRow && gifRow && bmpRow->handlers.count == 0 && gifRow->handlers.count == 0 &&
            settings::eligibleDefaultAppHandlerIndices(*bmpRow, staleBackend).empty() &&
            settings::eligibleDefaultAppHandlerIndices(*gifRow, staleBackend).empty() &&
            std::find(known.begin(), known.end(), ".bmp") != known.end() &&
            std::find(known.begin(), known.end(), ".gif") != known.end() &&
            readText(staleImagePath) == staleImageConfigBefore,
            "stale BMP/GIF policy remains inert: no effective default, capable handler, Open, or eligible Settings choice");
    }

    {
        DefaultAppHandlerStore staleStore((tempRoot / "stale.cfg").string());
        std::string error;
        check(staleStore.Commit({ { ".abc", "test.handler.gone" } }, error), "stale-ID fixture is stored as canonical identity only");
        AppRegistry stale(false, tempRoot / "stale.cfg");
        check(addBuiltIns(stale), "stale-ID resolution fixture has production Notepad only");
        const DefaultHandlerInfo staleInfo = stale.GetDefaultHandlerInfo(".abc");
        check(staleInfo.configuredOverrideAppId == "test.handler.gone" &&
            staleInfo.configuredStatus == ConfiguredDefaultHandlerStatus::RegistrationMissing &&
            staleInfo.effectiveDefaultAppId.empty() &&
            stale.ResolveFileAssociation("/docs/no-handler.abc").status == FileAssociationResolutionStatus::NoAssociation,
            "unknown stored ID with no built-in resolves to no handler and cannot redirect Open");
    }

    {
        AppRegistry missing(false, storePath);
        check(addBuiltIns(missing), "missing-registration fixture loads production handlers");
        const DefaultHandlerInfo state = missing.GetDefaultHandlerInfo(".txt");
        check(state.configuredStatus == ConfiguredDefaultHandlerStatus::RegistrationMissing &&
            state.effectiveDefaultAppId == "gxos.builtin.notepad" &&
            missing.ResolveFileAssociation("/docs/gone.TXT").activation.appId == "gxos.builtin.notepad",
            "disappeared override falls back to the built-in handler without changing stored policy");
    }
    {
        AppRegistry lostCapability(false, storePath);
        addBuiltIns(lostCapability);
        addDurable(lostCapability, durableHandler(appB.manifest.id, "Same Editor", { ".other" }));
        const DefaultHandlerInfo state = lostCapability.GetDefaultHandlerInfo(".txt");
        check(state.configuredStatus == ConfiguredDefaultHandlerStatus::CapabilityMissing &&
            state.effectiveDefaultAppId == "gxos.builtin.notepad",
            "override whose application loses the extension capability falls back safely");
    }
    {
        AppRegistry unavailableAgain(false, storePath);
        addBuiltIns(unavailableAgain);
        addDurable(unavailableAgain, durableHandler(appB.manifest.id, "Same Editor", { ".txt" }, true, false));
        const DefaultHandlerInfo state = unavailableAgain.GetDefaultHandlerInfo(".txt");
        check(state.configuredStatus == ConfiguredDefaultHandlerStatus::TemporarilyUnavailable &&
            state.configuredOverrideAppId == appB.manifest.id &&
            state.effectiveDefaultAppId == "gxos.builtin.notepad",
            "temporarily unavailable override remains stored while ordinary Open uses available Notepad");
    }
    {
        AppRegistry slotReuse(false, storePath);
        addBuiltIns(slotReuse);
        addDurable(slotReuse, durableHandler(appC.manifest.id, "Same Editor"));
        const DefaultHandlerInfo state = slotReuse.GetDefaultHandlerInfo(".txt");
        check(state.configuredStatus == ConfiguredDefaultHandlerStatus::RegistrationMissing &&
            state.effectiveDefaultAppId == "gxos.builtin.notepad" &&
            slotReuse.ResolveFileAssociation("/docs/reused-slot.txt").activation.appId == "gxos.builtin.notepad",
            "a different canonical ID reusing a registration position cannot receive the persisted override");
    }

    {
        AppRegistry clear(false, storePath);
        addBuiltIns(clear);
        addDurable(clear, appB);
        check(clear.ClearDefaultHandler(".TXT").succeeded() &&
            clear.GetDefaultHandlerInfo(".txt").configuredOverrideAppId.empty() &&
            clear.GetDefaultHandlerInfo(".txt").effectiveDefaultAppId == "gxos.builtin.notepad",
            "ClearDefaultHandler removes the override and restores the built-in default");
        bool allNotepad = true;
        for (const char* extension : { ".txt", ".log", ".ini", ".cfg" }) {
            const FileAssociationResolution resolved = clear.ResolveFileAssociation(std::string("/docs/production-proof") + extension);
            if (!resolved.launchable() || resolved.activation.appId != "gxos.builtin.notepad") allNotepad = false;
        }
        check(allNotepad, "production defaults remain Notepad for txt, log, ini, and cfg after clearing");
        const std::vector<std::string> known = clear.GetKnownDocumentExtensions();
        check(std::is_sorted(known.begin(), known.end()) &&
            std::find(known.begin(), known.end(), ".txt") != known.end() &&
            std::find(known.begin(), known.end(), ".cfg") != known.end(),
            "known associated extensions are bounded by the registry and enumerated deterministically");
    }

    {
        const std::filesystem::path corruptPath = tempRoot / "corrupt.cfg";
        const std::string corrupt = "GXOS-APP-DEFAULTS 1\n.txt=test.handler.broken\nEND 1 00000000\n";
        writeText(corruptPath, corrupt);
        AppRegistry corruptRegistry(false, corruptPath);
        addBuiltIns(corruptRegistry);
        const DefaultHandlerInfo state = corruptRegistry.GetDefaultHandlerInfo(".txt");
        const DefaultHandlerStoreDiagnostics diagnostics = corruptRegistry.GetDefaultHandlerStoreDiagnostics();
        check(state.configuredOverrideAppId.empty() && state.effectiveDefaultAppId == "gxos.builtin.notepad" &&
            diagnostics.loadStatus == DefaultHandlerStoreLoadStatus::Invalid && readText(corruptPath) == corrupt,
            "corrupt configuration is ignored, left untouched, and cannot redirect ordinary Open");
    }

    std::filesystem::remove_all(tempRoot);
    std::cout << "appModelDefaultHandlerChecks=" << (checks - failures) << "/" << checks << "\n";
    std::cout << "persistenceReloadCycles=" << reloadCycles << "/100\n";
    std::cout << "runtimeOrdinaryOpenDispatches=" << ordinaryDispatches << "/100\n";
    std::cout << "runtimeOneTimeOpenWithDispatches=" << explicitDispatches << "/100\n";
    return failures == 0 ? 0 : 1;
}
