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
<html><body><div id="root"><div id="parent">
<input id="unchecked" type="checkbox" name="flags" value="off">
<input id="enabled" type="checkbox" name="flags" value="on" checked>
<input id="first" type="radio" name="mode" value="one" checked>
<input id="second" type="radio" name="mode" value="two">
<input id="third" type="radio" name="mode" value="three">
<input id="other" type="radio" name="other-mode" value="other">
<select id="modeSelect" name="selection">
  <option value="one" selected>One</option>
  <option value="two">Two</option>
  <option value="three">Three</option>
</select>
<select id="otherSelect" name="selection">
  <option value="red" selected>Red</option>
  <option value="blue">Blue</option>
</select>
<input id="focusTarget" type="text" value="">
</div></div></body></html>
)HTML";

const char* kResetFixture = R"HTML(
<html><body>
<input id="resetCheck" type="checkbox" checked>
<select id="resetSelect"><option value="initial">Initial</option><option value="chosen" selected>Chosen</option></select>
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
    RuntimeErrorCode& error, const char* fixture = kFixture)
{
    expect(harness.loadHtml("file:///js27.html", fixture, error),
        "fixture loads");
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
    return 0;
}

void focus(NavigatorScriptExecutionHarness& harness, const char* id,
    RuntimeErrorCode& error, const std::string& label = "focus")
{
    expect(harness.focusElement(serialById(harness, id), error), label);
    expect(error == RuntimeErrorCode::None, std::string(label) + ": no error");
}

void userActivate(NavigatorScriptExecutionHarness& harness, const char* id,
    RuntimeErrorCode& error, const char* label = "user activation")
{
    focus(harness, id, error, std::string(label) + ": focus");
    expect(harness.dispatchFocusedUserFormControl(error), label);
    expect(error == RuntimeErrorCode::None, std::string(label) + ": no error");
}

void testInitialProjectionAndSilentAssignments()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var unchecked = document.getElementById("unchecked");
var enabled = document.getElementById("enabled");
var first = document.getElementById("first");
var second = document.getElementById("second");
var mode = document.getElementById("modeSelect");
var otherMode = document.getElementById("otherSelect");
var inputs = 0;
var changes = 0;
unchecked.addEventListener("input", function() { inputs = inputs + 1; });
unchecked.addEventListener("change", function() { changes = changes + 1; });
second.addEventListener("input", function() { inputs = inputs + 10; });
second.addEventListener("change", function() { changes = changes + 10; });
mode.addEventListener("input", function() { inputs = inputs + 100; });
mode.addEventListener("change", function() { changes = changes + 100; });
var initialUnchecked = unchecked.checked;
var initialEnabled = enabled.checked;
var initialFirst = first.checked;
var initialSecond = second.checked;
var initialMode = mode.value;
unchecked.checked = true;
unchecked.checked = false;
second.checked = true;
second.checked = false;
mode.value = "two";
var assignedMode = mode.value;
mode.value = "missing";
var unknownMode = mode.value;
otherMode.value = "blue";
var independentMode = otherMode.value;
)JS");
    expect(result.succeeded(), "projection: setup and assignment");
    expectBoolean(harness, "initialUnchecked", false, "projection: initial unchecked");
    expectBoolean(harness, "initialEnabled", true, "projection: checked markup");
    expectBoolean(harness, "initialFirst", true, "projection: radio initial state");
    expectBoolean(harness, "initialSecond", false, "projection: radio initial peer");
    expectString(harness, "initialMode", "one", "projection: selected option");
    result = harness.execute("var finalUnchecked = unchecked.checked; var finalSecond = second.checked;");
    expect(result.succeeded(), "projection: final checked getters");
    expectBoolean(harness, "finalUnchecked", false, "projection: script checkbox final");
    expectBoolean(harness, "finalSecond", false, "projection: script radio final");
    expectString(harness, "assignedMode", "two", "projection: script select setter");
    expectString(harness, "unknownMode", "two", "projection: unknown select value retains");
    expectString(harness, "independentMode", "blue", "projection: independent select");
    expectNumber(harness, "inputs", 0, "projection: assignments are input-silent");
    expectNumber(harness, "changes", 0, "projection: assignments are change-silent");
}

