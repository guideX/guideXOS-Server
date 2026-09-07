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
<html><body><div id="outer"><div id="parent"><div id="ordinary">Ordinary</div>
<form id="formA" action="file:///js29-result.html" method="get">
<input id="box" type="checkbox" name="enabled" value="yes">
<input id="first" type="radio" name="mode" value="first" checked>
<input id="second" type="radio" name="mode" value="second">
<button id="submit" type="submit">Submit</button>
<input id="submitInput" type="submit" value="Input submit">
<button id="plain" type="button">Plain</button>
<input id="text" type="text" value="text">
</form></div></div></body></html>
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

void expectNumber(const NavigatorScriptExecutionHarness& harness,
    const char* name, double expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::Number, label + ": Number");
    if (value->isNumber()) expect(value->numberValue() == expected,
        label + ": value");
}

void expectBoolean(const NavigatorScriptExecutionHarness& harness,
    const char* name, bool expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::Boolean, label + ": Boolean");
    if (value->isBoolean()) expect(value->booleanValue() == expected,
        label + ": value");
}

void expectString(const NavigatorScriptExecutionHarness& harness,
    const char* name, const std::string& expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::String, label + ": String");
    if (value->isString()) expect(harness.runtime().stringValue(*value) == expected,
        label + ": value (actual=" + harness.runtime().stringValue(*value) +
            ", expected=" + expected + ")");
}

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": error code");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const std::string& label = "fixture")
{
    expect(harness.loadHtml("file:///js29.html", kFixture, error),
        label + ": loads");
    expect(error == RuntimeErrorCode::None, label + ": no load error");
    expect(harness.relayout(), label + ": relayout");
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
        harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0;
}

void testBasicClickAndPropagation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var ordinary = document.getElementById("ordinary");
var parent = document.getElementById("parent");
var log = "";
var cancel = false;
var before = "";
var after = "";
function record(prefix, event) {
    log = log + prefix + ":" + event.eventPhase + ";";
}
document.addEventListener("click", function(event) {
    record("dc", event);
}, true);
parent.addEventListener("click", function(event) {
    record("pc", event);
}, true);
ordinary.addEventListener("click", function(event) {
    before = event.type + ":" + (event.target === ordinary) + ":" +
        (event.currentTarget === ordinary) + ":" + event.eventPhase + ":" +
        event.bubbles + ":" + event.cancelable + ":" + event.defaultPrevented;
    if (cancel) event.preventDefault();
    after = event.defaultPrevented;
    record("t", event);
});
parent.addEventListener("click", function(event) { record("pb", event); });
document.addEventListener("click", function(event) { record("db", event); });
ordinary.click();
var firstLog = log;
var firstBefore = before;
var firstAfter = after;
cancel = true;
log = "";
ordinary.click();
var secondLog = log;
var secondAfter = after;
)JS");
    expect(result.succeeded(), "basic: setup and two clicks");
    expectString(harness, "firstBefore", "click:true:true:2:true:true:false",
        "basic: click metadata");
    expectBoolean(harness, "firstAfter", false,
        "basic: defaultPrevented starts false");
    expectBoolean(harness, "firstAfter", false,
        "basic: uncanceled event remains clear");
    expectString(harness, "firstLog", "dc:1;pc:1;t:2;pb:3;db:3;",
        "basic: capture-target-bubble order");
    expectString(harness, "secondLog", "dc:1;pc:1;t:2;pb:3;db:3;",
        "basic: canceled click still propagates");
    expectBoolean(harness, "secondAfter", true,
        "basic: preventDefault sets defaultPrevented");
}

