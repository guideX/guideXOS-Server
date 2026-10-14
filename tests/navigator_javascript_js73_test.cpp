#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptRelativeSelectorTestAccess {
    using Collection = NavigatorScriptHostAdapter::SelectorCollectionRecord;

    static std::size_t activeCollectionCount(
        const NavigatorScriptHostAdapter& adapter)
    {
        std::size_t count = 0u;
        for (const Collection& record : adapter.selectorCollections_)
            if (record.active) ++count;
        return count;
    }

    static const Collection* collectionFor(
        const NavigatorScriptHostAdapter& adapter,
        const std::string& selector)
    {
        static_cast<void>(selector);
        for (const Collection& record : adapter.selectorCollections_) {
            if (!record.active) continue;
            for (std::size_t memberIndex = 0u;
                    memberIndex < record.selector.memberCount; ++memberIndex) {
                const auto& member = record.selector.members[memberIndex];
                const auto isFiltered = [](const auto& simple) {
                    return (simple.statePseudo ==
                                NavigatorScriptStatePseudo::NthChild ||
                            simple.statePseudo ==
                                NavigatorScriptStatePseudo::NthLastChild) &&
                        simple.selectorPayload[sizeof(std::int16_t) * 2u] !=
                            0xFFu;
                };
                if (isFiltered(member.leftSimple) ||
                    isFiltered(member.rightSimple)) return &record;
            }
        }
        return nullptr;
    }

    static const Collection* collectionForRankSlotCount(
        const NavigatorScriptHostAdapter& adapter, std::size_t slotCount)
    {
        for (const Collection& record : adapter.selectorCollections_) {
            if (record.active && adapter.selectorFilteredNthRankSlotCount(
                    record.selector) == slotCount)
                return &record;
        }
        return nullptr;
    }

#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    static NavigatorScriptSelectorMatchResult directMatch(
        const NavigatorScriptHostAdapter& adapter,
        const gxos::web::HtmlElementRef& element,
        const NavigatorScriptSimpleSelectorDescriptor& selector,
        const NavigatorScriptSelectorDescriptor& storage,
        const NavigatorScriptSelectorMatchContext& context,
        NavigatorScriptFilteredNthCounters* counters)
    {
        return adapter.selectorSimpleElementMatchResult(element, selector,
            storage, context, counters);
    }

    static std::size_t countWithDiagnostics(
        const NavigatorScriptHostAdapter& adapter,
        const Collection& record,
        NavigatorScriptFilteredNthCounters& counters)
    {
#ifdef GXOS_JS73_BASELINE
        const auto& member = record.selector.members[0u];
        const auto context =
            adapter.selectorMatchContextForScope(record.scopeSerial);
        HostInstanceId loadSerial = 0u;
        for (const auto& element : adapter.document_->structuralElements)
            if (element.id == "load") loadSerial = element.serial;
        std::size_t matched = 0u;
        for (const auto& element : adapter.document_->structuralElements) {
            if (element.parentSerial != loadSerial) continue;
            if (adapter.selectorSimpleElementMatchResult(element,
                    member.rightSimple, record.selector, context, &counters) ==
                NavigatorScriptSelectorMatchResult::Match)
                ++matched;
        }
        return matched;
#else
        return adapter.selectorMatchCount(record, &counters);
#endif
    }

    template<std::size_t RankSlotCount>
    static bool matchAtWithDiagnosticsSlots(
        const NavigatorScriptHostAdapter& adapter,
        const Collection& record, std::size_t index, HostInstanceId& serial,
        NavigatorScriptFilteredNthCounters& counters)
    {
        NavigatorScriptHostAdapter::FilteredNthQueryScratchStorage<
            RankSlotCount> storage{};
        auto& scratch = storage.scratch;
        const auto context =
            adapter.selectorMatchContextForScope(record.scopeSerial);
        scratch.document = adapter.document_;
        scratch.selector = &record.selector;
        scratch.rankValues = storage.rankValues.data();
        scratch.generation = adapter.generation_;
        scratch.scopeSerial = context.scopeSerial;
        adapter.initializeFilteredNthRankSlots(record.selector, scratch);
        if (scratch.rankSlotCount != RankSlotCount) return false;
        scratch.counters = &counters;
        return adapter.selectorMatchAtInternal(record, index, serial,
            &scratch);
    }

    static bool matchAtWithDiagnostics(
        const NavigatorScriptHostAdapter& adapter,
        const Collection& record, std::size_t index, HostInstanceId& serial,
        NavigatorScriptFilteredNthCounters& counters)
    {
        switch (adapter.selectorFilteredNthRankSlotCount(record.selector)) {
        case 1u: return matchAtWithDiagnosticsSlots<1u>(adapter, record,
            index, serial, counters);
        case 2u: return matchAtWithDiagnosticsSlots<2u>(adapter, record,
            index, serial, counters);
        case 3u: return matchAtWithDiagnosticsSlots<3u>(adapter, record,
            index, serial, counters);
        case 4u: return matchAtWithDiagnosticsSlots<4u>(adapter, record,
            index, serial, counters);
        case 5u: return matchAtWithDiagnosticsSlots<5u>(adapter, record,
            index, serial, counters);
        case 6u: return matchAtWithDiagnosticsSlots<6u>(adapter, record,
            index, serial, counters);
        case 7u: return matchAtWithDiagnosticsSlots<7u>(adapter, record,
            index, serial, counters);
        case 8u: return matchAtWithDiagnosticsSlots<8u>(adapter, record,
            index, serial, counters);
        default: return false;
        }
    }

    static void addCounters(NavigatorScriptFilteredNthCounters& total,
        const NavigatorScriptFilteredNthCounters& value)
    {
        total.structuralInspections += value.structuralInspections;
        total.parentResolutions += value.parentResolutions;
        total.siblingInspections += value.siblingInspections;
        total.simpleSelectorEvaluations += value.simpleSelectorEvaluations;
        total.matchingSiblings += value.matchingSiblings;
        total.formulaEvaluations += value.formulaEvaluations;
        total.rankCacheInitializations += value.rankCacheInitializations;
        total.rankCacheBuilds += value.rankCacheBuilds;
        total.rankCacheHits += value.rankCacheHits;
    }
#endif

    static constexpr std::size_t collectionBytes()
    {
        return sizeof(Collection);
    }

#ifndef GXOS_JS73_BASELINE
    static constexpr std::size_t scratchBytes()
    {
        return sizeof(NavigatorScriptHostAdapter::
            FilteredNthQueryScratchStorage<8u>);
    }

    template<std::size_t RankSlotCount>
    static constexpr std::size_t scratchBytesForSlots()
    {
        return sizeof(NavigatorScriptHostAdapter::
            FilteredNthQueryScratchStorage<RankSlotCount>);
    }
#endif
};

} // namespace gxos::javascript

