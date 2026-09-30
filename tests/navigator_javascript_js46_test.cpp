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
<form id="owner" class="form owner active">
  <div id="panel" class="panel active">
    <label id="label" class="label required"></label>
    <button id="save" class="action primary large" type="button">Save</button>
    <div id="save" class="action duplicate-id"></div>
    <button id="cancel" class="action secondary" type="button">Cancel</button>
    <span id="marker" class="marker active"></span>
    <button id="after-marker" class="action secondary" type="button">After</button>
    <div id="group" class="group enabled">
      <input id="field" class="field required" type="text" value="field-seed">
      <div id="nested-panel" class="panel active">
        <button id="nested-save" class="primary action" type="button">Nested</button>
      </div>
      <input id="deep-field" class="required field" type="text" value="deep-seed">
    </div>
    <span id="case" class="Foo bar"></span>
    <span id="substring" class="foobar bar"></span>
    <span id="duplicate" class="foo foo bar"></span>
    <span id="superset" class="foo bar baz large enabled"></span>
    <div id="rel-foo" class="foo"><span id="rel-bar" class="bar"></span></div>
    <span id="eight"></span>
    <span id="nine"></span>
    <span id="i"></span>
    <span id="long255"></span>
    <span id="long256"></span>
  </div>
</form>
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
    expect(harness.loadHtml("file:///js46.html", kFixture, error),
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

void testParsingMatchingAndRetrievalEquivalence()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::string eightTokens = "a b c d e f g h";
    const std::string nineTokens = "a b c d e f g h i";
    const std::string long255(255u, 'x');
    const std::string long256(256u, 'y');
    const std::string maxIdClassSelector = "#i." + std::string(253u, 'z');
    const std::string oversizedIdClassSelector = "#i." +
        std::string(254u, 'z');
    auto* eight = elementById(harness, "eight");
    auto* nine = elementById(harness, "nine");
    auto* maxToken = elementById(harness, "long255");
    auto* oversizedToken = elementById(harness, "long256");
    auto* idByteBoundary = elementById(harness, "i");
    expect(eight != nullptr && nine != nullptr && maxToken != nullptr &&
            oversizedToken != nullptr && idByteBoundary != nullptr,
        "bounds: fixture Elements exist");
    if (eight != nullptr) eight->className = eightTokens;
    if (nine != nullptr) nine->className = nineTokens;
    if (maxToken != nullptr) maxToken->className = long255;
    if (oversizedToken != nullptr) oversizedToken->className = long256;
    if (idByteBoundary != nullptr)
        idByteBoundary->className = std::string(253u, 'z');

    const std::string eightSelector = ".a.b.c.d.e.f.g.h";
    const std::string nineSelector = ".a.b.c.d.e.f.g.h.i";
    const std::string idEightSelector = "#eight.a.b.c.d.e.f.g.h";
    const std::string idNineSelector = "#eight.a.b.c.d.e.f.g.h.i";
    const std::string idRepeatedOverBound =
        "#eight.a.a.a.a.a.a.a.a.a";
    const std::string repeatedOverBound =
        ".foo.foo.foo.foo.foo.foo.foo.foo.foo";
    const std::string maxSelector = "." + long255;
    const std::string oversizedSelector = "." + long256;
    const ScriptResult result = harness.execute(
        "var panel = document.querySelector(\"#panel\");"
        "var save = document.querySelector(\"#save\");"
        "var nestedSave = document.querySelector(\"#nested-save\");"
        "var outside = document.querySelector(\"#outside\");"
        "var foo = document.querySelector(\"#duplicate\");"
        "var superset = document.querySelector(\"#superset\");"
        "var firstCompound = document.querySelector(\".action.primary\");"
        "var firstTagCompound = document.querySelector(\"button.action.primary\");"
        "var allCompound = document.querySelectorAll(\".action.primary\");"
        "var firstIdClass = document.querySelector(\"#save.action\");"
        "var duplicateIdClass = document.querySelectorAll(\"#save.action\");"
        "var firstFullCompound = document.querySelector(\"button#save.action.primary\");"
        "var allFullCompound = document.querySelectorAll(\"button#save.action.primary\");"
        "var reversed = document.querySelectorAll(\".primary.action\");"
        "var retrieval = document.getElementsByClassName(\"action primary\");"
        "var duplicateRetrieval = document.getElementsByClassName(\"action action primary\");"
        "var fooBars = document.querySelectorAll(\".foo.bar\");"
        "var duplicateFooBars = document.querySelectorAll(\".foo.foo.bar\");"
        "var fooRetrieval = document.getElementsByClassName(\"foo foo bar\");"
        "var panelSelectors = panel.querySelectorAll(\".action.primary\");"
        "var panelIdSelectors = panel.querySelectorAll(\"#save.action\");"
        "var panelTagSelectors = panel.querySelectorAll(\"button.action.primary\");"
        "var panelRetrieval = panel.getElementsByClassName(\"action primary\");"
        "var eight = document.querySelector(\"" + eightSelector + "\");"
        "var nine = document.querySelector(\"" + nineSelector + "\");"
        "var idEight = document.querySelector(\"" + idEightSelector + "\");"
        "var idNine = document.querySelector(\"" + idNineSelector + "\");"
        "var idRepeatedOverBound = document.querySelector(\"" +
            idRepeatedOverBound + "\");"
        "var repeatedOverBound = document.querySelector(\"" +
            repeatedOverBound + "\");"
        "var maxSelector = document.querySelector(\"" + maxSelector + "\");"
        "var oversizedSelector = document.querySelector(\"" +
            oversizedSelector + "\");"
        "var maxIdClassSelector = document.querySelector(\"" +
            maxIdClassSelector + "\");"
        "var oversizedIdClassSelector = document.querySelector(\"" +
            oversizedIdClassSelector + "\");"
        R"JS(
var positives = save.matches(".action.primary") &&
    save.matches(".primary.action") && save.matches(".action.primary.large") &&
    save.matches("button.action.primary") && save.matches("BUTTON.action.primary") &&
    nestedSave.matches("button.primary.action") &&
    panel.matches("div.panel.active") &&
    document.querySelector("#duplicate").matches(".foo.foo.bar") &&
    document.querySelector("#superset").matches(".foo.bar");
var negatives = !document.querySelector("#cancel").matches(".action.primary") &&
    !save.matches(".action.missing") &&
    !save.matches("div.action.primary") &&
    !document.querySelector("#case").matches(".foo.bar") &&
    !document.querySelector("#substring").matches(".foo.bar") &&
    !document.querySelector("#substring").matches(".foo");
var firstResults = firstCompound === save && firstTagCompound === save &&
    allCompound.length === 3 && allCompound[0] === save &&
    allCompound[1] === nestedSave && allCompound[2] === outside;
var canonicalRetrieval = allCompound === reversed && allCompound === retrieval &&
    allCompound === duplicateRetrieval && allCompound[0] === retrieval[0] &&
    retrieval[1] === nestedSave && retrieval[2] === outside;
var scopedEquivalence = panelSelectors.length === 2 &&
    panelSelectors[0] === save && panelSelectors[1] === nestedSave &&
    panelSelectors === panelRetrieval && panelSelectors[2] === undefined;
var scopedTagQuery = panelTagSelectors.length === 2 &&
    panelTagSelectors[0] === save && panelTagSelectors[1] === nestedSave;
var scopedIdQuery = panelIdSelectors.length === 2 &&
    panelIdSelectors[0] === save &&
    panelIdSelectors[1] === document.querySelector("div#save.action") &&
    panel.querySelector("#panel.active") === null &&
    panel.querySelector("#outside.action.primary") === null;
var duplicateEquivalence = fooBars.length === 2 && fooBars[0] === foo &&
    fooBars[1] === superset && fooBars === duplicateFooBars &&
    fooBars === fooRetrieval && foo.matches(".foo.bar") &&
    superset.matches(".bar.foo");
var subsetInvariants = allCompound.length <= document.querySelectorAll(".action").length &&
    allCompound.length <= document.querySelectorAll(".primary").length &&
    allCompound[0].matches(".action") && allCompound[0].matches(".primary") &&
    allCompound[1].matches(".action") && allCompound[1].matches(".primary") &&
    allCompound[2].matches(".action") && allCompound[2].matches(".primary");
var idCompatibility = document.querySelector("#save.action") === save &&
    document.querySelector("button#save.action") === save &&
    save.matches("#save.action") && save.matches("button#save") &&
    !save.matches("#other.action");
var universalClassDeferred = document.querySelector("*.foo") === null &&
    document.querySelector("*.foo.bar") === null &&
    document.querySelectorAll("*.foo").length === 0 &&
    !foo.matches("*.foo") && document.querySelector("*#save") === null &&
    document.querySelector("*#save.action") === null;
var eightBound = eight === document.querySelector("#eight") &&
    nine === null && repeatedOverBound === null &&
    idRepeatedOverBound === null;
var selectorByteBound = maxSelector === document.querySelector("#long255") &&
    oversizedSelector === null &&
    maxIdClassSelector === document.querySelector("#i") &&
    oversizedIdClassSelector === null;
var idClassCore = firstIdClass === save && firstFullCompound === save &&
    allFullCompound.length === 1 && allFullCompound[0] === save &&
    duplicateIdClass.length === 2 && duplicateIdClass[0] === save &&
    duplicateIdClass[1] === document.querySelector("div#save.action");
var idEightBound = idEight === document.querySelector("#eight") &&
    idNine === null;
var exactIdClassSemantics = save.matches("#save.action") &&
    save.matches("#save.primary") && save.matches("#save.action.primary") &&
    save.matches("#save.primary.action") &&
    save.matches("#save.action.action.primary") &&
    save.matches("button#save.action.primary") &&
    save.matches("BUTTON#save.action.primary") &&
    !save.matches("#other.action") && !save.matches("#save.missing") &&
    !save.matches("div#save.action.primary") &&
    !save.matches("#save.Primary") && !save.matches("#SAVE.action");
var duplicateIdOrder = document.querySelector("#save.action") === save &&
    document.querySelectorAll("#save.action").length === 2 &&
    document.querySelectorAll("div#save.action").length === 1 &&
    document.querySelectorAll("#save.action")[1].matches("div#save.action");
var canonicalIdAndClassIdentity = document.getElementById("save") === save &&
    document.getElementsByClassName("action primary")[0] === save &&
    document.querySelector("#save.action.primary") === save &&
    document.querySelector("button#save.action.primary") === save;
var classRetrievalSubset = document.querySelectorAll("#save.action.primary");
var classRetrievalCandidates = document.getElementsByClassName("action primary");
var subsetRelationship = classRetrievalSubset.length <= classRetrievalCandidates.length &&
    classRetrievalSubset.length === 1 && classRetrievalSubset[0] === save &&
    classRetrievalCandidates[0] === classRetrievalSubset[0];
var retrievalApiUnchanged = document.getElementsByClassName("foo bar").length === 2 &&
    document.getElementsByTagName("*") === document.querySelectorAll("*");
)JS");
    expect(result.succeeded(), "parse/match: script");
    expectBoolean(harness, "positives", true,
        "matching: all class tokens, tag, case-insensitive tag, and extra classes");
    expectBoolean(harness, "negatives", true,
        "matching: missing, tag mismatch, exact tokens, and class case sensitivity");
    expectBoolean(harness, "firstResults", true,
        "querySelector/querySelectorAll: first structural match and canonical identity");
    expectBoolean(harness, "canonicalRetrieval", true,
        "document retrieval and selector collections share ordered descriptor identity");
    expectBoolean(harness, "scopedEquivalence", true,
        "scoped selector and retrieval exclude receiver/outside Elements");
    expectBoolean(harness, "scopedTagQuery", true,
        "Element-scoped tag-plus-class selectors retain subtree scope");
    expectBoolean(harness, "scopedIdQuery", true,
        "scoped ID/class selectors preserve subtree bounds and receiver exclusion");
    expectBoolean(harness, "duplicateEquivalence", true,
        "duplicate selector/retrieval requirements normalize idempotently");
    expectBoolean(harness, "subsetInvariants", true,
        "compound results remain subsets of each single-class selector");
    expectBoolean(harness, "idCompatibility", true,
        "ID/class compounds and the existing tag#id form share matching");
    expectBoolean(harness, "universalClassDeferred", true,
        "universal-class compounds remain deferred");
    expectBoolean(harness, "eightBound", true,
        "eight distinct tokens accepted; ninth and ninth duplicate rejected");
    expectBoolean(harness, "idEightBound", true,
        "ID plus eight raw class requirements is accepted and the ninth is rejected");
    expectBoolean(harness, "selectorByteBound", true,
        "256-byte ID/class selector accepted; selector above 256 bytes rejected");
    expectBoolean(harness, "idClassCore", true,
        "querySelector and querySelectorAll find ID/class/full compounds in order");
    expectBoolean(harness, "exactIdClassSemantics", true,
        "tag, exact ID, and every exact class use AND semantics and case rules");
    expectBoolean(harness, "duplicateIdOrder", true,
        "duplicate IDs match every structural candidate in document order");
    expectBoolean(harness, "canonicalIdAndClassIdentity", true,
        "ID and multi-token class retrieval preserve canonical Element identity");
    expectBoolean(harness, "subsetRelationship", true,
        "ID/class results form a canonical-identity subset of multi-token class retrieval");
    expectBoolean(harness, "retrievalApiUnchanged", true,
        "JS44 retrieval remains independent and wildcard behavior is unchanged");
    expect(harness.hostAdapter().limits().maxSelectorCollections ==
            gxos::javascript::kNavigatorScriptMaxSelectorCollections,
        "collections: existing 128-record registry bound remains unchanged");

    const std::string malformed =
        "var malformed = document.querySelector(\".\") === null &&"
        " document.querySelector(\".foo.\") === null &&"
        " document.querySelector(\"..foo\") === null &&"
        " document.querySelector(\".foo..bar\") === null &&"
        " document.querySelector(\"tag.\") === null &&"
        " document.querySelector(\"tag..foo\") === null &&"
        " document.querySelector(\"tag.foo.\") === null &&"
        " document.querySelector(\"#\") === null &&"
        " document.querySelector(\"#.\") === null &&"
        " document.querySelector(\"#save.\") === null &&"
        " document.querySelector(\"#save..action\") === null &&"
        " document.querySelector(\"button#\") === null &&"
        " document.querySelector(\"button#save.\") === null &&"
        " document.querySelector(\"button##save\") === null &&"
        " document.querySelector(\"button#save..action\") === null &&"
        " document.querySelector(\"#one#two\") === null &&"
        " document.querySelector(\"button#one#two\") === null &&"
        " document.querySelector(\"#one.foo#two\") === null &&"
        " document.querySelector(\".action#save\") === null &&"
        " document.querySelector(\".action.primary#save\") === null &&"
        " document.querySelector(\"button.action#save\") === null &&"
        " document.querySelector(\".foo .bar\") === document.querySelector(\"#rel-bar\") &&"
        " document.querySelector(\".foo.bar\") !== document.querySelector(\"#rel-bar\");"
        "var malformedAll = document.querySelectorAll(\".\").length === 0 &&"
        " document.querySelectorAll(\".foo..bar\").length === 0 &&"
        " document.querySelectorAll(\"#one#two\").length === 0;";
    const ScriptResult malformedResult = harness.execute(malformed);
    expect(malformedResult.succeeded(), "parse/malformed: script");
    expectBoolean(harness, "malformed", true,
        "malformed IDs/classes, duplicate IDs, and reordered forms reject; descendant stays distinct");
    expectBoolean(harness, "malformedAll", true,
        "malformed plural selectors fail closed as empty collections");
}

