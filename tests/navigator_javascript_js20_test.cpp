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
using gxos::javascript::kNavigatorScriptMaxPropagationDepth;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<div id="root"><div id="grandparent"><div id="middle"><div id="parent">
<button id="target" type="button">Target</button>
<button id="stop" type="button">Stop</button>
<button id="immediate" type="button">Immediate</button>
<button id="target-stop" type="button">Target stop</button>
<button id="target-immediate" type="button">Target immediate</button>
<button id="bubble-stop" type="button">Bubble stop</button>
<button id="bubble-immediate" type="button">Bubble immediate</button>
<button id="once" type="button">Once</button>
<button id="target-once" type="button">Target once</button>
<button id="prevent" type="button">Prevent</button>
<button id="mutation" type="button">Mutation</button>
<button id="remove-bubble" type="button">Remove bubble</button>
<button id="reuse" type="button">Reuse</button>
<button id="onclick-mutation" type="button">Onclick mutation</button>
<button id="error" type="button">Error</button>
<button id="stop-error" type="button">Stop error</button>
<button id="immediate-error" type="button">Immediate error</button>
<button id="prevent-error" type="button">Prevent error</button>
<button id="once-error" type="button">Once error</button>
<a id="cancel" href="file:///js20-target.html">Cancel</a>
</div></div></div>
<div id="branch-a"><button id="branch-a-target" type="button">A</button></div>
<div id="branch-b"><button id="branch-b-target" type="button">B</button></div>
</div>
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
    expect(harness.loadHtml("file:///js20.html", kFixture, error),
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

void testOrderingAndMetadata()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "ordering");
    ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var grandparent = document.getElementById("grandparent");
var parent = document.getElementById("parent");
var child = document.getElementById("target");
var order = "";
var targetStable = true;
var rootCurrent = false;
var grandCurrent = false;
var parentCurrent = false;
var childCurrent = false;
var allMetadata = true;
function metadata(event, node) {
    targetStable = targetStable && event.target === child;
    allMetadata = allMetadata && event.bubbles === true && event.cancelable === true;
    if (event.currentTarget === root) rootCurrent = true;
    if (event.currentTarget === grandparent) grandCurrent = true;
    if (event.currentTarget === parent) parentCurrent = true;
    if (event.currentTarget === child) childCurrent = true;
}
root.addEventListener("click", function(event) { metadata(event, root); order = order + "A"; }, { capture: true });
root.addEventListener("click", function(event) { metadata(event, root); order = order + "B"; }, { capture: true });
grandparent.addEventListener("click", function(event) { metadata(event, grandparent); order = order + "C"; }, { capture: true });
grandparent.addEventListener("click", function(event) { metadata(event, grandparent); order = order + "D"; }, { capture: true });
parent.addEventListener("click", function(event) { metadata(event, parent); order = order + "E"; }, { capture: true });
parent.addEventListener("click", function(event) { metadata(event, parent); order = order + "F"; }, { capture: true });
child.addEventListener("click", function(event) { metadata(event, child); order = order + "G"; }, { capture: true });
child.addEventListener("click", function(event) { metadata(event, child); order = order + "H"; }, { capture: true });
child.onclick = function(event) { metadata(event, child); order = order + "I"; };
child.addEventListener("click", function(event) { metadata(event, child); order = order + "J"; });
child.addEventListener("click", function(event) { metadata(event, child); order = order + "K"; });
parent.onclick = function(event) { metadata(event, parent); order = order + "L"; };
parent.addEventListener("click", function(event) { metadata(event, parent); order = order + "M"; });
parent.addEventListener("click", function(event) { metadata(event, parent); order = order + "N"; });
root.onclick = function(event) { metadata(event, root); order = order + "O"; };
root.addEventListener("click", function(event) { metadata(event, root); order = order + "P"; });
root.addEventListener("click", function(event) { metadata(event, root); order = order + "Q"; });
)JS");
    expect(result.succeeded(), "ordering: setup succeeds");
    bool prevented = false;
    expect(click(harness, "target", error, &prevented,
        "ordering: authentic dispatch"), "ordering: dispatch succeeds");
    expectString(harness, "order", "ABCDEFGHIJKLMNOPQ",
        "ordering: root-to-target capture, target, then bubble");
    expectBoolean(harness, "targetStable", true,
        "ordering: event.target remains original child");
    expectBoolean(harness, "rootCurrent", true,
        "ordering: root currentTarget is canonical root");
    expectBoolean(harness, "grandCurrent", true,
        "ordering: grandparent currentTarget is canonical");
    expectBoolean(harness, "parentCurrent", true,
        "ordering: parent currentTarget is canonical");
    expectBoolean(harness, "childCurrent", true,
        "ordering: target currentTarget is canonical child");
    expectBoolean(harness, "allMetadata", true,
        "ordering: bubbles and cancelable stay true in every phase");
    expect(!prevented, "ordering: no cancellation by inspection");
    expect(harness.hostAdapter().clickListenerCount() == 14u,
        "ordering: capture and bubble share the fixed listener table");
}

