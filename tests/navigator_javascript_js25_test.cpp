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
<input id="third" type="text" value="">
<textarea id="area"></textarea>
<div id="not-focusable">plain</div>
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

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const std::string& label)
{
    expect(harness.loadHtml("file:///js25.html", kFixture, error),
        label + ": fixture loads");
    expect(harness.relayout(), label + ": relayout succeeds");
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

void testBasicFocusBlurAndNoOps()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "basic");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var second = document.getElementById("second");
var log = "";
first.addEventListener("focus", function(event) { log = log + event.type + ";"; });
first.addEventListener("focusin", function(event) { log = log + event.type + ";"; });
first.addEventListener("blur", function(event) { log = log + event.type + ";"; });
first.addEventListener("focusout", function(event) { log = log + event.type + ";"; });
first.focus();
)JS");
    expect(result.succeeded(), "basic: element.focus succeeds");
    expect(harness.focusedElementSerial() == serialById(harness, "first"),
        "basic: first is authoritative focus owner");
    expectString(harness, "log", "focus;focusin;",
        "basic: focus and focusin emitted");

    expect(harness.execute("first.focus();").succeeded(),
        "basic: repeated focus call succeeds");
    expectString(harness, "log", "focus;focusin;",
        "basic: repeated same-element focus is a no-op");
    expect(harness.execute("second.blur();").succeeded(),
        "basic: unfocused blur call succeeds");
    expect(harness.focusedElementSerial() == serialById(harness, "first"),
        "basic: unfocused blur leaves first focused");
    expect(harness.execute("first.blur();").succeeded(),
        "basic: element.blur succeeds");
    expect(harness.focusedElementSerial() == 0,
        "basic: blur clears authoritative focus");
    expectString(harness, "log", "focus;focusin;blur;focusout;",
        "basic: blur and focusout emitted");
}

void testPropagationMetadataAndOrdering()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "metadata");
    ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var first = document.getElementById("first");
