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
<div id="grandparent">
  <div id="parent">
    <button id="child" type="button">Child</button>
    <a id="link" href="file:///js17-target.html">Link</a>
    <button id="stop" type="button">Stop</button>
    <button id="immediate" type="button">Immediate</button>
    <button id="onclick-stop" type="button">Onclick stop</button>
    <button id="onclick-immediate" type="button">Onclick immediate</button>
    <button id="remove-later" type="button">Remove later</button>
    <button id="add-later" type="button">Add later</button>
    <button id="readd" type="button">Re-add</button>
    <button id="self" type="button">Self</button>
    <button id="onclick-mutation" type="button">Onclick mutation</button>
    <button id="error" type="button">Error</button>
    <button id="error-immediate" type="button">Error immediate</button>
    <button id="stress" type="button">Stress</button>
  </div>
</div>
<button id="identity" type="button">Identity</button>
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

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const std::string& label)
{
    expect(harness.loadHtml("file:///js17.html", kFixture, error),
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

void testOrderingIdentityAndRemoval()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "ordering");
    ScriptResult result = harness.execute(R"JS(
var child = document.getElementById("child");
var parent = document.getElementById("parent");
var grandparent = document.getElementById("grandparent");
var identity = document.getElementById("identity");
var order = "";
var targetStable = true;
var childCurrentStable = true;
function a(event) {
    order = order + "a";
    targetStable = targetStable && event.target === child;
    childCurrentStable = childCurrentStable && event.currentTarget === child;
}
function b(event) {
    order = order + "b";
    targetStable = targetStable && event.target === child;
    childCurrentStable = childCurrentStable && event.currentTarget === child;
}
function c(event) { order = order + "c"; }
child.onclick = function(event) { order = order + "o"; };
child.addEventListener("click", a);
child.addEventListener("click", b);
child.addEventListener("click", c);
child.addEventListener("click", a);
parent.addEventListener("click", function(event) {
    order = order + "p";
    targetStable = targetStable && event.target === child;
    childCurrentStable = childCurrentStable && event.currentTarget === parent;
});
grandparent.addEventListener("click", function(event) {
    order = order + "g";
    targetStable = targetStable && event.target === child;
});
function first(event) { order = order + "x"; }
function second(event) { order = order + "x"; }
identity.addEventListener("click", first);
identity.addEventListener("click", second);
)JS");
    expect(result.succeeded(), "ordering: setup succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 7u,
        "ordering: duplicate exact callback consumes no slot");
    expect(harness.hostAdapter().clickHandlerCount() == 4u,
        "ordering: four Elements are represented");

    bool prevented = false;
    expect(click(harness, "child", error, &prevented, "ordering: first click"),
        "ordering: first dispatch succeeds");
    expectString(harness, "order", "oabcpg", "ordering: onclick then listeners then ancestors");
    expectBoolean(harness, "targetStable", true, "ordering: target is stable");
    expectBoolean(harness, "childCurrentStable", true,
        "ordering: currentTarget is per-node and canonical");

    result = harness.execute("child.removeEventListener(\"click\", b);");
    expect(result.succeeded(), "ordering: middle removal succeeds");
    click(harness, "child", error, &prevented, "ordering: remove middle");
    expectString(harness, "order", "oabcpgoacpg",
        "ordering: removing middle preserves surrounding listeners");
    result = harness.execute("child.addEventListener(\"click\", b);");
    expect(result.succeeded(), "ordering: re-add succeeds");
    click(harness, "child", error, &prevented, "ordering: re-add");
    expectString(harness, "order", "oabcpgoacpgoacbpg",
        "ordering: remove/re-add appends");

    result = harness.execute(
        "child.removeEventListener(\"click\", function(event) {});"
        "child.removeEventListener(\"click\", b);"
        "child.removeEventListener(\"click\", b);");
    expect(result.succeeded(), "ordering: wrong and repeated removal is harmless");
    click(harness, "identity", error, &prevented, "identity: both functions");
    expectString(harness, "order", "oabcpgoacpgoacbpgxx",
        "identity: different function objects with identical code coexist");
    result = harness.execute("identity.removeEventListener(\"click\", first);");
    expect(result.succeeded(), "identity: exact removal succeeds");
    click(harness, "identity", error, &prevented, "identity: remove first");
    expectString(harness, "order", "oabcpgoacpgoacbpgxxx",
        "identity: removing first leaves second");

    result = harness.execute("child.onclick = null;");
    expect(result.succeeded(), "ordering: clearing onclick succeeds");
    click(harness, "child", error, &prevented, "ordering: listener survives onclick clear");
    expectString(harness, "order", "oabcpgoacpgoacbpgxxxacpg",
        "ordering: onclick remains independent from listeners");
}

