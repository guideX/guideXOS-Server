#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::RuntimeHostObjectId;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<div id="panel" class="container">
  <button id="first" class="action" type="button">First</button>
  <div id="middle" class="middle"><input id="name" type="text" value="seed"></div>
  <button id="last" class="action" type="button">Last</button>
</div>
<form id="form" name="form-name" class="form">
  <div id="wrapper"><input id="nested-input" name="nested" type="text" value="nested-seed"></div>
  <select id="select" name="choice">
    <option id="option-a" value="a" selected>A</option>
    <option id="option-b" value="b">B</option>
  </select>
  <button id="form-button" type="button">Plain</button>
</form>
<form id="action-form">
  <button id="submit-action" type="submit">Submit</button>
  <button id="reset-action" type="reset">Reset</button>
  <input id="action-input" name="action" type="text" value="action-seed">
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

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label);

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0u;
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(harness.loadHtml("file:///js38.html", kFixture, error),
        "fixture: loads");
    expect(error == RuntimeErrorCode::None, "fixture: no load error");
    expect(harness.relayout(), "fixture: relayout");
}

void testStructuralTraversal()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var root = document.querySelector("html");
var body = document.querySelector("body");
var panel = document.querySelector("#panel");
var first = document.querySelector("#first");
var middle = document.querySelector("#middle");
var name = document.querySelector("#name");
var last = document.querySelector("#last");
var form = document.querySelector("#form");
var wrapper = document.querySelector("#wrapper");
var nestedInput = document.querySelector("#nested-input");
var select = document.querySelector("#select");
var optionA = document.querySelector("#option-a");
var optionB = document.querySelector("#option-b");
var actionForm = document.querySelector("#action-form");
var submitAction = document.querySelector("#submit-action");
var resetAction = document.querySelector("#reset-action");
var actionInput = document.querySelector("#action-input");
var methodsAndProperties = panel.parentElement !== undefined &&
    panel.children !== undefined && panel.childElementCount !== undefined &&
    panel.firstElementChild !== undefined &&
    panel.lastElementChild !== undefined &&
    panel.nextElementSibling !== undefined &&
    panel.previousElementSibling !== undefined;
var rootBehavior = root.parentElement === null && body.parentElement === root;
var panelChildren = panel.children;
var panelOrder = panelChildren.length === 3 &&
    panelChildren[0] === first && panelChildren[1] === middle &&
    panelChildren[2] === last;
var panelCollectionIdentity = panelChildren === panel.children;
var panelCounts = panel.childElementCount === panelChildren.length &&
    panel.childElementCount === 3;
var panelFirstLast = panel.firstElementChild === panelChildren[0] &&
    panel.lastElementChild === panelChildren[2] &&
    panel.firstElementChild === first && panel.lastElementChild === last;
var panelBoundaries = first.previousElementSibling === null &&
    last.nextElementSibling === null;
var siblingRoundTrip = first.nextElementSibling === middle &&
    middle.previousElementSibling === first &&
    middle.nextElementSibling === last && last.previousElementSibling === middle;
var directOnly = panel.children[1] === middle &&
    middle.children.length === 1 && middle.children[0] === name &&
    panel.children[0].querySelector("#name") === null;
var nestedParents = name.parentElement === middle &&
    middle.parentElement === panel && panel.parentElement === body &&
    nestedInput.parentElement === wrapper && wrapper.parentElement === form;
var parentRoundTrip = panel.children[0].parentElement === panel &&
    panel.children[1].parentElement === panel &&
    panel.children[2].parentElement === panel &&
    middle.children[0].parentElement === middle;
var formDistinction = form.children.length === 3 &&
    form.children[0] === wrapper && form.children[1] === select &&
    form.children[2] === document.querySelector("#form-button") &&
    form.elements.length === 3 && form.elements[0] === nestedInput &&
    form.children[0] !== form.elements[0] && nestedInput.parentElement === wrapper &&
    nestedInput.closest("form") === form;
