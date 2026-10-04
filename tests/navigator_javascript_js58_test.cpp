#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

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

const gxos::javascript::Value* binding(
    const gxos::javascript::NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const std::string key(name);
    return harness.runtime().lookup(
        gxos::javascript::SourceView(key.data(), key.size()));
}

void expectBoolean(
    const gxos::javascript::NavigatorScriptExecutionHarness& harness,
    const char* name, bool expected, const std::string& label)
{
    const gxos::javascript::Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (!value) return;
    expect(value->isBoolean(), label + ": Boolean value");
    if (value->isBoolean())
        expect(value->booleanValue() == expected, label + ": expected value");
}

gxos::web::HtmlElementRef* elementById(
    gxos::web::WebDocument& document, const std::string& id)
{
    for (gxos::web::HtmlElementRef& element : document.structuralElements)
        if (element.id == id) return &element;
    return nullptr;
}

const gxos::web::HtmlElementRef* elementById(
    const gxos::web::WebDocument& document, const std::string& id)
{
    for (const gxos::web::HtmlElementRef& element : document.structuralElements)
        if (element.id == id) return &element;
    return nullptr;
}

gxos::web::HtmlElementContentMetadata* contentFor(
    gxos::web::WebDocument& document, std::uint64_t serial)
{
    for (gxos::web::HtmlElementContentMetadata& content : document.contentMetadata)
        if (content.serial == serial) return &content;
    return nullptr;
}

const gxos::web::HtmlElementContentMetadata* contentFor(
    const gxos::web::WebDocument& document, std::uint64_t serial)
{
    for (const gxos::web::HtmlElementContentMetadata& content : document.contentMetadata)
        if (content.serial == serial) return &content;
    return nullptr;
}

bool sameStructure(const std::vector<gxos::web::HtmlElementRef>& left,
    const std::vector<gxos::web::HtmlElementRef>& right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0u; index < left.size(); ++index) {
        const gxos::web::HtmlElementRef& a = left[index];
        const gxos::web::HtmlElementRef& b = right[index];
        if (a.tagName != b.tagName || a.className != b.className ||
            a.id != b.id || a.serial != b.serial ||
            a.parentSerial != b.parentSerial || a.childIndex != b.childIndex ||
            a.childCount != b.childCount || a.siblingCount != b.siblingCount ||
            a.typeIndex != b.typeIndex || a.typeCount != b.typeCount ||
            a.previousSiblingSerial != b.previousSiblingSerial) return false;
    }
    return true;
}

bool sameContent(const std::vector<gxos::web::HtmlElementContentMetadata>& left,
    const std::vector<gxos::web::HtmlElementContentMetadata>& right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0u; index < left.size(); ++index) {
        const gxos::web::HtmlElementContentMetadata& a = left[index];
        const gxos::web::HtmlElementContentMetadata& b = right[index];
        if (a.serial != b.serial || a.elementChildCount != b.elementChildCount ||
            a.visibleTextByteCount != b.visibleTextByteCount ||
            a.hasElementChild != b.hasElementChild ||
            a.hasDirectTextChild != b.hasDirectTextChild ||
            a.hasNonWhitespaceText != b.hasNonWhitespaceText ||
            a.hasImageOrMediaChild != b.hasImageOrMediaChild ||
            a.hasVisibleBreak != b.hasVisibleBreak ||
            a.hasVisibleReplacedContent != b.hasVisibleReplacedContent ||
            a.hasRenderableContent != b.hasRenderableContent ||
            a.contentMetadataComplete != b.contentMetadataComplete) return false;
    }
    return true;
}

std::uint64_t serialById(
    const gxos::web::WebDocument& document, const std::string& id)
{
    const gxos::web::HtmlElementRef* element = elementById(document, id);
    return element ? element->serial : 0u;
}

bool execute(gxos::javascript::NavigatorScriptExecutionHarness& harness,
    const std::string& source, const std::string& label)
{
    const gxos::javascript::ScriptResult result = harness.execute(source);
    expect(result.succeeded(), label + " executes");
    if (!result.succeeded()) {
        std::cerr << "INFO: " << label << " error="
            << gxos::javascript::runtimeErrorCodeName(
                result.runtimeError.code) << " at="
            << result.runtimeError.location.line << ":"
            << result.runtimeError.location.column << "\n";
    }
    return result.succeeded();
}

