#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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

void expectNumber(const NavigatorScriptExecutionHarness& harness,
    const char* name, double expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr && value->isNumber() &&
        value->numberValue() == expected, label);
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
    expect(!fixture.empty(), "JS55 fixture is available");
    expect(harness.loadHtml("file:///javascript-js55.html", fixture, error),
        std::string(label) + ": fixture parses");
    expect(error == RuntimeErrorCode::None,
        std::string(label) + ": fixture load succeeds");
    expect(harness.relayout(), std::string(label) + ": relayout succeeds");
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

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0u;
}

void refreshSiblingMetadata(gxos::web::WebDocument& document,
    std::uint64_t parentSerial)
{
    gxos::web::HtmlElementRef* parent = nullptr;
    std::size_t count = 0u;
    for (gxos::web::HtmlElementRef& element : document.structuralElements) {
        if (element.serial == parentSerial) parent = &element;
        if (element.serial != 0u && element.parentSerial == parentSerial)
            ++count;
    }
    if (parent == nullptr || count >
            std::numeric_limits<std::uint16_t>::max()) return;

    parent->childCount = static_cast<std::uint16_t>(count);
    std::uint16_t childIndex = 0u;
    std::uint64_t previousSiblingSerial = 0u;
    for (gxos::web::HtmlElementRef& element : document.structuralElements) {
        if (element.serial == 0u || element.parentSerial != parentSerial)
            continue;
        element.childIndex = ++childIndex;
        element.siblingCount = static_cast<std::uint16_t>(count);
        element.previousSiblingSerial = previousSiblingSerial;
        previousSiblingSerial = element.serial;
    }
}

void testParserAndSelectorComposition()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "parser");
    const ScriptResult result = harness.execute(R"JS(
var first = document.getElementById("button-first");
var last = document.getElementById("button-last");
var only = document.getElementById("only-button");
var pseudoOnly = first.matches(":first-of-type") &&
  last.matches(":last-of-type") && only.matches(":only-of-type");
var caseNames = first.matches(":FIRST-OF-TYPE") &&
  first.matches(":First-Of-Type") && last.matches(":LaSt-Of-TyPe") &&
  only.matches(":OnLy-Of-TyPe");
var universal = first.matches("*:first-of-type") &&
  last.matches("*:last-of-type") && only.matches("*:only-of-type");
var tag = first.matches("BUTTON:first-of-type") &&
  last.matches("button:last-of-type") && only.matches("button:only-of-type");
var id = first.matches("#button-first:first-of-type") &&
  last.matches("#button-last:last-of-type") &&
  only.matches("#only-button:only-of-type");
var classes = first.matches(".target:first-of-type") &&
  last.matches(".item:last-of-type") && only.matches(".item:only-of-type");
var attributes = first.matches("[data-role=primary]:first-of-type") &&
  last.matches("[data-role=secondary]:last-of-type") &&
  only.matches("[data-role=solo]:only-of-type");
var fullCompound = first.matches(
  "button#button-first.item.target[data-role=primary]:first-of-type") &&
  last.matches("button#button-last.item[data-role=secondary]:last-of-type") &&
  only.matches("button#only-button.item[data-role=solo]:only-of-type");
var malformed = document.querySelector(":firstoftype") === null &&
  document.querySelector(":first-type") === null &&
  document.querySelector(":first--of-type") === null &&
  document.querySelector(":first-of-type-extra") === null &&
  document.querySelector(":lastoftype") === null &&
  document.querySelector(":only--of-type") === null;
var functional = document.querySelector(":first-of-type()") === null &&
  document.querySelector(":last-of-type()") === null &&
  document.querySelector(":only-of-type()") === null;
var unknownAndSecond = document.querySelector(":hover") === null &&
  document.querySelector("button:first-of-type:last-of-type") === null &&
  document.querySelector("button:first-child:first-of-type") === null &&
  !first.matches("button:first-of-type:last-of-type");
var canonical = first.matches(
  "button#button-first.item.target[data-role=primary]:first-of-type") &&
  !first.matches("button:first-of-type.target") &&
  !first.matches("button[data-role=primary]:first-of-type#button-first");
var wrongParts = !first.matches("input:first-of-type") &&
  !first.matches("button#button-last:first-of-type") &&
  !first.matches(".missing:first-of-type") &&
  !first.matches("[data-role=wrong]:first-of-type");
var selectorListBadIsAtomic =
  document.querySelectorAll(":first-of-type, button:first-of-type:last-of-type").length === 0;
)JS");
    expect(result.succeeded(), "pseudo parser and compound integration execute");
    reportScriptFailure(result, "parser and compound integration");
    const char* names[] = {"pseudoOnly", "caseNames", "universal", "tag", "id",
        "classes", "attributes", "fullCompound", "malformed", "functional",
        "unknownAndSecond", "canonical", "wrongParts", "selectorListBadIsAtomic"};
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("parser: ") + name);

    const std::string boundarySelector = "#only-button:only-of-type, #" +
        std::string(228u, 'x');
    expect(boundarySelector.size() == 256u,
        "selector-list boundary fixture is exactly 256 bytes");
    const ScriptResult boundary = harness.execute(
        "var selectorAt256 = document.querySelector(\"" +
        boundarySelector + "\") === only; var selectorAt257 = "
        "document.querySelector(\"" + boundarySelector +
        "x\") === null && document.querySelectorAll(\"" +
        boundarySelector + "x\").length === 0;");
    expect(boundary.succeeded(), "selector length boundary executes");
    reportScriptFailure(boundary, "selector length boundary");
    expectBoolean(harness, "selectorAt256", true,
        "the complete 256-byte selector-list input is accepted");
    expectBoolean(harness, "selectorAt257", true,
        "the 257-byte selector-list input is rejected as a whole");
}

