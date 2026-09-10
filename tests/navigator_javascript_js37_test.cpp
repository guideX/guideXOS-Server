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
<div id="outer" class="panel outer">
  <div id="inner" class="panel inner">
    <button id="x" class="leaf action" type="button">X</button>
  </div>
</div>
<div id="actions" class="action-zone">
  <button id="action" class="action primary" type="button">Action</button>
  <button id="nested-a" class="action" type="button">A</button>
  <button id="nested-b" class="action" type="button">B</button>
</div>
<form id="settings" name="settings" class="form panel">
  <input id="name" class="field" name="name" type="text" value="seed">
  <select id="mode" name="mode">
    <option id="mode-a" value="a" selected>A</option>
    <option id="mode-b" value="b">B</option>
  </select>
  <button id="form-button" class="primary" type="button">Form button</button>
  <input id="submit" type="submit" value="Submit">
  <input id="reset" type="reset" value="Reset">
</form>
</body></html>
)HTML";

const char* kReplacementFixture = R"HTML(
<html><body><div id="replacement" class="fresh"><button id="x" class="fresh-action" type="button">New</button></div></body></html>
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

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0u;
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(harness.loadHtml("file:///js37.html", kFixture, error),
        "fixture: loads");
    expect(error == RuntimeErrorCode::None, "fixture: no load error");
    expect(harness.relayout(), "fixture: relayout");
}

