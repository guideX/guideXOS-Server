#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::NavigatorScriptHostLimits;
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
<div id="root">
  <div id="grandparent">
    <div id="parent">
      <div id="gap"><button id="child" type="button">Child</button></div>
    </div>
  </div>
</div>
<div id="branch-a"><button id="branch-a-child" type="button">A</button></div>
<div id="branch-b"><button id="branch-b-child" type="button">B</button></div>
<div id="error-parent"><button id="error-child" type="button">Error</button></div>
<div id="before-parent"><button id="before-child" type="button">Before</button></div>
<div id="mutation-parent"><button id="mutation-child" type="button">Mutation</button></div>
<div id="retained-parent"><button id="retained-child" type="button">Retained</button></div>
<button id="zero" type="button">Zero</button>
<button id="closure" type="button">Closure</button>
<a id="link" href="file:///js14-target.html">Link</a>
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
    expect(harness.loadHtml("file:///js14.html", kFixture, error),
        label + ": fixture loads");
    expect(harness.relayout(), label + ": initial relayout succeeds");
}

bool click(NavigatorScriptExecutionHarness& harness, std::uint64_t serial,
    RuntimeErrorCode& error, const std::string& label)
{
    const bool dispatched = harness.dispatchClick(serial, error);
    expect(dispatched, label + ": dispatch succeeds");
    if (harness.documentDirty()) expect(harness.relayout(),
        label + ": controlled relayout succeeds");
    return dispatched;
}

