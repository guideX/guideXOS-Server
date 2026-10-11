#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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
        const NavigatorScriptSelectorDescriptor& storage)
    {
        return adapter.selectorSimpleElementMatchResult(element, selector,
            storage);
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
        , const NavigatorScriptSimpleSelectorCoreDescriptor*
            firstLogicalArgument = nullptr
        , const NavigatorScriptSimpleSelectorCoreDescriptor*
            secondLogicalArgument = nullptr
        )
    {
        return adapter.selectorRelativeChainMatchResult(anchor, firstRelation,
            firstSelector, secondRelation, secondSelector, storage
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , counters
#endif
            , context, firstLogicalArgument, secondLogicalArgument);
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
        )
    {
        return adapter.selectorRelativeElementMatchResult(anchor, relation,
            selector, 0, 0, storage
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
            , counters
#endif
            , nullptr, NavigatorScriptRelativeSelectorRelation::Descendant,
            context, &logicalArgument, nullptr);
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
            "sequence", "malformedRejected", "ok"}) {
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
        maximumAttributeValue + "\"]))";
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
        maximumHarness->loadHtml("file:///js68-maximum.html", maximumHtml,
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

const char* const kJs68Html =
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

} // namespace

int main()
{
    static_assert(sizeof(NavigatorScriptSimpleSelectorCoreDescriptor) == 32u,
        "JS68 does not grow the shared simple-selector core");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor) == 50u,
        "JS68 does not grow the outer simple-selector descriptor");
    static_assert(sizeof(NavigatorScriptCompactRelativeSelectorCoreDescriptor) ==
        16u, "JS68 preserves compact relative cores");
    static_assert(sizeof(NavigatorScriptHasExpressionDescriptor) == 108u,
        "JS69 keeps two logical arguments fixed per relative core");
    static_assert(sizeof(NavigatorScriptSelectorMatchContext) == 16u,
        "JS68 reuses the call-local JS66 match context");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1918u,
        "JS69 adds compact second logical arguments per :has() core");
    static_assert(sizeof(gxos::web::HtmlElementRef) == 440u,
        "JS68 does not grow document structural records");
    static_assert(sizeof(gxos::web::HtmlElementContentMetadata) == 24u,
        "JS68 does not grow document content metadata");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() == 1936u,
        "JS69 collection records include the bounded logical slots");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
        collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections ==
            247808u,
        "JS69 fixed logical slots keep the 128-entry registry bounded");

    const auto harnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& harness = *harnessStorage;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js68.html", kJs68Html, error) &&
        error == RuntimeErrorCode::None, "JS68 structural fixture loads");

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
            const bool loaded = pairHarness.loadHtml("file:///js68-pair.html",
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
    expect(eventHarness.loadHtml("file:///js68-events.html", kJs68Html, error) &&
        error == RuntimeErrorCode::None, "JS68 Event fixture loads");
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
        "nested JS68 Event dispatch completes");
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
            std::cerr << "JS68 " << name << "=";
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
    expect(replacementHarness.loadHtml("file:///js68-old.html",
        "<html><body><div class='ancestor'><section id='panel' class='panel'>"
        "<div class='row'><span id='leaf'></span></div><i class='target'></i>"
        "</section></div></body></html>", error) &&
        error == RuntimeErrorCode::None, "old generation JS68 fixture loads");
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
    expect(replacementHarness.replaceHtml("file:///js68-new.html",
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
    expect(corruptHarness.loadHtml("file:///js68-corrupt.html",
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
    expect(capacityHarness.loadHtml("file:///js68-capacity.html", capacityHtml,
        error) && error == RuntimeErrorCode::None &&
        capacityHarness.document().structuralElements.size() ==
            kNavigatorScriptMaxDocumentNodes,
        "near-capacity scope fixture reaches 1,024 Elements");
    const auto capacityStart = std::chrono::steady_clock::now();
    expectScript(capacityHarness, R"JS(
var root = document.querySelector('#query-root');
var all = root.querySelectorAll(
 '.panel:has(> .row > :is(.missing), > .row > :where(:scope)) .target');
var first = root.querySelector(
 '.panel:has(> .row > :is(.missing), > .row > :where(:scope)) .target');
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
    const char missingText[] = "missing";
    for (std::size_t index = 0u; index < sizeof(missingText); ++index)
        capacityStorage.text[4u + index] = missingText[index];
    capacityStorage.textLength = 4u + sizeof(missingText);
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
            , &rowArgument, &terminalScope
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
        );
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
        NavigatorScriptRelativeSelectorRelation::Descendant, capacityNotCore,
        rowArgument, capacityStorage, capacityContext
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
        , &earlyNotCounters
#endif
        );
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
        lateWhere == NavigatorScriptSelectorMatchResult::Match &&
        earlyNot == NavigatorScriptSelectorMatchResult::Match &&
        fullFalseNot == NavigatorScriptSelectorMatchResult::NoMatch &&
        noTarget == NavigatorScriptSelectorMatchResult::NoMatch,
        "near-capacity early/late is/where, existential not, full-false, and zero-target workloads stay bounded");
    std::cout << "JS68 near-capacity: elements=" <<
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
    printWorkloadCounters("where-late", lateWhereCounters);
    printWorkloadCounters("not-early", earlyNotCounters);
    printWorkloadCounters("not-full-false", fullFalseNotCounters);
    printWorkloadCounters("not-zero-target", noTargetCounters);
#endif
    std::cout << ", query-workload=" << capacityElapsed << "us\n";

    const auto fixtureHarnessStorage = std::make_unique<NavigatorScriptExecutionHarness>();
    auto& fixtureHarness = *fixtureHarnessStorage;
    error = RuntimeErrorCode::None;
    std::ifstream fixtureFile("navigator-smoke/javascript-js68.html",
        std::ios::binary);
    const std::string fixtureHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = fixtureHtml.find("<script>");
    const std::size_t scriptClose = fixtureHtml.find("</script>", scriptOpen);
    const bool fixtureLoaded = !fixtureHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        fixtureHarness.loadHtml("file:///js68-hosted-fixture.html", fixtureHtml,
            error) && error == RuntimeErrorCode::None;
    expect(fixtureLoaded, "hosted JS68 fixture loads in native harness");
    if (fixtureLoaded) {
        const std::string program = fixtureHtml.substr(scriptOpen + 8u,
            scriptClose - scriptOpen - 8u);
        const auto& capturedScripts = fixtureHarness.document().scriptSources;
        const bool scriptCapturedIntact = capturedScripts.size() == 1u &&
            capturedScripts[0] == program;
        expect(scriptCapturedIntact,
            "hosted JS68 inline script is preserved by the HTML parser");
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
            std::cerr << "JS68 hosted fixture failed assertion indexes: "
                << fixtureHarness.runtime().stringValue(*failedIndexes) << '\n';
        }
        expect(result.succeeded() && assertions != nullptr &&
            assertions->isNumber() && assertions->numberValue() >= 20.0 &&
            fixtureFailures != nullptr && fixtureFailures->isNumber() &&
            fixtureFailures->numberValue() == 0.0,
            "hosted JS68 fixture initial assertions pass in native harness");
        if (assertions != nullptr && assertions->isNumber())
            std::cout << "JS68 hosted fixture native assertions=" <<
                assertions->numberValue() << "\n";
    }

    std::cout << "JS68 bounded structure bytes: match-context="
        << sizeof(NavigatorScriptSelectorMatchContext)
        << ", selector-descriptor=" << sizeof(NavigatorScriptSelectorDescriptor)
        << ", collection-record="
        << NavigatorScriptRelativeSelectorTestAccess::collectionRecordBytes()
        << ", registry=" << NavigatorScriptRelativeSelectorTestAccess::
            collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections
        << ", HtmlElementRef=" << sizeof(gxos::web::HtmlElementRef)
        << ", HtmlElementContentMetadata="
        << sizeof(gxos::web::HtmlElementContentMetadata) << "\n";
    std::cout << "Navigator JavaScript JS68 checks: " << checks - failures
        << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