void testRelationsMatchesClosestAndCollectionLifecycle()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var nestedSave = document.querySelector("#nested-save");
var descendantLeft = document.querySelector(".panel.active button.action.primary") === save &&
    document.querySelector(".group.enabled input.field.required") ===
        document.querySelector("#field");
var descendantIdClass =
    document.querySelector("#panel.active button#save.action.primary") === save &&
    document.querySelector("form#owner.form.owner.active input#field.field.required") ===
        document.querySelector("#field");
var descendantRight = document.querySelector("div button.action.primary") === save &&
    document.querySelectorAll("div input.field.required").length === 2;
var bothSides = document.querySelector(".panel.active > button.action.primary") === save &&
    document.querySelectorAll(".panel.active > button.action.primary").length === 2;
var childRightTag = document.querySelector("form > div.panel.active") === panel;
var childRightClasses =
    document.querySelectorAll(".group.enabled > input.field.required").length === 2;
var childIdClass =
    document.querySelector("form#owner.form.owner.active > div#panel.panel.active") === panel &&
    document.querySelector("div#panel.panel.active > button#save.action.primary") === save;
var childRight = childRightTag && childRightClasses;
var adjacent = document.querySelector(".label.required + button.action.primary") === save &&
    save.matches(".label.required + button.action.primary");
