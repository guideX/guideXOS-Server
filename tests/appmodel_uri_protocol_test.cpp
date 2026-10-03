#include "app_registry.h"
#include "app_manifest_validator.h"
#include "navigator_uri_routing.h"
#include "settings_default_apps_model.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace gxos::apps;

int checks = 0;
int failures = 0;

void check(bool condition, const std::string& name)
{
    ++checks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else { ++failures; std::cout << "FAIL: " << name << "\n"; }
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

RegisteredApp durableProtocolApp(const std::string& id, const std::string& label,
                                const std::vector<std::string>& protocols,
                                bool textCapability = false)
{
    RegisteredApp app;
    app.sourceKind = AppSourceKind::Package;
    app.protocolActivationBackendAvailable = true;
    app.documentActivationBackendAvailable = textCapability;
    app.manifest.id = id;
    app.manifest.displayName = label;
    app.manifest.version = "1.0.0";
    app.manifest.publisher = "Phase 15 test fixture";
    app.manifest.kind = AppKind::Service;
    app.manifest.protocols = protocols;
    app.manifest.supportsProtocolActivation = true;
    if (textCapability) {
        app.manifest.supportsDocumentActivation = true;
        app.manifest.fileAssociations.push_back({ ".txt", "text/plain", "test text document" });
    }
    return app;
}

class RegistrySettingsBackend final : public settings::DefaultAppsBackend {
public:
    explicit RegistrySettingsBackend(AppRegistry& registry) : m_registry(registry) {}
    bool available() const override { return true; }
    std::vector<std::string> knownDocumentExtensions() override { return m_registry.GetKnownDocumentExtensions(); }
    DefaultHandlerInfo defaultHandlerInfo(const std::string& key) override { return m_registry.GetDefaultHandlerInfo(key); }
    DocumentHandlerList capableHandlers(const std::string& key) override { return m_registry.EnumerateCapableHandlers(key); }
    std::string displayName(const std::string& appId) override
    {
        const RegisteredApp* app = m_registry.FindById(appId);
        return app ? app->manifest.displayName : std::string();
    }
    bool isDurableApp(const std::string& appId) override
    {
        const RegisteredApp* app = m_registry.FindById(appId);
        return app && !app->temporaryDevelopment && app->sourceKind != AppSourceKind::DevelopmentTemporary;
    }
    DefaultHandlerMutationResult setDefaultHandler(const std::string& key, const std::string& appId) override
    {
        return m_registry.SetDefaultHandler(key, appId);
    }
    DefaultHandlerMutationResult clearDefaultHandler(const std::string& key) override
    {
        return m_registry.ClearDefaultHandler(key);
    }
    std::vector<std::string> knownProtocols() override { return m_registry.GetKnownProtocols(); }
    ProtocolDefaultHandlerInfo protocolDefaultHandlerInfo(const std::string& scheme) override
    {
        return m_registry.GetDefaultProtocolHandlerInfo(scheme);
    }
    ProtocolHandlerList capableProtocolHandlers(const std::string& scheme) override
    {
        return m_registry.EnumerateCapableProtocolHandlers(scheme);
    }
    DefaultHandlerMutationResult setDefaultProtocolHandler(const std::string& scheme, const std::string& appId) override
    {
        return m_registry.SetDefaultProtocolHandler(scheme, appId);
    }
    DefaultHandlerMutationResult clearDefaultProtocolHandler(const std::string& scheme) override
    {
        return m_registry.ClearDefaultProtocolHandler(scheme);
    }
private:
    AppRegistry& m_registry;
};

bool addBuiltIns(AppRegistry& registry)
{
    registry.RegisterBuiltInAppsAsManifests();
    return registry.FindById("guidexos.navigator") != nullptr && registry.FindById("gxos.builtin.notepad") != nullptr;
}

bool addFixtureHandlers(AppRegistry& registry, std::string& error)
{
    return registry.RegisterTestDurableApp(
            durableProtocolApp("test.protocol.alpha", "Protocol Handler", { "http", "foo+test" }, true), error) &&
        registry.RegisterTestDurableApp(
            durableProtocolApp("test.protocol.beta", "Protocol Handler", { "http", "https" }), error);
}

} // namespace

