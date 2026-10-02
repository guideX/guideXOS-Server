#include "settings_default_apps_model.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace gxos::apps;
using namespace gxos::apps::settings;

int modelChecks = 0;
int interactionChecks = 0;
int failures = 0;
int modelFailures = 0;
int interactionFailures = 0;

void check(bool condition, const std::string& name, bool interaction = false)
{
    if (interaction) ++interactionChecks;
    else ++modelChecks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else {
        ++failures;
        if (interaction) ++interactionFailures;
        else ++modelFailures;
        std::cout << "FAIL: " << name << "\n";
    }
}

RegisteredApp handler(const std::string& id, const std::string& label,
                      const std::vector<std::string>& extensions,
                      bool backendAvailable = true)
{
    RegisteredApp app;
    app.sourceKind = AppSourceKind::Package;
    app.documentActivationBackendAvailable = backendAvailable;
    app.manifest.id = id;
    app.manifest.displayName = label;
    app.manifest.version = "1.0.0";
    app.manifest.publisher = "Settings test fixture";
    app.manifest.kind = AppKind::Service;
    app.manifest.supportsDocumentActivation = true;
    for (const std::string& extension : extensions)
        app.manifest.fileAssociations.push_back({ extension, "text/plain", "synthetic test document" });
    return app;
}

bool addBuiltIns(AppRegistry& registry)
{
    registry.RegisterBuiltInAppsAsManifests();
    return registry.FindById("gxos.builtin.notepad") != nullptr;
}

bool add(AppRegistry& registry, const RegisteredApp& app)
{
    std::string error;
    return registry.RegisterTestDurableApp(app, error);
}

class RegistryBackend final : public DefaultAppsBackend {
public:
    explicit RegistryBackend(AppRegistry& value) : registry(value) {}

    bool available() const override { return serviceAvailable; }
    std::vector<std::string> knownDocumentExtensions() override
    {
        if (replaceKnownExtensions) return extraExtensions;
        std::vector<std::string> result = registry.GetKnownDocumentExtensions();
        result.insert(result.end(), extraExtensions.begin(), extraExtensions.end());
        return result;
    }
    DefaultHandlerInfo defaultHandlerInfo(const std::string& extension) override
    {
        return registry.GetDefaultHandlerInfo(extension);
    }
    DocumentHandlerList capableHandlers(const std::string& extension) override
    {
        return registry.EnumerateCapableHandlers(extension);
    }
    std::string displayName(const std::string& canonicalAppId) override
    {
        const RegisteredApp* app = registry.FindById(canonicalAppId);
        return app ? app->manifest.displayName : std::string();
    }
    bool isDurableApp(const std::string& canonicalAppId) override
    {
        const RegisteredApp* app = registry.FindById(canonicalAppId);
        return app && !app->temporaryDevelopment && app->sourceKind != AppSourceKind::DevelopmentTemporary &&
            app->temporaryOwnerRuntimeId == 0 && app->temporaryGeneration == 0;
    }
    DefaultHandlerMutationResult setDefaultHandler(
        const std::string& extension, const std::string& canonicalAppId) override
    {
        return registry.SetDefaultHandler(extension, canonicalAppId);
    }
    DefaultHandlerMutationResult clearDefaultHandler(const std::string& extension) override
    {
        return registry.ClearDefaultHandler(extension);
    }

    AppRegistry& registry;
    bool serviceAvailable{true};
    bool replaceKnownExtensions{false};
    std::vector<std::string> extraExtensions;
};

int eligibleChoiceFor(const DefaultAppsRow& row, DefaultAppsBackend& backend,
                      const std::string& appId)
{
    const std::vector<size_t> choices = eligibleDefaultAppHandlerIndices(row, backend);
    for (size_t i = 0; i < choices.size(); ++i)
        if (row.handlers.handlers[choices[i]].appId == appId) return static_cast<int>(i);
    return -1;
}

} // namespace

