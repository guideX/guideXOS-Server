#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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
    const bool matched = value != nullptr && value->isNumber() &&
        value->numberValue() == expected;
    if (!matched && value != nullptr && value->isNumber())
        std::cerr << "INFO: " << label << " actual=" << value->numberValue()
            << " expected=" << expected << "\n";
    expect(matched, label);
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error)
{
    expect(!fixture.empty(), "fixture file is available");
    expect(harness.loadHtml("file:///javascript-js51.html", fixture, error),
        "JS51 fixture parses into the authoritative document");
    expect(error == RuntimeErrorCode::None, "fixture load has no error");
    expect(harness.relayout(), "fixture relayout succeeds");
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

void testListParsingAndQueryOrder()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.getElementById("panel");
var group = document.getElementById("group");
var early = document.getElementById("early");
var late = document.getElementById("late");
var sibling = document.getElementById("sibling");
var separatorWhitespace =
  document.querySelector("button,#early") === early &&
  document.querySelector("button, #early") === early &&
  document.querySelector("button ,#early") === early &&
  document.querySelector("button , #early") === early &&
  document.querySelector(" button , #early ") === early;
var firstStructural = document.querySelector("#late, #early") === early;
var reversedStructural = document.querySelector("#early, #late") === early;
var structuralIndependentOfMemberOrder =
  document.querySelector("#late, #early") ===
  document.querySelector("#early, #late");
var overlapQueryOnce = document.querySelector(".action, button") === early;
var scopedDescendant = panel.querySelector("#panel, #late") === late &&
  panel.querySelector("#outside, #sibling") === sibling;
var scopeExcludesSelfAndOutside =
  panel.querySelector("#panel") === null &&
  panel.querySelectorAll("#panel").length === 0 &&
  panel.querySelector("#outside, #late") === late;
var fourMemberList = document.querySelectorAll(
  "#missing, #early, #late, #sibling");
var fourMemberCount = fourMemberList.length;
var fourMembersAccepted = fourMemberList.length === 3 &&
  fourMemberList[0] === early && fourMemberList[1] === late &&
  fourMemberList[2] === sibling;
var invalidFiveMembers = document.querySelector("#early,#late,#sibling,#panel,#outside") === null &&
  document.querySelectorAll("#early,#late,#sibling,#panel,#outside").length === 0;
var invalidEmptyMembers =
  document.querySelector(",button") === null &&
  document.querySelector("button,") === null &&
  document.querySelector("button,,input") === null &&
  document.querySelector("button, ,input") === null &&
  document.querySelectorAll(",button").length === 0 &&
  document.querySelectorAll("button,").length === 0 &&
  !early.matches("button,,input") &&
  early.closest("button, ,input") === null;
var invalidMemberInvalidatesWholeList =
  document.querySelector("button, [type^=text]") === null &&
  document.querySelectorAll("button, [type^=text]").length === 0 &&
  !early.matches("button, :hover") &&
  early.closest("button, *.foo") === null &&
  document.querySelector(".panel > .group > button, input") === null;
var mixedSimpleAndRelation =
  document.querySelector("#outside, #panel > #group") === group &&
  document.querySelectorAll("#panel > #group, #group #nested").length === 2;
var quotedCommaAndBracket =
  panel.matches("[data-label='a],b'], #missing") &&
  panel.matches('[data-label="a],b"], #missing');
panel.setAttribute("data-label", "a,b");
var quotedCommaValue = panel.matches('[data-label="a,b"], #missing') &&
  document.querySelectorAll('[data-label="a,b"], #early').length === 2;
var duplicateMembersAccepted =
  document.querySelectorAll(".action, .action").length === 3;
var indexedMissUndefined = fourMemberList[999] === undefined;
var canonicalIdentity = fourMemberList[0] === early &&
  early === document.querySelector("#early") &&
  early === document.getElementById("early") && early === group.children[0];
var relationalMatches =
  early.matches(".group > button.action, #missing") &&
  sibling.matches("#late ~ button, #missing") &&
  document.querySelector("#missing, #group > #early") === early;
var malformedChainNotPartiallyApplied =
  document.querySelector("#group > #early > span, #early") === null &&
  document.querySelectorAll("#group > #early > span, #early").length === 0;
)JS");
    expect(result.succeeded(), "list parser and query-order cases execute");
    const char* names[] = {
        "separatorWhitespace", "firstStructural", "reversedStructural",
        "structuralIndependentOfMemberOrder", "overlapQueryOnce",
        "scopedDescendant", "scopeExcludesSelfAndOutside", "fourMembersAccepted",
        "invalidFiveMembers", "invalidEmptyMembers",
        "invalidMemberInvalidatesWholeList", "mixedSimpleAndRelation",
        "quotedCommaAndBracket", "quotedCommaValue", "duplicateMembersAccepted",
        "indexedMissUndefined", "canonicalIdentity", "relationalMatches",
        "malformedChainNotPartiallyApplied",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("list/query: ") + name);

    std::string oversized(257u, ' ');
    const std::string oversizedScript =
        "var over256 = document.querySelector(\"" + oversized +
        "\") === null && document.querySelectorAll(\"" + oversized +
        "\").length === 0;";
    const ScriptResult bounds = harness.execute(oversizedScript);
    expect(bounds.succeeded(), "overall selector input cap cases execute");
    expectBoolean(harness, "over256", true,
        "entire selector-list input is limited to 256 bytes");
    const std::string boundarySelector = "#early,#" + std::string(248u, 'x');
    const std::string boundaryScript =
        "var exactly256 = document.querySelector(\"" + boundarySelector +
        "\") === early && document.querySelectorAll(\"" + boundarySelector +
        "\").length === 1;";
    const ScriptResult boundary = harness.execute(boundaryScript);
    expect(boundary.succeeded(), "256-byte selector-list boundary executes");
    expectBoolean(harness, "exactly256", true,
        "a valid selector list at exactly 256 bytes is accepted");

    const ScriptResult readonly = harness.execute(
        "fourMemberList.length = 0;");
    expect(!readonly.succeeded(), "selector-list collection is read-only");
    expectNumber(harness, "fourMemberCount", 3.0,
        "read-only write leaves live selector collection intact");
}