void testIdentityOptionsAndRemoval()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "identity");
    ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var child = document.getElementById("target");
var sameCalls = 0;
function same(event) { sameCalls = sameCalls + 1; }
parent.addEventListener("click", same, { capture: true, once: true });
parent.addEventListener("click", same, { capture: true, once: false });
parent.addEventListener("click", same);
parent.addEventListener("click", same, { capture: false, once: true });
var phaseLog = "";
function phase(event) { phaseLog = phaseLog + "x"; }
parent.addEventListener("click", phase, { capture: true });
parent.addEventListener("click", phase);
)JS");
    expect(result.succeeded(), "identity: options setup succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 4u,
        "identity: capture and bubble differ, once does not");
    bool prevented = false;
    expect(click(harness, "target", error, &prevented,
        "identity: first click"), "identity: first click succeeds");
    expectNumber(harness, "sameCalls", 2.0,
        "identity: same callback runs once in capture and once in bubble");
    expect(harness.hostAdapter().clickListenerCount() == 3u,
        "identity: once capture releases its slot");
    result = harness.execute(
        "parent = document.getElementById(\"parent\");"
        "parent.removeEventListener(\"click\", phase);"
        "parent.removeEventListener(\"click\", same, { capture: true, once: true });");
    expect(result.succeeded(), "identity: capture-aware removal succeeds");
    expect(click(harness, "target", error, &prevented,
        "identity: bubble-only removal click"),
        "identity: second click succeeds");
    expectString(harness, "phaseLog", "xxx",
        "identity: two-argument removal removes only bubble");
    expectNumber(harness, "sameCalls", 3.0,
        "identity: persistent bubble remains after capture once is gone");
    result = harness.execute(
        "parent.removeEventListener(\"click\", phase, { capture: true, once: false });"
        "parent.removeEventListener(\"click\", same, { capture: false });");
    expect(result.succeeded(), "identity: removal once field is ignored");
    expect(click(harness, "target", error, &prevented,
        "identity: all phase removals click"),
        "identity: final click succeeds");
    expectString(harness, "phaseLog", "xxx",
        "identity: capture removal leaves no new phase callback");

    result = harness.execute(
        "function unknown(event) { sameCalls = sameCalls + 10; }"
        "parent.addEventListener(\"click\", unknown, { passive: true, signal: 1 });");
    expect(result.succeeded(), "identity: unknown option members remain ignored");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "identity: unknown options do not create extra registrations");
    result = harness.execute(
        "parent.addEventListener(\"click\", unknown, { capture: 1 });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "identity: malformed capture is rejected safely");
    result = harness.execute(
        "parent.addEventListener(\"click\", unknown, { capture: true, once: 1 });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "identity: malformed capture+once is rejected safely");
    result = harness.execute(
        "parent.removeEventListener(\"click\", unknown, { capture: true, once: 1 });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "identity: malformed removal options are rejected safely");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "identity: malformed options consume no capacity");
}

void testPropagationControls()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "controls");
    ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var target = document.getElementById("target");
var stop = document.getElementById("stop");
var immediate = document.getElementById("immediate");
var targetStop = document.getElementById("target-stop");
var targetImmediate = document.getElementById("target-immediate");
var bubbleStop = document.getElementById("bubble-stop");
var bubbleImmediate = document.getElementById("bubble-immediate");
var stopLog = "";
root.addEventListener("click", function(event) { if (event.target === stop) { stopLog = stopLog + "a"; event.stopPropagation(); } }, { capture: true });
root.addEventListener("click", function(event) { if (event.target === stop) stopLog = stopLog + "b"; }, { capture: true });
parent.addEventListener("click", function(event) { if (event.target === stop) stopLog = stopLog + "p"; }, { capture: true });
stop.addEventListener("click", function(event) { stopLog = stopLog + "t"; });
var immediateLog = "";
root.addEventListener("click", function(event) { if (event.target === immediate) { immediateLog = immediateLog + "a"; event.stopImmediatePropagation(); } }, { capture: true });
root.addEventListener("click", function(event) { if (event.target === immediate) immediateLog = immediateLog + "b"; }, { capture: true });
immediate.addEventListener("click", function(event) { immediateLog = immediateLog + "t"; });
var targetStopLog = "";
targetStop.onclick = function(event) { targetStopLog = targetStopLog + "o"; };
targetStop.addEventListener("click", function(event) { targetStopLog = targetStopLog + "c"; event.stopPropagation(); }, { capture: true });
targetStop.addEventListener("click", function(event) { targetStopLog = targetStopLog + "b"; });
var targetImmediateLog = "";
targetImmediate.onclick = function(event) { targetImmediateLog = targetImmediateLog + "o"; };
targetImmediate.addEventListener("click", function(event) { targetImmediateLog = targetImmediateLog + "c"; event.stopImmediatePropagation(); }, { capture: true });
targetImmediate.addEventListener("click", function(event) { targetImmediateLog = targetImmediateLog + "b"; });
var bubbleStopLog = "";
bubbleStop.addEventListener("click", function(event) { bubbleStopLog = bubbleStopLog + "a"; event.stopPropagation(); });
bubbleStop.addEventListener("click", function(event) { bubbleStopLog = bubbleStopLog + "b"; });
parent.addEventListener("click", function(event) { if (event.target === bubbleStop) bubbleStopLog = bubbleStopLog + "p"; });
var bubbleImmediateLog = "";
bubbleImmediate.addEventListener("click", function(event) { bubbleImmediateLog = bubbleImmediateLog + "a"; event.stopImmediatePropagation(); });
bubbleImmediate.addEventListener("click", function(event) { bubbleImmediateLog = bubbleImmediateLog + "b"; });
parent.addEventListener("click", function(event) { if (event.target === bubbleImmediate) bubbleImmediateLog = bubbleImmediateLog + "p"; });
)JS");
    expect(result.succeeded(), "controls: setup succeeds");
    bool prevented = false;
    expect(click(harness, "stop", error, &prevented,
        "controls: ancestor capture stop"), "controls: stop dispatch succeeds");
    expectString(harness, "stopLog", "ab",
        "controls: stopPropagation finishes same root capture node only");
    expect(click(harness, "immediate", error, &prevented,
        "controls: ancestor immediate stop"),
        "controls: immediate dispatch succeeds");
    expectString(harness, "immediateLog", "a",
        "controls: immediate stop skips same node and later phases");
    expect(click(harness, "target-stop", error, &prevented,
        "controls: target stop"), "controls: target stop dispatch succeeds");
    expectString(harness, "targetStopLog", "cob",
        "controls: target stop allows onclick and target bubble only");
    expect(click(harness, "target-immediate", error, &prevented,
        "controls: target immediate"),
        "controls: target immediate dispatch succeeds");
    expectString(harness, "targetImmediateLog", "c",
        "controls: target immediate skips onclick and target bubble");
    expect(click(harness, "bubble-stop", error, &prevented,
        "controls: bubble stop"), "controls: bubble stop dispatch succeeds");
    expectString(harness, "bubbleStopLog", "ab",
        "controls: bubble stop preserves same-node listener only");
    expect(click(harness, "bubble-immediate", error, &prevented,
        "controls: bubble immediate"),
        "controls: bubble immediate dispatch succeeds");
    expectString(harness, "bubbleImmediateLog", "a",
        "controls: bubble immediate skips later listeners and ancestors");
}