namespace {

using namespace gxos::javascript;

std::size_t checks = 0u;
std::size_t failures = 0u;

void expect(bool condition, const char* label)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

bool runScript(NavigatorScriptExecutionHarness& harness,
    const std::string& source, const char* label)
{
    const ScriptResult result = harness.execute(source);
    const Value* value = harness.runtime().lookup(SourceView("ok", 2u));
    const bool success = result.succeeded() && value != nullptr &&
        value->isBoolean() && value->booleanValue();
    expect(success, label);
    if (!success) {
        std::cerr << "  status=" << scriptStatusName(result.status) << ", error="
            << runtimeErrorCodeName(result.runtimeError.code) << ", location="
            << result.runtimeError.location.line << ':'
            << result.runtimeError.location.column << '\n';
        for (const char* name : {"ok", "forward", "reverse", "candidate",
                "formulaOk", "reverseOk", "distinctFilter", "fourOk",
                "parentIsolation", "scopeOk", "closest", "bad",
                "liveBefore", "liveChanged", "liveRestored", "checkedOk",
                "checkedOrder", "focusOk", "stressCount", "formulaLengthOk",
                "formulaFirst", "formulaSecond", "formulaThird", "fourLengthOk",
                "fourFirst", "fourSecond", "fourThird", "fourFourth",
                "liveBeforeLength", "liveBeforeElement", "liveChangedLength",
                "liveChangedElement", "liveRestoredLength",
                "liveRestoredElement", "checkedLengthOk", "checkedElementOk",
                "alternatingScopes", "otherChanged", "otherRestored",
                "indexedRows", "indexedOrder", "nestedA", "nestedB",
                "nestedMetadata", "disabledRows", "disabledC",
                "formulaLength", "fourLength", "liveBeforeCount",
                "liveChangedCount", "liveRestoredCount", "checkedCount",
                "allLength", "allOk", "allId0", "allId1", "allId2",
                "allId3", "sparseLength", "sparseId", "eightLength"}) {
            const Value* debug = harness.runtime().lookup(SourceView(name,
                std::char_traits<char>::length(name)));
            if (debug == nullptr) continue;
            std::cerr << "  " << name << '=';
            if (debug->isBoolean())
                std::cerr << (debug->booleanValue() ? "true" : "false");
            else if (debug->isNumber()) std::cerr << debug->numberValue();
            else std::cerr << valueTypeName(debug->type());
            std::cerr << '\n';
        }
    }
    return success;
}

std::string sharedParentHtml(const std::string& mode,
    std::size_t childCount = 1021u)
{
    std::string html = "<html><body><div id='load'>";
    for (std::size_t index = 0u; index < childCount; ++index) {
        bool selected = false;
        if (mode == "all") selected = true;
        else if (mode == "alternating") selected = index % 2u == 0u;
        else if (mode == "one-early") selected = index == 0u;
        else if (mode == "one-late") selected = index + 1u == childCount;
        else if (mode == "four") selected = true;
        const std::string classes = selected
            ? mode == "four"
                ? "selected f" + std::to_string(index % 4u)
                : "selected"
            : "unselected";
        html += "<span id='s" + std::to_string(index) + "' class='" +
            classes + "'></span>";
    }
    html += "</div></body></html>";
    return html;
}

std::string manyParentHtml()
{
    std::string html = "<html><body><div id='groups'>";
    for (std::size_t index = 0u; index < 510u; ++index) {
        html += "<section><span class='";
        html += index % 2u == 0u ? "selected" : "unselected";
        html += "'></span></section>";
    }
    html += "</div><i id='capacity-fill'></i></body></html>";
    return html;
}

std::string alternatingScopeHtml()
{
    std::string html = "<html><body><section id='scope-a'>";
    for (std::size_t index = 0u; index < 64u; ++index)
        html += "<i id='a" + std::to_string(index) +
            "' class='selected'></i>";
    html += "</section><section id='scope-b'>";
    for (std::size_t index = 0u; index < 64u; ++index)
        html += "<i id='b" + std::to_string(index) +
            "' class='active'></i>";
    html += "</section></body></html>";
    return html;
}

bool loadFixture(NavigatorScriptExecutionHarness& harness,
    const std::string& html, const char* label)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js73-test.html", html,
        error) && error == RuntimeErrorCode::None;
    expect(loaded, label);
    return loaded;
}

