#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

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
    expect(matched, label);
}

void reportScriptFailure(const ScriptResult& result, const char* label)
{
    if (result.succeeded()) return;
    std::cerr << "INFO: " << label << " error="
        << gxos::javascript::runtimeErrorCodeName(result.runtimeError.code)
        << " at=" << result.runtimeError.location.line << ":"
        << result.runtimeError.location.column << "\n";
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const char* label)
{
    expect(!fixture.empty(), "JS53 fixture is available");
    expect(harness.loadHtml("file:///javascript-js53.html", fixture, error),
        std::string(label) + ": fixture parses");
    expect(error == RuntimeErrorCode::None,
        std::string(label) + ": fixture load succeeds");
    expect(harness.relayout(), std::string(label) + ": relayout succeeds");
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

void testParserAndSelectorComposition()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "parser");
    const ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var middle = document.getElementById("middle");
var last = document.getElementById("last");
var only = document.getElementById("only");
var parserBare = first.matches(":first-child") && last.matches(":last-child") &&
  only.matches(":only-child");
var parserCase = first.matches(":FIRST-CHILD") &&
  first.matches(":First-Child") && last.matches(":LaSt-ChIlD") &&
  only.matches(":OnLy-ChIlD");
var parserUniversal = first.matches("*:first-child") &&
  last.matches("*:last-child") && only.matches("*:only-child");
var parserTag = first.matches("button:first-child") &&
  last.matches("button:last-child") && only.matches("span:only-child");
var parserId = first.matches("#first:first-child") &&
  last.matches("#last:last-child") && only.matches("#only:only-child");
var parserClass = first.matches(".action:first-child") &&
  last.matches(".action:last-child") && only.matches(".item:only-child");
var parserAttribute = first.matches("[data-role=primary]:first-child") &&
  only.matches("[data-role=primary]:only-child");
var parserFullCompound = first.matches(
  "button#first.item.action[data-role=primary]:first-child") &&
  only.matches("span#only.item[data-role=primary]:only-child");
var parserRejectsMalformed = document.querySelector(":firstchild") === null &&
  document.querySelector(":first--child") === null &&
  document.querySelector(":first-child-extra") === null &&
  document.querySelector(":lastchild") === null &&
  document.querySelector(":only--child") === null;
var parserRejectsFunctional = document.querySelector(":first-child()") === null &&
  document.querySelector(":last-child()") === null &&
  document.querySelector(":only-child()") === null;
var parserRejectsUnknownAndSecond = document.querySelector(":hover") === null &&
  document.querySelector("#first:first-child:last-child") === null &&
  !first.matches("button:first-child:last-child");
var parserCanonicalOrder = only.matches(
  "span#only.item[data-role=primary]:only-child") &&
  !only.matches("span:only-child.item") &&
  !only.matches("span[data-role=primary]:only-child#only");
var parserPseudoIsIndependent = only.matches(":only-child") &&
  only.matches(":first-child") && only.matches(":last-child");
var wrongCompounds = !first.matches("input:first-child") &&
  !first.matches(".missing:first-child") &&
  !first.matches("#last:first-child") &&
  !first.matches("[data-role=wrong]:first-child");
)JS");
    expect(result.succeeded(), "pseudo parser and selector composition execute");
    const char* names[] = {
        "parserBare", "parserCase", "parserUniversal", "parserTag",
        "parserId", "parserClass", "parserAttribute", "parserFullCompound",
        "parserRejectsMalformed", "parserRejectsFunctional",
        "parserRejectsUnknownAndSecond", "parserCanonicalOrder",
        "parserPseudoIsIndependent", "wrongCompounds",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("parser: ") + name);

    const std::string boundarySelector = "#only:only-child, #" +
        std::string(237u, 'x');
    expect(boundarySelector.size() == 256u,
        "selector-list boundary fixture is exactly 256 bytes");
    const ScriptResult boundary = harness.execute(
        "var selectorAt256 = document.querySelector(\"" +
        boundarySelector + "\") === only; var selectorAt257 = "
        "document.querySelector(\"" + boundarySelector +
        "x\") === null && document.querySelectorAll(\"" +
        boundarySelector + "x\").length === 0;");
    expect(boundary.succeeded(), "selector length boundary executes");
    reportScriptFailure(boundary, "selector length boundary");
    expectBoolean(harness, "selectorAt256", true,
        "the complete 256-byte selector-list input is accepted");
    expectBoolean(harness, "selectorAt257", true,
        "the 257-byte selector-list input is rejected as a whole");
}

