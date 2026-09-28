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
<div id="panel" class="panel">
  <span id="label"></span>
  <div id="outer"><div id="inner"><button id="deep" type="button">Deep</button></div></div>
  <form id="owner" name="owner">
    <div id="wrapper"><input id="name" type="text" value="name-seed"></div>
    <select id="choice" name="choice">
      <option id="option-a" value="a" selected>A</option>
      <option id="option-b" value="b">B</option>
    </select>
    <span id="before"></span>
    <button id="save" class="action" type="button">Save</button>
    <div id="middle"></div>
    <button id="last" type="button">Last</button>
  </form>
</div>
<div id="outside"><button id="outside-button" type="button">Outside</button></div>
<div id="hidden" style="display:none"><span id="hidden-child"></span></div>
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
        expect(value->booleanValue() == expected, label + ": value");
}

void expectNumber(const NavigatorScriptExecutionHarness& harness,
    const char* name, double expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::Number, label + ": Number");
    if (value->isNumber())
        expect(value->numberValue() == expected, label + ": value");
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
    expect(harness.loadHtml("file:///js43.html", kFixture, error),
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

void testUniversalBasicsAndDocumentOrder()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::size_t exposedCount = harness.document().structuralElements.size();
    const ScriptResult result = harness.execute(R"JS(
var first = document.querySelector("*");
var all = document.querySelectorAll("*");
var tags = document.getElementsByTagName("*");
var structuralCount = all.length;
var htmlRoot = document.querySelector("html");
var universalBasics = first !== null && first === htmlRoot &&
    first === all[0] && first === tags[0] && first.tagName === "HTML";
var outerWhitespace = document.querySelector(" * ") === first &&
    document.querySelectorAll(" * ")[0] === first &&
    document.querySelector("  *\t") === first;
var broadCollection = all.length > 0 && all.length === tags.length &&
    all.length > document.querySelectorAll("button").length;
var documentOrder = all[0] === htmlRoot && all[1] === document.querySelector("body") &&
    all[2] === document.querySelector("#panel") &&
    all[3] === document.querySelector("#label");
var canonicalDocumentIdentity = all[0] === document.querySelector("html") &&
    all[2] === document.getElementById("panel") &&
    all[3] === document.getElementById("label") &&
    document.querySelector("*") === tags[0];
var multipleKindsMatch = document.querySelector("body").matches("*") &&
    document.querySelector("form").matches("*") &&
    document.querySelector("input").matches("*") &&
    document.querySelector("option").matches("*") &&
    document.querySelector("button").matches("*");
var rootHierarchy = all[0].tagName === "HTML" &&
    all[0].firstElementChild === all[1] &&
    all[1].parentElement === all[0];
var hiddenStructuralElements = document.querySelector("#hidden").matches("*") &&
    document.querySelector("#hidden-child").matches("*") &&
    all[all.length - 2] === document.querySelector("#hidden") &&
    all[all.length - 1] === document.querySelector("#hidden-child") &&
    document.querySelector("#hidden").querySelector("*") ===
        document.querySelector("#hidden-child");
var sameWildcardCollection = all === tags &&
    all === document.querySelectorAll("*") &&
    tags === document.getElementsByTagName("*");
)JS");
    expect(result.succeeded(), "universal basics: script");
    expectBoolean(harness, "universalBasics", true,
        "document querySelector returns first canonical structural Element");
    expectBoolean(harness, "outerWhitespace", true,
        "selector outer whitespace is trimmed normally");
    expectBoolean(harness, "broadCollection", true,
        "querySelectorAll wildcard includes a broad structural result set");
    expectBoolean(harness, "documentOrder", true,
        "wildcard follows represented structural document order");
    expectBoolean(harness, "canonicalDocumentIdentity", true,
        "wildcard handles reuse getElementById/querySelector identities");
    expectBoolean(harness, "multipleKindsMatch", true,
        "root, form, input, option, and button match the same Universal kind");
    expectBoolean(harness, "rootHierarchy", true,
        "represented html/body hierarchy is not fabricated or reordered");
    expectBoolean(harness, "hiddenStructuralElements", true,
        "hidden/non-rendered parsed Elements remain in structural wildcard results");
    expectBoolean(harness, "sameWildcardCollection", true,
        "equivalent wildcard descriptors share the bounded collection record");
    expectNumber(harness, "structuralCount", static_cast<double>(exposedCount),
        "wildcard count equals current exposed structural records");

    gxos::web::HtmlElementRef invalid;
    harness.document().structuralElements.push_back(invalid);
    const ScriptResult invalidRecord = harness.execute(R"JS(
var invalidRecordExcluded = document.querySelectorAll("*").length ===
    document.getElementsByTagName("*").length &&
    document.querySelectorAll("*").length === all.length;
)JS");
    expect(invalidRecord.succeeded(), "eligibility: invalid-record script");
    expectBoolean(harness, "invalidRecordExcluded", true,
        "zero-serial empty parser record is not exposed as an Element");
    harness.document().structuralElements.pop_back();
}

