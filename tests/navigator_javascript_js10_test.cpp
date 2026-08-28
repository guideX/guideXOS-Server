#include "navigator_javascript/navigator_script_host.h"
#include "guide_web_html_parser.h"

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

const char* kFixture = R"HTML(
<html><body>
<button id="counter" type="button">0</button>
<button id="other" type="button">B 10</button>
<button id="bad" type="button">Bad</button>
<a id="link" href="file:///js10-target.html">Link</a>
</body></html>
)HTML";

void expect(bool condition, const std::string& message)
{
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

void expectString(const NavigatorScriptExecutionHarness& harness,
    const char* name, const std::string& expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::String, label + ": String");
    if (value->isString())
        expect(harness.runtime().stringValue(*value) == expected,
            label + ": value");
}

void expectNumber(const NavigatorScriptExecutionHarness& harness,
    const char* name, double expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::Number, label + ": Number");
    if (value->isNumber())
        expect(value->numberValue() == expected, label + ": value");
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

void testRegistrationDispatchClosureMutationAndCoexistence()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js10-fixture.html", kFixture, error),
        "JS10 fixture loads");
    expect(harness.relayout(), "initial JS10 relayout succeeds");
    const std::uint64_t counter = serialById(harness, "counter");
    const std::uint64_t other = serialById(harness, "other");
    const std::uint64_t bad = serialById(harness, "bad");
    expect(counter != 0 && other != 0 && bad != 0,
        "fixture has independent element serials");

    ScriptResult result = harness.execute(
        "var counter = document.getElementById(\"counter\");"
        "var other = document.getElementById(\"other\");"
        "var bad = document.getElementById(\"bad\");"
        "var count = 0; var otherCount = 10; var order = \"\";"
        "counter.onclick = function () { order = order + \"o\"; };"
        "function firstListener() {"
        "count = count + 1; order = order + \"l\";"
        "counter.textContent = \"Count \" + count; }"
        "counter.addEventListener(\"click\", firstListener);"
        "other.addEventListener(\"click\", function () {"
        "otherCount = otherCount + 2; other.textContent = \"B \" + otherCount; });"
        "bad.addEventListener(\"click\", function () { missingFunction(); });");
    expect(result.succeeded(), "click listeners and onclick register");
    expect(harness.hostAdapter().clickHandlerCount() == 3u,
        "coexisting handlers use one bounded record per element");
    expect(harness.hostAdapter().clickListenerCount() == 3u,
        "three listener registrations are counted");

    const std::uint64_t firstRevision = harness.layoutRevision();
    click(harness, counter, error, "counter click one");
    expect(error == RuntimeErrorCode::None, "counter click one has no error");
    expect(elementText(harness, counter) == "Count 1", "counter text is one");
    click(harness, counter, error, "counter click two");
    expect(elementText(harness, counter) == "Count 2", "counter text is two");
    click(harness, counter, error, "counter click three");
    expect(elementText(harness, counter) == "Count 3", "counter text is three");
    expectString(harness, "order", "ololol", "onclick precedes listener");
    expectNumber(harness, "count", 3.0, "closure persists through three clicks");
    expect(harness.layoutRevision() > firstRevision,
        "listener mutation increments layout revision");

    click(harness, other, error, "independent other click one");
    expect(elementText(harness, other) == "B 12",
        "independent listener updates other");
    expectNumber(harness, "count", 3.0,
        "other click does not alter counter closure");

    result = harness.execute(
        "counter.addEventListener(\"click\", firstListener);");
    expect(result.succeeded(), "duplicate listener is a safe no-op");
    expect(harness.hostAdapter().clickListenerCount() == 3u,
        "duplicate registration does not consume another listener slot");
    click(harness, counter, error, "duplicate listener click");
    expect(elementText(harness, counter) == "Count 4",
        "duplicate registration leaves the original listener active");
    expectString(harness, "order", "olololol",
        "duplicate registration preserves onclick order");

    result = harness.execute("counter.addEventListener(\"keydown\", function () {});");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "unsupported event name fails safely");
    result = harness.execute("counter.addEventListener(\"click\", 7);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "non-callable listener fails safely");
    click(harness, other, error, "good listener after registration errors");
    expect(elementText(harness, other) == "B 14",
        "registration errors do not corrupt future dispatch");

    expect(harness.dispatchClick(bad, error) == false &&
        error == RuntimeErrorCode::UnknownIdentifier,
        "throwing listener is contained");
    click(harness, other, error, "good listener after callback error");
    expect(elementText(harness, other) == "B 16",
        "good listener executes after bad listener error");
}

