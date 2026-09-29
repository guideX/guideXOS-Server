#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::NavigatorScriptHostLimits;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<!doctype html>
<html><body>
<form id="owner" name="owner">
  <div id="panel" class="panel action primary">
    <button id="save" class="action primary large enabled" type="button">Save</button>
    <div id="content">
      <input id="name" name="name" class="field required" type="text" value="name-seed">
      <input id="email" name="email" class="field urgent" type="text" value="email-seed">
      <button id="cancel" class="action secondary" type="button">Cancel</button>
      <div id="inner">
        <span id="deep" class="primary action"></span>
        <span id="duplicate" class="action action primary"></span>
        <span id="case" class="Action primary"></span>
        <span id="substring" class="action-primary primary"></span>
        <span id="eight"></span>
        <span id="nine"></span>
        <span id="long255"></span>
        <span id="long256"></span>
        <span id="longQuery"></span>
      </div>
    </div>
  </div>
</form>
<input id="outside-owned" name="outside-owned" class="field required" type="text" value="outside-seed">
<button id="outside" class="action primary" type="button">Outside</button>
</body></html>
)HTML";

void expect(bool condition, const std::string& message)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
}

const Value* binding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const std::string key(name);
    return harness.runtime().lookup(
        gxos::javascript::SourceView(key.data(), key.size()));
}

void expectBoolean(const NavigatorScriptExecutionHarness& harness,
    const char* name, bool expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::Boolean, label + ": Boolean");
    if (value->isBoolean())
        expect(value->booleanValue() == expected, label + ": value=" +
            (value->booleanValue() ? "true" : "false"));
}

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": error code");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(harness.loadHtml("file:///js44.html", kFixture, error),
        "fixture: loads");
    expect(error == RuntimeErrorCode::None, "fixture: no load error");
    expect(harness.relayout(), "fixture: relayout");
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0u;
}

gxos::web::HtmlElementRef* elementById(
    NavigatorScriptExecutionHarness& harness, const char* id)
{
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == id) return &element;
    }
    return nullptr;
}

void testAllTokenMatchingScopesAndIdentity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var inner = document.querySelector("#inner");
var multi = document.getElementsByClassName("action primary");
var reversed = document.getElementsByClassName("primary action");
var duplicateQuery = document.getElementsByClassName("action action primary");
var whitespaceQuery = document.getElementsByClassName("  action    primary  ");
var tabQuery = document.getElementsByClassName("action\tprimary");
var lineQuery = document.getElementsByClassName("action\r\nprimary");
var formFeedQuery = document.getElementsByClassName("action\fprimary");
var actions = document.getElementsByClassName("action");
var exactThree = document.getElementsByClassName("action primary large");
var exactFour = document.getElementsByClassName("action primary large enabled");
var missing = document.getElementsByClassName("action missing");
var multiOrder = multi.length === 5 && multi[0] === panel &&
    multi[1] === document.querySelector("#save") &&
    multi[2] === document.querySelector("#deep") &&
    multi[3] === document.querySelector("#duplicate") &&
    multi[4] === document.querySelector("#outside");
var equivalentQueries = multi === reversed && multi === duplicateQuery &&
    multi === whitespaceQuery && multi === tabQuery && multi === lineQuery &&
    multi === formFeedQuery;
var allTokensRequired = missing.length === 0 && exactThree.length === 1 &&
    exactThree[0] === document.querySelector("#save") &&
    exactFour.length === 1 && exactFour[0] === exactThree[0];
var exactAndCase = document.getElementsByClassName("act").length === 0 &&
    multi[0].matches(".action") && multi[0].matches(".primary") &&
    multi[2] === document.querySelector("#deep") &&
    document.querySelector("#case").matches(".primary") &&
    !document.querySelector("#case").matches(".action") &&
    !document.querySelector("#substring").matches(".action") &&
    document.getElementsByClassName("action-primary")[0] ===
        document.querySelector("#substring");
var subsetInvariant = multi[0].matches(".action") && multi[0].matches(".primary") &&
    multi[1].matches(".action") && multi[1].matches(".primary") &&
    multi[2].matches(".action") && multi[2].matches(".primary") &&
    multi[3].matches(".action") && multi[3].matches(".primary") &&
    multi[4].matches(".action") && multi[4].matches(".primary");
