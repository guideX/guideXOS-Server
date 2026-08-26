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
<button id="target" type="button">target</button>
<button id="other" type="button">other</button>
<button id="bad" type="button">bad</button>
<a id="link" href="file:///js11-target.html">Link</a>
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

void testIdentityElementAndBasicRemoval()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js11-identity.html", error, "identity");
    const std::uint64_t target = serialById(harness, "target");
    const std::uint64_t other = serialById(harness, "other");
    expect(target != 0 && other != 0, "identity: fixture has target elements");

    ScriptResult result = harness.execute(
        "var target = document.getElementById(\"target\");"
        "var other = document.getElementById(\"other\");"
        "var count = 0;"
        "function handler() { count = count + 1; target.textContent = \"identity \" + count; }"
        "function sameCode() { count = count + 1; target.textContent = \"identity \" + count; }"
        "target.addEventListener(\"click\", handler);");
    expect(result.succeeded(), "identity: listener registers");
    expect(harness.hostAdapter().clickHandlerCount() == 1u &&
        harness.hostAdapter().clickListenerCount() == 1u,
        "identity: one listener record is retained");

    result = harness.execute(
        "target.removeEventListener(\"click\", sameCode);");
    expect(result.succeeded(), "identity: identical-code function removal succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "identity: different function identity does not remove");
    click(harness, target, error, "identity: first click");
    expectNumber(harness, "count", 1.0,
        "identity: registered callback executes");

    result = harness.execute(
        "other.removeEventListener(\"click\", handler);");
    expect(result.succeeded(), "identity: wrong-element removal is harmless");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "identity: wrong element leaves target listener installed");
    click(harness, target, error, "identity: second click");
    expectNumber(harness, "count", 2.0,
        "identity: wrong-element removal does not affect callback");

    result = harness.execute(
        "target.removeEventListener(\"click\", handler);");
    expect(result.succeeded(), "identity: matching removal succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 0u &&
        harness.hostAdapter().clickHandlerCount() == 0u,
        "identity: matching removal releases the empty record");
    click(harness, target, error, "identity: post-removal click");
    expectNumber(harness, "count", 2.0,
        "identity: removed callback no longer executes");
}

void testNonexistentRepeatedRemovalAndReregistration()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js11-repeated.html", error, "repeated");
    const std::uint64_t target = serialById(harness, "target");

    ScriptResult result = harness.execute(
        "var target = document.getElementById(\"target\");"
        "var count = 0;"
        "function never() { count = count + 100; }"
        "function handler() { count = count + 1; }"
        "target.removeEventListener(\"click\", never);"
        "target.removeEventListener(\"click\", never);");
    expect(result.succeeded(), "repeated: nonexistent removal is a no-op");
    expect(harness.hostAdapter().clickHandlerCount() == 0u &&
        harness.hostAdapter().clickListenerCount() == 0u,
        "repeated: nonexistent removal creates no record");

    result = harness.execute(
        "target.addEventListener(\"click\", handler);"
        "target.removeEventListener(\"click\", handler);"
        "target.removeEventListener(\"click\", handler);");
    expect(result.succeeded(), "repeated: removal twice is harmless");
    expect(harness.hostAdapter().clickHandlerCount() == 0u &&
        harness.hostAdapter().clickListenerCount() == 0u,
        "repeated: removal twice leaves no stale record");

    result = harness.execute(
        "target.addEventListener(\"click\", handler);");
    expect(result.succeeded(), "repeated: re-registration succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "repeated: re-registration uses one listener slot");
    click(harness, target, error, "repeated: re-registered click");
    expectNumber(harness, "count", 1.0,
        "repeated: re-registered callback executes exactly once");
}

void testOnclickIndependenceAndReadd()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js11-onclick.html", error, "onclick");
    const std::uint64_t target = serialById(harness, "target");

    ScriptResult result = harness.execute(
        "var target = document.getElementById(\"target\");"
        "var onclickCount = 0; var listenerCount = 0;"
        "function onclickHandler() { onclickCount = onclickCount + 1; }"
        "function listenerHandler() { listenerCount = listenerCount + 1; }"
        "target.onclick = onclickHandler;"
        "target.addEventListener(\"click\", listenerHandler);"
        "target.removeEventListener(\"click\", listenerHandler);");
    expect(result.succeeded(), "onclick: setup succeeds");
    click(harness, target, error, "onclick: onclick-only click");
    expectNumber(harness, "onclickCount", 1.0,
        "onclick: onclick survives listener removal");
    expectNumber(harness, "listenerCount", 0.0,
        "onclick: removed listener does not execute");

    result = harness.execute(
        "target.addEventListener(\"click\", listenerHandler);"
        "target.onclick = null;");
    expect(result.succeeded(), "onclick: unrelated onclick change succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "onclick: clearing onclick retains listener registration");
    click(harness, target, error, "onclick: listener-only click");
    expectNumber(harness, "onclickCount", 1.0,
        "onclick: clearing onclick does not invoke onclick");
    expectNumber(harness, "listenerCount", 1.0,
        "onclick: listener survives onclick clearing");

    result = harness.execute(
        "target.onclick = onclickHandler;"
        "target.removeEventListener(\"click\", listenerHandler);");
    expect(result.succeeded(), "onclick: final listener removal succeeds");
    click(harness, target, error, "onclick: final onclick-only click");
    expectNumber(harness, "onclickCount", 2.0,
        "onclick: onclick remains after listener removal");
    expectNumber(harness, "listenerCount", 1.0,
        "onclick: listener remains removed");
}

