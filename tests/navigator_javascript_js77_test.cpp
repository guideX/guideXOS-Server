#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if defined(GXOS_JS77_STACK_INSTRUMENTED) && defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <limits>

namespace {
thread_local bool g_js77StackSampleActive = false;
thread_local std::uintptr_t g_js77LowestStackAddress =
    (std::numeric_limits<std::uintptr_t>::max)();
} // namespace

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void*, void*)
{
    if (!g_js77StackSampleActive) return;
    volatile std::uint8_t marker = 0u;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&marker);
    if (address < g_js77LowestStackAddress)
        g_js77LowestStackAddress = address;
}

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void*, void*)
{
}
#endif

namespace gxos::javascript {

struct NavigatorScriptRelativeSelectorTestAccess {
    using Collection = NavigatorScriptHostAdapter::SelectorCollectionRecord;

    static constexpr std::size_t collectionBytes()
    {
        return sizeof(Collection);
    }

    static std::size_t activeCollectionCount(
        const NavigatorScriptHostAdapter& adapter)
    {
        std::size_t count = 0u;
        for (const Collection& record : adapter.selectorCollections_)
            if (record.active) ++count;
        return count;
    }
};

} // namespace gxos::javascript

namespace {

using namespace gxos::javascript;

std::size_t checks = 0u;
std::size_t failures = 0u;

void expect(bool condition, const std::string& label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "  FAIL: " << label << '\n';
}

const Value* binding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    return harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
}

bool booleanIs(const NavigatorScriptExecutionHarness& harness,
    const char* name, bool expected)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isBoolean() &&
        value->booleanValue() == expected;
}

double numberIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isNumber() ? value->numberValue() : -1.0;
}

std::string stringIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isString()
        ? harness.runtime().stringValue(*value) : std::string();
}

bool loadHtml(NavigatorScriptExecutionHarness& harness,
    const std::string& html, const std::string& label)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js77-native-fixture.html",
        html, error) && error == RuntimeErrorCode::None;
    expect(loaded, label);
    return loaded;
}

struct SomeLookupCounter {
    std::size_t count = 0u;
    std::size_t lastIndex = 0u;
};

void countSomeLookup(void* context, std::size_t requestedIndex)
{
    auto* counter = static_cast<SomeLookupCounter*>(context);
    ++counter->count;
    counter->lastIndex = requestedIndex;
}

struct SomeGenerationProbe {
    NavigatorScriptExecutionHarness* harness = nullptr;
    bool changed = false;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    HostGenerationId initialGeneration = 0u;
    HostInstanceId initialSerial = 0u;
    HostInstanceId replacementSerial = 0u;
};

void replaceGenerationAfterSomeCallback(void* context,
    std::size_t completedIndex)
{
    auto* probe = static_cast<SomeGenerationProbe*>(context);
    if (probe == nullptr || probe->harness == nullptr || probe->changed ||
        completedIndex != 0u) return;
    probe->changed = probe->harness->invalidateDocumentGeneration(
        probe->error);
    if (!probe->changed) return;
    probe->harness->document() = gxos::web::parseHtml(
        "file:///js77-generation-replacement.html",
        "<html><body><i id='same' class='row'></i><i class='row'></i>"
        "</body></html>");
    probe->harness->hostAdapter().attachDocument(
        probe->harness->document(), probe->harness->runtime().hostGeneration());
    for (const auto& element : probe->harness->document().structuralElements)
        if (element.id == "same") probe->replacementSerial = element.serial;
}

void testHostedFixture()
{
    std::ifstream fixtureFile("navigator-smoke/javascript-js77.html",
        std::ios::binary);
    const std::string fixtureHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = fixtureHtml.find("<script>");
    const std::size_t scriptClose = fixtureHtml.find("</script>", scriptOpen);
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = !fixtureHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        harness.loadHtml("file:///js77-hosted-fixture.html", fixtureHtml,
            error) && error == RuntimeErrorCode::None;
    expect(loaded, "JS77 hosted fixture loads in the native harness");
    if (!loaded) return;

    const std::string source = fixtureHtml.substr(scriptOpen + 8u,
        scriptClose - scriptOpen - 8u);
    expect(source.find('<') == std::string::npos,
        "JS77 inline source preserves the hosted HTML-subset integrity guard");
    expect(harness.document().scriptSources.size() == 1u &&
        harness.document().scriptSources[0] == source,
        "JS77 inline script is captured byte-for-byte");
    if (harness.document().scriptSources.size() != 1u ||
        harness.document().scriptSources[0] != source) return;

    const ScriptResult result = harness.execute(source);
    if (!result.succeeded() || numberIs(harness, "failures") != 0.0) {
        std::cerr << "  JS77 fixture status=" << scriptStatusName(result.status)
            << " runtime=" << runtimeErrorCodeName(result.runtimeError.code)
            << " parser=" << parserErrorCodeName(result.parserError.code)
            << '@' << result.parserError.location.line << ':'
            << result.parserError.location.column << " assertions="
            << numberIs(harness, "assertions") << " failures="
            << numberIs(harness, "failures") << " failureAt="
            << numberIs(harness, "failureAt") << " marker="
            << stringIs(harness, "initialMarker") << '\n';
    }
    expect(result.succeeded() && numberIs(harness, "assertions") >= 16.0 &&
        numberIs(harness, "failures") == 0.0,
        "JS77 fixture verifies Boolean results, arguments, thisArg, short-circuit, adapters, and mutation");
    expect(stringIs(harness, "initialMarker").find(
            "JS77:initial:assertions=") == 0u &&
        stringIs(harness, "initialMarker").find(":failures=0") !=
            std::string::npos,
        "JS77 fixture publishes an explicit passing initial marker");

    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    error = RuntimeErrorCode::None;
    const bool dispatched = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, error) &&
        error == RuntimeErrorCode::None;
    expect(dispatched && booleanIs(harness, "outerResult", true) &&
        booleanIs(harness, "middleResult", true) &&
        booleanIs(harness, "innerResult", true) &&
        booleanIs(harness, "metadataResult", true) &&
        stringIs(harness, "eventMarker").find("JS77:event=outer:true") == 0u,
        "JS77 nested Event some/forEach dispatch preserves results and metadata");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS77 callback and nested Event frames unwind to zero");
}

