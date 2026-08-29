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
using gxos::javascript::kEventPhaseAtTarget;
using gxos::javascript::kEventPhaseBubbling;
using gxos::javascript::kEventPhaseCapturing;
using gxos::javascript::kEventPhaseNone;
using gxos::javascript::kNavigatorScriptMaxPropagationDepth;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<div id="root"><div id="parent">
<button id="child" type="button">Child</button>
<button id="stop" type="button">Stop</button>
<button id="immediate" type="button">Immediate</button>
<button id="target-stop" type="button">Target stop</button>
<button id="target-immediate" type="button">Target immediate</button>
<button id="mutation" type="button">Mutation</button>
<button id="error" type="button">Error</button>
<button id="stop-error" type="button">Stop error</button>
<a id="cancel" href="file:///cancelled.html">Cancel</a>
</div></div>
<div id="tree-a"><button id="a-child" type="button">A</button></div>
<div id="tree-b"><button id="b-child" type="button">B</button></div>
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
    expect(harness.loadHtml("file:///js22.html", kFixture, error),
        label + ": fixture loads");
    expect(harness.relayout(), label + ": relayout succeeds");
}

bool click(NavigatorScriptExecutionHarness& harness, const std::string& id,
    RuntimeErrorCode& error, bool* prevented, const std::string& label)
{
    const std::uint64_t serial = serialById(harness, id);
    expect(serial != 0, label + ": serial exists");
    if (serial == 0) return false;
    const bool dispatched = harness.dispatchClick(serial, error, prevented);
    if (harness.documentDirty()) expect(harness.relayout(),
        label + ": relayout succeeds");
    return dispatched;
}

void testBooleanMappingOrderingAndDuplicates()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "mapping");
    const ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var child = document.getElementById("child");
var samePhases = "";
var captureOrder = "";
var bubbleOrder = "";
var targetOrder = "";
var targetTruePhase = 0;
var targetFalsePhase = 0;
var onclickPhase = 0;
function same(event) { samePhases = samePhases + event.eventPhase; }
function duplicateBubble(event) { bubbleOrder = bubbleOrder + "d"; }
function bubbleA(event) { bubbleOrder = bubbleOrder + "a"; }
function bubbleB(event) { bubbleOrder = bubbleOrder + "b"; }
function bubbleC(event) { bubbleOrder = bubbleOrder + "c"; }
root.addEventListener("click", same, true);
root.addEventListener("click", same, { capture: true });
root.addEventListener("click", same, false);
root.addEventListener("click", same);
root.addEventListener("click", function(event) { captureOrder = captureOrder + "a"; }, true);
root.addEventListener("click", function(event) { captureOrder = captureOrder + "b"; }, { capture: true });
root.addEventListener("click", function(event) { captureOrder = captureOrder + "c"; }, true);
root.addEventListener("click", duplicateBubble, false);
root.addEventListener("click", duplicateBubble, { capture: false });
root.addEventListener("click", duplicateBubble);
root.addEventListener("click", bubbleA, false);
root.addEventListener("click", bubbleB, { capture: false });
root.addEventListener("click", bubbleC, false);
child.addEventListener("click", function(event) {
    targetOrder = targetOrder + "c"; targetTruePhase = event.eventPhase;
}, true);
child.onclick = function(event) {
    targetOrder = targetOrder + "o"; onclickPhase = event.eventPhase;
};
child.addEventListener("click", function(event) {
    targetOrder = targetOrder + "b"; targetFalsePhase = event.eventPhase;
}, false);
)JS");
    expect(result.succeeded(), "mapping: Boolean and object registrations succeed");
    expect(harness.hostAdapter().clickListenerCount() == 11u,
        "mapping: duplicate syntax consumes one slot per semantic listener");
    bool prevented = false;
    expect(click(harness, "child", error, &prevented,
        "mapping: descendant click"), "mapping: click dispatch succeeds");
    expectString(harness, "samePhases", "13",
        "mapping: same callback has capture then bubble phases");
    expectString(harness, "captureOrder", "abc",
        "mapping: Boolean/object capture order is registration order");
    expectString(harness, "bubbleOrder", "dabc",
        "mapping: multiple mixed-syntax bubble listeners retain order");
    expectString(harness, "targetOrder", "cob",
        "mapping: target Boolean capture, onclick, Boolean bubble ordering");
    expectNumber(harness, "targetTruePhase", kEventPhaseAtTarget,
        "mapping: target true uses AT_TARGET");
    expectNumber(harness, "targetFalsePhase", kEventPhaseAtTarget,
        "mapping: target false uses AT_TARGET");
    expectNumber(harness, "onclickPhase", kEventPhaseAtTarget,
        "mapping: onclick remains AT_TARGET");
}

