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
<div id="root"><div id="parent">
<input id="first" type="text" value="">
<input id="second" type="text" value="">
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
    expect(result.runtimeError.code == expected, label + ": expected " +
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
    expect(harness.loadHtml("file:///js24.html", kFixture, error),
        label + ": fixture loads");
    expect(harness.relayout(), label + ": relayout succeeds");
}

std::uint64_t firstSerial(const NavigatorScriptExecutionHarness& harness)
{
    return serialById(harness, "first");
}

std::uint64_t secondSerial(const NavigatorScriptExecutionHarness& harness)
{
    return serialById(harness, "second");
}

void testNonBubblingAndCapture()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "focus capture");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var order = "";
var focusTarget = false;
var focusCurrentTarget = false;
var focusPhase = 0;
var focusBubbles = true;
var focusCancelable = true;
document.addEventListener("focus", function (event) {
    order = order + "document-capture;";
    focusTarget = event.target === first;
    focusCurrentTarget = event.currentTarget === document;
    focusPhase = event.eventPhase;
    focusBubbles = event.bubbles;
    focusCancelable = event.cancelable;
}, true);
document.addEventListener("focus", function () { order = order + "document-bubble;"; });
var parent = document.getElementById("parent");
parent.addEventListener("focus", function () { order = order + "parent-capture;"; }, true);
parent.addEventListener("focus", function () { order = order + "parent-bubble;"; });
first.addEventListener("focus", function (event) {
    order = order + "target:" + event.type + ":" + event.target.id + ":" +
        event.currentTarget.id + ":" + event.eventPhase + ";";
});
)JS");
    expect(result.succeeded(), "focus capture: setup succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "focus capture: first focus succeeds");
    expectString(harness, "order",
        "document-capture;parent-capture;target:focus:first:first:2;",
        "focus capture: non-bubbling order");
    expectBoolean(harness, "focusTarget", true,
        "focus capture: target remains first");
    expectBoolean(harness, "focusCurrentTarget", true,
        "focus capture: currentTarget is document");
    expectNumber(harness, "focusPhase", 1,
        "focus capture: ancestor phase");
    expectBoolean(harness, "focusBubbles", false,
        "focus capture: bubbles is false");
    expectBoolean(harness, "focusCancelable", false,
        "focus capture: cancelable is false");

    result = harness.execute(R"JS(
var first = document.getElementById("first");
var cancelable = true;
var prevented = true;
first.addEventListener("focus", function (event) {
    event.preventDefault();
    cancelable = event.cancelable;
    prevented = event.defaultPrevented;
});
)JS");
    expect(result.succeeded(), "focus cancellation: setup succeeds");
    expect(harness.clearFocus(error), "focus cancellation: clear succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "focus cancellation: refocus succeeds");
    expectBoolean(harness, "cancelable", false,
        "focus cancellation: event is observational");
    expectBoolean(harness, "prevented", false,
        "focus cancellation: preventDefault has no effect");
}

void testBlurAndFocusOut()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "blur");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var parent = document.getElementById("parent");
var blurOrder = "";
var blurTarget = false;
var blurCurrentTarget = false;
document.addEventListener("blur", function (event) {
    blurOrder = blurOrder + "document-capture;";
    blurTarget = event.target === first;
    blurCurrentTarget = event.currentTarget === document;
}, true);
document.addEventListener("blur", function () { blurOrder = blurOrder + "document-bubble;"; });
parent.addEventListener("blur", function () { blurOrder = blurOrder + "parent-capture;"; }, true);
parent.addEventListener("blur", function () { blurOrder = blurOrder + "parent-bubble;"; });
first.addEventListener("blur", function (event) {
    blurOrder = blurOrder + "target:" + event.type + ":" + event.target.id + ":" +
        event.currentTarget.id + ":" + event.eventPhase + ";";
});
var outOrder = "";
document.addEventListener("focusout", function () { outOrder = outOrder + "document-capture;"; }, true);
parent.addEventListener("focusout", function () { outOrder = outOrder + "parent-capture;"; }, true);
first.addEventListener("focusout", function (event) {
    outOrder = outOrder + "target:" + event.type + ":" + event.target.id + ":" + event.eventPhase + ";";
});
parent.addEventListener("focusout", function () { outOrder = outOrder + "parent-bubble;"; });
document.addEventListener("focusout", function () { outOrder = outOrder + "document-bubble;"; });
)JS");
    expect(result.succeeded(), "blur: setup succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "blur: initial focus succeeds");
    expect(harness.clearFocus(error), "blur: clear succeeds");
    expectString(harness, "blurOrder",
        "document-capture;parent-capture;target:blur:first:first:2;",
        "blur: non-bubbling order");
    expectBoolean(harness, "blurTarget", true, "blur: target remains first");
    expectBoolean(harness, "blurCurrentTarget", true,
        "blur: currentTarget is document");
    expectString(harness, "outOrder",
        "document-capture;parent-capture;target:focusout:first:2;"
        "parent-bubble;document-bubble;",
        "focusout: bubbling order and target");
}

