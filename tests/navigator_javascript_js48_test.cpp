#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
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

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(!fixture.empty(), "fixture file is available");
    expect(harness.loadHtml("file:///javascript-js48.html", fixture, error),
        "JS48 fixture parses into the authoritative document");
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

void testGenericAttributesAndSpecializedReads()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var docs = document.querySelector("#docs");
var logo = document.querySelector("#logo");
var save = document.querySelector("#save");
var input = document.querySelector("#name");
var agree = document.querySelector("#agree");
var option = document.querySelector("#first-option");
var genericValues = panel.getAttribute("data-mode") === "Compact" &&
    panel.getAttribute("DATA-MODE") === "Compact" &&
    panel.getAttribute("aria-label") === "Main Panel" &&
    panel.getAttribute("title") === "Settings" &&
    panel.getAttribute("data-label") === "Tom & Jerry" &&
    panel.getAttribute("rel") === null;
var genericPresence = panel.hasAttribute("data-mode") &&
    panel.hasAttribute("aria-label") && panel.hasAttribute("title") &&
    !panel.hasAttribute("missing");
var hrefSrcReads = docs.getAttribute("href") === "/docs" &&
    docs.getAttribute("rel") === "next" &&
    docs.getAttribute("data-action") === "docs" &&
    logo.getAttribute("src") === "/logo.png" &&
    logo.getAttribute("alt") === "GuideXOS";
var customAndSyntax = panel.getAttribute("custom-flag") === "" &&
    panel.hasAttribute("custom-flag") &&
    panel.getAttribute("data-single") === "single-quoted" &&
    panel.getAttribute("data-unquoted") === "plain" &&
    panel.getAttribute("data-duplicate") === "first";
var emptyAndMissing = panel.getAttribute("data-empty") === "" &&
    panel.hasAttribute("data-empty") && panel.getAttribute("missing") === null &&
    !panel.hasAttribute("missing");
var specializedReads = save.getAttribute("id") === "save" &&
    save.getAttribute("class") === "Action Primary" &&
    save.getAttribute("type") === "button" &&
    save.getAttribute("value") === "button-seed" &&
    input.getAttribute("name") === "UserName" &&
    input.getAttribute("type") === "text" &&
    input.getAttribute("value") === "initial" &&
    agree.getAttribute("checked") === "" && agree.hasAttribute("checked") &&
    option.getAttribute("selected") === "" && option.hasAttribute("selected") &&
    save.getAttribute("disabled") === "" && save.hasAttribute("disabled");
var genericOnUnsupportedTag = document.querySelector("#owner").getAttribute(
    "data-form-kind") === "settings";
var selectorsStillAgree = save.matches(".Action.Primary") &&
    document.getElementById(save.getAttribute("id")) === save &&
    panel.getAttribute("class") === "panel active";
)JS");
    expect(result.succeeded(), "generic reads: JS48 script executes");
    expectBoolean(harness, "genericValues", true,
        "data, ARIA, title, case-insensitive names, and entity decoding work");
    expectBoolean(harness, "genericPresence", true,
        "generic presence and missing semantics are distinct");
    expectBoolean(harness, "hrefSrcReads", true,
        "link and image attributes are retained as read-only markup values");
    expectBoolean(harness, "customAndSyntax", true,
        "custom, valueless, single-quoted, unquoted, and duplicate attributes work");
    expectBoolean(harness, "emptyAndMissing", true,
        "present empty values remain distinct from missing attributes");
    expectBoolean(harness, "specializedReads", true,
        "JS47 specialized values and boolean attributes remain consistent");
    expectBoolean(harness, "genericOnUnsupportedTag", true,
        "generic attributes work on form containers");
    expectBoolean(harness, "selectorsStillAgree", true,
        "id and class attribute reads preserve selector identity");

    const gxos::web::HtmlElementRef* panel = elementById(harness, "panel");
    expect(panel != nullptr, "representation: panel structural record exists");
    if (panel != nullptr) {
        gxos::web::HtmlRetainedAttributeView retained;
        expect(gxos::web::findRetainedHtmlAttribute(harness.document(), *panel,
                "data-mode", 9u, retained),
            "representation: data-mode is in document-owned retained storage");
        expect(retained.value != nullptr && retained.valueLength == 7u &&
                std::string(retained.value, retained.valueLength) == "Compact",
            "representation: retained values survive parser completion");
        expect(panel->retainedAttributeCount <=
                gxos::web::kHtmlMaxRetainedAttributesPerElement,
            "representation: element span respects its fixed count cap");
    }
    expect(harness.document().retainedAttributeStorage.size() <=
            gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "representation: document byte pool stays within its cap");
    expect(harness.document().retainedAttributeRecordCount <=
            gxos::web::kHtmlMaxRetainedAttributeRecordsPerDocument,
        "representation: document record count stays within its cap");
}

