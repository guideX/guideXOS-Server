#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
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
    if (value->isBoolean()) {
        if (value->booleanValue() != expected)
            std::cerr << "INFO: " << label << " actual="
                << (value->booleanValue() ? "true" : "false")
                << " expected=" << (expected ? "true" : "false") << "\n";
        expect(value->booleanValue() == expected, label + ": value");
    }
}

void expectNumber(const NavigatorScriptExecutionHarness& harness,
    const char* name, double expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::Number, label + ": Number");
    if (value->isNumber()) expect(value->numberValue() == expected,
        label + ": value");
}

void loadFixture(NavigatorScriptExecutionHarness& harness,
    RuntimeErrorCode& error, const char* label)
{
    expect(!fixture.empty(), "JS52 fixture is available");
    expect(harness.loadHtml("file:///javascript-js52.html", fixture, error),
        std::string(label) + ": fixture parses");
    expect(error == RuntimeErrorCode::None,
        std::string(label) + ": fixture load succeeds");
    expect(harness.relayout(), std::string(label) + ": relayout succeeds");
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

const gxos::web::FormRuntimeControlState* runtimeStateBySerial(
    const gxos::web::WebDocument& document, std::uint64_t serial)
{
    const std::size_t count = std::min(document.formRuntimeState.count,
        gxos::web::kFormRuntimeControlCap);
    for (std::size_t index = 0u; index < count; ++index) {
        const gxos::web::FormRuntimeControlState& state =
            document.formRuntimeState.controls[index];
        if (state.logicalSerial == serial && state.metadataValid) return &state;
    }
    return nullptr;
}

void initializeReplacementRuntime(gxos::web::WebDocument& document,
    std::uint64_t focusedSerial)
{
    document.formRuntimeState = gxos::web::FormRuntimeStateTable{};
    document.formRuntimeState.initialized = true;
    document.formRuntimeState.documentGeneration = 1u;
    for (const gxos::web::HtmlElementRef& element : document.structuralElements) {
        const gxos::web::FormControlMetadata& metadata = element.formControl;
        if (element.serial == 0u || !metadata.metadataComplete ||
            !metadata.supported || document.formRuntimeState.count >=
                gxos::web::kFormRuntimeControlCap) continue;
        gxos::web::FormRuntimeControlState& state =
            document.formRuntimeState.controls[
                document.formRuntimeState.count++];
        state.logicalSerial = element.serial;
        state.type = metadata.type;
        state.checked = metadata.checked;
        state.defaultChecked = metadata.checked;
        state.disabled = metadata.disabled;
        state.metadataValid = true;
    }
    document.formRuntimeState.focusedLogicalSerial = focusedSerial;
    document.formRuntimeState.focusedDocumentGeneration =
        focusedSerial == 0u ? 0u : 1u;
    document.formRuntimeState.focusValid = focusedSerial != 0u;
}

void testParserAndSharedSelectorComposition()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "parser");
    const ScriptResult result = harness.execute(R"JS(
var agree = document.getElementById("agree");
var parserBarePseudo = agree.matches(":checked");
var parserPseudoCase = agree.matches(":CHECKED") && agree.matches(":Checked") &&
  agree.matches("INPUT:CHECKED");
var parserTagPseudo = agree.matches("input:checked");
var parserIdPseudo = agree.matches("#agree:checked");
var parserClassPseudo = agree.matches(".field:checked");
var parserAttributePseudo = agree.matches("input[type=checkbox]:checked");
var parserFullCompound = agree.matches(
  "input#agree.field.checkbox[type=checkbox]:checked");
var parserWrongTag = !agree.matches("button:checked");
var parserWrongClass = !agree.matches(".missing:checked");
var parserWrongAttribute = !agree.matches("input[type=radio]:checked");
var parserIdStillCaseSensitive = !agree.matches("#AGREE:checked");
var parserClassStillCaseSensitive = !agree.matches(".FIELD:checked");
var parserList = document.querySelectorAll(":checked, input:checked, :focus");
var parserListCount = parserList.length;
var parserListDeduplicates = parserList.length === 3 &&
  parserList[0] === agree && parserList[1] === document.getElementById("radio-a") &&
  parserList[2] === document.getElementById("option-a");
var parserUnknown = document.querySelector(":hover") === null &&
  document.querySelectorAll(":enabled").length === 0 &&
  !agree.matches(":active") && agree.closest(":first-child") === null;
var parserFunctionRejected = document.querySelector(":not(input)") === null &&
  document.querySelector(":is(input)") === null &&
  document.querySelector(":nth-child(1)") === null &&
  document.querySelector(":checked()") === null;
var parserSecondPseudoRejected = document.querySelector("input:checked:focus") === null &&
  !agree.matches("input:checked:focus") &&
  document.querySelectorAll("input:disabled:focus").length === 0;
var parserCanonicalOrder = !agree.matches("input:checked.field") &&
  !agree.matches("input:focus[type=checkbox]") &&
  agree.matches("input.field[type=checkbox]:checked");
var parserMalformedColon = document.querySelector(":") === null &&
  document.querySelector("input:") === null &&
  document.querySelector("input::checked") === null;
var parserUniversalPseudo = agree.matches("*:checked");
var parserUniversalAttributeStillRejected =
  document.querySelector("*[type=checkbox]") === null;
var parserListAllOrNothing = document.querySelector(
  ":checked, input:checked:focus") === null &&
  document.querySelectorAll(":checked, input:checked:focus").length === 0;
)JS");
    expect(result.succeeded(), "parser and compound integration execute");
    const char* names[] = {
        "parserBarePseudo", "parserPseudoCase", "parserTagPseudo",
        "parserIdPseudo", "parserClassPseudo", "parserAttributePseudo",
        "parserFullCompound", "parserWrongTag", "parserWrongClass",
        "parserWrongAttribute", "parserIdStillCaseSensitive",
        "parserClassStillCaseSensitive", "parserListDeduplicates",
        "parserUnknown", "parserFunctionRejected",
        "parserSecondPseudoRejected", "parserCanonicalOrder",
        "parserMalformedColon", "parserUniversalPseudo",
        "parserUniversalAttributeStillRejected", "parserListAllOrNothing",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("parser: ") + name);
    expectNumber(harness, "parserListCount", 3.0,
        "pseudo selector lists keep union deduplication");

    const std::string oversized(257u, ' ');
    const ScriptResult bounds = harness.execute(
        "var pseudoOver256 = document.querySelector(\"" + oversized +
        "\") === null && document.querySelectorAll(\"" + oversized +
        "\").length === 0;");
    expect(bounds.succeeded(), "256-byte selector cap check executes");
    expectBoolean(harness, "pseudoOver256", true,
        "state pseudos retain the 256-byte overall selector cap");
}