void testStructuralRulesQueriesAndRelations()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "structure");
    const ScriptResult result = harness.execute(R"JS(
var panel = document.getElementById("panel");
var first = document.getElementById("first");
var middle = document.getElementById("middle");
var last = document.getElementById("last");
var singleParent = document.getElementById("single-parent");
var only = document.getElementById("only");
var nestedButton = document.getElementById("nested-button");
var nestedFirst = document.getElementById("nested-first");
var basicFirst = first.matches(":first-child") &&
  !middle.matches(":first-child") && !last.matches(":first-child");
var basicLast = last.matches(":last-child") &&
  !middle.matches(":last-child") && !first.matches(":last-child");
var basicOnly = only.matches(":only-child") && only.matches(":first-child") &&
  only.matches(":last-child");
var twoSided = panel.childElementCount === 3 &&
  panel.firstElementChild === first && panel.lastElementChild === last &&
  first.previousElementSibling === null && first.nextElementSibling === middle &&
  middle.previousElementSibling === first && middle.nextElementSibling === last &&
  last.previousElementSibling === middle && last.nextElementSibling === null;
var onlyTraversal = singleParent.childElementCount === 1 &&
  singleParent.firstElementChild === only && singleParent.lastElementChild === only &&
  singleParent.children.length === 1 && singleParent.children[0] === only;
var root = document.querySelector("html");
var parentRequired = root !== null && root.parentElement === null &&
  !root.matches(":first-child") && !root.matches(":last-child") &&
  !root.matches(":only-child");
var hiddenStillStructural = document.getElementById("hidden").matches(":first-child") &&
  document.getElementById("visible").matches(":last-child");
var relationDescendantLeft = document.querySelector(
  ".panel:first-child .item:last-child") === last;
var relationDescendantRight = document.querySelector(
  ".panel .item:first-child") === first;
var relationChildBoth = document.querySelector(
  ".panel:first-child > .item:last-child") === last;
var relationAdjacent = document.querySelector(
  ".item:first-child + input") === middle &&
  document.querySelector("input + .item:last-child") === last;
var relationGeneral = document.querySelector(
  ".item:first-child ~ .item:last-child") === last;
var relationBothSides = document.querySelector(
  ".panel:first-child > .item:last-child") === last;
var chainRejected = document.querySelector(
  ".panel > .item:first-child > button:last-child") === null;
var queryStructuralOrder = document.querySelector("#last, #first") === first &&
  document.querySelectorAll("#last, #first").length === 2 &&
  document.querySelectorAll("#last, #first")[0] === first &&
  document.querySelectorAll("#last, #first")[1] === last;
var selectorListDedup = document.querySelectorAll(
  "#only:first-child, #only:last-child").length === 1 &&
  document.querySelectorAll("#only:first-child, #only:last-child")[0] === only;
var selectorListFour = document.querySelectorAll(
  "#last:last-child, #only:only-child, #first:first-child, #middle").length === 4;
var scopedQuery = panel.querySelector(".item:first-child") === first &&
  panel.querySelectorAll(".item:first-child, .item:last-child").length === 2 &&
  panel.querySelector("#panel:first-child") === null &&
  singleParent.querySelector(":only-child") === only;
var closestSelf = only.closest(":only-child") === only;
var closestAncestor = nestedButton.closest(".entry:first-child") === nestedFirst;
var closestNoMatch = first.closest("#absent:first-child") === null;
var formIndependent = document.getElementById("form-owned").matches(":only-child") &&
  document.getElementById("form-owned").parentElement ===
    document.getElementById("outside-parent");
)JS");
    expect(result.succeeded(), "structural rules and query APIs execute");
    const char* names[] = {
        "basicFirst", "basicLast", "basicOnly", "twoSided", "onlyTraversal",
        "parentRequired", "hiddenStillStructural", "relationDescendantLeft",
        "relationDescendantRight", "relationChildBoth", "relationAdjacent",
        "relationGeneral", "relationBothSides", "chainRejected",
        "queryStructuralOrder", "selectorListDedup", "selectorListFour",
        "scopedQuery", "closestSelf", "closestAncestor", "closestNoMatch",
        "formIndependent",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("structural: ") + name);

    const std::uint64_t ownerSerial = serialById(harness, "form-owner");
    bool formOwnedElementFound = false;
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id != "form-owned") continue;
        element.formControl.parentFormSerial = ownerSerial;
        formOwnedElementFound = true;
        break;
    }
    expect(formOwnedElementFound && ownerSerial != 0u,
        "form ownership metadata is available for an out-of-tree control");
    const ScriptResult ownership = harness.execute(
        "var structuralIgnoresFormOwner = "
        "document.getElementById('form-owned').matches(':only-child') && "
        "document.getElementById('form-owned').parentElement === "
        "document.getElementById('outside-parent');");
    expect(ownership.succeeded(), "form ownership independence executes");
    expectBoolean(harness, "structuralIgnoresFormOwner", true,
        "form ownership does not replace structural parentage");
}

