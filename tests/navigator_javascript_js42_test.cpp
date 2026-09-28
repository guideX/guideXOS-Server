#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::HostResultCode;
using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::NavigatorScriptHostLimits;
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
<form id="owner" name="owner">
  <div id="panel" class="panel">
    <button id="save" class="action primary" type="button">Save</button>
    <div id="content">
      <input id="name" class="field" type="text" value="name-seed">
      <input id="email" class="field urgent" type="text" value="email-seed">
      <button id="cancel" class="action" type="button">Cancel</button>
    </div>
    <select id="choice" name="choice">
      <option id="option-a" value="a" selected>A</option>
      <option id="option-b" value="b">B</option>
    </select>
  </div>
</form>
<input id="outside-owned" class="field" type="text" value="outside-seed">
<button id="outside" class="action" type="button">Outside</button>
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
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": error code");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(harness.loadHtml("file:///js42.html", kFixture, error),
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
    return 0u;
}

void testDocumentTagRetrievalAndEquivalence()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var methods = document.getElementsByTagName !== undefined &&
    document.getElementsByClassName !== undefined;
var tags = document.getElementsByTagName("button");
var tagQueries = document.querySelectorAll("button");
var documentTagBasics = tags !== null && tags.length === 3 &&
    tags[0] === document.querySelector("button") &&
    tags[0] === document.getElementById("save") &&
    tags[1] === document.getElementById("cancel") &&
    tags[2] === document.getElementById("outside");
var tagQueryEquivalent = tags.length === tagQueries.length &&
    tags[0] === tagQueries[0] && tags[1] === tagQueries[1] &&
    tags[2] === tagQueries[2];
var tagCaseEquivalent = document.getElementsByTagName("BUTTON")[0] ===
    document.getElementsByTagName("button")[0] &&
    document.getElementsByTagName("INPUT").length ===
    document.getElementsByTagName("input").length;
var tagEmpty = document.getElementsByTagName("does-not-exist");
var tagEmptyResult = tagEmpty !== null && tagEmpty.length === 0 &&
    tagEmpty[999] === undefined;
var tagMissAndSurface = tags[999] === undefined &&
    tags.item === undefined && tags.namedItem === undefined &&
    tags["save"] === undefined;
var repeatedTagIdentity = tags === document.getElementsByTagName("button");
)JS");
    expect(result.succeeded(), "document tags: script");
    expectBoolean(harness, "methods", true, "methods: document retrieval");
    expectBoolean(harness, "documentTagBasics", true,
        "document tags: document-order canonical elements");
    expectBoolean(harness, "tagQueryEquivalent", true,
        "document tags: equals querySelectorAll count/order");
    expectBoolean(harness, "tagCaseEquivalent", true,
        "document tags: ASCII case-insensitive matcher");
    expectBoolean(harness, "tagEmptyResult", true,
        "document tags: no matches return an empty collection");
    expectBoolean(harness, "tagMissAndSurface", true,
        "document tags: indexed miss and bounded surface");
    expectBoolean(harness, "repeatedTagIdentity", true,
        "document tags: same descriptor reuses collection record");
    expectError(harness.execute("tags[0] = document.querySelector(\"#outside\");"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "document tags: indexed writes are read-only");
    expectError(harness.execute("tags.length = 0;"),
        RuntimeErrorCode::HostPropertyReadOnly,
        "document tags: length is read-only");
    expectError(harness.execute(
            "var detachedGetTags = document.getElementsByTagName; "
            "detachedGetTags(\"button\");"),
        RuntimeErrorCode::InvalidReceiver,
        "document tags: detached method rejects missing receiver");
}

void testScopedTagRetrievalAndTraversal()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var cancel = document.querySelector("#cancel");
var scopedTags = panel.getElementsByTagName("button");
var scopedTagQueries = panel.querySelectorAll("button");
var scopedTagBasics = scopedTags.length === 2 && scopedTags[0] === save &&
    scopedTags[1] === cancel;
var scopedTagEquivalent = scopedTags.length === scopedTagQueries.length &&
    scopedTags[0] === scopedTagQueries[0] &&
    scopedTags[1] === scopedTagQueries[1];
