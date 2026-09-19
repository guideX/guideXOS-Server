#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;
using gxos::javascript::kNavigatorScriptMaxSelectorLength;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<form id="settings" class="settings">
  <label id="required-label" class="required">Name</label>
  <input id="first-input" class="field required" name="first">
  <span id="middle"></span>
  <input id="second-input" class="field" name="second">
  <button id="action" class="action primary" type="button">Action</button>
  <h2 id="title">Title</h2>
  <div id="gap"></div>
  <p id="first-note" class="note">First</p>
  <p id="second-note" class="note">Second</p>
  <div id="nested-wrapper">
    <input id="nested-input" name="nested">
  </div>
  <select id="choice">
    <option id="option-a">A</option>
    <option id="option-b">B</option>
    <option id="option-c">C</option>
  </select>
</form>
<div id="foreign-parent"><span id="foreign-left"></span></div>
<div id="foreign-target-parent"><button id="foreign-target" type="button">Foreign</button></div>
<div id="event-container">
  <span id="marker" class="marker"></span>
  <button id="event-button" class="event-button" type="button">Save</button>
  <span id="event-gap"></span>
  <button id="event-later" class="event-button" type="button">Later</button>
</div>
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
    if (!result.succeeded())
        expect(result.runtimeError.code == expected, label + ": error code");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(harness.loadHtml("file:///js40.html", kFixture, error),
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

void testAdjacentSelectors()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var form = document.querySelector("#settings");
var label = document.querySelector("#required-label");
var first = document.querySelector("#first-input");
var middle = document.querySelector("#middle");
var second = document.querySelector("#second-input");
var action = document.querySelector("#action");
var nested = document.querySelector("#nested-input");
var foreign = document.querySelector("#foreign-target");
var optionA = document.querySelector("#option-a");
var optionB = document.querySelector("#option-b");
var optionC = document.querySelector("#option-c");
var adjacentWhitespace = first.matches("label + input") &&
    document.querySelector("label+input") === first &&
    document.querySelector("label+ input") === first &&
    document.querySelector("label +input") === first &&
    document.querySelector("  label + input  ") === first;
var adjacentStructure = !second.matches("label + input") &&
    second.matches("span + input") && action.matches("input + button") &&
    !action.matches("span + button");
var adjacentCompounds = first.matches("label.required + input.field") &&
    document.querySelector("label.required + input.field") === first &&
    first.matches("LABEL.required + INPUT.field") &&
    !first.matches("label.Required + input.field");
var adjacentIdentity = first.matches("#first-input") &&
    !first.matches("#FIRST-INPUT") &&
    first.matches(".field") && !first.matches(".Field") &&
    label.matches("LABEL");
var adjacentBoundaries = first.previousElementSibling === label &&
    middle.previousElementSibling === first &&
    nested.previousElementSibling === null &&
    !nested.matches("input + input") &&
    !foreign.matches("span + button");
var adjacentOptions = optionB.matches("option + option") &&
    optionC.matches("option + option") && optionB.previousElementSibling === optionA;
var formOwnership = form.elements.length > 2 &&
    form.elements[0] === first && form.elements[1] === second &&
    !nested.matches("input + input");
)JS");
    expect(result.succeeded(), "adjacent: script");
    expectBoolean(harness, "adjacentWhitespace", true,
        "adjacent: whitespace variants");
    expectBoolean(harness, "adjacentStructure", true,
        "adjacent: immediate predecessor");
    expectBoolean(harness, "adjacentCompounds", true,
        "adjacent: compound selectors and tag case");
    expectBoolean(harness, "adjacentIdentity", true,
        "adjacent: id/class/tag semantics");
    expectBoolean(harness, "adjacentBoundaries", true,
        "adjacent: first, nested, and parent boundaries");
    expectBoolean(harness, "adjacentOptions", true,
        "adjacent: select options use structural siblings");
    expectBoolean(harness, "formOwnership", true,
        "adjacent: form ownership is not sibling structure");
}

