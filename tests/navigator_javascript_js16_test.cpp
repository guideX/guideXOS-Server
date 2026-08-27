#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;
using gxos::javascript::kNavigatorScriptMaxPropagationDepth;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<div id="root">
  <div id="grandparent">
    <div id="parent">
      <a id="link" href="file:///js16-target.html">Link</a>
      <button id="order" type="button">Order</button>
      <button id="listener-cancel" type="button">Listener cancel</button>
      <button id="stop-link" type="button">Stop propagation</button>
      <button id="immediate-link" type="button">Immediate stop</button>
      <button id="combo-one" type="button">Combo one</button>
      <button id="combo-two" type="button">Combo two</button>
      <button id="combo-immediate-one" type="button">Combo immediate one</button>
      <button id="combo-immediate-two" type="button">Combo immediate two</button>
      <button id="nonlink" type="button">Non-link</button>
      <button id="readonly-link" type="button">Read-only</button>
      <button id="error-after" type="button">Error after</button>
      <button id="error-before" type="button">Error before</button>
      <button id="retained" type="button">Retained</button>
      <button id="closure" type="button">Closure</button>
      <button id="zero" type="button">Zero</button>
      <button id="branch-a" type="button">Branch A</button>
      <button id="branch-b" type="button">Branch B</button>
      <div id="ancestor-parent"><a id="ancestor-link" href="file:///js16-ancestor-target.html">Ancestor link</a></div>
    </div>
  </div>
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
        label + ": value");
}

void expectFunction(const NavigatorScriptExecutionHarness& harness,
    const char* name, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value != nullptr) expect(value->type() == ValueType::Function,
        label + ": Function");
}

void expectUndefined(const NavigatorScriptExecutionHarness& harness,
    const char* name, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value != nullptr) expect(value->type() == ValueType::Undefined,
        label + ": Undefined");
}

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": error is " +
        gxos::javascript::runtimeErrorCodeName(result.runtimeError.code));
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
    expect(harness.loadHtml("file:///js16.html", kFixture, error),
        label + ": fixture loads");
    expect(harness.relayout(), label + ": initial relayout succeeds");
}

bool click(NavigatorScriptExecutionHarness& harness, std::uint64_t serial,
    RuntimeErrorCode& error, bool& defaultPrevented, const std::string& label)
{
    const bool dispatched = harness.dispatchClick(serial, error,
        &defaultPrevented);
    expect(dispatched, label + ": dispatch succeeds");
    if (harness.documentDirty()) expect(harness.relayout(),
        label + ": controlled relayout succeeds");
    return dispatched;
}

