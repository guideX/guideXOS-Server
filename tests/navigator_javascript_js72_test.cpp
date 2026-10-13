#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptRelativeSelectorTestAccess {
    using Collection = NavigatorScriptHostAdapter::SelectorCollectionRecord;

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
#ifdef GXOS_JS72_BASELINE
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
#endif

    static constexpr std::size_t collectionBytes()
    {
        return sizeof(Collection);
    }

#ifndef GXOS_JS72_BASELINE
    static constexpr std::size_t scratchBytes()
    {
        return sizeof(NavigatorScriptHostAdapter::
            FilteredNthQueryScratchStorage<8u>);
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
            << runtimeErrorCodeName(result.runtimeError.code) << '\n';
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
                "formulaLength", "fourLength", "liveBeforeCount",
                "liveChangedCount", "liveRestoredCount", "checkedCount"}) {
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

bool loadFixture(NavigatorScriptExecutionHarness& harness,
    const std::string& html, const char* label)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js72-test.html", html,
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
<input id="check-b" type="checkbox"></section>
<button id="event-a" type="button"></button>
<button id="event-b" type="button"></button>
</body></html>)HTML";
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness, html, "JS72 semantic fixture loads")) return;

    runScript(harness, R"JS(
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
var other = document.querySelectorAll('#other > :nth-child(2 of .selected)');
var parentIsolation = other.length === 1 && other[0].id === 'h';
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
 liveBefore && liveChanged && liveRestored;
)JS", "JS72 golden selector, ordering, scope, candidate, grammar, and live parity");

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
)JS", "JS72 filtered checked state is recomputed after activation");
    runScript(harness, R"JS(
var checkB = document.getElementById('check-b');
checkB.checked = true;
var checkedB = document.querySelectorAll(
 'input:nth-child(2 of :checked)');
var checkedOrder = checkedB.length === 1 && checkedB[0].id === 'check-b';
var ok = checkedOrder;
)JS", "JS72 checked filtered reverse membership follows live state");
    error = RuntimeErrorCode::None;
    expect(harness.focusElement(checkASerial, error) &&
        error == RuntimeErrorCode::None,
        "JS72 focus state transition completes");
    runScript(harness, R"JS(
var focusMatch = document.querySelectorAll(
 'input:nth-child(1 of :focus)');
var focusOk = focusMatch.length === 1 && focusMatch[0].id === 'check-a';
var ok = focusOk;
)JS", "JS72 filtered focus state follows the active Element");

    runScript(harness, R"JS(
var nestedA = false;
var nestedB = false;
var nestedMetadata = false;
document.getElementById('event-a').addEventListener('click', function(event) {
 var target = event.target;
 var before = document.querySelectorAll(
  'button:nth-child(1 of :scope)').length;
 document.getElementById('event-b').click();
 var after = document.querySelectorAll(
  'button:nth-child(1 of :scope)').length;
 nestedA = before === 0 && after === 0 && target === event.target;
 nestedMetadata = event.currentTarget === target && event.eventPhase === 2;
});
document.getElementById('event-b').addEventListener('click', function(event) {
 nestedB = document.querySelectorAll(
  'button:nth-child(1 of :scope)').length === 0 &&
  event.currentTarget === event.target && event.eventPhase === 2;
});
var ok = true;
)JS", "JS72 nested Event handlers issue independently scoped bulk queries");
    error = RuntimeErrorCode::None;
    HostInstanceId eventASerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventASerial = element.serial;
    expect(eventASerial != 0u && harness.dispatchClick(eventASerial, error) &&
        error == RuntimeErrorCode::None,
        "JS72 nested Event selector queries complete");
    const Value* nestedA = harness.runtime().lookup(SourceView("nestedA", 7u));
    const Value* nestedB = harness.runtime().lookup(SourceView("nestedB", 7u));
    const Value* nestedMetadata = harness.runtime().lookup(
        SourceView("nestedMetadata", 14u));
    expect(nestedA != nullptr && nestedA->isBoolean() && nestedA->booleanValue() &&
        nestedB != nullptr && nestedB->isBoolean() && nestedB->booleanValue() &&
        nestedMetadata != nullptr && nestedMetadata->isBoolean() &&
        nestedMetadata->booleanValue(),
        "JS72 nested scratch ownership preserves outer Event results");
}

