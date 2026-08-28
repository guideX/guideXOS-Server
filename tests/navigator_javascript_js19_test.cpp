#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::RuntimeLimits;
using gxos::javascript::RuntimeObjectId;
using gxos::javascript::RuntimeHostObjectId;
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
    <button id="target" type="button">Target</button>
    <button id="stop" type="button">Stop</button>
    <button id="immediate" type="button">Immediate</button>
    <a id="cancel" href="file:///js19-target.html">Cancel</a>
    <button id="error" type="button">Error</button>
    <button id="mutation" type="button">Mutation</button>
  </div>
</div>
<button id="safe" type="button">Safe</button>
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
    expect(harness.loadHtml("file:///js19.html", kFixture, error),
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

void testMetadataValuesAndReadOnlyState()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "metadata");
    ScriptResult result = harness.execute(R"JS(
var target = document.getElementById("target");
var parent = document.getElementById("parent");
var grandparent = document.getElementById("grandparent");
var order = "";
var onclickBubbles = false;
var onclickCancelable = false;
var listenerBubbles = false;
var listenerCancelable = false;
var laterBubbles = false;
var laterCancelable = false;
var parentBubbles = false;
var parentCancelable = false;
var grandparentBubbles = false;
var grandparentCancelable = false;
var initialDefault = true;
var laterDefault = true;
var unknownIsUndefined = false;
var assignmentDidNotChangeBubbles = false;
var assignmentDidNotChangeCancelable = false;
var targetIsCanonical = false;
var targetCurrentIsCanonical = false;
var savedEvent;
var callbackEvent;
target.onclick = function(event) {
    savedEvent = event;
    callbackEvent = event;
    onclickBubbles = event.bubbles;
    onclickCancelable = event.cancelable;
    initialDefault = event.defaultPrevented === false;
    targetIsCanonical = event.target === target;
    targetCurrentIsCanonical = event.currentTarget === target;
    order = order + "o";
};
target.addEventListener("click", function(event) {
    listenerBubbles = event.bubbles;
    listenerCancelable = event.cancelable;
    unknownIsUndefined = event.whatever === undefined;
    event.bubbles = false;
    event.cancelable = false;
    assignmentDidNotChangeBubbles = event.bubbles === true;
    assignmentDidNotChangeCancelable = event.cancelable === true;
    order = order + "l";
});
target.addEventListener("click", function(event) {
    laterBubbles = event.bubbles;
    laterCancelable = event.cancelable;
    laterDefault = event.defaultPrevented === false;
    callbackEvent = event;
    order = order + "m";
});
parent.addEventListener("click", function(event) {
    parentBubbles = event.bubbles;
    parentCancelable = event.cancelable;
    if (event.target === target && event.currentTarget === parent) order = order + "p";
});
grandparent.addEventListener("click", function(event) {
    grandparentBubbles = event.bubbles;
    grandparentCancelable = event.cancelable;
    if (event.target === target && event.currentTarget === grandparent) order = order + "g";
});
)JS");
    expect(result.succeeded(), "metadata: target and ancestor setup succeeds");
    expect(harness.hostAdapter().clickHandlerCount() == 3u &&
        harness.hostAdapter().clickListenerCount() == 4u,
        "metadata: registration tables contain only listener records");

    const std::size_t objectsBefore = harness.runtime().objectCount();
    const std::size_t propertiesBefore = harness.runtime().propertyCount();
    const std::size_t nativeFunctionsBefore =
        harness.runtime().nativeFunctionCount();
    const std::size_t hostObjectsBefore = harness.runtime().hostObjectCount();
    bool prevented = false;
    expect(click(harness, "target", error, &prevented,
        "metadata: first target click"), "metadata: dispatch succeeds");
    expect(!prevented, "metadata: inspection-only click is not cancelled");
    expectBoolean(harness, "onclickBubbles", true,
        "metadata: onclick sees bubbles true");
    expectBoolean(harness, "onclickCancelable", true,
        "metadata: onclick sees cancelable true");
    expectBoolean(harness, "listenerBubbles", true,
        "metadata: listener sees bubbles true");
    expectBoolean(harness, "listenerCancelable", true,
        "metadata: listener sees cancelable true");
    expectBoolean(harness, "laterBubbles", true,
        "metadata: later listener sees bubbles true");
    expectBoolean(harness, "laterCancelable", true,
        "metadata: later listener sees cancelable true");
    expectBoolean(harness, "parentBubbles", true,
        "metadata: parent sees bubbles true");
    expectBoolean(harness, "parentCancelable", true,
        "metadata: parent sees cancelable true");
    expectBoolean(harness, "grandparentBubbles", true,
        "metadata: grandparent sees bubbles true");
    expectBoolean(harness, "grandparentCancelable", true,
        "metadata: grandparent sees cancelable true");
    expectBoolean(harness, "initialDefault", true,
        "metadata: defaultPrevented starts false");
    expectBoolean(harness, "laterDefault", true,
        "metadata: inspection leaves defaultPrevented false");
    expectBoolean(harness, "unknownIsUndefined", true,
        "metadata: unknown property remains undefined");
    expectBoolean(harness, "assignmentDidNotChangeBubbles", true,
        "metadata: bubbles assignment is a no-op");
    expectBoolean(harness, "assignmentDidNotChangeCancelable", true,
        "metadata: cancelable assignment is a no-op");
    expectBoolean(harness, "targetIsCanonical", true,
        "metadata: target identity remains canonical");
    expectBoolean(harness, "targetCurrentIsCanonical", true,
        "metadata: target currentTarget remains canonical");
    expectString(harness, "order", "olmpg",
        "metadata: onclick, listeners, parent, grandparent order");
    expect(harness.runtime().objectCount() == objectsBefore + 1u,
        "metadata: first dispatch creates one cached Event object");
    expect(harness.runtime().propertyCount() == propertiesBefore + 9u,
        "metadata: cached Event adds exactly nine fixed properties");
    expect(harness.runtime().nativeFunctionCount() == nativeFunctionsBefore + 3u,
        "metadata: Event metadata adds no native functions");
    expect(harness.runtime().hostObjectCount() >= hostObjectsBefore,
        "metadata: target host wrapper remains valid");

    result = harness.execute(
        "var retainedBubbles = savedEvent.bubbles === true;"
        "var retainedCancelable = savedEvent.cancelable === true;"
        "var sameEvent = savedEvent === callbackEvent;"
        "var retainedTarget = savedEvent.target === target;"
        "var retainedUnknown = savedEvent.whatever === undefined;");
    expect(result.succeeded(), "metadata: retained Event reads succeed");
    expectBoolean(harness, "retainedBubbles", true,
        "metadata: retained Event bubbles read is safe");
    expectBoolean(harness, "retainedCancelable", true,
        "metadata: retained Event cancelable read is safe");
    expectBoolean(harness, "sameEvent", true,
        "metadata: callbacks share the cached Event identity");
    expectBoolean(harness, "retainedTarget", true,
        "metadata: retained Event target is safe");
    expectBoolean(harness, "retainedUnknown", true,
        "metadata: retained unknown property remains missing");

    const std::size_t propertiesAfterFirst = harness.runtime().propertyCount();
    const std::size_t objectsAfterFirst = harness.runtime().objectCount();
    const std::size_t hostsAfterFirst = harness.runtime().hostObjectCount();
    expect(click(harness, "target", error, &prevented,
        "metadata: repeated target click"), "metadata: repeated dispatch succeeds");
    expect(harness.runtime().propertyCount() == propertiesAfterFirst,
        "metadata: repeated click adds no property records");
    expect(harness.runtime().objectCount() == objectsAfterFirst,
        "metadata: repeated click adds no Event object");
    expect(harness.runtime().hostObjectCount() == hostsAfterFirst,
        "metadata: repeated click adds no host wrapper");
    expectBoolean(harness, "onclickBubbles", true,
        "metadata: repeated onclick remains true");
    expectBoolean(harness, "laterCancelable", true,
        "metadata: repeated listener remains true");
}

