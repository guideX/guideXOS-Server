#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body><div id="outer"><div id="parent">
<form id="formA" action="file:///js28-result.html" method="get">
<input id="user" type="text" name="user" value="parser">
<textarea id="notes" name="notes">hello</textarea>
<input id="remember" type="checkbox" name="remember" value="yes">
<input id="first" type="radio" name="mode" value="first" checked>
<input id="second" type="radio" name="mode" value="second">
<select id="mode" name="mode-select"><option value="basic" selected>Basic</option><option value="advanced">Advanced</option></select>
<button id="submitA" type="submit">Submit A</button>
<button id="plainA" type="button">Plain A</button>
</form>
<form id="formB" action="file:///js28-other.html" method="get">
<input id="other" type="text" name="other" value="other">
<button id="submitB" type="submit">Submit B</button>
</form>
</div></div></body></html>
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
    expect(result.runtimeError.code == expected, label + ": error code");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const std::string& label = "fixture")
{
    expect(harness.loadHtml("file:///js28.html", kFixture, error),
        label + ": loads");
    expect(error == RuntimeErrorCode::None, label + ": no load error");
    expect(harness.relayout(), label + ": relayout");
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
        harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0;
}

const gxos::web::DocBlock* blockById(
    const NavigatorScriptExecutionHarness& harness, const char* id)
{
    for (const gxos::web::DocBlock& block : harness.document().blocks) {
        if (block.id == id) return &block;
    }
    return nullptr;
}

void submit(NavigatorScriptExecutionHarness& harness, const char* formId,
    RuntimeErrorCode& error, bool& prevented, const std::string& label)
{
    expect(harness.dispatchSubmit(serialById(harness, formId), error,
        &prevented), label + ": dispatches");
    expect(error == RuntimeErrorCode::None, label + ": no dispatch error");
}

void testSubmitMetadataPropagationAndCancellation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var parent = document.getElementById("parent");
var user = document.getElementById("user");
var other = document.getElementById("other");
var log = "";
var submits = 0;
var preventedSeen = false;
var shouldCancel = true;
function record(prefix, event) {
    log = log + prefix + ":" + event.type + ":" +
        (event.target === form) + ":" + event.eventPhase + ":" +
        event.bubbles + ":" + event.cancelable + ";";
}
document.addEventListener("submit", function(event) { record("dc", event); }, true);
parent.addEventListener("submit", function(event) { record("pc", event); }, true);
form.addEventListener("submit", function(event) {
    submits = submits + 1;
    record("t", event);
    preventedSeen = event.defaultPrevented;
    if (shouldCancel) event.preventDefault();
});
parent.addEventListener("submit", function(event) { record("pb", event); });
document.addEventListener("submit", function(event) { record("db", event); });
form.addEventListener("input", function(event) { log = log + "unexpected-input;"; });
form.addEventListener("change", function(event) { log = log + "unexpected-change;"; });
)JS");
    expect(result.succeeded(), "metadata: setup");

    bool prevented = false;
    submit(harness, "formA", error, prevented, "cancelled submit");
    expect(prevented, "cancelled submit: adapter sees cancellation");
    expectBoolean(harness, "preventedSeen", false,
        "cancelled submit: handler sees false before preventDefault");
    expectNumber(harness, "submits", 1, "cancelled submit: one event");
    expectString(harness, "log",
        "dc:submit:true:1:true:true;pc:submit:true:1:true:true;"
        "t:submit:true:2:true:true;pb:submit:true:3:true:true;"
        "db:submit:true:3:true:true;",
        "submit: document/form propagation order");

    result = harness.execute("shouldCancel = false; preventedSeen = false;");
    expect(result.succeeded(), "uncancelled submit: reset policy");
    submit(harness, "formA", error, prevented, "uncancelled submit");
    expect(!prevented, "uncancelled submit: adapter allows default action");
    expectBoolean(harness, "preventedSeen", false,
        "uncancelled submit: new Event cancellation starts clear");
    expectNumber(harness, "submits", 2, "uncancelled submit: second event");
    expectString(harness, "log",
        "dc:submit:true:1:true:true;pc:submit:true:1:true:true;"
        "t:submit:true:2:true:true;pb:submit:true:3:true:true;"
        "db:submit:true:3:true:true;"
        "dc:submit:true:1:true:true;pc:submit:true:1:true:true;"
        "t:submit:true:2:true:true;pb:submit:true:3:true:true;"
        "db:submit:true:3:true:true;",
        "submit: cancellation does not stop propagation or leak");
}

