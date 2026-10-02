#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

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
    expect(harness.loadHtml("file:///javascript-js49.html", fixture, error),
        "JS49 fixture parses into the authoritative document");
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

std::string quoted(const std::string& value)
{
    std::string result(1u, '"');
    for (char character : value) {
        if (character == '"' || character == '\\') result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

bool setAttributeDirect(NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial, const std::string& name, const std::string& value)
{
    const gxos::javascript::HostObjectReference receiver{serial,
        harness.hostAdapter().generation(),
        gxos::javascript::kNavigatorElementHostKind};
    const gxos::javascript::HostValue arguments[] = {
        gxos::javascript::HostValue::string(
            gxos::javascript::SourceView(name.data(), name.size())),
        gxos::javascript::HostValue::string(
            gxos::javascript::SourceView(value.data(), value.size())),
    };
    gxos::javascript::HostValue result;
    return harness.hostAdapter().call(&receiver,
        gxos::javascript::kNavigatorSetAttributeMethod, arguments, 2u,
        result).succeeded();
}

bool removeAttributeDirect(NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial, const std::string& name)
{
    const gxos::javascript::HostObjectReference receiver{serial,
        harness.hostAdapter().generation(),
        gxos::javascript::kNavigatorElementHostKind};
    const gxos::javascript::HostValue argument =
        gxos::javascript::HostValue::string(
            gxos::javascript::SourceView(name.data(), name.size()));
    gxos::javascript::HostValue result;
    return harness.hostAdapter().call(&receiver,
        gxos::javascript::kNavigatorRemoveAttributeMethod, &argument, 1u,
        result).succeeded();
}

bool retainedAttributeEquals(const NavigatorScriptExecutionHarness& harness,
    const gxos::web::HtmlElementRef& element, const std::string& name,
    const std::string& expected)
{
    gxos::web::HtmlRetainedAttributeView view;
    return gxos::web::findRetainedHtmlAttribute(harness.document(), element,
            name.data(), name.size(), view) && view.valueLength == expected.size() &&
        (expected.empty() || std::string(view.value, view.valueLength) == expected);
}

void testApiReflectionsAndDeferredAttributes()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t panelSerial = serialById(harness, "panel");
    const std::uint64_t saveSerial = serialById(harness, "save");
    const std::uint64_t generation = harness.hostAdapter().generation();
    const std::uint64_t panelParent = elementById(harness, "panel")->parentSerial;
    const std::uint16_t panelChildren =
        elementById(harness, "panel")->childCount;
    const std::uint16_t elementAttributesBefore =
        elementById(harness, "panel")->retainedAttributeCount;
    const std::uint16_t recordsBefore =
        harness.document().retainedAttributeRecordCount;
    const std::size_t bytesBefore =
        harness.document().retainedAttributeStorage.size();

    const std::string maxName = "n" + std::string(63u, 'a');
    const std::string overName = maxName + "a";
    const std::string value256(256u, 'v');
    const std::string value257(257u, 'w');
    std::ostringstream code;
    code << R"JS(
var panel = document.querySelector("#panel");
var save = document.querySelector("#save");
var input = document.querySelector("#name");
var agree = document.querySelector("#agree");
var option = document.querySelector("#first-option");
var docs = document.querySelector("#docs");
var logo = document.querySelector("#logo");
panel.setAttribute("api-probe", "ok");
var apiExists = panel.getAttribute("api-probe") === "ok";
panel.removeAttribute("api-probe");
panel.setAttribute("DATA-MODE", "compact");
panel.setAttribute("data-mode", "Compact");
var genericReplace = panel.getAttribute("data-mode") === "Compact" &&
    panel.getAttribute("DaTa-MoDe") === "Compact" &&
    panel.hasAttribute("DATA-MODE");
panel.setAttribute("data-empty", "");
panel.setAttribute("data-action", "save");
panel.setAttribute("aria-label", "Settings panel");
panel.setAttribute("custom-thing", "abc");
panel.setAttribute("custom-flag", "");
docs.setAttribute("href", "/new");
logo.setAttribute("src", "/new.png");
var genericKinds = panel.getAttribute("data-action") === "save" &&
    panel.getAttribute("aria-label") === "Settings panel" &&
    panel.getAttribute("custom-thing") === "abc" &&
    panel.getAttribute("custom-flag") === "" &&
    panel.hasAttribute("custom-flag") && docs.getAttribute("href") === "/new" &&
    logo.getAttribute("src") === "/new.png";
panel.removeAttribute("custom-thing");
panel.removeAttribute("absent");
var genericRemove = panel.getAttribute("custom-thing") === null &&
    !panel.hasAttribute("custom-thing") && !panel.hasAttribute("absent");
panel.setAttribute("data-mode");
panel.removeAttribute();
panel.setAttribute(123, "bad-name");
panel.removeAttribute(true);
panel.setAttribute("data-number", 7);
var invalidArgumentsAreNoops = panel.getAttribute("data-mode") === "Compact" &&
    !panel.hasAttribute("data-number") && !panel.hasAttribute("bad-name");
panel.setAttribute("data-mode", )JS" << quoted(value257) << R"JS();
var oversizedValueAtomic = panel.getAttribute("data-mode") === "Compact";
panel.setAttribute()JS" << quoted(maxName) << R"JS(, )JS" << quoted(value256)
        << R"JS();
var maxNameAndValue = panel.getAttribute()JS" << quoted(maxName)
        << R"JS().length === 256;
panel.setAttribute()JS" << quoted(overName) << R"JS(, "x");
var overlongNameNoop = !panel.hasAttribute()JS" << quoted(overName)
        << R"JS();
var classes = document.getElementsByClassName("active");
var activeInitially = classes.length === 1;
panel.setAttribute("class", "panel");
var activeCollectionLive = classes.length === 0;
panel.setAttribute("class", "panel active");
activeCollectionLive = activeCollectionLive && classes.length === 1 &&
    classes[0] === panel;
var beforeSave = document.getElementById("save");
beforeSave.setAttribute("ID", "save2");
var idSynchronized = beforeSave === save && save.matches("#save2") &&
    !save.matches("#save") && document.getElementById("save2") === save &&
    document.getElementById("save") === null;
var primaryCollection = document.querySelectorAll(".primary");
save.setAttribute("class", "action danger");
var classSynchronized = save.getAttribute("CLASS") === "action danger" &&
    save.matches(".action.danger") && !save.matches(".primary") &&
    primaryCollection.length === 0;
var dangerCollection = document.getElementsByClassName("danger");
var classCollectionSynchronized = dangerCollection.length === 1 &&
    dangerCollection[0] === save;
save.removeAttribute("class");
var classRemoved = !save.hasAttribute("class") &&
    save.getAttribute("class") === null && !save.matches(".action") &&
    dangerCollection.length === 0;
save.setAttribute("class", "action danger");
save.removeAttribute("id");
var idRemoved = !save.hasAttribute("id") && save.getAttribute("id") === null &&
    !save.matches("#save2") && document.getElementById("save2") === null;
save.setAttribute("id", "save2");
var identityAndSelectors = document.getElementById("save2") === beforeSave &&
    save.matches(".action.danger");
var originalInputName = input.getAttribute("name");
var originalInputType = input.getAttribute("type");
var originalInputValue = input.getAttribute("value");
var originalStyle = panel.getAttribute("style");
var originalChecked = agree.getAttribute("checked");
var originalSelected = option.getAttribute("selected");
input.value = "edited";
input.setAttribute("name", "newName");
input.setAttribute("type", "checkbox");
input.setAttribute("value", "new-default");
input.removeAttribute("value");
agree.removeAttribute("checked");
option.removeAttribute("selected");
save.setAttribute("disabled", "");
panel.setAttribute("style", "color: blue");
var reflectedMutationsDeferredCoherently =
    input.getAttribute("name") === originalInputName &&
    input.getAttribute("type") === originalInputType &&
    input.getAttribute("value") === originalInputValue &&
    input.value === "edited" && agree.getAttribute("checked") === originalChecked &&
    option.getAttribute("selected") === originalSelected &&
    !save.hasAttribute("disabled") && panel.getAttribute("style") === originalStyle;
)JS";

    const ScriptResult result = harness.execute(code.str());
    if (!result.succeeded())
        std::cerr << "JS49 API script status=" << static_cast<int>(result.status)
            << " parse=" << static_cast<int>(result.parserError.code)
            << " parse-offset=" << result.parserError.location.offset
            << " runtime=" << static_cast<int>(result.runtimeError.code)
            << " runtime-offset=" << result.runtimeError.location.offset
            << " steps=" << result.executionSteps << "\n";
    expect(result.succeeded(), "API/reflections: mutation script executes");
    expectBoolean(harness, "apiExists", true,
        "setAttribute and removeAttribute methods are exposed");
    expectBoolean(harness, "genericReplace", true,
        "runtime replacement is case-insensitive and preserves value case");
    expectBoolean(harness, "genericKinds", true,
        "data, ARIA, empty, custom, href, and src attributes mutate");
    expectBoolean(harness, "genericRemove", true,
        "generic removal and absent removal are safe");
    expectBoolean(harness, "invalidArgumentsAreNoops", true,
        "missing and non-string arguments fail closed");
    expectBoolean(harness, "oversizedValueAtomic", true,
        "257-byte replacement leaves the existing value unchanged");
    expectBoolean(harness, "maxNameAndValue", true,
        "64-byte names and 256-byte values are accepted");
    expectBoolean(harness, "overlongNameNoop", true,
        "65-byte names are rejected without insertion");
    expectBoolean(harness, "activeInitially", true,
        "class collection begins with the parser class projection");
    expectBoolean(harness, "activeCollectionLive", true,
        "held class collection reevaluates after class mutation");
    expectBoolean(harness, "idSynchronized", true,
        "id mutation updates getAttribute, selectors, and ID lookup");
    expectBoolean(harness, "classSynchronized", true,
        "class replacement updates compound selectors and live selector collections");
    expectBoolean(harness, "classCollectionSynchronized", true,
        "getElementsByClassName observes the new class immediately");
    expectBoolean(harness, "classRemoved", true,
        "class removal updates presence, matching, and a held collection");
    expectBoolean(harness, "idRemoved", true,
        "id removal updates presence, selectors, and ID lookup");
    expectBoolean(harness, "identityAndSelectors", true,
        "canonical Element identity survives id/class mutation");
    expectBoolean(harness, "reflectedMutationsDeferredCoherently", true,
        "style and form-reflected writes are rejected without split state");

    const gxos::web::HtmlElementRef* panel = elementById(harness, "panel");
    const gxos::web::HtmlElementRef* save = elementById(harness, "save2");
    expect(panel != nullptr && panel->serial == panelSerial &&
        panel->parentSerial == panelParent && panel->childCount == panelChildren,
        "purity: structural identity and tree relationships remain unchanged");
    expect(save != nullptr && save->serial == saveSerial,
        "purity: id/class mutation does not allocate a new Element serial");
    expect(harness.hostAdapter().generation() == generation,
        "purity: attribute mutation preserves document generation");
    expect(harness.document().retainedAttributeStorage.size() <=
        gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "accounting: aggregate retained bytes stay within the JS48 cap");
    expect(harness.document().retainedAttributeRecordCount <=
        gxos::web::kHtmlMaxRetainedAttributeRecordsPerDocument,
        "accounting: logical records stay within the JS48 cap");
    expect(panel != nullptr && panel->retainedAttributeCount <=
        gxos::web::kHtmlMaxRetainedAttributesPerElement,
        "accounting: panel stays within the 16-record Element cap");
    expect(recordsBefore != 0u && bytesBefore != 0u &&
        elementAttributesBefore != 0u,
        "accounting: parser-owned generic storage exists before mutation");
}