void testPropagationCancellationAndOnce()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "propagation");
    ScriptResult result = harness.execute(R"JS(
var stop = document.getElementById("stop");
var immediate = document.getElementById("immediate");
var cancel = document.getElementById("cancel");
var parent = document.getElementById("parent");
var stopLog = "";
var stopParent = false;
var immediateLog = "";
var immediateParent = false;
var cancelLog = "";
var cancelParent = false;
stop.addEventListener("click", function(event) {
    if (event.bubbles === true && event.cancelable === true) stopLog = "b";
    else stopLog = "x";
    event.bubbles = false;
    event.cancelable = false;
    event.stopPropagation();
});
stop.addEventListener("click", function(event) {
    if (event.bubbles === true && event.cancelable === true) stopLog = stopLog + "c";
});
parent.addEventListener("click", function(event) {
    if (event.target === stop) stopParent = true;
    if (event.target === immediate) immediateParent = true;
    if (event.target === cancel) {
        cancelParent = event.bubbles === true && event.cancelable === true &&
            event.defaultPrevented === true;
        cancelLog = cancelLog + "p";
    }
});
immediate.addEventListener("click", function(event) {
    if (event.bubbles === true && event.cancelable === true) immediateLog = "b";
    else immediateLog = "x";
    event.stopImmediatePropagation();
}, { once: true });
immediate.addEventListener("click", function(event) {
    if (event.bubbles === true && event.cancelable === true) immediateLog = immediateLog + "c";
});
cancel.addEventListener("click", function(event) {
    if (event.bubbles === true && event.cancelable === true &&
        event.defaultPrevented === false) cancelLog = "c";
    else cancelLog = "x";
    event.preventDefault();
    if (event.cancelable === true && event.defaultPrevented === true) cancelLog = cancelLog + "d";
}, { once: true });
cancel.addEventListener("click", function(event) {
    if (event.bubbles === true && event.cancelable === true) {
        if (event.defaultPrevented) cancelLog = cancelLog + "l";
        else cancelLog = cancelLog + "n";
    }
});
)JS");
    expect(result.succeeded(), "propagation: setup succeeds");
    bool prevented = false;
    expect(click(harness, "stop", error, &prevented,
        "propagation: stopPropagation"), "propagation: stop dispatch succeeds");
    expectString(harness, "stopLog", "bc",
        "propagation: bubbles survives stopPropagation and same-node dispatch");
    expectBoolean(harness, "stopParent", false,
        "propagation: stopPropagation blocks parent only");

    expect(click(harness, "immediate", error, &prevented,
        "propagation: immediate first"),
        "propagation: immediate first dispatch succeeds");
    expectString(harness, "immediateLog", "b",
        "propagation: immediate stop leaves metadata true and skips later listener");
    expectBoolean(harness, "immediateParent", false,
        "propagation: immediate stop blocks ancestor");
    expect(click(harness, "immediate", error, &prevented,
        "propagation: immediate second"),
        "propagation: once removal allows second dispatch");
    expectString(harness, "immediateLog", "bc",
        "propagation: once listener is gone and persistent listener sees metadata");
    expectBoolean(harness, "immediateParent", true,
        "propagation: second immediate click bubbles normally");

    expect(click(harness, "cancel", error, &prevented,
        "propagation: cancelled link"),
        "propagation: cancelled dispatch succeeds");
    expect(prevented, "propagation: preventDefault reports cancellation");
    expectString(harness, "cancelLog", "cdlp",
        "propagation: later listener and ancestor see cancellation metadata");
    expectBoolean(harness, "cancelParent", true,
        "propagation: ancestor sees cancelable and defaultPrevented true");
    expect(click(harness, "cancel", error, &prevented,
        "propagation: uncancelled second link"),
        "propagation: once cancellation is removed");
    expect(!prevented, "propagation: metadata inspection does not cancel second link");
    expectString(harness, "cancelLog", "cdlpnp",
        "propagation: second link starts defaultPrevented false");
    expect(harness.hostAdapter().clickListenerCount() == 5u,
        "propagation: once records are removed without affecting metadata");
}

