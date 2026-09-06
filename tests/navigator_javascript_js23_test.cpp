#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::RuntimeLimits;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<div id="root"><div id="parent">
<input id="target" type="text" value="">
</div></div>
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
    expect(result.runtimeError.code == expected, label + ": error is " +
        std::string(gxos::javascript::runtimeErrorCodeName(
            result.runtimeError.code)));
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const std::string& id)
{
    for (const gxos::web::HtmlElementRef& element :
        harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0;
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const std::string& label)
{
    expect(harness.loadHtml("file:///js23.html", kFixture, error),
        label + ": fixture loads");
    expect(harness.relayout(), label + ": relayout succeeds");
}

bool key(NavigatorScriptExecutionHarness& harness, const std::string& id,
    int keyCode, bool down, bool shift, RuntimeErrorCode& error,
    bool* prevented, const std::string& label)
{
    const std::uint64_t serial = id.empty() ? 0 : serialById(harness, id);
    if (!id.empty()) expect(serial != 0, label + ": serial exists");
    if (!id.empty() && serial == 0) return false;
    const bool dispatched = harness.hostAdapter().dispatchKeyboardEvent(
        harness.runtime(), serial, keyCode, down, shift, error, prevented);
    return dispatched;
}

void testKeyboardValuesAndLifetime()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "values");
    ScriptResult result = harness.execute(R"JS(
var target = document.getElementById("target");
var valueLog = "";
var savedKey = "";
var savedCode = "";
function record(event) {
    valueLog = valueLog + event.type + ":" + event.key + ":" + event.code + ";";
    if (event.type === "keydown" && event.key === "a") {
        savedKey = event.key; savedCode = event.code;
    }
}
target.addEventListener("keydown", record);
target.addEventListener("keyup", record);
)JS");
    expect(result.succeeded(), "values: listener setup succeeds");
    bool prevented = false;
    expect(key(harness, "target", 65, true, false, error, &prevented,
        "values: a keydown"), "values: a keydown dispatches");
    expect(key(harness, "target", 65, false, false, error, &prevented,
        "values: a keyup"), "values: a keyup dispatches");
    expect(key(harness, "target", 65, true, true, error, &prevented,
        "values: shifted A keydown"), "values: shifted A keydown dispatches");
    expect(key(harness, "target", 13, true, false, error, &prevented,
        "values: Enter keydown"), "values: Enter keydown dispatches");
    expect(key(harness, "target", 27, true, false, error, &prevented,
        "values: Escape keydown"), "values: Escape keydown dispatches");
    expect(key(harness, "target", 37, true, false, error, &prevented,
        "values: arrow keydown"), "values: arrow keydown dispatches");
    expect(key(harness, "target", 49, true, false, error, &prevented,
        "values: digit keydown"), "values: digit keydown dispatches");
    expect(key(harness, "target", 16, true, false, error, &prevented,
        "values: modifier keydown"), "values: modifier keydown dispatches");
    expectString(harness, "valueLog",
        "keydown:a:KeyA;keyup:a:KeyA;keydown:A:KeyA;keydown:Enter:Enter;"
        "keydown:Escape:Escape;keydown:ArrowLeft:ArrowLeft;keydown:1:Digit1;"
        "keydown:Shift:Shift;", "values: type/key/code identity");
    expectString(harness, "savedKey", "a", "values: cached callback value stays stable");
    expectString(harness, "savedCode", "KeyA", "values: cached code stays stable");

    result = harness.execute(
        "target.removeEventListener(\"keydown\", record);"
        "target.removeEventListener(\"keyup\", record);"
        "function repeated(event) {}"
        "target.addEventListener(\"keydown\", repeated);");
    expect(result.succeeded(), "values: bounded repeat listener setup succeeds");

    const std::size_t objects = harness.runtime().objectCount();
    const std::size_t strings = harness.runtime().runtimeStringValueCount();
    for (int index = 0; index < 100; ++index)
        expect(key(harness, "target", 65, true, false, error, &prevented,
            "values: repeated keydown " + std::to_string(index + 1)),
            "values: repeated keydown dispatches");
    expect(harness.runtime().objectCount() == objects,
        "values: repeated keyboard dispatch reuses Event object");
    expect(harness.runtime().runtimeStringValueCount() == strings,
        "values: repeated bounded key values do not grow string store");
}

void testPropagationAndListenerSemantics()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "propagation");
    ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var target = document.getElementById("target");
