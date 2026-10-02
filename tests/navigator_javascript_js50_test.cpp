#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;
int checks = 0;
std::string fixture;

void expect(bool condition, const std::string& label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << label << "\n";
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
    const bool matched = value != nullptr && value->isNumber() &&
        value->numberValue() == expected;
    if (!matched && value != nullptr && value->isNumber())
        std::cerr << "INFO: " << label << " actual=" << value->numberValue()
            << " expected=" << expected << "\n";
    expect(matched, label);
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(!fixture.empty(), "fixture file is available");
    expect(harness.loadHtml("file:///javascript-js50.html", fixture, error),
        "JS50 fixture parses into the authoritative document");
    expect(error == RuntimeErrorCode::None, "fixture load has no error");
    expect(harness.relayout(), "fixture relayout succeeds");
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

void testParsingAndMatching()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("[data-mode=active]");
var presence = document.querySelector("[data-action]") === document.getElementById("save");
var exact = document.querySelector("[data-action=save]") === document.getElementById("save");
var emptyPresence = document.querySelector("[data-empty]") === panel;
var emptyExact = panel.matches("[data-empty='']");
var nameCase = panel.matches("[DATA-STATE=Ready]") && panel.matches("[Data-State=Ready]");
var valueCase = panel.matches("[data-state=Ready]") && !panel.matches("[data-state=ready]");
var valueless = panel.hasAttribute("custom-flag") && panel.matches("[custom-flag]");
var dataExact = document.querySelector("[data-action=save]").getAttribute("data-action") === "save";
var ariaPresence = panel.matches("[aria-label]") && panel.hasAttribute("ARIA-LABEL");
var titleExact = panel.matches("[title=tip]");
var customExact = panel.matches("[custom-flag='']");
var hrefExact = panel.matches("[href=/docs]");
var srcExact = panel.matches("[src=/logo.png]");
var tagPresence = document.querySelector("button[data-action]") === document.getElementById("save");
var tagExact = document.querySelector("button[data-action=save]") === document.getElementById("save");
var idPresence = document.querySelector("#save[data-action]") === document.getElementById("save");
var idExact = document.querySelector("#save[data-action=save]") === document.getElementById("save");
var classPresence = document.querySelector(".action[data-action]") === document.getElementById("save");
var classExact = document.querySelector(".action.primary[data-action=save]") === document.getElementById("save");
var fullCompound = document.querySelector("button#save.action.primary[data-action=save]") === document.getElementById("save");
var quotedDecoded = panel.matches("[data-label='Tom & Jerry']");
panel.setAttribute("data-punctuation", "a>+~ b");
panel.setAttribute("data-punctuation", "a>+~ b=ok");
var quotedPunctuation = panel.matches("[data-punctuation=\"a>+~ b=ok\"]");
panel.setAttribute("xml:lang", "en");
var colonName = panel.matches("[xml:lang=en]");
var malformedEmpty = document.querySelector("[]") === null && document.querySelector("[=x]") === null;
var malformedMissingClose = document.querySelector("[data-action") === null;
var malformedTrailingClose = document.querySelector("data-action]") === null;
var malformedEquals = document.querySelector("[data-action==save]") === null;
var malformedName = document.querySelector("[data action]") === null;
var malformedEmptyExact = document.querySelector("[data-action=]") === null;
var malformedBadQuote = document.querySelector("[data-action='save]") === null;
var malformedRepeated = document.querySelector("[data-action][data-mode]") === null;
var malformedRepeatedTag = document.querySelector("button[data-action][data-mode]") === null;
var malformedOperator = document.querySelector("[data-action^=save]") === null;
var malformedNamespace = document.querySelector("[foo|bar=x]") === null;
var malformedModifier = document.querySelector("[data-action=save i]") === null;
var universalDeferred = document.querySelector("*[data-action]") === null;
var malformedOrder = document.querySelector("button[data-action]#save") === null && document.querySelector("button[data-action].action") === null && document.querySelector("[data-action]button") === null;
var malformedRelationChain = document.querySelector(".panel > button > input") === null;
var entityValueCaseExact = !panel.matches("[data-label='Tom & jerry']");
var missingEmptyExact = !panel.matches("[missing='']");
)JS");
    expect(result.succeeded(), "parser/matcher cases execute without errors");
    const char* names[] = {
        "presence", "exact", "emptyPresence", "emptyExact", "nameCase",
        "valueCase", "valueless", "dataExact", "ariaPresence", "titleExact",
        "customExact", "hrefExact", "srcExact", "tagPresence", "tagExact",
        "idPresence", "idExact", "classPresence", "classExact", "fullCompound",
        "quotedDecoded", "quotedPunctuation", "colonName", "malformedEmpty",
        "malformedMissingClose", "malformedTrailingClose", "malformedEquals",
        "malformedName", "malformedEmptyExact", "malformedBadQuote",
        "malformedRepeated", "malformedRepeatedTag", "malformedOperator",
        "malformedNamespace", "malformedModifier", "universalDeferred", "malformedOrder",
        "malformedRelationChain", "entityValueCaseExact", "missingEmptyExact",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("grammar: ") + name);

    const std::string maxName = "a" + std::string(63u, 'b');
    const std::string maxValue(128u, 'x');
    const std::string overlongValue(129u, 'x');
    const std::string oversizedSelector(257u, ' ');
    const std::string boundsScript =
        std::string("var boundedPanel = document.getElementById(\"panel\");\n") +
        "var maxName = \"" + maxName + "\";\n" +
        "boundedPanel.setAttribute(maxName, \"ok\");\n" +
        "var maxAttributeName = boundedPanel.matches(\"[\" + maxName + \"]\");\n" +
        "var overName = maxName + \"b\";\n" +
        "var overAttributeNameRejected = document.querySelector(\"[\" + overName + \"]\") === null;\n" +
        "var maxValue = \"" + maxValue + "\";\n" +
        "boundedPanel.setAttribute(\"data-max-value\", maxValue);\n" +
        "var maxSelectorValue = boundedPanel.matches(\"[data-max-value='" + maxValue + "']\");\n" +
        "var overSelectorValueRejected = document.querySelector(\"[data-max-value='" + overlongValue + "']\") === null;\n" +
        "var oversizedSelectorRejected = document.querySelector(\"" + oversizedSelector + "\") === null;\n";
    const ScriptResult bounds = harness.execute(boundsScript);
    expect(bounds.succeeded(), "selector byte/name/value bound cases execute");
    expectBoolean(harness, "maxAttributeName", true,
        "selector accepts a 64-byte attribute name");
    expectBoolean(harness, "overAttributeNameRejected", true,
        "selector rejects an attribute name longer than 64 bytes");
    expectBoolean(harness, "maxSelectorValue", true,
        "selector accepts a 128-byte quoted exact value");
    expectBoolean(harness, "overSelectorValueRejected", true,
        "selector rejects a quoted exact value longer than 128 bytes");
    expectBoolean(harness, "oversizedSelectorRejected", true,
        "selector input longer than 256 bytes fails closed");
}

