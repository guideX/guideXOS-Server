#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace gxos::javascript {
struct NavigatorScriptRelativeSelectorTestAccess {
    static constexpr std::size_t collectionRecordBytes()
    {
        return sizeof(NavigatorScriptHostAdapter::SelectorCollectionRecord);
    }
    static NavigatorScriptSelectorMatchResult evaluate(
        const NavigatorScriptHostAdapter& adapter, HostObjectReference anchor,
        NavigatorScriptRelativeSelectorRelation relation,
        const NavigatorScriptSimpleSelectorCoreDescriptor& selector,
        const NavigatorScriptSelectorDescriptor& storage,
        NavigatorScriptRelativeSelectorCounters* counters = nullptr)
    {
        return adapter.selectorRelativeElementMatchResult(anchor, relation,
            selector, 0, 0, storage, counters);
    }
    static bool setDisabledState(NavigatorScriptHostAdapter& adapter,
        HostInstanceId serial, bool disabled)
    {
        auto* state = adapter.formRuntimeState(serial);
        if (state == nullptr) return false;
        state->disabled = disabled;
        return true;
    }
};
} // namespace gxos::javascript

namespace {
using namespace gxos::javascript;
using Relation = NavigatorScriptRelativeSelectorRelation;
using Result = NavigatorScriptSelectorMatchResult;
int checks = 0;
int failures = 0;

void expect(bool value, const char* label)
{
    ++checks;
    if (value) return;
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
}

gxos::web::HtmlElementRef* find(gxos::web::WebDocument& document,
    const std::string& id)
{
    for (auto& element : document.structuralElements)
        if (element.id == id) return &element;
    return nullptr;
}

HostObjectReference handle(const NavigatorScriptHostAdapter& adapter,
    const gxos::web::HtmlElementRef* element)
{
    return element == nullptr ? HostObjectReference{} : HostObjectReference{
        element->serial, adapter.generation(), kNavigatorElementHostKind};
}

NavigatorScriptSimpleSelectorCoreDescriptor classSelector(
    std::uint8_t offset, std::uint8_t length)
{
    NavigatorScriptSimpleSelectorCoreDescriptor core;
    core.valid = true;
    core.classTokenCount = 1u;
    core.classTokens[0].offset = offset;
    core.classTokens[0].length = length;
    return core;
}

NavigatorScriptSelectorDescriptor textStorage(const std::string& text)
{
    NavigatorScriptSelectorDescriptor storage;
    storage.textLength = static_cast<std::uint16_t>(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) storage.text[i] = text[i];
    return storage;
}

NavigatorScriptRelativeSelectorCounters evaluate(
    NavigatorScriptExecutionHarness& harness,
    const gxos::web::HtmlElementRef* anchor, Relation relation,
    const NavigatorScriptSimpleSelectorCoreDescriptor& selector,
    const NavigatorScriptSelectorDescriptor& storage, Result& result)
{
    NavigatorScriptRelativeSelectorCounters counters;
    result = NavigatorScriptRelativeSelectorTestAccess::evaluate(
        harness.hostAdapter(), handle(harness.hostAdapter(), anchor), relation,
        selector, storage, &counters);
    return counters;
}

std::string flatDocument(std::size_t childCount, std::size_t targetIndex,
    const char* targetClass = "target")
{
    std::string html = "<html><body><div id='anchor'>";
    for (std::size_t i = 0; i < childCount; ++i) {
        if (i == targetIndex) html += "<i class='" + std::string(targetClass) + "'></i>";
        else html += "<i></i>";
    }
    html += "</div></body></html>";
    return html;
}

void report(const char* label, const NavigatorScriptRelativeSelectorCounters& c)
{
    std::cout << label << ": calls=" << c.relativeEvaluationCalls
        << " records=" << c.structuralRecordsInspected
        << " hops=" << c.parentHops << " resolutions=" << c.serialIndexResolutions
        << " selectors=" << c.simpleSelectorEvaluations
        << " childChecks=" << c.childRelationCandidateChecks
        << " adjacentChecks=" << c.adjacentSiblingChecks
        << " generalChecks=" << c.generalSiblingChecks << '\n';
}

void measurementCases()
{
    NavigatorScriptExecutionHarness h;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string html = flatDocument(1021u, 1020u);
    expect(h.loadHtml("file:///js62r2-flat.html", html, error), "1024 fixture loads");
    expect(h.document().structuralElements.size() == kNavigatorScriptMaxDocumentNodes,
        "fixture reaches 1024 structural Elements");
    auto* anchor = find(h.document(), "anchor");
    const auto target = classSelector(0u, 6u);
    const auto storage = textStorage("target");
    Result result = Result::Invalid;
    const auto late = evaluate(h, anchor, Relation::Descendant, target, storage, result);
    expect(result == Result::Match, "near-capacity last descendant matches");
    report("flat late true", late);
    const auto repeat = evaluate(h, anchor, Relation::Descendant, target, storage, result);
    expect(result == Result::Match && repeat.structuralRecordsInspected ==
        late.structuralRecordsInspected && repeat.parentHops == late.parentHops &&
        repeat.serialIndexResolutions == late.serialIndexResolutions &&
        repeat.simpleSelectorEvaluations == late.simpleSelectorEvaluations,
        "repeated operation counts are deterministic");

    const auto absent = classSelector(0u, 6u);
    const auto absentStorage = textStorage("absent");
    const auto falseCounts = evaluate(h, anchor, Relation::Descendant, absent,
        absentStorage, result);
    expect(result == Result::NoMatch, "near-capacity absent descendant is false");
    report("flat false", falseCounts);
    expect(late.simpleSelectorEvaluations == falseCounts.simpleSelectorEvaluations,
        "last-position true and absent selector both inspect the full descendant set");
    const auto uncounted = NavigatorScriptRelativeSelectorTestAccess::evaluate(
        h.hostAdapter(), handle(h.hostAdapter(), anchor), Relation::Descendant,
        absent, absentStorage);
    expect(uncounted == result,
        "diagnostic counters do not change evaluator result");

    expect(h.replaceHtml("file:///js62r2-capacity-reject.html",
        flatDocument(1022u, 1021u), error), "over-capacity child fixture loads within parser bounds");
    anchor = find(h.document(), "anchor");
    expect(h.document().structuralElements.size() == kNavigatorScriptMaxDocumentNodes,
        "structural registry remains at the hard capacity after rejected insertion");
    const auto rejectedTarget = evaluate(h, anchor, Relation::Descendant, target,
        storage, result);
    expect(result == Result::NoMatch && rejectedTarget.simpleSelectorEvaluations == 1021u,
        "target beyond represented capacity is absent from Descendant results");
    for (const auto relation : {Relation::Child, Relation::AdjacentSibling,
            Relation::GeneralSibling}) {
        evaluate(h, anchor, relation, target, storage, result);
        expect(result == Result::NoMatch,
            "target rejected at capacity is absent from other relative relations");
    }

    for (std::size_t targetAt : {0u, 510u, 1020u}) {
        expect(h.replaceHtml("file:///js62r2-wide.html",
            flatDocument(1021u, targetAt), error), "wide tree replacement loads");
        anchor = find(h.document(), "anchor");
        const auto child = evaluate(h, anchor, Relation::Child, target, storage, result);
        expect(result == Result::Match, "wide Child match found");
        report(targetAt == 0u ? "wide Child first" :
            (targetAt == 510u ? "wide Child middle" : "wide Child last"), child);
        const auto desc = evaluate(h, anchor, Relation::Descendant, target, storage, result);
        expect(result == Result::Match, "wide Descendant match found");
        report("wide Descendant", desc);
    }
    expect(h.replaceHtml("file:///js62r2-wide.html", flatDocument(1021u, 0u, "other"), error),
        "wide absent tree replacement loads");
    anchor = find(h.document(), "anchor");
    const auto childAbsent = evaluate(h, anchor, Relation::Child, target, storage, result);
    expect(result == Result::NoMatch, "wide Child absent result");
    report("wide Child absent", childAbsent);

    std::string chain = "<html><body>";
    constexpr std::size_t depth = 400u;
    for (std::size_t i = 0; i < depth; ++i) chain += "<div>";
    chain += "<i class='target'></i>";
    for (std::size_t i = 0; i < depth; ++i) chain += "</div>";
    chain += "</body></html>";
    expect(h.replaceHtml("file:///js62r2-chain.html", chain, error), "deep chain loads");
    auto* root = &h.document().structuralElements[2];
    const auto chainTrue = evaluate(h, root, Relation::Descendant, target, storage, result);
    expect(result == Result::Match, "deepest chain target matches");
    report("deep chain true", chainTrue);
    const auto chainFalse = evaluate(h, root, Relation::Descendant, absent, absentStorage, result);
    expect(result == Result::NoMatch, "deep chain absent target is false");
    report("deep chain false", chainFalse);
    std::cout << "deep chain depth=" << depth << " structural="
        << h.document().structuralElements.size() << '\n';
}

void siblingCases()
{
    NavigatorScriptExecutionHarness h;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    std::string html = "<html><body><div id='group'>";
    for (std::size_t i = 0; i < 1018u; ++i) {
        html += "<i" + std::string(i == 0u ? " id='first'" : "") +
            (i == 1017u ? " class='target'" : "") + "></i>";
    }
    html += "</div></body></html>";
    expect(h.loadHtml("file:///js62r2-siblings.html", html, error), "wide siblings load");
    const auto target = classSelector(0u, 6u);
    const auto storage = textStorage("target");
    Result result;
    auto* first = find(h.document(), "first");
    auto* middle = h.document().structuralElements.size() > 512u ?
        &h.document().structuralElements[512u] : nullptr;
    auto* last = &h.document().structuralElements.back();
    auto stats = evaluate(h, first, Relation::GeneralSibling, target, storage, result);
    expect(result == Result::Match, "general sibling late target matches");
    report("general sibling late", stats);
    stats = evaluate(h, first, Relation::GeneralSibling, classSelector(0u, 6u),
        textStorage("absent"), result);
    expect(result == Result::NoMatch, "general sibling absent result");
    report("general sibling false", stats);
    stats = evaluate(h, first, Relation::AdjacentSibling, target, storage, result);
    expect(result == Result::NoMatch, "adjacent ignores late sibling");
    report("adjacent first", stats);
    stats = evaluate(h, middle, Relation::AdjacentSibling, target, storage, result);
    expect(result == Result::NoMatch, "middle adjacent does not skip sibling");
    report("adjacent middle", stats);
    stats = evaluate(h, last, Relation::GeneralSibling, target, storage, result);
    expect(result == Result::NoMatch, "last anchor has no following sibling");
    report("general sibling last", stats);
    stats = evaluate(h, last, Relation::AdjacentSibling, target, storage, result);
    expect(result == Result::NoMatch, "last anchor has no adjacent following sibling");
    report("adjacent last", stats);
    stats = evaluate(h, middle, Relation::GeneralSibling, target, storage, result);
    expect(result == Result::Match, "middle anchor finds late general sibling");
    report("general sibling middle late", stats);

    auto* group = find(h.document(), "group");
    const std::uint64_t anchorParent = first->parentSerial;
    first->parentSerial = 0u;
    stats = evaluate(h, first, Relation::GeneralSibling, target, storage, result);
    expect(result == Result::NoMatch, "parentless anchor cannot claim following siblings");
    report("parentless sibling", stats);
    first->parentSerial = kNavigatorScriptMaxDocumentNodes + 1u;
    stats = evaluate(h, first, Relation::AdjacentSibling, target, storage, result);
    expect(result == Result::Invalid, "invalid sibling common parent fails closed");
    report("invalid sibling parent", stats);
    first->parentSerial = anchorParent;
    (void)group;

    expect(h.replaceHtml("file:///js62r2-sibling-immediate.html",
        "<html><body><div><i id='first'></i><b class='target'></b><i></i></div></body></html>", error),
        "immediate sibling fixture loads");
    first = find(h.document(), "first");
    stats = evaluate(h, first, Relation::AdjacentSibling, target, storage, result);
    expect(result == Result::Match, "immediate adjacent sibling matches");
    report("adjacent immediate", stats);
    stats = evaluate(h, first, Relation::GeneralSibling, target, storage, result);
    expect(result == Result::Match, "general sibling includes immediate sibling");
    report("general sibling immediate", stats);

    expect(h.replaceHtml("file:///js62r2-rich.html",
        "<html><body><div id='group'><button id='start'></button><button class='primary' data-state='ready'></button><section><button class='primary' data-state='ready'></button></section></div></body></html>", error),
        "rich compound fixture loads");
    auto rich = NavigatorScriptSimpleSelectorCoreDescriptor{};
    rich.valid = true;
    rich.tagOffset = 0u; rich.tagLength = 6u;
    rich.classTokenCount = 1u;
    rich.classTokens[0].offset = 6u; rich.classTokens[0].length = 7u;
    rich.hasAttributePredicate = true;
    rich.attributeNameOffset = 13u; rich.attributeNameLength = 10u;
    rich.attributeValuePresent = true;
    rich.attributeValueOffset = 23u; rich.attributeValueLength = 5u;
    const auto richStorage = textStorage("buttonprimarydata-stateready");
    auto* richGroup = find(h.document(), "group");
    auto* start = find(h.document(), "start");
    for (const auto relation : {Relation::Descendant, Relation::Child}) {
        stats = evaluate(h, richGroup, relation, rich, richStorage, result);
        expect(result == Result::Match, "rich compound matches Descendant and Child");
    }
    for (const auto relation : {Relation::AdjacentSibling, Relation::GeneralSibling}) {
        stats = evaluate(h, start, relation, rich, richStorage, result);
        expect(result == Result::Match, "rich compound matches sibling relations");
    }
    NavigatorScriptSimpleSelectorCoreDescriptor root;
    root.valid = true;
    root.statePseudo = NavigatorScriptStatePseudo::Root;
    for (const auto relation : {Relation::Descendant, Relation::Child})
        expect(evaluate(h, richGroup, relation, root, textStorage(""), result).relativeEvaluationCalls == 1u &&
            result == Result::NoMatch, "root target cannot match descendant or child");
    for (const auto relation : {Relation::AdjacentSibling, Relation::GeneralSibling})
        expect(evaluate(h, start, relation, root, textStorage(""), result).relativeEvaluationCalls == 1u &&
            result == Result::NoMatch, "root target cannot match sibling relation");
}

void outerCandidateSimulations()
{
    const std::string id = "file:///js62r2-simulation.html";
    for (const char* scenario : {"early", "late", "false"}) {
        NavigatorScriptExecutionHarness h;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        std::string html = "<html><body>";
        const bool matching = std::string(scenario) != "false";
        const bool late = std::string(scenario) == "late";
        constexpr std::size_t groups = 340u;
        for (std::size_t i = 0; i < groups; ++i) {
            html += "<section id='a" + std::to_string(i) + "'>";
            if (!late) html += matching ? "<b class='target'></b>" : "<b></b>";
            html += "<i></i>";
            if (late) html += matching ? "<b class='target'></b>" : "<b></b>";
            html += "</section>";
        }
        html += "</body></html>";
        expect(h.loadHtml(id, html, error), "multi-anchor simulation loads");
        const std::size_t count = h.document().structuralElements.size();
        std::uint64_t calls = 0, records = 0, hops = 0, resolutions = 0, selectors = 0;
        std::uint64_t maxRecords = 0, maxHops = 0;
        std::size_t anchors = 0;
        const auto target = classSelector(0u, 6u);
        const auto storage = textStorage("target");
        const auto start = std::chrono::steady_clock::now();
        for (auto& element : h.document().structuralElements) {
            ++anchors;
            Result result;
            const auto one = evaluate(h, &element, Relation::Descendant, target,
                storage, result);
            calls += one.relativeEvaluationCalls;
            records += one.structuralRecordsInspected;
            hops += one.parentHops;
            resolutions += one.serialIndexResolutions;
            selectors += one.simpleSelectorEvaluations;
            if (one.structuralRecordsInspected > maxRecords)
                maxRecords = one.structuralRecordsInspected;
            if (one.parentHops > maxHops) maxHops = one.parentHops;
            bool expected = false;
            for (const auto& candidate : h.document().structuralElements) {
                if (candidate.className != "target") continue;
                std::uint64_t parent = candidate.parentSerial;
                for (std::size_t hop = 0u; hop < count && parent != 0u; ++hop) {
                    if (parent == element.serial) { expected = true; break; }
                    if (parent > count) break;
                    parent = h.document().structuralElements[
                        static_cast<std::size_t>(parent - 1u)].parentSerial;
                }
                if (expected) break;
            }
            expect((result == Result::Match) == (matching && expected),
                "every represented outer candidate agrees with independent ancestry oracle");
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "simulation " << scenario << ": elements=" << count
            << " anchors=" << anchors << " calls=" << calls << " records=" << records
            << " hops=" << hops << " resolutions=" << resolutions
            << " selectors=" << selectors << " avgRecords="
            << (anchors ? static_cast<double>(records) / anchors : 0.0)
            << " maxRecords=" << maxRecords << " avgHops="
            << (anchors ? static_cast<double>(hops) / anchors : 0.0)
            << " maxHops=" << maxHops << " elapsedMs=" << elapsed << '\n';
    }
}

void corruptionAndLiveState()
{
    NavigatorScriptExecutionHarness h;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(h.loadHtml("file:///js62r2-qual.html",
        "<html><body><input id='outside' type='text'><div id='a'><input id='b' type='text' class='target' data-state='pending'><input id='c' type='text'></div></body></html>", error),
        "qualification fixture loads");
    auto* a = find(h.document(), "a");
    auto* b = find(h.document(), "b");
    auto* c = find(h.document(), "c");
    const auto target = classSelector(0u, 6u);
    const auto storage = textStorage("target");
    Result result;
    auto stats = evaluate(h, a, Relation::Descendant, target, storage, result);
    expect(result == Result::Match, "descendant target initial match");
    const std::uint64_t savedA = a->parentSerial;
    a->parentSerial = a->serial;
    stats = evaluate(h, a, Relation::Descendant, target, storage, result);
    expect(result == Result::Invalid && stats.parentHops <= h.document().structuralElements.size(),
        "self-parent corruption fails closed within capacity");
    report("self-parent corruption", stats);
    a->parentSerial = savedA;
    const std::uint64_t savedB = b->parentSerial;
    a->parentSerial = b->serial; b->parentSerial = a->serial;
    stats = evaluate(h, a, Relation::Descendant, target, storage, result);
    expect(result == Result::Invalid, "two-node cycle fails closed");
    report("two-node cycle", stats);
    a->parentSerial = savedA; b->parentSerial = savedB;

    const std::uint64_t savedC = c->parentSerial;
    a->parentSerial = b->serial; b->parentSerial = c->serial;
    c->parentSerial = a->serial;
    stats = evaluate(h, a, Relation::Descendant, target, storage, result);
    expect(result == Result::Invalid && stats.parentHops <=
        2u * h.document().structuralElements.size(),
        "three-node cycle fails closed within bounded walks");
    report("three-node cycle", stats);
    a->parentSerial = savedA; b->parentSerial = savedB; c->parentSerial = savedC;

    const std::uint64_t savedIndexSerial = h.document().structuralElements[0].serial;
    h.document().structuralElements[0].serial = 99u;
    stats = evaluate(h, a, Relation::Descendant, target, storage, result);
    expect(result == Result::Invalid, "broken sequential serial mapping fails closed");
    report("sequential mapping corruption", stats);
    h.document().structuralElements[0].serial = savedIndexSerial;
    const std::uint64_t savedDuplicateSerial = h.document().structuralElements[1].serial;
    h.document().structuralElements[1].serial = savedIndexSerial;
    stats = evaluate(h, a, Relation::Descendant, target, storage, result);
    expect(result == Result::Invalid, "duplicate serial invariant fails closed");
    h.document().structuralElements[1].serial = savedDuplicateSerial;
    const std::uint64_t savedParent = a->parentSerial;
    a->parentSerial = kNavigatorScriptMaxDocumentNodes + 9u;
    stats = evaluate(h, a, Relation::Descendant, target, storage, result);
    expect(result == Result::Invalid, "out-of-range parent serial fails closed");
    a->parentSerial = savedParent;

    const auto generation = h.hostAdapter().generation();
    const auto stale = handle(h.hostAdapter(), a);
    const auto staleTarget = handle(h.hostAdapter(), b);
    const auto serialA = a->serial;
    expect(h.replaceHtml("file:///js62r2-replaced.html",
        "<html><body><input id='outside' type='text'><div id='a'></div></body></html>", error), "replacement document loads");
    expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(), stale,
        Relation::Descendant, target, storage) == Result::Invalid,
        "old-generation anchor is invalid");
    expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(), staleTarget,
        Relation::Descendant, target, storage) == Result::Invalid,
        "old-generation target handle is invalid");
    expect(h.hostAdapter().generation() != generation && find(h.document(), "a")->serial == serialA,
        "replacement reuses serial only in a new generation");

    auto previousGenerationAnchor = handle(h.hostAdapter(), find(h.document(), "a"));
    for (std::size_t index = 0u; index < 300u; ++index) {
        const bool matches = (index % 2u) == 0u;
        const std::string child = matches ? "<i class='target'></i>" : "<i></i>";
        expect(h.replaceHtml("file:///js62r2-generation.html",
            "<html><body><input id='outside' type='text'><div id='a'>" + child +
                "</div></body></html>", error),
            "generation replacement succeeds");
        auto* currentAnchor = find(h.document(), "a");
        evaluate(h, currentAnchor, Relation::Descendant, target, storage, result);
        expect((result == Result::Match) == matches,
            "reused serial observes only current document target state");
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(),
            previousGenerationAnchor, Relation::Descendant, target, storage) == Result::Invalid,
            "old generation anchor remains stale during replacement stress");
        previousGenerationAnchor = handle(h.hostAdapter(), currentAnchor);
    }
    std::cout << "generation replacements=300 finalGeneration="
        << h.hostAdapter().generation() << '\n';

    expect(h.replaceHtml("file:///js62r2-live.html",
        "<html><body><input id='outside' type='text'><div id='a'><input id='b' type='checkbox' class='target'><input id='c' type='text'><i id='empty'></i><section id='textParent'><i id='text'>x</i></section><section id='childParent'><i id='child'><span id='only'></span></i></section></div></body></html>", error),
        "live state document loads");
    a = find(h.document(), "a"); b = find(h.document(), "b"); c = find(h.document(), "c");
    auto attrPresent = classSelector(0u, 6u);
    attrPresent.classTokenCount = 0u;
    attrPresent.hasAttributePredicate = true;
    attrPresent.attributeNameOffset = 0u;
    attrPresent.attributeNameLength = 10u;
    const auto attrStorage = textStorage("data-state");
    stats = evaluate(h, a, Relation::Descendant, attrPresent, attrStorage, result);
    expect(result == Result::NoMatch, "relative attribute initially absent");
    expect(h.execute("document.getElementById('b').setAttribute('data-state','ready');").succeeded(),
        "attribute add executes");
    stats = evaluate(h, a, Relation::Descendant, attrPresent, attrStorage, result);
    expect(result == Result::Match, "relative attribute observes addition live");
    expect(h.execute("document.getElementById('b').removeAttribute('data-state');").succeeded(),
        "attribute remove executes");
    stats = evaluate(h, a, Relation::Descendant, attrPresent, attrStorage, result);
    expect(result == Result::NoMatch, "relative attribute observes removal live");

    auto attrEqual = attrPresent;
    attrEqual.attributeValuePresent = true;
    attrEqual.attributeValueOffset = 10u;
    attrEqual.attributeValueLength = 5u;
    const auto equalityStorage = textStorage("data-stateready");
    expect(h.execute("document.getElementById('b').setAttribute('data-state','pending');").succeeded(),
        "attribute equality pending setup executes");
    evaluate(h, a, Relation::Descendant, attrEqual, equalityStorage, result);
    expect(result == Result::NoMatch, "relative attribute equality pending is false");
    expect(h.execute("document.getElementById('b').setAttribute('data-state','ready');").succeeded(),
        "attribute equality ready mutation executes");
    evaluate(h, a, Relation::Descendant, attrEqual, equalityStorage, result);
    expect(result == Result::Match, "relative attribute equality observes ready live");
    expect(h.execute("document.getElementById('b').setAttribute('data-state','other');").succeeded(),
        "attribute equality changed mutation executes");
    evaluate(h, a, Relation::Descendant, attrEqual, equalityStorage, result);
    expect(result == Result::NoMatch, "relative attribute equality observes changed value live");

    NavigatorScriptSimpleSelectorCoreDescriptor stateCore;
    stateCore.valid = true;
    auto checked = stateCore;
    checked.statePseudo = NavigatorScriptStatePseudo::Checked;
    evaluate(h, a, Relation::Descendant, checked, textStorage(""), result);
    expect(result == Result::NoMatch, "relative checked state initially false");
    bool checkedChanged = false;
    expect(h.hostAdapter().setFormControlFromUser(b->serial, checkedChanged) && checkedChanged,
        "checked state user transition succeeds");
    evaluate(h, a, Relation::Descendant, checked, textStorage(""), result);
    expect(result == Result::Match, "relative checked state observes live check");

    auto disabled = stateCore;
    disabled.statePseudo = NavigatorScriptStatePseudo::Disabled;
    expect(NavigatorScriptRelativeSelectorTestAccess::setDisabledState(
        h.hostAdapter(), b->serial, true), "disabled runtime state mutation succeeds");
    evaluate(h, a, Relation::Descendant, disabled, textStorage(""), result);
    expect(result == Result::Match, "relative disabled state observes live disable");
    expect(NavigatorScriptRelativeSelectorTestAccess::setDisabledState(
        h.hostAdapter(), b->serial, false), "enabled runtime state mutation succeeds");
    evaluate(h, a, Relation::Descendant, disabled, textStorage(""), result);
    expect(result == Result::NoMatch, "relative disabled state observes live enable");

    NavigatorScriptSimpleSelectorCoreDescriptor focus;
    focus.valid = true;
    focus.statePseudo = NavigatorScriptStatePseudo::Focus;
    auto* outside = find(h.document(), "outside");
    if (outside == nullptr) outside = &h.document().structuralElements[2];
    expect(h.focusElement(outside->serial, error), "focus outside transition succeeds");
    expect(evaluate(h, a, Relation::Descendant, focus, storage, result).relativeEvaluationCalls == 1u && result == Result::NoMatch,
        "relative focus false when focus outside");
    expect(h.focusElement(b->serial, error), "focus descendant transition succeeds");
    evaluate(h, a, Relation::Descendant, focus, storage, result);
    expect(result == Result::Match, "relative focus follows descendant");
    expect(h.focusElement(c->serial, error), "focus moves to another descendant");
    evaluate(h, a, Relation::Descendant, focus, storage, result);
    expect(result == Result::Match, "relative focus follows the second descendant");
    expect(h.focusElement(outside->serial, error), "focus returns outside the subtree");
    evaluate(h, a, Relation::Descendant, focus, storage, result);
    expect(result == Result::NoMatch, "relative focus becomes false when focus returns outside");

    NavigatorScriptSimpleSelectorCoreDescriptor empty;
    empty.valid = true;
    empty.tagOffset = 0u;
    empty.tagLength = 1u;
    empty.statePseudo = NavigatorScriptStatePseudo::Empty;
    evaluate(h, a, Relation::Descendant, empty, textStorage("i"), result);
    expect(result == Result::Match, "relative empty target matches empty Element");
    auto* textAnchor = find(h.document(), "textParent");
    const auto textEmpty = evaluate(h, textAnchor, Relation::Descendant, empty,
        textStorage("i"), result);
    expect(result == Result::NoMatch && textEmpty.relativeEvaluationCalls == 1u,
        "text-only subtree has no empty descendant match");
    auto* childAnchor = find(h.document(), "childParent");
    evaluate(h, childAnchor, Relation::Descendant, empty, textStorage("i"), result);
    expect(result == Result::NoMatch, "Element-child subtree is not empty");
    auto first = target; first.statePseudo = NavigatorScriptStatePseudo::FirstChild;
    evaluate(h, a, Relation::Child, first, storage, result);
    expect(result == Result::Match, "Child relative structural pseudo matches first child");
    auto* onlyChildAnchor = find(h.document(), "child");
    for (const auto pseudo : {NavigatorScriptStatePseudo::FirstChild,
            NavigatorScriptStatePseudo::LastChild,
            NavigatorScriptStatePseudo::OnlyChild,
            NavigatorScriptStatePseudo::FirstOfType,
            NavigatorScriptStatePseudo::LastOfType,
            NavigatorScriptStatePseudo::OnlyOfType}) {
        auto structural = stateCore;
        structural.statePseudo = pseudo;
        stats = evaluate(h, onlyChildAnchor, Relation::Child, structural, textStorage(""), result);
        if (result != Result::Match) std::cout << "structural pseudo="
            << static_cast<unsigned>(pseudo) << " result=" << static_cast<unsigned>(result)
            << " calls=" << stats.relativeEvaluationCalls << " selectors="
            << stats.simpleSelectorEvaluations << " anchor=" << onlyChildAnchor->serial
            << " target=" << h.document().structuralElements.back().serial << '\n';
        expect(result == Result::Match, "relative Child honors structural pseudo state");
    }
    auto root = focus; root.statePseudo = NavigatorScriptStatePseudo::Root;
    evaluate(h, a, Relation::Descendant, root, textStorage(""), result);
    expect(result == Result::NoMatch, "relative root cannot match descendant");
    (void)c;
}