void testCheckedStateOptionsQueriesAndLiveCollections()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "checked");
    const ScriptResult result = harness.execute(R"JS(
var agree = document.getElementById("agree");
var scripted = document.getElementById("scripted");
var radioA = document.getElementById("radio-a");
var radioB = document.getElementById("radio-b");
var optionA = document.getElementById("option-a");
var optionB = document.getElementById("option-b");
var checkedInitial = agree.matches(":checked") && agree.matches("[checked]");
var checkedLive = document.querySelectorAll(":checked");
var inputCheckedLive = document.querySelectorAll("input:checked");
var radioLive = document.querySelectorAll("input:checked");
var checkedInitialCounts = checkedLive.length === 3 && inputCheckedLive.length === 2;
agree.checked = false;
var retainedVsCurrentFalse = agree.matches("[checked]") &&
  !agree.matches(":checked") && checkedLive.length === 2;
agree.checked = true;
var retainedVsCurrentTrueAgain = agree.matches("[checked]") &&
  agree.matches(":checked") && checkedLive.length === 3;
scripted.checked = true;
var currentTrueWithoutDefault = !scripted.matches("[checked]") &&
  scripted.matches(":checked") && inputCheckedLive.length === 3;
scripted.checked = false;
var scriptedFalseImmediate = !scripted.matches(":checked") &&
  inputCheckedLive.length === 2;
radioB.checked = true;
var radioSwitchCurrent = !radioA.matches(":checked") &&
  radioB.matches(":checked") && radioLive.length === 2;
radioA.checked = true;
var radioCollectionTracksScript = radioLive.length === 2 &&
  radioLive[0] === agree && radioLive[1] === radioA;
radioB.click();
var radioClickCurrent = radioB.matches(":checked") &&
  !radioA.matches(":checked") && radioLive.length === 2 &&
  radioLive[0] === agree && radioLive[1] === radioB;
optionB.selected = true;
var optionSelectionCurrent = !optionA.matches(":checked") &&
  optionB.matches(":checked") && optionA.matches("[selected]") &&
  !optionB.matches("[selected]");
var inputCheckedStillExclusive = inputCheckedLive.length === 2 &&
  inputCheckedLive[0] === agree && inputCheckedLive[1] === radioB;
agree.checked = true;
var beforeClick = agree.checked;
agree.click();
var clickImmediatelyObserved = agree.checked !== beforeClick &&
  agree.matches(":checked") === agree.checked && checkedLive.length === 2;
var nonCheckableFalse = !document.getElementById("fake-checked").matches(":checked") &&
  document.getElementById("fake-checked").matches("[checked]") &&
  !document.getElementById("save").matches(":checked") &&
  !document.getElementById("name").matches(":checked");
agree.checked = true;
var querySelectorStructural = document.querySelector(":checked") === agree;
var scopedQuery = document.getElementById("panel").querySelector("input:checked") === agree &&
  document.getElementById("panel").querySelectorAll(":checked").length === 3;
var selectorUnion = document.querySelectorAll(":checked, input:checked");
var selectorUnionDedup = selectorUnion.length === 3 &&
  selectorUnion[0] === agree && selectorUnion[1] === radioB &&
  selectorUnion[2] === optionB;
var relations = document.querySelector("#agree:checked + input") === radioA &&
  document.querySelector("#agree:checked ~ input:checked") === radioB &&
  document.querySelector("#form > input:checked") === agree &&
  radioB.matches("#agree:checked ~ input:checked");
var closestMatches = radioB.closest("#agree:checked, #radio-b:checked") === radioB &&
  document.getElementById("option-b").closest("#form, option:checked") ===
    document.getElementById("option-b");
)JS");
    expect(result.succeeded(), "checked, radio, option, and query cases execute");
    const char* names[] = {
        "checkedInitial", "checkedInitialCounts", "retainedVsCurrentFalse",
        "retainedVsCurrentTrueAgain", "currentTrueWithoutDefault",
        "scriptedFalseImmediate", "radioSwitchCurrent",
        "radioCollectionTracksScript", "radioClickCurrent",
        "optionSelectionCurrent", "inputCheckedStillExclusive",
        "clickImmediatelyObserved", "nonCheckableFalse",
        "querySelectorStructural", "scopedQuery", "selectorUnionDedup",
        "relations", "closestMatches",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("checked: ") + name);
}

