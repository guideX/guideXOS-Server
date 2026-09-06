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
<html><body>
<div id="root"><div id="parent">
<input id="first" type="text" value="">
<input id="second" type="text" value="">
<textarea id="area">x</textarea>
<div id="plain">plain</div>
</div></div>
</body></html>
)HTML";

const char* kInitialValueFixture = R"HTML(
<html><body><div id="parent">
<input id="first" type="text" value="abc">
<input id="second" type="text" value="">
</div></body></html>
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
    expect(result.runtimeError.code == expected, label + ": expected " +
        std::string(gxos::javascript::runtimeErrorCodeName(
            result.runtimeError.code)));
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const char* fixture = kFixture)
{
    expect(harness.loadHtml("file:///js26.html", fixture, error),
        "fixture loads");
    expect(harness.relayout(), "fixture relayout succeeds");
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

void focus(NavigatorScriptExecutionHarness& harness, const char* id,
    RuntimeErrorCode& error, const char* label = "focus")
{
    expect(harness.focusElement(serialById(harness, id), error), label);
    expect(error == RuntimeErrorCode::None, std::string(label) + ": no error");
}

void userKey(NavigatorScriptExecutionHarness& harness, int keyCode,
    RuntimeErrorCode& error, const char* label = "user key")
{
    expect(harness.dispatchFocusedUserEdit(keyCode, false, error), label);
    expect(error == RuntimeErrorCode::None, std::string(label) + ": no error");
}

void testBasicInputChangeAndMetadata()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var box = document.getElementById("first");
var log = "";
var observed = "";
var inputCount = 0;
var changeCount = 0;
var inputPrevented = false;
var changePrevented = false;
box.addEventListener("keydown", function(event) { log = log + event.type + ":" + event.key + ";"; });
box.addEventListener("input", function(event) {
    inputCount = inputCount + 1;
    observed = box.value;
    inputPrevented = event.cancelable;
    log = log + event.type + ":" + box.value + ":" + event.bubbles + ":" + event.cancelable + ";";
    event.preventDefault();
});
box.addEventListener("change", function(event) {
    changeCount = changeCount + 1;
    changePrevented = event.cancelable;
    log = log + event.type + ":" + box.value + ":" + event.bubbles + ":" + event.cancelable + ";";
    event.preventDefault();
});
box.addEventListener("keyup", function(event) { log = log + event.type + ":" + event.key + ";"; });
)JS");
    expect(result.succeeded(), "basic: listeners install");
    focus(harness, "first", error, "basic: focus");
    userKey(harness, 65, error, "basic: printable key");
    expect(harness.execute("var boxValue = box.value;").succeeded(),
        "basic: value getter after edit");
    expectString(harness, "boxValue", "a", "basic: value changed");
    expectString(harness, "observed", "a", "basic: input observes post-edit value");
    expectNumber(harness, "inputCount", 1, "basic: one input");
    expectBoolean(harness, "inputPrevented", false, "basic: input non-cancelable");
    expect(harness.execute("box.blur();").succeeded(),
        "basic: programmatic blur");
    expectNumber(harness, "changeCount", 1, "basic: one committed change");
    expectBoolean(harness, "changePrevented", false,
        "basic: change non-cancelable");
    expectString(harness, "log",
        "keydown:a;input:a:true:false;keyup:a;change:a:true:false;",
        "basic: keyboard/input/change order");

    expect(harness.execute("box.focus(); box.blur();").succeeded(),
        "basic: no-edit focus session");
    expectNumber(harness, "changeCount", 1, "basic: no edit no change");

    expect(harness.execute("box.focus(); box.focus();").succeeded(),
        "basic: repeated focus");
    expectNumber(harness, "changeCount", 1,
        "basic: repeated focus does not commit");
    expect(harness.execute("box.blur();").succeeded(),
        "basic: final blur");
}

