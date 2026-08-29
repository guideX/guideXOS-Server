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
<div id="root"><div id="grandparent"><div id="parent">
<button id="child" type="button">Child</button>
<button id="prevent" type="button">Prevent</button>
<button id="stop-capture" type="button">Stop capture</button>
<button id="capture-immediate" type="button">Capture immediate</button>
<button id="target-stop" type="button">Target stop</button>
<button id="target-immediate" type="button">Target immediate</button>
<button id="bubble-stop" type="button">Bubble stop</button>
<button id="bubble-immediate" type="button">Bubble immediate</button>
<button id="once" type="button">Once</button>
<button id="mutation" type="button">Mutation</button>
<button id="error" type="button">Error</button>
<button id="stop-error" type="button">Stop error</button>
</div></div></div>
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
    expect(harness.loadHtml("file:///js21.html", kFixture, error),
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

void testPhasesConstantsAndMetadata()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "phases");
    const ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var grandparent = document.getElementById("grandparent");
var parent = document.getElementById("parent");
var child = document.getElementById("child");
var prevent = document.getElementById("prevent");
var phaseOrder = "";
var targetCapturePhase = 0;
var targetOnclickPhase = 0;
var targetBubblePhase = 0;
var rootCaptureCurrent = false;
var parentCaptureCurrent = false;
var targetCurrent = false;
var parentBubbleCurrent = false;
var targetStable = true;
var metadataStable = true;
var phasesAreNumeric = true;
var phaseAssignmentSafe = true;
var constantsOk = Event.NONE === 0 && Event.CAPTURING_PHASE === 1 &&
    Event.AT_TARGET === 2 && Event.BUBBLING_PHASE === 3;
var constantsReadOnly = true;
Event.NONE = 99;
Event.CAPTURING_PHASE = 99;
Event.AT_TARGET = 99;
Event.BUBBLING_PHASE = 99;
constantsReadOnly = Event.NONE === 0 && Event.CAPTURING_PHASE === 1 &&
    Event.AT_TARGET === 2 && Event.BUBBLING_PHASE === 3;