var second = document.getElementById("second");
var focusLog = "";
var focusinLog = "";
var blurLog = "";
var focusoutLog = "";
document.addEventListener("focus", function(event) {
    focusLog = focusLog + "d:" + event.target.id + ":" +
        (event.currentTarget === document) + ":" + event.eventPhase + ":" +
        event.bubbles + ":" + event.cancelable + ";";
}, true);
document.addEventListener("focus", function(event) { focusLog = focusLog + "db;"; });
parent.addEventListener("focus", function(event) {
    focusLog = focusLog + "p:" + event.target.id + ":" +
        (event.currentTarget === parent) + ":" + event.eventPhase + ";";
}, true);
parent.addEventListener("focus", function(event) { focusLog = focusLog + "pb;"; });
first.addEventListener("focus", function(event) {
    focusLog = focusLog + "t:" + event.target.id + ":" +
        (event.currentTarget === first) + ":" + event.eventPhase + ";";
});
second.addEventListener("focus", function(event) {
    focusLog = focusLog + "t:" + event.target.id + ":" +
        (event.currentTarget === second) + ":" + event.eventPhase + ";";
});
document.addEventListener("focusin", function(event) {
    focusinLog = focusinLog + "d1;";
}, true);
parent.addEventListener("focusin", function(event) { focusinLog = focusinLog + "p1;"; }, true);
first.addEventListener("focusin", function(event) { focusinLog = focusinLog + "t2;"; });
second.addEventListener("focusin", function(event) { focusinLog = focusinLog + "t2;"; });
parent.addEventListener("focusin", function(event) { focusinLog = focusinLog + "p3;"; });
document.addEventListener("focusin", function(event) { focusinLog = focusinLog + "d3;"; });
document.addEventListener("blur", function(event) { blurLog = blurLog + "d1;"; }, true);
parent.addEventListener("blur", function(event) { blurLog = blurLog + "p1;"; }, true);
first.addEventListener("blur", function(event) { blurLog = blurLog + "t2;"; });
document.addEventListener("blur", function(event) { blurLog = blurLog + "db;"; });
parent.addEventListener("blur", function(event) { blurLog = blurLog + "pb;"; });
document.addEventListener("focusout", function(event) { focusoutLog = focusoutLog + "d1;"; }, true);
parent.addEventListener("focusout", function(event) { focusoutLog = focusoutLog + "p1;"; }, true);
first.addEventListener("focusout", function(event) { focusoutLog = focusoutLog + "t2;"; });
parent.addEventListener("focusout", function(event) { focusoutLog = focusoutLog + "p3;"; });
document.addEventListener("focusout", function(event) { focusoutLog = focusoutLog + "d3;"; });
first.focus();
)JS");
    expect(result.succeeded(), "metadata: listener setup and focus succeed");
    expectString(harness, "focusLog",
        "d:first:true:1:false:false;p:first:true:1;t:first:true:2;",
        "metadata: focus capture/target and non-bubbling metadata");
    expectString(harness, "focusinLog", "d1;p1;t2;p3;d3;",
        "metadata: focusin capture/target/bubble");
    expectString(harness, "blurLog", "", "metadata: no blur during initial focus");
    expectString(harness, "focusoutLog", "", "metadata: no focusout during initial focus");

    result = harness.execute("first.blur();");
    expect(result.succeeded(), "metadata: clear focus succeeds");
    expectString(harness, "blurLog", "d1;p1;t2;",
        "metadata: blur capture/target and non-bubbling");
    expectString(harness, "focusoutLog", "d1;p1;t2;p3;d3;",
        "metadata: focusout capture/target/bubble");

    result = harness.execute(R"JS(
focusLog = ""; focusinLog = ""; blurLog = ""; focusoutLog = "";
first.focus();
focusLog = focusLog + "|"; focusinLog = focusinLog + "|";
second.focus();
)JS");
    expect(result.succeeded(), "ordering: A-to-B focus succeeds");
    expectString(harness, "blurLog", "d1;p1;t2;",
        "ordering: A blur occurs before B focus");
    expectString(harness, "focusoutLog", "d1;p1;t2;p3;d3;",
        "ordering: A focusout occurs before B focus");
    expectString(harness, "focusLog",
        "d:first:true:1:false:false;p:first:true:1;t:first:true:2;|"
        "d:second:true:1:false:false;p:second:true:1;t:second:true:2;",
        "ordering: B focus follows A loss");
    expectString(harness, "focusinLog", "d1;p1;t2;p3;d3;|d1;p1;t2;p3;d3;",
        "ordering: B focusin follows B focus");

    result = harness.execute("second.blur();");
    expect(result.succeeded(), "ordering: clear B succeeds");
    expect(harness.focusedElementSerial() == 0,
        "ordering: clear leaves no authoritative owner");
}

void testListenerSemanticsAndCancellation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "listeners");
    ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var first = document.getElementById("first");
