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
<button id="other" type="button">Other</button>
<a id="link" href="file:///js9-target.html">Link</a>
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
    if (value->isString()) {
        expect(harness.runtime().stringValue(*value) == expected,
            label + ": value");
    }
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
    if (error != RuntimeErrorCode::None)
        std::cerr << "click diagnostic: " <<
            gxos::javascript::runtimeErrorCodeName(error) << "\n";
    if (harness.documentDirty()) expect(harness.relayout(),
        label + ": controlled relayout succeeds");
    return dispatched;
}

void testRegistrationDispatchClosureAndMutation()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js9-fixture.html", kFixture, error),
        "JS9 fixture loads");
    expect(harness.relayout(), "initial JS9 relayout succeeds");
    const std::uint64_t counter = serialById(harness, "counter");
    const std::uint64_t other = serialById(harness, "other");
    const std::uint64_t link = serialById(harness, "link");
    expect(counter != 0 && other != 0 && link != 0,
        "fixture has independent element serials");

    ScriptResult result = harness.execute(
        "var counter = document.getElementById(\"counter\");"
        "var other = document.getElementById(\"other\");"
        "var link = document.getElementById(\"link\");"
        "var onclickInitiallyNull = counter.onclick === null;");
    expect(result.succeeded(), "onclick property exists on Element host");
    expectBoolean(harness, "onclickInitiallyNull", true,
        "onclick initially null");

    result = harness.execute(
        "function handlerA() { counter.textContent = \"A\"; }"
        "function handlerB() { counter.textContent = \"B\"; }"
        "counter.onclick = handlerA; counter.onclick = handlerB;"
        "var readbackIsFunction = counter.onclick === handlerB;"
        "other.onclick = function () { other.textContent = \"Other clicked\"; };");
    expect(result.succeeded(), "function assignment succeeds");
    expectBoolean(harness, "readbackIsFunction", true,
        "onclick getter exposes installed function");
    expect(harness.hostAdapter().clickHandlerCount() == 2,
        "two independent handlers retained");

    click(harness, counter, error, "counter matching click");
    expect(error == RuntimeErrorCode::None, "counter callback has no error");
    expect(elementText(harness, counter) == "B",
        "replacement handler invokes only handlerB");
    expect(elementText(harness, other) == "Other",
        "counter click does not invoke other handler");

    result = harness.execute(
        "function install() { var count = 0; counter.onclick = function () {"
        "count = count + 1; counter.textContent = count; }; } install();");
    expect(result.succeeded(), "closure handler registration succeeds");
    click(harness, counter, error, "closure click one");
    expect(elementText(harness, counter) == "1", "closure state is one");
    click(harness, counter, error, "closure click two");
    expect(elementText(harness, counter) == "2", "closure state is two");
    click(harness, counter, error, "closure click three");
    expect(elementText(harness, counter) == "3", "closure state is three");

    const std::uint64_t revisionBeforeOther = harness.layoutRevision();
    click(harness, other, error, "other matching click");
    expect(elementText(harness, other) == "Other clicked",
        "other handler mutates only other element");
    expect(elementText(harness, counter) == "3",
        "other click preserves counter state");
    expect(harness.layoutRevision() > revisionBeforeOther,
        "callback mutation triggers controlled relayout");

    result = harness.execute("counter.onclick = null; var cleared = counter.onclick === null;");
    expect(result.succeeded(), "null clears onclick");
    expectBoolean(harness, "cleared", true, "onclick clear readback");
    expect(harness.hostAdapter().clickHandlerCount() == 1,
        "clearing one handler releases one table record");
    result = harness.execute("counter.onclick = 7;");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "non-callable onclick assignment fails safely");

    result = harness.execute(
        "function first() { counter.onclick = second; counter.textContent = \"first\"; }"
        "function second() { counter.textContent = \"second\"; }"
        "counter.onclick = first;");
    expect(result.succeeded(), "self-replacement setup succeeds");
    click(harness, counter, error, "self-replacement first click");
    expect(elementText(harness, counter) == "first",
        "currently executing callback completes");
    click(harness, counter, error, "self-replacement second click");
    expect(elementText(harness, counter) == "second",
        "replacement takes effect on next click");

    result = harness.execute("counter.onclick = function () { missingFunction(); };");
    expect(result.succeeded(), "failing handler registration succeeds");
    expect(!harness.dispatchClick(counter, error) &&
        error == RuntimeErrorCode::UnknownIdentifier,
        "callback runtime error fails safely");
    result = harness.execute(
        "counter.onclick = function () { counter.textContent = \"recovered\"; };");
    expect(result.succeeded(), "realm remains usable after callback error");
    click(harness, counter, error, "post-error callback click");
    expect(elementText(harness, counter) == "recovered",
        "post-error callback executes once registered");

    result = harness.execute("var missing = document.getElementById(\"missing\"); var missingIsNull = missing === null;");
    expect(result.succeeded(), "missing lookup remains safe");
    expectBoolean(harness, "missingIsNull", true, "missing element remains null");
    result = harness.execute("missing.onclick = function () {};");
    expectError(result, RuntimeErrorCode::CannotWriteProperty,
        "missing host cannot receive a handler");

    result = harness.execute("link.onclick = function () { link.textContent = \"link clicked\"; };");
    expect(result.succeeded(), "link handler registration succeeds");
    click(harness, link, error, "link direct click dispatch");
    expect(elementText(harness, link) == "link clicked",
        "link element uses the same direct callback bridge");
}