void testScopedQueriesCollectionsAndTraversal()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var label = document.querySelector("#label");
var outside = document.querySelector("#outside");
var descendants = panel.querySelectorAll("*");
var descendantTags = panel.getElementsByTagName("*");
var firstDescendant = panel.querySelector("*");
var scopedBasics = firstDescendant === label && firstDescendant === descendants[0] &&
    firstDescendant === descendantTags[0];
var strictScope = firstDescendant !== panel && descendants[0] !== panel &&
    descendants[descendants.length - 1] !== panel && !panel.contains(panel) === false;
var outsideExcluded = !panel.contains(outside) &&
    panel.querySelectorAll("*").length === panel.getElementsByTagName("*").length &&
    panel.querySelector("*") !== outside;
var nestedOrder = descendants[0] === label &&
    descendants[1] === document.querySelector("#outer") &&
    descendants[2] === document.querySelector("#inner") &&
    descendants[3] === document.querySelector("#deep");
var directTraversal = panel.firstElementChild === panel.querySelector("*") &&
    panel.children[0] === label && panel.children[0].matches("*") &&
    panel.children[0].closest("*") === panel.children[0];
var scopedContainment = panel.contains(descendants[0]) &&
    panel.contains(descendants[3]) && panel.contains(descendantTags[0]);
var scopedCounts = descendants.length > panel.children.length &&
    descendants.length === descendantTags.length &&
    descendants[999] === undefined && panel.querySelector("#outside") === null;
var sharedScopedCollection = descendants === descendantTags &&
    descendants === panel.querySelectorAll("*") &&
    descendantTags === panel.getElementsByTagName("*");