void testGenerationAndNavigationCleanup()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js10-generation-a.html", kFixture, error),
        "generation A loads");
    const std::uint64_t oldCounter = serialById(harness, "counter");
    expect(harness.execute(
        "var staleElement = document.getElementById(\"counter\");"
        "staleElement.addEventListener(\"click\", function () {"
        "staleElement.textContent = \"OLD\"; });").succeeded(),
        "old listener registers");
    expect(harness.hostAdapter().clickListenerCount() == 1u,
        "old listener is recorded");

    expect(harness.invalidateDocumentGeneration(error),
        "generation invalidation succeeds");
    expect(harness.hostAdapter().clickListenerCount() == 0u,
        "generation invalidation clears listeners");
    ScriptResult result = harness.execute(
        "staleElement.addEventListener(\"click\", function () {});");
    expectError(result, RuntimeErrorCode::StaleHostObject,
        "stale Element cannot register on newer generation");
    expect(harness.dispatchClick(oldCounter, error),
        "stale callback dispatch boundary fails closed");
    expect(error == RuntimeErrorCode::None,
        "stale callback dispatch has no runtime error");
    expect(elementText(harness, oldCounter) == "0",
        "stale listener cannot mutate document");

    expect(harness.replaceHtml("file:///js10-generation-b.html", kFixture, error),
        "document replacement succeeds");
    expect(harness.hostAdapter().clickHandlerCount() == 0u &&
        harness.hostAdapter().clickListenerCount() == 0u,
        "navigation reclaims all old click records");
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

void testListenerLimit()
{
    NavigatorScriptHostLimits limits;
    limits.maxClickListeners = 64u;
    NavigatorScriptExecutionHarness harness(RuntimeLimits(), limits);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js10-limit.html", boundedFixture(), error),
        "listener-limit fixture loads");

    std::string source;
    for (int index = 0; index < 64; ++index) {
        source += "var e" + std::to_string(index) +
            " = document.getElementById(\"e" + std::to_string(index) + "\");";
        source += "e" + std::to_string(index) +
            ".addEventListener(\"click\", function () { e" +
            std::to_string(index) + ".textContent = \"hit\"; });";
    }
    const ScriptResult registrations = harness.execute(source);
    expect(registrations.succeeded(), "listener registrations 1 through 64 succeed");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "listener count reaches the fixed limit");
    expect(harness.hostAdapter().clickHandlerCount() == 64u,
        "listener records remain bounded at 64");

    const ScriptResult exhausted = harness.execute(
        "var e64 = document.getElementById(\"e64\");"
        "e64.addEventListener(\"click\", function () { e64.textContent = \"hit\"; });");
    expectError(exhausted, RuntimeErrorCode::HostCallbackLimitExceeded,
        "registration 65 fails deterministically");
    const std::uint64_t first = serialById(harness, "e0");
    click(harness, first, error, "existing listener after exhaustion");
    expect(elementText(harness, first) == "hit",
        "existing listeners continue after exhaustion");
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "exhaustion does not grow listener storage");
}

} // namespace

int main()
{
    testRegistrationDispatchClosureMutationAndCoexistence();
    testGenerationAndNavigationCleanup();
    testListenerLimit();
    if (failures != 0) {
        std::cerr << failures << " JS10 test failure(s)\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS10 tests PASS\n";
    return 0;
}
