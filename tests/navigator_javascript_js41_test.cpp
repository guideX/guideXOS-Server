#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::HostObjectReference;
using gxos::javascript::HostResultCode;
using gxos::javascript::HostValue;
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
<form id="settings" name="settings">
  <div id="wrapper">
    <input id="wrapped" name="wrapped" type="text" value="seed">
  </div>
  <input id="direct" name="direct" type="text" value="direct-seed">
  <select id="choice" name="choice">
    <option id="option-a" value="a" selected>A</option>
    <option id="option-b" value="b">B</option>
  </select>
  <div id="panel" class="panel">
    <div id="toolbar">
      <button id="save" class="action" type="button">Save</button>
    </div>
    <div id="content"><input id="name" name="name" value="panel-seed"></div>
    <button id="sibling-a" type="button">A</button>
    <button id="sibling-b" type="button">B</button>
  </div>
  <button id="submit" type="submit">Submit</button>
  <button id="reset" type="reset">Reset</button>
</form>
<form id="other-form" name="other-form">
  <input id="other-control" name="other" value="other-seed">
</form>
<div id="unrelated"><button id="outside-button" type="button">Outside</button></div>
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

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(harness.loadHtml("file:///js41.html", kFixture, error),
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

void testContainsBasicsAndIntegration()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var root = document.querySelector("html");
var body = document.querySelector("body");
var form = document.querySelector("#settings");
var wrapper = document.querySelector("#wrapper");
var wrapped = document.querySelector("#wrapped");
var panel = document.querySelector("#panel");
var toolbar = document.querySelector("#toolbar");
var save = document.querySelector("#save");
var content = document.querySelector("#content");
var name = document.querySelector("#name");
var siblingA = document.querySelector("#sibling-a");
var siblingB = document.querySelector("#sibling-b");
var unrelated = document.querySelector("#unrelated");
var choice = document.querySelector("#choice");
var optionA = document.querySelector("#option-a");
var methods = panel.contains !== undefined &&
    save.contains !== undefined;
var self = panel.contains(panel) && save.contains(save);
var directChild = panel.contains(toolbar) && toolbar.contains(save);
var grandchild = panel.contains(save);
var deepDescendant = root.contains(name) && body.contains(name) &&
    form.contains(name) && panel.contains(name);
var direction = !save.contains(panel) && !name.contains(form);
var siblings = siblingA.nextElementSibling === siblingB &&
    !siblingA.contains(siblingB) && !siblingB.contains(siblingA);
var unrelatedElements = !panel.contains(unrelated) &&
    !unrelated.contains(panel);
var rootBehavior = root.parentElement === null &&
    root.contains(root) && root.contains(body) && root.contains(unrelated) &&
    !body.contains(root);
var structuralWrapper = form.contains(wrapped) &&
    wrapper.contains(wrapped) && !wrapped.contains(form);
var selectOption = choice.children[0] === choice.options[0] &&
    choice.contains(optionA) && optionA.parentElement === choice;
var selectorIdentity = panel.querySelector(".action") === save &&
    panel.contains(panel.querySelector(".action")) &&
    document.querySelectorAll(".action")[0] === save &&
    panel.contains(document.querySelectorAll(".action")[0]);
var traversalConsistency = save.parentElement === toolbar &&
    panel.contains(save) && save.closest("#panel") === panel &&
    panel.contains(save.closest("#panel").querySelector(".action"));
var siblingDistinction = siblingA.nextElementSibling === siblingB &&
    siblingA.contains(siblingB) === false;
var repeatedCalls = panel.contains(save) && !panel.contains(unrelated) &&
    panel.contains(panel) && panel.contains(save);
var nestedCalls = panel.contains(toolbar) && toolbar.contains(save) &&
    panel.contains(name) && !name.contains(save);
)JS");
    expect(result.succeeded(), "basics: script");
    expectBoolean(harness, "methods", true, "method: exposed on Elements");
    expectBoolean(harness, "self", true, "contains: self-inclusive");
    expectBoolean(harness, "directChild", true, "contains: direct child");
    expectBoolean(harness, "grandchild", true, "contains: grandchild");
    expectBoolean(harness, "deepDescendant", true,
        "contains: deep structural ancestry");
    expectBoolean(harness, "direction", true, "contains: direction");
    expectBoolean(harness, "siblings", true,
        "contains: sibling relation is not containment");
    expectBoolean(harness, "unrelatedElements", true,
        "contains: unrelated elements");
    expectBoolean(harness, "rootBehavior", true,
        "contains: exposed html/body roots");
    expectBoolean(harness, "structuralWrapper", true,
        "contains: form and nested wrapper structure");
    expectBoolean(harness, "selectOption", true,
        "contains: select options use structural ancestry");
    expectBoolean(harness, "selectorIdentity", true,
        "contains: querySelector and querySelectorAll identities");
    expectBoolean(harness, "traversalConsistency", true,
        "contains: parentElement and closest consistency");
    expectBoolean(harness, "siblingDistinction", true,
        "contains: nextElementSibling does not imply containment");
    expectBoolean(harness, "repeatedCalls", true,
        "contains: repeated calls are deterministic");
    expectBoolean(harness, "nestedCalls", true,
        "contains: nested queries evaluate independently");
}