var actionOnly = document.querySelectorAll(".action");
var primaryOnly = document.querySelectorAll(".primary");
var querySubsets = multi.length <= actionOnly.length &&
    multi.length <= primaryOnly.length &&
    actionOnly[0] === multi[0] && actionOnly[1] === multi[1] &&
    actionOnly[3] === multi[2] && actionOnly[4] === multi[3] &&
    actionOnly[5] === multi[4] &&
    primaryOnly[0] === multi[0] && primaryOnly[1] === multi[1] &&
    primaryOnly[2] === multi[2] && primaryOnly[3] === multi[3] &&
    primaryOnly[6] === multi[4];
var scoped = panel.getElementsByClassName("action primary");
var strictScope = scoped.length === 3 &&
    scoped[0] === document.querySelector("#save") &&
    scoped[1] === document.querySelector("#deep") &&
    scoped[2] === document.querySelector("#duplicate") &&
    scoped[0] !== panel && scoped[2] !== document.querySelector("#outside") &&
    scoped[3] === undefined;
var nestedScope = inner.getElementsByClassName("action primary");
var nestedReceiverExcluded = nestedScope.length === 2 &&
    nestedScope[0] === document.querySelector("#deep") &&
    nestedScope[1] === document.querySelector("#duplicate");
var traversalAndContains = panel.contains(scoped[0]) && panel.contains(scoped[1]) &&
    panel.contains(scoped[2]) && !panel.contains(document.querySelector("#outside")) &&
    scoped[0].parentElement === panel &&
    scoped[1].parentElement === inner && scoped[1].closest(".panel") === panel &&
    scoped[0].matches("button.action") &&
    scoped[0].nextElementSibling === document.querySelector("#content");
)JS");
    expect(result.succeeded(), "matching/scope: script");
    expectBoolean(harness, "multiOrder", true,
        "matching: document order and superset tokens");
    expectBoolean(harness, "equivalentQueries", true,
        "matching: token order, duplicates, and ASCII whitespace canonicalize");
    expectBoolean(harness, "allTokensRequired", true,
        "matching: all tokens required and extra classes allowed");
    expectBoolean(harness, "exactAndCase", true,
        "matching: exact tokens, substring rejection, and case sensitivity");
    expectBoolean(harness, "subsetInvariant", true,
        "matching: each result matches both single-class selectors");
    expectBoolean(harness, "querySubsets", true,
        "matching: results are subsets of both single-class query sets");
    expectBoolean(harness, "strictScope", true,
        "scope: strict structural descendants exclude receiver and outside");
    expectBoolean(harness, "nestedReceiverExcluded", true,
        "scope: nested Element receiver excluded");
    expectBoolean(harness, "traversalAndContains", true,
        "integration: canonical identity, contains, and Element traversal");
    expect(harness.hostAdapter().limits().maxSelectorCollections ==
            gxos::javascript::kNavigatorScriptMaxSelectorCollections,
        "collections: registry remains at its existing 128-record bound");
}

