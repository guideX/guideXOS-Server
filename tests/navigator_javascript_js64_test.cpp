#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace gxos::javascript {

struct NavigatorScriptRelativeSelectorTestAccess {
    static constexpr std::size_t collectionRecordBytes()
    {
        return sizeof(NavigatorScriptHostAdapter::SelectorCollectionRecord);
    }

    static void setHasInlineCore(
        NavigatorScriptSimpleSelectorDescriptor& selector,
        const NavigatorScriptCompactRelativeSelectorCoreDescriptor& core,
        std::uint8_t expressionIndex)
    {
        std::memcpy(selector.selectorPayload.data(), &core, sizeof(core));
        selector.selectorPayload[sizeof(core)] = expressionIndex;
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
    for (const char* name : {"pairFailures", "firstFailure", "orAnchor",
            "mixed", "cardFound", "scopedOK", "outerOK", "closestOK",
            "spaced", "commas", "rejected", "twoPresent", "poolLength",
            "pool0", "pool1", "pool2", "pool3", "poolOrder",
            "radioBefore", "radioAfter", "radioLive",
            "checkedBefore", "checkedAfter", "radioLive", "focusOn",
            "focusOff", "disabled", "empty"}) {
        const Value* debug = harness.runtime().lookup(SourceView(name,
            std::char_traits<char>::length(name)));
        if (debug == nullptr) continue;
        std::cerr << "  " << name << "=";
        if (debug->isBoolean()) std::cerr << (debug->booleanValue() ? "true" : "false");
        else if (debug->isNumber()) std::cerr << debug->numberValue();
        else if (debug->isString()) std::cerr << "<string>";
        else std::cerr << "<other>";
        std::cerr << '\n';
    }
}

void expectSuccess(NavigatorScriptExecutionHarness& harness,
    const std::string& source, const char* label)
{
    ++checks;
    const ScriptResult result = harness.execute(source);
    if (result.succeeded()) return;
    ++failures;
    std::cerr << "FAIL: " << label << " (status="
        << scriptStatusName(result.status) << ", error="
        << runtimeErrorCodeName(result.runtimeError.code) << ", line="
        << result.runtimeError.location.line << ", column="
        << result.runtimeError.location.column << ")\n";
}

HostInstanceId serialById(const gxos::web::WebDocument& document,
    const std::string& id)
{
    for (const auto& element : document.structuralElements)
        if (element.id == id) return element.serial;
    return 0u;
}

NavigatorScriptSelectorDescriptor diagnosticDescriptor(
    const char* firstClass, const char* secondClass)
{
    NavigatorScriptSelectorDescriptor storage;
    auto& simple = storage.members[0].rightSimple;
    simple.valid = true;
    simple.statePseudo = NavigatorScriptStatePseudo::HasDescendant;
    NavigatorScriptCompactRelativeSelectorCoreDescriptor firstCore;
    firstCore.classTokenCount = 1u;
    firstCore.classBlockOffset = 0u;
    std::size_t offset = 0u;
    while (firstClass[offset] != '\0') {
        storage.text[storage.textLength++] = firstClass[offset++];
        ++firstCore.classBlockLength;
    }
    NavigatorScriptRelativeSelectorTestAccess::setHasInlineCore(simple,
        firstCore, 0u);
    if (secondClass[0] != '\0') storage.text[storage.textLength++] = '\0';
    auto& compact = storage.hasRelativeCores[0];
    compact.classTokenCount = 1u;
    compact.classBlockOffset = static_cast<std::uint8_t>(storage.textLength);
    std::size_t secondOffset = 0u;
    while (secondClass[secondOffset] != '\0') {
        storage.text[storage.textLength++] = secondClass[secondOffset++];
        ++compact.classBlockLength;
    }
    auto& expression = storage.hasExpressions[0];
    expression.secondFirstCoreIndex = 0u;
    expression.flags = 0xC0u; // Valid expression with two alternatives.
    storage.hasRelativeCoreCount = 1u;
    storage.hasExpressionCount = 1u;
    return storage;
}

} // namespace