void testArgumentsOwnershipAndPurity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t wrappedSerial = serialById(harness, "wrapped");
    const std::uint64_t otherFormSerial = serialById(harness, "other-form");
    expect(wrappedSerial != 0u && otherFormSerial != 0u,
        "ownership: structural nodes exist");

    // Deliberately separate form ownership metadata from ancestry. The forms
    // collection follows parentFormSerial; contains must remain structural.
    for (gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.serial == wrappedSerial)
            element.formControl.parentFormSerial = otherFormSerial;
    }

    const ScriptResult setup = harness.execute(R"JS(
var form = document.querySelector("#settings");
var otherForm = document.querySelector("#other-form");
var wrapper = document.querySelector("#wrapper");
var wrapped = document.querySelector("#wrapped");
var panel = document.querySelector("#panel");
var unrelated = document.querySelector("#unrelated");
var collection = form.elements;
var ownershipDiffers = form.contains(wrapped) && wrapper.contains(wrapped) &&
    !otherForm.contains(wrapped) && otherForm.elements[0] === wrapped &&
    form.elements[0] === document.querySelector("#direct") &&
    form.contains(collection) === false;
var formProjection = form.elements.length >= 2 &&
    form.elements[0] === document.querySelector("#direct") &&
    otherForm.elements[0] === wrapped;
var badArguments = panel.contains() === false && panel.contains(null) === false &&
    panel.contains(true) === false && panel.contains(123) === false &&
    panel.contains("save") === false && panel.contains({}) === false &&
    panel.contains(form.elements) === false;
var methodSurface = document.contains === undefined &&
    form.elements.contains === undefined && collection.contains === undefined;
var pureWarmup = panel.contains(panel) && panel.contains(wrapper) === false;
)JS");
    expect(setup.succeeded(), "arguments/ownership: script");
    expectBoolean(harness, "ownershipDiffers", true,
        "ownership: parentFormSerial does not replace structural ancestry");
    expectBoolean(harness, "formProjection", true,
        "ownership: form.elements remains an ownership projection");
    expectBoolean(harness, "badArguments", true,
        "arguments: missing, primitive, object, and collection are false");
    expectBoolean(harness, "methodSurface", true,
        "surface: contains is not exposed on document or collections");
    expectBoolean(harness, "pureWarmup", true, "purity: warmed predicate");

    const bool dirtyBefore = harness.documentDirty();
    const std::uint64_t revisionBefore = harness.layoutRevision();
    const std::uint64_t mutationBefore =
        harness.document().scriptMutationCount;
    const std::uint64_t focusedBefore = harness.focusedElementSerial();
    const std::uint64_t generationBefore = harness.runtime().hostGeneration();
    const std::size_t hostObjectsBefore = harness.runtime().hostObjectCount();
    const ScriptResult pure = harness.execute(R"JS(
var pureCalls = wrapper.contains(wrapped) && panel.contains(panel) &&
    !panel.contains(unrelated) && wrapper.contains(wrapped) &&
    panel.contains(panel);
)JS");
    expect(pure.succeeded(), "purity: repeated contains script");
    expectBoolean(harness, "pureCalls", true,
        "purity: repeated containment result");
    expect(harness.documentDirty() == dirtyBefore,
        "purity: layout dirty state unchanged");
    expect(harness.layoutRevision() == revisionBefore,
        "purity: layout revision unchanged");
    expect(harness.document().scriptMutationCount == mutationBefore,
        "purity: script mutation count unchanged");
    expect(harness.focusedElementSerial() == focusedBefore,
        "purity: focus state unchanged");
    expect(harness.runtime().hostGeneration() == generationBefore,
        "purity: document generation unchanged");
    expect(harness.runtime().hostObjectCount() == hostObjectsBefore,
        "purity: no host-object or collection allocation");
}

