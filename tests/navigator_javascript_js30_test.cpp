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
<html><body><form id="formA">
<input id="a" type="text" value="alpha">
<input id="b" type="text" value="bravo">
<textarea id="c">charlie</textarea>
<input id="d" type="checkbox" name="d" value="yes">
<input id="e" type="radio" name="mode" value="e">
<button id="f" type="button">Plain button</button>
<select id="g" name="mode-select"><option value="one" selected>One</option><option value="two">Two</option></select>
<button id="submit" type="submit">Submit</button>
<div id="plain">Plain non-focusable element</div>
</form></body></html>
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
    if (value->isBoolean()) expect(value->booleanValue() == expected,
        label + ": value");
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
    expect(harness.loadHtml("file:///js30.html", kFixture, error),
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

void testBasicProjectionIdentityReadOnlyAndNoSideEffects()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var a2 = document.getElementById("a");
var b = document.getElementById("b");
var plain = document.getElementById("plain");
var initialNull = document.activeElement === null;
var repeatedLookup = a === a2;
var differentElements = a !== b;
var focusEvents = 0;
a.addEventListener("focus", function() { focusEvents = focusEvents + 1; });
a.focus();
var activeA = document.activeElement === a;
var activeA2 = document.getElementById("a") === document.activeElement;
var activeRepeated = a2 === document.activeElement;
var beforeReadEvents = focusEvents;
var readValue = document.activeElement;
var afterReadEvents = focusEvents;
var noReadSideEffects = beforeReadEvents === afterReadEvents && readValue === a;
var nonFocusableBefore = document.activeElement === a;
plain.focus();
var nonFocusableUnchanged = nonFocusableBefore && document.activeElement === a;
)JS");
    expect(result.succeeded(), "basic: projection script");
    expectBoolean(harness, "initialNull", true, "basic: initial null fallback");
    expectBoolean(harness, "repeatedLookup", true, "identity: repeated lookup");
    expectBoolean(harness, "differentElements", true, "identity: different elements");
    expectBoolean(harness, "activeA", true, "basic: active A");
    expectBoolean(harness, "activeA2", true, "identity: lookup equals active");
    expectBoolean(harness, "activeRepeated", true, "identity: independent reference equals active");
    expectBoolean(harness, "noReadSideEffects", true, "basic: read has no side effects");
    expectBoolean(harness, "nonFocusableUnchanged", true, "focusability: non-focusable no-op");

    result = harness.execute("document.activeElement = b;");
    expectError(result, RuntimeErrorCode::HostPropertyReadOnly,
        "read-only: assignment rejected");
    expect(harness.focusedElementSerial() == serialById(harness, "a"),
        "read-only: assignment cannot move native focus");
}

void testProgrammaticTransitionsAndEventObservation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "events");
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var b = document.getElementById("b");
var log = "";
function record(name, value) { log = log + name + ":" + value + ";"; }
a.addEventListener("blur", function() { record("blur-a", document.activeElement === a); });
a.addEventListener("focusout", function() { record("focusout-a", document.activeElement === a); });
a.addEventListener("change", function() { record("change-a", document.activeElement === a); });
b.addEventListener("focus", function() { record("focus-b", document.activeElement === b); });
b.addEventListener("focusin", function() { record("focusin-b", document.activeElement === b); });
a.focus();
a.value = "edited";
b.focus();
var transferLog = log;
a.focus();
a.blur();
var afterBlurNull = document.activeElement === null;
b.blur();
var unrelatedBlurLeavesNull = document.activeElement === null;
)JS");
    expect(result.succeeded(), "events: transition script");
    expectString(harness, "transferLog",
        "blur-a:true;focusout-a:true;change-a:true;focus-b:true;focusin-b:true;",
        "events: activeElement ordering");
    expectBoolean(harness, "afterBlurNull", true, "blur: clears active element");
    expectBoolean(harness, "unrelatedBlurLeavesNull", true,
        "blur: unrelated element is a no-op");

    result = harness.execute(R"JS(
var a2 = document.getElementById("a");
var b2 = document.getElementById("b");
a2.focus();
var beforeSameFocus = document.activeElement === a2;
a2.focus();
var afterSameFocus = document.activeElement === a2;
b2.blur();
var blurUnfocusedLeavesA = document.activeElement === a2;
)JS");
    expect(result.succeeded(), "events: same-element script");
    expectBoolean(harness, "beforeSameFocus", true, "same focus: initial owner");
    expectBoolean(harness, "afterSameFocus", true, "same focus: strict no-op");
    expectBoolean(harness, "blurUnfocusedLeavesA", true,
        "blur: unfocused receiver leaves A");
}