void publicBoundary()
{
    NavigatorScriptExecutionHarness h;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(h.loadHtml("file:///js62r2-public.html", "<html><body><div></div></body></html>", error),
        "public-boundary fixture loads");
    for (const char* suffix : {".matches(':has(.x)')", ".matches(':has(> .x)')",
            ".matches(':has(+ .x)')", ".matches(':has(~ .x)')"})
        expect(!h.execute(std::string("document.body") + suffix + ";").succeeded(),
            "public :has form remains unsupported");
    std::size_t leadingIndex = 0u;
    for (const char* selector : {"> .x", "+ .x", "~ .x"}) {
        const std::string name = "leadingRejected" + std::to_string(leadingIndex++);
        const std::string source = "var " + name + " = document.querySelector(\"" +
            selector + "\") === null;";
        const ScriptResult evaluated = h.execute(source);
        const Value* value = h.runtime().lookup(SourceView(name.data(), name.size()));
        expect(evaluated.succeeded() && value != nullptr && value->isBoolean() &&
            value->booleanValue(),
            "standalone leading combinator is rejected by selector matching");
    }

    expect(h.replaceHtml("file:///js62r2-semantics.html",
        "<html><body><div id='self' class='target'></div><form id='f'></form><div id='group'><i id='before' class='target'></i><i id='anchor'></i><section><b class='target'></b></section></div><div id='form-anchor'></div><input form='f' class='target'><div id='hidden' hidden><i class='target'></i></div></body></html>", error),
        "relative semantic boundary fixture loads");
    const auto target = classSelector(0u, 6u);
    const auto targetText = textStorage("target");
    auto* self = find(h.document(), "self");
    expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(),
        handle(h.hostAdapter(), self), Relation::Descendant, target, targetText) == Result::NoMatch &&
        NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(),
            handle(h.hostAdapter(), self), Relation::Child, target, targetText) == Result::NoMatch,
        "Descendant and Child exclude a matching anchor itself");
    auto* siblingAnchor = find(h.document(), "anchor");
    expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(),
        handle(h.hostAdapter(), siblingAnchor), Relation::AdjacentSibling, target, targetText) == Result::NoMatch &&
        NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(),
            handle(h.hostAdapter(), siblingAnchor), Relation::GeneralSibling, target, targetText) == Result::NoMatch,
        "previous sibling and descendant of a following sibling do not match sibling relations");
    auto* hidden = find(h.document(), "hidden");
    expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(),
        handle(h.hostAdapter(), hidden), Relation::Descendant, target, targetText) == Result::Match,
        "hidden Elements remain in structural relative matching");
    auto* formIndependent = find(h.document(), "form-anchor");
    // The input associated with #f is outside this subtree; form ownership
    // metadata cannot create a structural descendant relationship.
    expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(h.hostAdapter(),
        handle(h.hostAdapter(), formIndependent), Relation::Descendant, target, targetText) == Result::NoMatch,
        "form ownership does not create structural descendants");
}
} // namespace