var optionIdentity = select.children.length === 2 &&
    select.children[0] === optionA && select.children[1] === optionB &&
    select.children[0] === select.options[0] &&
    select.children[1] === select.options[1] &&
    select.children[0].parentElement === select;
var emptyLeaf = name.children.length === 0 && name.childElementCount === 0 &&
    name.firstElementChild === null && name.lastElementChild === null &&
    name.children[999] === undefined;
var outOfRange = panel.children[999] === undefined &&
    panel.children[3] === undefined;
var canonicalIdentity = panel.children[0] === document.querySelector("#first") &&
    panel.firstElementChild === document.querySelector("#first") &&
    panel.lastElementChild === document.querySelector("#last");
var selectors = first.parentElement.matches(".container") &&
    first.closest(".container") === first.parentElement &&
    middle.firstElementChild.closest("#panel") === panel &&
    middle.querySelector("#name") === name && panel.querySelector("#name") === name;
var controlState = nestedInput.value === "nested-seed" &&
    nestedInput.defaultValue === "nested-seed";
var siblingWalkCount = 0;
var cursor = panel.firstElementChild;
while (cursor !== null) {
  siblingWalkCount = siblingWalkCount + 1;
  cursor = cursor.nextElementSibling;
}
var walkAgreement = siblingWalkCount === panel.childElementCount &&
    panel.children[0].nextElementSibling === panel.children[1] &&
    panel.children[1].nextElementSibling === panel.children[2];
)JS");
    expect(result.succeeded(), "traversal: structural script");
    expectBoolean(harness, "methodsAndProperties", true,
        "traversal: properties exist");
    expectBoolean(harness, "rootBehavior", true, "traversal: root parent");
    expectBoolean(harness, "panelOrder", true,
        "traversal: direct children document order");
    expectBoolean(harness, "panelCollectionIdentity", true,
        "traversal: live collection identity");
    expectBoolean(harness, "panelCounts", true,
        "traversal: children/count agreement");
    expectBoolean(harness, "panelFirstLast", true,
        "traversal: first/last identity");
    expectBoolean(harness, "panelBoundaries", true,
        "traversal: sibling boundaries");
    expectBoolean(harness, "siblingRoundTrip", true,
        "traversal: sibling round trip");
    expectBoolean(harness, "directOnly", true,
        "traversal: grandchildren excluded");
    expectBoolean(harness, "nestedParents", true,
        "traversal: nested parent chain");
    expectBoolean(harness, "parentRoundTrip", true,
        "traversal: child parent round trip");
    expectBoolean(harness, "formDistinction", true,
        "traversal: form parent versus ownership");
    expectBoolean(harness, "optionIdentity", true,
        "traversal: select children/options identity");
    expectBoolean(harness, "emptyLeaf", true,
        "traversal: empty collection and indexed miss");
    expectBoolean(harness, "outOfRange", true,
        "traversal: out of range indexed miss");
    expectBoolean(harness, "canonicalIdentity", true,
        "traversal: canonical element identity");
    expectBoolean(harness, "selectors", true,
        "traversal: selector and closest integration");
    expectBoolean(harness, "controlState", true,
        "traversal: current/default control state");
    expectBoolean(harness, "walkAgreement", true,
        "traversal: sibling walk count");

    expectError(harness.execute("panel.parentElement = null;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "readonly: parentElement");
    expectError(harness.execute("panel.children = null;"),
        RuntimeErrorCode::HostPropertyReadOnly, "readonly: children");
    expectError(harness.execute("panel.childElementCount = 99;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "readonly: childElementCount");
    expectError(harness.execute("panel.firstElementChild = null;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "readonly: firstElementChild");
    expectError(harness.execute("panel.lastElementChild = null;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "readonly: lastElementChild");
    expectError(harness.execute("first.nextElementSibling = null;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "readonly: nextElementSibling");
    expectError(harness.execute("last.previousElementSibling = null;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "readonly: previousElementSibling");
    expectError(harness.execute("panel.children[0] = last;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "readonly: children index");
    expectError(harness.execute("panel.children.length = 0;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "readonly: children length");

    const std::uint64_t firstSerial = serialById(harness, "first");
    const std::uint64_t lastSerial = serialById(harness, "last");
    expect(firstSerial != 0u && lastSerial != 0u,
        "traversal: serials available for same-parent proof");
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.serial == firstSerial) element.parentSerial = 0xDEADu;
        if (element.serial == lastSerial) element.parentSerial = element.serial;
    }
    const ScriptResult malformed = harness.execute(
        "var malformedSafe = first.parentElement === null && "
        "first.nextElementSibling === null && first.previousElementSibling === null && "
        "last.parentElement === null && last.nextElementSibling === null; ");
    expect(malformed.succeeded(), "malformed: traversal script");
    expectBoolean(harness, "malformedSafe", true,
        "malformed: invalid and self parent fail closed");
}

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    if (!result.succeeded())
        expect(result.runtimeError.code == expected, label + ": error code");
}

void testEventFocusAndDefaultActions()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t firstSerial = serialById(harness, "first");
    const ScriptResult setup = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var first = panel.firstElementChild;
var actionForm = document.querySelector("#action-form");
var submitAction = actionForm.children[0];
var resetAction = actionForm.children[1];
var actionInput = actionForm.children[2];
var clickSeen = false;
var clickMetadata = false;
var focusSeen = false;
var submitSeen = false;
var resetSeen = false;
panel.addEventListener("click", function(event) {
  clickSeen = event.target === first && event.target.parentElement === panel &&
    event.currentTarget === panel && event.relatedTarget === null &&
    event.defaultPrevented === false && panel.children[0] === event.target;
  clickMetadata = event.target === first && event.currentTarget === panel &&
    event.relatedTarget === null && event.defaultPrevented === false;
});
first.addEventListener("focus", function(event) {
  focusSeen = event.target === first && event.target.parentElement === panel &&
    event.currentTarget === first && event.relatedTarget === null;
});
actionForm.addEventListener("submit", function(event) {
  submitSeen = event.target === actionForm &&
    event.target.children[0] === submitAction &&
    event.currentTarget === actionForm && event.defaultPrevented === false;
  event.preventDefault();
});
actionForm.addEventListener("reset", function(event) {
  resetSeen = event.target === actionForm &&
    event.target.children[1] === resetAction &&
    event.currentTarget === actionForm && event.defaultPrevented === false;
});
actionInput.value = "changed";
)JS");
    expect(setup.succeeded(), "integration: listener setup");
    expect(harness.dispatchClick(firstSerial, error),
        "integration: host click dispatch");
    expect(error == RuntimeErrorCode::None,
        "integration: host click no error");
    expectBoolean(harness, "clickSeen", true,
        "integration: event.target.parentElement");
    expectBoolean(harness, "clickMetadata", true,
        "integration: event metadata preserved");

    const ScriptResult click = harness.execute("first.click();");
    expect(click.succeeded(), "integration: traversal-returned click");
    const ScriptResult focus = harness.execute("first.focus();");
    expect(focus.succeeded(), "integration: traversal-returned focus");
    expect(error == RuntimeErrorCode::None,
        "integration: focus no error");
    expectBoolean(harness, "focusSeen", true,
        "integration: focus event traversal");
    const ScriptResult active = harness.execute(
        "var activeIdentity = document.activeElement === first;");
    expect(active.succeeded(), "integration: activeElement probe");
    expectBoolean(harness, "activeIdentity", true,
        "integration: activeElement canonical identity");

    const ScriptResult submit = harness.execute("actionForm.children[0].click();");
    expect(submit.succeeded(), "integration: traversal-returned submit click");
    expectBoolean(harness, "submitSeen", true,
        "integration: traversal-returned submit path");
    const ScriptResult reset = harness.execute("actionForm.children[1].click();");
    expect(reset.succeeded(), "integration: traversal-returned reset click");
    expectBoolean(harness, "resetSeen", true,
        "integration: traversal-returned reset path");
    const ScriptResult defaults = harness.execute(
        "var resetRestored = actionInput.value === \"action-seed\" && "
        "actionInput.defaultValue === \"action-seed\";");
    expect(defaults.succeeded(), "integration: reset state probe");
    expectBoolean(harness, "resetRestored", true,
        "integration: current/default state preserved");
}

void testLifecycleAndGenerationSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult saved = harness.execute(
        "var oldElement = document.querySelector(\"#name\"); "
        "var oldChildren = document.querySelector(\"#panel\").children;");
    expect(saved.succeeded(), "lifecycle: save traversal references");
    expect(harness.invalidateDocumentGeneration(error),
        "lifecycle: invalidate generation");
    expect(error == RuntimeErrorCode::None,
        "lifecycle: invalidation no error");
    const ScriptResult stale = harness.execute(
        "var staleTraversal = oldElement.parentElement === null && "
        "oldElement.nextElementSibling === null && "
        "oldElement.previousElementSibling === null && "
        "oldElement.firstElementChild === null && "
        "oldElement.lastElementChild === null && "
        "oldElement.childElementCount === 0 && oldElement.children === undefined;");
    expect(stale.succeeded(), "lifecycle: stale element traversal fails closed");
    expectBoolean(harness, "staleTraversal", true,
        "lifecycle: stale parent/sibling/children properties");
    expectError(harness.execute("oldChildren.length;"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale children collection length");
    expectError(harness.execute("oldChildren[0];"),
        RuntimeErrorCode::StaleHostObject,
        "lifecycle: stale children collection index");

    NavigatorScriptExecutionHarness replacement;
    loadFixture(replacement, error);
    Value oldElementValue;
    const ScriptResult capture = replacement.execute(
        "var oldForReplacement = document.querySelector(\"#first\");");
    expect(capture.succeeded(), "lifecycle: capture replacement handle");
    const Value* captured = binding(replacement, "oldForReplacement");
    expect(captured != nullptr && captured->isHostObject(),
        "lifecycle: captured host handle");
    if (captured != nullptr && captured->isHostObject())
        oldElementValue = *captured;
    expect(replacement.replaceHtml("file:///js38-new.html",
        "<html><body><div id=new-root><span id=new-child></span></div>"
        "</body></html>", error),
        "lifecycle: replacement document loads");
    expect(error == RuntimeErrorCode::None,
        "lifecycle: replacement no error");
    Value ignored;
    RuntimeErrorCode staleError = RuntimeErrorCode::None;
    const RuntimeHostObjectId oldObject = oldElementValue.hostObjectId();
    expect(!replacement.runtime().readHostPropertyForTesting(oldObject,
        "parentElement", ignored, staleError) &&
        (staleError == RuntimeErrorCode::StaleHostObject ||
            staleError == RuntimeErrorCode::InvalidHostObject),
        "lifecycle: replaced serial cannot resolve old generation");
    const ScriptResult fresh = replacement.execute(
        "var newRoot = document.querySelector(\"#new-root\"); "
        "var newChild = newRoot.children[0]; "
        "var freshTraversal = newChild.parentElement === newRoot && "
        "newRoot.children[0] === newChild && newChild.parentElement !== null;");
    expect(fresh.succeeded(), "lifecycle: new generation traversal");
    expectBoolean(replacement, "freshTraversal", true,
        "lifecycle: new generation is independent");
}

void testRegressionSurface()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var regression = document.querySelector("#panel") ===
    document.getElementById("panel") &&
    document.querySelectorAll(".action").length === 2 &&
    document.querySelector("#nested-input").closest("form") ===
        document.forms[0] &&
    document.forms[0].elements[0] === document.querySelector("#nested-input") &&
    document.querySelector("form input") === null &&
    document.querySelectorAll("form input").length === 0;
)JS");
    expect(result.succeeded(), "regression: JS34-JS37 script");
    expectBoolean(harness, "regression", true,
        "regression: selectors/forms/closest unchanged");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "regression: listener capacity remains capped");
}

} // namespace

int main()
{
    testStructuralTraversal();
    testEventFocusAndDefaultActions();
    testLifecycleAndGenerationSafety();
    testRegressionSurface();
    if (failures != 0) {
        std::cerr << failures << " JS38 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS38 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
