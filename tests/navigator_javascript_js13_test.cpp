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
<div id="grandparent">
  <div id="parent">
    <div id="middle">
      <button id="child" type="button">Child</button>
    </div>
  </div>
</div>
<div id="ancestor-only">
  <div id="ancestor-gap">
    <button id="unregistered-child" type="button">Unregistered</button>
  </div>
</div>
<div id="branch-a"><button id="branch-a-child" type="button">A</button></div>
<div id="branch-b"><button id="branch-b-child" type="button">B</button></div>
<div id="mutation-ancestor">
  <div id="mutation-gap">
    <button id="mutation-child" type="button">Mutation</button>
  </div>
</div>
<div id="error-ancestor"><button id="error-child" type="button">Error</button></div>
<button id="zero-child" type="button">Zero</button>
<button id="closure-child" type="button">Closure</button>
<a id="link" href="file:///js13-target.html">Link</a>
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

void loadFixture(NavigatorScriptExecutionHarness& harness, const char* url,
    RuntimeErrorCode& error, const std::string& label)
{
    expect(harness.loadHtml(url, kFixture, error), label + ": fixture loads");
    expect(harness.relayout(), label + ": initial relayout succeeds");
}

bool click(NavigatorScriptExecutionHarness& harness, std::uint64_t serial,
    RuntimeErrorCode& error, const std::string& label)
{
    const bool dispatched = harness.dispatchClick(serial, error);
    expect(dispatched, label + ": dispatch succeeds");
    if (harness.documentDirty())
        expect(harness.relayout(), label + ": controlled relayout succeeds");
    return dispatched;
}

void testBubblingIdentityOrderAndMutation()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js13-bubbling.html", error, "bubbling");
    const std::uint64_t child = serialById(harness, "child");
    const std::uint64_t parent = serialById(harness, "parent");
    const std::uint64_t grandparent = serialById(harness, "grandparent");
    expect(child != 0 && parent != 0 && grandparent != 0,
        "bubbling: nested structural Elements exist");

    ScriptResult result = harness.execute(R"JS(
var child = document.getElementById("child");
var parent = document.getElementById("parent");
var grandparent = document.getElementById("grandparent");
var order = "";
var childTarget = "";
var childCurrent = "";
var parentTarget = "";
var parentCurrent = "";
var grandTarget = "";
var grandCurrent = "";
var childType = "";
var childIdentity = false;
var parentIdentity = false;
var grandIdentity = false;
var parentDiverges = false;
var grandDiverges = false;
var childListenerSawChild = false;
var savedEvent = null;
function childOnclick(event) {
    order = order + "child-onclick>";
    childTarget = event.target.id;
    childCurrent = event.currentTarget.id;
    childType = event.type;
    childIdentity = event.target === child && event.currentTarget === child;
    savedEvent = event;
    event.target.textContent = "clicked";
}
function childListener(event) {
    order = order + "child-listener>";
    childListenerSawChild = event.target === child && event.currentTarget === child;
}
function parentOnclick(event) {
    order = order + "parent-onclick>";
    parentTarget = event.target.id;
    parentCurrent = event.currentTarget.id;
    parentIdentity = event.target === child && event.currentTarget === parent;
    parentDiverges = event.target !== event.currentTarget;
}
function parentListener(event) {
    order = order + "parent-listener>";
    parentTarget = event.target.id;
    parentCurrent = event.currentTarget.id;
}
function grandOnclick(event) {
    order = order + "grand-onclick>";
    grandTarget = event.target.id;
    grandCurrent = event.currentTarget.id;
    grandIdentity = event.target === child && event.currentTarget === grandparent;
    grandDiverges = event.target !== event.currentTarget;
}
function grandListener(event) {
    order = order + "grand-listener>";
    grandTarget = event.target.id;
    grandCurrent = event.currentTarget.id;
}
child.onclick = childOnclick;
child.addEventListener("click", childListener);
parent.onclick = parentOnclick;
parent.addEventListener("click", parentListener);
grandparent.onclick = grandOnclick;
grandparent.addEventListener("click", grandListener);
)JS");
    expect(result.succeeded(), "bubbling: all node handlers register");
    const std::size_t objectsBeforeClick = harness.runtime().objectCount();
    const std::uint64_t revisionBeforeClick = harness.layoutRevision();
    click(harness, child, error, "bubbling: authentic target click");
    expectString(harness, "order",
        "child-onclick>child-listener>parent-onclick>parent-listener>grand-onclick>grand-listener>",
        "bubbling: child to parent to grandparent order");
    expectString(harness, "childType", "click", "bubbling: Event type");
    expectString(harness, "childTarget", "child", "bubbling: child target");
    expectString(harness, "parentTarget", "child", "bubbling: parent target remains child");
    expectString(harness, "grandTarget", "child", "bubbling: grandparent target remains child");
    expectString(harness, "childCurrent", "child", "bubbling: child currentTarget");
    expectString(harness, "parentCurrent", "parent", "bubbling: parent currentTarget");
    expectString(harness, "grandCurrent", "grandparent",
        "bubbling: grandparent currentTarget");
    expectBoolean(harness, "childIdentity", true,
        "bubbling: child canonical target/currentTarget identity");
    expectBoolean(harness, "childListenerSawChild", true,
        "bubbling: child listener receives canonical wrappers");
    expectBoolean(harness, "parentIdentity", true,
        "bubbling: parent target/currentTarget identity");
    expectBoolean(harness, "grandIdentity", true,
        "bubbling: grandparent target/currentTarget identity");
    expectBoolean(harness, "parentDiverges", true,
        "bubbling: parent target differs from currentTarget");
    expectBoolean(harness, "grandDiverges", true,
        "bubbling: grandparent target differs from currentTarget");
    expect(harness.runtime().objectCount() == objectsBeforeClick + 1u,
        "bubbling: one cached Event object is created");
    expect(harness.layoutRevision() > revisionBeforeClick,
        "bubbling: target mutation advances layout revision");
    expect(harness.documentDirty() == false,
        "bubbling: relayout settles after target mutation");
    result = harness.execute(
        "var retainedAfterBubble = savedEvent.currentTarget.id;"
        "var retainedTargetAfterBubble = savedEvent.target.id;"
        "var retainedSameChild = savedEvent.target === child;");
    expect(result.succeeded(), "bubbling: retained Event reads after dispatch");
    expectString(harness, "retainedAfterBubble", "grandparent",
        "bubbling: retained Event currentTarget is final node");
    expectString(harness, "retainedTargetAfterBubble", "child",
        "bubbling: retained Event target stays original child");
    expectBoolean(harness, "retainedSameChild", true,
        "bubbling: retained Event target keeps canonical identity");
    click(harness, child, error, "bubbling: repeated target click");
    expect(harness.runtime().objectCount() == objectsBeforeClick + 1u,
        "bubbling: repeated bubbling reuses Event object");
}

