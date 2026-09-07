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
<html><body><div id="parent">
<form id="formA">
<input id="name" type="text" name="name" value="alice">
<textarea id="notes" name="notes">hello</textarea>
<input id="yes" type="checkbox" name="yes" value="yes" checked>
<input id="no" type="checkbox" name="no" value="no">
<input id="radioA" type="radio" name="mode" value="a" checked>
<input id="radioB" type="radio" name="mode" value="b">
<select id="mode" name="mode"><option value="a" selected>A</option><option value="b">B</option></select>
<button id="resetButton" type="reset">Reset</button>
<input id="resetInput" type="reset" value="Reset input">
<button id="plainButton" type="button">Plain</button>
<button id="submitButton" type="submit">Submit</button>
</form>
<form id="formB"><input id="other" type="text" name="other" value="other-default"></form>
<input id="unowned" type="text" value="unowned-default">
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
    expect(harness.loadHtml("file:///js31.html", kFixture, error),
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

void testResetEventAndAuthoritativeRestoration()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var parent = document.getElementById("parent");
var name = document.getElementById("name");
var notes = document.getElementById("notes");
var yes = document.getElementById("yes");
var no = document.getElementById("no");
var radioA = document.getElementById("radioA");
var radioB = document.getElementById("radioB");
var mode = document.getElementById("mode");
var resetCount = 0;
var inputCount = 0;
var changeCount = 0;
var propagation = "";
var seenInitialDefaultPrevented = false;
var shouldCancel = true;
function recordReset(event) {
    resetCount = resetCount + 1;
    seenInitialDefaultPrevented = !event.defaultPrevented;
    propagation = propagation + "target:" + (event.target === form) + ":" +
        (event.currentTarget === form) + ":" + event.eventPhase + ":" +
        event.bubbles + ":" + event.cancelable + ";";
    if (shouldCancel) event.preventDefault();
}
document.addEventListener("reset", function(event) {
    propagation = propagation + "document-capture:" + (event.target === form) + ";";
}, true);
parent.addEventListener("reset", function(event) {
    propagation = propagation + "parent-capture:" + (event.target === form) + ";";
}, true);
form.addEventListener("reset", recordReset);
parent.addEventListener("reset", function(event) {
    propagation = propagation + "parent-bubble:" + (event.currentTarget === parent) + ";";
});
document.addEventListener("reset", function(event) {
    propagation = propagation + "document-bubble:" + (event.currentTarget === document) + ";";
});
form.addEventListener("input", function() { inputCount = inputCount + 1; });
form.addEventListener("change", function() { changeCount = changeCount + 1; });
name.value = "script";
notes.value = "changed";
yes.checked = false;
no.checked = true;
radioB.checked = true;
mode.value = "b";
)JS");
    expect(result.succeeded(), "event: setup and mutations");

    result = harness.execute("form.reset();");
    expect(result.succeeded(), "cancel: form.reset call");
    expectBoolean(harness, "seenInitialDefaultPrevented", true,
        "event: defaultPrevented starts false");
    expectNumber(harness, "resetCount", 1, "cancel: reset fires once");
    expectString(harness, "propagation",
        "document-capture:true;parent-capture:true;target:true:true:2:true:true;"
        "parent-bubble:true;document-bubble:true;",
        "event: capture-target-bubble metadata");
    result = harness.execute(R"JS(
var canceledText = name.value === "script" && notes.value === "changed";
var canceledCheckbox = yes.checked === false && no.checked === true;
var canceledRadio = radioA.checked === false && radioB.checked === true;
var canceledSelect = mode.value === "b";
shouldCancel = false;
)JS");
    expect(result.succeeded(), "cancel: inspect preserved state");
    expectBoolean(harness, "canceledText", true, "cancel: text preserved");
    expectBoolean(harness, "canceledCheckbox", true, "cancel: checkbox preserved");
    expectBoolean(harness, "canceledRadio", true, "cancel: radio preserved");
    expectBoolean(harness, "canceledSelect", true, "cancel: select preserved");

    result = harness.execute("form.reset();");
    expect(result.succeeded(), "uncanceled: form.reset call");
    result = harness.execute(R"JS(
var restoredText = name.value === "alice" && notes.value === "hello";
var restoredCheckbox = yes.checked === true && no.checked === false;
var restoredRadio = radioA.checked === true && radioB.checked === false;
var restoredSelect = mode.value === "a";
var silentReset = inputCount === 0 && changeCount === 0;
var resetMetadata = resetCount === 2;
)JS");
    expect(result.succeeded(), "uncanceled: inspect restored state");
    expectBoolean(harness, "restoredText", true, "uncanceled: text defaults");
    expectBoolean(harness, "restoredCheckbox", true, "uncanceled: checkbox defaults");
    expectBoolean(harness, "restoredRadio", true, "uncanceled: radio defaults");
    expectBoolean(harness, "restoredSelect", true, "uncanceled: select defaults");
    expectBoolean(harness, "silentReset", true, "uncanceled: no input/change");
    expectBoolean(harness, "resetMetadata", true, "uncanceled: event lifetime");
}