void testCheckboxUserTransitionsAndMetadata()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var box = document.getElementById("unchecked");
var log = "";
var inputCount = 0;
var changeCount = 0;
var inputState = false;
var changeState = false;
box.addEventListener("input", function(event) {
    inputCount = inputCount + 1;
    inputState = box.checked;
    log = log + event.type + ":" + box.checked + ":" + event.bubbles + ":" + event.cancelable + ";";
});
box.addEventListener("change", function(event) {
    changeCount = changeCount + 1;
    changeState = box.checked;
    log = log + event.type + ":" + box.checked + ":" + event.bubbles + ":" + event.cancelable + ";";
});
)JS");
    expect(result.succeeded(), "checkbox: listeners install");
    userActivate(harness, "unchecked", error, "checkbox: first toggle");
    result = harness.execute("var firstChecked = box.checked;");
    expect(result.succeeded(), "checkbox: read first state");
    expectBoolean(harness, "firstChecked", true, "checkbox: first state");
    expectBoolean(harness, "inputState", true, "checkbox: input sees new state");
    expectBoolean(harness, "changeState", true, "checkbox: change sees new state");
    expectNumber(harness, "inputCount", 1, "checkbox: one input");
    expectNumber(harness, "changeCount", 1, "checkbox: one change");
    userActivate(harness, "unchecked", error, "checkbox: second toggle");
    result = harness.execute("var secondChecked = box.checked;");
    expect(result.succeeded(), "checkbox: read second state");
    expectBoolean(harness, "secondChecked", false, "checkbox: second state");
    expectNumber(harness, "inputCount", 2, "checkbox: second input");
    expectNumber(harness, "changeCount", 2, "checkbox: second change");
    expectString(harness, "log",
        "input:true:true:false;change:true:true:false;input:false:true:false;change:false:true:false;",
        "checkbox: input then change metadata/order");
}

void testRadioGroupAndReentrantMutation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var second = document.getElementById("second");
var third = document.getElementById("third");
var radioLog = "";
var radioInputs = 0;
var radioChanges = 0;
second.addEventListener("input", function() {
    radioInputs = radioInputs + 1;
    radioLog = radioLog + "input:" + first.checked + ":" + second.checked + ";";
});
second.addEventListener("change", function() {
    radioChanges = radioChanges + 1;
    radioLog = radioLog + "change:" + first.checked + ":" + second.checked + ";";
    third.checked = true;
});
)JS");
    expect(result.succeeded(), "radio: listeners install");
    userActivate(harness, "second", error, "radio: select unchecked peer");
    result = harness.execute("var afterFirst = first.checked; var afterSecond = second.checked; var afterThird = third.checked;");
    expect(result.succeeded(), "radio: read group state");
    expectBoolean(harness, "afterFirst", false, "radio: previous peer unchecked");
    expectBoolean(harness, "afterSecond", false, "radio: reentrant peer mutation");
    expectBoolean(harness, "afterThird", true, "radio: listener selected third silently");
    expectNumber(harness, "radioInputs", 1, "radio: one input");
    expectNumber(harness, "radioChanges", 1, "radio: one change");
    expectString(harness, "radioLog", "input:false:true;change:false:true;",
        "radio: listeners observe group transition before reentrant mutation");
    userActivate(harness, "third", error, "radio: already checked activation");
    expectNumber(harness, "radioInputs", 1, "radio: no redundant input");
    expectNumber(harness, "radioChanges", 1, "radio: no redundant change");
    result = harness.execute("second.checked = true; var afterScriptSecond = second.checked; var afterScriptThird = third.checked;");
    expect(result.succeeded(), "radio: scripted group assignment");
    expectBoolean(harness, "afterScriptSecond", true, "radio: scripted target checked");
    expectBoolean(harness, "afterScriptThird", false, "radio: scripted peer unchecked");
    expectNumber(harness, "radioInputs", 1, "radio: script remains input-silent");
    expectNumber(harness, "radioChanges", 1, "radio: script remains change-silent");
}