void testAncestorOnlyBranchesAndListenerMutation()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js13-ancestors.html", error, "ancestors");
    const std::uint64_t unregisteredChild = serialById(harness, "unregistered-child");
    const std::uint64_t ancestorOnly = serialById(harness, "ancestor-only");
    const std::uint64_t branchAChild = serialById(harness, "branch-a-child");
    const std::uint64_t branchBChild = serialById(harness, "branch-b-child");
    const std::uint64_t mutationChild = serialById(harness, "mutation-child");
    expect(unregisteredChild != 0 && ancestorOnly != 0 && branchAChild != 0 &&
        branchBChild != 0 && mutationChild != 0,
        "ancestors: all independent and gap nodes exist");

    ScriptResult result = harness.execute(R"JS(
var ancestorOnly = document.getElementById("ancestor-only");
var unregisteredChild = document.getElementById("unregistered-child");
var ancestorOnlyTarget = "";
var ancestorOnlyCurrent = "";
var ancestorOnlySawChild = false;
function ancestorOnlyHandler(event) {
    ancestorOnlyTarget = event.target.id;
    ancestorOnlyCurrent = event.currentTarget.id;
    ancestorOnlySawChild = event.target === unregisteredChild &&
        event.currentTarget === ancestorOnly;
}
ancestorOnly.addEventListener("click", ancestorOnlyHandler);

var branchA = document.getElementById("branch-a");
var branchB = document.getElementById("branch-b");
var branchAChild = document.getElementById("branch-a-child");
var branchBChild = document.getElementById("branch-b-child");
var branchACount = 0;
var branchBCount = 0;
var branchATarget = "";
var branchBTarget = "";
function branchAHandler(event) { branchACount = branchACount + 1; branchATarget = event.target.id; }
function branchBHandler(event) { branchBCount = branchBCount + 1; branchBTarget = event.target.id; }
branchA.addEventListener("click", branchAHandler);
branchB.addEventListener("click", branchBHandler);

var mutationAncestor = document.getElementById("mutation-ancestor");
var mutationChild = document.getElementById("mutation-child");
var mutationCount = 0;
var replacementCount = 0;
function mutationAncestorListener(event) { mutationCount = mutationCount + 1; }
function mutationAncestorReplacement(event) {
    replacementCount = replacementCount + 1;
    replacementTarget = event.target.id;
    replacementCurrent = event.currentTarget.id;
}
function mutationChildHandler(event) {
    mutationAncestor.removeEventListener("click", mutationAncestorListener);
}
var replacementTarget = "";
var replacementCurrent = "";
mutationAncestor.addEventListener("click", mutationAncestorListener);
mutationChild.addEventListener("click", mutationChildHandler);
)JS");
    expect(result.succeeded(), "ancestors: ancestor and branch setup succeeds");
    click(harness, unregisteredChild, error,
        "ancestors: child without handler reaches ancestor-only listener");
    expectString(harness, "ancestorOnlyTarget", "unregistered-child",
        "ancestors: parent-only target is original child");
    expectString(harness, "ancestorOnlyCurrent", "ancestor-only",
        "ancestors: parent-only currentTarget is ancestor");
    expectBoolean(harness, "ancestorOnlySawChild", true,
        "ancestors: handlerless gap does not terminate propagation");
    click(harness, branchAChild, error, "ancestors: branch A click");
    expectNumber(harness, "branchACount", 1.0,
        "ancestors: branch A handler runs");
    expectNumber(harness, "branchBCount", 0.0,
        "ancestors: unrelated branch B remains isolated");
    expectString(harness, "branchATarget", "branch-a-child",
        "ancestors: branch A target is local child");
    click(harness, branchBChild, error, "ancestors: branch B click");
    expectNumber(harness, "branchACount", 1.0,
        "ancestors: branch A is not dispatched from branch B");
    expectNumber(harness, "branchBCount", 1.0,
        "ancestors: branch B handler runs independently");
    expectString(harness, "branchBTarget", "branch-b-child",
        "ancestors: branch B target is local child");

    click(harness, mutationChild, error,
        "ancestors: child removes later ancestor listener");
    expectNumber(harness, "mutationCount", 0.0,
        "ancestors: removed later listener does not fire");
    result = harness.execute(
        "mutationAncestor.addEventListener(\"click\", mutationAncestorReplacement);");
    expect(result.succeeded(), "ancestors: replacement listener registers");
    click(harness, mutationChild, error,
        "ancestors: replacement listener click");
    expectNumber(harness, "replacementCount", 1.0,
        "ancestors: current later listener is used at node arrival");
    expectString(harness, "replacementTarget", "mutation-child",
        "ancestors: replacement listener target remains child");
    expectString(harness, "replacementCurrent", "mutation-ancestor",
        "ancestors: replacement listener currentTarget is ancestor");
}