void testPerElementCapacityAndReuse()
{
    const std::string html = "<html><body><div id=\"slots\"></div>"
        "<span id=\"outside\" data-safe=\"yes\"></span></body></html>";
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js49-slots.html", html, error),
        "per-element cap: generated document loads");
    expect(harness.relayout(), "per-element cap: relayout succeeds");
    const ScriptResult result = harness.execute(R"JS(
var slots = document.getElementById("slots");
var outside = document.getElementById("outside");
for (var i = 0; i < 15; i++) slots.setAttribute("a" + i, "");
var countAtSixteen = slots.hasAttribute("a14") && !slots.hasAttribute("a15");
slots.setAttribute("id", "slots2");
var replaceAtSixteen = slots.getAttribute("id") === "slots2" &&
    document.getElementById("slots2") === slots;
slots.setAttribute("a15", "");
var seventeenthRejected = !slots.hasAttribute("a15");
slots.removeAttribute("a0");
slots.setAttribute("a15", "reused");
var removalReusesSlot = !slots.hasAttribute("a0") &&
    slots.getAttribute("a15") === "reused" &&
    outside.getAttribute("data-safe") === "yes";
)JS");
    expect(result.succeeded(), "per-element cap: script executes");
    expectBoolean(harness, "countAtSixteen", true,
        "the sixteenth logical attribute is accepted and seventeenth is rejected");
    expectBoolean(harness, "replaceAtSixteen", true,
        "replacement succeeds while the Element is at its attribute cap");
    expectBoolean(harness, "seventeenthRejected", true,
        "a new seventeenth attribute fails without partial insertion");
    expectBoolean(harness, "removalReusesSlot", true,
        "removal frees the logical slot for another attribute");
    const gxos::web::HtmlElementRef* slots = elementById(harness, "slots2");
    expect(slots != nullptr && slots->retainedAttributeCount == 16u,
        "per-element accounting remains exactly 16 after slot reuse");
}