void testBooleanRemovalAndOnce()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "removal");
    const ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var child = document.getElementById("child");
var log = "";
function shared(event) { log = log + event.eventPhase; }
parent.addEventListener("click", shared, true);
parent.addEventListener("click", shared, false);
parent.removeEventListener("click", shared, true);
)JS");
    expect(result.succeeded(), "removal: initial Boolean registrations succeed");
    bool prevented = false;
    expect(click(harness, "child", error, &prevented,
        "removal: Boolean capture removed"),
        "removal: bubble survivor dispatches");
    expectString(harness, "log", "3",
        "removal: true removes only capture registration");
    const ScriptResult second = harness.execute(
        "parent.removeEventListener(\"click\", shared, false);");
    expect(second.succeeded(), "removal: false removal succeeds");
    expect(click(harness, "child", error, &prevented,
        "removal: Boolean bubble removed"),
        "removal: empty dispatch succeeds");
    expectString(harness, "log", "3",
        "removal: false removes only bubble registration");

    const ScriptResult cross = harness.execute(R"JS(
var crossBoolean = 0;
var crossObject = 0;
function crossCapture(event) { crossBoolean = crossBoolean + 1; }
function crossBubble(event) { crossObject = crossObject + 1; }
parent.addEventListener("click", crossCapture, true);
parent.removeEventListener("click", crossCapture, { capture: true });
parent.addEventListener("click", crossBubble, { capture: false });
parent.removeEventListener("click", crossBubble, false);
parent.addEventListener("click", crossCapture, { capture: true });
parent.removeEventListener("click", crossCapture, true);
parent.addEventListener("click", crossBubble, false);
parent.removeEventListener("click", crossBubble, { capture: false });
)JS");
    expect(cross.succeeded(), "removal: cross-form removal succeeds");
    expect(click(harness, "child", error, &prevented,
        "removal: cross-form empty dispatch"),
        "removal: cross-form dispatch succeeds");
    expectNumber(harness, "crossBoolean", 0.0,
        "removal: object removal removes Boolean registration");
    expectNumber(harness, "crossObject", 0.0,
        "removal: Boolean removal removes object registration");
    expect(harness.hostAdapter().clickListenerCount() == 0u,
        "removal: all cross-form registrations are gone");

    const ScriptResult once = harness.execute(R"JS(
var onceCount = 0;
function onceHandler(event) { onceCount = onceCount + 1; }
parent.addEventListener("click", onceHandler, { capture: true, once: true });
parent.removeEventListener("click", onceHandler, true);
parent.addEventListener("click", onceHandler, { capture: true, once: true });
)JS");
    expect(once.succeeded(), "removal: object once setup succeeds");
    expect(click(harness, "child", error, &prevented,
        "removal: once capture first"), "removal: once first dispatch succeeds");
    expect(click(harness, "child", error, &prevented,
        "removal: once capture second"), "removal: once second dispatch succeeds");
    expectNumber(harness, "onceCount", 1.0,
        "removal: object capture+once remains once");

    const ScriptResult booleanOnce = harness.execute(R"JS(
var booleanCount = 0;
function booleanHandler(event) { booleanCount = booleanCount + 1; }
parent.addEventListener("click", booleanHandler, true);
)JS");
    expect(booleanOnce.succeeded(), "removal: Boolean once-only regression setup");
    expect(click(harness, "child", error, &prevented,
        "removal: Boolean persistent first"),
        "removal: Boolean persistent first succeeds");
    expect(click(harness, "child", error, &prevented,
        "removal: Boolean persistent second"),
        "removal: Boolean persistent second succeeds");
    expectNumber(harness, "booleanCount", 2.0,
        "removal: Boolean shorthand leaves once false");
}