var adjacentIdClass =
    document.querySelector("#label.required + button#save.action.primary") === save &&
    save.matches("#label.required + button#save.primary.action");
var generalSibling = document.querySelector(".marker.active ~ button.action") ===
    document.querySelector("#after-marker") &&
    document.querySelectorAll(".marker.active ~ button.action").length === 1;
var generalSiblingIdClass = document.querySelector(
    "#marker.active ~ button#after-marker.action.secondary") ===
        document.querySelector("#after-marker");
var matchesAndClosest = save.matches("button.action.primary") &&
    save.matches("button#save.action.primary") &&
    save.closest("button#save.action.primary") === save &&
    save.closest(".panel.active") === panel &&
    nestedSave.closest(".panel.active") === document.querySelector("#nested-panel") &&
    nestedSave.closest("div#nested-panel.panel.active") ===
        document.querySelector("#nested-panel") &&
    nestedSave.closest("#panel.active") === panel &&
    nestedSave.closest("div.panel.active > button.action.primary") === nestedSave &&
    nestedSave.closest(".missing.active") === null;
var chainRejected = document.querySelector(".panel.active > div.group > input.field") === null &&
    document.querySelectorAll(".panel.active > div.group > input.field").length === 0 &&
    !save.matches(".panel.active > div.group > button.action") &&
    document.querySelector("form#owner.form.owner.active > div#group.group.enabled > input#field.field") === null;