void testQueryCollectionsMutationAndForms()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.getElementById("panel");
var save = document.getElementById("save");
var scopedFirst = panel.querySelector("button[data-action=save]") === save;
var scopedAll = panel.querySelectorAll("[data-state=ready]").length === 1;
var scopedSelfExcluded = panel.querySelector("[data-mode=active]") !== panel;
var scopedOutsideExcluded = panel.querySelector("#outside") === null;
var allActions = document.querySelectorAll("[data-action]").length === 3;
var exactActions = document.querySelectorAll("[data-action=save]").length === 2;
var collectionFirst = document.querySelectorAll("[data-action=save]")[0] === save;
var idIdentity = document.querySelector("[data-action=save]") === document.getElementById("save");
var ready = document.querySelectorAll("[data-state=ready]");
var readyCountBefore = ready.length;
var readyBefore = ready.length === 2;
save.setAttribute("data-state", "ready");
var readyCountAfterInsert = ready.length;
var mutationVisible = save.matches("[data-state=ready]") && save.hasAttribute("DATA-STATE") && save.getAttribute("data-state") === "ready";
var liveInsert = ready.length === 3 && ready[0] === save;
save.setAttribute("data-state", "busy");
var readyCountAfterReplace = ready.length;
var replacementVisible = !save.matches("[data-state=ready]") && save.matches("[data-state=busy]") && ready.length === 2;
var busy = document.querySelectorAll("[data-state=busy]");
save.removeAttribute("data-state");
var readyCountAfterRemove = ready.length;
var removalVisible = !save.matches("[data-state]") && !save.matches("[data-state=busy]") && save.getAttribute("data-state") === null && !save.hasAttribute("data-state");
var liveRemoval = busy.length === 0 && ready.length === 2;
var formValue = document.getElementById("field");
formValue.value = "edited";
var valueDefaultRetained = formValue.matches("[value=initial]") && !formValue.matches("[value=edited]") && formValue.getAttribute("value") === "initial";
var checked = document.getElementById("agree");
checked.checked = false;
var checkedDefaultRetained = checked.matches("[checked]") && checked.getAttribute("checked") === "";
var option = document.getElementById("option");
option.selected = false;
var selectedDefaultRetained = option.matches("[selected]") && option.getAttribute("selected") === "";
var disabledBoolean = document.getElementById("save").matches("[disabled]") &&
    document.getElementById("save").matches("[disabled='']") &&
    !document.getElementById("save").matches("[disabled=disabled]");