void testParserAndCoreSemantics(
    gxos::javascript::NavigatorScriptExecutionHarness& harness)
{
    execute(harness, R"JS(
var empty = document.getElementById("empty");
var text = document.getElementById("text");
var comment = document.getElementById("comment");
var child = document.getElementById("child");
var mixed = document.getElementById("mixed");
var outer = document.getElementById("outer");
var inner = document.getElementById("inner");
var emptyBasic = empty.matches(":empty") && comment.matches(":empty") &&
  !text.matches(":empty") && !document.getElementById("space").matches(":empty") &&
  !document.getElementById("tab").matches(":empty") &&
  !document.getElementById("cr").matches(":empty") &&
  !document.getElementById("lf").matches(":empty") &&
  !document.getElementById("entity").matches(":empty") &&
  !document.getElementById("numeric").matches(":empty") &&
  !document.getElementById("newline").matches(":empty") &&
  !child.matches(":empty") && !mixed.matches(":empty") &&
  !outer.matches(":empty") && !inner.matches(":empty") &&
  !document.getElementById("hidden-text").matches(":empty") &&
  !document.getElementById("hidden-parent").matches(":empty");
var emptyDirectness = outer.childElementCount === 1 &&
  !outer.matches(":empty") &&
  inner.childElementCount === 0 && !inner.matches(":empty") &&
  empty.childElementCount === 0 && empty.matches(":empty");
var emptyParser = empty.matches(":EMPTY") && empty.matches(":Empty") &&
  empty.matches("*:empty") && empty.matches("div:empty") &&
  empty.matches("#empty:empty") && empty.matches(".slot:empty") &&
  empty.matches("[data-state=idle]:empty") &&
  empty.matches("div#empty.slot[data-state=idle]:empty") &&
  document.querySelector(":empty") === empty &&
  document.querySelector(".slot:empty") === empty &&
  document.querySelector("#empty:empty") === empty &&
  document.querySelector("div#empty.slot[data-state=idle]:empty") === empty;
var emptyMalformed = document.querySelector(":empty()") === null &&
  document.querySelector(":emptyx") === null &&
  document.querySelector(":hover") === null &&
  document.querySelector("div:empty:first-child") === null &&
  document.querySelector("div:first-child:empty") === null &&
  !empty.matches(":empty:first-child") && !empty.matches(":first-child:empty") &&
  document.querySelector(".group .slot:empty + .other") === null;
var emptyQueries = document.querySelectorAll(".slot:empty");
var emptyQueryApi = emptyQueries.length === 4 && emptyQueries[0] === empty &&
  emptyQueries[1] === comment &&
  emptyQueries[2] === document.getElementById("relation-empty") &&
  emptyQueries[3] === document.getElementById("relation-empty-two") &&
  document.querySelector(":root:empty") === null;
var emptyClosest = empty.closest(".slot:empty") === empty &&
  comment.closest(":empty") === comment &&
  inner.closest(":empty") === null &&
  inner.closest(".container:empty") === null;
var emptyScoped = document.getElementById("relation-group").querySelector(
  ".slot:empty") === document.getElementById("relation-empty") &&
  document.getElementById("relation-group").querySelector("#empty") === null &&
  empty.querySelector(":empty") === null;
var emptyRelations = document.querySelector(".group > .slot:empty") ===
  document.getElementById("relation-empty") &&
  document.querySelector(".slot:empty + .other") ===
    document.getElementById("relation-empty-two") &&
  document.querySelector(".slot:empty + .slot:empty") ===
    document.getElementById("relation-empty-two") &&
  document.querySelector("#relation-empty:empty + #relation-empty-two:empty") ===
    document.getElementById("relation-empty-two") &&
  document.querySelector(".group .slot:empty") ===
    document.getElementById("relation-empty");
var emptyLists = document.querySelectorAll(":root, .slot:empty");
var emptyListOrder = emptyLists.length === 5 &&
  emptyLists[0] === document.querySelector(":root") &&
  emptyLists[1] === empty &&
  document.querySelectorAll(".slot:empty, :root")[0] === emptyLists[0] &&
  document.querySelectorAll(".slot:empty, #empty:empty").length === 4 &&
  document.querySelectorAll(":root, :root, .slot:empty").length === 5 &&
  document.querySelectorAll("#text, :root, #comment:empty, #empty:empty").length === 4;
var emptyPosition = document.getElementById("empty-first").matches(":empty") &&
  document.getElementById("empty-first").matches(":first-child") &&
  !document.getElementById("empty-first").matches(":only-child") &&
  document.getElementById("empty-second").matches(":empty") &&
  !document.getElementById("empty-second").matches(":first-child") &&
  document.getElementById("empty-second").matches(":nth-child(2)") &&
  !document.getElementById("empty-second").matches(":last-child");
)JS", "parser, semantics, and query APIs");

    expectBoolean(harness, "emptyBasic", true, "empty semantic matrix");
    expectBoolean(harness, "emptyDirectness", true, "direct and descendant text");
    expectBoolean(harness, "emptyParser", true, "pseudo grammar and compounds");
    expectBoolean(harness, "emptyMalformed", true, "malformed forms fail closed");
    expectBoolean(harness, "emptyQueryApi", true, "querySelectorAll order and scope");
    expectBoolean(harness, "emptyClosest", true, "matches and closest");
    expectBoolean(harness, "emptyScoped", true, "scoped queries exclude receiver");
    expectBoolean(harness, "emptyRelations", true, "one-relation integration");
    expectBoolean(harness, "emptyListOrder", true, "selector list order and dedup");
    expectBoolean(harness, "emptyPosition", true, "empty is independent of position");
    const gxos::web::HtmlElementRef* outer =
        elementById(harness.document(), "outer");
    const gxos::web::HtmlElementRef* inner =
        elementById(harness.document(), "inner");
    const gxos::web::HtmlElementContentMetadata* outerContent = outer
        ? contentFor(harness.document(), outer->serial) : nullptr;
    const gxos::web::HtmlElementContentMetadata* innerContent = inner
        ? contentFor(harness.document(), inner->serial) : nullptr;
    expect(outerContent && outerContent->contentMetadataComplete &&
        outerContent->hasElementChild && !outerContent->hasDirectTextChild,
        "descendant text does not propagate to the ancestor metadata");
    expect(innerContent && innerContent->contentMetadataComplete &&
        !innerContent->hasElementChild && innerContent->hasDirectTextChild,
        "nested text is owned by the inner represented Element");

    const std::string boundary = std::string(250u, ' ') + ":empty";
    execute(harness, "var selector256Accepted = document.querySelector(\"" +
        boundary + "\") === document.getElementById(\"empty\");",
        "256-byte selector boundary");
    expectBoolean(harness, "selector256Accepted", true,
        "256-byte selector bound remains accepted");
    const std::string overBoundary(251u, ' ');
    execute(harness, "var selector257Rejected = document.querySelector(\"" +
        overBoundary + ":empty\") === null;", "257-byte selector rejection");
    expectBoolean(harness, "selector257Rejected", true,
        "selectors beyond 256 bytes remain rejected");
}