void testDuplicatesBoundsAndParserContinuation()
{
    std::string name64 = "x" + std::string(63u, 'y');
    std::string name65 = "x" + std::string(64u, 'y');
    std::string value256(256u, 'V');
    std::string value257(257u, 'W');
    std::ostringstream html;
    html << "<html><body><div id='dups' id='ignored' class='first' "
        << "class='second' data-x='first' data-x='second'></div>";
    html << "<div id='bounds' " << name64 << "='ok' " << name65
        << "='drop' data-max='" << value256 << "' data-over='"
        << value257 << "'></div>";
    html << "<div id='per-element'";
    for (int index = 1; index <= 17; ++index) {
        html << " x" << (index < 10 ? "0" : "") << index << "='"
            << index << "'";
    }
    html << "></div></body></html>";

    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    expect(harness.loadHtml("file:///js48-bounds.html", html.str(), error),
        "bounds: parser accepts duplicate and boundary fixture");
    expect(error == RuntimeErrorCode::None, "bounds: fixture has no parser error");
    expect(harness.relayout(), "bounds: relayout succeeds");
    const ScriptResult result = harness.execute(
        "var d = document.getElementById('dups');"
        "var b = document.getElementById('bounds');"
        "var p = document.getElementById('per-element');"
        "var duplicateReads = d.getAttribute('id') === 'dups' &&"
        " d.getAttribute('class') === 'first' &&"
        " d.getAttribute('data-x') === 'first' &&"
        " d.matches('#dups.first') && document.getElementById('dups') === d;"
        "var nameBounds = b.getAttribute('" + name64 + "') === 'ok' &&"
        " b.hasAttribute('" + name64 + "') &&"
        " b.getAttribute('" + name65 + "') === null &&"
        " !b.hasAttribute('" + name65 + "');"
        "var valueBounds = b.getAttribute('data-max') === '" + value256 +
        "' && b.hasAttribute('data-max') &&"
        " b.getAttribute('data-over') === null && !b.hasAttribute('data-over');"
        "var perElementBounds = p.hasAttribute('x15') &&"
        " !p.hasAttribute('x16') && !p.hasAttribute('x17');");
    expect(result.succeeded(), "bounds: JS boundary checks execute");
    expectBoolean(harness, "duplicateReads", true,
        "first duplicate wins for generic, id, and class values");
    expectBoolean(harness, "nameBounds", true,
        "64-byte names are queryable and overlong names are dropped whole");
    expectBoolean(harness, "valueBounds", true,
        "256-byte values are retained and overlong values are dropped whole");
    expectBoolean(harness, "perElementBounds", true,
        "only the first 16 source attributes are eligible for retention");

    const gxos::web::WebDocument& document = harness.document();
    expect(document.retainedAttributePerElementDrops > 0u,
        "bounds: per-element overflow is reported");
    expect(document.retainedAttributeNameDrops > 0u,
        "bounds: overlong attribute name is reported");
    expect(document.retainedAttributeValueDrops > 0u,
        "bounds: overlong attribute value is reported");
    expect(document.retainedAttributeDuplicateDrops >= 3u,
        "duplicates: duplicate generic and specialized attributes are counted");
    expect(document.retainedAttributeStorage.size() <=
            gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "bounds: storage remains within the aggregate byte limit");
}

void testAggregateStorageOverflow()
{
    const std::string payload(gxos::web::kHtmlMaxRetainedAttributeValueBytes, 'P');
    std::ostringstream html;
    html << "<html><body>";
    for (int index = 0; index < 360; ++index) {
        html << "<div id='node" << index << "' data-payload='" << payload
            << "'></div>";
    }
    html << "</body></html>";
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    expect(harness.loadHtml("file:///js48-document-cap.html", html.str(), error),
        "aggregate: large document parses without failure");
    expect(error == RuntimeErrorCode::None, "aggregate: parse has no error");
    expect(harness.relayout(), "aggregate: relayout succeeds");
    expect(harness.document().retainedAttributeStorage.size() <=
            gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "aggregate: encoded storage never exceeds 65,535 bytes");
    expect(harness.document().retainedAttributeDocumentDrops > 0u,
        "aggregate: attributes are dropped after the document pool fills");
    expect(harness.document().structuralElements.size() > 350u,
        "aggregate: structural parsing continues after retention overflow");
    const ScriptResult result = harness.execute(
        "var late = document.getElementById('node359');"
        "var afterOverflow = late !== null && late.getAttribute('id') === 'node359';"
        "var droppedPayloadIsMissing = !late.hasAttribute('data-payload');");
    expect(result.succeeded(), "aggregate: late structural node remains queryable");
    expectBoolean(harness, "afterOverflow", true,
        "aggregate: specialized metadata after pool overflow remains intact");
    expectBoolean(harness, "droppedPayloadIsMissing", true,
        "aggregate: later generic attributes fail closed when storage is full");
}