void testUnionMatchesClosestAndRelations()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.getElementById("panel");
var group = document.getElementById("group");
var early = document.getElementById("early");
var late = document.getElementById("late");
var sibling = document.getElementById("sibling");
var nested = document.getElementById("nested");
var union = document.querySelectorAll(".action, button");
var unionCount = union.length;
var unionStructuralOrder = union.length === 3 && union[0] === early &&
  union[1] === late && union[2] === sibling;
var reverseUnion = document.querySelectorAll("button, .action");
var memberOrderDoesNotReorderUnion = reverseUnion.length === union.length &&
  reverseUnion[0] === union[0] && reverseUnion[1] === union[1] &&
  reverseUnion[2] === union[2];
var overlappingThreeMembers =
  document.querySelectorAll(".action, button, #early").length === 3;
var scopedUnion = panel.querySelectorAll(".field, [data-action]");
var scopedUnionCorrect = scopedUnion.length === 4 &&
  scopedUnion[0] === early && scopedUnion[1] === late &&
  scopedUnion[2] === document.getElementById("field") &&
  scopedUnion[3] === sibling;
var matchFirst = early.matches("button, #missing");
var matchSecond = early.matches("#missing, .primary");
var matchOverlap = early.matches(".primary, [data-action=save]");
var matchAttribute = late.matches("[data-action=save], [data-action=cancel]");
var matchNeither = !late.matches("#missing, [data-action=save]");
var matchRelationsIndependent = early.matches("#group > button, #outside ~ #early") &&
  sibling.matches("#late ~ button, #missing");
var closestSelf = early.closest("#early, .group") === early;
var closestSelfSecond = late.closest("#missing, #late") === late;
var closestNearestWins = nested.closest(".panel, .group") === group;
var closestNearestWinsReversed = nested.closest(".group, .panel") === group;
var closestIndependentOfMemberOrder =
  nested.closest(".panel, .group") === nested.closest(".group, .panel");
