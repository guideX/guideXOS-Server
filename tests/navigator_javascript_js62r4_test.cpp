#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace gxos::javascript {
struct NavigatorScriptRelativeSelectorTestAccess {
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
};
} // namespace gxos::javascript

namespace {
using namespace gxos::javascript;
using Relation = NavigatorScriptRelativeSelectorRelation;
using Match = NavigatorScriptSelectorMatchResult;

struct ProbeState {
    NavigatorScriptExecutionHarness* harness = nullptr;
    HostInstanceId outer = 0;
    HostInstanceId inner = 0;
    HostInstanceId leaf = 0;
    NavigatorScriptSimpleSelectorCoreDescriptor outerDescendant;
    NavigatorScriptSimpleSelectorCoreDescriptor outerChild;
    NavigatorScriptSimpleSelectorCoreDescriptor innerAdjacent;
    NavigatorScriptSimpleSelectorCoreDescriptor innerGeneral;
    NavigatorScriptSimpleSelectorCoreDescriptor innerId;
    NavigatorScriptSimpleSelectorCoreDescriptor emptyPseudo;
    NavigatorScriptSelectorDescriptor outerDescendantStorage;
    NavigatorScriptSelectorDescriptor outerChildStorage;
    NavigatorScriptSelectorDescriptor innerAdjacentStorage;
    NavigatorScriptSelectorDescriptor innerGeneralStorage;
    NavigatorScriptSelectorDescriptor innerIdStorage;
    NavigatorScriptSelectorDescriptor emptyStorage;
    NavigatorScriptRelativeSelectorCounters outerCounters;
    NavigatorScriptRelativeSelectorCounters innerCounters;
    NavigatorScriptRelativeSelectorCounters leafCounters;
    NavigatorScriptRelativeSelectorCounters otherCounters;
    std::size_t outerCallbacks = 0;
    std::size_t innerCallbacks = 0;
    std::size_t leafCallbacks = 0;
    std::size_t otherCallbacks = 0;
    std::size_t outerExpectedCalls = 0;
    std::size_t innerExpectedCalls = 0;
    std::size_t leafExpectedCalls = 0;
    std::size_t otherExpectedCalls = 0;
    std::size_t phaseCapture = 0;
    std::size_t phaseTarget = 0;
    std::size_t phaseBubble = 0;
    std::size_t metadataChecks = 0;
    std::size_t metadataFailures = 0;
    std::size_t relationChecks = 0;
    std::size_t relationFailures = 0;
    std::size_t innerCounterResets = 0;
    std::size_t resetIsolationFailures = 0;
};

NavigatorScriptSimpleSelectorCoreDescriptor classCore(std::uint8_t length)
{
    NavigatorScriptSimpleSelectorCoreDescriptor core;
    core.valid = true;
    core.classTokenCount = 1;
    core.classTokens[0] = {0, length};
    return core;
}

NavigatorScriptSelectorDescriptor storage(const char* text)
{
    NavigatorScriptSelectorDescriptor result;
    const std::size_t length = std::strlen(text);
    result.textLength = static_cast<std::uint16_t>(length);
    for (std::size_t i = 0; i < length; ++i) result.text[i] = text[i];
    return result;
}

bool same(const Value& left, const Value& right)
{
    if (left.type() != right.type()) return false;
    switch (left.type()) {
    case ValueType::Undefined:
    case ValueType::Null: return true;
    case ValueType::Boolean: return left.booleanValue() == right.booleanValue();
    case ValueType::Number: return left.numberValue() == right.numberValue();
    case ValueType::String: return left.stringId() == right.stringId();
    case ValueType::Function: return left.functionId() == right.functionId();
    case ValueType::Object: return left.objectId() == right.objectId();
    case ValueType::HostObject: return left.hostObjectId() == right.hostObjectId();
    }
    return false;
}

bool read(RuntimeContext& runtime, const Value& event, const char* name,
    Value& result)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    return event.isObject() && runtime.readObjectPropertyForHost(
        event.objectId(), name, result, error) && error == RuntimeErrorCode::None;
}