var selfAndOutsideExcluded = panel.getElementsByTagName("div").length === 1 &&
    panel.getElementsByTagName("button")[2] === undefined &&
    panel.getElementsByTagName("button")[0] !== panel &&
    panel.getElementsByTagName("button")[0] !== document.querySelector("#outside");
var strictDescendants = panel.contains(scopedTags[0]) &&
    panel.contains(scopedTags[1]) && !scopedTags[0].contains(panel);
var traversalCoherent = scopedTags[0].parentElement === panel &&
    scopedTags[1].parentElement === document.querySelector("#content") &&
    scopedTags[1].closest(".panel") === panel;
var noOutside = panel.getElementsByTagName("button").length ===
    panel.querySelectorAll("button").length &&
    panel.getElementsByTagName("button")[2] === undefined;
)JS");
    expect(result.succeeded(), "scoped tags: script");
    expectBoolean(harness, "scopedTagBasics", true,
        "scoped tags: direct and deep structural descendants");
    expectBoolean(harness, "scopedTagEquivalent", true,
        "scoped tags: equals scoped querySelectorAll order");
    expectBoolean(harness, "selfAndOutsideExcluded", true,
        "scoped tags: receiver and outside subtree excluded");
    expectBoolean(harness, "strictDescendants", true,
        "scoped tags: contains integration");
    expectBoolean(harness, "traversalCoherent", true,
        "scoped tags: parentElement and closest integration");
    expectBoolean(harness, "noOutside", true,
        "scoped tags: strict subtree count");
}

void testDocumentClassRetrievalAndEquivalence()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var actions = document.getElementsByClassName("action");
var actionQueries = document.querySelectorAll(".action");
var classBasics = actions !== null && actions.length === 3 &&
    actions[0] === document.querySelector("#save") &&
    actions[1] === document.querySelector("#cancel") &&
    actions[2] === document.querySelector("#outside");
var classQueryEquivalent = actions.length === actionQueries.length &&
    actions[0] === actionQueries[0] && actions[1] === actionQueries[1] &&
    actions[2] === actionQueries[2];
var exactTokens = document.getElementsByClassName("primary")[0] === actions[0] &&
    document.getElementsByClassName("act").length === 0 &&
    document.getElementsByClassName("Action").length === 0;
var classOrder = actions[0].id === "save" && actions[1].id === "cancel" &&
    actions[2].id === "outside";
var classEmpty = document.getElementsByClassName("missing");
var classEmptyResult = classEmpty !== null && classEmpty.length === 0 &&
    classEmpty[50] === undefined;
var classMissAndSurface = actions[999] === undefined &&
    actions.item === undefined && actions.namedItem === undefined &&
    actions["save"] === undefined;
var repeatedClassIdentity = actions ===
    document.getElementsByClassName("action");
)JS");
    expect(result.succeeded(), "document classes: script");
    expectBoolean(harness, "classBasics", true,
        "document classes: exact whitespace-delimited tokens");
    expectBoolean(harness, "classQueryEquivalent", true,
        "document classes: equals querySelectorAll count/order");
    expectBoolean(harness, "exactTokens", true,
        "document classes: multi-class element, substring, and case");
    expectBoolean(harness, "classOrder", true,
        "document classes: structural document order");
    expectBoolean(harness, "classEmptyResult", true,
        "document classes: empty no-match collection");
    expectBoolean(harness, "classMissAndSurface", true,
        "document classes: indexed miss and bounded surface");
    expectBoolean(harness, "repeatedClassIdentity", true,
        "document classes: same descriptor reuses collection record");
}

void testScopedClassRetrievalAndIntegration()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var scopedActions = panel.getElementsByClassName("action");
var scopedActionQueries = panel.querySelectorAll(".action");
var scopedFields = panel.getElementsByClassName("field");
var scopedClassBasics = scopedActions.length === 2 &&
    scopedActions[0] === document.querySelector("#save") &&
    scopedActions[1] === document.querySelector("#cancel") &&
    scopedFields.length === 2 &&
    scopedFields[0] === document.querySelector("#name") &&
    scopedFields[1] === document.querySelector("#email");