var first = document.querySelectorAll(".action.primary");
var second = document.querySelectorAll(".field.required");
var relational = document.querySelectorAll(".panel.active > button.action.primary");
var relationalOther = document.querySelectorAll(".group.enabled input.field.required");
var idA = document.querySelectorAll("#save.action.primary");
var idB = document.querySelectorAll("#cancel.action.secondary");
var idRelA = document.querySelectorAll("#panel.active > button#save.action.primary");
var idRelB = document.querySelectorAll("#group.enabled input#field.field.required");
var independentDescriptors = first !== second && first !== relational &&
    second !== relationalOther && first.length === 3 && second.length === 2 &&
    relational.length === 2 && relationalOther.length === 2 &&
    first[0] === save && second[0] === document.querySelector("#field") &&
    relational[0] === save && relationalOther[0] === document.querySelector("#field") &&
    idA.length === 1 && idA[0] === save && idB.length === 1 &&
    idB[0] === document.querySelector("#cancel") && idRelA.length === 1 &&
    idRelA[0] === save && idRelB.length === 1 &&
    idRelB[0] === document.querySelector("#field");
var indexedMiss = first[999] === undefined && first.item === undefined &&
    first.namedItem === undefined;
)JS");
    expect(result.succeeded(), "relations/lifecycle: script");
    expectBoolean(harness, "descendantLeft", true,
        "descendant relation accepts compound left and right descriptors");
    expectBoolean(harness, "descendantRight", true,
        "descendant compound queries retain structural document order");
    expectBoolean(harness, "descendantIdClass", true,
        "descendant relation accepts ID/class compounds on either side");
    expectBoolean(harness, "bothSides", true,
        "both sides of one relation may carry compound class requirements");
    expectBoolean(harness, "childRight", true,
        "child relation keeps immediate structural parent semantics");
    expectBoolean(harness, "childRightTag", true,
        "tag compound child relation uses immediate structural parent");
    expectBoolean(harness, "childRightClasses", true,
        "class compound child relation uses immediate structural parent");
    expectBoolean(harness, "childIdClass", true,
        "child relation applies rich left and right descriptors to immediate parents");
    expectBoolean(harness, "adjacent", true,
        "adjacent sibling accepts compounds and keeps sibling direction");
    expectBoolean(harness, "adjacentIdClass", true,
        "adjacent sibling matches ID/class compounds on the shared matcher");
    expectBoolean(harness, "generalSibling", true,
        "general sibling accepts compounds with existing backward search");
    expectBoolean(harness, "generalSiblingIdClass", true,
        "general sibling matches the richer right-side ID/class compound");
    expectBoolean(harness, "matchesAndClosest", true,
        "matches and closest use the same full compound matcher");
    expectBoolean(harness, "chainRejected", true,
        "arbitrary combinator chains remain rejected");
    expectBoolean(harness, "independentDescriptors", true,
        "simultaneous simple and relational collections retain independent descriptors");
    expectBoolean(harness, "indexedMiss", true,
        "bounded selector collections retain indexed-miss behavior");
    expectError(harness.execute("first[0] = save;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "collections: indexed writes stay read-only");
    expectError(harness.execute("first.length = 0;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "collections: length stays read-only");

    auto* field = elementById(harness, "field");
    expect(field != nullptr, "collections: live class Element exists");
    if (field != nullptr) field->className = "field optional";
    const ScriptResult live = harness.execute(
        "var liveDescriptor = second.length === 1 && second[0].id === \"deep-field\";");
    expect(live.succeeded(), "collections: live descriptor script");
    expectBoolean(harness, "liveDescriptor", true,
        "held selector collection reevaluates its copied descriptor on reads");
    if (field != nullptr) field->className = "field required";

    NavigatorScriptHostLimits limits;
    limits.maxSelectorCollections = 2u;
    NavigatorScriptExecutionHarness bounded(
        gxos::javascript::RuntimeLimits(), limits);
    loadFixture(bounded, error);
    const ScriptResult capacity = bounded.execute(R"JS(
var first = document.querySelectorAll(".action.primary");
var canonical = document.querySelectorAll(".primary.action");
var retrieval = document.getElementsByClassName("action primary");
var second = document.querySelectorAll(".field.required");
var sharedSlots = first === canonical && first === retrieval &&
    first.length === 3 && second.length === 2;
)JS");
    expect(capacity.succeeded(), "collections: bounded registry script");
    expectBoolean(bounded, "sharedSlots", true,
        "retrieval-equivalent canonical descriptors share their existing registry slot");
    expectError(bounded.execute("document.querySelectorAll(\".action.secondary\");"),
        RuntimeErrorCode::DocumentLookupLimitExceeded,
        "collections: compound selectors do not increase configured capacity");
    expect(bounded.hostAdapter().limits().maxSelectorCollections == 2u,
        "collections: configured registry bound is preserved");
}

void testStaleGenerationAndPurity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldSave = serialById(harness, "save");
    const std::uint64_t fieldSerial = serialById(harness, "field");
    const ScriptResult capture = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var oldCompoundCollection = document.querySelectorAll("#save.action.primary");
var oldScopedCollection = panel.querySelectorAll("button#save.action.primary");
var oldResult = oldCompoundCollection[0];
var oldField = document.querySelector("#field.field.required");
)JS");
    expect(capture.succeeded(), "stale: capture compound descriptors and Elements");
    expect(harness.invalidateDocumentGeneration(error),
        "stale: invalidate document generation");
    expect(error == RuntimeErrorCode::None, "stale: invalidation no error");
    const ScriptResult stale = harness.execute(R"JS(
var stalePredicates = oldResult.matches("#save.action.primary") === false &&
    oldResult.closest("#panel.active") === null &&
    oldField.matches("#field.field.required") === false &&
    oldField.closest("#group.group.enabled") === null;
)JS");
    expect(stale.succeeded(), "stale: compound predicates fail closed");
    expectBoolean(harness, "stalePredicates", true,
        "stale matches returns false and stale closest returns null");
    expectError(harness.execute("oldCompoundCollection.length;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: document compound collection rejects old generation");
    expectError(harness.execute("oldScopedCollection[0];"),
        RuntimeErrorCode::StaleHostObject,
        "stale: scoped compound collection rejects old generation");
    expectError(harness.execute("oldResult.id;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: individual query result rejects old generation");
    expectError(harness.execute("oldField.id;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: individual scoped candidate rejects old generation");

    harness.document() = gxos::web::parseHtml(
        "file:///js46-replacement.html", kFixture);
    expect(oldSave == serialById(harness, "save") &&
            fieldSerial == serialById(harness, "field"),
        "stale: replacement fixture deliberately reuses serials");
    expect(harness.runtime().installHostGlobal("newSave",
            serialById(harness, "save"),
            gxos::javascript::kNavigatorElementHostKind, error),
        "stale: install replacement generation Element");
    expect(harness.runtime().installHostGlobal("newPanel",
            serialById(harness, "panel"),
            gxos::javascript::kNavigatorElementHostKind, error),
        "stale: install replacement generation scope");
    const ScriptResult replacement = harness.execute(R"JS(
var freshWorks = newSave.matches("button#save.action.primary") &&
    newSave.closest("div#panel.panel.active") === newPanel &&
    newPanel.querySelectorAll("button#save.action.primary")[0] === newSave;
)JS");
    if (!replacement.succeeded())
        std::cerr << "  stale replacement runtime error=" << static_cast<int>(
            replacement.runtimeError.code) << " line=" <<
            replacement.runtimeError.location.line << " column=" <<
            replacement.runtimeError.location.column << "\n";
    expect(replacement.succeeded(), "stale: replacement script");
    expectBoolean(harness, "freshWorks", true,
        "old compound descriptors cannot resolve reused serials in new generation");
}

void testEventDelegationNestedDispatchAndPurity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t saveSerial = serialById(harness, "save");
    const std::uint64_t fieldSerial = serialById(harness, "field");
    const gxos::web::HtmlElementRef* saveElement =
        elementById(harness, "save");
    const std::string saveIdBefore = saveElement == nullptr
        ? std::string() : saveElement->id;
    const std::string saveClassesBefore = saveElement == nullptr
        ? std::string() : saveElement->className;
    const ScriptResult setup = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var form = document.querySelector("#owner");
var save = document.querySelector("#save");
var outside = document.querySelector("#outside");
var field = document.querySelector("#field");
var delegated = false;
var nestedDispatch = false;
var metadataPreserved = false;
var nestedDone = false;
var submitCompound = false;
var resetCompound = false;
save.addEventListener("click", function(event) {
  if (!nestedDone) {
    nestedDone = true;
    event.preventDefault();
    outside.click();
  }
});
outside.addEventListener("click", function(event) {
  nestedDispatch = event.target === outside &&
    event.target.matches("button#outside.action.primary") &&
    event.target.closest("#panel.active") === null &&
    event.currentTarget === outside && event.eventPhase === 2 &&
    event.relatedTarget === null && event.defaultPrevented === false;
});
panel.addEventListener("click", function(event) {
  if (event.target === save) {
    var targetBefore = event.target;
    var currentBefore = event.currentTarget;
    var phaseBefore = event.eventPhase;
    var relatedBefore = event.relatedTarget;
    delegated = event.target.matches("button#save.action.primary") &&
      event.target.closest("#panel.active > button#save.action.primary") === save;
    metadataPreserved = event.target === targetBefore &&
      event.currentTarget === currentBefore && currentBefore === panel &&
      event.eventPhase === phaseBefore && phaseBefore === 3 &&
      event.relatedTarget === relatedBefore && relatedBefore === null &&
      event.defaultPrevented === true;
  }
});
form.addEventListener("submit", function(event) {
  submitCompound = event.target.matches("form#owner.form.owner.active") &&
    event.target.closest("form#owner.form.owner.active") === form &&
    event.defaultPrevented === false;
  event.preventDefault();
});
form.addEventListener("reset", function(event) {
  resetCompound = event.target.matches("form#owner.form.owner.active") &&
    event.target.closest("form#owner.form.owner.active") === form &&
    event.defaultPrevented === false;
});
)JS");
    expect(setup.succeeded(), "events: listeners use compound selectors");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "events: listener registry stays within its 64-listener cap");

    const ScriptResult focusResult = harness.execute(
        "document.querySelector(\"#field.field.required\").focus();");
    expect(focusResult.succeeded(),
        "focus: selected ID/class Element retains normal focus behavior");
    expect(harness.focusedElementSerial() == fieldSerial,
        "focus: selected Element becomes the canonical activeElement");
    expect(harness.focusElement(fieldSerial, error),
        "purity: establish focus before dispatch");
    expect(error == RuntimeErrorCode::None, "purity: focus setup no error");
    const bool dirtyBefore = harness.documentDirty();
    const std::uint64_t revisionBefore = harness.layoutRevision();
    const std::uint64_t mutationBefore = harness.document().scriptMutationCount;
    const std::uint64_t generationBefore = harness.runtime().hostGeneration();
    const std::size_t structuralCountBefore =
        harness.document().structuralElements.size();
    const std::size_t listenersBefore =
        harness.hostAdapter().clickListenerCount();
    bool defaultPrevented = false;
    expect(harness.dispatchClick(saveSerial, error, &defaultPrevented),
        "events: authentic click dispatch");
    expect(error == RuntimeErrorCode::None, "events: dispatch has no runtime error");
    expect(defaultPrevented, "events: preventDefault behavior remains intact");
    expectBoolean(harness, "delegated", true,
        "event delegation uses event.target.matches and closest compounds");
    expectBoolean(harness, "nestedDispatch", true,
        "nested dispatch does not corrupt selector descriptor scratch");
    expectBoolean(harness, "metadataPreserved", true,
        "target/currentTarget/phase/relatedTarget/defaultPrevented are preserved");
    expect(harness.hostAdapter().clickListenerCount() == listenersBefore &&
            harness.hostAdapter().clickListenerCount() <= 64u,
        "purity: event listener registry remains bounded and unchanged");
    const ScriptResult state = harness.execute(R"JS(
var focusUnchanged = document.activeElement === field &&
    document.activeElement.matches("#field.field.required");
var valuesUnchanged = field.value === "field-seed" &&
    field.defaultValue === "field-seed" &&
    document.querySelector("#deep-field").value === "deep-seed";
)JS");
    expect(state.succeeded(), "purity: state inspection script");
    expectBoolean(harness, "focusUnchanged", true,
        "selectors and event delegation do not change focused Element");
    expectBoolean(harness, "valuesUnchanged", true,
        "selectors do not change current/default control values");
    expect(harness.documentDirty() == dirtyBefore,
        "purity: document dirty state unchanged");
    expect(harness.layoutRevision() == revisionBefore,
        "purity: layout revision unchanged");
    expect(harness.document().scriptMutationCount == mutationBefore,
        "purity: script mutation count unchanged");
    expect(harness.document().structuralElements.size() == structuralCountBefore,
        "purity: structural tree size unchanged");
    expect(saveElement != nullptr && saveElement->id == saveIdBefore &&
            saveElement->className == saveClassesBefore,
        "purity: matching leaves ID and class attributes unchanged");
    expect(harness.runtime().hostGeneration() == generationBefore,
        "purity: generation unchanged");
    expect(harness.focusedElementSerial() == fieldSerial,
        "purity: focus owner identity unchanged");
    const ScriptResult submit = harness.execute("field.value = \"changed\";");
    expect(submit.succeeded(), "submit/reset: prepare changed form value");
    bool submitDefaultPrevented = false;
    const std::uint64_t ownerSerial = serialById(harness, "owner");
    expect(harness.dispatchSubmit(ownerSerial, error, &submitDefaultPrevented),
        "submit: authentic host submit dispatch");
    expect(error == RuntimeErrorCode::None, "submit: dispatch no runtime error");
    expect(submitDefaultPrevented, "submit: preventDefault behavior is preserved");
    expectBoolean(harness, "submitCompound", true,
        "submit listener can match compound class selectors without changing default action");
    const ScriptResult reset = harness.execute("form.reset();");
    expect(reset.succeeded(), "reset: form reset dispatch");
    expectBoolean(harness, "resetCompound", true,
        "reset listener can match compound class selectors");
    const ScriptResult resetState = harness.execute(
        "var resetRestored = field.value === \"field-seed\" && field.defaultValue === \"field-seed\";");
    expect(resetState.succeeded(), "reset: verify default-action state");
    expectBoolean(harness, "resetRestored", true,
        "compound matching preserves reset default values");
}

} // namespace

int main()
{
    testParsingMatchingAndRetrievalEquivalence();
    testRelationsMatchesClosestAndCollectionLifecycle();
    testStaleGenerationAndPurity();
    testEventDelegationNestedDispatchAndPurity();
    if (failures != 0) {
        std::cerr << failures << " JS46 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS46 checks: " << (checks - failures)
        << "/" << checks << " passed\n";
    return 0;
}