void testListenerFeaturesAndNonCancelableRegression()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "listeners");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var user = document.getElementById("user");
var onceCount = 0;
var removedCount = 0;
var stopLog = "";
function onceHandler(event) { onceCount = onceCount + 1; }
function removedHandler(event) { removedCount = removedCount + 1; }
form.addEventListener("submit", onceHandler, { once: true });
form.addEventListener("submit", removedHandler, true);
form.removeEventListener("submit", removedHandler, true);
form.addEventListener("submit", function(event) {
    stopLog = stopLog + "a";
    event.stopPropagation();
});
form.addEventListener("submit", function(event) {
    stopLog = stopLog + "b";
    event.stopImmediatePropagation();
});
form.addEventListener("input", function(event) {
    inputCancelable = event.cancelable;
    event.preventDefault();
    inputAfterPrevent = event.defaultPrevented;
});
var inputCancelable = true;
var inputAfterPrevent = true;
form.addEventListener("change", function(event) {
    changeCancelable = event.cancelable;
    event.preventDefault();
    changeAfterPrevent = event.defaultPrevented;
});
var changeCancelable = true;
var changeAfterPrevent = true;
)JS");
    expect(result.succeeded(), "listeners: setup");
    bool prevented = false;
    submit(harness, "formA", error, prevented, "listener features first");
    submit(harness, "formA", error, prevented, "listener features second");
    expectNumber(harness, "onceCount", 1, "listener features: once");
    expectNumber(harness, "removedCount", 0, "listener features: remove");
    expectString(harness, "stopLog", "abab",
        "listener features: stop controls stay separate from cancellation");
    expect(!prevented, "listener features: stopping does not cancel default");
    expectError(harness.execute(
        "form.addEventListener(\"submi\", onceHandler);"),
        RuntimeErrorCode::HostInvalidValue, "listener names: submi rejected");
    expectError(harness.execute(
        "form.addEventListener(\"submits\", onceHandler);"),
        RuntimeErrorCode::HostInvalidValue, "listener names: submits rejected");
    expectError(harness.execute(
        "form.addEventListener(\"submitx\", onceHandler);"),
        RuntimeErrorCode::HostInvalidValue, "listener names: submitx rejected");
    expect(harness.hostAdapter().clickListenerCount() == 4u,
        "listener names: aliases do not consume slots");

    const std::uint64_t userSerial = serialById(harness, "user");
    expect(harness.focusElement(userSerial, error),
        "non-cancelable: focus user");
    expect(harness.dispatchFocusedUserEdit(65, false, error),
        "non-cancelable: input dispatch");
    expect(harness.clearFocus(error), "non-cancelable: change dispatch");
    expectBoolean(harness, "inputCancelable", false,
        "non-cancelable: input remains non-cancelable");
    expectBoolean(harness, "inputAfterPrevent", false,
        "non-cancelable: input preventDefault remains ineffective");
    expectBoolean(harness, "changeCancelable", false,
        "non-cancelable: change remains non-cancelable");
    expectBoolean(harness, "changeAfterPrevent", false,
        "non-cancelable: change preventDefault remains ineffective");
}