var scopedClassEquivalent = scopedActions.length === scopedActionQueries.length &&
    scopedActions[0] === scopedActionQueries[0] &&
    scopedActions[1] === scopedActionQueries[1];
var scopedSelfExcluded = panel.getElementsByClassName("panel").length === 0 &&
    panel.querySelectorAll(".panel").length === 0;
var scopedOutsideExcluded = scopedActions[2] === undefined &&
    !panel.contains(document.querySelector("#outside")) &&
    panel.contains(scopedActions[0]) && panel.contains(scopedActions[1]);
var scopedTraversal = scopedActions[0].parentElement === panel &&
    scopedActions[1].closest(".panel") === panel;
)JS");
    expect(result.succeeded(), "scoped classes: script");
    expectBoolean(harness, "scopedClassBasics", true,
        "scoped classes: direct/deep descendants and multi-class element");
    expectBoolean(harness, "scopedClassEquivalent", true,
        "scoped classes: equals scoped querySelectorAll count/order");
    expectBoolean(harness, "scopedSelfExcluded", true,
        "scoped classes: receiver itself excluded");
    expectBoolean(harness, "scopedOutsideExcluded", true,
        "scoped classes: matching outside element excluded; contains true");
    expectBoolean(harness, "scopedTraversal", true,
        "scoped classes: canonical parentElement/closest results");
}

void testInvalidInputsAndBoundedSemantics()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    std::string oversizedTag(
        gxos::javascript::kNavigatorScriptMaxSelectorLength + 1u, 'x');
    std::string oversizedClass(
        gxos::javascript::kNavigatorScriptMaxSelectorLength, 'x');
    const ScriptResult result = harness.execute(
        "var emptyTag = document.getElementsByTagName(\"\");"
        "var spacedTag = document.getElementsByTagName(\" input \");"
        "var joinedTag = document.getElementsByTagName(\"input button\");"
        "var selectorTextTag = document.getElementsByTagName(\"input.field\");"
        "var idSelectorTag = document.getElementsByTagName(\"#save\");"
        "var wildcardTag = document.getElementsByTagName(\"*\");"
        "var missingTag = document.getElementsByTagName();"
        "var numberTag = document.getElementsByTagName(123);"
        "var booleanTag = document.getElementsByTagName(true);"
        "var longTag = document.getElementsByTagName(\"" + oversizedTag +
        "\");"
        "var emptyClass = document.getElementsByClassName(\"\");"
        "var spacedClass = document.getElementsByClassName(\" action \");"
        "var multiClass = document.getElementsByClassName(\"action primary\");"
        "var missingClass = document.getElementsByClassName();"
        "var numberClass = document.getElementsByClassName(123);"
        "var booleanClass = document.getElementsByClassName(true);"
        "var longClass = document.getElementsByClassName(\"" +
        oversizedClass + "\");"
        "var retrievalExpansion = spacedClass.length === 3 &&"
        " multiClass.length === 1 && multiClass[0].id === \"save\";"
        "var invalidSafe = emptyTag.length === 0 &&"
        " joinedTag.length === 0 && selectorTextTag.length === 0 &&"
        " idSelectorTag.length === 0 &&"
        " missingTag.length === 0 && numberTag.length === 0 &&"
        " booleanTag.length === 0 && longTag.length === 0 &&"
        " emptyClass.length === 0 && missingClass.length === 0 &&"
        " numberClass.length === 0 && booleanClass.length === 0 &&"
        " longClass.length === 0;"
        "var wildcardTagUsesSharedDescriptor = wildcardTag.length > 0 &&"
        " wildcardTag === document.querySelectorAll(\"*\");");
    expect(result.succeeded(), "invalid inputs: retrieval calls fail closed");
    expectBoolean(harness, "invalidSafe", true,
        "invalid inputs: empty/missing/non-string/oversized retrievals");
    expectBoolean(harness, "retrievalExpansion", true,
        "JS44: outer whitespace trims and multi-token class retrieval uses AND semantics");
    expectBoolean(harness, "wildcardTagUsesSharedDescriptor", true,
        "wildcard tag retrieval now shares the Universal selector collection");

    const ScriptResult methodSurface = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var results = document.getElementsByTagName("button");