var closestOverlapSameAncestor = nested.closest("#group, div") === group;
var closestAttributeOrClass = early.closest("[data-state=ready], .panel") === early;
var closestNoMatch = nested.closest("#missing, .absent") === null;
var relationKindsIndependent =
  document.querySelector("#panel > #group, #late + #field") === group &&
  document.querySelector("#missing, #late + #field") === document.getElementById("field") &&
  document.querySelector("#late ~ #sibling, #missing") === sibling;
var attributeRelationMember =
  document.querySelector("#panel[data-state=ready] > #group, #missing") === group;
)JS");
    expect(result.succeeded(), "union, matches, closest, and relation cases execute");
    const char* names[] = {
        "unionStructuralOrder", "memberOrderDoesNotReorderUnion",
        "overlappingThreeMembers", "scopedUnionCorrect", "matchFirst",
        "matchSecond", "matchOverlap", "matchAttribute", "matchNeither",
        "matchRelationsIndependent", "closestSelf", "closestSelfSecond",
        "closestNearestWins", "closestNearestWinsReversed",
        "closestIndependentOfMemberOrder", "closestOverlapSameAncestor",
        "closestAttributeOrClass", "closestNoMatch", "relationKindsIndependent",
        "attributeRelationMember",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("union/matches/closest: ") + name);
    expectNumber(harness, "unionCount", 3.0,
        "overlapping simple members return each Element once");
}

void testMutationsFormsAndLiveCollections()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.getElementById("panel");
var early = document.getElementById("early");
var late = document.getElementById("late");
var field = document.getElementById("field");
var agree = document.getElementById("agree");
var live = document.querySelectorAll("[data-state=ready], .always");
var initialUnion = live.length === 2 && live[0] === panel && live[1] === early;
early.removeAttribute("data-state");
var alternateMemberRetainsOnce = live.length === 2 && live[1] === early;
early.setAttribute("data-state", "ready");
var setAttributeVisible = live.length === 2 && live[1] === early;
early.setAttribute("data-state", "busy");
var replaceAttributeVisible = live.length === 2 && live[1] === early &&
  !early.matches("[data-state=ready], .never");
early.removeAttribute("class");
var removeDropsLastMatch = live.length === 1 && live[0] === panel;
late.setAttribute("data-state", "ready");
var liveGain = live.length === 2 && live[1] === late;
late.setAttribute("data-state", "busy");
var liveReplaceLoss = live.length === 1;
late.removeAttribute("data-state");
var liveRemoveStable = live.length === 1;
var classes = document.querySelectorAll(".featured, .secondary");
early.setAttribute("class", "featured");
var classMutationGain = classes.length === 2 && classes[0] === early;
early.setAttribute("class", "plain");
var classMutationLoss = classes.length === 1 && classes[0] === late;
var ids = document.querySelectorAll("#early, #late");
early.setAttribute("id", "renamed");
var idMutationLoss = ids.length === 1 && ids[0] === late;
late.setAttribute("id", "early");
var idMutationGain = ids.length === 1 && ids[0] === late;
var attrs = document.querySelectorAll("[data-action=save], .fallback");
var attrBeforeRemoval = attrs.length === 2;
document.getElementById("outside").removeAttribute("data-action");
var removalNoAlternate = attrs.length === 1 && attrs[0] === early;
var retainedChecked = agree.matches("[checked], .fallback");
agree.checked = false;
var retainedCheckedAfterCurrentStateChange =
  agree.matches("[checked], .fallback") && !agree.matches("[checked=no], .fallback");
field.value = "edited";
var retainedValueAfterCurrentStateChange =
  field.matches("[value=initial], .fallback") &&
  !field.matches("[value=edited], .fallback");
var mutationDidNotChangeOtherMembership =
  document.querySelectorAll("#group, .group").length === 1;