void testCheckboxAndRadioDefaultActions()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "controls");
    ScriptResult result = harness.execute(R"JS(
var box = document.getElementById("box");
var first = document.getElementById("first");
var second = document.getElementById("second");
var boxLog = "";
var boxInputs = 0;
var boxChanges = 0;
var cancelBox = false;
box.addEventListener("click", function(event) {
    boxLog = boxLog + "click:" + box.checked + ":" + event.defaultPrevented + ";";
    if (cancelBox) event.preventDefault();
});
box.addEventListener("input", function() {
    boxInputs = boxInputs + 1;
    boxLog = boxLog + "input:" + box.checked + ";";
});
box.addEventListener("change", function() {
    boxChanges = boxChanges + 1;
    boxLog = boxLog + "change:" + box.checked + ";";
});
box.click();
box.click();
var boxAfterTwo = box.checked;
var boxEventsAfterTwo = boxInputs + ":" + boxChanges;
box.checked = true;
var boxAfterAssignment = box.checked;
cancelBox = true;
box.click();
var boxAfterCancel = box.checked;
var boxEventsAfterCancel = boxInputs + ":" + boxChanges;
var radioInputs = 0;
var radioChanges = 0;
var radioCancel = false;
second.addEventListener("click", function(event) {
    if (radioCancel) event.preventDefault();
});
second.addEventListener("input", function() { radioInputs = radioInputs + 1; });
second.addEventListener("change", function() { radioChanges = radioChanges + 1; });
second.click();
var radioSelected = first.checked + ":" + second.checked;
second.click();
var radioEvents = radioInputs + ":" + radioChanges;
first.checked = true;
second.checked = false;
radioCancel = true;
second.click();
var radioAfterCancel = first.checked + ":" + second.checked;
var radioEventsAfterCancel = radioInputs + ":" + radioChanges;
)JS");
    expect(result.succeeded(), "controls: setup");
    expectBoolean(harness, "boxAfterTwo", false, "checkbox: two clicks toggle back");
    expectString(harness, "boxEventsAfterTwo", "2:2", "checkbox: one input/change per toggle");
    expectBoolean(harness, "boxAfterAssignment", true, "checkbox: assignment is authoritative");
    expectBoolean(harness, "boxAfterCancel", true, "checkbox: canceled click does not toggle");
    expectString(harness, "boxEventsAfterCancel", "2:2", "checkbox: canceled click emits no form events");
    expectString(harness, "boxLog",
        "click:false:false;input:true;change:true;click:true:false;input:false;change:false;click:true:false;",
        "checkbox: click sees pre-default state");
    expectString(harness, "radioSelected", "false:true", "radio: peer becomes unchecked");
    expectString(harness, "radioEvents", "1:1", "radio: no redundant events");
    expectString(harness, "radioAfterCancel", "true:false", "radio: canceled click preserves group");
    expectString(harness, "radioEventsAfterCancel", "1:1", "radio: canceled click emits no events");
}

void testSubmitCancellationMatrix()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "submit");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var submit = document.getElementById("submit");
var inputSubmit = document.getElementById("submitInput");
var plain = document.getElementById("plain");
var clicks = 0;
var submits = 0;
var clickCancel = false;
var submitCancel = false;
var log = "";
submit.addEventListener("click", function(event) {
    clicks = clicks + 1;
    log = log + "click;";
    if (clickCancel) event.preventDefault();
});
form.addEventListener("submit", function(event) {
    submits = submits + 1;
    log = log + "submit;";
    if (submitCancel) event.preventDefault();
});
submit.click();
var neither = log;
clickCancel = true;
submit.click();
var clickCanceled = log;
clickCancel = false;
submitCancel = true;
submit.click();
var submitCanceled = log;
submitCancel = false;
submit.addEventListener("click", function(event) { event.stopPropagation(); });
submit.click();
var stopOnly = log;
inputSubmit.click();
var inputSubmitResult = submits;
plain.click();
var plainResult = submits;
)JS");
    expect(result.succeeded(), "submit: setup");
    expectString(harness, "neither", "click;submit;", "submit: neither canceled");
    expectString(harness, "clickCanceled", "click;submit;click;", "submit: click cancellation suppresses submit");
    expectString(harness, "submitCanceled", "click;submit;click;click;submit;", "submit: submit cancellation is second boundary");
    expectString(harness, "stopOnly", "click;submit;click;click;submit;click;submit;",
        "submit: stopPropagation does not cancel default action");
    expectNumber(harness, "inputSubmitResult", 4, "submit: input type submit shares the path");
    expectNumber(harness, "plainResult", 4, "submit: type button does not submit");
}