void testRecordCountOverflow()
{
    std::ostringstream html;
    html << "<html><body>";
    for (int index = 0; index < 700; ++index) {
        html << "<div id='r" << index << "' x='1' y='2' z='3'></div>";
    }
    html << "</body></html>";
    const gxos::web::WebDocument document = gxos::web::parseHtml(
        "file:///js48-record-cap.html", html.str());
    expect(document.retainedAttributeRecordCount ==
            gxos::web::kHtmlMaxRetainedAttributeRecordsPerDocument,
        "records: fixed document attribute-record ceiling is reached");
    expect(document.retainedAttributeDocumentDrops > 0u,
        "records: attributes beyond the record ceiling are dropped");
    expect(document.structuralElements.size() > 690u,
        "records: parser continues to retain neighboring structural elements");
    expect(document.retainedAttributeStorage.size() <
            gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "records: record ceiling is independently exercised before byte ceiling");
    expect(document.retainedAttributeStorage.size() <=
            gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "records: record overflow does not exceed document byte budget");
}

void testCurrentDefaultsPurityAndCollections()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult mutations = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var input = document.querySelector("#name");
var agree = document.querySelector("#agree");
var option = document.querySelector("#first-option");
input.value = "changed";
agree.checked = false;
option.selected = false;
var currentAndMarkup = input.value === "changed" &&
    input.getAttribute("value") === "initial" &&
    !agree.checked && agree.hasAttribute("checked") &&
    agree.getAttribute("checked") === "" &&
    !option.selected && option.hasAttribute("selected") &&
    option.getAttribute("selected") === "" &&
    panel.getAttribute("data-mode") === "Compact";
var collectionAndTraversal = document.querySelectorAll("#panel")[0]
    .getAttribute("aria-label") === "Main Panel" &&
    document.getElementById("docs").getAttribute("href") === "/docs" &&
    panel.firstElementChild.getAttribute("data-action") === "docs" &&
    panel.children[2].getAttribute("data-action") === "save";
var active = document.querySelector("#name");
active.focus();
var activeAttribute = document.activeElement.getAttribute("name") === "UserName";
)JS");
    expect(mutations.succeeded(), "state: current value and selection changes execute");
    expectBoolean(harness, "currentAndMarkup", true,
        "current/default values do not overwrite retained markup attributes");
    expectBoolean(harness, "collectionAndTraversal", true,
        "query, collection, ID, child, and traversal Elements share retained reads");
    expectBoolean(harness, "activeAttribute", true,
        "activeElement exposes the same generic and specialized attributes");

    const gxos::web::WebDocument& beforeDocument = harness.document();
    const std::vector<uint8_t> poolBefore = beforeDocument.retainedAttributeStorage;
    const gxos::web::HtmlElementRef* panelBefore = elementById(harness, "panel");
    const std::string idBefore = panelBefore == nullptr ? std::string() : panelBefore->id;
    const std::string classBefore = panelBefore == nullptr
        ? std::string() : panelBefore->className;
    const std::uint64_t generationBefore = harness.runtime().hostGeneration();
    const std::uint64_t layoutBefore = harness.layoutRevision();
    const std::size_t mutationsBefore = harness.document().scriptMutationCount;
    const std::uint64_t focusBefore = harness.focusedElementSerial();
    const ScriptResult reads = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var first = panel.getAttribute("data-mode");
var second = document.querySelector("#docs").getAttribute("href");
var third = panel.getAttribute("aria-label");
var stringsStayStable = first === "Compact" && second === "/docs" &&
    third === "Main Panel" && first === panel.getAttribute("data-mode");
var readPurity = panel.getAttribute("data-empty") === "" &&
    panel.hasAttribute("data-mode");
)JS");
    expect(reads.succeeded(), "purity: simultaneous and repeated reads execute");
    expectBoolean(harness, "stringsStayStable", true,
        "returned JS strings remain stable after later attribute reads");
    expectBoolean(harness, "readPurity", true,
        "pure reads preserve empty values and presence");
    const gxos::web::HtmlElementRef* panelAfter = elementById(harness, "panel");
    expect(harness.document().retainedAttributeStorage == poolBefore,
        "purity: parser-owned attribute bytes do not change during reads");
    expect(panelAfter != nullptr && panelAfter->id == idBefore &&
            panelAfter->className == classBefore,
        "purity: ID and class selector metadata remain unchanged");
    expect(harness.runtime().hostGeneration() == generationBefore,
        "purity: host generation remains unchanged");
    expect(harness.layoutRevision() == layoutBefore,
        "purity: layout revision remains unchanged");
    expect(harness.document().scriptMutationCount == mutationsBefore,
        "purity: generic reads do not count as script mutations");
    expect(harness.focusedElementSerial() == focusBefore,
        "purity: active focus remains unchanged");
}