var leafEmpty = label.querySelector("*") === null &&
    label.querySelectorAll("*").length === 0 &&
    label.getElementsByTagName("*").length === 0;
)JS");
    expect(result.succeeded(), "scoped wildcard: script");
    expectBoolean(harness, "scopedBasics", true,
        "Element querySelector returns the first strict descendant");
    expectBoolean(harness, "strictScope", true,
        "Element-scoped wildcard excludes its receiver");
    expectBoolean(harness, "outsideExcluded", true,
        "Element-scoped wildcard never leaks outside its subtree");
    expectBoolean(harness, "nestedOrder", true,
        "scoped querySelectorAll preserves nested structural order");
    expectBoolean(harness, "directTraversal", true,
        "firstElementChild and child.matches/closest share canonical identity");
    expectBoolean(harness, "scopedContainment", true,
        "each tested strict descendant satisfies Element.contains");
    expectBoolean(harness, "scopedCounts", true,
        "broad scoped collection has indexed reads and undefined out of range");
    expectBoolean(harness, "sharedScopedCollection", true,
        "scoped tag retrieval and wildcard query reuse one descriptor record");
    expectBoolean(harness, "leafEmpty", true,
        "leaf-scoped wildcard query and retrieval are empty");
    expectError(harness.execute(
            "descendants[0] = document.querySelector(\"#outside\");"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "wildcard collection indexed writes remain read-only");
    expectError(harness.execute("descendants.length = 0;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "wildcard collection length remains read-only");
}

void testTagEquivalenceFormSelectAndClassRegression()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var all = document.querySelectorAll("*");
var tags = document.getElementsByTagName("*");
var panel = document.querySelector("#panel");
var scoped = panel.querySelectorAll("*");
var scopedTags = panel.getElementsByTagName("*");
var form = document.querySelector("#owner");
var formAll = form.getElementsByTagName("*");
var wrapper = document.querySelector("#wrapper");
var name = document.querySelector("#name");
var formOwnershipDistinct = formAll.length > form.elements.length &&
    form.contains(wrapper) && form.contains(name) &&
    formAll[0] === wrapper && form.elements["wrapper"] === undefined &&
    form.elements["name"] === name;
var formControlsRemainControls = form.elements.length === 4 &&
    form.elements["name"] === name && form.elements["save"] ===
        document.querySelector("#save");
var select = document.querySelector("#choice");
var selectAll = select.getElementsByTagName("*");
var optionIdentity = selectAll.length === 2 &&
    selectAll[0] === select.options[0] && selectAll[1] === select.options[1] &&
    select.children[0] === select.options[0] &&
    selectAll[0] === document.querySelector("#option-a");
var retrievalEquivalence = all.length === tags.length && all === tags &&
    scoped.length === scopedTags.length && scoped === scopedTags &&
    all[all.length - 1] === tags[tags.length - 1] &&
    scoped[scoped.length - 1] === scopedTags[scopedTags.length - 1];
var tagRetrievalUnchanged = document.getElementsByTagName("BUTTON").length === 4 &&
    document.getElementsByTagName("button")[0] === document.querySelector("#deep") &&
    document.getElementsByTagName(" * ").length === 0 &&
    form.getElementsByTagName("div")[0] === wrapper;
var classRetrievalUnchanged = document.getElementsByClassName("action").length === 1 &&
    document.getElementsByClassName("action")[0] === document.querySelector("#save") &&
    document.getElementsByClassName("action primary").length === 0 &&
    document.querySelector("#save").matches("button.action");
var scopedReceiverAndOutside = scoped[0] !== panel &&
    scoped[scoped.length - 1] !== document.querySelector("#outside") &&
    panel.contains(scoped[scoped.length - 1]);
)JS");
    expect(result.succeeded(), "retrieval integration: script");
    expectBoolean(harness, "formOwnershipDistinct", true,
        "wildcard tag retrieval sees structural wrappers outside form.elements");
    expectBoolean(harness, "formControlsRemainControls", true,
        "form.elements continues to expose form controls only");
    expectBoolean(harness, "optionIdentity", true,
        "select wildcard identity equals options and structural children");
    expectBoolean(harness, "retrievalEquivalence", true,
        "document and scoped wildcard retrieval equal querySelectorAll order/identity");
    expectBoolean(harness, "tagRetrievalUnchanged", true,
        "ordinary tag matching remains case-insensitive and wildcard argument exact");
    expectBoolean(harness, "classRetrievalUnchanged", true,
        "single exact class token behavior is unchanged");
    expectBoolean(harness, "scopedReceiverAndOutside", true,
        "scoped wildcard results are strict descendants with no outside result");
}

void testRelationalUniversalSelectors()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var inner = document.querySelector("#inner");
var deep = document.querySelector("#deep");
var name = document.querySelector("#name");
var save = document.querySelector("#save");
var last = document.querySelector("#last");
var childUniversal = inner.matches("div > *") &&
    deep.matches("div > *") && name.matches("div > *") &&
    name.matches("* > input") && deep.matches("* > *");
var childImmediateOnly = !deep.matches("form > *") &&
    !name.matches("form > *") &&
    document.querySelectorAll("form > *").length === 6;
var descendantUniversal = deep.matches("div *") &&
    name.matches("* input") && deep.matches("* *") &&
    document.querySelector("form > *") === document.querySelector("#wrapper");
