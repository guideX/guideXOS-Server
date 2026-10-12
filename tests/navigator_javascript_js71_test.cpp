#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>

namespace gxos::javascript {

struct NavigatorScriptRelativeSelectorTestAccess {
    static constexpr std::size_t collectionRecordBytes()
    {
        return sizeof(NavigatorScriptHostAdapter::SelectorCollectionRecord);
    }

    static std::size_t activeCollectionCount(
        const NavigatorScriptHostAdapter& adapter)
    {
        std::size_t count = 0u;
        for (const auto& record : adapter.selectorCollections_)
            if (record.active) ++count;
        return count;
    }

    static std::size_t maximumRelativeCoreCount(
        const NavigatorScriptHostAdapter& adapter)
    {
        std::size_t maximum = 0u;
        for (const auto& record : adapter.selectorCollections_)
            if (record.active) maximum = std::max(maximum,
                static_cast<std::size_t>(
                    record.selector.hasRelativeCoreCount));
        return maximum;
    }

    static bool hasDescriptorShape(const NavigatorScriptHostAdapter& adapter,
        std::size_t relativeCoreCount, std::size_t expressionCount,
        std::size_t memberCount, std::size_t logicalArgumentCount)
    {
        for (const auto& record : adapter.selectorCollections_) {
            if (!record.active ||
                record.selector.hasRelativeCoreCount != relativeCoreCount ||
                record.selector.hasExpressionCount != expressionCount ||
                record.selector.memberCount != memberCount) continue;
            std::size_t logicalArguments = 0u;
            for (std::size_t expressionIndex = 0u;
                    expressionIndex < expressionCount; ++expressionIndex) {
                for (const auto& argument : record.selector
                        .hasExpressions[expressionIndex].logicalArguments)
                    if ((argument.flags & 0x01u) != 0u) ++logicalArguments;
            }
            if (logicalArguments == logicalArgumentCount) return true;
        }
        return false;
    }

    static void setHasInlineCore(
        NavigatorScriptSimpleSelectorDescriptor& selector,
        const NavigatorScriptCompactRelativeSelectorCoreDescriptor& core,
        std::uint8_t expressionIndex)
    {
        std::memcpy(selector.selectorPayload.data(), &core, sizeof(core));
        selector.selectorPayload[sizeof(core)] = expressionIndex;
    }

    static NavigatorScriptSelectorMatchContext contextForScope(
        const NavigatorScriptHostAdapter& adapter, HostInstanceId scope)
    {
        return adapter.selectorMatchContextForScope(scope);
    }

    static NavigatorScriptSelectorMatchResult coreMatch(
        const NavigatorScriptHostAdapter& adapter,
        const gxos::web::HtmlElementRef& element,
        const NavigatorScriptSimpleSelectorCoreDescriptor& selector,
        const NavigatorScriptSelectorDescriptor& storage,
        const NavigatorScriptSelectorMatchContext& context)
    {
        return adapter.selectorCoreElementMatchResult(element, selector, 0, 0,
            storage, true, context);
    }

    static NavigatorScriptSelectorMatchResult simpleMatch(
        const NavigatorScriptHostAdapter& adapter,
        const gxos::web::HtmlElementRef& element,
        const NavigatorScriptSimpleSelectorDescriptor& selector,
        const NavigatorScriptSelectorDescriptor& storage,
        const NavigatorScriptSelectorMatchContext& context =
            NavigatorScriptSelectorMatchContext()
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
        , NavigatorScriptFilteredNthCounters* counters = nullptr
#endif
        )
    {
        return adapter.selectorSimpleElementMatchResult(element, selector,
            storage, context
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , counters
#endif
            );
    }

#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    static const NavigatorScriptSimpleSelectorDescriptor* filteredNthSelector(
        const NavigatorScriptHostAdapter& adapter)
    {
        for (const auto& record : adapter.selectorCollections_) {
            if (!record.active) continue;
            for (std::size_t memberIndex = 0u;
                    memberIndex < record.selector.memberCount; ++memberIndex) {
                const auto& member = record.selector.members[memberIndex];
                const NavigatorScriptSimpleSelectorDescriptor* candidates[] =
                    {&member.leftSimple, &member.rightSimple};
                for (const auto* candidate : candidates) {
                    if ((candidate->statePseudo ==
                            NavigatorScriptStatePseudo::NthChild ||
                         candidate->statePseudo ==
                            NavigatorScriptStatePseudo::NthLastChild) &&
                        candidate->selectorPayload[sizeof(std::int16_t) * 2u]
                            != 0xFFu)
                        return candidate;
                }
            }
        }
        return nullptr;
    }

    static const NavigatorScriptSelectorDescriptor* filteredNthStorage(
        const NavigatorScriptHostAdapter& adapter,
        const NavigatorScriptSimpleSelectorDescriptor* selector)
    {
        if (selector == nullptr) return nullptr;
        for (const auto& record : adapter.selectorCollections_) {
            if (!record.active) continue;
            for (std::size_t memberIndex = 0u;
                    memberIndex < record.selector.memberCount; ++memberIndex) {
                const auto& member = record.selector.members[memberIndex];
                if (selector == &member.leftSimple ||
                    selector == &member.rightSimple)
                    return &record.selector;
            }
        }
        return nullptr;
    }

#endif

    static NavigatorScriptSelectorMatchResult relativeChain(
        const NavigatorScriptHostAdapter& adapter,
        const HostObjectReference& anchor,
        NavigatorScriptRelativeSelectorRelation firstRelation,
        const NavigatorScriptSimpleSelectorCoreDescriptor& firstSelector,
        NavigatorScriptRelativeSelectorRelation secondRelation,
        const NavigatorScriptSimpleSelectorCoreDescriptor& secondSelector,
        const NavigatorScriptSelectorDescriptor& storage,
        const NavigatorScriptSelectorMatchContext& context
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
        , NavigatorScriptRelativeSelectorCounters* counters = nullptr
#endif
        , const NavigatorScriptSimpleSelectorCoreDescriptor*
            firstLogicalArgument = nullptr
        , const NavigatorScriptSimpleSelectorCoreDescriptor*
            secondLogicalArgument = nullptr
        , const NavigatorScriptSimpleSelectorCoreDescriptor*
            firstLogicalArgumentSecond = nullptr
        , const NavigatorScriptSimpleSelectorCoreDescriptor*
            secondLogicalArgumentSecond = nullptr
        )
    {
        return adapter.selectorRelativeChainMatchResult(anchor, firstRelation,
            firstSelector, secondRelation, secondSelector, storage
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , counters
#endif
            , context, firstLogicalArgument, secondLogicalArgument,
            firstLogicalArgumentSecond, secondLogicalArgumentSecond);
    }