void testValidationAndControls()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "validation");
    ScriptResult result = harness.execute(
        "var child = document.getElementById(\"child\");"
        "function handler(event) {}"
        "child.addEventListener(\"click\", handler, 1);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: number third argument remains invalid");
    result = harness.execute(
        "child.addEventListener(\"click\", handler, \"capture\");");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: string third argument remains invalid");
    result = harness.execute(
        "child.addEventListener(\"click\", handler, null);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: null remains invalid under JS18 policy");
    result = harness.execute(
        "child.addEventListener(\"click\", handler, undefined);");
    expect(result.succeeded(), "validation: explicit undefined matches omitted options");
    result = harness.execute(
        "child.removeEventListener(\"click\", handler, undefined);");
    expect(result.succeeded(), "validation: explicit undefined removal succeeds");
    result = harness.execute(
        "child.addEventListener(\"click\", handler, { capture: 1 });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: object capture still requires Boolean");
    result = harness.execute(
        "child.addEventListener(\"click\", handler, { once: \"yes\" });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: object once still requires Boolean");
    result = harness.execute(
        "child.addEventListener(\"mouseover\", handler, true);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: unsupported event remains rejected");
    result = harness.execute(
        "child.removeEventListener(\"click\", null, true);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: invalid callback remains rejected");
    result = harness.execute(
        "child.addEventListener(\"click\", handler, { capture: true, future: true });");
    expect(result.succeeded(), "validation: unknown object member remains ignored");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "validation: undefined and object registrations use ordinary slots");

    NavigatorScriptExecutionHarness controls;
    loadFixture(controls, error, "controls");
    result = controls.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var child = document.getElementById("child");
var stop = document.getElementById("stop");
var immediate = document.getElementById("immediate");
var targetStop = document.getElementById("target-stop");
var targetImmediate = document.getElementById("target-immediate");
var stopLog = "";
root.addEventListener("click", function(event) {
    if (event.target === stop) { stopLog = stopLog + "a"; event.stopPropagation(); }
}, true);
root.addEventListener("click", function(event) {
    if (event.target === stop) stopLog = stopLog + "b";
}, true);
parent.addEventListener("click", function(event) {
    if (event.target === stop) stopLog = stopLog + "p";
}, true);
var immediateLog = "";
root.addEventListener("click", function(event) {
    if (event.target === immediate) { immediateLog = immediateLog + "a"; event.stopImmediatePropagation(); }
}, true);
root.addEventListener("click", function(event) {
    if (event.target === immediate) immediateLog = immediateLog + "b";
}, true);
var targetStopLog = "";
targetStop.addEventListener("click", function(event) {
    targetStopLog = targetStopLog + "c" + event.eventPhase; event.stopPropagation();
}, true);
targetStop.onclick = function(event) { targetStopLog = targetStopLog + "o" + event.eventPhase; };
targetStop.addEventListener("click", function(event) { targetStopLog = targetStopLog + "b" + event.eventPhase; }, false);
parent.addEventListener("click", function(event) { if (event.target === targetStop) targetStopLog = targetStopLog + "p"; }, false);
var targetImmediateLog = "";
targetImmediate.addEventListener("click", function(event) {
    targetImmediateLog = targetImmediateLog + "c" + event.eventPhase; event.stopImmediatePropagation();
}, true);
targetImmediate.onclick = function(event) { targetImmediateLog = targetImmediateLog + "o"; };
targetImmediate.addEventListener("click", function(event) { targetImmediateLog = targetImmediateLog + "b"; }, false);
)JS");
    expect(result.succeeded(), "controls: Boolean propagation setup succeeds");
    bool prevented = false;
    expect(click(controls, "stop", error, &prevented,
        "controls: Boolean stopPropagation"),
        "controls: stopPropagation dispatch succeeds");
    expectString(controls, "stopLog", "ab",
        "controls: stopPropagation preserves same-node capture listeners");
    expect(click(controls, "immediate", error, &prevented,
        "controls: Boolean stopImmediatePropagation"),
        "controls: stopImmediatePropagation dispatch succeeds");
    expectString(controls, "immediateLog", "a",
        "controls: immediate stop suppresses later listeners and nodes");
    expect(click(controls, "target-stop", error, &prevented,
        "controls: target Boolean stopPropagation"),
        "controls: target stop dispatch succeeds");
    expectString(controls, "targetStopLog", "c2o2b2",
        "controls: target stop keeps all target callbacks at phase 2");
    expect(click(controls, "target-immediate", error, &prevented,
        "controls: target Boolean immediate stop"),
        "controls: target immediate dispatch succeeds");
    expectString(controls, "targetImmediateLog", "c2",
        "controls: target immediate stop suppresses later target callbacks");

    NavigatorScriptExecutionHarness cancel;
    loadFixture(cancel, error, "cancel");
    result = cancel.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var link = document.getElementById("cancel");