void testTextareaBaselineFocusAndUserMutations()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "baseline");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var name = document.getElementById("name");
var notes = document.getElementById("notes");
var yes = document.getElementById("yes");
var radioA = document.getElementById("radioA");
var radioB = document.getElementById("radioB");
var mode = document.getElementById("mode");
var changes = 0;
var inputs = 0;
form.addEventListener("change", function() { changes = changes + 1; });
form.addEventListener("input", function() { inputs = inputs + 1; });
)JS");
    expect(result.succeeded(), "baseline: setup");

    expect(harness.focusElement(serialById(harness, "name"), error),
        "baseline: focus input");
    expect(harness.dispatchFocusedUserEdit(66, false, error),
        "baseline: user text edit");
    result = harness.execute(R"JS(
var editedByUser = name.value === "aliceb";
form.reset();
var focusedAfterReset = document.activeElement === name;
var restoredAfterReset = name.value === "alice";
)JS");
    expect(result.succeeded(), "baseline: reset while focused");
    expectBoolean(harness, "editedByUser", true, "baseline: user mutation");
    expectBoolean(harness, "focusedAfterReset", true,
        "baseline: activeElement remains focused");
    expectBoolean(harness, "restoredAfterReset", true,
        "baseline: focused value restored");
    result = harness.execute("name.blur(); var noStaleChange = changes === 0;");
    expect(result.succeeded(), "baseline: blur after reset");
    expectBoolean(harness, "noStaleChange", true,
        "baseline: no stale change after reset");

    result = harness.execute(R"JS(
name.value = "script";
notes.value = "script-notes";
yes.checked = false;
radioB.checked = true;
mode.value = "b";
var beforeScriptResetInput = inputs;
var beforeScriptResetChange = changes;
form.reset();
var scriptDefaults = name.value === "alice" && notes.value === "hello" &&
    yes.checked && radioA.checked && !radioB.checked && mode.value === "a";
var scriptResetSilent = inputs === beforeScriptResetInput &&
    changes === beforeScriptResetChange;
)JS");
    expect(result.succeeded(), "script mutation: reset");
    expectBoolean(harness, "scriptDefaults", true,
        "script mutation: all defaults restored");
    expectBoolean(harness, "scriptResetSilent", true,
        "script mutation: reset emits no events");

    expect(harness.focusElement(serialById(harness, "yes"), error),
        "user discrete: focus checkbox");
    expect(harness.dispatchFocusedUserFormControl(error),
        "user discrete: checkbox mutation");
    expect(harness.focusElement(serialById(harness, "radioB"), error),
        "user discrete: focus radio");
    expect(harness.dispatchFocusedUserFormControl(error),
        "user discrete: radio mutation");
    expect(harness.focusElement(serialById(harness, "mode"), error),
        "user discrete: focus select");
    expect(harness.dispatchFocusedUserFormControl(error),
        "user discrete: select mutation");
    result = harness.execute("var beforeUserResetInput = inputs; var beforeUserResetChange = changes; form.reset();");
    expect(result.succeeded(), "user discrete: reset");
    result = harness.execute(R"JS(
var userDefaults = yes.checked && radioA.checked && !radioB.checked && mode.value === "a";
var userResetSilent = inputs === beforeUserResetInput && changes === beforeUserResetChange;
)JS");
    expect(result.succeeded(), "user discrete: inspect reset");
    expectBoolean(harness, "userDefaults", true,
        "user mutation: discrete defaults restored");
    expectBoolean(harness, "userResetSilent", true,
        "user mutation: reset emits no events");
}