void testOnceCancellationAndMutations()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "once-mutation");
    ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var target = document.getElementById("target");
var onceTarget = document.getElementById("target-once");
var once = document.getElementById("once");
var prevent = document.getElementById("prevent");
var mutationTarget = document.getElementById("mutation");
var removeBubbleTarget = document.getElementById("remove-bubble");
var reuseTarget = document.getElementById("reuse");
var cancelLog = "";
var onceCalls = 0;
var targetOnceCalls = 0;
parent.addEventListener("click", function(event) { onceCalls = onceCalls + 1; }, { capture: true, once: true });
onceTarget.addEventListener("click", function(event) { targetOnceCalls = targetOnceCalls + 1; }, { capture: true, once: true });
root.addEventListener("click", function(event) { if (event.target === prevent) { cancelLog = cancelLog + "r"; event.preventDefault(); } }, { capture: true });
parent.addEventListener("click", function(event) { if (event.target === prevent) cancelLog = cancelLog + "p"; });
prevent.addEventListener("click", function(event) { if (event.defaultPrevented) cancelLog = cancelLog + "d"; else cancelLog = cancelLog + "x"; });
var mutationLog = "";
function laterCapture(event) { if (event.target === mutationTarget) mutationLog = mutationLog + "l"; }
function newCapture(event) { if (event.target === mutationTarget) mutationLog = mutationLog + "n"; }
function oldBubble(event) { if (event.target === mutationTarget) mutationLog = mutationLog + "o"; }
function newBubble(event) { if (event.target === mutationTarget) mutationLog = mutationLog + "b"; }
function mutate(event) {
    if (event.target !== mutationTarget) return;
    mutationLog = mutationLog + "a";
    parent.removeEventListener("click", laterCapture, { capture: true });
    parent.addEventListener("click", newCapture, { capture: true });
    parent.addEventListener("click", newBubble);
}
parent.addEventListener("click", mutate, { capture: true });
parent.addEventListener("click", laterCapture, { capture: true });
parent.addEventListener("click", oldBubble);
var removedBubbleLog = "";
function removableBubble(event) { if (event.target === removeBubbleTarget) removedBubbleLog = removedBubbleLog + "b"; }
function removeBubble(event) {
    if (event.target !== removeBubbleTarget) return;
    removedBubbleLog = removedBubbleLog + "a";
    parent.removeEventListener("click", removableBubble);
}
parent.addEventListener("click", removeBubble, { capture: true });
parent.addEventListener("click", removableBubble);
var reuseLog = "";
function old(event) { if (event.target === reuseTarget) reuseLog = reuseLog + "o"; }
function replacement(event) { if (event.target === reuseTarget) reuseLog = reuseLog + "n"; }
function replace(event) {
    if (event.target !== reuseTarget) return;
    parent.removeEventListener("click", old);
    parent.addEventListener("click", replacement);
}
parent.addEventListener("click", old);
parent.addEventListener("click", replace, { capture: true });
)JS");
    expect(result.succeeded(), std::string("once-mutation: setup succeeds (") +
        gxos::javascript::runtimeErrorCodeName(result.runtimeError.code) +
        ",status=" + gxos::javascript::scriptStatusName(result.status) +
        ",parser=" + gxos::javascript::parserErrorCodeName(
            result.parserError.code) + ",actual=" +
        gxos::javascript::tokenTypeName(result.parserError.actual) +
        ",expected=" + gxos::javascript::tokenTypeName(
            result.parserError.expected) + ",line=" +
        std::to_string(result.parserError.location.line) + ",column=" +
        std::to_string(result.parserError.location.column) + ")");
    bool prevented = false;
    expect(click(harness, "once", error, &prevented,
        "once-mutation: once capture first"), "once-mutation: first succeeds");
    expect(click(harness, "once", error, &prevented,
        "once-mutation: once capture second"), "once-mutation: second succeeds");
    expectNumber(harness, "onceCalls", 1.0,
        "once-mutation: ancestor capture once fires once");
    expect(click(harness, "target-once", error, &prevented,
        "once-mutation: target capture once first"), "once-mutation: target first succeeds");
    expect(click(harness, "target-once", error, &prevented,
        "once-mutation: target capture once second"), "once-mutation: target second succeeds");
    expectNumber(harness, "targetOnceCalls", 1.0,
        "once-mutation: target capture once fires once");
    expect(click(harness, "prevent", error, &prevented,
        "once-mutation: capture cancellation"), "once-mutation: cancellation dispatch succeeds");
    expect(prevented, "once-mutation: capture preventDefault reaches default action boundary");
    expectString(harness, "cancelLog", "rdp",
        "once-mutation: cancellation is visible in target and bubble");
    expect(click(harness, "mutation", error, &prevented,
        "once-mutation: mutation first"), "once-mutation: mutation first succeeds");
    expect(click(harness, "mutation", error, &prevented,
        "once-mutation: mutation second"), "once-mutation: mutation second succeeds");
    expectString(harness, "mutationLog", "aobanob",
        "once-mutation: capture snapshot filters removal and later bubble sees addition");
    expect(click(harness, "remove-bubble", error, &prevented,
        "once-mutation: capture removes bubble"), "once-mutation: remove first succeeds");
    expectString(harness, "removedBubbleLog", "a",
        "once-mutation: removed bubble is absent from later snapshot");
    expect(click(harness, "reuse", error, &prevented,
        "once-mutation: stale slot reuse"), "once-mutation: reuse dispatch succeeds");
    expectString(harness, "reuseLog", "n",
        "once-mutation: reused physical slot cannot invoke stale callback");

    result = harness.execute(R"JS(
var onclickMutation = document.getElementById("onclick-mutation");
var onclickMutationLog = "";
function addedCapture(event) { onclickMutationLog = onclickMutationLog + "c"; }
onclickMutation.onclick = function(event) {
    onclickMutationLog = onclickMutationLog + "o";
    onclickMutation.addEventListener("click", addedCapture, { capture: true });
};
)JS");
    expect(result.succeeded(), "once-mutation: onclick mutation setup succeeds");
    expect(click(harness, "onclick-mutation", error, &prevented,
        "once-mutation: onclick capture addition first"), "once-mutation: onclick first succeeds");
    expect(click(harness, "onclick-mutation", error, &prevented,
        "once-mutation: onclick capture addition second"), "once-mutation: onclick second succeeds");
    expectString(harness, "onclickMutationLog", "oco",
        "once-mutation: onclick-added capture waits for next event");
}