var adjacentUniversal = save.matches("span + *") &&
    save.matches("* + button") && save.matches("* + *") &&
    last.matches("div + button");
var adjacentImmediateOnly = !last.matches("span + *") &&
    !document.querySelector("#wrapper").matches("* + div");
var generalSiblingUniversal = last.matches("span ~ *") &&
    last.matches("* ~ button") && last.matches("* ~ *") &&
    save.matches("span ~ button");
var siblingSameParent = !deep.matches("span ~ *") &&
    !document.querySelector("#before").matches("form ~ *");
var parserShapes = document.querySelectorAll("*").length > 0 &&
    document.querySelectorAll("div > *").length > 0 &&
    document.querySelectorAll("* > input").length === 1 &&
    document.querySelectorAll("* > *").length > 0 &&
    document.querySelectorAll("div *").length > 0 &&
    document.querySelectorAll("* input").length === 1 &&
    document.querySelectorAll("* *").length > 0 &&
    document.querySelectorAll("span + *").length > 0 &&
    document.querySelectorAll("* + button").length > 0 &&
    document.querySelectorAll("* + *").length > 0 &&
    document.querySelectorAll("span ~ *").length > 0 &&
    document.querySelectorAll("* ~ button").length > 0 &&
    document.querySelectorAll("* ~ *").length > 0;
var selectorChainLimit = document.querySelectorAll("form > * + button").length === 0 &&
    document.querySelectorAll("* > * > *").length === 0 &&
    document.querySelectorAll("div * button").length === 0;
)JS");
    expect(result.succeeded(), "relational universal: script");
    expectBoolean(harness, "childUniversal", true,
        "child relation accepts universal on either or both sides");
    expectBoolean(harness, "childImmediateOnly", true,
        "child relation continues to require the immediate parent");
    expectBoolean(harness, "descendantUniversal", true,
        "descendant relation accepts universal left/right simple selectors");
    expectBoolean(harness, "adjacentUniversal", true,
        "adjacent sibling relation accepts universal and concrete selectors");
    expectBoolean(harness, "adjacentImmediateOnly", true,
        "adjacent sibling relation remains immediate and same-parent only");
    expectBoolean(harness, "generalSiblingUniversal", true,
        "general sibling relation accepts universal and concrete selectors");
    expectBoolean(harness, "siblingSameParent", true,
        "general sibling matching remains backward-only and same-parent");
    expectBoolean(harness, "parserShapes", true,
        "all one-relation universal grammar shapes parse and match");
    expectBoolean(harness, "selectorChainLimit", true,
        "arbitrary combinator chains remain rejected");
}

