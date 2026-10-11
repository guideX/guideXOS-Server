#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace gxos::javascript {

struct NavigatorScriptScopeSelectorTestAccess {
    static constexpr std::size_t collectionRecordBytes()
    {
        return sizeof(NavigatorScriptHostAdapter::SelectorCollectionRecord);
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
    for (const char* name : {"documentRoot", "rootPseudo", "body", "root",
            "row", "directChild", "descendant", "strictSelf", "listOrder",
            "collectionIsolation", "functional", "stableClosest", "eventScope",
            "nestedScope", "eventMetadata", "newRoot", "replacementResult",
            "all", "directOnly", "docIdentity", "bodyIdentity", "selfQuery",
            "selfList", "directFirst", "directCount", "descendantFirst",
            "descendantCount", "warningFound", "newIdentity", "newRootPseudo",
            "newDirect", "newSelf", "ok"}) {
        const Value* debug = harness.runtime().lookup(SourceView(name,
            std::char_traits<char>::length(name)));
        if (debug == nullptr) continue;
        std::cerr << "  " << name << "=";
        if (debug->isBoolean()) std::cerr << (debug->booleanValue() ? "true" : "false");
        else if (debug->isNumber()) std::cerr << debug->numberValue();
        else std::cerr << valueTypeName(debug->type());
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

} // namespace

int main()
{
    static_assert(sizeof(NavigatorScriptSimpleSelectorCoreDescriptor) == 32u,
        "JS66 does not grow the shared simple-selector core");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor) == 68u,
        "JS66 does not grow the simple-selector descriptor");
    static_assert(sizeof(NavigatorScriptCompactRelativeSelectorCoreDescriptor) ==
        18u, "JS66 preserves compact relative cores");
    static_assert(sizeof(NavigatorScriptHasExpressionDescriptor) == 76u,
        "JS66 preserves JS65 expression metadata");
    static_assert(sizeof(NavigatorScriptSelectorMatchContext) == 16u,
        "JS66 keeps match context call-local and compact");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1856u,
        "JS66 reuses the JS65 selector descriptor");
    static_assert(sizeof(gxos::web::HtmlElementRef) == 440u,
        "JS66 does not grow document structural records");
    static_assert(sizeof(gxos::web::HtmlElementContentMetadata) == 24u,
        "JS66 does not grow document content metadata records");
    static_assert(NavigatorScriptScopeSelectorTestAccess::
        collectionRecordBytes() == 1872u,
        "JS66 reuses the JS65 collection record");
    static_assert(NavigatorScriptScopeSelectorTestAccess::
        collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections ==
            239616u,
        "JS66 does not grow the 128-entry collection registry");

    const std::string html =
        "<html><body><main id='outer'>"
        "<section id='root' class='selected'>"
        "<div id='row' class='row'><span id='leaf' class='leaf'></span></div>"
        "<div id='nested'><div class='row' id='nested-row'></div></div>"
        "<i id='item' class='item'></i><i id='selected-item' class='selected-item'></i>"
        "<i id='ordinary' class='ordinary'></i>"
        "<div><i id='warning' class='warning'></i></div>"
        "<button id='button-a' type='button'></button>"
        "</section>"
        "<section id='root-b'><i id='target-b' class='target-b'></i>"
        "<button id='button-b' type='button'></button></section>"
        "</main></body></html>";
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js66.html", html, error) &&
        error == RuntimeErrorCode::None, "JS66 fixture loads");

    expectScript(harness, R"JS(
var documentRoot = document.querySelector(':scope');
var rootPseudo = document.querySelector(':root');
var body = document.querySelector(':scope > body');
var root = document.querySelector('#root');
var row = document.querySelector('#row');
var docIdentity = documentRoot === rootPseudo;
var bodyIdentity = body === document.querySelector('body');
var selfQuery = root.querySelector(':scope') === null;
var selfList = root.querySelectorAll(':scope').length === 0;
var directFirst = root.querySelector(':scope > .row') === row;
var directCount = root.querySelectorAll(':scope > .row').length === 1;
var descendantFirst = root.querySelector(':scope .row') === row;
var descendantCount = root.querySelectorAll(':scope .row').length === 2;
var warningFound = root.querySelector(':scope .warning') === document.querySelector('#warning');
var ok = docIdentity && bodyIdentity && selfQuery && selfList && directFirst &&
 directCount && descendantFirst && descendantCount && warningFound;
)JS", "document authority, strict query candidates, direct children, and descendants");

    expectScript(harness, R"JS(
var root = document.querySelector('#root');
var leaf = document.querySelector('#leaf');
var outer = document.querySelector('#outer');
var ok = root.matches(':scope') && root.matches('section:scope') &&
 root.matches('.selected:scope') && !root.matches('div:scope') &&
 root.matches(':not(:root)') && root.matches(':is(:scope)') &&
 root.matches(':where(:scope)') && !root.matches(':not(:scope)') &&
 !root.matches(':scope > .row') && root.matches('main > :scope') &&
 root.matches('main :scope') && root.closest(':scope') === root &&
 root.closest('section:scope') === root &&
 leaf.closest(':scope') === leaf && leaf.closest('div') === document.querySelector('#row') &&
 leaf.closest('div:scope') === null && outer.matches(':root') === false;
)JS", "matches, functional propagation, structural relations, and stable closest scope");

    expectScript(harness, R"JS(
var root = document.querySelector('#root');
var row = document.querySelector('#row');
var ordinary = document.querySelector('#ordinary');
var warning = document.querySelector('#warning');
var list = root.querySelectorAll(':scope > .row, .ordinary, :scope > .row, :scope > .row');
var reverseList = root.querySelectorAll('.warning, .row');
var a = root.querySelectorAll(':scope > .row');
var rootB = document.querySelector('#root-b');
var b = rootB.querySelectorAll(':scope > .target-b');
var ok = root.querySelector(':scope > .missing, :scope > .row') === row &&
 list.length === 2 && list[0] === row && list[1] === ordinary &&
 reverseList.length === 3 && reverseList[0] === row &&
 reverseList[1] === document.querySelector('#nested-row') &&
 reverseList[2] === warning && a.length === 1 && b.length === 1 &&
 b[0] === document.querySelector('#target-b') && a[0] === row &&
 root.querySelector('main .warning') === warning &&
 root.querySelector('.item + .selected-item') === document.querySelector('#selected-item');
)JS", "selector-list union order, deduplication, isolation, and existing outer relations");

    expectScript(harness, R"JS(
var documentRoot = document.querySelector(':scope');
var root = document.querySelector('#root');
var ok = document.querySelector(':SCOPE') === documentRoot &&
 document.querySelector(':sCoPe') === documentRoot &&
 root.matches(':scope') && root.matches('section:scope') &&
 root.matches('#root:scope') && root.matches('.selected:scope') &&
 root.matches('[id="root"]:scope') && root.matches('*:scope');
)JS", "pseudo-name case handling and supported compound forms");

    expectScript(harness, R"JS(
var invalid = [':scope()', ':scope(anything)', ':scope(', ':scope)',
 ':scope:', ':scope:focus'];
var rejected = true;
for (var i = 0; i < invalid.length; i++)
 if (document.querySelector(invalid[i]) !== null) rejected = false;
var ok = rejected;
)JS", "malformed syntax and the established one-pseudo bound");

    expectScript(harness, R"JS(
var root = document.querySelector('#root');
var targetA = document.querySelector('#row');
var targetB = document.querySelector('#target-b');
var buttonA = document.querySelector('#button-a');
var buttonB = document.querySelector('#button-b');
var eventScope = false;
var nestedScope = false;
var eventMetadata = false;
document.querySelector('#root').addEventListener('click', function(event) {
 var savedTarget = event.target;
 var savedCurrent = event.currentTarget;
 var savedPhase = event.eventPhase;
 var savedDefault = event.defaultPrevented;
 eventScope = root.querySelector(':scope > .row') === targetA;
 buttonB.click();
 eventScope = eventScope && root.querySelector(':scope > .row') === targetA;
 eventMetadata = event.target === savedTarget &&
  event.currentTarget === savedCurrent && event.eventPhase === savedPhase &&
  event.defaultPrevented === savedDefault && savedTarget === buttonA;
});
document.querySelector('#root-b').addEventListener('click', function(event) {
 nestedScope = document.querySelector('#root-b').querySelector(':scope > .target-b') === targetB &&
  event.target === buttonB && event.currentTarget === document.querySelector('#root-b') &&
  event.eventPhase === 3 && event.defaultPrevented === false;
});
var ok = buttonA === document.querySelector('#button-a');
)JS", "Event callback setup");
    const HostInstanceId buttonASerial = serialById(harness.document(), "button-a");
    bool defaultPrevented = true;
    expect(buttonASerial != 0u && harness.dispatchClick(buttonASerial, error,
        &defaultPrevented) && error == RuntimeErrorCode::None &&
        !defaultPrevented, "outer and nested Event dispatch complete");
    const Value* eventScope = harness.runtime().lookup(SourceView("eventScope", 10u));
    const Value* nestedScope = harness.runtime().lookup(SourceView("nestedScope", 11u));
    const Value* eventMetadata = harness.runtime().lookup(SourceView("eventMetadata", 13u));
    expect(eventScope != nullptr && eventScope->isBoolean() &&
        eventScope->booleanValue() && nestedScope != nullptr &&
        nestedScope->isBoolean() && nestedScope->booleanValue() &&
        eventMetadata != nullptr && eventMetadata->isBoolean() &&
        eventMetadata->booleanValue(),
        "nested query scope and outer Event metadata survive reentrancy");

    std::ifstream fixtureFile("navigator-smoke/javascript-js66.html",
        std::ios::binary);
    const std::string hostedHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = hostedHtml.find("<script>");
    const std::size_t scriptClose = hostedHtml.find("</script>", scriptOpen);
    NavigatorScriptExecutionHarness hostedFixtureHarness;
    error = RuntimeErrorCode::None;
    const bool hostedFixtureLoaded = !hostedHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        hostedFixtureHarness.loadHtml("file:///js66-hosted-fixture.html",
            hostedHtml, error) && error == RuntimeErrorCode::None;
    expect(hostedFixtureLoaded, "hosted JS66 fixture loads in native harness");
    if (hostedFixtureLoaded) {
        const std::size_t scriptBegin = scriptOpen + 8u;
        const std::string hostedProgram = hostedHtml.substr(scriptBegin,
            scriptClose - scriptBegin);
        const ScriptResult hostedProgramResult =
            hostedFixtureHarness.execute(hostedProgram);
        const Value* hostedFailures = hostedFixtureHarness.runtime().lookup(
            SourceView("failures", 8u));
        const Value* hostedAssertions = hostedFixtureHarness.runtime().lookup(
            SourceView("assertions", 10u));
        const Value* hostedFailedIndexes = hostedFixtureHarness.runtime().lookup(
            SourceView("failedIndexes", 13u));
        const Value* lastFailedIndex = hostedFixtureHarness.runtime().lookup(
            SourceView("lastFailedIndex", 15u));
        if (!hostedProgramResult.succeeded()) {
            std::cerr << "JS66 hosted fixture script parse/execution: status="
                << scriptStatusName(hostedProgramResult.status) << ", error="
                << runtimeErrorCodeName(hostedProgramResult.runtimeError.code)
                << ", line=" << hostedProgramResult.runtimeError.location.line
                << ", column=" << hostedProgramResult.runtimeError.location.column
                << '\n';
        }
        if (hostedFailures == nullptr || !hostedFailures->isNumber() ||
            hostedAssertions == nullptr || !hostedAssertions->isNumber() ||
            hostedFailures->numberValue() != 0.0 ||
            hostedAssertions->numberValue() != 26.0) {
            std::cerr << "JS66 hosted fixture initial counts: failures="
                << (hostedFailures != nullptr && hostedFailures->isNumber()
                    ? hostedFailures->numberValue() : -1.0)
                << ", assertions="
                << (hostedAssertions != nullptr && hostedAssertions->isNumber()
                    ? hostedAssertions->numberValue() : -1.0) << '\n';
            std::cerr << "JS66 hosted fixture last failed assertion: "
                << (lastFailedIndex != nullptr && lastFailedIndex->isNumber()
                    ? lastFailedIndex->numberValue() : -1.0) << '\n';
            if (hostedFailedIndexes != nullptr && hostedFailedIndexes->isString())
                std::cerr << "JS66 hosted fixture failed assertion indexes: "
                    << hostedFixtureHarness.runtime().stringValue(
                        *hostedFailedIndexes) << '\n';
        }
        expect(hostedProgramResult.succeeded() && hostedFailures != nullptr &&
            hostedFailures->isNumber() && hostedFailures->numberValue() == 0.0 &&
            hostedAssertions != nullptr && hostedAssertions->isNumber() &&
            hostedAssertions->numberValue() == 26.0,
            "hosted fixture syntax and all initial JS66 assertions pass natively");
        const HostInstanceId hostedButtonSerial =
            serialById(hostedFixtureHarness.document(), "event-button");
        expect(hostedButtonSerial != 0u &&
            hostedFixtureHarness.dispatchClick(hostedButtonSerial, error),
            "hosted fixture nested Event dispatch passes natively");
        const Value* hostedEventScope = hostedFixtureHarness.runtime().lookup(
            SourceView("eventScope", 10u));
        const Value* hostedNestedScope = hostedFixtureHarness.runtime().lookup(
            SourceView("nestedScope", 11u));
        const Value* hostedEventMetadata = hostedFixtureHarness.runtime().lookup(
            SourceView("eventMetadata", 13u));
        expect(hostedEventScope != nullptr && hostedEventScope->isBoolean() &&
            hostedEventScope->booleanValue() && hostedNestedScope != nullptr &&
            hostedNestedScope->isBoolean() && hostedNestedScope->booleanValue() &&
            hostedEventMetadata != nullptr && hostedEventMetadata->isBoolean() &&
            hostedEventMetadata->booleanValue(),
            "hosted fixture Event scope remains stable through nested dispatch");
    }

    NavigatorScriptExecutionHarness missingRootHarness;
    error = RuntimeErrorCode::None;
    expect(missingRootHarness.loadHtml("file:///js66-no-root.html",
        "<html><body><i id='other'></i></body></html>", error) &&
        error == RuntimeErrorCode::None,
        "missing-root fixture loads");
    missingRootHarness.document().hasDocumentElement = false;
    if (missingRootHarness.document().structuralElements.size() > 1u)
        missingRootHarness.document().structuralElements[1].parentSerial = 0u;
    expectScript(missingRootHarness,
        "var ok = document.querySelector(':scope') === null;",
        "document scope fails safely without authoritative documentElement");

    NavigatorScriptExecutionHarness invalidRootHarness;
    error = RuntimeErrorCode::None;
    expect(invalidRootHarness.loadHtml("file:///js66-invalid-root.html",
        "<html><body><i id='other'></i></body></html>", error) &&
        error == RuntimeErrorCode::None, "invalid-root fixture loads");
    invalidRootHarness.document().structuralElements[0].serial = 7u;
    expectScript(invalidRootHarness,
        "var ok = document.querySelector(':scope') === null;",
        "invalid canonical root record does not fall back to another parentless Element");

    NavigatorScriptExecutionHarness replacementHarness;
    error = RuntimeErrorCode::None;
    expect(replacementHarness.loadHtml("file:///js66-old.html",
        "<html><body><section id='old'><i class='row'></i></section></body></html>",
        error) && error == RuntimeErrorCode::None,
        "document replacement source loads");
    const HostGenerationId oldGeneration = replacementHarness.hostAdapter().generation();
    const HostObjectReference oldElement{serialById(replacementHarness.document(),
        "old"), oldGeneration, kNavigatorElementHostKind};
    const HostObjectReference oldCollection{1u, oldGeneration,
        kNavigatorSelectorCollectionHostKind};
    expect(replacementHarness.execute(
        "var oldRoot = document.querySelector('#old');"
        "var oldRows = oldRoot.querySelectorAll(':scope > .row');"
        "var ok = oldRoot.matches(':scope') && oldRows.length === 1;").succeeded(),
        "old generation establishes Element and retained collection");
    error = RuntimeErrorCode::None;
    expect(replacementHarness.replaceHtml("file:///js66-new.html",
        "<html><body><section id='new'><i class='row'></i></section></body></html>",
        error) && error == RuntimeErrorCode::None,
        "document replacement succeeds");
    const HostResult oldElementResult = replacementHarness.hostAdapter().validate(oldElement);
    const HostResult oldCollectionResult = replacementHarness.hostAdapter().validate(oldCollection);
    expect(oldElementResult.code == HostResultCode::StaleObject &&
        oldCollectionResult.code == HostResultCode::StaleObject &&
        serialById(replacementHarness.document(), "new") == oldElement.instanceId,
        "same serial in the replacement generation does not revive old scope handles");
    expectScript(replacementHarness, R"JS(
var root = document.querySelector(':scope');
var newRoot = document.querySelector('#new');
var newRootPseudo = root === document.querySelector(':root');
var newDirect = newRoot.querySelector(':scope > .row') === document.querySelector('.row');
var newSelf = newRoot.querySelector(':scope') === null;
var ok = newRootPseudo && newDirect && newSelf;
)JS", "replacement document uses only its current root and Element scope");

    NavigatorScriptExecutionHarness capacityHarness;
    std::string capacityHtml = "<html><body><section id='capacity'>"
        "<i class='row' id='direct'></i><div><i class='row' id='nested'></i></div>";
    for (std::size_t index = 0u; index < 1018u; ++index)
        capacityHtml += "<i></i>";
    capacityHtml += "</section></body></html>";
    error = RuntimeErrorCode::None;
    expect(capacityHarness.loadHtml("file:///js66-capacity.html",
        capacityHtml, error) && error == RuntimeErrorCode::None &&
        capacityHarness.document().structuralElements.size() ==
            kNavigatorScriptMaxDocumentNodes,
        "near-capacity scope fixture reaches 1,024 Elements");
    const auto start = std::chrono::steady_clock::now();
    expectScript(capacityHarness, R"JS(
var root = document.querySelector('#capacity');
var direct = document.querySelector('#direct');
var all = root.querySelectorAll(':scope .row');
var directOnly = root.querySelectorAll(':scope > .row');
var ok = all.length === 2 && directOnly.length === 1 && directOnly[0] === direct;
for (var i = 0; i < 100; i++)
 if (!root.matches(':scope') || root.closest(':scope') !== root ||
  root.querySelector(':scope > .row') !== direct) ok = false;
)JS", "near-capacity candidate ordering and repeated scope APIs");
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "JS66 bounded structure bytes: match-context="
        << sizeof(NavigatorScriptSelectorMatchContext)
        << ", selector-descriptor=" << sizeof(NavigatorScriptSelectorDescriptor)
        << ", collection-record="
        << NavigatorScriptScopeSelectorTestAccess::collectionRecordBytes()
        << ", registry=" << NavigatorScriptScopeSelectorTestAccess::
            collectionRecordBytes() * kNavigatorScriptMaxSelectorCollections
        << ", HtmlElementRef=" << sizeof(gxos::web::HtmlElementRef)
        << ", HtmlElementContentMetadata="
        << sizeof(gxos::web::HtmlElementContentMetadata) << "\n";
    std::cout << "JS66 1,024-node repeated scope API workload=" << elapsed
        << "us\n";

    std::cout << "Navigator JavaScript JS66 checks: " << checks - failures
        << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