void testCoreCancellationAndReadOnly()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "core");
    const std::uint64_t link = serialById(harness, "link");
    const std::uint64_t parent = serialById(harness, "parent");
    const std::uint64_t grandparent = serialById(harness, "grandparent");
    expect(link != 0 && parent != 0 && grandparent != 0,
        "core: link ancestry exists");

    ScriptResult result = harness.execute(R"JS(
var link = document.getElementById("link");
var parent = document.getElementById("parent");
var grandparent = document.getElementById("grandparent");
var initialPrevented = false;
var assignedBeforePrevent = false;
var afterPrevented = false;
var assignedAfterPrevent = false;
var savedPrevent = null;
var savedEvent = null;
var targetId = "";
var currentId = "";
var targetCanonical = false;
var currentCanonical = false;
var missingMember = null;
link.onclick = function(event) {
    savedEvent = event;
    savedPrevent = event.preventDefault;
    initialPrevented = event.defaultPrevented;
    event.defaultPrevented = true;
    assignedBeforePrevent = event.defaultPrevented;
    event.preventDefault(1, 2, 3);
    event.preventDefault();
    afterPrevented = event.defaultPrevented;
    event.defaultPrevented = false;
    assignedAfterPrevent = event.defaultPrevented;
    targetId = event.target.id;
    currentId = event.currentTarget.id;
    targetCanonical = event.target === link;
    currentCanonical = event.currentTarget === link;
    missingMember = event.notARealEventMember;
    event.target.textContent = "Cancelled";
};
)JS");
    expect(result.succeeded(), "core: cancellation setup succeeds");
    const std::size_t nativeFunctionsBefore = harness.runtime().nativeFunctionCount();
    const std::size_t objectsBefore = harness.runtime().objectCount();
    bool prevented = false;
    click(harness, link, error, prevented, "core: cancelled link click");
    expect(prevented, "core: default action receives cancellation");
    expectBoolean(harness, "initialPrevented", false,
        "core: defaultPrevented starts false");
    expectBoolean(harness, "assignedBeforePrevent", false,
        "core: read-only assignment cannot set true");
    expectBoolean(harness, "afterPrevented", true,
        "core: preventDefault sets true");
    expectBoolean(harness, "assignedAfterPrevent", true,
        "core: read-only assignment cannot clear true");
    expectFunction(harness, "savedPrevent",
        "core: preventDefault is a callable native function");
    expectString(harness, "targetId", "link", "core: target remains stable");
    expectString(harness, "currentId", "link",
        "core: currentTarget remains stable");
    expectBoolean(harness, "targetCanonical", true,
        "core: target identity remains canonical");
    expectBoolean(harness, "currentCanonical", true,
        "core: currentTarget identity remains canonical");
    expectUndefined(harness, "missingMember",
        "core: unknown Event property remains missing");
    result = harness.execute("var changed = link.textContent;");
    expect(result.succeeded(), "core: mutation readback succeeds");
    expectString(harness, "changed", "Cancelled",
        "core: callback continues after preventDefault");
    expect(harness.layoutRevision() > 1u && !harness.documentDirty(),
        "core: mutation after cancellation relayouts");
    expect(harness.runtime().nativeFunctionCount() == nativeFunctionsBefore + 3u,
        "core: Event uses three cached native functions");
    click(harness, link, error, prevented, "core: repeated cancelled link click");
    expect(prevented, "core: repeated preventDefault remains monotonic");
    expect(harness.runtime().objectCount() == objectsBefore + 1u,
        "core: cached Event object remains bounded");
}

void testBubblingOrderingAndAncestorCancellation()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "bubbling");
    const std::uint64_t link = serialById(harness, "link");
    const std::uint64_t parent = serialById(harness, "parent");
    const std::uint64_t grandparent = serialById(harness, "grandparent");
    const std::uint64_t order = serialById(harness, "order");
    const std::uint64_t listenerCancel = serialById(harness, "listener-cancel");
    const std::uint64_t ancestorLink = serialById(harness, "ancestor-link");
    const std::uint64_t ancestorParent = serialById(harness, "ancestor-parent");
    expect(link != 0 && parent != 0 && grandparent != 0 && order != 0 &&
        listenerCancel != 0 && ancestorLink != 0 && ancestorParent != 0,
        "bubbling: elements exist");

    ScriptResult result = harness.execute(R"JS(
var link = document.getElementById("link");
var parent = document.getElementById("parent");
var grandparent = document.getElementById("grandparent");
var order = document.getElementById("order");
var listenerCancel = document.getElementById("listener-cancel");
var ancestorLink = document.getElementById("ancestor-link");
var ancestorParent = document.getElementById("ancestor-parent");
var log = "";
var parentSaw = false;
var grandparentSaw = false;
var parentCurrent = "";
var grandparentCurrent = "";
function childCancel(event) { log = log + "c"; event.preventDefault(); }
function parentObserve(event) {
    log = log + "p";
    parentSaw = event.defaultPrevented;
    parentCurrent = event.currentTarget.id;
}
function grandparentObserve(event) {
    log = log + "g";
    grandparentSaw = event.defaultPrevented;
    grandparentCurrent = event.currentTarget.id;
}
link.addEventListener("click", childCancel);
parent.addEventListener("click", parentObserve);
grandparent.addEventListener("click", grandparentObserve);
var orderLog = "";
    order.onclick = function(event) { orderLog = orderLog + "o"; event.preventDefault(); };
    order.addEventListener("click", function(event) {
    if (event.defaultPrevented) orderLog = orderLog + "P";
    else orderLog = orderLog + "N";
});
var listenerLog = "";
listenerCancel.onclick = function(event) { listenerLog = listenerLog + "o"; };
listenerCancel.addEventListener("click", function(event) {
    listenerLog = listenerLog + "l";
    event.preventDefault();
});
var ancestorSaw = false;
var ancestorTarget = "";
var ancestorCurrent = "";
ancestorParent.onclick = function(event) {
    ancestorSaw = event.defaultPrevented;
    ancestorTarget = event.target.id;
    ancestorCurrent = event.currentTarget.id;
    event.preventDefault();
};
ancestorParent.addEventListener("click", function(event) {
    ancestorSaw = event.defaultPrevented;
});
)JS");
    expect(result.succeeded(), std::string("bubbling: setup succeeds (") +
        gxos::javascript::runtimeErrorCodeName(result.runtimeError.code) + ")");
    bool prevented = false;
    click(harness, link, error, prevented, "bubbling: child cancellation");
    expect(prevented, "bubbling: child cancellation reaches default-action boundary");
    expectString(harness, "log", "cpg", "bubbling: cancellation does not stop bubbling");
    expectBoolean(harness, "parentSaw", true,
        "bubbling: parent observes cancellation");
    expectBoolean(harness, "grandparentSaw", true,
        "bubbling: grandparent observes cancellation");
    expectString(harness, "parentCurrent", "parent",
        "bubbling: parent currentTarget remains correct");
    expectString(harness, "grandparentCurrent", "grandparent",
        "bubbling: grandparent currentTarget remains correct");

    click(harness, order, error, prevented, "ordering: onclick cancellation");
    expect(prevented, "ordering: onclick cancels default action");
    expectString(harness, "orderLog", "oP",
        "ordering: listener follows onclick and sees cancellation");
    click(harness, listenerCancel, error, prevented,
        "ordering: listener cancellation after onclick");
    expect(prevented, "ordering: listener cancels default action");
    expectString(harness, "listenerLog", "ol",
        "ordering: listener cancellation preserves onclick order");

    click(harness, ancestorLink, error, prevented,
        "ancestor: parent cancellation");
    expect(prevented, "ancestor: parent cancels descendant link default action");
    expectBoolean(harness, "ancestorSaw", true,
        "ancestor: same-node listener sees cancellation");
    expectString(harness, "ancestorTarget", "ancestor-link",
        "ancestor: original target is retained");
    expectString(harness, "ancestorCurrent", "ancestor-parent",
        "ancestor: currentTarget identifies cancelling ancestor");
}

