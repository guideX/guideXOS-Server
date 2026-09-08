#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::HostObjectReference;
using gxos::javascript::HostGenerationId;
using gxos::javascript::HostResultCode;
using gxos::javascript::HostValue;
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
<form id="form">
  <input id="a" type="text" value="alpha">
  <input id="b" type="text" value="bravo">
  <input id="c" type="text" value="charlie">
  <button id="button" type="button">Button</button>
  <button id="submit" type="submit">Submit</button>
  <button id="reset" type="reset">Reset</button>
</form>
<div id="plain">plain</div>
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

void expectString(const NavigatorScriptExecutionHarness& harness,
    const char* name, const std::string& expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::String, label + ": String");
    if (value->isString()) {
        const std::string actual = harness.runtime().stringValue(*value);
        expect(actual == expected, label + ": value (actual=" + actual +
            ", expected=" + expected + ")");
    }
}

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": error code");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const std::string& label)
{
    expect(harness.loadHtml("file:///js32.html", kFixture, error),
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

void testRelatedTargetAndDocumentFocus()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "relatedTarget");
    const ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var aAgain = document.getElementById("a");
var b = document.getElementById("b");
var form = document.getElementById("form");
var focusCount = 0;
var focusinCount = 0;
var blurCount = 0;
var focusoutCount = 0;
var focusInitialNull = false;
var focusinInitialNull = false;
var blurTransferTarget = false;
var focusoutTransferTarget = false;
var focusTransferTarget = false;
var focusinTransferTarget = false;
var blurClearNull = false;
var focusoutClearNull = false;
var focusStageActive = false;
var focusinStageActive = false;
var blurStageActive = false;
var focusoutStageActive = false;
var focusStageHasDocumentFocus = false;
var focusinStageHasDocumentFocus = false;
var blurStageHasDocumentFocus = false;
var focusoutStageHasDocumentFocus = false;
var readOnlyInitial = false;
var readOnlyTransfer = false;
var log = "";
a.addEventListener("focus", function(event) {
    focusCount = focusCount + 1;
    log = log + "focus-a;";
    focusInitialNull = event.relatedTarget === null;
    focusStageActive = document.activeElement === a;
    focusStageHasDocumentFocus = document.hasFocus();
    event.relatedTarget = b;
    readOnlyInitial = event.relatedTarget === null;
});
a.addEventListener("focusin", function(event) {
    focusinCount = focusinCount + 1;
    log = log + "focusin-a;";
    focusinInitialNull = event.relatedTarget === null;
    focusinStageActive = document.activeElement === a;
    focusinStageHasDocumentFocus = document.hasFocus();
});
a.addEventListener("blur", function(event) {
    blurCount = blurCount + 1;
    log = log + "blur-a;";
    blurTransferTarget = event.relatedTarget === b;
    blurStageActive = document.activeElement === a;
    blurStageHasDocumentFocus = document.hasFocus();
    event.relatedTarget = a;
    readOnlyTransfer = event.relatedTarget === b;
});
a.addEventListener("focusout", function(event) {
    focusoutCount = focusoutCount + 1;
    log = log + "focusout-a;";
    focusoutTransferTarget = event.relatedTarget === b;
    focusoutStageActive = document.activeElement === a;
    focusoutStageHasDocumentFocus = document.hasFocus();
});
b.addEventListener("focus", function(event) {
    focusCount = focusCount + 1;
    log = log + "focus-b;";
    focusTransferTarget = event.relatedTarget === a;
    focusStageActive = focusStageActive && document.activeElement === b;
    focusStageHasDocumentFocus = focusStageHasDocumentFocus && document.hasFocus();
});
b.addEventListener("focusin", function(event) {
    focusinCount = focusinCount + 1;
    log = log + "focusin-b;";
    focusinTransferTarget = event.relatedTarget === a;
    focusinStageActive = focusinStageActive && document.activeElement === b;
    focusinStageHasDocumentFocus = focusinStageHasDocumentFocus && document.hasFocus();
});
b.addEventListener("blur", function(event) {
    blurCount = blurCount + 1;
    log = log + "blur-b;";
    blurClearNull = event.relatedTarget === null;
    blurStageActive = blurStageActive && document.activeElement === b;
    blurStageHasDocumentFocus = blurStageHasDocumentFocus && document.hasFocus();
});
b.addEventListener("focusout", function(event) {
    focusoutCount = focusoutCount + 1;
    log = log + "focusout-b;";
    focusoutClearNull = event.relatedTarget === null;
    focusoutStageActive = focusoutStageActive && document.activeElement === b;
    focusoutStageHasDocumentFocus = focusoutStageHasDocumentFocus && document.hasFocus();
});
var initialHasFocus = document.hasFocus() === false;
var initialActiveNull = document.activeElement === null;
var identityStable = a === aAgain && document.getElementById("a") === a;
a.focus();
var afterInitialFocus = document.activeElement === a && document.hasFocus();
b.focus();
var afterTransfer = document.activeElement === b && document.hasFocus();
a.blur();
var unrelatedBlurNoop = document.activeElement === b;
b.blur();
var afterClear = document.activeElement === null && !document.hasFocus();
var noIncomingFocusOnClear = focusCount === 2 && focusinCount === 2;
var noPseudoTransfer = blurCount === 2 && focusoutCount === 2;
)JS");
    expect(result.succeeded(), "relatedTarget: observation script");
    expectBoolean(harness, "initialHasFocus", true,
        "hasFocus: deterministic initial false");
    expectBoolean(harness, "initialActiveNull", true,
        "activeElement: deterministic initial null");
    expectBoolean(harness, "identityStable", true,
        "relatedTarget: canonical identity path");
    expectBoolean(harness, "afterInitialFocus", true,
        "hasFocus: programmatic focus");
    expectBoolean(harness, "afterTransfer", true,
        "hasFocus: transfer owner");
    expectBoolean(harness, "afterClear", true,
        "hasFocus: programmatic clear");
    expectBoolean(harness, "focusInitialNull", true,
        "relatedTarget: initial focus is null");
    expectBoolean(harness, "focusinInitialNull", true,
        "relatedTarget: initial focusin is null");
    expectBoolean(harness, "blurTransferTarget", true,
        "relatedTarget: A blur points to B");
    expectBoolean(harness, "focusoutTransferTarget", true,
        "relatedTarget: A focusout points to B");
    expectBoolean(harness, "focusTransferTarget", true,
        "relatedTarget: B focus points to A");
    expectBoolean(harness, "focusinTransferTarget", true,
        "relatedTarget: B focusin points to A");
    expectBoolean(harness, "blurClearNull", true,
        "relatedTarget: clear blur is null");
    expectBoolean(harness, "focusoutClearNull", true,
        "relatedTarget: clear focusout is null");
    expectBoolean(harness, "focusStageActive", true,
        "stage: focus sees new active owner");
    expectBoolean(harness, "focusinStageActive", true,
        "stage: focusin sees new active owner");
    expectBoolean(harness, "blurStageActive", true,
        "stage: blur sees old active owner");
    expectBoolean(harness, "focusoutStageActive", true,
        "stage: focusout sees old active owner");
    expectBoolean(harness, "focusStageHasDocumentFocus", true,
        "stage: focus hasFocus true");
    expectBoolean(harness, "focusinStageHasDocumentFocus", true,
        "stage: focusin hasFocus true");
    expectBoolean(harness, "blurStageHasDocumentFocus", true,
        "stage: blur hasFocus true");
    expectBoolean(harness, "focusoutStageHasDocumentFocus", true,
        "stage: focusout hasFocus true");
    expectBoolean(harness, "readOnlyInitial", true,
        "relatedTarget: initial assignment is read-only");
    expectBoolean(harness, "readOnlyTransfer", true,
        "relatedTarget: transfer assignment is read-only");
    expectBoolean(harness, "unrelatedBlurNoop", true,
        "blur: unfocused receiver is a no-op");
    expectBoolean(harness, "noIncomingFocusOnClear", true,
        "clear: no incoming focus event");
    expectBoolean(harness, "noPseudoTransfer", true,
        "focus: two real loss transitions only");
    expectString(harness, "log",
        "focus-a;focusin-a;blur-a;focusout-a;focus-b;focusin-b;"
        "blur-b;focusout-b;",
        "relatedTarget: established event ordering");
}

