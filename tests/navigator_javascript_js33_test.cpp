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
<form id="form">
<input id="name" type="text" name="name" value="alice">
<textarea id="notes" name="notes">hello</textarea>
<input id="yes" type="checkbox" name="yes" value="yes" checked>
<input id="no" type="checkbox" name="no" value="no">
<input id="radioA" type="radio" name="mode" value="a" checked>
<input id="radioB" type="radio" name="mode" value="b">
<select id="mode" name="mode">
  <option value="a" selected>A</option>
  <option value="b">B</option>
  <option value="c">C</option>
</select>
<button id="button" type="button">Button</button>
</form>
<div id="plain">plain</div>
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
    if (!result.succeeded()) expect(result.runtimeError.code == expected,
        label + ": error code");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const std::string& label = "fixture")
{
    expect(harness.loadHtml("file:///js33.html", kFixture, error),
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

void testDefaultValueAndCheckedProjection()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("form");
var name = document.getElementById("name");
var notes = document.getElementById("notes");
var yes = document.getElementById("yes");
var radioA = document.getElementById("radioA");
var radioB = document.getElementById("radioB");
var mode = document.getElementById("mode");
var inputs = 0;
var changes = 0;
form.addEventListener("input", function() { inputs = inputs + 1; });
form.addEventListener("change", function() { changes = changes + 1; });
var initialDefaults = name.defaultValue === "alice" &&
    notes.defaultValue === "hello" && yes.defaultChecked &&
    radioA.defaultChecked && !radioB.defaultChecked;
name.value = "bob";
notes.value = "changed";
yes.checked = false;
var currentDoesNotChangeDefaults = name.defaultValue === "alice" &&
    notes.defaultValue === "hello" && yes.defaultChecked;
name.defaultValue = "carol";
notes.defaultValue = "world";
yes.defaultChecked = true;
var defaultWritesAreSilentAndCurrentPreserved = name.value === "bob" &&
    notes.value === "changed" && !yes.checked &&
    name.defaultValue === "carol" && notes.defaultValue === "world" &&
    yes.defaultChecked && inputs === 0 && changes === 0;
form.reset();
var resetUsesNewDefaults = name.value === "carol" &&
    notes.value === "world" && yes.checked && inputs === 0 && changes === 0;
name.value = "again";
notes.value = "again-notes";
form.reset();
var repeatedResetKeepsDefaults = name.value === "carol" &&
    notes.value === "world";
)JS");
    expect(result.succeeded(), "defaultValue/defaultChecked: projection and reset");
    expectBoolean(harness, "initialDefaults", true,
        "defaultValue: parser defaults projected");
    expectBoolean(harness, "currentDoesNotChangeDefaults", true,
        "defaultValue: current/default state distinct");
    expectBoolean(harness, "defaultWritesAreSilentAndCurrentPreserved", true,
        "defaultValue/defaultChecked: silent current-preserving writes");
    expectBoolean(harness, "resetUsesNewDefaults", true,
        "defaultValue/defaultChecked: reset uses mutated defaults");
    expectBoolean(harness, "repeatedResetKeepsDefaults", true,
        "defaultValue: repeated reset persists default");
}

void testRadioDefaultGroupAndSelectIndex()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "groups");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("form");
var radioA = document.getElementById("radioA");
var radioB = document.getElementById("radioB");
var mode = document.getElementById("mode");
var inputCount = 0;
var changeCount = 0;
form.addEventListener("input", function() { inputCount = inputCount + 1; });
form.addEventListener("change", function() { changeCount = changeCount + 1; });
var initialIndex = mode.selectedIndex === 0 && mode.value === "a";
mode.selectedIndex = 1;
var indexTracksValue = mode.selectedIndex === 1 && mode.value === "b";
var indexWriteSilent = inputCount === 0 && changeCount === 0;
mode.value = "c";
var valueTracksIndex = mode.selectedIndex === 2 && mode.value === "c";
mode.selectedIndex = -1;
var noSelection = mode.selectedIndex === -1 && mode.value === "";
mode.selectedIndex = 999;
var invalidIndexNoOp = mode.selectedIndex === -1 && mode.value === "";
mode.selectedIndex = 1;
radioA.checked = true;
radioA.defaultChecked = false;
radioB.defaultChecked = true;
var currentRadioUnaffected = radioA.checked && !radioB.checked &&
    !radioA.defaultChecked && radioB.defaultChecked;
var defaultRadioWinnerIsExclusive = radioB.defaultChecked &&
    !radioA.defaultChecked && radioA.checked;