void testPropagationIndependenceAndCombinations()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "controls");
    const std::uint64_t stop = serialById(harness, "stop-link");
    const std::uint64_t immediate = serialById(harness, "immediate-link");
    const std::uint64_t comboOne = serialById(harness, "combo-one");
    const std::uint64_t comboTwo = serialById(harness, "combo-two");
    const std::uint64_t immediateOne = serialById(harness, "combo-immediate-one");
    const std::uint64_t immediateTwo = serialById(harness, "combo-immediate-two");
    const std::uint64_t parent = serialById(harness, "parent");
    expect(stop != 0 && immediate != 0 && comboOne != 0 && comboTwo != 0 &&
        immediateOne != 0 && immediateTwo != 0 && parent != 0,
        "controls: elements exist");

    ScriptResult result = harness.execute(R"JS(
var stop = document.getElementById("stop-link");
var immediate = document.getElementById("immediate-link");
var comboOne = document.getElementById("combo-one");
var comboTwo = document.getElementById("combo-two");
var immediateOne = document.getElementById("combo-immediate-one");
var immediateTwo = document.getElementById("combo-immediate-two");
var parent = document.getElementById("parent");
var stopParentCalls = 0;
var immediateListenerCalls = 0;
var immediateParentCalls = 0;
var comboOneLog = "";
var comboOneParent = 0;
var comboTwoLog = "";
var comboTwoParent = 0;
var immediateOneListener = 0;
var immediateOneParent = 0;
var immediateTwoListener = 0;
var immediateTwoParent = 0;
    stop.addEventListener("click", function(event) { event.stopPropagation(); });
    parent.addEventListener("click", function(event) { stopParentCalls = stopParentCalls + 1; });
immediate.onclick = function(event) { event.stopImmediatePropagation(); };
immediate.addEventListener("click", function(event) { immediateListenerCalls = immediateListenerCalls + 1; });
immediateParentCalls = 0;
comboOne.onclick = function(event) {
    comboOneLog = comboOneLog + "o";
    event.preventDefault();
    event.stopPropagation();
};
comboOne.addEventListener("click", function(event) { comboOneLog = comboOneLog + "l"; });
comboTwo.onclick = function(event) {
    comboTwoLog = comboTwoLog + "o";
    event.stopPropagation();
    event.preventDefault();
};
comboTwo.addEventListener("click", function(event) { comboTwoLog = comboTwoLog + "l"; });
immediateOne.onclick = function(event) {
    event.preventDefault();
    event.stopImmediatePropagation();
};
immediateOne.addEventListener("click", function(event) { immediateOneListener = immediateOneListener + 1; });
immediateTwo.onclick = function(event) {
    event.stopImmediatePropagation();
    event.preventDefault();
};
immediateTwo.addEventListener("click", function(event) { immediateTwoListener = immediateTwoListener + 1; });
)JS");
    expect(result.succeeded(), "controls: setup succeeds");
    bool prevented = false;
    click(harness, stop, error, prevented, "controls: stopPropagation only");
    expect(!prevented, "controls: stopPropagation leaves default action enabled");
    expectNumber(harness, "stopParentCalls", 0.0,
        "controls: stopPropagation suppresses ancestors only");
    expect(harness.runtime().eventDefaultPrevented() == false,
        "controls: cancellation state is independent of propagation state");

    click(harness, immediate, error, prevented,
        "controls: stopImmediatePropagation only");
    expect(!prevented, "controls: immediate stop leaves default action enabled");
    expectNumber(harness, "immediateListenerCalls", 0.0,
        "controls: immediate stop suppresses same-node listener");

    click(harness, comboOne, error, prevented, "controls: prevent then stop");
    expect(prevented, "controls: prevent then stop cancels default action");
    expectString(harness, "comboOneLog", "ol",
        "controls: prevent then stop preserves current-node listener");
    click(harness, comboTwo, error, prevented, "controls: stop then prevent");
    expect(prevented, "controls: stop then prevent cancels default action");
    expectString(harness, "comboTwoLog", "ol",
        "controls: stop then prevent preserves current-node listener");
    click(harness, immediateOne, error, prevented,
        "controls: prevent then immediate stop");
    expect(prevented, "controls: prevent then immediate stop cancels default action");
    expectNumber(harness, "immediateOneListener", 0.0,
        "controls: prevent then immediate suppresses listener");
    click(harness, immediateTwo, error, prevented,
        "controls: immediate then prevent");
    expect(prevented, "controls: immediate then prevent cancels default action");
    expectNumber(harness, "immediateTwoListener", 0.0,
        "controls: immediate then prevent suppresses listener");
}