void testSemantics()
{
    const std::string html = R"HTML(<html><body>
<section id="series" class="container">
<li id="a" class="selected active visible"></li>
<li id="b"></li>
<li id="c" class="selected visible"></li>
<li id="d" class="selected active"></li>
<li id="e" class="active"></li>
<li id="f" class="selected required"></li>
</section>
<section id="other"><li id="g" class="selected"></li>
<li id="h" class="selected"></li></section>
<section id="checks"><input id="check-a" type="checkbox">
<input id="check-b" type="checkbox">
<input id="disabled-c" type="checkbox" disabled></section>
<section id="radio-group"><input id="radio-a" type="radio" name="choice">
<input id="radio-b" type="radio" name="choice"></section>
<section id="event-buttons">
<button id="event-a" class="selected" type="button"></button>
<button id="event-b" class="selected" type="button"></button>
</section>
</body></html>)HTML";
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness, html, "JS73 semantic fixture loads")) return;

    runScript(harness, R"JS(
var series = document.getElementById('series');
var a = document.getElementById('a');
var b = document.getElementById('b');
var c = document.getElementById('c');
var d = document.getElementById('d');
var e = document.getElementById('e');
var f = document.getElementById('f');
var forward = document.querySelector(':nth-child(2 of .selected)') === c;
var reverse = document.querySelector(':nth-last-child(1 of .selected)') === f;
var candidate = !b.matches(':nth-child(2 of .selected)') &&
 c.matches(':nth-child(2 of .selected)');
var formula = document.querySelectorAll(
 '#series > :nth-child(-n+3 of .selected)');
var formulaLength = formula.length;
var formulaLengthOk = formulaLength === 3;
var formulaFirst = formula[0] === a;
var formulaSecond = formula[1] === c;
var formulaThird = formula[2] === d;
var formulaOk = formulaLengthOk && formulaFirst && formulaSecond && formulaThird;
var reverseFormula = document.querySelectorAll(
 ':nth-last-child(even of .active)');
var reverseOk = reverseFormula.length === 1 && reverseFormula[0] === d;
var distinctFilter = document.querySelector(
 ':nth-child(2 of .active)') === d;
var four = document.querySelectorAll(
 '#series > :nth-child(1 of .visible), ' +
 '#series > :nth-child(2 of .selected), ' +
 '#series > :nth-last-child(1 of .active), ' +
 '#series > :nth-last-child(1 of .required)');
var fourLength = four.length;
var fourLengthOk = fourLength === 4;
var fourFirst = four[0] === a;
var fourSecond = four[1] === c;
var fourThird = four[2] === e;
var fourFourth = four[3] === f;
var fourOk = fourLengthOk && fourFirst && fourSecond && fourThird && fourFourth;
var indexedRows = series.querySelectorAll(
 ':scope > :nth-child(n of .selected)');
var indexedOrder = indexedRows.length === 4 && indexedRows[0] === a &&
 indexedRows[2] === d && indexedRows[3] === f;
var otherRoot = document.getElementById('other');
var other = otherRoot.querySelectorAll(
 ':scope > :nth-child(n of .active)');
var parentIsolation = other.length === 0;
var otherA = document.querySelectorAll(
 '#other > :nth-child(n of .selected)');
var alternatingScopes = otherA.length === 2 && otherA[0] ===
 document.getElementById('g') && indexedRows[0] === a &&
 other[0] === undefined;
document.getElementById('g').setAttribute('class', 'selected active');
var otherChanged = other.length === 1 &&
 other[0] === document.getElementById('g') && indexedRows[0] === a;
document.getElementById('g').removeAttribute('class');
var otherRestored = other.length === 0 && indexedRows[0] === a;
var scoped = document.getElementById('series').querySelectorAll(
 ':scope > li:nth-child(odd of .selected)');
var scopeOk = scoped.length === 2 && scoped[0] === a && scoped[1] === d &&
 a.matches(':nth-child(1 of :scope)') &&
 !c.matches(':nth-child(2 of :scope)');
var closest = d.closest(':nth-child(3 of .selected)') === d;
var bad = document.querySelector(':nth-child(2 of .selected, .active)') === null &&
 document.querySelector(':nth-child(2 of .selected > .active)') === null;
var live = document.querySelectorAll(
 '#series > :nth-child(2 of .selected)');
var liveBeforeCount = live.length;
var liveBeforeLength = liveBeforeCount === 1;
var liveBeforeElement = live[0] === c;
var liveBefore = liveBeforeLength && liveBeforeElement;
b.setAttribute('class', 'selected');
var liveChangedCount = live.length;
var liveChangedLength = liveChangedCount === 1;
var liveChangedElement = live[0] === b;
var liveChanged = liveChangedLength && liveChangedElement;
b.removeAttribute('class');
var liveRestoredCount = live.length;
var liveRestoredLength = liveRestoredCount === 1;
var liveRestoredElement = live[0] === c;
var liveRestored = liveRestoredLength && liveRestoredElement;
var ok = forward && reverse && candidate && formulaOk && reverseOk &&
 distinctFilter && fourOk && parentIsolation && scopeOk && closest && bad &&
 liveBefore && liveChanged && liveRestored && alternatingScopes &&
 otherChanged && otherRestored && indexedOrder;
)JS", "JS73 golden selector, ordering, scope, candidate, grammar, and live parity");

    const HostInstanceId checkASerial = [&]() {
        for (const auto& element : harness.document().structuralElements)
            if (element.id == "check-a") return element.serial;
        return HostInstanceId{0u};
    }();
    RuntimeErrorCode error = RuntimeErrorCode::None;
    runScript(harness, R"JS(
var checkA = document.getElementById('check-a');
checkA.checked = true;
var checkedA = document.querySelectorAll(
 'input:nth-child(1 of :checked)');
var checkedCount = checkedA.length;
var checkedLengthOk = checkedCount === 1;
var checkedElementOk = checkedLengthOk && checkedA[0].id === 'check-a';
var checkedOk = checkedElementOk;
var ok = checkedOk;
)JS", "JS73 filtered checked state is recomputed after activation");
    runScript(harness, R"JS(
var checkB = document.getElementById('check-b');
checkB.checked = true;
var checkedB = document.querySelectorAll(
 'input:nth-child(2 of :checked)');
var checkedOrder = checkedB.length === 1 && checkedB[0].id === 'check-b';
var ok = checkedOrder;
)JS", "JS73 checked filtered reverse membership follows live state");
    error = RuntimeErrorCode::None;
    expect(harness.focusElement(checkASerial, error) &&
        error == RuntimeErrorCode::None,
        "JS73 focus state transition completes");
    runScript(harness, R"JS(
var focusMatch = document.querySelectorAll(
 'input:nth-child(1 of :focus)');
var focusOk = focusMatch.length === 1 && focusMatch[0].id === 'check-a';
var ok = focusOk;
)JS", "JS73 filtered focus state follows the active Element");

    runScript(harness, R"JS(
checkA.checked = false;
checkB.checked = false;
var retainedChecked = document.querySelectorAll(
 'input:nth-child(n of :checked)');
var initiallyEmpty = retainedChecked.length === 0;
checkA.checked = true;
var firstChecked = retainedChecked.length === 1 &&
 retainedChecked[0] === checkA;
checkB.checked = true;
var bothChecked = retainedChecked.length === 2 &&
 retainedChecked[0] === checkA && retainedChecked[1] === checkB;
checkA.checked = false;
var secondRemains = retainedChecked.length === 1 &&
 retainedChecked[0] === checkB;
var retainedFocus = document.querySelectorAll(
 'input:nth-child(n of :focus)');
checkA.focus();
var firstFocused = retainedFocus.length === 1 &&
 retainedFocus[0] === checkA;
checkB.focus();
var secondFocused = retainedFocus.length === 1 &&
 retainedFocus[0] === checkB;
var ok = initiallyEmpty && firstChecked && bothChecked && secondRemains &&
 firstFocused && secondFocused;
)JS", "JS73 retained checked and focus collections follow live state");
    runScript(harness, R"JS(
var radioA = document.getElementById('radio-a');
var radioB = document.getElementById('radio-b');
var radioRows = document.querySelectorAll(
 '#radio-group > input:nth-child(n of :checked)');
var initiallyEmpty = radioRows.length === 0;
radioA.checked = true;
var firstRadio = radioRows.length === 1 && radioRows[0] === radioA;
radioB.checked = true;
var radioMoved = radioRows.length === 1 && radioRows[0] === radioB &&
 radioA.checked === false;
var ok = initiallyEmpty && firstRadio && radioMoved;
)JS", "JS73 retained radio collection follows exclusive checked ownership");
    runScript(harness, R"JS(
var disabledRows = document.querySelectorAll(
 'input:nth-child(n of :disabled)');
var disabledC = document.getElementById('disabled-c');
var ok = disabledRows.length === 1 && disabledRows[0] === disabledC;
)JS", "JS73 supported disabled-state collection reads");

    runScript(harness, R"JS(
var nestedA = false;
var nestedB = false;
var nestedMetadata = false;
var eventAElement = document.getElementById('event-a');
var eventBElement = document.getElementById('event-b');
eventAElement.addEventListener('click', function(event) {
 var target = event.target;
 var rows = document.querySelectorAll(
  '#event-buttons > :nth-child(n of .selected)');
 var beforeA = rows[0];
 var beforeB = rows[1];
 eventBElement.click();
 var after = document.querySelectorAll(
  '#event-buttons > :nth-child(n of .selected)');
 nestedA = beforeA === eventAElement && beforeB === eventBElement &&
  after.length === 2 &&
  after[0] === beforeA && after[1] === beforeB &&
  rows.length === 2 && rows[0] === beforeA && rows[1] === beforeB &&
  target === event.target;
 nestedMetadata = event.currentTarget === target && event.eventPhase === 2;
});
eventBElement.addEventListener('click', function(event) {
 var rows = document.querySelectorAll(
  '#event-buttons > :nth-child(2 of .selected)');
 nestedB = rows.length === 1 && rows[0] === eventBElement &&
  event.target === eventBElement &&
  event.currentTarget === event.target && event.eventPhase === 2;
});
var ok = true;
)JS", "JS73 nested Event handlers issue independently scoped bulk queries");
    error = RuntimeErrorCode::None;
    HostInstanceId eventASerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventASerial = element.serial;
    expect(eventASerial != 0u && harness.dispatchClick(eventASerial, error) &&
        error == RuntimeErrorCode::None,
        "JS73 nested Event selector queries complete");
    const Value* nestedA = harness.runtime().lookup(SourceView("nestedA", 7u));
    const Value* nestedB = harness.runtime().lookup(SourceView("nestedB", 7u));
    const Value* nestedMetadata = harness.runtime().lookup(
        SourceView("nestedMetadata", 14u));
    expect(nestedA != nullptr && nestedA->isBoolean() && nestedA->booleanValue() &&
        nestedB != nullptr && nestedB->isBoolean() && nestedB->booleanValue() &&
        nestedMetadata != nullptr && nestedMetadata->isBoolean() &&
        nestedMetadata->booleanValue(),
        "JS73 nested scratch ownership preserves outer Event results");
    if (nestedA == nullptr || !nestedA->isBoolean() || !nestedA->booleanValue() ||
        nestedB == nullptr || !nestedB->isBoolean() || !nestedB->booleanValue() ||
        nestedMetadata == nullptr || !nestedMetadata->isBoolean() ||
        !nestedMetadata->booleanValue()) {
        std::cerr << "  event-error=" << runtimeErrorCodeName(error)
            << ", serial=" << eventASerial << ", nestedA="
            << (nestedA != nullptr && nestedA->isBoolean() ?
                (nestedA->booleanValue() ? "true" : "false") : "missing")
            << ", nestedB="
            << (nestedB != nullptr && nestedB->isBoolean() ?
                (nestedB->booleanValue() ? "true" : "false") : "missing")
            << ", metadata="
            << (nestedMetadata != nullptr && nestedMetadata->isBoolean() ?
                (nestedMetadata->booleanValue() ? "true" : "false") :
                    "missing") << '\n';
    }
}