void testClosureMutationRemovalAndLayout()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js11-closure.html", error, "closure");
    const std::uint64_t target = serialById(harness, "target");

    ScriptResult result = harness.execute(
        "var target = document.getElementById(\"target\");"
        "function makeHandler(element) {"
        "  var localCount = 0;"
        "  function closure() { localCount = localCount + 1; element.textContent = \"closure \" + localCount; }"
        "  return closure;"
        "}"
        "var closureHandler = makeHandler(target);"
        "target.addEventListener(\"click\", closureHandler);");
    expect(result.succeeded(), "closure: listener registration succeeds");
    const std::uint64_t revisionBeforeClicks = harness.layoutRevision();
    click(harness, target, error, "closure: first click");
    click(harness, target, error, "closure: second click");
    expect(elementText(harness, target) == "closure 2",
        "closure: persistent captured state and DOM mutation");
    expect(harness.layoutRevision() > revisionBeforeClicks,
        "closure: callback mutation advances layout revision");

    result = harness.execute(
        "target.removeEventListener(\"click\", closureHandler);");
    expect(result.succeeded(), "closure: removal after execution succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 0u,
        "closure: removal releases listener slot");
    click(harness, target, error, "closure: post-removal click");
    expect(elementText(harness, target) == "closure 2",
        "closure: removed callback cannot mutate DOM");

    result = harness.execute(
        "target.addEventListener(\"click\", closureHandler);");
    expect(result.succeeded(), "closure: captured function can be re-registered");
    click(harness, target, error, "closure: re-registered click");
    expect(elementText(harness, target) == "closure 3",
        "closure: re-registration preserves captured environment");
}

void testIndependentCallbacksAndErrorContainment()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js11-independent.html", error, "independent");
    const std::uint64_t target = serialById(harness, "target");
    const std::uint64_t other = serialById(harness, "other");
    const std::uint64_t bad = serialById(harness, "bad");

    ScriptResult result = harness.execute(
        "var target = document.getElementById(\"target\");"
        "var other = document.getElementById(\"other\");"
        "var bad = document.getElementById(\"bad\");"
        "var targetCount = 0; var otherCount = 0;"
        "function targetHandler() { targetCount = targetCount + 1; }"
        "function otherHandler() { otherCount = otherCount + 1; }"
        "function badHandler() { missingFunction(); }"
        "target.addEventListener(\"click\", targetHandler);"
        "other.addEventListener(\"click\", otherHandler);"
        "bad.addEventListener(\"click\", badHandler);");
    expect(result.succeeded(), "independent: registrations succeed");
    click(harness, target, error, "independent: target click");
    click(harness, other, error, "independent: other click");
    expectNumber(harness, "targetCount", 1.0,
        "independent: target callback is isolated");
    expectNumber(harness, "otherCount", 1.0,
        "independent: other callback is isolated");

    expect(!harness.dispatchClick(bad, error) &&
        error == RuntimeErrorCode::UnknownIdentifier,
        "independent: callback error is contained");
    result = harness.execute(
        "bad.removeEventListener(\"click\", badHandler);");
    expect(result.succeeded(), "independent: removal after callback error succeeds");
    click(harness, other, error, "independent: good callback after error");
    expectNumber(harness, "otherCount", 2.0,
        "independent: callback error does not poison other listeners");
    expect(harness.hostAdapter().clickListenerCount() == 2u,
        "independent: only bad listener was removed");
}