    static NavigatorScriptSelectorMatchResult relativeOne(
        const NavigatorScriptHostAdapter& adapter,
        const HostObjectReference& anchor,
        NavigatorScriptRelativeSelectorRelation relation,
        const NavigatorScriptSimpleSelectorCoreDescriptor& selector,
        const NavigatorScriptSimpleSelectorCoreDescriptor& logicalArgument,
        const NavigatorScriptSelectorDescriptor& storage,
        const NavigatorScriptSelectorMatchContext& context
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
        , NavigatorScriptRelativeSelectorCounters* counters = nullptr
#endif
        , const NavigatorScriptSimpleSelectorCoreDescriptor*
            secondLogicalArgument = nullptr
        )
    {
        return adapter.selectorRelativeElementMatchResult(anchor, relation,
            selector, 0, 0, storage
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , counters
#endif
            , nullptr, NavigatorScriptRelativeSelectorRelation::Descendant,
            context, &logicalArgument, nullptr, secondLogicalArgument, nullptr);
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
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
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
    for (const char* name : {"eventAResult", "eventBResult", "eventMetadata",
            "leaf", "panel", "collection", "first", "second", "all",
            "initially", "changed", "restored", "rejected", "listRejected",
            "sequence", "malformedRejected", "forward", "reverse",
            "candidateMembership", "ordinaryDifference", "formulas",
            "allAndEmpty", "mixedTags", "separateParents", "publicApis",
            "quotedOf", "invalid", "afterInvalid", "initial", "checkedInitial",
            "checkedChanged", "checkedRestored", "scopeFilter", "ok"}) {
        const Value* debug = harness.runtime().lookup(SourceView(name,
            std::char_traits<char>::length(name)));
        if (debug == nullptr) continue;
        std::cerr << "  " << name << "=";
        if (debug->isBoolean())
            std::cerr << (debug->booleanValue() ? "true" : "false");
        else if (debug->isNumber())
            std::cerr << debug->numberValue();
        else
            std::cerr << valueTypeName(debug->type());
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

void testMaximumNestedSelectorBounds()
{
    const std::string maximumAttributeValue(128u, 'x');
    const std::string nestedSuffix = ":has(:is([data-label=\"" +
        maximumAttributeValue + "\"], .missing))";
    const std::string maximumId(255u - nestedSuffix.size(), 'i');
    const std::string maximumNestedSelector = "#" + maximumId + nestedSuffix;
    const std::string maximumHtml = "<html><body><div id='" + maximumId +
        "'><span data-label='" + maximumAttributeValue + "'></span></div>" +
        "<div id='" + maximumId + "i'><span data-label='" +
        maximumAttributeValue + "'></span></div></body></html>";
    const auto maximumHarness =
        std::make_unique<NavigatorScriptExecutionHarness>();
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(maximumNestedSelector.size() == kNavigatorScriptMaxSelectorLength &&
        maximumHarness->loadHtml("file:///js71-maximum.html", maximumHtml,
            error) && error == RuntimeErrorCode::None,
        "maximum nested selector fixture reaches the 256-byte bound");
    expectScript(*maximumHarness,
        "var selector = '" + maximumNestedSelector + "';"
        "var over = '#" + maximumId + "i" + nestedSuffix + "';"
        "var ok = selector.length === 256 && "
        "document.querySelector(selector) !== null && "
        "over.length === 257 && document.querySelector(over) === null;",
        "256-byte nested logical selector succeeds and 257-byte input rejects");
}

gxos::javascript::NavigatorScriptRelativeSelectorRelation relationAt(
    unsigned value)
{
    using Relation = NavigatorScriptRelativeSelectorRelation;
    return static_cast<Relation>(value);
}

std::string relationPairHtml(unsigned first, unsigned second)
{
    std::string html = "<html><body><main>";
    if (first <= 1u) {
        html += "<div id='anchor'>";
        if (first == 0u) html += "<div class='first-wrap'>";
        html += "<div id='middle' class='middle'>";
    } else {
        html += "<div id='anchor'></div>";
        if (first == 3u) html += "<i class='gap'></i>";
        html += "<div id='middle' class='middle'>";
    }

    if (second == 0u) {
        html += "<div class='terminal-wrap'><span id='scope'></span></div></div>";
    } else if (second == 1u) {
        html += "<span id='scope'></span></div>";
    } else if (second == 2u) {
        html += "</div><span id='scope'></span>";
    } else {
        html += "</div><i class='gap'></i><span id='scope'></span>";
    }

    if (first <= 1u) {
        if (first == 0u) html += "</div>";
        html += "</div>";
    }
    html += "</main></body></html>";
    return html;
}

const char* const kJs69Html =
    "<html id='document-root'><body>"
    "<div id='ancestor' class='ancestor'><section id='query-root'>"
    "<span id='target' class='target'></span></section></div>"
    "<div id='nested-ancestor' class='ancestor'><div><section id='nested-query-root'>"
    "<span id='nested-target' class='target'></span></section></div></div>"
    "<div id='container' class='container'><span id='direct-leaf' class='child'></span>"
    "<div id='deep-branch'><span id='deep-leaf' class='child'></span></div></div>"
    "<section id='panel' class='panel'><div id='row' class='row'>"
    "<span id='leaf' class='leaf'></span></div></section>"
    "<section id='bad-panel' class='panel'><div class='row'></div>"
    "<div class='other'><span id='bad-leaf'></span></div></section>"
    "<section id='first-panel' class='panel'><span id='first-leaf'>"
    "<i class='note'></i></span><span id='other-leaf'></span></section>"
    "<div id='or-panel' class='panel'><section id='or-root'>"
    "<i class='target'></i></section></div><i class='badge'></i>"
    "<div id='sibling-parent' class='sibling-parent'><span id='sibling-scope' class='scope'>"
    "</span><i class='peer'></i><i class='peer'></i></div>"
    "<i id='adjacent-anchor' class='adjacent-anchor'></i>"
    "<span id='adjacent-scope' class='scope'></span>"
    "<i id='general-anchor' class='general-anchor'></i><b></b>"
    "<span id='general-scope' class='scope'></span>"
    "<div id='event-panel' class='panel'><div class='row'><span id='event-leaf'>"
    "</span><button id='event-a'></button></div></div>"
    "<div id='event-panel-b' class='panel'><div class='row'><span id='event-leaf-b'>"
    "</span><button id='event-b'></button><i class='selected'></i></div>"
    "<section id='logical-panel' class='panel'><span class='warning' data-label='a,b'></span>"
    "<span class='disabled'></span><span class='enabled'></span></section>"
    "<section id='empty-panel' class='panel'></section>"
    "<section id='disabled-panel' class='panel'><span class='disabled'></span>"
    "<span class='disabled'></span></section>"
    "<section id='excluded-panel' class='panel'><span class='disabled'></span>"
    "<span class='hidden'></span></section>"
    "<section id='strict-panel' class='panel'><span class='warning'></span></section>"
    "<section id='attribute-panel' class='panel'><div class='row' data-state='disabled'>"
    "</div><div class='row' data-state='ready' data-name='a b'></div></section>"
    "<form id='bad-correlated' class='form'><div class='row'>"
    "<span class='valid'></span></div><div class='other'><span class='invalid'>"
    "</span></div></form>"
    "<form id='good-correlated' class='form'><div class='row'>"
    "<span class='invalid'></span></div></form>"
    "<form id='state-form' class='form'><input id='state-check' type='checkbox'>"
    "<button id='state-focus-reset'></button></form>"
    "<div id='relative-child'><i class='a'></i></div>"
    "<div id='relative-desc'><div><i class='a'></i></div></div>"
    "<i id='relative-adjacent'></i><i class='a'></i>"
    "<i id='relative-general'></i><b></b><i class='a'></i>"
    "<span id='list-a' class='a'></span>"
    "<span id='list-ab' class='a b'></span>"
    "</body></html>";

void testFilteredNthSelectors()
{
    const std::string html =
        "<html><body>"
        "<ul id='series' class='container'>"
        "<li id='a'></li><li id='b' class='selected'></li>"
        "<li id='c'></li><li id='d' class='selected'></li>"
        "<li id='e' class='selected' data-label='color of sky'></li>"
        "</ul>"
        "<ul id='all'><li id='all1' class='selected'></li>"
        "<li id='all2' class='selected'></li>"
        "<li id='all3' class='selected'></li></ul>"
        "<ul id='none'><li id='none1'></li><li id='none2'></li></ul>"
        "<div id='mixed'><span id='s1' class='selected'></span>"
        "<button id='b1' class='selected'></button>"
        "<span id='s2' class='selected'></span></div>"
        "<div id='parent-one'><span id='other1' class='selected'></span></div>"
        "<div id='parent-two'><span id='other2' class='selected'></span></div>"
        "<ul id='state-list'><li id='state-a' data-state='ready'></li>"
        "<li id='state-b' data-state='idle'></li>"
        "<li id='state-c' data-state='ready'></li></ul>"
        "<div id='check-list'><input id='check-a' type='checkbox'>"
        "<input id='check-b' type='checkbox' checked></div>"
        "<div id='events'><i id='event-a' class='selected'></i>"
        "<button id='event-b' class='selected'></button></div>"
        "</body></html>";
    auto harnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& harness = *harnessStorage;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js71-filtered.html", html, error) &&
        error == RuntimeErrorCode::None,
        "JS71 filtered positional fixture loads");
    expectScript(harness, R"JS(
var series = document.getElementById('series');
var a = document.getElementById('a');
var b = document.getElementById('b');
var c = document.getElementById('c');
var d = document.getElementById('d');
var e = document.getElementById('e');
var forward = document.querySelector(':nth-child(1 of .selected)') === b &&
 document.querySelector(':nth-child(2 of .selected)') === d &&
 document.querySelector(':nth-child(3 of .selected)') === e &&
 document.querySelector(':nth-child(4 of .selected)') === null;
var reverse = document.querySelector(':nth-last-child(1 of .selected)') === e &&
 document.querySelector(':nth-last-child(2 of .selected)') === d &&
 document.querySelector(':nth-last-child(3 of .selected)') === b &&
 document.querySelector(':nth-last-child(4 of .selected)') === null;
var candidateMembership = !a.matches(':nth-child(1 of .selected)') &&
 !c.matches(':nth-child(2 of .selected)') &&
 b.matches(':nth-child(1 of .selected)');
var ordinaryDifference = document.querySelector(
 '.selected:nth-child(2)') === b &&
 document.querySelector(':nth-child(2 of .selected)') === d;
var formulas = series.querySelectorAll(':nth-child(odd of .selected)').length === 2 &&
 series.querySelectorAll(':nth-child(odd of .selected)')[0] === b &&
 series.querySelectorAll(':nth-child(odd of .selected)')[1] === e &&
 series.querySelectorAll(':nth-child(even of .selected)').length === 1 &&
 document.querySelector(':nth-child(2n+1 of .selected)') === b &&
 document.querySelector(':nth-child(-n+2 of .selected)') === b &&
 document.querySelectorAll(':nth-child(-n+2 of .selected)')[1] === d &&
 document.querySelector(':nth-child(3n-1 of .selected)') === d &&
 series.querySelector(':nth-child( 2n + 1 OF .selected )') === b &&
 document.querySelector(':nth-child(0n+2 of .selected)') === d &&
 document.querySelector(':nth-child(0 of .selected)') === null &&
 document.querySelector(':nth-last-child(odd of .selected)') === b;
var allAndEmpty = document.querySelectorAll(
 ':nth-child(2 of .selected)')[0] === d &&
 document.querySelectorAll('#all > :nth-child(2 of .selected)').length === 1 &&
 document.querySelector(':nth-child(1 of .selected)') === b &&
 document.getElementById('none').querySelectorAll(
 ':nth-child(1 of .selected)').length === 0;
var mixedTags = document.querySelector(':nth-child(2 of .selected)') === d &&
 document.querySelector('#mixed :nth-child(2 of .selected)') ===
 document.getElementById('b1') &&
 document.querySelector('#mixed :nth-child(2 of span.selected)') ===
 document.getElementById('s2');
var separateParents = !document.getElementById('other1').matches(
 ':nth-child(2 of .selected)') &&
 !document.getElementById('other2').matches(
 ':nth-child(2 of .selected)');
var publicApis = document.querySelectorAll(
 '#series > li:nth-child(2 of .selected)').length === 1 &&
 series.querySelector(':nth-child(2 of .selected)') === d &&
 series.querySelectorAll(':scope > li:nth-child(odd of .selected)').length === 2 &&
 series.querySelectorAll(':scope > li:nth-child(odd of .selected)')[0] === b &&
 d.matches(':nth-child(2 of .selected)') &&
 d.closest(':nth-child(2 of .selected)') === d &&
 series.querySelectorAll(
 ':nth-child(2 of .selected), #d').length === 1;
var quotedOf = document.querySelector(
 ':nth-child(1 oF [data-label="color of sky"])') === e &&
 document.querySelector(':nth-child(1 of [data-label="color of sky"])') === e;
var ok = forward && reverse && candidateMembership && ordinaryDifference &&
 formulas && allAndEmpty && mixedTags && separateParents && publicApis &&
 quotedOf;
)JS", "JS71 forward/reverse filtering, candidate membership, formulas, tags, parents, and APIs");

    expectScript(harness, R"JS(
var invalid = document.querySelector(':nth-child(of .selected)') === null &&
 document.querySelector(':nth-child(2 of)') === null &&
 document.querySelector(':nth-child(2 of .a, .b)') === null &&
 document.querySelector(':nth-child(2 of .a > .b)') === null &&
 document.querySelector(':nth-child(2 of :has(.a))') === null &&
 document.querySelector(':nth-child(2 of :nth-child(1))') === null &&
 document.querySelector(':nth-child(2 of :is(.a, .b))') === null &&
 document.querySelector(':nth-child(2 of .a of .b)') === null &&
 document.querySelector(':nth-last-child(1 of .a, .b)') === null &&
 document.querySelector(':nth-child(2 of .a[broken)') === null &&
 document.querySelector(':nth-child(2 of [data-label="unterminated])') === null &&
 document.querySelector(':nth-child(2 of .a') === null &&
 document.querySelector(':nth-child(2 of .a))') === null &&
 document.querySelector(':has(> li:nth-child(2 of .selected))') === null &&
 document.querySelector(':nth-of-type(2 of .selected)') === null &&
 document.querySelector(':nth-child(999999999999999999999n of .selected)') === null &&
 document.querySelectorAll(':nth-child(2 of .a, .b)').length === 0;
var afterInvalid = document.querySelector(':nth-child(2 of .selected)') ===
 document.getElementById('d');
var ok = invalid && afterInvalid;
)JS", "JS71 rejects unsupported grammar and recovers for the next valid parse");

    expectScript(harness, R"JS(
var stateList = document.getElementById('state-list');
var stateA = document.getElementById('state-a');
var stateB = document.getElementById('state-b');
var stateC = document.getElementById('state-c');
var held = stateList.querySelectorAll(
 ':nth-child(2 of [data-state=ready])');
var initial = held.length === 1 && held[0] === stateC;
stateB.setAttribute('data-state', 'ready');
var changed = held.length === 1 && held[0] === stateB;
stateB.setAttribute('data-state', 'idle');
var restored = held.length === 1 && held[0] === stateC;
var checkA = document.getElementById('check-a');
var checkB = document.getElementById('check-b');
var checkedInitial = document.querySelector(
 ':nth-last-child(1 of :checked)') === checkB;
checkB.checked = false;
var checkedChanged = document.querySelector(
 ':nth-last-child(1 of :checked)') === null;
checkA.checked = true;
var checkedRestored = document.querySelector(
 ':nth-last-child(1 of :checked)') === checkA;
var scopeFilter = d.matches(':nth-child(1 of :scope)') &&
 series.querySelectorAll(':scope > li:nth-child(1 of :scope)').length === 0;
var ok = initial && changed && restored && checkedInitial && checkedChanged &&
 checkedRestored && scopeFilter;
)JS", "JS71 live attributes, checked state, retained collections, and scope context");

    NavigatorScriptExecutionHarness invalidStructureHarness;
    error = RuntimeErrorCode::None;
    expect(invalidStructureHarness.loadHtml("file:///js71-invalid-parent.html",
        "<html><body><div id='bad-parent'><span id='bad-child' "
        "class='selected'></span></div></body></html>", error) &&
        error == RuntimeErrorCode::None,
        "JS71 invalid-parent fixture loads");
    expectScript(invalidStructureHarness, R"JS(
var badChild = document.getElementById('bad-child');
var ok = true;
)JS", "JS71 invalid-parent candidate is retained");
    for (auto& element : invalidStructureHarness.document().structuralElements)
        if (element.id == "bad-child") element.parentSerial = 65535u;
    expectScript(invalidStructureHarness, R"JS(
var ok = !badChild.matches(':nth-child(1 of .selected)');
)JS", "JS71 invalid parent serial cannot create a filtered-position match");

    NavigatorScriptExecutionHarness staleHarness;
    error = RuntimeErrorCode::None;
    const bool staleLoaded = staleHarness.loadHtml(
        "file:///js71-stale-old.html",
        "<html><body><div><span id='stale-filtered' "
        "class='selected'></span></div></body></html>", error) &&
        error == RuntimeErrorCode::None;
    expect(staleLoaded, "JS71 stale-generation fixture loads");
    expectScript(staleHarness, R"JS(
var staleFiltered = document.getElementById('stale-filtered');
var staleCollection = document.querySelectorAll(
 ':nth-child(1 of .selected)');
var ok = staleCollection.length === 1;
)JS", "JS71 stale Element and collection handles are captured");
    const HostGenerationId staleGeneration =
        staleHarness.hostAdapter().generation();
    error = RuntimeErrorCode::None;
    const bool invalidated = staleHarness.invalidateDocumentGeneration(error) &&
        error == RuntimeErrorCode::None;
    expect(invalidated, "JS71 stale-generation invalidation succeeds");
    const HostObjectReference staleCollectionReference{1u, staleGeneration,
        kNavigatorSelectorCollectionHostKind};
    const HostResult staleCollectionValidation = staleHarness.hostAdapter().
        validate(staleCollectionReference);
    expect(staleCollectionValidation.code == HostResultCode::StaleObject,
        "JS71 retained filtered collection is rejected after generation invalidation");
    expectScript(staleHarness, R"JS(
var ok = !staleFiltered.matches(':nth-child(1 of .selected)');
)JS", "JS71 stale candidate cannot match after generation invalidation");

    const std::string listPrefix =
        ":nth-child(1 of .selected),#";
    const std::string selectorAt256 = listPrefix +
        std::string(kNavigatorScriptMaxSelectorLength - listPrefix.size(), 'x');
    const std::string selectorAt257 = selectorAt256 + "x";
    const ScriptResult lengthResult = harness.execute(
        "var selectorAt256 = '" + selectorAt256 + "';"
        "var selectorAt257 = '" + selectorAt257 + "';"
        "var length256 = document.querySelector(selectorAt256) === "
        "document.getElementById('b');"
        "var length257 = document.querySelector(selectorAt257) === null;");
    expect(lengthResult.succeeded(), "JS71 selector length probes execute");
    for (const char* name : {"length256", "length257"}) {
        const Value* value = harness.runtime().lookup(SourceView(name,
            std::char_traits<char>::length(name)));
        expect(value != nullptr && value->isBoolean() &&
            value->booleanValue(),
            (std::string("JS71 selector length bound: ") + name).c_str());
    }

    const std::string fullPoolMember =
        ":nth-child(1 of .selected)>:nth-child(1 of .selected)";
    const std::string fullPoolSelector = fullPoolMember + "," +
        fullPoolMember + "," + fullPoolMember + "," + fullPoolMember;
    const ScriptResult poolResult = harness.execute(
        "var poolCollection = document.querySelectorAll('" +
        fullPoolSelector + "'); var ok = poolCollection.length === 0;");
    expect(poolResult.succeeded(), "JS71 eight-filter capacity query executes");
    expect(poolResult.succeeded() &&
        NavigatorScriptRelativeSelectorTestAccess::maximumRelativeCoreCount(
            harness.hostAdapter()) == 8u,
        "JS71 eight filtered cores fit the shared 24-entry descriptor pool");

    auto& adapter = harness.hostAdapter();
    const auto* filteredSelector =
        NavigatorScriptRelativeSelectorTestAccess::filteredNthSelector(adapter);
    const auto* filteredStorage =
        NavigatorScriptRelativeSelectorTestAccess::filteredNthStorage(adapter,
            filteredSelector);
    expect(filteredSelector != nullptr && filteredStorage != nullptr,
        "JS71 retained descriptor owns its filtered simple core");
    if (filteredSelector != nullptr && filteredStorage != nullptr) {
        const auto context =
            NavigatorScriptRelativeSelectorTestAccess::contextForScope(
                adapter, 0u);
        const auto& document = harness.document();
        const gxos::web::HtmlElementRef* seriesElement = nullptr;
        for (const auto& element : document.structuralElements)
            if (element.id == "series") seriesElement = &element;
        std::size_t matching = 0u;
        for (const auto& element : document.structuralElements) {
            if (seriesElement == nullptr ||
                element.parentSerial != seriesElement->serial) continue;
            if (NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
                    adapter, element, *filteredSelector, *filteredStorage,
                    context) == NavigatorScriptSelectorMatchResult::Match)
                ++matching;
        }
        expect(matching == 2u,
            "JS71 retained simple selector evaluates every candidate with its descriptor");
    }

    const std::string eventHtml =
        "<html><body><div><button id='event-a' class='selected' "
        "type='button'></button>"
        "<button id='event-b' class='selected'></button></div></body></html>";
    NavigatorScriptExecutionHarness eventHarness;
    error = RuntimeErrorCode::None;
    expect(eventHarness.loadHtml("file:///js71-filtered-events.html",
            eventHtml, error) && error == RuntimeErrorCode::None,
        "JS71 filtered Event fixture loads");
    expectScript(eventHarness, R"JS(
var eventAResult = false;
var eventBResult = false;
var eventMetadata = false;
document.getElementById('event-a').addEventListener('click', function(event) {
 var savedTarget = event.target;
 var savedCurrent = event.currentTarget;
 var savedPhase = event.eventPhase;
 var savedDefault = event.defaultPrevented;
 eventAResult = event.target.matches(':nth-child(1 of .selected)');
 document.getElementById('event-b').click();
 eventAResult = eventAResult &&
  event.target.matches(':nth-child(1 of .selected)');
 eventMetadata = event.target === savedTarget &&
  event.currentTarget === savedCurrent && event.eventPhase === savedPhase &&
  event.defaultPrevented === savedDefault;
});
document.getElementById('event-b').addEventListener('click', function(event) {
 eventBResult = event.target.matches(':nth-last-child(1 of .selected)') &&
  event.currentTarget === event.target &&
  event.eventPhase === 2 && event.defaultPrevented === false;
});
var ok = true;
)JS", "JS71 Event listeners execute filtered selectors during nested dispatch");
    const HostInstanceId eventASerial = serialById(eventHarness.document(),
        "event-a");
    expect(eventASerial != 0u &&
        eventHarness.dispatchClick(eventASerial, error) &&
        error == RuntimeErrorCode::None,
        "JS71 nested filtered Event dispatch completes");
    const Value* eventAResult = eventHarness.runtime().lookup(
        SourceView("eventAResult", 12u));
    const Value* eventBResult = eventHarness.runtime().lookup(
        SourceView("eventBResult", 12u));
    const Value* eventMetadata = eventHarness.runtime().lookup(
        SourceView("eventMetadata", 13u));
    expect(eventAResult != nullptr && eventAResult->isBoolean() &&
        eventAResult->booleanValue() && eventBResult != nullptr &&
        eventBResult->isBoolean() && eventBResult->booleanValue() &&
        eventMetadata != nullptr && eventMetadata->isBoolean() &&
        eventMetadata->booleanValue(),
        "JS71 nested Event dispatch preserves selector results and metadata");
    if (eventAResult == nullptr || !eventAResult->isBoolean() ||
        !eventAResult->booleanValue() || eventBResult == nullptr ||
        !eventBResult->isBoolean() || !eventBResult->booleanValue() ||
        eventMetadata == nullptr || !eventMetadata->isBoolean() ||
        !eventMetadata->booleanValue()) {
        std::cerr << "JS71 Event values: dispatched=" <<
            (eventASerial != 0u && error == RuntimeErrorCode::None)
            << ",A=" << (eventAResult != nullptr &&
                eventAResult->isBoolean() && eventAResult->booleanValue())
            << ",B=" << (eventBResult != nullptr &&
                eventBResult->isBoolean() && eventBResult->booleanValue())
            << ",metadata=" << (eventMetadata != nullptr &&
                eventMetadata->isBoolean() && eventMetadata->booleanValue())
            << ",error=" << runtimeErrorCodeName(error) << '\n';
    }

    NavigatorScriptExecutionHarness sizeProbe;
    error = RuntimeErrorCode::None;
    const bool sizeProbeLoaded = sizeProbe.loadHtml(
        "file:///js71-size-probe.html",
        "<html><body><div id='load'><span></span></div></body></html>",
        error) && error == RuntimeErrorCode::None;
    const std::size_t structuralOverhead = sizeProbeLoaded &&
        !sizeProbe.document().structuralElements.empty()
        ? sizeProbe.document().structuralElements.size() - 1u
        : kNavigatorScriptMaxDocumentNodes;
    const std::size_t childTargetCount =
        structuralOverhead < kNavigatorScriptMaxDocumentNodes
        ? kNavigatorScriptMaxDocumentNodes - structuralOverhead : 0u;
    std::string stressHtml = "<html><body><div id='load'>";
    for (std::size_t child = 0u; child < childTargetCount; ++child) {
        stressHtml += "<span id='load-" + std::to_string(child) +
            "' class='selected'></span>";
    }
    stressHtml += "</div></body></html>";
    NavigatorScriptExecutionHarness stressHarness;
    error = RuntimeErrorCode::None;
    const bool stressLoaded = stressHarness.loadHtml(
        "file:///js71-near-capacity.html", stressHtml, error) &&
        error == RuntimeErrorCode::None &&
        stressHarness.document().structuralElements.size() ==
            kNavigatorScriptMaxDocumentNodes;
    expect(stressLoaded,
        "JS71 performance fixture reaches 1,024 structural Elements");
    if (stressLoaded) {
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = stressHarness.execute(
            "var stress = document.querySelectorAll("
            "':nth-child(odd of .selected)');"
            "var stressCount = stress.length;"
            "var reverseStress = document.querySelectorAll("
            "':nth-last-child(odd of .selected)');"
            "var reverseStressCount = reverseStress.length;"
            "var repeatedStress = document.querySelectorAll("
            "':nth-child(odd of .selected)');"
            "var repeatedStressCount = repeatedStress.length;");
        const auto finish = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(finish - start).count();
        const Value* count = stressHarness.runtime().lookup(
            SourceView("stressCount", 11u));
        const Value* reverseCount = stressHarness.runtime().lookup(
            SourceView("reverseStressCount", 18u));
        const Value* repeatedCount = stressHarness.runtime().lookup(
            SourceView("repeatedStressCount", 19u));
        expect(result.succeeded() && count != nullptr && count->isNumber() &&
            count->numberValue() ==
                static_cast<double>((childTargetCount + 1u) / 2u) &&
            reverseCount != nullptr && reverseCount->isNumber() &&
            reverseCount->numberValue() ==
                static_cast<double>((childTargetCount + 1u) / 2u) &&
            repeatedCount != nullptr && repeatedCount->isNumber() &&
            repeatedCount->numberValue() ==
                static_cast<double>((childTargetCount + 1u) / 2u),
            "JS71 near-capacity forward, reverse, and repeated queries return expected counts");
        std::cout << "JS71 near-capacity: elements=" <<
            stressHarness.document().structuralElements.size() <<
            ", sibling-count=" << childTargetCount <<
            ", expected structural inspections=" << childTargetCount *
                stressHarness.document().structuralElements.size()
            << ", expected filter evaluations=" << childTargetCount *
                childTargetCount << ", query-us=" << elapsed << "\n";

#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
        const auto* stressSelector =
            NavigatorScriptRelativeSelectorTestAccess::filteredNthSelector(
                stressHarness.hostAdapter());
        const auto* stressStorage =
            NavigatorScriptRelativeSelectorTestAccess::filteredNthStorage(
                stressHarness.hostAdapter(), stressSelector);
        expect(stressSelector != nullptr && stressStorage != nullptr,
            "JS71 near-capacity query retains a diagnostic descriptor");
        if (stressSelector != nullptr && stressStorage != nullptr) {
            NavigatorScriptFilteredNthCounters counters;
            const auto measuredContext =
                NavigatorScriptRelativeSelectorTestAccess::contextForScope(
                    stressHarness.hostAdapter(), 0u);
            std::size_t matched = 0u;
            const HostInstanceId stressParent = serialById(
                stressHarness.document(), "load");
            for (const auto& element :
                    stressHarness.document().structuralElements) {
                if (element.parentSerial != stressParent) continue;
                if (NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
                        stressHarness.hostAdapter(), element,
                        *stressSelector, *stressStorage, measuredContext,
                        &counters) ==
                    NavigatorScriptSelectorMatchResult::Match)
                    ++matched;
            }
            const std::uint64_t n = childTargetCount;
            const std::uint64_t structuralCount =
                stressHarness.document().structuralElements.size();
            expect(matched == (childTargetCount + 1u) / 2u &&
                counters.parentResolutions == n &&
                counters.structuralInspections == n * structuralCount &&
                counters.siblingInspections == n * n &&
                counters.simpleSelectorEvaluations == n * n &&
                counters.matchingSiblings == n * n &&
                counters.formulaEvaluations == n,
                "JS71 near-capacity operation counters match the bounded sibling scan");
            std::cout << "JS71 measured counters: parent-resolutions=" <<
                counters.parentResolutions << ", structural-inspections=" <<
                counters.structuralInspections << ", sibling-inspections=" <<
                counters.siblingInspections << ", simple-selector-evaluations=" <<
                counters.simpleSelectorEvaluations << ", matching-siblings=" <<
                counters.matchingSiblings << ", formula-evaluations=" <<
                counters.formulaEvaluations << "\n";

            const auto runSelectedVariant = [&](const char* label,
                    std::size_t selectedChild, std::size_t expectedCount) {
                std::size_t childIndex = 0u;
                for (auto& element :
                        stressHarness.document().structuralElements) {
                    if (element.parentSerial != stressParent) continue;
                    element.className = childIndex == selectedChild
                        ? "selected" : "";
                    ++childIndex;
                }
                const auto variantStart = std::chrono::steady_clock::now();
                const ScriptResult variantResult = stressHarness.execute(
                    "var variant = document.querySelectorAll("
                    "':nth-child(odd of .selected)');"
                    "var variantCount = variant.length;");
                const auto variantElapsed = std::chrono::duration_cast<
                    std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - variantStart).count();
                const Value* variantCount = stressHarness.runtime().lookup(
                    SourceView("variantCount", 12u));
                expect(variantResult.succeeded() && variantCount != nullptr &&
                    variantCount->isNumber() && variantCount->numberValue() ==
                        static_cast<double>(expectedCount),
                    (std::string("JS71 near-capacity ") + label +
                        " matching count").c_str());

                NavigatorScriptFilteredNthCounters variantCounters;
                const auto variantContext =
                    NavigatorScriptRelativeSelectorTestAccess::contextForScope(
                        stressHarness.hostAdapter(), 0u);
                std::size_t variantMatches = 0u;
                for (const auto& element :
                        stressHarness.document().structuralElements) {
                    if (element.parentSerial != stressParent) continue;
                    if (NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
                            stressHarness.hostAdapter(), element,
                            *stressSelector, *stressStorage, variantContext,
                            &variantCounters) ==
                        NavigatorScriptSelectorMatchResult::Match)
                        ++variantMatches;
                }
                expect(variantMatches == expectedCount &&
                    variantCounters.parentResolutions == n &&
                    variantCounters.structuralInspections ==
                        n * structuralCount &&
                    variantCounters.siblingInspections == n * n &&
                    variantCounters.simpleSelectorEvaluations == n * n &&
                    variantCounters.matchingSiblings ==
                        n * expectedCount &&
                    variantCounters.formulaEvaluations == expectedCount,
                    (std::string("JS71 near-capacity ") + label +
                        " operation counts").c_str());
                std::cout << "JS71 near-capacity " << label <<
                    ": selected=" << expectedCount << ", structural=" <<
                    variantCounters.structuralInspections << ", filter-tests=" <<
                    variantCounters.simpleSelectorEvaluations <<
                    ", formulas=" << variantCounters.formulaEvaluations <<
                    ", query-us=" << variantElapsed << "\n";
            };
            runSelectedVariant("none-match", childTargetCount, 0u);
            runSelectedVariant("one-early", 0u, 1u);
            runSelectedVariant("one-late", childTargetCount - 1u, 1u);
        }