void testNthParserMathAndComposition()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "nth parser");
    const ScriptResult result = harness.execute(R"JS(
var series = document.getElementById("series");
var n1 = document.getElementById("n1");
var n2 = document.getElementById("n2");
var n3 = document.getElementById("n3");
var n4 = document.getElementById("n4");
var n5 = document.getElementById("n5");
var firstButton = document.getElementById("button-first");
var lastButton = document.getElementById("button-last");
var basicIndices = n1.matches(":nth-child(1)") &&
  n2.matches(":nth-child(2)") && n5.matches(":nth-child(5)") &&
  !n1.matches(":nth-child(2)") && !n5.matches(":nth-child(4)");
var reverseIndices = n5.matches(":nth-last-child(1)") &&
  n4.matches(":nth-last-child(2)") && n1.matches(":nth-last-child(5)") &&
  !n5.matches(":nth-last-child(2)") &&
  series.querySelectorAll(":nth-last-child(odd)").length === 3 &&
  series.querySelectorAll(":nth-last-child(even)").length === 2;
var typeIndices = firstButton.matches(":nth-of-type(1)") &&
  lastButton.matches(":nth-of-type(2)") &&
  firstButton.matches(":nth-last-of-type(2)") &&
  lastButton.matches(":nth-last-of-type(1)") &&
  !firstButton.matches(":nth-of-type(2)");
var keywords = n1.matches(":nth-child(odd)") &&
  n3.matches(":nth-child(ODD)") && n5.matches(":nth-child(Odd)") &&
  n2.matches(":nth-child(even)") && n4.matches(":nth-child(EVEN)") &&
  n1.matches(":nth-child(n)") && n5.matches(":nth-child(N)") &&
  series.querySelectorAll(":nth-child(n)").length === 5 &&
  series.querySelectorAll(":nth-child(n+1)").length === 5;
var coefficientForms = n1.matches(":nth-child(+n)") &&
  n5.matches(":nth-child(n+1)") && n3.matches(":nth-child(n+3)") &&
  !n2.matches(":nth-child(n+3)") && n1.matches(":nth-child(n-2)") &&
  n5.matches(":nth-child(n-2)") && n2.matches(":nth-child(2n)") &&
  n4.matches(":nth-child(2n)") && n1.matches(":nth-child(2n+1)") &&
  n3.matches(":nth-child(2n-1)") && n5.matches(":nth-child(2n+1)");
var negativeCoefficients = n1.matches(":nth-child(-n+3)") &&
  n2.matches(":nth-child(-n+3)") && n3.matches(":nth-child(-n+3)") &&
  !n4.matches(":nth-child(-n+3)") && n1.matches(":nth-child(-2n+5)") &&
  n3.matches(":nth-child(-2n+5)") && n5.matches(":nth-child(-2n+5)") &&
  !n2.matches(":nth-child(-2n+5)") && !n4.matches(":nth-child(-2n+5)") &&
  !n1.matches(":nth-child(-n)");
var constantAndZero = n3.matches(":nth-child(3)") &&
  n3.matches(":nth-child(+3)") && !n1.matches(":nth-child(0)") &&
  !n1.matches(":nth-child(-1)") && n3.matches(":nth-child(0n+3)");
var acceptedWhitespace = n3.matches(":nth-child(2n + 1)") &&
  n3.matches(":nth-child(2n+ 1)") && n3.matches(":nth-child(2n +1)") &&
  n3.matches(":nth-child(  odd  )") && n4.matches(":nth-child( even )");
var compounds = n2.matches("div:nth-child(2)") &&
  n3.matches("#n3:nth-child(3)") && n4.matches(".entry:nth-child(4)") &&
  n5.matches("[id=n5]:nth-child(5)") &&
  n3.matches("div#n3.entry[id=n3]:nth-child(3)") &&
  !n3.matches("span:nth-child(3)") && !n3.matches("#n2:nth-child(3)") &&
  !n3.matches(".missing:nth-child(3)") &&
  !n3.matches("[id=n2]:nth-child(3)");
var typeFilters = document.getElementById("class-target").matches(
    ".target:nth-of-type(1)") &&
  document.getElementById("class-peer").matches(".peer:nth-of-type(2)") &&
  !document.getElementById("class-peer").matches(".peer:nth-of-type(1)") &&
  document.getElementById("attribute-target").matches(
    "[data-role=target]:nth-of-type(1)") &&
  !document.getElementById("attribute-peer").matches(
    "[data-role=peer]:nth-of-type(1)");
var hiddenAndForm = document.getElementById("hidden-button").matches(
    ":nth-child(1)") && document.getElementById("visible-button").matches(
    ":nth-child(3)") && document.getElementById("form-owned").matches(
    ":nth-of-type(1)");
var crossChecks = n1.matches(":first-child") ===
    n1.matches(":nth-child(1)") &&
  n5.matches(":last-child") === n5.matches(":nth-last-child(1)") &&
  firstButton.matches(":first-of-type") ===
    firstButton.matches(":nth-of-type(1)") &&
  lastButton.matches(":last-of-type") ===
    lastButton.matches(":nth-last-of-type(1)") &&
  document.getElementById("only-button").matches(":only-child") &&
  document.getElementById("only-button").matches(":nth-child(1)") &&
  document.getElementById("only-button").matches(":nth-last-child(1)") &&
  document.getElementById("only-type-button").matches(":only-of-type") &&
  document.getElementById("only-type-button").matches(":nth-of-type(1)") &&
  document.getElementById("only-type-button").matches(":nth-last-of-type(1)") &&
  firstButton.matches(":nth-child(2)") &&
  firstButton.matches(":nth-of-type(1)");
var seriesPosition = 0;
var bodyChildren = series.parentElement.children;
for (var i = 0; i < bodyChildren.length; i = i + 1) {
  if (bodyChildren[i] === series) seriesPosition = i + 1;
}
var apiComposition = document.querySelector("#n3:nth-child(3)") === n3 &&
  document.querySelector("#n2:nth-of-type(2)") === n2 &&
  series.querySelector("#n4:nth-last-child(2)") === n4 &&
  series.querySelectorAll("div:nth-child(odd)").length === 3 &&
  series.querySelectorAll("div:nth-child(even)").length === 2 &&
  series.querySelectorAll("div:nth-of-type(2n+1)")[2] === n5 &&
  n3.matches("div:nth-child(2n+1)") &&
  n3.closest("div:nth-child(3)") === n3 &&
  n3.closest("div:nth-child(" + seriesPosition + ")") === series;
var relationComposition = document.querySelector(
    "#n1:nth-child(1) + #n2:nth-child(2)") === n2 &&
  document.querySelector(
    "#n1:nth-child(2n+1) ~ #n5:nth-last-child(1)") === n5 &&
  document.querySelector("#panel:nth-of-type(1) > #button-first:nth-child(2)") === firstButton &&
  document.querySelector("#panel:nth-of-type(1) #button-last:nth-child(4)") === lastButton &&
  document.querySelector("#n1:nth-child(1) + #n2:nth-child(2) + #n3") === null &&
  document.querySelector("#n1:nth-child(1) ~ #n5:nth-child(1) ~ #n4") === null;
var listComposition = series.querySelectorAll(
    "div:nth-child(odd), #n1:nth-child(1)").length === 3 &&
  series.querySelectorAll(
    "div:nth-child(odd), #n1:nth-child(1)")[0] === n1 &&
  series.querySelectorAll(
    "div:nth-child(odd), #n1:nth-child(1)")[1] === n3 &&
  series.querySelectorAll(
    "div:nth-child(odd), #n1:nth-child(1)")[2] === n5 &&
  series.querySelector("#n5:nth-child(5), #n1:nth-child(1)") === n1 &&
  series.querySelectorAll(":nth-child(odd), :first-child").length >= 3 &&
  series.querySelectorAll("#n1:nth-child(0), #n5").length === 1 &&
  series.querySelectorAll(":nth-child(2n+foo), #n5").length === 0;
var parserFailClosed = document.querySelector(":nth-child") === null &&
  document.querySelector(":nth-child(") === null &&
  document.querySelector(":nth-child(1") === null &&
  document.querySelector(":nth-child(1))") === null &&
  document.querySelector(":nth-child((2))") === null &&
  document.querySelector(":nth-child()") === null &&
  document.querySelector(":nth-child( )") === null &&
  document.querySelector(":nth-child(foo)") === null &&
  document.querySelector(":nth-child(1.5)") === null &&
  document.querySelector(":nth-child(2.0n)") === null &&
  document.querySelector(":nth-child(1e3)") === null &&
  document.querySelector(":nth-child(0x10)") === null &&
  document.querySelector(":nth-child(2*n+1)") === null &&
  document.querySelector(":nth-child(n/2)") === null &&
  document.querySelector(":nth-child(1,2)") === null &&
  document.querySelector(":nth-child(++n)") === null &&
  document.querySelector(":nth-child(--n)") === null &&
  document.querySelector(":nth-child(+-n)") === null &&
  document.querySelector(":nth-child(2nn)") === null &&
  document.querySelector(":nth-child(n++1)") === null &&
  document.querySelector(":nth-child(n+-1)") === null &&
  document.querySelector(":nth-child(2n--1)") === null &&
  document.querySelector(":nth-child(+)") === null &&
  document.querySelector(":nth-child(-)") === null &&
  document.querySelector(":nth-child(2n+)") === null &&
  document.querySelector(":nth-child(2n-)") === null &&
  document.querySelector(":nth-child(oddx)") === null &&
  document.querySelector(":nth-child(+ 2n)") === null &&
  document.querySelector(":nth-child(2 n)") === null &&
  document.querySelector(":nth-child(n 1)") === null &&
  document.querySelector(":nth-child(2 of .entry)") === null &&
  document.querySelector("li:nth-child(1):focus") === null &&
  document.querySelector("#n5:nth-child(5) .entry") === null &&
  document.querySelector(":nth-child(1) > .entry > div") === null;
var root = document.querySelector("html");
var noParentFailsClosed = root.parentElement === null &&
  !root.matches(":nth-child(1)") && !root.matches(":nth-last-child(1)") &&
  !root.matches(":nth-of-type(1)") && !root.matches(":nth-last-of-type(1)");
)JS");
    expect(result.succeeded(), "nth parser, math, and composition execute");
    reportScriptFailure(result, "nth parser, math, and composition");
    const char* names[] = {"basicIndices", "reverseIndices", "typeIndices",
        "keywords", "coefficientForms", "negativeCoefficients",
        "constantAndZero", "acceptedWhitespace", "compounds", "typeFilters",
        "hiddenAndForm", "crossChecks", "apiComposition",
        "relationComposition", "listComposition", "parserFailClosed",
        "noParentFailsClosed"};
    for (const char* name : names)
        expectBoolean(harness, name, true,
            std::string("nth parser and matcher: ") + name);

    const std::string validNoMatch = ":nth-child(0), #n5";
    const std::string validNegativeNoMatch = ":nth-child(-1), #n5";
    const std::string validMaxBounds = ":nth-child(32767n+32767), #n5";
    const std::string validMinBounds =
        ":nth-child(-32767n-32767), #n5";
    const std::string overflowA = ":nth-child(32768n+1), #n5";
    const std::string overflowB = ":nth-child(1n+32768), #n5";
    const std::string overbound = ":nth-child(" + std::string(32u, ' ') +
        "n), #n5";
    const ScriptResult bounds = harness.execute(
        "var validZeroExpression = document.querySelectorAll(\"" +
        validNoMatch + "\").length === 1; "
        "var validNegativeExpression = document.querySelectorAll(\"" +
        validNegativeNoMatch + "\").length === 1; "
        "var maxNthBounds = document.querySelectorAll(\"" + validMaxBounds +
        "\").length === 1; "
        "var minNthBounds = document.querySelectorAll(\"" + validMinBounds +
        "\").length === 1; "
        "var overflowCoefficientRejected = document.querySelectorAll(\"" +
        overflowA + "\").length === 0; "
        "var overflowOffsetRejected = document.querySelectorAll(\"" +
        overflowB + "\").length === 0; "
        "var overboundNthRejected = document.querySelectorAll(\"" +
        overbound + "\").length === 0;");
    expect(bounds.succeeded(), "nth arithmetic bounds execute safely");
    reportScriptFailure(bounds, "nth arithmetic bounds");
    for (const char* name : {"validZeroExpression", "validNegativeExpression",
            "maxNthBounds", "minNthBounds", "overflowCoefficientRejected",
            "overflowOffsetRejected", "overboundNthRejected"})
        expectBoolean(harness, name, true,
            std::string("nth bounded parser: ") + name);

    const std::string prefix = "#n1:nth-child(1), #";
    const std::string selectorAt256 = prefix +
        std::string(gxos::javascript::kNavigatorScriptMaxSelectorLength -
            prefix.size(), 'x');
    expect(selectorAt256.size() ==
        gxos::javascript::kNavigatorScriptMaxSelectorLength,
        "nth selector-list boundary is exactly 256 bytes");
    const ScriptResult selectorBounds = harness.execute(
        "var nthSelectorAt256 = document.querySelector(\"" + selectorAt256 +
        "\") === n1; var nthSelectorAt257 = document.querySelector(\"" +
        selectorAt256 + "x\") === null;");
    expect(selectorBounds.succeeded(), "nth selector length boundary executes");
    reportScriptFailure(selectorBounds, "nth selector length boundary");
    expectBoolean(harness, "nthSelectorAt256", true,
        "the 256-byte selector containing an nth pseudo is accepted");
    expectBoolean(harness, "nthSelectorAt257", true,
        "the 257-byte selector containing an nth pseudo is rejected");
}