void testFocusCommitRulesAndSessions()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var a = document.getElementById("first");
var b = document.getElementById("second");
var log = "";
var changes = 0;
a.addEventListener("blur", function() { log = log + "blur;"; });
a.addEventListener("focusout", function() { log = log + "focusout;"; });
a.addEventListener("change", function() { changes = changes + 1; log = log + "change;"; });
b.addEventListener("focus", function() { log = log + "focus-b;"; });
)JS");
    expect(result.succeeded(), "focus: listeners install");
    focus(harness, "first", error, "focus: first");
    userKey(harness, 65, error, "focus: edit first");
    expect(harness.focusElement(serialById(harness, "second"), error),
        "focus: A-to-B transfer");
    expectString(harness, "log", "blur;focusout;change;focus-b;",
        "focus: blur/focusout/change/focus order");
    expectNumber(harness, "changes", 1, "focus: transfer commits once");

    expect(harness.execute("a.focus(); b.blur();").succeeded(),
        "focus: unrelated blur and refocus");
    expectNumber(harness, "changes", 1,
        "focus: unrelated blur does not commit another control");
    userKey(harness, 66, error, "focus: second edit");
    expect(harness.execute("a.focus();").succeeded(),
        "focus: same-element focus");
    expectNumber(harness, "changes", 1,
        "focus: same-element focus does not commit");
    expect(harness.execute("a.blur();").succeeded(),
        "focus: same-element session blur");
    expectNumber(harness, "changes", 2, "focus: second session commits");

    expect(harness.execute("a.focus(); a.blur();").succeeded(),
        "focus: third no-edit session");
    expectNumber(harness, "changes", 2,
        "focus: baseline resets for no-edit session");
    expect(harness.execute("a.focus();").succeeded(),
        "focus: fourth session");
    userKey(harness, 67, error, "focus: third edit");
    expect(harness.execute("a.blur();").succeeded(),
        "focus: fourth session blur");
    expectNumber(harness, "changes", 3,
        "focus: later session commits once");
}

void testBackspaceDeleteAndRevert()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, kInitialValueFixture);
    ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("first");
var second = document.getElementById("second");
var inputCount = 0;
var changeCount = 0;
first.addEventListener("input", function() { inputCount = inputCount + 1; });
first.addEventListener("change", function() { changeCount = changeCount + 1; });
second.addEventListener("input", function() { inputCount = inputCount + 100; });
second.addEventListener("change", function() { changeCount = changeCount + 100; });
)JS");
    expect(result.succeeded(), "delete: listeners install");
    focus(harness, "first", error, "delete: first focus");
    userKey(harness, 8, error, "delete: backspace changes value");
    expect(harness.execute("var afterBackspace = first.value;").succeeded(),
        "delete: read after backspace");
    expectString(harness, "afterBackspace", "ab", "delete: backspace value");
    userKey(harness, 37, error, "delete: move caret left");
    userKey(harness, 46, error, "delete: delete changes value");
    expect(harness.execute("var afterDelete = first.value;").succeeded(),
        "delete: read after delete");
    expectString(harness, "afterDelete", "a", "delete: delete value");
    expectNumber(harness, "inputCount", 2, "delete: two changing edits");
    expect(harness.execute("first.blur();").succeeded(), "delete: commit");
    expectNumber(harness, "changeCount", 1, "delete: changed commit");

    focus(harness, "second", error, "delete: empty field focus");
    userKey(harness, 8, error, "delete: empty backspace");
    userKey(harness, 46, error, "delete: empty delete");
    expectNumber(harness, "inputCount", 2,
        "delete: no value change no input");
    expect(harness.execute("second.blur();").succeeded(),
        "delete: empty field blur");
    expectNumber(harness, "changeCount", 1,
        "delete: empty field no change");

    NavigatorScriptExecutionHarness revert;
    loadFixture(revert, error, kInitialValueFixture);
    result = revert.execute(R"JS(
var first = document.getElementById("first");
var inputs = 0;
var changes = 0;
first.addEventListener("input", function() { inputs = inputs + 1; });
first.addEventListener("change", function() { changes = changes + 1; });
)JS");
    expect(result.succeeded(), "revert: listeners install");
    focus(revert, "first", error, "revert: focus");
    userKey(revert, 8, error, "revert: remove character");
    userKey(revert, 67, error, "revert: restore character");
    expect(revert.execute("first.blur();").succeeded(), "revert: blur");
    expectNumber(revert, "inputs", 2, "revert: two input events");
    expectNumber(revert, "changes", 0,
        "revert: baseline comparison suppresses change");
}