void testMatchesAndClosest()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t xSerial = serialById(harness, "x");
    const ScriptResult result = harness.execute(R"JS(
var x = document.querySelector("#x");
var inner = document.querySelector("#inner");
var outer = document.querySelector("#outer");
var settings = document.forms["settings"];
var name = settings.elements[0];
var select = document.querySelector("#mode");
var option = select.options[0];
var methods = x.matches !== undefined && x.closest !== undefined;
var idPositive = x.matches("#x") && !x.matches("#missing");
var classPositive = x.matches(".leaf") && !x.matches(".lea");
var classToken = x.matches(".action") && !x.matches(".act");
var classCase = !x.matches(".Action");
var tagPositive = x.matches("button") && !x.matches("input");
var tagCase = x.matches("BUTTON");
var idCase = !x.matches("#X");
var tagClass = x.matches("button.action") && !x.matches("span.action");
var tagId = x.matches("button#x") && !x.matches("span#x");
var trimmed = x.matches("  button.action  ");
var repeated = x.matches(".leaf") && !x.matches(".missing") &&
    x.matches("#x") && x.closest(".panel") === inner &&
    x.closest("form") === null;
var selfClosest = x.closest("#x") === x && x.closest(".leaf") === x &&
    x.closest("button") === x;
var parentClosest = x.closest("#inner") === inner &&
    x.closest("div#inner") === inner && x.closest("#outer") === outer;
var nearest = x.closest(".panel") === inner &&
    x.closest("div") === inner;
var root = document.querySelector("html");
var rootBehavior = root.closest("HTML") === root &&
    root.closest("#not-there") === null;
var formIdentity = name.closest("form") === settings &&
    settings === document.querySelector("form#settings") &&
    settings === document.forms[0];
var optionIdentity = option.closest("select") === select;
var scopedDifference = inner.querySelector(".inner") === null &&
    inner.closest(".inner") === inner;
var noMatch = x.closest(".does-not-exist") === null;
var invalid = x.matches("") === false && x.closest("") === null &&
    x.matches("form input") === false && x.closest("form input") === null &&
    x.matches("[name=x]") === false && x.closest("[name=x]") === null &&
    x.matches(":focus") === false && x.closest(":focus") === null &&
    x.matches("a,b") === false && x.closest("a,b") === null;
var consistency = document.querySelector("button.action") ===
    document.querySelectorAll("button.action")[0] &&
    document.querySelectorAll("button.action")[0].matches("button.action") &&
    document.querySelector("button#x").closest("div#inner") === inner &&
    document.querySelector("form input") === null &&
    document.querySelectorAll("form input").length === 0;
var badArguments = x.matches() === false && x.closest() === null &&
    x.matches(1) === false && x.closest(1) === null;
var receiverOnly = inner.matches(".inner") &&
    inner.querySelector(".inner") === null &&
    inner.closest(".inner") === inner;
)JS");
    expect(result.succeeded(), "matches/closest: script");
    expectBoolean(harness, "methods", true, "matches: methods exist");
    expectBoolean(harness, "idPositive", true, "matches: id positive/negative");
    expectBoolean(harness, "classPositive", true,
        "matches: exact class token and substring rejection");
    expectBoolean(harness, "classToken", true, "matches: class token");
    expectBoolean(harness, "classCase", true, "matches: class case");
    expectBoolean(harness, "tagPositive", true, "matches: tag positive/negative");
    expectBoolean(harness, "tagCase", true, "matches: ASCII tag case");
    expectBoolean(harness, "idCase", true, "matches: exact id case");
    expectBoolean(harness, "tagClass", true, "matches: tag.class");
    expectBoolean(harness, "tagId", true, "matches: tag#id");
    expectBoolean(harness, "trimmed", true, "matches: outer whitespace");
    expectBoolean(harness, "repeated", true, "matches/closest: repeated calls");
    expectBoolean(harness, "selfClosest", true, "closest: self inclusion");
    expectBoolean(harness, "parentClosest", true, "closest: parent and compound parent");
    expectBoolean(harness, "nearest", true, "closest: nearest ancestor");
    expectBoolean(harness, "rootBehavior", true, "closest: root self behavior");
    expectBoolean(harness, "formIdentity", true,
        "closest: form identity converges with document.forms");
    expectBoolean(harness, "optionIdentity", true,
        "closest: option structural parent is select");
    expectBoolean(harness, "scopedDifference", true,
        "closest: receiver inclusion differs from querySelector");
    expectBoolean(harness, "noMatch", true, "closest: no-match null");
    expectBoolean(harness, "invalid", true,
        "selectors: unsupported grammar fails consistently");
    expectBoolean(harness, "consistency", true,
        "selectors: querySelector/querySelectorAll consistency");
    expectBoolean(harness, "badArguments", true,
        "selectors: missing and non-string arguments fail closed");
    expectBoolean(harness, "receiverOnly", true,
        "matches: receiver-only semantics");
    expect(xSerial != 0u, "fixture: x serial exists");

    std::string oversized(kNavigatorScriptMaxSelectorLength + 1u, 'x');
    const ScriptResult oversizedResult = harness.execute(
        "var tooLongMatches = x.matches(\"" + oversized +
        "\") === false; var tooLongClosest = x.closest(\"" + oversized +
        "\") === null;");
    expect(oversizedResult.succeeded(), "selectors: oversized script");
    expectBoolean(harness, "tooLongMatches", true,
        "matches: 256-byte input cap");
    expectBoolean(harness, "tooLongClosest", true,
        "closest: 256-byte input cap");

    const std::uint64_t revision = harness.layoutRevision();
    const bool dirty = harness.documentDirty();
    const ScriptResult pureResult = harness.execute(
        "var beforeActive = document.activeElement; var pureMatches = x.matches(\".action\"); "
        "var pureClosest = x.closest(\"#inner\"); var afterActive = document.activeElement; "
        "var pureNoFocusChange = beforeActive === afterActive;");
    expect(pureResult.succeeded(), "matches/closest: pure query script");
    expectBoolean(harness, "pureNoFocusChange", true,
        "matches/closest: activeElement unchanged");
    expect(harness.layoutRevision() == revision,
        "matches/closest: layout revision unchanged");
    expect(harness.documentDirty() == dirty,
        "matches/closest: layout dirty state unchanged");
}

