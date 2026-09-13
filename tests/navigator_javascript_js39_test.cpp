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
<div id="outside" class="panel"><input id="outside-input" class="field"></div>
<form id="settings" name="settings" class="panel form">
  <input id="direct" class="field required" name="direct" type="text" value="direct-seed">
  <div id="wrapper" class="group">
    <button id="nested" class="action primary" type="button">Save</button>
    <input id="wrapped-input" class="field required" name="wrapped" type="text" value="wrapped-seed">
  </div>
  <select id="select" name="choice">
    <option id="option-a" value="a" selected>A</option>
    <option id="option-b" value="b">B</option>
  </select>
  <button id="submit" type="submit">Submit</button>
  <button id="reset" type="reset">Reset</button>
</form>
<form id="other-form"><input id="other-input" name="other"></form>
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
    if (value->isNumber()) {
        expect(value->numberValue() == expected, label + ": value");
    }
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
    expect(harness.loadHtml("file:///js39.html", kFixture, error),
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

void testDescendantAndChildQueries()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var settings = document.querySelector("#settings");
var direct = document.querySelector("#direct");
var wrapper = document.querySelector("#wrapper");
var nested = document.querySelector("#nested");
var wrapped = document.querySelector("#wrapped-input");
var select = document.querySelector("#select");
var optionA = document.querySelector("#option-a");
var optionB = document.querySelector("#option-b");
var outside = document.querySelector("#outside-input");
var directDescendant = document.querySelector("form input") === direct;
var spacedDescendant = document.querySelector("form   input") === direct;
var tabDescendant = document.querySelector("form\tinput") === direct;
var newlineDescendant = document.querySelector("form\ninput") === direct;
var trimmedDescendant = document.querySelector("  form input  ") === direct;
var idLeft = document.querySelector("#settings input") === direct;
var classLeft = document.querySelector(".panel input") === outside;
var compoundLeft = document.querySelector("form.panel input") === direct;
var compoundRight = document.querySelector("form input.required") === direct;
var compoundBoth = document.querySelector("form.panel input.required") === direct;
var directChild = document.querySelector("form > input") === direct;
var noSpaceChild = document.querySelector("form>input") === direct;
var aroundChild = document.querySelector(" form  >  input ") === direct;
var compoundChild = document.querySelector("form#settings > input.required") === direct;
var childOrder = document.querySelector("form > div") === wrapper &&
    document.querySelector("div > button") === nested;
var descendantAll = document.querySelectorAll("form input");
var childAll = document.querySelectorAll("form > input");
var panelButtons = document.querySelectorAll(".panel button");
var descendantCount = descendantAll.length === 3 &&
    descendantAll[0] === direct && descendantAll[1] === wrapped &&
    descendantAll[2] === document.querySelector("#other-input");
var childCount = childAll.length === 2 && childAll[0] === direct &&
    childAll[1] === document.querySelector("#other-input");
var panelButtonOrder = panelButtons.length === 3 &&
    panelButtons[0] === nested && panelButtons[1] === document.querySelector("#submit") &&
    panelButtons[2] === document.querySelector("#reset");
var descendantLength = descendantAll.length;
var childLength = childAll.length;
var panelButtonLength = panelButtons.length;
var noMatch = document.querySelector("#other-form input") ===
    document.querySelector("#other-input") &&
    document.querySelector("#missing input") === null;
var scope = wrapper.querySelector("form input") === wrapped &&
    wrapper.querySelector("form > input") === null &&
    settings.querySelector("form input") === direct &&
    settings.querySelector("form > input") === direct &&
    settings.querySelector("#outside input") === null;
var identity = document.querySelector("form input") === direct &&
    document.querySelector("#direct") === direct &&
    document.querySelectorAll("select > option")[0] === optionA &&
    select.options[1] === optionB;
var formStructureA = settings.children[0] === direct &&
    settings.elements[0] === direct && !wrapped.matches("form > input");
var formChild0IsDirect = settings.children[0] === direct;
var formElement0IsDirect = settings.elements[0] === direct;
var formStructureB = wrapped.parentElement === wrapper;
var formStructureC = wrapper.parentElement === settings;
var formStructure = formStructureA && formStructureB && formStructureC;
)JS");
    expect(result.succeeded(), "queries: script");
    expectNumber(harness, "descendantLength", 3.0,
        "queryAll: descendant collection length");
    expectNumber(harness, "childLength", 2.0,
        "queryAll: child collection length");
    expectNumber(harness, "panelButtonLength", 3.0,
        "queryAll: panel button collection length");
    expectBoolean(harness, "directDescendant", true, "descendant: direct child");
    expectBoolean(harness, "spacedDescendant", true, "descendant: spaces");
    expectBoolean(harness, "tabDescendant", true, "descendant: tab");
    expectBoolean(harness, "newlineDescendant", true, "descendant: newline");
    expectBoolean(harness, "trimmedDescendant", true, "descendant: trim");
    expectBoolean(harness, "idLeft", true, "descendant: id left");
    expectBoolean(harness, "classLeft", true, "descendant: class left");
    expectBoolean(harness, "compoundLeft", true, "descendant: compound left");
    expectBoolean(harness, "compoundRight", true, "descendant: compound right");
    expectBoolean(harness, "compoundBoth", true, "descendant: compounds");
    expectBoolean(harness, "directChild", true, "child: spaced");
    expectBoolean(harness, "noSpaceChild", true, "child: no spaces");
    expectBoolean(harness, "aroundChild", true, "child: whitespace around");
    expectBoolean(harness, "compoundChild", true, "child: compounds");
    expectBoolean(harness, "childOrder", true, "child: structural order");
    expectBoolean(harness, "descendantCount", true, "queryAll: descendant count/order");
    expectBoolean(harness, "childCount", true, "queryAll: child count/order");
    expectBoolean(harness, "panelButtonOrder", true, "queryAll: result identity/order");
    expectBoolean(harness, "noMatch", true, "query: no match");
    expectBoolean(harness, "scope", true, "query: scoped boundary");
    expectBoolean(harness, "identity", true, "query: canonical identity");
    expectBoolean(harness, "formStructure", true, "query: form ownership distinction");
    expectBoolean(harness, "formStructureA", true, "query: structural child versus form ownership");
    expectBoolean(harness, "formStructureB", true, "query: wrapper parent");
    expectBoolean(harness, "formStructureC", true, "query: form parent");
    expectBoolean(harness, "formChild0IsDirect", true, "query: form child order");
    expectBoolean(harness, "formElement0IsDirect", true, "query: form ownership order");
}