void testEventsFocusAndDefaultActions()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t saveSerial = serialById(harness, "save");
    const std::uint64_t panelSerial = serialById(harness, "panel");
    const std::uint64_t nameSerial = serialById(harness, "name");
    const std::uint64_t formSerial = serialById(harness, "settings");
    const ScriptResult setup = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var outside = document.querySelector("#outside-button");
var name = document.querySelector("#name");
var form = document.querySelector("#settings");
var clickTarget = false;
var clickSelf = false;
var outsideTarget = false;
var eventSurfaceSafe = false;
var targetPreserved = false;
var currentTargetPreserved = false;
var eventPhasePreserved = false;
var relatedTargetPreserved = false;
var defaultPreventedPreserved = false;
var nestedPreserved = false;
var focusListenerContains = false;
var submitListenerContains = false;
var resetListenerContains = false;
var submitDefaultPreserved = false;
var resetDefaultPreserved = false;
panel.addEventListener("click", function(event) {
  var beforeTarget = event.target;
  var beforeCurrent = event.currentTarget;
  var beforePhase = event.eventPhase;
  var beforeRelated = event.relatedTarget;
  var beforeDefault = event.defaultPrevented;
  var contained = panel.contains(event.target);
  eventSurfaceSafe = event.contains === undefined;
  targetPreserved = event.target === beforeTarget;
  currentTargetPreserved = event.currentTarget === beforeCurrent &&
    event.currentTarget === panel;
  eventPhasePreserved = event.eventPhase === beforePhase;
  relatedTargetPreserved = event.relatedTarget === beforeRelated;
  defaultPreventedPreserved = event.defaultPrevented === beforeDefault;
  if (event.target === save) clickTarget = contained;
  if (event.target === panel) clickSelf = contained;
});
document.addEventListener("click", function(event) {
  if (event.target === outside)
    outsideTarget = panel.contains(event.target) === false;
});
save.addEventListener("click", function(event) {
  event.preventDefault();
  outside.click();
  nestedPreserved = event.target === save && panel.contains(event.target) &&
    event.defaultPrevented === true;
});
name.addEventListener("focus", function(event) {
  focusListenerContains = panel.contains(event.target) &&
    event.target === name;
});
form.addEventListener("submit", function(event) {
  submitListenerContains = form.contains(event.target) &&
    event.target === form;
  submitDefaultPreserved = event.defaultPrevented === false;
});
form.addEventListener("reset", function(event) {
  resetListenerContains = form.contains(event.target) &&
    event.target === form;
  resetDefaultPreserved = event.defaultPrevented === false;
});
name.value = "changed";
)JS");
    expect(setup.succeeded(), "events: setup script");

    bool defaultPrevented = false;
    expect(harness.dispatchClick(saveSerial, error, &defaultPrevented),
        "events: authentic descendant click dispatch");
    expect(error == RuntimeErrorCode::None, "events: click has no runtime error");
    expect(defaultPrevented, "events: contains does not clear cancellation");
    expectBoolean(harness, "clickTarget", true,
        "events: panel contains descendant target");
    expectBoolean(harness, "outsideTarget", true,
        "events: nested outside target is not contained");
    expectBoolean(harness, "eventSurfaceSafe", true,
        "surface: contains is not exposed on Event objects");
    expectBoolean(harness, "targetPreserved", true,
        "events: target preserved");
    expectBoolean(harness, "currentTargetPreserved", true,
        "events: currentTarget preserved");
    expectBoolean(harness, "eventPhasePreserved", true,
        "events: eventPhase preserved");
    expectBoolean(harness, "relatedTargetPreserved", true,
        "events: relatedTarget preserved");
    expectBoolean(harness, "defaultPreventedPreserved", true,
        "events: defaultPrevented preserved");
    expectBoolean(harness, "nestedPreserved", true,
        "events: nested click dispatch preserves outer target and contains");

    defaultPrevented = true;
    expect(harness.dispatchClick(panelSerial, error, &defaultPrevented),
        "events: receiver-self click dispatch");
    expect(error == RuntimeErrorCode::None,
        "events: receiver-self click no runtime error");
    expect(!defaultPrevented, "events: receiver-self click remains uncancelled");
    expectBoolean(harness, "clickSelf", true,
        "events: target equal to receiver is contained");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "events: listener registry remains capped");

    expect(harness.focusElement(nameSerial, error),
        "focus: authoritative focus transition");
    expect(error == RuntimeErrorCode::None, "focus: no runtime error");
    const ScriptResult active = harness.execute(
        "var activeContained = panel.contains(document.activeElement);");
    expect(active.succeeded(), "focus: activeElement query");
    expectBoolean(harness, "activeContained", true,
        "focus: panel contains activeElement descendant");
    expectBoolean(harness, "focusListenerContains", true,
        "focus: contains works inside focus listener");
    expect(harness.focusedElementSerial() == nameSerial,
        "focus: contains does not change focus owner");

    defaultPrevented = false;
    expect(harness.dispatchSubmit(formSerial, error, &defaultPrevented),
        "default action: submit dispatch");
    expect(error == RuntimeErrorCode::None,
        "default action: submit no runtime error");
    expectBoolean(harness, "submitListenerContains", true,
        "default action: contains in submit listener");
    expectBoolean(harness, "submitDefaultPreserved", true,
        "default action: submit cancellation state preserved");
    expect(harness.execute("form.reset();").succeeded(),
        "default action: reset request script");
    expectBoolean(harness, "resetListenerContains", true,
        "default action: contains in reset listener");
    expectBoolean(harness, "resetDefaultPreserved", true,
        "default action: reset cancellation state preserved");
    bool resetRestoredDefault = false;
    for (const gxos::web::DocBlock& block : harness.document().blocks) {
        if (block.formControl.logicalSerial == nameSerial)
            resetRestoredDefault = block.inputValue == "panel-seed";
    }
    expect(resetRestoredDefault,
        "default action: reset default is not changed by contains");
}