void testScriptValueAndReentrantBehavior()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var box = document.getElementById("first");
var inputs = 0;
var changes = 0;
box.addEventListener("input", function() { inputs = inputs + 1; });
box.addEventListener("change", function() { changes = changes + 1; });
box.value = "hello";
var afterAssignment = box.value;
)JS");
    expect(result.succeeded(), "script value: assignment succeeds");
    expectString(harness, "afterAssignment", "hello",
        "script value: getter observes assignment");
    expectNumber(harness, "inputs", 0, "script value: no input");
    expectNumber(harness, "changes", 0, "script value: no change");
    expect(harness.execute("box.focus(); box.value = \"world\";").succeeded(),
        "script value: active assignment");
    expectNumber(harness, "inputs", 0,
        "script value: active assignment no input");
    expectNumber(harness, "changes", 0,
        "script value: active assignment no immediate change");
    expect(harness.execute("box.blur();").succeeded(),
        "script value: active assignment blur");
    expectNumber(harness, "changes", 1,
        "script value: final changed value commits");

    expect(harness.execute("box.value = \"hello\"; box.focus();").succeeded(),
        "script value: restore and focus");
    userKey(harness, 65, error, "script value: user edit after restore");
    expect(harness.execute("box.value = \"hello\"; box.blur();").succeeded(),
        "script value: restore baseline before blur");
    expectNumber(harness, "inputs", 1,
        "script value: restore does not synthesize input");
    expectNumber(harness, "changes", 1,
        "script value: restore suppresses eventual change");

    NavigatorScriptExecutionHarness rewrite;
    loadFixture(rewrite, error);
    result = rewrite.execute(R"JS(
var box = document.getElementById("first");
var inputs = 0;
var changes = 0;
box.addEventListener("input", function() { inputs = inputs + 1; box.value = "normalized"; });
box.addEventListener("change", function() { changes = changes + 1; });
box.focus();
)JS");
    expect(result.succeeded(), "reentrant: rewrite setup");
    userKey(rewrite, 65, error, "reentrant: rewrite input");
    expect(rewrite.execute("var finalValue = box.value;").succeeded(),
        "reentrant: read rewritten value");
    expectString(rewrite, "finalValue", "normalized",
        "reentrant: listener rewrite value");
    expectNumber(rewrite, "inputs", 1,
        "reentrant: rewrite does not recurse input");
    expect(rewrite.execute("box.blur();").succeeded(),
        "reentrant: rewrite blur");
    expectNumber(rewrite, "changes", 1,
        "reentrant: rewrite final value commits");

    NavigatorScriptExecutionHarness inputBlur;
    loadFixture(inputBlur, error);
    result = inputBlur.execute(R"JS(
var box = document.getElementById("first");
var log = "";
box.addEventListener("input", function() { log = log + "input;"; box.blur(); });
box.addEventListener("change", function() { log = log + "change;"; });
box.focus();
)JS");
    expect(result.succeeded(), "reentrant: input blur setup");
    userKey(inputBlur, 65, error, "reentrant: input-triggered blur");
    expect(inputBlur.focusedElementSerial() == 0,
        "reentrant: input blur clears focus safely");
    expectString(inputBlur, "log", "input;change;",
        "reentrant: input blur commits deterministically");

    NavigatorScriptExecutionHarness redirect;
    loadFixture(redirect, error);
    result = redirect.execute(R"JS(
var a = document.getElementById("first");
var b = document.getElementById("second");
var log = "";
a.addEventListener("input", function() { log = log + "input;"; b.focus(); });
a.addEventListener("change", function() { log = log + "change-a;"; });
b.addEventListener("focus", function() { log = log + "focus-b;"; });
)JS");
    expect(result.succeeded(), "reentrant: focus redirect setup");
    focus(redirect, "first", error, "reentrant: redirect focus");
    userKey(redirect, 65, error, "reentrant: input focus redirect");
    expect(redirect.focusedElementSerial() == serialById(redirect, "second"),
        "reentrant: input redirect final owner");
    expectString(redirect, "log", "input;change-a;focus-b;",
        "reentrant: input redirect order");

    NavigatorScriptExecutionHarness changeRedirect;
    loadFixture(changeRedirect, error);
    result = changeRedirect.execute(R"JS(
var a = document.getElementById("first");
var b = document.getElementById("second");
var redirects = 0;
a.addEventListener("change", function() { redirects = redirects + 1; b.focus(); });
)JS");
    expect(result.succeeded(), "reentrant: change redirect setup");
    focus(changeRedirect, "first", error, "reentrant: change focus");
    userKey(changeRedirect, 65, error, "reentrant: change edit");
    expect(changeRedirect.clearFocus(error), "reentrant: change clear");
    expect(changeRedirect.focusedElementSerial() ==
        serialById(changeRedirect, "second"),
        "reentrant: change redirect final owner");
    expectNumber(changeRedirect, "redirects", 1,
        "reentrant: change redirect once");
}