void testEventMethodAndCurrentNodeSemantics()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "core");
    const std::uint64_t child = serialById(harness, "child");
    const std::uint64_t parent = serialById(harness, "parent");
    const std::uint64_t grandparent = serialById(harness, "grandparent");
    const std::uint64_t root = serialById(harness, "root");
    expect(child != 0 && parent != 0 && grandparent != 0 && root != 0,
        "core: propagation elements exist");

    ScriptResult result = harness.execute(R"JS(
var child = document.getElementById("child");
var parent = document.getElementById("parent");
var grandparent = document.getElementById("grandparent");
var root = document.getElementById("root");
var order = "";
var parentCount = 0;
var grandparentCount = 0;
var rootCount = 0;
var stopTarget = "";
var stopCurrent = "";
var listenerTarget = "";
var listenerCurrent = "";
var savedEvent = null;
var savedStop = null;
var missingMember = null;
function childStop(event) {
    order = order + "c";
    stopTarget = event.target.id;
    stopCurrent = event.currentTarget.id;
    savedEvent = event;
    savedStop = event.stopPropagation;
    missingMember = event.notARealEventMember;
    event.stopPropagation();
}
function parentHandler(event) { parentCount = parentCount + 1; }
function grandparentHandler(event) { grandparentCount = grandparentCount + 1; }
function rootHandler(event) { rootCount = rootCount + 1; }
function childListener(event) {
    order = order + "l";
    listenerTarget = event.target.id;
    listenerCurrent = event.currentTarget.id;
}
child.onclick = childStop;
child.addEventListener("click", childListener);
parent.addEventListener("click", parentHandler);
grandparent.addEventListener("click", grandparentHandler);
root.addEventListener("click", rootHandler);
)JS");
    expect(result.succeeded(), "core: target stop setup succeeds");
    click(harness, child, error, "core: target listener stops bubbling");
    expectString(harness, "order", "cl", "core: target listener executes");
    expectNumber(harness, "parentCount", 0.0, "core: parent suppressed");
    expectNumber(harness, "grandparentCount", 0.0,
        "core: grandparent suppressed");
    expectNumber(harness, "rootCount", 0.0, "core: root suppressed");
    expectString(harness, "stopTarget", "child", "core: target at stop");
    expectString(harness, "stopCurrent", "child", "core: currentTarget at stop");
    expectString(harness, "listenerTarget", "child",
        "core: target remains child for second handler");
    expectString(harness, "listenerCurrent", "child",
        "core: currentTarget remains child for second handler");
    expectFunction(harness, "savedStop", "core: stopPropagation is callable");
    expectUndefined(harness, "missingMember", "core: unknown Event member remains missing");

    result = harness.execute(
        "child.removeEventListener(\"click\", childStop);"
        "child.onclick = function(event) { order = order + \"o\"; event.stopPropagation(); };"
        "child.addEventListener(\"click\", childListener);"
        "order = \"\";");
    expect(result.succeeded(), "core: onclick stop setup succeeds");
    click(harness, child, error, "core: onclick stops bubbling");
    expectString(harness, "order", "ol",
        "core: onclick still permits second current-node handler");
    expectNumber(harness, "parentCount", 0.0,
        "core: onclick keeps parent suppressed");
    expectNumber(harness, "grandparentCount", 0.0,
        "core: onclick keeps grandparent suppressed");

    result = harness.execute(
        "child.onclick = function(event) { order = order + \"o\"; };"
        "child.addEventListener(\"click\", function(event) { order = order + \"l\"; event.stopPropagation(1, 2, 3); });"
        "order = \"\";");
    expect(result.succeeded(), "core: listener-after-onclick setup succeeds");
    click(harness, child, error, "core: listener after onclick stops bubbling");
    expectString(harness, "order", "ol",
        "core: listener stop preserves onclick-before-listener order");
    expectNumber(harness, "parentCount", 0.0,
        "core: listener stop suppresses parent");

    result = harness.execute(R"JS(
function childNormal(event) { order = order + "c"; }
function parentStop(event) {
    order = order + "p";
    parentTarget = event.target.id;
    parentCurrent = event.currentTarget.id;
    parentSawCanonicalTarget = event.target === child;
    parentSawCanonicalCurrent = event.currentTarget === parent;
    event.currentTarget.textContent = "Parent";
    event.stopPropagation();
}
function grandparentUnexpected(event) { order = order + "g"; grandparentCount = grandparentCount + 1; }
function rootUnexpected(event) { order = order + "r"; rootCount = rootCount + 1; }
child.onclick = null;
child.removeEventListener("click", childListener);
child.addEventListener("click", childNormal);
parent.addEventListener("click", parentStop);
grandparent.addEventListener("click", grandparentUnexpected);
root.addEventListener("click", rootUnexpected);
var parentTarget = "";
var parentCurrent = "";
var parentSawCanonicalTarget = false;
var parentSawCanonicalCurrent = false;
order = "";
)JS");
    expect(result.succeeded(), "core: ancestor stop setup succeeds");
    const std::uint64_t revisionBeforeParentStop = harness.layoutRevision();
    click(harness, child, error, "core: parent stops higher ancestors");
    expectString(harness, "order", "cp", "core: child and parent only");
    expectString(harness, "parentTarget", "child",
        "core: parent stop retains original target");
    expectString(harness, "parentCurrent", "parent",
        "core: parent stop reports parent currentTarget");
    expectBoolean(harness, "parentSawCanonicalTarget", true,
        "core: parent stop target identity is canonical");
    expectBoolean(harness, "parentSawCanonicalCurrent", true,
        "core: parent stop currentTarget identity is canonical");
    result = harness.execute("var parentText = parent.textContent;");
    expect(result.succeeded(), "core: parent mutation readback succeeds");
    expectString(harness, "parentText", "Parent", "core: parent DOM mutation succeeds");
    expect(harness.layoutRevision() > revisionBeforeParentStop,
        "core: parent mutation advances layout revision");
    expectNumber(harness, "grandparentCount", 0.0,
        "core: parent suppresses grandparent");
    expectNumber(harness, "rootCount", 0.0,
        "core: parent suppresses root");

    result = harness.execute(R"JS(
root.onclick = function(event) { order = order + "o"; event.stopPropagation(); event.stopPropagation(); };
root.addEventListener("click", function(event) { order = order + "l"; event.stopPropagation(); });
order = "";
)JS");
    expect(result.succeeded(), "core: root repeated-stop setup succeeds");
    click(harness, root, error, "core: root stop has no later ancestor");
    expectString(harness, "order", "ol",
        "core: repeated stop calls and both root handlers are harmless");
    expectNumber(harness, "grandparentCount", 0.0,
        "core: root click does not dispatch descendants");
}