var hrefRetained = panel.matches("[href=/docs]") && panel.getAttribute("HREF") === "/docs";
var srcRetained = panel.matches("[src=/logo.png]");
var stressMatches = true;
for (var i = 0; i < 300; i = i + 1) {
    if (!document.getElementById("field").matches("input[data-state=ready]")) stressMatches = false;
}
var stressQueries = true;
for (var j = 0; j < 200; j = j + 1) {
    if (document.querySelectorAll("[data-state=ready]").length !== 2) stressQueries = false;
}
)JS");
    expect(result.succeeded(), "query, mutation, collection, and form cases execute");
    const char* names[] = {
        "scopedFirst", "scopedAll", "scopedSelfExcluded", "scopedOutsideExcluded",
        "allActions", "exactActions", "collectionFirst", "idIdentity", "readyBefore",
        "mutationVisible", "liveInsert", "replacementVisible", "removalVisible",
        "liveRemoval", "valueDefaultRetained", "checkedDefaultRetained",
        "selectedDefaultRetained", "disabledBoolean", "hrefRetained", "srcRetained",
        "stressMatches", "stressQueries",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("integration: ") + name);
    expectNumber(harness, "readyCountBefore", 2.0,
        "live collection initially sees the field and outside Element");
    expectNumber(harness, "readyCountAfterInsert", 3.0,
        "live collection sees insertion after replacement");
    expectNumber(harness, "readyCountAfterReplace", 2.0,
        "live collection sees exact-value replacement");
    expectNumber(harness, "readyCountAfterRemove", 2.0,
        "live collection sees removal");
}

void testRelationsAndEvents()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.getElementById("panel");
var save = document.getElementById("save");
var descendantLeft = document.querySelector(".panel[data-mode=active] button[data-action=save]") === save;
var descendantRight = document.querySelector(".panel button[data-action=save]") === save;
var childBothSides = document.querySelector(".panel[data-mode=active] > button[data-action=save]") === save;
var childRelation = document.querySelector("#panel[data-mode=active] > #save[data-action=save]") === save;
var adjacent = document.querySelector("label[data-required=yes] + input[data-state=ready]") === document.getElementById("field");
var generalSibling = document.querySelector(".marker[data-state=active] ~ button[data-action=save]") === document.getElementById("next");
var closestPanel = save.closest("[data-mode=active]") === panel;
var closestCompound = save.closest(".panel[data-mode=active]") === panel;
var eventTargetMatch = false;
var eventMutationMatch = false;
var nestedEventMatch = false;
var outerMetadata = false;
var innerMetadata = false;
var outerEvent = false;
var other = document.getElementById("other");
other.addEventListener("click", function (e) {
    e.target.setAttribute("data-nested", "yes");
    nestedEventMatch = e.target.matches("[data-nested=yes]");
    innerMetadata = e.target === other && e.currentTarget === other && e.eventPhase === 2;
});
panel.addEventListener("click", function (e) {
    if (e.target.matches("[data-action=save]")) {
        eventTargetMatch = true;
        e.target.setAttribute("data-state", "clicked");
        eventMutationMatch = e.target.matches("[data-state=clicked]") && document.querySelector("button[data-state=clicked]") === save;
        outerMetadata = e.target === save && e.currentTarget === panel && e.defaultPrevented === false;
        other.click();
        outerEvent = e.target === save && e.currentTarget === panel && e.eventPhase === 3;
    }
});
save.click();
var eventAfterMutation = save.matches("[data-state=clicked]");
)JS");
    expect(result.succeeded(), "relations and nested attribute-selector event dispatch execute");
    const char* names[] = {
        "descendantLeft", "descendantRight", "childBothSides", "childRelation",
        "adjacent", "generalSibling", "closestPanel", "closestCompound",
        "eventTargetMatch", "eventMutationMatch", "nestedEventMatch",
        "outerMetadata", "innerMetadata", "outerEvent", "eventAfterMutation",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("relations/events: ") + name);
}

