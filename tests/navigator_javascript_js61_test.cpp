#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <cstdint>
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
    if (value == nullptr) return;
    expect(value->isBoolean(), label + ": Boolean value");
    if (value->isBoolean()) {
        if (value->booleanValue() != expected)
            std::cerr << "INFO: " << label << " actual="
                << (value->booleanValue() ? "true" : "false")
                << " expected=" << (expected ? "true" : "false") << "\n";
        expect(value->booleanValue() == expected, label + ": expected value");
    }
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

std::uint64_t serialById(
    const gxos::web::WebDocument& document, const std::string& id)
{
    const gxos::web::HtmlElementRef* element = elementById(document, id);
    return element == nullptr ? 0u : element->serial;
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

bool execute(gxos::javascript::NavigatorScriptExecutionHarness& harness,
    const std::string& source, const std::string& label)
{
    const gxos::javascript::ScriptResult result = harness.execute(source);
    expect(result.succeeded(), label + " executes");
    if (!result.succeeded()) {
        std::cerr << "INFO: " << label << " error="
            << gxos::javascript::runtimeErrorCodeName(result.runtimeError.code)
            << " at=" << result.runtimeError.location.line << ":"
            << result.runtimeError.location.column << "\n";
    }
    return result.succeeded();
}

std::string quoteJs(const std::string& value)
{
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

void testParserAndSharedSelectorSemantics(
    gxos::javascript::NavigatorScriptExecutionHarness& harness)
{
    execute(harness, R"JS(
var root = document.getElementById("root");
var panel = document.getElementById("panel");
var primary = document.getElementById("primary");
var secondary = document.getElementById("secondary");
var primaryDiv = document.getElementById("primary-div");
var pending = document.getElementById("item-pending");
var disabledItem = document.getElementById("item-disabled");
var notBasic = pending.matches(":not(.disabled)") &&
  !disabledItem.matches(":not(.disabled)") &&
  root.matches(":not(div)") && !panel.matches(":not(DIV)") &&
  panel.matches(":NOT(:not(.active))") === false;
var notUniversal = document.querySelectorAll(":not(*)").length === 0 &&
  !root.matches(":not(*)") && !panel.matches("*:not(*)");
var notIdClass = root.matches(":not(#missing)") &&
  !panel.matches(":not(#panel)") && panel.matches(":not(#PANEL)") &&
  pending.matches(":not(.missing)") && pending.matches(":not(.ITEM)") &&
  !pending.matches(":not(.item)") &&
  pending.matches(":not(.item.disabled)");
var notCompound = !primary.matches(":not(button.primary)") &&
  secondary.matches(":not(button.primary)") &&
  primaryDiv.matches(":not(button.primary)") &&
  !primary.matches(":not(button#primary.action.primary[data-role=submit])");
var notAttribute = pending.matches(":not([hidden])") &&
  !pending.matches(":not([data-state=pending])") &&
  pending.matches(":not([data-state=ready])") &&
  !document.getElementById("empty").matches(":not([data-empty])") &&
  document.getElementById("empty").matches(":not([data-empty=nonempty])") &&
  document.getElementById("empty").matches(":not([data-role=save])") &&
  document.getElementById("quoted").matches(":not([data-label=other])") &&
  !document.getElementById("quoted").matches(":not([data-label=\")\"])") &&
  document.getElementById("quoted").matches(":not([data-note=\"other words\"])");
var notPseudo = root.matches(":not(:root)") === false &&
  panel.matches(":not(:root)") &&
  !document.getElementById("empty").matches(":not(:empty)") &&
  document.getElementById("text").matches(":not(:empty)") &&
  document.getElementById("space").matches(":not(:empty)") &&
  !document.getElementById("comment").matches(":not(:empty)") &&
  document.getElementById("child").matches(":not(:empty)") &&
  document.getElementById("unchecked").matches(":not(:checked)") &&
  !document.getElementById("checked").matches(":not(:checked)") &&
  document.getElementById("field-a").matches(":not(:focus)") &&
  !document.getElementById("disabled-input").matches(":not(:disabled)") &&
  !document.getElementById("inherited-disabled").matches(":not(:disabled)");
var notStructural = !document.getElementById("first-child").matches(":not(:first-child)") &&
  document.getElementById("middle-child").matches(":not(:first-child)") &&
  !document.getElementById("last-child").matches(":not(:last-child)") &&
  document.getElementById("middle-child").matches(":not(:last-child)") &&
  !document.getElementById("only-child").matches(":not(:only-child)") &&
  !document.getElementById("type-first").matches(":not(:first-of-type)") &&
  document.getElementById("type-last").matches(":not(:first-of-type)") &&
  !document.getElementById("type-last").matches(":not(:last-of-type)") &&
  document.getElementById("type-first").matches(":not(:only-of-type)") &&
  !document.getElementById("only-child").matches(":not(:only-of-type)");
var notOuterComposition = primary.matches("button:not(.disabled)") &&
  primary.matches("#primary:not(:disabled)") &&
  primary.matches(".action:not(.disabled)") &&
  primary.matches("[data-role=submit]:not([disabled])") &&
  primary.matches("button#primary.action[data-role=submit]:not(.disabled)") &&
  document.querySelector("button:not(.primary)") === secondary &&
  document.querySelector("button.primary:not(.disabled)") === primary;
var notRelations = document.querySelector(".panel:not(.disabled) > button:not(.primary)") === secondary &&
  document.querySelector(".panel:not(.missing) .item:not(.disabled)") ===
    document.getElementById("item-ready") &&
  document.querySelector(".label + input:not(:disabled)") ===
    document.getElementById("related-input") &&
  document.querySelector(".label:not(.disabled) + input:not(:disabled)") ===
    document.getElementById("related-input") &&
  document.querySelector(".marker ~ .item:not(.disabled)") ===
    document.getElementById("sibling-a") &&
  document.querySelector(".marker:not(.disabled) ~ .item:not(.disabled)") ===
    document.getElementById("sibling-a") &&
  document.querySelector(".panel:not(.disabled) > button:not(.disabled)") === primary;
var notListBasicCounts = document.querySelectorAll("button:not(.disabled), input:not(:disabled)").length === 9 &&
  document.querySelectorAll(".item:not(.disabled), span.item:not(:disabled)").length === 4;
var notListOverlapOrder = document.querySelectorAll(".item:not(.disabled), button:not(.disabled)")[0] ===
    document.getElementById("primary");
var notListReverseCount = document.querySelectorAll("button:not(.disabled), .item:not(.disabled)").length ===
    document.querySelectorAll(".item:not(.disabled), button:not(.disabled)").length;
var notListReverseOrder = document.querySelectorAll("button:not(.disabled), .item:not(.disabled)")[0] ===
    document.getElementById("primary") &&
  document.querySelectorAll("button:not(.disabled), .item:not(.disabled)")[2] ===
    document.getElementById("item-ready");
var notListOuterVsInner = document.querySelectorAll(":not(.a), :not(.b)").length > 0 &&
  document.querySelectorAll(":not(.a), :not(.b)")[0] === root &&
  document.querySelectorAll(":not(.absent), button").length > 0;
var notLists = notListBasicCounts && notListOverlapOrder &&
  notListReverseCount && notListReverseOrder && notListOuterVsInner;
var notScopeAndClosest = panel.querySelector(".item:not(.disabled)") ===
    document.getElementById("item-ready") &&
  panel.querySelector(".panel:not(.disabled)") === null &&
  secondary.closest(".panel:not(.disabled)") === panel &&
  secondary.closest("button:not(.primary)") === secondary &&
  secondary.closest(":not(:root)") === secondary;
var notMalformed = document.querySelector(":not()") === null &&
  document.querySelector(":not( )") === null &&
  document.querySelector(":not(.a,.b)") === null &&
  document.querySelector(":not(.a, .b)") === null &&
  document.querySelector(":not(.a > .b)") === null &&
  document.querySelector(":not(.a .b)") === null &&
  document.querySelector(":not(.a + .b)") === null &&
  document.querySelector(":not(.a ~ .b)") === null &&
  document.querySelector(":not(:not(.x))") === null &&
  document.querySelector(":not(:nth-child(2))") === null &&
  document.querySelector(":not(:nth-last-child(odd))") === null &&
  document.querySelector(":not(:nth-of-type(2))") === null &&
  document.querySelector(":not(.x") === null &&
  document.querySelector(":not(.x))") === null &&
  document.querySelector(":not((.x))") === null &&
  document.querySelector("button:not(.x):focus") === null &&
  document.querySelector("button:focus:not(.x)") === null &&
  document.querySelector(".panel > .item > span:not(.disabled)") === null &&
  document.querySelector(":not(.x)garbage") === null &&
  document.querySelector(":not(.x), button:not(.primary)") ===
    document.querySelector(":not(.x)");
var notWhitespaceAndQuotes = document.querySelector(":not( .absent )") === root &&
  document.getElementById("quoted").matches(":not([data-label=\"(\"])") &&
  !document.getElementById("quoted").matches(":not([data-label=\")\"])") &&
  document.getElementById("quoted").matches(":NOT([data-label=\"other\"])");
)JS", "shared parser, matcher, and query composition");

    const char* names[] = {
        "notBasic", "notUniversal", "notIdClass", "notCompound",
        "notAttribute", "notPseudo", "notStructural", "notOuterComposition",
        "notRelations", "notListBasicCounts", "notListOverlapOrder",
        "notListReverseCount", "notListReverseOrder", "notListOuterVsInner",
        "notLists",
        "notScopeAndClosest", "notMalformed",
        "notWhitespaceAndQuotes",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("JS61 parser/API: ") + name);

    const std::string base256 = ":not(.absent)";
    const std::string valid256 =
        std::string(256u - base256.size(), ' ') + base256;
    execute(harness, "var not256Accepted = document.querySelector(" +
        quoteJs(valid256) + ") === document.getElementById(\"root\");",
        "256-byte selector bound");
    expectBoolean(harness, "not256Accepted", true,
        "256-byte complete selector is accepted");
    const std::string invalid257 =
        std::string(257u - base256.size(), ' ') + base256;
    execute(harness, "var not257Rejected = document.querySelector(" +
        quoteJs(invalid257) + ") === null;", "257-byte selector bound");
    expectBoolean(harness, "not257Rejected", true,
        "257-byte selector is rejected");

    const std::vector<std::string> malformed = {
        ":not(", ":not()", ":not( )", ":not(.x", ":not(.x))",
        ":not((.x))", ":not(.x,.y)", ":not(.x > .y)",
        ":not(.x .y)", ":not(.x + .y)", ":not(.x ~ .y)",
        ":not(:not(.x))", ":not(:nth-child(2))", ":not(.x):focus",
        ":not(.x)garbage", ":not([data-label=\")\"]",
    };
    for (std::size_t index = 0u; index < malformed.size(); ++index) {
        const gxos::javascript::ScriptResult result = harness.execute(
            "document.querySelector(" + quoteJs(malformed[index]) + ");");
        expect(result.succeeded(), "malformed parser sweep completes " +
            std::to_string(index));
    }
}

void testIsSemanticsAndLiveState(
    gxos::javascript::NavigatorScriptExecutionHarness& harness)
{
    execute(harness, R"JS(
var isBasic = document.getElementById("primary").matches(":is(.primary)") &&
  !document.getElementById("secondary").matches(":IS(.primary)") &&
  document.getElementById("root").matches(":Is(html)") &&
  document.getElementById("root").matches(":is(*)") &&
  document.getElementById("primary").matches(":is( button.primary )") &&
  document.getElementById("primary").matches(":is(#primary)") &&
  document.getElementById("primary").matches(":is(.action.primary)") &&
  document.getElementById("primary").matches(
    ":is(button#primary.action.primary[data-role=submit])") &&
  document.getElementById("quoted").matches(":is([data-label=\")\"])");
var isEquivalence =
  document.getElementById("primary").matches("button") ===
    document.getElementById("primary").matches(":is(button)") &&
  document.getElementById("primary").matches("#primary") ===
    document.getElementById("primary").matches(":is(#primary)") &&
  document.getElementById("primary").matches(".primary") ===
    document.getElementById("primary").matches(":is(.primary)") &&
  document.getElementById("empty").matches("[data-empty]") ===
    document.getElementById("empty").matches(":is([data-empty])") &&
  document.getElementById("primary").matches("[data-role=submit]") ===
    document.getElementById("primary").matches(":is([data-role=submit])") &&
  document.getElementById("root").matches(":root") ===
    document.getElementById("root").matches(":is(:root)") &&
  document.getElementById("empty").matches(":empty") ===
    document.getElementById("empty").matches(":is(:empty)") &&
  document.getElementById("first-child").matches(":first-child") ===
    document.getElementById("first-child").matches(":is(:first-child)") &&
  document.getElementById("middle-child").matches(":last-child") ===
    document.getElementById("middle-child").matches(":is(:last-child)") &&
  document.getElementById("only-child").matches(":only-child") ===
    document.getElementById("only-child").matches(":is(:only-child)") &&
  document.getElementById("type-first").matches(":first-of-type") ===
    document.getElementById("type-first").matches(":is(:first-of-type)") &&
  document.getElementById("type-last").matches(":last-of-type") ===
    document.getElementById("type-last").matches(":is(:last-of-type)") &&
  document.getElementById("only-child").matches(":only-of-type") ===
    document.getElementById("only-child").matches(":is(:only-of-type)");
var isComplement =
  document.getElementById("primary").matches(":is(.primary)") !==
    document.getElementById("primary").matches(":not(.primary)") &&
  document.getElementById("secondary").matches(":is(.primary)") !==
    document.getElementById("secondary").matches(":not(.primary)") &&
  document.getElementById("primary").matches(":is([data-role=submit])") !==
    document.getElementById("primary").matches(":not([data-role=submit])") &&
  document.getElementById("root").matches(":is(:root)") !==
    document.getElementById("root").matches(":not(:root)") &&
  document.getElementById("empty").matches(":is(:empty)") !==
    document.getElementById("empty").matches(":not(:empty)") &&
  document.getElementById("middle-child").matches(":is(:first-child)") !==
    document.getElementById("middle-child").matches(":not(:first-child)");
var isStatePseudo =
  document.getElementById("checked").matches(":is(:checked)") &&
  !document.getElementById("unchecked").matches(":is(:checked)") &&
  document.getElementById("disabled-input").matches(":is(:disabled)") &&
  !document.getElementById("secondary").matches(":is(:disabled)") &&
  document.getElementById("field-a").matches(":is(:focus)") === false &&
  document.getElementById("only-child").matches(":is(:only-child)") &&
  document.getElementById("type-last").matches(":is(:last-of-type)");
var isOuterComposition =
  document.getElementById("primary").matches("button:is(.primary)") &&
  document.getElementById("primary").matches("#primary:is(button)") &&
  document.getElementById("primary").matches(".action:is(.primary)") &&
  document.getElementById("primary").matches("[data-role=submit]:is(button.primary)") &&
  document.getElementById("primary").matches(
    "button#primary.action[data-role=submit]:is(.primary)") &&
  document.querySelector(".panel:is(.active) > button:is(.primary)") ===
    document.getElementById("primary") &&
  document.querySelector(".marker:is(:empty) ~ .item:is(.item)") ===
    document.getElementById("sibling-a") &&
  document.querySelector(".panel:is(.active) .item:is(.item)") ===
    document.getElementById("item-ready") &&
  document.querySelector(".label:is(.label) + input:is(input)") ===
    document.getElementById("related-input");
var isQueriesAndLists =
  document.querySelector("button:is(.primary)") ===
    document.getElementById("primary") &&
  document.getElementById("panel").querySelector("button:is(.primary)") ===
    document.getElementById("primary") &&
  document.getElementById("panel").querySelector(".panel:is(.active)") === null &&
  document.getElementById("primary").closest("button:is(.primary)") ===
    document.getElementById("primary") &&
  document.getElementById("secondary").closest(".panel:is(.active)") ===
    document.getElementById("panel") &&
  document.querySelectorAll("button:is(.primary), #primary:is(button)").length === 1 &&
  document.querySelectorAll("button:is(.primary), button:is(.action)")[0] ===
    document.getElementById("primary") &&
  document.querySelectorAll("button:is(.primary), button:is(.action)")[1] ===
    document.getElementById("secondary");
var isMalformed = document.querySelector(":is()") === null &&
  document.querySelector(":is( )") === null &&
  document.querySelector(":is(.a,.b)") === null &&
  document.querySelector(":is(.a, .b)") === null &&
  document.querySelector(":is(.a > .b)") === null &&
  document.querySelector(":is(.a .b)") === null &&
  document.querySelector(":is(.a + .b)") === null &&
  document.querySelector(":is(.a ~ .b)") === null &&
  document.querySelector(":is(:is(.a))") === null &&
  document.querySelector(":is(:not(.a))") === null &&
  document.querySelector(":is(:nth-child(2))") === null &&
  document.querySelector(":is(:nth-last-child(2))") === null &&
  document.querySelector(":is(:nth-of-type(2))") === null &&
  document.querySelector(":is(:nth-last-of-type(2))") === null &&
  document.querySelector(":is(.a") === null &&
  document.querySelector(":is(.a))") === null &&
  document.querySelector(":is((.a))") === null &&
  document.querySelector("button:is(.a):focus") === null &&
  document.querySelector("button:focus:is(.a)") === null &&
  document.querySelector(".panel:is(.active) > .item:is(.item) > span") === null &&
  document.querySelector(":is(.a)garbage") === null &&
  document.querySelector(":is( .absent )") === null &&
  document.getElementById("quoted").matches(":is([data-label=\")\"])");
)JS", "positive logical pseudo, grammar, and composition");
    const char* names[] = {
        "isBasic", "isEquivalence", "isComplement", "isStatePseudo",
        "isOuterComposition", "isQueriesAndLists", "isMalformed",
    };
    for (const char* name : names) {
        expectBoolean(harness, name, true, std::string("JS61 is/API: ") + name);
    }

    execute(harness, R"JS(
var isSelectorSource = ".item:is([data-state=ready])";
var isAttributeCollection = document.querySelectorAll(isSelectorSource);
isSelectorSource = ".never-used";
var isAttributeCollectionBefore = isAttributeCollection.length === 1;
document.getElementById("item-pending").setAttribute("data-state", "ready");
var isAttributeMutationLive = isAttributeCollection.length === 2 &&
  document.getElementById("item-pending").matches(":is([data-state=ready])");
document.getElementById("item-pending").setAttribute("data-state", "pending");
var isAttributeMutationRestored = isAttributeCollection.length === 1;
var isCheckedCollection = document.querySelectorAll("input:is(:checked)");
var isCheckedBefore = isCheckedCollection.length === 1;
document.getElementById("unchecked").checked = true;
var isCheckedMutationLive = isCheckedCollection.length === 2 &&
  document.getElementById("unchecked").matches(":is(:checked)");
document.getElementById("unchecked").checked = false;
var isCheckedMutationRestored = isCheckedCollection.length === 1;
var isFocusCollection = document.querySelectorAll(".field:is(:focus)");
var isFocusBefore = isFocusCollection.length === 0;
document.getElementById("field-a").focus();
var isFocusAfterFirst = isFocusCollection.length === 1 &&
  isFocusCollection[0] === document.getElementById("field-a");
document.getElementById("field-b").focus();
var isFocusAfterTransfer = isFocusCollection.length === 1 &&
  isFocusCollection[0] === document.getElementById("field-b");
document.getElementById("field-b").blur();
var isFocusAfterBlur = isFocusCollection.length === 0;
var isContentCollection = document.querySelectorAll(".slot:is(:empty)");
var isContentBefore = isContentCollection.length === 2 &&
  document.getElementById("empty").matches(":is(:empty)");
var isMatchStress = true;
var isEmptyStress = true;
var isFocusStress = true;
for (var i = 0; i < 1000; i = i + 1) {
  if (!document.getElementById("primary").matches(":is(.primary)"))
    isMatchStress = false;
  if (!document.getElementById("empty").matches(":is(:empty)"))
    isEmptyStress = false;
  if (document.getElementById("field-a").matches(":is(:focus)"))
    isFocusStress = false;
}
var isComplementStress = true;
for (var j = 0; j < 300; j = j + 1) {
  if (document.getElementById("primary").matches(":is(.primary)") ===
      document.getElementById("primary").matches(":not(.primary)"))
    isComplementStress = false;
  if (document.getElementById("middle-child").matches(":is(:first-child)") ===
      document.getElementById("middle-child").matches(":not(:first-child)"))
    isComplementStress = false;
}
var isCollectionStress = true;
for (var k = 0; k < 320; k = k + 1) {
  if (isAttributeCollection.length !== 1 || isCheckedCollection.length !== 1 ||
      isFocusCollection.length !== 0) isCollectionStress = false;
}
)JS", "live logical pseudo and bounded stress");
    const char* liveNames[] = {
        "isAttributeCollectionBefore", "isAttributeMutationLive",
        "isAttributeMutationRestored", "isCheckedBefore",
        "isCheckedMutationLive", "isCheckedMutationRestored",
        "isFocusBefore", "isFocusAfterFirst", "isFocusAfterTransfer",
        "isFocusAfterBlur", "isContentBefore", "isMatchStress", "isEmptyStress",
        "isFocusStress", "isComplementStress", "isCollectionStress",
    };
    for (const char* name : liveNames)
        expectBoolean(harness, name, true, std::string("JS61 live/stress: ") + name);
}

void testWhereSemanticsAndLiveState(
    gxos::javascript::NavigatorScriptExecutionHarness& harness)
{
    execute(harness, R"JS(
var whereRoot = document.getElementById("root");
var wherePanel = document.getElementById("panel");
var wherePrimary = document.getElementById("primary");
var whereSecondary = document.getElementById("secondary");
var wherePending = document.getElementById("item-pending");
var whereBasic = wherePrimary.matches(":where(.primary)") &&
  !whereSecondary.matches(":WHERE(.primary)") &&
  whereRoot.matches(":Where(html)") && whereRoot.matches(":where(*)") &&
  wherePrimary.matches(":where(#primary)") &&
  wherePrimary.matches(":where(.action.primary)") &&
  wherePrimary.matches(":where(button#primary.action.primary[data-role=submit])") &&
  wherePending.matches(":where([data-state=pending])") &&
  wherePending.matches(":where([data-state])") &&
  document.getElementById("quoted").matches(":where([data-label=\")\"])");
function whereEquivalent(element, selector) {
  return element.matches(selector) === element.matches(":where(" + selector + ")") &&
    element.matches(selector) === element.matches(":is(" + selector + ")");
}
var whereStandaloneAndIsEquivalence =
  whereEquivalent(wherePrimary, "button") &&
  whereEquivalent(whereRoot, "#root") &&
  whereEquivalent(wherePrimary, ".primary") &&
  whereEquivalent(wherePending, "[data-state=pending]") &&
  whereEquivalent(document.getElementById("checked"), ":checked") &&
  whereEquivalent(document.getElementById("disabled-input"), ":disabled") &&
  whereEquivalent(document.getElementById("field-a"), ":focus") &&
  whereEquivalent(whereRoot, ":root") &&
  whereEquivalent(document.getElementById("empty"), ":empty") &&
  whereEquivalent(document.getElementById("first-child"), ":first-child") &&
  whereEquivalent(document.getElementById("last-child"), ":last-child") &&
  whereEquivalent(document.getElementById("type-first"), ":first-of-type") &&
  whereEquivalent(document.getElementById("type-last"), ":last-of-type") &&
  whereEquivalent(document.getElementById("only-child"), ":only-child") &&
  whereEquivalent(document.getElementById("only-child"), ":only-of-type");
var whereComplement =
  wherePrimary.matches(":where(.primary)") !== wherePrimary.matches(":not(.primary)") &&
  whereSecondary.matches(":where(.primary)") !== whereSecondary.matches(":not(.primary)") &&
  wherePending.matches(":where([data-state=pending])") !==
    wherePending.matches(":not([data-state=pending])") &&
  document.getElementById("field-a").matches(":where(:focus)") !==
    document.getElementById("field-a").matches(":not(:focus)") &&
  whereRoot.matches(":where(:root)") !== whereRoot.matches(":not(:root)") &&
  document.getElementById("empty").matches(":where(:empty)") !==
    document.getElementById("empty").matches(":not(:empty)");
var whereOuterComposition = wherePrimary.matches("button:where(.primary)") &&
  wherePrimary.matches("#primary:where(button)") &&
  wherePrimary.matches(".action:where(.primary)") &&
  wherePrimary.matches("[data-role=submit]:where(button.primary)") &&
  wherePrimary.matches("button#primary.action[data-role=submit]:where(.primary)") &&
  document.querySelector(".panel:where(.active) > button:where(.primary)") === wherePrimary &&
  document.querySelector(".panel:where(.active) .item:where([data-state=ready])") ===
    document.getElementById("item-ready") &&
  document.querySelector(".marker:where(:empty) + .item:where(.item)") ===
    document.getElementById("sibling-a") &&
  document.querySelector(".marker:where(:empty) ~ .item:where(.item)") ===
    document.getElementById("sibling-a") &&
  document.querySelector(".panel:where(.active) > button:where(.primary)") === wherePrimary;
var whereQueries = document.querySelector("button:where(.primary)") === wherePrimary &&
  document.querySelectorAll("*:where(*)").length === document.querySelectorAll("*").length &&
  document.querySelectorAll("button:where(.primary), #primary:where(button)").length === 1 &&
  wherePanel.querySelector(".item:where([data-state=ready])") ===
    document.getElementById("item-ready") &&
  wherePanel.querySelector(".panel:where(.active)") === null &&
  wherePrimary.closest("button:where(.primary)") === wherePrimary &&
  whereSecondary.closest(".panel:where(.active)") === wherePanel &&
  wherePrimary.matches(":where(.primary)");
var whereMalformed = document.querySelector(":where()") === null &&
  document.querySelector(":where( )") === null &&
  document.querySelector(":where(.a,.b)") === null &&
  document.querySelector(":where(.a, .b)") === null &&
  document.querySelector(":where(.a > .b)") === null &&
  document.querySelector(":where(.a .b)") === null &&
  document.querySelector(":where(.a + .b)") === null &&
  document.querySelector(":where(.a ~ .b)") === null &&
  document.querySelector(":where(:where(.x))") === null &&
  document.querySelector(":where(:is(.x))") === null &&
  document.querySelector(":where(:not(.x))") === null &&
  document.querySelector(":where(:nth-child(2))") === null &&
  document.querySelector(":where(:nth-last-child(2))") === null &&
  document.querySelector(":where(:nth-of-type(2))") === null &&
  document.querySelector(":where(:nth-last-of-type(2))") === null &&
  document.querySelector(":where(.a.b.c.d.e.f.g.h.i)") === null &&
  document.querySelector(":where(.x") === null &&
  document.querySelector(":where(.x))") === null &&
  document.querySelector(":where((.x))") === null &&
  document.querySelector("button:where(.x):focus") === null &&
  document.querySelector("button:focus:where(.x)") === null &&
  document.querySelector("button:focus:where(.x)") === null &&
  document.querySelector(":where(.x)garbage") === null;
var whereOuterList = document.querySelectorAll(
  "button:where(.primary), #primary:where(button), button:where(.primary)");
var whereOuterListWorks = whereOuterList.length === 1 &&
  whereOuterList[0] === wherePrimary;
)JS", "bounded where grammar and matching semantics");
    const char* names[] = {
        "whereBasic", "whereStandaloneAndIsEquivalence", "whereComplement",
        "whereOuterComposition", "whereQueries", "whereMalformed",
        "whereOuterListWorks",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("JS61 where/API: ") + name);

    execute(harness, R"JS(
var whereSource = ".item:where([data-state=ready])";
var whereAttributeCollection = document.querySelectorAll(whereSource);
whereSource = ".never-used";
var whereCollectionBefore = whereAttributeCollection.length === 1;
wherePending.setAttribute("data-state", "ready");
var whereAttributeMutationLive = whereAttributeCollection.length === 2 &&
  wherePending.matches(":where([data-state=ready])");
wherePending.setAttribute("data-state", "pending");
var whereAttributeMutationRestored = whereAttributeCollection.length === 1;
var whereCheckedCollection = document.querySelectorAll("input:where(:checked)");
var whereCheckedBefore = whereCheckedCollection.length === 1;
document.getElementById("unchecked").checked = true;
var whereCheckedMutationLive = whereCheckedCollection.length === 2 &&
  document.getElementById("unchecked").matches(":where(:checked)");
document.getElementById("unchecked").checked = false;
var whereCheckedMutationRestored = whereCheckedCollection.length === 1;
var whereFocusCollection = document.querySelectorAll(".field:where(:focus)");
var whereFocusBefore = whereFocusCollection.length === 0;
document.getElementById("field-a").focus();
var whereFocusAfterFirst = whereFocusCollection.length === 1 &&
  whereFocusCollection[0] === document.getElementById("field-a");
document.getElementById("field-b").focus();
var whereFocusAfterTransfer = whereFocusCollection.length === 1 &&
  whereFocusCollection[0] === document.getElementById("field-b");
document.getElementById("field-b").blur();
var whereFocusAfterBlur = whereFocusCollection.length === 0;
var whereEmptyCollection = document.querySelectorAll(".slot:where(:empty)");
var whereEmptyLive = whereEmptyCollection.length === 2 &&
  document.getElementById("empty").matches(":where(:empty)") &&
  !document.getElementById("text").matches(":where(:empty)");
var whereStress = true;
var whereIsStress = true;
var whereNotStress = true;
for (var i = 0; i < 1000; i = i + 1) {
  if (!wherePrimary.matches(":where(.primary)")) whereStress = false;
  if (wherePrimary.matches(":where(.primary)") !==
      wherePrimary.matches(":is(.primary)")) whereIsStress = false;
  if (wherePrimary.matches(":where(.primary)") ===
      wherePrimary.matches(":not(.primary)")) whereNotStress = false;
  if (!document.getElementById("empty").matches(":where(:empty)"))
    whereStress = false;
}
var whereCollectionStress = true;
for (var j = 0; j < 320; j = j + 1) {
  if (whereAttributeCollection.length !== 1 || whereCheckedCollection.length !== 1 ||
      whereFocusCollection.length !== 0 || whereEmptyCollection.length !== 2)
    whereCollectionStress = false;
}
)JS", "where live collections, mutation, and stress");
    const char* liveNames[] = {
        "whereCollectionBefore", "whereAttributeMutationLive",
        "whereAttributeMutationRestored", "whereCheckedBefore",
        "whereCheckedMutationLive", "whereCheckedMutationRestored",
        "whereFocusBefore", "whereFocusAfterFirst", "whereFocusAfterTransfer",
        "whereFocusAfterBlur", "whereEmptyLive", "whereStress",
        "whereIsStress", "whereNotStress", "whereCollectionStress",
    };
    for (const char* name : liveNames)
        expectBoolean(harness, name, true, std::string("JS61 where/live: ") + name);

    gxos::web::HtmlElementRef* whereEmptyElement = elementById(
        harness.document(), "empty");
    gxos::web::HtmlElementContentMetadata* whereEmptyContent = whereEmptyElement
        ? contentFor(harness.document(), whereEmptyElement->serial) : nullptr;
    expect(whereEmptyContent != nullptr,
        "empty Element has authoritative metadata for :where(:empty) liveness");
    bool whereEmptyMutationLive = false;
    if (whereEmptyContent != nullptr) {
        const bool savedDirectText = whereEmptyContent->hasDirectTextChild;
        whereEmptyContent->hasDirectTextChild = true;
        whereEmptyMutationLive = execute(harness,
            "var whereEmptyMetadataMutationLive = !document.getElementById(\"empty\").matches(\":where(:empty)\") && whereEmptyCollection.length === 1;",
            "live :where(:empty) metadata update") &&
            binding(harness, "whereEmptyMetadataMutationLive") != nullptr &&
            binding(harness, "whereEmptyMetadataMutationLive")->booleanValue();
        whereEmptyContent->hasDirectTextChild = savedDirectText;
    }
    expect(whereEmptyMutationLive,
        ":where(:empty) and held collections reread current content metadata");

    const gxos::web::HtmlElementRef* whereDisabledElement = elementById(
        harness.document(), "disabled-input");
    gxos::web::FormRuntimeControlState* whereDisabledState = nullptr;
    if (whereDisabledElement != nullptr) {
        const std::size_t count = std::min(harness.document().formRuntimeState.count,
            gxos::web::kFormRuntimeControlCap);
        for (std::size_t index = 0u; index < count; ++index) {
            gxos::web::FormRuntimeControlState& candidate =
                harness.document().formRuntimeState.controls[index];
            if (candidate.logicalSerial == whereDisabledElement->serial &&
                candidate.metadataValid) whereDisabledState = &candidate;
        }
    }
    expect(whereDisabledState != nullptr,
        "disabled control has current runtime state for :where() liveness");
    bool whereDisabledMutationLive = false;
    if (whereDisabledState != nullptr) {
        execute(harness,
            "var whereDisabledCollection = document.querySelectorAll(\"input:where(:disabled)\"); var whereDisabledBefore = whereDisabledCollection.length === 2;",
            "held where-disabled collection setup");
        const bool savedDisabled = whereDisabledState->disabled;
        whereDisabledState->disabled = false;
        whereDisabledMutationLive = execute(harness,
            "var whereDisabledChanged = !document.getElementById(\"disabled-input\").matches(\":where(:disabled)\") && whereDisabledCollection.length === 1;",
            "live :where(:disabled) runtime state") &&
            binding(harness, "whereDisabledChanged") != nullptr &&
            binding(harness, "whereDisabledChanged")->booleanValue();
        whereDisabledState->disabled = savedDisabled;
    }
    expect(whereDisabledMutationLive,
        ":where(:disabled) and held collections reread current form state");

    const std::vector<std::string> malformed = {
        ":where(", ":where()", ":where( )", ":where(.x", ":where(.x))",
        ":where((.x))", ":where(.x,.y)", ":where(.x > .y)",
        ":where(.x .y)", ":where(.x + .y)", ":where(.x ~ .y)",
        ":where(:where(.x))", ":where(:is(.x))", ":where(:not(.x))",
        ":where(:nth-child(2))", "button:where(.x):focus",
        ":where([data-label=\")\"]",
    };
    for (std::size_t index = 0u; index < malformed.size(); ++index) {
        const gxos::javascript::ScriptResult result = harness.execute(
            "document.querySelector(" + quoteJs(malformed[index]) + ");");
        expect(result.succeeded(), "JS61 bounded malformed sweep completes " +
            std::to_string(index));
    }

    const std::string whereBase = ":where(*)";
    const std::string where256 =
        std::string(256u - whereBase.size(), ' ') + whereBase;
    execute(harness, "var where256Accepted = document.querySelector(" +
        quoteJs(where256) + ") === document.getElementById(\"root\"); "
        "var where257Rejected = document.querySelector(" +
        quoteJs(where256 + " ") + ") === null;", "256-byte :where() selector bound");
    expectBoolean(harness, "where256Accepted", true,
        "256-byte :where() selector fits the unchanged total input bound");
    expectBoolean(harness, "where257Rejected", true,
        "257-byte :where() selector is rejected");
    const std::string overAttributeName = "data-" + std::string(60u, 'a');
    const std::string overAttributeValue(129u, 'x');
    std::string nineClassSelector = ":where(";
    for (std::size_t index = 0u; index < 9u; ++index)
        nineClassSelector += ".c" + std::to_string(index);
    nineClassSelector += ")";
    const std::string overNameSelector = ":where([" + overAttributeName + "])";
    const std::string overValueSelector = ":where([data-state=\"" +
        overAttributeValue + "\"])";
    execute(harness, "var whereInnerBoundsRejected = document.querySelector(" +
        quoteJs(nineClassSelector) + ") === null && document.querySelector(" +
        quoteJs(overNameSelector) + ") === null && document.querySelector(" +
        quoteJs(overValueSelector) + ") === null;",
        "bounded :where() inner limits");
    expectBoolean(harness, "whereInnerBoundsRejected", true,
        "nine classes, a 65-byte attribute name, and a 129-byte value remain rejected");
}

void testLiveCollectionsAndPurity(
    gxos::javascript::NavigatorScriptExecutionHarness& harness)
{
    const std::vector<gxos::web::HtmlElementRef> originalStructure =
        harness.document().structuralElements;
    const std::vector<gxos::web::HtmlElementContentMetadata> originalContent =
        harness.document().contentMetadata;
    const std::uint64_t originalGeneration = harness.hostAdapter().generation();
    execute(harness, R"JS(
var selectorSource = ".item:not([data-state=ready])";
var pendingItems = document.querySelectorAll(selectorSource);
selectorSource = ".never-used";
var wherePurityMatch = document.getElementById("primary").matches(":where(.primary)") &&
  document.getElementById("empty").matches(":where(:empty)");
var initialPendingItems = pendingItems.length === 4 &&
  pendingItems[0] === document.getElementById("item-pending") &&
  pendingItems[1] === document.getElementById("item-disabled");
document.getElementById("item-ready").setAttribute("data-state", "pending");
var afterFirstAttributeChange = pendingItems.length === 5 &&
  pendingItems[0] === document.getElementById("item-ready");
document.getElementById("item-pending").setAttribute("data-state", "ready");
var afterSecondAttributeChange = pendingItems.length === 4 &&
  pendingItems[0] === document.getElementById("item-ready");
var checkedElement = document.getElementById("checked");
var uncheckedCollection = document.querySelectorAll("input:not(:checked)");
var initialUncheckedCollection = uncheckedCollection.length >= 5 &&
  uncheckedCollection[0] === document.getElementById("unchecked");
checkedElement.checked = false;
var checkedMutationIsLive = uncheckedCollection.length >= 6 &&
  uncheckedCollection[0] === checkedElement &&
  checkedElement.matches(":not(:checked)") &&
  !checkedElement.matches(":not([checked])");
checkedElement.checked = true;
var checkedMutationRestored = uncheckedCollection.length >= 5 &&
  uncheckedCollection[0] === document.getElementById("unchecked");
var fields = document.querySelectorAll(".field:not(:focus)");
var focusCollectionInitial = fields.length === 2;
document.getElementById("field-a").focus();
var focusCollectionAfterFirst = fields.length === 1 &&
  fields[0] === document.getElementById("field-b");
document.getElementById("field-b").focus();
var focusCollectionAfterTransfer = fields.length === 1 &&
  fields[0] === document.getElementById("field-a");
document.getElementById("field-b").blur();
var focusCollectionAfterBlur = fields.length === 2;
var emptyCollection = document.querySelectorAll(".slot:not(:empty)");
var emptyInitialLive = emptyCollection.length === 3 &&
  emptyCollection[0] === document.getElementById("text") &&
  emptyCollection[1] === document.getElementById("space") &&
  emptyCollection[2] === document.getElementById("child");
)JS", "live selectors and original query text independence");
    const char* names[] = {
        "wherePurityMatch", "initialPendingItems", "afterFirstAttributeChange",
        "afterSecondAttributeChange", "initialUncheckedCollection",
        "checkedMutationIsLive", "checkedMutationRestored",
        "focusCollectionInitial", "focusCollectionAfterFirst",
        "focusCollectionAfterTransfer", "focusCollectionAfterBlur",
        "emptyInitialLive",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("JS61 live: ") + name);

    const gxos::web::HtmlElementRef* disabled = elementById(
        harness.document(), "disabled-input");
    expect(disabled != nullptr, "disabled input is present for runtime-state proof");
    bool disabledStateChanged = false;
    if (disabled != nullptr) {
        gxos::web::FormRuntimeControlState* state = nullptr;
        const std::size_t count = std::min(harness.document().formRuntimeState.count,
            gxos::web::kFormRuntimeControlCap);
        for (std::size_t index = 0u; index < count; ++index) {
            gxos::web::FormRuntimeControlState& candidate =
                harness.document().formRuntimeState.controls[index];
            if (candidate.logicalSerial == disabled->serial &&
                candidate.metadataValid) state = &candidate;
        }
        expect(state != nullptr, "disabled runtime state exists");
        if (state != nullptr) {
            execute(harness,
                "var enabledControls = document.querySelectorAll(\"input:not(:disabled)\"); var enabledControlsBefore = enabledControls.length;",
                "held not-disabled collection setup");
            const bool savedDisabled = state->disabled;
            state->disabled = false;
            disabledStateChanged = execute(harness,
                "var enabledByCurrentState = document.getElementById(\"disabled-input\").matches(\":not(:disabled)\"); var disabledCollectionIsLive = enabledControls.length === enabledControlsBefore + 1;",
                "live negated disabled read") &&
                binding(harness, "enabledByCurrentState") != nullptr &&
                binding(harness, "enabledByCurrentState")->booleanValue() &&
                binding(harness, "disabledCollectionIsLive") != nullptr &&
                binding(harness, "disabledCollectionIsLive")->booleanValue();
            state->disabled = savedDisabled;
            execute(harness,
                "var disabledCollectionRestored = enabledControls.length === enabledControlsBefore;",
                "disabled collection state restoration");
            expectBoolean(harness, "disabledCollectionRestored", true,
                "held not-disabled collection rereads restored form state");
        }
    }
    expect(disabledStateChanged,
        "not(:disabled) and held collections read current form runtime state");

    gxos::web::HtmlElementRef* empty = elementById(harness.document(), "empty");
    gxos::web::HtmlElementContentMetadata* emptyContent = empty
        ? contentFor(harness.document(), empty->serial) : nullptr;
    expect(emptyContent != nullptr,
        "empty Element has authoritative content metadata for mutation proof");
    bool emptyMutationLive = false;
    if (emptyContent != nullptr) {
        const bool savedDirectText = emptyContent->hasDirectTextChild;
        emptyContent->hasDirectTextChild = true;
        emptyMutationLive = execute(harness,
            "var emptyMetadataMutationLive = document.querySelectorAll(\".slot:not(:empty)\").length === 4;",
            "live negated empty reread") &&
            binding(harness, "emptyMetadataMutationLive") != nullptr &&
            binding(harness, "emptyMetadataMutationLive")->booleanValue();
        emptyContent->hasDirectTextChild = savedDirectText;
    }
    expect(emptyMutationLive,
        "not(:empty) rereads authoritative JS58 content metadata");

    execute(harness, R"JS(
var notStressElement = document.getElementById("item-ready");
var notStressMatch = true;
for (var i = 0; i < 1000; i = i + 1) {
  if (!notStressElement.matches(":not(.disabled)")) notStressMatch = false;
}
var emptyNotStress = document.getElementById("empty");
var notEmptyStress = true;
for (var j = 0; j < 1000; j = j + 1) {
  if (emptyNotStress.matches(":not(:empty)")) notEmptyStress = false;
}
var focusNotStress = document.getElementById("field-a");
var notFocusStress = true;
focusNotStress.blur();
for (var k = 0; k < 1000; k = k + 1) {
  if (!focusNotStress.matches(":not(:focus)")) notFocusStress = false;
}
var rereadStress = true;
for (var m = 0; m < 320; m = m + 1) {
  if (pendingItems.length !== 4 || fields.length !== 2 ||
      emptyCollection.length !== 3) rereadStress = false;
}
)JS", "bounded negation stress");
    const char* stressNames[] = {
        "notStressMatch", "notEmptyStress", "notFocusStress", "rereadStress",
    };
    for (const char* name : stressNames)
        expectBoolean(harness, name, true, std::string("JS61 stress: ") + name);

    expect(sameStructure(originalStructure, harness.document().structuralElements),
        "selector matching leaves structural Elements unchanged");
    expect(sameContent(originalContent, harness.document().contentMetadata),
        "selector matching leaves JS57/JS58 content metadata unchanged");
    expect(harness.hostAdapter().generation() == originalGeneration,
        "selector matching leaves document generation unchanged");
}

void testIncompleteMetadataAndEvents(
    gxos::javascript::NavigatorScriptExecutionHarness& harness)
{
    gxos::web::HtmlElementRef* text = elementById(harness.document(), "text");
    gxos::web::HtmlElementContentMetadata* metadata = text
        ? contentFor(harness.document(), text->serial) : nullptr;
    expect(metadata != nullptr,
        "text Element has metadata for invalid-evaluation negation proof");
    bool incompleteFailedClosed = false;
    if (metadata != nullptr) {
        const bool savedComplete = metadata->contentMetadataComplete;
        metadata->contentMetadataComplete = false;
        incompleteFailedClosed = execute(harness,
        "var incompleteNotEmptyFalse = !document.getElementById(\"text\").matches(\":not(:empty)\") && !document.getElementById(\"text\").matches(\":is(:empty)\") && !document.getElementById(\"text\").matches(\":where(:empty)\");",
            "incomplete empty negation") &&
            binding(harness, "incompleteNotEmptyFalse") != nullptr &&
            binding(harness, "incompleteNotEmptyFalse")->booleanValue();
        metadata->contentMetadataComplete = savedComplete;
    }
    expect(incompleteFailedClosed,
        "incomplete :empty evaluation cannot invert into a positive :not match");

    gxos::web::HtmlElementRef* middle = elementById(harness.document(),
        "middle-child");
    bool incompleteStructureFailedClosed = false;
    if (middle != nullptr) {
        const std::uint16_t savedChildIndex = middle->childIndex;
        middle->childIndex = static_cast<std::uint16_t>(savedChildIndex + 1u);
        incompleteStructureFailedClosed = execute(harness,
            "var incompleteNotStructuralFalse = !document.getElementById(\"middle-child\").matches(\":not(:first-child)\") && !document.getElementById(\"middle-child\").matches(\":is(:first-child)\") && !document.getElementById(\"middle-child\").matches(\":where(:first-child)\");",
            "incomplete structural negation") &&
            binding(harness, "incompleteNotStructuralFalse") != nullptr &&
            binding(harness, "incompleteNotStructuralFalse")->booleanValue();
        middle->childIndex = savedChildIndex;
    }
    expect(incompleteStructureFailedClosed,
        "inconsistent structural metadata cannot invert a pseudo non-match");

    gxos::web::FormRuntimeStateTable savedRuntime =
        harness.document().formRuntimeState;
    harness.document().formRuntimeState.focusValid = true;
    harness.document().formRuntimeState.focusedLogicalSerial = 0x7fffffffu;
    harness.document().formRuntimeState.focusedDocumentGeneration =
        harness.document().formRuntimeState.documentGeneration + 1u;
    const bool staleFocusFailedClosed = execute(harness,
        "var staleNotFocusFalse = !document.getElementById(\"field-a\").matches(\":not(:focus)\") && !document.getElementById(\"field-a\").matches(\":is(:focus)\") && !document.getElementById(\"field-a\").matches(\":where(:focus)\");",
        "stale focus negation") &&
        binding(harness, "staleNotFocusFalse") != nullptr &&
        binding(harness, "staleNotFocusFalse")->booleanValue();
    harness.document().formRuntimeState = savedRuntime;
    expect(staleFocusFailedClosed,
        "stale focus authority cannot invert into a positive :not match");

    const std::uint64_t savedRootSerial =
        harness.document().documentElement.serial;
    harness.document().documentElement.serial = 0u;
    const bool incompleteRootFailedClosed = execute(harness,
        "var incompleteNotRootFalse = !document.getElementById(\"panel\").matches(\":not(:root)\") && !document.getElementById(\"panel\").matches(\":is(:root)\") && !document.getElementById(\"panel\").matches(\":where(:root)\");",
        "incomplete root negation") &&
        binding(harness, "incompleteNotRootFalse") != nullptr &&
        binding(harness, "incompleteNotRootFalse")->booleanValue();
    harness.document().documentElement.serial = savedRootSerial;
    expect(incompleteRootFailedClosed,
        "missing document-root authority cannot invert into a positive :not match");

    const std::size_t validElementCount =
        harness.document().structuralElements.size();
    gxos::web::HtmlElementRef invalidSerial;
    invalidSerial.tagName = "div";
    harness.document().structuralElements.push_back(invalidSerial);
    const bool invalidSerialFailedClosed = execute(harness,
        "var invalidSerialNotCount = document.querySelectorAll(\":not(.missing)\").length === " +
            std::to_string(validElementCount) + "; " +
            "var invalidSerialIsCount = document.querySelectorAll(\":is(*)\").length === " +
            std::to_string(validElementCount) + "; " +
            "var invalidSerialWhereCount = document.querySelectorAll(\":where(*)\").length === " +
            std::to_string(validElementCount) + ";",
        "invalid serial negation") &&
        binding(harness, "invalidSerialNotCount") != nullptr &&
        binding(harness, "invalidSerialNotCount")->booleanValue() &&
        binding(harness, "invalidSerialIsCount") != nullptr &&
        binding(harness, "invalidSerialIsCount")->booleanValue() &&
        binding(harness, "invalidSerialWhereCount") != nullptr &&
        binding(harness, "invalidSerialWhereCount")->booleanValue();
    harness.document().structuralElements.pop_back();
    expect(invalidSerialFailedClosed,
        "serial-zero candidate is invalid before :where(), :is(), or :not() matching");

    gxos::web::HtmlElementRef* emptyElement = elementById(
        harness.document(), "empty");
    gxos::web::HtmlElementContentMetadata* emptyMetadata = emptyElement
        ? contentFor(harness.document(), emptyElement->serial) : nullptr;
    expect(emptyMetadata != nullptr,
        "empty Element has metadata for live :is(:empty) verification");
    bool emptyIsLive = false;
    if (emptyMetadata != nullptr) {
        execute(harness,
            "var heldIsEmpty = document.querySelectorAll(\".slot:is(:empty)\");",
            "held is-empty collection setup");
        const bool savedText = emptyMetadata->hasDirectTextChild;
        emptyMetadata->hasDirectTextChild = true;
        emptyIsLive = execute(harness,
            "var isEmptyMetadataChanged = !document.getElementById(\"empty\").matches(\":is(:empty)\") && heldIsEmpty.length === 1;",
            "live is-empty metadata update") &&
            binding(harness, "isEmptyMetadataChanged") != nullptr &&
            binding(harness, "isEmptyMetadataChanged")->booleanValue();
        emptyMetadata->hasDirectTextChild = savedText;
    }
    expect(emptyIsLive,
        ":is(:empty) and held collections reread current content metadata");

    const std::uint64_t eventSerial = serialById(harness.document(), "event-target");
    execute(harness, R"JS(
var negatedEventMatch = false;
var negatedEventClosest = false;
var negatedEventNested = false;
var negatedEventMetadata = false;
var positiveEventMatch = false;
var positiveEventClosest = false;
var whereEventMatch = false;
var whereEventClosest = false;
document.getElementById("event-target").addEventListener("click", function (event) {
  var target = event.target;
  var current = event.currentTarget;
  var phase = event.eventPhase;
  negatedEventMatch = target.matches(":not(:disabled)");
  positiveEventMatch = target.matches(":is(input)");
  whereEventMatch = target.matches(":where(input)");
  negatedEventClosest = target.closest(".panel:not(.disabled)") ===
    document.getElementById("panel");
  positiveEventClosest = target.closest(".panel:is(.active)") ===
    document.getElementById("panel");
  whereEventClosest = target.closest(".panel:where(.active)") ===
    document.getElementById("panel");
  document.getElementById("event-nested").click();
  negatedEventNested = event.target === target &&
    event.currentTarget === current && event.eventPhase === phase;
  negatedEventMetadata = event.relatedTarget === null &&
    event.defaultPrevented === false && phase === 2;
});
)JS", "event callbacks use bounded logical pseudos");
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    expect(eventSerial != 0u && harness.dispatchClick(eventSerial, error) &&
        error == gxos::javascript::RuntimeErrorCode::None,
        "nested event dispatch with logical pseudos succeeds");
    expectBoolean(harness, "negatedEventMatch", true,
        "event target matches :not(:disabled)");
    expectBoolean(harness, "negatedEventClosest", true,
        "event target closest evaluates an outer compound with :not()");
    expectBoolean(harness, "negatedEventNested", true,
        "nested dispatch preserves outer Event identity and phase");
    expectBoolean(harness, "negatedEventMetadata", true,
        "negation preserves relatedTarget and defaultPrevented metadata");
    expectBoolean(harness, "positiveEventMatch", true,
        "Event target applies the positive :is() matcher");
    expectBoolean(harness, "positiveEventClosest", true,
        "Event closest traverses a compound with :is()");
    expectBoolean(harness, "whereEventMatch", true,
        "Event target applies the :where() matcher");
    expectBoolean(harness, "whereEventClosest", true,
        "Event closest traverses a compound with :where()");
}

void initializeReplacementRuntime(gxos::web::WebDocument& document)
{
    document.formRuntimeState = gxos::web::FormRuntimeStateTable{};
    document.formRuntimeState.initialized = true;
    document.formRuntimeState.documentGeneration = 1u;
    for (const gxos::web::HtmlElementRef& element : document.structuralElements) {
        const gxos::web::FormControlMetadata& metadata = element.formControl;
        if (element.serial == 0u || !metadata.metadataComplete ||
            !metadata.supported || document.formRuntimeState.count >=
                gxos::web::kFormRuntimeControlCap) continue;
        gxos::web::FormRuntimeControlState& state =
            document.formRuntimeState.controls[
                document.formRuntimeState.count++];
        state.logicalSerial = element.serial;
        state.type = metadata.type;
        state.checked = metadata.checked;
        state.defaultChecked = metadata.checked;
        state.disabled = metadata.disabled;
        state.metadataValid = true;
    }
}

void testStaleAndSerialReuse()
{
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    gxos::javascript::NavigatorScriptExecutionHarness stale;
    expect(stale.loadHtml("file:///js61-stale.html", fixture, error),
        "stale-negation fixture loads");
    expect(stale.relayout(), "stale-negation fixture relayouts");
    const std::uint64_t reusedSerial = serialById(stale.document(), "item-pending");
    execute(stale, R"JS(
var oldNegatedElement = document.getElementById("item-pending");
var oldNegatedCollection = document.querySelectorAll(":not(.missing)");
var oldIsCollection = document.querySelectorAll(":is(*)");
var oldWhereCollection = document.querySelectorAll(":where(*)");
)JS", "capture old logical handle and collections");
    expect(reusedSerial != 0u && stale.invalidateDocumentGeneration(error),
        "generation invalidates before serial reuse");
    execute(stale, R"JS(
var staleNotUniversal = !oldNegatedElement.matches(":not(*)");
var staleNotMissing = !oldNegatedElement.matches(":not(.missing)");
var staleNotRoot = !oldNegatedElement.matches(":not(:root)");
var staleNotEmpty = !oldNegatedElement.matches(":not(:empty)");
var staleNotClosest = oldNegatedElement.closest(":not(.missing)") === null;
var staleIsUniversal = !oldNegatedElement.matches(":is(*)");
var staleIsMissing = !oldNegatedElement.matches(":is(.missing)");
var staleIsRoot = !oldNegatedElement.matches(":is(:root)");
var staleIsEmpty = !oldNegatedElement.matches(":is(:empty)");
var staleIsClosest = oldNegatedElement.closest(":is(*)") === null;
var staleWhereUniversal = !oldNegatedElement.matches(":where(*)");
var staleWhereMissing = !oldNegatedElement.matches(":where(.missing)");
var staleWhereRoot = !oldNegatedElement.matches(":where(:root)");
var staleWhereEmpty = !oldNegatedElement.matches(":where(:empty)");
var staleWhereClosest = oldNegatedElement.closest(":where(*)") === null;
)JS", "stale negated match calls");
    const char* staleNames[] = {
        "staleNotUniversal", "staleNotMissing", "staleNotRoot",
        "staleNotEmpty", "staleNotClosest", "staleIsUniversal",
        "staleIsMissing", "staleIsRoot", "staleIsEmpty", "staleIsClosest",
        "staleWhereUniversal", "staleWhereMissing", "staleWhereRoot",
        "staleWhereEmpty", "staleWhereClosest",
    };
    for (const char* name : staleNames)
        expectBoolean(stale, name, true,
            std::string("stale candidates fail closed: ") + name);
    const gxos::javascript::ScriptResult staleCollection = stale.execute(
        "var staleNegatedCollectionLength = oldNegatedCollection.length;");
    expect(!staleCollection.succeeded() && staleCollection.runtimeError.code ==
        gxos::javascript::RuntimeErrorCode::StaleHostObject,
        "stale negated collection rejects its old generation");
    const gxos::javascript::ScriptResult staleIsCollection = stale.execute(
        "var staleIsCollectionLength = oldIsCollection.length;");
    expect(!staleIsCollection.succeeded() && staleIsCollection.runtimeError.code ==
        gxos::javascript::RuntimeErrorCode::StaleHostObject,
        "stale positive-pseudo collection rejects its old generation");
    const gxos::javascript::ScriptResult staleWhereCollection = stale.execute(
        "var staleWhereCollectionLength = oldWhereCollection.length;");
    expect(!staleWhereCollection.succeeded() &&
        staleWhereCollection.runtimeError.code ==
            gxos::javascript::RuntimeErrorCode::StaleHostObject,
        "stale :where() collection rejects its old generation");

    stale.document() = gxos::web::parseHtml("file:///js61-replacement.html",
        fixture);
    gxos::web::HtmlElementRef* replacementElement = elementById(
        stale.document(), "item-pending");
    if (replacementElement != nullptr) replacementElement->className = "missing";
    expect(replacementElement != nullptr,
        "replacement fixture contains the serial-reuse target");
    expect(replacementElement != nullptr &&
        replacementElement->serial == reusedSerial,
        "replacement fixture reuses the exact old serial");
    initializeReplacementRuntime(stale.document());
    gxos::javascript::RuntimeErrorCode installError =
        gxos::javascript::RuntimeErrorCode::None;
    expect(stale.runtime().installHostGlobal("freshReusedElement", reusedSerial,
        gxos::javascript::kNavigatorElementHostKind, installError),
        "replacement document installs a handle at the reused serial");
    execute(stale, R"JS(
var replacementHasNoInnerMatch = freshReusedElement.matches(":not(.missing)") === false;
var oldCannotInvertReplacement = !oldNegatedElement.matches(":not(.missing)");
var replacementIsInnerMatch = freshReusedElement.matches(":is(.missing)") === true;
var oldIsCannotMatchReplacement = !oldNegatedElement.matches(":is(*)");
var replacementWhereInnerMatch = freshReusedElement.matches(":where(.missing)") === true;
var oldWhereCannotMatchReplacement = !oldNegatedElement.matches(":where(*)");
)JS", "serial reuse negation isolation");
    expectBoolean(stale, "replacementHasNoInnerMatch", true,
        "replacement serial evaluates only replacement class metadata");
    expectBoolean(stale, "oldCannotInvertReplacement", true,
        "old wrapper cannot turn reused serial into a true negated match");
    expectBoolean(stale, "replacementIsInnerMatch", true,
        "positive logical matching reads the replacement Element's class");
    expectBoolean(stale, "oldIsCannotMatchReplacement", true,
        "old wrapper cannot match a replacement serial through :is(*)");
    expectBoolean(stale, "replacementWhereInnerMatch", true,
        "replacement :where() reads the reused serial's current Element");
    expectBoolean(stale, "oldWhereCannotMatchReplacement", true,
        "old wrapper cannot match a replacement serial through :where(*)");
    const gxos::javascript::ScriptResult staleCollectionAfterReuse = stale.execute(
        "var staleNegatedCollectionAfterReuse = oldNegatedCollection.length;");
    expect(!staleCollectionAfterReuse.succeeded() &&
        staleCollectionAfterReuse.runtimeError.code ==
            gxos::javascript::RuntimeErrorCode::StaleHostObject,
        "old live collection stays stale after serial reuse");
    const gxos::javascript::ScriptResult staleIsCollectionAfterReuse = stale.execute(
        "var staleIsCollectionAfterReuse = oldIsCollection.length;");
    expect(!staleIsCollectionAfterReuse.succeeded() &&
        staleIsCollectionAfterReuse.runtimeError.code ==
            gxos::javascript::RuntimeErrorCode::StaleHostObject,
        "old :is() live collection stays stale after serial reuse");
    const gxos::javascript::ScriptResult staleWhereCollectionAfterReuse = stale.execute(
        "var staleWhereCollectionAfterReuse = oldWhereCollection.length;");
    expect(!staleWhereCollectionAfterReuse.succeeded() &&
        staleWhereCollectionAfterReuse.runtimeError.code ==
            gxos::javascript::RuntimeErrorCode::StaleHostObject,
        "old :where() live collection stays stale after serial reuse");
}

void testMemoryBoundsAndParserStress()
{
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    gxos::javascript::NavigatorScriptExecutionHarness harness;
    expect(harness.loadHtml("file:///js61-bounds.html", fixture, error),
        "bounds fixture loads");
    expect(harness.relayout(), "bounds fixture relayouts");
    const std::string maxAttributeName = "data-" + std::string(59u, 'a');
    const std::string maxAttributeValue(128u, 'x');
    std::string classSelector;
    std::string classValue;
    for (std::size_t index = 0u; index < 8u; ++index) {
        classSelector += ".c" + std::to_string(index);
        if (!classValue.empty()) classValue.push_back(' ');
        classValue += "c" + std::to_string(index);
    }
    const std::string innerCompound = classSelector + "[" + maxAttributeName +
        "=\"" + maxAttributeValue + "\"]";
    const std::string nearMaxBase = "div:not(" + innerCompound + ")";
    const std::string nearMaxSelector =
        std::string(256u - nearMaxBase.size(), ' ') + nearMaxBase;
    expect(nearMaxBase.size() < 256u && nearMaxSelector.size() == 256u,
        "near-maximum inner compound fits the existing selector bound");
    execute(harness, "var nearMaxTarget = document.getElementById(\"item-pending\"); "
        "nearMaxTarget.setAttribute(\"class\", " + quoteJs(classValue) + "); "
        "nearMaxTarget.setAttribute(" + quoteJs(maxAttributeName) + ", " +
            quoteJs(maxAttributeValue) + "); "
        "var nearMaxInnerAccepted = !nearMaxTarget.matches(" +
            quoteJs(nearMaxSelector) + ");",
        "near-maximum inner selector compound");
    expectBoolean(harness, "nearMaxInnerAccepted", true,
        "eight classes and a maximum-sized attribute value parse within 256 bytes");
    const std::string nearMaxIsBase = "div:is(" + innerCompound + ")";
    const std::string nearMaxIsSelector =
        std::string(256u - nearMaxIsBase.size(), ' ') + nearMaxIsBase;
    expect(nearMaxIsBase.size() < 256u && nearMaxIsSelector.size() == 256u,
        "near-maximum :is() compound fits the unchanged selector bound");
    execute(harness, "var nearMaxIsAccepted = nearMaxTarget.matches(" +
        quoteJs(nearMaxIsSelector) + ");", "near-maximum :is() compound");
    expectBoolean(harness, "nearMaxIsAccepted", true,
        ":is() stores eight classes and a maximum-sized attribute within 256 bytes");
    const std::string nearMaxWhereBase = "div:where(" + innerCompound + ")";
    const std::string nearMaxWhereSelector =
        std::string(256u - nearMaxWhereBase.size(), ' ') + nearMaxWhereBase;
    expect(nearMaxWhereBase.size() < 256u &&
        nearMaxWhereSelector.size() == 256u,
        "near-maximum :where() compound fits the unchanged selector bound");
    execute(harness, "var nearMaxWhereAccepted = nearMaxTarget.matches(" +
        quoteJs(nearMaxWhereSelector) + ");", "near-maximum :where() compound");
    expectBoolean(harness, "nearMaxWhereAccepted", true,
        ":where() stores eight classes and a maximum-sized attribute within 256 bytes");
    const std::string isBase = ":is(*)";
    const std::string is256 = std::string(256u - isBase.size(), ' ') + isBase;
    execute(harness, "var is256Accepted = document.querySelector(" +
        quoteJs(is256) + ") === document.getElementById(\"root\"); var is257Rejected = document.querySelector(" +
        quoteJs(is256 + " ") + ") === null;", "256-byte :is() selector bound");
    expectBoolean(harness, "is256Accepted", true,
        "256-byte :is() selector is accepted within the shared total bound");
    expectBoolean(harness, "is257Rejected", true,
        "257-byte :is() selector is rejected");
    const std::vector<std::string> malformed = {
        ":is(", ":is(.", ":is(.x", ":is(.x))", ":is((.x))",
        ":is(.x,.y)", ":is(.x > .y)", ":is(.x .y)", ":is(.x + .y)",
        ":is(.x ~ .y)", ":is(:is(.x))", ":is(:not(.x))",
        ":is(:nth-child(2))", ":is(.x):focus", ":is(.x)garbage",
        ":is([data-label=\")\"]",
        ":not(", ":not(.", ":not(.x", ":not(.x))", ":not((.x))",
        ":not(.x,.y)", ":not(.x > .y)", ":not(:not(.x))",
        ":not(:nth-child(2))", ":not([data-label=\")\"]",
        ":where(", ":where(.x", ":where(.x))", ":where((.x))",
        ":where(.x,.y)", ":where(.x > .y)", ":where(.x .y)",
        ":where(.x + .y)", ":where(.x ~ .y)", ":where(:where(.x))",
        ":where(:is(.x))", ":where(:not(.x))", ":where(:nth-child(2))",
        ":where(.x):focus",
    };
    for (std::size_t repeat = 0u; repeat < 40u; ++repeat) {
        for (std::size_t index = 0u; index < malformed.size(); ++index) {
            const gxos::javascript::ScriptResult result = harness.execute(
                "document.querySelector(" + quoteJs(malformed[index]) + ");");
            expect(result.succeeded(), "bounded malformed parser repeat " +
                std::to_string(repeat) + ":" + std::to_string(index));
        }
    }
    execute(harness, R"JS(
var longClasses = ".c0.c1.c2.c3.c4.c5.c6.c7";
var boundedCompound = document.querySelector("div:not(" + longClasses + ")");
var nearMaxNotReads = true;
for (var i = 0; i < 100; i = i + 1) {
  if (!document.getElementById("root").matches("html:not(.absent)"))
    nearMaxNotReads = false;
}
)JS", "fixed-capacity compound stress");
    expectBoolean(harness, "nearMaxNotReads", true,
        "repeated selector reads remain deterministic");

    const std::size_t core = sizeof(
        gxos::javascript::NavigatorScriptSimpleSelectorCoreDescriptor);
    const std::size_t simple = sizeof(
        gxos::javascript::NavigatorScriptSimpleSelectorDescriptor);
    const std::size_t selector = sizeof(
        gxos::javascript::NavigatorScriptSelectorDescriptor);
    const std::size_t collection = (16u + selector + 7u) / 8u * 8u;
    const std::size_t registry = collection *
        gxos::javascript::kNavigatorScriptMaxSelectorCollections;
    expect(core == 32u && simple == 68u && selector == 812u &&
        collection == 832u && registry == 106496u,
        "fixed inner core and registry sizes match the audited bound");
    std::cout << "JS61 memory: core=" << core << " bytes simpleSelector="
        << simple << " bytes selectorDescriptor=" << selector
        << " bytes collectionRecord=" << collection << " bytes registry="
        << registry << " bytes incrementalRegistryGrowthFromJS59=0 bytes"
        << " cumulativeGrowthFromJS58=32768 bytes\n";
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js61.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());

    gxos::javascript::RuntimeLimits runtimeLimits;
    runtimeLimits.maxExecutionSteps = 5000000u;
    runtimeLimits.maxHostOperations = 20000000u;
    runtimeLimits.maxRuntimeStringValues = 30000u;
    runtimeLimits.maxTotalRuntimeStringBytes = 2u * 1024u * 1024u;
    gxos::javascript::NavigatorScriptExecutionHarness harness(runtimeLimits);
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    expect(!fixture.empty(), "JS61 hosted fixture is available");
    expect(harness.loadHtml("file:///javascript-js61.html", fixture, error),
        "JS61 fixture parses into the current document model");
    expect(error == gxos::javascript::RuntimeErrorCode::None &&
        harness.relayout(), "JS61 fixture setup and relayout succeed");
    expect(harness.document().structuralElements.size() ==
        harness.document().contentMetadata.size(),
        "each represented Element retains one JS57/JS58 content record");
    expect(sizeof(gxos::web::HtmlElementContentMetadata) == 24u &&
        sizeof(gxos::web::HtmlElementRef) == 440u,
        "document Element and content record sizes remain unchanged");

    const std::size_t scriptBegin = fixture.find("<script>");
    const std::size_t scriptEnd = fixture.find("</script>", scriptBegin);
    expect(scriptBegin != std::string::npos && scriptEnd != std::string::npos,
        "hosted JS61 fixture contains a bounded script body");
    if (scriptBegin != std::string::npos && scriptEnd != std::string::npos)
        execute(harness, fixture.substr(scriptBegin + 8u,
            scriptEnd - (scriptBegin + 8u)), "hosted JS61 fixture script");
    execute(harness,
        "var hostedFixtureAssertions = js61IsBasic && js61IsEquivalence && "
            "js61IsQueries && js61IsRelations && js61IsLists && "
            "js61IsMalformed && js61IsLive && js61WhereBasic && "
            "js61WhereEquivalence && js61WhereComplement && js61WhereQueries && js61WhereRelations && "
            "js61WhereLists && js61WhereMalformed && js61WhereLive && "
            "js61Not && js61Attributes && js61Queries && "
            "js61Pseudo && js61Relations && js61Lists && js61LiveAttributes && "
            "js61Malformed;",
        "hosted JS61 fixture assertions");
    expectBoolean(harness, "hostedFixtureAssertions", true,
        "hosted positive semantics, equivalence, queries, relations, outer lists, malformed grammar, live attributes, and JS59 complement proofs");

    testParserAndSharedSelectorSemantics(harness);
    testIsSemanticsAndLiveState(harness);
    testWhereSemanticsAndLiveState(harness);
    testLiveCollectionsAndPurity(harness);
    testIncompleteMetadataAndEvents(harness);
    testStaleAndSerialReuse();
    testMemoryBoundsAndParserStress();

    if (failures != 0) {
        std::cerr << failures << " JS61 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS61 checks: " << checks << "/"
        << checks << " passed; maximumFunctionalDepth=1 parserRecursionBounded=1"
        << " matchTimeReparse=none\n";
    return 0;
}
