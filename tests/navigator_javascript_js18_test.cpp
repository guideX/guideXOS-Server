#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::RuntimeLimits;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;
using gxos::javascript::kNavigatorScriptMaxPropagationDepth;

namespace {

int failures = 0;
int checks = 0;

const char* kFixture = R"HTML(
<html><body>
<div id="parent">
  <button id="basic" type="button">Basic</button>
  <button id="duplicate" type="button">Duplicate</button>
  <button id="mixed" type="button">Mixed</button>
  <button id="mutation" type="button">Mutation</button>
  <button id="onclick" type="button">Onclick</button>
  <button id="remove" type="button">Remove</button>
  <button id="self" type="button">Self</button>
  <button id="self-persistent" type="button">Self persistent</button>
  <button id="error" type="button">Error</button>
  <button id="stop" type="button">Stop</button>
  <button id="immediate" type="button">Immediate</button>
  <a id="link" href="file:///js18-target.html">Link</a>
</div>
<button id="identity" type="button">Identity</button>
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
        label + ": value");
}

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": expected " +
        std::string(gxos::javascript::runtimeErrorCodeName(expected)) +
        ", got " +
        gxos::javascript::runtimeErrorCodeName(result.runtimeError.code));
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const std::string& id)
{
    for (const gxos::web::HtmlElementRef& element :
        harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0;
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const std::string& label)
{
    expect(harness.loadHtml("file:///js18.html", kFixture, error),
        label + ": fixture loads");
    expect(harness.relayout(), label + ": relayout succeeds");
}

bool click(NavigatorScriptExecutionHarness& harness, const std::string& id,
    RuntimeErrorCode& error, bool* prevented, const std::string& label)
{
    const std::uint64_t serial = serialById(harness, id);
    expect(serial != 0, label + ": serial exists");
    if (serial == 0) return false;
    const bool dispatched = harness.dispatchClick(serial, error, prevented);
    if (harness.documentDirty()) expect(harness.relayout(),
        label + ": relayout succeeds");
    return dispatched;
}

void testOptionParsingAndDuplicates()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "options");
    ScriptResult result = harness.execute(R"JS(
var basic = document.getElementById("basic");
var duplicate = document.getElementById("duplicate");
var mixed = document.getElementById("mixed");
var basicCount = 0;
var falseCount = 0;
var emptyCount = 0;
var onceCount = 0;
var duplicateCount = 0;
var duplicateOnceFirstCount = 0;
function basicHandler(event) { basicCount = basicCount + 1; }
function falseHandler(event) { falseCount = falseCount + 1; }
function emptyHandler(event) { emptyCount = emptyCount + 1; }
function onceHandler(event) { onceCount = onceCount + 1; }
function duplicateHandler(event) { duplicateCount = duplicateCount + 1; }
function duplicateOnceFirstHandler(event) {
    duplicateOnceFirstCount = duplicateOnceFirstCount + 1;
}
basic.addEventListener("click", basicHandler);
basic.addEventListener("click", falseHandler, { once: false });
basic.addEventListener("click", emptyHandler, {});
basic.addEventListener("click", onceHandler, { once: true, banana: 123 });
duplicate.addEventListener("click", duplicateHandler);
duplicate.addEventListener("click", duplicateHandler, { once: true });
mixed.addEventListener("click", duplicateOnceFirstHandler, { once: true });
mixed.addEventListener("click", duplicateOnceFirstHandler);
)JS");
    expect(result.succeeded(), "options: supported forms and duplicates succeed");
    expect(harness.hostAdapter().clickListenerCount() == 6u,
        "options: duplicate options consume no extra slots");
    bool prevented = false;
    click(harness, "basic", error, &prevented, "options: basic first");
    click(harness, "basic", error, &prevented, "options: basic second");
    click(harness, "basic", error, &prevented, "options: basic third");
    expectNumber(harness, "basicCount", 3.0, "options: two-argument persistent");
    expectNumber(harness, "falseCount", 3.0, "options: once false persistent");
    expectNumber(harness, "emptyCount", 3.0, "options: missing once persistent");
    expectNumber(harness, "onceCount", 1.0, "options: once true fires once");
    expect(harness.hostAdapter().clickListenerCount() == 5u,
        "options: once slot released after invocation");
    result = harness.execute(
        "basic.removeEventListener(\"click\", onceHandler);");
    expect(result.succeeded(),
        "options: remove after automatic once removal is harmless");
    result = harness.execute(
        "basic.addEventListener(\"click\", onceHandler, { once: true });");
    expect(result.succeeded(), "options: re-add after once firing succeeds");
    click(harness, "basic", error, &prevented, "options: re-add click");
    expectNumber(harness, "basicCount", 4.0,
        "options: persistent listener remains after re-add");
    expectNumber(harness, "onceCount", 2.0,
        "options: re-added once callback fires once again");
    expect(harness.hostAdapter().clickListenerCount() == 5u,
        "options: re-added once slot releases again");

    click(harness, "duplicate", error, &prevented, "duplicates: persistent first");
    click(harness, "duplicate", error, &prevented, "duplicates: persistent second");
    expectNumber(harness, "duplicateCount", 2.0,
        "duplicates: persistent then once preserves persistent semantics");
    click(harness, "mixed", error, &prevented, "duplicates: once first");
    click(harness, "mixed", error, &prevented, "duplicates: once second");
    expectNumber(harness, "duplicateOnceFirstCount", 1.0,
        "duplicates: once then persistent preserves once semantics");

    result = harness.execute(
        "basic.addEventListener(\"click\", basicHandler, 123);");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "options: numeric third argument rejected");
    result = harness.execute(
        "basic.addEventListener(\"click\", basicHandler, \"once\");");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "options: string third argument rejected");
    result = harness.execute(
        "basic.addEventListener(\"click\", basicHandler, { once: 1 });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "options: non-Boolean once rejected");
    expect(harness.hostAdapter().clickListenerCount() == 4u,
        "options: malformed calls consume no listener slots");
    result = harness.execute(
        "basic.addEventListener(\"mouseover\", basicHandler, { once: true });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "options: unsupported event remains rejected");
    result = harness.execute(
        "basic.addEventListener(\"click\", null, { once: true });");
    expectError(result, RuntimeErrorCode::HostInvalidValue,
        "options: null callback remains rejected");
    expect(harness.hostAdapter().clickListenerCount() == 4u,
        "options: invalid callback and event consume no slots");
}

void testOnceMutationAndEventState()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "mutation");
    ScriptResult result = harness.execute(R"JS(
var mutation = document.getElementById("mutation");
var onclickNode = document.getElementById("onclick");
var remove = document.getElementById("remove");
var self = document.getElementById("self");
var selfPersistent = document.getElementById("self-persistent");
var errorNode = document.getElementById("error");
var stop = document.getElementById("stop");
var immediate = document.getElementById("immediate");
var link = document.getElementById("link");
var parent = document.getElementById("parent");
var mutationLog = "";
var onclickLog = "";
var removeLog = "";
var selfLog = "";
var selfPersistentLog = "";
var errorLog = "";
var stopLog = "";
var immediateLog = "";
var linkLog = "";
var sawDefault = false;
var targetStable = true;
var currentStable = true;
var parentOnceLog = "";
function mutationAdded(event) { mutationLog = mutationLog + "c"; }
function mutationFirst(event) {
    mutationLog = mutationLog + "a";
    mutation.addEventListener("click", mutationAdded, { once: true });
}
mutation.addEventListener("click", mutationFirst);
mutation.addEventListener("click", function(event) { mutationLog = mutationLog + "b"; });
function onclickAdded(event) { onclickLog = onclickLog + "c"; }
onclickNode.onclick = function(event) {
    onclickLog = onclickLog + "o";
    onclickNode.addEventListener("click", onclickAdded, { once: true });
};
function onclickExisting(event) { onclickLog = onclickLog + "a"; }
onclickNode.addEventListener("click", onclickExisting, { once: true });
function removeLater(event) { removeLog = removeLog + "b"; }
remove.onclick = function(event) {
    removeLog = removeLog + "a";
    remove.removeEventListener("click", removeLater);
};
remove.addEventListener("click", removeLater, { once: true });
function selfOnce(event) {
    selfLog = selfLog + "a";
    self.addEventListener("click", selfOnce, { once: true });
}
self.addEventListener("click", selfOnce, { once: true });
function selfPersistentHandler(event) {
    selfPersistentLog = selfPersistentLog + "a";
    selfPersistent.addEventListener("click", selfPersistentHandler);
}
selfPersistent.addEventListener("click", selfPersistentHandler, { once: true });
errorNode.addEventListener("click", function(event) {
    errorLog = errorLog + "a";
    var x = missingOnceIdentifier;
}, { once: true });
errorNode.addEventListener("click", function(event) { errorLog = errorLog + "b"; });
stop.addEventListener("click", function(event) {
    stopLog = stopLog + "a";
    event.stopPropagation();
}, { once: true });
stop.addEventListener("click", function(event) { stopLog = stopLog + "b"; });
immediate.addEventListener("click", function(event) {
    immediateLog = immediateLog + "a";
    event.stopImmediatePropagation();
}, { once: true });
immediate.addEventListener("click", function(event) { immediateLog = immediateLog + "b"; });
parent.addEventListener("click", function(event) {
    stopLog = stopLog + "p";
    immediateLog = immediateLog + "p";
});
parent.addEventListener("click", function(event) {
    parentOnceLog = parentOnceLog + "a";
}, { once: true });
link.addEventListener("click", function(event) {
    linkLog = linkLog + "a";
    event.preventDefault();
}, { once: true });
link.addEventListener("click", function(event) {
    if (event.defaultPrevented) linkLog = linkLog + "d";
    sawDefault = event.defaultPrevented;
    targetStable = targetStable && event.target === link;
    currentStable = currentStable && event.currentTarget === link;
});
)JS");
    expect(result.succeeded(), "mutation: setup succeeds");
    bool prevented = false;
    click(harness, "mutation", error, &prevented, "mutation: first");
    click(harness, "mutation", error, &prevented, "mutation: second");
    click(harness, "mutation", error, &prevented, "mutation: third");
    expectString(harness, "mutationLog", "ababcab",
        "mutation: added once listener waits for next dispatch");
    expect(harness.hostAdapter().clickListenerCount() == 16u,
        "mutation: fired once listener releases its slot");
    expectString(harness, "parentOnceLog", "a",
        "mutation: ancestor once listener fires once from descendant clicks");

    click(harness, "onclick", error, &prevented, "onclick: first");
    click(harness, "onclick", error, &prevented, "onclick: second");
    click(harness, "onclick", error, &prevented, "onclick: third");
    expectString(harness, "onclickLog", "oaoco",
        "onclick: added once listener waits and refires after removal");

    click(harness, "remove", error, &prevented, "remove: earlier callback");
    expectString(harness, "removeLog", "a",
        "remove: later once listener is skipped and remains removed");
    ScriptResult harmless = harness.execute(
        "remove.removeEventListener(\"click\", removeLater);");
    expect(harmless.succeeded(), "remove: after automatic/normal removal is harmless");

    click(harness, "self", error, &prevented, "self: first");
    click(harness, "self", error, &prevented, "self: second");
    click(harness, "self", error, &prevented, "self: third");
    expectString(harness, "selfLog", "aaa",
        "self: once callback re-registers itself once per click");
    expect(harness.hostAdapter().clickListenerCount() == 15u,
        "self: re-registration keeps one bounded active slot");

    click(harness, "self-persistent", error, &prevented,
        "self persistent: first");
    click(harness, "self-persistent", error, &prevented,
        "self persistent: second");
    click(harness, "self-persistent", error, &prevented,
        "self persistent: third");
    expectString(harness, "selfPersistentLog", "aaa",
        "self persistent: replacement runs on later clicks only");

    const bool firstError = click(harness, "error", error, &prevented,
        "error: first once callback");
    expect(!firstError && error == RuntimeErrorCode::UnknownIdentifier,
        "error: callback failure is contained and reported");
    click(harness, "error", error, &prevented, "error: second click");
    expectString(harness, "errorLog", "abb",
        "error: once callback removed before error and later listener survives");

    result = harness.execute("stopLog = \"\";");
    expect(result.succeeded(), "stop: reset isolated log");
    click(harness, "stop", error, &prevented, "stop: first");
    click(harness, "stop", error, &prevented, "stop: second");
    expectString(harness, "stopLog", "abbp",
        "stop: once stopPropagation allows same-node then bubbles later");
    result = harness.execute("immediateLog = \"\";");
    expect(result.succeeded(), "immediate: reset isolated log");
    click(harness, "immediate", error, &prevented, "immediate: first");
    click(harness, "immediate", error, &prevented, "immediate: second");
    expectString(harness, "immediateLog", "abp",
        "immediate: once stopImmediatePropagation is removed before stopping");

    const bool firstLink = click(harness, "link", error, &prevented,
        "link: first");
    expect(firstLink && prevented, "link: once preventDefault cancels first action");
    expectString(harness, "linkLog", "ad",
        "link: later listener sees defaultPrevented");
    const bool secondLink = click(harness, "link", error, &prevented,
        "link: second");
    expect(secondLink && !prevented, "link: second click is no longer cancelled");
    expectBoolean(harness, "sawDefault", false,
        "link: defaultPrevented resets on next click");
    expectBoolean(harness, "targetStable", true, "event: target remains canonical");
    expectBoolean(harness, "currentStable", true,
        "event: currentTarget remains canonical");
}