void testPropagationAndListenerSemantics()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var parent = document.getElementById("parent");
var target = document.getElementById("first");
var log = "";
function record(prefix, event) {
    log = log + prefix + ":" + event.type + ":" + event.target.id + ":" +
        (event.currentTarget === document) + ":" + event.eventPhase + ":" +
        event.bubbles + ":" + event.cancelable + ";";
}
document.addEventListener("input", function(event) { record("dc", event); }, true);
parent.addEventListener("input", function(event) { record("pc", event); }, true);
target.addEventListener("input", function(event) { record("t", event); });
parent.addEventListener("input", function(event) { record("pb", event); });
document.addEventListener("input", function(event) { record("db", event); });
document.addEventListener("change", function(event) { record("dc", event); }, true);
parent.addEventListener("change", function(event) { record("pc", event); }, true);
target.addEventListener("change", function(event) { record("t", event); });
parent.addEventListener("change", function(event) { record("pb", event); });
document.addEventListener("change", function(event) { record("db", event); });
)JS");
    expect(result.succeeded(), "propagation: listeners install");
    focus(harness, "first", error, "propagation: focus");
    userKey(harness, 65, error, "propagation: input");
    expect(harness.clearFocus(error), "propagation: change");
    expectString(harness, "log",
        "dc:input:first:true:1:true:false;pc:input:first:false:1:true:false;"
        "t:input:first:false:2:true:false;pb:input:first:false:3:true:false;"
        "db:input:first:true:3:true:false;"
        "dc:change:first:true:1:true:false;pc:change:first:false:1:true:false;"
        "t:change:first:false:2:true:false;pb:change:first:false:3:true:false;"
        "db:change:first:true:3:true:false;",
        "propagation: input/change capture-target-bubble");

    NavigatorScriptExecutionHarness stop;
    loadFixture(stop, error);
    result = stop.execute(R"JS(
var parent = document.getElementById("parent");
var target = document.getElementById("first");
var stopLog = "";
target.addEventListener("input", function(event) { stopLog = stopLog + "one;"; event.stopPropagation(); });
target.addEventListener("input", function(event) { stopLog = stopLog + "two;"; });
parent.addEventListener("input", function(event) { stopLog = stopLog + "parent;"; });
)JS");
    expect(result.succeeded(), "stop: listeners install");
    focus(stop, "first", error, "stop: focus");
    userKey(stop, 65, error, "stop: input");
    expectString(stop, "stopLog", "one;two;",
        "stop: stopPropagation preserves target listeners");

    NavigatorScriptExecutionHarness immediate;
    loadFixture(immediate, error);
    result = immediate.execute(R"JS(
var target = document.getElementById("first");
var immediateLog = "";
target.addEventListener("input", function(event) { immediateLog = immediateLog + "one;"; event.stopImmediatePropagation(); });
target.addEventListener("input", function(event) { immediateLog = immediateLog + "two;"; });
)JS");
    expect(result.succeeded(), "immediate: listeners install");
    focus(immediate, "first", error, "immediate: focus");
    userKey(immediate, 65, error, "immediate: input");
    expectString(immediate, "immediateLog", "one;",
        "immediate: stopImmediatePropagation stops same target");

    NavigatorScriptExecutionHarness once;
    loadFixture(once, error);
    result = once.execute(R"JS(
var target = document.getElementById("first");
var onceCount = 0;
var removedCount = 0;
function onceHandler(event) { onceCount = onceCount + 1; }
function removedHandler(event) { removedCount = removedCount + 1; }
target.addEventListener("input", onceHandler, { once: true });
target.addEventListener("input", removedHandler, true);
target.removeEventListener("input", removedHandler, true);
)JS");
    expect(result.succeeded(), "listener semantics: setup");
    focus(once, "first", error, "listener semantics: focus");
    userKey(once, 65, error, "listener semantics: first input");
    userKey(once, 66, error, "listener semantics: second input");
    expectNumber(once, "onceCount", 1, "listener semantics: once");
    expectNumber(once, "removedCount", 0, "listener semantics: remove");

    NavigatorScriptExecutionHarness names;
    loadFixture(names, error);
    result = names.execute(
        "var target = document.getElementById(\"first\");"
        "function handler(event) {}"
        "target.addEventListener(\"input\", handler);"
        "target.addEventListener(\"change\", handler);");
    expect(result.succeeded(), "names: exact names register");
    expectError(names.execute("target.addEventListener(\"inputs\", handler);"),
        RuntimeErrorCode::HostInvalidValue, "names: inputs rejected");
    expectError(names.execute("target.addEventListener(\"inputx\", handler);"),
        RuntimeErrorCode::HostInvalidValue, "names: inputx rejected");
    expectError(names.execute("target.addEventListener(\"chang\", handler);"),
        RuntimeErrorCode::HostInvalidValue, "names: chang rejected");
    expectError(names.execute("target.addEventListener(\"changes\", handler);"),
        RuntimeErrorCode::HostInvalidValue, "names: changes rejected");
    expect(names.hostAdapter().clickListenerCount() == 2u,
        "names: rejected aliases consume no slots");

    NavigatorScriptExecutionHarness capacity;
    loadFixture(capacity, error);
    for (int index = 0; index < 64; ++index) {
        const std::string source = "function listener" + std::to_string(index) +
            "(event) {} document.addEventListener(\"input\", listener" +
            std::to_string(index) + ");";
        expect(capacity.execute(source).succeeded(),
            "capacity: registration " + std::to_string(index + 1));
    }
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: fixed listener registry remains 64");
    expectError(capacity.execute(
        "function overflow(event) {} document.addEventListener(\"change\", overflow);"),
        RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th input/change listener rejected");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: overflow does not grow registry");
}