void testTruthinessAndCallbackArguments()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='a' class='row'></i></body></html>",
            "JS77 truthiness fixture loads")) return;
    const ScriptResult result = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "function truthyFunction(){}"
        "var falseResult=rows.some(function(){return false;});"
        "var trueResult=rows.some(function(){return true;});"
        "var zeroResult=rows.some(function(){return 0;});"
        "var negativeZeroResult=rows.some(function(){return -0;});"
        "var nonzeroResult=rows.some(function(){return 4;});"
        "var nanResult=rows.some(function(){return 0/0;});"
        "var emptyStringResult=rows.some(function(){return '';});"
        "var stringResult=rows.some(function(){return 'found';});"
        "var nullResult=rows.some(function(){return null;});"
        "var undefinedResult=rows.some(function(){return undefined;});"
        "var objectResult=rows.some(function(){return {};});"
        "var functionResult=rows.some(function(){return truthyFunction;});"
        "var hostObjectResult=rows.some(function(element){return element;});"
        "var receiver={tag:'receiver'};var receiverCalls=0;"
        "var receiverResult=rows.some(function(element,index,collection){"
        "receiverCalls++;return this.tag==='receiver'&&element===rows[0]&&"
        "index===0&&collection===rows;},receiver);"
        "var omittedReceiverResult=rows.some(function(){"
        "return this===undefined;});"
        "var emptyCalls=0;var empty=document.querySelectorAll('.absent');"
        "var emptyResult=empty.some(function(){emptyCalls++;return true;});"
        "var methodStable=rows.some===rows.some;var savedSome=rows.some;"
        "var allBooleans=falseResult===false&&trueResult===true&&"
        "zeroResult===false&&negativeZeroResult===false&&"
        "nonzeroResult===true&&nanResult===false&&"
        "emptyStringResult===false&&stringResult===true&&"
        "nullResult===false&&undefinedResult===false&&objectResult===true&&"
        "functionResult===true&&hostObjectResult===true;"
        "var argumentsOk=receiverResult===true&&receiverCalls===1&&"
        "omittedReceiverResult===true&&emptyResult===false&&emptyCalls===0&&"
        "methodStable&&savedSome===rows.some;");
    if (!result.succeeded()) {
        std::cerr << "  JS77 truthiness status=" << scriptStatusName(result.status)
            << " runtime=" << runtimeErrorCodeName(result.runtimeError.code)
            << " parser=" << parserErrorCodeName(result.parserError.code)
            << '@' << result.parserError.location.line << ':'
            << result.parserError.location.column << '\n';
    }
    expect(result.succeeded(), "JS77 truthiness and callback argument script runs");
    expect(booleanIs(harness, "allBooleans", true),
        "JS77 uses JavaScript truthiness for Boolean, number, NaN, string, null, undefined, object, function, and host values");
    expect(booleanIs(harness, "argumentsOk", true),
        "JS77 forwards canonical Element/index/collection, optional thisArg, empty behavior, and callable method references");
    if (!booleanIs(harness, "allBooleans", true) ||
        !booleanIs(harness, "argumentsOk", true)) {
        for (const char* name : {"falseResult", "trueResult", "zeroResult",
                "negativeZeroResult", "nonzeroResult", "nanResult",
                "emptyStringResult", "stringResult", "nullResult",
                "undefinedResult", "objectResult", "functionResult",
                "hostObjectResult", "receiverResult", "omittedReceiverResult",
                "emptyResult", "allBooleans", "argumentsOk"}) {
            const Value* value = binding(harness, name);
            std::cerr << "  JS77 value " << name << '='
                << (value == nullptr ? "missing" : valueTypeName(value->type()));
            if (value != nullptr && value->isBoolean())
                std::cerr << ':' << value->booleanValue();
            std::cerr << '\n';
        }
    }
}

void testInvalidCallbacksAndCallbackErrors()
{
    const std::pair<const char*, const char*> invalidCalls[] = {
        {"rows.some()", "missing callback"},
        {"rows.some(null)", "null callback"},
        {"rows.some(123)", "numeric callback"},
        {"rows.some('invalid')", "string callback"},
    };
    for (const auto& invalid : invalidCalls) {
        NavigatorScriptExecutionHarness harness;
        if (!loadHtml(harness,
                "<html><body><i class='row'></i></body></html>",
                std::string("JS77 ") + invalid.second + " fixture loads"))
            continue;
        SomeLookupCounter lookups;
        harness.hostAdapter().setSelectorCollectionSomeLookupProbe(
            countSomeLookup, &lookups);
        const ScriptResult result = harness.execute(
            std::string("var rows=document.querySelectorAll('.row');") +
            invalid.first + ";");
        harness.hostAdapter().setSelectorCollectionSomeLookupProbe(nullptr,
            nullptr);
        expect(!result.succeeded() &&
            result.status == ScriptStatus::RuntimeFailure &&
            result.runtimeError.code == RuntimeErrorCode::TypeError,
            std::string("JS77 ") + invalid.second + " reports TypeError");
        expect(lookups.count == 0u &&
            harness.runtime().activeCallFrameCount() == 0u,
            std::string("JS77 ") + invalid.second +
                " does not traverse or leak an active callback frame");
        const ScriptResult recovered = harness.execute(
            "var recovered=rows.some(function(){return true;});");
        expect(recovered.succeeded() && booleanIs(harness, "recovered", true),
            std::string("JS77 ") + invalid.second +
                " leaves the evaluator usable for a later callback");
    }

    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i class='row'></i><i class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS77 callback-error fixture loads")) return;
    SomeLookupCounter lookups;
    harness.hostAdapter().setSelectorCollectionSomeLookupProbe(
        countSomeLookup, &lookups);
    const ScriptResult failed = harness.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "rows.some(function(element,index){calls++;if(index===0)return false;"
        "notDefined();return true;});");
    harness.hostAdapter().setSelectorCollectionSomeLookupProbe(nullptr, nullptr);
    expect(!failed.succeeded() && failed.runtimeError.code ==
            RuntimeErrorCode::UnknownIdentifier &&
        numberIs(harness, "calls") == 2.0 && lookups.count == 2u &&
        lookups.lastIndex == 1u,
        "JS77 propagates callback errors after prior false values and stops before later indices");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS77 callback error unwinds all evaluator frames");
}