var finalLiveCount = live.length;
var finalClassCount = classes.length;
)JS");
    expect(result.succeeded(), "mutation and live selector-list cases execute");
    const char* names[] = {
        "initialUnion", "alternateMemberRetainsOnce", "setAttributeVisible",
        "replaceAttributeVisible", "removeDropsLastMatch", "liveGain",
        "liveReplaceLoss", "liveRemoveStable", "classMutationGain",
        "classMutationLoss", "idMutationLoss", "idMutationGain",
        "attrBeforeRemoval", "removalNoAlternate", "retainedChecked",
        "retainedCheckedAfterCurrentStateChange",
        "retainedValueAfterCurrentStateChange",
        "mutationDidNotChangeOtherMembership",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("mutation/live: ") + name);
    expectNumber(harness, "finalLiveCount", 1.0,
        "held list collection reflects current union membership");
    expectNumber(harness, "finalClassCount", 1.0,
        "held class list reflects current class state");
}

void testEventsAndStress()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const ScriptResult result = harness.execute(R"JS(
var panel = document.getElementById("panel");
var group = document.getElementById("group");
var early = document.getElementById("early");
var nestedTarget = document.getElementById("outside");
var delegated = false;
var closestDelegated = false;
var sameCallbackMutation = false;
var nestedSeen = false;
var nestedMetadata = false;
var outerMetadataBefore = false;
var outerMetadataAfter = false;
nestedTarget.addEventListener("click", function (event) {
  nestedSeen = event.target === nestedTarget &&
    event.target.matches("[data-action=save], .never");
  nestedMetadata = event.currentTarget === nestedTarget && event.eventPhase === 2 &&
    event.relatedTarget === null && event.defaultPrevented === false;
});
panel.addEventListener("click", function (event) {
  if (event.target.matches("[data-action=save], [data-action=cancel]")) {
    delegated = true;
    closestDelegated = event.target.closest(".panel, .group") ===
      group;
    event.target.setAttribute("data-state", "clicked");
    sameCallbackMutation = event.target.matches(".always, [data-state=clicked]");
    outerMetadataBefore = event.target === early && event.currentTarget === panel &&
      event.eventPhase === 3 && event.relatedTarget === null &&
      event.defaultPrevented === false;
    nestedTarget.click();
    outerMetadataAfter = event.target === early && event.currentTarget === panel &&
      event.eventPhase === 3 && event.relatedTarget === null &&
      event.defaultPrevented === false;
  }
});
early.click();
var eventMutationCollection =
  document.querySelectorAll("[data-state=clicked], .always");
var eventMutationVisible = eventMutationCollection.length === 1 &&
  eventMutationCollection[0] === early;
var stressMatches = true;
for (var i = 0; i < 360; i = i + 1) {
  if (!early.matches("#missing, [data-action=save], .never")) stressMatches = false;
}
var stressQueries = true;
var stressAll = document.querySelectorAll(".action, button");
var stressAllCount = stressAll.length;
for (var j = 0; j < 240; j = j + 1) {
  if (document.querySelector("#late, #early") !== early ||
      document.querySelectorAll("button, .action").length !== 3 ||
      stressAll[0] !== early) stressQueries = false;
}
var stressMutation = document.querySelectorAll("[data-state=ready], .always");
var stressMutationStable = true;
for (var k = 0; k < 40; k = k + 1) {
  if ((k % 2) === 0) early.setAttribute("data-state", "ready");
  else early.removeAttribute("data-state");
  if (stressMutation.length !== 2 || stressMutation[0] !== panel ||
      stressMutation[1] !== early)
    stressMutationStable = false;
}
)JS");
    expect(result.succeeded(), "authentic event delegation and bounded stress execute");
    if (!result.succeeded()) {
        std::cerr << "event/stress status=" << static_cast<unsigned>(result.status)
            << " runtime=" << static_cast<unsigned>(result.runtimeError.code)
            << " location=" << result.runtimeError.location.line << ":"
            << result.runtimeError.location.column
            << " lexer=" << static_cast<unsigned>(result.lexerError.code)
            << " parser=" << static_cast<unsigned>(result.parserError.code)
            << "\n";
    }
    const char* names[] = {
        "delegated", "closestDelegated", "sameCallbackMutation", "nestedSeen",
        "nestedMetadata", "outerMetadataBefore", "outerMetadataAfter",
        "eventMutationVisible", "stressMatches", "stressQueries",
        "stressMutationStable",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("event/stress: ") + name);
    expectNumber(harness, "stressAllCount", 3.0,
        "repeated overlapping live reads remain deduplicated");

    RuntimeErrorCode capacityError = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness capacity;
    loadFixture(capacity, capacityError);
    const ScriptResult nearCapacity = capacity.execute(R"JS(
var capacityOk = true;
for (var n = 0; n < 127; n = n + 1) {
  if (document.querySelectorAll("[data-probe=v" + n + "], .missing").length !== 0)
    capacityOk = false;
}
var reusedCapacityRecord = document.querySelectorAll(
  "[data-probe=v126], .missing").length === 0;
)JS");
    expect(nearCapacity.succeeded(), "127 distinct list collections fit registry");
    expectBoolean(capacity, "capacityOk", true,
        "near-capacity collection descriptors remain intact");
    expectBoolean(capacity, "reusedCapacityRecord", true,
        "equivalent list descriptor reuses its collection record");
    const ScriptResult fullCapacity = capacity.execute(
        "var lastList = document.querySelectorAll('[data-probe=last], .missing');");
    expect(fullCapacity.succeeded(), "128th list collection uses final fixed slot");
    const ScriptResult overflowCapacity = capacity.execute(
        "var overCapacity = document.querySelectorAll('[data-probe=overflow], .missing');");
    expect(!overflowCapacity.succeeded(),
        "selector collection registry does not grow beyond 128 records");
}