void testErrorsRetainedResetAndNonLink()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "safety");
    const std::uint64_t nonlink = serialById(harness, "nonlink");
    const std::uint64_t readonly = serialById(harness, "readonly-link");
    const std::uint64_t after = serialById(harness, "error-after");
    const std::uint64_t before = serialById(harness, "error-before");
    const std::uint64_t retained = serialById(harness, "retained");
    const std::uint64_t closure = serialById(harness, "closure");
    const std::uint64_t zero = serialById(harness, "zero");
    const std::uint64_t branchA = serialById(harness, "branch-a");
    const std::uint64_t branchB = serialById(harness, "branch-b");
    expect(nonlink != 0 && readonly != 0 && after != 0 && before != 0 &&
        retained != 0 && closure != 0 && zero != 0 && branchA != 0 && branchB != 0,
        "safety: elements exist");

    ScriptResult result = harness.execute(R"JS(
var nonlink = document.getElementById("nonlink");
var readonly = document.getElementById("readonly-link");
var after = document.getElementById("error-after");
var before = document.getElementById("error-before");
var retained = document.getElementById("retained");
var closure = document.getElementById("closure");
var zero = document.getElementById("zero");
var branchA = document.getElementById("branch-a");
var branchB = document.getElementById("branch-b");
var nonlinkLog = "";
var nonlinkSaw = false;
nonlink.addEventListener("click", function(event) {
    nonlinkLog = nonlinkLog + "n";
    event.preventDefault();
    event.preventDefault();
    nonlinkSaw = event.defaultPrevented;
});
    readonly.onclick = function(event) { event.defaultPrevented = true; };
var afterSaw = false;
after.addEventListener("click", function(event) {
    event.preventDefault();
    afterSaw = event.defaultPrevented;
    var x = unknownAfterPrevent;
});
var beforeSaw = false;
before.addEventListener("click", function(event) {
    var x = unknownBeforePrevent;
    event.preventDefault();
    beforeSaw = event.defaultPrevented;
});
var saved = null;
var retainFirst = true;
retained.addEventListener("click", function(event) {
    saved = event;
    if (retainFirst) { retainFirst = false; event.preventDefault(); }
});
var closureCount = 0;
var closureFirst = true;
closure.addEventListener("click", function(event) {
    closureCount = closureCount + 1;
    if (closureFirst) { closureFirst = false; event.preventDefault(); }
});
var zeroCalls = 0;
zero.onclick = function() { zeroCalls = zeroCalls + 1; };
zero.addEventListener("click", function() { zeroCalls = zeroCalls + 1; });
var branchACalls = 0;
var branchBCalls = 0;
branchA.addEventListener("click", function(event) { branchACalls = branchACalls + 1; event.preventDefault(); });
branchB.addEventListener("click", function(event) { branchBCalls = branchBCalls + 1; });
)JS");
    expect(result.succeeded(), std::string("safety: setup succeeds (") +
        gxos::javascript::runtimeErrorCodeName(result.runtimeError.code) + ")");
    bool prevented = false;
    click(harness, nonlink, error, prevented, "safety: non-link cancellation");
    expect(prevented, "safety: non-link preventDefault is safe and observable");
    expectString(harness, "nonlinkLog", "n", "safety: non-link handler runs");
    expectBoolean(harness, "nonlinkSaw", true,
        "safety: repeated non-link cancellation remains true");
    click(harness, readonly, error, prevented, "safety: assignment only");
    expect(!prevented, "safety: assignment cannot independently cancel default action");

    const bool afterDispatch = harness.dispatchClick(after, error, &prevented);
    expect(!afterDispatch && error == RuntimeErrorCode::UnknownIdentifier,
        "safety: error after preventDefault remains contained");
    expect(prevented, "safety: error after preventDefault preserves cancellation");
    expectBoolean(harness, "afterSaw", true,
        "safety: callback sees cancellation before its error");
    const bool beforeDispatch = harness.dispatchClick(before, error, &prevented);
    expect(!beforeDispatch && error == RuntimeErrorCode::UnknownIdentifier,
        "safety: error before preventDefault remains contained");
    expect(!prevented, "safety: unreachable preventDefault does not cancel");
    expectBoolean(harness, "beforeSaw", false,
        "safety: unreachable cancellation code does not run");

    bool firstPrevented = false;
    click(harness, retained, error, firstPrevented, "retained: first click");
    bool secondPrevented = true;
    click(harness, retained, error, secondPrevented, "retained: second click");
    expect(firstPrevented && !secondPrevented,
        "retained: cancellation resets for each dispatch");
    result = harness.execute("var savedState = saved.defaultPrevented;");
    expect(result.succeeded(), "retained: cached property read succeeds");
    expectBoolean(harness, "savedState", false,
        "retained: most recent dispatch state is retained safely");
    result = harness.execute("var detached = saved.preventDefault; detached();");
    expectError(result, RuntimeErrorCode::InvalidReceiver,
        "retained: detached preventDefault uses native receiver convention");
    result = harness.execute("saved.preventDefault(); savedState = saved.defaultPrevented;");
    expect(result.succeeded(), "retained: outside-dispatch call is harmless");
    expectBoolean(harness, "savedState", false,
        "retained: outside call cannot pre-cancel future state");
    bool thirdPrevented = true;
    click(harness, retained, error, thirdPrevented, "retained: third click");
    expect(!thirdPrevented, "retained: outside call does not cancel later click");

    click(harness, closure, error, prevented, "safety: closure first click");
    expect(prevented, "safety: closure first click cancels");
    click(harness, closure, error, prevented, "safety: closure second click");
    expect(!prevented, "safety: closure second click is uncancelled");
    expectNumber(harness, "closureCount", 2.0,
        "safety: closure persists across dispatches");
    click(harness, zero, error, prevented, "safety: zero-argument callbacks");
    expectNumber(harness, "zeroCalls", 2.0,
        "safety: zero-argument callback regression");
    click(harness, branchA, error, prevented, "safety: independent branch A");
    expect(prevented, "safety: branch A cancellation is local");
    click(harness, branchB, error, prevented, "safety: independent branch B");
    expect(!prevented, "safety: branch B starts uncancelled");
    expectNumber(harness, "branchACalls", 1.0,
        "safety: branch A callback count");
    expectNumber(harness, "branchBCalls", 1.0,
        "safety: branch B callback count");

    expect(harness.invalidateDocumentGeneration(error),
        "retained: generation invalidation succeeds");
    result = harness.execute("saved.preventDefault();");
    expect(result.succeeded(), "retained: stale Event method remains safe");
    result = harness.execute("saved.target.id;");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "retained: stale Event target fails closed");

    expect(harness.replaceHtml("file:///js16-new.html",
        "<html><body><button id=\"new\" type=\"button\">New</button></body></html>",
        error), "navigation: replacement document loads");
    result = harness.execute(R"JS(
var fresh = document.getElementById("new");
var freshSeen = false;
fresh.addEventListener("click", function(event) { freshSeen = !event.defaultPrevented; });
)JS");
    expect(result.succeeded(), "navigation: new document listener installs");
    const std::uint64_t fresh = serialById(harness, "new");
    click(harness, fresh, error, prevented, "navigation: fresh document click");
    expect(!prevented, "navigation: cancellation state does not leak to new document");
    expectBoolean(harness, "freshSeen", true,
        "navigation: new Event starts uncancelled");
}