void testInlineScriptSourcesAreBoundedAndOrdered()
{
    const gxos::web::WebDocument document = gxos::web::parseHtml(
        "file:///js9-scripts.html",
        "<html><body><button id=\"a\">A</button>"
        "<script>var first = 1;</script>"
        "<script>var second = 2;</script></body></html>");
    expect(document.scriptSources.size() == 2u,
        "HTML parser preserves two inline script sources");
    if (document.scriptSources.size() == 2u) {
        expect(document.scriptSources[0].find("first") != std::string::npos,
            "inline script sources retain source order");
        expect(document.scriptSources[1].find("second") != std::string::npos,
            "second inline script source follows first");
    }
}

void testNavigationGenerationClearsHandlers()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js9-generation-a.html", kFixture, error),
        "generation A loads");
    const std::uint64_t oldCounter = serialById(harness, "counter");
    expect(harness.execute(
        "var oldCounterElement = document.getElementById(\"counter\");"
        "oldCounterElement.onclick = function () { oldCounterElement.textContent = \"OLD\"; };").succeeded(),
        "old generation handler installs");
    expect(harness.hostAdapter().clickHandlerCount() == 1,
        "old generation handler is recorded");
    expect(harness.replaceHtml("file:///js9-generation-b.html", kFixture, error),
        "generation B replaces document");
    expect(harness.hostAdapter().clickHandlerCount() == 0,
        "navigation releases old callback records");
    const std::uint64_t newCounter = serialById(harness, "counter");
    expect(newCounter != 0, "new generation counter exists");
    expect(harness.dispatchClick(oldCounter, error),
        "stale identity click boundary fails closed without crashing");
    expect(error == RuntimeErrorCode::None,
        "released stale callback produces no runtime error");
    expect(elementText(harness, newCounter) == "0",
        "old callback cannot mutate replacement document");
}

void testCallbackLimit()
{
    NavigatorScriptHostLimits limits;
    limits.maxClickHandlers = 2u;
    NavigatorScriptExecutionHarness harness(RuntimeLimits(), limits);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const char* fixture =
        "<html><body><button id=\"a\" type=\"button\">A</button>"
        "<button id=\"b\" type=\"button\">B</button>"
        "<button id=\"c\" type=\"button\">C</button></body></html>";
    expect(harness.loadHtml("file:///js9-limit.html", fixture, error),
        "callback-limit fixture loads");
    const ScriptResult result = harness.execute(
        "var a = document.getElementById(\"a\");"
        "var b = document.getElementById(\"b\");"
        "var c = document.getElementById(\"c\");"
        "a.onclick = function () {}; b.onclick = function () {};"
        "c.onclick = function () {};");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "callback table limit is enforced");
    expect(harness.hostAdapter().clickHandlerCount() == 2,
        "callback limit prevents accumulation beyond configured bound");
}

} // namespace

int main()
{
    testInlineScriptSourcesAreBoundedAndOrdered();
    testRegistrationDispatchClosureAndMutation();
    testNavigationGenerationClearsHandlers();
    testCallbackLimit();
    if (failures != 0) {
        std::cerr << failures << " JS9 test failure(s)\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS9 tests PASS\n";
    return 0;
}