void testFormsVoidAndIgnoredConstructs()
{
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    gxos::javascript::NavigatorScriptExecutionHarness harness;
    const std::string html = "<!doctype html><html><body>"
        "<input id=input value=seed><img id=image src=x><hr id=rule>"
        "<br id=unrepresented><textarea id=textarea>seed</textarea>"
        "<select id=select><option id=option>Label</option></select>"
        "<input id=check type=checkbox checked>"
        "<script>raw</script><style>raw</style></body></html>";
    expect(harness.loadHtml("file:///js58-forms.html", html, error),
        "form, void, and raw-text fixture parses");
    expect(harness.relayout(), "form, void, and raw-text fixture relayouts");
    expect(elementById(harness.document(), "unrepresented") == nullptr,
        "br is not fabricated as a structural Element");
    bool representedRawTextTag = false;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        representedRawTextTag = representedRawTextTag ||
            element.tagName == "script" || element.tagName == "style";
    }
    expect(!representedRawTextTag,
        "script and style are absent from the structural Element model");
    expect(harness.document().scriptSources.size() == 1u,
        "raw script capture remains on its existing parser path");

    execute(harness, R"JS(
var input = document.getElementById("input");
var image = document.getElementById("image");
var rule = document.getElementById("rule");
var textarea = document.getElementById("textarea");
var select = document.getElementById("select");
var option = document.getElementById("option");
var check = document.getElementById("check");
var voidSemantics = input.value === "seed" && input.matches(":empty") &&
  image.matches(":empty") && rule.matches(":empty") &&
  !textarea.matches(":empty") && !option.matches(":empty") &&
  document.querySelector("br:empty") === null &&
  document.querySelector("script:empty") === null &&
  document.querySelector("style:empty") === null;
input.value = "";
input.setAttribute("value", "changed");
input.setAttribute("data-test", "yes");
input.setAttribute("class", "hidden");
input.setAttribute("style", "display:none");
textarea.value = "";
select.value = "Label";
check.checked = false;
var formStateIndependent = input.matches(":empty") &&
  !textarea.matches(":empty") && !option.matches(":empty") &&
  check.matches(":empty");
)JS", "form state and void semantics");
    expectBoolean(harness, "voidSemantics", true,
        "represented void Elements match without attribute-created children");
    expectBoolean(harness, "formStateIndependent", true,
        "form values, selection, and checked state do not change emptiness");

    const std::uint64_t checkSerial = serialById(harness.document(), "check");
    expect(checkSerial != 0u && harness.focusElement(checkSerial, error),
        "focus transition succeeds for an empty checkbox");
    execute(harness, "var focusIndependent = "
        "document.getElementById(\"check\").matches(\":empty\");",
        "focus independence");
    expectBoolean(harness, "focusIndependent", true,
        "focus state does not affect :empty");
}