void testGeneralSelectorsAndQueries()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var title = document.querySelector("#title");
var gap = document.querySelector("#gap");
var firstNote = document.querySelector("#first-note");
var secondNote = document.querySelector("#second-note");
var action = document.querySelector("#action");
var foreign = document.querySelector("#foreign-target");
var form = document.querySelector("#settings");
var notes = document.querySelectorAll("h2 ~ p");
var notesTight = document.querySelectorAll("h2~p");
var actionSiblings = document.querySelectorAll("input ~ button");
var generalWhitespace = firstNote.matches("h2 ~ p") &&
    secondNote.matches("h2~p") && secondNote.matches("h2~ p") &&
    secondNote.matches("h2 ~p") && secondNote.matches("  h2 ~ p  ");
var generalBackward = firstNote.previousElementSibling === gap &&
    firstNote.matches("h2 ~ p") && secondNote.matches("h2 ~ p") &&
    secondNote.matches("p.note ~ p.note") &&
    !title.matches("p ~ h2") && !gap.matches("p ~ div");
var generalCompounds = firstNote.matches("H2 ~ P.note") &&
    document.querySelector("h2 ~ p.note") === firstNote &&
    !action.matches("h3 ~ button");
var generalSameParent = foreign.matches("span ~ button") === false &&
    action.matches("input ~ button") && firstNote.parentElement === form;
var generalOrder = notes.length === 2 && notes[0] === firstNote &&
    notes[1] === secondNote && notesTight[1] === secondNote;
var generalAll = actionSiblings.length === 1 && actionSiblings[0] === action &&
    document.querySelectorAll("p ~ p").length === 1;
var scoped = form.querySelector("h2 ~ p") === firstNote &&
    form.querySelectorAll("h2 ~ p").length === 2 &&
    form.querySelector("foreign-parent ~ button") === null;
var queryOrder = document.querySelector("h2 ~ p") === firstNote &&
    document.querySelector("missing ~ p") === null &&
    document.querySelectorAll("missing ~ p").length === 0;
var collectionBounds = notes[0] === firstNote && notes[1] === secondNote &&
    notes[2] === undefined && notes[128] === undefined;
)JS");
    expect(result.succeeded(), "general/query: script");
    expectBoolean(harness, "generalWhitespace", true,
        "general: whitespace variants");
    expectBoolean(harness, "generalBackward", true,
        "general: backward-only scan");
    expectBoolean(harness, "generalCompounds", true,
        "general: compound selectors");
    expectBoolean(harness, "generalSameParent", true,
        "general: same-parent enforcement");
    expectBoolean(harness, "generalOrder", true,
        "general: querySelectorAll order");
    expectBoolean(harness, "generalAll", true,
        "general: collection count and identity");
    expectBoolean(harness, "scoped", true,
        "query: scoped relational candidates");
    expectBoolean(harness, "queryOrder", true,
        "query: first match and empty result");
    expectBoolean(harness, "collectionBounds", true,
        "queryAll: indexed bounds");

    const ScriptResult readOnly = harness.execute(R"JS(
var siblingResults = document.querySelectorAll("h2 ~ p");
siblingResults[0] = null;
)JS");
    expectError(readOnly, RuntimeErrorCode::HostPropertyReadOnly,
        "queryAll: indexed result is read-only");
    const ScriptResult readOnlyLength = harness.execute(
        "siblingResults.length = 0;");
    expectError(readOnlyLength, RuntimeErrorCode::HostPropertyReadOnly,
        "queryAll: length is read-only");
}

