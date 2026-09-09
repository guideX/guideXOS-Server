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
<form id="login" name="signin">
  <input id="username" name="id-wins" type="text" value="alice">
  <input id="named-user" name="username" type="text" value="named">
  <input id="dup-first" name="duplicate-control" type="text" value="first">
  <input id="dup-second" name="duplicate-control" type="text" value="second">
  <textarea id="notes" name="notes">hello</textarea>
  <input id="check" name="check" type="checkbox" value="yes" checked>
  <input id="radio-a" name="mode" type="radio" value="a" checked>
  <input id="radio-b" name="mode" type="radio" value="b">
  <select id="select" name="choice">
    <option id="choice-a" value="a" selected>A</option>
    <option id="choice-b" value="b">B</option>
  </select>
  <button id="action-by-id" name="action" type="button">Action</button>
  <input id="submit" name="submit" type="submit" value="Submit">
  <input id="reset" name="reset" type="reset" value="Reset">
</form>
<form id="duplicate-a" name="duplicate">
  <input id="duplicate-a-input" name="value" type="text" value="a">
</form>
<form id="duplicate-b" name="duplicate">
  <input id="duplicate-b-input" name="value" type="text" value="b">
</form>
<form id="id-target" name="other-name"></form>
<form id="other-form" name="id-target"></form>
<form id="case-form" name="Login"></form>
<form id="empty-form" name=""></form>
<input id="unowned" name="username" type="text" value="outside">
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
    expect(harness.loadHtml("file:///js35.html", kFixture, error),
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

void testDocumentFormsAndNamedPolicy()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var forms = document.forms;
var login = document.getElementById("login");
var duplicateA = document.getElementById("duplicate-a");
var duplicateB = document.getElementById("duplicate-b");
var idTarget = document.getElementById("id-target");
var otherForm = document.getElementById("other-form");
var caseForm = document.getElementById("case-form");
var emptyForm = document.getElementById("empty-form");
var formsExists = forms !== undefined && forms !== null;
var formsIdentity = forms === document.forms;
var formsLength = forms.length === 7;
var formsOrder = forms[0] === login && forms[1] === duplicateA &&
    forms[2] === duplicateB && forms[3] === idTarget &&
    forms[4] === otherForm && forms[5] === caseForm &&
    forms[6] === emptyForm;
var indexedIdentity = forms[0] === document.getElementById("login") &&
    forms[3] === idTarget;
var indexedMiss = forms[999] === undefined && forms[7] === undefined;
var idLookup = forms["login"] === login;
var nameLookup = forms["signin"] === login;
var duplicateNameFirst = forms["duplicate"] === duplicateA;
var idPrecedence = forms["id-target"] === idTarget &&
    forms["other-name"] === idTarget;
var exactCase = forms["Login"] === caseForm;
var caseSensitive = forms["login2"] === undefined &&
    forms["LoGiN"] === undefined;
var exactOnly = forms["dup"] === undefined && forms["duplicate2"] === undefined;
var emptyNameSkipped = forms[""] === undefined;
var numericStringIsIndex = forms["0"] === forms[0] &&
    forms["999"] === undefined;
var missingName = forms["missing"] === undefined;
)JS");
    expect(result.succeeded(), "document.forms: read projection");
    expectBoolean(harness, "formsExists", true, "document.forms: exists");
    expectBoolean(harness, "formsIdentity", true, "document.forms: identity");
    // The named script binding is deliberately separate because property
    // expressions are not copied into the runtime environment.
    const ScriptResult lengthProbe = harness.execute(
        "var actualFormsLength = document.forms.length;");
    expect(lengthProbe.succeeded(), "document.forms: length probe");
    expectNumber(harness, "actualFormsLength", 7,
        "document.forms: actual length");
    expectBoolean(harness, "formsLength", true, "document.forms: length");
    expectBoolean(harness, "formsOrder", true, "document.forms: document order");
    expectBoolean(harness, "indexedIdentity", true, "document.forms: identity");
    expectBoolean(harness, "indexedMiss", true, "document.forms: indexed miss");
    expectBoolean(harness, "idLookup", true, "document.forms: id lookup");
    expectBoolean(harness, "nameLookup", true, "document.forms: name lookup");
    expectBoolean(harness, "duplicateNameFirst", true,
        "document.forms: duplicate name first match");
    expectBoolean(harness, "idPrecedence", true,
        "document.forms: id precedence");
    expectBoolean(harness, "exactCase", true,
        "document.forms: exact case");
    expectBoolean(harness, "caseSensitive", true,
        "document.forms: case sensitivity");
    expectBoolean(harness, "exactOnly", true,
        "document.forms: exact matching");
    expectBoolean(harness, "emptyNameSkipped", true,
        "document.forms: empty name skipped");
    expectBoolean(harness, "numericStringIsIndex", true,
        "document.forms: numeric string indexed");
    expectBoolean(harness, "missingName", true,
        "document.forms: missing name");
    expectError(harness.execute("document.forms = null;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "document.forms: replacement read-only");
    expectError(harness.execute("document.forms.length = 0;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "document.forms.length: read-only");
}

void testNamedControlsAndIntegration()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t actionSerial = serialById(harness, "action-by-id");
    const ScriptResult result = harness.execute(R"JS(
var form = document.forms["login"];
var elements = form.elements;
var usernameById = document.getElementById("username");
var namedUser = document.getElementById("named-user");
var notes = document.getElementById("notes");
var check = document.getElementById("check");
var radioA = document.getElementById("radio-a");
var radioB = document.getElementById("radio-b");
var select = document.getElementById("select");
var action = document.getElementById("action-by-id");
var submit = document.getElementById("submit");
var reset = document.getElementById("reset");
var elementsIdentity = elements === form.elements;
var elementLength = elements.length === 12 && form.length === 12;
var idLookup = elements["username"] === usernameById;
var nameLookup = elements["notes"] === notes && elements["choice"] === select;
var conflictIdFirst = elements["username"] !== namedUser;
var duplicateFirst = elements["duplicate-control"] ===
    document.getElementById("dup-first");
var radioFirst = elements["mode"] === radioA && elements["mode"] !== radioB;
var typeCoverage = elements["check"] === check && elements["action"] === action &&
    elements["submit"] === submit && elements["reset"] === reset;
var indexedNamedIdentity = elements[0] === usernameById &&
    elements["username"] === elements[0];
var numericStringIsIndex = elements["0"] === elements[0] &&
    elements["999"] === undefined;
var missingControl = elements["missing"] === undefined;
var unownedExcluded = elements["unowned"] === undefined;
var otherFormIsolated = document.forms["duplicate"] .elements["value"] ===
    document.getElementById("duplicate-a-input");
var namedValue = elements["username"].value === "alice";
elements["notes"].value = "named-notes";
var namedMutation = notes.value === "named-notes";
elements["check"].checked = false;
var namedCheckMutation = !check.checked && check.defaultChecked;
var selectSurface = elements["choice"].value === "a" &&
    elements["choice"].selectedIndex === 0 &&
    elements["choice"].options.length === 2 &&
    elements["choice"].options[1].value === "b";
var focused = false;
elements["notes"].focus();
focused = document.activeElement === notes && form.elements["notes"] === notes;
var actionClicks = 0;
action.addEventListener("click", function() {
    actionClicks = actionClicks + 1;
    var nestedLookup = form.elements["username"] === usernameById;
    focused = focused && nestedLookup;
});
var submitted = "none";
form.addEventListener("submit", function(event) {
    submitted = form.elements["notes"].value;
    var submitLookup = form.elements["username"] === usernameById;
    focused = focused && submitLookup;
    event.preventDefault();
});
action.click();
submit.click();
var submitIntegration = actionClicks === 1 && submitted === "named-notes";
elements["username"].defaultValue = "default-user";
elements["username"].value = "current-user";
elements["reset"].click();
var resetIntegration = usernameById.value === "default-user" &&
    notes.value === "hello" && check.checked && document.activeElement === notes;
)JS");
    expect(result.succeeded(), "form.elements named: integration script");
    expectBoolean(harness, "elementsIdentity", true,
        "form.elements named: canonical collection");
    expectBoolean(harness, "elementLength", true,
        "form.elements named: length");
    expectBoolean(harness, "idLookup", true,
        "form.elements named: id lookup");
    expectBoolean(harness, "nameLookup", true,
        "form.elements named: name lookup");
    expectBoolean(harness, "conflictIdFirst", true,
        "form.elements named: id precedence");
    expectBoolean(harness, "duplicateFirst", true,
        "form.elements named: duplicate first match");
    expectBoolean(harness, "radioFirst", true,
        "form.elements named: radio first match");
    expectBoolean(harness, "typeCoverage", true,
        "form.elements named: control types");
    expectBoolean(harness, "indexedNamedIdentity", true,
        "form.elements named: indexed identity");
    expectBoolean(harness, "numericStringIsIndex", true,
        "form.elements named: numeric string indexed");
    expectBoolean(harness, "missingControl", true,
        "form.elements named: missing control");
    expectBoolean(harness, "unownedExcluded", true,
        "form.elements named: unowned excluded");
    expectBoolean(harness, "otherFormIsolated", true,
        "form.elements named: form isolation");
    expectBoolean(harness, "namedValue", true,
        "form.elements named: value projection");
    expectBoolean(harness, "namedMutation", true,
        "form.elements named: text mutation");
    expectBoolean(harness, "namedCheckMutation", true,
        "form.elements named: checkbox/default projection");
    expectBoolean(harness, "selectSurface", true,
        "form.elements named: select/options surface");
    expectBoolean(harness, "focused", true,
        "form.elements named: focus and nested lookup");
    expectBoolean(harness, "submitIntegration", true,
        "form.elements named: submit integration");
    expectBoolean(harness, "resetIntegration", true,
        "form.elements named: reset integration");
    expectError(harness.execute("form.elements[\"username\"] = namedUser;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "form.elements named: assignment read-only");
    expect(harness.focusedElementSerial() == serialById(harness, "notes"),
        "form.elements named: focus owner remains authoritative");
    expect(harness.dispatchClick(actionSerial, error),
        "form.elements named: production click dispatch");
    expect(error == RuntimeErrorCode::None,
        "form.elements named: production click has no error");
}

void testLifecycleAndZeroForms()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness empty;
    expect(empty.loadHtml("file:///empty.html", "<html><body><p>empty</p></body></html>", error),
        "zero forms: loads");
    expect(empty.relayout(), "zero forms: relayout");
    const ScriptResult zero = empty.execute(
        "var noForms = document.forms.length === 0 && document.forms[0] === undefined && document.forms[\"x\"] === undefined;");
    expect(zero.succeeded(), "zero forms: script");
    expectBoolean(empty, "noForms", true, "zero forms: bounded empty view");

    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult capture = harness.execute(R"JS(
var oldForms = document.forms;
var oldForm = oldForms[0];
var oldElements = oldForm.elements;
var oldNamed = oldElements["notes"];
var liveBeforeStale = oldForms.length === 7 && oldElements["notes"] === oldNamed;
)JS");
    expect(capture.succeeded(), "lifecycle: capture collections");
    expectBoolean(harness, "liveBeforeStale", true,
        "lifecycle: live before replacement");
    expect(harness.invalidateDocumentGeneration(error),
        "lifecycle: invalidate generation");
    expect(error == RuntimeErrorCode::None, "lifecycle: invalidate no error");
    expectError(harness.execute("oldForms.length;"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale document.forms fails closed");
    expectError(harness.execute("oldForms[0];"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale indexed form fails closed");
    expectError(harness.execute("oldElements[\"notes\"];"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale named elements fails closed");
    expectError(harness.execute("oldNamed.value;"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale named control fails closed");
}

void testRegressionBaseline()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var form = document.forms["login"];
var baseline = form.elements.length === 12 &&
    form.elements["username"].defaultValue === "alice" &&
    form.elements["check"].defaultChecked &&
    form.elements["choice"].selectedIndex === 0 && document.activeElement === null;
var inputs = 0;
var changes = 0;
form.addEventListener("input", function() { inputs = inputs + 1; });
form.addEventListener("change", function() { changes = changes + 1; });
form.elements["choice"].options[1].selected = true;
var silent = inputs === 0 && changes === 0 && form.elements["choice"].value === "b";
)JS");
    expect(result.succeeded(), "regression: JS34/JS33 baseline script");
    expectBoolean(harness, "baseline", true, "regression: baseline");
    expectBoolean(harness, "silent", true, "regression: silent selection");
}

} // namespace

int main()
{
    testDocumentFormsAndNamedPolicy();
    testNamedControlsAndIntegration();
    testLifecycleAndZeroForms();
    testRegressionBaseline();
    if (failures != 0) {
        std::cerr << failures << " JS35 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS35 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