void testParserBoundsAndInvalidInputs()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::string oversized(
        gxos::javascript::kNavigatorScriptMaxSelectorLength + 1u, 'x');
    const ScriptResult result = harness.execute(
        "var leaf = document.querySelector(\"#label\");"
        "var emptySelector = document.querySelector(\"\") === null &&"
        " document.querySelectorAll(\"\").length === 0 &&"
        " leaf.matches(\"\") === false && leaf.closest(\"\") === null;"
        "var blankSelector = document.querySelector(\"   \") === null &&"
        " document.querySelectorAll(\"   \").length === 0 &&"
        " leaf.matches(\"   \") === false;"
        "var malformedUniversal = document.querySelector(\"**\") === null &&"
        " document.querySelectorAll(\"**\").length === 0 &&"
        " leaf.matches(\"**\") === false && leaf.closest(\"**\") === null;"
        "var malformedRelations = document.querySelectorAll(\"> *\").length === 0 &&"
        " document.querySelectorAll(\"* >\").length === 0 &&"
        " document.querySelectorAll(\"* + + *\").length === 0 &&"
        " document.querySelectorAll(\"* ~ ~ *\").length === 0;"
        "var compoundUniversalDeferred = document.querySelectorAll(\"*.action\").length === 0 &&"
        " document.querySelectorAll(\"*#save\").length === 0 &&"
        " document.querySelectorAll(\"*.\").length === 0 &&"
        " document.querySelectorAll(\"*#\").length === 0;"
        "var exactTagWildcardOnly = document.getElementsByTagName(\"*\").length > 0 &&"
        " document.getElementsByTagName(\" * \" ).length === 0;"
        "var tooLong = document.querySelector(\"" + oversized + "\") === null &&"
        " document.querySelectorAll(\"" + oversized + "\").length === 0 &&"
        " leaf.matches(\"" + oversized + "\") === false;"
        "var parserBoundChecks = emptySelector && blankSelector &&"
        " malformedUniversal && malformedRelations && compoundUniversalDeferred &&"
        " exactTagWildcardOnly && tooLong;");
    expect(result.succeeded(), "parser bounds: invalid selector calls fail closed");
    expectBoolean(harness, "emptySelector", true,
        "empty selector remains invalid");
    expectBoolean(harness, "blankSelector", true,
        "whitespace-only selector remains invalid");
    expectBoolean(harness, "malformedUniversal", true,
        "double wildcard is rejected safely");
    expectBoolean(harness, "malformedRelations", true,
        "missing operands and repeated combinators are rejected");
    expectBoolean(harness, "compoundUniversalDeferred", true,
        "optional *.class and *#id compound forms remain unsupported");
    expectBoolean(harness, "exactTagWildcardOnly", true,
        "getElementsByTagName accepts exact wildcard token without trimming");
    expectBoolean(harness, "tooLong", true,
        "selector input beyond 256 bytes fails safely");
    expect(harness.hostAdapter().limits().maxSelectorCollections ==
            gxos::javascript::kNavigatorScriptMaxSelectorCollections,
        "wildcard parsing does not raise the selector collection capacity");
}

void testSharedCapacityAndSimultaneousCollections()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptHostLimits limits;
    limits.maxSelectorCollections = 1u;
    NavigatorScriptExecutionHarness harness(
        gxos::javascript::RuntimeLimits(), limits);
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var all = document.querySelectorAll("*");
var tags = document.getElementsByTagName("*");
)JS");
    expect(result.succeeded(),
        "capacity: wildcard query and tag retrieval share one record");
    expectError(harness.execute("document.querySelectorAll(\"button\");"),
        RuntimeErrorCode::DocumentLookupLimitExceeded,
        "capacity: second unique descriptor fails at configured bound");
    const ScriptResult shared = harness.execute(R"JS(
var sharedCapacity = all === tags && all.length > 0 && tags[0] === all[0];
)JS");
    expect(shared.succeeded(), "capacity: shared wildcard collection reads");
    expectBoolean(harness, "sharedCapacity", true,
        "wildcard APIs reuse the same bounded slot at capacity");
    expect(harness.hostAdapter().limits().maxSelectorCollections == 1u,
        "capacity: configured one-record limit is preserved");

    RuntimeErrorCode independentError = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness independent;
    loadFixture(independent, independentError);
    const ScriptResult simultaneous = independent.execute(R"JS(
var allNow = document.querySelectorAll("*");
var buttonsNow = document.querySelectorAll("button");
var tagsNow = document.getElementsByTagName("*");
var simultaneousStable = allNow === tagsNow && allNow.length > buttonsNow.length &&
    buttonsNow.length === 4 && allNow[0] === tagsNow[0] &&
    buttonsNow[0] === document.querySelector("button") &&
    allNow[3] === document.querySelector("#label");
)JS");
    expect(simultaneous.succeeded(), "collections: simultaneous descriptors script");
    expectBoolean(independent, "simultaneousStable", true,
        "wildcard/tag/button descriptors retain independent indexed reads");
}