var cancelLog = "";
root.addEventListener("click", function(event) {
    if (event.target === link) { cancelLog = cancelLog + "c" + event.eventPhase; event.preventDefault(); }
}, true);
link.onclick = function(event) {
    cancelLog = cancelLog + "t" + event.eventPhase + event.defaultPrevented;
};
parent.addEventListener("click", function(event) {
    if (event.target === link) cancelLog = cancelLog + "p" + event.eventPhase + event.defaultPrevented;
}, false);
)JS");
    expect(result.succeeded(), "cancel: Boolean preventDefault setup succeeds");
    expect(click(cancel, "cancel", error, &prevented,
        "cancel: Boolean capture preventDefault"),
        "cancel: dispatch continues after cancellation");
    expect(prevented, "cancel: default action is cancelled");
    expectString(cancel, "cancelLog", "c1t2truep3true",
        "cancel: defaultPrevented is visible in target and bubble");
}

void testMutationErrorsOverflowAndNavigation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "mutation");
    ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var mutation = document.getElementById("mutation");
var removedRan = false;
var mutationLog = "";
function removed(event) { removedRan = true; mutationLog = mutationLog + "r"; }
function added(event) { mutationLog = mutationLog + "a" + event.eventPhase; }
parent.addEventListener("click", removed, false);
mutation.addEventListener("click", function(event) {
    mutationLog = mutationLog + "c" + event.eventPhase;
    parent.removeEventListener("click", removed, false);
    parent.addEventListener("click", added, false);
}, true);
)JS");
    expect(result.succeeded(), "mutation: Boolean registration setup succeeds");
    bool prevented = false;
    expect(click(harness, "mutation", error, &prevented,
        "mutation: Boolean capture changes bubble"),
        "mutation: Boolean mutation dispatch succeeds");
    expectString(harness, "mutationLog", "c2a3",
        "mutation: removed/added bubble snapshots retain JS20 semantics");
    expectBoolean(harness, "removedRan", false,
        "mutation: removed bubble listener does not execute");

    NavigatorScriptExecutionHarness errors;
    loadFixture(errors, error, "errors");
    result = errors.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var target = document.getElementById("error");