void testErrorAndMutationContainment()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "errors");
    ScriptResult result = harness.execute(R"JS(
var errorNode = document.getElementById("error");
var parent = document.getElementById("parent");
var errorLog = "";
var errorParentBubbles = false;
var errorParentCancelable = false;
errorNode.addEventListener("click", function(event) {
    if (event.bubbles === true && event.cancelable === true) errorLog = "a";
    else errorLog = "x";
    var ignored = missingAfterMetadata;
});
errorNode.addEventListener("click", function(event) {
    if (event.bubbles === true && event.cancelable === true) errorLog = errorLog + "b";
});
parent.addEventListener("click", function(event) {
    if (event.target === errorNode) {
        errorParentBubbles = event.bubbles;
        errorParentCancelable = event.cancelable;
        errorLog = errorLog + "p";
    }
});
)JS");
    expect(result.succeeded(), "errors: setup succeeds");
    bool prevented = false;
    const bool dispatched = click(harness, "error", error, &prevented,
        "errors: callback metadata then error");
    expect(!dispatched && error == RuntimeErrorCode::UnknownIdentifier,
        "errors: callback error is reported");
    expectString(harness, "errorLog", "abp",
        "errors: later listener and ancestor survive contained error");
    expectBoolean(harness, "errorParentBubbles", true,
        "errors: ancestor metadata survives callback error");
    expectBoolean(harness, "errorParentCancelable", true,
        "errors: ancestor cancelable survives callback error");

    result = harness.execute(R"JS(
var mutation = document.getElementById("mutation");
var mutationLog = "";
function added(event) { mutationLog = mutationLog + "c"; }
function removed(event) { mutationLog = mutationLog + "r"; }
function first(event) {
    mutationLog = mutationLog + "a";
    mutation.removeEventListener("click", removed);
    mutation.addEventListener("click", added);
}
mutation.addEventListener("click", first);
mutation.addEventListener("click", removed);
)JS");
    expect(result.succeeded(), "mutation: setup succeeds");
    expect(click(harness, "mutation", error, &prevented,
        "mutation: first"), "mutation: first dispatch succeeds");
    expect(click(harness, "mutation", error, &prevented,
        "mutation: second"), "mutation: second dispatch succeeds");
    expectString(harness, "mutationLog", "aac",
        "mutation: snapshot skips removal and defers addition");
    expect(harness.hostAdapter().clickListenerCount() == 5u,
        "mutation: listener slot identity remains bounded");
}

