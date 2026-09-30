#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

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
<!doctype html>
<html><body>
<form id="owner" name="profileForm" class="form active">
  <div id="panel" class="panel active" style="">
    <label id="label" class="label">Name</label>
    <button id="save" class="Action Primary" name="saveButton" type="button" value="button-seed">Save</button>
    <button id="locked" type="button" disabled>Locked</button>
    <input id="name" class="Field Required" name="UserName" type="TEXT" value="initial">
    <input id="agree" type="checkbox" value="yes" checked>
    <select id="choice" name="choice">
      <option id="first-option" value="One" selected>One</option>
      <option id="empty-option" value="">Empty</option>
      <option id="text-option">Text fallback</option>
    </select>
    <span id="" class="" data-empty="" data-mode="compact"></span>
    <a id="link" href="target.html">Link</a>
  </div>
</form>
<button id="outside" type="button">Outside</button>
</body></html>
)HTML";

void expect(bool condition, const std::string& label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << label << "\n";
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

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(harness.loadHtml("file:///js47.html", kFixture, error),
        "fixture parses into the authoritative document");
    expect(error == RuntimeErrorCode::None, "fixture load has no error");
    expect(harness.relayout(), "fixture relayout succeeds");
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0u;
}

gxos::web::HtmlElementRef* elementById(
    NavigatorScriptExecutionHarness& harness, const char* id)
{
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == id) return &element;
    }
    return nullptr;
}

void testRetainedValuesPresenceAndCase()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var form = document.querySelector("#owner");
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var locked = document.querySelector("#locked");
var name = document.querySelector("#name");
var agree = document.querySelector("#agree");
var choice = document.querySelector("#choice");
var option = choice.options[0];
var emptyOption = choice.options[1];
var blank = panel.querySelector("span");
var idReads = save.getAttribute("id") === "save" &&
    save.getAttribute("ID") === "save" && save.hasAttribute("id");
var classReads = save.getAttribute("class") === "Action Primary" &&
    save.getAttribute("CLASS") === "Action Primary" && save.hasAttribute("class");
var formName = form.getAttribute("name") === "profileForm" &&
    form.hasAttribute("NAME");
var inputNameTypeValue = name.getAttribute("name") === "UserName" &&
    name.getAttribute("type") === "text" &&
    name.getAttribute("value") === "initial" &&
    name.hasAttribute("name") && name.hasAttribute("type") &&
    name.hasAttribute("value");
var buttonNameTypeValue = save.getAttribute("name") === "saveButton" &&
    save.getAttribute("type") === "button" &&
    save.getAttribute("value") === "button-seed";
var booleanReads = locked.hasAttribute("disabled") &&
    locked.getAttribute("disabled") === "" &&
    agree.hasAttribute("checked") && agree.getAttribute("checked") === "" &&
    option.hasAttribute("selected") && option.getAttribute("selected") === "";
var emptyReads = blank.getAttribute("id") === "" &&
    blank.hasAttribute("id") && blank.getAttribute("class") === "" &&
    blank.hasAttribute("class") && panel.getAttribute("style") === "" &&
    panel.hasAttribute("style") && emptyOption.getAttribute("value") === "" &&
    emptyOption.hasAttribute("value");
var exactValueAndSelectorIntegration = save.matches(".Action.Primary") &&
    document.getElementById(save.getAttribute("id")) === save;
)JS");
    expect(result.succeeded(), "reads: valid retained attribute script executes");
    expectBoolean(harness, "idReads", true, "ID reads preserve exact value");
    expectBoolean(harness, "classReads", true, "class reads preserve exact text and case");
    expectBoolean(harness, "formName", true, "form name uses existing form metadata");
    expectBoolean(harness, "inputNameTypeValue", true,
        "input name/type/value use retained form metadata");
    expectBoolean(harness, "buttonNameTypeValue", true,
        "button name/type/value use retained form metadata");
    expectBoolean(harness, "booleanReads", true,
        "boolean attribute presence returns normalized empty values");
    expectBoolean(harness, "emptyReads", true,
        "present empty attributes remain distinct from missing attributes");
    expectBoolean(harness, "exactValueAndSelectorIntegration", true,
        "attribute ID reads preserve getElementById identity and class matching");
}

