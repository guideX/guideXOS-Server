#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace gxos::javascript {

struct NavigatorScriptRelativeSelectorTestAccess {
    static constexpr std::size_t collectionRecordBytes()
    {
        return sizeof(NavigatorScriptHostAdapter::SelectorCollectionRecord);
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
        )
    {
        return adapter.selectorRelativeChainMatchResult(anchor, firstRelation,
            firstSelector, secondRelation, secondSelector, storage
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , counters
#endif
            , context);
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
            "leaf", "panel", "collection", "ok"}) {
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
        html += "<div id='middle'>";
    } else {
        html += "<div id='anchor'></div>";
        if (first == 3u) html += "<i class='gap'></i>";
        html += "<div id='middle'>";
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

const char* const kJs67Html =
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
    "</span><button id='event-b'></button></div></div>"
    "</body></html>";

} // namespace

int main()
{
    static_assert(sizeof(NavigatorScriptSimpleSelectorCoreDescriptor) == 32u,
        "JS67 does not grow the shared simple-selector core");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor) == 68u,
        "JS67 does not grow the outer simple-selector descriptor");
    static_assert(sizeof(NavigatorScriptCompactRelativeSelectorCoreDescriptor) ==
        18u, "JS67 preserves compact relative cores");
    static_assert(sizeof(NavigatorScriptHasExpressionDescriptor) == 76u,
        "JS67 preserves JS65 expression metadata");
    static_assert(sizeof(NavigatorScriptSelectorMatchContext) == 16u,
        "JS67 reuses the call-local JS66 match context");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1856u,
        "JS67 does not grow the bounded selector descriptor");
    static_assert(sizeof(gxos::web::HtmlElementRef) == 440u,
        "JS67 does not grow document structural records");
    static_assert(sizeof(gxos::web::HtmlElementContentMetadata) == 24u,
        "JS67 does not grow document content metadata");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() == 1872u,
        "JS67 does not grow retained selector collection records");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections ==
            239616u,
        "JS67 does not grow the 128-entry collection registry");

    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js67.html", kJs67Html, error) &&
        error == RuntimeErrorCode::None, "JS67 structural fixture loads");

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
 ':has(:is(:not(:scope)))', ':has(:not(:where(:scope)))',
 ':has(:where(:is(:scope)))',
 ':has(:scope .a .b)',
 ':has(> .a > :scope > .b)', ':has(.a, .b, :scope)'];
var rejected = true;
for (var i = 0; i < invalid.length; i++)
 if (document.querySelector(invalid[i]) !== null) rejected = false;