form.reset();
var resetRadioAndSelect = radioB.checked && !radioA.checked &&
    mode.selectedIndex === 0 && mode.value === "a";
)JS");
    expect(result.succeeded(), "selectedIndex/radio: setup and mutation");
    expectBoolean(harness, "initialIndex", true,
        "selectedIndex: initial parser selection");
    expectBoolean(harness, "indexTracksValue", true,
        "selectedIndex: index selects current value");
    expectBoolean(harness, "indexWriteSilent", true,
        "selectedIndex: scripted write emits no events");
    expectBoolean(harness, "valueTracksIndex", true,
        "selectedIndex: value setter updates index");
    expectBoolean(harness, "noSelection", true,
        "selectedIndex: -1 clears bounded selection");
    expectBoolean(harness, "invalidIndexNoOp", true,
        "selectedIndex: out-of-range write is safe no-op");
    expectBoolean(harness, "currentRadioUnaffected", true,
        "defaultChecked: radio default write leaves current state");
    expectBoolean(harness, "defaultRadioWinnerIsExclusive", true,
        "defaultChecked: radio default group is exclusive");
    expectBoolean(harness, "resetRadioAndSelect", true,
        "selectedIndex/radio: reset restores document defaults");
}

void testMalformedRadioParserPolicy()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    expect(harness.loadHtml("file:///js33-malformed.html",
        "<html><body><form id=f><input id=a type=radio name=x checked>"
        "<input id=b type=radio name=x checked></form></body></html>", error),
        "radio policy: malformed fixture loads");
    expect(error == RuntimeErrorCode::None, "radio policy: no load error");
    expect(harness.relayout(), "radio policy: relayout");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("f");
var a = document.getElementById("a");
var b = document.getElementById("b");
var parserUsesFirstDefault = a.defaultChecked && !b.defaultChecked;
a.defaultChecked = false;
b.defaultChecked = true;
var scriptedUsesRequestedDefault = !a.defaultChecked && b.defaultChecked;
form.reset();
var resetUsesScriptedDefault = !a.checked && b.checked;
)JS");
    expect(result.succeeded(), "radio policy: parser and scripted defaults");
    expectBoolean(harness, "parserUsesFirstDefault", true,
        "radio policy: parser uses first document-order default");
    expectBoolean(harness, "scriptedUsesRequestedDefault", true,
        "radio policy: scripted group write is deterministic");
    expectBoolean(harness, "resetUsesScriptedDefault", true,
        "radio policy: reset uses scripted group default");
}

void testResetListenerTimingAndFocusedBaseline()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "timing");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("form");
var name = document.getElementById("name");
var yes = document.getElementById("yes");
var resetCount = 0;
var cancel = false;
form.addEventListener("reset", function(event) {
    resetCount = resetCount + 1;
    if (resetCount === 1) name.defaultValue = "listener-default";
    if (cancel) name.defaultValue = "canceled-default";
    yes.defaultChecked = false;
    if (cancel) event.preventDefault();
});
name.value = "current";
yes.checked = false;
form.reset();
var uncanceledListenerDefaultsWin = name.value === "listener-default" &&
    !yes.checked && name.defaultValue === "listener-default";
name.value = "preserved";
yes.checked = true;
cancel = true;
form.reset();
var canceledKeepsCurrentAndDefault = name.value === "preserved" &&
    yes.checked && name.defaultValue === "canceled-default" &&
    !yes.defaultChecked;
cancel = false;
form.reset();
var nextResetUsesCanceledDefault = name.value === "canceled-default" &&
    !yes.checked && resetCount === 3;
)JS");
    expect(result.succeeded(), "reset timing: listener mutation setup");
    expectBoolean(harness, "uncanceledListenerDefaultsWin", true,
        "reset listener: uncanceled reset reads new defaults");
    expectBoolean(harness, "canceledKeepsCurrentAndDefault", true,
        "reset listener: canceled reset preserves mutation");
    expectBoolean(harness, "nextResetUsesCanceledDefault", true,
        "reset listener: canceled default persists");

    expect(harness.focusElement(serialById(harness, "name"), error),
        "focused reset: focus input");
    result = harness.execute(
        "name.value = \"focused-current\"; name.defaultValue = \"focused-default\"; form.reset();"
        "var focusedReset = document.activeElement === name && name.value === \"focused-default\";");
    expect(result.succeeded(), "focused reset: reset after default mutation");
    expectBoolean(harness, "focusedReset", true,
        "focused reset: active owner and current value remain coherent");
    result = harness.execute("name.blur(); var noStaleChange = true;");
    expect(result.succeeded(), "focused reset: blur after synchronized baseline");
    expectBoolean(harness, "noStaleChange", true,
        "focused reset: blur has no stale reset change");
}

void testLifecycleUnsupportedAndReadOnlySafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "lifecycle");
    ScriptResult result = harness.execute(
        "var oldName = document.getElementById(\"name\"); oldName.defaultValue = \"old-default\";");
    expect(result.succeeded(), "lifecycle: old default mutation");
    expect(harness.invalidateDocumentGeneration(error),
        "lifecycle: invalidate old generation");
    expectError(harness.execute("oldName.defaultValue;"),
        RuntimeErrorCode::StaleHostObject, "stale: default getter fails closed");
    expectError(harness.execute("oldName.defaultValue = \"leak\";"),
        RuntimeErrorCode::StaleHostObject, "stale: default setter fails closed");

    NavigatorScriptExecutionHarness replacement;
    loadFixture(replacement, error, "replacement");
    result = replacement.execute(
        "var oldName = document.getElementById(\"name\"); oldName.defaultValue = \"old-default\";");
    expect(result.succeeded(), "replacement: old default mutation");
    expect(replacement.replaceHtml("file:///js33-new.html",
        "<html><body><form id=newForm><input id=newName type=text value=bravo></form></body></html>",
        error), "replacement: new document loads");
    result = replacement.execute(
        "var newForm = document.getElementById(\"newForm\");"
        "var newName = document.getElementById(\"newName\");"
        "newForm.reset(); var newDocumentDefault = newName.defaultValue === \"bravo\" && newName.value === \"bravo\";");
    expect(result.succeeded(), "replacement: new defaults query");
    expectBoolean(replacement, "newDocumentDefault", true,
        "replacement: old default does not leak");

    NavigatorScriptExecutionHarness unsupported;
    loadFixture(unsupported, error, "unsupported");
    result = unsupported.execute(
        "var plain = document.getElementById(\"plain\"); var unsupportedDefaultValue = plain.defaultValue === undefined;");
    expect(result.succeeded(), "unsupported: div.defaultValue is safe");
    expectBoolean(unsupported, "unsupportedDefaultValue", true,
        "unsupported: div.defaultValue");
    result = unsupported.execute(
        "var button = document.getElementById(\"button\"); var unsupportedDefaultChecked = button.defaultChecked === undefined;");
    expect(result.succeeded(), "unsupported: button.defaultChecked is safe");
    expectBoolean(unsupported, "unsupportedDefaultChecked", true,
        "unsupported: button.defaultChecked");
    result = unsupported.execute(
        "var name = document.getElementById(\"name\"); var unsupportedSelectedIndex = name.selectedIndex === undefined;");
    expect(result.succeeded(), "unsupported: input.selectedIndex is safe");
    expectBoolean(unsupported, "unsupportedSelectedIndex", true,
        "unsupported: input.selectedIndex");
    expectError(unsupported.execute(
        "document.activeElement = null;"),
        RuntimeErrorCode::HostPropertyReadOnly, "regression: activeElement remains read-only");
    expectError(unsupported.execute(
        "var mode = document.getElementById(\"mode\"); mode.selectedIndex = \"1\";"),
        RuntimeErrorCode::HostPropertyWriteFailed, "selectedIndex: numeric assignment required");
}

void testEventMetadataAndReentry()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "events");
    ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("form");
var name = document.getElementById("name");
var log = "";
var resetCount = 0;
var sawInputMetadata = false;
var sawChangeMetadata = false;
form.addEventListener("input", function(event) {
    sawInputMetadata = event.target === name && event.currentTarget === form &&
        event.eventPhase === 3;
    log = log + "i:" + (event.target === name) + ":" +
        (event.currentTarget === form) + ":" + event.eventPhase + ";";
    name.defaultValue = "from-input";
});
form.addEventListener("change", function(event) {
    sawChangeMetadata = event.target === name && event.currentTarget === form &&
        event.eventPhase === 3;
    log = log + "c:" + (event.target === name) + ":" +
        (event.currentTarget === form) + ":" + event.eventPhase + ";";
});
form.addEventListener("reset", function(event) {
    resetCount = resetCount + 1;
    log = log + "r:" + (event.target === form) + ":" +
        (event.currentTarget === form) + ":" + event.eventPhase + ";";
    if (resetCount === 1) form.reset();
});
)JS");
    expect(result.succeeded(), "events: listener setup");
    expect(harness.focusElement(serialById(harness, "name"), error),
        "events: focus input");
    expect(harness.dispatchFocusedUserEdit(66, false, error),
        "events: user input dispatch");
    result = harness.execute("name.blur();");
    expect(result.succeeded(), "events: blur dispatch");
    result = harness.execute("resetCount = 0; form.reset(); var metadataSafe = sawInputMetadata && sawChangeMetadata && resetCount === 2;");
    expect(result.succeeded(), "events: reset re-entry remains bounded");
    expectBoolean(harness, "metadataSafe", true,
        "events: cached metadata survives property mutation");
}

} // namespace

int main()
{
    testDefaultValueAndCheckedProjection();
    testRadioDefaultGroupAndSelectIndex();
    testMalformedRadioParserPolicy();
    testResetListenerTimingAndFocusedBaseline();
    testLifecycleUnsupportedAndReadOnlySafety();
    testEventMetadataAndReentry();
    if (failures != 0) {
        std::cerr << failures << " JS33 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS33 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