void testResetBranchesMutationAndCallbackErrors()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "reset");
    const std::uint64_t child = serialById(harness, "child");
    const std::uint64_t grandparent = serialById(harness, "grandparent");
    const std::uint64_t branchAChild = serialById(harness, "branch-a-child");
    const std::uint64_t branchBChild = serialById(harness, "branch-b-child");
    const std::uint64_t branchA = serialById(harness, "branch-a");
    const std::uint64_t branchB = serialById(harness, "branch-b");
    const std::uint64_t zero = serialById(harness, "zero");
    const std::uint64_t closure = serialById(harness, "closure");
    const std::uint64_t mutationChild = serialById(harness, "mutation-child");
    const std::uint64_t mutationParent = serialById(harness, "mutation-parent");
    expect(child != 0 && grandparent != 0 && branchAChild != 0 &&
        branchBChild != 0 && branchA != 0 && branchB != 0 && zero != 0 &&
        closure != 0 && mutationChild != 0 && mutationParent != 0,
        "reset: branch and utility elements exist");

    ScriptResult result = harness.execute(R"JS(
var child = document.getElementById("child");
var grandparent = document.getElementById("grandparent");
var stopOnce = true;
var childCalls = 0;
var grandparentCalls = 0;
var parentCount = 0;
function conditionalStop(event) {
    childCalls = childCalls + 1;
    if (stopOnce) { stopOnce = false; event.stopPropagation(); }
}
function grandHandler(event) { grandparentCalls = grandparentCalls + 1; }
child.addEventListener("click", conditionalStop);
grandparent.addEventListener("click", grandHandler);
var branchA = document.getElementById("branch-a");
var branchB = document.getElementById("branch-b");
var branchAChild = document.getElementById("branch-a-child");
var branchBChild = document.getElementById("branch-b-child");
var branchACalls = 0;
var branchBCalls = 0;
function branchAStop(event) { branchACalls = branchACalls + 1; event.stopPropagation(); }
function branchBHandler(event) { branchBCalls = branchBCalls + 1; }
branchAChild.addEventListener("click", branchAStop);
branchB.addEventListener("click", branchBHandler);
var zero = document.getElementById("zero");
var zeroOnclickCalls = 0;
var zeroListenerCalls = 0;
zero.onclick = function() { zeroOnclickCalls = zeroOnclickCalls + 1; };
zero.addEventListener("click", function() { zeroListenerCalls = zeroListenerCalls + 1; });
var closure = document.getElementById("closure");
var closureValue = 0;
function makeClosure() {
    var local = 0;
    return function(event) { local = local + 1; closureValue = local; };
}
closure.addEventListener("click", makeClosure());
var mutationChild = document.getElementById("mutation-child");
var mutationParent = document.getElementById("mutation-parent");
var mutationParentCalls = 0;
function mutationParentHandler(event) { mutationParentCalls = mutationParentCalls + 1; }
function removeAndStop(event) {
    mutationParent.removeEventListener("click", mutationParentHandler);
    event.stopPropagation();
}
mutationParent.addEventListener("click", mutationParentHandler);
mutationChild.addEventListener("click", removeAndStop);
)JS");
    expect(result.succeeded(), "reset: setup succeeds");
    click(harness, child, error, "reset: first conditional click");
    click(harness, child, error, "reset: second conditional click");
    expectNumber(harness, "childCalls", 2.0,
        "reset: child runs on both dispatches");
    expectNumber(harness, "grandparentCalls", 1.0,
        "reset: stopped state resets for second click");

    click(harness, branchAChild, error, "branches: stopped tree A click");
    click(harness, branchBChild, error, "branches: normal tree B click");
    expectNumber(harness, "branchACalls", 1.0,
        "branches: tree A child executes once");
    expectNumber(harness, "branchBCalls", 1.0,
        "branches: tree B bubbles independently");
    click(harness, zero, error, "reset: zero-argument callbacks");
    expectNumber(harness, "zeroOnclickCalls", 1.0,
        "reset: zero-argument onclick remains valid");
    expectNumber(harness, "zeroListenerCalls", 1.0,
        "reset: zero-argument listener remains valid");
    click(harness, closure, error, "reset: closure click one");
    click(harness, closure, error, "reset: closure click two");
    expectNumber(harness, "closureValue", 2.0,
        "reset: closure state persists across clicks");
    const std::size_t listenersBeforeMutation =
        harness.hostAdapter().clickListenerCount();
    click(harness, mutationChild, error, "mutation: remove ancestor and stop");
    expectNumber(harness, "mutationParentCalls", 0.0,
        "mutation: stopped dispatch requires no later ancestor lookup");
    expect(harness.hostAdapter().clickListenerCount() + 1u ==
        listenersBeforeMutation,
        "mutation: listener removal leaves table coherent");
}