void testMutationAndShortCircuit()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='a' class='row'></i>"
            "<i id='b' class='row'></i><i id='x' class='candidate'></i>"
            "<i id='y' class='candidate'></i><i id='s0' class='shrink'></i>"
            "<i id='s1' class='shrink'></i><i id='s2' class='shrink'></i>"
            "</body></html>",
            "JS77 live-mutation fixture loads")) return;
    const ScriptResult result = harness.execute(
        "var rows=document.querySelectorAll('.row');var visited='';"
        "var indices='';var calls=0;var found=rows.some(function(element,index){"
        "calls++;visited=visited+element.id;indices=indices+index;"
        "if(index===0){element.removeAttribute('class');"
        "document.querySelectorAll('#x').item(0).setAttribute('class','row');}"
        "return element.id==='x';});"
        "var shrink=document.querySelectorAll('.shrink');var shrinkVisited='';"
        "var shrinkIndices='';var shrinkCalls=0;"
        "var shrinkFound=shrink.some(function(element,index){"
        "shrinkCalls++;shrinkVisited=shrinkVisited+element.id;"
        "shrinkIndices=shrinkIndices+index;"
        "if(index===0){element.removeAttribute('class');"
        "document.querySelectorAll('#s1').item(0).removeAttribute('class');"
        "document.querySelectorAll('#s2').item(0).removeAttribute('class');}"
        "return false;});"
        "var ok=found===true&&visited==='ax'&&indices==='01'&&calls===2&&"
        "shrinkFound===false&&shrinkVisited==='s0'&&shrinkIndices==='0'&&"
        "shrinkCalls===1&&shrink.length===0;");
    expect(result.succeeded() && booleanIs(harness, "ok", true),
        "JS77 observes live index membership after mutation and safely skips shrunken indices");

    NavigatorScriptExecutionHarness growth;
    if (!loadHtml(growth,
            "<html><body><i id='g0' class='grow'></i>"
            "<i id='g1' class='grow'></i><i id='g2' class='candidate'></i>"
            "<i id='g3' class='candidate'></i></body></html>",
            "JS77 bounded-growth fixture loads")) return;
    const ScriptResult grown = growth.execute(
        "var rows=document.querySelectorAll('.grow');var visited='';"
        "var indices='';var calls=0;var found=rows.some(function(element,index){"
        "calls++;visited=visited+element.id;indices=indices+index;"
        "if(index===0)document.querySelectorAll('#g2').item(0).setAttribute('class','grow');"
        "if(index===1)document.querySelectorAll('#g3').item(0).setAttribute('class','grow');"
        "return false;});var ok=found===false&&calls===2&&visited==='g0g1'&&"
        "indices==='01'&&rows.length===4;");
    expect(grown.succeeded() && booleanIs(growth, "ok", true),
        "JS77 growth changes live membership without extending the entry bound");
}

std::string capacityHtml()
{
    const std::string thresholdClasses = "s16 s100 s128 s512 s1024";
    std::string html = "<html class='all s1 " + thresholdClasses +
        "'><body class='all " + thresholdClasses + "'><div class='all " +
        thresholdClasses + "'>";
    for (std::size_t index = 0u; index < 1021u; ++index) {
        std::string classes = "all";
        if (index < 13u) classes += " s16";
        if (index < 97u) classes += " s100";
        if (index < 125u) classes += " s128";
        if (index < 509u) classes += " s512";
        classes += " s1024";
        html += "<i class='" + classes + "'></i>";
    }
    html += "</div></body></html>";
    return html;
}

struct CapacityCase {
    const char* selector;
    std::size_t size;
};

void runCapacityCase(const std::string& html, const CapacityCase& sizeCase,
    std::size_t target, bool shouldFind)
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, html,
            std::string("JS77 capacity ") + sizeCase.selector +
                " fixture loads")) return;
    SomeLookupCounter lookups;
    harness.hostAdapter().setSelectorCollectionSomeLookupProbe(
        countSomeLookup, &lookups);
    const std::string source = std::string("var rows=document.querySelectorAll('") +
        sizeCase.selector + "');var calls=0;var found=rows.some(function(element,index){"
        "calls++;return index===" + std::to_string(target) +
        ";});var ok=found===" + (shouldFind ? "true" : "false") +
        "&&calls===" + std::to_string(shouldFind ? target + 1u : sizeCase.size) + ";";
    const ScriptResult result = harness.execute(source);
    harness.hostAdapter().setSelectorCollectionSomeLookupProbe(nullptr, nullptr);
    const std::size_t expectedCount = shouldFind ? target + 1u : sizeCase.size;
    std::cout << "JS77 short-circuit selector=" << sizeCase.selector
        << " target=" << target << " expected_result="
        << (shouldFind ? "true" : "false") << " callbacks="
        << numberIs(harness, "calls") << " indexed_lookups="
        << lookups.count << '\n';
    expect(result.succeeded() && booleanIs(harness, "found", shouldFind) &&
        booleanIs(harness, "ok", true) && lookups.count == expectedCount &&
        (expectedCount == 0u || lookups.lastIndex == expectedCount - 1u),
        std::string("JS77 short-circuit count selector=") + sizeCase.selector +
            " target=" + std::to_string(target) + " callbacks=" +
            std::to_string(numberIs(harness, "calls")) + " lookups=" +
            std::to_string(lookups.count));
}