var unknownConstant = Event.BANANA;
function inspect(event, node, expected) {
    phasesAreNumeric = phasesAreNumeric &&
        (event.eventPhase === 1 || event.eventPhase === 2 || event.eventPhase === 3);
    targetStable = targetStable && event.target === child;
    metadataStable = metadataStable && event.bubbles === true &&
        event.cancelable === true;
    if (event.currentTarget === root && expected === 1) rootCaptureCurrent = true;
    if (event.currentTarget === parent && expected === 1) parentCaptureCurrent = true;
    if (event.currentTarget === child && expected === 2) targetCurrent = true;
    if (event.currentTarget === parent && expected === 3) parentBubbleCurrent = true;
    var before = event.eventPhase;
    event.eventPhase = 99;
    phaseAssignmentSafe = phaseAssignmentSafe && event.eventPhase === before;
    if (expected === 1) phaseAssignmentSafe = phaseAssignmentSafe &&
        event.eventPhase === Event.CAPTURING_PHASE;
    if (expected === 2) phaseAssignmentSafe = phaseAssignmentSafe &&
        event.eventPhase === Event.AT_TARGET;
    if (expected === 3) phaseAssignmentSafe = phaseAssignmentSafe &&
        event.eventPhase === Event.BUBBLING_PHASE;
}
root.addEventListener("click", function(event) { if (event.target === child) { inspect(event, root, 1); phaseOrder = phaseOrder + "r1"; } }, { capture: true });
root.addEventListener("click", function(event) { if (event.target === child) { inspect(event, root, 1); phaseOrder = phaseOrder + "R1"; } }, { capture: true });
grandparent.addEventListener("click", function(event) { if (event.target === child) { inspect(event, grandparent, 1); phaseOrder = phaseOrder + "g1"; } }, { capture: true });
parent.addEventListener("click", function(event) { if (event.target === child) { inspect(event, parent, 1); phaseOrder = phaseOrder + "p1"; } }, { capture: true });
child.addEventListener("click", function(event) { if (event.target === child) { inspect(event, child, 2); targetCapturePhase = event.eventPhase; phaseOrder = phaseOrder + "c2"; } }, { capture: true });
child.onclick = function(event) { if (event.target === child) { inspect(event, child, 2); targetOnclickPhase = event.eventPhase; phaseOrder = phaseOrder + "o2"; } };
var saved;
child.addEventListener("click", function(event) { if (event.target === child) { inspect(event, child, 2); saved = event; targetBubblePhase = event.eventPhase; phaseOrder = phaseOrder + "t2"; } });
parent.onclick = function(event) { if (event.target === child) { inspect(event, parent, 3); phaseOrder = phaseOrder + "po3"; } };
parent.addEventListener("click", function(event) { if (event.target === child) { inspect(event, parent, 3); phaseOrder = phaseOrder + "p3"; } });
grandparent.onclick = function(event) { if (event.target === child) { inspect(event, grandparent, 3); phaseOrder = phaseOrder + "go3"; } };
grandparent.addEventListener("click", function(event) { if (event.target === child) { inspect(event, grandparent, 3); phaseOrder = phaseOrder + "g3"; } });
root.onclick = function(event) { if (event.target === child) { inspect(event, root, 3); phaseOrder = phaseOrder + "ro3"; } };
root.addEventListener("click", function(event) { if (event.target === child) { inspect(event, root, 3); phaseOrder = phaseOrder + "r3"; } });
var preventLog = "";
root.addEventListener("click", function(event) { if (event.target === prevent) { inspect(event, root, 1); preventLog = preventLog + "r1"; event.preventDefault(); } }, { capture: true });
prevent.onclick = function(event) { if (event.target === prevent) { inspect(event, prevent, 2); preventLog = preventLog + "t2"; } };
parent.addEventListener("click", function(event) { if (event.target === prevent) { inspect(event, parent, 3); preventLog = preventLog + "p3" + event.defaultPrevented; } });
)JS");
    expect(result.succeeded(), "phases: setup succeeds");
    bool prevented = false;
    expect(click(harness, "child", error, &prevented,
        "phases: nested dispatch"), "phases: dispatch succeeds");
    expectString(harness, "phaseOrder",
        "r1R1g1p1c2o2t2po3p3go3g3ro3r3",
        "phases: complete capture-target-bubble order");
    expectNumber(harness, "targetCapturePhase", kEventPhaseAtTarget,
        "phases: target capture is AT_TARGET");
    expectNumber(harness, "targetOnclickPhase", kEventPhaseAtTarget,
        "phases: target onclick is AT_TARGET");
    expectNumber(harness, "targetBubblePhase", kEventPhaseAtTarget,
        "phases: target listener is AT_TARGET");
    expectBoolean(harness, "phasesAreNumeric", true,
        "phases: eventPhase is numeric");
    expectBoolean(harness, "phaseAssignmentSafe", true,
        "phases: eventPhase is read-only");
    expectBoolean(harness, "constantsOk", true,
        "phases: standard constants have numeric values");
    expectBoolean(harness, "constantsReadOnly", true,
        "phases: constants are read-only");
    expect(harness.runtime().lookup(gxos::javascript::SourceView(
        "unknownConstant", 15u)) != nullptr,
        "phases: unknown constant result binding exists");
    const Value* unknown = binding(harness, "unknownConstant");
    expect(unknown != nullptr && unknown->isUndefined(),
        "phases: unknown Event member is undefined");
    expectBoolean(harness, "rootCaptureCurrent", true,
        "phases: root capture currentTarget");
    expectBoolean(harness, "parentCaptureCurrent", true,
        "phases: parent capture currentTarget");
    expectBoolean(harness, "targetCurrent", true,
        "phases: target currentTarget");
    expectBoolean(harness, "parentBubbleCurrent", true,
        "phases: parent bubble currentTarget");
    expectBoolean(harness, "targetStable", true,
        "phases: target remains original child");
    expectBoolean(harness, "metadataStable", true,
        "phases: bubbles/cancelable stay true");
    expect(harness.runtime().eventPhase() == kEventPhaseNone,
        "phases: runtime phase resets outside dispatch");
    const ScriptResult retained = harness.execute(
        "var retainedPhase = saved.eventPhase; var retainedTarget = saved.target === child;");
    expect(retained.succeeded(), "phases: retained Event read succeeds");
    expectNumber(harness, "retainedPhase", kEventPhaseNone,
        "phases: retained Event reports NONE");
    expectBoolean(harness, "retainedTarget", true,
        "phases: retained Event target is stable");
    expect(click(harness, "prevent", error, &prevented,
        "phases: preventDefault across phases"),
        "phases: cancellation dispatch succeeds");
    expectString(harness, "preventLog", "r1t2p3true",
        "phases: defaultPrevented survives phase transitions");
    expect(prevented, "phases: preventDefault reaches dispatch boundary");
}