void testMaximumCapacityAndCounters()
{
    NavigatorScriptExecutionHarness harness;
    const std::string html = sharedParentHtml("alternating");
    if (!loadFixture(harness, html,
            "JS72 1,024-Element shared-parent fixture loads")) return;
    expect(harness.document().structuralElements.size() == 1024u,
        "JS72 fixture reaches all 1,024 structural Elements");
    runScript(harness,
        "var stress = document.querySelectorAll("
        "':nth-child(odd of .selected)');"
        "var stressCount = stress.length;"
        "var ok = stressCount === 256 && stress[0].id === 's0' && "
        "stress[255].id === 's1020';",
        "JS72 maximum-capacity alternating filtered ranks preserve identities");

    const std::string selector = ":nth-child(odd of .selected)";
    const auto* record = NavigatorScriptRelativeSelectorTestAccess::collectionFor(
        harness.hostAdapter(), selector);
    expect(record != nullptr,
        "JS72 diagnostic collection retains its descriptor");
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    if (record != nullptr) {
        NavigatorScriptFilteredNthCounters counters;
        const std::size_t resultCount =
            NavigatorScriptRelativeSelectorTestAccess::countWithDiagnostics(
                harness.hostAdapter(), *record, counters);
        expect(resultCount == 256u,
            "JS72 diagnostic bulk evaluation returns exact alternating count");
#ifdef GXOS_JS72_BASELINE
        expect(counters.parentResolutions == 1021u &&
            counters.structuralInspections == 1021u * 1024u &&
            counters.siblingInspections == 1021u * 1021u &&
            counters.simpleSelectorEvaluations == 1021u * 1021u &&
            counters.formulaEvaluations == 511u,
            "JS72 baseline diagnostics record repeated JS71 scans");
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
            "JS72 bulk ranking evaluates each same-parent filter once");
#endif
        std::cout << "JS72 counters: parent-resolutions=" <<
            counters.parentResolutions << ", structural-inspections=" <<
            counters.structuralInspections << ", sibling-inspections=" <<
            counters.siblingInspections << ", filter-evaluations=" <<
            counters.simpleSelectorEvaluations << ", matching-siblings=" <<
            counters.matchingSiblings << ", formula-evaluations=" <<
            counters.formulaEvaluations;
#ifndef GXOS_JS72_BASELINE
        std::cout << ", rank-cache-initializations=" <<
            counters.rankCacheInitializations << ", rank-cache-builds=" <<
            counters.rankCacheBuilds << ", rank-cache-hits=" <<
            counters.rankCacheHits;
#endif
        std::cout << '\n';
    }
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
        expect(record != nullptr, "JS72 capacity variant descriptor is retained");
        if (record != nullptr) {
            NavigatorScriptFilteredNthCounters counters;
            const std::size_t count =
                NavigatorScriptRelativeSelectorTestAccess::countWithDiagnostics(
                    harness.hostAdapter(), *record, counters);
            expect(count == expectedCount,
                "JS72 capacity variant diagnostic result count matches");
#ifndef GXOS_JS72_BASELINE
            const std::uint64_t expectedInspections = fromEnd
                ? 3u * 1024u : 2u * 1024u;
            expect(counters.parentResolutions == 3u &&
                counters.structuralInspections == expectedInspections &&
                counters.simpleSelectorEvaluations == 1023u &&
                counters.rankCacheInitializations == 1u &&
                counters.rankCacheBuilds == 1u,
                "JS72 capacity variant builds one fixed rank table");
#endif
        }
#else
        static_cast<void>(fromEnd);
#endif
    };
    runSharedVariant("JS72 all-selected 1,024-Element fixture",
        "all", ":nth-child(odd of .selected)", 511u, nullptr, false);
    runSharedVariant("JS72 empty filtered set at capacity",
        "none", ":nth-child(odd of .selected)", 0u, nullptr, false);
    runSharedVariant("JS72 one early matching sibling at capacity",
        "one-early", ":nth-child(1 of .selected)", 1u, "s0", false);
    runSharedVariant("JS72 one late matching sibling at capacity",
        "one-late", ":nth-child(1 of .selected)", 1u, "s1020", false);
    runSharedVariant("JS72 reverse rank at maximum capacity",
        "all", ":nth-last-child(odd of .selected)", 511u, nullptr, true);

    NavigatorScriptExecutionHarness groups;
    if (loadFixture(groups, manyParentHtml(),
            "JS72 many-parent 1,024-Element fixture loads")) {
        expect(groups.document().structuralElements.size() == 1024u,
            "JS72 many-parent fixture reaches structural capacity");
        runScript(groups,
            "var rows=document.querySelectorAll("
            "'section > :nth-child(odd of .selected)');"
            "var ok=rows.length===255;",
            "JS72 many-parent groups keep rank state isolated");
#ifndef GXOS_JS72_BASELINE
        const std::string groupsSelector =
            "section > :nth-child(odd of .selected)";
        const auto* groupsRecord =
            NavigatorScriptRelativeSelectorTestAccess::collectionFor(
                groups.hostAdapter(), groupsSelector);
        expect(groupsRecord != nullptr,
            "JS72 many-parent diagnostic collection retains its descriptor");
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
                "JS72 many-parent groups build one bounded rank table");
            std::cout << "JS72 many-parent counters: parent-resolutions=" <<
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
            "JS72 single direct-child fixture loads")) {
        runScript(singleton,
            "var rows=document.querySelectorAll("
            "':nth-child(odd of .selected)');"
            "var ok=rows.length===1&&rows[0].id==='only';",
            "JS72 one-child filtered query returns its canonical Element");
    }
}