void testCapacityShortCircuitAndPools()
{
    const std::string html = capacityHtml();
    const CapacityCase sizes[] = {
        {".absent", 0u}, {".s1", 1u}, {".s16", 16u},
        {".s100", 100u},
        {".s128", 128u}, {".s512", 512u}, {".s1024", 1024u},
    };
    for (const CapacityCase& sizeCase : sizes) {
        if (sizeCase.size == 0u) {
            runCapacityCase(html, sizeCase, 0u, false);
            continue;
        }
        runCapacityCase(html, sizeCase, 0u, true);
        runCapacityCase(html, sizeCase, sizeCase.size / 2u, true);
        runCapacityCase(html, sizeCase, sizeCase.size - 1u, true);
        runCapacityCase(html, sizeCase, sizeCase.size, false);
    }
    runCapacityCase(html, CapacityCase{".s100", 100u}, 3u, true);

    NavigatorScriptExecutionHarness full;
    if (!loadHtml(full, html,
            "JS77 full-capacity all-false fixture loads")) return;
    if (full.document().structuralElements.size() != 1024u) {
        expect(false, "JS77 full-capacity fixture has exactly 1,024 Elements");
        return;
    }
    SomeLookupCounter lookups;
    full.hostAdapter().setSelectorCollectionSomeLookupProbe(
        countSomeLookup, &lookups);
    const ScriptResult result = full.execute(
        "var rows=document.querySelectorAll('.all');var calls=0;"
        "var found=rows.some(function(){calls++;return false;});"
        "var ok=rows.length===1024&&found===false&&calls===1024;");
    full.hostAdapter().setSelectorCollectionSomeLookupProbe(nullptr, nullptr);
    const std::size_t envs = full.runtime().environmentCount();
    const std::size_t hosts = full.runtime().hostObjectCount();
    expect(result.succeeded() && booleanIs(full, "ok", true) &&
        lookups.count == 1024u && envs <= kDefaultMaxJavaScriptEnvironments &&
        hosts <= kDefaultMaxJavaScriptHostObjects,
        "JS77 completes 1,024 all-false callbacks within existing runtime pools");
    std::cout << "JS77 full-capacity pools: environments=" << envs << '/'
        << kDefaultMaxJavaScriptEnvironments << " hostObjects=" << hosts << '/'
        << kDefaultMaxJavaScriptHostObjects << " callbacks=1024 lookups="
        << lookups.count << '\n';
    expect(sizeof(NavigatorScriptSelectorDescriptor) == 1918u &&
        NavigatorScriptRelativeSelectorTestAccess::collectionBytes() == 1936u &&
        NavigatorScriptRelativeSelectorTestAccess::collectionBytes() *
            kNavigatorScriptMaxSelectorCollections == 247808u &&
        kNavigatorScriptMaxSelectorCollections == 128u &&
        kNavigatorScriptMaxDocumentNodes == 1024u,
        "JS77 preserves descriptor, collection, registry, and structural capacities");
}

void testCollectionRegistryReuse()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i class='row'></i></body></html>",
            "JS77 collection-registry fixture loads")) return;
    std::string createCollections;
    for (std::size_t index = 0u;
            index < kNavigatorScriptMaxSelectorCollections; ++index) {
        createCollections += "var rows" + std::to_string(index) +
            "=document.querySelectorAll('.absent" +
            std::to_string(index) + "');";
    }
    createCollections += "var filled=rows0.length===0;";
    const ScriptResult saturated = harness.execute(createCollections);
    expect(saturated.succeeded() && booleanIs(harness, "filled", true) &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == kNavigatorScriptMaxSelectorCollections,
        "JS77 fills the existing 128-record collection registry");
    const ScriptResult traversed = harness.execute(
        "var result=rows0.some(function(){return true;});var ok=result===false;");
    expect(traversed.succeeded() && booleanIs(harness, "ok", true) &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == kNavigatorScriptMaxSelectorCollections,
        "JS77 traverses an empty retained collection without registry growth");
    const ScriptResult overflow = harness.execute(
        "document.querySelectorAll('.absent-overflow');");
    expect(!overflow.succeeded() && overflow.runtimeError.code ==
            RuntimeErrorCode::DocumentLookupLimitExceeded,
        "JS77 preserves collection registry overflow behavior");
    const ScriptResult preserved = harness.execute(
        "var result=rows0.some(function(){return true;});var ok=result===false;");
    expect(preserved.succeeded() && booleanIs(harness, "ok", true) &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == kNavigatorScriptMaxSelectorCollections,
        "JS77 registry saturation does not corrupt existing handles");
}