void testSelectUserTransitionsAndReentrantMutation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var mode = document.getElementById("modeSelect");
var log = "";
var inputCount = 0;
var changeCount = 0;
mode.addEventListener("input", function(event) {
    inputCount = inputCount + 1;
    log = log + event.type + ":" + mode.value + ":" + event.bubbles + ":" + event.cancelable + ";";
    mode.value = "normalized";
});
mode.addEventListener("change", function(event) {
    changeCount = changeCount + 1;
    log = log + event.type + ":" + mode.value + ":" + event.bubbles + ":" + event.cancelable + ";";
});
)JS");
    expect(result.succeeded(), "select: listeners install");
    userActivate(harness, "modeSelect", error, "select: user transition");
    result = harness.execute("var userValue = mode.value;");
    expect(result.succeeded(), "select: read user value");
    expectString(harness, "userValue", "two", "select: user selected value");
    expectNumber(harness, "inputCount", 1, "select: one input");
    expectNumber(harness, "changeCount", 1, "select: one change");
    expectString(harness, "log", "input:two:true:false;change:two:true:false;",
        "select: reentrant unknown assignment is silent");
    userActivate(harness, "modeSelect", error, "select: second user transition");
    result = harness.execute("var secondUserValue = mode.value;");
    expect(result.succeeded(), "select: read second user value");
    expectString(harness, "secondUserValue", "three", "select: second value");
    expectNumber(harness, "inputCount", 2, "select: second input");
    expectNumber(harness, "changeCount", 2, "select: second change");
}

void testPropagationAndListenerFeatures()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var target = document.getElementById("unchecked");
var log = "";
function record(prefix, event) {
    log = log + prefix + ":" + event.type + ":" + event.target.id + ":" +
        event.eventPhase + ":" + event.bubbles + ":" + event.cancelable + ";";
}
document.addEventListener("change", function(event) { record("dc", event); }, true);
parent.addEventListener("change", function(event) { record("pc", event); }, true);
target.addEventListener("change", function(event) { record("t", event); });
parent.addEventListener("change", function(event) { record("pb", event); });
document.addEventListener("change", function(event) { record("db", event); });
document.addEventListener("input", function(event) { record("di", event); }, true);
target.addEventListener("input", function(event) { record("ti", event); });
document.addEventListener("input", function(event) { record("do", event); });
)JS");
    expect(result.succeeded(), "propagation: listeners install");
    userActivate(harness, "unchecked", error, "propagation: activation");
    expectString(harness, "log",
        "di:input:unchecked:1:true:false;ti:input:unchecked:2:true:false;do:input:unchecked:3:true:false;"
        "dc:change:unchecked:1:true:false;pc:change:unchecked:1:true:false;"
        "t:change:unchecked:2:true:false;pb:change:unchecked:3:true:false;"
        "db:change:unchecked:3:true:false;",
        "propagation: input/change capture-target-bubble");

    NavigatorScriptExecutionHarness features;
    loadFixture(features, error);
    result = features.execute(R"JS(
var box = document.getElementById("unchecked");
var once = 0;
var removed = 0;
var stopLog = "";
function onceHandler() { once = once + 1; }
function removedHandler() { removed = removed + 1; }
box.addEventListener("input", onceHandler, { once: true });
box.addEventListener("input", removedHandler, true);
box.removeEventListener("input", removedHandler, true);
box.addEventListener("change", function(event) { stopLog = stopLog + "one;"; event.stopPropagation(); });
box.addEventListener("change", function() { stopLog = stopLog + "two;"; });
)JS");
    expect(result.succeeded(), "features: listeners install");
    userActivate(features, "unchecked", error, "features: first");
    userActivate(features, "unchecked", error, "features: second");
    expectNumber(features, "once", 1, "features: once");
    expectNumber(features, "removed", 0, "features: remove");
    expectString(features, "stopLog", "one;two;one;two;",
        "features: stopPropagation preserves target listeners");

    NavigatorScriptExecutionHarness names;
    loadFixture(names, error);
    result = names.execute("var box = document.getElementById(\"unchecked\"); function handler(event) {} box.addEventListener(\"input\", handler);");
    expect(result.succeeded(), "names: valid listener");
    expectError(names.execute("box.addEventListener(\"inputs\", handler);"),
        RuntimeErrorCode::HostInvalidValue, "names: inputs rejected");
    expectError(names.execute("box.addEventListener(\"changes\", handler);"),
        RuntimeErrorCode::HostInvalidValue, "names: changes rejected");
    expect(names.hostAdapter().clickListenerCount() == 1u,
        "names: rejected aliases consume no slots");
}