var onceCount = 0;
var removedCount = 0;
var captureCount = 0;
var stopLog = "";
var immediateLog = "";
function onceHandler(event) { onceCount = onceCount + 1; }
function removedHandler(event) { removedCount = removedCount + 1; }
first.addEventListener("focusin", onceHandler, { once: true });
first.addEventListener("focusin", removedHandler);
first.removeEventListener("focusin", removedHandler);
parent.addEventListener("focus", function(event) { captureCount = captureCount + 1; }, true);
first.addEventListener("focusin", function(event) { stopLog = stopLog + "a"; event.stopPropagation(); });
first.addEventListener("focusin", function(event) { stopLog = stopLog + "b"; });
parent.addEventListener("focusin", function(event) { stopLog = stopLog + "p"; });
first.addEventListener("focus", function(event) { immediateLog = immediateLog + "a"; event.stopImmediatePropagation(); });
first.addEventListener("focus", function(event) { immediateLog = immediateLog + "b"; });
first.focus();
first.blur();
first.focus();
)JS");
    expect(result.succeeded(), "listeners: setup and focus calls succeed");
    expectString(harness, "stopLog", "abab",
        "listeners: stopPropagation keeps same-target listeners");
    expectString(harness, "immediateLog", "aa",
        "listeners: stopImmediatePropagation stops immediately");
    expectNumber(harness, "onceCount", 1, "listeners: once");
    expectNumber(harness, "removedCount", 0, "listeners: removeEventListener");
    expectNumber(harness, "captureCount", 2, "listeners: Boolean capture shorthand");

    result = harness.execute(R"JS(
var cancelled = false;
first.addEventListener("focus", function(event) {
    cancelled = event.cancelable;
    event.preventDefault();
});
first.blur();
first.focus();
)JS");
    expect(result.succeeded(), "cancellation: preventDefault setup succeeds");
    expectBoolean(harness, "cancelled", false,
        "cancellation: focus is non-cancelable");
    expect(harness.focusedElementSerial() != 0,
        "cancellation: preventDefault does not block focus");
}

void testReceiverFocusabilityAndKeyboardTarget()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "receiver and keyboard");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var second = document.getElementById("second");
var plain = document.getElementById("not-focusable");
var keys = "";
first.addEventListener("keydown", function(event) { keys = keys + event.target.id + ":" + event.key + ";"; });
second.addEventListener("keydown", function(event) { keys = keys + event.target.id + ":" + event.key + ";"; });
first.focus();
)JS");
    expect(result.succeeded(), "receiver: setup and first focus succeed");
    expect(harness.dispatchFocusedKeyboardEvent(65, true, false, error),
        "keyboard: focused keydown dispatches");
    expectString(harness, "keys", "first:a;",
        "keyboard: JS23 target follows programmatic focus");
    expect(harness.execute("plain.focus(); plain.blur();").succeeded(),
        "receiver: non-focusable focus/blur are safe");
    expect(harness.focusedElementSerial() == serialById(harness, "first"),
        "receiver: non-focusable calls do not corrupt focus");
    expect(harness.execute("first.blur();").succeeded(),
        "keyboard: programmatic blur succeeds");
    expect(harness.dispatchFocusedKeyboardEvent(66, true, false, error),
        "keyboard: no-focus fallback dispatches");
    expectString(harness, "keys", "first:a;",
        "keyboard: blur removes JS23 element targeting");
    expect(harness.execute("second.focus();").succeeded(),
        "receiver: second receiver focuses second");
    expect(harness.focusedElementSerial() == serialById(harness, "second"),
        "receiver: independent receiver identity is preserved");
    expectError(harness.execute("second.focus(1);"),
        RuntimeErrorCode::HostInvalidValue,
        "receiver: focus arguments are rejected by host convention");
}