void testDocumentRecordCapacity()
{
    std::ostringstream html;
    html << "<html><body>";
    for (int index = 0; index < 128; ++index)
        html << "<div id=\"d" << index << "\"></div>";
    html << "<span></span></body></html>";
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string markup = html.str();
    expect(harness.loadHtml("file:///js49-record-cap.html", markup, error),
        "document record cap: generated document loads");
    expect(harness.relayout(), "document record cap: relayout succeeds");
    bool callsSucceeded = true;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.tagName != "div") continue;
        for (int index = 0; index < 16; ++index)
            callsSucceeded = setAttributeDirect(harness, element.serial,
                "a" + std::to_string(index), "") && callsSucceeded;
    }
    expect(callsSucceeded,
        "document record cap: all bounded host calls return safely");
    std::uint64_t spareSerial = 0u;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.tagName == "span") spareSerial = element.serial;
    }
    expect(spareSerial != 0u, "document record cap: empty span exists");
    expect(setAttributeDirect(harness, spareSerial, "overflow", ""),
        "document record cap: overflow host call returns safely");
    const gxos::web::HtmlElementRef* spare = nullptr;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements)
        if (element.serial == spareSerial) spare = &element;
    expect(spare != nullptr && spare->retainedAttributeCount == 0u,
        "document record cap: rejected insertion leaves the target empty");
    expect(harness.document().retainedAttributeRecordCount ==
        gxos::web::kHtmlMaxRetainedAttributeRecordsPerDocument,
        "document record count reaches but does not exceed 2,048");
    expect(harness.document().retainedAttributeStorage.size() <=
        gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "record-cap population also stays within the byte cap");
}