void testEventTargetsNestedDispatchAndMetadata()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t saveSerial = serialById(harness, "save");
    const ScriptResult listeners = harness.execute(R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var other = document.querySelector("#other");
var eventAction = false;
var nestedAction = false;
var eventMetadata = false;
save.addEventListener("click", function(event) {
    event.preventDefault();
});
panel.addEventListener("click", function(event) {
    if (event.target === save) {
        var target = event.target;
        var current = event.currentTarget;
        var phase = event.eventPhase;
        var related = event.relatedTarget;
        eventAction = event.target.getAttribute("data-action") === "save";
        other.click();
        eventMetadata = event.target === target &&
            event.currentTarget === current && current === panel &&
            event.eventPhase === phase && phase === 3 &&
            event.relatedTarget === related && related === null &&
            event.defaultPrevented === true;
    } else if (event.target === other) {
        nestedAction = event.target.getAttribute("data-action") === "other";
    }
});
)JS");
    expect(listeners.succeeded(), "events: listener setup executes");
    bool defaultPrevented = false;
    expect(harness.dispatchClick(saveSerial, error, &defaultPrevented),
        "events: authentic click dispatch succeeds");
    expect(error == RuntimeErrorCode::None && defaultPrevented,
        "events: default prevention survives generic reads");
    expectBoolean(harness, "eventAction", true,
        "event.target reads its generic data-action attribute");
    expectBoolean(harness, "nestedAction", true,
        "nested dispatch resolves the nested target attribute");
    expectBoolean(harness, "eventMetadata", true,
        "target/currentTarget/phase/relatedTarget/defaultPrevented survive nested reads");
}

void testGenerationSafetyAndSerialReuse()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldSerial = serialById(harness, "panel");
    expect(oldSerial != 0u, "stale: original generic Element exists");
    expect(harness.execute(
        "var oldPanel = document.querySelector('#panel');").succeeded(),
        "stale: capture canonical generic Element");
    expect(harness.invalidateDocumentGeneration(error),
        "stale: generation invalidation succeeds");
    expect(error == RuntimeErrorCode::None, "stale: invalidation has no error");
    const ScriptResult stale = harness.execute(R"JS(
var staleReads = oldPanel.getAttribute("data-mode") === null &&
    oldPanel.hasAttribute("data-mode") === false;
)JS");
    expect(stale.succeeded(), "stale: old handle fails closed without throwing");
    expectBoolean(harness, "staleReads", true,
        "old generic attributes cannot leak into a later generation");

    std::string replacement = fixture;
    const std::string oldId = "id=\"panel\"";
    const std::size_t position = replacement.find(oldId);
    expect(position != std::string::npos,
        "stale: replacement fixture contains the old panel ID");
    if (position != std::string::npos)
        replacement.replace(position, oldId.size(), "id=\"new-panel\"");
    harness.document() = gxos::web::parseHtml(
        "file:///js48-replacement.html", replacement);
    const std::uint64_t newSerial = serialById(harness, "new-panel");
    expect(newSerial == oldSerial,
        "stale: replacement deliberately reuses the old logical serial");
    expect(harness.runtime().installHostGlobal("newPanel", newSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "stale: replacement-generation Element is installed");
    const ScriptResult replacementReads = harness.execute(R"JS(
var replacementReads = oldPanel.getAttribute("data-mode") === null &&
    !oldPanel.hasAttribute("data-mode") &&
    newPanel.getAttribute("data-mode") === "Compact" &&
    newPanel.hasAttribute("data-mode");
)JS");
    expect(replacementReads.succeeded(),
        "stale: replacement reads execute with serial reuse");
    expectBoolean(harness, "replacementReads", true,
        "serial reuse cannot resolve replacement-document generic attributes");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js48.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testGenericAttributesAndSpecializedReads();
    testDuplicatesBoundsAndParserContinuation();
    testAggregateStorageOverflow();
    testRecordCountOverflow();
    testCurrentDefaultsPurityAndCollections();
    testEventTargetsNestedDispatchAndMetadata();
    testGenerationSafetyAndSerialReuse();
    if (failures != 0) {
        std::cerr << failures << " JS48 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS48 checks: " << checks
        << "/" << checks << " passed\n";
    return 0;
}