var ownerForm = document.querySelector("#owner");
var choice = document.querySelector("#choice");
var surfaceRestricted = panel.getElementsByTagName !== undefined &&
    panel.getElementsByClassName !== undefined &&
    results.getElementsByTagName === undefined &&
    results.getElementsByClassName === undefined &&
    document.forms.getElementsByTagName === undefined &&
    panel.children.getElementsByClassName === undefined &&
    ownerForm.elements.getElementsByTagName === undefined &&
    choice.options.getElementsByClassName === undefined;
)JS");
    expect(methodSurface.succeeded(), "invalid receivers: script");
    expectBoolean(harness, "surfaceRestricted", true,
        "invalid receivers: methods limited to document and Element");
    expectError(harness.execute(
            "var detachedGetClasses = panel.getElementsByClassName; "
            "detachedGetClasses(\"action\");"),
        RuntimeErrorCode::InvalidReceiver,
        "scoped classes: detached method rejects missing receiver");
}

void testSharedCollectionRegistryAndLiveProjection()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult setup = harness.execute(R"JS(
var sharedQuery = document.querySelectorAll("button");
var sharedTag = document.getElementsByTagName("button");
var actions = document.getElementsByClassName("action");
var fields = document.getElementsByClassName("field");
var inputs = document.getElementsByTagName("input");
var independentDescriptors = sharedTag.length === 3 && actions.length === 3 &&
    fields.length === 3 && inputs.length === 3 &&
    sharedQuery === sharedTag && actions[0].id === "save" &&
    fields[0].id === "name" && inputs[0].id === "name";
var aliveCounts = actions.length + fields.length + sharedTag.length;
)JS");
    expect(setup.succeeded(), "collections: simultaneous references script");
    expectBoolean(harness, "independentDescriptors", true,
        "collections: selector and retrieval share slots without descriptor changes");
    gxos::web::HtmlElementRef* saveElement = nullptr;
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == "save") saveElement = &element;
    }
    expect(saveElement != nullptr, "live collection: source Element exists");
    if (saveElement != nullptr) saveElement->className = "primary";
    const ScriptResult live = harness.execute(
        "var liveCounts = actions.length === 2 && fields.length === 3 && "
        "sharedTag.length === 3 && aliveCounts === 9;");
    expect(live.succeeded(), "collections: access-time projection script");
    expectBoolean(harness, "liveCounts", true,
        "collections: access-time matcher sees authoritative structure");
    if (saveElement != nullptr) saveElement->className = "action primary";
}

void testFormAndSelectStructuralDistinctions()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t ownerSerial = serialById(harness, "owner");
    const std::uint64_t outsideOwnedSerial = serialById(harness, "outside-owned");
    expect(ownerSerial != 0u && outsideOwnedSerial != 0u,
        "structure: owner and detached form control exist");
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.serial == outsideOwnedSerial)
            element.formControl.parentFormSerial = ownerSerial;
    }
    const ScriptResult result = harness.execute(R"JS(
var form = document.querySelector("#owner");
var detachedOwned = document.querySelector("#outside-owned");
var structuralInputs = form.getElementsByTagName("input");
var formOwnershipContainsDetached =
    form.elements["outside-owned"] === detachedOwned;
var structuralInputOrder = structuralInputs.length === 2 &&
    structuralInputs[0].id === "name" &&
    structuralInputs[1].id === "email" &&
    form.contains(detachedOwned) === false;
var structuralOwnershipDistinct = formOwnershipContainsDetached &&
    structuralInputOrder;
var select = document.querySelector("#choice");
var optionRetrieval = select.getElementsByTagName("option");
var optionIdentity = optionRetrieval.length === 2 &&
    optionRetrieval[0] === select.options[0] &&
    optionRetrieval[1] === select.options[1] &&
    select.children[0] === select.options[0] &&
    select.contains(optionRetrieval[0]);
)JS");
    expect(result.succeeded(), "structure: form and option script");
    expectBoolean(harness, "structuralOwnershipDistinct", true,
        "structure: form.elements ownership differs from structural descendants");
    expectBoolean(harness, "formOwnershipContainsDetached", true,
        "structure: form.elements sees the synthetic form-owned control");
    expectBoolean(harness, "structuralInputOrder", true,
        "structure: retrieval follows only structural input descendants");
    expectBoolean(harness, "optionIdentity", true,
        "structure: options, children, and tag retrieval share identity");
}