void testErrorClosureZeroArgumentAndRetainedEvent()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js13-errors.html", error, "errors");
    const std::uint64_t errorChild = serialById(harness, "error-child");
    const std::uint64_t zeroChild = serialById(harness, "zero-child");
    const std::uint64_t closureChild = serialById(harness, "closure-child");
    const std::uint64_t branchBChild = serialById(harness, "branch-b-child");
    expect(errorChild != 0 && zeroChild != 0 && closureChild != 0 && branchBChild != 0,
        "errors: callback test elements exist");

    ScriptResult result = harness.execute(R"JS(
var errorChild = document.getElementById("error-child");
var errorAncestor = document.getElementById("error-ancestor");
var errorSaved = null;
var errorAncestorCount = 0;
function bad(event) { errorSaved = event; unknownIdentifier(); }
function errorAncestorHandler(event) {
    errorAncestorCount = errorAncestorCount + 1;
    errorAncestorTarget = event.target.id;
    errorAncestorCurrent = event.currentTarget.id;
}
var errorAncestorTarget = "";
var errorAncestorCurrent = "";
errorChild.addEventListener("click", bad);
errorAncestor.addEventListener("click", errorAncestorHandler);

var zeroChild = document.getElementById("zero-child");
var zeroOnclickCount = 0;
var zeroListenerCount = 0;
zeroChild.onclick = function () { zeroOnclickCount = zeroOnclickCount + 1; };
zeroChild.addEventListener("click", function () { zeroListenerCount = zeroListenerCount + 1; });

var closureChild = document.getElementById("closure-child");
var closureValue = 0;
var closureTarget = "";
function makeClosure() {
    var local = 0;
    return function (event) {
        local = local + 1;
        closureValue = local;
        closureTarget = event.target.id;
    };
}
closureChild.addEventListener("click", makeClosure());
)JS");
    expect(result.succeeded(), "errors: error/zero/closure setup succeeds");
    const bool errorDispatch = harness.dispatchClick(errorChild, error);
    expect(!errorDispatch && error == RuntimeErrorCode::UnknownIdentifier,
        "errors: child runtime error is reported and contained");
    expectNumber(harness, "errorAncestorCount", 1.0,
        "errors: bubbling continues to ancestor after child error");
    expectString(harness, "errorAncestorTarget", "error-child",
        "errors: ancestor after error sees original target");
    expectString(harness, "errorAncestorCurrent", "error-ancestor",
        "errors: ancestor after error sees currentTarget");
    result = harness.execute("errorChild.removeEventListener(\"click\", bad);");
    expect(result.succeeded(), "errors: removing failing callback succeeds");
    click(harness, errorChild, error, "errors: post-error click remains healthy");
    expectNumber(harness, "errorAncestorCount", 2.0,
        "errors: subsequent click still bubbles after error");
    click(harness, zeroChild, error, "errors: zero-argument callbacks");
    expectNumber(harness, "zeroOnclickCount", 1.0,
        "errors: zero-argument onclick remains valid");
    expectNumber(harness, "zeroListenerCount", 1.0,
        "errors: zero-argument listener remains valid");
    click(harness, closureChild, error, "errors: closure click one");
    click(harness, closureChild, error, "errors: closure click two");
    expectNumber(harness, "closureValue", 2.0,
        "errors: closure state persists across bubbling clicks");
    expectString(harness, "closureTarget", "closure-child",
        "errors: closure Event argument remains valid");
    expect(harness.runtime().hostObjectCount() > 0u,
        "errors: canonical host wrappers remain bounded and live");
    result = harness.execute(
        "var retainedErrorType = errorSaved.type;"
        "var retainedErrorTarget = errorSaved.target.id;"
        "var retainedErrorSafe = retainedErrorType === \"click\" && retainedErrorTarget === \"closure-child\";");
    expect(result.succeeded(), "errors: retained Event remains readable before invalidation");
    expectString(harness, "retainedErrorType", "click",
        "errors: retained Event type remains click");
    expectString(harness, "retainedErrorTarget", "closure-child",
        "errors: retained Event target reflects later cached-event reuse");
    expectBoolean(harness, "retainedErrorSafe", true,
        "errors: retained Event fields remain deterministic");
}