void testInputAndFormControlProjection()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "controls");
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var c = document.getElementById("c");
var d = document.getElementById("d");
var e = document.getElementById("e");
var f = document.getElementById("f");
var g = document.getElementById("g");
var submit = document.getElementById("submit");
var form = document.getElementById("formA");
var inputSeen = false;
a.addEventListener("input", function(event) {
    inputSeen = document.activeElement === a && event.target === a &&
        event.currentTarget === a;
});
a.focus();
)JS");
    expect(result.succeeded(), "controls: setup");
    expect(harness.dispatchFocusedUserEdit(65, false, error),
        "controls: native edit dispatch");
    expect(error == RuntimeErrorCode::None, "controls: edit no error");
    expectBoolean(harness, "inputSeen", true,
        "input: active element and event metadata");

    result = harness.execute(R"JS(
c.focus(); var textareaActive = document.activeElement === c;
d.focus(); var checkboxActive = document.activeElement === d;
e.focus(); var radioActive = document.activeElement === e;
f.focus(); var buttonActive = document.activeElement === f;
g.focus(); var selectActive = document.activeElement === g;
var submitLog = "";
form.addEventListener("submit", function(event) {
    submitLog = (document.activeElement === submit) + ":" + event.defaultPrevented;
    event.preventDefault();
});
submit.focus();
submit.click();
var canceledSubmitKeepsFocus = document.activeElement === submit;
)JS");
    expect(result.succeeded(), "controls: control matrix");
    expectBoolean(harness, "textareaActive", true, "controls: textarea");
    expectBoolean(harness, "checkboxActive", true, "controls: checkbox");
    expectBoolean(harness, "radioActive", true, "controls: radio");
    expectBoolean(harness, "buttonActive", true, "controls: button");
    expectBoolean(harness, "selectActive", true, "controls: select");
    expectString(harness, "submitLog", "true:false",
        "submit: activeElement remains submit during event");
    expectBoolean(harness, "canceledSubmitKeepsFocus", true,
        "submit: cancellation preserves focus");
}

void testReentrantFocusProjectionAndNestedClick()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "reentrant");
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var b = document.getElementById("b");
var c = document.getElementById("c");
var focusInsideA = false;
var redirectFromFocus = true;
a.addEventListener("focus", function() {
    focusInsideA = document.activeElement === a;
    if (redirectFromFocus) b.focus();
});
a.focus();
var redirectedToB = document.activeElement === b;
redirectFromFocus = false;
var blurInsideA = false;
a.addEventListener("blur", function() {
    blurInsideA = document.activeElement === a;
    c.focus();
});
b.focus();
b.blur();
a.focus();
b.focus();
var blurRedirectedToC = document.activeElement === c;
var nestedMetadata = false;
var nestedActiveWasC = false;
a.addEventListener("click", function(event) {
    nestedMetadata = event.target === a && event.currentTarget === a &&
        event.eventPhase == 2;
    nestedActiveWasC = document.activeElement === c;
    b.focus();
    c.click();
    nestedMetadata = nestedMetadata && event.target === a &&
        event.currentTarget === a && event.eventPhase == 2;
});
a.click();
var nestedClickFinalB = document.activeElement === b;
)JS");
    expect(result.succeeded(), "reentrant: transition script");
    expectBoolean(harness, "focusInsideA", true,
        "reentrant focus: deferred redirect still sees A");
    expectBoolean(harness, "redirectedToB", true,
        "reentrant focus: final owner B");
    expectBoolean(harness, "blurInsideA", true,
        "reentrant blur: listener sees A before clear");
    expectBoolean(harness, "blurRedirectedToC", true,
        "reentrant blur: final owner C");
    expectBoolean(harness, "nestedActiveWasC", true,
        "nested click: projection sees current authoritative owner");
    expectBoolean(harness, "nestedMetadata", true,
        "nested click: event metadata survives activeElement reads");
    expectBoolean(harness, "nestedClickFinalB", true,
        "nested click: deferred focus final owner B");
}

void testLifecycleAndStaleFocusSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "lifecycle");
    ScriptResult result = harness.execute(
        "var oldA = document.getElementById(\"a\"); oldA.focus();");
    expect(result.succeeded(), "lifecycle: old focus");
    expect(harness.replaceHtml("file:///js30-replacement.html",
        "<html><body><input id=next type=text value=next></body></html>", error),
        "lifecycle: replacement loads");
    expect(harness.relayout(), "lifecycle: replacement relayout");
    result = harness.execute(
        "var replacementStartsNull = document.activeElement === null;");
    expect(result.succeeded(), "lifecycle: replacement projection");
    expectBoolean(harness, "replacementStartsNull", true,
        "lifecycle: old focus does not leak to new document");

    NavigatorScriptExecutionHarness stale;
    loadFixture(stale, error, "stale");
    stale.document().formRuntimeState.focusValid = true;
    stale.document().formRuntimeState.focusedLogicalSerial = 0xFFFFFFFFu;
    stale.document().formRuntimeState.focusedDocumentGeneration =
        stale.document().formRuntimeState.documentGeneration;
    result = stale.execute("var staleProjection = document.activeElement === null;");
    expect(result.succeeded(), "stale: dangling serial query");
    expectBoolean(stale, "staleProjection", true,
        "stale: dangling serial fails closed to null");
}

void testKeyboardFocusProjection()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "keyboard");
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var b = document.getElementById("b");
var focusLog = "";
b.addEventListener("focus", function() {
    focusLog = focusLog + (document.activeElement === b);
});
a.focus();
)JS");
    expect(result.succeeded(), "keyboard: setup");
    expect(harness.dispatchFocusedKeyboardEvent(9, true, false, error),
        "keyboard: Tab down dispatch");
    expect(harness.dispatchFocusedKeyboardEvent(9, false, false, error),
        "keyboard: Tab up dispatch");
    // The harness dispatches keyboard events through the adapter; the native
    // Navigator Tab traversal itself is covered by the hosted smoke path.
    expectString(harness, "focusLog", "",
        "keyboard: adapter event path does not invent traversal");
    expect(harness.focusedElementSerial() == serialById(harness, "a"),
        "keyboard: unsupported harness traversal leaves authoritative owner");
}

} // namespace

int main()
{
    testBasicProjectionIdentityReadOnlyAndNoSideEffects();
    testProgrammaticTransitionsAndEventObservation();
    testInputAndFormControlProjection();
    testReentrantFocusProjectionAndNestedClick();
    testLifecycleAndStaleFocusSafety();
    testKeyboardFocusProjection();
    if (failures != 0) {
        std::cerr << failures << " JS30 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS30 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