void testCallbackErrorsRetainedEventAndReceiverRules()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "errors");
    const std::uint64_t errorChild = serialById(harness, "error-child");
    const std::uint64_t errorParent = serialById(harness, "error-parent");
    const std::uint64_t beforeChild = serialById(harness, "before-child");
    const std::uint64_t beforeParent = serialById(harness, "before-parent");
    const std::uint64_t retainedChild = serialById(harness, "retained-child");
    const std::uint64_t retainedParent = serialById(harness, "retained-parent");
    expect(errorChild != 0 && errorParent != 0 && beforeChild != 0 &&
        beforeParent != 0 && retainedChild != 0 && retainedParent != 0,
        "errors: callback elements exist");

    ScriptResult result = harness.execute(R"JS(
var errorChild = document.getElementById("error-child");
var errorParent = document.getElementById("error-parent");
var errorParentCalls = 0;
var saved = null;
function afterStop(event) {
    saved = event;
    event.stopPropagation();
    var x = unknownIdentifier;
}
function errorParentHandler(event) { errorParentCalls = errorParentCalls + 1; }
errorChild.addEventListener("click", afterStop);
errorParent.addEventListener("click", errorParentHandler);
var beforeChild = document.getElementById("before-child");
var beforeParent = document.getElementById("before-parent");
var beforeParentCalls = 0;
function beforeStop(event) { var x = unknownIdentifier; event.stopPropagation(); }
function beforeParentHandler(event) { beforeParentCalls = beforeParentCalls + 1; }
beforeChild.addEventListener("click", beforeStop);
beforeParent.addEventListener("click", beforeParentHandler);
var retainedChild = document.getElementById("retained-child");
var retainedParent = document.getElementById("retained-parent");
var retainedParentCalls = 0;
function retain(event) { saved = event; }
function retainedParentHandler(event) { retainedParentCalls = retainedParentCalls + 1; }
retainedChild.addEventListener("click", retain);
retainedParent.addEventListener("click", retainedParentHandler);
)JS");
    expect(result.succeeded(), "errors: setup succeeds");
    const bool afterStopDispatch = harness.dispatchClick(errorChild, error);
    expect(!afterStopDispatch && error == RuntimeErrorCode::UnknownIdentifier,
        "errors: error after stop is reported");
    expectNumber(harness, "errorParentCalls", 0.0,
        "errors: ancestor remains stopped after callback error");
    result = harness.execute(
        "errorChild.removeEventListener(\"click\", afterStop);"
        "errorChild.addEventListener(\"click\", function(event) { });");
    expect(result.succeeded(), "errors: remove failing callback succeeds");
    click(harness, errorChild, error, "errors: recovery click bubbles");
    expectNumber(harness, "errorParentCalls", 1.0,
        "errors: future click bubbles after contained error");

    const bool beforeStopDispatch = harness.dispatchClick(beforeChild, error);
    expect(!beforeStopDispatch && error == RuntimeErrorCode::UnknownIdentifier,
        "errors: error before stop is reported");
    expectNumber(harness, "beforeParentCalls", 1.0,
        "errors: error before method follows normal bubbling continuation");

    result = harness.execute("var detachedStop = saved.stopPropagation; detachedStop();");
    expectError(result, RuntimeErrorCode::InvalidReceiver,
        "receiver: detached stopPropagation uses native receiver convention");
    result = harness.execute("saved.stopPropagation();");
    expect(result.succeeded(), "retained: stop outside active dispatch is harmless");
    click(harness, retainedChild, error, "retained: first retained Event click");
    click(harness, retainedChild, error, "retained: second retained Event click");
    expectNumber(harness, "retainedParentCalls", 2.0,
        "retained: outside call cannot pre-stop a future click");

    result = harness.execute("errorChild.addEventListener(\"mouseover\", afterStop);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: unsupported event behavior remains unchanged");
    result = harness.execute("errorChild.addEventListener(\"click\", null);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: invalid callback behavior remains unchanged");
    expect(harness.invalidateDocumentGeneration(error),
        "retained: generation invalidates old Event handles");
    result = harness.execute("saved.stopPropagation();");
    expect(result.succeeded(), "retained: stale Event stop call is harmless");
    result = harness.execute("saved.target.id;");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "retained: stale Event target still fails closed");
}