#endif
    }
}

} // namespace

int main()
{
    static_assert(std::is_trivially_copyable<
        NavigatorScriptSelectorDescriptor>::value,
        "JS71 descriptors remain self-contained value copies");
    static_assert(sizeof(NavigatorScriptCompactLogicalArgumentDescriptor) ==
            13u && alignof(NavigatorScriptCompactLogicalArgumentDescriptor) ==
            1u,
        "JS71 logical argument records use dense byte-addressable storage");
    static_assert(sizeof(NavigatorScriptSimpleSelectorCoreDescriptor) == 32u,
        "JS71 does not grow the shared simple-selector core");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor) == 50u,
        "JS71 does not grow the outer simple-selector descriptor");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor{}.selectorPayload) == 17u,
        "JS71 reuses the existing tagged positional payload");
    static_assert(sizeof(NavigatorScriptCompactRelativeSelectorCoreDescriptor) ==
        16u, "JS71 preserves compact relative cores");
    static_assert(sizeof(NavigatorScriptHasExpressionDescriptor) == 108u,
        "JS71 keeps two logical arguments fixed per relative core");
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    static_assert(sizeof(NavigatorScriptSelectorMatchContext) == 16u,
        "JS71 keeps caller-owned counters outside the diagnostic context");
#else
    static_assert(sizeof(NavigatorScriptSelectorMatchContext) == 16u,
        "JS71 production context retains the JS70 size");
