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

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<button id="alpha" type="button">Alpha</button>
<button id="beta" type="button">Beta</button>
<button id="bad" type="button">Bad</button>
<a id="link" href="file:///js12-target.html">Link</a>
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

std::string elementText(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    std::string result;
    expect(gxos::javascript::navigatorScriptElementTextContent(
        harness.document(), serial, result), "host text inspection succeeds");
    return result;
}

bool click(NavigatorScriptExecutionHarness& harness, std::uint64_t serial,
    RuntimeErrorCode& error, const std::string& label)
{
    const bool dispatched = harness.dispatchClick(serial, error);
    expect(dispatched, label + ": dispatch boundary succeeds");
    if (harness.documentDirty())
        expect(harness.relayout(), label + ": controlled relayout succeeds");
    return dispatched;
}

void loadFixture(NavigatorScriptExecutionHarness& harness, const char* url,
    RuntimeErrorCode& error, const std::string& label)
{
    expect(harness.loadHtml(url, kFixture, error), label + ": fixture loads");
    expect(harness.relayout(), label + ": initial relayout succeeds");
}

void testEventPropertiesAndCallbackArguments()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js12-properties.html", error, "properties");
    const std::uint64_t alpha = serialById(harness, "alpha");
    const std::uint64_t beta = serialById(harness, "beta");
    expect(alpha != 0 && beta != 0, "properties: fixture has two elements");

    const std::size_t objectsBeforeClick = harness.runtime().objectCount();
    ScriptResult result = harness.execute(R"JS(
var alpha = document.getElementById("alpha");
var beta = document.getElementById("beta");
var sawArgument = false;
var onclickType = "";
var onclickTarget = "";
var onclickCurrent = "";
var listenerType = "";
var listenerTarget = "";
var listenerCurrent = "";
var order = "";
var targetEqualsAlpha = false;
var currentEqualsAlpha = false;
var targetCurrentSame = false;
var repeatedTargetStable = false;
var eventSelfStable = false;
var localTargetStable = false;
var unknownIsUndefined = false;
var overwriteDidNotCorrupt = false;
var onclickEvent;
var listenerEvent;
var savedEvent;
function alphaOnclick(event) {
    onclickEvent = event;
    onclickType = event.type;
    onclickTarget = event.target.id;
    onclickCurrent = event.currentTarget.id;
    order = order + "o";
}
function alphaListener(event) {
    listenerEvent = event;
    savedEvent = event;
    sawArgument = event !== undefined;
    var localEvent = event;
    var firstTarget = event.target;
    var secondTarget = event.target;
    listenerType = localEvent.type;
    listenerTarget = localEvent.target.id;
    listenerCurrent = localEvent.currentTarget.id;
    targetEqualsAlpha = firstTarget === alpha;
    currentEqualsAlpha = localEvent.currentTarget === alpha;
    targetCurrentSame = localEvent.target === localEvent.currentTarget;
    repeatedTargetStable = firstTarget === secondTarget;
    eventSelfStable = event === event;
    localTargetStable = localEvent.target === alpha;
    unknownIsUndefined = event.foo === undefined;
    event.type = "banana";
    event.target = beta;
    event.currentTarget = beta;
    overwriteDidNotCorrupt = event.type === "click" &&
        event.target === alpha && event.currentTarget === alpha;
    order = order + "l";
    event.target.textContent = "Clicked";
}
alpha.onclick = alphaOnclick;
alpha.addEventListener("click", alphaListener);
)JS");
    expect(result.succeeded(), "properties: callback registration succeeds");
    expect(harness.hostAdapter().clickHandlerCount() == 1u &&
        harness.hostAdapter().clickListenerCount() == 1u,
        "properties: onclick and listener share one bounded record");

    const std::uint64_t revisionBeforeClick = harness.layoutRevision();
    click(harness, alpha, error, "properties: authentic alpha click");
    expectString(harness, "onclickType", "click", "properties: onclick type");
    expectString(harness, "onclickTarget", "alpha", "properties: onclick target");
    expectString(harness, "onclickCurrent", "alpha", "properties: onclick currentTarget");
    expectString(harness, "listenerType", "click", "properties: listener type");
    expectString(harness, "listenerTarget", "alpha", "properties: listener target");
    expectString(harness, "listenerCurrent", "alpha", "properties: listener currentTarget");
    expectString(harness, "order", "ol", "properties: onclick precedes listener");
    expectBoolean(harness, "sawArgument", true, "properties: first callback argument exists");
    expectBoolean(harness, "targetEqualsAlpha", true, "properties: target equals canonical alpha");
    expectBoolean(harness, "currentEqualsAlpha", true,
        "properties: currentTarget equals registered alpha");
    expectBoolean(harness, "targetCurrentSame", true,
        "properties: direct target/currentTarget equality");
    expectBoolean(harness, "repeatedTargetStable", true,
        "properties: repeated target reads preserve identity");
    expectBoolean(harness, "eventSelfStable", true,
        "properties: event self equality");
    expectBoolean(harness, "localTargetStable", true,
        "properties: local Event reference works");
    expectBoolean(harness, "unknownIsUndefined", true,
        "properties: unknown field follows missing-property semantics");
    expectBoolean(harness, "overwriteDidNotCorrupt", true,
        "properties: host-defined fields are read-only to script");
    expect(elementText(harness, alpha) == "Clicked",
        "properties: DOM mutation through event.target reaches real element");
    expect(harness.layoutRevision() > revisionBeforeClick,
        "properties: event-target mutation advances layout revision");
    expect(harness.runtime().objectCount() == objectsBeforeClick + 1u,
        "properties: first dispatch creates exactly one cached Event object");

    click(harness, alpha, error, "properties: repeated alpha click");
    expect(harness.runtime().objectCount() == objectsBeforeClick + 1u,
        "properties: repeated click reuses Event object storage");
    result = harness.execute(
        "var sameCallbackEvent = onclickEvent === listenerEvent;"
        "var sameSavedEvent = savedEvent === listenerEvent;");
    expect(result.succeeded(), "properties: callback Event identity read succeeds");
    expectBoolean(harness, "sameCallbackEvent", true,
        "properties: onclick and listener receive one dispatch Event");
    expectBoolean(harness, "sameSavedEvent", true,
        "properties: retained local Event identity is stable");

    result = harness.execute(
        "var zeroCount = 0;"
        "function zero() { zeroCount = zeroCount + 1; }"
        "var zeroOnclickCount = 0;"
        "function zeroOnclick() { zeroOnclickCount = zeroOnclickCount + 1; }"
        "beta.onclick = zeroOnclick;"
        "beta.addEventListener(\"click\", zero);");
    expect(result.succeeded(),
        "properties: zero-parameter onclick and listener register");
    click(harness, beta, error, "properties: zero-parameter click");
    expectNumber(harness, "zeroOnclickCount", 1.0,
        "properties: zero-parameter onclick ignores supplied Event");
    expectNumber(harness, "zeroCount", 1.0,
        "properties: zero-parameter callback ignores supplied Event");

    result = harness.execute(
        "function many(event, extra) { manyType = event.type; manyExtraUndefined = extra === undefined; }"
        "var manyType = \"\"; var manyExtraUndefined = false;"
        "beta.addEventListener(\"click\", many);");
    expect(result.succeeded(), "properties: multi-parameter listener registers");
    click(harness, beta, error, "properties: multi-parameter click");
    expectString(harness, "manyType", "click", "properties: first formal receives Event");
    expectBoolean(harness, "manyExtraUndefined", true,
        "properties: unspecified extra parameter remains undefined");
}