void testMissingInvalidAndBoundedArguments()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::string overlongName(65u, 'a');
    const ScriptResult result = harness.execute(
        "var save = document.querySelector(\"#save\");"
        "var missingReads = save.getAttribute(\"missing\") === null &&"
        " !save.hasAttribute(\"missing\");"
        "var emptyNameReads = save.getAttribute(\"\") === null &&"
        " !save.hasAttribute(\"\");"
        "var invalidNameReads = save.getAttribute(\"id \") === null &&"
        " !save.hasAttribute(\"id \");"
        "var noArgumentReads = save.getAttribute() === null &&"
        " !save.hasAttribute();"
        "var nonStringReads = save.getAttribute(123) === null &&"
        " !save.hasAttribute(true) && save.getAttribute({}) === null;"
        "var overlongNameReads = save.getAttribute(\"" + overlongName +
        "\") === null && !save.hasAttribute(\"" + overlongName + "\");"
        "var unsupportedDataReads = save.getAttribute(\"data-mode\") === null &&"
        " !save.hasAttribute(\"data-mode\");");
    expect(result.succeeded(), "arguments: missing and invalid reads do not throw");
    expectBoolean(harness, "missingReads", true, "missing returns null and false");
    expectBoolean(harness, "emptyNameReads", true, "empty names fail closed");
    expectBoolean(harness, "invalidNameReads", true,
        "malformed names fail closed");
    expectBoolean(harness, "noArgumentReads", true,
        "missing method arguments fail closed");
    expectBoolean(harness, "nonStringReads", true,
        "non-string method arguments fail closed without coercion");
    expectBoolean(harness, "overlongNameReads", true,
        "attribute names above the 64-byte bound fail closed");
    expectBoolean(harness, "unsupportedDataReads", true,
        "discarded generic data attributes remain unavailable");

    gxos::web::HtmlElementRef* save = elementById(harness, "save");
    expect(save != nullptr, "bounds: retained Element is available");
    if (save != nullptr) save->id.assign(65537u, 'x');
    const ScriptResult valueBound = harness.execute(
        "var valueBoundReads = save.getAttribute(\"id\") === null &&"
        " save.hasAttribute(\"id\");");
    expect(valueBound.succeeded(), "bounds: large retained value check executes");
    expectBoolean(harness, "valueBoundReads", true,
        "getAttribute fails closed above the 64 KiB return bound while presence remains true");
}

void testCurrentDefaultsAndPureReads()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t saveSerial = serialById(harness, "save");
    const ScriptResult stateChanges = harness.execute(R"JS(
var form = document.querySelector("#owner");
var input = document.querySelector("#name");
var agree = document.querySelector("#agree");
var option = document.querySelector("#first-option");
input.value = "changed";
agree.checked = false;
option.selected = false;
var valueDefaultStayedSeparate = input.value === "changed" &&
    input.getAttribute("value") === "initial" &&
    input.defaultValue === input.getAttribute("value");
var checkedAttributeStaysPresent = !agree.checked && agree.hasAttribute("checked") &&
    agree.getAttribute("checked") === "" && agree.defaultChecked;
var selectedAttributeStaysPresent = !option.selected &&
    option.hasAttribute("selected") && option.getAttribute("selected") === "" &&
    option.defaultSelected;
form.reset();
var resetUsesDefault = input.value === "initial" &&
    input.getAttribute("value") === "initial";
)JS");
    expect(stateChanges.succeeded(), "defaults: property edits and reset execute");
    expectBoolean(harness, "valueDefaultStayedSeparate", true,
        "value attribute follows the default while current value changes");
    expectBoolean(harness, "checkedAttributeStaysPresent", true,
        "checked presence does not alias current checked state");
    expectBoolean(harness, "selectedAttributeStaysPresent", true,
        "selected presence does not alias current selected state");
    expectBoolean(harness, "resetUsesDefault", true,
        "reset restores current value without changing the retained default attribute");

    const gxos::web::HtmlElementRef* save = elementById(harness, "save");
    const std::string idBefore = save == nullptr ? std::string() : save->id;
    const std::string classBefore = save == nullptr
        ? std::string() : save->className;
    const std::uint64_t generationBefore = harness.runtime().hostGeneration();
    const std::size_t structuralCountBefore =
        harness.document().structuralElements.size();
    const std::size_t mutationsBefore = harness.document().scriptMutationCount;
    const std::uint64_t layoutRevisionBefore = harness.layoutRevision();
    const std::uint64_t focusedBefore = harness.focusedElementSerial();
    const std::size_t listenerCountBefore =
        harness.hostAdapter().clickListenerCount();
    const ScriptResult pure = harness.execute(R"JS(
var save = document.querySelector("#save");
var input = document.querySelector("#name");
var pureReads = save.getAttribute("id") === "save" &&
    save.getAttribute("class") === "Action Primary" &&
    save.getAttribute("id") === "save" && save.hasAttribute("disabled") === false &&
    input.getAttribute("value") === "initial" &&
    input.getAttribute("name") === "UserName";
)JS");
    expect(pure.succeeded(), "purity: repeated reads execute");
    expectBoolean(harness, "pureReads", true,
        "repeated and nested reads are deterministic");
    expect(save != nullptr && save->id == idBefore &&
            save->className == classBefore,
        "purity: ID and class parser metadata are unchanged");
    expect(harness.runtime().hostGeneration() == generationBefore,
        "purity: generation is unchanged");
    expect(harness.document().structuralElements.size() == structuralCountBefore,
        "purity: structural records are unchanged");
    expect(harness.document().scriptMutationCount == mutationsBefore,
        "purity: mutation count is unchanged");
    expect(harness.layoutRevision() == layoutRevisionBefore,
        "purity: layout revision is unchanged");
    expect(harness.focusedElementSerial() == focusedBefore,
        "purity: focus is unchanged");
    expect(harness.hostAdapter().clickListenerCount() == listenerCountBefore &&
            listenerCountBefore <= 64u,
        "purity: listener registry is unchanged and bounded");
    expect(saveSerial != 0u, "purity: canonical save serial exists");
}