int main()
{
    static_assert(sizeof(NavigatorScriptSimpleSelectorCoreDescriptor) == 32u,
        "JS62R2 does not grow the shared selector core");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor) == 68u,
        "JS62R2 does not grow the simple selector descriptor");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 812u,
        "JS62R2 does not grow the four-member selector descriptor");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::collectionRecordBytes() == 832u,
        "JS62R2 does not grow selector collection records");
    static_assert(sizeof(gxos::web::HtmlElementRef) == 440u,
        "JS62R2 does not grow structural Elements");
    static_assert(sizeof(gxos::web::HtmlElementContentMetadata) == 24u,
        "JS62R2 does not grow content records");
    static_assert(sizeof(NavigatorScriptRelativeSelectorCounters) == 64u,
        "diagnostic counters remain test-local and compact");
    std::cout << "memory core=32 simple=68 complete=812 collection=832 registry="
        << (128u * NavigatorScriptRelativeSelectorTestAccess::collectionRecordBytes())
        << " htmlElement=440 content=24"
        << " diagnosticCounters=64 productionCounterStorage=0\n";
    measurementCases();
    siblingCases();
    outerCandidateSimulations();
    corruptionAndLiveState();
    publicBoundary();
    if (failures != 0) return 1;
    std::cout << "Navigator JavaScript JS62R2 checks: " << checks << "/"
        << checks << " passed\n";
    return 0;
}