void testCompactScratchSlots()
{
    std::string html = "<html><body><div id='root'>";
    std::string selector;
    for (std::size_t index = 0u; index < 4u; ++index) {
        html += "<section id='p" + std::to_string(index) +
            "' class='selected f" + std::to_string(index) +
            "'><i id='i" + std::to_string(index) +
            "' class='selected'></i></section>";
        if (index != 0u) selector += ", ";
        selector += ":nth-child(1 of .selected.f" +
            std::to_string(index) + ")";
    }
    html += "</div></body></html>";

    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness, html,
            "JS73 logical-rank-slot fixture loads")) return;
    std::string source = "var all=document.querySelectorAll('" + selector +
        "');var allLength=all.length;var allOk=allLength===4";
    for (std::size_t index = 0u; index < 4u; ++index)
        source += ";var allId" + std::to_string(index) + "=all[" +
            std::to_string(index) + "].id;allOk=allOk&&allId" +
            std::to_string(index) + "==='p" + std::to_string(index) + "'";
    source += ";var sparse=document.querySelectorAll("
        "'.missing, .nope, .absent, :nth-child(1 of .selected.f2)');"
        "var sparseLength=sparse.length;var sparseId=sparse[0].id;";
    const std::string fullPoolMember =
        ":nth-child(1 of .selected)>:nth-child(1 of .selected)";
    const std::string fullPoolSelector = fullPoolMember + "," +
        fullPoolMember + "," + fullPoolMember + "," + fullPoolMember;
    source += "var eight=document.querySelectorAll('" + fullPoolSelector +
        "');var eightLength=eight.length;var ok=allOk&&sparseLength===1&&"
        "sparseId==='p2'&&eightLength===1&&eight[0].id==='i0';";
    runScript(harness, source,
        "JS73 sparse logical rank slots preserve order");

    const auto* fourSlots =
        NavigatorScriptRelativeSelectorTestAccess::collectionForRankSlotCount(
            harness.hostAdapter(), 4u);
    const auto* eightSlots =
        NavigatorScriptRelativeSelectorTestAccess::collectionForRankSlotCount(
            harness.hostAdapter(), 8u);
    const auto* oneSlot =
        NavigatorScriptRelativeSelectorTestAccess::collectionForRankSlotCount(
            harness.hostAdapter(), 1u);
    expect(fourSlots != nullptr && oneSlot != nullptr && eightSlots != nullptr,
        "JS73 one, four, eight, and sparse logical rank slots are retained");
    expect(NavigatorScriptRelativeSelectorTestAccess::
            scratchBytesForSlots<1u>() == 4232u &&
        NavigatorScriptRelativeSelectorTestAccess::
            scratchBytesForSlots<4u>() == 10376u &&
        NavigatorScriptRelativeSelectorTestAccess::scratchBytes() == 18568u,
        "JS73 scratch storage scales with active filtered descriptors");
}