void testDisabledStateAndDeferredMutation()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "disabled");
    const ScriptResult result = harness.execute(R"JS(
var save = document.getElementById("save");
var fake = document.getElementById("fake-disabled");
var disabledLive = document.querySelectorAll(":disabled");
var enabledInput = document.getElementById("name");
var inherited = document.getElementById("inherited-disabled");
var disabledFieldset = document.getElementById("disabled-group");
var notes = document.getElementById("notes");
var select = document.getElementById("disabled-select");
var option = document.getElementById("disabled-option");
var disabledInput = document.getElementById("disabled-input");
var disabledEach = save.matches(":disabled") &&
  document.getElementById("after-marker").matches(":disabled") &&
  disabledInput.matches(":disabled") && notes.matches(":disabled") &&
  select.matches(":disabled") &&
  option.matches(":disabled") && disabledFieldset.matches(":disabled");
var disabledSaveOnly = save.matches(":disabled");
var disabledAfterOnly = document.getElementById("after-marker").matches(":disabled");
var disabledInputOnly = disabledInput.matches(":disabled");
var disabledNotesOnly = notes.matches(":disabled");
var disabledSelectOnly = select.matches(":disabled");
var disabledOptionOnly = option.matches(":disabled");
var disabledFieldsetOnly = disabledFieldset.matches(":disabled");
var disabledApplicable = save.matches("button:disabled") &&
  disabledInput.matches("input:disabled") &&
  document.getElementById("after-marker").matches(":disabled") &&
  notes.matches("textarea:disabled") && select.matches("select:disabled") &&
  option.matches("option:disabled") && disabledFieldset.matches("fieldset:disabled");
var enabledFalse = !enabledInput.matches(":disabled") &&
  !document.getElementById("run").matches(":disabled");
var arbitraryAttributeDistinct = fake.matches("[disabled]") &&
  !fake.matches(":disabled") && !document.querySelector("div:disabled");
var inheritedFieldsetState = inherited.matches(":disabled") &&
  !inherited.matches("[disabled]") &&
  document.querySelector("fieldset:disabled > input:disabled") === inherited;
var disabledQuery = document.querySelector("button:disabled") === save &&
  document.querySelectorAll(":disabled").length === disabledLive.length &&
  document.querySelectorAll("button:disabled").length === 2;
var disabledRelations = document.querySelector("#marker ~ button:disabled") ===
  document.getElementById("after-marker");
)JS");
    expect(result.succeeded(), "disabled matching and applicability execute");
    const char* names[] = {
        "disabledEach", "disabledSaveOnly", "disabledAfterOnly",
        "disabledInputOnly", "disabledNotesOnly", "disabledSelectOnly", "disabledOptionOnly",
        "disabledFieldsetOnly", "disabledApplicable", "enabledFalse", "arbitraryAttributeDistinct",
        "inheritedFieldsetState", "disabledQuery", "disabledRelations",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("disabled: ") + name);

    const ScriptResult deferred = harness.execute(
        "save.removeAttribute('disabled');");
    expect(deferred.succeeded(),
        "JS49 deferred disabled removal returns as a bounded no-op");
    const ScriptResult afterDeferred = harness.execute(R"JS(
var deferredDisabledUnchanged = save.matches(":disabled") &&
  save.matches("[disabled]") && disabledLive.length >= 1;
)JS");
    expect(afterDeferred.succeeded(),
        "deferred disabled write leaves pseudo state readable");
    expectBoolean(harness, "deferredDisabledUnchanged", true,
        "deferred disabled attribute write does not alter current state");
}