std::string overflowFixture(int nestedDivCount, bool includeSafe)
{
    std::string html = "<html><body>";
    for (int index = 0; index < nestedDivCount; ++index)
        html += "<div id=\"deep" + std::to_string(index) + "\">";
    html += "<button id=\"deep-child\" type=\"button\">Deep</button>";
    for (int index = 0; index < nestedDivCount; ++index) html += "</div>";
    if (includeSafe)
        html += "<button id=\"safe\" type=\"button\">Safe</button>";
    return html + "</body></html>";
}

void testOverflowRecoveryAndBoundedStress()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness overflow;
    expect(overflow.loadHtml("file:///js16-overflow.html",
        overflowFixture(static_cast<int>(kNavigatorScriptMaxPropagationDepth - 2u),
            true), error), "overflow: fixture loads");
    expect(overflow.relayout(), "overflow: fixture relayout succeeds");
    ScriptResult result = overflow.execute(R"JS(
var deepChild = document.getElementById("deep-child");
var safe = document.getElementById("safe");
var deepCalls = 0;
var safeCalls = 0;
deepChild.addEventListener("click", function(event) {
    deepCalls = deepCalls + 1;
    event.preventDefault();
});
safe.addEventListener("click", function(event) {
    safeCalls = safeCalls + 1;
    event.preventDefault();
});
)JS");
    expect(result.succeeded(), "overflow: listeners register");
    const std::uint64_t deep = serialById(overflow, "deep-child");
    const std::uint64_t safe = serialById(overflow, "safe");
    const std::size_t objectsBefore = overflow.runtime().objectCount();
    bool prevented = true;
    const bool overflowDispatch = overflow.dispatchClick(deep, error, &prevented);
    expect(!overflowDispatch && error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "overflow: path limit remains deterministic");
    expect(!prevented, "overflow: no cancellation state before callbacks");
    expectNumber(overflow, "deepCalls", 0.0,
        "overflow: callbacks do not run before path completion");
    expect(overflow.runtime().objectCount() == objectsBefore,
        "overflow: no Event object is allocated");
    const bool safeDispatch = overflow.dispatchClick(safe, error, &prevented);
    expect(safeDispatch && prevented,
        "overflow: valid cancellation works after overflow");
    expectNumber(overflow, "safeCalls", 1.0,
        "overflow: recovery callback runs once");

    NavigatorScriptExecutionHarness stress;
    expect(stress.loadHtml("file:///js16-stress.html",
        "<html><body><button id=\"stress\" type=\"button\">Stress</button></body></html>",
        error), "stress: fixture loads");
    expect(stress.relayout(), "stress: fixture relayout succeeds");
    result = stress.execute(R"JS(
var stress = document.getElementById("stress");
var stressCalls = 0;
var stressLast = false;
stress.addEventListener("click", function(event) {
    stressCalls = stressCalls + 1;
    event.preventDefault();
    stressLast = event.defaultPrevented;
});
)JS");
    expect(result.succeeded(), "stress: listener registers");
    const std::uint64_t stressSerial = serialById(stress, "stress");
    bool stressPrevented = false;
    stress.dispatchClick(stressSerial, error, &stressPrevented);
    const std::size_t objectsAfterFirst = stress.runtime().objectCount();
    const std::size_t hostsAfterFirst = stress.runtime().hostObjectCount();
    for (int index = 1; index < 100; ++index) {
        const bool dispatched = stress.dispatchClick(stressSerial, error,
            &stressPrevented);
        expect(dispatched && stressPrevented,
            "stress: cancelled click " + std::to_string(index + 1));
    }
    expect(stress.runtime().objectCount() == objectsAfterFirst,
        "stress: Event allocation remains bounded");
    expect(stress.runtime().hostObjectCount() == hostsAfterFirst,
        "stress: Event host wrappers remain bounded");
    expect(stress.hostAdapter().clickHandlerCount() == 1u &&
        stress.hostAdapter().clickListenerCount() == 1u,
        "stress: listener table remains bounded");
    expectNumber(stress, "stressCalls", 100.0,
        "stress: callback totals remain exact");
    expectBoolean(stress, "stressLast", true,
        "stress: every dispatch observes cancellation");
}