void testIndependentElementIdentity()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js12-elements.html", error, "elements");
    const std::uint64_t alpha = serialById(harness, "alpha");
    const std::uint64_t beta = serialById(harness, "beta");

    ScriptResult result = harness.execute(R"JS(
var alpha = document.getElementById("alpha");
var beta = document.getElementById("beta");
var alphaTarget = "";
var alphaCurrent = "";
var betaTarget = "";
var betaCurrent = "";
var alphaIsAlpha = false;
var betaIsBeta = false;
function shared(event) {
    if (event.target === alpha) {
        alphaTarget = event.target.id;
        alphaCurrent = event.currentTarget.id;
        alphaIsAlpha = event.currentTarget === alpha;
    }
    if (event.target === beta) {
        betaTarget = event.target.id;
        betaCurrent = event.currentTarget.id;
        betaIsBeta = event.currentTarget === beta;
    }
}
alpha.addEventListener("click", shared);
beta.addEventListener("click", shared);
)JS");
    expect(result.succeeded(), "elements: independent registrations succeed");
    click(harness, alpha, error, "elements: alpha click");
    click(harness, beta, error, "elements: beta click");
    expectString(harness, "alphaTarget", "alpha", "elements: alpha target id");
    expectString(harness, "alphaCurrent", "alpha", "elements: alpha currentTarget id");
    expectString(harness, "betaTarget", "beta", "elements: beta target id");
    expectString(harness, "betaCurrent", "beta", "elements: beta currentTarget id");
    expectBoolean(harness, "alphaIsAlpha", true,
        "elements: alpha callback sees registered alpha wrapper");
    expectBoolean(harness, "betaIsBeta", true,
        "elements: beta callback sees registered beta wrapper");
}