void testFocusOwnershipEventsAndCollections()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "focus");
    const ScriptResult setup = harness.execute(R"JS(
var first = document.getElementById("name");
var second = document.getElementById("name-two");
var third = document.getElementById("name-three");
var focusLive = document.querySelectorAll(":focus");
var blurSawOldOwner = false;
var secondSawNewOwner = false;
var secondFocusMetadata = false;
var thirdSawRedirectOwner = false;
var thirdSawRedirectMetadata = false;
var redirect = false;
first.addEventListener("blur", function (event) {
  blurSawOldOwner = first.matches(":focus") && event.target === first &&
    event.currentTarget === first && event.eventPhase === 2 &&
    event.relatedTarget === second && event.defaultPrevented === false;
});
second.addEventListener("focus", function (event) {
  secondSawNewOwner = second.matches(":focus");
  secondFocusMetadata = event.target === second && event.currentTarget === second &&
    event.eventPhase === 2 && event.relatedTarget === first &&
    event.defaultPrevented === false;
  if (redirect) third.focus();
});
third.addEventListener("focus", function (event) {
  thirdSawRedirectOwner = third.matches(":focus");
  thirdSawRedirectMetadata = event.target === third &&
    event.relatedTarget === second && event.eventPhase === 2;
});
var noFocusInitially = !first.matches(":focus") && focusLive.length === 0 &&
  document.activeElement === null;
first.focus();
var focusAcquiredSameTurn = first.matches(":focus") &&
  document.activeElement === first && focusLive.length === 1 && focusLive[0] === first;
first.focus();
var sameFocusNoDuplicate = focusLive.length === 1 && document.activeElement === first;
second.focus();
var focusTransferred = !first.matches(":focus") && second.matches(":focus") &&
  document.activeElement === second && focusLive.length === 1 && focusLive[0] === second;
redirect = true;
second.focus();
first.focus();
var redirectDeferred = !second.matches(":focus") && first.matches(":focus");
second.focus();
var redirectedFinalState = third.matches(":focus") && !second.matches(":focus") &&
  document.activeElement === third && focusLive.length === 1 && focusLive[0] === third;
third.blur();
var blurClearsAfterEvent = !third.matches(":focus") &&
  document.activeElement === null && focusLive.length === 0;
document.getElementById("fake-disabled").focus();
var nonFocusableNoOwner = focusLive.length === 0 &&
  !document.getElementById("fake-disabled").matches(":focus");
)JS");
    expect(setup.succeeded(), "focus transitions, redirection, and event checks execute");
    const char* names[] = {
        "noFocusInitially", "focusAcquiredSameTurn", "sameFocusNoDuplicate",
        "focusTransferred", "redirectDeferred", "redirectedFinalState",
        "blurClearsAfterEvent", "nonFocusableNoOwner", "blurSawOldOwner",
        "secondSawNewOwner", "secondFocusMetadata",
        "thirdSawRedirectOwner", "thirdSawRedirectMetadata",
    };
    for (const char* name : names)
        expectBoolean(harness, name, true, std::string("focus: ") + name);
}