HostInstanceId targetSerial(ProbeState& state, RuntimeContext& runtime,
    const Value& target)
{
    const Value* outer = runtime.lookup(SourceView("outer", 5));
    const Value* inner = runtime.lookup(SourceView("inner", 5));
    const Value* leaf = runtime.lookup(SourceView("leaf", 4));
    if (outer && outer->isHostObject() && same(*outer, target)) return state.outer;
    if (inner && inner->isHostObject() && same(*inner, target)) return state.inner;
    if (leaf && leaf->isHostObject() && same(*leaf, target)) return state.leaf;
    return 0;
}

void probe(void* opaque, NavigatorScriptHostAdapter& adapter,
    RuntimeContext& runtime, const Value& event, bool beforeListener)
{
    auto& state = *static_cast<ProbeState*>(opaque);
    Value type, target, current, phase, related, prevented;
    const bool readable = read(runtime, event, "type", type) &&
        read(runtime, event, "target", target) &&
        read(runtime, event, "currentTarget", current) &&
        read(runtime, event, "eventPhase", phase) &&
        read(runtime, event, "relatedTarget", related) &&
        read(runtime, event, "defaultPrevented", prevented);
    ++state.metadataChecks;
    if (!readable || !target.isHostObject() || !current.isHostObject() ||
        !phase.isNumber() || !prevented.isBoolean()) {
        ++state.metadataFailures;
        return;
    }
    const HostInstanceId anchorSerial = targetSerial(state, runtime, target);
    NavigatorScriptRelativeSelectorCounters* counters = &state.otherCounters;
    if (anchorSerial == state.outer) { counters = &state.outerCounters; ++state.outerCallbacks; }
    else if (anchorSerial == state.inner) { counters = &state.innerCounters; ++state.innerCallbacks; }
    else if (anchorSerial == state.leaf) { counters = &state.leafCounters; ++state.leafCallbacks; }
    if (phase.numberValue() == kEventPhaseCapturing) ++state.phaseCapture;
    else if (phase.numberValue() == kEventPhaseAtTarget) ++state.phaseTarget;
    else if (phase.numberValue() == kEventPhaseBubbling) ++state.phaseBubble;
    if (anchorSerial == state.inner && beforeListener &&
        phase.numberValue() == kEventPhaseAtTarget) {
        const std::uint64_t outerCallsBeforeReset =
            state.outerCounters.relativeEvaluationCalls;
        state.innerCounters = {};
        state.innerExpectedCalls = 0;
        ++state.innerCounterResets;
        if (state.outerCounters.relativeEvaluationCalls != outerCallsBeforeReset)
            ++state.resetIsolationFailures;
    }

    const auto evaluate = [&](HostInstanceId serial, Relation relation,
        const NavigatorScriptSimpleSelectorCoreDescriptor& core,
        const NavigatorScriptSelectorDescriptor& text) {
        if (counters == &state.outerCounters) ++state.outerExpectedCalls;
        else if (counters == &state.innerCounters) ++state.innerExpectedCalls;
        else if (counters == &state.leafCounters) ++state.leafExpectedCalls;
        else ++state.otherExpectedCalls;
        return NavigatorScriptRelativeSelectorTestAccess::evaluate(adapter,
            {serial, adapter.generation(), kNavigatorElementHostKind},
            relation, core, text, counters);
    };
    const std::array<Match, 6> results = {
        evaluate(state.outer, Relation::Descendant, state.outerDescendant,
            state.outerDescendantStorage),
        evaluate(state.outer, Relation::Child, state.outerChild,
            state.outerChildStorage),
        evaluate(state.leaf, Relation::AdjacentSibling, state.innerAdjacent,
            state.innerAdjacentStorage),
        evaluate(state.leaf, Relation::GeneralSibling, state.innerGeneral,
            state.innerGeneralStorage),
        evaluate(state.outer, Relation::Descendant, state.innerId,
            state.innerIdStorage),
        evaluate(state.outer, Relation::Child, state.emptyPseudo,
            state.emptyStorage),
    };
    ++state.relationChecks;
    if (results != std::array<Match, 6>{Match::Match, Match::Match,
            Match::Match, Match::Match, Match::Match, Match::Match}) {
        ++state.relationFailures;
        if (state.relationFailures < 4)
            std::cerr << "relation results=" << static_cast<int>(results[0])
                << ',' << static_cast<int>(results[1]) << ','
                << static_cast<int>(results[2]) << ','
                << static_cast<int>(results[3]) << ','
                << static_cast<int>(results[4]) << ','
                << static_cast<int>(results[5]) << '\n';
    }

    const HostInstanceId eventTargetAnchor = targetSerial(state, runtime, target);
    if (eventTargetAnchor != 0) {
        const Match targetAnchorResult = evaluate(eventTargetAnchor,
            Relation::Descendant, state.outerDescendant,
            state.outerDescendantStorage);
        const Match expected = eventTargetAnchor == state.leaf
            ? Match::NoMatch : Match::Match;
        if (targetAnchorResult != expected) ++state.relationFailures;
    }
    const HostInstanceId eventCurrentAnchor = targetSerial(state, runtime, current);
    if (eventCurrentAnchor != 0) {
        const Match currentAnchorResult = evaluate(eventCurrentAnchor,
            Relation::Descendant, state.outerDescendant,
            state.outerDescendantStorage);
        const Match expected = eventCurrentAnchor == state.leaf
            ? Match::NoMatch : Match::Match;
        if (currentAnchorResult != expected) ++state.relationFailures;
    }
    const Match uncounted = NavigatorScriptRelativeSelectorTestAccess::evaluate(
        adapter, {state.outer, adapter.generation(), kNavigatorElementHostKind},
        Relation::Descendant, state.outerDescendant,
        state.outerDescendantStorage, nullptr);
    if (uncounted != results[0]) ++state.relationFailures;
    const Match staleGeneration = NavigatorScriptRelativeSelectorTestAccess::evaluate(
        adapter, {state.outer, static_cast<HostGenerationId>(adapter.generation() + 1u),
            kNavigatorElementHostKind}, Relation::Descendant, state.outerDescendant,
        state.outerDescendantStorage, nullptr);
    const Match invalidSerial = NavigatorScriptRelativeSelectorTestAccess::evaluate(
        adapter, {0, adapter.generation(), kNavigatorElementHostKind},
        Relation::Descendant, state.outerDescendant, state.outerDescendantStorage,
        nullptr);
    if (staleGeneration != Match::Invalid || invalidSerial != Match::Invalid)
        ++state.relationFailures;

    Value typeAfter, targetAfter, currentAfter, phaseAfter, relatedAfter,
        preventedAfter;
    const bool unchanged = read(runtime, event, "type", typeAfter) &&
        read(runtime, event, "target", targetAfter) &&
        read(runtime, event, "currentTarget", currentAfter) &&
        read(runtime, event, "eventPhase", phaseAfter) &&
        read(runtime, event, "relatedTarget", relatedAfter) &&
        read(runtime, event, "defaultPrevented", preventedAfter) &&
        same(type, typeAfter) && same(target, targetAfter) &&
        same(current, currentAfter) && same(phase, phaseAfter) &&
        same(related, relatedAfter) && same(prevented, preventedAfter);
    if (!unchanged) {
        ++state.metadataFailures;
        if (state.metadataFailures < 4)
            std::cerr << "metadata snapshot changed; phase=" << phase.numberValue()
                << " default=" << prevented.booleanValue() << '\n';
    }
}