std::string boundedFixture()
{
    std::string html = "<html><body>";
    for (int index = 0; index < 65; ++index) {
        html += "<button id=\"e" + std::to_string(index) +
            "\" type=\"button\">" + std::to_string(index) + "</button>";
    }
    html += "</body></html>";
    return html;
}

void testRemovalCapacityAndStress()
{
    NavigatorScriptHostLimits limits;
    limits.maxClickListeners = 64u;
    NavigatorScriptExecutionHarness harness(RuntimeLimits(), limits);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string fixture = boundedFixture();
    expect(harness.loadHtml("file:///js12-capacity.html", fixture, error),
        "capacity: fixture loads");
    expect(harness.relayout(), "capacity: initial relayout succeeds");

    ScriptResult result = harness.execute(R"JS(
var e0 = document.getElementById("e0");
var e1 = document.getElementById("e1");
var e64 = document.getElementById("e64");
var calls = 0;
var lastEventTarget = "";
function handler(event) { calls = calls + 1; lastEventTarget = event.target.id; }
function wrong(event) { calls = calls + 100; }
e0.addEventListener("click", handler);
e1.removeEventListener("click", handler);
e0.removeEventListener("click", wrong);
)JS");
    expect(result.succeeded(), "capacity: initial listener and no-op removals succeed");
    const std::uint64_t e0Serial = serialById(harness, "e0");
    click(harness, e0Serial, error,
        "capacity: initial listener click");
    expectNumber(harness, "calls", 1.0,
        "capacity: wrong-function/element removal preserves listener");
    expectString(harness, "lastEventTarget", "e0",
        "capacity: Event target is correct after removal checks");

    result = harness.execute(
        "e0.removeEventListener(\"click\", handler);"
        "e0.removeEventListener(\"click\", handler);"
        "e0.addEventListener(\"click\", handler);");
    expect(result.succeeded(), "capacity: repeated remove and re-add succeed");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "capacity: remove/re-add reuses listener slot");

    result = harness.execute(
        "e0.addEventListener(\"mouseover\", handler);");
    expect(!result.succeeded() &&
        result.runtimeError.code == RuntimeErrorCode::HostInvalidValue,
        "capacity: unsupported event registration is deterministic");
    result = harness.execute(
        "e0.removeEventListener(\"mouseover\", handler);");
    expect(!result.succeeded() &&
        result.runtimeError.code == RuntimeErrorCode::HostInvalidValue,
        "capacity: unsupported event removal is deterministic");
    result = harness.execute(
        "e0.addEventListener(\"click\", null);");
    expect(!result.succeeded() &&
        result.runtimeError.code == RuntimeErrorCode::HostInvalidValue,
        "capacity: invalid callback is rejected safely");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "capacity: rejected inputs leave valid listener installed");

    std::string source =
        "function shared(event) { calls = calls + 1; lastEventTarget = event.target.id; }";
    for (int index = 0; index < 64; ++index) {
        source += "var x" + std::to_string(index) +
            " = document.getElementById(\"e" + std::to_string(index) + "\");";
        source += "x" + std::to_string(index) +
            ".addEventListener(\"click\", shared);";
    }
    result = harness.execute(source);
    expect(result.succeeded(), "capacity: 64 registrations succeed");
    expect(harness.hostAdapter().clickListenerCount() == 64u &&
        harness.hostAdapter().clickHandlerCount() == 64u,
        "capacity: listener table remains bounded at 64");
    result = harness.execute(
        "x0.removeEventListener(\"click\", shared);"
        "e64.addEventListener(\"click\", shared);");
    expect(result.succeeded(), "capacity: released slot can be reused");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: slot reuse returns to exactly 64");

    const std::size_t objectCountBeforeStress = harness.runtime().objectCount();
    const std::size_t hostObjectCountBeforeStress = harness.runtime().hostObjectCount();
    const std::size_t eventTargetSerial = serialById(harness, "e64");
    for (int clickIndex = 0; clickIndex < 100; ++clickIndex)
        click(harness, eventTargetSerial, error,
            "capacity: bounded repeated click " + std::to_string(clickIndex));
    expect(harness.runtime().objectCount() == objectCountBeforeStress,
        "capacity: repeated clicks do not allocate Event objects");
    expect(harness.runtime().hostObjectCount() == hostObjectCountBeforeStress,
        "capacity: repeated clicks do not grow host wrapper records");
    expectNumber(harness, "calls", 101.0,
        "capacity: stress clicks remain dispatchable");
    expectString(harness, "lastEventTarget", "e64",
        "capacity: stress Event target remains correct");
}