void testMatchesClosestAndInvalidSelectors()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var form = document.querySelector("#settings");
var direct = document.querySelector("#direct");
var wrapper = document.querySelector("#wrapper");
var nested = document.querySelector("#nested");
var wrapped = document.querySelector("#wrapped-input");
var option = document.querySelector("#option-a");
var selfDescendant = direct.matches("form input") && direct.matches("form > input");
var grandchildDescendant = wrapped.matches("form input") &&
    !wrapped.matches("form > input") && wrapped.matches("div > input");
var nestedRelations = nested.matches("form button") &&
    !nested.matches("form > button") && nested.matches("div > button") &&
    nested.matches(".panel .action") && nested.matches("form.panel button.primary");
var exactSemantics = !nested.matches("#NESTED") &&
    !nested.matches(".act") && nested.matches("BUTTON") &&
    !nested.matches("span.action");
var optionRelation = option.matches("select > option") &&
    option.closest("select > option") === option && option.closest("select") ===
        document.querySelector("#select");
var closestSelf = nested.closest("form button") === nested &&
    nested.closest("div > button") === nested &&
    nested.closest("form > button") === null;
var closestAncestor = wrapped.closest("form input") === wrapped &&
    wrapped.closest("div > input") === wrapped &&
    direct.closest("form > input") === direct;
var closestNearest = nested.closest(".panel button") === nested &&
    nested.closest("form") === form && wrapper.closest("form") === form;
var noAncestor = nested.matches("#outside button") === false &&
    nested.closest("#outside button") === null;
var malformed = document.querySelector(" > button") === null &&
    document.querySelector("div >") === null &&
    document.querySelector("div > > button") === null &&
    document.querySelector("div >> button") === null &&
    document.querySelector("div button extra") === null &&
    document.querySelector("div div button") === null &&
    document.querySelector("div > button > span") === null;
var malformedAll = document.querySelectorAll("a >").length === 0 &&
    document.querySelectorAll("> b").length === 0 &&
    document.querySelectorAll("a > > b").length === 0 &&
    document.querySelectorAll("a + b").length === 0 &&
    document.querySelectorAll("a,b").length === 0 &&
    document.querySelectorAll("[id=x]").length === 0;