void testFilteredPositionAndAdapters()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><ul><li id='p0' class='pos selected'></li>"
            "<li id='p1' class='pos selected'></li>"
            "<li id='p2' class='pos selected'></li>"
            "<li id='p3' class='pos selected'></li></ul>"
            "<section id='root'><b id='b0' class='target'></b>"
            "<b id='b1' class='target'></b></section>"
            "<section id='left-root'><i id='la' class='scope-row'></i>"
            "<i id='la-extra' class='scope-row'></i></section>"
            "<section id='right-root'><i id='ra' class='scope-row'></i></section>"
            "</body></html>",
            "JS77 filtered-selector and adapter fixture loads")) return;
    SomeLookupCounter lookups;
    harness.hostAdapter().setSelectorCollectionSomeLookupProbe(
        countSomeLookup, &lookups);
    const ScriptResult result = harness.execute(
        "var filtered=document.querySelectorAll('.pos:nth-child(odd of .selected)');"
        "var ids='';var indices='';var calls=0;"
        "var found=filtered.some(function(element,index,collection){calls++;"
        "ids=ids+element.id;indices=indices+index;"
        "if(index===0)document.querySelectorAll('#p0').item(0).removeAttribute('class');"
        "return element.id==='p3'&&collection===filtered;});"
        "var ok=found===true&&ids==='p0p3'&&indices==='01'&&calls===2;");
    harness.hostAdapter().setSelectorCollectionSomeLookupProbe(nullptr, nullptr);
    expect(result.succeeded() && booleanIs(harness, "ok", true) &&
        lookups.count == 2u && lookups.lastIndex == 1u,
        "JS77 preserves filtered positional reads and stops at the live matching index");
    const ScriptResult adapters = harness.execute(
        "var root=document.getElementById('root');"
        "var tags=root.getElementsByTagName('b');"
        "var tagFound=tags.some(function(element,index,collection){"
        "return element.id==='b1'&&index===1&&collection===tags;});"
        "var classes=root.getElementsByClassName('target');"
        "var classFound=classes.some(function(element,index,collection){"
        "return element===classes[0]&&index===0&&collection===classes;});"
        "var ok=tagFound===true&&classFound===true;");
    expect(adapters.succeeded() && booleanIs(harness, "ok", true),
        "JS77 some is shared by retained tag and class collection adapters");
    const ScriptResult scopes = harness.execute(
        "var leftRoot=document.getElementById('left-root');"
        "var rightRoot=document.getElementById('right-root');"
        "var leftRows=leftRoot.querySelectorAll('.scope-row');"
        "var rightRows=rightRoot.querySelectorAll('.scope-row');"
        "var leftResult=leftRows.some(function(element,index,collection){"
        "return element.id==='la'&&index===0&&collection===leftRows;});"
        "var rightResult=rightRows.some(function(element,index,collection){"
        "return element.id==='ra'&&index===0&&collection===rightRows;});"
        "var leftAgain=leftRows.some(function(element,index,collection){"
        "return element.id==='la-extra'&&index===1&&collection===leftRows;});"
        "var scopeOk=leftRows.length===2&&rightRows.length===1&&"
        "leftResult===true&&rightResult===true&&leftAgain===true;");
    expect(scopes.succeeded() && booleanIs(harness, "scopeOk", true),
        "JS77 keeps independent scoped descriptors and callback collection identity");
}

void testNestedCallbacksAndEvents()
{
    NavigatorScriptExecutionHarness harness;
    const std::string html =
        "<html><body><button id='event-a' type='button'>A</button>"
        "<button id='event-b' type='button'>B</button>"
        "<button id='event-c' type='button'>C</button>"
        "<i id='o0' class='outer'></i><i id='o1' class='outer'></i>"
        "<i id='i0' class='inner'></i><i id='i1' class='inner'></i>"
        "<i id='ea0' class='event-a-row'></i><i id='ea1' class='event-a-row'></i>"
        "<i id='eb0' class='event-b-row'></i><i id='eb1' class='event-b-row'></i>"
        "<i id='ec0' class='event-c-row'></i></body></html>";
    if (!loadHtml(harness, html, "JS77 nested callback and Event fixture loads"))
        return;
    const ScriptResult setup = harness.execute(
        "var outer=document.querySelectorAll('.outer');"
        "var inner=document.querySelectorAll('.inner');"
        "var outerCalls=0;var innerSomeCalls=0;var innerForEachCalls=0;"
        "var nestedResult=outer.some(function(element,index,collection){"
        "outerCalls++;var innerResult=inner.some(function(item,innerIndex,list){"
        "innerSomeCalls++;return innerIndex===1&&list===inner;});"
        "inner.forEach(function(){innerForEachCalls++;});"
        "return index===1&&collection===outer&&innerResult;});"
        "var someInForEachCalls=0;var forEachResult=outer.forEach(function(){"
        "if(inner.some(function(){someInForEachCalls++;return false;})){}"
        "});var forEachReturnOk=forEachResult===undefined;"
        "var forEachInSomeCalls=0;var inverseResult=inner.some(function(){"
        "outer.forEach(function(){forEachInSomeCalls++;});return false;});"
        "var eventA=document.querySelectorAll('#event-a').item(0);"
        "var eventB=document.querySelectorAll('#event-b').item(0);"
        "var eventC=document.querySelectorAll('#event-c').item(0);"
        "var outerEvent;var middleEvent;var outerResult=false;"
        "var middleResult=false;var innerResult=false;var metadataResult=false;"
        "var eventAIds='';var eventBIds='';var eventCIds='';"
        "var middleMetadata=false;var innerMetadata=false;"
        "eventA.addEventListener('click',function(event){outerEvent=event;"
        "var eventRows=document.querySelectorAll('.event-a-row');"
        "outerResult=eventRows.some(function(element,index,collection){"
        "eventAIds=eventAIds+element.id;if(index===0)eventB.click();"
        "return index===1&&collection===eventRows;});event.preventDefault();"
        "var metadata=event.target===eventA&&event.currentTarget===eventA&&"
        "event.eventPhase===2&&event.defaultPrevented;"
        "outerResult=outerResult&&metadata;metadataResult=metadata&&"
        "middleMetadata&&innerMetadata;});"
        "eventB.addEventListener('click',function(event){middleEvent=event;"
        "var eventRows=document.querySelectorAll('.event-b-row');"
        "middleResult=eventRows.forEach(function(element,index,collection){"
        "eventBIds=eventBIds+element.id;if(index===0)eventC.click();"
        "middleResult=collection===eventRows;});"
        "var metadata=event.target===eventB&&event.currentTarget===eventB&&"
        "event.eventPhase===2&&!event.defaultPrevented&&event!==outerEvent;"
        "middleResult=metadata;middleMetadata=metadata;});"
        "eventC.addEventListener('click',function(event){"
        "var eventRows=document.querySelectorAll('.event-c-row');"
        "innerResult=eventRows.some(function(element,index,collection){"
        "eventCIds=eventCIds+element.id;return index===0&&collection===eventRows;});"
        "innerMetadata=event.target===eventC&&event.currentTarget===eventC&&"
        "event.eventPhase===2&&!event.defaultPrevented&&event!==outerEvent&&"
        "event!==middleEvent;innerResult=innerResult&&innerMetadata;});");
    if (!setup.succeeded()) {
        std::cerr << "  JS77 nested callback status=" << scriptStatusName(setup.status)
            << " runtime=" << runtimeErrorCodeName(setup.runtimeError.code)
            << " parser=" << parserErrorCodeName(setup.parserError.code)
            << '@' << setup.parserError.location.line << ':'
            << setup.parserError.location.column << '\n';
    }
    expect(setup.succeeded(), "JS77 nested callbacks and Event listeners install");
    expect(booleanIs(harness, "nestedResult", true) &&
        numberIs(harness, "outerCalls") == 2.0 &&
        numberIs(harness, "innerSomeCalls") == 4.0 &&
        numberIs(harness, "innerForEachCalls") == 4.0 &&
        numberIs(harness, "someInForEachCalls") == 4.0 &&
        numberIs(harness, "forEachInSomeCalls") == 4.0 &&
        booleanIs(harness, "forEachReturnOk", true) &&
        booleanIs(harness, "inverseResult", false),
        "JS77 nested some/forEach calls retain independent bounds and results");
    if (!booleanIs(harness, "nestedResult", true) ||
        numberIs(harness, "outerCalls") != 2.0 ||
        numberIs(harness, "innerSomeCalls") != 4.0 ||
        numberIs(harness, "innerForEachCalls") != 4.0 ||
        numberIs(harness, "someInForEachCalls") != 4.0 ||
        numberIs(harness, "forEachInSomeCalls") != 4.0 ||
        !booleanIs(harness, "forEachReturnOk", true) ||
        !booleanIs(harness, "inverseResult", false)) {
        std::cerr << "  JS77 nested values nested=" << booleanIs(harness, "nestedResult", true)
            << " outer=" << numberIs(harness, "outerCalls") << " innerSome="
            << numberIs(harness, "innerSomeCalls") << " innerForEach="
            << numberIs(harness, "innerForEachCalls") << " someInForEach="
            << numberIs(harness, "someInForEachCalls") << " forEachInSome="
            << numberIs(harness, "forEachInSomeCalls") << '\n';
    }

    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool dispatched = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, error) &&
        error == RuntimeErrorCode::None;
    if (!dispatched || !booleanIs(harness, "metadataResult", true)) {
        std::cerr << "  JS77 Event dispatch=" << dispatched << " error="
            << runtimeErrorCodeName(error) << " outer="
            << booleanIs(harness, "outerResult", true) << " middle="
            << booleanIs(harness, "middleResult", true) << " inner="
            << booleanIs(harness, "innerResult", true) << " metadata="
            << booleanIs(harness, "metadataResult", true) << " marker="
            << stringIs(harness, "eventMarker") << '\n';
    }
    expect(dispatched && booleanIs(harness, "outerResult", true) &&
        booleanIs(harness, "middleResult", true) &&
        booleanIs(harness, "innerResult", true) &&
        booleanIs(harness, "metadataResult", true) &&
        stringIs(harness, "eventAIds") == "ea0ea1" &&
        stringIs(harness, "eventBIds") == "eb0eb1" &&
        stringIs(harness, "eventCIds") == "ec0",
        "JS77 nested Event dispatch preserves some/forEach results, identities, and metadata");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS77 nested callback/Event frame depth returns to zero");
}