void testSelectorPurity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t generation = harness.hostAdapter().generation();
    const std::size_t retainedRecords =
        harness.document().retainedAttributeRecordCount;
    const std::size_t retainedBytes =
        harness.document().retainedAttributeStorage.size();
    const gxos::web::HtmlElementRef* save = nullptr;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == "save") {
            save = &element;
            break;
        }
    }
    expect(save != nullptr, "purity: fixture Element exists");
    const std::string idBefore = save == nullptr ? std::string() : save->id;
    const std::string classBefore = save == nullptr
        ? std::string() : save->className;

    const ScriptResult result = harness.execute(R"JS(
var saveForPurity = document.querySelector("[data-action=save]");
var retainedForPurity = saveForPurity.matches("button.action[data-action=save]") &&
    saveForPurity.closest("[data-mode=active]") === document.getElementById("panel");
var liveForPurity = document.querySelectorAll("[data-action]");
var collectionForPurity = liveForPurity.length === 3 && liveForPurity[0] === saveForPurity;
)JS");
    expect(result.succeeded(), "purity: attribute selector reads execute");
    expectBoolean(harness, "retainedForPurity", true,
        "purity: matches and closest return expected results");
    expectBoolean(harness, "collectionForPurity", true,
        "purity: querySelectorAll returns a live canonical collection");
    expect(harness.hostAdapter().generation() == generation,
        "purity: selector reads preserve document generation");
    expect(harness.document().retainedAttributeRecordCount == retainedRecords &&
        harness.document().retainedAttributeStorage.size() == retainedBytes,
        "purity: selector reads do not mutate authoritative attribute storage");
    save = nullptr;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == "save") {
            save = &element;
            break;
        }
    }
    expect(save != nullptr && save->id == idBefore &&
        save->className == classBefore,
        "purity: ID and class projections remain unchanged");
}

void testBoundedCollectionsAndStaleReuse()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult stress = harness.execute(R"JS(
var collectionCount = 0;
var collectionIntegrity = true;
for (var i = 0; i < 120; i = i + 1) {
    var result = document.querySelectorAll("[data-probe=v" + i + "]");
    if (result.length !== 0) collectionIntegrity = false;
    collectionCount = collectionCount + 1;
}
var nearCapacityQuery = document.querySelectorAll("[data-action=save]").length === 2;
)JS");
    expect(stress.succeeded(), "many distinct bounded live collections execute");
    expectBoolean(harness, "collectionIntegrity", true,
        "selector collection descriptors remain intact near registry capacity");
    const Value* count = binding(harness, "collectionCount");
    expect(count != nullptr && count->isNumber() && count->numberValue() == 120.0,
        "120 distinct live selector collections remain available");
    expectBoolean(harness, "nearCapacityQuery", true,
        "a normal attribute query remains valid after collection stress");

    const std::uint64_t oldSerial = serialById(harness, "save");
    expect(harness.execute(R"JS(
var oldSave = document.querySelector("[data-action=save]");
var oldCollection = document.querySelectorAll("[data-action=save]");
)JS").succeeded(), "stale test captures attribute-selected Element and collection");
    expect(harness.invalidateDocumentGeneration(error),
        "stale test invalidates document generation");
    const ScriptResult stale = harness.execute(R"JS(
var staleMatch = oldSave.matches("[data-action=save]") === false;
var staleClosest = oldSave.closest("[data-action=save]") === null;
)JS");
    expect(stale.succeeded(), "stale selector calls fail closed without throwing");
    expectBoolean(harness, "staleMatch", true, "stale Element match fails closed");
    expectBoolean(harness, "staleClosest", true, "stale Element closest fails closed");
    const ScriptResult staleCollectionRead =
        harness.execute("var staleCollectionLength = oldCollection.length;");
    expect(!staleCollectionRead.succeeded(),
        "stale attribute-selector collection cannot read current-generation records");

    harness.document() = gxos::web::parseHtml(
        "file:///js50-replacement.html", fixture);
    const std::uint64_t newSerial = serialById(harness, "save");
    expect(newSerial == oldSerial,
        "replacement fixture deliberately reuses the original Element serial");
    expect(harness.runtime().installHostGlobal("newSave", newSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "replacement Element is installed in the current generation");
    const ScriptResult replacement = harness.execute(R"JS(
var replacementMatches = newSave.matches("[data-action=save]");
var staleStillFails = oldSave.matches("[data-action=save]") === false;
)JS");
    expect(replacement.succeeded(), "serial reuse selector calls execute safely");
    expectBoolean(harness, "replacementMatches", true,
        "current Element resolves its own retained attribute");
    expectBoolean(harness, "staleStillFails", true,
        "old Element cannot resolve reused-serial attributes");
    const ScriptResult reusedCollectionRead =
        harness.execute("var reusedCollectionLength = oldCollection.length;");
    expect(!reusedCollectionRead.succeeded(),
        "old collection cannot resolve replacement records after serial reuse");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js50.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testParsingAndMatching();
    testQueryCollectionsMutationAndForms();
    testRelationsAndEvents();
    testSelectorPurity();
    testBoundedCollectionsAndStaleReuse();
    if (failures != 0) {
        std::cerr << failures << " JS50 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS50 checks: " << checks
        << "/" << checks << " passed\n";
    return 0;
}