void testStaleCollectionsReceiversAndSerialReuse()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldPanelSerial = serialById(harness, "panel");
    const std::uint64_t oldSaveSerial = serialById(harness, "save");
    const ScriptResult setup = harness.execute(R"JS(
var oldTagCollection = document.getElementsByTagName("button");
var oldClassCollection = document.getElementsByClassName("action");
var oldPanel = document.querySelector("#panel");
var oldScopedTagCollection = oldPanel.getElementsByTagName("button");
var oldScopedClassCollection = oldPanel.getElementsByClassName("action");
var oldResultElement = oldTagCollection[0];
)JS");
    expect(setup.succeeded(), "stale: capture retrieval collections and Element");
    expect(harness.invalidateDocumentGeneration(error),
        "stale: invalidate generation");
    expect(error == RuntimeErrorCode::None, "stale: invalidation no error");
    const ScriptResult staleReceiver = harness.execute(R"JS(
var staleReceiverTag = oldPanel.getElementsByTagName("button");
var staleReceiverClass = oldPanel.getElementsByClassName("action");
var staleReceiverEmpty = staleReceiverTag.length === 0 &&
    staleReceiverClass.length === 0;
)JS");
    expect(staleReceiver.succeeded(), "stale receiver: method calls are safe");
    expectBoolean(harness, "staleReceiverEmpty", true,
        "stale receiver: returns current-generation empty collections");
    expectError(harness.execute("oldTagCollection.length;"),
        RuntimeErrorCode::StaleHostObject, "stale: document tag collection");
    expectError(harness.execute("oldClassCollection[0];"),
        RuntimeErrorCode::StaleHostObject, "stale: document class collection");
    expectError(harness.execute("oldScopedTagCollection.length;"),
        RuntimeErrorCode::StaleHostObject, "stale: scoped tag collection");
    expectError(harness.execute("oldScopedClassCollection[0];"),
        RuntimeErrorCode::StaleHostObject, "stale: scoped class collection");
    expectError(harness.execute("oldResultElement.id;"),
        RuntimeErrorCode::StaleHostObject, "stale: individual result Element");

    harness.document() = gxos::web::parseHtml(
        "file:///js42-replacement.html", kFixture);
    const std::uint64_t newPanelSerial = serialById(harness, "panel");
    const std::uint64_t newSaveSerial = serialById(harness, "save");
    expect(oldPanelSerial == newPanelSerial && oldSaveSerial == newSaveSerial,
        "stale: replacement deliberately reuses structural serials");
    expect(harness.runtime().installHostGlobal("newPanel", newPanelSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "stale: install current-generation replacement Element");
    const ScriptResult reusedSerial = harness.execute(R"JS(
var reusedSerialSafe = newPanel.getElementsByTagName("button").length === 2 &&
    oldPanel.getElementsByTagName("button").length === 0 &&
    oldPanel.getElementsByClassName("action").length === 0 &&
    newPanel.getElementsByTagName(oldPanel).length === 0 &&
    newPanel.getElementsByClassName(oldResultElement).length === 0;
)JS");
    expect(reusedSerial.succeeded(), "stale: serial reuse script");
    expectBoolean(harness, "reusedSerialSafe", true,
        "stale: old scope cannot resolve reused current-generation serial");
}