void testOnceMutationFocusAndReentry()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "reentry");
    ScriptResult result = harness.execute(R"JS(
var box = document.getElementById("box");
var other = document.getElementById("text");
var onceCount = 0;
var onceCancel = 0;
box.addEventListener("click", function(event) {
    onceCount = onceCount + 1;
    onceCancel = onceCancel + 1;
    event.preventDefault();
}, { once: true });
box.click();
box.click();
var onceState = box.checked + ":" + onceCount;
var nestedA = document.getElementById("ordinary");
var nestedB = document.getElementById("parent");
var nestedLog = "";
var outerRestored = false;
nestedB.addEventListener("click", function(event) {
    nestedLog = nestedLog + (event.target === nestedB) + ";";
});
nestedA.addEventListener("click", function(event) {
    nestedLog = nestedLog + "a:" + (event.target === nestedA) + ";";
    nestedB.click();
    outerRestored = event.target === nestedA && event.currentTarget === nestedA && event.eventPhase == 2;
});
nestedA.click();
var mutate = false;
box.addEventListener("click", function() {
    if (mutate) box.checked = true;
});
box.checked = false;
mutate = true;
box.click();
var mutationResult = box.checked;
var focusRedirect = false;
var submitButton = document.getElementById("submit");
submitButton.addEventListener("click", function() {
    if (focusRedirect) other.focus();
});
focusRedirect = true;
submitButton.click();
var self = document.getElementById("plain");
var selfCount = 0;
self.addEventListener("click", function() {
    selfCount = selfCount + 1;
    self.click();
});
)JS");
    expect(result.succeeded(), "reentry: setup");
    expectString(harness, "onceState", "true:1", "once: first cancellation then toggle");
    expectString(harness, "nestedLog", "a:true;true;false;false;false;", "nested: both targets dispatch safely");
    expectBoolean(harness, "outerRestored", true, "nested: outer event metadata restored");
    expectBoolean(harness, "mutationResult", false, "mutation: default action reads current state");
    expect(harness.focusedElementSerial() == serialById(harness, "text"),
        "focus: click listener redirect is deferred safely");

    result = harness.execute("self.click();");
    expect(!result.succeeded(), "self recursion: bounded call reports contained failure");
    expectNumber(harness, "selfCount", 16, "self recursion: activation depth is capped");
}

void testInvalidationAndCapacity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "lifetime");
    ScriptResult result = harness.execute(R"JS(
var oldBox = document.getElementById("box");
var listenerCount = 0;
function clickListener(event) { listenerCount = listenerCount + 1; }
oldBox.addEventListener("click", clickListener);
oldBox.addEventListener("click", clickListener);
)JS");
    expect(result.succeeded(), "capacity: 64 registrations remain available");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "capacity: duplicate registrations do not consume slots");
    for (int index = 0; index < 63; ++index) {
        const std::string source = "function listener" + std::to_string(index) +
            "(event) {} oldBox.addEventListener(\"click\", listener" +
            std::to_string(index) + ");";
        expect(harness.execute(source).succeeded(),
            "capacity: registration " + std::to_string(index + 2));
    }
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: exactly 64 listeners");
    result = harness.execute("function overflow(event) {} oldBox.addEventListener(\"click\", overflow);");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th listener rejected");
    expect(harness.invalidateDocumentGeneration(error),
        "lifetime: generation invalidation");
    result = harness.execute("oldBox.click();");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "lifetime: stale click receiver fails safely");
}

} // namespace

int main()
{
    testBasicClickAndPropagation();
    testCheckboxAndRadioDefaultActions();
    testSubmitCancellationMatrix();
    testOnceMutationFocusAndReentry();
    testInvalidationAndCapacity();
    if (failures != 0) {
        std::cerr << failures << " JS29 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS29 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