void testErrorsAndCapacity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "errors");
    ScriptResult result = harness.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var target = document.getElementById("error");
var stopTarget = document.getElementById("stop-error");
var immediateTarget = document.getElementById("immediate-error");
var preventTarget = document.getElementById("prevent-error");
var onceTarget = document.getElementById("once-error");
var errorLog = "";
root.addEventListener("click", function(event) { if (event.target === target) { errorLog = errorLog + "a"; var bad = missingCaptureError; } }, { capture: true });
root.addEventListener("click", function(event) { errorLog = errorLog + "b"; }, { capture: true });
parent.addEventListener("click", function(event) { if (event.target === target) errorLog = errorLog + "p"; });
target.addEventListener("click", function(event) { errorLog = errorLog + "t"; });
var stopErrorLog = "";
root.addEventListener("click", function(event) { if (event.target === stopTarget) { stopErrorLog = stopErrorLog + "a"; event.stopPropagation(); var bad = stopCaptureError; } }, { capture: true });
root.addEventListener("click", function(event) { if (event.target === stopTarget) stopErrorLog = stopErrorLog + "b"; }, { capture: true });
stopTarget.addEventListener("click", function(event) { stopErrorLog = stopErrorLog + "t"; });
var immediateErrorLog = "";
root.addEventListener("click", function(event) { if (event.target === immediateTarget) { immediateErrorLog = immediateErrorLog + "a"; event.stopImmediatePropagation(); var bad = immediateCaptureError; } }, { capture: true });
root.addEventListener("click", function(event) { if (event.target === immediateTarget) immediateErrorLog = immediateErrorLog + "b"; }, { capture: true });
immediateTarget.addEventListener("click", function(event) { immediateErrorLog = immediateErrorLog + "t"; });
var preventErrorLog = "";
root.addEventListener("click", function(event) { if (event.target === preventTarget) { preventErrorLog = preventErrorLog + "a"; event.preventDefault(); var bad = preventCaptureError; } }, { capture: true });
preventTarget.addEventListener("click", function(event) { if (event.target === preventTarget && event.defaultPrevented) preventErrorLog = preventErrorLog + "t"; });
parent.addEventListener("click", function(event) { if (event.target === preventTarget && event.defaultPrevented) preventErrorLog = preventErrorLog + "p"; });
var onceErrorCalls = 0;
onceTarget.addEventListener("click", function(event) { onceErrorCalls = onceErrorCalls + 1; var bad = onceCaptureError; }, { capture: true, once: true });
)JS");
    expect(result.succeeded(), "errors: setup succeeds");
    bool prevented = false;
    expect(!click(harness, "error", error, &prevented,
        "errors: capture callback error"), "errors: callback error is reported");
    expect(error == RuntimeErrorCode::UnknownIdentifier,
        "errors: callback error code is contained");
    expectString(harness, "errorLog", "abtp",
        "errors: later capture, target, and bubble survive error");
    expect(!click(harness, "stop-error", error, &prevented,
        "errors: stop then error"), "errors: stopped callback error is reported");
    expectString(harness, "stopErrorLog", "ab",
        "errors: stopPropagation state survives callback error");
    expect(!click(harness, "immediate-error", error, &prevented,
        "errors: immediate stop then error"), "errors: immediate error is reported");
    expectString(harness, "immediateErrorLog", "a",
        "errors: immediate stop state survives callback error");
    expect(!click(harness, "prevent-error", error, &prevented,
        "errors: prevent then error"), "errors: prevented callback error is reported");
    expect(prevented, "errors: cancellation state survives callback error");
    expectString(harness, "preventErrorLog", "atp",
        "errors: cancellation continues through target and bubble");
    expect(!click(harness, "once-error", error, &prevented,
        "errors: once callback error first"), "errors: once error first is reported");
    expect(click(harness, "once-error", error, &prevented,
        "errors: once callback error second"), "errors: once error is gone on second click");
    expectNumber(harness, "onceErrorCalls", 1.0,
        "errors: once capture remains removed after callback error");

    NavigatorScriptExecutionHarness capacity;
    std::string html = "<html><body><div id=\"root\"><button id=\"target\">T</button></div>";
    expect(capacity.loadHtml("file:///js20-capacity.html", html, error),
        "capacity: fixture loads");
    expect(capacity.relayout(), "capacity: relayout succeeds");
    std::string source = "var root = document.getElementById(\"root\");var calls = 0;";
    for (int index = 0; index < 32; ++index) {
        source += "function c" + std::to_string(index) +
            "(event) { calls = calls + 1; }";
        source += "root.addEventListener(\"click\", c" +
            std::to_string(index) + ", { capture: true });";
    }
    for (int index = 0; index < 32; ++index) {
        source += "function b" + std::to_string(index) +
            "(event) { calls = calls + 1; }";
        source += "root.addEventListener(\"click\", b" +
            std::to_string(index) + ");";
    }
    result = capacity.execute(source);
    expect(result.succeeded(), "capacity: 32 capture and 32 bubble registrations succeed");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: capture listeners consume global capacity");
    result = capacity.execute(
        "root.addEventListener(\"click\", function(event) {}, { capture: true });");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th registration fails safely");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: rejected 65th registration changes nothing");
    bool capacityPrevented = false;
    expect(capacity.dispatchClick(serialById(capacity, "target"), error,
        &capacityPrevented), "capacity: full mixed table dispatches");
    expectNumber(capacity, "calls", 64.0,
        "capacity: all capture and bubble callbacks execute");
}