void testTypeRulesRelationsQueriesAndLists()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "structure");
    const ScriptResult result = harness.execute(R"JS(
var panel = document.getElementById("panel");
var first = document.getElementById("button-first");
var last = document.getElementById("button-last");
var only = document.getElementById("only-button");
var mixedOnly = document.getElementById("only-type-button");
var threeFirst = document.getElementById("three-first");
var threeMiddle = document.getElementById("three-middle");
var threeLast = document.getElementById("three-last");
var hidden = document.getElementById("hidden-button");
var visible = document.getElementById("visible-button");
var classTarget = document.getElementById("class-target");
var attributeTarget = document.getElementById("attribute-target");
var firstRules = first.matches(":first-of-type") &&
  !last.matches(":first-of-type") &&
  first.matches(":first-of-type") &&
  !document.getElementById("span-last").matches(":first-of-type");
var lastRules = last.matches(":last-of-type") &&
  !first.matches(":last-of-type") &&
  last.matches(":last-of-type") &&
  !document.getElementById("span-before").matches(":last-of-type");
var onlyRules = mixedOnly.matches(":only-of-type") &&
  !mixedOnly.matches(":only-child") &&
  !first.matches(":only-of-type") && !last.matches(":only-of-type") &&
  only.matches(":only-of-type");
var threeTypeRules = threeFirst.matches(":first-of-type") &&
  !threeFirst.matches(":last-of-type") && !threeFirst.matches(":only-of-type") &&
  !threeMiddle.matches(":first-of-type") &&
  !threeMiddle.matches(":last-of-type") && !threeMiddle.matches(":only-of-type") &&
  threeLast.matches(":last-of-type") && !threeLast.matches(":first-of-type") &&
  !threeLast.matches(":only-of-type");
var childDivergence = first.matches(":first-of-type") &&
  !first.matches(":first-child") && last.matches(":last-of-type") &&
  !last.matches(":last-child") && mixedOnly.matches(":only-of-type") &&
  !mixedOnly.matches(":only-child");
var implications = only.matches(":first-child") &&
  only.matches(":last-child") && only.matches(":only-child") &&
  only.matches(":first-of-type") && only.matches(":last-of-type") &&
  only.matches(":only-of-type");
var hiddenCounts = !visible.matches(":first-of-type") &&
  !hidden.matches(":last-of-type");
var classDoesNotDefineType = classTarget.matches(".target:first-of-type") &&
  !classTarget.matches(".target:only-of-type");
var attributeDoesNotDefineType = attributeTarget.matches(
  "[data-role=target]:first-of-type") &&
  !attributeTarget.matches("[data-role=target]:only-of-type");
var foreignParentIgnored = only.matches(":only-of-type") &&
  document.getElementById("form-owned").matches(":only-of-type");
var root = document.querySelector("html");
var rootFailsClosed = root.parentElement === null &&
  !root.matches(":first-of-type") && !root.matches(":last-of-type") &&
  !root.matches(":only-of-type");
var queryApis = document.querySelector("button:first-of-type") === first &&
  document.querySelector("button:last-of-type") === last &&
  document.querySelector("button:only-of-type") === only &&
  panel.querySelector("button:first-of-type") === first &&
  panel.querySelector("button:last-of-type") === last &&
  panel.querySelector("#panel:first-of-type") === null &&
  panel.querySelector("#only-button:only-of-type") === null &&
  document.querySelectorAll("button:first-of-type")[0] === first &&
  document.querySelectorAll("#only-button:only-of-type")[0] === only;
var queryOrder = document.querySelectorAll(
  "#button-last:last-of-type, #button-first:first-of-type")[0] === first &&
  document.querySelectorAll(
  "#button-last:last-of-type, #button-first:first-of-type")[1] === last &&
  document.querySelector("#button-last:last-of-type, #button-first:first-of-type") === first;
var listDedup = document.querySelectorAll(
  "#only-button:first-child, #only-button:only-of-type").length === 1 &&
  document.querySelectorAll(
  "#only-button:first-of-type, #only-button:last-of-type, #only-button:only-of-type").length === 1;
var listChildOverlap = document.querySelectorAll(
  "#only-button:only-child, #only-button:only-of-type").length === 1;
var listFour = document.querySelectorAll(
  "button:first-of-type, button:last-of-type, button:only-of-type, span:only-of-type").length >= 4;
var relationDescendant = document.querySelector(
  ".group:first-of-type button:last-of-type") === last;
var relationChild = document.querySelector(
  "#panel:first-of-type > button:last-of-type") === last;
var relationAdjacent = document.querySelector(
  "#icon:first-of-type + button:last-of-type") === last;
var relationGeneral = document.querySelector(
  "#span-before:first-of-type ~ button:last-of-type") === last;
var relationBothSides = last.matches("button:first-of-type ~ button:last-of-type") &&
  document.querySelector("#button-first:first-of-type ~ #button-last:last-of-type") === last;
var relationOneLimit = document.querySelector(
  "button:first-of-type + i + button:last-of-type") === null;
var closest = first.closest("button:first-of-type") === first &&
  last.closest(".group:first-of-type") === panel &&
  last.closest("#absent:only-of-type") === null;
var structuralCrossCheck = panel.children.length === 5 &&
  panel.firstElementChild.id === "span-before" &&
  panel.lastElementChild.id === "span-last" &&
  !panel.firstElementChild.matches("button:first-of-type") &&
  panel.children[1] === first;
)JS");
    expect(result.succeeded(), "of-type rules, queries, relations, and lists execute");
    reportScriptFailure(result, "of-type rules and query APIs");
    const char* names[] = {"firstRules", "lastRules", "onlyRules",
        "threeTypeRules",
        "childDivergence", "implications", "hiddenCounts",
        "classDoesNotDefineType", "attributeDoesNotDefineType",
        "foreignParentIgnored", "rootFailsClosed", "queryApis", "queryOrder",
        "listDedup", "listChildOverlap", "listFour", "relationDescendant",
        "relationChild", "relationAdjacent", "relationGeneral",
        "relationBothSides", "relationOneLimit", "closest",
        "structuralCrossCheck"};
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("structure: ") + name);

    const std::uint64_t formOwnerSerial = serialById(harness, "form-owner");
    gxos::web::HtmlElementRef* formOwned = elementById(harness, "form-owned");
    expect(formOwned != nullptr && formOwnerSerial != 0u,
        "form-owned structural Element and owner are available");
    if (formOwned != nullptr) {
        formOwned->formControl.parentFormSerial = formOwnerSerial;
        const ScriptResult ownership = harness.execute(
            "var formDoesNotDefineOfType = "
            "document.getElementById('form-owned').matches(':only-of-type') && "
            "document.getElementById('form-owned').parentElement === "
            "document.getElementById('outside-parent');");
        expect(ownership.succeeded(), "form ownership independence executes");
        reportScriptFailure(ownership, "form ownership independence");
        expectBoolean(harness, "formDoesNotDefineOfType", true,
            "logical form ownership does not replace structural parentage");
    }

    gxos::web::HtmlElementRef* buttonLast = elementById(harness,
        "button-last");
    gxos::web::HtmlElementRef* buttonFirst = elementById(harness,
        "button-first");
    expect(buttonLast != nullptr && buttonFirst != nullptr,
        "mixed-type button siblings resolve");
    if (buttonLast != nullptr && buttonFirst != nullptr) {
        expect(buttonFirst->tagName == "button",
            "parsed uppercase HTML tag is normalized in the structural Element");
        const std::string savedTag = buttonLast->tagName;
        buttonLast->tagName = "BUTTON";
        const ScriptResult tagCase = harness.execute(
            "var normalizedTypeCase = "
            "document.getElementById('button-last').matches('button:last-of-type') && "
            "document.getElementById('button-first').matches('BUTTON:first-of-type');");
        expect(tagCase.succeeded(), "mixed-case tag type check executes");
        reportScriptFailure(tagCase, "tag comparison case");
        expectBoolean(harness, "normalizedTypeCase", true,
            "of-type uses the ordinary case-insensitive tag matcher");
        buttonLast->tagName = savedTag;
    }

    if (buttonLast != nullptr) {
        const std::uint64_t savedPrevious =
            buttonLast->previousSiblingSerial;
        buttonLast->previousSiblingSerial = 0u;
        const ScriptResult brokenPrevious = harness.execute(
            "var brokenPreviousLinkFailsClosed = "
            "!document.getElementById('button-last').matches(':first-of-type') && "
            "!document.getElementById('button-last').matches(':last-of-type') && "
            "!document.getElementById('button-last').matches(':only-of-type') && "
            "!document.getElementById('button-last').matches(':nth-child(4)') && "
            "!document.getElementById('button-last').matches(':nth-of-type(2)');");
        expect(brokenPrevious.succeeded(),
            "broken previous-sibling metadata check executes");
        expectBoolean(harness, "brokenPreviousLinkFailsClosed", true,
            "broken structural sibling ordering fails closed for all of-type pseudos");
        buttonLast->previousSiblingSerial = savedPrevious;

        const std::uint16_t savedChildIndex = buttonLast->childIndex;
        buttonLast->childIndex = 1u;
        const ScriptResult brokenIndex = harness.execute(
            "var brokenChildIndexFailsClosed = "
            "!document.getElementById('button-last').matches(':first-of-type') && "
            "!document.getElementById('button-last').matches(':last-of-type') && "
            "!document.getElementById('button-last').matches(':only-of-type') && "
            "!document.getElementById('button-last').matches(':nth-child(4)') && "
            "!document.getElementById('button-last').matches(':nth-last-child(2)') && "
            "!document.getElementById('button-last').matches(':nth-of-type(2)') && "
            "!document.getElementById('button-last').matches(':nth-last-of-type(1)');");
        expect(brokenIndex.succeeded(), "broken child-index check executes");
        expectBoolean(harness, "brokenChildIndexFailsClosed", true,
            "inconsistent sibling order indexes fail closed");
        buttonLast->childIndex = savedChildIndex;
    }
}