void testLifecycleAndCanonicalGeneration()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldParentSerial = serialById(harness, "panel");
    const std::uint64_t oldChildSerial = serialById(harness, "save");
    const ScriptResult saved = harness.execute(
        "var oldParent = document.querySelector(\"#panel\"); "
        "var oldChild = document.querySelector(\"#save\");");
    expect(saved.succeeded(), "lifecycle: capture old handles");
    expect(harness.invalidateDocumentGeneration(error),
        "lifecycle: generation invalidation");
    expect(error == RuntimeErrorCode::None,
        "lifecycle: invalidation no error");
    const ScriptResult staleReceiver = harness.execute(
        "var staleReceiverSafe = oldParent.contains(oldChild) === false && "
        "oldParent.contains() === false;");
    expect(staleReceiver.succeeded(),
        "lifecycle: stale receiver remains script-safe");
    expectBoolean(harness, "staleReceiverSafe", true,
        "lifecycle: stale receiver returns false");

    // Preserve the old JS handles while replacing the structural document.
    // Direct adapter calls cover all old/current receiver/argument pairs
    // without registering new runtime host objects over stale handle slots.
    harness.document() = gxos::web::parseHtml("file:///js41-replacement.html",
        kFixture);
    const std::uint64_t newParentSerial = serialById(harness, "panel");
    const std::uint64_t newChildSerial = serialById(harness, "save");
    expect(newParentSerial == oldParentSerial &&
            newChildSerial == oldChildSerial,
        "lifecycle: replacement reuses the same structural serial values");
    expect(harness.runtime().installHostGlobal("newChild", newChildSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "lifecycle: register current child in new generation");
    const ScriptResult staleReceiverCurrent = harness.execute(
        "var staleReceiverCurrentSafe = "
        "oldParent.contains(newChild) === false;");
    expect(staleReceiverCurrent.succeeded(),
        "lifecycle: stale receiver with current candidate is script-safe");
    expectBoolean(harness, "staleReceiverCurrentSafe", true,
        "lifecycle: stale receiver returns false with current argument");
    expect(harness.runtime().installHostGlobal("newParent", newParentSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "lifecycle: register current parent in new generation");
    expect(error == RuntimeErrorCode::None,
        "lifecycle: current host registration has no error");
    const ScriptResult staleCandidate = harness.execute(
        "var staleCandidateSafe = newParent.contains(oldChild) === false;");
    expect(staleCandidate.succeeded(),
        "lifecycle: stale candidate returns safely with current receiver");
    expectBoolean(harness, "staleCandidateSafe", true,
        "lifecycle: stale candidate does not resolve a reused serial");
    const HostObjectReference oldParent{
        oldParentSerial, harness.runtime().hostGeneration() - 1u,
        gxos::javascript::kNavigatorElementHostKind};
    const HostObjectReference oldChild{
        oldChildSerial, harness.runtime().hostGeneration() - 1u,
        gxos::javascript::kNavigatorElementHostKind};
    const HostObjectReference newParent{
        newParentSerial, harness.runtime().hostGeneration(),
        gxos::javascript::kNavigatorElementHostKind};
    const HostObjectReference newChild{
        newChildSerial, harness.runtime().hostGeneration(),
        gxos::javascript::kNavigatorElementHostKind};
    const auto callContains = [&](const HostObjectReference& receiver,
                                  const HostObjectReference& candidate,
                                  bool expected, const std::string& label) {
        const HostValue argument = HostValue::fromHostObject(candidate);
        HostValue result;
        const auto status = harness.hostAdapter().call(&receiver,
            gxos::javascript::kNavigatorContainsMethod, &argument, 1u, result);
        expect(status.succeeded(), label + ": host call succeeds");
        expect(result.type == gxos::javascript::HostValueType::Boolean,
            label + ": Boolean result");
        if (result.type == gxos::javascript::HostValueType::Boolean)
            expect(result.booleanValue == expected, label + ": value");
    };
    callContains(oldParent, oldChild, false,
        "lifecycle: stale receiver and stale candidate");
    callContains(oldParent, newChild, false,
        "lifecycle: stale receiver and current candidate");
    callContains(newParent, oldChild, false,
        "lifecycle: current receiver and stale candidate");
    callContains(newParent, newChild, true,
        "lifecycle: current generation works with reused serial");

    const HostObjectReference documentReceiver{
        gxos::javascript::kNavigatorDocumentHostInstance,
        harness.runtime().hostGeneration(),
        gxos::javascript::kNavigatorDocumentHostKind};
    HostValue result;
    const HostValue candidate = HostValue::fromHostObject(HostObjectReference{
        newChildSerial, harness.runtime().hostGeneration(),
        gxos::javascript::kNavigatorElementHostKind});
    const auto wrongReceiver = harness.hostAdapter().call(&documentReceiver,
        gxos::javascript::kNavigatorContainsMethod, &candidate, 1u, result);
    expect(wrongReceiver.code == HostResultCode::InvalidValue,
        "surface: non-Element host receiver is rejected safely");
}

void testMalformedAncestryAndTraversalBound()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness selfParent;
    loadFixture(selfParent, error);
    const std::uint64_t toolbarSerial = serialById(selfParent, "toolbar");
    const std::uint64_t saveSerial = serialById(selfParent, "save");
    for (gxos::web::HtmlElementRef& element :
            selfParent.document().structuralElements) {
        if (element.serial == saveSerial) element.parentSerial = saveSerial;
    }
    const ScriptResult selfCycle = selfParent.execute(
        "var malformedSelf = document.querySelector(\"#toolbar\")"
        ".contains(document.querySelector(\"#save\")) === false && "
        "document.querySelector(\"#save\").contains("
        "document.querySelector(\"#save\")) === true;");
    expect(selfCycle.succeeded(), "malformed: self-parent script terminates");
    expectBoolean(selfParent, "malformedSelf", true,
        "malformed: self-parent fails closed except identity");
    expect(toolbarSerial != 0u, "malformed: parent serial exists");

    NavigatorScriptExecutionHarness invalidParent;
    loadFixture(invalidParent, error);
    const std::uint64_t invalidSave = serialById(invalidParent, "save");
    for (gxos::web::HtmlElementRef& element :
            invalidParent.document().structuralElements) {
        if (element.serial == invalidSave)
            element.parentSerial = UINT64_C(0xFFFFFFFFFFFFFFFE);
    }
    const ScriptResult invalid = invalidParent.execute(
        "var invalidParentSafe = document.querySelector(\"#toolbar\")"
        ".contains(document.querySelector(\"#save\")) === false;");
    expect(invalid.succeeded(), "malformed: invalid parent script terminates");
    expectBoolean(invalidParent, "invalidParentSafe", true,
        "malformed: invalid parent serial fails closed");

    NavigatorScriptHostLimits boundedLimits;
    boundedLimits.maxDocumentNodes = 5u;
    NavigatorScriptExecutionHarness cyclic(
        gxos::javascript::RuntimeLimits(), boundedLimits);
    loadFixture(cyclic, error);
    const std::uint64_t cyclicWrapper = serialById(cyclic, "wrapper");
    const std::uint64_t cyclicWrapped = serialById(cyclic, "wrapped");
    for (gxos::web::HtmlElementRef& element :
            cyclic.document().structuralElements) {
        if (element.serial == cyclicWrapper)
            element.parentSerial = cyclicWrapped;
        if (element.serial == cyclicWrapped)
            element.parentSerial = cyclicWrapper;
    }
    expect(cyclic.document().structuralElements.size() >
            boundedLimits.maxDocumentNodes,
        "malformed: traversal cap is smaller than the structural document");
    const auto structuralIndex = [&](std::uint64_t serial) {
        for (std::size_t index = 0u;
             index < cyclic.document().structuralElements.size(); ++index) {
            if (cyclic.document().structuralElements[index].serial == serial)
                return index;
        }
        return cyclic.document().structuralElements.size();
    };
    const std::uint64_t cyclicSettings = serialById(cyclic, "settings");
    expect(structuralIndex(cyclicSettings) < boundedLimits.maxDocumentNodes &&
            structuralIndex(cyclicWrapper) < boundedLimits.maxDocumentNodes &&
            structuralIndex(cyclicWrapped) < boundedLimits.maxDocumentNodes,
        "malformed: traversal endpoints are inside the host's node window");
    const HostObjectReference settingsRef{cyclicSettings,
        cyclic.runtime().hostGeneration(),
        gxos::javascript::kNavigatorElementHostKind};
    const HostObjectReference wrapperRef{cyclicWrapper,
        cyclic.runtime().hostGeneration(),
        gxos::javascript::kNavigatorElementHostKind};
    const HostObjectReference wrappedRef{cyclicWrapped,
        cyclic.runtime().hostGeneration(),
        gxos::javascript::kNavigatorElementHostKind};
    const auto boundedCall = [&](const HostObjectReference& receiver,
                                 bool expected, const std::string& label) {
        const HostValue argument = HostValue::fromHostObject(wrappedRef);
        HostValue result;
        const auto status = cyclic.hostAdapter().call(&receiver,
            gxos::javascript::kNavigatorContainsMethod, &argument, 1u, result);
        expect(status.succeeded(), label + ": host call returns");
        expect(result.type == gxos::javascript::HostValueType::Boolean,
            label + ": Boolean result");
        if (result.type == gxos::javascript::HostValueType::Boolean)
            expect(result.booleanValue == expected, label + ": value");
    };
    boundedCall(settingsRef, false,
        "malformed: parent cycle terminates at maxDocumentNodes=5");
    boundedCall(wrapperRef, true,
        "malformed: encountered receiver returns true before cycle bound");
}