void testCurrentStructurePurityStressAndMalformedMetadata()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    gxos::javascript::RuntimeLimits stressLimits;
    stressLimits.maxHostOperations = 50000u;
    NavigatorScriptExecutionHarness harness(stressLimits);
    loadFixture(harness, error, "live");
    const std::uint64_t initialGeneration = harness.hostAdapter().generation();
    const std::uint64_t firstSerial = serialById(harness, "first");
    const std::uint64_t singleParentSerial = serialById(harness, "single-parent");
    const std::uint64_t onlySerial = serialById(harness, "only");
    using StructuralState = std::tuple<std::uint64_t, std::uint64_t,
        std::string, std::string, std::string, std::string, std::uint16_t,
        std::uint16_t, std::uint8_t, std::uint64_t, std::uint64_t, bool, bool>;
    std::vector<StructuralState> before;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        before.emplace_back(element.serial, element.parentSerial,
            element.tagName, element.id, element.className, element.inlineStyle,
            element.attributePresence, element.retainedAttributeOffset,
            element.retainedAttributeCount, element.previousSiblingSerial,
            element.formControl.parentFormSerial,
            element.formControl.checked, element.formControl.disabled);
    }
    const std::vector<std::uint8_t> retainedAttributesBefore =
        harness.document().retainedAttributeStorage;
    const std::size_t mutationCountBefore =
        harness.document().scriptMutationCount;
    using RuntimeControlState = std::tuple<std::uint64_t, std::uint64_t,
        std::uint64_t, std::uint8_t, bool, bool, bool, bool, bool, std::uint32_t,
        std::string, int, std::string>;
    std::vector<RuntimeControlState> runtimeControlsBefore;
    for (std::size_t index = 0u;
         index < harness.document().formRuntimeState.count; ++index) {
        const gxos::web::FormRuntimeControlState& state =
            harness.document().formRuntimeState.controls[index];
        runtimeControlsBefore.emplace_back(state.logicalSerial,
            state.parentFormSerial, state.parentFieldsetSerial,
            static_cast<std::uint8_t>(state.type), state.checked,
            state.defaultChecked, state.disabled, state.metadataValid,
            state.editBaselineValid, state.activationCount, state.defaultValue,
            state.defaultSelectedOption, state.editBaselineValue);
    }
    const std::uint64_t focusBefore =
        harness.document().formRuntimeState.focusedLogicalSerial;
    const std::size_t runtimeCountBefore =
        harness.document().formRuntimeState.count;
    const std::size_t listenerCountBefore =
        harness.hostAdapter().clickListenerCount();
    const ScriptResult stress = harness.execute(R"JS(
var first = document.getElementById("first");
var middle = document.getElementById("middle");
var last = document.getElementById("last");
var only = document.getElementById("only");
var firstHeld = document.querySelectorAll(":first-child");
var lastHeld = document.querySelectorAll(":last-child");
var onlyHeld = document.querySelectorAll("#only:only-child");
var overlapHeld = document.querySelectorAll(":first-child, :last-child");
var firstPseudo = ":first-child";
var lastPseudo = ":last-child";
var onlyPseudo = ":only-child";
var stressDeterministic = true;
for (var i = 0; i < 1000; i = i + 1) {
  if (!first.matches(firstPseudo) || !last.matches(lastPseudo) ||
      !only.matches(onlyPseudo) || middle.matches(firstPseudo) ||
      middle.matches(lastPseudo)) stressDeterministic = false;
}
var heldReadsStable = true;
for (var j = 0; j < 300; j = j + 1) {
  if (firstHeld.length === 0 || lastHeld.length === 0 || onlyHeld.length === 0 ||
      overlapHeld.length === 0 || !first.matches(firstPseudo) ||
      !last.matches(lastPseudo) || !only.matches(onlyPseudo))
    heldReadsStable = false;
}
)JS");
    expect(stress.succeeded(), "1,000 pseudo checks and held collection reads complete");
    reportScriptFailure(stress, "structural stress");
    expectBoolean(harness, "stressDeterministic", true,
        "1,000 repeated first/last/only checks are deterministic");
    expectBoolean(harness, "heldReadsStable", true,
        "300 held live collection rereads remain current");
    bool unchanged = before.size() ==
        harness.document().structuralElements.size();
    std::size_t position = 0u;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (position >= before.size()) { unchanged = false; break; }
        unchanged = unchanged && before[position] == StructuralState(
            element.serial, element.parentSerial, element.tagName, element.id,
            element.className, element.inlineStyle, element.attributePresence,
            element.retainedAttributeOffset, element.retainedAttributeCount,
            element.previousSiblingSerial, element.formControl.parentFormSerial,
            element.formControl.checked, element.formControl.disabled);
        ++position;
    }
    std::vector<RuntimeControlState> runtimeControlsAfter;
    for (std::size_t index = 0u;
         index < harness.document().formRuntimeState.count; ++index) {
        const gxos::web::FormRuntimeControlState& state =
            harness.document().formRuntimeState.controls[index];
        runtimeControlsAfter.emplace_back(state.logicalSerial,
            state.parentFormSerial, state.parentFieldsetSerial,
            static_cast<std::uint8_t>(state.type), state.checked,
            state.defaultChecked, state.disabled, state.metadataValid,
            state.editBaselineValid, state.activationCount, state.defaultValue,
            state.defaultSelectedOption, state.editBaselineValue);
    }
    unchanged = unchanged && initialGeneration == harness.hostAdapter().generation() &&
        focusBefore == harness.document().formRuntimeState.focusedLogicalSerial &&
        runtimeCountBefore == harness.document().formRuntimeState.count &&
        runtimeControlsBefore == runtimeControlsAfter &&
        retainedAttributesBefore == harness.document().retainedAttributeStorage &&
        mutationCountBefore == harness.document().scriptMutationCount &&
        listenerCountBefore == harness.hostAdapter().clickListenerCount();
    expect(unchanged,
        "matching leaves structure, attributes, form/focus state, listeners, and generation unchanged");

    gxos::web::HtmlElementRef* firstElement = nullptr;
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.serial == firstSerial) { firstElement = &element; break; }
    }
    expect(firstElement != nullptr && singleParentSerial != 0u &&
        onlySerial != 0u, "synthetic structure members resolve");
    if (firstElement != nullptr) {
        firstElement->parentSerial = singleParentSerial;
        const ScriptResult reevaluated = harness.execute(R"JS(
var first = document.getElementById("first");
var middle = document.getElementById("middle");
var only = document.getElementById("only");
var liveFirstMoved = first.matches(":first-child") &&
  firstHeld.length > 0 && first.parentElement ===
    document.getElementById("single-parent");
var livePanelFirstChanged = middle.matches(":first-child") &&
  document.querySelector(".panel > .item:first-child") === middle;
var liveOnlyChanged = !only.matches(":only-child") && onlyHeld.length === 0;
)JS");
        expect(reevaluated.succeeded(), "synthetic structural reread succeeds");
        reportScriptFailure(reevaluated, "synthetic structural reread");
        expectBoolean(harness, "liveFirstMoved", true,
            "held first-child collection reevaluates changed parent metadata");
        expectBoolean(harness, "livePanelFirstChanged", true,
            "current structural order changes the panel first-child result");
        expectBoolean(harness, "liveOnlyChanged", true,
            "held only-child collection reevaluates changed child count");
    }

    if (firstElement != nullptr) {
        const std::uint64_t savedParent = firstElement->parentSerial;
        firstElement->parentSerial = firstSerial;
        const ScriptResult selfParent = harness.execute(
            "var selfParentFailsClosed = "
            "!document.getElementById('first').matches(':first-child') && "
            "!document.getElementById('first').matches(':last-child') && "
            "!document.getElementById('first').matches(':only-child');");
        expect(selfParent.succeeded(), "self-parent metadata check executes");
        reportScriptFailure(selfParent, "self-parent check");
        expectBoolean(harness, "selfParentFailsClosed", true,
            "self-parent metadata fails closed for all three pseudos");
        firstElement->parentSerial = 999999u;
        const ScriptResult missingParent = harness.execute(
            "var invalidParentFailsClosed = "
            "!document.getElementById('first').matches(':first-child') && "
            "!document.getElementById('first').matches(':last-child') && "
            "!document.getElementById('first').matches(':only-child');");
        expect(missingParent.succeeded(), "invalid-parent metadata check executes");
        reportScriptFailure(missingParent, "invalid-parent check");
        expectBoolean(harness, "invalidParentFailsClosed", true,
            "invalid parent serial fails closed for all three pseudos");
        firstElement->parentSerial = savedParent;
    }
}