void testPropagationAndCancellation()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "controls");
    ScriptResult result = harness.execute(R"JS(
var stop = document.getElementById("stop");
var immediate = document.getElementById("immediate");
var onclickStop = document.getElementById("onclick-stop");
var onclickImmediate = document.getElementById("onclick-immediate");
var link = document.getElementById("link");
var parent = document.getElementById("parent");
var log = "";
var sawDefault = false;
stop.addEventListener("click", function(event) { log = log + "a"; event.stopPropagation(); });
stop.addEventListener("click", function(event) { log = log + "b"; });
stop.addEventListener("click", function(event) { log = log + "c"; });
parent.addEventListener("click", function(event) { log = log + "p"; });
immediate.addEventListener("click", function(event) { log = log + "a"; event.stopImmediatePropagation(); });
immediate.addEventListener("click", function(event) { log = log + "b"; });
onclickStop.onclick = function(event) { log = log + "o"; event.stopPropagation(); };
onclickStop.addEventListener("click", function(event) { log = log + "a"; });
onclickStop.addEventListener("click", function(event) { log = log + "b"; });
onclickImmediate.onclick = function(event) { log = log + "o"; event.stopImmediatePropagation(); };
onclickImmediate.addEventListener("click", function(event) { log = log + "a"; });
link.addEventListener("click", function(event) { log = log + "a"; event.preventDefault(); });
link.addEventListener("click", function(event) {
    log = log + "b";
    sawDefault = event.defaultPrevented;
});
)JS");
    expect(result.succeeded(), "controls: setup succeeds");
    bool prevented = false;
    click(harness, "stop", error, &prevented, "controls: stopPropagation");
    expectString(harness, "log", "abc", "controls: stopPropagation allows same-node listeners");
    click(harness, "immediate", error, &prevented, "controls: immediate stop");
    expectString(harness, "log", "abca", "controls: immediate stop skips later same-node listeners and parent");
    click(harness, "onclick-stop", error, &prevented, "controls: onclick stop");
    expectString(harness, "log", "abcaoab",
        "controls: onclick stop allows registered listeners");
    click(harness, "onclick-immediate", error, &prevented,
        "controls: onclick immediate stop");
    expectString(harness, "log", "abcaoabo",
        "controls: onclick immediate stop skips registered listeners");
    click(harness, "link", error, &prevented, "controls: preventDefault");
    expect(prevented, "controls: preventDefault cancels the link default action");
    expectBoolean(harness, "sawDefault", true,
        "controls: later listener sees defaultPrevented");
    expectString(harness, "log", "abcaoaboabp",
        "controls: preventDefault permits later listeners and bubbling");
}

void testMutationDuringDispatch()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "mutation");
    ScriptResult result = harness.execute(R"JS(
var removeLater = document.getElementById("remove-later");
var addLater = document.getElementById("add-later");
var readd = document.getElementById("readd");
var self = document.getElementById("self");
var onclickMutation = document.getElementById("onclick-mutation");
var removeLog = "";
var addLog = "";
var readdLog = "";
var selfLog = "";
var onclickLog = "";
function removed(event) { removeLog = removeLog + "b"; }
removeLater.addEventListener("click", function(event) {
    removeLog = removeLog + "a";
    removeLater.removeEventListener("click", removed);
});
removeLater.addEventListener("click", removed);
function later(event) { addLog = addLog + "c"; }
addLater.addEventListener("click", function(event) {
    addLog = addLog + "a";
    addLater.addEventListener("click", later);
});
addLater.addEventListener("click", function(event) { addLog = addLog + "b"; });
function readded(event) { readdLog = readdLog + "b"; }
var readdFirst = true;
readd.addEventListener("click", function(event) {
    readdLog = readdLog + "a";
    if (readdFirst) {
        readdFirst = false;
        readd.removeEventListener("click", readded);
        readd.addEventListener("click", readded);
    }
});
readd.addEventListener("click", readded);
function onceByHand(event) {
    selfLog = selfLog + "s";
    self.removeEventListener("click", onceByHand);
}
self.addEventListener("click", onceByHand);
self.addEventListener("click", function(event) { selfLog = selfLog + "2"; });
self.addEventListener("click", function(event) { selfLog = selfLog + "3"; });
function onclickLater(event) { onclickLog = onclickLog + "c"; }
onclickMutation.onclick = function(event) {
    onclickLog = onclickLog + "o";
    onclickMutation.removeEventListener("click", onclickRemoved);
    onclickMutation.addEventListener("click", onclickLater);
};
function onclickRemoved(event) { onclickLog = onclickLog + "b"; }
function onclickFirst(event) { onclickLog = onclickLog + "a"; }
onclickMutation.addEventListener("click", onclickFirst);
onclickMutation.addEventListener("click", onclickRemoved);
)JS");
    expect(result.succeeded(), "mutation: setup succeeds");
    bool prevented = false;
    click(harness, "remove-later", error, &prevented, "mutation: remove later first");
    click(harness, "remove-later", error, &prevented, "mutation: remove later second");
    expectString(harness, "removeLog", "aa", "mutation: removed later listener never runs");
    click(harness, "add-later", error, &prevented, "mutation: add later first");
    click(harness, "add-later", error, &prevented, "mutation: add later second");
    expectString(harness, "addLog", "ababc", "mutation: addition waits for next dispatch");
    click(harness, "readd", error, &prevented, "mutation: re-add first");
    expectString(harness, "readdLog", "a", "mutation: re-added identity is not current snapshot");
    click(harness, "readd", error, &prevented, "mutation: re-add second");
    expectString(harness, "readdLog", "aab", "mutation: re-added callback runs at end next click");
    click(harness, "self", error, &prevented, "mutation: self-removal first");
    click(harness, "self", error, &prevented, "mutation: self-removal second");
    expectString(harness, "selfLog", "s2323", "mutation: self-removal does not skip later listeners");
    click(harness, "onclick-mutation", error, &prevented, "mutation: onclick mutation first");
    click(harness, "onclick-mutation", error, &prevented, "mutation: onclick mutation second");
    expectString(harness, "onclickLog", "oaoac", "mutation: onclick removal/addition uses pre-onclick snapshot");
}