void testListenerMutationTimingAndCrossFormIsolation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "listener");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var formB = document.getElementById("formB");
var name = document.getElementById("name");
var yes = document.getElementById("yes");
var radioA = document.getElementById("radioA");
var radioB = document.getElementById("radioB");
var mode = document.getElementById("mode");
var other = document.getElementById("other");
var unowned = document.getElementById("unowned");
var cancel = false;
var resetCount = 0;
function mutateReset(event) {
    resetCount = resetCount + 1;
    name.value = "listener";
    yes.checked = false;
    radioB.checked = true;
    mode.value = "b";
    if (cancel) event.preventDefault();
}
form.addEventListener("reset", mutateReset);
name.value = "current";
other.value = "other-current";
unowned.value = "unowned-current";
cancel = false;
form.reset();
var uncanceledListenerMutationDefaults = name.value === "alice" && yes.checked &&
    radioA.checked && !radioB.checked && mode.value === "a";
var otherStillCurrent = other.value === "other-current";
var unownedStillCurrent = unowned.value === "unowned-current";
cancel = true;
name.value = "current-again";
form.reset();
var canceledListenerMutationRemains = name.value === "listener" &&
    !yes.checked && !radioA.checked && radioB.checked && mode.value === "b";
)JS");
    expect(result.succeeded(), "listener mutation: setup and reset");
    expectBoolean(harness, "uncanceledListenerMutationDefaults", true,
        "listener mutation: uncanceled defaults win");
    expectBoolean(harness, "otherStillCurrent", true,
        "cross form: other form unchanged");
    expectBoolean(harness, "unownedStillCurrent", true,
        "ownership: unowned control unchanged");
    expectBoolean(harness, "canceledListenerMutationRemains", true,
        "listener mutation: canceled changes remain");
    expectNumber(harness, "resetCount", 2, "listener mutation: two events");
}