void testEventIntegrationAndReentry()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t actionSerial = serialById(harness, "action");
    const std::uint64_t inputSerial = serialById(harness, "name");
    const std::uint64_t formSerial = serialById(harness, "settings");
    const std::uint64_t aSerial = serialById(harness, "nested-a");
    const ScriptResult setup = harness.execute(R"JS(
var actions = document.querySelector("#actions");
var action = document.querySelector("#action");
var form = document.forms["settings"];
var input = form.elements[0];
var focusSeen = false;
var inputSeen = false;
var changeSeen = false;
var submitSeen = false;
var resetSeen = false;
var clickSeen = false;
var clickMetadata = false;
actions.addEventListener("click", function(event) {
  clickSeen = event.target.matches(".action") &&
    event.target.closest("#actions") === actions;
  clickMetadata = event.target === action && event.currentTarget === actions &&
    event.eventPhase === 3 && event.relatedTarget === null &&
    event.defaultPrevented === false;
});
input.addEventListener("focus", function(event) {
  focusSeen = event.target.matches("input") &&
    event.target.closest("form") === form && event.target === input &&
    event.currentTarget === input && event.eventPhase === 2 &&
    event.relatedTarget === null && event.defaultPrevented === false;
});
input.addEventListener("input", function(event) {
  inputSeen = event.target.matches("input") &&
    event.target.closest("form") === form && event.target === input &&
    event.currentTarget === input && event.defaultPrevented === false;
});
input.addEventListener("change", function(event) {
  changeSeen = event.target.matches("input") &&
    event.target.closest("form") === form && event.target === input &&
    event.currentTarget === input && event.defaultPrevented === false;
});
form.addEventListener("submit", function(event) {
  submitSeen = event.target.matches("form") &&
    event.target.closest("form") === form && event.currentTarget === form &&
    event.eventPhase === 2 && event.defaultPrevented === false;
});
form.addEventListener("reset", function(event) {
  resetSeen = event.target.matches("form") &&
    event.target.closest("#settings") === form && event.currentTarget === form &&
    event.eventPhase === 2 && event.defaultPrevented === false;
});
)JS");
    expect(setup.succeeded(), "events: listener setup");
    bool defaultPrevented = false;
    expect(harness.dispatchClick(actionSerial, error, &defaultPrevented),
        "events: click dispatch");
    expect(error == RuntimeErrorCode::None, "events: click no runtime error");
    expect(!defaultPrevented, "events: click default not prevented");
    expectBoolean(harness, "clickSeen", true,
        "events: event.target.matches and closest delegation");
    expectBoolean(harness, "clickMetadata", true,
        "events: click metadata preserved");

    expect(harness.focusElement(inputSerial, error), "events: focus dispatch");
    expect(error == RuntimeErrorCode::None, "events: focus no runtime error");
    expectBoolean(harness, "focusSeen", true,
        "events: focus listener uses matches/closest");
    expect(harness.dispatchFocusedUserEdit(65, false, error, &defaultPrevented),
        "events: input dispatch");
    expect(error == RuntimeErrorCode::None, "events: input no runtime error");
    expectBoolean(harness, "inputSeen", true,
        "events: input listener uses matches/closest");
    expect(harness.clearFocus(error), "events: change dispatch");
    expect(error == RuntimeErrorCode::None, "events: change no runtime error");
    expectBoolean(harness, "changeSeen", true,
        "events: change listener uses matches/closest");

    expect(harness.dispatchSubmit(formSerial, error, &defaultPrevented),
        "events: submit dispatch");
    expect(error == RuntimeErrorCode::None, "events: submit no runtime error");
    expect(!defaultPrevented, "events: submit default not prevented");
    expectBoolean(harness, "submitSeen", true,
        "events: submit listener uses matches");
    const ScriptResult reset = harness.execute("form.reset();");
    expect(reset.succeeded(), "events: reset call");
    expectBoolean(harness, "resetSeen", true,
        "events: reset listener uses matches");

    const ScriptResult nestedSetup = harness.execute(R"JS(
var nestedContainer = document.querySelector("#actions");
var nestedA = document.querySelector("#nested-a");
var nestedB = document.querySelector("#nested-b");
var nestedClicks = 0;
var nestedClosest = false;
var nestedQuery = false;
var nestedMatch = false;
nestedB.addEventListener("click", function(event) {
  nestedClicks = nestedClicks + 1;
  nestedMatch = event.target.matches(".action") &&
    event.target.closest("#actions") === nestedContainer;
});
nestedA.addEventListener("click", function(event) {
  nestedClosest = event.target.closest(".action") === nestedA;
  var owner = event.target.closest(".action-zone");
  var found = owner.querySelector("#nested-b");
  nestedQuery = found === nestedB;
  nestedB.click();
});
)JS");
    expect(nestedSetup.succeeded(), "nested: listener setup");
    expect(harness.dispatchClick(aSerial, error, &defaultPrevented),
        "nested: outer click dispatch");
    expect(error == RuntimeErrorCode::None, "nested: dispatch no runtime error");
    expectBoolean(harness, "nestedClosest", true,
        "nested: closest survives nested dispatch");
    expectBoolean(harness, "nestedQuery", true,
        "nested: querySelector after closest");
    expectBoolean(harness, "nestedMatch", true,
        "nested: nested target matches");
    expect(harness.runtime().eventPhase() == 0,
        "nested: event phase returns to NONE");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "nested: listener registry remains capped at 64");
}