void testGenerationBoundaryAndEarlySuccess()
{
    NavigatorScriptExecutionHarness early;
    if (!loadHtml(early,
            "<html><body><i class='row'></i><i class='row'></i></body></html>",
            "JS77 early-success fixture loads")) return;
    SomeLookupCounter earlyLookups;
    early.hostAdapter().setSelectorCollectionSomeLookupProbe(
        countSomeLookup, &earlyLookups);
    const ScriptResult success = early.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "var found=rows.some(function(){calls++;return 'found';});");
    early.hostAdapter().setSelectorCollectionSomeLookupProbe(nullptr, nullptr);
    expect(success.succeeded() && booleanIs(early, "found", true) &&
        numberIs(early, "calls") == 1.0 && earlyLookups.count == 1u &&
        earlyLookups.lastIndex == 0u,
        "JS77 truthy non-Boolean return short-circuits before another lookup");

    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='same' class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS77 generation-boundary fixture loads")) return;
    SomeGenerationProbe probe;
    probe.harness = &harness;
    probe.initialGeneration = harness.hostAdapter().generation();
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "same") probe.initialSerial = element.serial;
    SomeLookupCounter lookups;
    harness.hostAdapter().setSelectorCollectionSomeLookupProbe(
        countSomeLookup, &lookups);
    harness.hostAdapter().setSelectorCollectionSomeCallbackProbe(
        replaceGenerationAfterSomeCallback, &probe);
    const ScriptResult stale = harness.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "var result=rows.some(function(){calls++;return false;});");
    harness.hostAdapter().setSelectorCollectionSomeCallbackProbe(nullptr, nullptr);
    harness.hostAdapter().setSelectorCollectionSomeLookupProbe(nullptr, nullptr);
    expect(probe.changed && probe.error == RuntimeErrorCode::None &&
        harness.hostAdapter().generation() != probe.initialGeneration &&
        probe.initialSerial != 0u &&
        probe.replacementSerial == probe.initialSerial,
        "JS77 generation probe replaces the document and reuses an Element serial");
    expect(!stale.succeeded() && stale.runtimeError.code ==
            RuntimeErrorCode::StaleHostObject &&
        numberIs(harness, "calls") == 1.0 && lookups.count == 1u,
        "JS77 old collection fails stale after one callback without reusing a new-generation Element");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS77 stale-generation failure unwinds all callback frames");
}

std::uint64_t medianMicroseconds(const std::vector<std::uint64_t>& values)
{
    std::vector<std::uint64_t> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2u];
}

std::uint64_t measureWorkload(const std::string& html,
    const std::string& source, const std::string& name)
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, html, "JS77 " + name + " timing fixture loads"))
        return 0u;
    const auto start = std::chrono::steady_clock::now();
    const ScriptResult result = harness.execute(source);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    expect(result.succeeded() && booleanIs(harness, "ok", true),
        "JS77 " + name + " timing workload completes correctly");
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed));
}