void testWhitespaceEmptyAndQueryBounds()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::string long255(255u, 'x');
    const std::string long256(256u, 'y');
    const std::string eightTokens = "a b c d e f g h";
    const std::string nineTokens = "a b c d e f g h i";
    const std::string manyRepeated =
        "action action action action action action action action action";
    gxos::web::HtmlElementRef* eight = elementById(harness, "eight");
    gxos::web::HtmlElementRef* nine = elementById(harness, "nine");
    gxos::web::HtmlElementRef* maxToken = elementById(harness, "long255");
    gxos::web::HtmlElementRef* oversizedToken =
        elementById(harness, "long256");
    gxos::web::HtmlElementRef* oversizedQuery =
        elementById(harness, "longQuery");
    expect(eight != nullptr && nine != nullptr && maxToken != nullptr &&
            oversizedToken != nullptr && oversizedQuery != nullptr,
        "bounds: query-bound Elements exist");
    if (eight != nullptr) eight->className = eightTokens;
    if (nine != nullptr) nine->className = nineTokens;
    if (maxToken != nullptr) maxToken->className = long255;
    if (oversizedToken != nullptr) oversizedToken->className = long256;
    if (oversizedQuery != nullptr)
        oversizedQuery->className = long255 + " action";

    const std::string source =
        "var empty = document.getElementsByClassName(\"\");"
        "var whitespaceOnly = document.getElementsByClassName(\" \\t\\r\\n\\f \");"
        "var eight = document.getElementsByClassName(\"" + eightTokens +
        "\");"
        "var nine = document.getElementsByClassName(\"" + nineTokens +
        "\");"
        "var repeated = document.getElementsByClassName(\"" + manyRepeated +
        "\");"
        "var maxToken = document.getElementsByClassName(\"" + long255 +
        "\");"
        "var maxInput = document.getElementsByClassName(\" " + long255 +
        "\");"
        "var oversizedToken = document.getElementsByClassName(\"" + long256 +
        "\");"
        "var oversizedInput = document.getElementsByClassName(\"" +
        long255 + " action\");"
        "var emptyBehavior = empty.length === 0 && whitespaceOnly.length === 0 &&"
        " empty[0] === undefined && whitespaceOnly[0] === undefined;"
        "var tokenCountBound = eight.length === 2 && eight[0].id === \"eight\" &&"
        " eight[1].id === \"nine\" &&"
        " nine.length === 0 && repeated.length === 0;"
        "var tokenLengthBound = maxToken.length === 2 && maxInput === maxToken &&"
        " maxToken[0].id === \"long255\" &&"
        " oversizedToken.length === 0 && oversizedInput.length === 0;";
    const ScriptResult result = harness.execute(source);
    expect(result.succeeded(), "bounds/whitespace: script");
    expectBoolean(harness, "emptyBehavior", true,
        "empty and ASCII-whitespace-only queries stay empty");
    expectBoolean(harness, "tokenCountBound", true,
        "bounds: eight raw tokens accepted; ninth and ninth duplicate rejected");
    expectBoolean(harness, "tokenLengthBound", true,
        "bounds: 255-byte token accepted; 256-byte token rejected");
    const ScriptResult tooLong = harness.execute(
        "var tooLong = document.getElementsByClassName(\"" + long255 +
        "   action\"); var tooLongEmpty = tooLong.length === 0;");
    expect(tooLong.succeeded(), "bounds: oversized input script");
    expectBoolean(harness, "tooLongEmpty", true,
        "bounds: input above 256 bytes fails safely with an empty collection");
    const ScriptResult invalidArguments = harness.execute(R"JS(
var missing = document.getElementsByClassName();
var number = document.getElementsByClassName(123);
var boolean = document.getElementsByClassName(true);
var invalidArgumentsSafe = missing.length === 0 && number.length === 0 &&
    boolean.length === 0 && missing[0] === undefined;
)JS");
    expect(invalidArguments.succeeded(), "invalid inputs: script");
    expectBoolean(harness, "invalidArgumentsSafe", true,
        "invalid inputs: missing and non-string arguments fail closed");
}

void testCollectionsReadOnlyLiveAndCapacity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult setup = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var all = document.getElementsByClassName("action primary");
var actions = document.getElementsByClassName("action");
var fields = panel.getElementsByClassName("field required");
var independent = all.length === 5 && actions.length === 6 &&
    fields.length === 1 && all !== actions && all[0] === panel;
var stable = all === document.getElementsByClassName("primary action") &&
    fields === panel.getElementsByClassName("required field");
