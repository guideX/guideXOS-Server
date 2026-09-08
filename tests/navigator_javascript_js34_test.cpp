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
<form id="formA">
  <input id="a" type="text" name="a" value="alpha">
  <select id="selectA" name="mode">
    <option id="optionA" value="a" selected>Alpha</option>
    <option id="optionB" value="b">Bravo</option>
    <option id="optionC" value="c">Charlie</option>
  </select>
  <textarea id="notes" name="notes">notes</textarea>
  <input id="check" type="checkbox" name="check" value="yes" checked>
  <button id="button" type="button">Button</button>
  <input id="submit" type="submit" value="Submit">
  <input id="reset" type="reset" value="Reset">
</form>
<input id="unowned" type="text" value="outside">
<form id="formB">
  <input id="b" type="text" name="b" value="bravo">
  <select id="selectB" name="other">
    <option id="otherA" value="red" selected>Red</option>
    <option id="otherB" value="blue">Blue</option>
  </select>
</form>
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
    if (value->isBoolean())
        expect(value->booleanValue() == expected, label + ": value");
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

void expectString(const NavigatorScriptExecutionHarness& harness,
    const char* name, const std::string& expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::String, label + ": String");
    if (value->isString()) {
        const std::string actual = harness.runtime().stringValue(*value);
        expect(actual == expected, label + ": value (actual=" + actual +
            ", expected=" + expected + ")");
    }
}

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": error code");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(harness.loadHtml("file:///js34.html", kFixture, error),
        "fixture: loads");
    expect(error == RuntimeErrorCode::None, "fixture: no load error");
    expect(harness.relayout(), "fixture: relayout");
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

void testFormCollectionsAndIdentity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var formA = document.getElementById("formA");
var formB = document.getElementById("formB");
var a = document.getElementById("a");
var selectA = document.getElementById("selectA");
var notes = document.getElementById("notes");
var check = document.getElementById("check");
var button = document.getElementById("button");
var submit = document.getElementById("submit");
var reset = document.getElementById("reset");
var unowned = document.getElementById("unowned");
var elements = formA.elements;
var formCollectionExists = elements !== undefined && elements !== null;
var actualFormLength = elements.length;
var actualConvenienceLength = formA.length;
var formLength = elements.length === 7;
var convenienceLength = formA.length === 7;
var lengthsAgree = elements.length === formA.length;
var ordering = elements[0] === a && elements[1] === selectA &&
    elements[2] === notes && elements[3] === check &&
    elements[4] === button && elements[5] === submit && elements[6] === reset;
var index0IsA = elements[0] === a;
var index1IsSelect = elements[1] === selectA;
var index2IsNotes = elements[2] === notes;
var index3IsCheck = elements[3] === check;
var index4IsButton = elements[4] === button;
var index5IsSubmit = elements[5] === submit;
var index6IsReset = elements[6] === reset;
var canonicalIdentity = elements[0] === document.getElementById("a") &&
    elements[1] === selectA;
var collectionIdentity = elements === formA.elements;
var outOfRange = elements[999] === undefined && elements[7] === undefined;
var otherFormIsolated = formB.elements.length === 2 &&
    formB.elements[0] === document.getElementById("b") &&
    formB.elements[1] === document.getElementById("selectB");
var unownedExcluded = elements[7] !== unowned &&
    formA.elements.length === 7;