void testErrorsAndEventState()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, error, "errors");
    ScriptResult result = harness.execute(R"JS(
var error = document.getElementById("error");
var errorImmediate = document.getElementById("error-immediate");
var errorLog = "";
var errorSawDefault = false;
error.addEventListener("click", function(event) {
    errorLog = errorLog + "a";
    event.preventDefault();
    var x = missingAfterPrevent;
});
error.addEventListener("click", function(event) {
    errorLog = errorLog + "b";
    errorSawDefault = event.defaultPrevented;
});
errorImmediate.addEventListener("click", function(event) {
    errorLog = errorLog + "i";
    event.stopImmediatePropagation();
    var x = missingAfterImmediate;
});
errorImmediate.addEventListener("click", function(event) { errorLog = errorLog + "j"; });
)JS");
    expect(result.succeeded(), "errors: setup succeeds");
    bool prevented = false;
    const bool first = click(harness, "error", error, &prevented,
        "errors: contained preventDefault error");
    expect(!first && error == RuntimeErrorCode::UnknownIdentifier,
        "errors: callback error is reported but contained");
    expect(prevented, "errors: cancellation survives callback error");
    expectBoolean(harness, "errorSawDefault", true,
        "errors: later listener sees defaultPrevented");
    click(harness, "error", error, &prevented, "errors: second dispatch after error");
    expectString(harness, "errorLog", "abab", "errors: later/future listener dispatch remains healthy");
    const bool immediate = click(harness, "error-immediate", error, &prevented,
        "errors: immediate stop after error");
    expect(!immediate && error == RuntimeErrorCode::UnknownIdentifier,
        "errors: immediate callback error is reported");
    expectString(harness, "errorLog", "ababi", "errors: immediate stop survives callback error");
    click(harness, "error-immediate", error, &prevented,
        "errors: immediate error future dispatch");
    expectString(harness, "errorLog", "ababii",
        "errors: immediate-stop registration remains healthy");
}

std::string capacityFixture(std::size_t elementCount)
{
    std::string html = "<html><body>";
    for (std::size_t index = 0; index < elementCount; ++index)
        html += "<button id=\"e" + std::to_string(index) +
            "\" type=\"button\">E</button>";
    return html + "</body></html>";
}