void testTextareaAndDocumentReset()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var area = document.getElementById("area");
var valueAtInput = "";
var inputs = 0;
var changes = 0;
area.addEventListener("input", function() { inputs = inputs + 1; valueAtInput = area.value; });
area.addEventListener("change", function() { changes = changes + 1; });
area.focus();
)JS");
    expect(result.succeeded(), "textarea: setup and focus");
    userKey(harness, 65, error, "textarea: user edit");
    expectString(harness, "valueAtInput", "xa",
        "textarea: input observes updated value");
    expectNumber(harness, "inputs", 1, "textarea: input");
    expect(harness.execute("area.blur();").succeeded(), "textarea: blur");
    expectNumber(harness, "changes", 1, "textarea: change");

    NavigatorScriptExecutionHarness reset;
    loadFixture(reset, error);
    result = reset.execute(R"JS(
var box = document.getElementById("first");
var changes = 0;
box.addEventListener("change", function() { changes = changes + 1; });
box.focus();
)JS");
    expect(result.succeeded(), "reset: setup");
    userKey(reset, 65, error, "reset: pending edit");
    expect(reset.replaceHtml("file:///js26-replacement.html", kFixture, error),
        "reset: replace document");
    expect(reset.relayout(), "reset: relayout replacement");
    expect(reset.execute("var replacement = document.getElementById(\"first\");").succeeded(),
        "reset: replacement script realm");
    expect(reset.execute("var replacementValue = replacement.value;").succeeded(),
        "reset: replacement value getter");
    expectString(reset, "replacementValue", "",
        "reset: replacement starts with parser value");
    for (const gxos::web::FormRuntimeControlState& state :
        reset.document().formRuntimeState.controls) {
        expect(!state.editBaselineValid,
            "reset: no baseline survives document replacement");
    }
}

} // namespace

int main()
{
    testBasicInputChangeAndMetadata();
    testFocusCommitRulesAndSessions();
    testBackspaceDeleteAndRevert();
    testScriptValueAndReentrantBehavior();
    testPropagationAndListenerSemantics();
    testTextareaAndDocumentReset();
    if (failures != 0) {
        std::cerr << failures << " JS26 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS26 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