void testHostedFixtureSourceIntegrity()
{
    std::ifstream fixtureFile("navigator-smoke/javascript-js73.html",
        std::ios::binary);
    const std::string fixtureHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = fixtureHtml.find("<script>");
    const std::size_t scriptClose = fixtureHtml.find("</script>", scriptOpen);
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = !fixtureHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        harness.loadHtml("file:///js73-hosted-fixture.html", fixtureHtml,
            error) && error == RuntimeErrorCode::None;
    expect(loaded, "JS73 hosted JavaScript fixture loads in native harness");
    if (!loaded) return;

    const std::string source = fixtureHtml.substr(scriptOpen + 8u,
        scriptClose - scriptOpen - 8u);
    const auto& capturedScripts = harness.document().scriptSources;
    const bool preserved = capturedScripts.size() == 1u &&
        capturedScripts[0] == source;
    expect(preserved,
        "JS73 hosted inline script is captured byte-for-byte");
    if (!preserved) return;

    const ScriptResult result = harness.execute(capturedScripts[0]);
    const Value* assertions = harness.runtime().lookup(
        SourceView("assertions", 10u));
    const Value* fixtureFailures = harness.runtime().lookup(
        SourceView("failures", 8u));
    if (!result.succeeded() || assertions == nullptr ||
        !assertions->isNumber() || assertions->numberValue() != 35.0 ||
        fixtureFailures == nullptr || !fixtureFailures->isNumber() ||
        fixtureFailures->numberValue() != 0.0) {
        std::cerr << "  hosted-fixture status=" <<
            scriptStatusName(result.status) << ", error=" <<
            runtimeErrorCodeName(result.runtimeError.code) << ", assertions=";
        if (assertions != nullptr && assertions->isNumber())
            std::cerr << assertions->numberValue();
        else std::cerr << "missing";
        std::cerr << ", failures=";
        if (fixtureFailures != nullptr && fixtureFailures->isNumber())
            std::cerr << fixtureFailures->numberValue();
        else std::cerr << "missing";
        std::cerr << '\n';
    }
    expect(result.succeeded() && assertions != nullptr &&
        assertions->isNumber() && assertions->numberValue() == 35.0 &&
        fixtureFailures != nullptr && fixtureFailures->isNumber() &&
        fixtureFailures->numberValue() == 0.0,
        "JS73 hosted fixture initial assertions pass after intact extraction");
}

void testMaximumCapacityAndCounters()
{
    NavigatorScriptExecutionHarness harness;
    const std::string html = sharedParentHtml("alternating");
    if (!loadFixture(harness, html,
            "JS73 1,024-Element shared-parent fixture loads")) return;
    expect(harness.document().structuralElements.size() == 1024u,
        "JS73 fixture reaches all 1,024 structural Elements");
    runScript(harness,
        "var stress = document.querySelectorAll("
        "':nth-child(odd of .selected)');"
        "var stressCount = stress.length;"
        "var ok = stressCount === 256 && stress[0].id === 's0' && "
        "stress[255].id === 's1020';",
        "JS73 maximum-capacity alternating filtered ranks preserve identities");

    const std::string selector = ":nth-child(odd of .selected)";
    const auto* record = NavigatorScriptRelativeSelectorTestAccess::collectionFor(
        harness.hostAdapter(), selector);
    expect(record != nullptr,
        "JS73 diagnostic collection retains its descriptor");
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    if (record != nullptr) {
        NavigatorScriptFilteredNthCounters counters;
        const std::size_t resultCount =
            NavigatorScriptRelativeSelectorTestAccess::countWithDiagnostics(
                harness.hostAdapter(), *record, counters);
        expect(resultCount == 256u,
            "JS73 diagnostic bulk evaluation returns exact alternating count");
#ifdef GXOS_JS73_BASELINE
        expect(counters.parentResolutions == 1021u &&
            counters.structuralInspections == 1021u * 1024u &&
            counters.siblingInspections == 1021u * 1021u &&
            counters.simpleSelectorEvaluations == 1021u * 1021u &&
            counters.formulaEvaluations == 511u,
            "JS73 baseline diagnostics record repeated JS71 scans");
#else
        expect(counters.parentResolutions == 3u &&
            counters.structuralInspections <= 2u * 1024u &&
            counters.siblingInspections <= 2u * 1023u &&
            counters.simpleSelectorEvaluations == 1023u &&
            counters.matchingSiblings == 511u &&
            counters.formulaEvaluations == 511u &&
            counters.rankCacheInitializations == 1u &&
            counters.rankCacheBuilds == 1u &&
            counters.rankCacheHits == 1023u,
            "JS73 bulk ranking evaluates each same-parent filter once");
#endif
        std::cout << "JS73 counters: parent-resolutions=" <<
            counters.parentResolutions << ", structural-inspections=" <<
            counters.structuralInspections << ", sibling-inspections=" <<
            counters.siblingInspections << ", filter-evaluations=" <<
            counters.simpleSelectorEvaluations << ", matching-siblings=" <<
            counters.matchingSiblings << ", formula-evaluations=" <<
            counters.formulaEvaluations;
#ifndef GXOS_JS73_BASELINE
        std::cout << ", rank-cache-initializations=" <<
            counters.rankCacheInitializations << ", rank-cache-builds=" <<
            counters.rankCacheBuilds << ", rank-cache-hits=" <<
            counters.rankCacheHits;
#endif
        std::cout << '\n';
    }
#endif
}

void testRepeatedIndexedReadCounters()
{
    constexpr std::size_t elementCount = 1021u;
    const std::string selector = ":nth-child(n of .selected)";
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness, sharedParentHtml("all"),
            "JS73 indexed-read maximum-capacity fixture loads")) return;
    runScript(harness, "var rows=document.querySelectorAll('" + selector +
        "');var ok=true;", "JS73 indexed-read collection is retained");
    const auto* record = NavigatorScriptRelativeSelectorTestAccess::collectionFor(
        harness.hostAdapter(), selector);
    expect(record != nullptr,
        "JS73 indexed-read diagnostic collection retains its descriptor");
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    if (record == nullptr) return;
    constexpr std::size_t indices[] = {0u, 512u, 1020u, 1021u};
    NavigatorScriptFilteredNthCounters total;
    std::uint64_t candidateScans = 0u;
    for (std::size_t read = 0u; read < sizeof(indices) / sizeof(indices[0]);
            ++read) {
        NavigatorScriptFilteredNthCounters current;
        HostInstanceId serial = 0u;
        const bool found =
            NavigatorScriptRelativeSelectorTestAccess::matchAtWithDiagnostics(
                harness.hostAdapter(), *record, indices[read], serial,
                current);
        const bool expectedFound = indices[read] < elementCount;
        expect(found == expectedFound,
            "JS73 first/middle/last/out-of-range indexed read result");
        if (found) {
            const auto& element = harness.document().structuralElements[
                static_cast<std::size_t>(serial - 1u)];
            expect(element.serial == serial && &element ==
                    &harness.document().structuralElements[
                        static_cast<std::size_t>(serial - 1u)] &&
                element.id == "s" + std::to_string(indices[read]),
                "JS73 indexed read preserves canonical identity and order");
            candidateScans += static_cast<std::uint64_t>(
                static_cast<std::size_t>(&element -
                    harness.document().structuralElements.data()) + 1u);
        } else {
            expect(serial == 0u,
                "JS73 out-of-range indexed read clears its serial");
            candidateScans += harness.document().structuralElements.size();
        }
        NavigatorScriptRelativeSelectorTestAccess::addCounters(total, current);
    }
    expect(total.rankCacheInitializations == 4u &&
        total.rankCacheBuilds == 4u && total.structuralInspections ==
            4u * 2u * harness.document().structuralElements.size(),
        "JS73 independent indexed reads rebuild bounded invocation-local ranks");
    std::cout << "JS73 indexed counters: reads=4, query-candidate-scans=" <<
        candidateScans << ", structural-inspections=" <<
        total.structuralInspections << ", parent-resolutions=" <<
        total.parentResolutions << ", sibling-inspections=" <<
        total.siblingInspections << ", filter-evaluations=" <<
        total.simpleSelectorEvaluations << ", rank-builds=" <<
        total.rankCacheBuilds << ", rank-lookups=" << total.rankCacheHits
        << '\n';