std::string boundedFixture(int nestedDivCount)
{
    std::string html = "<html><body>";
    for (int index = 0; index < nestedDivCount; ++index) {
        html += "<div id=\"deep" + std::to_string(index) + "\">";
    }
    html += "<button id=\"deep-child\" type=\"button\">Deep</button>";
    for (int index = 0; index < nestedDivCount; ++index) html += "</div>";
    html += "</body></html>";
    return html;
}

void testDeepHierarchyAndOverflow()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string fixture = boundedFixture(12);
    expect(harness.loadHtml("file:///js13-deep.html", fixture, error),
        "deep: fixture loads");
    expect(harness.relayout(), "deep: initial relayout succeeds");
    std::string source =
        "var deepChild = document.getElementById(\"deep-child\");"
        "var deepOrder = \"\"; var deepTarget = \"\";"
        "function deepHandler(event) { deepOrder = deepOrder + event.currentTarget.id + \",\"; deepTarget = event.target.id; }";
    source += "deepChild.addEventListener(\"click\", deepHandler);";
    for (int index = 0; index < 12; ++index) {
        source += "var deep" + std::to_string(index) +
            " = document.getElementById(\"deep" + std::to_string(index) + "\");";
        source += "deep" + std::to_string(index) +
            ".addEventListener(\"click\", deepHandler);";
    }
    ScriptResult result = harness.execute(source);
    expect(result.succeeded(), "deep: all nested listeners register");
    const std::uint64_t deepChild = serialById(harness, "deep-child");
    click(harness, deepChild, error, "deep: nested bubbling click");
    std::string expectedOrder = "deep-child,";
    for (int index = 11; index >= 0; --index)
        expectedOrder += "deep" + std::to_string(index) + ",";
    expectString(harness, "deepOrder", expectedOrder,
        "deep: target-to-root order has no skipped ancestors");
    expectString(harness, "deepTarget", "deep-child",
        "deep: target remains deepest child");

    NavigatorScriptExecutionHarness nearHarness;
    expect(nearHarness.loadHtml("file:///js13-depth-bound.html",
        boundedFixture(static_cast<int>(kNavigatorScriptMaxPropagationDepth - 3u)), error),
        "depth: maximum-sized path fixture loads");
    expect(nearHarness.relayout(), "depth: maximum-sized path relayout succeeds");
    result = nearHarness.execute(
        "var deepChild = document.getElementById(\"deep-child\");"
        "var nearCount = 0;"
        "deepChild.addEventListener(\"click\", function(event) { nearCount = nearCount + 1; });");
    expect(result.succeeded(), "depth: maximum-sized path listener registers");
    const std::uint64_t nearChild = serialById(nearHarness, "deep-child");
    click(nearHarness, nearChild, error, "depth: maximum-sized path dispatch");
    expectNumber(nearHarness, "nearCount", 1.0,
        "depth: path at documented bound dispatches safely");

    NavigatorScriptExecutionHarness overflowHarness;
    expect(overflowHarness.loadHtml("file:///js13-depth-overflow.html",
        boundedFixture(static_cast<int>(kNavigatorScriptMaxPropagationDepth - 2u)), error),
        "depth: overflow fixture loads");
    expect(overflowHarness.relayout(), "depth: overflow fixture relayout succeeds");
    result = overflowHarness.execute(
        "var deepChild = document.getElementById(\"deep-child\");"
        "var overflowCount = 0;"
        "deepChild.addEventListener(\"click\", function(event) { overflowCount = overflowCount + 1; });");
    expect(result.succeeded(), "depth: overflow listener registers");
    const std::uint64_t overflowChild = serialById(overflowHarness, "deep-child");
    const std::size_t objectsBeforeOverflow = overflowHarness.runtime().objectCount();
    const bool overflowDispatch = overflowHarness.dispatchClick(overflowChild, error);
    expect(!overflowDispatch && error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "depth: overflow fails with dedicated bounded-path error");
    expectNumber(overflowHarness, "overflowCount", 0.0,
        "depth: overflow executes no callback");
    expect(overflowHarness.runtime().objectCount() == objectsBeforeOverflow,
        "depth: overflow creates no Event object or retained path state");
}