std::string boundedFixture(int nestedDivCount, bool includeSafeButton = false)
{
    std::string html = "<html><body>";
    for (int index = 0; index < nestedDivCount; ++index)
        html += "<div id=\"deep" + std::to_string(index) + "\">";
    html += "<button id=\"deep-child\" type=\"button\">Deep</button>";
    for (int index = 0; index < nestedDivCount; ++index) html += "</div>";
    if (includeSafeButton)
        html += "<button id=\"safe\" type=\"button\">Safe</button>";
    return html + "</body></html>";
}

void testOverflowRecoveryAndRepeatedBoundedClicks()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness overflowHarness;
    expect(overflowHarness.loadHtml("file:///js14-overflow.html",
        boundedFixture(static_cast<int>(kNavigatorScriptMaxPropagationDepth - 2u),
            true), error), "overflow: fixture loads");
    expect(overflowHarness.relayout(), "overflow: fixture relayout succeeds");
    ScriptResult result = overflowHarness.execute(R"JS(
var deepChild = document.getElementById("deep-child");
var safe = document.getElementById("safe");
var deepCount = 0;
var safeCount = 0;
deepChild.addEventListener("click", function(event) { deepCount = deepCount + 1; event.stopPropagation(); });
safe.addEventListener("click", function(event) { safeCount = safeCount + 1; });
)JS");
    expect(result.succeeded(), "overflow: listeners register");
    const std::uint64_t deepChild = serialById(overflowHarness, "deep-child");
    const std::uint64_t safe = serialById(overflowHarness, "safe");
    const std::size_t objectsBeforeOverflow = overflowHarness.runtime().objectCount();
    const bool overflowDispatch = overflowHarness.dispatchClick(deepChild, error);
    expect(!overflowDispatch && error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "overflow: dedicated path error remains deterministic");
    expectNumber(overflowHarness, "deepCount", 0.0,
        "overflow: no callback runs before path completion");
    expect(overflowHarness.runtime().objectCount() == objectsBeforeOverflow,
        "overflow: no Event object is created");
    click(overflowHarness, safe, error, "overflow: normal click recovers");
    expectNumber(overflowHarness, "safeCount", 1.0,
        "overflow: recovery click executes normally");

    NavigatorScriptExecutionHarness nearHarness;
    expect(nearHarness.loadHtml("file:///js14-stress.html",
        "<html><body><div id=\"stress-parent\"><button id=\"stress-child\" type=\"button\">Stress</button></div></body></html>", error),
        "stress: fixture loads");
    expect(nearHarness.relayout(), "stress: fixture relayout succeeds");
    result = nearHarness.execute(R"JS(
var stressChild = document.getElementById("stress-child");
var stressParent = document.getElementById("stress-parent");
var stressChildCount = 0;
var stressParentCount = 0;
stressChild.addEventListener("click", function(event) { stressChildCount = stressChildCount + 1; event.stopPropagation(); });
stressParent.addEventListener("click", function(event) { stressParentCount = stressParentCount + 1; });
)JS");
    expect(result.succeeded(), "stress: listeners register");
    const std::uint64_t stressChild = serialById(nearHarness, "stress-child");
    click(nearHarness, stressChild, error, "stress: first stopped click");
    const std::size_t objectsAfterFirst = nearHarness.runtime().objectCount();
    const std::size_t hostObjectsAfterFirst = nearHarness.runtime().hostObjectCount();
    for (int index = 1; index < 100; ++index)
        click(nearHarness, stressChild, error,
            "stress: stopped click " + std::to_string(index + 1));
    expect(nearHarness.runtime().objectCount() == objectsAfterFirst,
        "stress: Event object count stays bounded");
    expect(nearHarness.runtime().hostObjectCount() == hostObjectsAfterFirst,
        "stress: canonical host wrapper count stays bounded");
    expect(nearHarness.hostAdapter().clickHandlerCount() == 2u &&
        nearHarness.hostAdapter().clickListenerCount() == 2u,
        "stress: listener table stays bounded");
    expectNumber(nearHarness, "stressChildCount", 100.0,
        "stress: child executes on every click");
    expectNumber(nearHarness, "stressParentCount", 0.0,
        "stress: parent remains stopped on every click");
}