void testAggregateByteCapacityAndAtomicReplacement()
{
    std::ostringstream html;
    html << "<html><body>";
    for (int index = 0; index < 17; ++index)
        html << "<div id=\"d" << index << "\"></div>";
    html << "<span id=\"anchor\" data-anchor=\"safe\"></span>"
        "</body></html>";
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string markup = html.str();
    expect(harness.loadHtml("file:///js49-byte-cap.html", markup, error),
        "byte cap: generated document loads");
    expect(harness.relayout(), "byte cap: relayout succeeds");

    const std::string wide(256u, 'w');
    bool callsSucceeded = true;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.tagName != "div") continue;
        for (int index = 0; index < 16; ++index)
            callsSucceeded = setAttributeDirect(harness, element.serial,
                "a" + std::to_string(index), wide) && callsSucceeded;
    }
    std::uint64_t anchorSerial = 0u;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == "anchor") anchorSerial = element.serial;
    }
    expect(anchorSerial != 0u, "byte cap: anchor Element exists");
    for (int index = 0; index < 4; ++index) {
        const std::string fillerName = "f" + std::string(62u, 'a') +
            static_cast<char>('a' + index);
        callsSucceeded = setAttributeDirect(harness, anchorSerial,
            fillerName, "") && callsSucceeded;
        gxos::web::HtmlElementRef* anchor = elementById(harness, "anchor");
        if (anchor == nullptr || !retainedAttributeEquals(harness, *anchor,
                fillerName, "")) break;
    }
    callsSucceeded = setAttributeDirect(harness, anchorSerial, "data-anchor",
        wide) && callsSucceeded;
    const std::string tooLongName = "n" + std::string(63u, 'q');
    callsSucceeded = setAttributeDirect(harness, anchorSerial, tooLongName,
        wide) && callsSucceeded;
    expect(callsSucceeded, "byte cap: host calls return safely at capacity");
    const gxos::web::HtmlElementRef* anchor = elementById(harness, "anchor");
    expect(anchor != nullptr && retainedAttributeEquals(harness, *anchor,
        "data-anchor", "safe"),
        "byte-cap replacement leaves the previous value intact");
    gxos::web::HtmlRetainedAttributeView tooLongView;
    const bool longNamePresent = anchor != nullptr &&
        gxos::web::findRetainedHtmlAttribute(harness.document(), *anchor,
            tooLongName.data(), tooLongName.size(), tooLongView);
    expect(anchor != nullptr && !longNamePresent,
        "byte-cap failure does not leave a partial maximum-size record");
    expect(harness.document().retainedAttributeStorage.size() <=
        gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "aggregate byte count never exceeds 65,535");
    expect(gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument -
        harness.document().retainedAttributeStorage.size() < 67u,
        "bounded 64-byte-name fillers reuse space up to fewer than 67 bytes remaining");
    expect(harness.document().retainedAttributeRecordCount <=
        gxos::web::kHtmlMaxRetainedAttributeRecordsPerDocument,
        "aggregate byte stress leaves record count bounded");
}