int main()
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() /
        ("guidexos-settings-default-apps-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempRoot);
    const std::filesystem::path storePath = tempRoot / "appmodel-default-handlers.cfg";

    SettingsRoute route{};
    check(parseSettingsRoute("settings://apps/default", route) && route.category == CategoryId::Apps &&
        route.target == TargetId::AppsDefault &&
        parseSettingsRoute("settings://apps/defaults", route) && route.category == CategoryId::Apps &&
        route.target == TargetId::AppsDefault,
        "Default apps has stable singular and plural Apps deep links");
    NavigationModel navigation;
    check(navigation.navigate(route) && navigation.route() == route,
        "Default apps route is accepted by the Settings navigation model");
    DefaultAppsSnapshot focusSnapshot;
    focusSnapshot.available = true;
    focusSnapshot.count = 1;
    const FocusControl rowFocus = initialDefaultAppsFocusControl(focusSnapshot);
    focusSnapshot.count = 0;
    const FocusControl emptyFocus = initialDefaultAppsFocusControl(focusSnapshot);
    check(rowFocus == FocusControl::DefaultAppEntry && navigation.canFocus(rowFocus) &&
        emptyFocus == FocusControl::AppsDefaultTab && navigation.canFocus(emptyFocus),
        "deep link focuses the first file type or falls back to the Default apps tab when no rows exist");
    check(!parseSettingsRoute("settings://default-apps", route) &&
        !parseSettingsRoute("settings://apps/other", route), "unknown Default apps routes are rejected");
    const std::vector<std::string> searchTerms = {
        "default apps", "default applications", "file types", "file associations", "open with",
        ".txt", ".log", ".ini", ".cfg"
    };
    bool allSearchTermsRoute = true;
    for (const std::string& query : searchTerms) {
        const SearchResultSet results = searchSettings(query);
        bool found = false;
        for (size_t i = 0; i < results.count; ++i)
            if (results.values[i].route.category == CategoryId::Apps &&
                results.values[i].route.target == TargetId::AppsDefault) found = true;
        allSearchTermsRoute = allSearchTermsRoute && found;
    }
    check(allSearchTermsRoute, "bounded Settings search routes all Default apps and production-extension terms");

    const std::string longExtension = ".abcdefghijklmnopqrstuvwxy123456";
    check(longExtension.size() == kAppModelMaxFileExtensionBytes,
        "synthetic near-limit extension fixture uses the full backend extension bound");
    const std::string longLabel(112, 'L');
    const RegisteredApp alpha = handler("app.test.alpha", "Editor", { ".testtxt", ".multi", ".many" });
    const RegisteredApp beta = handler("app.test.beta", "Editor", { ".testtxt", ".multi", ".many" });
    const RegisteredApp textEditor = handler("app.test.texteditor", "Text Editor", { ".txt" });
    const RegisteredApp one = handler("app.test.one", "One Handler", { ".one" });
    const RegisteredApp longName = handler("app.test.longname", longLabel, { ".longname" });
    const RegisteredApp longExtensionHandler = handler("app.test.longextension", "Long Extension", { longExtension });

    AppRegistry registry(false, storePath);
    bool registered = addBuiltIns(registry) && add(registry, alpha) && add(registry, beta) &&
        add(registry, textEditor) && add(registry, one) && add(registry, longName) &&
        add(registry, longExtensionHandler);
    for (int index = 0; index < 20; ++index) {
        char suffix[4]{};
        std::snprintf(suffix, sizeof(suffix), "%02d", index);
        const std::string id = std::string("app.test.handler.") + suffix;
        registered = add(registry, handler(id, "Handler " + std::to_string(index),
            { ".many" })) && registered;
    }
    for (int index = 0; index < 3; ++index)
        registered = add(registry, handler("app.test.multi." + std::to_string(index),
            "Multi " + std::to_string(index), { ".multi" })) && registered;
    check(registered, "synthetic registry contains durable, duplicate-label and capacity fixtures");

    RegistryBackend backend(registry);
    backend.extraExtensions = { ".empty", ".TESTTXT", longExtension };
    DefaultAppsModel model;
    model.refresh(backend);
    const DefaultAppsSnapshot& initial = model.snapshot();
    check(initial.available && initial.count > 0 && initial.count <= kMaxSettingsDefaultAppExtensions,
        "model reads the authoritative known-extension enumeration into a bounded snapshot");
    bool sorted = true;
    for (size_t i = 1; i < initial.count; ++i)
        sorted = sorted && initial.rows[i - 1].extension < initial.rows[i].extension;
    check(sorted, "extension rows are normalized, deduplicated and lexically ordered");
    check(findDefaultAppsRow(initial, ".testtxt") != nullptr &&
        findDefaultAppsRow(initial, longExtension) != nullptr,
        "registry-declared synthetic extensions appear without Settings hard-coding them");

    const DefaultAppsRow* html = findDefaultAppsRow(initial, ".html");
    const DefaultAppsRow* htm = findDefaultAppsRow(initial, ".htm");
    check(html && htm && html->policy.builtInDefaultAppId == "guidexos.navigator" &&
        html->policy.configuredOverrideAppId.empty() &&
        html->policy.effectiveDefaultAppId == "guidexos.navigator" &&
        html->handlers.count == 1 && html->handlers.handlers[0].appId == "guidexos.navigator" &&
        htm->policy.effectiveDefaultAppId == "guidexos.navigator",
        "Settings discovers HTML and HTM from AppRegistry with Navigator as the unconfigured built-in default");

    const DefaultAppsRow* txt = findDefaultAppsRow(initial, ".txt");
    check(txt && txt->policy.builtInDefaultAppId == "gxos.builtin.notepad" &&
        txt->policy.configuredOverrideAppId.empty() &&
        txt->policy.effectiveDefaultAppId == "gxos.builtin.notepad" &&
        txt->policy.effectiveDefaultAvailable && txt->effectiveDisplayName == "Notepad",
        "production built-in, configured and effective state remain distinct and truthful");
    const DefaultAppsRow* empty = findDefaultAppsRow(initial, ".empty");
    check(empty && empty->policy.effectiveDefaultAppId.empty() &&
        empty->handlers.count == 0 && eligibleDefaultAppHandlerIndices(*empty, backend).empty(),
        "known extension with no capable handler is represented without fabricating a choice");
    const DefaultAppsRow* oneRow = findDefaultAppsRow(initial, ".one");
    const DefaultAppsRow* multiRow = findDefaultAppsRow(initial, ".multi");
    const DefaultAppsRow* manyRow = findDefaultAppsRow(initial, ".many");
    check(oneRow && eligibleDefaultAppHandlerIndices(*oneRow, backend).size() == 1,
        "handler picker model supports one eligible handler");
    check(multiRow && eligibleDefaultAppHandlerIndices(*multiRow, backend).size() == 5,
        "handler picker model supports several handlers and stable canonical identities");
    check(manyRow && manyRow->handlers.count == kAppModelMaxDocumentHandlersPerExtension &&
        manyRow->handlers.declaredHandlerCount == 22 && manyRow->handlers.truncated &&
        eligibleDefaultAppHandlerIndices(*manyRow, backend).size() == 16,
        "handler picker remains bounded and reports backend truncation at its 16-handler limit");
    check(findDefaultAppsRow(initial, longExtension)->extension.size() == kAppModelMaxFileExtensionBytes,
        "near-limit extension remains intact in the value-owned row identity");
    const DefaultAppsRow* longNameRow = findDefaultAppsRow(initial, ".longname");
    check(longNameRow && longNameRow->effectiveDisplayName == longLabel &&
        boundedSettingValue(longNameRow->effectiveDisplayName, 22).size() <= 22,
        "long display labels remain presentation data and clip to a bounded UI value");

    const DefaultAppsRow* duplicateRow = findDefaultAppsRow(initial, ".testtxt");
    const int alphaChoice = duplicateRow ? eligibleChoiceFor(*duplicateRow, backend, alpha.manifest.id) : -1;
    const int betaChoice = duplicateRow ? eligibleChoiceFor(*duplicateRow, backend, beta.manifest.id) : -1;
    const std::vector<size_t> duplicateChoices = duplicateRow
        ? eligibleDefaultAppHandlerIndices(*duplicateRow, backend) : std::vector<size_t>{};
    const bool labelsEqual = duplicateRow && alphaChoice >= 0 && betaChoice >= 0 &&
        duplicateRow->handlers.handlers[duplicateChoices[static_cast<size_t>(alphaChoice)]].displayName ==
        duplicateRow->handlers.handlers[duplicateChoices[static_cast<size_t>(betaChoice)]].displayName;
    const std::string alphaLabel = duplicateRow && alphaChoice >= 0
        ? formatDefaultAppHandlerChoice(*duplicateRow,
            duplicateChoices[static_cast<size_t>(alphaChoice)], backend, 80) : std::string();
    const std::string betaLabel = duplicateRow && betaChoice >= 0
        ? formatDefaultAppHandlerChoice(*duplicateRow,
            duplicateChoices[static_cast<size_t>(betaChoice)], backend, 80) : std::string();
    check(labelsEqual && alphaLabel != betaLabel && alphaLabel.find(alpha.manifest.id) != std::string::npos &&
        betaLabel.find(beta.manifest.id) != std::string::npos,
        "duplicate display labels receive secondary presentation disambiguation without becoming identity");

    std::string selectedExtension;
    bool pickerOpen = false;
    int pickerScroll = 0;
    const int alphaRowIndex = findDefaultAppsRowIndex(initial, ".testtxt");
    check(openDefaultAppsPicker(initial, static_cast<size_t>(alphaRowIndex), selectedExtension,
            pickerOpen, pickerScroll) && pickerOpen && selectedExtension == ".testtxt",
        "mouse and Enter row activation open a picker by stable extension identity", true);
    check(closeDefaultAppsPicker(initial, selectedExtension, pickerOpen, pickerScroll) == alphaRowIndex &&
        !pickerOpen && pickerScroll == 0,
        "picker cancel closes cleanly and restores focus to the same extension row", true);

    check(settingsDefaultAppsTabsY(82) == 150 && settingsDefaultAppsTabsHeight(true) == 26 &&
        settingsDefaultAppsListTop(82, true) == 220 &&
        settingsDefaultAppsVisibleRows(220, 456, 48) == 4,
        "640x480 compact layout keeps tabs, status and four reachable extension rows in bounds", true);
    check(settingsDefaultAppsPickerTop(82, true) == 228 &&
        settingsDefaultAppsPickerRowsTop(82, true) == 250 &&
        settingsDefaultAppsVisibleRows(250, 416, 28) == 5 &&
        426 + 30 == 456,
        "640x480 picker keeps handler rows, Restore and Cancel controls reachable", true);
    check(settingsDefaultAppsMouseRowIndex(221, 220, 456, 48, 0, 10) == 0 &&
        settingsDefaultAppsMouseRowIndex(220 + 3 * 48 + 4, 220, 456, 48, 0, 10) == 3 &&
        settingsDefaultAppsMouseRowIndex(456, 220, 456, 48, 0, 10) == -1,
        "mouse hit testing selects only visible extension rows and ignores the viewport edge", true);
    check(nextDefaultAppsRowIndex(0, 5, -1) == 0 && nextDefaultAppsRowIndex(3, 5, 1) == 4 &&
        nextDefaultAppsRowIndex(4, 5, 1) == 4,
        "keyboard row navigation remains stable at first and last entries", true);
    const DefaultAppsPickerFocus f0{ DefaultAppsPickerFocusKind::Handler, 0 };
    const DefaultAppsPickerFocus f2 = nextDefaultAppsPickerFocus(f0, 3, true, 2);
    const DefaultAppsPickerFocus restore = nextDefaultAppsPickerFocus(f2, 3, true, 1);
    const DefaultAppsPickerFocus cancel = nextDefaultAppsPickerFocus(restore, 3, true, 1);
    const DefaultAppsPickerFocus restoreBack = nextDefaultAppsPickerFocus(cancel, 3, true, -1);
    check(f2.kind == DefaultAppsPickerFocusKind::Handler && f2.handlerIndex == 2 &&
        restore.kind == DefaultAppsPickerFocusKind::Restore &&
        cancel.kind == DefaultAppsPickerFocusKind::Cancel &&
        restoreBack.kind == DefaultAppsPickerFocusKind::Restore,
        "keyboard navigation reaches handlers, Restore, Cancel and returns without losing focus", true);
    const DefaultAppsPickerFocus emptyPicker = nextDefaultAppsPickerFocus(
        DefaultAppsPickerFocus{ DefaultAppsPickerFocusKind::Cancel, 0 }, 0, false, -1);
    check(emptyPicker.kind == DefaultAppsPickerFocusKind::Cancel,
        "zero-handler picker remains keyboard-cancelable without a fake choice", true);
    check(settingsDefaultAppsScrollToInclude(0, 31, 32, 4) == 28 &&
        settingsDefaultAppsMouseRowIndex(220, 220, 456, 48, 28, 32) == 28 &&
        settingsDefaultAppsMouseRowIndex(220 + 3 * 48, 220, 456, 48, 28, 32) == 31,
        "extension scrolling reaches the bounded final row with stable selection", true);

    RegistryBackend unavailableBackend(registry);
    unavailableBackend.replaceKnownExtensions = true;
    unavailableBackend.extraExtensions.clear();
    DefaultAppsModel emptyModel;
    emptyModel.refresh(unavailableBackend);
    check(emptyModel.snapshot().available && emptyModel.snapshot().count == 0 &&
        emptyModel.snapshot().totalCount == 0,
        "empty known-extension enumeration yields a stable empty page");
    unavailableBackend.serviceAvailable = false;
    emptyModel.refresh(unavailableBackend);
    check(!emptyModel.snapshot().available && emptyModel.snapshot().count == 0,
        "unavailable default-policy service yields no fabricated host-default state");

    RegistryBackend capacityBackend(registry);
    for (int index = 0; index < 40; ++index) {
        char suffix[4]{};
        std::snprintf(suffix, sizeof(suffix), "%02d", index);
        capacityBackend.extraExtensions.push_back(std::string(".x") + suffix);
    }
    DefaultAppsModel capacityModel;
    capacityModel.refresh(capacityBackend);
    check(capacityModel.snapshot().count == kMaxSettingsDefaultAppExtensions &&
        capacityModel.snapshot().totalCount > capacityModel.snapshot().count &&
        capacityModel.snapshot().truncated,
        "extension-row capacity reports truncation without aliasing or overflow");

    const std::vector<size_t> txtChoices = eligibleDefaultAppHandlerIndices(*txt, backend);
    size_t notepadChoice = txtChoices.size();
    size_t textEditorChoice = txtChoices.size();
    for (size_t i = 0; i < txtChoices.size(); ++i) {
        const std::string& id = txt->handlers.handlers[txtChoices[i]].appId;
        if (id == "gxos.builtin.notepad") notepadChoice = i;
        if (id == textEditor.manifest.id) textEditorChoice = i;
    }
    const DefaultHandlerMutationResult noRedundant = model.chooseHandler(backend, ".txt", notepadChoice);
    check(noRedundant.succeeded() && registry.GetDefaultHandlerStoreDiagnostics().overrideCount == 0 &&
        !std::filesystem::exists(storePath),
        "selecting the unconfigured built-in default is a no-op and writes no redundant override");
    const DefaultHandlerMutationResult setTextEditor = model.chooseHandler(backend, ".txt", textEditorChoice);
    const DefaultAppsRow* configuredText = findDefaultAppsRow(model.snapshot(), ".txt");
    check(setTextEditor.succeeded() && configuredText &&
        configuredText->policy.configuredOverrideAppId == textEditor.manifest.id &&
        configuredText->policy.effectiveDefaultAppId == textEditor.manifest.id,
        "Settings model mutation stores and rereads the selected canonical App ID");
    check(registry.SetTestDocumentActivationBackend(textEditor.manifest.id, false),
        "synthetic text editor backend can be made temporarily unavailable");
    model.refresh(backend);
    configuredText = findDefaultAppsRow(model.snapshot(), ".txt");
    check(configuredText && configuredText->policy.configuredOverrideAppId == textEditor.manifest.id &&
        configuredText->policy.configuredStatus == ConfiguredDefaultHandlerStatus::TemporarilyUnavailable &&
        configuredText->policy.effectiveDefaultAppId == "gxos.builtin.notepad" &&
        configuredText->effectiveDisplayName == "Notepad" &&
        configuredText->configuredDisplayName == "Text Editor",
        "unavailable configured handler stays visible while the effective fallback is Notepad");
    const DefaultHandlerMutationResult clearUnavailable = model.restoreBuiltInDefault(backend, ".txt");
    configuredText = findDefaultAppsRow(model.snapshot(), ".txt");
    check(clearUnavailable.succeeded() && configuredText &&
        configuredText->policy.configuredOverrideAppId.empty() &&
        configuredText->policy.effectiveDefaultAppId == "gxos.builtin.notepad",
        "Restore default clears an unavailable override and rereads the built-in fallback");
    check(registry.SetTestDocumentActivationBackend(textEditor.manifest.id, true),
        "synthetic text editor backend returns to its available state");

    const DefaultAppsRow* testtxt = findDefaultAppsRow(model.snapshot(), ".testtxt");
    const int currentBetaChoice = testtxt ? eligibleChoiceFor(*testtxt, backend, beta.manifest.id) : -1;
    const DefaultHandlerMutationResult chooseBeta = currentBetaChoice >= 0
        ? model.chooseHandler(backend, ".testtxt", static_cast<size_t>(currentBetaChoice))
        : DefaultHandlerMutationResult{};
    check(currentBetaChoice >= 0 && chooseBeta.succeeded() &&
        registry.GetDefaultHandlerInfo(".testtxt").configuredOverrideAppId == beta.manifest.id &&
        registry.GetDefaultHandlerInfo(".testtxt").effectiveDefaultAppId == beta.manifest.id,
        "duplicate-label picker selection persists Editor B by canonical ID");
    const FileAssociationResolution ordinary = registry.ResolveFileAssociation("/docs/model.testtxt");
    const DocumentHandlerList afterChoice = registry.EnumerateCapableHandlers(".testtxt");
    auto alphaHandler = std::find_if(afterChoice.handlers.begin(), afterChoice.handlers.begin() + afterChoice.count,
        [&](const DocumentHandlerInfo& info) { return info.appId == alpha.manifest.id; });
    const FileAssociationResolution explicitAlpha = alphaHandler == afterChoice.handlers.begin() + afterChoice.count
        ? FileAssociationResolution{}
        : registry.ResolveDocumentActivation(*alphaHandler, "/docs/model.testtxt");
    check(ordinary.launchable() && ordinary.activation.appId == beta.manifest.id &&
        explicitAlpha.launchable() && explicitAlpha.activation.appId == alpha.manifest.id &&
        registry.GetDefaultHandlerInfo(".testtxt").effectiveDefaultAppId == beta.manifest.id,
        "ordinary Open observes B while one-time Open With can still activate A without changing policy");

    AppRegistry reloaded(false, storePath);
    bool reloadedApps = addBuiltIns(reloaded) && add(reloaded, alpha) && add(reloaded, beta) &&
        add(reloaded, textEditor) && add(reloaded, one) && add(reloaded, longName) &&
        add(reloaded, longExtensionHandler);
    for (int index = 0; index < 20; ++index) {
        char suffix[4]{};
        std::snprintf(suffix, sizeof(suffix), "%02d", index);
        reloadedApps = reloadedApps && add(reloaded,
            handler(std::string("app.test.handler.") + suffix, "Handler " + std::to_string(index), { ".many" }));
    }
    for (int index = 0; index < 3; ++index)
        reloadedApps = reloadedApps && add(reloaded,
            handler("app.test.multi." + std::to_string(index), "Multi " + std::to_string(index), { ".multi" }));
    RegistryBackend reloadedBackend(reloaded);
    DefaultAppsModel reloadedModel;
    reloadedModel.refresh(reloadedBackend);
    const DefaultAppsRow* reloadedTesttxt = findDefaultAppsRow(reloadedModel.snapshot(), ".testtxt");
    check(reloadedApps && reloadedTesttxt &&
        reloadedTesttxt->policy.configuredOverrideAppId == beta.manifest.id &&
        reloadedTesttxt->policy.effectiveDefaultAppId == beta.manifest.id,
        "configured canonical selection survives Settings model refresh and AppRegistry owner reload");

    const DefaultHandlerMutationResult restoreTesttxt = reloadedModel.restoreBuiltInDefault(
        reloadedBackend, ".testtxt");
    reloadedTesttxt = findDefaultAppsRow(reloadedModel.snapshot(), ".testtxt");
    check(restoreTesttxt.succeeded() && reloadedTesttxt &&
        reloadedTesttxt->policy.configuredOverrideAppId.empty() &&
        reloadedTesttxt->policy.effectiveDefaultAppId == alpha.manifest.id,
        "Restore default removes the override and returns the extension to its normal fallback");

    check(reloaded.SetDefaultHandler(".testtxt", beta.manifest.id).succeeded(),
        "stale-registration fixture persists the original canonical handler B");
    AppRegistry reused(false, storePath);
    bool reusedApps = addBuiltIns(reused) && add(reused, alpha) &&
        add(reused, handler("app.test.charlie", "Editor", { ".testtxt", ".multi" }));
    RegistryBackend reusedBackend(reused);
    DefaultAppsModel reusedModel;
    reusedModel.refresh(reusedBackend);
    const DefaultAppsRow* reusedRow = findDefaultAppsRow(reusedModel.snapshot(), ".testtxt");
    check(reusedApps && reusedRow && reusedRow->policy.configuredOverrideAppId == beta.manifest.id &&
        reusedRow->policy.configuredStatus == ConfiguredDefaultHandlerStatus::RegistrationMissing &&
        reusedRow->policy.effectiveDefaultAppId == alpha.manifest.id &&
        reusedRow->configuredDisplayName.empty(),
        "a reused registration position cannot redirect stale B to C or fabricate B's display name");
    const DefaultHandlerMutationResult clearStale = reusedModel.restoreBuiltInDefault(reusedBackend, ".testtxt");
    reusedRow = findDefaultAppsRow(reusedModel.snapshot(), ".testtxt");
    check(clearStale.succeeded() && reusedRow && reusedRow->policy.configuredOverrideAppId.empty() &&
        reusedRow->policy.effectiveDefaultAppId == alpha.manifest.id,
        "Restore default clears a stale canonical ID after handler disappearance");

    DefaultAppsModel stressModel;
    stressModel.refresh(reloadedBackend);
    bool stressOk = true;
    size_t stressCycles = 0;
    for (size_t cycle = 0; cycle < 100; ++cycle) {
        stressModel.refresh(reloadedBackend);
        const int rowIndex = findDefaultAppsRowIndex(stressModel.snapshot(), ".testtxt");
        std::string selected;
        bool open = false;
        int scroll = 0;
        if (rowIndex < 0 || !openDefaultAppsPicker(stressModel.snapshot(),
                static_cast<size_t>(rowIndex), selected, open, scroll) || !open || selected != ".testtxt") {
            stressOk = false;
            continue;
        }
        const DefaultAppsRow* row = findDefaultAppsRow(stressModel.snapshot(), selected);
        const int choice = row ? eligibleChoiceFor(*row, reloadedBackend, beta.manifest.id) : -1;
        if (choice < 0 || !stressModel.chooseHandler(reloadedBackend, selected,
                static_cast<size_t>(choice)).succeeded()) stressOk = false;
        row = findDefaultAppsRow(stressModel.snapshot(), selected);
        if (!row || row->policy.configuredOverrideAppId != beta.manifest.id ||
            row->policy.effectiveDefaultAppId != beta.manifest.id) stressOk = false;
        if (!stressModel.restoreBuiltInDefault(reloadedBackend, selected).succeeded()) stressOk = false;
        if (closeDefaultAppsPicker(stressModel.snapshot(), selected, open, scroll) != rowIndex || open) stressOk = false;
        ++stressCycles;
    }
    check(stressOk && stressCycles == 100,
        "100 repeated open, select, refresh, restore and close cycles keep identity and capacity stable");

    check(registry.GetDefaultHandlerInfo(".txt").configuredOverrideAppId.empty() &&
        registry.GetDefaultHandlerInfo(".txt").effectiveDefaultAppId == "gxos.builtin.notepad" &&
        registry.GetDefaultHandlerInfo(".log").effectiveDefaultAppId == "gxos.builtin.notepad" &&
        registry.GetDefaultHandlerInfo(".ini").effectiveDefaultAppId == "gxos.builtin.notepad" &&
        registry.GetDefaultHandlerInfo(".cfg").effectiveDefaultAppId == "gxos.builtin.notepad",
        "synthetic policy tests leave all four production defaults at Notepad");

    std::cout << "settingsDefaultAppsModelChecks=" << (modelChecks - modelFailures) << "/" << modelChecks << " passed\n";
    std::cout << "settingsDefaultAppsInteractionChecks=" << (interactionChecks - interactionFailures)
        << "/" << interactionChecks << " passed\n";
    std::cout << "defaultAppsLifecycleCycles=" << stressCycles << "/100\n";
    std::cout << "failures=" << failures << "\n";
    std::error_code cleanupError;
    std::filesystem::remove_all(tempRoot, cleanupError);
    if (cleanupError) std::cout << "cleanupError=" << cleanupError.message() << "\n";
    return failures == 0 ? 0 : 1;
}