void testStructuralMutationLiveCollectionsAndPurity()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    gxos::javascript::RuntimeLimits limits;
    limits.maxHostOperations = 100000u;
    limits.maxRuntimeStringValues = 20000u;
    limits.maxTotalRuntimeStringBytes = 1024u * 1024u;
    NavigatorScriptExecutionHarness harness(limits);
    loadFixture(harness, error, "live");
    const std::uint64_t generation = harness.hostAdapter().generation();
    const std::size_t mutationCount = harness.document().scriptMutationCount;
    const std::uint64_t focusSerial =
        harness.document().formRuntimeState.focusedLogicalSerial;
    const std::size_t runtimeCount = harness.document().formRuntimeState.count;
    const std::size_t listenerCount = harness.hostAdapter().clickListenerCount();
    using ElementSnapshot = std::tuple<std::uint64_t, std::uint64_t,
        std::string, std::string, std::string, std::string, std::uint16_t,
        std::uint16_t, std::uint8_t, std::uint64_t, std::uint16_t,
        std::uint16_t, std::uint16_t, std::uint64_t>;
    std::vector<ElementSnapshot> before;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        before.emplace_back(element.serial, element.parentSerial, element.tagName,
            element.id, element.className, element.inlineStyle,
            element.attributePresence, element.retainedAttributeOffset,
            element.retainedAttributeCount,
            element.formControl.parentFormSerial, element.childIndex,
            element.childCount, element.siblingCount,
            element.previousSiblingSerial);
    }
    const std::vector<std::uint8_t> retainedBefore =
        harness.document().retainedAttributeStorage;
    const ScriptResult setup = harness.execute(R"JS(
var panel = document.getElementById("panel");
var heldPanelOnly = panel.querySelectorAll("button:only-of-type");
var heldPanelFirst = panel.querySelectorAll("button:first-of-type");
var heldPanelNth = panel.querySelectorAll("#button-first:nth-of-type(1)");
var heldTargetOnly = document.querySelectorAll("#class-target:only-of-type");
var heldList = panel.querySelectorAll(
  "button:first-of-type, button:only-of-type");
var heldReadsStable = true;
for (var i = 0; i < 300; i = i + 1) {
  if (heldPanelOnly.length !== 0 || heldPanelFirst.length !== 1 ||
      heldPanelNth.length !== 1 || heldTargetOnly.length !== 0 ||
      heldList.length !== 1)
    heldReadsStable = false;
}
)JS");
    expect(setup.succeeded(), "held selector collections and rereads execute");
    reportScriptFailure(setup, "held collection setup");
    expectBoolean(harness, "heldReadsStable", true,
        "300 held collections reread current structure deterministically");

    gxos::web::HtmlElementRef* moved = elementById(harness, "button-last");
    gxos::web::HtmlElementRef* panel = elementById(harness, "panel");
    expect(moved != nullptr && panel != nullptr,
        "synthetic alternate structural parent resolves");
    if (moved != nullptr && panel != nullptr) {
        const std::uint64_t savedParent = moved->parentSerial;
        const std::uint64_t alternateParent = panel->parentSerial;
        moved->parentSerial = alternateParent;
        refreshSiblingMetadata(harness.document(), savedParent);
        refreshSiblingMetadata(harness.document(), alternateParent);
        const ScriptResult movedRead = harness.execute(R"JS(
var movedParentLive = heldPanelOnly.length === 1 &&
  heldPanelOnly[0] === document.getElementById("button-first") &&
  heldPanelFirst.length === 1 && heldPanelFirst[0] ===
    document.getElementById("button-first") &&
  heldPanelNth.length === 1 && heldPanelNth[0] ===
    document.getElementById("button-first") &&
  document.getElementById("button-last").matches(":only-of-type");
)JS");
        expect(movedRead.succeeded(), "synthetic parent reevaluation succeeds");
        reportScriptFailure(movedRead, "synthetic parent reevaluation");
        expectBoolean(harness, "movedParentLive", true,
            "held collections reevaluate changed parent membership live");
        moved->parentSerial = savedParent;
        refreshSiblingMetadata(harness.document(), savedParent);
        refreshSiblingMetadata(harness.document(), alternateParent);
    }

    gxos::web::HtmlElementRef* peer = elementById(harness, "class-peer");
    expect(peer != nullptr, "same-type class peer resolves");
    if (peer != nullptr) {
        const std::string savedTag = peer->tagName;
        peer->tagName = "input";
        const ScriptResult typeRead = harness.execute(R"JS(
var movedTypeLive = heldTargetOnly.length === 1 &&
  heldTargetOnly[0] === document.getElementById("class-target") &&
  document.getElementById("class-target").matches(".target:only-of-type") &&
  !document.getElementById("class-peer").matches("button:only-of-type");
)JS");
        expect(typeRead.succeeded(), "synthetic type reevaluation succeeds");
        reportScriptFailure(typeRead, "synthetic type reevaluation");
        expectBoolean(harness, "movedTypeLive", true,
            "held collections reevaluate current tag type, independent of class");
        peer->tagName = savedTag;
    }

    const std::uint64_t n1Serial = serialById(harness, "n1");
    const std::uint64_t n2Serial = serialById(harness, "n2");
    const std::uint64_t seriesSerial = serialById(harness, "series");
    const std::uint64_t firstButtonSerial = serialById(harness, "button-first");
    const std::uint64_t lastButtonSerial = serialById(harness, "button-last");
    const ScriptResult liveNthSetup = harness.execute(R"JS(
var series = document.getElementById("series");
var heldSeriesOdd = series.querySelectorAll(".entry:nth-child(odd)");
var heldButtonTypeFirst = document.getElementById("panel").querySelectorAll(
  "button:nth-of-type(1)");
)JS");
    expect(liveNthSetup.succeeded(), "held nth collections are captured");
    reportScriptFailure(liveNthSetup, "held nth collection setup");

    const auto findIndexBySerial = [&harness](std::uint64_t serial) {
        for (std::size_t index = 0u;
             index < harness.document().structuralElements.size(); ++index) {
            if (harness.document().structuralElements[index].serial == serial)
                return index;
        }
        return harness.document().structuralElements.size();
    };
    std::size_t n1Index = findIndexBySerial(n1Serial);
    std::size_t n2Index = findIndexBySerial(n2Serial);
    expect(n1Index < harness.document().structuralElements.size() &&
        n2Index < harness.document().structuralElements.size(),
        "synthetic nth reorder endpoints resolve");
    if (n1Index < harness.document().structuralElements.size() &&
        n2Index < harness.document().structuralElements.size()) {
        std::swap(harness.document().structuralElements[n1Index],
            harness.document().structuralElements[n2Index]);
        refreshSiblingMetadata(harness.document(), seriesSerial);
        const ScriptResult childReorder = harness.execute(R"JS(
var reorderedChildIndex = heldSeriesOdd.length === 3 &&
  heldSeriesOdd[0] === document.getElementById("n2") &&
  heldSeriesOdd[1] === document.getElementById("n3") &&
  heldSeriesOdd[2] === document.getElementById("n5");
)JS");
        expect(childReorder.succeeded(), "synthetic child reorder is readable");
        reportScriptFailure(childReorder, "synthetic child reorder");
        expectBoolean(harness, "reorderedChildIndex", true,
            "held nth-child collection reevaluates structural sibling order");
        n1Index = findIndexBySerial(n1Serial);
        n2Index = findIndexBySerial(n2Serial);
        std::swap(harness.document().structuralElements[n1Index],
            harness.document().structuralElements[n2Index]);
        refreshSiblingMetadata(harness.document(), seriesSerial);
    }

    std::size_t firstButtonIndex = findIndexBySerial(firstButtonSerial);
    std::size_t lastButtonIndex = findIndexBySerial(lastButtonSerial);
    expect(firstButtonIndex < harness.document().structuralElements.size() &&
        lastButtonIndex < harness.document().structuralElements.size(),
        "synthetic type reorder endpoints resolve");
    if (firstButtonIndex < harness.document().structuralElements.size() &&
        lastButtonIndex < harness.document().structuralElements.size()) {
        std::swap(harness.document().structuralElements[firstButtonIndex],
            harness.document().structuralElements[lastButtonIndex]);
        refreshSiblingMetadata(harness.document(),
            serialById(harness, "panel"));
        const ScriptResult typeReorder = harness.execute(R"JS(
var reorderedTypeIndex = heldButtonTypeFirst.length === 1 &&
  heldButtonTypeFirst[0] === document.getElementById("button-last") &&
  document.getElementById("button-first").matches(":nth-of-type(2)");
)JS");
        expect(typeReorder.succeeded(), "synthetic type reorder is readable");
        reportScriptFailure(typeReorder, "synthetic type reorder");
        expectBoolean(harness, "reorderedTypeIndex", true,
            "held nth-of-type collection reevaluates same-tag sibling order");
        firstButtonIndex = findIndexBySerial(firstButtonSerial);
        lastButtonIndex = findIndexBySerial(lastButtonSerial);
        std::swap(harness.document().structuralElements[firstButtonIndex],
            harness.document().structuralElements[lastButtonIndex]);
        refreshSiblingMetadata(harness.document(),
            serialById(harness, "panel"));
    }

    const ScriptResult pure = harness.execute(R"JS(
var noEventsFromMatching = 0;
document.getElementById("button-first").addEventListener("click", function () {
  noEventsFromMatching = noEventsFromMatching + 1;
});
var pureMatchResults = true;
for (var j = 0; j < 100; j = j + 1) {
  if (!document.getElementById("button-first").matches(":first-of-type") ||
      !document.getElementById("button-last").matches(":last-of-type") ||
      !document.getElementById("only-button").matches(":only-of-type") ||
      !document.getElementById("button-first").matches(":nth-of-type(1)") ||
      !document.getElementById("button-last").matches(":nth-last-of-type(1)"))
    pureMatchResults = false;
}
)JS");
    expect(pure.succeeded(), "matching purity checks execute");
    reportScriptFailure(pure, "matching purity");
    expectBoolean(harness, "pureMatchResults", true,
        "repeated selector evaluation returns stable of-type results");
    expectNumber(harness, "noEventsFromMatching", 0,
        "matching does not dispatch events");

    bool unchanged = before.size() ==
        harness.document().structuralElements.size();
    std::size_t position = 0u;
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (position >= before.size()) { unchanged = false; break; }
        unchanged = unchanged && before[position] == ElementSnapshot(
            element.serial, element.parentSerial, element.tagName, element.id,
            element.className, element.inlineStyle, element.attributePresence,
            element.retainedAttributeOffset, element.retainedAttributeCount,
            element.formControl.parentFormSerial, element.childIndex,
            element.childCount, element.siblingCount,
            element.previousSiblingSerial);
        ++position;
    }
    unchanged = unchanged && generation == harness.hostAdapter().generation() &&
        mutationCount == harness.document().scriptMutationCount &&
        focusSerial == harness.document().formRuntimeState.focusedLogicalSerial &&
        runtimeCount == harness.document().formRuntimeState.count &&
        listenerCount + 1u == harness.hostAdapter().clickListenerCount() &&
        retainedBefore == harness.document().retainedAttributeStorage;
    expect(unchanged,
        "matching leaves tags, parent/sibling metadata, attributes, form/focus state, and generation unchanged");
}