void testTransitionsAndNoOp()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "transition");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var second = document.getElementById("second");
var log = "";
function record(event) { log = log + event.type + ":" + event.target.id + ";"; }
first.addEventListener("blur", record);
first.addEventListener("focusout", record);
first.addEventListener("focus", record);
first.addEventListener("focusin", record);
second.addEventListener("blur", record);
second.addEventListener("focusout", record);
second.addEventListener("focus", record);
second.addEventListener("focusin", record);
)JS");
    expect(result.succeeded(), "transition: setup succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "transition: first focus succeeds");
    expectString(harness, "log", "focus:first;focusin:first;",
        "transition: first acquisition");
    expect(harness.focusElement(secondSerial(harness), error),
        "transition: first to second succeeds");
    expectString(harness, "log",
        "focus:first;focusin:first;blur:first;focusout:first;"
        "focus:second;focusin:second;",
        "transition: A blur/focusout then B focus/focusin");
    expect(harness.focusElement(secondSerial(harness), error),
        "transition: same-element request succeeds");
    expectString(harness, "log",
        "focus:first;focusin:first;blur:first;focusout:first;"
        "focus:second;focusin:second;",
        "transition: same-element request is a no-op");
    expect(harness.clearFocus(error), "transition: clear succeeds");
    expectString(harness, "log",
        "focus:first;focusin:first;blur:first;focusout:first;"
        "focus:second;focusin:second;blur:second;focusout:second;",
        "transition: clear exposes blur");
}

void testOnceRemovalAndOrdering()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "listener semantics");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var focusOnce = 0;
var blurOnce = 0;
var removedFocus = 0;
var removedBlur = 0;
function onFocusOnce() { focusOnce = focusOnce + 1; }
function onBlurOnce() { blurOnce = blurOnce + 1; }
function onRemovedFocus() { removedFocus = removedFocus + 1; }
function onRemovedBlur() { removedBlur = removedBlur + 1; }
first.addEventListener("focus", onFocusOnce, { once: true });
first.addEventListener("blur", onBlurOnce, { once: true });
first.addEventListener("focus", onRemovedFocus);
first.removeEventListener("focus", onRemovedFocus);
first.addEventListener("blur", onRemovedBlur);
first.removeEventListener("blur", onRemovedBlur);
var order = "";
first.addEventListener("focus", function () { order = order + "a"; });
first.addEventListener("focus", function () { order = order + "b"; });
)JS");
    expect(result.succeeded(), "listener semantics: setup succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "listener semantics: first focus succeeds");
    expect(harness.clearFocus(error), "listener semantics: first blur succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "listener semantics: second focus succeeds");
    expect(harness.clearFocus(error), "listener semantics: second blur succeeds");
    expectNumber(harness, "focusOnce", 1, "listener semantics: once focus");
    expectNumber(harness, "blurOnce", 1, "listener semantics: once blur");
    expectNumber(harness, "removedFocus", 0,
        "listener semantics: removed focus");
    expectNumber(harness, "removedBlur", 0,
        "listener semantics: removed blur");
    expectString(harness, "order", "abab",
        "listener semantics: registration order on repeated focus");

    result = harness.execute(R"JS(
var captureOrder = "";
document.addEventListener("focus", function (event) {
    captureOrder = captureOrder + "document:" + event.eventPhase + ";";
}, true);
)JS");
    expect(result.succeeded(), "listener semantics: Boolean capture setup succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "listener semantics: Boolean capture focus succeeds");
    expectString(harness, "captureOrder", "document:1;",
        "listener semantics: Boolean capture shorthand");
}

void testPropagationControls()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "propagation");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var parent = document.getElementById("parent");
var log = "";
document.addEventListener("focus", function (event) {
    log = log + "stop;";
    event.stopPropagation();
}, true);
parent.addEventListener("focus", function () { log = log + "parent;"; }, true);
first.addEventListener("focus", function () { log = log + "target;"; });
)JS");
    expect(result.succeeded(), "propagation: stop setup succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "propagation: stop focus succeeds");
    expectString(harness, "log", "stop;",
        "propagation: stopPropagation ends capture path");

    RuntimeErrorCode secondError = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness immediate;
    loadFixture(immediate, secondError, "immediate propagation");
    result = immediate.execute(R"JS(
var first = document.getElementById("first");
var log = "";
document.addEventListener("focus", function (event) {
    log = log + "a";
    event.stopImmediatePropagation();
}, true);
document.addEventListener("focus", function () { log = log + "b"; }, true);
first.addEventListener("focus", function () { log = log + "target"; });
)JS");
    expect(result.succeeded(), "immediate propagation: setup succeeds");
    expect(immediate.focusElement(firstSerial(immediate), secondError),
        "immediate propagation: focus succeeds");
    expectString(immediate, "log", "a",
        "immediate propagation: later listeners are suppressed");
}