#endif
}

void testMaximumCapacityVariants()
{
    const auto runSharedVariant = [](const char* label, const char* mode,
            const std::string& selector, std::size_t expectedCount,
            const char* firstId, bool fromEnd) {
        NavigatorScriptExecutionHarness harness;
        if (!loadFixture(harness, sharedParentHtml(mode), label)) return;
        std::string source = "var rows=document.querySelectorAll('" + selector +
            "');var ok=rows.length===" + std::to_string(expectedCount);
        if (firstId != nullptr)
            source += "&&rows.length===1&&rows[0].id==='" +
                std::string(firstId) + "'";
        source += ";";
        runScript(harness, source, label);
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
        const auto* record =
            NavigatorScriptRelativeSelectorTestAccess::collectionFor(
                harness.hostAdapter(), selector);
        expect(record != nullptr, "JS73 capacity variant descriptor is retained");
        if (record != nullptr) {
            NavigatorScriptFilteredNthCounters counters;
            const std::size_t count =
                NavigatorScriptRelativeSelectorTestAccess::countWithDiagnostics(
                    harness.hostAdapter(), *record, counters);
            expect(count == expectedCount,
                "JS73 capacity variant diagnostic result count matches");
#ifndef GXOS_JS73_BASELINE
            const std::uint64_t expectedInspections = fromEnd
                ? 3u * 1024u : 2u * 1024u;
            expect(counters.parentResolutions == 3u &&
                counters.structuralInspections == expectedInspections &&
                counters.simpleSelectorEvaluations == 1023u &&
                counters.rankCacheInitializations == 1u &&
                counters.rankCacheBuilds == 1u,
                "JS73 capacity variant builds one fixed rank table");
#endif
        }
#else
        static_cast<void>(fromEnd);
#endif
    };
    runSharedVariant("JS73 all-selected 1,024-Element fixture",
        "all", ":nth-child(odd of .selected)", 511u, nullptr, false);
    runSharedVariant("JS73 empty filtered set at capacity",
        "none", ":nth-child(odd of .selected)", 0u, nullptr, false);
    runSharedVariant("JS73 one early matching sibling at capacity",
        "one-early", ":nth-child(1 of .selected)", 1u, "s0", false);
    runSharedVariant("JS73 one late matching sibling at capacity",
        "one-late", ":nth-child(1 of .selected)", 1u, "s1020", false);
    runSharedVariant("JS73 reverse rank at maximum capacity",
        "all", ":nth-last-child(odd of .selected)", 511u, nullptr, true);

    NavigatorScriptExecutionHarness groups;
    if (loadFixture(groups, manyParentHtml(),
            "JS73 many-parent 1,024-Element fixture loads")) {
        expect(groups.document().structuralElements.size() == 1024u,
            "JS73 many-parent fixture reaches structural capacity");
        runScript(groups,
            "var rows=document.querySelectorAll("
            "'section > :nth-child(odd of .selected)');"
            "var ok=rows.length===255;",
            "JS73 many-parent groups keep rank state isolated");
#ifndef GXOS_JS73_BASELINE
        const std::string groupsSelector =
            "section > :nth-child(odd of .selected)";
        const auto* groupsRecord =
            NavigatorScriptRelativeSelectorTestAccess::collectionFor(
                groups.hostAdapter(), groupsSelector);
        expect(groupsRecord != nullptr,
            "JS73 many-parent diagnostic collection retains its descriptor");
        if (groupsRecord != nullptr) {
            NavigatorScriptFilteredNthCounters counters;
            const std::size_t resultCount =
                NavigatorScriptRelativeSelectorTestAccess::countWithDiagnostics(
                    groups.hostAdapter(), *groupsRecord, counters);
            expect(resultCount == 255u &&
                counters.rankCacheInitializations == 1u &&
                counters.rankCacheBuilds == 1u &&
                counters.simpleSelectorEvaluations == 1023u &&
                counters.matchingSiblings == 255u,
                "JS73 many-parent groups build one bounded rank table");
            std::cout << "JS73 many-parent counters: parent-resolutions=" <<
                counters.parentResolutions << ", structural-inspections=" <<
                counters.structuralInspections << ", sibling-inspections=" <<
                counters.siblingInspections << ", filter-evaluations=" <<
                counters.simpleSelectorEvaluations <<
                ", matching-siblings=" << counters.matchingSiblings <<
                ", formula-evaluations=" << counters.formulaEvaluations <<
                ", rank-cache-initializations=" <<
                counters.rankCacheInitializations << ", rank-cache-builds=" <<
                counters.rankCacheBuilds << ", rank-cache-hits=" <<
                counters.rankCacheHits << '\n';
        }
#endif
    }

    NavigatorScriptExecutionHarness singleton;
    if (loadFixture(singleton,
            "<html><body><div><span id='only' class='selected'></span>"
            "</div></body></html>",
            "JS73 single direct-child fixture loads")) {
        runScript(singleton,
            "var rows=document.querySelectorAll("
            "':nth-child(odd of .selected)');"
            "var ok=rows.length===1&&rows[0].id==='only';",
            "JS73 one-child filtered query returns its canonical Element");
    }
}

void testInvalidStructuresAndGenerationReuse()
{
    const std::string html =
        "<html><body><div id='load'><span id='selected' "
        "class='selected'></span></div></body></html>";
    NavigatorScriptExecutionHarness invalid;
    if (!loadFixture(invalid, html,
            "JS73 invalid-structure fixture loads")) return;
    runScript(invalid, R"JS(
var retained = document.querySelectorAll(':nth-child(1 of .selected)');
var retainedElement = retained[0];
var ok = retained.length === 1 && retainedElement.id === 'selected';
)JS", "JS73 retained collection captures the initial rank");
    for (auto& element : invalid.document().structuralElements)
        if (element.id == "selected") element.childIndex = 2u;
    runScript(invalid, R"JS(
var ok = retained.length === 0;
)JS", "JS73 invalid sibling structure cannot reuse a previous rank");
    for (auto& element : invalid.document().structuralElements)
        if (element.id == "selected") element.childIndex = 1u;
    runScript(invalid, R"JS(
var ok = retained.length === 1 && retained[0] === retainedElement;
)JS", "JS73 repaired live structure rebuilds its rank table");

    const HostGenerationId oldGeneration = invalid.hostAdapter().generation();
    const Value* collectionValue = invalid.runtime().lookup(
        SourceView("retained", 8u));
    expect(collectionValue != nullptr && collectionValue->isHostObject(),
        "JS73 old-generation filtered collection handle is retained");
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool replaced = invalid.replaceHtml("file:///js73-replacement.html",
        html, error) && error == RuntimeErrorCode::None;
    expect(replaced && invalid.hostAdapter().generation() != oldGeneration,
        "JS73 document generation replacement reuses structural serials");
    const HostObjectReference oldCollection{1u, oldGeneration,
        kNavigatorSelectorCollectionHostKind};
    expect(invalid.hostAdapter().validate(oldCollection).code ==
            HostResultCode::StaleObject,
        "JS73 old rank collection cannot cross a generation boundary");
    runScript(invalid, R"JS(
var fresh = document.querySelectorAll(':nth-child(1 of .selected)');
var ok = fresh.length === 1 && fresh[0].id === 'selected';
)JS", "JS73 reused serials receive fresh filtered ranks");
}