void testMatchesClosestAndEvents()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t eventButtonSerial = serialById(harness, "event-button");
    const ScriptResult setup = harness.execute(R"JS(
var eventContainer = document.querySelector("#event-container");
var eventButton = document.querySelector("#event-button");
var eventLater = document.querySelector("#event-later");
var targetPreserved = false;
var currentPreserved = false;
var relatedPreserved = false;
var defaultPreserved = false;
var adjacentEvent = false;
var generalEvent = false;
var closestEvent = false;
var nestedDispatch = false;
eventContainer.addEventListener("click", function(event) {
  if (event.target === eventButton) {
    targetPreserved = event.target === eventButton;
    currentPreserved = event.currentTarget === eventContainer;
    relatedPreserved = event.relatedTarget === null;
    defaultPreserved = event.defaultPrevented === false;
    adjacentEvent = event.target.matches(".marker + button");
    closestEvent = event.target.closest(".marker + button") === eventButton;
  }
  if (event.target === eventLater) {
    generalEvent = event.target.matches(".marker ~ button") &&
      event.target.closest(".marker ~ button") === eventLater;
  }
});
eventButton.addEventListener("click", function(event) {
  nestedDispatch = event.target.matches(".marker + button") &&
    event.target.closest(".marker + button") === eventButton;
  eventLater.click();
});
var matchesAdjacent = eventButton.matches(".marker + button") &&
    !eventLater.matches(".marker + button");
var matchesGeneral = eventButton.matches(".marker ~ button") &&
    eventLater.matches(".marker ~ button") &&
    !document.querySelector("#marker").matches("button ~ span");
var closestSelf = eventButton.closest(".marker + button") === eventButton &&
    eventLater.closest(".marker ~ button") === eventLater;
var closestParent = document.querySelector("#nested-wrapper").closest("form") ===
    document.querySelector("#settings");
var malformedBefore = document.querySelector("#event-button").matches("+ button") === false &&
    document.querySelector("#event-button").closest(".marker +") === null;
)JS");
    expect(setup.succeeded(), "matches/events: setup script");
    expectBoolean(harness, "matchesAdjacent", true,
        "matches: adjacent positive and intervening negative");
    expectBoolean(harness, "matchesGeneral", true,
        "matches: general sibling positive and direction");
    expectBoolean(harness, "closestSelf", true,
        "closest: sibling selector returns candidate B");
    expectBoolean(harness, "closestParent", true,
        "closest: parent walk remains full matcher");
    expectBoolean(harness, "malformedBefore", true,
        "matches/closest: malformed selector fails closed");

    bool defaultPrevented = false;
    expect(harness.dispatchClick(eventButtonSerial, error, &defaultPrevented),
        "events: authentic click dispatch");
    expect(error == RuntimeErrorCode::None, "events: click has no runtime error");
    expect(!defaultPrevented, "events: click remains uncancelled");
    expectBoolean(harness, "adjacentEvent", true,
        "events: target.matches adjacent");
    expectBoolean(harness, "generalEvent", true,
        "events: nested target.matches general");
    expectBoolean(harness, "closestEvent", true,
        "events: target.closest adjacent");
    expectBoolean(harness, "targetPreserved", true,
        "events: target metadata");
    expectBoolean(harness, "currentPreserved", true,
        "events: currentTarget metadata");
    expectBoolean(harness, "relatedPreserved", true,
        "events: relatedTarget metadata");
    expectBoolean(harness, "defaultPreserved", true,
        "events: defaultPrevented metadata");
    expectBoolean(harness, "nestedDispatch", true,
        "events: nested selector and click dispatch");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "events: listener registry remains capped");
}

void testStaleAndMalformedSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t firstSerial = serialById(harness, "first-input");
    const ScriptResult setup = harness.execute(R"JS(
var oldElement = document.querySelector("#first-input");
var oldResults = document.querySelectorAll("label + input");
)JS");
    expect(setup.succeeded(), "stale: setup");
    expect(harness.invalidateDocumentGeneration(error),
        "stale: invalidate generation");
    expect(error == RuntimeErrorCode::None, "stale: invalidation no error");
    const ScriptResult stale = harness.execute(
        "var staleSibling = oldElement.matches(\"label + input\") === false && "
        "oldElement.closest(\".marker ~ button\") === null;");
    expect(stale.succeeded(), "stale: selector calls remain script-safe");
    expectBoolean(harness, "staleSibling", true,
        "stale: matches false and closest null");
    expectError(harness.execute("oldResults.length;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: sibling collection rejects old generation");

    NavigatorScriptExecutionHarness invalid;
    loadFixture(invalid, error);
    for (gxos::web::HtmlElementRef& element :
            invalid.document().structuralElements) {
        if (element.serial == firstSerial) element.parentSerial = element.serial;
    }
    const ScriptResult invalidSibling = invalid.execute(
        "var invalidSiblingSafe = document.querySelector(\"#first-input\")"
        ".matches(\"label + input\") === false && "
        "document.querySelector(\"#first-input\").previousElementSibling === null;");
    expect(invalidSibling.succeeded(), "safety: invalid sibling script terminates");
    expectBoolean(invalid, "invalidSiblingSafe", true,
        "safety: self-parent sibling metadata fails closed");

    NavigatorScriptExecutionHarness oversized;
    loadFixture(oversized, error);
    const std::string tooLong(kNavigatorScriptMaxSelectorLength + 1u, 'x');
    const ScriptResult limit = oversized.execute(
        "var tooLong = document.querySelector(\"" + tooLong +
        "\") === null && document.querySelectorAll(\"" + tooLong +
        "\").length === 0;");
    expect(limit.succeeded(), "safety: oversized selector script");
    expectBoolean(oversized, "tooLong", true,
        "safety: 256-byte selector cap");
}

void testMalformedAndRegression()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var malformed = document.querySelector("+ input") === null &&
    document.querySelector("label +") === null &&
    document.querySelector("label ++ input") === null &&
    document.querySelector("label + + input") === null &&
    document.querySelector("label > + input") === null &&
    document.querySelector("~ input") === null &&
    document.querySelector("h2 ~") === null &&
    document.querySelector("h2 ~~ p") === null &&
    document.querySelector("h2 ~ ~ p") === null &&
    document.querySelector("h2 + ~ p") === null;
var malformedAll = document.querySelectorAll("+ input").length === 0 &&
    document.querySelectorAll("h2 ~").length === 0 &&
    document.querySelectorAll("h2 + p + button").length === 0 &&
    document.querySelectorAll("h2 ~ p ~ button").length === 0 &&
    document.querySelectorAll("form > input + button").length === 0 &&
    document.querySelectorAll("form input ~ button").length === 0;
var descendantRegression = document.querySelector("form input") ===
    document.querySelector("#first-input") &&
    document.querySelector("form > label") === document.querySelector("#required-label");
var canonicalSibling = document.querySelector("#first-input").previousElementSibling ===
    document.querySelector("label") &&
    document.querySelector("#second-input").previousElementSibling ===
    document.querySelector("span");
var closestRegression = document.querySelector("#second-input").closest("form") ===
    document.querySelector("#settings") &&
    document.querySelector("#second-input").closest("missing") === null;
var collectionRegression = document.querySelectorAll("form input").length === 3 &&
    document.querySelectorAll("form input")[0] === document.querySelector("#first-input") &&
    document.querySelectorAll("form input")[2] === document.querySelector("#nested-input");
)JS");
    expect(result.succeeded(), "regression/malformed: script");
    expectBoolean(harness, "malformed", true,
        "malformed: adjacent/general rejection");
    expectBoolean(harness, "malformedAll", true,
        "malformed: multi-relation rejection");
    expectBoolean(harness, "descendantRegression", true,
        "regression: JS39 descendant and child");
    expectBoolean(harness, "canonicalSibling", true,
        "regression: JS38 previousElementSibling identity");
    expectBoolean(harness, "closestRegression", true,
        "regression: JS37 closest");
    expectBoolean(harness, "collectionRegression", true,
        "regression: JS36 collection order");
}

} // namespace

int main()
{
    testAdjacentSelectors();
    testGeneralSelectorsAndQueries();
    testMatchesClosestAndEvents();
    testStaleAndMalformedSafety();
    testMalformedAndRegression();
    if (failures != 0) {
        std::cerr << failures << " JS40 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS40 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