void testRemovalCapacityAndRepeatedBubbling()
{
    NavigatorScriptHostLimits limits;
    limits.maxClickListeners = 64u;
    NavigatorScriptExecutionHarness harness(RuntimeLimits(), limits);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string fixture = [] {
        std::string html = "<html><body>";
        for (int index = 0; index < 65; ++index) {
            html += "<button id=\"e" + std::to_string(index) +
                "\" type=\"button\">" + std::to_string(index) + "</button>";
        }
        return html + "</body></html>";
    }();
    expect(harness.loadHtml("file:///js13-capacity.html", fixture, error),
        "capacity: fixture loads");
    expect(harness.relayout(), "capacity: initial relayout succeeds");
    ScriptResult result = harness.execute(R"JS(
var e0 = document.getElementById("e0");
var e1 = document.getElementById("e1");
var e64 = document.getElementById("e64");
var calls = 0;
var lastTarget = "";
function shared(event) { calls = calls + 1; lastTarget = event.target.id; }
function wrong(event) { calls = calls + 100; }
e0.addEventListener("click", shared);
e1.removeEventListener("click", shared);
e0.removeEventListener("click", wrong);
)JS");
    expect(result.succeeded(), "capacity: wrong-function and wrong-element setup succeeds");
    click(harness, serialById(harness, "e0"), error,
        "capacity: initial listener click");
    expectNumber(harness, "calls", 1.0,
        "capacity: wrong-function/element removal preserves listener");
    expectString(harness, "lastTarget", "e0",
        "capacity: Event target is clicked element");
    result = harness.execute(
        "e0.removeEventListener(\"click\", shared);"
        "e0.removeEventListener(\"click\", shared);"
        "e0.addEventListener(\"click\", shared);");
    expect(result.succeeded(), "capacity: repeated remove and re-add succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "capacity: listener slot is reused");

    std::string registrations = "";
    for (int index = 0; index < 64; ++index) {
        registrations += "var x" + std::to_string(index) +
            " = document.getElementById(\"e" + std::to_string(index) + "\");";
        registrations += "x" + std::to_string(index) +
            ".addEventListener(\"click\", shared);";
    }
    result = harness.execute(registrations);
    expect(result.succeeded(), "capacity: 64 listener registrations succeed");
    expect(harness.hostAdapter().clickListenerCount() == 64u &&
        harness.hostAdapter().clickHandlerCount() == 64u,
        "capacity: listener table remains exactly 64 records");
    result = harness.execute(
        "x0.removeEventListener(\"click\", shared);"
        "e64.addEventListener(\"click\", shared);");
    expect(result.succeeded(), "capacity: released slot can be reused at e64");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: slot reuse restores exactly 64 listeners");
    const std::size_t objectsBeforeStress = harness.runtime().objectCount();
    const std::size_t hostObjectsBeforeStress = harness.runtime().hostObjectCount();
    for (int index = 0; index < 100; ++index)
        click(harness, serialById(harness, "e64"), error,
            "capacity: repeated bubbling click " + std::to_string(index));
    expect(harness.runtime().objectCount() == objectsBeforeStress,
        "capacity: repeated bubbling does not grow Event storage");
    expect(harness.runtime().hostObjectCount() == hostObjectsBeforeStress,
        "capacity: repeated bubbling does not grow host wrappers");
    expect(harness.hostAdapter().clickHandlerCount() == 64u &&
        harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: repeated bubbling does not grow listener table");
    expectNumber(harness, "calls", 101.0,
        "capacity: all repeated clicks remain dispatchable");
    expectString(harness, "lastTarget", "e64",
        "capacity: repeated bubbling target remains e64");
}

void testNavigationCleanupAndStaleReferences()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js13-generation-a.html", error, "generation A");
    const std::uint64_t child = serialById(harness, "child");
    ScriptResult result = harness.execute(
        "var oldChild = document.getElementById(\"child\");"
        "var oldParent = document.getElementById(\"parent\");"
        "var oldCount = 0; var oldSaved = null;"
        "function oldHandler(event) { oldCount = oldCount + 1; oldSaved = event; }"
        "oldParent.addEventListener(\"click\", oldHandler);");
    expect(result.succeeded(), "generation: old nested listener registers");
    click(harness, child, error, "generation: old nested bubbling click");
    expectNumber(harness, "oldCount", 1.0,
        "generation: old ancestor receives old child click");
    expect(harness.invalidateDocumentGeneration(error),
        "generation: host generation invalidates");
    expect(harness.hostAdapter().clickHandlerCount() == 0u &&
        harness.hostAdapter().clickListenerCount() == 0u,
        "generation: invalidation clears old nested listener state");
    result = harness.execute("oldSaved.target.id;");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "generation: retained old Event target fails closed");
    const char* replacement =
        "<html><body><div id=\"new-parent\"><button id=\"new-child\" type=\"button\">New</button></div></body></html>";
    expect(harness.replaceHtml("file:///js13-generation-b.html", replacement,
        error), "generation: replacement nested document loads");
    expect(harness.hostAdapter().clickHandlerCount() == 0u,
        "generation: replacement has no old handlers");
    result = harness.execute(
        "var newChild = document.getElementById(\"new-child\");"
        "var newParent = document.getElementById(\"new-parent\");"
        "var newCount = 0; var newTarget = \"\"; var newCurrent = \"\";"
        "newParent.addEventListener(\"click\", function(event) { newCount = newCount + 1; newTarget = event.target.id; newCurrent = event.currentTarget.id; });");
    expect(result.succeeded(), "generation: new nested listener registers");
    click(harness, serialById(harness, "new-child"), error,
        "generation: new nested bubbling click");
    expectNumber(harness, "newCount", 1.0,
        "generation: new parent receives new child click");
    expectString(harness, "newTarget", "new-child",
        "generation: new target is not stale old child");
    expectString(harness, "newCurrent", "new-parent",
        "generation: new currentTarget is new parent");
}

} // namespace

int main()
{
    testBubblingIdentityOrderAndMutation();
    testAncestorOnlyBranchesAndListenerMutation();
    testErrorClosureZeroArgumentAndRetainedEvent();
    testDeepHierarchyAndOverflow();
    testRemovalCapacityAndRepeatedBubbling();
    testNavigationCleanupAndStaleReferences();
    if (failures != 0) {
        std::cerr << failures << " JS13 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS13 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