void testMalformedParentAndNearCapacityStress()
{
    std::string html = "<!doctype html><html><body><div id=stress-parent>";
    for (std::size_t index = 0u; index < 1019u; ++index) {
        html += "<span id=stress-span-" + std::to_string(index) + "></span>";
    }
    html += "<input id=stress-input><button id=stress-button></button>"
        "</div></body></html>";

    RuntimeErrorCode error = RuntimeErrorCode::None;
    gxos::javascript::RuntimeLimits limits;
    limits.maxHostOperations = 100000u;
    limits.maxRuntimeStringValues = 20000u;
    limits.maxTotalRuntimeStringBytes = 1024u * 1024u;
    NavigatorScriptExecutionHarness harness(limits);
    expect(harness.loadHtml("file:///js54-near-capacity.html", html, error),
        "near-capacity mixed-tag fixture loads");
    expect(error == RuntimeErrorCode::None,
        "near-capacity fixture load has no runtime error");
    expect(harness.relayout(), "near-capacity fixture relayout succeeds");
    expect(harness.document().structuralElements.size() >= 1022u &&
        harness.document().structuralElements.size() <=
            gxos::javascript::kNavigatorScriptMaxDocumentNodes,
        "fixture exercises a near-capacity bounded structural tree");
    const ScriptResult stress = harness.execute(R"JS(
var parent = document.getElementById("stress-parent");
var firstSpan = document.getElementById("stress-span-0");
var lastSpan = document.getElementById("stress-span-1018");
var onlyInput = document.getElementById("stress-input");
var onlyButton = document.getElementById("stress-button");
var mixedNearLimit = firstSpan.matches(":first-of-type") &&
  lastSpan.matches(":last-of-type") &&
  onlyInput.matches(":only-of-type") &&
  onlyButton.matches(":first-of-type") && onlyButton.matches(":last-of-type") &&
  onlyButton.matches(":only-of-type") && !firstSpan.matches(":only-of-type") &&
  firstSpan.matches(":nth-child(1)") &&
  lastSpan.matches(":nth-child(1019)") &&
  onlyInput.matches(":nth-child(1020)") &&
  onlyButton.matches(":nth-child(1021)") &&
  onlyButton.matches(":nth-last-child(1)") &&
  onlyInput.matches(":nth-last-child(2)") &&
  lastSpan.matches(":nth-of-type(1019)") &&
  lastSpan.matches(":nth-last-of-type(1)") &&
  onlyButton.matches(":nth-of-type(1)") &&
  onlyButton.matches(":nth-last-of-type(1)");
var deterministic = true;
for (var i = 0; i < 1000; i = i + 1) {
  if (!firstSpan.matches(":first-of-type") ||
      !lastSpan.matches(":last-of-type") ||
      !onlyButton.matches(":only-of-type") ||
      !firstSpan.matches(":nth-child(1)") ||
      !lastSpan.matches(":nth-child(1019)") ||
      !onlyButton.matches(":nth-last-child(1)") ||
      !lastSpan.matches(":nth-of-type(1019)")) deterministic = false;
}
var firstHeld = document.querySelectorAll("#stress-span-0:first-of-type");
var lastHeld = document.querySelectorAll("#stress-span-1018:last-of-type");
var onlyHeld = document.querySelectorAll("#stress-button:only-of-type");
var nthHeld = document.querySelectorAll("#stress-button:nth-child(1021)");
var heldDeterministic = true;
for (var j = 0; j < 300; j = j + 1) {
  if (firstHeld.length !== 1 || lastHeld.length !== 1 || onlyHeld.length !== 1 ||
      nthHeld.length !== 1 || nthHeld[0] !== onlyButton)
    heldDeterministic = false;
}
var elementOrderAgreement = parent.children.length === 1021 &&
  parent.firstElementChild === firstSpan && parent.lastElementChild === onlyButton &&
  parent.children[1018] === lastSpan;
)JS");
    expect(stress.succeeded(),
        "1,000 repeated nth/of-type matches and 300 held collection reads complete");
    reportScriptFailure(stress, "near-capacity of-type stress");
    expectBoolean(harness, "mixedNearLimit", true,
        "near-capacity same-type scans preserve mixed-tag semantics");
    expectBoolean(harness, "deterministic", true,
        "1,000 first/last/only-of-type repetitions are deterministic");
    expectBoolean(harness, "heldDeterministic", true,
        "300 held live collection rereads stay current");
    expectBoolean(harness, "elementOrderAgreement", true,
        "of-type order agrees with the bounded children collection");

    gxos::web::HtmlElementRef* root = elementById(harness, "stress-parent");
    expect(root != nullptr, "stress parent resolves for malformed metadata proof");
    if (root != nullptr) {
        const std::uint64_t originalParent = root->parentSerial;
        const std::uint64_t serial = root->serial;
        root->parentSerial = serial;
        const ScriptResult selfParent = harness.execute(
            "var selfParentFailsClosed = "
            "!document.getElementById('stress-parent').matches(':first-of-type') && "
            "!document.getElementById('stress-parent').matches(':last-of-type') && "
            "!document.getElementById('stress-parent').matches(':only-of-type') && "
            "!document.getElementById('stress-parent').matches(':nth-child(1)') && "
            "!document.getElementById('stress-parent').matches(':nth-last-child(1)') && "
            "!document.getElementById('stress-parent').matches(':nth-of-type(1)') && "
            "!document.getElementById('stress-parent').matches(':nth-last-of-type(1)');");
        expect(selfParent.succeeded(), "self-parent metadata check executes");
        expectBoolean(harness, "selfParentFailsClosed", true,
            "self-parent metadata fails closed for all of-type pseudos");
        root->parentSerial = 999999u;
        const ScriptResult missingParent = harness.execute(
            "var missingParentFailsClosed = "
            "!document.getElementById('stress-parent').matches(':first-of-type') && "
            "!document.getElementById('stress-parent').matches(':last-of-type') && "
            "!document.getElementById('stress-parent').matches(':only-of-type') && "
            "!document.getElementById('stress-parent').matches(':nth-child(1)') && "
            "!document.getElementById('stress-parent').matches(':nth-last-child(1)') && "
            "!document.getElementById('stress-parent').matches(':nth-of-type(1)') && "
            "!document.getElementById('stress-parent').matches(':nth-last-of-type(1)');");
        expect(missingParent.succeeded(), "missing-parent metadata check executes");
        expectBoolean(harness, "missingParentFailsClosed", true,
            "missing parent serial fails closed for all of-type pseudos");
        root->parentSerial = originalParent;
    }

    gxos::web::HtmlElementRef* malformedChild = elementById(harness,
        "stress-span-0");
    expect(malformedChild != nullptr,
        "near-capacity child resolves for order-corruption proof");
    if (malformedChild != nullptr) {
        const std::uint16_t savedChildIndex = malformedChild->childIndex;
        malformedChild->childIndex = static_cast<std::uint16_t>(
            savedChildIndex + 1u);
        const ScriptResult brokenOrder = harness.execute(
            "var brokenNthOrderFailsClosed = "
            "!document.getElementById('stress-span-0').matches(':nth-child(1)') && "
            "!document.getElementById('stress-span-0').matches(':nth-last-child(1021)') && "
            "!document.getElementById('stress-span-0').matches(':nth-of-type(1)') && "
            "!document.getElementById('stress-span-0').matches(':nth-last-of-type(1019)');");
        expect(brokenOrder.succeeded(), "corrupt nth order check executes");
        expectBoolean(harness, "brokenNthOrderFailsClosed", true,
            "inconsistent child indexes fail closed for all four nth pseudos");
        malformedChild->childIndex = savedChildIndex;
    }
}