struct TimingSample {
    const char* label;
    std::string html;
    std::string source;
};

void benchmarkCase(const TimingSample& sample)
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    if (!harness.loadHtml("file:///js73-benchmark.html", sample.html, error) ||
        error != RuntimeErrorCode::None) {
        std::cout << "JS73 timing " << sample.label << " fixture=FAIL\n";
        return;
    }
    std::vector<std::int64_t> values;
    values.reserve(5u);
    for (std::size_t iteration = 0u; iteration < 5u; ++iteration) {
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(sample.source);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
        const Value* resultValue = harness.runtime().lookup(SourceView("ok", 2u));
        if (!result.succeeded() || resultValue == nullptr ||
            !resultValue->isBoolean() || !resultValue->booleanValue()) {
            std::cout << "JS73 timing " << sample.label << " result=FAIL, status="
                << scriptStatusName(result.status) << ", error="
                << runtimeErrorCodeName(result.runtimeError.code) << '\n';
            return;
        }
        values.push_back(elapsed);
    }
    std::sort(values.begin(), values.end());
    std::cout << "JS73 timing " << sample.label << ": median-us=" <<
        values[values.size() / 2u] << ", min-us=" << values.front() <<
        ", max-us=" << values.back() << ", repetitions=" << values.size()
        << '\n';
}

std::string indexedReadSource(const std::string& selector,
    const std::vector<std::size_t>& indices, bool outOfRange = false)
{
    std::string source = "var rows=document.querySelectorAll('" + selector +
        "');var ok=true;";
    for (std::size_t index : indices) {
        if (outOfRange) {
            source += "ok=ok&&rows[" + std::to_string(index) +
                "]===undefined;";
        } else {
            source += "ok=ok&&rows[" + std::to_string(index) +
                "].id==='s" + std::to_string(index) + "';";
        }
    }
    return source;
}

void runIndexedReadBenchmarks()
{
    const std::string selector = ":nth-child(n of .selected)";
    const std::string full = sharedParentHtml("all");
    const std::string small = sharedParentHtml("all", 64u);
    const std::string sequential = sharedParentHtml("all", 512u);
    const std::string one = sharedParentHtml("one-early", 32u);
    std::vector<std::size_t> first(64u, 0u);
    std::vector<std::size_t> middle(64u, 510u);
    std::vector<std::size_t> last(64u, 1020u);
    std::vector<std::size_t> outOfRange(64u, 1021u);
    std::vector<std::size_t> random;
    random.reserve(64u);
    for (std::size_t index = 0u; index < 64u; ++index)
        random.push_back((index * 197u + 31u) % 1021u);
    std::vector<std::size_t> smallSequential;
    for (std::size_t index = 0u; index < 32u; ++index)
        smallSequential.push_back(index);
    std::string sequentialSource = "var rows=document.querySelectorAll('" +
        selector + "');";
    for (std::size_t index = 0u; index < 512u; ++index)
        sequentialSource += "rows[" + std::to_string(index) + "];";
    sequentialSource += "var ok=true;";
    std::string alternatingSource =
        "var rootA=document.getElementById('scope-a');"
        "var rootB=document.getElementById('scope-b');"
        "var rowsA=rootA.querySelectorAll("
            "':scope > :nth-child(n of .selected)');"
        "var rowsB=rootB.querySelectorAll("
            "':scope > :nth-child(n of .active)');"
        "var ok=rowsA.length===64&&rowsB.length===64;";
    for (std::size_t index = 0u; index < 32u; ++index)
        alternatingSource += "ok=ok&&rowsA[" + std::to_string(index) +
            "].id==='a" + std::to_string(index) + "'&&rowsB[" +
            std::to_string(index) + "].id==='b" + std::to_string(index) + "';";
    const std::string liveMutation =
        "var rows=document.querySelectorAll(':nth-child(n of .selected)');"
        "var first=rows[0];"
        "document.getElementById('s0').setAttribute('class','unselected');"
        "var changed=rows[0];"
        "document.getElementById('s0').setAttribute('class','selected');"
        "var restored=rows[0];"
        "var ok=first.id==='s0'&&changed.id==='s1'&&restored.id==='s0';";
    const std::string liveCheckedFocus =
        "var a=document.getElementById('check-a');"
        "var b=document.getElementById('check-b');"
        "var checked=document.querySelectorAll("
        "'input:nth-child(n of :checked)');"
        "var focus=document.querySelectorAll('input:nth-child(n of :focus)');"
        "a.checked=false;b.checked=false;a.checked=true;"
        "var first=checked.length===1&&checked[0]===a;"
        "b.checked=true;"
        "var both=checked.length===2&&checked[0]===a&&checked[1]===b;"
        "a.checked=false;"
        "var remaining=checked.length===1&&checked[0]===b;"
        "a.focus();var focusA=focus.length===1&&focus[0]===a;"
        "b.focus();var focusB=focus.length===1&&focus[0]===b;"
        "var ok=first&&both&&remaining&&focusA&&focusB;";
    const std::string liveStateHtml =
        "<html><body><section><input id='check-a' type='checkbox'>"
        "<input id='check-b' type='checkbox'></section></body></html>";

    benchmarkCase({"indexed-one-match-first", one,
        indexedReadSource(":nth-child(1 of .selected)", {0u})});
    benchmarkCase({"indexed-32-matches-sequential", small,
        indexedReadSource(selector, smallSequential)});
    benchmarkCase({"indexed-512-matches-sequential", sequential,
        sequentialSource});
    benchmarkCase({"indexed-large-repeat-first", full,
        indexedReadSource(selector, first)});
    benchmarkCase({"indexed-large-repeat-middle", full,
        indexedReadSource(selector, middle)});
    benchmarkCase({"indexed-large-repeat-final", full,
        indexedReadSource(selector, last)});
    benchmarkCase({"indexed-large-repeat-out-of-range", full,
        indexedReadSource(selector, outOfRange, true)});
    benchmarkCase({"indexed-large-deterministic-random", full,
        indexedReadSource(selector, random)});
    benchmarkCase({"indexed-alternating-scoped-collections",
        alternatingScopeHtml(), alternatingSource});
    benchmarkCase({"indexed-live-attribute-mutation", full, liveMutation});
    benchmarkCase({"indexed-live-checked-focus-mutation", liveStateHtml,
        liveCheckedFocus});
}