void testIdentityCollectionsTraversalFocusAndEvents()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t saveSerial = serialById(harness, "save");
    const std::uint64_t inputSerial = serialById(harness, "name");
    const ScriptResult identity = harness.execute(R"JS(
var form = document.querySelector("#owner");
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var input = document.querySelector("#name");
var choice = document.querySelector("#choice");
var option = choice.options[0];
var queryAndIdIdentity = document.querySelector("#save.Action.Primary") === save &&
    document.getElementById(save.getAttribute("id")) === save;
var formCollectionReads = form.elements[0].getAttribute("name") === "saveButton" &&
    form.elements["UserName"] === input &&
    form.elements["UserName"].getAttribute("id") === "name";
var traversalReads = panel.firstElementChild.getAttribute("id") === "label" &&
    panel.firstElementChild.nextElementSibling.getAttribute("class") === "Action Primary" &&
    save.parentElement.getAttribute("id") === "panel" &&
    panel.children[1] === save;
var optionIdentityReads = choice.options[0] === choice.children[0] &&
    choice.options[0].getAttribute("value") === "One" &&
    choice.children[1].getAttribute("value") === "";
)JS");
    expect(identity.succeeded(), "identity: collection and traversal reads execute");
    expectBoolean(harness, "queryAndIdIdentity", true,
        "querySelector and getElementById share canonical Element identity");
    expectBoolean(harness, "formCollectionReads", true,
        "form.elements exposes the same attribute methods");
    expectBoolean(harness, "traversalReads", true,
        "children and sibling traversal preserve attribute access");
    expectBoolean(harness, "optionIdentityReads", true,
        "select.options and children share canonical option identity");

    const ScriptResult focus = harness.execute(
        "document.querySelector(\"#name\").focus();"
        "var activeElementAttribute = document.activeElement.getAttribute(\"id\") === \"name\";");
    expect(focus.succeeded(), "focus: active Element attribute read executes");
    expectBoolean(harness, "activeElementAttribute", true,
        "activeElement retains canonical Element attribute methods");

    const std::uint64_t outsideSerial = serialById(harness, "link");
    (void)outsideSerial;
    const ScriptResult listeners = harness.execute(R"JS(
var save = document.querySelector("#save");
var panel = document.querySelector("#panel");
var locked = document.querySelector("#locked");
var eventReads = false;
var eventMetadata = false;
var nestedReads = false;
var nestedDone = false;
var nestedButton = document.querySelector("#outside");
save.addEventListener("click", function(event) {
  event.preventDefault();
  eventReads = event.target.getAttribute("id") === "save" &&
    event.target.hasAttribute("class") && !event.target.hasAttribute("disabled");
});
panel.addEventListener("click", function(event) {
  if (event.target === save) {
    var target = event.target;
    var current = event.currentTarget;
    var phase = event.eventPhase;
    var related = event.relatedTarget;
    nestedButton.click();
    nestedReads = nestedReads && event.target.getAttribute("id") === "save";
    eventMetadata = event.target === target && event.currentTarget === current &&
      current === panel && event.eventPhase === phase && phase === 3 &&
      event.relatedTarget === related && related === null &&
      event.defaultPrevented === true;
  }
});

nestedButton.addEventListener("click", function(event) {
  nestedReads = event.target.getAttribute("id") === "outside" &&
    !event.target.hasAttribute("disabled");
});
)JS");
    expect(listeners.succeeded(), "events: listener setup executes");
    bool defaultPrevented = false;
    expect(harness.dispatchClick(saveSerial, error, &defaultPrevented),
        "events: authentic click dispatch succeeds");
    expect(error == RuntimeErrorCode::None && defaultPrevented,
        "events: click default prevention is retained");
    expectBoolean(harness, "eventReads", true,
        "event.target supports getAttribute and hasAttribute");
    expectBoolean(harness, "nestedReads", true,
        "nested dispatch does not corrupt attribute resolver output");
    expectBoolean(harness, "eventMetadata", true,
        "target/currentTarget/phase/relatedTarget/defaultPrevented survive attribute reads and nested dispatch");
    expect(inputSerial != 0u, "events: focused Element serial remains valid");
}