void testEventsAndGenerationSafety()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "events");
    const std::uint64_t firstSerial = serialById(harness, "button-first");
    const std::uint64_t lastSerial = serialById(harness, "button-last");
    const ScriptResult setup = harness.execute(R"JS(
var first = document.getElementById("button-first");
var last = document.getElementById("button-last");
var oldFirst = first;
var oldLast = last;
var oldCollection = document.querySelectorAll(
  ":first-of-type, :only-of-type, :nth-child(odd), :nth-last-of-type(1)");
var firstEvent = false;
var lastEvent = false;
var nestedEvent = false;
var eventMetadata = false;
first.addEventListener("click", function (event) {
  var target = event.target;
  var current = event.currentTarget;
  var phase = event.eventPhase;
  firstEvent = target.matches(":first-of-type") &&
    !target.matches(":first-child") &&
    target.matches(":nth-child(2)") && target.matches(":nth-of-type(1)") &&
    target.closest(".group:first-of-type") === document.getElementById("panel");
  last.click();
  nestedEvent = event.target === target && event.currentTarget === current &&
    event.eventPhase === phase;
  eventMetadata = event.relatedTarget === null &&
    event.defaultPrevented === false && current === first && phase === 2;
});
last.addEventListener("click", function (event) {
  lastEvent = event.target.matches(":last-of-type") &&
    !event.target.matches(":last-child") &&
    event.target.matches(":nth-child(4)") &&
    event.target.matches(":nth-last-of-type(1)");
});
)JS");
    expect(setup.succeeded(), "event handles and live collection are captured");
    reportScriptFailure(setup, "event/generation setup");
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(firstSerial, clickError),
        "first-of-type authentic click dispatch succeeds");
    expect(clickError == RuntimeErrorCode::None,
        "first-of-type click dispatch has no runtime error");
    clickError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(lastSerial, clickError),
        "last-of-type authentic click dispatch succeeds");
    expect(clickError == RuntimeErrorCode::None,
        "last-of-type click dispatch has no runtime error");
    for (const char* name : {"firstEvent", "lastEvent", "nestedEvent",
            "eventMetadata"})
        expectBoolean(harness, name, true,
            std::string("event integration: ") + name);

    expect(harness.invalidateDocumentGeneration(error),
        "of-type test advances document generation");
    const ScriptResult stale = harness.execute(R"JS(
var staleFailsClosed = !oldFirst.matches(":first-of-type") &&
  !oldFirst.matches(":last-of-type") && !oldFirst.matches(":only-of-type") &&
  !oldFirst.matches(":nth-child(2)") &&
  !oldFirst.matches(":nth-last-child(2)") &&
  !oldFirst.matches(":nth-of-type(1)") &&
  !oldFirst.matches(":nth-last-of-type(2)") &&
  oldFirst.closest(":first-of-type") === null &&
  oldFirst.closest(":nth-child(2)") === null &&
  oldLast.closest(":last-of-type") === null;
)JS");
    expect(stale.succeeded(), "stale matches and closest remain script-safe");
    reportScriptFailure(stale, "stale of-type match");
    expectBoolean(harness, "staleFailsClosed", true,
        "stale Elements cannot match or traverse replacement type state");
    expect(!harness.execute(
        "var staleCollectionLength = oldCollection.length;").succeeded(),
        "stale live collection fails closed");

    harness.document() = gxos::web::parseHtml(
        "file:///js54-replacement.html", fixture);
    const std::uint64_t newSerial = serialById(harness, "button-first");
    expect(newSerial == firstSerial,
        "replacement document reuses the structural Element serial");
    expect(harness.runtime().installHostGlobal("freshFirst", newSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "replacement first-of-type receives a current-generation handle");
    const ScriptResult reuse = harness.execute(R"JS(
var replacementTypeIsCurrent = freshFirst.matches(":first-of-type") &&
  freshFirst.matches(":nth-of-type(1)");
var oldTypeCannotSeeReplacement = !oldFirst.matches(":first-of-type") &&
  !oldFirst.matches(":last-of-type") && !oldFirst.matches(":only-of-type") &&
  !oldFirst.matches(":nth-child(2)") &&
  !oldFirst.matches(":nth-of-type(1)") &&
  oldFirst.closest(":nth-child(2)") === null &&
  oldFirst.closest(":first-of-type") === null;
)JS");
    expect(reuse.succeeded(), "serial-reuse comparison executes");
    reportScriptFailure(reuse, "serial reuse");
    expectBoolean(harness, "replacementTypeIsCurrent", true,
        "new generation observes current structural type state");
    expectBoolean(harness, "oldTypeCannotSeeReplacement", true,
        "old generation cannot see replacement-document siblings");
    expect(!harness.execute(
        "var reusedOldCollectionLength = oldCollection.length;").succeeded(),
        "old collection remains isolated after serial reuse");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js55.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testParserAndSelectorComposition();
    testNthParserMathAndComposition();
    testTypeRulesRelationsQueriesAndLists();
    testStructuralMutationLiveCollectionsAndPurity();
    testMalformedParentAndNearCapacityStress();
    testEventsAndGenerationSafety();
    if (failures != 0) {
        std::cerr << failures << " JS55 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    const std::size_t simple =
        sizeof(gxos::javascript::NavigatorScriptSimpleSelectorDescriptor);
    const std::size_t descriptor =
        sizeof(gxos::javascript::NavigatorScriptSelectorDescriptor);
    const std::size_t collection = (16u + descriptor + 7u) / 8u * 8u;
    std::cout << "Navigator JavaScript JS55 checks: " << checks << "/"
        << checks << " passed; simple=" << simple << " bytes complete="
        << descriptor << " bytes collection=" << collection
        << " bytes registry="
        << collection * gxos::javascript::kNavigatorScriptMaxSelectorCollections
        << " bytes\n";
    return 0;
}