var replacementIsUnsupported = true;
)JS");
    expect(result.succeeded(), "form collection: read projection");
    expectBoolean(harness, "formCollectionExists", true,
        "form.elements: exists");
    expectNumber(harness, "actualFormLength", 7,
        "form.elements: actual length");
    expectNumber(harness, "actualConvenienceLength", 7,
        "form.length: actual length");
    expectBoolean(harness, "formLength", true, "form.elements: length");
    expectBoolean(harness, "convenienceLength", true, "form.length");
    expectBoolean(harness, "lengthsAgree", true, "form lengths agree");
    expectBoolean(harness, "ordering", true, "form elements: document order");
    expectBoolean(harness, "index0IsA", true, "form elements: index 0");
    expectBoolean(harness, "index1IsSelect", true, "form elements: index 1");
    expectBoolean(harness, "index2IsNotes", true, "form elements: index 2");
    expectBoolean(harness, "index3IsCheck", true, "form elements: index 3");
    expectBoolean(harness, "index4IsButton", true, "form elements: index 4");
    expectBoolean(harness, "index5IsSubmit", true, "form elements: index 5");
    expectBoolean(harness, "index6IsReset", true, "form elements: index 6");
    expectBoolean(harness, "canonicalIdentity", true,
        "form elements: canonical identity");
    expectBoolean(harness, "collectionIdentity", true,
        "form elements: canonical collection");
    expectBoolean(harness, "outOfRange", true,
        "form elements: indexed miss");
    expectBoolean(harness, "otherFormIsolated", true,
        "form elements: multiple forms isolated");
    expectBoolean(harness, "unownedExcluded", true,
        "form elements: unowned excluded");

    expectError(harness.execute("formA.elements = null;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "form.elements: replacement read-only");
    expectError(harness.execute("formA.length = 1;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "form.length: read-only");
    expectError(harness.execute("formA.elements[0] = unowned;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "form.elements index: structural replacement unsupported");
}

void testOptionCollectionsAndCurrentState()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var select = document.getElementById("selectA");
var optionA = document.getElementById("optionA");
var optionB = document.getElementById("optionB");
var optionC = document.getElementById("optionC");
var options = select.options;
var optionsExists = options !== undefined && options !== null;
var optionsLength = options.length === 3;
var selectLength = select.length === 3;
var optionLengthsAgree = options.length === select.length;
var optionOrdering = options[0] === optionA && options[1] === optionB &&
    options[2] === optionC;
var optionIdentity = options[0] === options[0] &&
    options[1] === document.getElementById("optionB");
var optionOutOfRange = options[99] === undefined;
var independentSelect = document.getElementById("selectB");
var independentOptions = independentSelect.options;
var selectsIsolated = independentOptions.length === 2 &&
    independentOptions[0] === document.getElementById("otherA") &&
    options.length === 3;
var initialState = optionA.selected && !optionB.selected &&
    !optionC.selected && optionA.defaultSelected &&
    !optionB.defaultSelected && !optionC.defaultSelected &&
    select.selectedIndex === 0 && select.value === "a";
var optionValue = optionB.value === "b";
)JS");
    expect(result.succeeded(), "option collection: read projection");
    expectBoolean(harness, "optionsExists", true, "select.options: exists");
    expectBoolean(harness, "optionsLength", true, "select.options: length");
    expectBoolean(harness, "selectLength", true, "select.length");
    expectBoolean(harness, "optionLengthsAgree", true,
        "select lengths agree");
    expectBoolean(harness, "optionOrdering", true,
        "options: document order");
    expectBoolean(harness, "optionIdentity", true,
        "options: stable canonical identity");
    expectBoolean(harness, "optionOutOfRange", true,
        "options: indexed miss");
    expectBoolean(harness, "selectsIsolated", true,
        "options: independent selects isolated");
    expectBoolean(harness, "initialState", true,
        "option: initial current/default state");
    expectBoolean(harness, "optionValue", true, "option.value: getter");

    expectError(harness.execute("select.options = null;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "select.options: replacement read-only");
    expectError(harness.execute("select.length = 1;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "select.length: read-only");
    expectError(harness.execute("select.options[0] = optionC;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "select.options index: structural replacement unsupported");
    expectError(harness.execute("optionB.value = \"other\";"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "option.value: bounded getter is read-only");
}

void testCurrentDefaultResetAndSubmit()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t formSerial = serialById(harness, "formA");
    const ScriptResult setup = harness.execute(R"JS(
var form = document.getElementById("formA");
var select = document.getElementById("selectA");
var optionA = document.getElementById("optionA");
var optionB = document.getElementById("optionB");
var optionC = document.getElementById("optionC");
var inputCount = 0;
var changeCount = 0;
var submittedValue = "none";
form.addEventListener("input", function() { inputCount = inputCount + 1; });
form.addEventListener("change", function() { changeCount = changeCount + 1; });
form.addEventListener("submit", function(event) {
    submittedValue = select.value;
    event.preventDefault();
});
var initial = optionA.selected && optionA.defaultSelected;
optionB.defaultSelected = true;
var defaultWriteKeepsCurrent = optionB.defaultSelected &&
    !optionA.defaultSelected && optionA.selected &&
    !optionB.selected && select.selectedIndex === 0 && select.value === "a";
optionC.selected = true;
var currentWriteKeepsDefault = optionC.selected && !optionB.selected &&
    optionB.defaultSelected && !optionC.defaultSelected &&
    select.selectedIndex === 2 && select.value === "c";
var silentOptionWrite = inputCount === 0 && changeCount === 0;
select.options[1].selected = true;
var indexedSelectedWrite = optionB.selected && !optionA.selected &&
    !optionC.selected && select.selectedIndex === 1 && select.value === "b";
select.selectedIndex = 2;
var selectedIndexCoherent = optionC.selected && !optionB.selected &&
    select.value === "c";
select.value = "a";
var valueCoherent = optionA.selected && !optionC.selected &&
    select.selectedIndex === 0;
optionA.selected = false;
var selectedFalseClears = select.selectedIndex === -1 &&
    select.value === "" && !optionA.selected && !optionB.selected &&
    !optionC.selected;
optionA.selected = false;
var selectedFalseNoOp = select.selectedIndex === -1 && select.value === "";
optionC.selected = true;
var beforeReset = optionC.selected && optionB.defaultSelected &&
    select.value === "c";
form.reset();
var resetUsesDefault = optionB.selected && !optionA.selected &&
    !optionC.selected && select.selectedIndex === 1 && select.value === "b";
var eventsRemainSilent = inputCount === 0 && changeCount === 0;
optionC.selected = true;
)JS");
    expect(setup.succeeded(), "selection: setup and mutations");
    expectBoolean(harness, "initial", true, "selection: initial state");
    expectBoolean(harness, "defaultWriteKeepsCurrent", true,
        "defaultSelected: current state unchanged");
    expectBoolean(harness, "currentWriteKeepsDefault", true,
        "selected: default state unchanged");
    expectBoolean(harness, "silentOptionWrite", true,
        "selected/defaultSelected: no input/change");
    expectBoolean(harness, "indexedSelectedWrite", true,
        "options index: selected write");
    expectBoolean(harness, "selectedIndexCoherent", true,
        "selectedIndex: option projection");
    expectBoolean(harness, "valueCoherent", true,
        "value: option projection");
    expectBoolean(harness, "selectedFalseClears", true,
        "selected=false: clears current selection");
    expectBoolean(harness, "selectedFalseNoOp", true,
        "selected=false: unselected no-op");
    expectBoolean(harness, "beforeReset", true,
        "selection: current/default distinction before reset");
    expectBoolean(harness, "resetUsesDefault", true,
        "reset: updated defaultSelected");
    expectBoolean(harness, "eventsRemainSilent", true,
        "reset/selection: no synthetic events");

    const ScriptResult indexedSubmit = harness.execute(
        "form.elements[5].click();");
    expect(indexedSubmit.succeeded(), "submit: indexed control path dispatches");
    expectString(harness, "submittedValue", "c",
        "submit: current selection is serialized");

    const ScriptResult resetSetup = harness.execute(
        "form.reset(); submittedValue = \"none\";");
    expect(resetSetup.succeeded(), "reset/submit: restore default setup");
    expect(harness.dispatchSubmit(formSerial, error),
        "reset/submit: form dispatches");
    expect(error == RuntimeErrorCode::None,
        "reset/submit: no dispatch error");
    expectString(harness, "submittedValue", "b",
        "reset/submit: restored default is serialized");

    const ScriptResult afterClick = harness.execute(
        "form.elements[6].click(); var resetClickRestored = select.value === \"b\" && select.options[1].selected;");
    expect(afterClick.succeeded(), "reset: indexed click observation");
    expectBoolean(harness, "resetClickRestored", true,
        "reset: indexed click restores defaults");
}

void testFormIntegrationAndNestedDispatch()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t buttonSerial = serialById(harness, "button");
    const ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var a = document.getElementById("a");
var button = document.getElementById("button");
var check = document.getElementById("check");
var nestedBefore = false;
var nestedAfter = false;
var sawButton = false;
var nested = false;
button.addEventListener("click", function(event) {
    nestedBefore = event.target === button && event.currentTarget === button &&
        event.eventPhase === 2;
    sawButton = form.elements[4] === button &&
        form.elements[0].value === "alpha";
    if (!nested) {
        nested = true;
        button.click();
    }
    nestedAfter = event.target === button && event.currentTarget === button &&
        event.eventPhase === 2 && form.elements[3] === check;
});
var focusReady = form.elements[0] === a;
a.focus();
var indexedFocus = document.activeElement === form.elements[0] &&
    document.activeElement === a;
var defaultCheckedProjection = form.elements[3].defaultChecked &&
    form.elements[3] === check;
)JS");
    expect(result.succeeded(), "integration: setup");
    expect(harness.focusedElementSerial() == serialById(harness, "a"),
        "integration: indexed focus reaches authoritative focus");
    expectBoolean(harness, "focusReady", true,
        "integration: indexed element usable");
    expectBoolean(harness, "indexedFocus", true,
        "integration: indexed focus/activeElement identity");
    expectBoolean(harness, "defaultCheckedProjection", true,
        "integration: JS33 defaultChecked projection");
    expect(harness.dispatchClick(buttonSerial, error),
        "integration: indexed button click path");
    expect(error == RuntimeErrorCode::None,
        "integration: nested button click no error");
    expectBoolean(harness, "nestedBefore", true,
        "integration: nested event metadata before");
    expectBoolean(harness, "nestedAfter", true,
        "integration: nested event metadata after");
    expectBoolean(harness, "sawButton", true,
        "integration: collection read during dispatch");
}

void testLifecycleAndGenerationSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var select = document.getElementById("selectA");
var oldElements = form.elements;
var oldOptions = select.options;
var oldOption = oldOptions[1];
var liveBeforeGenerationChange = oldElements[0] === form.elements[0] &&
    oldOptions[1] === oldOption && oldOption.value === "b";
)JS");
    expect(result.succeeded(), "lifecycle: capture references");
    expectBoolean(harness, "liveBeforeGenerationChange", true,
        "lifecycle: live references before change");
    expect(harness.invalidateDocumentGeneration(error),
        "lifecycle: invalidate generation");
    expect(error == RuntimeErrorCode::None,
        "lifecycle: invalidation no error");
    expectError(harness.execute("oldElements.length;"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale form collection fails closed");
    expectError(harness.execute("oldOptions[0];"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale options collection fails closed");
    expectError(harness.execute("oldOption.selected;"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale option fails closed");

    NavigatorScriptExecutionHarness replacement;
    loadFixture(replacement, error);
    expect(replacement.replaceHtml("file:///js34-new.html",
        "<html><body><form id=newForm><select id=newSelect>"
        "<option value=new selected>New</option></select></form></body></html>",
        error), "lifecycle: replacement document loads");
    const ScriptResult newDocument = replacement.execute(
        "var newSelect = document.getElementById(\"newSelect\");"
        "var newOptions = newSelect.options;"
        "var newDocumentIsIndependent = newOptions.length === 1 &&"
        "newOptions[0].value === \"new\";");
    expect(newDocument.succeeded(), "lifecycle: new document projection");
    expectBoolean(replacement, "newDocumentIsIndependent", true,
        "lifecycle: new collection uses new generation");
}

void testRegressionBaseline()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var form = document.getElementById("formA");
var a = document.getElementById("a");
var check = document.getElementById("check");
var select = document.getElementById("selectA");
var button = document.getElementById("button");
var baseline = form.elements.length === 7 &&
    a.defaultValue === "alpha" && check.defaultChecked &&
    select.selectedIndex === 0 && document.activeElement === null;
var inputEvents = 0;
var changeEvents = 0;
form.addEventListener("input", function() { inputEvents = inputEvents + 1; });
form.addEventListener("change", function() { changeEvents = changeEvents + 1; });
select.options[2].selected = true;
var noSyntheticEvents = inputEvents === 0 && changeEvents === 0;
)JS");
    expect(result.succeeded(), "regression: baseline script");
    expectBoolean(harness, "baseline", true,
        "regression: JS33/JS30 baseline");
    expectBoolean(harness, "noSyntheticEvents", true,
        "regression: JS26 event baseline");
}

} // namespace

int main()
{
    testFormCollectionsAndIdentity();
    testOptionCollectionsAndCurrentState();
    testCurrentDefaultResetAndSubmit();
    testFormIntegrationAndNestedDispatch();
    testLifecycleAndGenerationSafety();
    testRegressionBaseline();
    if (failures != 0) {
        std::cerr << failures << " JS34 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS34 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