void testReentrantFocusAndBoundedLifetime()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "reentrant");
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var second = document.getElementById("second");
var third = document.getElementById("third");
var log = "";
var metadataStable = false;
first.addEventListener("focus", function(event) {
    var before = event.target.id;
    second.focus();
    metadataStable = before === event.target.id;
    log = log + "first-focus;";
});
first.addEventListener("focusin", function(event) { log = log + "first-focusin;"; });
first.addEventListener("blur", function(event) { log = log + "first-blur;"; });
first.addEventListener("focusout", function(event) { log = log + "first-focusout;"; });
second.addEventListener("focus", function(event) { log = log + "second-focus;"; });
second.addEventListener("focusin", function(event) { log = log + "second-focusin;"; });
first.focus();
)JS");
    expect(result.succeeded(), "reentrant: focus callback redirect succeeds");
    expect(harness.focusedElementSerial() == serialById(harness, "second"),
        "reentrant: final focus owner is deterministic second");
    expectString(harness, "log",
        "first-focus;first-focusin;first-blur;first-focusout;"
        "second-focus;second-focusin;",
        "reentrant: redirect is processed after the outer transition");
    expectBoolean(harness, "metadataStable", true,
        "reentrant: outer Event metadata is restored/stable");

    NavigatorScriptExecutionHarness redirect;
    loadFixture(redirect, error, "reentrant blur redirect");
    result = redirect.execute(R"JS(
var first = document.getElementById("first");
var second = document.getElementById("second");
var third = document.getElementById("third");
var log = "";
first.addEventListener("blur", function(event) { log = log + "first-blur;"; third.focus(); });
first.addEventListener("focusout", function(event) { log = log + "first-focusout;"; });
second.addEventListener("focus", function(event) { log = log + "second-focus;"; });
second.addEventListener("focusin", function(event) { log = log + "second-focusin;"; });
first.focus();
log = "";
second.focus();
)JS");
    expect(result.succeeded(), "reentrant: blur callback redirect succeeds");
    expect(redirect.focusedElementSerial() == serialById(redirect, "third"),
        "reentrant: blur redirect leaves third focused");
    expectString(redirect, "log", "first-blur;first-focusout;second-focus;second-focusin;",
        "reentrant: redirect does not corrupt outer event metadata");

    NavigatorScriptExecutionHarness bounded;
    loadFixture(bounded, error, "bounded lifetime");
    expect(bounded.execute(
        "var first = document.getElementById(\"first\");"
        "first.addEventListener(\"focus\", function(event) {});").succeeded(),
        "bounded: listener setup succeeds");
    expect(bounded.execute("first.focus(); first.blur();").succeeded(),
        "bounded: warm-up focus/blur succeeds");
    const std::size_t objects = bounded.runtime().objectCount();
    const std::size_t strings = bounded.runtime().runtimeStringValueCount();
    const std::size_t listeners = bounded.hostAdapter().clickListenerCount();
    for (int index = 0; index < 24; ++index)
        expect(bounded.execute("first.focus(); first.blur();").succeeded(),
            "bounded: repeated focus/blur " + std::to_string(index + 1));
    expect(bounded.runtime().objectCount() == objects,
        "bounded: focus calls do not grow Event objects");
    expect(bounded.runtime().runtimeStringValueCount() == strings,
        "bounded: focus calls do not grow cached strings");
    expect(bounded.hostAdapter().clickListenerCount() == listeners,
        "bounded: focus calls do not grow listener registry");
}

void testStaleReceiverAndCapacity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "stale and capacity");
    ScriptResult result = harness.execute(
        "var first = document.getElementById(\"first\");"
        "first.focus();");
    expect(result.succeeded(), "stale: initial receiver succeeds");
    expect(harness.invalidateDocumentGeneration(error),
        "stale: generation invalidation succeeds");
    expectError(harness.execute("first.focus();"),
        RuntimeErrorCode::StaleHostObject,
        "stale: old element focus fails safely");

    NavigatorScriptExecutionHarness capacity;
    loadFixture(capacity, error, "capacity");
    for (int index = 0; index < 64; ++index) {
        const std::string source = "function listener" + std::to_string(index) +
            "(event) {} document.addEventListener(\"focus\", listener" +
            std::to_string(index) + ");";
        expect(capacity.execute(source).succeeded(),
            "capacity: registration " + std::to_string(index + 1));
    }
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: listener registry remains 64");
    expect(capacity.execute(
        "function overflow(event) {} document.addEventListener(\"focusin\", overflow);").runtimeError.code ==
        RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th listener remains rejected");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: overflow does not grow listener registry");
}

} // namespace

int main()
{
    testBasicFocusBlurAndNoOps();
    testPropagationMetadataAndOrdering();
    testListenerSemanticsAndCancellation();
    testReceiverFocusabilityAndKeyboardTarget();
    testReentrantFocusAndBoundedLifetime();
    testStaleReceiverAndCapacity();
    if (failures != 0) {
        std::cerr << failures << " JS25 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS25 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