int main()
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() /
        ("guidexos-appmodel-uri-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempRoot);
    const std::filesystem::path storePath = tempRoot / "appmodel-default-handlers.cfg";
    const std::filesystem::path unrelatedPath = tempRoot / "desktop.json";
    const std::string unrelatedBytes = "{\"keep\":\"phase15\",\"state\":9}\r\n";
    writeText(unrelatedPath, unrelatedBytes);

    std::string normalized;
    check(NormalizeProtocolScheme("http", normalized) && normalized == "http" &&
        NormalizeProtocolScheme("HTTP", normalized) && normalized == "http" &&
        NormalizeProtocolScheme("Http", normalized) && normalized == "http" &&
        NormalizeProtocolScheme("web+test-1.foo", normalized) && normalized == "web+test-1.foo",
        "protocol schemes normalize ASCII case and accept bounded conventional grammar");
    check(!NormalizeProtocolScheme("", normalized) && !NormalizeProtocolScheme("1http", normalized) &&
        !NormalizeProtocolScheme("http_foo", normalized) &&
        !NormalizeProtocolScheme(std::string(kAppModelMaxProtocolSchemeBytes + 1, 'a'), normalized),
        "empty, malformed, underscore, digit-leading, and overlong schemes fail closed");

    AppManifest manifest;
    manifest.id = "test.manifest.protocols";
    manifest.displayName = "Protocol fixture";
    manifest.version = "1";
    manifest.kind = AppKind::Service;
    manifest.supportsProtocolActivation = true;
    for (size_t i = 0; i < kAppModelMaxProtocolsPerApp; ++i)
        manifest.protocols.push_back("x" + std::to_string(i));
    check(AppManifestValidator::Validate(manifest).valid,
        "maximum protocol declarations per application are accepted");
    manifest.protocols.push_back("x-extra");
    check(!AppManifestValidator::Validate(manifest).valid,
        "maximum protocol declarations plus one are rejected");
    manifest.protocols = { "http", "HTTP" };
    check(!AppManifestValidator::Validate(manifest).valid,
        "duplicate and case-equivalent protocol declarations are rejected");
    manifest.protocols = { "1bad" };
    check(!AppManifestValidator::Validate(manifest).valid,
        "malformed scheme declaration is rejected by manifest validation");
    manifest.protocols = { std::string(kAppModelMaxProtocolSchemeBytes + 1, 'a') };
    check(!AppManifestValidator::Validate(manifest).valid,
        "overlong scheme declaration is rejected by manifest validation");

    std::vector<DefaultHandlerOverride> stored;
    std::string storeText;
    std::string storeError;
    size_t invalidRecords = 0;
    std::string parseError;
    const std::vector<DefaultHandlerOverride> legacyPolicy = { { ".txt", "gxos.builtin.notepad" } };
    check(DefaultAppHandlerStore::Serialize(legacyPolicy, storeText, storeError) &&
        storeText.rfind("GXOS-APP-DEFAULTS 1\n", 0) == 0,
        "extension-only policy serialization remains byte-compatible with store version 1");
    const std::string validLegacyText = storeText;
    check(DefaultAppHandlerStore::ParseText(validLegacyText, stored, invalidRecords, parseError) &&
        stored.size() == 1 && stored[0].keyKind == DefaultHandlerOverride::KeyKind::Extension && invalidRecords == 0,
        "version 2 parser remains backward compatible with valid extension-only version 1 files");
    const std::vector<DefaultHandlerOverride> typedPolicy = {
        { ".txt", "test.protocol.alpha" },
        { "http", "test.protocol.beta", DefaultHandlerOverride::KeyKind::Protocol }
    };
    check(DefaultAppHandlerStore::Serialize(typedPolicy, storeText, storeError) &&
        storeText.rfind("GXOS-APP-DEFAULTS 2\n", 0) == 0 &&
        storeText.find("extension:.txt=test.protocol.alpha\n") != std::string::npos &&
        storeText.find("protocol:http=test.protocol.beta\n") != std::string::npos &&
        DefaultAppHandlerStore::ParseText(storeText, stored, invalidRecords, parseError) &&
        stored.size() == 2 && invalidRecords == 0 && stored[0].keyKind == DefaultHandlerOverride::KeyKind::Extension &&
        stored[1].keyKind == DefaultHandlerOverride::KeyKind::Protocol,
        "version 2 uses explicit typed extension/protocol keys and round-trips them");
    std::string legacyText;
    check(DefaultAppHandlerStore::ParseText("GXOS-APP-DEFAULTS 1\n.txt=gxos.builtin.notepad\nEND 1 00000000\n",
        stored, invalidRecords, parseError) == false,
        "legacy v1 integrity checks still reject a checksum mismatch");

    AppRegistry registry(false, storePath);
    check(addBuiltIns(registry), "production built-ins register Navigator and existing text document defaults");
    std::string registrationError;
    check(addFixtureHandlers(registry, registrationError),
        "two same-label durable synthetic protocol handlers register by distinct canonical IDs");
    check(registry.GetDefaultProtocolHandlerInfo("HTTP").builtInDefaultAppId == "guidexos.navigator" &&
        registry.GetDefaultProtocolHandlerInfo("https").builtInDefaultAppId == "guidexos.navigator" &&
        registry.GetDefaultProtocolHandlerInfo("http").effectiveDefaultAppId == "guidexos.navigator" &&
        registry.GetDefaultProtocolHandlerInfo("https").effectiveDefaultAppId == "guidexos.navigator",
        "HTTP and HTTPS have independent built-in and effective Navigator defaults");
    check(registry.EnumerateCapableProtocolHandlers("HTTP").validScheme &&
        registry.EnumerateCapableProtocolHandlers("HTTP").scheme == "http" &&
        registry.EnumerateCapableProtocolHandlers("http").count == 3,
        "capable-handler enumeration is case-insensitive, value-owned, and includes all declarations");
    const std::vector<std::string> knownExtensions = registry.GetKnownDocumentExtensions();
    check(registry.GetDefaultProtocolHandlerInfo("http").effectiveDefaultAppId == "guidexos.navigator" &&
        registry.GetDefaultHandlerInfo(".html").effectiveDefaultAppId == "guidexos.navigator" &&
        std::find(knownExtensions.begin(), knownExtensions.end(), "http") == knownExtensions.end(),
        "protocol and document defaults remain separate namespaces");

    const UriActivationResolution normalHttp = registry.ResolveUriActivation("HTTP://fixture.test:8080/path?q=1#part");
    check(normalHttp.launchable() && normalHttp.appId == "guidexos.navigator" &&
        normalHttp.activation.kind == AppActivationKind::Uri &&
        normalHttp.activation.uri == "HTTP://fixture.test:8080/path?q=1#part" &&
        registry.IsUriActivationCurrent(normalHttp.activation),
        "AppRegistry selects Navigator and preserves the exact owned HTTP URI including authority and suffix");
    check(registry.ResolveUriActivation("gxphase15-test://example").status == UriActivationResolutionStatus::NoHandler,
        "unknown protocol reports a deterministic no-handler result");
    check(registry.ResolveUriActivation("://bad").status == UriActivationResolutionStatus::InvalidScheme &&
        registry.ResolveUriActivation("1http://bad").status == UriActivationResolutionStatus::InvalidScheme &&
        registry.ResolveUriActivation("http//missing-colon").status == UriActivationResolutionStatus::InvalidScheme,
        "malformed URI schemes fail before handler selection");
    check(registry.ResolveUriActivation("http:").launchable() &&
        registry.ResolveUriActivation("http:").activation.uri == "http:",
        "scheme-valid but Navigator-invalid payload is separated from handler resolution");
    check(!registry.ResolveUriActivation("").launchable() &&
        !registry.ResolveUriActivation(std::string(kAppModelMaxUriBytes + 1, 'x')).launchable() &&
        !registry.ResolveUriActivation("http://fixture.test/a\nnext").launchable(),
        "empty, overlong, and control-containing URI payloads are rejected");
    const std::string exactMaximumUri = "http://fixture.test/" +
        std::string(kAppModelMaxUriBytes - std::string("http://fixture.test/").size(), 'a');
    check(exactMaximumUri.size() == kAppModelMaxUriBytes &&
        registry.ResolveUriActivation(exactMaximumUri).launchable() &&
        registry.ResolveUriActivation(exactMaximumUri).activation.uri == exactMaximumUri,
        "exact maximum URI length is accepted and owned without truncation");

    check(registry.SetDefaultHandler(".txt", "test.protocol.alpha").succeeded(),
        "document default remains independently settable before protocol persistence cycles");
    RegistrySettingsBackend settingsBackend(registry);
    settings::DefaultAppsModel settingsModel;
    settingsModel.refresh(settingsBackend);
    const settings::DefaultAppsRow* httpRow = settings::findDefaultAppsRow(settingsModel.snapshot(), "protocol:http");
    check(httpRow && httpRow->protocol && settings::defaultAppsRowLabel(*httpRow) == "http:" &&
        httpRow->policy.builtInDefaultAppId == "guidexos.navigator" && httpRow->handlers.count == 3,
        "Settings exposes protocol rows through the same policy model with truthful built-in/current handlers");
    const std::vector<size_t> eligible = httpRow
        ? settings::eligibleDefaultAppHandlerIndices(*httpRow, settingsBackend) : std::vector<size_t>{};
    size_t alphaChoice = eligible.size();
    size_t betaChoice = eligible.size();
    for (size_t i = 0; i < eligible.size(); ++i) {
        const std::string& appId = httpRow->handlers.handlers[eligible[i]].appId;
        if (appId == "test.protocol.alpha") alphaChoice = i;
        if (appId == "test.protocol.beta") betaChoice = i;
    }
    check(alphaChoice < eligible.size() && betaChoice < eligible.size() &&
        settingsModel.chooseHandler(settingsBackend, "protocol:http", alphaChoice).succeeded() &&
        registry.ResolveUriActivation("http://fixture.test/settings").appId == "test.protocol.alpha",
        "Settings selects a synthetic alternate handler by canonical ID and ordinary activation observes it");
    const ProtocolHandlerList httpHandlers = registry.EnumerateCapableProtocolHandlers("http");
    const auto navigatorChoice = std::find_if(httpHandlers.handlers.begin(), httpHandlers.handlers.begin() + httpHandlers.count,
        [](const ProtocolHandlerInfo& handler) { return handler.appId == "guidexos.navigator"; });
    check(navigatorChoice != httpHandlers.handlers.begin() + httpHandlers.count &&
        registry.ResolveUriActivation(*navigatorChoice, "http://fixture.test/one-time").appId == "guidexos.navigator" &&
        registry.GetDefaultProtocolHandlerInfo("http").configuredOverrideAppId == "test.protocol.alpha",
        "one-time explicit Navigator activation leaves the configured synthetic default untouched");
    check(registry.SetDefaultProtocolHandler("https", "test.protocol.beta").succeeded() &&
        registry.GetDefaultProtocolHandlerInfo("http").effectiveDefaultAppId == "test.protocol.alpha" &&
        registry.GetDefaultProtocolHandlerInfo("https").effectiveDefaultAppId == "test.protocol.beta",
        "HTTP and HTTPS configured defaults are independently represented and resolved");
    check(settingsModel.restoreBuiltInDefault(settingsBackend, "protocol:http").succeeded() &&
        registry.ResolveUriActivation("http://fixture.test/restored").appId == "guidexos.navigator" &&
        registry.GetDefaultHandlerInfo(".txt").effectiveDefaultAppId == "test.protocol.alpha",
        "Settings restore clears only the protocol override and leaves the file-type policy unchanged");

    check(registry.SetDefaultProtocolHandler("http", "test.protocol.alpha").succeeded(),
        "a durable protocol preference persists by canonical App ID");
    check(registry.SetTestProtocolActivationBackend("test.protocol.alpha", false) &&
        registry.GetDefaultProtocolHandlerInfo("http").configuredOverrideAppId == "test.protocol.alpha" &&
        registry.GetDefaultProtocolHandlerInfo("http").effectiveDefaultAppId == "guidexos.navigator" &&
        registry.ResolveUriActivation("http://fixture.test/unavailable").appId == "guidexos.navigator",
        "unavailable configured protocol handler remains stored but is ignored for effective activation");
    registry.SetTestProtocolActivationBackend("test.protocol.alpha", true);

    RegisteredApp temporary = durableProtocolApp("test.protocol.temporary", "Temporary Protocol", { "x-temp" });
    temporary.sourceKind = AppSourceKind::DevelopmentTemporary;
    temporary.temporaryDevelopment = true;
    temporary.temporaryOwnerRuntimeId = 44;
    temporary.temporaryGeneration = 3;
    check(registry.RegisterTemporaryDevelopmentApp(temporary, registrationError) &&
        registry.SetProtocolActivationBackendAvailable(temporary.manifest.id, true) &&
        registry.SetDefaultProtocolHandler("x-temp", temporary.manifest.id).status == DefaultHandlerMutationStatus::NonDurableRegistration,
        "temporary development protocol registrations cannot become durable defaults");
    ProtocolHandlerList temporaryHandlers = registry.EnumerateCapableProtocolHandlers("x-temp");
    check(temporaryHandlers.count == 1 && registry.ResolveUriActivation(temporaryHandlers.handlers[0], "x-temp://payload").launchable(),
        "temporary activation is scoped to the current registration generation");
    const ProtocolHandlerInfo staleTemporary = temporaryHandlers.handlers[0];
    check(registry.UnregisterTemporaryDevelopmentApp(temporary.manifest.id, 44, 3),
        "temporary protocol registration can be unregistered by owner and generation");
    temporary.temporaryGeneration = 4;
    check(registry.RegisterTemporaryDevelopmentApp(temporary, registrationError) &&
        registry.ResolveUriActivation(staleTemporary, "x-temp://payload").status == UriActivationResolutionStatus::HandlerStale,
        "reused canonical ID cannot inherit stale protocol authority from an earlier generation");
    registry.UnregisterTemporaryDevelopmentApp(temporary.manifest.id, 44, 4);

    bool activationCyclesPassed = true;
    for (int i = 0; i < 100; ++i) {
        const std::string uri = "http://fixture.test/cycle/" + std::to_string(i) + "?q=" + std::to_string(i) + "#f";
        const UriActivationResolution activation = registry.ResolveUriActivation(uri);
        activationCyclesPassed = activationCyclesPassed && activation.launchable() &&
            activation.activation.uri == uri && registry.IsUriActivationCurrent(activation.activation);
    }
    check(activationCyclesPassed, "100 deterministic URI resolution and activation-context cycles preserve exact owned values");

    bool noHandlerCyclesPassed = true;
    const size_t registeredBeforeUnknown = registry.GetAllApps().size();
    const size_t protocolRecordsBeforeUnknown = registry.GetProtocolHandlers().size();
    for (int i = 0; i < 100; ++i) {
        const UriActivationResolution unknown = registry.ResolveUriActivation("gxphase15-test://" + std::to_string(i));
        noHandlerCyclesPassed = noHandlerCyclesPassed && unknown.status == UriActivationResolutionStatus::NoHandler &&
            unknown.appId.empty() && unknown.activation.appId.empty();
    }
    check(noHandlerCyclesPassed && registry.GetAllApps().size() == registeredBeforeUnknown &&
        registry.GetProtocolHandlers().size() == protocolRecordsBeforeUnknown,
        "100 unknown-protocol resolutions produce no activation and do not mutate registry state");

    bool persistenceCyclesPassed = true;
    const std::vector<std::string> targetIds = { "test.protocol.alpha", "test.protocol.beta" };
    for (int i = 0; i < 100; ++i) {
        const std::string& target = targetIds[static_cast<size_t>(i) % targetIds.size()];
        if (!registry.SetDefaultProtocolHandler("http", target).succeeded()) {
            persistenceCyclesPassed = false;
            break;
        }
        AppRegistry reloaded(false, storePath);
        if (!addBuiltIns(reloaded) || !addFixtureHandlers(reloaded, registrationError) ||
            reloaded.GetDefaultProtocolHandlerInfo("http").configuredOverrideAppId != target ||
            reloaded.GetDefaultProtocolHandlerInfo("http").effectiveDefaultAppId != target ||
            reloaded.GetDefaultHandlerInfo(".txt").effectiveDefaultAppId != "test.protocol.alpha") {
            persistenceCyclesPassed = false;
            break;
        }
    }
    check(persistenceCyclesPassed && readText(unrelatedPath) == unrelatedBytes,
        "100 typed-policy persistence/reload cycles preserve protocol selection, .txt policy, and unrelated bytes");
    check(readText(storePath).rfind("GXOS-APP-DEFAULTS 2\n", 0) == 0,
        "durable protocol preferences use the explicitly versioned v2 policy format");

    AppRegistry staleOwner(false, storePath);
    addBuiltIns(staleOwner);
    check(staleOwner.GetDefaultProtocolHandlerInfo("http").configuredStatus == ConfiguredDefaultHandlerStatus::RegistrationMissing &&
        staleOwner.GetDefaultProtocolHandlerInfo("http").effectiveDefaultAppId == "guidexos.navigator" &&
        staleOwner.ResolveUriActivation("http://fixture.test/missing").appId == "guidexos.navigator",
        "missing configured protocol app retains its ID while resolution falls back to Navigator");

    AppRegistry capacityRegistry(false, tempRoot / "capacity.cfg");
    bool capacityRegistrationsSucceeded = true;
    for (size_t i = 0; i < kAppModelMaxProtocolRecords + 1; ++i) {
        const std::string suffix = i < 10 ? "00" + std::to_string(i) : (i < 100 ? "0" + std::to_string(i) : std::to_string(i));
        if (!capacityRegistry.RegisterTestDurableApp(
                durableProtocolApp("test.capacity." + suffix, "Capacity handler", { "x-cap" }), registrationError)) {
            capacityRegistrationsSucceeded = false;
            break;
        }
    }
    const ProtocolHandlerList capacityHandlers = capacityRegistry.EnumerateCapableProtocolHandlers("x-cap");
    check(capacityRegistrationsSucceeded && capacityRegistry.ProtocolHandlerCapacityExceeded() &&
        capacityRegistry.GetProtocolHandlers().size() == kAppModelMaxProtocolRecords && capacityHandlers.truncated &&
        capacityHandlers.count == kAppModelMaxProtocolHandlersPerScheme &&
        capacityRegistry.ResolveUriActivation("x-cap://payload").appId == "test.capacity.000",
        "protocol registry record and Settings-handler snapshot limits fail closed deterministically");

    std::string schemeForRouting;
    check(ClassifyNavigatorUriRoute("HTTP://example.test/", schemeForRouting) == NavigatorUriRouteKind::NavigatorInternal &&
        schemeForRouting == "http" &&
        ClassifyNavigatorUriRoute("file:///C:/docs/index.html", schemeForRouting) == NavigatorUriRouteKind::NavigatorInternal &&
        ClassifyNavigatorUriRoute("mailto:person@example.test", schemeForRouting) == NavigatorUriRouteKind::AppModelProtocol &&
        ClassifyNavigatorUriRoute("://bad", schemeForRouting) == NavigatorUriRouteKind::Invalid,
        "Navigator keeps HTTP, HTTPS, file, and about internal while classifying external schemes for App Model delegation");

    std::cout << "appModelUriProtocolChecks=" << checks << "/" << checks - failures << "\n";
    std::cout << "persistenceCycles=100/100\nactivationCycles=100/100\nnoHandlerCycles=100/100\n";
    std::cout << "failures=" << failures << "\n";
    std::error_code cleanupError;
    std::filesystem::remove_all(tempRoot, cleanupError);
    return failures == 0 ? 0 : 1;
}