void testCollectionCapacityIsSharedAndBounded()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptHostLimits limits;
    limits.maxSelectorCollections = 2u;
    NavigatorScriptExecutionHarness harness(
        gxos::javascript::RuntimeLimits(), limits);
    loadFixture(harness, error);
    const ScriptResult twoDescriptors = harness.execute(R"JS(
var queryButtons = document.querySelectorAll("button");
var tagButtons = document.getElementsByTagName("button");
var actionClasses = document.getElementsByClassName("action");
var sharedDescriptorSlot = queryButtons === tagButtons &&
    queryButtons.length === 3 && actionClasses.length === 3;
)JS");
    expect(twoDescriptors.succeeded(), "capacity: two shared descriptors fit");
    expectBoolean(harness, "sharedDescriptorSlot", true,
        "capacity: querySelectorAll and retrieval reuse same bounded registry");
    expectError(harness.execute(
            "document.getElementsByTagName(\"input\");"),
        RuntimeErrorCode::DocumentLookupLimitExceeded,
        "capacity: third unique descriptor fails at configured registry bound");
    expect(harness.hostAdapter().limits().maxSelectorCollections == 2u,
        "capacity: configured limit is not expanded");
}

void testEventsNestedDispatchAndPurity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t nameSerial = serialById(harness, "name");
    const std::uint64_t saveSerial = serialById(harness, "save");
    const ScriptResult setup = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var outside = document.querySelector("#outside");
var name = document.querySelector("#name");
var tagInCallback = false;
var classInCallback = false;
var containsInCallback = false;
var eventMetadataPreserved = false;
var eventSurfaceSafe = false;
var nestedDispatchSafe = false;
var nestedLookupSafe = false;
panel.addEventListener("click", function(event) {
  if (event.target === save) {
    var beforeTarget = event.target;
    var beforeCurrent = event.currentTarget;
    var beforePhase = event.eventPhase;
    var beforeRelated = event.relatedTarget;
    var beforeDefault = event.defaultPrevented;
    var buttons = panel.getElementsByTagName("button");
    var actions = panel.getElementsByClassName("action");
    eventSurfaceSafe = event.getElementsByTagName === undefined &&
      event.getElementsByClassName === undefined;
    tagInCallback = buttons.length === 2 && buttons[0] === save;
    classInCallback = actions.length === 2 && actions[0] === save;
    containsInCallback = panel.contains(event.target) &&
      buttons[0].closest(".panel") === panel;
    eventMetadataPreserved = event.target === beforeTarget &&
      event.currentTarget === beforeCurrent && event.currentTarget === panel &&
      event.eventPhase === beforePhase && beforePhase === 3 &&
      event.relatedTarget === beforeRelated && beforeRelated === null &&
      event.defaultPrevented === beforeDefault && beforeDefault === true;
  }
});
save.addEventListener("click", function(event) {
  var beforeTarget = event.target;
  var beforeCurrent = event.currentTarget;
  var beforePhase = event.eventPhase;
  var beforeRelated = event.relatedTarget;
  event.preventDefault();
  outside.click();
  nestedDispatchSafe = event.target === beforeTarget &&
    event.currentTarget === beforeCurrent && event.currentTarget === save &&
    event.eventPhase === beforePhase && beforePhase === 2 &&
    event.relatedTarget === beforeRelated && beforeRelated === null &&
    event.defaultPrevented === true &&
    panel.getElementsByClassName("action").length === 2;
});
outside.addEventListener("click", function(event) {
  var buttons = document.getElementsByTagName("button");
  var actions = document.getElementsByClassName("action");
  nestedLookupSafe = event.target === outside && buttons.length === 3 &&
    actions.length === 3 && !panel.contains(event.target);
});
var initialFocus = document.activeElement;
var initialNameValue = name.value;
var initialNameDefault = name.defaultValue;
var initialGeneration = document.querySelector("#panel").tagName;
)JS");
    expect(setup.succeeded(), "events/purity: setup");
    expect(harness.focusElement(nameSerial, error),
        "purity: establish an authoritative focus owner");
    expect(error == RuntimeErrorCode::None, "purity: focus setup no error");
    const bool dirtyBefore = harness.documentDirty();
    const std::uint64_t revisionBefore = harness.layoutRevision();
    const std::uint64_t mutationBefore = harness.document().scriptMutationCount;
    const std::uint64_t generationBefore = harness.runtime().hostGeneration();
    const std::size_t listenerCountBefore = harness.hostAdapter().clickListenerCount();
    bool defaultPrevented = false;
    expect(harness.dispatchClick(saveSerial, error, &defaultPrevented),
        "events: authentic click invokes bounded retrieval callbacks");
    expect(error == RuntimeErrorCode::None, "events: no host/runtime error");
    expect(defaultPrevented, "events: retrieval preserves click cancellation");
    expectBoolean(harness, "tagInCallback", true,
        "events: tag lookup in real click callback");
    expectBoolean(harness, "classInCallback", true,
        "events: class lookup in real click callback");
    expectBoolean(harness, "containsInCallback", true,
        "events: contains and closest integrate with retrieved results");
    expectBoolean(harness, "eventMetadataPreserved", true,
        "events: target/currentTarget/phase/related/default metadata preserved");
    expectBoolean(harness, "eventSurfaceSafe", true,
        "events: retrieval methods are not exposed on Event objects");
    expectBoolean(harness, "nestedDispatchSafe", true,
        "events: outer callback remains intact after nested click");
    expectBoolean(harness, "nestedLookupSafe", true,
        "events: retrieval remains safe during nested dispatch");
    expect(harness.hostAdapter().clickListenerCount() == listenerCountBefore,
        "events: retrieval does not change the listener registry");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "events: listener registry remains bounded at 64");

    const ScriptResult after = harness.execute(R"JS(
var focusUnchanged = document.activeElement === name;
var formValuesUnchanged = name.value === "name-seed" &&
    name.defaultValue === "name-seed" &&
    document.querySelector("#choice").selectedIndex === 0;
var purityStateAvailable = initialFocus === null &&
    initialNameValue === "name-seed" && initialNameDefault === "name-seed" &&
    initialGeneration === "DIV";
)JS");
    expect(after.succeeded(), "purity: verify post-query state");
    expectBoolean(harness, "focusUnchanged", true,
        "purity: activeElement remains the explicit focus owner");
    expectBoolean(harness, "formValuesUnchanged", true,
        "purity: current/default control values unchanged");
    expectBoolean(harness, "purityStateAvailable", true,
        "purity: captured pre-query state unchanged");
    expect(!harness.documentDirty() && !dirtyBefore,
        "purity: document layout remains clean");
    expect(harness.layoutRevision() == revisionBefore,
        "purity: layout revision unchanged");
    expect(harness.document().scriptMutationCount == mutationBefore,
        "purity: document mutation counter unchanged");
    expect(harness.runtime().hostGeneration() == generationBefore,
        "purity: document host generation unchanged");
    expect(harness.focusedElementSerial() == nameSerial,
        "purity: focus owner remains unchanged");
}