void testStaleGenerationAndSerialReuse()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error);
    const std::uint64_t oldSerial = serialById(harness, "early");
    expect(oldSerial != 0u, "stale test captures list Element serial");
    expect(harness.execute(R"JS(
var oldListElement = document.querySelector("#early, .action");
var oldListCollection = document.querySelectorAll("#early, .action");
)JS").succeeded(), "stale test captures Element and selector-list collection");
    expect(harness.invalidateDocumentGeneration(error),
        "stale test invalidates document generation");
    const ScriptResult stale = harness.execute(R"JS(
var staleListMatches = oldListElement.matches("#early, .action") === false;
var staleListClosest = oldListElement.closest("#early, .action") === null;
)JS");
    expect(stale.succeeded(), "stale list methods fail closed without throwing");
    expectBoolean(harness, "staleListMatches", true,
        "stale Element matches returns false before selector evaluation");
    expectBoolean(harness, "staleListClosest", true,
        "stale Element closest returns null before selector evaluation");
    expect(!harness.execute("var staleListLength = oldListCollection.length;").succeeded(),
        "stale list collection cannot read replacement generation");

    harness.document() = gxos::web::parseHtml(
        "file:///js51-replacement.html", fixture);
    const std::uint64_t newSerial = serialById(harness, "early");
    expect(newSerial == oldSerial,
        "replacement document deliberately reuses the old Element serial");
    expect(harness.runtime().installHostGlobal("newListElement", newSerial,
            gxos::javascript::kNavigatorElementHostKind, error),
        "replacement Element is installed in current generation");
    const ScriptResult replacement = harness.execute(R"JS(
var freshListMatches = newListElement.matches("#early, .action");
var staleCannotMatchReusedSerial = oldListElement.matches("#early, .action") === false;
var staleCannotClosestIntoReplacement = oldListElement.closest("#early, .action") === null;
)JS");
    expect(replacement.succeeded(), "serial-reuse list calls execute safely");
    expectBoolean(harness, "freshListMatches", true,
        "fresh generation evaluates its own list selector state");
    expectBoolean(harness, "staleCannotMatchReusedSerial", true,
        "old handle cannot match a reused serial");
    expectBoolean(harness, "staleCannotClosestIntoReplacement", true,
        "old handle cannot closest into a reused serial");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js51.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testListParsingAndQueryOrder();
    testUnionMatchesClosestAndRelations();
    testMutationsFormsAndLiveCollections();
    testEventsAndStress();
    testStaleGenerationAndSerialReuse();
    if (failures != 0) {
        std::cerr << failures << " JS51 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS51 checks: " << checks
        << "/" << checks << " passed\n";
    return 0;
}