void testErrorsAndNavigationSafety()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js12-generation-a.html", error, "generation A");
    const std::uint64_t alpha = serialById(harness, "alpha");
    const std::uint64_t beta = serialById(harness, "beta");

    ScriptResult result = harness.execute(R"JS(
var alpha = document.getElementById("alpha");
var beta = document.getElementById("beta");
var saved = null;
var goodCount = 0;
function bad(event) { saved = event; var x = unknownIdentifier; }
function good(event) { goodCount = goodCount + 1; }
alpha.addEventListener("click", bad);
beta.addEventListener("click", good);
)JS");
    expect(result.succeeded(), "errors: setup succeeds");
    expect(!harness.dispatchClick(alpha, error) &&
        error == RuntimeErrorCode::UnknownIdentifier,
        "errors: Event-using callback failure is contained");
    click(harness, beta, error, "errors: unrelated listener after failure");
    expectNumber(harness, "goodCount", 1.0,
        "errors: subsequent listener still executes");
    result = harness.execute("var retainedType = saved.type;");
    expect(result.succeeded(), "generation: retained Event type remains safe");
    expectString(harness, "retainedType", "click",
        "generation: retained Event keeps immutable type");

    expect(harness.invalidateDocumentGeneration(error),
        "generation: host generation invalidates");
    expect(harness.hostAdapter().clickListenerCount() == 0u,
        "generation: invalidation clears listener records");
    result = harness.execute("saved.target.id;");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "generation: retained Event target fails closed after replacement");

    const char* replacement =
        "<html><body><button id=\"fresh\" type=\"button\">Fresh</button></body></html>";
    expect(harness.replaceHtml("file:///js12-generation-b.html", replacement,
        error), "generation: replacement document loads");
    expect(harness.hostAdapter().clickHandlerCount() == 0u,
        "generation: replacement has no stale handlers");
    result = harness.execute(
        "var fresh = document.getElementById(\"fresh\");"
        "var freshCount = 0;"
        "function freshHandler(event) { freshCount = freshCount + 1; }"
        "fresh.addEventListener(\"click\", freshHandler);");
    expect(result.succeeded(), "generation: new document listener registers");
    const std::uint64_t fresh = serialById(harness, "fresh");
    click(harness, fresh, error, "generation: new document click");
    expectNumber(harness, "freshCount", 1.0,
        "generation: new document callback still works");
}

} // namespace

int main()
{
    testEventPropertiesAndCallbackArguments();
    testIndependentElementIdentity();
    testRemovalCapacityAndStress();
    testErrorsAndNavigationSafety();
    if (failures != 0) {
        std::cerr << failures << " JS12 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS12 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