void testCapacityOrderingAndStress()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness capacity;
    const std::string html = [] {
        std::string value = "<html><body>";
        for (int index = 0; index < 65; ++index)
            value += "<button id=\"e" + std::to_string(index) +
                "\" type=\"button\">E</button>";
        return value + "</body></html>";
    }();
    expect(capacity.loadHtml("file:///js18-capacity.html", html, error),
        "capacity: fixture loads");
    expect(capacity.relayout(), "capacity: relayout succeeds");
    std::string source = "var target = document.getElementById(\"e0\");var calls = 0;";
    for (int index = 0; index < 64; ++index) {
        source += "function l" + std::to_string(index) +
            "(event) { calls = calls + 1; }";
        source += "target.addEventListener(\"click\", l" +
            std::to_string(index) + ", { once: true });";
    }
    ScriptResult result = capacity.execute(source);
    expect(result.succeeded(), "capacity: 64 once registrations succeed");
    expect(capacity.hostAdapter().clickListenerCount() == 64u,
        "capacity: fixed global capacity reaches exactly 64");
    result = capacity.execute(
        "target.addEventListener(\"click\", function(event) {});");
    expectError(result, RuntimeErrorCode::HostCallbackLimitExceeded,
        "capacity: 65th registration is rejected");
    bool prevented = false;
    const std::uint64_t targetSerial = serialById(capacity, "e0");
    expect(capacity.dispatchClick(targetSerial, error, &prevented),
        "capacity: all once callbacks dispatch");
    expectNumber(capacity, "calls", 64.0,
        "capacity: all 64 once callbacks execute in order");
    expect(capacity.hostAdapter().clickListenerCount() == 0u,
        "capacity: all once slots release after dispatch");
    result = capacity.execute(
        "function replacement(event) {} target.addEventListener(\"click\", replacement);");
    expect(result.succeeded(), "capacity: released slot accepts replacement");
    expect(capacity.hostAdapter().clickListenerCount() == 1u,
        "capacity: replacement occupies released capacity");

    NavigatorScriptExecutionHarness ordering;
    loadFixture(ordering, error, "ordering");
    result = ordering.execute(R"JS(
var basic = document.getElementById("basic");
var orderingLog = "";
basic.onclick = function(event) { orderingLog = orderingLog + "o"; };
function orderA(event) { orderingLog = orderingLog + "A"; }
function orderB(event) { orderingLog = orderingLog + "B"; }
function orderC(event) { orderingLog = orderingLog + "C"; }
function orderD(event) { orderingLog = orderingLog + "D"; }
function orderE(event) { orderingLog = orderingLog + "E"; }
basic.addEventListener("click", orderA);
basic.addEventListener("click", orderB, { once: true });
basic.addEventListener("click", orderC);
basic.addEventListener("click", orderE, { once: true });
)JS");
    expect(result.succeeded(), "ordering: mixed setup succeeds");
    click(ordering, "basic", error, &prevented, "ordering: first");
    result = ordering.execute("basic.addEventListener(\"click\", orderD);");
    expect(result.succeeded(), "ordering: post-removal append succeeds");
    click(ordering, "basic", error, &prevented, "ordering: second");
    expectString(ordering, "orderingLog", "oABCEoACD",
        "ordering: onclick, multiple once listeners, and slot reuse order");

    NavigatorScriptExecutionHarness identity;
    loadFixture(identity, error, "identity");
    result = identity.execute(R"JS(
var identity = document.getElementById("identity");
var identityLog = "";
function identicalA(event) { identityLog = identityLog + "a"; }
function identicalB(event) { identityLog = identityLog + "a"; }
identity.addEventListener("click", identicalA, { once: true });
identity.addEventListener("click", identicalB);
identity.addEventListener("click", identicalA, { once: true });
)JS");
    expect(result.succeeded(), "identity: same-source function setup succeeds");
    expect(identity.hostAdapter().clickListenerCount() == 2u,
        "identity: duplicate once registration consumes one slot");
    click(identity, "identity", error, &prevented, "identity: first");
    click(identity, "identity", error, &prevented, "identity: second");
    expectString(identity, "identityLog", "aaa",
        "identity: distinct same-source functions coexist");
    expect(identity.hostAdapter().clickListenerCount() == 1u,
        "identity: only persistent same-source function remains");

    NavigatorScriptExecutionHarness readd;
    loadFixture(readd, error, "readd");
    result = readd.execute(R"JS(
var target = document.getElementById("basic");
var outer = 10;
var readdLog = "";
function retainedHandler(event) { readdLog = readdLog + outer; }
var saved = retainedHandler;
target.addEventListener("click", retainedHandler, { once: true });
)JS");
    expect(result.succeeded(), "readd: retained function setup succeeds");
    click(readd, "basic", error, &prevented, "readd: first");
    expectString(readd, "readdLog", "10",
        "readd: once callback preserves captured state");
    result = readd.execute(
        "target.removeEventListener(\"click\", saved);");
    expect(result.succeeded(), "readd: remove after automatic removal is harmless");
    result = readd.execute(
        "target.addEventListener(\"click\", saved, { once: true });");
    expect(result.succeeded(), "readd: same function can register again");
    click(readd, "basic", error, &prevented, "readd: second");
    expectString(readd, "readdLog", "1010",
        "readd: retained function fires once on a new registration");
    expect(readd.hostAdapter().clickListenerCount() == 0u,
        "readd: automatic removal leaves no stale capacity");

    RuntimeLimits stressLimits;
    stressLimits.maxEnvironments = 1024u;
    NavigatorScriptExecutionHarness stress(stressLimits);
    loadFixture(stress, error, "stress");
    result = stress.execute(R"JS(
var self = document.getElementById("self");
var stressOnce = 0;
var stressPersistent = 0;
var onceOptions = { once: true };
function stressOnceHandler(event) {
    stressOnce = stressOnce + 1;
    self.addEventListener("click", stressOnceHandler, onceOptions);
}
self.addEventListener("click", stressOnceHandler, onceOptions);
self.addEventListener("click", function(event) { stressPersistent = stressPersistent + 1; });
)JS");
    expect(result.succeeded(), "stress: persistent and re-registering once setup");
    const std::size_t objectsBefore = stress.runtime().objectCount();
    for (int index = 0; index < 100; ++index)
        expect(click(stress, "self", error, &prevented,
            "stress: click " + std::to_string(index + 1)),
            "stress: dispatch succeeds");
    expectNumber(stress, "stressOnce", 100.0,
        "stress: once re-registration executes exactly once per click");
    expectNumber(stress, "stressPersistent", 100.0,
        "stress: persistent listener remains bounded");
    expect(stress.hostAdapter().clickListenerCount() == 2u,
        "stress: listener table remains fixed after 100 clicks");
    expect(stress.runtime().objectCount() == objectsBefore + 1u,
        "stress: cached Event prevents object growth");
}