void runBenchmarks()
{
    const std::string sameParent = sharedParentHtml("all");
    const std::string alternating = sharedParentHtml("alternating");
    const std::string empty = sharedParentHtml("none");
    const std::string late = sharedParentHtml("one-late");
    const std::string four = sharedParentHtml("four");
    const std::string small =
        "<html><body><div><span class='selected'></span></div></body></html>";
    const std::string sameParentQuery =
        "var rows = document.querySelectorAll("
        "':nth-child(odd of .selected)');var ok = rows.length === 511;";
    const std::string repeatedQuery =
        "var rowsA = document.querySelectorAll("
        "':nth-child(odd of .selected)').length;"
        "var rowsB = document.querySelectorAll("
        "':nth-child(odd of .selected)').length;"
        "var ok = rowsA === 511 && rowsB === 511;";
    const std::string manyParents = manyParentHtml();
    std::vector<TimingSample> samples;
    samples.push_back({"same-parent-1024-all-selected", sameParent,
        sameParentQuery});
    samples.push_back({"same-parent-1024-alternating", alternating,
        "var rows = document.querySelectorAll("
        "':nth-child(odd of .selected)');var ok = rows.length === 256;"});
    samples.push_back({"same-parent-1024-no-selected", empty,
        "var rows = document.querySelectorAll("
        "':nth-child(odd of .selected)');var ok = rows.length === 0;"});
    samples.push_back({"same-parent-1024-late-match", late,
        "var row = document.querySelectorAll("
        "':nth-child(1 of .selected)');"
        "var ok = row.length === 1 && row[0].id === 's1020';"});
    samples.push_back({"many-parent-groups-1024", manyParents,
        "var rows = document.querySelectorAll("
        "'section > :nth-child(odd of .selected)');"
        "var ok = rows.length === 255;"});
    samples.push_back({"one-direct-child", small,
        "var rows = document.querySelectorAll("
        "':nth-child(odd of .selected)');var ok = rows.length === 1;"});
    samples.push_back({"repeated-qsa-two-reads", sameParent, repeatedQuery});
    samples.push_back({"matches-single-candidate", sameParent,
        "var target = document.getElementById('s1020');"
        "var ok = target.matches(':nth-child(1021 of .selected)');"});
    samples.push_back({"closest-ancestor-traversal", sameParent,
        "var target = document.getElementById('s1020');"
        "var ok = target.closest(':nth-last-child(1 of .selected)') === target;"});
    samples.push_back({"nth-last-child-reverse", sameParent,
        "var rows = document.querySelectorAll("
        "':nth-last-child(odd of .selected)');var ok = rows.length === 511;"});
    samples.push_back({"four-member-outer-list", four,
        "var rows = document.querySelectorAll("
        "':nth-child(odd of .f0),:nth-child(odd of .f1),"
        ":nth-last-child(even of .f2),:nth-child(-n+3 of .f3)');"
        "var ok = rows.length === 386 && rows[0].id === 's0' && "
        "rows[1].id === 's1' && rows[2].id === 's3' && "
        "rows[3].id === 's6' && rows[4].id === 's7' && "
        "rows[5].id === 's8' && rows[385].id === 's1017';"});
    for (const TimingSample& sample : samples) benchmarkCase(sample);
    runIndexedReadBenchmarks();
}

} // namespace

int main(int argc, char** argv)
{
    using namespace gxos::javascript;
    static_assert(sizeof(NavigatorScriptSelectorMatchContext) == 16u,
        "JS73 preserves the JS70 selector match context");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1918u,
        "JS73 preserves the compact selector descriptor");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::collectionBytes()
            == 1936u,
        "JS73 preserves the compact collection record");
    static_assert(kNavigatorScriptMaxSelectorLength == 256u &&
        kNavigatorScriptMaxSelectorListMembers == 4u &&
        kNavigatorScriptMaxHasRelativeCores == 24u &&
        kNavigatorScriptMaxSelectorCollections == 128u &&
        kNavigatorScriptMaxDocumentNodes == 1024u,
        "JS73 preserves all established selector and structure capacities");
#ifndef GXOS_JS73_BASELINE
    static_assert(NavigatorScriptRelativeSelectorTestAccess::scratchBytes() <=
            24u * 1024u,
        "JS73 query scratch remains within its fixed 24 KiB bound");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
            scratchBytesForSlots<1u>() <= 6u * 1024u &&
        NavigatorScriptRelativeSelectorTestAccess::
            scratchBytesForSlots<4u>() <= 12u * 1024u,
        "JS73 active-slot scratch scales with filtered descriptors");
#endif

    const bool benchmarkOnly = argc > 1 &&
        std::string(argv[1]) == "--bench-only";
    if (!benchmarkOnly) {
        testSemantics();
        testCompactScratchSlots();
        testHostedFixtureSourceIntegrity();
        testMaximumCapacityAndCounters();
        testRepeatedIndexedReadCounters();
        testMaximumCapacityVariants();
        testInvalidStructuresAndGenerationReuse();
        std::cout << "JS73 memory: descriptor=" <<
            sizeof(NavigatorScriptSelectorDescriptor) <<
            ", collection=" <<
            NavigatorScriptRelativeSelectorTestAccess::collectionBytes() <<
            ", registry=" <<
                NavigatorScriptRelativeSelectorTestAccess::collectionBytes() *
                    kNavigatorScriptMaxSelectorCollections <<
            ", structural-element=" << sizeof(gxos::web::HtmlElementRef) <<
            ", content-metadata=" <<
                sizeof(gxos::web::HtmlElementContentMetadata);
#ifndef GXOS_JS73_BASELINE
        std::cout << ", call-local-scratch=" <<
            NavigatorScriptRelativeSelectorTestAccess::scratchBytes() <<
            ", scratch-1-slot=" <<
            NavigatorScriptRelativeSelectorTestAccess::
                scratchBytesForSlots<1u>() << ", scratch-4-slots=" <<
            NavigatorScriptRelativeSelectorTestAccess::
                scratchBytesForSlots<4u>();
#endif
        std::cout << '\n';
        std::cout << "Navigator JavaScript JS73 checks: " << checks - failures
            << '/' << checks << " passed\n";
    }
    runBenchmarks();
    return failures == 0u ? 0 : 1;
}
