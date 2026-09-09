#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<div id="container" class="outer wrapper">
  <span id="first" class="item warning"></span>
  <span id="second" class="item"></span>
  <div id="nested" class="item"><button id="nested-button">Nested</button></div>
</div>
<div id="outside" class="item"></div>
<form id="login" name="login" class="panel form-class">
  <input id="username" class="required field" name="user" type="text" value="seed">
  <input id="other-input" class="field" name="other" type="text" value="other">
  <select id="mode" name="mode">
    <option id="mode-a" value="a" selected>A</option>
    <option id="mode-b" value="b">B</option>
  </select>
  <input id="submit" type="submit" value="Submit">
  <input id="reset" type="reset" value="Reset">
</form>
<input id="unowned" class="required" type="text" value="outside">
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
    expect(harness.loadHtml("file:///js36.html", kFixture, error),
        "fixture: loads");
    expect(error == RuntimeErrorCode::None, "fixture: no load error");
    expect(harness.relayout(), "fixture: relayout");
}

void testBasicSelectorsAndOrdering()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var methods = document.querySelector !== undefined &&
    document.querySelectorAll !== undefined;
var login = document.querySelector("#login");
var warning = document.querySelector(".warning");
var firstInput = document.querySelector("input");
var inputs = document.querySelectorAll("input");
var methodsAndBasics = methods && login !== null && warning ===
    document.getElementById("first") && firstInput ===
    document.getElementById("username");
var ordering = inputs.length === 5 && inputs[0] === firstInput &&
    inputs[1] === document.getElementById("other-input") &&
    inputs[2] === document.getElementById("submit") &&
    inputs[3] === document.getElementById("reset") &&
    inputs[4] === document.getElementById("unowned");
var exactClassToken = document.querySelector(".item") ===
    document.getElementById("first");
var noSubstring = document.querySelector(".warn") === null;
var exactCase = document.querySelector(".Warning") === null;
var tagCase = document.querySelector("INPUT") === firstInput;
var trimmed = document.querySelector("  #login  ") === login;
var noMatch = document.querySelector("#missing") === null &&
    document.querySelector(".missing") === null;
var identity = login === document.getElementById("login") &&
    inputs[0] === document.getElementById("username");
)JS");
    expect(result.succeeded(), "basic selectors: script");
    expectBoolean(harness, "methodsAndBasics", true,
        "basic selectors: methods and matches");
    expectBoolean(harness, "ordering", true,
        "basic selectors: document order");
    expectBoolean(harness, "exactClassToken", true,
        "basic selectors: class token");
    expectBoolean(harness, "noSubstring", true,
        "basic selectors: no substring class match");
    expectBoolean(harness, "exactCase", true,
        "basic selectors: class case");
    expectBoolean(harness, "tagCase", true,
        "basic selectors: tag case");
    expectBoolean(harness, "trimmed", true,
        "basic selectors: outer whitespace");
    expectBoolean(harness, "noMatch", true, "basic selectors: no match");
    expectBoolean(harness, "identity", true,
        "basic selectors: canonical identity");
}

void testScopedAndCompoundSelectors()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var container = document.querySelector("#container");
var nested = document.querySelector("#nested");
var scopedItems = container.querySelectorAll(".item");
var scopedInputs = document.querySelector("#login").querySelectorAll("input");
var form = document.forms["login"];
var user = document.querySelector("#username");
var select = document.querySelector("#mode");
var option = document.querySelector("option");
var scope = scopedItems.length === 3 && scopedItems[0] ===
    document.getElementById("first") && scopedItems[1] ===
    document.getElementById("second") && scopedItems[2] === nested &&
    container.querySelector("div") === nested &&
    container.querySelector("#outside") === null;
var formScope = scopedInputs.length === 4 && scopedInputs[0] === user &&
    form.querySelector("form") === null &&
    form.querySelector("input") === user &&
    form.querySelector("#unowned") === null;
var leafScope = user.querySelector("span") === null &&
    user.querySelectorAll("span").length === 0;
var compounds = document.querySelector("div.item") === nested &&
    document.querySelector("form#login") === form &&
    document.querySelector("form#username") === null &&
    document.querySelector("input.required") === user &&
    document.querySelector("div#login") === null;
var collectionIdentity = form === document.getElementById("login") &&
    user === form.elements[0] && option === select.options[0];
)JS");
    expect(result.succeeded(), "scoped selectors: script");
    if (!result.succeeded()) {
        std::cerr << "scoped runtime error=" << static_cast<int>(
            result.runtimeError.code) << " status=" << static_cast<int>(
            result.status) << "\n";
    }
    expectBoolean(harness, "scope", true,
        "scoped selectors: descendant bounds and order");
    expectBoolean(harness, "formScope", true,
        "scoped selectors: form descendants");
    expectBoolean(harness, "leafScope", true,
        "scoped selectors: leaf empty");
    expectBoolean(harness, "compounds", true,
        "scoped selectors: optional compounds");
    expectBoolean(harness, "collectionIdentity", true,
        "scoped selectors: collection identity");
}