void testCapacityReuseAndDuplicate()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    expect(harness.loadHtml("file:///js17-capacity.html", capacityFixture(65), error),
        "capacity: fixture loads");
    expect(harness.relayout(), "capacity: relayout succeeds");
    std::string source = "var target = document.getElementById(\"e0\");var calls = 0;";
    for (int index = 0; index < 64; ++index) {
        source += "function l" + std::to_string(index) + "(event) { calls = calls + 1; }";
        source += "target.addEventListener(\"click\", l" +
            std::to_string(index) + ");";
    }
    ScriptResult result = harness.execute(source);
    expect(result.succeeded(), "capacity: 64 unique registrations succeed");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: listener count reaches 64");
    expect(harness.hostAdapter().clickHandlerCount() == 1u,
        "capacity: 64 registrations may share one Element");
    result = harness.execute(
        "target.addEventListener(\"click\", function(event) {});");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th registration is rejected");
    bool prevented = false;
    click(harness, "e0", error, &prevented, "capacity: all listeners dispatch");
    expectNumber(harness, "calls", 64.0, "capacity: all 64 callbacks run");

    result = harness.execute(R"JS(
target.removeEventListener("click", l0);
target.removeEventListener("click", l1);
target.removeEventListener("click", l2);
target.removeEventListener("click", l3);
target.removeEventListener("click", l4);
function n0(event) { calls = calls + 1; }
function n1(event) { calls = calls + 1; }
function n2(event) { calls = calls + 1; }
function n3(event) { calls = calls + 1; }
function n4(event) { calls = calls + 1; }
target.addEventListener("click", n0);
target.addEventListener("click", n1);
target.addEventListener("click", n2);
target.addEventListener("click", n3);
target.addEventListener("click", n4);
)JS");
    expect(result.succeeded(), "capacity: removed slots are reusable");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: reuse restores full count without a leak");
    click(harness, "e0", error, &prevented, "capacity: reused slots dispatch");
    expectNumber(harness, "calls", 128.0,
        "capacity: reused listeners preserve bounded dispatch");

    result = harness.execute(R"JS(
function reusable(event) { calls = calls + 1; }
for (var cycle = 0; cycle < 100; cycle = cycle + 1) {
    target.removeEventListener("click", l63);
    target.addEventListener("click", l63);
}
)JS");
    expect(result.succeeded(), "capacity: repeated add/remove cycles succeed");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: repeated cycles do not leak slots");

    NavigatorScriptExecutionHarness across;
    expect(across.loadHtml("file:///js17-across.html", capacityFixture(3), error),
        "capacity: multi-element fixture loads");
    expect(across.relayout(), "capacity: multi-element relayout succeeds");
    std::string acrossSource;
    for (int index = 0; index < 20; ++index) {
        acrossSource += "function a" + std::to_string(index) + "(event) {}";
        acrossSource += "document.getElementById(\"e0\").addEventListener(\"click\", a" +
            std::to_string(index) + ");";
    }
    for (int index = 0; index < 20; ++index) {
        acrossSource += "function b" + std::to_string(index) + "(event) {}";
        acrossSource += "document.getElementById(\"e1\").addEventListener(\"click\", b" +
            std::to_string(index) + ");";
    }
    // The remaining 24 registrations use distinct function objects on e2.
    for (int index = 0; index < 24; ++index) {
        acrossSource += "function x" + std::to_string(index) +
            "(event) {} document.getElementById(\"e2\").addEventListener(\"click\", x" +
            std::to_string(index) + ");";
    }
    result = across.execute(acrossSource);
    expect(result.succeeded(), "capacity: 20+20+24 global registrations succeed");
    expect(across.hostAdapter().clickListenerCount() == 64u,
        "capacity: registrations across Elements share one global bound");
    result = across.execute(
        "function extra(event) {} document.getElementById(\"e2\").addEventListener(\"click\", extra);");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th registration across Elements is rejected");
}