void testRegressionAndEventNesting()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult regression = harness.execute(R"JS(
var form = document.forms["settings"];
var direct = form.elements[0];
var option = document.querySelectorAll("option")[0];
var regression = document.querySelector("form input") ===
    document.querySelector("#wrapped") &&
    document.querySelectorAll(".panel").length === 1 &&
    form === document.querySelector("form#settings") &&
    direct === document.querySelector("#wrapped") &&
    option === document.querySelector("#option-a") &&
    document.querySelector("#sibling-b").previousElementSibling ===
        document.querySelector("#sibling-a") &&
    document.querySelector("#save").closest("#panel") ===
        document.querySelector("#panel") &&
    document.querySelector("#save").matches("button.action");
var nestedResults = document.querySelector("#panel").contains(
    document.querySelector("#save")) &&
    document.querySelector("#toolbar").contains(
    document.querySelector("#save")) &&
    !document.querySelector("#save").contains(
    document.querySelector("#panel"));
)JS");
    expect(regression.succeeded(), "regression: JS36-JS40 APIs");
    expectBoolean(harness, "regression", true,
        "regression: selectors/forms/options/sibling traversal unchanged");
    expectBoolean(harness, "nestedResults", true,
        "regression: nested contains order and direction");
}

} // namespace

int main()
{
    testContainsBasicsAndIntegration();
    testArgumentsOwnershipAndPurity();
    testEventsFocusAndDefaultActions();
    testLifecycleAndCanonicalGeneration();
    testMalformedAncestryAndTraversalBound();
    testRegressionAndEventNesting();
    if (failures != 0) {
        std::cerr << failures << " JS41 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS41 tests PASS (" << checks
        << " checks, 0 failures)\n";
    return 0;
}