void testEventsPurityStressAndStaleGeneration()
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness harness;
    loadFixture(harness, error, "events/stress");
    const ScriptResult eventSetup = harness.execute(R"JS(
var panel = document.getElementById("panel");
var run = document.getElementById("run");
var agree = document.getElementById("agree");
var delegatedPseudo = false;
var nestedPseudo = false;
var eventMetadataStable = false;
var outerTarget = null;
panel.addEventListener("click", function (event) {
  if (event.target !== run) return;
  delegatedPseudo = event.target.matches("button.action") &&
    event.target.matches(":disabled") === false;
  outerTarget = event.target;
  agree.click();
  nestedPseudo = agree.matches(":checked") === agree.checked &&
    document.querySelectorAll(":checked, :focus").length >= 1;
  eventMetadataStable = event.target === run && outerTarget === run &&
    event.currentTarget === panel && event.eventPhase === 3 &&
    event.relatedTarget === null && event.defaultPrevented === false;
});
var noSelectorSideEffectsEvents = 0;
agree.addEventListener("change", function () { noSelectorSideEffectsEvents++; });
)JS");
    expect(eventSetup.succeeded(), "delegated nested event setup succeeds");
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(serialById(harness, "run"), clickError),
        "authentic click dispatch completes");
    expect(clickError == RuntimeErrorCode::None,
        "authentic click dispatch has no runtime error");
    for (const char* name : {"delegatedPseudo", "nestedPseudo",
            "eventMetadataStable"})
        expectBoolean(harness, name, true, std::string("event: ") + name);

    const std::uint64_t generation = harness.hostAdapter().generation();
    const std::size_t mutationCount = harness.document().scriptMutationCount;
    const gxos::web::FormRuntimeControlState* checkedStateBefore =
        runtimeStateBySerial(harness.document(), serialById(harness, "agree"));
    const bool checkedBefore = checkedStateBefore != nullptr &&
        checkedStateBefore->checked;
    const ScriptResult pure = harness.execute(R"JS(
var purity = true;
for (var p = 0; p < 100; p = p + 1) {
  if (agree.matches(":checked") !== agree.checked ||
      document.querySelectorAll(":checked")[0] !==
        document.getElementById("radio-a") ||
      run.matches(":checked") || !document.getElementById("save").matches(":disabled"))
    purity = false;
}
var noEventsFromMatching = noSelectorSideEffectsEvents === 1;
)JS");
    expect(pure.succeeded(), "pseudo purity reads execute");
    expectBoolean(harness, "purity", true,
        "repeated selector evaluation does not change checked/focus/disabled state");
    expectBoolean(harness, "noEventsFromMatching", true,
        "selector evaluation dispatches no form events");
    expect(harness.hostAdapter().generation() == generation,
        "selector matching preserves host document generation");
    expect(harness.document().scriptMutationCount == mutationCount,
        "selector matching does not increment document mutation count");
    const gxos::web::FormRuntimeControlState* checkedStateAfter =
        runtimeStateBySerial(harness.document(), serialById(harness, "agree"));
    expect(checkedStateAfter != nullptr && checkedStateAfter->checked == checkedBefore,
        "selector matching leaves authoritative runtime checked state untouched");

    RuntimeErrorCode stressError = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness stress;
    loadFixture(stress, stressError, "stress");
    const ScriptResult stressResult = stress.execute(R"JS(
var stressBox = document.getElementById("scripted");
var stressDisabled = document.getElementById("save");
var stressCollection = document.querySelectorAll(":checked, :focus");
var stressStates = true;
for (var i = 0; i < 1000; i = i + 1) {
  stressBox.checked = !stressBox.checked;
  if (stressBox.matches(":checked") !== stressBox.checked) stressStates = false;
  if (!stressDisabled.matches(":disabled")) stressStates = false;
}
var stressReads = true;
for (var j = 0; j < 300; j = j + 1) {
  if (stressCollection.length !== 3 || stressCollection[0] !==
      document.getElementById("agree") || stressDisabled.matches(":disabled") !== true)
    stressReads = false;
}
var stressFinal = !stressBox.checked && !stressBox.matches(":checked");
)JS");
    expect(stressResult.succeeded(), "bounded state stress completes");
    expectBoolean(stress, "stressStates", true,
        "1,000 checked writes and disabled reads have no stale state");
    expectBoolean(stress, "stressReads", true,
        "300 held selector collection reads remain current and bounded");
    expectBoolean(stress, "stressFinal", true,
        "even toggle count leaves the final current checked state false");

    RuntimeErrorCode staleError = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness stale;
    loadFixture(stale, staleError, "stale");
    const std::uint64_t oldCheckedSerial = serialById(stale, "agree");
    const std::uint64_t oldFocusSerial = serialById(stale, "name");
    const ScriptResult capture = stale.execute(R"JS(
var oldCheckedElement = document.getElementById("agree");
var oldFocusElement = document.getElementById("name");
var oldDisabledElement = document.getElementById("save");
var oldStateCollection = document.querySelectorAll(":checked, :focus, :disabled");
oldFocusElement.focus();
)JS");
    expect(capture.succeeded(), "stale-state handles and collection are captured");
    expect(stale.focusedElementSerial() == oldFocusSerial,
        "stale test starts with authoritative focus active");
    expect(stale.invalidateDocumentGeneration(staleError),
        "stale-state test advances host generation");
    const ScriptResult staleReads = stale.execute(R"JS(
var stalePseudoReadsFailClosed = !oldCheckedElement.matches(":checked") &&
  !oldFocusElement.matches(":focus") && !oldDisabledElement.matches(":disabled") &&
  oldFocusElement.closest(":focus") === null;
)JS");
    expect(staleReads.succeeded(), "stale pseudo Element calls fail closed");
    expectBoolean(stale, "stalePseudoReadsFailClosed", true,
        "stale handles cannot match current checked/focused/disabled state");
    expect(!stale.execute("var stalePseudoCollectionLength = oldStateCollection.length;").succeeded(),
        "stale state-pseudo collection cannot read the replacement generation");

    stale.document() = gxos::web::parseHtml("file:///js52-replacement.html",
        fixture);
    initializeReplacementRuntime(stale.document(), oldFocusSerial);
    RuntimeErrorCode installError = RuntimeErrorCode::None;
    expect(stale.runtime().installHostGlobal("freshCheckedElement",
            oldCheckedSerial, gxos::javascript::kNavigatorElementHostKind,
            installError), "replacement checked Element uses reused serial");
    const ScriptResult reusedSerial = stale.execute(R"JS(
var replacementChecked = freshCheckedElement.matches(":checked");
var oldCheckedSafe = !oldCheckedElement.matches(":checked");
)JS");
    if (!reusedSerial.succeeded())
        std::cerr << "INFO: serial reuse runtime error="
            << gxos::javascript::runtimeErrorCodeName(reusedSerial.runtimeError.code)
            << " at=" << reusedSerial.runtimeError.location.line << ":"
            << reusedSerial.runtimeError.location.column << "\n";
    expect(reusedSerial.succeeded(),
        "reused checked serial is isolated across document generations");
    expectBoolean(stale, "replacementChecked", true,
        "new generation observes current checked state at reused serial");
    expectBoolean(stale, "oldCheckedSafe", true,
        "old generation cannot see checked state at reused serial");
    expect(!stale.execute(
        "var reusedStateCollectionLength = oldStateCollection.length;").succeeded(),
        "old live state collection cannot read replacement document after serial reuse");

    RuntimeErrorCode focusReuseError = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness focusReuse;
    loadFixture(focusReuse, focusReuseError, "focus serial reuse");
    const std::uint64_t reusedFocusSerial = serialById(focusReuse, "name");
    expect(focusReuse.execute(
        "var oldFocusReuse = document.getElementById('name'); oldFocusReuse.focus();")
            .succeeded(), "serial reuse focus handle acquires authoritative focus");
    expect(focusReuse.invalidateDocumentGeneration(focusReuseError),
        "serial reuse focus advances generation");
    focusReuse.document() = gxos::web::parseHtml(
        "file:///js52-focus-replacement.html", fixture);
    initializeReplacementRuntime(focusReuse.document(), reusedFocusSerial);
    expect(focusReuse.runtime().installHostGlobal("freshFocusReuse",
            reusedFocusSerial, gxos::javascript::kNavigatorElementHostKind,
            focusReuseError), "replacement focused Element reuses its serial");
    const ScriptResult focusReuseResult = focusReuse.execute(R"JS(
var newFocusReuseMatches = freshFocusReuse.matches(":focus");
var oldFocusReuseSafe = !oldFocusReuse.matches(":focus");
)JS");
    expect(focusReuseResult.succeeded(),
        "reused focused serial remains generation isolated");
    expectBoolean(focusReuse, "newFocusReuseMatches", true,
        "replacement generation sees its current focused serial");
    expectBoolean(focusReuse, "oldFocusReuseSafe", true,
        "old focused handle cannot match replacement focus");

    RuntimeErrorCode disabledReuseError = RuntimeErrorCode::None;
    NavigatorScriptExecutionHarness disabledReuse;
    loadFixture(disabledReuse, disabledReuseError, "disabled serial reuse");
    const std::uint64_t reusedDisabledSerial = serialById(disabledReuse, "save");
    expect(disabledReuse.execute(
        "var oldDisabledReuse = document.getElementById('save');")
            .succeeded(), "serial reuse disabled handle is captured");
    expect(disabledReuse.invalidateDocumentGeneration(disabledReuseError),
        "serial reuse disabled state advances generation");
    disabledReuse.document() = gxos::web::parseHtml(
        "file:///js52-disabled-replacement.html", fixture);
    initializeReplacementRuntime(disabledReuse.document(), 0u);
    expect(disabledReuse.runtime().installHostGlobal("freshDisabledReuse",
            reusedDisabledSerial, gxos::javascript::kNavigatorElementHostKind,
            disabledReuseError), "replacement disabled Element reuses its serial");
    const ScriptResult disabledReuseResult = disabledReuse.execute(R"JS(
var newDisabledReuseMatches = freshDisabledReuse.matches(":disabled");
var oldDisabledReuseSafe = !oldDisabledReuse.matches(":disabled");
)JS");
    expect(disabledReuseResult.succeeded(),
        "reused disabled serial remains generation isolated");
    expectBoolean(disabledReuse, "newDisabledReuseMatches", true,
        "replacement generation sees its current disabled state");
    expectBoolean(disabledReuse, "oldDisabledReuseSafe", true,
        "old disabled handle cannot match replacement disabled state");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js52.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testParserAndSharedSelectorComposition();
    testCheckedStateOptionsQueriesAndLiveCollections();
    testDisabledStateAndDeferredMutation();
    testFocusOwnershipEventsAndCollections();
    testEventsPurityStressAndStaleGeneration();
    if (failures != 0) {
        std::cerr << failures << " JS52 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS52 checks: " << checks << "/"
        << checks << " passed; simple="
        << sizeof(gxos::javascript::NavigatorScriptSimpleSelectorDescriptor)
        << " bytes complete="
        << sizeof(gxos::javascript::NavigatorScriptSelectorDescriptor)
        << " bytes collection="
        << ((16u + sizeof(gxos::javascript::NavigatorScriptSelectorDescriptor) +
            7u) / 8u * 8u)
        << " bytes (fixed aligned record estimate)\n";
    return 0;
}