void testRegressionCoverage()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var regressions = document.querySelector("#save").matches("button.action") &&
    save.closest(".panel") === panel && panel.contains(save) &&
    panel.children[0] === save && save.parentElement === panel &&
    document.forms["owner"].elements["name"] ===
        document.querySelector("#name") &&
    document.querySelector("#choice").options[0] ===
        document.querySelector("#option-a") &&
    document.querySelectorAll(".action").length === 3 &&
    document.querySelectorAll(".panel > button").length === 1 &&
    document.querySelector("#save + div").id === "content" &&
    document.querySelector("#cancel").previousElementSibling.id === "email";
)JS");
    expect(result.succeeded(), "regression: JS36-JS41 integrations execute");
    expectBoolean(harness, "regressions", true,
        "regression: selectors/forms/options/traversal/contains unchanged");
}

} // namespace

int main()
{
    testDocumentTagRetrievalAndEquivalence();
    testScopedTagRetrievalAndTraversal();
    testDocumentClassRetrievalAndEquivalence();
    testScopedClassRetrievalAndIntegration();
    testInvalidInputsAndBoundedSemantics();
    testSharedCollectionRegistryAndLiveProjection();
    testFormAndSelectStructuralDistinctions();
    testStaleCollectionsReceiversAndSerialReuse();
    testCollectionCapacityIsSharedAndBounded();
    testEventsNestedDispatchAndPurity();
    testRegressionCoverage();
    if (failures != 0) {
        std::cerr << failures << " JS42 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS42 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