void testDuplicateAndUnsupportedInputs()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    expect(harness.loadHtml("file:///js17-inputs.html", capacityFixture(1), error),
        "inputs: fixture loads");
    expect(harness.relayout(), "inputs: relayout succeeds");
    ScriptResult result = harness.execute(R"JS(
var target = document.getElementById("e0");
var calls = 0;
function same(event) { calls = calls + 1; }
target.addEventListener("click", same);
target.addEventListener("click", same);
target.addEventListener("click", same);
)JS");
    expect(result.succeeded(), "inputs: duplicate exact callback succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "inputs: duplicates consume one slot");
    bool prevented = false;
    click(harness, "e0", error, &prevented, "inputs: duplicate dispatch");
    expectNumber(harness, "calls", 1.0, "inputs: duplicate invokes once");
    result = harness.execute("target.addEventListener(\"mouseover\", same);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "inputs: unsupported registration rejected");
    result = harness.execute("target.removeEventListener(\"mouseover\", same);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "inputs: unsupported removal rejected");
    result = harness.execute("target.addEventListener(\"click\", null);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "inputs: null callback rejected");
    result = harness.execute("target.removeEventListener(\"click\", 123);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "inputs: invalid removal callback rejected");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "inputs: invalid calls consume no slots");
}

void testNavigationOverflowAndBoundedStress()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "navigation");
    ScriptResult result = harness.execute(R"JS(
var old = document.getElementById("child");
var oldCalls = 0;
old.addEventListener("click", function(event) { oldCalls = oldCalls + 1; });
)JS");
    expect(result.succeeded(), "navigation: old registration succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "navigation: old listener recorded");
    expect(harness.replaceHtml("file:///js17-new.html",
        "<html><body><button id=\"new\" type=\"button\">New</button></body></html>",
        error), "navigation: replacement succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 0u &&
        harness.hostAdapter().clickHandlerCount() == 0u,
        "navigation: old listener tables reset");
    result = harness.execute(R"JS(
var fresh = document.getElementById("new");
var freshCalls = 0;
function freshHandler(event) { freshCalls = freshCalls + 1; }
fresh.addEventListener("click", freshHandler);
)JS");
    expect(result.succeeded(), "navigation: fresh registration succeeds");
    bool prevented = false;
    click(harness, "new", error, &prevented, "navigation: fresh listener dispatch");
    expectNumber(harness, "freshCalls", 1.0, "navigation: old callback cannot execute");

    NavigatorScriptExecutionHarness overflow;
    std::string deep = "<html><body>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth; ++index)
        deep += "<div id=\"d" + std::to_string(index) + "\">";
    deep += "<button id=\"deep\" type=\"button\">Deep</button>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth; ++index)
        deep += "</div>";
    deep += "<button id=\"safe\" type=\"button\">Safe</button></body></html>";
    expect(overflow.loadHtml("file:///js17-overflow.html", deep, error),
        "overflow: fixture loads");
    expect(overflow.relayout(), "overflow: relayout succeeds");
    result = overflow.execute(R"JS(
var deep = document.getElementById("deep");
var safe = document.getElementById("safe");
var deepCalls = 0;
var safeCalls = 0;
deep.addEventListener("click", function(event) { deepCalls = deepCalls + 1; });
safe.addEventListener("click", function(event) { safeCalls = safeCalls + 1; });
)JS");
    expect(result.succeeded(), "overflow: listeners register");
    const std::size_t objectsBefore = overflow.runtime().objectCount();
    const std::uint64_t deepSerial = serialById(overflow, "deep");
    const bool overflowClick = overflow.dispatchClick(deepSerial, error, &prevented);
    expect(!overflowClick && error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "overflow: path limit remains deterministic");
    expect(overflow.runtime().objectCount() == objectsBefore,
        "overflow: no Event or snapshot retention before callbacks");
    click(overflow, "safe", error, &prevented, "overflow: recovery click");
    expectNumber(overflow, "safeCalls", 1.0, "overflow: valid click recovers");

    RuntimeLimits stressLimits;
    stressLimits.maxEnvironments = 1024u;
    NavigatorScriptExecutionHarness stress(stressLimits);
    loadFixture(stress, error, "stress");
    result = stress.execute(R"JS(
var stress = document.getElementById("stress");
var parent = document.getElementById("parent");
var stressCalls = 0;
var parentCalls = 0;
stress.addEventListener("click", function(event) { stressCalls = stressCalls + 1; });
stress.addEventListener("click", function(event) { stressCalls = stressCalls + 1; });
parent.addEventListener("click", function(event) { parentCalls = parentCalls + 1; });
)JS");
    expect(result.succeeded(), "stress: multiple listeners register");
    const std::size_t stressObjectsBefore = stress.runtime().objectCount();
    for (int index = 0; index < 100; ++index) {
        const bool dispatched = click(stress, "stress", error, &prevented,
            "stress: repeated click " + std::to_string(index + 1));
        expect(dispatched, "stress: dispatch succeeds");
    }
    expectNumber(stress, "stressCalls", 200.0,
        "stress: exact same-node callback total");
    expectNumber(stress, "parentCalls", 100.0,
        "stress: exact bubbling callback total");
    expect(stress.hostAdapter().clickListenerCount() == 3u,
        "stress: listener table remains fixed after 100 clicks");
    expect(stress.runtime().objectCount() == stressObjectsBefore + 1u,
        "stress: Event object remains cached");
}

} // namespace

int main()
{
    testOrderingIdentityAndRemoval();
    testPropagationAndCancellation();
    testMutationDuringDispatch();
    testErrorsAndEventState();
    testCapacityReuseAndDuplicate();
    testDuplicateAndUnsupportedInputs();
    testNavigationOverflowAndBoundedStress();
    if (failures != 0) {
        std::cerr << failures << " JS17 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS17 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