void testResetButtonClickCancellationAndRouting()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "buttons");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var resetButton = document.getElementById("resetButton");
var resetInput = document.getElementById("resetInput");
var plainButton = document.getElementById("plainButton");
var submitButton = document.getElementById("submitButton");
var name = document.getElementById("name");
var resets = 0;
var submits = 0;
var cancelClick = false;
var cancelReset = false;
var stopClick = false;
form.addEventListener("reset", function(event) {
    resets = resets + 1;
    if (cancelReset) event.preventDefault();
});
form.addEventListener("submit", function(event) {
    submits = submits + 1;
    event.preventDefault();
});
resetButton.addEventListener("click", function(event) {
    if (stopClick) event.stopPropagation();
    if (cancelClick) event.preventDefault();
});
)JS");
    expect(result.succeeded(), "buttons: setup");

    result = harness.execute("name.value = \"button-current\"; resetButton.click(); var buttonRestored = name.value === \"alice\" && resets === 1;");
    expect(result.succeeded(), "buttons: allowed reset button");
    expectBoolean(harness, "buttonRestored", true,
        "buttons: button and form.reset share default action");

    result = harness.execute("name.value = \"click-canceled\"; cancelClick = true; resetButton.click(); cancelClick = false; var clickCancelPreserved = name.value === \"click-canceled\" && resets === 1;");
    expect(result.succeeded(), "buttons: click cancellation");
    expectBoolean(harness, "clickCancelPreserved", true,
        "buttons: click cancellation suppresses reset event");

    result = harness.execute("name.value = \"reset-canceled\"; cancelReset = true; resetButton.click(); cancelReset = false; var resetCancelPreserved = name.value === \"reset-canceled\" && resets === 2;");
    expect(result.succeeded(), "buttons: reset cancellation");
    expectBoolean(harness, "resetCancelPreserved", true,
        "buttons: reset cancellation preserves state");

    result = harness.execute("name.value = \"stopped-click\"; stopClick = true; resetButton.click(); stopClick = false; var stoppedClickStillResets = name.value === \"alice\" && resets === 3;");
    expect(result.succeeded(), "buttons: stopPropagation only");
    expectBoolean(harness, "stoppedClickStillResets", true,
        "buttons: click propagation stop does not cancel default");

    result = harness.execute("name.value = \"input-reset\"; resetInput.click(); var inputResetWorked = name.value === \"alice\" && resets === 4;");
    expect(result.succeeded(), "buttons: input reset activation");
    expectBoolean(harness, "inputResetWorked", true,
        "buttons: input type=reset uses same seam");

    result = harness.execute("name.value = \"plain\"; plainButton.click(); var plainDidNotReset = name.value === \"plain\" && resets === 4; submitButton.click(); var submitDidNotReset = name.value === \"plain\" && submits === 1 && resets === 4;");
    expect(result.succeeded(), "buttons: ordinary and submit routing");
    expectBoolean(harness, "plainDidNotReset", true,
        "buttons: type=button does not reset");
    expectBoolean(harness, "submitDidNotReset", true,
        "buttons: submit does not reset");
}

void testNestedSubmitResetAndResetReentryBound()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "nested");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var name = document.getElementById("name");
var submitButton = document.getElementById("submitButton");
var resetCount = 0;
var submitCount = 0;
form.addEventListener("reset", function() {
    resetCount = resetCount + 1;
    if (resetCount === 1) submitButton.click();
});
form.addEventListener("submit", function(event) {
    submitCount = submitCount + 1;
    event.preventDefault();
    if (submitCount === 1) form.reset();
});
name.value = "nested-current";
)JS");
    expect(result.succeeded(), "nested: setup");
    result = harness.execute("form.reset(); var nestedState = name.value === \"alice\" && resetCount === 2 && submitCount === 1;");
    expect(result.succeeded(), "nested: reset listener submit/reset");
    expectBoolean(harness, "nestedState", true,
        "nested: submit/reset state remains coherent");

    NavigatorScriptExecutionHarness recursive;
    loadFixture(recursive, error, "recursive");
    result = recursive.execute(R"JS(
var recursiveForm = document.getElementById("formA");
var recursiveCount = 0;
recursiveForm.addEventListener("reset", function() {
    recursiveCount = recursiveCount + 1;
    recursiveForm.reset();
});
recursiveForm.reset();
    )JS");
    expect(!result.succeeded(), "reentry: self-recursive reset is bounded");
    const Value* recursiveValue = binding(recursive, "recursiveCount");
    expect(recursiveValue != nullptr && recursiveValue->isNumber() &&
        recursiveValue->numberValue() > 0.0 &&
        recursiveValue->numberValue() <= 16.0,
        "reentry: reset recursion has the existing depth bound");
}