#endif
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1918u,
        "JS71 filtered cores do not grow the JS70 descriptor");
    static_assert(kNavigatorScriptMaxHasRelativeCores == 24u,
        "JS71 shares the unchanged bounded compact-core pool");
    static_assert(sizeof(gxos::web::HtmlElementRef) == 440u,
        "JS71 does not grow document structural records");
    static_assert(sizeof(gxos::web::HtmlElementContentMetadata) == 24u,
        "JS71 does not grow document content metadata");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() == 1936u,
        "JS71 collection records include the bounded logical slots");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections ==
            247808u,
        "JS71 fixed logical slots keep the 128-entry registry bounded");

    const auto harnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& harness = *harnessStorage;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js71.html", kJs69Html, error) &&
        error == RuntimeErrorCode::None, "JS71 structural fixture loads");

    expectScript(harness, R"JS(
var ok = document.querySelector(':has(:scope)') === null &&
 document.querySelector(':has(> :scope)') === null &&
 document.querySelector(':has(+ :scope)') === null &&
 document.querySelector(':has(~ :scope)') === null &&
 document.querySelector(':scope:has(:scope)') === null;
)JS", "document scope remains distinct from relative anchors");

    expectScript(harness, R"JS(
var leaf = document.querySelector('#direct-leaf');
var deepLeaf = document.querySelector('#deep-leaf');
var container = document.querySelector('#container');
var ok = leaf.closest('.container:has(:scope)') === container &&
 leaf.closest('.container:has(> :scope)') === container &&
 deepLeaf.closest('.container:has(:scope)') === container &&
 deepLeaf.closest('.container:has(> :scope)') === null &&
 leaf.closest('.container:has(:scope)') === container;
)JS", "closest keeps its original scope while testing ancestor subjects");

    expectScript(harness, R"JS(
var leaf = document.querySelector('#leaf');
var panel = document.querySelector('#panel');
var row = document.querySelector('#row');
var badLeaf = document.querySelector('#bad-leaf');
var ok = leaf.closest('.panel:has(> .row > :scope)') === panel &&
 leaf.closest('.panel:has(.row > :scope)') === panel &&
 panel.matches(':has(> .row > :scope)') === false &&
 badLeaf.closest('.panel:has(> .row > :scope)') === null &&
 row.querySelector(':scope') === null;
)JS", "terminal scope remains correlated with the matching intermediate");

    expectScript(harness, R"JS(
var logical = document.querySelector('#logical-panel');
var empty = document.querySelector('#empty-panel');
var disabled = document.querySelector('#disabled-panel');
var attr = document.querySelector('#attribute-panel');
var bad = document.querySelector('#bad-correlated');
var good = document.querySelector('#good-correlated');
var leaf = document.querySelector('#leaf');
var panel = document.querySelector('#panel');
var ok = logical.matches(':has(:is(.warning))') &&
 logical.matches(':has(:where(.warning))') ===
 logical.matches(':has(:is(.warning))') &&
 logical.matches(':has(:is([data-label="a,b"]))') &&
 !empty.matches(':has(:not(.disabled))') &&
 !disabled.matches(':has(> :not(.disabled))') &&
 logical.matches(':has(> :not(.disabled))') &&
 attr.matches(':has(> .row:not([data-state=disabled]))') &&
 attr.matches(':has(> .row:is([data-name="a b"]))') &&
 !bad.matches(':has(> .row :not(.valid))') &&
 good.matches(':has(> .row:is(.row) > :not(.valid))') &&
 !bad.matches(':has(> .row:is(.row) > :not(.valid))') &&
 leaf.closest('.panel:has(> .row > :is(:scope))') === panel &&
 !leaf.closest('.panel:has(> .row > :not(:scope))') &&
 panel.matches(':has(> :not(:scope))');
)JS", "nested is/where/not predicates preserve existential and scope semantics");

    expectScript(harness, R"JS(
var child = document.querySelector('#relative-child');
var desc = document.querySelector('#relative-desc');
var adjacent = document.querySelector('#relative-adjacent');
var general = document.querySelector('#relative-general');
var ok = child.matches(':has(> :is(.a))') &&
 desc.matches(':has(:where(.a))') &&
 adjacent.matches(':has(+ :is(.a))') &&
 general.matches(':has(~ :not(.missing))');
)JS", "nested predicates work with descendant, child, adjacent, and general sibling relations");

    expectScript(harness, R"JS(
var logical = document.querySelector('#logical-panel');
var empty = document.querySelector('#empty-panel');
var disabled = document.querySelector('#disabled-panel');
var excluded = document.querySelector('#excluded-panel');
var warningFirst = logical.matches(':has(:is(.warning, .error))');
var warningSecond = logical.matches(':has(:is(.missing, .warning))');
var warningBoth = logical.matches(':has(:is(.warning, .enabled))');
var neither = logical.matches(':has(:is(.missing, .absent))');
var whereEquivalent = logical.matches(':has(:where(.warning, .error))') &&
 logical.matches(':has(:where(.missing, .warning))') &&
 !logical.matches(':has(:where(.missing, .absent))');
var notNone = logical.matches(':has(> :not(.warning, .disabled))') &&
 !disabled.matches(':has(> :not(.disabled, .hidden))') &&
 !excluded.matches(':has(> :not(.disabled, .hidden))') &&
 !empty.matches(':has(> :not(.disabled, .hidden))');
var duplicateAndOrder = logical.matches(':has(:is(.warning, .warning))') &&
 logical.matches(':has(:is(.enabled, .warning))');
var forgiving = logical.matches(
 ':has(:is(.warning, :unsupported-pseudo))') &&
 logical.matches(':has(:where(:unsupported-pseudo, .warning))') &&
 !logical.matches(':has(:is(:unsupported-one, :unsupported-two))');
var strictNot = document.querySelector(
 '#logical-panel:has(:not(.warning, :unsupported-pseudo))') === null;
var ok = warningFirst && warningSecond && warningBoth && !neither &&
 whereEquivalent && notNone && duplicateAndOrder && forgiving && strictNot;
)JS", "two-member is/where OR, not NONE, forgiving and strict semantics pass");

    expectScript(harness, R"JS(
var good = document.querySelector('#good-correlated');
var bad = document.querySelector('#bad-correlated');
var logical = document.querySelector('#logical-panel');
var panel = document.querySelector('#panel');
var leaf = document.querySelector('#leaf');
var orPanel = document.querySelector('#or-panel');
var chain = good.matches(
 ':has(> .row:is(.missing, .row) > :not(.disabled, .hidden))') &&
 !bad.matches(':has(> .row:is(.missing, .row) > .invalid:not(.disabled, .hidden))');
var scope = leaf.closest(
 '.panel:has(> .row > :is(:scope, .selected))') === panel &&
 leaf.closest('.panel:has(> .row > :is(.selected, .missing))') === null;
var alternatives = orPanel.matches(
 ':has(> :is(.missing, .absent), + :where(.badge, .active))') &&
 logical.matches(':has(> :is(.warning, .missing), + :where(.active, .badge))');
var outer = document.querySelectorAll(
 '#or-panel:has(> :is(.missing, .absent), + :where(.badge, .active)), ' +
 '#logical-panel:has(> :is(.warning, .missing), + :where(.active, .badge))');
var scoped = orPanel.querySelector(
 ':has(> :is(.target, .missing))') !== null &&
 orPanel.querySelectorAll(':has(> :is(.target, .missing))').length === 1 &&
 logical.matches(':has(:is(.warning, .missing))');
var ok = chain && scope && alternatives && outer.length === 2 &&
 outer[0] === orPanel && outer[1] === logical && scoped &&
 document.querySelector('#bad-correlated:has(:is(.missing, .invalid) > .valid)') === null;
)JS", "three comma levels preserve alternatives, outer order, scope, and chain correlation");

    expectScript(harness, R"JS(
var invalid = [
 '#strict-panel:has(:is(.warning,))',
 '#strict-panel:has(:is(,.warning))',
 '#strict-panel:has(:is(.warning,,.error))',
 '#strict-panel:has(:where(.warning, .error, .missing))',
 '#strict-panel:has(:not(.disabled, .hidden, .warning))',
 '#strict-panel:has(:not(.disabled, :unsupported-pseudo))',
 '#strict-panel:has(:is(.warning, :unsupported()))',
 '#strict-panel:has(:is(.warning, :not(.disabled)))',
 '#strict-panel:has(:where(.warning, :has(.child)))',
 '#strict-panel:has(:is(.warning], .error))',
 '#strict-panel:has(:is(.warning, [data-x="unclosed]))',
 '#strict-panel:has(:is(.warning, [data-x=ready))'
];
var rejected = true;
for (var index = 0; index !== invalid.length; ++index)
 if (document.querySelector(invalid[index]) !== null) rejected = false;
var stillUsable = document.querySelector(
 '#strict-panel:has(:is(.warning, .missing))') !== null;
var ok = rejected && stillUsable;
)JS", "empty, excess, malformed, nested, and strict-invalid logical lists reject atomically");

    expectScript(harness, R"JS(
var first = document.querySelector('#good-correlated');
var second = document.querySelector('#or-panel');
var all = document.querySelectorAll(
 '#good-correlated:has(> .row:not(.valid)), #or-panel:has(+ :is(.badge))');
var ok = first.matches(':has(> .row:not(.valid), + .badge)') &&
 second.matches(':has(> .missing:not(.valid), + :is(.badge))') &&
 all.length === 2 && all[0] === second && all[1] === first &&
 document.querySelector('#panel:has(:is(.warning))') === null;
)JS", "nested logical predicates compose with two-alternative OR and outer selector lists");

    NavigatorScriptSelectorDescriptor shortCircuitStorage;
    auto& shortCircuitSelector = shortCircuitStorage.members[0].rightSimple;
    shortCircuitSelector.valid = true;
    shortCircuitSelector.statePseudo =
        NavigatorScriptStatePseudo::HasChild;
    NavigatorScriptCompactRelativeSelectorCoreDescriptor firstCore;
    firstCore.universal = true;
    firstCore.statePseudo = NavigatorScriptStatePseudo::Is;
    NavigatorScriptRelativeSelectorTestAccess::setHasInlineCore(
        shortCircuitSelector, firstCore, 0u);
    for (std::size_t index = 0u; index < 7u; ++index)
        shortCircuitStorage.text[index] = "warning"[index];
    shortCircuitStorage.textLength = 7u;
    auto& shortCircuitExpression = shortCircuitStorage.hasExpressions[0];
    shortCircuitExpression.secondFirstCoreIndex = 0u;
    shortCircuitExpression.flags = 0xC0u;
    auto& firstLogicalArgument =
        shortCircuitExpression.logicalArguments[0];
    firstLogicalArgument.flags = 0x01u;
    firstLogicalArgument.classTokenCount = 1u;
    firstLogicalArgument.classBlockLength = 7u;
    shortCircuitStorage.hasRelativeCoreCount = 1u;
    shortCircuitStorage.hasRelativeCores[0].classTokenCount =
        kNavigatorScriptMaxClassQueryTokens + 1u;
    shortCircuitStorage.hasExpressionCount = 1u;
    const HostInstanceId logicalPanelSerial = serialById(
        harness.document(), "logical-panel");
    const gxos::web::HtmlElementRef* logicalPanelElement = nullptr;
    for (const auto& candidate : harness.document().structuralElements)
        if (candidate.serial == logicalPanelSerial)
            logicalPanelElement = &candidate;
    expect(logicalPanelElement != nullptr &&
        NavigatorScriptRelativeSelectorTestAccess::simpleMatch(
            harness.hostAdapter(), *logicalPanelElement,
            shortCircuitSelector, shortCircuitStorage) ==
                NavigatorScriptSelectorMatchResult::Match,
        "nested first alternative short-circuits an invalid second core");

    expectScript(harness, R"JS(
var form = document.querySelector('#state-form');
var check = document.querySelector('#state-check');
var attr = document.querySelector('#attribute-panel');
var ready = attr.querySelector('[data-state=ready]');
var initially = !form.matches(':has(> input:is(:checked))') &&
 form.matches(':has(> input:not(:checked))') &&
 !form.matches(':has(> input:is(:focus))') &&
 attr.matches(':has(> .row:not([data-state=disabled]))');
check.checked = true;
ready.setAttribute('data-state', 'disabled');
check.focus();
var changed = form.matches(':has(> input:is(:checked))') &&
 !form.matches(':has(> input:not(:checked))') &&
 form.matches(':has(> input:is(:focus))') &&
 !attr.matches(':has(> .row:not([data-state=disabled]))');
check.checked = false;
ready.setAttribute('data-state', 'ready');
check.blur();
var restored = !form.matches(':has(> input:is(:checked))') &&
 form.matches(':has(> input:not(:checked))') &&
 !form.matches(':has(> input:is(:focus))') &&
 attr.matches(':has(> .row:not([data-state=disabled]))');
var ok = initially && changed && restored;
)JS", "nested logical predicates read live checked, focus, and attribute state");

    expectScript(harness, R"JS(
var firstLeaf = document.querySelector('#first-leaf');
var otherLeaf = document.querySelector('#other-leaf');
var firstPanel = document.querySelector('#first-panel');
var ok = firstLeaf.closest('.panel:has(:scope > .note)') === firstPanel &&
 otherLeaf.closest('.panel:has(:scope > .note)') === null &&
 firstLeaf.matches('span:scope') &&
 !firstLeaf.matches('div:scope') &&
 firstLeaf.closest('.panel:has(:scope > .note)') === firstPanel;
)JS", "first-step and compound scope use the API receiver");

    expectScript(harness, R"JS(
var leaf = document.querySelector('#leaf');
var container = document.querySelector('#container');
var directLeaf = document.querySelector('#direct-leaf');
var ok = !leaf.matches(':has(:scope)') &&
 !leaf.matches(':has(> :scope)') &&
 directLeaf.matches('.container:has(> :scope) > .child') &&
 leaf.matches('.missing, .leaf') &&
 directLeaf.closest('.container') === container;
)JS", "matches self-exclusion and outer relation matching");

    expectScript(harness, R"JS(
var root = document.querySelector('#query-root');
var target = document.querySelector('#target');
var nestedRoot = document.querySelector('#nested-query-root');
var nestedTarget = document.querySelector('#nested-target');
var direct = root.querySelector('.ancestor:has(> :scope) .target');
var all = root.querySelectorAll('.ancestor:has(> :scope) .target');
var descendant = root.querySelector('.ancestor:has(:scope) .target');
var nestedDescendant = nestedRoot.querySelector('.ancestor:has(:scope) .target');
var nestedFalse = nestedRoot.querySelector('.ancestor:has(> :scope) .target');
var ok = direct === target && all.length === 1 && all[0] === target &&
 descendant === target && nestedDescendant === nestedTarget && nestedFalse === null &&
 root.querySelector(':scope') === null && root.querySelectorAll(':scope').length === 0;
)JS", "outer query matching keeps the return domain inside the query root");

    expectScript(harness, R"JS(
var root = document.querySelector('#query-root');
var saved = root.querySelectorAll('.ancestor:has(> :scope) .target');
var secondRoot = document.querySelector('#nested-query-root');
var second = secondRoot.querySelectorAll('.ancestor:has(:scope) .target');
document.querySelector('#leaf').matches(':scope');
var ok = saved.length === 1 && saved[0] === document.querySelector('#target') &&
 second.length === 1 && second[0] === document.querySelector('#nested-target') &&
 saved[0] === document.querySelector('#target');
)JS", "retained collections keep independent scope contexts after later queries");

    expectScript(harness, R"JS(
var root = document.querySelector('#or-root');
var target = root.querySelector('.target');
var forward = root.querySelectorAll(
 '.panel:has(> :scope, + .badge) .target, .target, .panel:has(+ .badge, > :scope) .target');
var secondAlt = root.querySelector('.panel:has(> .missing > :scope, > :scope) .target');
var ok = target !== null && forward.length === 1 && forward[0] === target &&
 secondAlt === target;
)JS", "two bounded alternatives preserve OR behavior and shared API scope");

    expectScript(harness, R"JS(
var scope = document.querySelector('#sibling-scope');
var adjacent = document.querySelector('#adjacent-scope');
var general = document.querySelector('#general-scope');
var ok = scope.closest('.sibling-parent:has(:scope + .peer)') ===
 document.querySelector('#sibling-parent') &&
 scope.closest('.sibling-parent:has(:scope ~ .peer)') ===
 document.querySelector('#sibling-parent') &&
 adjacent.matches('.adjacent-anchor:has(+ :scope) + .scope') &&
 general.matches('.general-anchor:has(~ :scope) ~ .scope');
)JS", "explicit scope works as a forward sibling target and source");

    expectScript(harness, R"JS(
var invalid = [':has()', ':has(:scope())', ':has(:scope(anything))',
 ':has(:scope:focus)', ':has(:has(:scope))',
 ':has(:scope .a .b)',
 ':has(> .a > :scope > .b)', ':has(.a, .b, :scope)', ':has(:is())',
 ':has(:where())', ':has(:not())', ':has(:is(:not(.a)))',
 ':has(:where(:is(.a)))', ':has(:not(:where(.a)))',
 ':has(:not(:has(.a)))', ':has(:has(.a))',
 ':has(:is(.a, .b, .c))', ':has(:where(.a, .b, .c))',
 ':has(:not(.a, .b, .c))', ':has(.a:is(.b):focus)',
 ':has(:not(.a) .b .c)', ':has(:is(.a), .b, .c)',
 ':has(> .a > :not(.b) > .c)', ':has(:is(.a)'];
var rejected = true;
for (var i = 0; i !== invalid.length; i++)
 if (document.querySelector(invalid[i]) !== null) rejected = false;
var listRejected = document.querySelector(
 '#list-a:has(:is(.a, .b, .c))') === null && document.querySelector(
 '#list-ab:has(:is(.a, .b, .c))') === null && document.querySelector(
 '#list-a:has(:not(.a, .b, .c))') === null;
var leaf = document.querySelector('#leaf');
var panel = document.querySelector('#panel');
var logical = document.querySelector('#logical-panel');
var sequence = !panel.matches(':has(:is(:not(.leaf)))') &&
 leaf.matches(':scope') && panel.matches(':has(> .row > :is(:scope))') === false &&
 logical.matches(':has(:is(.warning))');
var ok = rejected && listRejected && sequence &&
 document.querySelector('#leaf:has(> :scope)') === null;
)JS", "nested functions, multiple pseudos, excess steps, and alternatives stay rejected");

    testMaximumNestedSelectorBounds();

    for (unsigned first = 0u; first < 4u; ++first) {
        for (unsigned second = 0u; second < 4u; ++second) {
            const auto pairHarnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
            auto& pairHarness = *pairHarnessStorage;
            error = RuntimeErrorCode::None;
            const std::string pairHtml = relationPairHtml(first, second);
            const bool loaded = pairHarness.loadHtml("file:///js71-pair.html",
                pairHtml, error) && error == RuntimeErrorCode::None;
            const HostInstanceId anchorSerial = serialById(
                pairHarness.document(), "anchor");
            const HostInstanceId scopeSerial = serialById(
                pairHarness.document(), "scope");
            NavigatorScriptSimpleSelectorCoreDescriptor firstCore;
            firstCore.valid = true;
            firstCore.universal = true;
            NavigatorScriptSimpleSelectorCoreDescriptor scopeCore;
            scopeCore.valid = true;
            scopeCore.statePseudo = NavigatorScriptStatePseudo::Scope;
            NavigatorScriptSelectorDescriptor storage;
            const auto context =
                NavigatorScriptRelativeSelectorTestAccess::contextForScope(
                    pairHarness.hostAdapter(), scopeSerial);
            const HostObjectReference anchor{anchorSerial,
                pairHarness.hostAdapter().generation(),
                kNavigatorElementHostKind};
            const auto result = loaded && anchorSerial != 0u &&
                    scopeSerial != 0u && context.valid
                ? NavigatorScriptRelativeSelectorTestAccess::relativeChain(
                    pairHarness.hostAdapter(), anchor, relationAt(first),
                    firstCore, relationAt(second), scopeCore, storage, context)
                : NavigatorScriptSelectorMatchResult::Invalid;
            const std::string label = "scope terminal relation pair " +
                std::to_string(first) + "/" + std::to_string(second);
            expect(loaded && result == NavigatorScriptSelectorMatchResult::Match,
                label.c_str());
            const std::array<const char*, 4u> relationText{{
                " ", " > ", " + ", " ~ "}};
            std::string logicalSelector =
                "var anchor = document.querySelector('#anchor');"
                "var ok = anchor.matches(':has(";
            logicalSelector += relationText[first];
            logicalSelector += ":is(.middle)";
            logicalSelector += relationText[second];
            logicalSelector += ":not(.missing))');";
            const std::string logicalLabel =
                "logical relation pair " + std::to_string(first) + "/" +
                std::to_string(second);
            expectScript(pairHarness, logicalSelector, logicalLabel.c_str());
        }
    }

    const auto eventHarnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& eventHarness = *eventHarnessStorage;
    error = RuntimeErrorCode::None;
    expect(eventHarness.loadHtml("file:///js71-events.html", kJs69Html, error) &&
        error == RuntimeErrorCode::None, "JS71 Event fixture loads");
    expectScript(eventHarness, R"JS(
var leaf = document.querySelector('#event-leaf');
var leafB = document.querySelector('#event-leaf-b');
var panel = document.querySelector('#event-panel');
var panelB = document.querySelector('#event-panel-b');
var eventAResult = false;
var eventBResult = false;
var eventMetadata = false;
document.querySelector('#event-a').addEventListener('click', function(event) {
 var savedTarget = event.target;
 var savedCurrent = event.currentTarget;
 var savedPhase = event.eventPhase;
 var savedDefault = event.defaultPrevented;
 eventAResult = panel.matches(':has(> .row > :not(.disabled))');
 document.querySelector('#event-b').click();
 eventAResult = eventAResult &&
  panel.matches(':has(> .row > :not(.disabled))');
 eventMetadata = event.target === savedTarget &&
  event.currentTarget === savedCurrent && event.eventPhase === savedPhase &&
  event.defaultPrevented === savedDefault;
});
document.querySelector('#event-b').addEventListener('click', function(event) {
 eventBResult = document.querySelector('#event-b').matches(
  ':has(+ :is(.selected))') && leafB.closest('.panel') === panelB &&
  event.target === document.querySelector('#event-b') &&
  event.currentTarget === document.querySelector('#event-b') &&
  event.eventPhase === 2 && event.defaultPrevented === false;
});
var ok = true;
)JS", "nested Event handlers install independently scoped selectors");
    const HostInstanceId eventASerial = serialById(eventHarness.document(), "event-a");
    expect(eventASerial != 0u && eventHarness.dispatchClick(eventASerial, error) &&
        error == RuntimeErrorCode::None,
        "nested JS71 Event dispatch completes");
    const Value* eventAResult = eventHarness.runtime().lookup(
        SourceView("eventAResult", 12u));
    const Value* eventBResult = eventHarness.runtime().lookup(
        SourceView("eventBResult", 12u));
    const Value* eventMetadata = eventHarness.runtime().lookup(
        SourceView("eventMetadata", 13u));
    expect(eventAResult != nullptr && eventAResult->isBoolean() &&
        eventAResult->booleanValue() && eventBResult != nullptr &&
        eventBResult->isBoolean() && eventBResult->booleanValue() &&
        eventMetadata != nullptr && eventMetadata->isBoolean() &&
        eventMetadata->booleanValue(),
        "nested dispatch preserves both API scopes and outer Event metadata");
    if (eventAResult == nullptr || !eventAResult->isBoolean() ||
        !eventAResult->booleanValue() || eventBResult == nullptr ||
        !eventBResult->isBoolean() || !eventBResult->booleanValue() ||
        eventMetadata == nullptr || !eventMetadata->isBoolean() ||
        !eventMetadata->booleanValue()) {
        const auto reportEventValue = [](const char* name, const Value* value) {
            std::cerr << "JS71 " << name << "=";
            if (value == nullptr) std::cerr << "<missing>";
            else if (value->isBoolean())
                std::cerr << (value->booleanValue() ? "true" : "false");
            else std::cerr << valueTypeName(value->type());
            std::cerr << '\n';
        };
        reportEventValue("eventAResult", eventAResult);
        reportEventValue("eventBResult", eventBResult);
        reportEventValue("eventMetadata", eventMetadata);
    }

    const auto replacementHarnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& replacementHarness = *replacementHarnessStorage;
    error = RuntimeErrorCode::None;
    expect(replacementHarness.loadHtml("file:///js71-old.html",
        "<html><body><div class='ancestor'><section id='panel' class='panel'>"
        "<div class='row'><span id='leaf'></span></div><i class='target'></i>"
        "</section></div></body></html>", error) &&
        error == RuntimeErrorCode::None, "old generation JS71 fixture loads");
    const HostGenerationId oldGeneration =
        replacementHarness.hostAdapter().generation();
    const HostInstanceId oldLeafSerial = serialById(
        replacementHarness.document(), "leaf");
    const auto oldContext =
        NavigatorScriptRelativeSelectorTestAccess::contextForScope(
            replacementHarness.hostAdapter(), oldLeafSerial);
    expectScript(replacementHarness,
        "var leaf = document.querySelector('#leaf');"
        "var panel = document.querySelector('#panel');"
        "var collection = panel.querySelectorAll("
        "'.ancestor:has(> :scope) .target');"
        "var ok = leaf.closest('.panel:has(> .row > :scope)') === panel && "
        "collection.length === 1;",
        "old generation positive scope and retained collection");
    error = RuntimeErrorCode::None;
    expect(replacementHarness.replaceHtml("file:///js71-new.html",
        "<html><body><div class='ancestor'><section id='panel' class='panel'>"
        "<div class='row'><span id='leaf'></span></div><i class='target'></i>"
        "</section></div></body></html>", error) &&
        error == RuntimeErrorCode::None, "document generation replacement succeeds");
    const HostInstanceId newLeafSerial = serialById(
        replacementHarness.document(), "leaf");
    const HostObjectReference oldCollection{1u, oldGeneration,
        kNavigatorSelectorCollectionHostKind};
    const HostResult oldCollectionResult = replacementHarness.hostAdapter().
        validate(oldCollection);
    const HostInstanceId newPanelSerial = serialById(
        replacementHarness.document(), "panel");
    const auto* newLeaf = newLeafSerial == 0u ? nullptr :
        &replacementHarness.document().structuralElements[
            static_cast<std::size_t>(newLeafSerial - 1u)];
    NavigatorScriptSimpleSelectorCoreDescriptor scopeCore;
    scopeCore.valid = true;
    scopeCore.statePseudo = NavigatorScriptStatePseudo::Scope;
    NavigatorScriptSimpleSelectorCoreDescriptor notCore;
    notCore.valid = true;
    notCore.universal = true;
    notCore.statePseudo = NavigatorScriptStatePseudo::Not;
    const HostObjectReference currentPanelRef{newPanelSerial,
        replacementHarness.hostAdapter().generation(),
        kNavigatorElementHostKind};
    expect(oldGeneration != replacementHarness.hostAdapter().generation() &&
        oldLeafSerial == newLeafSerial && newLeaf != nullptr &&
        oldCollectionResult.code == HostResultCode::StaleObject &&
        NavigatorScriptRelativeSelectorTestAccess::coreMatch(
            replacementHarness.hostAdapter(), *newLeaf, scopeCore,
            NavigatorScriptSelectorDescriptor(), oldContext) ==
                NavigatorScriptSelectorMatchResult::Invalid &&
        NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            replacementHarness.hostAdapter(), currentPanelRef,
            NavigatorScriptRelativeSelectorRelation::Descendant, notCore,
            scopeCore, NavigatorScriptSelectorDescriptor(), oldContext) ==
                NavigatorScriptSelectorMatchResult::Invalid,
        "same-serial replacement rejects stale contexts before :not inversion");
    expectScript(replacementHarness,
        "var leaf = document.querySelector('#leaf');"
        "var panel = document.querySelector('#panel');"
        "var rows = panel.querySelectorAll("
        "'.ancestor:has(> :scope) .target');"
        "var ok = leaf.closest('.panel:has(> .row > :scope)') === panel && "
        "rows.length === 1;",
        "new generation establishes a fresh scope without stale leakage");

    const auto corruptHarnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& corruptHarness = *corruptHarnessStorage;
    error = RuntimeErrorCode::None;
    expect(corruptHarness.loadHtml("file:///js71-corrupt.html",
        "<html><body><section id='panel' class='panel'><div id='row' class='row'>"
        "<span id='leaf'></span></div></section></body></html>", error) &&
        error == RuntimeErrorCode::None, "structural corruption fixture loads");
    const HostInstanceId rowSerial = serialById(corruptHarness.document(), "row");
    if (rowSerial != 0u)
        corruptHarness.document().structuralElements[
            static_cast<std::size_t>(rowSerial - 1u)].parentSerial = 9999u;
    expectScript(corruptHarness,
        "var leaf = document.querySelector('#leaf');"
        "var panel = document.querySelector('#panel');"
        "var ok = leaf.closest('.panel:has(> .row > :scope)') === null && "
        "!panel.matches(':has(> .row:not(.valid))');",
        "invalid parent index cannot satisfy a positive or negated target");
    if (rowSerial != 0u)
        corruptHarness.document().structuralElements[
            static_cast<std::size_t>(rowSerial - 1u)].serial = 9999u;
    expectScript(corruptHarness,
        "var panel = document.querySelector('#panel');"
        "var ok = !panel.matches(':has(> :not(.invalid))');",
        "out-of-range structural serial cannot satisfy :not()");

    const auto capacityHarnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& capacityHarness = *capacityHarnessStorage;
    std::string capacityHtml = "<html><body><section id='panel' class='panel'>";
    for (std::size_t index = 0u; index < 495u; ++index)
        capacityHtml += "<div class='row'><i></i></div>";
    capacityHtml += "<div class='row'><div id='query-root'>";
    for (std::size_t index = 0u; index < 15u; ++index)
        capacityHtml += index == 0u
            ? "<i id='first-target' class='target'></i>"
            : "<i class='target'></i>";
    capacityHtml += "</div>";
    for (std::size_t index = 0u; index < 14u; ++index)
        capacityHtml += "<i></i>";
    capacityHtml += "</section></body></html>";
    error = RuntimeErrorCode::None;
    expect(capacityHarness.loadHtml("file:///js71-capacity.html", capacityHtml,
        error) && error == RuntimeErrorCode::None &&
        capacityHarness.document().structuralElements.size() ==
            kNavigatorScriptMaxDocumentNodes,
        "near-capacity scope fixture reaches 1,024 Elements");
    const auto capacityStart = std::chrono::steady_clock::now();
    expectScript(capacityHarness, R"JS(
var root = document.querySelector('#query-root');
var selector = '.panel:has(> .row > :is(.missing, .absent), ' +
 ' > .row > :where(:scope, .missing)) .target';
var all = root.querySelectorAll(selector);
var retained = all;
var repeated = root.querySelectorAll(selector);
var first = root.querySelector(selector);
var ok = all.length === 15 && repeated.length === 15 &&
 first === all[0] && retained[14] === repeated[14] &&
 root.querySelector(':scope') === null;
)JS", "near-capacity two-alternative outer matching and result domain");
    const auto capacityElapsed = std::chrono::duration_cast<
        std::chrono::microseconds>(std::chrono::steady_clock::now() -
            capacityStart).count();
    const HostInstanceId capacityScope = serialById(
        capacityHarness.document(), "query-root");
    const HostInstanceId capacityAnchor = serialById(
        capacityHarness.document(), "panel");
    NavigatorScriptSimpleSelectorCoreDescriptor rowCore;
    rowCore.valid = true;
    rowCore.classTokenCount = 1u;
    rowCore.classTokens[0].offset = 0u;
    rowCore.classTokens[0].length = 3u;
    NavigatorScriptSelectorDescriptor capacityStorage;
    capacityStorage.text[0] = 'r';
    capacityStorage.text[1] = 'o';
    capacityStorage.text[2] = 'w';
    capacityStorage.text[3] = '\0';
    const char missingText[] = "missing";
    for (std::size_t index = 0u; index < sizeof(missingText); ++index)
        capacityStorage.text[4u + index] = missingText[index];
    const char absentText[] = "absent";
    for (std::size_t index = 0u; index < sizeof(absentText); ++index)
        capacityStorage.text[12u + index] = absentText[index];
    capacityStorage.textLength = 12u + sizeof(absentText);
    rowCore.statePseudo = NavigatorScriptStatePseudo::Is;
    NavigatorScriptSimpleSelectorCoreDescriptor rowArgument;
    rowArgument.valid = true;
    rowArgument.classTokenCount = 1u;
    rowArgument.classTokens[0].offset = 0u;
    rowArgument.classTokens[0].length = 3u;
    NavigatorScriptSimpleSelectorCoreDescriptor missingArgument;
    missingArgument.valid = true;
    missingArgument.classTokenCount = 1u;
    missingArgument.classTokens[0].offset = 4u;
    missingArgument.classTokens[0].length = 7u;
    NavigatorScriptSimpleSelectorCoreDescriptor absentArgument;
    absentArgument.valid = true;
    absentArgument.classTokenCount = 1u;
    absentArgument.classTokens[0].offset = 12u;
    absentArgument.classTokens[0].length = 6u;
    NavigatorScriptSimpleSelectorCoreDescriptor terminalScope;
    terminalScope.valid = true;
    terminalScope.statePseudo = NavigatorScriptStatePseudo::Scope;
    NavigatorScriptSimpleSelectorCoreDescriptor terminalIsScope;
    terminalIsScope.valid = true;
    terminalIsScope.universal = true;
    terminalIsScope.statePseudo = NavigatorScriptStatePseudo::Is;
    const auto capacityContext =
        NavigatorScriptRelativeSelectorTestAccess::contextForScope(
            capacityHarness.hostAdapter(), capacityScope);
    const HostObjectReference capacityAnchorRef{capacityAnchor,
        capacityHarness.hostAdapter().generation(), kNavigatorElementHostKind};
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    NavigatorScriptRelativeSelectorCounters counters;
#endif
    const auto capacityRelative =
        NavigatorScriptRelativeSelectorTestAccess::relativeChain(
            capacityHarness.hostAdapter(), capacityAnchorRef,
            NavigatorScriptRelativeSelectorRelation::Child, rowCore,
            NavigatorScriptRelativeSelectorRelation::Child, terminalIsScope,
            capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &counters
#endif
            , &missingArgument, &missingArgument,
            &rowArgument, &terminalScope
            );
    expect(capacityRelative == NavigatorScriptSelectorMatchResult::Match,
        "near-capacity two-step :is(:scope) finds the correlated late scope");

    const HostInstanceId firstTargetSerial = serialById(
        capacityHarness.document(), "first-target");
    const HostObjectReference firstTargetRef{firstTargetSerial,
        capacityHarness.hostAdapter().generation(), kNavigatorElementHostKind};
    NavigatorScriptSimpleSelectorCoreDescriptor whereScopeCore =
        terminalIsScope;
    whereScopeCore.statePseudo = NavigatorScriptStatePseudo::Where;
    NavigatorScriptSimpleSelectorCoreDescriptor capacityNotCore = terminalIsScope;
    capacityNotCore.statePseudo = NavigatorScriptStatePseudo::Not;
    NavigatorScriptSimpleSelectorCoreDescriptor universalArgument;
    universalArgument.valid = true;
    universalArgument.universal = true;
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    NavigatorScriptRelativeSelectorCounters earlyIsCounters;
    NavigatorScriptRelativeSelectorCounters lateIsCounters;
    NavigatorScriptRelativeSelectorCounters isBothFalseCounters;
    NavigatorScriptRelativeSelectorCounters whereBothFalseCounters;
    NavigatorScriptRelativeSelectorCounters notFirstCounters;
    NavigatorScriptRelativeSelectorCounters notSecondCounters;
    NavigatorScriptRelativeSelectorCounters notBothFalseCounters;
    NavigatorScriptRelativeSelectorCounters lateWhereCounters;
    NavigatorScriptRelativeSelectorCounters earlyNotCounters;
    NavigatorScriptRelativeSelectorCounters fullFalseNotCounters;
    NavigatorScriptRelativeSelectorCounters noTargetCounters;