void testStaleCollectionsReceiversAndSerialReuse()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldPanelSerial = serialById(harness, "panel");
    const std::uint64_t oldSaveSerial = serialById(harness, "save");
    const ScriptResult setup = harness.execute(R"JS(
var oldAll = document.querySelectorAll("*");
var oldTags = document.getElementsByTagName("*");
var oldPanel = document.querySelector("#panel");
var oldScopedAll = oldPanel.querySelectorAll("*");
var oldScopedTags = oldPanel.getElementsByTagName("*");
var oldSave = document.querySelector("#save");
)JS");
    expect(setup.succeeded(), "stale wildcard: capture collections and Elements");
    expect(harness.invalidateDocumentGeneration(error),
        "stale wildcard: invalidate generation");
    expect(error == RuntimeErrorCode::None,
        "stale wildcard: generation invalidation reports no error");
    const ScriptResult staleMethods = harness.execute(R"JS(
var staleMatchesFalse = oldSave.matches("*") === false;
var staleClosestNull = oldSave.closest("*") === null;
var staleTagReceiverEmpty = oldPanel.getElementsByTagName("*").length === 0;
)JS");
    expect(staleMethods.succeeded(), "stale wildcard: predicates and retrieval fail closed");
    expectBoolean(harness, "staleMatchesFalse", true,
        "stale Element.matches('*') validates generation before universal match");
    expectBoolean(harness, "staleClosestNull", true,
        "stale Element.closest('*') returns null");
    expectBoolean(harness, "staleTagReceiverEmpty", true,
        "stale Element tag retrieval returns empty current collection");
    expectError(harness.execute("oldAll.length;"),
        RuntimeErrorCode::StaleHostObject, "stale document wildcard selector collection");
    expectError(harness.execute("oldTags[0];"),
        RuntimeErrorCode::StaleHostObject, "stale wildcard tag collection");
    expectError(harness.execute("oldScopedAll.length;"),
        RuntimeErrorCode::StaleHostObject, "stale scoped wildcard query collection");
    expectError(harness.execute("oldScopedTags.length;"),
        RuntimeErrorCode::StaleHostObject, "stale scoped wildcard tag collection");
    expectError(harness.execute("oldSave.id;"),
        RuntimeErrorCode::StaleHostObject, "stale individual wildcard-result Element");
    expectError(harness.execute("oldPanel.querySelectorAll(\"*\").length;"),
        RuntimeErrorCode::StaleHostObject, "stale querySelectorAll receiver fails safely");

    harness.document() = gxos::web::parseHtml(
        "file:///js43-replacement.html", kFixture);
    const std::uint64_t newPanelSerial = serialById(harness, "panel");
    const std::uint64_t newSaveSerial = serialById(harness, "save");
    expect(oldPanelSerial == newPanelSerial && oldSaveSerial == newSaveSerial,
        "stale wildcard: replacement fixture deliberately reuses serials");
    expect(harness.runtime().installHostGlobal("document",
            gxos::javascript::kNavigatorDocumentHostInstance,
            gxos::javascript::kNavigatorDocumentHostKind, error),
        "stale wildcard: reinstall current-generation document host");
    const ScriptResult staleAfterReplacement = harness.execute(R"JS(
var replacementStaleMatches = oldSave.matches("*") === false;
var replacementStaleClosest = oldSave.closest("*") === null;
var replacementStaleScope = oldPanel.getElementsByTagName("*").length === 0;
)JS");
    expect(staleAfterReplacement.succeeded(),
        "stale wildcard: old handles remain fail-closed after structural serial reuse");
    expectBoolean(harness, "replacementStaleMatches", true,
        "stale Element.matches('*') remains false against replacement document");
    expectBoolean(harness, "replacementStaleClosest", true,
        "stale Element.closest('*') remains null against replacement document");
    expectBoolean(harness, "replacementStaleScope", true,
        "stale receiver wildcard tag retrieval cannot see replacement subtree");
    expect(harness.runtime().installHostGlobal("newPanel", newPanelSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "stale wildcard: install replacement-generation Element");
    const ScriptResult replacement = harness.execute(R"JS(
var freshAll = document.querySelectorAll("*");
var freshTags = document.getElementsByTagName("*");
var freshWildcardBasics = freshAll.length > 0 && freshAll === freshTags;
var freshScopedLookup = newPanel.getElementsByTagName("*").length > 1;
var freshSave = document.querySelector("#save");
var freshPredicates = freshSave.matches("*") && freshSave.closest("*") === freshSave;
var staleIdentityDifferent = oldSave !== freshSave;
var generationSafeReplacement = freshWildcardBasics && freshPredicates &&
    freshScopedLookup && staleIdentityDifferent;
)JS");
    expect(replacement.succeeded(), "stale wildcard: replacement document script (code " +
        std::to_string(static_cast<int>(replacement.runtimeError.code)) + ")");
    expectBoolean(harness, "freshWildcardBasics", true,
        "replacement document creates current-generation wildcard collections");
    expectBoolean(harness, "freshPredicates", true,
        "replacement document Element matches and closest use Universal");
    expectBoolean(harness, "freshScopedLookup", true,
        "replacement Element receiver scopes universal retrieval");
    expectBoolean(harness, "staleIdentityDifferent", true,
        "old and replacement Elements retain generation-distinct identity");
    expectBoolean(harness, "generationSafeReplacement", true,
        "new wildcard results are fresh while reused old serials remain stale");
}