void testStaleGenerationAndSerialReuse()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldSerial = serialById(harness, "name");
    expect(oldSerial != 0u, "stale: original Element serial exists");
    expect(harness.execute(
        "var oldInput = document.querySelector(\"#name\");").succeeded(),
        "stale: capture old canonical Element");
    expect(harness.invalidateDocumentGeneration(error),
        "stale: generation invalidation succeeds");
    expect(error == RuntimeErrorCode::None, "stale: invalidation has no error");
    const ScriptResult stale = harness.execute(R"JS(
var staleAttributeReads = oldInput.getAttribute("id") === null &&
    oldInput.hasAttribute("id") === false;
)JS");
    expect(stale.succeeded(), "stale: old methods fail closed without throwing");
    expectBoolean(harness, "staleAttributeReads", true,
        "stale Element does not resolve metadata in a later generation");

    std::string replacementHtml = kFixture;
    const std::string oldId = "id=\"name\"";
    const std::size_t position = replacementHtml.find(oldId);
    expect(position != std::string::npos,
        "stale: replacement fixture contains the old ID");
    if (position != std::string::npos)
        replacementHtml.replace(position, oldId.size(), "id=\"new-name\"");
    harness.document() = gxos::web::parseHtml(
        "file:///js47-replacement.html", replacementHtml);
    const std::uint64_t newSerial = serialById(harness, "new-name");
    expect(newSerial == oldSerial,
        "stale: replacement deliberately reuses the old logical serial");
    expect(harness.runtime().installHostGlobal("newInput", newSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "stale: install replacement-generation Element");
    const ScriptResult replacement = harness.execute(R"JS(
var replacementAttributeReads = oldInput.getAttribute("id") === null &&
    oldInput.hasAttribute("id") === false &&
    newInput.getAttribute("id") === "new-name" &&
    newInput.hasAttribute("id");
)JS");
    expect(replacement.succeeded(), "stale: replacement reads execute");
    expectBoolean(harness, "replacementAttributeReads", true,
        "serial reuse cannot make a stale Element resolve replacement attributes");
}

} // namespace

int main()
{
    testRetainedValuesPresenceAndCase();
    testMissingInvalidAndBoundedArguments();
    testCurrentDefaultsAndPureReads();
    testIdentityCollectionsTraversalFocusAndEvents();
    testStaleGenerationAndSerialReuse();
    if (failures != 0) {
        std::cerr << failures << " JS47 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS47 checks: " << checks
        << "/" << checks << " passed\n";
    return 0;
}