void testLongTextAndIncompleteAuthority()
{
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    gxos::javascript::NavigatorScriptExecutionHarness harness;
    std::string html = "<html><body><div id=long>";
    html.append(12000u, 'x');
    html += "</div><div id=empty></div></body></html>";
    expect(harness.loadHtml("file:///js58-long.html", html, error),
        "long-text document parses");
    expect(harness.relayout(), "long-text document relayouts");
    const gxos::web::HtmlElementRef* longElement =
        elementById(harness.document(), "long");
    const gxos::web::HtmlElementContentMetadata* longContent = longElement
        ? contentFor(harness.document(), longElement->serial) : nullptr;
    expect(longContent && longContent->hasDirectTextChild &&
        longContent->contentMetadataComplete,
        "long direct text remains complete after layout summary truncation");
    expect(longContent && longContent->visibleTextByteCount == 1024u,
        "legacy visible text summary remains bounded at 1,024 bytes");
    execute(harness, "var longTextNeverEmpty = "
        "!document.getElementById(\"long\").matches(\":empty\") && "
        "document.getElementById(\"empty\").matches(\":empty\");",
        "long-text emptiness");
    expectBoolean(harness, "longTextNeverEmpty", true,
        "layout truncation cannot produce a false empty match");

    gxos::web::HtmlElementRef* empty =
        elementById(harness.document(), "empty");
    gxos::web::HtmlElementContentMetadata* emptyContent = empty
        ? contentFor(harness.document(), empty->serial) : nullptr;
    expect(empty && emptyContent, "incomplete-authority candidate exists");
    if (!empty || !emptyContent) return;
    const bool priorComplete = emptyContent->contentMetadataComplete;
    emptyContent->contentMetadataComplete = false;
    execute(harness, "var incompleteFailsClosed = "
        "!document.getElementById(\"empty\").matches(\":empty\") && "
        "document.querySelector(\"#empty:empty\") === null;",
        "incomplete metadata fails closed");
    expectBoolean(harness, "incompleteFailsClosed", true,
        "incomplete child-content authority cannot match");
    emptyContent->contentMetadataComplete = priorComplete;
    const gxos::web::HtmlElementContentMetadata saved =
        harness.document().contentMetadata.back();
    harness.document().contentMetadata.pop_back();
    execute(harness, "var missingRecordFailsClosed = "
        "!document.getElementById(\"empty\").matches(\":empty\") && "
        "document.querySelector(\"#empty:empty\") === null;",
        "missing metadata record fails closed");
    expectBoolean(harness, "missingRecordFailsClosed", true,
        "metadata/structure mismatch cannot match");
    harness.document().contentMetadata.push_back(saved);
}