void testEventsNestedDispatchAndSelectorPurity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t saveSerial = serialById(harness, "save");
    const ScriptResult setup = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var outside = document.querySelector("#outside-button");
var allHeld = document.querySelectorAll("*");
var tagsHeld = document.getElementsByTagName("*");
var buttonsHeld = document.querySelectorAll("button");
var callbackTargetUniversal = false;
var callbackScopedCollections = false;
var callbackEventSurface = false;
var callbackMetadataPreserved = false;
var nestedEventUniversal = false;
var nestedDispatchPreserved = false;
save.addEventListener("click", function(event) {
  var targetBefore = event.target;
  var currentBefore = event.currentTarget;
  var phaseBefore = event.eventPhase;
  var relatedBefore = event.relatedTarget;
  event.preventDefault();
  outside.click();
  nestedDispatchPreserved = event.target === targetBefore &&
    event.currentTarget === currentBefore && currentBefore === save &&
    event.eventPhase === phaseBefore && phaseBefore === 2 &&
    event.relatedTarget === relatedBefore && relatedBefore === null &&
    event.defaultPrevented === true;
});
panel.addEventListener("click", function(event) {
  if (event.target === save) {
    var targetBefore = event.target;
    var currentBefore = event.currentTarget;
    var phaseBefore = event.eventPhase;
    var relatedBefore = event.relatedTarget;
    var defaultBefore = event.defaultPrevented;
    var scopedAll = panel.querySelectorAll("*");
    var scopedTags = panel.getElementsByTagName("*");
    callbackTargetUniversal = event.target.matches("*") &&
      panel.contains(event.target);
    callbackScopedCollections = scopedAll === scopedTags &&
      scopedAll.length > 0 && scopedAll[0] === document.querySelector("#label") &&
      allHeld === tagsHeld && buttonsHeld.length === 4 &&
      buttonsHeld[0] === document.querySelector("#deep");
    callbackEventSurface = event.matches === undefined &&
      event.querySelectorAll === undefined;
    callbackMetadataPreserved = event.target === targetBefore &&
      event.currentTarget === currentBefore && currentBefore === panel &&
      event.eventPhase === phaseBefore && phaseBefore === 3 &&
      event.relatedTarget === relatedBefore && relatedBefore === null &&
      event.defaultPrevented === defaultBefore && defaultBefore === true;
  }
});
outside.addEventListener("click", function(event) {
  nestedEventUniversal = event.target.matches("*") &&
    event.target === outside && !panel.contains(event.target) &&
    document.querySelectorAll("*").length === allHeld.length;
});
)JS");
    expect(setup.succeeded(), "events: wildcard callback setup");
    const bool dirtyBefore = harness.documentDirty();
    const std::uint64_t revisionBefore = harness.layoutRevision();
    const std::uint64_t mutationBefore = harness.document().scriptMutationCount;
    const std::uint64_t generationBefore = harness.runtime().hostGeneration();
    const std::size_t listenersBefore = harness.hostAdapter().clickListenerCount();
    bool defaultPrevented = false;
    expect(harness.dispatchClick(saveSerial, error, &defaultPrevented),
        "events: authentic save click runs universal selector listeners");
    expect(error == RuntimeErrorCode::None, "events: no wildcard callback error");
    expect(defaultPrevented, "events: defaultPrevented remains true");
    expectBoolean(harness, "callbackTargetUniversal", true,
        "event.target.matches('*') accepts the valid current Element");
    expectBoolean(harness, "callbackScopedCollections", true,
        "wildcard and normal collections coexist during click callback");
    expectBoolean(harness, "callbackEventSurface", true,
        "universal Element APIs are not added to Event host objects");
    expectBoolean(harness, "callbackMetadataPreserved", true,
        "target/currentTarget/phase/relatedTarget/defaultPrevented survive wildcard work");
    expectBoolean(harness, "nestedEventUniversal", true,
        "nested click can use matches('*') and document wildcard query");
    expectBoolean(harness, "nestedDispatchPreserved", true,
        "outer event state is restored after nested wildcard dispatch");
    expect(harness.hostAdapter().clickListenerCount() == listenersBefore,
        "events: wildcard matching does not alter listener registry");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "events: listener registry remains bounded at 64");
    expect(!dirtyBefore && !harness.documentDirty(),
        "events: selector matching leaves layout dirtiness unchanged");
    expect(harness.layoutRevision() == revisionBefore,
        "events: selector matching leaves layout revision unchanged");
    expect(harness.document().scriptMutationCount == mutationBefore,
        "events: selector matching leaves document mutation count unchanged");
    expect(harness.runtime().hostGeneration() == generationBefore,
        "events: selector matching leaves generation unchanged");
}