var ok = rejected && document.querySelector('#leaf:has(> :scope)') === null;
)JS", "nested functions, multiple pseudos, excess steps, and alternatives stay rejected");

    for (unsigned first = 0u; first < 4u; ++first) {
        for (unsigned second = 0u; second < 4u; ++second) {
            NavigatorScriptExecutionHarness pairHarness;
            error = RuntimeErrorCode::None;
            const std::string pairHtml = relationPairHtml(first, second);
            const bool loaded = pairHarness.loadHtml("file:///js67-pair.html",
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
        }
    }

    NavigatorScriptExecutionHarness eventHarness;
    error = RuntimeErrorCode::None;
    expect(eventHarness.loadHtml("file:///js67-events.html", kJs67Html, error) &&
        error == RuntimeErrorCode::None, "JS67 Event fixture loads");
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
 eventAResult = leaf.closest('.panel:has(> .row > :scope)') === panel;
 document.querySelector('#event-b').click();
 eventAResult = eventAResult &&
  leaf.closest('.panel:has(> .row > :scope)') === panel;
 eventMetadata = event.target === savedTarget &&
  event.currentTarget === savedCurrent && event.eventPhase === savedPhase &&
  event.defaultPrevented === savedDefault;
});
document.querySelector('#event-b').addEventListener('click', function(event) {
 eventBResult = leafB.closest('.panel:has(> .row > :scope)') === panelB &&
  event.target === document.querySelector('#event-b') &&
  event.currentTarget === document.querySelector('#event-b') &&
  event.eventPhase === 2 && event.defaultPrevented === false;
});
var ok = true;
)JS", "nested Event handlers install independently scoped selectors");
    const HostInstanceId eventASerial = serialById(eventHarness.document(), "event-a");
    expect(eventASerial != 0u && eventHarness.dispatchClick(eventASerial, error) &&
        error == RuntimeErrorCode::None,
        "nested JS67 Event dispatch completes");
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
            std::cerr << "JS67 " << name << "=";
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

    NavigatorScriptExecutionHarness replacementHarness;
    error = RuntimeErrorCode::None;
    expect(replacementHarness.loadHtml("file:///js67-old.html",
        "<html><body><div class='ancestor'><section id='panel' class='panel'>"
        "<div class='row'><span id='leaf'></span></div><i class='target'></i>"
        "</section></div></body></html>", error) &&
        error == RuntimeErrorCode::None, "old generation JS67 fixture loads");
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
    expect(replacementHarness.replaceHtml("file:///js67-new.html",
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
    const auto* newLeaf = newLeafSerial == 0u ? nullptr :
        &replacementHarness.document().structuralElements[
            static_cast<std::size_t>(newLeafSerial - 1u)];
    NavigatorScriptSimpleSelectorCoreDescriptor scopeCore;
    scopeCore.valid = true;
    scopeCore.statePseudo = NavigatorScriptStatePseudo::Scope;
    expect(oldGeneration != replacementHarness.hostAdapter().generation() &&
        oldLeafSerial == newLeafSerial && newLeaf != nullptr &&
        oldCollectionResult.code == HostResultCode::StaleObject &&
        NavigatorScriptRelativeSelectorTestAccess::coreMatch(
            replacementHarness.hostAdapter(), *newLeaf, scopeCore,
            NavigatorScriptSelectorDescriptor(), oldContext) ==
                NavigatorScriptSelectorMatchResult::Invalid,
        "same-serial replacement rejects the old generation match context");
    expectScript(replacementHarness,
        "var leaf = document.querySelector('#leaf');"
        "var panel = document.querySelector('#panel');"
        "var rows = panel.querySelectorAll("
        "'.ancestor:has(> :scope) .target');"
        "var ok = leaf.closest('.panel:has(> .row > :scope)') === panel && "
        "rows.length === 1;",
        "new generation establishes a fresh scope without stale leakage");

    NavigatorScriptExecutionHarness corruptHarness;
    error = RuntimeErrorCode::None;
    expect(corruptHarness.loadHtml("file:///js67-corrupt.html",
        "<html><body><section id='panel' class='panel'><div id='row' class='row'>"
        "<span id='leaf'></span></div></section></body></html>", error) &&
        error == RuntimeErrorCode::None, "structural corruption fixture loads");
    const HostInstanceId rowSerial = serialById(corruptHarness.document(), "row");
    if (rowSerial != 0u)
        corruptHarness.document().structuralElements[
            static_cast<std::size_t>(rowSerial - 1u)].parentSerial = 9999u;
    expectScript(corruptHarness,
        "var leaf = document.querySelector('#leaf');"
        "var ok = leaf.closest('.panel:has(> .row > :scope)') === null;",
        "invalid intermediate ancestry cannot be made valid by :scope");

    NavigatorScriptExecutionHarness capacityHarness;
    std::string capacityHtml = "<html><body><section id='panel' class='panel'>";
    for (std::size_t index = 0u; index < 495u; ++index)
        capacityHtml += "<div class='row'><i></i></div>";
    capacityHtml += "<div class='row'><div id='query-root'>";
    for (std::size_t index = 0u; index < 15u; ++index)
        capacityHtml += "<i class='target'></i>";
    capacityHtml += "</div>";
    for (std::size_t index = 0u; index < 14u; ++index)
        capacityHtml += "<i></i>";
    capacityHtml += "</section></body></html>";
    error = RuntimeErrorCode::None;
    expect(capacityHarness.loadHtml("file:///js67-capacity.html", capacityHtml,
        error) && error == RuntimeErrorCode::None &&
        capacityHarness.document().structuralElements.size() ==
            kNavigatorScriptMaxDocumentNodes,
        "near-capacity scope fixture reaches 1,024 Elements");
    const auto capacityStart = std::chrono::steady_clock::now();
    expectScript(capacityHarness, R"JS(
var root = document.querySelector('#query-root');
var all = root.querySelectorAll(
 '.panel:has(> .row > .missing, > .row > :scope) .target');
var first = root.querySelector(
 '.panel:has(> .row > .missing, > .row > :scope) .target');
var ok = all.length === 15 && first === all[0] &&
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
    capacityStorage.textLength = 4u;
    NavigatorScriptSimpleSelectorCoreDescriptor terminalScope;
    terminalScope.valid = true;
    terminalScope.statePseudo = NavigatorScriptStatePseudo::Scope;
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
            NavigatorScriptRelativeSelectorRelation::Child, terminalScope,
            capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , &counters
#endif
            );
    expect(capacityRelative == NavigatorScriptSelectorMatchResult::Match,
        "near-capacity second-step matcher finds the late API scope");
    std::cout << "JS67 near-capacity: elements=" <<
        capacityHarness.document().structuralElements.size() <<
        ", scope-serial=" << capacityScope << ", outer-candidates=15";
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    std::cout << ", relative-calls=" << counters.relativeEvaluationCalls
        << ", records=" << counters.structuralRecordsInspected
        << ", parent-hops=" << counters.parentHops
        << ", simple-tests=" << counters.simpleSelectorEvaluations;
#endif
    std::cout << ", query-workload=" << capacityElapsed << "us\n";

    NavigatorScriptExecutionHarness fixtureHarness;
    error = RuntimeErrorCode::None;
    std::ifstream fixtureFile("navigator-smoke/javascript-js67.html",
        std::ios::binary);
    const std::string fixtureHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = fixtureHtml.find("<script>");
    const std::size_t scriptClose = fixtureHtml.find("</script>", scriptOpen);
    const bool fixtureLoaded = !fixtureHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        fixtureHarness.loadHtml("file:///js67-hosted-fixture.html", fixtureHtml,
            error) && error == RuntimeErrorCode::None;
    expect(fixtureLoaded, "hosted JS67 fixture loads in native harness");
    if (fixtureLoaded) {
        const std::string program = fixtureHtml.substr(scriptOpen + 8u,
            scriptClose - scriptOpen - 8u);
        const auto& capturedScripts = fixtureHarness.document().scriptSources;
        const bool scriptCapturedIntact = capturedScripts.size() == 1u &&
            capturedScripts[0] == program;
        expect(scriptCapturedIntact,
            "hosted JS67 inline script is preserved by the HTML parser");
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
            std::cerr << "JS67 hosted fixture failed assertion indexes: "
                << fixtureHarness.runtime().stringValue(*failedIndexes) << '\n';
        }
        expect(result.succeeded() && assertions != nullptr &&
            assertions->isNumber() && assertions->numberValue() >= 20.0 &&
            fixtureFailures != nullptr && fixtureFailures->isNumber() &&
            fixtureFailures->numberValue() == 0.0,
            "hosted JS67 fixture initial assertions pass in native harness");
        if (assertions != nullptr && assertions->isNumber())
            std::cout << "JS67 hosted fixture native assertions=" <<
                assertions->numberValue() << "\n";
    }

    std::cout << "JS67 bounded structure bytes: match-context="
        << sizeof(NavigatorScriptSelectorMatchContext)
        << ", selector-descriptor=" << sizeof(NavigatorScriptSelectorDescriptor)
        << ", collection-record="
        << NavigatorScriptRelativeSelectorTestAccess::collectionRecordBytes()
        << ", registry=" << NavigatorScriptRelativeSelectorTestAccess::
            collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections
        << ", HtmlElementRef=" << sizeof(gxos::web::HtmlElementRef)
        << ", HtmlElementContentMetadata="
        << sizeof(gxos::web::HtmlElementContentMetadata) << "\n";
    std::cout << "Navigator JavaScript JS67 checks: " << checks - failures
        << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