void testPropagationControlsAndOnce()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "controls");
    const ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var stopCapture = document.getElementById("stop-capture");
var captureImmediate = document.getElementById("capture-immediate");
var targetStop = document.getElementById("target-stop");
var targetImmediate = document.getElementById("target-immediate");
var bubbleStop = document.getElementById("bubble-stop");
var bubbleImmediate = document.getElementById("bubble-immediate");
var once = document.getElementById("once");
var stopLog = "";
root.addEventListener("click", function(event) { if (event.target === stopCapture) { stopLog = stopLog + "a" + event.eventPhase; event.stopPropagation(); } }, { capture: true });
root.addEventListener("click", function(event) { if (event.target === stopCapture) stopLog = stopLog + "b" + event.eventPhase; }, { capture: true });
parent.addEventListener("click", function(event) { if (event.target === stopCapture) stopLog = stopLog + "p" + event.eventPhase; }, { capture: true });
stopCapture.addEventListener("click", function(event) { stopLog = stopLog + "t" + event.eventPhase; });
var captureImmediateLog = "";
root.addEventListener("click", function(event) { if (event.target === captureImmediate) { captureImmediateLog = captureImmediateLog + "a" + event.eventPhase; event.stopImmediatePropagation(); } }, { capture: true });
root.addEventListener("click", function(event) { if (event.target === captureImmediate) captureImmediateLog = captureImmediateLog + "b" + event.eventPhase; }, { capture: true });
captureImmediate.addEventListener("click", function(event) { captureImmediateLog = captureImmediateLog + "t" + event.eventPhase; });
var targetStopLog = "";
targetStop.addEventListener("click", function(event) { targetStopLog = targetStopLog + "c" + event.eventPhase; event.stopPropagation(); }, { capture: true });
targetStop.onclick = function(event) { targetStopLog = targetStopLog + "o" + event.eventPhase; };
targetStop.addEventListener("click", function(event) { targetStopLog = targetStopLog + "b" + event.eventPhase; });
parent.addEventListener("click", function(event) { if (event.target === targetStop) targetStopLog = targetStopLog + "p" + event.eventPhase; });
var targetImmediateLog = "";
targetImmediate.addEventListener("click", function(event) { targetImmediateLog = targetImmediateLog + "c" + event.eventPhase; event.stopImmediatePropagation(); }, { capture: true });
targetImmediate.onclick = function(event) { targetImmediateLog = targetImmediateLog + "o" + event.eventPhase; };
targetImmediate.addEventListener("click", function(event) { targetImmediateLog = targetImmediateLog + "b" + event.eventPhase; });
var bubbleStopLog = "";
bubbleStop.addEventListener("click", function(event) { bubbleStopLog = bubbleStopLog + "t" + event.eventPhase; });
parent.addEventListener("click", function(event) { if (event.target === bubbleStop) { bubbleStopLog = bubbleStopLog + "a" + event.eventPhase; event.stopPropagation(); } });
parent.addEventListener("click", function(event) { if (event.target === bubbleStop) bubbleStopLog = bubbleStopLog + "b" + event.eventPhase; });
root.addEventListener("click", function(event) { if (event.target === bubbleStop) bubbleStopLog = bubbleStopLog + "r" + event.eventPhase; });
var bubbleImmediateLog = "";
bubbleImmediate.addEventListener("click", function(event) { bubbleImmediateLog = bubbleImmediateLog + "t" + event.eventPhase; });
parent.addEventListener("click", function(event) { if (event.target === bubbleImmediate) { bubbleImmediateLog = bubbleImmediateLog + "a" + event.eventPhase; event.stopImmediatePropagation(); } });
parent.addEventListener("click", function(event) { if (event.target === bubbleImmediate) bubbleImmediateLog = bubbleImmediateLog + "b" + event.eventPhase; });
root.addEventListener("click", function(event) { if (event.target === bubbleImmediate) bubbleImmediateLog = bubbleImmediateLog + "r" + event.eventPhase; });
var onceCapture = "";
var onceTargetCapture = "";
var onceTargetBubble = "";
var onceBubble = "";
once.addEventListener("click", function(event) { onceTargetCapture = onceTargetCapture + event.eventPhase; }, { capture: true, once: true });
once.addEventListener("click", function(event) { onceTargetBubble = onceTargetBubble + event.eventPhase; }, { once: true });
)JS");
    expect(result.succeeded(), "controls: setup succeeds");
    bool prevented = false;
    expect(click(harness, "stop-capture", error, &prevented,
        "controls: capture stopPropagation"), "controls: capture stop succeeds");
    expectString(harness, "stopLog", "a1b1", "controls: capture stop phase 1");
    expect(click(harness, "capture-immediate", error, &prevented,
        "controls: capture immediate stop"), "controls: immediate stop succeeds");
    expectString(harness, "captureImmediateLog", "a1",
        "controls: immediate capture phase 1");
    expect(click(harness, "target-stop", error, &prevented,
        "controls: target stopPropagation"), "controls: target stop succeeds");
    expectString(harness, "targetStopLog", "c2o2b2",
        "controls: target stop keeps target phase and suppresses ancestors");
    expect(click(harness, "target-immediate", error, &prevented,
        "controls: target immediate stop"), "controls: target immediate succeeds");
    expectString(harness, "targetImmediateLog", "c2",
        "controls: target immediate phase 2");
    expect(click(harness, "bubble-stop", error, &prevented,
        "controls: bubble stopPropagation"), "controls: bubble stop succeeds");
    expectString(harness, "bubbleStopLog", "t2a3b3",
        "controls: bubble stop keeps phase 3 on same node");
    expect(click(harness, "bubble-immediate", error, &prevented,
        "controls: bubble immediate stop"), "controls: bubble immediate succeeds");
    expectString(harness, "bubbleImmediateLog", "t2a3",
        "controls: bubble immediate phase 3");
    expect(click(harness, "once", error, &prevented,
        "controls: once first"), "controls: first once dispatch succeeds");
    expect(click(harness, "once", error, &prevented,
        "controls: once second"), "controls: second once dispatch succeeds");
    expectString(harness, "onceTargetCapture", "2", "controls: target once capture phase 2");
    expectString(harness, "onceTargetBubble", "2", "controls: target once bubble phase 2");
    expect(harness.runtime().eventPhase() == kEventPhaseNone,
        "controls: phase reset after propagation controls");

    NavigatorScriptExecutionHarness ancestorOnce;
    loadFixture(ancestorOnce, error, "controls-ancestor-once");
    const ScriptResult onceSetup = ancestorOnce.execute(
        "var root = document.getElementById(\"root\");"
        "var parent = document.getElementById(\"parent\");"
        "var once = document.getElementById(\"once\");"
        "var capture = \"\"; var bubble = \"\";"
        "root.addEventListener(\"click\", function(event) { capture = capture + event.eventPhase; }, { capture: true, once: true });"
        "parent.addEventListener(\"click\", function(event) { bubble = bubble + event.eventPhase; }, { once: true });");
    expect(onceSetup.succeeded(), "controls: ancestor once setup succeeds");
    expect(click(ancestorOnce, "once", error, &prevented,
        "controls: ancestor once first"), "controls: ancestor once first succeeds");
    expect(click(ancestorOnce, "once", error, &prevented,
        "controls: ancestor once second"), "controls: ancestor once second succeeds");
    expectString(ancestorOnce, "capture", "1",
        "controls: ancestor once capture phase 1");
    expectString(ancestorOnce, "bubble", "3",
        "controls: ancestor once bubble phase 3");
}