#endif
    const auto earlyIs = NavigatorScriptRelativeSelectorTestAccess::relativeOne(
        capacityHarness.hostAdapter(), capacityAnchorRef,
        NavigatorScriptRelativeSelectorRelation::Descendant, rowCore,
        rowArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
        , &earlyIsCounters
#endif
        , &missingArgument
        );
    const auto lateIs =
        NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            capacityHarness.hostAdapter(), capacityAnchorRef,
            NavigatorScriptRelativeSelectorRelation::Descendant,
            terminalIsScope, missingArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &lateIsCounters
#endif
            , &terminalScope);
    const auto isBothFalse =
        NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            capacityHarness.hostAdapter(), capacityAnchorRef,
            NavigatorScriptRelativeSelectorRelation::Child, rowCore,
            missingArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &isBothFalseCounters
#endif
            , &absentArgument);
    const auto whereBothFalse =
        NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            capacityHarness.hostAdapter(), capacityAnchorRef,
            NavigatorScriptRelativeSelectorRelation::Child, whereScopeCore,
            missingArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &whereBothFalseCounters
#endif
            , &absentArgument);
    const auto lateWhere =
        NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            capacityHarness.hostAdapter(), capacityAnchorRef,
            NavigatorScriptRelativeSelectorRelation::Descendant,
            whereScopeCore, terminalScope, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &lateWhereCounters