void testNonFocusCacheHygieneAndPureHasFocus()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "cache hygiene");
    const std::uint64_t aSerial = serialById(harness, "a");
    const std::uint64_t formSerial = serialById(harness, "form");
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var b = document.getElementById("b");
var form = document.getElementById("form");
var clickNull = false;
var inputNull = false;
var changeNull = false;
var submitNull = false;
var resetNull = false;
var clickTarget = false;
var inputTarget = false;
var resetTarget = false;
a.addEventListener("click", function(event) {
    clickNull = event.relatedTarget === null;
    clickTarget = event.target === a;
});
a.addEventListener("input", function(event) {
    inputNull = event.relatedTarget === null;
    inputTarget = event.target === a;
});
a.addEventListener("change", function(event) {
    changeNull = event.relatedTarget === null;
});
form.addEventListener("submit", function(event) {
    submitNull = event.relatedTarget === null;
    event.preventDefault();
});
form.addEventListener("reset", function(event) {
    resetNull = event.relatedTarget === null;
    resetTarget = event.target === form;
});
)JS");
    expect(result.succeeded(), "cache hygiene: setup script");
    result = harness.execute(R"JS(
a.focus();
b.focus();
var beforeQueryActive = document.activeElement === b;
var queryOne = document.hasFocus();
var queryTwo = document.hasFocus();
var pureQuery = beforeQueryActive && queryOne && queryTwo &&
    document.activeElement === b;
)JS");
    expect(result.succeeded(), "cache hygiene: focus/query script");
    result = harness.execute("form.reset();");
    expect(result.succeeded(), "cache hygiene: reset script");
    result = harness.execute("a.focus();");
    expect(result.succeeded(), "cache hygiene: restore focus script");
    expect(harness.dispatchClick(aSerial, error),
        "cache hygiene: click dispatch");
    expect(error == RuntimeErrorCode::None,
        "cache hygiene: click dispatch has no error");
    expect(harness.dispatchFocusedUserEdit(65, false, error),
        "cache hygiene: input dispatch");
    expect(error == RuntimeErrorCode::None,
        "cache hygiene: input dispatch has no error");
    expect(harness.dispatchSubmit(formSerial, error),
        "cache hygiene: submit dispatch");
    expect(error == RuntimeErrorCode::None,
        "cache hygiene: submit dispatch has no error");
    result = harness.execute("b.focus(); var changedFocus = document.activeElement === b;");
    expect(result.succeeded(), "cache hygiene: change transition");
    expectBoolean(harness, "clickNull", true,
        "cache hygiene: click relatedTarget null");
    expectBoolean(harness, "inputNull", true,
        "cache hygiene: input relatedTarget null");
    expectBoolean(harness, "changeNull", true,
        "cache hygiene: change relatedTarget null");
    expectBoolean(harness, "submitNull", true,
        "cache hygiene: submit relatedTarget null");
    expectBoolean(harness, "resetNull", true,
        "cache hygiene: reset relatedTarget null");
    expectBoolean(harness, "clickTarget", true,
        "cache hygiene: click target unchanged");
    expectBoolean(harness, "inputTarget", true,
        "cache hygiene: input target unchanged");
    expectBoolean(harness, "resetTarget", true,
        "cache hygiene: reset target unchanged");
    expectBoolean(harness, "pureQuery", true,
        "hasFocus: repeated query is pure");
    expectBoolean(harness, "changedFocus", true,
        "cache hygiene: focus still works after non-focus events");
}