var indexedMiss = all[999] === undefined && all.item === undefined &&
    all.namedItem === undefined && all["save"] === undefined;
)JS");
    expect(setup.succeeded(), "collections: setup");
    expectBoolean(harness, "independent", true,
        "collections: simultaneous single, multi, and scoped descriptors");
    expectBoolean(harness, "stable", true,
        "collections: normalized queries reuse stable descriptors");
    expectBoolean(harness, "indexedMiss", true,
        "collections: indexed miss and unsupported collection APIs");
    expectError(harness.execute(
            "all[0] = document.querySelector(\"#outside\");"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "collections: indexed writes remain read-only");
    expectError(harness.execute("all.length = 0;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "collections: length remains read-only");

    gxos::web::HtmlElementRef* deep = elementById(harness, "deep");
    expect(deep != nullptr, "collections: live projection Element exists");
    if (deep != nullptr) deep->className = "primary only";
    const ScriptResult live = harness.execute(
        "var liveRead = all.length === 4 && all[2].id === \"duplicate\";");
    expect(live.succeeded(), "collections: live-on-read script");
    expectBoolean(harness, "liveRead", true,
        "collections: held descriptor reevaluates current structural class state");
    if (deep != nullptr) deep->className = "primary action";

    NavigatorScriptHostLimits limits;
    limits.maxSelectorCollections = 2u;
    NavigatorScriptExecutionHarness bounded(
        gxos::javascript::RuntimeLimits(), limits);
    loadFixture(bounded, error);
    const ScriptResult capacity = bounded.execute(R"JS(
var query = document.querySelectorAll(".action");
var single = document.getElementsByClassName("action");
var multi = document.getElementsByClassName("action primary");
var sharedSlot = query === single && query.length === 6 && multi.length === 5;
)JS");
    expect(capacity.succeeded(), "collections: bounded shared registry script");
    expectBoolean(bounded, "sharedSlot", true,
        "collections: exact single selector and retrieval share a slot");
    expectError(bounded.execute("document.querySelectorAll(\".field\");"),
        RuntimeErrorCode::DocumentLookupLimitExceeded,
        "collections: multi-token query does not increase registry capacity");
    expect(bounded.hostAdapter().limits().maxSelectorCollections == 2u,
        "collections: configured registry bound is preserved");
}

void testFormStructureAndSelectorRelationships()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t ownerSerial = serialById(harness, "owner");
    const std::uint64_t outsideOwnedSerial = serialById(harness, "outside-owned");
    expect(ownerSerial != 0u && outsideOwnedSerial != 0u,
        "form scope: owner and external form-owned Element exist");
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.serial == outsideOwnedSerial)
            element.formControl.parentFormSerial = ownerSerial;
    }
    const ScriptResult result = harness.execute(R"JS(
var form = document.querySelector("#owner");
var panel = document.querySelector("#panel");
var external = document.querySelector("#outside-owned");
var formFields = form.getElementsByClassName("field required");
var formScopeIsStructural = formFields.length === 1 &&
    formFields[0] === document.querySelector("#name") &&
    form.contains(external) === false && form.elements["outside-owned"] === external;
var actionQuery = panel.getElementsByClassName("action primary");
var actionOnly = panel.querySelectorAll(".action");
var primaryOnly = panel.querySelectorAll(".primary");
var queryRelationships = actionQuery.length === 3 && actionOnly.length >= 3 &&
    primaryOnly.length >= 3 && actionQuery[0].matches(".action") &&
    actionQuery[0].matches(".primary") && actionQuery[1].matches(".action") &&
    actionQuery[1].matches(".primary") && actionQuery[2].matches(".action") &&
    actionQuery[2].matches(".primary");
var singleTokenBackwardCompatible =
    document.getElementsByClassName("action") ===
        document.querySelectorAll(".action") &&
    document.getElementsByClassName("action").length === 6;
var wildcardUnchanged = document.querySelectorAll("*").length ===
    document.getElementsByTagName("*").length &&
    document.querySelectorAll("*") === document.getElementsByTagName("*");
)JS");
    expect(result.succeeded(), "form/selectors: script");
    expectBoolean(harness, "formScopeIsStructural", true,
        "scope: class retrieval follows structural ancestry, not form ownership");
    expectBoolean(harness, "queryRelationships", true,
        "selectors: multi-token results match both single-class selectors");
    expectBoolean(harness, "singleTokenBackwardCompatible", true,
        "selectors: one-token retrieval keeps canonical selector identity");
    expectBoolean(harness, "wildcardUnchanged", true,
        "regression: JS43 universal collections remain shared and unchanged");
}