void testNearCapacitySiblingOrder()
{
    std::string html = "<!doctype html><html><body><div id=parent>";
    for (std::size_t index = 0u; index < 1000u; ++index) {
        html += "<span id=child-" + std::to_string(index) + "></span>";
    }
    html += "</div></body></html>";

    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    expect(harness.loadHtml("file:///js53-near-capacity.html", html, error),
        "near-capacity structural fixture loads");
    expect(error == RuntimeErrorCode::None,
        "near-capacity fixture load has no runtime error");
    expect(harness.relayout(), "near-capacity fixture relayout succeeds");
    expect(harness.document().structuralElements.size() > 1000u &&
        harness.document().structuralElements.size() <=
            gxos::javascript::kNavigatorScriptMaxDocumentNodes,
        "fixture exercises more than 1,000 bounded structural Elements");

    const ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var first = parent.firstElementChild;
var last = parent.lastElementChild;
var boundedEdges = parent.childElementCount === 1000 &&
  first.id === "child-0" && last.id === "child-999" &&
  first.matches(":first-child") && last.matches(":last-child") &&
  !first.matches(":last-child") && !last.matches(":first-child") &&
  !first.matches(":only-child") && first.previousElementSibling === null &&
  last.nextElementSibling === null;
var boundedQueryOrder = document.querySelector("#child-999:last-child") === last &&
  document.querySelector("#child-0:first-child") === first;
)JS");
    expect(result.succeeded(), "near-capacity sibling lookup completes within bounds");
    reportScriptFailure(result, "near-capacity sibling check");
    expectBoolean(harness, "boundedEdges", true,
        "first/last/only results are stable among 1,000 structural siblings");
    expectBoolean(harness, "boundedQueryOrder", true,
        "querySelector and pseudo lookup preserve structural order near the cap");
}

void testEventsAndGenerationSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "events");
    const std::uint64_t oldFirstSerial = serialById(harness, "first");
    const std::uint64_t lastSerial = serialById(harness, "last");
    const ScriptResult events = harness.execute(R"JS(
var first = document.getElementById("first");
var middle = document.getElementById("middle");
var last = document.getElementById("last");
var firstEventMatches = false;
var lastEventMatches = false;
var nestedPreserved = false;
var eventMetadataPreserved = false;
first.addEventListener("click", function (event) {
  var target = event.target;
  var current = event.currentTarget;
  var phase = event.eventPhase;
  firstEventMatches = target.matches(":first-child") &&
    target.closest(".panel:first-child") === document.getElementById("panel");
  middle.click();
  nestedPreserved = event.target === target && event.currentTarget === current &&
    event.eventPhase === phase;
  eventMetadataPreserved = event.relatedTarget === null &&
    event.defaultPrevented === false && current === first && phase === 2;
});
middle.addEventListener("click", function (event) {
  nestedPreserved = event.target.matches(":only-child") === false;
});
last.addEventListener("click", function (event) {
  lastEventMatches = event.target.matches(":last-child");
});
)JS");
    expect(events.succeeded(), "pseudo event listeners register");
    reportScriptFailure(events, "event listener setup");
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(oldFirstSerial, clickError),
        "first-child authentic click dispatch succeeds");
    expect(clickError == RuntimeErrorCode::None,
        "first-child click dispatch has no runtime error");
    clickError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(lastSerial, clickError),
        "last-child authentic click dispatch succeeds");
    expect(clickError == RuntimeErrorCode::None,
        "last-child click dispatch has no runtime error");
    expectBoolean(harness, "firstEventMatches", true,
        "event target matches first-child and closest structural ancestor");
    expectBoolean(harness, "lastEventMatches", true,
        "event target matches last-child");
    expectBoolean(harness, "nestedPreserved", true,
        "nested dispatch preserves the outer Event target/currentTarget/phase");
    expectBoolean(harness, "eventMetadataPreserved", true,
        "pseudo queries preserve relatedTarget/defaultPrevented metadata");

    const ScriptResult capture = harness.execute(R"JS(
var oldFirst = document.getElementById("first");
var oldStructuralCollection = document.querySelectorAll(":first-child");
)JS");
    expect(capture.succeeded(), "old generation handle and collection are captured");
    expect(harness.invalidateDocumentGeneration(error),
        "structural pseudo test advances document generation");
    const ScriptResult stale = harness.execute(R"JS(
var stalePseudosFailClosed = !oldFirst.matches(":first-child") &&
  !oldFirst.matches(":last-child") && !oldFirst.matches(":only-child") &&
  oldFirst.closest(":first-child") === null;
)JS");
    expect(stale.succeeded(), "stale matches and closest remain script-safe");
    reportScriptFailure(stale, "stale structural pseudo");
    expectBoolean(harness, "stalePseudosFailClosed", true,
        "stale Element handles cannot match or traverse replacement structure");
    expect(!harness.execute(
        "var staleStructuralCollectionLength = oldStructuralCollection.length;")
            .succeeded(), "stale live structural collection fails closed");

    harness.document() = gxos::web::parseHtml(
        "file:///js53-replacement.html", fixture);
    const std::uint64_t newFirstSerial = serialById(harness, "first");
    expect(newFirstSerial == oldFirstSerial,
        "replacement document reuses the structural Element serial");
    expect(harness.runtime().installHostGlobal("freshFirst", newFirstSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "replacement first child receives a current generation handle");
    const ScriptResult reuse = harness.execute(R"JS(
var replacementFirstMatches = freshFirst.matches(":first-child");
var oldSerialCannotSeeReplacement = !oldFirst.matches(":first-child") &&
  !oldFirst.matches(":last-child") && !oldFirst.matches(":only-child") &&
  oldFirst.closest(":first-child") === null;
)JS");
    expect(reuse.succeeded(), "serial reuse comparison executes");
    reportScriptFailure(reuse, "structural serial reuse");
    expectBoolean(harness, "replacementFirstMatches", true,
        "new generation observes first-child state at reused serial");
    expectBoolean(harness, "oldSerialCannotSeeReplacement", true,
        "old generation cannot see structural state at reused serial");
    expect(!harness.execute(
        "var reusedOldCollectionLength = oldStructuralCollection.length;")
            .succeeded(), "old collection stays isolated after serial reuse");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js53.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testParserAndSelectorComposition();
    testStructuralRulesQueriesAndRelations();
    testCurrentStructurePurityStressAndMalformedMetadata();
    testNearCapacitySiblingOrder();
    testEventsAndGenerationSafety();
    if (failures != 0) {
        std::cerr << failures << " JS53 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    const std::size_t simple =
        sizeof(gxos::javascript::NavigatorScriptSimpleSelectorDescriptor);
    const std::size_t descriptor =
        sizeof(gxos::javascript::NavigatorScriptSelectorDescriptor);
    const std::size_t collection = (16u + descriptor + 7u) / 8u * 8u;
    std::cout << "Navigator JavaScript JS53 checks: " << checks << "/"
        << checks << " passed; simple=" << simple << " bytes complete="
        << descriptor << " bytes collection=" << collection
        << " bytes registry="
        << collection * gxos::javascript::kNavigatorScriptMaxSelectorCollections
        << " bytes\n";
    return 0;
}