var stopTarget = document.getElementById("stop-error");
var errorLog = "";
root.addEventListener("click", function(event) {
    if (event.target === target) { errorLog = errorLog + "a" + event.eventPhase; event.preventDefault(); var x = unknownCapture; }
}, true);
root.addEventListener("click", function(event) {
    if (event.target === target) errorLog = errorLog + "b" + event.eventPhase;
}, true);
target.onclick = function(event) { errorLog = errorLog + "t" + event.eventPhase + event.defaultPrevented; };
parent.addEventListener("click", function(event) {
    if (event.target === target) errorLog = errorLog + "p" + event.eventPhase + event.defaultPrevented;
}, false);
var stopLog = "";
root.addEventListener("click", function(event) {
    if (event.target === stopTarget) { stopLog = stopLog + "a"; event.stopPropagation(); var x = unknownStop; }
}, true);
root.addEventListener("click", function(event) {
    if (event.target === stopTarget) stopLog = stopLog + "b";
}, true);
)JS");
    expect(result.succeeded(), "errors: Boolean error setup succeeds");
    expect(!click(errors, "error", error, &prevented,
        "errors: Boolean callback error"),
        "errors: callback error is contained and reported");
    expect(error == RuntimeErrorCode::UnknownIdentifier,
        "errors: first callback error is preserved");
    expectString(errors, "errorLog", "a1b1t2truep3true",
        "errors: later phases survive Boolean capture error");
    expect(prevented, "errors: preventDefault survives Boolean callback error");
    expect(!click(errors, "stop-error", error, &prevented,
        "errors: Boolean stop callback error"),
        "errors: stop callback error is reported");
    expectString(errors, "stopLog", "ab",
        "errors: stopPropagation survives Boolean callback error");
    expect(errors.runtime().eventPhase() == kEventPhaseNone,
        "errors: phase resets after contained errors");

    NavigatorScriptExecutionHarness overflow;
    std::string deep = "<html><body><div id=\"root\">";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
        ++index) deep += "<div>";
    deep += "<button id=\"deep\" type=\"button\">Deep</button>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
        ++index) deep += "</div>";
    deep += "</div></body></html>";
    expect(overflow.loadHtml("file:///js22-overflow.html", deep, error),
        "overflow: fixture loads");
    expect(overflow.relayout(), "overflow: relayout succeeds");
    result = overflow.execute(
        "var deep = document.getElementById(\"deep\");"
        "var root = document.getElementById(\"root\");"
        "var calls = 0;"
        "root.addEventListener(\"click\", function(event) { calls = calls + 1; }, true);"
        "deep.addEventListener(\"click\", function(event) { calls = calls + 1; });");
    expect(result.succeeded(), "overflow: Boolean setup succeeds");
    bool overflowPrevented = false;
    expect(!overflow.dispatchClick(serialById(overflow, "deep"), error,
        &overflowPrevented) &&
        error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "overflow: path fails before Boolean callbacks");
    expectNumber(overflow, "calls", 0.0,
        "overflow: no Boolean callback executes");
    const std::uint64_t deepSerial = serialById(overflow, "deep");
    const std::uint64_t rootSerial = serialById(overflow, "root");
    for (gxos::web::HtmlElementRef& element : overflow.document().structuralElements) {
        if (element.serial == deepSerial) { element.parentSerial = rootSerial; break; }
    }
    expect(overflow.dispatchClick(deepSerial, error, &overflowPrevented),
        "overflow: repaired path recovers");
    expectNumber(overflow, "calls", 2.0,
        "overflow: recovered path invokes capture and target");
    expect(overflow.runtime().eventPhase() == kEventPhaseNone,
        "overflow: recovery ends at NONE");

    NavigatorScriptExecutionHarness navigation;
    loadFixture(navigation, error, "navigation");
    result = navigation.execute(
        "var stale = document.getElementById(\"child\");"
        "var root = document.getElementById(\"root\");"
        "root.addEventListener(\"click\", function(event) {}, true);"
        "root.addEventListener(\"click\", function(event) {}, false);");
    expect(result.succeeded(), "navigation: Boolean setup succeeds");
    expect(navigation.hostAdapter().clickListenerCount() == 2u,
        "navigation: shorthand registrations are ordinary listeners");
    expect(navigation.invalidateDocumentGeneration(error),
        "navigation: generation invalidates");
    result = navigation.execute("var staleId = stale.id;");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "navigation: stale Element remains fail-closed");
    expect(navigation.replaceHtml("file:///js22-new.html",
        "<html><body><button id=\"fresh\" type=\"button\">Fresh</button></body></html>",
        error), "navigation: replacement succeeds");
    expect(navigation.hostAdapter().clickListenerCount() == 0u,
        "navigation: Boolean listener table is cleared");
    result = navigation.execute(
        "var fresh = document.getElementById(\"fresh\");"
        "var freshPhase = 0;"
        "fresh.addEventListener(\"click\", function(event) { freshPhase = event.eventPhase; }, true);");
    expect(result.succeeded(), "navigation: fresh Boolean listener installs");
    expect(click(navigation, "fresh", error, &prevented,
        "navigation: fresh click"), "navigation: fresh dispatch succeeds");
    expectNumber(navigation, "freshPhase", kEventPhaseAtTarget,
        "navigation: fresh target reports phase 2");
}

void testCapacityAndBoundedStress()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness capacity;
    loadFixture(capacity, error, "capacity");
    std::string source =
        "var root = document.getElementById(\"root\");"
        "function shared(event) {}"
        "root.addEventListener(\"click\", shared, true);"
        "root.addEventListener(\"click\", shared, { capture: true });";
    for (int index = 0; index < 63; ++index) {
        source += "root.addEventListener(\"click\", function(event) {}, ";
        source += (index % 2 == 0) ? "true);" : "{ capture: false });";
    }
    ScriptResult result = capacity.execute(source);
    expect(result.succeeded(), "capacity: 63 mixed registrations plus duplicate succeed");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: duplicate Boolean/object registration uses one slot");
    result = capacity.execute(
        "root.addEventListener(\"click\", function(event) {}, false);");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: registration 65 remains rejected");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: failed 65th registration does not grow table");

    RuntimeLimits stressLimits;
    stressLimits.maxEnvironments = 1024u;
    NavigatorScriptExecutionHarness stress(stressLimits);
    loadFixture(stress, error, "stress");
    result = stress.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var child = document.getElementById("child");