void testAuthoritativeStateHandlerMutationAndIsolation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "state");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var formB = document.getElementById("formB");
var user = document.getElementById("user");
var remember = document.getElementById("remember");
var second = document.getElementById("second");
var mode = document.getElementById("mode");
var formBSubmits = 0;
var inputEvents = 0;
var changeEvents = 0;
form.addEventListener("input", function() { inputEvents = inputEvents + 1; });
form.addEventListener("change", function() { changeEvents = changeEvents + 1; });
form.addEventListener("submit", function(event) {
    user.value = "normalized";
    remember.checked = false;
    second.checked = true;
    mode.value = "advanced";
});
formB.addEventListener("submit", function() { formBSubmits = formBSubmits + 1; });
user.value = "alice";
remember.checked = true;
mode.value = "basic";
)JS");
    expect(result.succeeded(), "state: setup and silent pre-submit mutations");
    const gxos::web::DocBlock* userBlock = blockById(harness, "user");
    const gxos::web::DocBlock* rememberBlock = blockById(harness, "remember");
    const gxos::web::DocBlock* secondBlock = blockById(harness, "second");
    const gxos::web::DocBlock* modeBlock = blockById(harness, "mode");
    expect(userBlock != nullptr && userBlock->inputValue == "alice",
        "state: text setter reaches authoritative block");
    expect(rememberBlock != nullptr && rememberBlock->formControl.checked,
        "state: checkbox setter reaches authoritative state");
    expect(modeBlock != nullptr && modeBlock->inputValue == "basic",
        "state: select setter reaches authoritative state");

    bool prevented = false;
    submit(harness, "formA", error, prevented, "state: handler mutation");
    expect(!prevented, "state: handler mutation leaves submit uncanceled");
    userBlock = blockById(harness, "user");
    rememberBlock = blockById(harness, "remember");
    secondBlock = blockById(harness, "second");
    modeBlock = blockById(harness, "mode");
    expect(userBlock != nullptr && userBlock->inputValue == "normalized",
        "state: default action would read post-handler text");
    expect(rememberBlock != nullptr && !rememberBlock->formControl.checked,
        "state: default action would read post-handler checkbox");
    expect(secondBlock != nullptr && secondBlock->formControl.checked,
        "state: default action would read post-handler radio");
    expect(modeBlock != nullptr && modeBlock->inputValue == "advanced",
        "state: default action would read post-handler select");
    expectNumber(harness, "inputEvents", 0,
        "state: script mutations during submit emit no input");
    expectNumber(harness, "changeEvents", 0,
        "state: script mutations during submit emit no change");

    submit(harness, "formA", error, prevented, "state: repeated submit");
    expect(!prevented, "state: repeated submit remains independent");
    submit(harness, "formB", error, prevented, "state: second form");
    expect(!prevented, "state: second form is not canceled by form A");
    expectNumber(harness, "formBSubmits", 1,
        "state: second form receives its own event");
}

void testReentrantFocusLifetimeAndReset()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "reentry");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var other = document.getElementById("other");
var retained = null;
var focusRedirects = 0;
var submitCount = 0;
form.addEventListener("submit", function(event) {
    retained = event;
    submitCount = submitCount + 1;
    other.focus();
});
)JS");
    expect(result.succeeded(), "reentry: setup");
    bool prevented = false;
    submit(harness, "formA", error, prevented, "reentry: submit");
    expect(!prevented, "reentry: focus redirect does not cancel");
    expect(harness.focusedElementSerial() == serialById(harness, "other"),
        "reentry: deferred focus redirect completes safely");
    expectNumber(harness, "submitCount", 1, "reentry: one dispatch");
    result = harness.execute("var retainedType = retained.type; var retainedDefault = retained.defaultPrevented;");
    expect(result.succeeded(), "lifetime: retained event remains readable");
    expectString(harness, "retainedType", "submit",
        "lifetime: retained event type");
    expectBoolean(harness, "retainedDefault", false,
        "lifetime: uncanceled state is not stale");

    for (int index = 0; index < 100; ++index) {
        submit(harness, "formA", error, prevented,
            "reentry: bounded repeated submit " + std::to_string(index));
        expect(!prevented, "reentry: repeated submit remains uncanceled");
    }
    expectNumber(harness, "submitCount", 101,
        "reentry: repeated dispatches are finite");
    expect(harness.replaceHtml("file:///js28-replacement.html",
        "<html><body><form id=next><button id=next-submit type=submit>Next</button></form></body></html>", error),
        "reset: replacement loads");
    expect(harness.relayout(), "reset: replacement relayout");
    expect(harness.hostAdapter().clickListenerCount() == 0u,
        "reset: submit listeners clear with document generation");
}

void testListenerCapacity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "capacity");
    for (int index = 0; index < 64; ++index) {
        const std::string source = "function submitListener" +
            std::to_string(index) + "(event) {} document.addEventListener(\"submit\", submitListener" +
            std::to_string(index) + ");";
        expect(harness.execute(source).succeeded(),
            "capacity: registration " + std::to_string(index + 1));
    }
    expect(harness.hostAdapter().clickListenerCount() == 64u,
        "capacity: submit uses the unchanged global 64-listener bound");
    expectError(harness.execute(
        "function overflowSubmit(event) {} document.addEventListener(\"submit\", overflowSubmit);"),
        RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th listener rejected");
}

} // namespace

int main()
{
    testSubmitMetadataPropagationAndCancellation();
    testListenerFeaturesAndNonCancelableRegression();
    testAuthoritativeStateHandlerMutationAndIsolation();
    testReentrantFocusLifetimeAndReset();
    testListenerCapacity();
    if (failures != 0) {
        std::cerr << failures << " JS28 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS28 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