void testRepeatedReplacementAndRemoveReadd()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint16_t recordsBefore =
        harness.document().retainedAttributeRecordCount;
    const std::size_t storageBefore =
        harness.document().retainedAttributeStorage.size();
    const std::uint64_t panelSerial = serialById(harness, "panel");
    bool callsSucceeded = true;
    for (int index = 0; index < 1000; ++index)
        callsSucceeded = setAttributeDirect(harness, panelSerial, "data-mode",
            index % 2 == 0 ? "aa" : "b") && callsSucceeded;
    const gxos::web::HtmlElementRef* panel = elementById(harness, "panel");
    expect(panel != nullptr && retainedAttributeEquals(harness, *panel,
        "data-mode", "b"),
        "stress: 1,000 replacements leave the final value correct");
    for (int index = 0; index < 1000; ++index) {
        callsSucceeded = removeAttributeDirect(harness, panelSerial,
            "data-mode") && callsSucceeded;
        callsSucceeded = setAttributeDirect(harness, panelSerial, "data-mode",
            "cycle") && callsSucceeded;
        callsSucceeded = removeAttributeDirect(harness, panelSerial,
            "data-mode") && callsSucceeded;
    }
    callsSucceeded = setAttributeDirect(harness, panelSerial, "data-mode",
        "normal") && callsSucceeded;
    panel = elementById(harness, "panel");
    expect(callsSucceeded, "stress: repeated host calls return safely");
    expect(panel != nullptr && retainedAttributeEquals(harness, *panel,
        "data-mode", "normal"),
        "stress: 1,000 remove/re-add cycles leave the final value correct");
    expect(retainedAttributeEquals(harness, *elementById(harness, "save"),
        "data-action", "save"),
        "stress: neighboring Element attributes remain intact");
    expect(harness.document().retainedAttributeRecordCount == recordsBefore,
        "stress: repeated mutation does not grow the logical record count");
    expect(harness.document().retainedAttributeStorage.size() == storageBefore,
        "stress: final replacement returns to the original packed byte count");
    expect(harness.document().retainedAttributeStorage.size() <=
        gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument,
        "stress: repeated mutation cannot exhaust the aggregate arena");
}