void testUnsupportedAndInvalidRemoval()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js11-validation.html", error, "validation");
    const std::uint64_t target = serialById(harness, "target");

    ScriptResult result = harness.execute(
        "var target = document.getElementById(\"target\");"
        "var count = 0;"
        "function handler() { count = count + 1; }"
        "target.addEventListener(\"click\", handler);");
    expect(result.succeeded(), "validation: baseline registration succeeds");
    result = harness.execute(
        "target.removeEventListener(\"mouseover\", handler);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: unsupported event removal");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "validation: unsupported removal leaves click installed");

    result = harness.execute(
        "target.removeEventListener(\"click\", 123);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: numeric callback removal");
    result = harness.execute(
        "target.removeEventListener(\"click\", null);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "validation: null callback removal");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "validation: invalid removals preserve click listener");
    click(harness, target, error, "validation: listener after invalid removals");
    expectNumber(harness, "count", 1.0,
        "validation: valid listener still executes");
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

void testCapacityRecovery()
{
    NavigatorScriptHostLimits limits;
    limits.maxClickListeners = 64u;
    NavigatorScriptExecutionHarness harness(RuntimeLimits(), limits);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js11-capacity.html", boundedFixture(), error),
        "capacity: fixture loads");
    expect(harness.relayout(), "capacity: initial relayout succeeds");

    std::string source =
        "var hitCount = 0; function sharedHandler() { hitCount = hitCount + 1; }";
    for (int index = 0; index < 64; ++index) {
        source += "var e" + std::to_string(index) +
            " = document.getElementById(\"e" + std::to_string(index) + "\");";
        source += "e" + std::to_string(index) +
            ".addEventListener(\"click\", sharedHandler);";
    }
    ScriptResult result = harness.execute(source);
    expect(result.succeeded(), "capacity: 64 registrations succeed");
    expect(harness.hostAdapter().clickListenerCount() == 64u &&
        harness.hostAdapter().clickHandlerCount() == 64u,
        "capacity: fixed 64-record table is full");

    result = harness.execute(
        "e0.removeEventListener(\"click\", sharedHandler);");
    expect(result.succeeded(), "capacity: one listener can be removed");
    expect(harness.hostAdapter().clickListenerCount() == 63u &&
        harness.hostAdapter().clickHandlerCount() == 63u,
        "capacity: removal returns one bounded slot");

    result = harness.execute(
        "var e64 = document.getElementById(\"e64\");"
        "e64.addEventListener(\"click\", sharedHandler);");
    expect(result.succeeded(), "capacity: new listener uses released slot");
    expect(harness.hostAdapter().clickListenerCount() == 64u &&
        harness.hostAdapter().clickHandlerCount() == 64u,
        "capacity: count returns to 64 without table growth");

    for (int cycle = 0; cycle < 8; ++cycle) {
        result = harness.execute(
            "e1.removeEventListener(\"click\", sharedHandler);"
            "e1.addEventListener(\"click\", sharedHandler);");
        expect(result.succeeded(), "capacity: remove/re-add cycle succeeds");
        expect(harness.hostAdapter().clickListenerCount() == 64u,
            "capacity: remove/re-add cycle does not leak a slot");
    }
    const std::uint64_t newSerial = serialById(harness, "e64");
    click(harness, newSerial, error, "capacity: listener in reused slot clicks");
    expectNumber(harness, "hitCount", 1.0,
        "capacity: re-registered listener executes once");
}

void testNavigationAndStaleStateCleanup()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    loadFixture(harness, "file:///js11-generation-a.html", error, "generation A");
    const std::uint64_t oldTarget = serialById(harness, "target");
    ScriptResult result = harness.execute(
        "var oldElement = document.getElementById(\"target\");"
        "function oldHandler() { oldElement.textContent = \"old\"; }"
        "oldElement.addEventListener(\"click\", oldHandler);");
    expect(result.succeeded(), "generation: old listener registers");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "generation: old listener is recorded");

    expect(harness.invalidateDocumentGeneration(error),
        "generation: host generation invalidates");
    expect(harness.hostAdapter().clickListenerCount() == 0u &&
        harness.hostAdapter().clickHandlerCount() == 0u,
        "generation: invalidation clears old listener state");
    result = harness.execute(
        "oldElement.removeEventListener(\"click\", oldHandler);");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "generation: stale Element cannot remove in new generation");
    expect(harness.dispatchClick(oldTarget, error) &&
        error == RuntimeErrorCode::None,
        "generation: stale dispatch fails closed without callback");

    expect(harness.replaceHtml("file:///js11-generation-b.html", kFixture, error),
        "generation: replacement document loads");
    expect(harness.hostAdapter().clickHandlerCount() == 0u &&
        harness.hostAdapter().clickListenerCount() == 0u,
        "generation: navigation starts with no stale records");
    const std::uint64_t newTarget = serialById(harness, "target");
    result = harness.execute(
        "var newElement = document.getElementById(\"target\");"
        "var newCount = 0;"
        "function newHandler() { newCount = newCount + 1; newElement.textContent = \"new\"; }"
        "newElement.addEventListener(\"click\", newHandler);");
    expect(result.succeeded(), "generation: new listener registers");
    click(harness, newTarget, error, "generation: new document click");
    expectNumber(harness, "newCount", 1.0,
        "generation: new document listener executes");
    expect(elementText(harness, newTarget) == "new",
        "generation: stale callback cannot affect new document");
}

} // namespace

int main()
{
    testIdentityElementAndBasicRemoval();
    testNonexistentRepeatedRemovalAndReregistration();
    testOnclickIndependenceAndReadd();
    testClosureMutationRemovalAndLayout();
    testIndependentCallbacksAndErrorContainment();
    testUnsupportedAndInvalidRemoval();
    testCapacityRecovery();
    testNavigationAndStaleStateCleanup();
    if (failures != 0) {
        std::cerr << failures << " JS11 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS11 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