void testKeyboardRetargetAndEventLifetime()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "keyboard retarget");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var second = document.getElementById("second");
var keys = "";
function key(event) { keys = keys + event.target.id + ":" + event.type + ";"; }
document.addEventListener("keydown", key);
document.addEventListener("keyup", key);
var savedEvent = null;
var sameEventObject = false;
var eventLifecycle = "";
function lifecycle(event) {
    if (savedEvent !== null) sameEventObject = savedEvent === event;
    eventLifecycle = eventLifecycle + event.type + ":" + event.target.id + ":" +
        event.currentTarget.id + ";";
    savedEvent = event;
}
first.addEventListener("focus", lifecycle);
first.addEventListener("blur", lifecycle);
)JS");
    expect(result.succeeded(), "keyboard retarget: setup succeeds");
    expect(harness.focusElement(firstSerial(harness), error),
        "keyboard retarget: first focus succeeds");
    expect(harness.dispatchFocusedKeyboardEvent(65, true, false, error),
        "keyboard retarget: first keydown succeeds");
    expect(harness.dispatchFocusedKeyboardEvent(65, false, false, error),
        "keyboard retarget: first keyup succeeds");
    expect(harness.focusElement(secondSerial(harness), error),
        "keyboard retarget: second focus succeeds");
    expect(harness.dispatchFocusedKeyboardEvent(66, true, false, error),
        "keyboard retarget: second keydown succeeds");
    expect(harness.dispatchFocusedKeyboardEvent(66, false, false, error),
        "keyboard retarget: second keyup succeeds");
    expectString(harness, "keys",
        "first:keydown;first:keyup;second:keydown;second:keyup;",
        "keyboard retarget: new focus owns subsequent keys");
    expectString(harness, "eventLifecycle",
        "focus:first:first;blur:first:first;",
        "event lifecycle: target/currentTarget are refreshed");
    expectBoolean(harness, "sameEventObject", true,
        "event lifecycle: cached Event object is reused safely");
}

void testEventNamesAndCapacity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "names and capacity");
    ScriptResult result = harness.execute(
        "var first = document.getElementById(\"first\");"
        "function noop(event) {};");
    expect(result.succeeded(), "names and capacity: setup succeeds");
    const char* invalidNames[] = {
        "focu", "focused", "focusx", "blu", "blurred", "blurx",
        "focusinx", "focusoutx"
    };
    for (const char* name : invalidNames) {
        const std::string source = std::string("first.addEventListener(\"") + name +
            "\", noop);";
        expectError(harness.execute(source), RuntimeErrorCode::HostInvalidValue,
            std::string("names and capacity: exact rejection of ") + name);
    }
    expect(harness.hostAdapter().clickListenerCount() == 0u,
        "names and capacity: malformed names consume no slots");
    for (int index = 0; index < 64; ++index) {
        const std::string source = "function listener" + std::to_string(index) +
            "(event) {} document.addEventListener(\"" +
            (index % 2 == 0 ? "focus" : "blur") + "\", listener" +
            std::to_string(index) + ");";
        expect(harness.execute(source).succeeded(),
            "names and capacity: registration " + std::to_string(index + 1));
    }
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "names and capacity: shared 64-listener bound");
    expectError(harness.execute(
        "function overflow(event) {} document.addEventListener(\"focusin\", overflow);"),
        RuntimeErrorCode::HostCallbackLimitExceeded,
        "names and capacity: 65th registration rejected");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "names and capacity: overflow does not grow registry");
}

} // namespace

int main()
{
    testNonBubblingAndCapture();
    testBlurAndFocusOut();
    testTransitionsAndNoOp();
    testOnceRemovalAndOrdering();
    testPropagationControls();
    testKeyboardRetargetAndEventLifetime();
    testEventNamesAndCapacity();
    if (failures != 0) {
        std::cerr << failures << " JS24 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS24 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