void testMutationErrorsOverflowAndNavigation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "mutation");
    const ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var child = document.getElementById("child");
var mutation = document.getElementById("mutation");
var errorTarget = document.getElementById("error");
var stopError = document.getElementById("stop-error");
var mutationLog = "";
var removedRan = false;
function removed(event) { removedRan = true; mutationLog = mutationLog + "removed"; }
function added(event) { mutationLog = mutationLog + "a" + event.eventPhase; }
parent.addEventListener("click", removed);
mutation.addEventListener("click", function(event) {
    if (event.target === mutation) {
        mutationLog = mutationLog + "c" + event.eventPhase;
        parent.removeEventListener("click", removed);
        parent.addEventListener("click", added);
    }
}, { capture: true });
var errorLog = "";
var errorPhaseHealthy = true;
root.addEventListener("click", function(event) {
    if (event.target === errorTarget) { errorLog = errorLog + "r" + event.eventPhase; event.preventDefault(); var x = captureUnknown; }
}, { capture: true });
root.addEventListener("click", function(event) {
    if (event.target === errorTarget) { errorLog = errorLog + "s" + event.eventPhase; errorPhaseHealthy = errorPhaseHealthy && event.eventPhase === 1; }
}, { capture: true });
errorTarget.onclick = function(event) { errorLog = errorLog + "o" + event.eventPhase; errorPhaseHealthy = errorPhaseHealthy && event.eventPhase === 2; var x = targetUnknown; };
errorTarget.addEventListener("click", function(event) { errorLog = errorLog + "t" + event.eventPhase; errorPhaseHealthy = errorPhaseHealthy && event.eventPhase === 2 && event.defaultPrevented; });
parent.addEventListener("click", function(event) { if (event.target === errorTarget) { errorLog = errorLog + "p" + event.eventPhase; errorPhaseHealthy = errorPhaseHealthy && event.eventPhase === 3 && event.defaultPrevented; } });
var stopErrorLog = "";
root.addEventListener("click", function(event) { if (event.target === stopError) { stopErrorLog = stopErrorLog + "a" + event.eventPhase; event.stopPropagation(); var x = stopUnknown; } }, { capture: true });
root.addEventListener("click", function(event) { if (event.target === stopError) stopErrorLog = stopErrorLog + "b" + event.eventPhase; }, { capture: true });
stopError.addEventListener("click", function(event) { if (event.target === stopError) stopErrorLog = stopErrorLog + "t" + event.eventPhase; });
)JS");
    expect(result.succeeded(), "mutation: setup succeeds");
    bool prevented = false;
    expect(click(harness, "mutation", error, &prevented,
        "mutation: add/remove between phases"), "mutation: dispatch succeeds");
    expectString(harness, "mutationLog", "c2a3",
        "mutation: added bubble listener observes phase 3");
    expectBoolean(harness, "removedRan", false,
        "mutation: removed bubble listener does not execute");
    expect(!click(harness, "error", error, &prevented,
        "errors: contained capture and target errors"),
        "errors: callback error is reported");
    expect(error == RuntimeErrorCode::UnknownIdentifier,
        "errors: first callback error is preserved");
    expectString(harness, "errorLog", "r1s1o2t2p3",
        "errors: phase progression survives callback errors");
    expectBoolean(harness, "errorPhaseHealthy", true,
        "errors: phase values remain correct after errors");
    expect(prevented, "errors: preventDefault survives callback error");
    expect(!click(harness, "stop-error", error, &prevented,
        "errors: propagation control before error"),
        "errors: stop callback error is reported");
    expectString(harness, "stopErrorLog", "a1b1",
        "errors: stopPropagation state survives error at phase 1");
    expect(harness.runtime().eventPhase() == kEventPhaseNone,
        "errors: phase resets after error dispatch");

    NavigatorScriptExecutionHarness overflow;
    std::string deep = "<html><body><div id=\"root\">";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
        ++index) deep += "<div>";
    deep += "<button id=\"deep\" type=\"button\">Deep</button>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
        ++index) deep += "</div>";
    deep += "</div></body></html>";
    expect(overflow.loadHtml("file:///js21-overflow.html", deep, error),
        "overflow: fixture loads");
    expect(overflow.relayout(), "overflow: relayout succeeds");
    ScriptResult overflowSetup = overflow.execute(
        "var deep = document.getElementById(\"deep\");"
        "var root = document.getElementById(\"root\");"
        "var overflowCalls = 0; var recoveryLog = \"\";"
        "root.addEventListener(\"click\", function(event) { recoveryLog = recoveryLog + event.eventPhase; }, { capture: true });"
        "deep.addEventListener(\"click\", function(event) { overflowCalls = overflowCalls + 1; recoveryLog = recoveryLog + event.eventPhase; });"
        "root.addEventListener(\"click\", function(event) { recoveryLog = recoveryLog + event.eventPhase; });");
    expect(overflowSetup.succeeded(), "overflow: setup succeeds");
    bool overflowPrevented = false;
    expect(!overflow.dispatchClick(serialById(overflow, "deep"), error,
        &overflowPrevented) && error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "overflow: path fails before callbacks");
    expect(!overflowPrevented && overflow.runtime().eventPhase() == kEventPhaseNone,
        "overflow: phase is safe after rejected traversal");
    expectNumber(overflow, "overflowCalls", 0.0,
        "overflow: no callback executes");
    const std::uint64_t deepSerial = serialById(overflow, "deep");
    const std::uint64_t rootSerial = serialById(overflow, "root");
    for (gxos::web::HtmlElementRef& element : overflow.document().structuralElements) {
        if (element.serial == deepSerial) { element.parentSerial = rootSerial; break; }
    }
    expect(overflow.dispatchClick(deepSerial, error, &overflowPrevented),
        "overflow: repaired path recovers");
    expectString(overflow, "recoveryLog", "12" + std::to_string(kEventPhaseBubbling),
        "overflow: recovery observes capture/target/bubble phases");
    expect(overflow.runtime().eventPhase() == kEventPhaseNone,
        "overflow: recovery ends at NONE");

    const std::uint64_t oldChild = serialById(harness, "child");
    expect(harness.invalidateDocumentGeneration(error),
        "navigation: generation invalidates");
    ScriptResult stale = harness.execute("var staleId = child.id;");
    expectError(stale, RuntimeErrorCode::StaleHostObject,
        "navigation: stale Element fails closed");
    expect(harness.replaceHtml("file:///js21-new.html",
        "<html><body><button id=\"fresh\" type=\"button\">Fresh</button></body></html>",
        error), "navigation: replacement succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 0u,
        "navigation: listener table clears");
    expect(harness.runtime().eventPhase() == kEventPhaseNone,
        "navigation: phase clears with realm");
    ScriptResult freshSetup = harness.execute(
        "var fresh = document.getElementById(\"fresh\"); var freshPhase = 0;"
        "fresh.addEventListener(\"click\", function(event) { freshPhase = event.eventPhase; });");
    expect(freshSetup.succeeded(), "navigation: fresh listener installs");
    expect(click(harness, "fresh", error, &prevented,
        "navigation: fresh click"), "navigation: fresh dispatch succeeds");
    expectNumber(harness, "freshPhase", kEventPhaseAtTarget,
        "navigation: new dispatch starts with target phase");
    (void)oldChild;
}