var order = "";
function log(event) { order = order + event.currentTarget.tagName + ":" + event.eventPhase + ";"; }
document.addEventListener("keydown", log, true);
parent.addEventListener("keydown", log, true);
target.addEventListener("keydown", log);
parent.addEventListener("keydown", log);
document.addEventListener("keydown", log);
document.addEventListener("keyup", function(event) { order = order + "U" + event.eventPhase + ";"; }, true);
document.addEventListener("keyup", function(event) { order = order + "u" + event.eventPhase + ";"; });
)JS");
    expect(result.succeeded(), "propagation: setup succeeds");
    bool prevented = false;
    expect(key(harness, "target", 65, true, false, error, &prevented,
        "propagation: keydown"), "propagation: keydown dispatches");
    expectString(harness, "order", "undefined:1;DIV:1;INPUT:2;DIV:3;undefined:3;",
        "propagation: document capture, target, and bubble order");
    expect(key(harness, "target", 65, false, false, error, &prevented,
        "propagation: keyup"), "propagation: keyup dispatches");
    expectString(harness, "order", "undefined:1;DIV:1;INPUT:2;DIV:3;undefined:3;U1;u3;",
        "propagation: keyup uses the same path");

    result = harness.execute(R"JS(
var onceCount = 0;
var removedCount = 0;
function onceHandler(event) { onceCount = onceCount + 1; }
function removedHandler(event) { removedCount = removedCount + 1; }
target.addEventListener("keydown", onceHandler, { once: true });
target.addEventListener("keydown", removedHandler, true);
target.removeEventListener("keydown", removedHandler, true);
)JS");
    expect(result.succeeded(), "semantics: once/removal setup succeeds");
    expect(key(harness, "target", 65, true, false, error, &prevented,
        "semantics: once first"), "semantics: once first dispatches");
    expect(key(harness, "target", 65, true, false, error, &prevented,
        "semantics: once second"), "semantics: once second dispatches");
    expectNumber(harness, "onceCount", 1, "semantics: once only fires once");
    expectNumber(harness, "removedCount", 0, "semantics: removal suppresses listener");

    result = harness.execute(R"JS(
var shorthandCount = 0;
function shorthand(event) { shorthandCount = shorthandCount + event.eventPhase; }
parent.addEventListener("keydown", shorthand, true);
)JS");
    expect(result.succeeded(), "semantics: Boolean capture setup succeeds");
    expect(key(harness, "target", 65, true, false, error, &prevented,
        "semantics: Boolean capture"), "semantics: Boolean capture dispatches");
    expectNumber(harness, "shorthandCount", 1,
        "semantics: Boolean capture remains capture");

    result = harness.execute(R"JS(
var stopLog = "";
function stop(event) { stopLog = stopLog + "a"; event.stopPropagation(); }
function sameNode(event) { stopLog = stopLog + "b"; }
function ancestor(event) { stopLog = stopLog + "p"; }
target.addEventListener("keydown", stop);
target.addEventListener("keydown", sameNode);
parent.addEventListener("keydown", ancestor);
)JS");
    expect(result.succeeded(), "semantics: stop setup succeeds");
    expect(key(harness, "target", 65, true, false, error, &prevented,
        "semantics: stopPropagation"), "semantics: stop dispatches");
    expectString(harness, "stopLog", "ab",
        "semantics: stopPropagation preserves same target listeners");

    result = harness.execute(
        "target.addEventListener(\"keydow\", stop);"
        "target.addEventListener(\"keydownx\", stop);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "semantics: wrong event name is rejected exactly");
    expect(harness.hostAdapter().clickListenerCount() == 11u,
        "semantics: wrong event names consume no listener slots (actual=" +
        std::to_string(harness.hostAdapter().clickListenerCount()) + ")");
}

void testImmediateStopAndDefaultPrevention()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "controls");
    ScriptResult result = harness.execute(R"JS(
var target = document.getElementById("target");
var log = "";
target.addEventListener("keydown", function(event) {
    log = log + "a"; event.stopImmediatePropagation();
});
target.addEventListener("keydown", function(event) { log = log + "b"; });
target.addEventListener("keydown", function(event) {
    log = log + "c"; event.preventDefault();
});
)JS");
    expect(result.succeeded(), "controls: setup succeeds");
    bool prevented = false;
    expect(key(harness, "target", 13, true, false, error, &prevented,
        "controls: immediate stop"), "controls: immediate stop dispatches");
    expectString(harness, "log", "a", "controls: immediate stop suppresses later listeners");
    expect(!prevented, "controls: suppressed preventDefault is not observed");

    NavigatorScriptExecutionHarness cancel;
    loadFixture(cancel, error, "cancel");
    result = cancel.execute(
        "var target = document.getElementById(\"target\");"
        "target.addEventListener(\"keydown\", function(event) { event.preventDefault(); });");
    expect(result.succeeded(), "controls: preventDefault setup succeeds");
    expect(key(cancel, "target", 65, true, false, error, &prevented,
        "controls: preventDefault"), "controls: preventDefault dispatches");
    expect(prevented, "controls: preventDefault is reported to host");
}

void testBoundedCapacity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "capacity");
    for (int index = 0; index < 64; ++index) {
        const std::string source = "function listener" + std::to_string(index) +
            "(event) {} document.addEventListener(\"keydown\", listener" +
            std::to_string(index) + ");";
        const ScriptResult result = harness.execute(source);
        expect(result.succeeded(), "capacity: registration " +
            std::to_string(index + 1) + " succeeds");
    }
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: 64 shared keyboard registrations fit");
    const ScriptResult overflow = harness.execute(
        "function overflow(event) {} document.addEventListener(\"keyup\", overflow);");
    expectError(overflow, RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: registration 65 remains bounded");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: overflow does not grow listener table");
}

} // namespace

int main()
{
    testKeyboardValuesAndLifetime();
    testPropagationAndListenerSemantics();
    testImmediateStopAndDefaultPrevention();
    testBoundedCapacity();
    if (failures != 0) {
        std::cerr << failures << " JS23 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS23 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
