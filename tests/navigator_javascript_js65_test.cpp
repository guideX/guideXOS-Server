#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace gxos::javascript {

struct NavigatorScriptRelativeSelectorTestAccess {
    static constexpr std::size_t collectionRecordBytes()
    {
        return sizeof(NavigatorScriptHostAdapter::SelectorCollectionRecord);
    }

    static NavigatorScriptSelectorMatchResult simpleMatch(
        const NavigatorScriptHostAdapter& adapter,
        const gxos::web::HtmlElementRef& element,
        const NavigatorScriptSimpleSelectorDescriptor& selector,
        const NavigatorScriptSelectorDescriptor& storage)
    {
        return adapter.selectorSimpleElementMatchResult(element, selector,
            storage);
    }
};

} // namespace gxos::javascript

namespace {

using namespace gxos::javascript;

int checks = 0;
int failures = 0;

void expect(bool condition, const char* label)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

void expectScript(NavigatorScriptExecutionHarness& harness,
    const std::string& source, const char* label)
{
    ++checks;
    const ScriptResult result = harness.execute(source);
    const Value* value = harness.runtime().lookup(SourceView("ok", 2u));
    if (result.succeeded() && value != nullptr && value->isBoolean() &&
        value->booleanValue()) return;
    ++failures;
    std::cerr << "FAIL: " << label << " (status="
        << scriptStatusName(result.status) << ", error="
        << runtimeErrorCodeName(result.runtimeError.code) << ", line="
        << result.runtimeError.location.line << ", column="
        << result.runtimeError.location.column << ")\n";
    for (const char* name : {"ok", "card", "adjacent", "general",
            "structuralNegatives", "alternatives", "pairChecks",
            "pairFailures", "rejected", "validRepeated", "invalidRepeated",
            "poolLength", "pool0", "pool1", "pool2", "pool3",
            "fullFalse", "lateTrue", "secondAlternative", "repeatedValid",
            "repeatedInvalid"}) {
        const Value* value = harness.runtime().lookup(SourceView(name,
            std::char_traits<char>::length(name)));
        if (value == nullptr) continue;
        std::cerr << "  " << name << "=";
        if (value->isBoolean()) std::cerr << (value->booleanValue() ? "true" : "false");
        else if (value->isNumber()) std::cerr << value->numberValue();
        else std::cerr << "<other>";
        std::cerr << '\n';
    }
}

HostInstanceId serialById(const gxos::web::WebDocument& document,
    const std::string& id)
{
    for (const auto& element : document.structuralElements)
        if (element.id == id) return element.serial;
    return 0u;
}

} // namespace