void testIndependentTreesAndBoundedStress()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    RuntimeLimits limits;
    limits.maxEnvironments = 1024u;
    NavigatorScriptExecutionHarness harness(limits);
    loadFixture(harness, error, "stress");
    const ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var child = document.getElementById("child");
var treeA = document.getElementById("tree-a");
var treeB = document.getElementById("tree-b");
var aChild = document.getElementById("a-child");
var bChild = document.getElementById("b-child");
var captureCalls = 0;
var targetCalls = 0;
var bubbleCalls = 0;
var onceCaptureCalls = 0;
var onceBubbleCalls = 0;
var healthy = true;
var treeLog = "";
root.addEventListener("click", function(event) { if (event.target === child) { healthy = healthy && event.eventPhase === 1 && event.currentTarget === root; captureCalls = captureCalls + 1; } }, { capture: true });
parent.addEventListener("click", function(event) { if (event.target === child) { healthy = healthy && event.eventPhase === 1 && event.currentTarget === parent; captureCalls = captureCalls + 1; } }, { capture: true });
child.addEventListener("click", function(event) { if (event.target === child) { healthy = healthy && event.eventPhase === 2 && event.currentTarget === child; targetCalls = targetCalls + 1; } });
parent.addEventListener("click", function(event) { if (event.target === child) { healthy = healthy && event.eventPhase === 3 && event.currentTarget === parent; bubbleCalls = bubbleCalls + 1; } });
root.addEventListener("click", function(event) { if (event.target === child) onceCaptureCalls = onceCaptureCalls + event.eventPhase; }, { capture: true, once: true });
root.addEventListener("click", function(event) { if (event.target === child) onceBubbleCalls = onceBubbleCalls + event.eventPhase; }, { once: true });
treeA.addEventListener("click", function(event) { if (event.target === aChild) treeLog = treeLog + event.eventPhase; }, { capture: true });
treeA.addEventListener("click", function(event) { if (event.target === aChild) treeLog = treeLog + event.eventPhase; });
treeB.addEventListener("click", function(event) { if (event.target === bChild) treeLog = treeLog + event.eventPhase; }, { capture: true });
treeB.addEventListener("click", function(event) { if (event.target === bChild) treeLog = treeLog + event.eventPhase; });
)JS");
    expect(result.succeeded(), "stress: setup succeeds");
    const std::size_t objects = harness.runtime().objectCount();
    const std::size_t properties = harness.runtime().propertyCount();
    const std::size_t hosts = harness.runtime().hostObjectCount();
    bool prevented = false;
    for (int index = 0; index < 100; ++index) {
        expect(click(harness, "child", error, &prevented,
            "stress: repeated click " + std::to_string(index + 1)),
            "stress: click dispatch succeeds");
        expect(harness.runtime().eventPhase() == kEventPhaseNone,
            "stress: phase resets after click " + std::to_string(index + 1));
    }
    expectNumber(harness, "captureCalls", 200.0,
        "stress: persistent ancestor capture count");
    expectNumber(harness, "targetCalls", 100.0,
        "stress: target phase count");
    expectNumber(harness, "bubbleCalls", 100.0,
        "stress: ancestor bubble count");
    expectNumber(harness, "onceCaptureCalls", 1.0,
        "stress: once capture phase value");
    expectNumber(harness, "onceBubbleCalls", 3.0,
        "stress: once bubble phase value");
    expectBoolean(harness, "healthy", true,
        "stress: phase/currentTarget metadata remains coherent");
    expect(click(harness, "a-child", error, &prevented,
        "stress: independent tree A"), "stress: tree A dispatch succeeds");
    expect(click(harness, "b-child", error, &prevented,
        "stress: independent tree B"), "stress: tree B dispatch succeeds");
    expectString(harness, "treeLog", "1313",
        "stress: independent trees start fresh");
    expect(harness.runtime().objectCount() == objects + 1u,
        "stress: 100 clicks create no Event growth");
    expect(harness.runtime().propertyCount() == properties + 10u,
        "stress: 100 clicks create one cached Event with ten properties");
    expect(harness.runtime().hostObjectCount() == hosts,
        "stress: 100 clicks create no host-object growth");
    expect(harness.hostAdapter().clickListenerCount() == 8u,
        "stress: once listeners release fixed listener slots");
}

void testUnsupportedRegressions()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "unsupported");
    ScriptResult result = harness.execute(
        "var child = document.getElementById(\"child\");"
        "function handler(event) {}"
        "child.addEventListener(\"mouseover\", handler, { capture: true });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "unsupported: event type remains rejected");
    result = harness.execute(
        "child.addEventListener(\"click\", null, { capture: true });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "unsupported: invalid callback remains rejected");
    result = harness.execute(
        "child.addEventListener(\"click\", handler, true);");
    expect(result.succeeded(),
        "unsupported: Boolean shorthand is accepted by JS22");
    result = harness.execute(
        "child.removeEventListener(\"click\", handler, true);");
    expect(result.succeeded(),
        "unsupported: Boolean removal shorthand is accepted by JS22");
}

} // namespace

int main()
{
    testPhasesConstantsAndMetadata();
    testPropagationControlsAndOnce();
    testMutationErrorsOverflowAndNavigation();
    testIndependentTreesAndBoundedStress();
    testUnsupportedRegressions();
    if (failures != 0) {
        std::cerr << failures << " JS21 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS21 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