void testLifecycleAndMalformedAncestry()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness stale;
    loadFixture(stale, error);
    const ScriptResult saved = stale.execute(
        "var oldElement = document.querySelector(\"#x\");");
    expect(saved.succeeded(), "lifecycle: save old receiver");
    expect(stale.invalidateDocumentGeneration(error),
        "lifecycle: invalidate generation");
    expect(error == RuntimeErrorCode::None, "lifecycle: invalidation no error");
    const ScriptResult staleResult = stale.execute(
        "var staleMatches = oldElement.matches(\".leaf\") === false; "
        "var staleClosest = oldElement.closest(\".panel\") === null;");
    expect(staleResult.succeeded(), "lifecycle: stale selector calls fail closed");
    expectBoolean(stale, "staleMatches", true,
        "lifecycle: stale matches returns false");
    expectBoolean(stale, "staleClosest", true,
        "lifecycle: stale closest returns null");

    NavigatorScriptExecutionHarness replacement;
    loadFixture(replacement, error);
    const ScriptResult oldScript = replacement.execute(
        "var oldSerialElement = document.querySelector(\"#x\");");
    expect(oldScript.succeeded(), "lifecycle: replacement old handle");
    expect(replacement.replaceHtml("file:///replacement.html",
        kReplacementFixture, error), "lifecycle: document replacement");
    expect(error == RuntimeErrorCode::None, "lifecycle: replacement no error");
    const ScriptResult fresh = replacement.execute(
        "var freshElement = document.querySelector(\"#x\"); "
        "var freshWorks = freshElement.matches(\".fresh-action\") && "
        "freshElement.closest(\"#replacement\") === "
        "document.querySelector(\"#replacement\");");
    expect(fresh.succeeded(), "lifecycle: fresh generation script");
    expectBoolean(replacement, "freshWorks", true,
        "lifecycle: new generation works with reused serial");

    NavigatorScriptExecutionHarness malformed;
    loadFixture(malformed, error);
    const std::uint64_t outerSerial = serialById(malformed, "outer");
    const std::uint64_t innerSerial = serialById(malformed, "inner");
    expect(outerSerial != 0u && innerSerial != 0u,
        "malformed: cycle nodes exist");
    for (gxos::web::HtmlElementRef& element :
            malformed.document().structuralElements) {
        if (element.serial == outerSerial) element.parentSerial = innerSerial;
        if (element.serial == innerSerial) element.parentSerial = outerSerial;
    }
    const ScriptResult cycle = malformed.execute(
        "var cycleElement = document.querySelector(\"#x\"); "
        "var cycleSafe = cycleElement.closest(\"#missing\") === null && "
        "cycleElement.matches(\"#x\");");
    expect(cycle.succeeded(), "malformed: bounded cycle script");
    expectBoolean(malformed, "cycleSafe", true,
        "malformed: parent cycle terminates and self match survives");
}

void testRegressionSurface()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var regressionForm = document.forms["settings"];
var regressionName = regressionForm.elements["name"];
var regressionOption = document.querySelectorAll("option")[0];
var regression = document.querySelector("#x") ===
    document.getElementById("x") &&
    document.querySelectorAll(".panel").length === 3 &&
    regressionForm === document.querySelector("form#settings") &&
    regressionName === document.getElementById("name") &&
    regressionOption === document.querySelector("#mode-a") &&
    document.querySelector("form input") === null &&
    document.querySelectorAll("form input").length === 0;
)JS");
    expect(result.succeeded(), "regression: JS36/JS35 surface script");
    expectBoolean(harness, "regression", true,
        "regression: query, forms, options, and invalid selectors unchanged");
}

} // namespace

int main()
{
    testMatchesAndClosest();
    testEventIntegrationAndReentry();
    testLifecycleAndMalformedAncestry();
    testRegressionSurface();
    if (failures != 0) {
        std::cerr << failures << " JS37 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS37 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