int main()
{
    static_assert(sizeof(NavigatorScriptSimpleSelectorCoreDescriptor) == 32u,
        "JS65 keeps the shared selector-core size");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor) == 68u,
        "JS65 keeps the outer simple-selector size");
    static_assert(sizeof(NavigatorScriptCompactRelativeSelectorCoreDescriptor) ==
        18u, "JS65 compact cores remain fixed-size");
    static_assert(sizeof(NavigatorScriptHasExpressionDescriptor) == 4u,
        "JS65 expression metadata remains fixed-size");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1278u,
        "JS65 selector descriptor has bounded fixed pools");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() == 1296u,
        "JS65 selector collection record size is measured");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections ==
            165888u,
        "JS65 128-record registry memory is measured");

    const std::string html =
        "<html><body><main id='main'>"
        "<section id='card'>"
        "<div id='row' class='row'><div><b id='badge' class='badge'></b></div></div>"
        "<div class='toolbar'><button class='button'></button></div>"
        "<i class='item'></i><i class='selected'></i><i></i><i class='warning'></i>"
        "<div class='branch'></div><div class='candidate'></div>"
        "<div class='other'><i class='icon'></i></div><i class='error'></i>"
        "<div class='a'><div class='b'><div class='c'></div></div></div>"
        "<div class='one'></div><div class='two'></div><div class='three'></div>"
        "</section>"
        "<section id='adjacent-anchor'></section>"
        "<div class='panel'><button class='button'></button></div>"
        "<div class='panel'></div><section id='wrong-side'></section>"
        "<section id='general-anchor'></section>"
        "<div class='later'><div><i class='warning'></i></div></div>"
        "<div class='outside'><i class='warning'></i></div>"
        "</main></body></html>";
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js65.html", html, error) &&
        error == RuntimeErrorCode::None, "JS65 fixture loads");

    expectScript(harness, R"JS(
var card = document.querySelector('#card');
var adjacent = document.querySelector('#adjacent-anchor');
var general = document.querySelector('#general-anchor');
var ok = card.matches(':has(> .row .badge)') &&
 card.matches(':has(> .toolbar > .button)') &&
 card.matches(':has(.item + .selected)') &&
 card.matches(':has(.item ~ .warning)') &&
 adjacent.matches(':has(+ .panel > .button)') &&
 general.matches(':has(~ .later .warning)') &&
 document.querySelector('#card:has(.branch .icon, > .toolbar > .button)') === card;
)JS", "child, descendant, sibling, and chained OR relationships");

    expectScript(harness, R"JS(
var card = document.querySelector('#card');
var wrongSide = document.querySelector('#wrong-side');
var outside = document.querySelector('#outside');
var structuralNegatives = !card.matches(':has(> .row > .badge)') &&
 !wrongSide.matches(':has(+ .panel > .button)') &&
 !card.matches(':has(.outside .warning)') &&
 !card.matches(':has(> .row .error)') &&
 !card.matches(':has(.candidate .badge)') &&
 !card.matches(':has(.branch .icon)');
var alternatives = !card.matches(':has(.branch .warning, > .toolbar > .missing)') &&
 card.matches(':has(.branch .warning, > .toolbar > .button)') &&
 !card.matches(':has(.branch .warning, > .toolbar > .missing)');
var ok = structuralNegatives && alternatives;
)JS", "wrong relationships, unrelated branches, and failed second steps stay false");

    const std::string matrixHtml =
        "<html><body><main><section id='subject'>"
        "<div id='interChild'><div><i id='childDesc'></i></div><i id='childChild'></i></div>"
        "<i id='childAdjacent'></i><i id='childGap'></i><i id='childGeneral'></i>"
        "<div id='descWrap'><div id='interDesc'><div><i id='descDesc'></i></div>"
        "<i id='descChild'></i></div><i id='descAdjacent'></i><i id='descGap'></i>"
        "<i id='descGeneral'></i></div>"
        "</section>"
        "<div id='interAdjacent'><div><i id='adjDesc'></i></div><i id='adjChild'></i></div>"
        "<i id='adjAdjacent'></i><i id='adjGap'></i><i id='adjGeneral'></i>"
        "<i id='generalGap'></i>"
        "<div id='interGeneral'><div><i id='genDesc'></i></div><i id='genChild'></i></div>"
        "<i id='genAdjacent'></i><i id='genGap'></i><i id='genGeneral'></i>"
        "</main></body></html>";
    NavigatorScriptExecutionHarness matrixHarness;
    error = RuntimeErrorCode::None;
    expect(matrixHarness.loadHtml("file:///js65-matrix.html", matrixHtml,
        error) && error == RuntimeErrorCode::None,
        "all-combinator fixture loads");
    expectScript(matrixHarness, R"JS(
var anchor = document.querySelector('#subject');
var first = ['', '> ', '+ ', '~ '];
var between = [' ', ' > ', ' + ', ' ~ '];
var intermediateIds = ['interDesc', 'interChild', 'interAdjacent', 'interGeneral'];
var terminalIds = [
 'descDesc','descChild','descAdjacent','descGeneral',
 'childDesc','childChild','childAdjacent','childGeneral',
 'adjDesc','adjChild','adjAdjacent','adjGeneral',
 'genDesc','genChild','genAdjacent','genGeneral'];
var pairChecks = 0;
var pairFailures = 0;
for (var left = 0; left < 4; left++) {
 for (var right = 0; right < 4; right++) {
  for (var i = 0; i < 4; i++)
   document.querySelector('#' + intermediateIds[i]).removeAttribute('class');
  for (var j = 0; j < 16; j++)
   document.querySelector('#' + terminalIds[j]).removeAttribute('class');
  document.querySelector('#' + intermediateIds[left]).setAttribute('class', 'a');
  document.querySelector('#' + terminalIds[left * 4 + right]).setAttribute('class', 'b');
  var selector = ':has(' + first[left] + '.a' + between[right] + '.b)';
  pairChecks = pairChecks + 1;
  if (!anchor.matches(selector)) pairFailures = pairFailures + 1;
  document.querySelector('#' + terminalIds[left * 4 + right]).removeAttribute('class');
  pairChecks = pairChecks + 1;
  if (anchor.matches(selector)) pairFailures = pairFailures + 1;
 }
}
var ok = pairChecks === 32 && pairFailures === 0;
)JS", "all sixteen ordered relation pairs pass true and false states");

    NavigatorScriptExecutionHarness poolHarness;
    std::string poolHtml = "<html><body><main>";
    for (std::size_t i = 0u; i < 4u; ++i) {
        poolHtml += "<div id='pool" + std::to_string(i + 1u) + "'>"
            "<div class='a'><i class='b'></i></div>"
            "<div class='c'><i class='d'></i></div><span>"
            "<div class='e'><i class='f'></i></div>"
            "<div class='g'><i class='h'></i></div>"
            "</span></div>";
    }
    poolHtml += "</main></body></html>";
    error = RuntimeErrorCode::None;
    expect(poolHarness.loadHtml("file:///js65-pool.html", poolHtml, error) &&
        error == RuntimeErrorCode::None, "maximum chain-pool fixture loads");
    const std::string poolMember =
        "div:has(.a .b, .c .d)>span:has(.e .f, .g .h)";
    std::string poolList = poolMember;
    for (std::size_t i = 1u; i < kNavigatorScriptMaxSelectorListMembers; ++i)
        poolList += "," + poolMember;
    expectScript(poolHarness,
        "var pool = document.querySelectorAll(\"" + poolList + "\");"
        "var poolLength = pool.length;"
        "var pool0 = pool.length > 0 && pool[0].closest('#pool1') === document.querySelector('#pool1');"
        "var pool1 = pool.length > 1 && pool[1].closest('#pool2') === document.querySelector('#pool2');"
        "var pool2 = pool.length > 2 && pool[2].closest('#pool3') === document.querySelector('#pool3');"
        "var pool3 = pool.length > 3 && pool[3].closest('#pool4') === document.querySelector('#pool4');"
        "var ok = poolLength === 4 && pool0 && pool1 && pool2 && pool3;",
        "all eight chained :has() expressions consume the fixed 24-core pool");

    expectScript(harness, R"JS(
var card = document.querySelector('#card');
var unsupported = [
 '#card:has(.a .b .c)',
 '#card:has(.one, .two, .three)',
 '#card:has(> > .row)',
 '#card:has(.row + > .badge)',
 '#card:has(.row >)',
 '#card:has(.row ++ .badge)',
 '#card:has(+)',
 '#card:has(.row +)',
 '#card:has(> .row >)',
 '#card:has(:not(.one) .badge)',
 '#card:has(:has(.row) .badge)'];
var rejected = true;
for (var i = 0; i < unsupported.length; i++)
 if (document.querySelector(unsupported[i]) !== null) rejected = false;
var ok = rejected;
)JS", "three-step, three-alternative, malformed, and nested grammar is rejected");

    const std::string exactClass(242u, 'x');
    const std::string overClass(243u, 'x');
    const std::string exactSelector = ":has(> .row ." + exactClass + ")";
    const std::string overSelector = ":has(> .row ." + overClass + ")";
    expect(exactSelector.size() == kNavigatorScriptMaxSelectorLength &&
        overSelector.size() == kNavigatorScriptMaxSelectorLength + 1u,
        "near-capacity chained selectors have exact byte lengths");
    expectScript(harness,
        "var card = document.querySelector('#card');"
        "var badge = document.querySelector('#badge');"
        "badge.setAttribute('class','" + exactClass + "');"
        "var exactSelector = ':has(> .row ." + exactClass + ")';"
        "var overSelector = ':has(> .row ." + overClass + ")';"
        "var validRepeated = true;"
        "for (var i = 0; i < 24; i++) if (!card.matches(exactSelector)) validRepeated = false;"
        "badge.setAttribute('class','" + overClass + "');"
        "var invalidRepeated = true;"
        "for (var j = 0; j < 24; j++) if (card.matches(overSelector)) invalidRepeated = false;"
        "var ok = validRepeated && invalidRepeated;",
        "256-byte chained selectors succeed repeatedly and 257-byte inputs reject");
    expect(harness.execute(
        "document.querySelector('#badge').setAttribute('class','badge');")
        .succeeded(), "restore the ordinary badge class");

    // The first complete alternative succeeds. Its invalid second alternative
    // must remain untouched to preserve JS64 short-circuit behavior.
    NavigatorScriptSelectorDescriptor shortCircuitStorage;
    auto& shortCircuit = shortCircuitStorage.members[0].rightSimple;
    shortCircuit.valid = true;
    shortCircuit.statePseudo = NavigatorScriptStatePseudo::HasChild;
    shortCircuit.nthA = 1;
    shortCircuit.logicalSelector.valid = true;
    shortCircuit.logicalSelector.classTokenCount = 1u;
    shortCircuit.logicalSelector.classTokens[0].offset = 0u;
    shortCircuit.logicalSelector.classTokens[0].length = 3u;
    shortCircuitStorage.text[0] = 'r';
    shortCircuitStorage.text[1] = 'o';
    shortCircuitStorage.text[2] = 'w';
    shortCircuitStorage.text[3] = '\0';
    auto& firstTerminal = shortCircuitStorage.hasRelativeCores[0];
    firstTerminal.valid = true;
    firstTerminal.classTokenCount = 1u;
    firstTerminal.classBlockOffset = 4u;
    firstTerminal.classBlockLength = 5u;
    for (std::size_t i = 0u; i < 5u; ++i)
        shortCircuitStorage.text[4u + i] = "badge"[i];
    shortCircuitStorage.textLength = 9u;
    shortCircuitStorage.hasRelativeCores[1].valid = false;
    shortCircuitStorage.hasRelativeCoreCount = 2u;
    auto& shortCircuitExpression = shortCircuitStorage.hasExpressions[0];
    shortCircuitExpression.firstStepTwoCoreIndex = 0u;
    shortCircuitExpression.secondFirstCoreIndex = 1u;
    shortCircuitExpression.flags = 0xC0u;
    shortCircuitStorage.hasExpressionCount = 1u;
    const HostInstanceId cardSerial = serialById(harness.document(), "card");
    const gxos::web::HtmlElementRef* cardElement = nullptr;
    for (const auto& element : harness.document().structuralElements)
        if (element.serial == cardSerial) cardElement = &element;
    expect(cardElement != nullptr &&
        NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
            harness.hostAdapter(), *cardElement, shortCircuit, shortCircuitStorage) ==
            NavigatorScriptSelectorMatchResult::Match,
        "a successful complete first chain short-circuits a corrupt second alternative");

    NavigatorScriptExecutionHarness capacityHarness;
    std::string capacityHtml = "<html><body><div id='capacity'><div id='wrong'>";
    for (std::size_t i = 0u; i < 500u; ++i)
        capacityHtml += "<div class='row'><i class='badge'></i></div>";
    capacityHtml += "</div><div class='row'><i id='late-terminal'></i></div>";
    for (std::size_t i = 0u; i < 18u; ++i) capacityHtml += "<i></i>";
    capacityHtml += "</div></body></html>";
    error = RuntimeErrorCode::None;
    expect(capacityHarness.loadHtml("file:///js65-capacity.html", capacityHtml,
        error) && error == RuntimeErrorCode::None &&
        capacityHarness.document().structuralElements.size() ==
            kNavigatorScriptMaxDocumentNodes,
        "late-match fixture reaches the 1,024-Element bound");
    expectScript(capacityHarness, R"JS(
var capacity = document.querySelector('#capacity');
var fullFalse = !capacity.matches(':has(> .row .badge)');
document.querySelector('#late-terminal').setAttribute('class', 'badge');
var lateTrue = capacity.matches(':has(> .row .badge)');
var secondAlternative = capacity.matches(':has(> .row .absent, > .row .badge)');
var repeatedValid = true;
var repeatedInvalid = true;
for (var i = 0; i < 24; i++) {
 if (!capacity.matches(':has(> .row .badge)')) repeatedValid = false;
 if (capacity.matches(':has(> .row .badge .extra)')) repeatedInvalid = false;
}
var ok = fullFalse && lateTrue && secondAlternative && repeatedValid && repeatedInvalid;
)JS", "full-false, late-true, late intermediate, OR, and repeated rejection on 1,024 nodes");

    const HostInstanceId capacitySerial = serialById(
        capacityHarness.document(), "capacity");
    const gxos::web::HtmlElementRef* capacityElement = nullptr;
    for (const auto& element : capacityHarness.document().structuralElements)
        if (element.serial == capacitySerial) capacityElement = &element;
    expect(capacityElement != nullptr,
        "direct chain benchmark anchor is present");
    NavigatorScriptSelectorDescriptor chainDescriptor;
    auto& chainSelector = chainDescriptor.members[0].rightSimple;
    chainSelector.valid = true;
    chainSelector.statePseudo = NavigatorScriptStatePseudo::HasChild;
    chainSelector.nthA = 1;
    chainSelector.logicalSelector.valid = true;
    chainSelector.logicalSelector.classTokenCount = 1u;
    chainSelector.logicalSelector.classTokens[0].offset = 0u;
    chainSelector.logicalSelector.classTokens[0].length = 3u;
    chainDescriptor.text[0] = 'r';
    chainDescriptor.text[1] = 'o';
    chainDescriptor.text[2] = 'w';
    chainDescriptor.text[3] = '\0';
    chainDescriptor.text[4] = 'b';
    chainDescriptor.text[5] = 'a';
    chainDescriptor.text[6] = 'd';
    chainDescriptor.text[7] = 'g';
    chainDescriptor.text[8] = 'e';
    chainDescriptor.textLength = 9u;
    auto& chainTerminal = chainDescriptor.hasRelativeCores[0];
    chainTerminal.valid = true;
    chainTerminal.classTokenCount = 1u;
    chainTerminal.classBlockOffset = 4u;
    chainTerminal.classBlockLength = 5u;
    chainDescriptor.hasRelativeCoreCount = 1u;
    chainDescriptor.hasExpressions[0].firstStepTwoCoreIndex = 0u;
    chainDescriptor.hasExpressions[0].flags = 0x80u;
    chainDescriptor.hasExpressionCount = 1u;
    const auto start = std::chrono::steady_clock::now();
    NavigatorScriptSelectorMatchResult timedResult =
        NavigatorScriptSelectorMatchResult::Invalid;
    if (capacityElement != nullptr) {
        for (std::size_t iteration = 0u; iteration < 20u; ++iteration)
            timedResult = NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
                capacityHarness.hostAdapter(), *capacityElement, chainSelector,
                chainDescriptor);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    expect(timedResult == NavigatorScriptSelectorMatchResult::Match,
        "20 repeated late chained matches complete");
    std::cout << "JS65 1,024-node direct chain timing: 20 late successful evaluations="
        << elapsed << "us\n";

    NavigatorScriptExecutionHarness deepFalseHarness;
    std::string deepFalseHtml =
        "<html><body><section id='subject'></section><div id='outside'>";
    for (std::size_t index = 0u; index < 500u; ++index)
        deepFalseHtml += "<div class='row'><i class='badge'></i>";
    for (std::size_t index = 0u; index < 500u; ++index)
        deepFalseHtml += "</div>";
    for (std::size_t index = 0u; index < 20u; ++index)
        deepFalseHtml += "<i></i>";
    deepFalseHtml += "</div></body></html>";
    error = RuntimeErrorCode::None;
    expect(deepFalseHarness.loadHtml("file:///js65-deep-false.html",
        deepFalseHtml, error) && error == RuntimeErrorCode::None &&
        deepFalseHarness.document().structuralElements.size() ==
            kNavigatorScriptMaxDocumentNodes,
        "descendant-chain full-false fixture reaches 1,024 Elements");
    const HostInstanceId deepFalseSerial = serialById(
        deepFalseHarness.document(), "subject");
    const gxos::web::HtmlElementRef* deepFalseAnchor = nullptr;
    for (const auto& element : deepFalseHarness.document().structuralElements)
        if (element.serial == deepFalseSerial) deepFalseAnchor = &element;
    NavigatorScriptSelectorDescriptor deepFalseDescriptor;
    auto& deepFalseSelector = deepFalseDescriptor.members[0].rightSimple;
    deepFalseSelector.valid = true;
    deepFalseSelector.statePseudo = NavigatorScriptStatePseudo::HasDescendant;
    deepFalseSelector.nthA = 1;
    deepFalseSelector.logicalSelector.valid = true;
    deepFalseSelector.logicalSelector.classTokenCount = 1u;
    deepFalseSelector.logicalSelector.classTokens[0].offset = 0u;
    deepFalseSelector.logicalSelector.classTokens[0].length = 3u;
    deepFalseDescriptor.text[0] = 'r';
    deepFalseDescriptor.text[1] = 'o';
    deepFalseDescriptor.text[2] = 'w';
    deepFalseDescriptor.text[3] = '\0';
    deepFalseDescriptor.text[4] = 'b';
    deepFalseDescriptor.text[5] = 'a';
    deepFalseDescriptor.text[6] = 'd';
    deepFalseDescriptor.text[7] = 'g';
    deepFalseDescriptor.text[8] = 'e';
    deepFalseDescriptor.textLength = 9u;
    auto& deepFalseTerminal = deepFalseDescriptor.hasRelativeCores[0];
    deepFalseTerminal.valid = true;
    deepFalseTerminal.classTokenCount = 1u;
    deepFalseTerminal.classBlockOffset = 4u;
    deepFalseTerminal.classBlockLength = 5u;
    deepFalseDescriptor.hasRelativeCoreCount = 1u;
    deepFalseDescriptor.hasExpressions[0].firstStepTwoCoreIndex = 0u;
    deepFalseDescriptor.hasExpressions[0].flags = 0x80u;
    deepFalseDescriptor.hasExpressionCount = 1u;
    const auto deepStart = std::chrono::steady_clock::now();
    const NavigatorScriptSelectorMatchResult deepFalseResult =
        deepFalseAnchor == nullptr
            ? NavigatorScriptSelectorMatchResult::Invalid
            : NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
                deepFalseHarness.hostAdapter(), *deepFalseAnchor,
                deepFalseSelector, deepFalseDescriptor);
    const auto deepElapsed = std::chrono::duration_cast<
        std::chrono::microseconds>(std::chrono::steady_clock::now() -
            deepStart).count();
    expect(deepFalseResult == NavigatorScriptSelectorMatchResult::NoMatch,
        "deep unrelated descendant chains fail closed at 1,024 Elements");
    std::cout << "JS65 1,024-node direct descendant-chain full-false timing="
        << deepElapsed << "us\n";

    std::cout << "Navigator JavaScript JS65 checks: " << checks - failures
        << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