void testOptionsCapacityAndRepeatedBoundedness()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness options;
    loadFixture(options, error, "options");
    ScriptResult result = options.execute(R"JS(
var target = document.getElementById("target");
var optionCalls = 0;
function optionHandler(event) {
    if (event.bubbles === true && event.cancelable === true) optionCalls = optionCalls + 1;
}
target.addEventListener("click", optionHandler, { once: false });
target.addEventListener("click", optionHandler, { once: false });
)JS");
    expect(result.succeeded(), "options: once false setup succeeds");
    expect(options.hostAdapter().clickListenerCount() == 1u,
        "options: duplicate once false registration is a no-op");
    bool prevented = false;
    expect(click(options, "target", error, &prevented,
        "options: once false first"), "options: once false first succeeds");
    expect(click(options, "target", error, &prevented,
        "options: once false second"), "options: once false second succeeds");
    expectNumber(options, "optionCalls", 2.0,
        "options: persistent listener remains after metadata reads");

    result = options.execute(
        "target.addEventListener(\"mouseover\", optionHandler);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "options: unsupported event remains rejected");
    result = options.execute(
        "target.addEventListener(\"click\", null, { once: true });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "options: invalid callback remains rejected");
    result = options.execute(
        "target.addEventListener(\"click\", optionHandler, { once: 1 });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "options: malformed once remains rejected");
    expect(options.hostAdapter().clickListenerCount() == 1u,
        "options: malformed calls consume no listener capacity");

    NavigatorScriptExecutionHarness capacity;
    std::string html = "<html><body>";
    for (int index = 0; index < 65; ++index)
        html += "<button id=\"e" + std::to_string(index) +
            "\" type=\"button\">E</button>";
    html += "</body></html>";
    expect(capacity.loadHtml("file:///js19-capacity.html", html, error),
        "capacity: fixture loads");
    expect(capacity.relayout(), "capacity: relayout succeeds");
    std::string source = "var target = document.getElementById(\"e0\");var calls = 0;";
    for (int index = 0; index < 64; ++index) {
        source += "function l" + std::to_string(index) +
            "(event) { if (event.bubbles === true && event.cancelable === true) calls = calls + 1; }";
        source += "target.addEventListener(\"click\", l" +
            std::to_string(index) + ");";
    }
    result = capacity.execute(source);
    expect(result.succeeded(), "capacity: 64 registrations succeed");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: metadata consumes no listener slots");
    result = capacity.execute(
        "target.addEventListener(\"click\", function(event) {});");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th registration fails safely");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: rejected 65th registration changes nothing");
    expect(capacity.dispatchClick(serialById(capacity, "e0"), error,
        &prevented), "capacity: 64 metadata listeners dispatch");
    expectNumber(capacity, "calls", 64.0,
        "capacity: all 64 callbacks see Boolean metadata");

    RuntimeLimits stressLimits;
    stressLimits.maxEnvironments = 1024u;
    NavigatorScriptExecutionHarness stress(stressLimits);
    loadFixture(stress, error, "stress");
    result = stress.execute(R"JS(
var target = document.getElementById("target");
var parent = document.getElementById("parent");
var persistentCalls = 0;
var onceCalls = 0;
var parentCalls = 0;
var allMetadata = true;
function persistent(event) {
    allMetadata = allMetadata && event.bubbles === true && event.cancelable === true;
    persistentCalls = persistentCalls + 1;
}
function once(event) {
    allMetadata = allMetadata && event.bubbles === true && event.cancelable === true;
    onceCalls = onceCalls + 1;
}
target.addEventListener("click", persistent);
target.addEventListener("click", once, { once: true });
parent.addEventListener("click", function(event) {
    allMetadata = allMetadata && event.bubbles === true && event.cancelable === true;
    parentCalls = parentCalls + 1;
});
)JS");
    expect(result.succeeded(), "stress: persistent, once, ancestor setup succeeds");
    const std::size_t stressObjects = stress.runtime().objectCount();
    const std::size_t stressProperties = stress.runtime().propertyCount();
    for (int index = 0; index < 100; ++index)
        expect(click(stress, "target", error, &prevented,
            "stress: repeated click " + std::to_string(index + 1)),
            "stress: dispatch succeeds");
    expectNumber(stress, "persistentCalls", 100.0,
        "stress: persistent callback total");
    expectNumber(stress, "onceCalls", 1.0,
        "stress: once callback remains once");
    expectNumber(stress, "parentCalls", 100.0,
        "stress: ancestor callback total");
    expectBoolean(stress, "allMetadata", true,
        "stress: all callbacks see coherent metadata");
    expect(stress.hostAdapter().clickListenerCount() == 2u,
        "stress: once removal leaves one target and one ancestor listener");
    expect(stress.runtime().objectCount() == stressObjects + 1u,
        "stress: 100 clicks create no additional Event objects");
    expect(stress.runtime().propertyCount() == stressProperties + 9u,
        "stress: 100 clicks create no additional Event properties");
}

void testNavigationOverflowAndStaleSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness navigation;
    loadFixture(navigation, error, "navigation");
    ScriptResult result = navigation.execute(R"JS(
var old = document.getElementById("target");
var saved;
var oldCalls = 0;
old.addEventListener("click", function(event) {
    saved = event;
    oldCalls = oldCalls + 1;
});
)JS");
    expect(result.succeeded(), "navigation: old listener setup succeeds");
    bool prevented = false;
    expect(click(navigation, "target", error, &prevented,
        "navigation: old click"), "navigation: old click succeeds");
    const Value* oldValue = binding(navigation, "old");
    const Value* savedValue = binding(navigation, "saved");
    expect(oldValue != nullptr && oldValue->isHostObject(),
        "navigation: old Element handle exists");
    expect(savedValue != nullptr && savedValue->isObject(),
        "navigation: saved Event object exists");
    const RuntimeHostObjectId oldObject = oldValue == nullptr
        ? gxos::javascript::kInvalidRuntimeHostObjectId : oldValue->hostObjectId();
    const RuntimeObjectId savedObject = savedValue == nullptr
        ? gxos::javascript::kInvalidRuntimeObjectId : savedValue->objectId();
    expect(navigation.invalidateDocumentGeneration(error),
        "navigation: generation invalidation succeeds");
    Value staleValue;
    expect(!navigation.runtime().readHostPropertyForTesting(oldObject, "id",
        staleValue, error) && error == RuntimeErrorCode::StaleHostObject,
        "navigation: stale Element fails closed");
    Value staleEventProperty;
    error = RuntimeErrorCode::None;
    expect(navigation.runtime().readPropertyForTesting(savedObject, "bubbles",
        staleEventProperty, error),
        "navigation: retained Event metadata remains safe after generation reset");
    expect(staleEventProperty.type() == ValueType::Boolean &&
        staleEventProperty.booleanValue(),
        "navigation: retained Event bubbles remains Boolean true");
    error = RuntimeErrorCode::None;
    expect(navigation.replaceHtml("file:///js19-new.html",
        "<html><body><button id=\"fresh\" type=\"button\">Fresh</button></body></html>",
        error), "navigation: document replacement succeeds");
    expect(navigation.hostAdapter().clickListenerCount() == 0u,
        "navigation: old listeners are cleared");
    result = navigation.execute(R"JS(
var fresh = document.getElementById("fresh");
var freshBubbles = false;
var freshCancelable = false;
var freshCalls = 0;
fresh.addEventListener("click", function(event) {
    freshBubbles = event.bubbles;
    freshCancelable = event.cancelable;
    freshCalls = freshCalls + 1;
});
)JS");
    expect(result.succeeded(), "navigation: fresh listener setup succeeds");
    expect(click(navigation, "fresh", error, &prevented,
        "navigation: fresh click"), "navigation: fresh click succeeds");
    expectBoolean(navigation, "freshBubbles", true,
        "navigation: fresh document reports bubbles true");
    expectBoolean(navigation, "freshCancelable", true,
        "navigation: fresh document reports cancelable true");
    expectNumber(navigation, "freshCalls", 1.0,
        "navigation: stale callback cannot execute");

    NavigatorScriptExecutionHarness overflow;
    std::string deep = "<html><body>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
        ++index) deep += "<div>";
    deep += "<button id=\"deep\" type=\"button\">Deep</button>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
        ++index) deep += "</div>";
    deep += "</body></html>";
    expect(overflow.loadHtml("file:///js19-overflow.html", deep, error),
        "overflow: fixture loads");
    expect(overflow.relayout(), "overflow: relayout succeeds");
    result = overflow.execute(R"JS(
var deep = document.getElementById("deep");
var overflowCalls = 0;
deep.addEventListener("click", function(event) {
    overflowCalls = overflowCalls + 1;
    event.preventDefault();
});
)JS");
    expect(result.succeeded(), "overflow: listener setup succeeds");
    const std::size_t overflowObjects = overflow.runtime().objectCount();
    const std::size_t overflowProperties = overflow.runtime().propertyCount();
    const std::uint64_t deepSerial = serialById(overflow, "deep");
    const bool overflowed = overflow.dispatchClick(deepSerial, error,
        &prevented);
    expect(!overflowed && error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "overflow: 32-node path limit remains unchanged");
    expect(!prevented, "overflow: no default cancellation occurs before callbacks");
    expect(overflow.runtime().objectCount() == overflowObjects &&
        overflow.runtime().propertyCount() == overflowProperties,
        "overflow: aborted path creates no Event metadata");
    expect(overflow.hostAdapter().clickListenerCount() == 1u,
        "overflow: aborted path does not consume listener");
    for (gxos::web::HtmlElementRef& element :
        overflow.document().structuralElements) {
        if (element.serial == deepSerial) {
            element.parentSerial = 0;
            break;
        }
    }
    expect(overflow.dispatchClick(deepSerial, error, &prevented),
        "overflow: repaired path dispatches normally");
    expectNumber(overflow, "overflowCalls", 1.0,
        "overflow: repaired path invokes listener");
    expect(overflow.hostAdapter().clickListenerCount() == 1u,
        "overflow: persistent listener remains after recovery");
}

} // namespace

int main()
{
    testMetadataValuesAndReadOnlyState();
    testPropagationCancellationAndOnce();
    testErrorAndMutationContainment();
    testOptionsCapacityAndRepeatedBoundedness();
    testNavigationOverflowAndStaleSafety();
    if (failures != 0) {
        std::cerr << failures << " JS19 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS19 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