#endif
            );
    const auto earlyNot = NavigatorScriptRelativeSelectorTestAccess::relativeOne(
        capacityHarness.hostAdapter(), capacityAnchorRef,
        NavigatorScriptRelativeSelectorRelation::Child, capacityNotCore,
        rowArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
        , &earlyNotCounters
#endif
        , &missingArgument
        );
    const auto secondNot =
        NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            capacityHarness.hostAdapter(), capacityAnchorRef,
            NavigatorScriptRelativeSelectorRelation::Child, capacityNotCore,
            missingArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &notSecondCounters
#endif
            , &rowArgument);
    const auto bothFalseNot =
        NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            capacityHarness.hostAdapter(), capacityAnchorRef,
            NavigatorScriptRelativeSelectorRelation::Child, capacityNotCore,
            missingArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &notBothFalseCounters
#endif
            , &absentArgument);
    const auto fullFalseNot =
        NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            capacityHarness.hostAdapter(), capacityAnchorRef,
            NavigatorScriptRelativeSelectorRelation::Descendant, capacityNotCore,
            universalArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &fullFalseNotCounters
#endif
            );
    const auto noTarget = firstTargetSerial == 0u
        ? NavigatorScriptSelectorMatchResult::Invalid
        : NavigatorScriptRelativeSelectorTestAccess::relativeOne(
            capacityHarness.hostAdapter(), firstTargetRef,
            NavigatorScriptRelativeSelectorRelation::Descendant, capacityNotCore,
            rowArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &noTargetCounters
#endif
            );
    expect(earlyIs == NavigatorScriptSelectorMatchResult::Match &&
        lateIs == NavigatorScriptSelectorMatchResult::Match &&
        isBothFalse == NavigatorScriptSelectorMatchResult::NoMatch &&
        whereBothFalse == NavigatorScriptSelectorMatchResult::NoMatch &&
        lateWhere == NavigatorScriptSelectorMatchResult::Match &&
        earlyNot == NavigatorScriptSelectorMatchResult::NoMatch &&
        secondNot == NavigatorScriptSelectorMatchResult::NoMatch &&
        bothFalseNot == NavigatorScriptSelectorMatchResult::Match &&
        fullFalseNot == NavigatorScriptSelectorMatchResult::NoMatch &&
        noTarget == NavigatorScriptSelectorMatchResult::NoMatch,
        "near-capacity early/late is/where, existential not, full-false, and zero-target workloads stay bounded");
    std::cout << "JS71 near-capacity: elements=" <<
        capacityHarness.document().structuralElements.size() <<
        ", scope-serial=" << capacityScope << ", outer-candidates=15";
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    const auto printWorkloadCounters = [](const char* label,
        const NavigatorScriptRelativeSelectorCounters& workload) {
        std::cout << ", " << label << "{calls=" <<
            workload.relativeEvaluationCalls << ",records=" <<
            workload.structuralRecordsInspected << ",parent-hops=" <<
            workload.parentHops << ",serial-index-resolutions=" <<
            workload.serialIndexResolutions << ",simple-tests=" <<
            workload.simpleSelectorEvaluations << ",logical-tests=" <<
            workload.logicalPredicateEvaluations << ",early-exits=" <<
            workload.earlyExits << "}";
    };
    printWorkloadCounters("two-step-late-scope", counters);
    printWorkloadCounters("is-early", earlyIsCounters);
    printWorkloadCounters("is-second-late", lateIsCounters);
    printWorkloadCounters("is-both-false", isBothFalseCounters);
    printWorkloadCounters("where-both-false", whereBothFalseCounters);
    printWorkloadCounters("where-late", lateWhereCounters);
    printWorkloadCounters("not-first-excluded", earlyNotCounters);
    printWorkloadCounters("not-second-excluded", notSecondCounters);
    printWorkloadCounters("not-both-unexcluded", notBothFalseCounters);
    printWorkloadCounters("not-full-false", fullFalseNotCounters);
    printWorkloadCounters("not-zero-target", noTargetCounters);