int main()
{
    static_assert(sizeof(NavigatorScriptSimpleSelectorCoreDescriptor) == 32u,
        "JS64 keeps the shared JS59-JS61 core size");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor) == 50u,
        "JS64 keeps the outer simple-selector size");
    static_assert(sizeof(NavigatorScriptCompactRelativeSelectorCoreDescriptor) ==
        16u, "JS64 stores each second alternative in 16 bytes");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1918u,
        "JS65 stores bounded chains in fixed compact pools");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() == 1936u,
        "JS65 selector collection record size is measured");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections ==
            247808u,
        "JS65 128-record registry memory is measured");

    const std::string html =
        "<html><body><main id='main'>"
        "<section id='anchor'><i id='direct'></i>"
        "<div><b id='deep'></b></div><i id='comma' data-key='alpha,beta'></i>"
        "</section><i id='adjacent'></i><i id='gap'></i><i id='later'></i>"
        "<section id='unsupported'>"
        "<div class='a'><div class='b'><i class='c'></i></div></div>"
        "<div class='one'></div><div class='two'></div><div class='three'></div>"
        "</section>"
        "<section id='card'><i id='warning' class='warning'></i></section>"
        "<form id='form'><input id='check' type='checkbox' checked>"
        "<input id='radio-a' type='radio' name='group' checked>"
        "<input id='radio-b' type='radio' name='group'></form>"
        "<button id='disabled' disabled></button><div id='empty'></div>"
        "<div id='pool1'><i class='a'></i><span id='span1'><i class='c'></i></span></div>"
        "<div id='pool2'><i class='a'></i><span id='span2'><i class='c'></i></span></div>"
        "<div id='pool3'><i class='a'></i><span id='span3'><i class='c'></i></span></div>"
        "<div id='pool4'><i class='a'></i><span id='span4'><i class='c'></i></span></div>"
        "<section id='event-anchor'></section><i id='event-next'></i>"
        "</main></body></html>";
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js64.html", html, error) &&
        error == RuntimeErrorCode::None, "JS64 fixture loads");

    expectScript(harness, R"JS(
var anchor = document.querySelector('#anchor');
var direct = document.querySelector('#direct');
var adjacent = document.querySelector('#adjacent');
var ok = anchor.matches(':has(#deep)') &&
 anchor.matches(':has(> #direct)') &&
 anchor.matches(':has([data-key="alpha,beta"], .missing)') &&
 !anchor.matches(':has(#anchor, .missing)') &&
 adjacent.matches(':has(~ #later)') &&
 !adjacent.matches(':has(+ #later)');
)JS", "single-argument JS63 relations, strict descendant, and quoted comma compatibility");

    const std::string maxClass(249u, 'x');
    const std::string maxSelector = ":has(." + maxClass + ")";
    const std::string overClass(250u, 'x');
    const std::string overSelector = ":has(." + overClass + ")";
    expect(maxSelector.size() == kNavigatorScriptMaxSelectorLength &&
        overSelector.size() == kNavigatorScriptMaxSelectorLength + 1u,
        "JS64 max-selector test inputs have exact lengths");
    expectScript(harness,
        "var anchor = document.querySelector('#anchor');"
        "document.querySelector('#direct').setAttribute('class','" +
        maxClass + "');"
        "var exactLimit = anchor.matches('" + maxSelector + "');"
        "document.querySelector('#direct').setAttribute('class','" +
        overClass + "');"
        "var overLimit = document.querySelector('#anchor" + overSelector +
        "') === null;"
        "var ok = exactLimit && overLimit;",
        "256-character selector accepted and 257-character selector rejected");

    expectScript(harness, R"JS(
var anchor = document.querySelector('#anchor');
var relationPrefixes = ['', '> ', '+ ', '~ '];
var relationTargets = ['deep', 'direct', 'adjacent', 'later'];
var allRelationPairs = true;
var pairFailures = 0;
var firstFailure = -1;
for (var left = 0; left < 4; left++) {
 for (var right = 0; right < 4; right++) {
  for (var t = 0; t < 4; t++)
   document.querySelector('#' + relationTargets[t]).removeAttribute('class');
  var selector = ':has(' + relationPrefixes[left] + '.a, ' +
   relationPrefixes[right] + '.b)';
  document.querySelector('#' + relationTargets[left]).setAttribute('class', 'a');
  if (!anchor.matches(selector)) {
   allRelationPairs = false; pairFailures++;
   if (firstFailure < 0) firstFailure = (left * 4 + right) * 3 + 1;
  }
  document.querySelector('#' + relationTargets[left]).removeAttribute('class');
  document.querySelector('#' + relationTargets[right]).setAttribute('class', 'b');
  if (!anchor.matches(selector)) {
   allRelationPairs = false; pairFailures++;
   if (firstFailure < 0) firstFailure = (left * 4 + right) * 3 + 2;
  }
  document.querySelector('#' + relationTargets[right]).removeAttribute('class');
  if (anchor.matches(selector)) {
   allRelationPairs = false; pairFailures++;
   if (firstFailure < 0) firstFailure = (left * 4 + right) * 3 + 3;
  }
 }
}
var ok = allRelationPairs;
)JS", "all 16 ordered relation pairs independently match first-only, second-only, and neither");

    expectScript(harness, R"JS(
var anchor = document.querySelector('#anchor');
document.querySelector('#direct').setAttribute('class', 'icon');
document.querySelector('#adjacent').setAttribute('class', 'badge');
var scope = document.querySelector('#main');
var scoped = scope.querySelectorAll(':has(> .icon, + .badge)');
var outer = document.querySelectorAll('#anchor:has(> .icon, + .badge), ' +
 '#card:has(.warning, .error), #anchor:has(.icon)');
var first = document.querySelector('#direct');
var closest = first.closest('#anchor:has(> .icon, + .badge)');
var orAnchor = anchor.matches(':has(> .icon, + .badge)');
var mixed = document.querySelector('#anchor:has(.missing, > .icon)') === anchor;
var cardFound = document.querySelector('#card:has(.warning, .error)') ===
 document.querySelector('#card');
var scopedOK = scoped.length === 1 && scoped[0] === anchor;
var outerOK = outer.length === 2 && outer[0] === anchor && outer[1] ===
 document.querySelector('#card');
var closestOK = closest === anchor;
var bothTrue = anchor.matches(':has(> .icon)') &&
 anchor.matches(':has(+ .badge)') && anchor.matches(':has(> .icon, + .badge)');
var duplicate = document.querySelector('#pool1:has(.a, .a)') ===
 document.querySelector('#pool1');
var liveAttr = document.querySelectorAll('#anchor:has(.missing, [data-live=yes])');
var attrBefore = liveAttr.length === 0;
document.querySelector('#direct').setAttribute('data-live', 'yes');
var attrAfter = liveAttr.length === 1;
var ok = orAnchor && mixed && cardFound && scopedOK && outerOK && closestOK &&
 bothTrue && duplicate && attrBefore && attrAfter;
)JS", "OR semantics, mixed relations, outer comma lists, APIs, order, and deduplication");

    expectScript(harness, R"JS(
var anchor = document.querySelector('#anchor');
var spaced = anchor.matches(':has(  .never  ,  > #direct  )');
var commas = anchor.matches(':has([data-key="alpha,beta"], .never)');
var invalid = [':has()', ':has( )', ':has(, .a)', ':has(.a,)',
 ':has(.a,,.b)', ':has(.a,.b,.c)', ':has(> , .a)', ':has(+)',
 ':has(~)', ':has(.a .b .c)', ':has(.a, .b, .c)',
 ':has(> .a + .b + .c)', ':has(:not(.x, .y, .z))',
 ':has(:is(.x, .y, .z))', ':has(:where(.x, .y, .z))',
 ':has(:is(:not(.x), .y))', ':has(:where(:is(.x), .y))',
 ':has(:not(:where(.x), .y))', ':has(:has(.x), .y)',
 ':has(:nth-child(2), .y)', ':has(.a):focus'];
document.querySelector('#direct').setAttribute('class', 'a b c');
var rejected = true;
var badCount = 0;
var firstBad = '';
for (var i = 0; i < invalid.length; i++)
 if (document.querySelector('#unsupported' + invalid[i]) !== null) {
  rejected = false; badCount = badCount + 1; firstBad = invalid[i];
 }
var twoPresent = document.querySelector('#anchor:has(.a, .b)') === anchor;
var ok = spaced && commas && rejected && twoPresent;
)JS", "whitespace, comma-aware quotes, malformed and unsupported syntax rejection");

    std::string eightPoolSelector =
        "div:has(.missing,.a)>span:has(.missing,.c)";
    std::string eightMemberList = eightPoolSelector;
    for (std::size_t index = 1u; index < 4u; ++index)
        eightMemberList += "," + eightPoolSelector;
    expectScript(harness,
        "var pool = document.querySelectorAll(\"" + eightMemberList + "\");"
        "var poolLength = pool.length;"
        "var pool0 = pool[0].matches('#span1');"
        "var pool1 = pool[1].matches('#span2');"
        "var pool2 = pool[2].matches('#span3');"
        "var pool3 = pool[3].matches('#span4');"
        "var poolTag0 = pool[0].tagName === 'SPAN';"
        "var poolClass0 = pool[0].querySelector('.c') !== null;"
        "var poolId0 = pool[0].id === 'span1';"
        "var poolMatch0 = pool[0].matches('span:has(.c)');"
        "var poolOrder = pool0 && pool1 && pool2 && pool3;"
        "var ok = pool.length === 4 && poolOrder;",
        "four outer members each retain two :has() cores using all eight pool slots");

    expectScript(harness, R"JS(
var form = document.querySelector('#form');
var check = document.querySelector('#check');
var checkedLive = document.querySelectorAll('#form:has(.none, #check:checked)');
var checkedBefore = checkedLive.length === 1;
check.checked = false;
var checkedAfter = checkedLive.length === 0;
var radio = document.querySelector('#radio-a');
var radioBefore = radio.checked;
var radioQuery = document.querySelectorAll('#form:has(.none, #radio-a:checked)');
radio.checked = false;
var radioAfter = radio.checked;
var radioLive = radioQuery.length === 0;
var focusLive = document.querySelectorAll('#form:has(.none, :focus)');
check.focus();
var focusOn = focusLive.length === 1;
check.blur();
var focusOff = focusLive.length === 0;
var disabled = document.querySelector('#main:has(.none, :disabled)') !== null;
var empty = document.querySelector('#main:has(.none, #empty:empty)') !== null;
var ok = checkedBefore && checkedAfter && radioLive && focusOn && focusOff &&
 disabled && empty;
)JS", "live checked, radio, focus, disabled, and empty state in either alternative");

    expectSuccess(harness, R"JS(
var eventAnchor = document.querySelector('#event-anchor');
var eventNext = document.querySelector('#event-next');
eventNext.setAttribute('class', 'event-next');
var eventRuns = 0;
var nested = false;
var metadataOK = true;
eventAnchor.addEventListener('click', function(event) {
 if (!event.target.matches(':has(+ .event-next, .never)') ||
     event.currentTarget !== eventAnchor || event.eventPhase !== 2 ||
     event.defaultPrevented) metadataOK = false;
 eventRuns = eventRuns + 1;
 if (!nested) { nested = true; eventAnchor.click(); }
});
)JS", "JS64 listener setup and nested dispatch");
    const HostInstanceId eventAnchor = serialById(harness.document(),
        "event-anchor");
    bool defaultPrevented = true;
    expect(eventAnchor != 0u && harness.dispatchClick(eventAnchor, error,
        &defaultPrevented) && !defaultPrevented,
        "JS64 Event dispatch succeeds and preserves default state");
    const Value* eventRuns = harness.runtime().lookup(SourceView("eventRuns", 9u));
    const Value* metadataOK = harness.runtime().lookup(SourceView("metadataOK", 10u));
    expect(eventRuns != nullptr && eventRuns->isNumber() &&
        eventRuns->numberValue() == 2.0 && metadataOK != nullptr &&
        metadataOK->isBoolean() && metadataOK->booleanValue(),
        "nested Event dispatch preserves metadata and evaluates the second syntax");

    NavigatorScriptExecutionHarness capacityHarness;
    std::string capacityHtml = "<html><body><div id='capacity'>";
    for (std::size_t index = 0u; index < 1020u; ++index)
        capacityHtml += "<i></i>";
    capacityHtml += "<b id='tail'></b></div></body></html>";
    error = RuntimeErrorCode::None;
    expect(capacityHarness.loadHtml("file:///js64-capacity.html",
        capacityHtml, error) && error == RuntimeErrorCode::None &&
        capacityHarness.document().structuralElements.size() ==
            kNavigatorScriptMaxDocumentNodes,
        "JS64 near-capacity fixture reaches exactly 1,024 Elements");
    expectScript(capacityHarness, R"JS(
var capacity = document.querySelector('#capacity');
var tail = document.querySelector('#tail');
var fullFalse = !capacity.matches(':has(.early, .target)');
tail.setAttribute('class', 'target');
var lateTrue = capacity.matches(':has(.early, .target)');
var list = document.querySelectorAll('div:has(.early, .target)');
var ok = fullFalse && lateTrue && list.length === 1 && list[0] === capacity;
)JS", "near-capacity two-alternative full-false and late-true");

    // Corrupting the second parsed descriptor distinguishes true short
    // circuiting from eager OR evaluation: the first core matches, while the
    // invalid second core would force Invalid if it were evaluated.
    auto& nearDocument = capacityHarness.document();
    const auto anchorPosition = serialById(nearDocument, "capacity");
    const gxos::web::HtmlElementRef* capacityElement = nullptr;
    for (const auto& element : nearDocument.structuralElements)
        if (element.serial == anchorPosition) capacityElement = &element;
    const ScriptResult addEarlyTarget = capacityHarness.execute(
        "document.querySelector('i').setAttribute('class', 'early');");
    expect(addEarlyTarget.succeeded(),
        "first near-capacity descendant becomes an early target");
    auto shortCircuitStorage = diagnosticDescriptor("early", "target");
    shortCircuitStorage.hasRelativeCores[0].classTokenCount =
        kNavigatorScriptMaxClassQueryTokens + 1u;
    const auto shortCircuit = capacityElement == nullptr
        ? NavigatorScriptSelectorMatchResult::Invalid
        : NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
            capacityHarness.hostAdapter(), *capacityElement,
            shortCircuitStorage.members[0].rightSimple,
            shortCircuitStorage);
    expect(capacityElement != nullptr &&
        shortCircuit == NavigatorScriptSelectorMatchResult::Match,
        "first alternative match short-circuits an invalid second core");

    auto secondEvaluationStorage = diagnosticDescriptor("missing", "target");
    const auto secondEvaluation = capacityElement == nullptr
        ? NavigatorScriptSelectorMatchResult::Invalid
        : NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
            capacityHarness.hostAdapter(), *capacityElement,
            secondEvaluationStorage.members[0].rightSimple,
            secondEvaluationStorage);
    expect(secondEvaluation == NavigatorScriptSelectorMatchResult::Match,
        "second alternative runs after a first-alternative non-match");

    auto allFalseStorage = diagnosticDescriptor("missing", "absent");
    const auto allFalse = capacityElement == nullptr
        ? NavigatorScriptSelectorMatchResult::Invalid
        : NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
            capacityHarness.hostAdapter(), *capacityElement,
            allFalseStorage.members[0].rightSimple, allFalseStorage);
    expect(allFalse == NavigatorScriptSelectorMatchResult::NoMatch,
        "two false alternatives return NoMatch");

    // Measure the branch cost on the bounded 1,024-node document without a
    // timing threshold; the result is emitted for the qualification report.
    const auto benchmark = [&capacityHarness, capacityElement](
        const char* firstClass, std::size_t repetitions) {
        if (capacityElement == nullptr) return std::int64_t{0};
        const auto descriptor = diagnosticDescriptor(firstClass, "target");
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t index = 0u; index < repetitions; ++index)
            (void)NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
                capacityHarness.hostAdapter(), *capacityElement,
                descriptor.members[0].rightSimple, descriptor);
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
    };
    constexpr std::size_t benchmarkRepetitions = 20u;
    const auto firstTrueMicros = benchmark("early", benchmarkRepetitions);
    const auto secondTrueMicros = benchmark("missing", benchmarkRepetitions);
    std::cout << "JS64 1,024-node match timing (" << benchmarkRepetitions
        << " calls): first alternative true=" << firstTrueMicros
        << "us; second alternative true=" << secondTrueMicros << "us\n";

    NavigatorScriptExecutionHarness staleHarness;
    error = RuntimeErrorCode::None;
    expect(staleHarness.loadHtml("file:///js64-stale.html",
        "<html><body><div id='old'><i class='x'></i></div></body></html>",
        error), "stale-handle fixture loads");
    expect(staleHarness.execute(
        "var stale = document.querySelector('#old');").succeeded(),
        "stale Element retained for JS64 calls");
    expect(staleHarness.invalidateDocumentGeneration(error),
        "JS64 host generation invalidates");
    expectScript(staleHarness, R"JS(
var staleMatchFalse = stale.matches(':has(.x, .y)') === false;
var staleClosestNull = stale.closest(':has(.x, .y)') === null;
var ok = staleMatchFalse && staleClosestNull;
)JS", "stale matches and closest fail closed before OR evaluation");
    expect(staleHarness.replaceHtml("file:///js64-stale-replacement.html",
        "<html><body><div id='new'><i class='y'></i></div></body></html>",
        error), "generation replacement document loads");
    expectScript(staleHarness,
        "var ok = document.querySelector('#new:has(.x, .y)') !== null;",
        "replacement generation uses JS64 selectors normally");

    std::cout << "Navigator JavaScript JS64 checks: " << checks - failures
        << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