void expect(bool condition, const char* message, std::size_t& checks,
    std::size_t& failures)
{
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

HostInstanceId serialById(const gxos::web::WebDocument& document,
    const char* id)
{
    for (const auto& element : document.structuralElements)
        if (element.id == id) return element.serial;
    return 0;
}
} // namespace

int main()
{
    std::size_t checks = 0, failures = 0;
    RuntimeLimits runtimeLimits;
    runtimeLimits.maxEnvironments = 4096;
    NavigatorScriptExecutionHarness harness(runtimeLimits);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string html =
        "<html><body><div id='outer' class='outerAnchor'>"
        "<button id='outerChild' class='outerChild'></button>"
        "<div id='inner' class='innerAnchor'>"
        "<button id='leaf' class='innerTarget'></button>"
        "<i id='innerNext' class='innerNext'></i>"
        "<i id='innerFar' class='innerFar'></i></div>"
        "<input id='focusA'><input id='focusB'>"
        "</div></body></html>";
    expect(harness.loadHtml("file:///js62r4.html", html, error) &&
        error == RuntimeErrorCode::None, "JS62R4 fixture loads", checks, failures);
    ProbeState state;
    state.harness = &harness;
    state.outer = serialById(harness.document(), "outer");
    state.inner = serialById(harness.document(), "inner");
    state.leaf = serialById(harness.document(), "leaf");
    state.outerDescendant = classCore(11);
    state.outerChild = classCore(10);
    state.innerAdjacent = classCore(9);
    state.innerGeneral = classCore(8);
    state.innerId.valid = true;
    state.innerId.idLength = 5;
    state.emptyPseudo.valid = true;
    state.emptyPseudo.statePseudo = NavigatorScriptStatePseudo::Empty;
    state.outerDescendantStorage = storage("innerTarget");
    state.outerChildStorage = storage("outerChild");
    state.innerAdjacentStorage = storage("innerNext");
    state.innerGeneralStorage = storage("innerFar");
    state.innerIdStorage = storage("inner");
    state.emptyStorage = storage("");
    harness.hostAdapter().setRelativeSelectorDispatchProbe(probe, &state);
    const auto setup = harness.execute(R"JS(
var outer = document.querySelector('#outer');
var inner = document.querySelector('#inner');
var leaf = document.querySelector('#leaf');
var focusA = document.querySelector('#focusA');
var focusB = document.querySelector('#focusB');
var outerRuns = 0, innerRuns = 0, leafRuns = 0;
var phaseLog = '';
var outerNestedMetadata = true, innerNestedMetadata = true;
document.querySelector('body').addEventListener('click', function(e) {
  if (e.eventPhase === Event.CAPTURING_PHASE) phaseLog = phaseLog + 'c';
}, {capture:true});
outer.addEventListener('click', function(e) {
  if (e.target === outer) {
    outerRuns = outerRuns + 1;
    var savedTarget = e.target, savedCurrent = e.currentTarget;
    var savedPhase = e.eventPhase, savedRelated = e.relatedTarget;
    e.preventDefault();
    inner.click();
    outerNestedMetadata = outerNestedMetadata && e.target === savedTarget &&
      e.currentTarget === savedCurrent && savedCurrent === outer &&
      e.eventPhase === savedPhase && savedPhase === Event.AT_TARGET &&
      e.relatedTarget === savedRelated && e.relatedTarget === null &&
      e.defaultPrevented === true;
  }
});
inner.addEventListener('click', function(e) {
  if (e.target === inner) {
    innerRuns = innerRuns + 1;
    var savedTarget = e.target, savedCurrent = e.currentTarget;
    var savedPhase = e.eventPhase, savedDefault = e.defaultPrevented;
    leaf.click();
    innerNestedMetadata = innerNestedMetadata && e.target === savedTarget &&
      e.currentTarget === savedCurrent && savedCurrent === inner &&
      e.eventPhase === savedPhase && savedPhase === Event.AT_TARGET &&
      e.defaultPrevented === savedDefault;
  }
});
leaf.addEventListener('click', function(e) {
  if (e.target === leaf) leafRuns = leafRuns + 1;
});
var focusMetadata = true;
focusA.addEventListener('focusout', function(e) {
  focusMetadata = focusMetadata && e.relatedTarget === focusB &&
    e.target === focusA && e.eventPhase === Event.AT_TARGET;
});
focusB.addEventListener('focusin', function(e) {
  focusMetadata = focusMetadata && e.relatedTarget === focusA &&
    e.target === focusB && e.eventPhase === Event.AT_TARGET;
});
)JS");
    if (!setup.succeeded()) std::cerr << "setup status="
        << static_cast<unsigned>(setup.status) << " error="
        << static_cast<unsigned>(setup.runtimeError.code) << '\n';
    expect(setup.succeeded(), "listeners and fixture globals initialize", checks, failures);

    const std::array<NavigatorScriptSimpleSelectorCoreDescriptor, 6>
        selectorCoreSnapshot = {state.outerDescendant, state.outerChild,
            state.innerAdjacent, state.innerGeneral, state.innerId,
            state.emptyPseudo};
    const std::array<NavigatorScriptSelectorDescriptor, 6>
        selectorStorageSnapshot = {state.outerDescendantStorage,
            state.outerChildStorage, state.innerAdjacentStorage,
            state.innerGeneralStorage, state.innerIdStorage,
            state.emptyStorage};
    for (std::size_t i = 0; i < 100; ++i) {
        bool defaultPrevented = false;
        expect(harness.dispatchClick(state.outer, error, &defaultPrevented) &&
            error == RuntimeErrorCode::None && defaultPrevented,
            "nested click cycle dispatches and preserves cancellation", checks, failures);
    }
    expect(state.outerCallbacks > 0 && state.innerCallbacks > 0 &&
        state.leafCallbacks > 0, "outer, inner, and depth-two listeners were probed",
        checks, failures);
    expect(state.metadataChecks > 1000 && state.metadataFailures == 0,
        "Event metadata is stable across instrumented relative calls",
        checks, failures);
    expect(state.relationChecks == state.metadataChecks &&
        state.relationFailures == 0, "all four relations remain deterministic",
        checks, failures);
    expect(state.phaseCapture > 0 && state.phaseTarget > 0 && state.phaseBubble > 0,
        "capture, target, and bubble phases were observed", checks, failures);
    expect(state.outerCounters.relativeEvaluationCalls == state.outerExpectedCalls &&
        state.innerCounters.relativeEvaluationCalls == state.innerExpectedCalls &&
        state.leafCounters.relativeEvaluationCalls == state.leafExpectedCalls &&
        state.otherCounters.relativeEvaluationCalls == state.otherExpectedCalls,
        "independent counter sets contain only their target's calls", checks, failures);
    expect(state.innerCounterResets == 100 && state.resetIsolationFailures == 0,
        "inner counters reset per nested dispatch without changing outer totals",
        checks, failures);
    const std::array<NavigatorScriptSimpleSelectorCoreDescriptor, 6>
        selectorCoresAfter = {state.outerDescendant, state.outerChild,
            state.innerAdjacent, state.innerGeneral, state.innerId,
            state.emptyPseudo};
    const std::array<NavigatorScriptSelectorDescriptor, 6>
        selectorStorageAfter = {state.outerDescendantStorage,
            state.outerChildStorage, state.innerAdjacentStorage,
            state.innerGeneralStorage, state.innerIdStorage,
            state.emptyStorage};
    expect(std::memcmp(selectorCoresAfter.data(), selectorCoreSnapshot.data(),
        sizeof(selectorCoreSnapshot)) == 0 &&
        std::memcmp(selectorStorageAfter.data(), selectorStorageSnapshot.data(),
            sizeof(selectorStorageSnapshot)) == 0,
        "all selector cores and storages remain byte-identical", checks, failures);
    expect(harness.execute("var countsOK = outerRuns === 100 && innerRuns === 100 && leafRuns === 100 && outerNestedMetadata && innerNestedMetadata;").succeeded(),
        "nested event script completes", checks, failures);
    const Value* countsOK = harness.runtime().lookup(SourceView("countsOK", 8));
    expect(countsOK && countsOK->isBoolean() && countsOK->booleanValue(),
        "one hundred depth-two nested dispatches preserve both Event objects",
        checks, failures);

    expect(harness.focusElement(serialById(harness.document(), "focusA"), error) &&
        harness.focusElement(serialById(harness.document(), "focusB"), error),
        "focus transitions dispatch successfully", checks, failures);
    const auto focusResult = harness.execute("var focusOK = focusMetadata;");
    const Value* focusOK = harness.runtime().lookup(SourceView("focusOK", 7));
    expect(focusResult.succeeded() && focusOK && focusOK->isBoolean() &&
        focusOK->booleanValue(), "focus relatedTarget survives relative evaluation",
        checks, failures);

    harness.hostAdapter().setRelativeSelectorDispatchProbe(nullptr, nullptr);
    std::cout << "JS62R4 event/reentrancy checks: " << checks - failures << "/"
        << checks << " passed; probes=" << state.metadataChecks
        << ", nested outer/inner/depth-two callbacks=" << state.outerCallbacks
        << "/" << state.innerCallbacks << "/" << state.leafCallbacks
        << ", counters=" << state.outerCounters.relativeEvaluationCalls << "/"
        << state.innerCounters.relativeEvaluationCalls << "/"
        << state.leafCounters.relativeEvaluationCalls << ", relation failures="
        << state.relationFailures << ", metadata failures="
        << state.metadataFailures << '\n';
    return failures == 0 ? 0 : 1;
}