void testOverflowNavigationCleanup()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness overflow;
    std::string deep = "<html><body>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
         ++index)
        deep += "<div>";
    deep += "<button id=\"deep\" type=\"button\">Deep</button>";
    for (std::size_t index = 0; index < kNavigatorScriptMaxPropagationDepth;
         ++index)
        deep += "</div>";
    deep += "</body></html>";
    expect(overflow.loadHtml("file:///js18-overflow.html", deep, error),
        "overflow: fixture loads");
    expect(overflow.relayout(), "overflow: relayout succeeds");
    ScriptResult result = overflow.execute(R"JS(
var deep = document.getElementById("deep");
var overflowCalls = 0;
function overflowHandler(event) { overflowCalls = overflowCalls + 1; }
deep.addEventListener("click", overflowHandler, { once: true });
)JS");
    expect(result.succeeded(), "overflow: once registration succeeds");
    bool prevented = false;
    const std::uint64_t deepSerial = serialById(overflow, "deep");
    const bool overflowed = overflow.dispatchClick(deepSerial, error, &prevented);
    expect(!overflowed && error == RuntimeErrorCode::PropagationPathLimitExceeded,
        "overflow: path failure remains deterministic");
    expectNumber(overflow, "overflowCalls", 0.0,
        "overflow: once callback was never invoked");
    expect(overflow.hostAdapter().clickListenerCount() == 1u,
        "overflow: failed path does not consume once registration");
    for (gxos::web::HtmlElementRef& element :
         overflow.document().structuralElements) {
        if (element.serial == deepSerial) {
            element.parentSerial = 0;
            break;
        }
    }
    expect(overflow.dispatchClick(deepSerial, error, &prevented),
        "overflow: repaired valid path dispatches");
    expectNumber(overflow, "overflowCalls", 1.0,
        "overflow: once callback fires after later valid dispatch");
    expect(overflow.hostAdapter().clickListenerCount() == 0u,
        "overflow: valid dispatch releases once slot");

    NavigatorScriptExecutionHarness navigation;
    loadFixture(navigation, error, "navigation");
    result = navigation.execute(R"JS(
var old = document.getElementById("basic");
var oldCalls = 0;
old.addEventListener("click", function(event) { oldCalls = oldCalls + 1; },
    { once: true });
)JS");
    expect(result.succeeded(), "navigation: old once registration succeeds");
    expect(navigation.replaceHtml("file:///js18-new.html",
        "<html><body><button id=\"new\" type=\"button\">New</button></body></html>",
        error), "navigation: replacement succeeds");
    expect(navigation.hostAdapter().clickListenerCount() == 0u,
        "navigation: old once table is cleared");
    result = navigation.execute(
        "var fresh = document.getElementById(\"new\");var freshCalls = 0;"
        "fresh.addEventListener(\"click\", function(event) { freshCalls = freshCalls + 1; });");
    expect(result.succeeded(), "navigation: fresh listener succeeds");
    click(navigation, "new", error, &prevented, "navigation: fresh click");
    expectNumber(navigation, "freshCalls", 1.0,
        "navigation: stale once callback cannot execute in new document");
}

} // namespace

int main()
{
    testOptionParsingAndDuplicates();
    testOnceMutationAndEventState();
    testCapacityOrderingAndStress();
    testOverflowNavigationCleanup();
    if (failures != 0) {
        std::cerr << failures << " JS18 test failure(s) across " << checks <<
            " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS18 tests PASS (" << checks <<
        " checks, 0 failures)\n";
    return 0;
}