void testBranchesOverflowCleanupAndStress()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness branches;
    loadFixture(branches, error, "branches");
    ScriptResult result = branches.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var branchA = document.getElementById("branch-a");
var branchATarget = document.getElementById("branch-a-target");
var branchB = document.getElementById("branch-b");
var branchBTarget = document.getElementById("branch-b-target");
var branchLog = "";
root.addEventListener("click", function(event) { if (event.target === branchATarget) branchLog = branchLog + "r"; }, { capture: true });
branchA.addEventListener("click", function(event) { branchLog = branchLog + "a"; }, { capture: true });
branchB.addEventListener("click", function(event) { branchLog = branchLog + "b"; }, { capture: true });
var ancestorOnly = 0;
parent.addEventListener("click", function(event) { if (event.target === branchATarget) ancestorOnly = ancestorOnly + 1; }, { capture: true });
)JS");
    expect(result.succeeded(), "branches: setup succeeds");
    bool prevented = false;
    expect(click(branches, "branch-a-target", error, &prevented,
        "branches: branch A click"), "branches: branch A dispatch succeeds");
    expectString(branches, "branchLog", "ra",
        "branches: handlerless ancestry and ancestor-only capture work");
    expectNumber(branches, "ancestorOnly", 0.0,
        "branches: unrelated parent does not observe branch A");
    expect(click(branches, "branch-b-target", error, &prevented,
        "branches: branch B click"), "branches: branch B dispatch succeeds");
    expectString(branches, "branchLog", "rab",
        "branches: independent branch capture is isolated");

    NavigatorScriptExecutionHarness navigation;
    loadFixture(navigation, error, "cleanup");
    result = navigation.execute(R"JS(
var old = document.getElementById("target");
var oldCalls = 0;
old.addEventListener("click", function(event) { oldCalls = oldCalls + 1; }, { capture: true });
)JS");
    expect(result.succeeded(), "cleanup: old capture setup succeeds");
    expect(click(navigation, "target", error, &prevented,
        "cleanup: old click"), "cleanup: old click succeeds");
    expectNumber(navigation, "oldCalls", 1.0,
        "cleanup: old capture executes before navigation");
    expect(navigation.replaceHtml("file:///js20-new.html",
        "<html><body><button id=\"fresh\" type=\"button\">Fresh</button></body></html>",
        error), "cleanup: replacement succeeds");
    expect(navigation.hostAdapter().clickListenerCount() == 0u,
        "cleanup: navigation clears capture registrations");
    result = navigation.execute(R"JS(
var fresh = document.getElementById("fresh");
var freshCalls = 0;
fresh.addEventListener("click", function(event) { freshCalls = freshCalls + 1; }, { capture: true });
)JS");
    expect(result.succeeded(), "cleanup: fresh capture setup succeeds");
    expect(click(navigation, "fresh", error, &prevented,
        "cleanup: fresh click"), "cleanup: fresh click succeeds");
    expectNumber(navigation, "freshCalls", 1.0,
        "cleanup: new document capture works and old callback is gone");

    NavigatorScriptExecutionHarness overflow;
    std::string deep = "<html><body>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
        ++index) deep += "<div>";
    deep += "<button id=\"deep\" type=\"button\">Deep</button>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
        ++index) deep += "</div>";
    deep += "</body></html>";
    expect(overflow.loadHtml("file:///js20-overflow.html", deep, error),
        "overflow: fixture loads");
    expect(overflow.relayout(), "overflow: relayout succeeds");
    result = overflow.execute(R"JS(
var deep = document.getElementById("deep");
var overflowCalls = 0;
deep.addEventListener("click", function(event) { overflowCalls = overflowCalls + 1; event.preventDefault(); }, { capture: true, once: true });
)JS");
    expect(result.succeeded(), "overflow: once capture setup succeeds");
    bool overflowPrevented = false;
    const bool overflowed = overflow.dispatchClick(serialById(overflow, "deep"),
        error, &overflowPrevented);
    expect(!overflowed && error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "overflow: path bound rejects before every phase");
    expect(!overflowPrevented, "overflow: no capture cancellation before dispatch");
    expectNumber(overflow, "overflowCalls", 0.0,
        "overflow: no once callback is consumed");
    for (gxos::web::HtmlElementRef& element :
        overflow.document().structuralElements) {
        if (element.serial == serialById(overflow, "deep")) {
            element.parentSerial = 0;
            break;
        }
    }
    expect(overflow.dispatchClick(serialById(overflow, "deep"), error,
        &overflowPrevented), "overflow: repaired path recovers");
    expectNumber(overflow, "overflowCalls", 1.0,
        "overflow: repaired path consumes capture once exactly once");
    expect(overflowPrevented, "overflow: repaired capture cancellation works");

    RuntimeLimits stressLimits;
    stressLimits.maxEnvironments = 1024u;
    NavigatorScriptExecutionHarness stress(stressLimits);
    loadFixture(stress, error, "stress");
    result = stress.execute(R"JS(
var root = document.getElementById("root");
var parent = document.getElementById("parent");
var target = document.getElementById("target");
var captureCalls = 0;
var bubbleCalls = 0;
var onceCaptureCalls = 0;
var onceBubbleCalls = 0;
var metadataHealthy = true;
root.addEventListener("click", function(event) { metadataHealthy = metadataHealthy && event.target === target && event.currentTarget === root && event.bubbles === true && event.cancelable === true; captureCalls = captureCalls + 1; }, { capture: true });
parent.addEventListener("click", function(event) { metadataHealthy = metadataHealthy && event.target === target && event.currentTarget === parent && event.bubbles === true && event.cancelable === true; captureCalls = captureCalls + 1; }, { capture: true });
parent.addEventListener("click", function(event) { metadataHealthy = metadataHealthy && event.target === target && event.currentTarget === parent && event.bubbles === true && event.cancelable === true; bubbleCalls = bubbleCalls + 1; });
target.addEventListener("click", function(event) { metadataHealthy = metadataHealthy && event.target === target && event.currentTarget === target && event.bubbles === true && event.cancelable === true; bubbleCalls = bubbleCalls + 1; });
root.addEventListener("click", function(event) { onceCaptureCalls = onceCaptureCalls + 1; }, { capture: true, once: true });
root.addEventListener("click", function(event) { onceBubbleCalls = onceBubbleCalls + 1; }, { once: true });
)JS");
    expect(result.succeeded(), "stress: bounded listener setup succeeds");
    const std::size_t objects = stress.runtime().objectCount();
    const std::size_t properties = stress.runtime().propertyCount();
    const std::size_t hosts = stress.runtime().hostObjectCount();
    for (int index = 0; index < 100; ++index)
        expect(click(stress, "target", error, &prevented,
            "stress: repeated click " + std::to_string(index + 1)),
            "stress: dispatch succeeds");
    expectNumber(stress, "captureCalls", 200.0,
        "stress: persistent capture callbacks plus once capture total");
    expectNumber(stress, "bubbleCalls", 200.0,
        "stress: target and ancestor bubble callbacks total");
    expectNumber(stress, "onceCaptureCalls", 1.0,
        "stress: once capture fires once");
    expectNumber(stress, "onceBubbleCalls", 1.0,
        "stress: once bubble fires once");
    expectBoolean(stress, "metadataHealthy", true,
        "stress: metadata remains coherent across phases");
    expect(stress.hostAdapter().clickListenerCount() == 4u,
        "stress: once registrations release bounded slots");
    expect(stress.runtime().objectCount() == objects + 1u,
        "stress: 100 clicks create no additional Event objects");
    expect(stress.runtime().propertyCount() == properties + 10u,
        "stress: 100 clicks create no additional Event properties");
    expect(stress.runtime().hostObjectCount() == hosts,
        "stress: target/currentTarget wrappers remain bounded");
}

void testUnsupportedAndBooleanShorthand()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "unsupported");
    ScriptResult result = harness.execute(
        "var target = document.getElementById(\"target\");"
        "function handler(event) {}"
        "target.addEventListener(\"mouseover\", handler, { capture: true });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "unsupported: capture does not add other event types");
    result = harness.execute(
        "target.addEventListener(\"click\", null, { capture: true });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "unsupported: capture does not relax callback validation");
    result = harness.execute(
        "target.addEventListener(\"click\", handler, true);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "unsupported: Boolean capture shorthand remains unsupported");
    result = harness.execute(
        "target.removeEventListener(\"click\", handler, true);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "unsupported: Boolean removal shorthand remains unsupported");
}

} // namespace

int main()
{
    testOrderingAndMetadata();
    testIdentityOptionsAndRemoval();
    testPropagationControls();
    testOnceCancellationAndMutations();
    testErrorsAndCapacity();
    testBranchesOverflowCleanupAndStress();
    testUnsupportedAndBooleanShorthand();
    if (failures != 0) {
        std::cerr << failures << " JS20 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS20 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