void testPerformanceAndPoolReuse()
{
    const std::string html = capacityHtml();
    const std::pair<const char*, const char*> workloads[] = {
        {"some-early", "var rows=document.querySelectorAll('.all');var calls=0;"
            "var result=rows.some(function(){calls++;return true;});"
            "var ok=result===true&&calls===1;"},
        {"some-all-false", "var rows=document.querySelectorAll('.all');var calls=0;"
            "var result=rows.some(function(){calls++;return false;});"
            "var ok=result===false&&calls===1024;"},
        {"forEach-all-false", "var rows=document.querySelectorAll('.all');var calls=0;"
            "rows.forEach(function(){calls++;return false;});var ok=calls===1024;"},
        {"item-loop", "var rows=document.querySelectorAll('.all');var calls=0;"
            "var length=rows.length;for(var index=0;index<length;index++){"
            "if(rows.item(index))calls++;}var ok=calls===1024;"},
    };
    for (const auto& workload : workloads) {
        std::vector<std::uint64_t> times;
        for (std::size_t repetition = 0u; repetition < 3u; ++repetition)
            times.push_back(measureWorkload(html, workload.second,
                std::string(workload.first) + " run " +
                    std::to_string(repetition)));
        std::cout << "JS77 timing workload=" << workload.first
            << " median-us=" << medianMicroseconds(times)
            << " min-us=" << *std::min_element(times.begin(), times.end())
            << " max-us=" << *std::max_element(times.begin(), times.end())
            << " repetitions=" << times.size() << '\n';
    }

    NavigatorScriptExecutionHarness repeated;
    if (!loadHtml(repeated,
            "<html><body><i class='row'></i></body></html>",
            "JS77 repeated-traversal fixture loads")) return;
    const ScriptResult run = repeated.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "for(var pass=0;pass<128;pass++){rows.some(function(){calls++;return true;});}"
        "var ok=calls===128;");
    expect(run.succeeded() && booleanIs(repeated, "ok", true) &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            repeated.hostAdapter()) == 1u &&
        repeated.runtime().environmentCount() <=
            kDefaultMaxJavaScriptEnvironments &&
        repeated.runtime().hostObjectCount() <=
            kDefaultMaxJavaScriptHostObjects,
        "JS77 repeated traversal reuses its collection record within pool bounds");
}

#if defined(GXOS_JS77_STACK_INSTRUMENTED) && defined(_WIN32)
struct Js77StackSample {
    const char* label = nullptr;
    std::size_t bytes = 0u;
    bool passed = false;
};

struct Js77StackProbeResult {
    static constexpr std::size_t kReserveBytes = 1024u * 1024u;
    static constexpr std::size_t kMaxSamples = 12u;
    std::uintptr_t stackBase = 0u;
    std::uintptr_t initialStackLimit = 0u;
    Js77StackSample samples[kMaxSamples]{};
    std::size_t sampleCount = 0u;
    std::size_t maximumCommittedBytes = 0u;
    bool passed = true;
};

bool beginJs77StackSample(Js77StackProbeResult& result,
    std::uintptr_t marker)
{
    if (result.sampleCount >= Js77StackProbeResult::kMaxSamples ||
        marker >= result.stackBase) {
        result.passed = false;
        return false;
    }
    g_js77LowestStackAddress = marker;
    g_js77StackSampleActive = true;
    return true;
}

void finishJs77StackSample(Js77StackProbeResult& result,
    const char* label, bool passed)
{
    g_js77StackSampleActive = false;
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const std::uintptr_t stackLimit =
        reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    const bool boundsValid = g_js77LowestStackAddress < result.stackBase &&
        stackLimit <= result.stackBase &&
        stackLimit >= result.stackBase - Js77StackProbeResult::kReserveBytes &&
        stackLimit <= result.initialStackLimit;
    const std::size_t bytes = g_js77LowestStackAddress < result.stackBase
        ? static_cast<std::size_t>(result.stackBase -
            g_js77LowestStackAddress) : 0u;
    const std::size_t committed = stackLimit <= result.stackBase
        ? static_cast<std::size_t>(result.stackBase - stackLimit) : 0u;
    result.maximumCommittedBytes =
        std::max(result.maximumCommittedBytes, committed);
    const bool samplePassed = passed && boundsValid &&
        committed < Js77StackProbeResult::kReserveBytes;
    result.samples[result.sampleCount++] =
        Js77StackSample{label, bytes, samplePassed};
    result.passed = result.passed && samplePassed;
}

bool measureJs77StackScript(Js77StackProbeResult& result,
    NavigatorScriptExecutionHarness& harness, const char* label,
    const std::string& source, const char* resultName = "ok")
{
    volatile std::uint8_t marker = 0u;
    if (!beginJs77StackSample(result,
            reinterpret_cast<std::uintptr_t>(&marker))) return false;
    const ScriptResult script = harness.execute(source);
    const bool passed = script.succeeded() &&
        booleanIs(harness, resultName, true);
    finishJs77StackSample(result, label, passed);
    return passed;
}