void testFocusReentryCapacityAndReset()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness focusHarness;
    loadFixture(focusHarness, error);
    ScriptResult result = focusHarness.execute(R"JS(
var box = document.getElementById("unchecked");
var other = document.getElementById("focusTarget");
var redirects = 0;
box.addEventListener("change", function() { redirects = redirects + 1; other.focus(); });
)JS");
    expect(result.succeeded(), "focus: listener install");
    userActivate(focusHarness, "unchecked", error, "focus: activation");
    expect(focusHarness.focusedElementSerial() == serialById(focusHarness, "focusTarget"),
        "focus: change listener owns final focus");
    expectNumber(focusHarness, "redirects", 1, "focus: redirect once");

    NavigatorScriptExecutionHarness capacity;
    loadFixture(capacity, error);
    for (int index = 0; index < 64; ++index) {
        const std::string source = "function listener" + std::to_string(index) +
            "(event) {} document.addEventListener(\"change\", listener" +
            std::to_string(index) + ");";
        expect(capacity.execute(source).succeeded(),
            "capacity: registration " + std::to_string(index + 1));
    }
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: global listener bound remains 64");
    expectError(capacity.execute(
        "function overflow(event) {} document.addEventListener(\"input\", overflow);"),
        RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th listener rejected");

    NavigatorScriptExecutionHarness reset;
    loadFixture(reset, error);
    result = reset.execute("var check = document.getElementById(\"unchecked\"); check.checked = true;");
    expect(result.succeeded(), "reset: scripted state before replacement");
    expect(reset.replaceHtml("file:///js27-replacement.html", kResetFixture, error),
        "reset: replacement loads");
    expect(reset.relayout(), "reset: replacement relayout");
    result = reset.execute(R"JS(
var resetCheck = document.getElementById("resetCheck");
var resetSelect = document.getElementById("resetSelect");
var resetChecked = resetCheck.checked;
var resetValue = resetSelect.value;
)JS");
    expect(result.succeeded(), "reset: replacement projection");
    expectBoolean(reset, "resetChecked", true, "reset: checked state rebuilt");
    expectString(reset, "resetValue", "chosen", "reset: selected state rebuilt");
    expect(reset.hostAdapter().clickListenerCount() == 0u,
        "reset: listener registry cleared with document");
}

} // namespace

int main()
{
    testInitialProjectionAndSilentAssignments();
    testCheckboxUserTransitionsAndMetadata();
    testRadioGroupAndReentrantMutation();
    testSelectUserTransitionsAndReentrantMutation();
    testPropagationAndListenerFeatures();
    testFocusReentryCapacityAndReset();
    if (failures != 0) {
        std::cerr << failures << " JS27 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS27 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