void testInvalidSelectorsAndReadOnlyCollections()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var invalid = document.querySelector("") === null &&
    document.querySelector("   ") === null &&
    document.querySelector("form input") === null &&
    document.querySelector("input[type=text]") === null &&
    document.querySelector("input, textarea") === null &&
    document.querySelector(":focus") === null;
var invalidCollections = document.querySelectorAll("").length === 0 &&
    document.querySelectorAll("form input").length === 0 &&
    document.querySelectorAll("*").length === 0 &&
    document.querySelectorAll("input,textarea").length === 0;
var empty = document.querySelectorAll(".missing");
var emptyCollection = empty !== null && empty.length === 0 &&
    empty[999] === undefined;
)JS");
    expect(result.succeeded(), "invalid selectors: script");
    expectBoolean(harness, "invalid", true,
        "invalid selectors: querySelector null");
    expectBoolean(harness, "invalidCollections", true,
        "invalid selectors: querySelectorAll empty");
    expectBoolean(harness, "emptyCollection", true,
        "invalid selectors: bounded empty collection");
    expectError(harness.execute("empty[0] = document.querySelector(\"#login\");"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "selector collection: indexed write read-only");
    expectError(harness.execute("empty.length = 1;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "selector collection: length write read-only");
    std::string oversized = "var tooLong = document.querySelectorAll(\"" +
        std::string(gxos::javascript::kNavigatorScriptMaxSelectorLength +
            1u, 'x') +
        "\").length === 0;";
    const ScriptResult oversizedResult = harness.execute(oversized);
    expect(oversizedResult.succeeded(), "invalid selectors: oversized script");
    expectBoolean(harness, "tooLong", true,
        "invalid selectors: oversized empty collection");
}

void testIntegrationAndLifecycle()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var user = document.querySelector("#username");
var submit = document.querySelector("#submit");
var reset = document.querySelector("#reset");
var form = document.querySelector("form#login");
var focusSeen = false;
var clickSeen = false;
var nestedSeen = false;
var submitted = "none";
user.addEventListener("focus", function(event) {
  focusSeen = document.querySelector("#username") === event.target;
});
user.focus();
var activeCoherent = document.activeElement === user;
submit.addEventListener("click", function(event) {
  clickSeen = document.querySelector("#submit") === event.target;
  nestedSeen = document.querySelector("#nested-button") !== null;
});
form.addEventListener("submit", function(event) {
  submitted = document.querySelector("#username").value;
  event.preventDefault();
});
user.value = "alice";
submit.click();
var submitIntegration = submitted === "alice";
var resetSeen = false;
form.addEventListener("reset", function() {
  resetSeen = document.querySelector("#username") === user;
});
user.defaultValue = "reset-value";
user.value = "changed";
reset.click();
var resetIntegration = resetSeen && user.value === "reset-value" &&
    user.defaultValue === "reset-value";
var oldResults = document.querySelectorAll(".item");
var oldElement = document.querySelector("#container");
)JS");
    expect(result.succeeded(), "integration: script");
    if (!result.succeeded()) {
        std::cerr << "integration runtime error=" << static_cast<int>(
            result.runtimeError.code) << " status=" << static_cast<int>(
            result.status) << " line=" << result.runtimeError.location.line <<
            " column=" << result.runtimeError.location.column << "\n";
    }
    expectBoolean(harness, "focusSeen", true,
        "integration: selector in focus listener");
    expectBoolean(harness, "activeCoherent", true,
        "integration: activeElement identity");
    expectBoolean(harness, "clickSeen", true,
        "integration: selector in click listener");
    expectBoolean(harness, "nestedSeen", true,
        "integration: nested lookup");
    expectBoolean(harness, "submitIntegration", true,
        "integration: submit current value");
    expectBoolean(harness, "resetIntegration", true,
        "integration: reset current/default value");
    expect(harness.invalidateDocumentGeneration(error),
        "lifecycle: invalidate generation");
    expect(error == RuntimeErrorCode::None, "lifecycle: invalidate no error");
    expectError(harness.execute("oldResults.length;"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale selector collection");
    expectError(harness.execute("oldElement.querySelector(\"input\");"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale scoped element");
}

} // namespace

int main()
{
    testBasicSelectorsAndOrdering();
    testScopedAndCompoundSelectors();
    testInvalidSelectorsAndReadOnlyCollections();
    testIntegrationAndLifecycle();
    if (failures != 0) {
        std::cerr << failures << " JS36 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS36 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