bool runJs77StackWorkloads(Js77StackProbeResult& result)
{
    bool passed = true;
    NavigatorScriptExecutionHarness ordinary;
    if (!loadHtml(ordinary,
            "<html><body><i class='row'></i><i class='row'></i>"
            "<i class='row'></i><li class='pos selected'></li>"
            "<li class='pos selected'></li><li class='pos selected'></li>"
            "<button id='event-a' type='button'>A</button>"
            "<button id='event-b' type='button'>B</button>"
            "<button id='event-c' type='button'>C</button>"
            "<i class='ea'></i><i class='eb'></i><i class='ec'></i>"
            "</body></html>", "JS77 stack ordinary fixture loads") ||
        !ordinary.execute("var rows=document.querySelectorAll('.row');"
            "var ok=false;").succeeded()) return false;
    passed = measureJs77StackScript(result, ordinary, "empty-some",
        "var empty=document.querySelectorAll('.absent');var calls=0;"
        "var value=empty.some(function(){calls++;return true;});"
        "var ok=value===false&&calls===0;") && passed;
    passed = measureJs77StackScript(result, ordinary, "early-true-some",
        "var calls=0;var value=rows.some(function(){calls++;return true;});"
        "var ok=value===true&&calls===1;") && passed;
    passed = measureJs77StackScript(result, ordinary, "all-false-some",
        "var calls=0;var value=rows.some(function(){calls++;return false;});"
        "var ok=value===false&&calls===3;") && passed;
    passed = measureJs77StackScript(result, ordinary, "filtered-some",
        "var filtered=document.querySelectorAll('.pos:nth-child(odd of .selected)');"
        "var calls=0;var value=filtered.some(function(){calls++;return false;});"
        "var ok=value===false&&calls===2;") && passed;
    passed = measureJs77StackScript(result, ordinary, "nested-some",
        "var calls=0;var value=rows.some(function(){return rows.some(function(){"
        "calls++;return true;});});var ok=value===true&&calls===1;") && passed;
    passed = measureJs77StackScript(result, ordinary, "some-inside-forEach",
        "var calls=0;rows.forEach(function(){if(rows.some(function(){"
        "calls++;return true;})){} });var ok=calls===3;") && passed;
    passed = measureJs77StackScript(result, ordinary, "forEach-inside-some",
        "var calls=0;var value=rows.some(function(){rows.forEach(function(){"
        "calls++;});return true;});var ok=value===true&&calls===3;") && passed;

    NavigatorScriptExecutionHarness event;
    if (!loadHtml(event,
            "<html><body><button id='event-a' type='button'>A</button>"
            "<button id='event-b' type='button'>B</button>"
            "<button id='event-c' type='button'>C</button>"
            "<i class='ea'></i><i class='eb'></i><i class='ec'></i>"
            "</body></html>", "JS77 nested Event stack fixture loads") ||
        !event.execute(
            "var a=document.querySelectorAll('#event-a').item(0);"
            "var b=document.querySelectorAll('#event-b').item(0);"
            "var c=document.querySelectorAll('#event-c').item(0);var ok=false;"
            "a.addEventListener('click',function(){var rows=document.querySelectorAll('.ea');"
            "ok=rows.some(function(){b.click();return true;});});"
            "b.addEventListener('click',function(){var rows=document.querySelectorAll('.eb');"
            "rows.forEach(function(){var nested=document.querySelectorAll('.ec');"
            "nested.some(function(){return true;});});});").succeeded()) return false;
    HostInstanceId eventTarget = 0u;
    for (const auto& element : event.document().structuralElements)
        if (element.id == "event-a") eventTarget = element.serial;
    volatile std::uint8_t marker = 0u;
    if (eventTarget == 0u || !beginJs77StackSample(result,
            reinterpret_cast<std::uintptr_t>(&marker))) return false;
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool dispatched = event.dispatchClick(eventTarget, eventError);
    finishJs77StackSample(result, "nested-Event-some-and-forEach",
        dispatched && eventError == RuntimeErrorCode::None &&
            booleanIs(event, "ok", true));
    return passed && result.passed;
}

DWORD WINAPI js77StackProbeThread(void* parameter)
{
    auto* result = static_cast<Js77StackProbeResult*>(parameter);
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    result->stackBase = reinterpret_cast<std::uintptr_t>(tib->StackBase);
    result->initialStackLimit =
        reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    result->passed = result->stackBase > result->initialStackLimit &&
        runJs77StackWorkloads(*result);
    return result->passed ? 0u : 1u;
}

bool runJs77StackProbe()
{
    Js77StackProbeResult result;
    DWORD threadId = 0u;
    HANDLE thread = CreateThread(nullptr, Js77StackProbeResult::kReserveBytes,
        js77StackProbeThread, &result, STACK_SIZE_PARAM_IS_A_RESERVATION,
        &threadId);
    if (thread == nullptr) return false;
    const DWORD wait = WaitForSingleObject(thread, INFINITE);
    DWORD exitCode = 1u;
    const bool exitRead = GetExitCodeThread(thread, &exitCode) != 0;
    CloseHandle(thread);
    std::size_t maximum = 0u;
    for (std::size_t index = 0u; index < result.sampleCount; ++index) {
        const Js77StackSample& sample = result.samples[index];
        maximum = std::max(maximum, sample.bytes);
        std::cout << "JS77 stack sample " << sample.label << ": observed="
            << sample.bytes << " B; " <<
            (sample.passed ? "PASS" : "FAIL") << '\n';
    }
    std::cout << "JS77 stack maximum instrumented=" << maximum
        << " B; maximum TEB committed=" << result.maximumCommittedBytes
        << " B / " << Js77StackProbeResult::kReserveBytes << " B; status="
        << (result.passed && wait == WAIT_OBJECT_0 && exitRead && exitCode == 0u
            ? "PASS" : "FAIL") << '\n';
    return result.passed && wait == WAIT_OBJECT_0 && exitRead && exitCode == 0u &&
        maximum < Js77StackProbeResult::kReserveBytes;
}
#endif

} // namespace

int main(int argc, char** argv)
{
#if defined(GXOS_JS77_STACK_INSTRUMENTED) && defined(_WIN32)
    if (argc > 1 && std::string(argv[1]) == "--stack-only")
        return runJs77StackProbe() ? 0 : 1;
#endif
    (void)argc;
    (void)argv;
    testHostedFixture();
    testTruthinessAndCallbackArguments();
    testInvalidCallbacksAndCallbackErrors();
    testMutationAndShortCircuit();
    testCapacityShortCircuitAndPools();
    testCollectionRegistryReuse();
    testFilteredPositionAndAdapters();
    testNestedCallbacksAndEvents();
    testGenerationBoundaryAndEarlySuccess();
    testPerformanceAndPoolReuse();
    std::cout << "Navigator JavaScript JS77 checks: " << checks - failures
        << '/' << checks << " passed\n";
    return failures == 0u ? 0 : 1;
}