var nestedCallSafety = nested.matches("form button") &&
    nested.closest(".group > button").matches("div button") &&
    nested.matches("form button") && nested.closest(".group > button") === nested;
var collectionSafety = document.querySelectorAll("form input")[1] === wrapped &&
    document.querySelectorAll("form > input")[0] === direct &&
    document.querySelectorAll("form input")[99] === undefined;
)JS");
    expect(result.succeeded(), "matches/closest: script");
    expectBoolean(harness, "selfDescendant", true, "matches: direct candidate");
    expectBoolean(harness, "grandchildDescendant", true, "matches: grandchild versus child");
    expectBoolean(harness, "nestedRelations", true, "matches: relational compounds");
    expectBoolean(harness, "exactSemantics", true, "matches: tag/id/class semantics");
    expectBoolean(harness, "optionRelation", true, "matches: select option relation");
    expectBoolean(harness, "closestSelf", true, "closest: receiver full match");
    expectBoolean(harness, "closestAncestor", true, "closest: full selector");
    expectBoolean(harness, "closestNearest", true, "closest: nearest full match");
    expectBoolean(harness, "noAncestor", true, "matches: no ancestor");
    expectBoolean(harness, "malformed", true, "invalid: malformed selectors");
    expectBoolean(harness, "malformedAll", true, "invalid: unsupported selectors");
    expectBoolean(harness, "nestedCallSafety", true, "safety: nested selector calls");
    expectBoolean(harness, "collectionSafety", true, "collection: bounded indexed access");
}

void testEventsAndStaleGeneration()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t nestedSerial = serialById(harness, "nested");
    const std::uint64_t formSerial = serialById(harness, "settings");
    const ScriptResult setup = harness.execute(R"JS(
var form = document.querySelector("#settings");
var direct = document.querySelector("#direct");
var nested = document.querySelector("#nested");
var submit = document.querySelector("#submit");
var reset = document.querySelector("#reset");
var clickEvent = false;
var focusEvent = false;
var submitEvent = false;
var resetEvent = false;
var reentrant = false;
var clickTargetMetadata = false;
var clickCurrentMetadata = false;
var clickRelatedMetadata = false;
var clickDefaultMetadata = false;
var clickMatchesMetadata = false;
var clickClosestMetadata = false;
form.addEventListener("click", function(event) {
  clickTargetMetadata = event.target === nested;
  clickCurrentMetadata = event.currentTarget === form;
  clickRelatedMetadata = event.relatedTarget === null;
  clickDefaultMetadata = event.defaultPrevented === false;
  clickMatchesMetadata = event.target.matches(".panel button");
  clickClosestMetadata = event.target.closest(".group > button") === nested;
  clickEvent = event.target === nested && event.currentTarget === form &&
    event.relatedTarget === null && event.defaultPrevented === false &&
    event.target.matches(".panel button") &&
    event.target.closest(".group > button") === nested;
  var owner = event.target.closest(".group > button");
  reentrant = event.target.matches("form button") && owner === nested;
});
nested.addEventListener("focus", function(event) {
  focusEvent = event.target === nested && event.currentTarget === nested &&
    event.relatedTarget === null && event.target.matches(".group > button");
});
form.addEventListener("submit", function(event) {
  submitEvent = event.target === form && event.currentTarget === form &&
    event.defaultPrevented === false &&
    document.querySelector("form > button") === submit;
  event.preventDefault();
});
form.addEventListener("reset", function(event) {
  resetEvent = event.target === form && event.currentTarget === form &&
    event.defaultPrevented === false &&
    document.querySelector("form input") === direct;
});
var oldElement = nested;
var oldResults = document.querySelectorAll("form input");
)JS");
    if (!setup.succeeded()) {
        std::cerr << "  events runtime error=" << static_cast<int>(
            setup.runtimeError.code) << " line=" << setup.runtimeError.location.line
            << " column=" << setup.runtimeError.location.column << "\n";
    }
    expect(setup.succeeded(), "events: setup and dispatch");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "events: listener capacity remains capped");

    bool defaultPrevented = false;
    expect(harness.dispatchClick(nestedSerial, error, &defaultPrevented),
        "events: host click dispatch");
    expect(error == RuntimeErrorCode::None, "events: host click no error");
    expect(harness.focusElement(nestedSerial, error), "events: focus dispatch");
    expect(error == RuntimeErrorCode::None, "events: focus no error");
    expect(harness.dispatchSubmit(formSerial, error, &defaultPrevented),
        "events: submit dispatch");
    expect(error == RuntimeErrorCode::None, "events: submit no error");
    const ScriptResult resetDispatch = harness.execute("form.reset();");
    expect(resetDispatch.succeeded(), "events: reset dispatch");
    expectBoolean(harness, "clickEvent", true, "events: target.matches/closest metadata");
    expectBoolean(harness, "clickTargetMetadata", true, "events: target metadata");
    expectBoolean(harness, "clickCurrentMetadata", true, "events: currentTarget metadata");
    expectBoolean(harness, "clickRelatedMetadata", true, "events: relatedTarget metadata");
    expectBoolean(harness, "clickDefaultMetadata", true, "events: defaultPrevented metadata");
    expectBoolean(harness, "clickMatchesMetadata", true, "events: target.matches metadata");
    expectBoolean(harness, "clickClosestMetadata", true, "events: target.closest metadata");
    expectBoolean(harness, "focusEvent", true, "events: focus metadata");
    expectBoolean(harness, "submitEvent", true, "events: submit/defaultPrevented");
    expectBoolean(harness, "resetEvent", true, "events: reset metadata");
    expectBoolean(harness, "reentrant", true, "events: nested selector dispatch");

    expect(harness.invalidateDocumentGeneration(error),
        "stale: invalidate generation");
    expect(error == RuntimeErrorCode::None, "stale: invalidation no error");
    const ScriptResult stale = harness.execute(
        "var staleMatches = oldElement.matches(\"form button\") === false && "
        "oldElement.closest(\".group > button\") === null;");
    expect(stale.succeeded(), "stale: pure selector calls fail closed");
    expectBoolean(harness, "staleMatches", true,
        "stale: matches false and closest null");
    expectError(harness.execute("oldResults.length;"),
        RuntimeErrorCode::StaleHostObject,
        "stale: relational collection rejects old generation");
}