void testListenerLifecycleAndCapacity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    std::string html = "<html><body>";
    for (int index = 0; index < 65; ++index)
        html += "<button id=\"e" + std::to_string(index) +
            "\" type=\"button\">E</button>";
    html += "<button id=\"life\" type=\"button\">Life</button></body></html>";
    expect(harness.loadHtml("file:///js16-capacity.html", html, error),
        "lifecycle: fixture loads");
    expect(harness.relayout(), "lifecycle: fixture relayout succeeds");
    ScriptResult result = harness.execute(R"JS(
var calls = 0;
function listener(event) { calls = calls + 1; }
for (var i = 0; i < 64; i = i + 1) {
    document.getElementById("e" + i).addEventListener("click", listener);
}
)JS");
    expect(result.succeeded(), "lifecycle: 64 listeners register");
    result = harness.execute(
        "document.getElementById(\"e64\").addEventListener(\"click\", listener);");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "lifecycle: listener 65 is rejected");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "lifecycle: capacity remains exact");
    const std::uint64_t first = serialById(harness, "e0");
    bool prevented = false;
    click(harness, first, error, prevented, "lifecycle: existing listener still works");
    expectNumber(harness, "calls", 1.0,
        "lifecycle: capacity rejection does not corrupt listeners");

    NavigatorScriptExecutionHarness small;
    expect(small.loadHtml("file:///js16-lifecycle.html",
        "<html><body><button id=\"life\" type=\"button\">Life</button></body></html>",
        error), "lifecycle: small fixture loads");
    expect(small.relayout(), "lifecycle: small fixture relayout succeeds");
    result = small.execute(R"JS(
var life = document.getElementById("life");
var oneCalls = 0;
var twoCalls = 0;
function one(event) { oneCalls = oneCalls + 1; event.preventDefault(); }
function two(event) { twoCalls = twoCalls + 1; }
life.addEventListener("click", one);
life.removeEventListener("click", function(event) {});
life.removeEventListener("click", one);
life.removeEventListener("click", one);
life.addEventListener("click", two);
)JS");
    expect(result.succeeded(), "lifecycle: remove/re-add setup succeeds");
    const std::uint64_t life = serialById(small, "life");
    click(small, life, error, prevented, "lifecycle: re-added listener click");
    expect(!prevented, "lifecycle: removed cancellation listener cannot cancel");
    expectNumber(small, "oneCalls", 0.0,
        "lifecycle: exact callback removal works");
    expectNumber(small, "twoCalls", 1.0,
        "lifecycle: slot reuse works");
}

} // namespace

int main()
{
    testCoreCancellationAndReadOnly();
    testBubblingOrderingAndAncestorCancellation();
    testPropagationIndependenceAndCombinations();
    testErrorsRetainedResetAndNonLink();
    testOverflowRecoveryAndBoundedStress();
    testListenerLifecycleAndCapacity();
    if (failures != 0) {
        std::cerr << failures << " JS16 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS16 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