void testLiveCollectionsAndPurity(
    gxos::javascript::NavigatorScriptExecutionHarness& harness)
{
    const auto originalStructure = harness.document().structuralElements;
    const auto originalContent = harness.document().contentMetadata;
    const std::uint64_t originalMutationCount =
        harness.document().scriptMutationCount;
    const std::uint64_t originalLayoutRevision =
        harness.document().layoutRevision;
    const std::uint64_t originalGeneration = harness.hostAdapter().generation();
    execute(harness, R"JS(
var heldSlots = document.querySelectorAll(".slot:empty");
var heldStart = heldSlots.length === 4 &&
  heldSlots[0] === document.getElementById("empty");
)JS", "held empty collection");
    expectBoolean(harness, "heldStart", true,
        "held collection returns current ordered members");

    gxos::web::HtmlElementRef* empty =
        elementById(harness.document(), "empty");
    gxos::web::HtmlElementContentMetadata* emptyContent = empty
        ? contentFor(harness.document(), empty->serial) : nullptr;
    expect(empty && emptyContent, "live text reevaluation target exists");
    if (!empty || !emptyContent) return;
    emptyContent->hasDirectTextChild = true;
    execute(harness, R"JS(
var heldTextReevaluation = heldSlots.length === 3 &&
  document.querySelector("#empty:empty") === null;
)JS", "live direct-text reevaluation");
    expectBoolean(harness, "heldTextReevaluation", true,
        "held collection rereads direct-text authority without cached membership");
    emptyContent->hasDirectTextChild = false;

    const std::size_t originalElementCount =
        harness.document().structuralElements.size();
    const std::size_t originalContentCount =
        harness.document().contentMetadata.size();
    gxos::web::HtmlElementRef syntheticChild;
    syntheticChild.tagName = "span";
    syntheticChild.id = "synthetic-empty-child";
    syntheticChild.serial = static_cast<std::uint64_t>(originalElementCount + 1u);
    syntheticChild.parentSerial = empty->serial;
    syntheticChild.childIndex = 1u;
    syntheticChild.siblingCount = 1u;
    const gxos::web::HtmlElementContentMetadata syntheticContent = {
        syntheticChild.serial, 0u, 0u, false, false, false, false,
        false, false, false, true};
    harness.document().structuralElements.push_back(syntheticChild);
    harness.document().contentMetadata.push_back(syntheticContent);
    empty = elementById(harness.document(), "empty");
    emptyContent = empty ? contentFor(harness.document(), empty->serial) : nullptr;
    expect(empty && emptyContent, "synthetic child preserves metadata target");
    if (!empty || !emptyContent) return;
    empty->childCount = 1u;
    emptyContent->elementChildCount = 1u;
    emptyContent->hasElementChild = true;
    execute(harness, R"JS(
var heldChildReevaluation = heldSlots.length === 3 &&
  document.getElementById("empty").childElementCount === 1 &&
  !document.getElementById("empty").matches(":empty");
)JS", "live structural reevaluation");
    expectBoolean(harness, "heldChildReevaluation", true,
        "synthetic structural child removes held collection membership");
    harness.document().structuralElements.resize(originalElementCount);
    harness.document().contentMetadata.resize(originalContentCount);
    empty = elementById(harness.document(), "empty");
    emptyContent = empty ? contentFor(harness.document(), empty->serial) : nullptr;
    if (empty) empty->childCount = 0u;
    if (emptyContent) {
        emptyContent->elementChildCount = 0u;
        emptyContent->hasElementChild = false;
    }
    execute(harness, "var heldRestored = heldSlots.length === 4 && "
        "document.getElementById(\"empty\").matches(\":empty\");",
        "live collection restoration");
    expectBoolean(harness, "heldRestored", true,
        "removing test child restores collection membership");

    execute(harness, R"JS(
var emptyStressStable = true;
for (var i = 0; i < 1000; i = i + 1) {
  if (!document.getElementById("empty").matches(":empty") ||
      document.querySelector(":empty") === null) emptyStressStable = false;
}
var emptyHeldStressStable = true;
for (var j = 0; j < 320; j = j + 1) {
  if (heldSlots.length !== 4 || heldSlots[0] !== document.getElementById("empty"))
    emptyHeldStressStable = false;
}
)JS", "1,000 match/query and 320 live-read stress");
    expectBoolean(harness, "emptyStressStable", true,
        "repeated :empty matching and querySelector remain deterministic");
    expectBoolean(harness, "emptyHeldStressStable", true,
        "320 live collection rereads remain deterministic");
    expect(sameStructure(originalStructure,
            harness.document().structuralElements) &&
        sameContent(originalContent, harness.document().contentMetadata) &&
        originalMutationCount == harness.document().scriptMutationCount &&
        originalLayoutRevision == harness.document().layoutRevision &&
        originalGeneration == harness.hostAdapter().generation(),
        "matching does not mutate structure, content metadata, layout, or generation");
}