void testStaleCollectionsReceiversAndGeneration()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldPanelSerial = serialById(harness, "panel");
    const std::uint64_t oldSaveSerial = serialById(harness, "save");
    const ScriptResult setup = harness.execute(R"JS(
var oldDocumentCollection = document.getElementsByClassName("action primary");
var oldPanel = document.querySelector("#panel");
var oldScopedCollection = oldPanel.getElementsByClassName("action primary");
var oldResult = oldDocumentCollection[1];
)JS");
    expect(setup.succeeded(), "stale: capture multi-token collections and result");
    expect(harness.invalidateDocumentGeneration(error),
        "stale: invalidate generation");
    expect(error == RuntimeErrorCode::None, "stale: invalidation no error");
    const ScriptResult staleReceiver = harness.execute(R"JS(
var staleReceiverCollection =
    oldPanel.getElementsByClassName("action primary");
var staleReceiverSafe = staleReceiverCollection.length === 0;
)JS");
    expect(staleReceiver.succeeded(), "stale receiver: call fails closed");
    expectBoolean(harness, "staleReceiverSafe", true,
        "stale receiver: no replacement scope resolution");
    expectError(harness.execute("oldDocumentCollection.length;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: document multi-token collection");
    expectError(harness.execute("oldScopedCollection[0];"),
        RuntimeErrorCode::StaleHostObject,
        "stale: Element-scoped multi-token collection");
    expectError(harness.execute("oldResult.id;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: individual Element from multi-token retrieval");

    harness.document() = gxos::web::parseHtml(
        "file:///js44-replacement.html", kFixture);
    expect(oldPanelSerial == serialById(harness, "panel") &&
            oldSaveSerial == serialById(harness, "save"),
        "stale: replacement deliberately reuses structural serials");
    expect(harness.runtime().installHostGlobal("newPanel",
            serialById(harness, "panel"),
            gxos::javascript::kNavigatorElementHostKind, error),
        "stale: install current-generation Element");
    const ScriptResult reused = harness.execute(R"JS(
var replacement = newPanel.getElementsByClassName("action primary");
var replacementSafe = replacement.length === 3 &&
    replacement[0].id === "save" && replacement[1].id === "deep" &&
    replacement[2].id === "duplicate";
)JS");
    expect(reused.succeeded(), "stale: replacement collection script");
    expectBoolean(harness, "replacementSafe", true,
        "stale: old scope/result cannot bind to new generation serials");
    const ScriptResult staleArgument = harness.execute(
        "var staleElementArgumentEmpty = newPanel.getElementsByClassName(oldResult).length === 0;");
    expect(staleArgument.succeeded(), "stale: stale result argument script");
    expectBoolean(harness, "staleElementArgumentEmpty", true,
        "stale: individual result cannot become a class query");
    expectError(harness.execute("oldDocumentCollection.length;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: document collection remains stale after replacement");
    expectError(harness.execute("oldScopedCollection.length;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: scoped collection does not bind to replacement scope");
}

void testEventNestedDispatchAndPurity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t nameSerial = serialById(harness, "name");
    const std::uint64_t saveSerial = serialById(harness, "save");
    const ScriptResult setup = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var outside = document.querySelector("#outside");
var name = document.querySelector("#name");
var retrievalInCallback = false;
var nestedRetrieval = false;
var metadataPreserved = false;
var callbackSurfaceSafe = false;
var nestedDone = false;
save.addEventListener("click", function(event) {
  if (!nestedDone) {
    nestedDone = true;
    event.preventDefault();
    outside.click();
  }
});
outside.addEventListener("click", function(event) {
  var nested = document.getElementsByClassName("action primary");
  nestedRetrieval = nested.length === 5 && nested[4] === outside &&
    event.target === outside && event.defaultPrevented === false;
});
panel.addEventListener("click", function(event) {
  if (event.target === save) {
    var targetBefore = event.target;
    var currentBefore = event.currentTarget;
    var phaseBefore = event.eventPhase;
    var relatedBefore = event.relatedTarget;
    var defaultBefore = event.defaultPrevented;
    var primary = panel.getElementsByClassName("action primary");
    retrievalInCallback = primary.length === 3 && primary[0] === save &&
      panel.contains(primary[0]) && primary[0].closest(".panel") === panel;
    callbackSurfaceSafe = event.getElementsByClassName === undefined;
    metadataPreserved = event.target === targetBefore &&
      event.currentTarget === currentBefore && event.currentTarget === panel &&
      event.eventPhase === phaseBefore && phaseBefore === 3 &&
      event.relatedTarget === relatedBefore && relatedBefore === null &&
      event.defaultPrevented === defaultBefore && defaultBefore === true;
  }
});
)JS");
    expect(setup.succeeded(), "events/purity: setup");
    expect(harness.focusElement(nameSerial, error),
        "purity: establish authoritative focus owner");
    expect(error == RuntimeErrorCode::None, "purity: focus setup no error");
    const bool dirtyBefore = harness.documentDirty();
    const std::uint64_t revisionBefore = harness.layoutRevision();
    const std::uint64_t mutationBefore =
        harness.document().scriptMutationCount;
    const std::uint64_t generationBefore = harness.runtime().hostGeneration();
    const std::size_t structuralCountBefore =
        harness.document().structuralElements.size();
    const std::uint64_t firstSerialBefore =
        harness.document().structuralElements.empty() ? 0u :
            harness.document().structuralElements[0].serial;
    const std::size_t listenersBefore =
        harness.hostAdapter().clickListenerCount();
    bool defaultPrevented = false;
    expect(harness.dispatchClick(saveSerial, error, &defaultPrevented),
        "events: authentic click runs multi-token query callback");
    expect(error == RuntimeErrorCode::None, "events: no host/runtime error");
    expect(defaultPrevented, "events: click cancellation remains effective");
    expectBoolean(harness, "retrievalInCallback", true,
        "events: Element-scoped multi-token retrieval in click callback");
    expectBoolean(harness, "nestedRetrieval", true,
        "events: nested dispatch reads an independent descriptor");
    expectBoolean(harness, "metadataPreserved", true,
        "events: target/currentTarget/phase/related/default are preserved");
    expectBoolean(harness, "callbackSurfaceSafe", true,
        "events: Event objects do not gain Element retrieval methods");
    expect(harness.hostAdapter().clickListenerCount() == listenersBefore &&
            harness.hostAdapter().clickListenerCount() <= 64u,
        "purity: listener registry remains unchanged and bounded");

    const ScriptResult after = harness.execute(R"JS(
var focusUnchanged = document.activeElement === name;
var valuesUnchanged = name.value === "name-seed" &&
    name.defaultValue === "name-seed" &&
    document.querySelector("#email").value === "email-seed";
)JS");
    expect(after.succeeded(), "purity: verify state after retrieval");
    expectBoolean(harness, "focusUnchanged", true,
        "purity: activeElement remains the explicit focus owner");
    expectBoolean(harness, "valuesUnchanged", true,
        "purity: current and default control values unchanged");
    expect(harness.documentDirty() == dirtyBefore,
        "purity: layout dirty state unchanged");
    expect(harness.layoutRevision() == revisionBefore,
        "purity: layout revision unchanged");
    expect(harness.document().scriptMutationCount == mutationBefore,
        "purity: structural script mutation counter unchanged");
    expect(harness.document().structuralElements.size() ==
            structuralCountBefore &&
            harness.document().structuralElements[0].serial == firstSerialBefore,
        "purity: structural records remain unchanged");
    expect(harness.runtime().hostGeneration() == generationBefore,
        "purity: document generation unchanged");
    expect(harness.focusedElementSerial() == nameSerial,
        "purity: focus owner serial unchanged");
}

void testReceiverSurfaceAndRegressionBehavior()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var collection = document.getElementsByClassName("action primary");
var forms = document.forms;
var surfaces = panel.getElementsByClassName !== undefined &&
    collection.getElementsByClassName === undefined &&
    forms.getElementsByClassName === undefined &&
    panel.children.getElementsByClassName === undefined;
var selectorGrammarAdditive =
    document.querySelector(".action.primary") === panel &&
    document.querySelectorAll(".action.primary").length === 5 &&
    document.querySelectorAll("button.action.primary").length === 2 &&
    document.querySelectorAll("button.action").length >= 1;
var retrieversUnaffected = document.getElementsByTagName("button").length === 3 &&
    document.getElementsByTagName("*") === document.querySelectorAll("*");
)JS");
    expect(result.succeeded(), "surface/regression: script");
    expectBoolean(harness, "surfaces", true,
        "receiver surface remains limited to Document and Element");
    expectBoolean(harness, "selectorGrammarAdditive", true,
        "JS45 adds compound class queries without changing retrieval behavior");
    expectBoolean(harness, "retrieversUnaffected", true,
        "regression: tag and universal retrieval remain unchanged");
    expectError(harness.execute(
            "var detached = panel.getElementsByClassName; detached(\"action primary\");"),
        RuntimeErrorCode::InvalidReceiver,
        "receiver: detached Element retrieval rejects missing receiver");
}

} // namespace

int main()
{
    testAllTokenMatchingScopesAndIdentity();
    testWhitespaceEmptyAndQueryBounds();
    testCollectionsReadOnlyLiveAndCapacity();
    testFormStructureAndSelectorRelationships();
    testStaleCollectionsReceiversAndGeneration();
    testEventNestedDispatchAndPurity();
    testReceiverSurfaceAndRegressionBehavior();
    std::cout << "Navigator JavaScript JS44 checks: " << (checks - failures)
        << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