void testInvalidStructuresAndGenerationReuse()
{
    const std::string html =
        "<html><body><div id='load'><span id='selected' "
        "class='selected'></span></div></body></html>";
    NavigatorScriptExecutionHarness invalid;
    if (!loadFixture(invalid, html,
            "JS72 invalid-structure fixture loads")) return;
    runScript(invalid, R"JS(
var retained = document.querySelectorAll(':nth-child(1 of .selected)');
var retainedElement = retained[0];
var ok = retained.length === 1 && retainedElement.id === 'selected';
)JS", "JS72 retained collection captures the initial rank");
    for (auto& element : invalid.document().structuralElements)
        if (element.id == "selected") element.childIndex = 2u;
    runScript(invalid, R"JS(
var ok = retained.length === 0;
)JS", "JS72 invalid sibling structure cannot reuse a previous rank");
    for (auto& element : invalid.document().structuralElements)
        if (element.id == "selected") element.childIndex = 1u;
    runScript(invalid, R"JS(
var ok = retained.length === 1 && retained[0] === retainedElement;
)JS", "JS72 repaired live structure rebuilds its rank table");

    const HostGenerationId oldGeneration = invalid.hostAdapter().generation();
    const Value* collectionValue = invalid.runtime().lookup(
        SourceView("retained", 8u));
    expect(collectionValue != nullptr && collectionValue->isHostObject(),
        "JS72 old-generation filtered collection handle is retained");
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool replaced = invalid.replaceHtml("file:///js72-replacement.html",
        html, error) && error == RuntimeErrorCode::None;
    expect(replaced && invalid.hostAdapter().generation() != oldGeneration,
        "JS72 document generation replacement reuses structural serials");
    const HostObjectReference oldCollection{1u, oldGeneration,
        kNavigatorSelectorCollectionHostKind};
    expect(invalid.hostAdapter().validate(oldCollection).code ==
            HostResultCode::StaleObject,
        "JS72 old rank collection cannot cross a generation boundary");
    runScript(invalid, R"JS(
var fresh = document.querySelectorAll(':nth-child(1 of .selected)');
var ok = fresh.length === 1 && fresh[0].id === 'selected';
)JS", "JS72 reused serials receive fresh filtered ranks");
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
    if (!harness.loadHtml("file:///js72-benchmark.html", sample.html, error) ||
        error != RuntimeErrorCode::None) {
        std::cout << "JS72 timing " << sample.label << " fixture=FAIL\n";
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
            std::cout << "JS72 timing " << sample.label << " result=FAIL\n";
            return;
        }
        values.push_back(elapsed);
    }
    std::sort(values.begin(), values.end());
    std::cout << "JS72 timing " << sample.label << ": median-us=" <<
        values[values.size() / 2u] << ", min-us=" << values.front() <<
        ", max-us=" << values.back() << ", repetitions=" << values.size()
        << '\n';
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
}

} // namespace

int main(int argc, char** argv)
{
    using namespace gxos::javascript;
    static_assert(sizeof(NavigatorScriptSelectorMatchContext) == 16u,
        "JS72 preserves the JS70 selector match context");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1918u,
        "JS72 preserves the compact selector descriptor");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::collectionBytes()
            == 1936u,
        "JS72 preserves the compact collection record");
    static_assert(kNavigatorScriptMaxSelectorLength == 256u &&
        kNavigatorScriptMaxSelectorListMembers == 4u &&
        kNavigatorScriptMaxHasRelativeCores == 24u &&
        kNavigatorScriptMaxSelectorCollections == 128u &&
        kNavigatorScriptMaxDocumentNodes == 1024u,
        "JS72 preserves all established selector and structure capacities");
#ifndef GXOS_JS72_BASELINE
    static_assert(NavigatorScriptRelativeSelectorTestAccess::scratchBytes() <=
            24u * 1024u,
        "JS72 query scratch remains within its fixed 24 KiB bound");
#endif

    const bool benchmarkOnly = argc > 1 &&
        std::string(argv[1]) == "--bench-only";
    if (!benchmarkOnly) {
        testSemantics();
        testMaximumCapacityAndCounters();
        testMaximumCapacityVariants();
        testInvalidStructuresAndGenerationReuse();
        std::cout << "JS72 memory: descriptor=" <<
            sizeof(NavigatorScriptSelectorDescriptor) <<
            ", collection=" <<
            NavigatorScriptRelativeSelectorTestAccess::collectionBytes() <<
            ", registry=" <<
                NavigatorScriptRelativeSelectorTestAccess::collectionBytes() *
                    kNavigatorScriptMaxSelectorCollections <<
            ", structural-element=" << sizeof(gxos::web::HtmlElementRef) <<
            ", content-metadata=" <<
                sizeof(gxos::web::HtmlElementContentMetadata);
#ifndef GXOS_JS72_BASELINE
        std::cout << ", call-local-scratch=" <<
            NavigatorScriptRelativeSelectorTestAccess::scratchBytes();
#endif
        std::cout << '\n';
        std::cout << "Navigator JavaScript JS72 checks: " << checks - failures
            << '/' << checks << " passed\n";
    }
    runBenchmarks();
    return failures == 0u ? 0 : 1;
}