void testEventsAndGenerationSafety(
    gxos::javascript::NavigatorScriptExecutionHarness& harness)
{
    const std::uint64_t emptySerial = serialById(harness.document(), "empty");
    const std::uint64_t eventSerial = serialById(harness.document(), "event-empty");
    execute(harness, R"JS(
var oldEmpty = document.getElementById("empty");
var oldEmptyCollection = document.querySelectorAll(":empty");
var staleMatchAndClosest = false;
var eventEmptyMatch = false;
var eventMetadataPreserved = false;
var nestedEventPreserved = false;
document.getElementById("event-empty").addEventListener("click", function (event) {
  var target = event.target;
  var current = event.currentTarget;
  var phase = event.eventPhase;
  eventEmptyMatch = target.matches(":empty") && target.closest("input:empty") === target;
  document.getElementById("event-nested").click();
  nestedEventPreserved = event.target === target &&
    event.currentTarget === current && event.eventPhase === phase;
  eventMetadataPreserved = event.relatedTarget === null &&
    event.defaultPrevented === false && current === target && phase === 2;
});
)JS", "event setup with :empty");
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    expect(eventSerial != 0u && harness.dispatchClick(eventSerial, error) &&
        error == gxos::javascript::RuntimeErrorCode::None,
        "click event dispatch succeeds with :empty selector calls");
    expectBoolean(harness, "eventEmptyMatch", true,
        "event target matches :empty inside callback");
    expectBoolean(harness, "nestedEventPreserved", true,
        "nested dispatch preserves outer Event identity and phase");
    expectBoolean(harness, "eventMetadataPreserved", true,
        "selector matching preserves related/default Event metadata");
    expect(harness.hostAdapter().clickListenerCount() <= 64u,
        "bounded event listener registry remains at or below 64");

    expect(emptySerial != 0u && harness.invalidateDocumentGeneration(error),
        "document generation invalidation succeeds");
    execute(harness, R"JS(
var staleMatchAndClosest = !oldEmpty.matches(":empty") &&
  oldEmpty.closest(":empty") === null;
)JS", "stale Element :empty behavior");
    expectBoolean(harness, "staleMatchAndClosest", true,
        "stale matches is false and stale closest is null");
    const gxos::javascript::ScriptResult staleCollectionRead =
        harness.execute("var staleCollectionLength = oldEmptyCollection.length;");
    expect(!staleCollectionRead.succeeded() &&
        staleCollectionRead.runtimeError.code ==
            gxos::javascript::RuntimeErrorCode::StaleHostObject,
        "old-generation selector collection reads fail as stale host objects");

    gxos::javascript::NavigatorScriptExecutionHarness first;
    gxos::javascript::NavigatorScriptExecutionHarness second;
    expect(first.loadHtml("file:///serial-a.html",
            "<body><div id=target>x</div></body>", error),
        "serial-reuse nonempty document loads");
    expect(second.loadHtml("file:///serial-b.html",
            "<body><div id=target></div></body>", error),
        "serial-reuse empty document loads");
    expect(serialById(first.document(), "target") ==
        serialById(second.document(), "target"),
        "replacement fixtures reuse structural serial");
    execute(first, "var targetIsNotEmpty = "
        "!document.getElementById(\"target\").matches(\":empty\");",
        "first serial generation query");
    execute(second, "var reusedTargetIsEmpty = "
        "document.getElementById(\"target\").matches(\":empty\");",
        "second serial generation query");
    expectBoolean(first, "targetIsNotEmpty", true,
        "nonempty content is isolated to its document");
    expectBoolean(second, "reusedTargetIsEmpty", true,
        "reused serial reads only current document metadata");
}