var captureCalls = 0;
var targetCalls = 0;
var bubbleCalls = 0;
var onceCaptureCalls = 0;
var onceBubbleCalls = 0;
var healthy = true;
root.addEventListener("click", function(event) {
    healthy = healthy && event.target === child && event.currentTarget === root &&
        event.eventPhase === 1; captureCalls = captureCalls + 1;
}, true);
parent.addEventListener("click", function(event) {
    healthy = healthy && event.currentTarget === parent && event.eventPhase === 1;
    captureCalls = captureCalls + 1;
}, { capture: true });
child.addEventListener("click", function(event) {
    healthy = healthy && event.currentTarget === child && event.eventPhase === 2;
    targetCalls = targetCalls + 1;
}, false);
parent.addEventListener("click", function(event) {
    healthy = healthy && event.eventPhase === 3; bubbleCalls = bubbleCalls + 1;
}, false);
root.addEventListener("click", function(event) { onceCaptureCalls = onceCaptureCalls + event.eventPhase; }, { capture: true, once: true });
root.addEventListener("click", function(event) { onceBubbleCalls = onceBubbleCalls + event.eventPhase; }, { once: true });
)JS");
    expect(result.succeeded(), "stress: mixed Boolean/object setup succeeds");
    const std::size_t objects = stress.runtime().objectCount();
    const std::size_t properties = stress.runtime().propertyCount();
    const std::size_t hosts = stress.runtime().hostObjectCount();
    bool prevented = false;
    bool allClicks = true;
    for (int index = 0; index < 100; ++index) {
        if (!click(stress, "child", error, &prevented,
                "stress: repeated click " + std::to_string(index + 1))) {
            allClicks = false;
            std::cerr << "stress diagnostic: click " << (index + 1) <<
                " error=" << gxos::javascript::runtimeErrorCodeName(error) <<
                "\n";
            break;
        }
    }
    expect(allClicks, "stress: 100 mixed shorthand clicks succeed");
    expectNumber(stress, "captureCalls", 200.0,
        "stress: persistent capture callbacks remain bounded");
    expectNumber(stress, "targetCalls", 100.0,
        "stress: target callbacks remain bounded");
    expectNumber(stress, "bubbleCalls", 100.0,
        "stress: persistent bubble callbacks remain bounded");
    expectNumber(stress, "onceCaptureCalls", 1.0,
        "stress: object once capture executes once");
    expectNumber(stress, "onceBubbleCalls", 3.0,
        "stress: object once bubble executes once");
    expectBoolean(stress, "healthy", true,
        "stress: target/currentTarget/phase metadata remains coherent");
    expect(stress.runtime().objectCount() == objects + 1u,
        "stress: 100 clicks create no Event object growth");
    expect(stress.runtime().propertyCount() == properties + 10u,
        "stress: 100 clicks create one cached Event with ten properties");
    expect(stress.runtime().hostObjectCount() == hosts,
        "stress: 100 clicks create no host-object growth");
    expect(stress.hostAdapter().clickListenerCount() == 4u,
        "stress: once listeners release fixed listener slots");
    expect(stress.runtime().eventPhase() == kEventPhaseNone,
        "stress: phase is NONE after repeated clicks");
    expect(kEventPhaseCapturing == 1 && kEventPhaseAtTarget == 2 &&
        kEventPhaseBubbling == 3,
        "stress: phase constants retain standard values");
}

} // namespace

int main()
{
    testBooleanMappingOrderingAndDuplicates();
    testBooleanRemovalAndOnce();
    testValidationAndControls();
    testMutationErrorsOverflowAndNavigation();
    testCapacityAndBoundedStress();
    if (failures != 0) {
        std::cerr << failures << " JS22 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS22 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
