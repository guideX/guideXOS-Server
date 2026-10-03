#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;
int checks = 0;
std::string fixture;

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

void reportScriptFailure(const ScriptResult& result, const char* label)
{
    if (result.succeeded()) return;
    std::cerr << "INFO: " << label << " error="
        << gxos::javascript::runtimeErrorCodeName(result.runtimeError.code)
        << " at=" << result.runtimeError.location.line << ":"
        << result.runtimeError.location.column << "\n";
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const char* label)
{
    expect(!fixture.empty(), "JS56 fixture is available");
    expect(harness.loadHtml("file:///javascript-js56.html", fixture, error),
        std::string(label) + ": fixture parses");
    expect(error == RuntimeErrorCode::None,
        std::string(label) + ": fixture load succeeds");
    expect(harness.relayout(), std::string(label) + ": relayout succeeds");
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

using ElementSnapshot = std::tuple<std::uint64_t, std::uint64_t, std::string,
    std::string, std::string, std::uint16_t, std::uint16_t, std::uint16_t,
    std::uint16_t, std::uint64_t>;

std::vector<ElementSnapshot> snapshotStructure(
    const gxos::web::WebDocument& document)
{
    std::vector<ElementSnapshot> result;
    result.reserve(document.structuralElements.size());
    for (const gxos::web::HtmlElementRef& element :
            document.structuralElements) {
        result.emplace_back(element.serial, element.parentSerial,
            element.tagName, element.id, element.className, element.childIndex,
            element.childCount, element.siblingCount, element.typeIndex,
            element.previousSiblingSerial);
    }
    return result;
}

void testRootSelectorCompositionAndJs58Empty()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "root");
    const ScriptResult result = harness.execute(R"JS(
var root = document.querySelector(":root");
var body = document.getElementById("body");
var panel = document.getElementById("panel");
var fallback = document.getElementById("fallback");
var emptySlot = document.getElementById("empty-slot");
var pseudoOnly = root !== null && root.matches(":root") &&
  !body.matches(":root") && !panel.matches(":root");
var caseInsensitive = root.matches(":ROOT") && root.matches(":Root") &&
  document.querySelector(":rOoT") === root;
var universal = document.querySelector("*:root") === root &&
  root.matches("*:root");
var tag = document.querySelector("html:root") === root &&
  !body.matches("body:root");
var compoundFilters = root.matches("#rootId:root") &&
  root.matches(".document-root:root") &&
  root.matches("[data-root=yes]:root") &&
  root.matches("html#rootId.document-root[data-root=yes]:root");
var wrongFilters = !root.matches("#body:root") &&
  !root.matches(".fallback:root") && !root.matches("[data-root=no]:root") &&
  !fallback.matches(":root");
var queryApis = document.querySelector(":root") === root &&
  document.querySelectorAll(":root").length === 1 &&
  document.querySelectorAll(":root")[0] === root &&
  document.querySelector("html") === root;
var matchesAndClosest = root.matches(":root") &&
  !body.matches(":root") && root.closest(":root") === root &&
  body.closest(":root") === root && panel.closest(":root") === root;
var rootVsChildPseudos = root.parentElement === null &&
  !root.matches(":first-child") && !root.matches(":only-child") &&
  body.matches(":first-child") && body.matches(":only-child");
var relations = document.querySelector(":root > body") === body &&
  document.querySelector(":root .panel") === panel &&
  document.querySelector(".panel > :root") === null &&
  document.querySelector("body > :root") === null;
var selectorLists = document.querySelectorAll(":root, .fallback").length === 2 &&
  document.querySelectorAll(":root, .fallback")[0] === root &&
  document.querySelectorAll(":root, .fallback")[1] === fallback &&
  document.querySelectorAll(":root, html:root").length === 1 &&
  document.querySelectorAll("body, :root")[0] === root &&
  document.querySelector("body, :root") === root;
var malformedFailsClosed = document.querySelector(":root()") === null &&
  document.querySelector(":root:first-child") === null &&
  document.querySelector("html:root:focus") === null &&
  document.querySelector(":root:hover") === null;
var emptySupported = document.querySelector(":empty") === emptySlot &&
  emptySlot.matches(":EMPTY") &&
  document.querySelector(":empty()") === null &&
  document.querySelectorAll(":empty").length === 1 &&
  body.matches(":empty") === false;
)JS");
    expect(result.succeeded(), "root selectors and JS58 :empty execute");
    reportScriptFailure(result, "root composition");
    const char* names[] = {"pseudoOnly", "caseInsensitive", "universal", "tag",
        "compoundFilters", "wrongFilters", "queryApis", "matchesAndClosest",
        "rootVsChildPseudos", "relations", "selectorLists",
        "malformedFailsClosed", "emptySupported"};
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("root: ") + name);
}