void testStructuralCapacityAndParserRecovery()
{
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    gxos::javascript::NavigatorScriptExecutionHarness capacity;
    std::string html = "<body id=body>";
    for (std::size_t index = 0u; index < 1022u; ++index)
        html += "<span></span>";
    html += "<div id=parent><span id=rejected></span></div></body>";
    expect(capacity.loadHtml("file:///js58-capacity.html", html, error),
        "near-capacity document parses");
    gxos::web::HtmlElementRef* parent =
        elementById(capacity.document(), "parent");
    gxos::web::HtmlElementContentMetadata* parentContent = parent
        ? contentFor(capacity.document(), parent->serial) : nullptr;
    expect(capacity.document().structuralElements.size() == 1024u &&
        parent != nullptr && parentContent != nullptr &&
        !parentContent->contentMetadataComplete &&
        elementById(capacity.document(), "rejected") == nullptr,
        "capacity rejection marks the affected parent incomplete");
    execute(capacity, "var capacityParentFailsClosed = "
        "!document.getElementById(\"parent\").matches(\":empty\");",
        "capacity metadata fail-closed behavior");
    expectBoolean(capacity, "capacityParentFailsClosed", true,
        "incomplete metadata cannot create an empty match");

    gxos::javascript::NavigatorScriptExecutionHarness large;
    std::string largeHtml = "<body id=body>";
    for (std::size_t cycle = 0u; cycle < 160u; ++cycle) {
        largeHtml += "<div class=empty-slot></div>";
        largeHtml += "<div class=text-slot>x</div>";
        largeHtml += "<div class=parent-slot><span></span></div>";
        largeHtml += "<div class=comment-slot><!--c--></div>";
        largeHtml += "<div class=space-slot> </div>";
    }
    largeHtml += "</body>";
    expect(large.loadHtml("file:///js58-large.html", largeHtml, error),
        "large mixed-content document parses");
    expect(large.document().structuralElements.size() == 961u,
        "large stress fixture is near structural capacity");
    execute(large, R"JS(
var largeEmptyCollection = document.querySelectorAll(":empty");
var largeEmptyCount = largeEmptyCollection.length === 480;
var largeOrder = largeEmptyCollection[0] === document.querySelector(".empty-slot") &&
  largeEmptyCollection[1] === document.querySelector(".parent-slot span") &&
  largeEmptyCollection[2] === document.querySelector(".comment-slot") &&
  largeEmptyCollection[479] === document.querySelectorAll(".comment-slot")[159] &&
  document.querySelector(".text-slot").childElementCount === 0 &&
  !document.querySelector(".text-slot").matches(":empty") &&
  !document.querySelector(".parent-slot").matches(":empty") &&
  !document.querySelector(".space-slot").matches(":empty");
)JS", "near-capacity mixed :empty query");
    expectBoolean(large, "largeEmptyCount", true,
        "near-capacity empty, text, child, comment, and whitespace counts agree");
    expectBoolean(large, "largeOrder", true,
        "near-capacity :empty collection preserves structural document order");

    const gxos::web::WebDocument commentOnly = gxos::web::parseHtml(
        "file:///js58-comment.html", "<!doctype html><body><div id=x><!--c--></div>");
    const gxos::web::HtmlElementRef* comment = elementById(commentOnly, "x");
    bool commentMetadata = false;
    if (comment) {
        for (const gxos::web::HtmlElementContentMetadata& content :
                commentOnly.contentMetadata) {
            if (content.serial == comment->serial)
                commentMetadata = content.contentMetadataComplete &&
                    !content.hasDirectTextChild;
        }
    }
    expect(commentMetadata, "comment-only Element has complete no-text metadata");
    const gxos::web::WebDocument rawText = gxos::web::parseHtml(
        "file:///js58-raw.html",
        "<!doctype html><html><head><script>raw</script><style>raw</style>"
        "</head><body id=body></body></html>");
    const gxos::web::HtmlElementRef* body = elementById(rawText, "body");
    bool bodyTextAbsent = false;
    if (body) {
        for (const gxos::web::HtmlElementContentMetadata& content :
                rawText.contentMetadata) {
            if (content.serial == body->serial)
                bodyTextAbsent = !content.hasDirectTextChild;
        }
    }
    expect(bodyTextAbsent && rawText.scriptSources.size() == 1u &&
        !rawText.contentMetadata.empty(),
        "raw-text bodies do not fabricate structural Element content");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js58.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());

    gxos::javascript::RuntimeLimits runtimeLimits;
    runtimeLimits.maxHostOperations = 250000u;
    runtimeLimits.maxRuntimeStringValues = 20000u;
    runtimeLimits.maxTotalRuntimeStringBytes = 1024u * 1024u;
    gxos::javascript::NavigatorScriptExecutionHarness harness(runtimeLimits);
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    expect(!fixture.empty(), "JS58 hosted fixture is available");
    expect(harness.loadHtml("file:///javascript-js58.html", fixture, error),
        "JS58 fixture parses into the live document model");
    expect(error == gxos::javascript::RuntimeErrorCode::None && harness.relayout(),
        "JS58 fixture setup and relayout succeed");
    expect(harness.document().structuralElements.size() ==
        harness.document().contentMetadata.size(),
        "each represented Element has one parallel JS57 content record");
    expect(sizeof(gxos::web::HtmlElementContentMetadata) == 24u &&
        sizeof(gxos::web::HtmlElementRef) == 440u,
        "JS57 content record and structural Element sizes remain unchanged");

    testParserAndCoreSemantics(harness);
    testLiveCollectionsAndPurity(harness);
    testEventsAndGenerationSafety(harness);
    testFormsVoidAndIgnoredConstructs();
    testLongTextAndIncompleteAuthority();
    testStructuralCapacityAndParserRecovery();

    const std::size_t simpleSelector = sizeof(
        gxos::javascript::NavigatorScriptSimpleSelectorDescriptor);
    const std::size_t selectorDescriptor = sizeof(
        gxos::javascript::NavigatorScriptSelectorDescriptor);
    const std::size_t collectionRecord = (16u + selectorDescriptor + 7u) / 8u * 8u;
    expect(simpleSelector == 68u && selectorDescriptor == 812u &&
        collectionRecord == 832u && collectionRecord *
            gxos::javascript::kNavigatorScriptMaxSelectorCollections == 106496u,
        "JS59 adds a bounded inner core while preserving fixed collection storage");

    if (failures != 0) {
        std::cerr << failures << " JS58 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS58 checks: " << checks << "/"
        << checks << " passed; structuralElement="
        << sizeof(gxos::web::HtmlElementRef) << " bytes contentRecord="
        << sizeof(gxos::web::HtmlElementContentMetadata)
        << " bytes maxContentRecords=1024 maxContentBytes="
        << sizeof(gxos::web::HtmlElementContentMetadata) * 1024u
        << " bytes simpleSelector=" << simpleSelector
        << " bytes selectorDescriptor=" << selectorDescriptor
        << " bytes collectionRecord=" << collectionRecord
        << " bytes registry=" << collectionRecord *
            gxos::javascript::kNavigatorScriptMaxSelectorCollections
        << " bytes; metadataCache=none\n";
    return 0;
}