void testMalformedStructureAndSelectorLimit()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t wrapperSerial = harness.document().structuralElements[6].serial;
    const std::uint64_t nestedSerial = harness.document().structuralElements[7].serial;
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.serial == wrapperSerial) element.parentSerial = nestedSerial;
        if (element.serial == nestedSerial) element.parentSerial = wrapperSerial;
    }
    const ScriptResult cycle = harness.execute(
        "var cycleSafe = document.querySelector(\"#nested\").matches(\"form button\") === false && "
        "document.querySelector(\"#nested\").closest(\"form button\") === null;");
    expect(cycle.succeeded(), "safety: parent cycle script terminates");
    expectBoolean(harness, "cycleSafe", true, "safety: parent cycle fails closed");

    NavigatorScriptExecutionHarness invalidParent;
    loadFixture(invalidParent, error);
    for (gxos::web::HtmlElementRef& element :
            invalidParent.document().structuralElements) {
        if (element.id == "wrapper") element.parentSerial = 0xDEADu;
    }
    const ScriptResult invalid = invalidParent.execute(
        "var invalidParentSafe = document.querySelector(\"#nested\").matches(\"form button\") === false && "
        "document.querySelector(\"#nested\").closest(\"form button\") === null;");
    expect(invalid.succeeded(), "safety: invalid parent script terminates");
    expectBoolean(invalidParent, "invalidParentSafe", true,
        "safety: invalid parent fails closed");

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

void testSimpleSelectorRegression()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var direct = document.querySelector("#direct");
var form = document.querySelector("form#settings");
var simple = document.querySelector("#direct") === direct &&
    document.querySelector(".required") === direct &&
    document.querySelector("INPUT") === document.querySelector("#outside-input") &&
    direct.matches("input.required") && direct.closest("form") === form &&
    document.querySelectorAll("input")[1] === direct &&
    form.elements[0] === direct && form.children[1] !== form.elements[0] &&
    document.querySelector("form input") === direct;
)JS");
    expect(result.succeeded(), "regression: simple selectors script");
    expectBoolean(harness, "simple", true,
        "regression: JS36/37 simple selector behavior");
}

} // namespace

int main()
{
    testDescendantAndChildQueries();
    testMatchesClosestAndInvalidSelectors();
    testEventsAndStaleGeneration();
    testMalformedStructureAndSelectorLimit();
    testSimpleSelectorRegression();
    if (failures != 0) {
        std::cerr << failures << " JS39 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS39 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