#endif
    std::cout << ", query-workload=" << capacityElapsed << "us\n";

    static_assert(kNavigatorScriptMaxSelectorLength == 256u &&
        kNavigatorScriptMaxSelectorListMembers == 4u &&
        kNavigatorScriptMaxHasExpressions == 8u &&
        kNavigatorScriptMaxHasRelativeCores == 24u &&
        kNavigatorScriptMaxSelectorCollections == 128u &&
        kNavigatorScriptMaxDocumentNodes == 1024u,
        "JS71 preserves all JS69 parser, pool, and collection capacities");

    auto storageHarnessStorage =
        std::make_unique<NavigatorScriptExecutionHarness>();
    auto& storageHarness = *storageHarnessStorage;
    RuntimeErrorCode storageError = RuntimeErrorCode::None;
    expect(storageHarness.loadHtml("file:///js71-storage.html",
            "<html><body><div id='root'></div><i></i></body></html>",
            storageError) && storageError == RuntimeErrorCode::None,
        "maximum-storage fixture loads into the real document model");
    const std::string fullPoolMember =
        "a:has(>a>b,+c>d)>a:has(>a>b,+c>d)";
    std::string fullPoolSelector = fullPoolMember;
    for (std::size_t index = 1u;
            index < kNavigatorScriptMaxSelectorListMembers; ++index) {
        fullPoolSelector += ",";
        fullPoolSelector += fullPoolMember;
    }
    const ScriptResult fullPoolResult = storageHarness.execute(
        "var fullPool = document.querySelectorAll('" + fullPoolSelector +
        "'); var fullPoolAgain = document.querySelectorAll('" +
        fullPoolSelector + "'); var ok = fullPool.length === 0 && "
        "fullPoolAgain.length === 0;");
    expect(fullPoolResult.succeeded() &&
        fullPoolSelector.size() <= kNavigatorScriptMaxSelectorLength &&
        storageHarness.runtime().lookup(SourceView("ok", 2u)) != nullptr &&
        storageHarness.runtime().lookup(SourceView("ok", 2u))->isBoolean() &&
        storageHarness.runtime().lookup(SourceView("ok", 2u))->booleanValue() &&
        NavigatorScriptRelativeSelectorTestAccess::hasDescriptorShape(
            storageHarness.hostAdapter(), 24u, 8u, 4u, 0u) &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            storageHarness.hostAdapter()) == 1u &&
        NavigatorScriptRelativeSelectorTestAccess::maximumRelativeCoreCount(
            storageHarness.hostAdapter()) == 24u,
        "four outer members fill all 8 :has() records and 24 owned core slots");

    const std::string fullLogicalSelector =
        "a:has(>:is(a,b)>:not(c,d),+:where(e,f)>:is(g,h))";
    const ScriptResult fullLogicalResult = storageHarness.execute(
        "var fullLogical = document.querySelectorAll('" +
        fullLogicalSelector + "'); var ok = fullLogical.length === 0;");
    expect(fullLogicalResult.succeeded() &&
        fullLogicalSelector.size() <= kNavigatorScriptMaxSelectorLength &&
        storageHarness.runtime().lookup(SourceView("ok", 2u)) != nullptr &&
        storageHarness.runtime().lookup(SourceView("ok", 2u))->isBoolean() &&
        storageHarness.runtime().lookup(SourceView("ok", 2u))->booleanValue() &&
        NavigatorScriptRelativeSelectorTestAccess::hasDescriptorShape(
            storageHarness.hostAdapter(), 3u, 1u, 1u, 8u),
        "one expression retains two logical members on all four relative cores");

    auto saturationHarnessStorage =
        std::make_unique<NavigatorScriptExecutionHarness>();
    auto& saturationHarness = *saturationHarnessStorage;
    RuntimeErrorCode saturationError = RuntimeErrorCode::None;
    expect(saturationHarness.loadHtml("file:///js71-saturation.html",
            "<html><body><div></div></body></html>", saturationError) &&
        saturationError == RuntimeErrorCode::None,
        "collection saturation fixture loads");
    const ScriptResult first127 = saturationHarness.execute(R"JS(
var capacityOk = true;
for (var n = 0; n < 127; n = n + 1)
  if (document.querySelectorAll('[data-probe=v' + n + '], .missing').length !== 0)
    capacityOk = false;
var capacity127 = document.querySelectorAll('[data-probe=v126], .missing');
var ok = capacityOk && capacity127.length === 0;
)JS");
    const ScriptResult slot128 = saturationHarness.execute(
        "var capacity128 = document.querySelectorAll("
        "'[data-probe=last], .missing');");
    const ScriptResult slot129 = saturationHarness.execute(
        "var capacity129 = document.querySelectorAll("
        "'[data-probe=overflow], .missing');");
    expect(first127.succeeded() && slot128.succeeded() &&
        !slot129.succeeded() &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            saturationHarness.hostAdapter()) == 128u,
        "all 128 collection records remain available and overflow stays bounded");

    const auto fixtureHarnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& fixtureHarness = *fixtureHarnessStorage;
    error = RuntimeErrorCode::None;
    std::ifstream fixtureFile("navigator-smoke/javascript-js71.html",
        std::ios::binary);
    const std::string fixtureHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = fixtureHtml.find("<script>");
    const std::size_t scriptClose = fixtureHtml.find("</script>", scriptOpen);
    const bool fixtureLoaded = !fixtureHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        fixtureHarness.loadHtml("file:///js71-hosted-fixture.html", fixtureHtml,
            error) && error == RuntimeErrorCode::None;
    expect(fixtureLoaded, "hosted JS71 fixture loads in native harness");
    if (fixtureLoaded) {
        const std::string program = fixtureHtml.substr(scriptOpen + 8u,
            scriptClose - scriptOpen - 8u);
        const auto& capturedScripts = fixtureHarness.document().scriptSources;
        const bool scriptCapturedIntact = capturedScripts.size() == 1u &&
            capturedScripts[0] == program;
        expect(scriptCapturedIntact,
            "hosted JS71 inline script is preserved by the HTML parser");
        const ScriptResult result = scriptCapturedIntact
            ? fixtureHarness.execute(capturedScripts[0]) : ScriptResult();
        const Value* assertions = fixtureHarness.runtime().lookup(
            SourceView("assertions", 10u));
        const Value* fixtureFailures = fixtureHarness.runtime().lookup(
            SourceView("failures", 8u));
        const Value* failedIndexes = fixtureHarness.runtime().lookup(
            SourceView("failedIndexes", 13u));
        if (fixtureFailures != nullptr && fixtureFailures->isNumber() &&
            fixtureFailures->numberValue() != 0.0 && failedIndexes != nullptr &&
            failedIndexes->isString()) {
            std::cerr << "JS71 hosted fixture failed assertion indexes: "
                << fixtureHarness.runtime().stringValue(*failedIndexes) << '\n';
        }
        expect(result.succeeded() && assertions != nullptr &&
            assertions->isNumber() && assertions->numberValue() >= 20.0 &&
            fixtureFailures != nullptr && fixtureFailures->isNumber() &&
            fixtureFailures->numberValue() == 0.0,
            "hosted JS71 fixture initial assertions pass in native harness");
        if (assertions != nullptr && assertions->isNumber())
            std::cout << "JS71 hosted fixture native assertions=" <<
                assertions->numberValue() << "\n";
    }

    testFilteredNthSelectors();

    std::cout << "JS71 bounded structure bytes: match-context="
        << sizeof(NavigatorScriptSelectorMatchContext)
        << ", selector-descriptor=" << sizeof(NavigatorScriptSelectorDescriptor)
        << ", collection-record="
        << NavigatorScriptRelativeSelectorTestAccess::collectionRecordBytes()
        << ", registry=" << NavigatorScriptRelativeSelectorTestAccess::
            collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections
        << ", HtmlElementRef=" << sizeof(gxos::web::HtmlElementRef)
        << ", HtmlElementContentMetadata="
        << sizeof(gxos::web::HtmlElementContentMetadata) << "\n";
    std::cout << "Navigator JavaScript JS71 checks: " << checks - failures
        << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