void testListenerSemanticsNamesCapacityAndStaleReceiver()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "listeners");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var name = document.getElementById("name");
var onceCount = 0;
var removedCount = 0;
var stopLog = "";
function onceReset() { onceCount = onceCount + 1; }
function removedReset() { removedCount = removedCount + 1; }
form.addEventListener("reset", onceReset, { once: true });
form.addEventListener("reset", removedReset, true);
form.removeEventListener("reset", removedReset, true);
form.addEventListener("reset", function(event) { stopLog = stopLog + "a"; event.stopPropagation(); });
form.addEventListener("reset", function(event) { stopLog = stopLog + "b"; event.stopImmediatePropagation(); });
)JS");
    expect(result.succeeded(), "listeners: setup");
    expectError(harness.execute("form.addEventListener(\"rese\", onceReset);"),
        RuntimeErrorCode::HostInvalidValue, "names: rese rejected");
    expectError(harness.execute("form.addEventListener(\"resets\", onceReset);"),
        RuntimeErrorCode::HostInvalidValue, "names: resets rejected");
    expectError(harness.execute("form.addEventListener(\"resetx\", onceReset);"),
        RuntimeErrorCode::HostInvalidValue, "names: resetx rejected");
    result = harness.execute("name.value = \"one\"; form.reset(); name.value = \"two\"; form.reset();");
    expect(result.succeeded(), "listeners: two reset dispatches");
    expectNumber(harness, "onceCount", 1, "listeners: once");
    expectNumber(harness, "removedCount", 0, "listeners: remove");
    expectString(harness, "stopLog", "abab",
        "listeners: stop propagation stays separate from cancellation");

    NavigatorScriptExecutionHarness capacity;
    loadFixture(capacity, error, "capacity");
    for (int index = 0; index < 64; ++index) {
        const std::string source = "function resetListener" +
            std::to_string(index) + "(event) {} document.addEventListener(\"reset\", resetListener" +
            std::to_string(index) + ");";
        expect(capacity.execute(source).succeeded(),
            "capacity: registration " + std::to_string(index + 1));
    }
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: reset uses unchanged global bound");
    expectError(capacity.execute(
        "function overflowReset(event) {} document.addEventListener(\"reset\", overflowReset);"),
        RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th listener rejected");

    NavigatorScriptExecutionHarness stale;
    loadFixture(stale, error, "stale");
    result = stale.execute("var staleForm = document.getElementById(\"formA\");");
    expect(result.succeeded(), "stale: retain form");
    expect(stale.invalidateDocumentGeneration(error),
        "stale: invalidate generation");
    expectError(stale.execute("staleForm.reset();"),
        RuntimeErrorCode::StaleHostObject, "stale: reset fails closed");
}

void testNewDocumentDefaultsAndActiveElementRegression()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "replacement");
    ScriptResult result = harness.execute(
        "var name = document.getElementById(\"name\"); name.value = \"old-current\";");
    expect(result.succeeded(), "replacement: old document mutation");
    expect(harness.replaceHtml("file:///js31-new.html",
        "<html><body><form id=newForm><input id=newName type=text value=bravo></form></body></html>",
        error), "replacement: new document loads");
    expect(harness.relayout(), "replacement: relayout");
    result = harness.execute(R"JS(
var newForm = document.getElementById("newForm");
var newName = document.getElementById("newName");
newName.value = "new-current";
newForm.reset();
var newDefault = newName.value === "bravo";
)JS");
    expect(result.succeeded(), "replacement: new reset");
    expectBoolean(harness, "newDefault", true,
        "replacement: new document owns new default");
}

} // namespace

int main()
{
    testResetEventAndAuthoritativeRestoration();
    testTextareaBaselineFocusAndUserMutations();
    testListenerMutationTimingAndCrossFormIsolation();
    testResetButtonClickCancellationAndRouting();
    testNestedSubmitResetAndResetReentryBound();
    testListenerSemanticsNamesCapacityAndStaleReceiver();
    testNewDocumentDefaultsAndActiveElementRegression();
    if (failures != 0) {
        std::cerr << failures << " JS31 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS31 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