void testInvalidReceiversAndPriorSelectorRegression()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var deep = document.querySelector("#deep");
var save = document.querySelector("#save");
var receiverKindsRemainRestricted = document.matches === undefined &&
    document.forms.matches === undefined &&
    document.querySelectorAll("*").matches === undefined &&
    panel.matches("*") && panel.querySelector("#deep") === deep;
var previousSimpleSelectors = document.querySelector("#save") === save &&
    document.querySelector(".panel") === panel &&
    document.querySelector("button.action") === save &&
    document.querySelector("button#save") === save;
var previousRelations = document.querySelector("form input") ===
    document.querySelector("#name") &&
    document.querySelector("#wrapper > input") === document.querySelector("#name") &&
    document.querySelector("span + button") === save &&
    document.querySelector("span ~ button") === save;
var previousRetrieval = document.getElementsByTagName("button").length === 4 &&
    document.getElementsByClassName("action").length === 1 &&
    document.querySelector("#save").closest(".panel") === panel &&
    panel.contains(deep);
)JS");
    expect(result.succeeded(), "regression: existing selector surface executes");
    expectBoolean(harness, "receiverKindsRemainRestricted", true,
        "Document and collection host kinds do not acquire Element methods");
    expectBoolean(harness, "previousSimpleSelectors", true,
        "id/class/tag/tag.class/tag#id matching remains unchanged");
    expectBoolean(harness, "previousRelations", true,
        "descendant/child/adjacent/general relations remain intact");
    expectBoolean(harness, "previousRetrieval", true,
        "JS42 tag/class retrieval, JS41 contains, and JS37 closest remain intact");
}

} // namespace

int main()
{
    testUniversalBasicsAndDocumentOrder();
    testScopedQueriesCollectionsAndTraversal();
    testTagEquivalenceFormSelectAndClassRegression();
    testRelationalUniversalSelectors();
    testParserBoundsAndInvalidInputs();
    testSharedCapacityAndSimultaneousCollections();
    testStaleCollectionsReceiversAndSerialReuse();
    testEventsNestedDispatchAndSelectorPurity();
    testInvalidReceiversAndPriorSelectorRegression();
    if (failures != 0) {
        std::cerr << failures << " JS43 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS43 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