void testCanonicalRootAuthorityAndParentlessNonRoot()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "root authority");
    expect(harness.document().hasDocumentElement,
        "parser designates a document Element");
    expect(harness.document().documentElement.tagName == "html",
        "parser-designated document Element is html");
    const std::uint64_t canonicalSerial =
        harness.document().documentElement.serial;
    expect(canonicalSerial != 0u &&
        serialById(harness, "rootId") == canonicalSerial,
        "documentElement serial identifies the parser's structural html");

    gxos::web::HtmlElementRef synthetic;
    synthetic.tagName = "section";
    synthetic.id = "synthetic-rootlike";
    synthetic.serial = 1u;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements)
        synthetic.serial = std::max(synthetic.serial, element.serial + 1u);
    synthetic.parentSerial = 0u;
    harness.document().structuralElements.push_back(synthetic);
    const ScriptResult extraRoot = harness.execute(R"JS(
var syntheticRootlike = document.querySelector("#synthetic-rootlike");
var parentlessIsNotRoot = syntheticRootlike !== null &&
  syntheticRootlike.parentElement === null &&
  !syntheticRootlike.matches(":root") &&
  document.querySelector(":root").matches(":root") &&
  document.querySelectorAll(":root").length === 1;
)JS");
    expect(extraRoot.succeeded(), "synthetic parentless non-root check executes");
    reportScriptFailure(extraRoot, "parentless non-root");
    expectBoolean(harness, "parentlessIsNotRoot", true,
        "only the designated documentElement matches :root");

    harness.document().hasDocumentElement = false;
    const ScriptResult absentAuthority = harness.execute(R"JS(
var absentRootAuthorityFailsClosed =
  document.querySelector(":root") === null &&
  document.querySelectorAll(":root").length === 0 &&
  !document.querySelector("#synthetic-rootlike").matches(":root");
)JS");
    expect(absentAuthority.succeeded(),
        "missing canonical root authority check executes");
    reportScriptFailure(absentAuthority, "missing root authority");
    expectBoolean(harness, "absentRootAuthorityFailsClosed", true,
        "parent-null structure does not substitute for missing documentElement");
    harness.document().hasDocumentElement = true;

    NavigatorScriptExecutionHarness multipleParentless;
    const std::string unusual =
        "<body id=early-body><div id=early-child></div></body>"
        "<html id=chosen-root><body id=chosen-body></body></html>";
    expect(multipleParentless.loadHtml("file:///multiple-parentless.html",
            unusual, error),
        "parser accepts structural Elements both before and after html");
    std::size_t parentlessCount = 0u;
    for (const gxos::web::HtmlElementRef& element :
            multipleParentless.document().structuralElements) {
        if (element.serial != 0u && element.parentSerial == 0u)
            ++parentlessCount;
    }
    expect(parentlessCount > 1u,
        "live parser model permits multiple parentless structural Elements");
    const ScriptResult multipleRoots = multipleParentless.execute(R"JS(
var onlyDesignatedRootMatches =
  document.querySelector(":root") === document.querySelector("#chosen-root") &&
  document.querySelector("#chosen-root").matches(":root") &&
  !document.querySelector("#early-body").matches(":root") &&
  document.querySelectorAll(":root").length === 1;
)JS");
    expect(multipleRoots.succeeded(),
        "multiple parentless root selector proof executes");
    reportScriptFailure(multipleRoots, "multiple parentless roots");
    expectBoolean(multipleParentless, "onlyDesignatedRootMatches", true,
        "documentElement serial wins when multiple Elements have no parent");
}

void testStressPurityEventsAndGenerationSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    gxos::javascript::RuntimeLimits limits;
    limits.maxHostOperations = 100000u;
    limits.maxRuntimeStringValues = 20000u;
    limits.maxTotalRuntimeStringBytes = 1024u * 1024u;
    NavigatorScriptExecutionHarness harness(limits);
    loadFixture(harness, error, "stress and generation");
    const std::uint64_t rootSerial = harness.document().documentElement.serial;
    const std::uint64_t firstSerial = serialById(harness, "click-one");
    const auto before = snapshotStructure(harness.document());
    const std::uint64_t mutationCount = harness.document().scriptMutationCount;
    const std::uint64_t layoutRevision = harness.document().layoutRevision;
    const auto generation = harness.hostAdapter().generation();

    const ScriptResult stress = harness.execute(R"JS(
var root = document.querySelector(":root");
var deterministic = root !== null;
for (var i = 0; i < 1000; i = i + 1) {
  if (!root.matches(":root") || document.querySelector(":root") !== root)
    deterministic = false;
}
var heldRoot = document.querySelectorAll(":root");
var heldDeterministic = true;
for (var j = 0; j < 320; j = j + 1) {
  if (heldRoot.length !== 1 || heldRoot[0] !== root) heldDeterministic = false;
}
var oldRoot = root;
var oldRootCollection = heldRoot;
)JS");
    expect(stress.succeeded(),
        "1,000 root matches/queries and 320 held collection rereads complete");
    reportScriptFailure(stress, "root stress");
    expectBoolean(harness, "deterministic", true,
        "repeated matches and querySelector preserve root identity");
    expectBoolean(harness, "heldDeterministic", true,
        "held live root collection rereads remain stable");
    expect(before == snapshotStructure(harness.document()) &&
        mutationCount == harness.document().scriptMutationCount &&
        layoutRevision == harness.document().layoutRevision &&
        generation == harness.hostAdapter().generation(),
        "root matching is pure over structure, mutation, layout, and generation state");

    const ScriptResult events = harness.execute(R"JS(
var root = document.querySelector(":root");
var first = document.getElementById("click-one");
var second = document.getElementById("click-two");
var firstEvent = false;
var secondEvent = false;
var nestedEvent = false;
var eventMetadata = false;
first.addEventListener("click", function (event) {
  var target = event.target;
  var current = event.currentTarget;
  var phase = event.eventPhase;
  firstEvent = target.closest(":root") === root;
  second.click();
  nestedEvent = event.target === target && event.currentTarget === current &&
    event.eventPhase === phase;
  eventMetadata = event.relatedTarget === null &&
    event.defaultPrevented === false && current === first && phase === 2;
});
second.addEventListener("click", function (event) {
  secondEvent = event.target.closest(":root") === root;
});
)JS");
    expect(events.succeeded(), "root-aware event handlers install");
    reportScriptFailure(events, "root event setup");
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(firstSerial, clickError),
        "root-aware click and nested dispatch succeed");
    expect(clickError == RuntimeErrorCode::None,
        "root-aware click dispatch has no runtime error");
    for (const char* name : {"firstEvent", "secondEvent", "nestedEvent",
            "eventMetadata"})
        expectBoolean(harness, name, true,
            std::string("event integration: ") + name);

    expect(harness.invalidateDocumentGeneration(error),
        "root test advances document generation");
    const ScriptResult stale = harness.execute(R"JS(
var staleRootFailsClosed = !oldRoot.matches(":root") &&
  oldRoot.closest(":root") === null;
)JS");
    expect(stale.succeeded(), "stale root checks remain script-safe");
    reportScriptFailure(stale, "stale root");
    expectBoolean(harness, "staleRootFailsClosed", true,
        "old-generation root handle cannot match replacement root state");
    expect(!harness.execute("var staleRootLength = oldRootCollection.length;").succeeded(),
        "old-generation root collection fails closed");

    harness.document() = gxos::web::parseHtml(
        "file:///js56-replacement.html", fixture);
    const std::uint64_t replacementSerial =
        harness.document().documentElement.serial;
    expect(replacementSerial == rootSerial,
        "replacement parser document reuses the root serial");
    const ScriptResult staleAcrossReplacement = harness.execute(
        "var oldRootCannotSeeReplacement = !oldRoot.matches(':root');");
    expect(staleAcrossReplacement.succeeded(),
        "old-generation root is checked against replacement document");
    reportScriptFailure(staleAcrossReplacement, "stale root after replacement");
    expectBoolean(harness, "oldRootCannotSeeReplacement", true,
        "old-generation root cannot match replacement documentElement");
    expect(harness.runtime().installHostGlobal("freshRoot", replacementSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "replacement root receives a current-generation handle");
    expect(harness.runtime().installHostGlobal("freshDocument",
            gxos::javascript::kNavigatorDocumentHostInstance,
            gxos::javascript::kNavigatorDocumentHostKind, error),
        "replacement document receives a current-generation handle");
    const ScriptResult reuse = harness.execute(R"JS(
var replacementRootIsCurrent = freshRoot.matches(":root") &&
  freshDocument.querySelector(":root") === freshRoot;
)JS");
    expect(reuse.succeeded(), "root serial reuse checks execute");
    reportScriptFailure(reuse, "root serial reuse");
    expectBoolean(harness, "replacementRootIsCurrent", true,
        "replacement root uses current generation-local serial authority");
    expect(!harness.execute(
        "var reusedOldRootLength = oldRootCollection.length;").succeeded(),
        "stale root collection stays isolated after serial reuse");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js56.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testRootSelectorCompositionAndJs58Empty();
    testCanonicalRootAuthorityAndParentlessNonRoot();
    testStressPurityEventsAndGenerationSafety();
    if (failures != 0) {
        std::cerr << failures << " JS56 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    const std::size_t simple =
        sizeof(gxos::javascript::NavigatorScriptSimpleSelectorDescriptor);
    const std::size_t descriptor =
        sizeof(gxos::javascript::NavigatorScriptSelectorDescriptor);
    const std::size_t collection = (16u + descriptor + 7u) / 8u * 8u;
    std::cout << "Navigator JavaScript JS56 checks: " << checks << "/"
        << checks << " passed; simple=" << simple << " bytes four-member="
        << descriptor << " bytes collection-record=" << collection
        << " bytes registry="
        << collection * gxos::javascript::kNavigatorScriptMaxSelectorCollections
        << " bytes\n";
    return 0;
}