void testReentrantAndNestedEventLifetime()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "reentrant");
    const std::uint64_t aSerial = serialById(harness, "a");
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var b = document.getElementById("b");
var c = document.getElementById("c");
var redirect = true;
var outerFocusStable = false;
var redirectedFocusCorrect = false;
var nestedBefore = false;
var nestedAfter = false;
var nestedFinal = false;
a.addEventListener("focus", function(event) {
    if (!redirect) return;
    redirect = false;
    outerFocusStable = event.relatedTarget === null &&
        event.target === a && document.activeElement === a;
    b.focus();
    outerFocusStable = outerFocusStable && event.relatedTarget === null &&
        event.target === a && document.activeElement === a;
});
b.addEventListener("focus", function(event) {
    redirectedFocusCorrect = event.relatedTarget === a &&
        event.target === b && document.activeElement === b;
});
a.addEventListener("click", function(event) {
    nestedBefore = event.target === a && event.currentTarget === a &&
        event.eventPhase == 2 && event.relatedTarget === null;
    b.focus();
    c.focus();
    nestedAfter = event.target === a && event.currentTarget === a &&
        event.eventPhase == 2 && event.relatedTarget === null;
    nestedFinal = document.activeElement === a;
});
a.focus();
var redirectedOwner = document.activeElement === b;
)JS");
    expect(result.succeeded(), "reentrant: observation script");
    expectBoolean(harness, "outerFocusStable", true,
        "reentrant: current focus Event retains null relatedTarget");
    expectBoolean(harness, "redirectedFocusCorrect", true,
        "reentrant: redirected focus gets its own relatedTarget");
    expectBoolean(harness, "redirectedOwner", true,
        "reentrant: deferred focus request final owner");
    result = harness.execute("a.focus();");
    expect(result.succeeded(), "nested: establish outer target focus");
    expect(harness.dispatchClick(aSerial, error),
        "nested: click dispatch");
    expect(error == RuntimeErrorCode::None,
        "nested: click dispatch has no error");
    result = harness.execute(
        "var nestedDeferredOwner = document.activeElement === c;");
    expect(result.succeeded(), "nested: deferred focus observation");
    expectBoolean(harness, "nestedBefore", true,
        "nested: outer click metadata before nested dispatch");
    expectBoolean(harness, "nestedAfter", true,
        "nested: outer click metadata after nested dispatch");
    expectBoolean(harness, "nestedFinal", true,
        "nested: active owner during outer click");
    expectBoolean(harness, "nestedDeferredOwner", true,
        "nested: deferred focus final owner");

    NavigatorScriptExecutionHarness blurRedirect;
    loadFixture(blurRedirect, error, "blur redirect");
    result = blurRedirect.execute(R"JS(
var a = document.getElementById("a");
var b = document.getElementById("b");
var c = document.getElementById("c");
var blurStable = false;
var focusBRelatedA = false;
a.addEventListener("blur", function(event) {
    blurStable = event.relatedTarget === b && document.activeElement === a;
    c.focus();
    blurStable = blurStable && event.relatedTarget === b && document.activeElement === a;
});
b.addEventListener("focus", function(event) {
    focusBRelatedA = event.relatedTarget === a;
});
a.focus();
b.focus();
var finalC = document.activeElement === c;
)JS");
    expect(result.succeeded(), "blur redirect: observation script");
    expectBoolean(blurRedirect, "blurStable", true,
        "blur redirect: first Event keeps original B relatedTarget");
    expectBoolean(blurRedirect, "focusBRelatedA", true,
        "blur redirect: original B gain keeps A relatedTarget");
    expectBoolean(blurRedirect, "finalC", true,
        "blur redirect: deferred redirect final owner");
}

void testStaleLifecycleAndReceiverSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "stale related target");
    const std::uint64_t bSerial = serialById(harness, "b");
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("a");
var b = document.getElementById("b");
var staleResolvedNull = false;
b.addEventListener("focus", function(event) {
    staleResolvedNull = event.relatedTarget === null;
});
)JS");
    expect(result.succeeded(), "stale: listener setup");
    expect(harness.hostAdapter().dispatchFocusEvent(harness.runtime(), bSerial,
        true, false, 0xFFFFFFFFu, error),
        "stale: dispatch with dangling related serial");
    expect(error == RuntimeErrorCode::None,
        "stale: dangling related serial fails closed without dispatch error");
    expectBoolean(harness, "staleResolvedNull", true,
        "stale: dangling related serial becomes null");

    const HostGenerationId generation = harness.runtime().hostGeneration();
    const HostObjectReference staleDocument{
        gxos::javascript::kNavigatorDocumentHostInstance,
        generation == 1u ? 2u : generation - 1u,
        gxos::javascript::kNavigatorDocumentHostKind};
    HostValue hostResult;
    const auto staleCall = harness.hostAdapter().call(&staleDocument,
        gxos::javascript::kNavigatorHasFocusMethod, nullptr, 0u, hostResult);
    expect(staleCall.code == HostResultCode::StaleObject,
        "hasFocus: stale document receiver fails safely");

    expect(harness.replaceHtml("file:///js32-replacement.html",
        "<html><body><input id=next type=text value=next></body></html>",
        error), "lifecycle: replacement loads");
    expect(harness.relayout(), "lifecycle: replacement relayout");
    result = harness.execute(
        "var replacementActiveNull = document.activeElement === null;"
        "var replacementHasFocus = document.hasFocus() === false;");
    expect(result.succeeded(), "lifecycle: replacement query");
    expectBoolean(harness, "replacementActiveNull", true,
        "lifecycle: activeElement old owner does not leak");
    expectBoolean(harness, "replacementHasFocus", true,
        "lifecycle: hasFocus old owner does not leak");

}

void testListenerCapacityAndArgumentValidation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "capacity");
    for (int index = 0; index < 64; ++index) {
        const std::string source = "function listener" + std::to_string(index) +
            "(event) {} document.addEventListener(\"focus\", listener" +
            std::to_string(index) + ");";
        expect(harness.execute(source).succeeded(),
            "capacity: registration " + std::to_string(index + 1));
    }
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: global listener bound remains 64");
    expectError(harness.execute(
        "function overflow(event) {} document.addEventListener(\"focusin\", overflow);"),
        RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th listener remains rejected");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: overflow does not grow listener registry");

    NavigatorScriptExecutionHarness arguments;
    loadFixture(arguments, error, "arguments");
    expectError(arguments.execute("document.hasFocus(1);"),
        RuntimeErrorCode::HostInvalidValue,
        "hasFocus: nonzero argument rejected");
    expect(arguments.execute("var hasFocusBoolean = document.hasFocus() === false;").succeeded(),
        "hasFocus: zero-argument invocation");
    expectBoolean(arguments, "hasFocusBoolean", true,
        "hasFocus: boolean return value");
}

} // namespace

int main()
{
    testRelatedTargetAndDocumentFocus();
    testNonFocusCacheHygieneAndPureHasFocus();
    testReentrantAndNestedEventLifetime();
    testStaleLifecycleAndReceiverSafety();
    testListenerCapacityAndArgumentValidation();
    if (failures != 0) {
        std::cerr << failures << " JS32 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS32 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