void testRegistrationCapacityAndLifecycle()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptHostLimits limits;
    limits.maxClickListeners = 64u;
    NavigatorScriptExecutionHarness harness(RuntimeLimits(), limits);
    std::string html = "<html><body>";
    for (int index = 0; index < 65; ++index)
        html += "<button id=\"e" + std::to_string(index) +
            "\" type=\"button\">" + std::to_string(index) + "</button>";
    html += "</body></html>";
    expect(harness.loadHtml("file:///js14-capacity.html", html, error),
        "capacity: fixture loads");
    expect(harness.relayout(), "capacity: fixture relayout succeeds");
    ScriptResult result = harness.execute(R"JS(
var e0 = document.getElementById("e0");
var e1 = document.getElementById("e1");
var e64 = document.getElementById("e64");
var calls = 0;
function shared(event) { calls = calls + 1; }
function wrong(event) { calls = calls + 100; }
e0.addEventListener("click", shared);
e1.removeEventListener("click", shared);
e0.removeEventListener("click", wrong);
)JS");
    expect(result.succeeded(), "capacity: identity setup succeeds");
    click(harness, serialById(harness, "e0"), error,
        "capacity: initial click");
    expectNumber(harness, "calls", 1.0,
        "capacity: wrong-function and wrong-element removal regressions");
    result = harness.execute(
        "e0.removeEventListener(\"click\", shared);"
        "e0.removeEventListener(\"click\", shared);"
        "e0.addEventListener(\"click\", shared);");
    expect(result.succeeded(), "capacity: remove/re-add succeeds");
    std::string registrations;
    for (int index = 0; index < 64; ++index) {
        registrations += "var x" + std::to_string(index) +
            " = document.getElementById(\"e" + std::to_string(index) + "\");";
        registrations += "x" + std::to_string(index) +
            ".addEventListener(\"click\", shared);";
    }
    result = harness.execute(registrations);
    expect(result.succeeded(), "capacity: 64 listeners still register");
    expect(harness.hostAdapter().clickHandlerCount() == 64u &&
        harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: listener and record caps remain exact");
    result = harness.execute(
        "x0.removeEventListener(\"click\", shared);"
        "e64.addEventListener(\"click\", shared);");
    expect(result.succeeded(), "capacity: slot reuse succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: slot reuse restores capacity");

    result = harness.execute("var oldElement = e64;");
    expect(result.succeeded(), "lifecycle: old Element is retained");
    expect(harness.invalidateDocumentGeneration(error),
        "lifecycle: generation invalidates");
    expect(harness.hostAdapter().clickHandlerCount() == 0u &&
        harness.hostAdapter().clickListenerCount() == 0u,
        "lifecycle: old registrations clear on invalidation");
    result = harness.execute("oldElement.id;");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "lifecycle: old Element reference fails closed");
    result = harness.execute(
        "var oldEventCall = null;");
    expect(result.succeeded(), "lifecycle: same realm remains executable");
    const char* replacement =
        "<html><body><div id=\"new-parent\"><button id=\"new-child\" type=\"button\">New</button></div></body></html>";
    expect(harness.replaceHtml("file:///js14-new.html", replacement, error),
        "lifecycle: replacement document loads");
    result = harness.execute(R"JS(
var newChild = document.getElementById("new-child");
var newParent = document.getElementById("new-parent");
var newCount = 0;
var newTarget = "";
var newCurrent = "";
newParent.addEventListener("click", function(event) {
    newCount = newCount + 1;
    newTarget = event.target.id;
    newCurrent = event.currentTarget.id;
});
)JS");
    expect(result.succeeded(), "lifecycle: new listener registers");
    click(harness, serialById(harness, "new-child"), error,
        "lifecycle: new document click");
    expectNumber(harness, "newCount", 1.0,
        "lifecycle: new document propagation works");
    expectString(harness, "newTarget", "new-child",
        "lifecycle: new target is canonical");
    expectString(harness, "newCurrent", "new-parent",
        "lifecycle: new currentTarget is canonical");
}

} // namespace

int main()
{
    testEventMethodAndCurrentNodeSemantics();
    testResetBranchesMutationAndCallbackErrors();
    testCallbackErrorsRetainedEventAndReceiverRules();
    testOverflowRecoveryAndRepeatedBoundedClicks();
    testRegistrationCapacityAndLifecycle();
    if (failures != 0) {
        std::cerr << failures << " JS14 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS14 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