void testEventMutationAndLiveCollections()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t saveSerial = serialById(harness, "save");
    const ScriptResult listeners = harness.execute(R"JS(
var save = document.querySelector("#save");
var other = document.querySelector("#other");
var eventData = false;
var eventClass = false;
var eventId = false;
var nestedData = false;
var metadataPreserved = false;
other.addEventListener("click", function(e) {
    e.target.setAttribute("data-nested", "yes");
    nestedData = e.target.getAttribute("data-nested") === "yes" &&
        e.currentTarget === other;
});
save.addEventListener("click", function(e) {
    var target = e.target;
    var current = e.currentTarget;
    var phase = e.eventPhase;
    var related = e.relatedTarget;
    e.preventDefault();
    e.target.setAttribute("data-clicked", "yes");
    e.target.setAttribute("class", "action clicked");
    eventData = e.target.getAttribute("data-clicked") === "yes";
    eventClass = e.target.matches(".clicked") &&
        document.getElementsByClassName("clicked")[0] === e.target;
    e.target.setAttribute("id", "clicked-save");
    eventId = e.target.matches("#clicked-save") &&
        document.getElementById("clicked-save") === e.target;
    other.click();
    metadataPreserved = e.target === target && e.currentTarget === current &&
        current === save && e.eventPhase === phase && e.relatedTarget === related &&
        related === null && e.defaultPrevented === true;
});
)JS");
    expect(listeners.succeeded(), "events: listener setup executes");
    bool defaultPrevented = false;
    expect(harness.dispatchClick(saveSerial, error, &defaultPrevented),
        "events: authentic click dispatch succeeds");
    expect(defaultPrevented, "events: default prevention remains set");
    expectBoolean(harness, "eventData", true,
        "event callback sees generic attribute mutation immediately");
    expectBoolean(harness, "eventClass", true,
        "same callback selector and class collection see class mutation");
    expectBoolean(harness, "eventId", true,
        "same callback ID selector and lookup see id mutation");
    expectBoolean(harness, "nestedData", true,
        "nested dispatch can mutate a different Element safely");
    expectBoolean(harness, "metadataPreserved", true,
        "nested mutation preserves target/currentTarget/phase/related/defaultPrevented");
    const ScriptResult afterDispatch = harness.execute(R"JS(
var saveAfterEvent = document.getElementById("clicked-save");
var eventMutationPersists = saveAfterEvent.getAttribute("data-clicked") === "yes" &&
    saveAfterEvent.matches(".clicked");
)JS");
    expect(afterDispatch.succeeded(),
        "events: post-dispatch attribute read executes");
    expectBoolean(harness, "eventMutationPersists", true,
        "mutated authoritative attributes remain visible after callback return");
    expect(harness.document().retainedAttributeRecordCount <=
        gxos::web::kHtmlMaxRetainedAttributeRecordsPerDocument,
        "event mutation keeps bounded storage accounting");
}

void testStaleReceiverAndSerialReuse()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldSerial = serialById(harness, "save");
    expect(harness.execute(
        "var oldSave = document.querySelector('#save');").succeeded(),
        "stale: capture the original Element handle");
    expect(harness.invalidateDocumentGeneration(error),
        "stale: document generation invalidates");
    const ScriptResult staleCalls = harness.execute(R"JS(
oldSave.setAttribute("data-stale", "bad");
oldSave.removeAttribute("id");
var staleMethodsAreNoops = oldSave.getAttribute("data-stale") === null;
)JS");
    expect(staleCalls.succeeded(), "stale: stale mutator calls do not throw");
    expectBoolean(harness, "staleMethodsAreNoops", true,
        "stale receiver mutation fails closed");

    harness.document() = gxos::web::parseHtml(
        "file:///js49-replacement.html", fixture);
    const std::uint64_t newSerial = serialById(harness, "save");
    expect(newSerial == oldSerial,
        "stale: replacement document deliberately reuses the Element serial");
    expect(harness.runtime().installHostGlobal("newSave", newSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "stale: replacement Element host is installed");
    const ScriptResult replacement = harness.execute(R"JS(
oldSave.setAttribute("data-stale", "bad");
oldSave.removeAttribute("id");
var replacementUnaffected = newSave.getAttribute("id") === "save" &&
    newSave.getAttribute("data-stale") === null;
)JS");
    expect(replacement.succeeded(), "stale: serial reuse calls execute safely");
    expectBoolean(harness, "replacementUnaffected", true,
        "old handle cannot mutate a replacement document reusing its serial");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js49.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testApiReflectionsAndDeferredAttributes();
    testPerElementCapacityAndReuse();
    testDocumentRecordCapacity();
    testAggregateByteCapacityAndAtomicReplacement();
    testRepeatedReplacementAndRemoveReadd();
    testEventMutationAndLiveCollections();
    testStaleReceiverAndSerialReuse();
    if (failures != 0) {
        std::cerr << failures << " JS49 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS49 checks: " << checks
        << "/" << checks << " passed\n";
    return 0;
}
