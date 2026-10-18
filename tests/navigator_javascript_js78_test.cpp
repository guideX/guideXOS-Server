#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#if defined(GXOS_JS78_STACK_INSTRUMENTED) && defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

namespace {
thread_local bool g_js78StackSampleActive = false;
thread_local std::uintptr_t g_js78LowestStackAddress =
    (std::numeric_limits<std::uintptr_t>::max)();
} // namespace

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void*, void*)
{
    if (!g_js78StackSampleActive) return;
    volatile std::uint8_t marker = 0u;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&marker);
    if (address < g_js78LowestStackAddress)
        g_js78LowestStackAddress = address;
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

bool booleanIs(const NavigatorScriptExecutionHarness& harness,
    const char* name, bool expected);
bool loadHtml(NavigatorScriptExecutionHarness& harness,
    const std::string& html, const std::string& label);

#if defined(GXOS_JS78_STACK_INSTRUMENTED) && defined(_WIN32)
struct StackSample {
    const char* label = nullptr;
    std::size_t bytes = 0u;
    bool passed = false;
};

struct StackProbeResult {
    static constexpr std::size_t kReserveBytes = 1024u * 1024u;
    static constexpr std::size_t kMaxSamples = 12u;
    std::uintptr_t stackBase = 0u;
    std::uintptr_t initialStackLimit = 0u;
    StackSample samples[kMaxSamples]{};
    std::size_t sampleCount = 0u;
    std::size_t maximumCommittedBytes = 0u;
    bool passed = true;
};

bool beginStackSample(StackProbeResult& result, std::uintptr_t marker)
{
    if (result.sampleCount >= StackProbeResult::kMaxSamples ||
        marker >= result.stackBase) {
        result.passed = false;
        return false;
    }
    g_js78LowestStackAddress = marker;
    g_js78StackSampleActive = true;
    return true;
}

void finishStackSample(StackProbeResult& result, const char* label,
    bool passed)
{
    g_js78StackSampleActive = false;
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const std::uintptr_t stackLimit =
        reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    const bool boundsValid = g_js78LowestStackAddress < result.stackBase &&
        stackLimit <= result.stackBase &&
        stackLimit >= result.stackBase - StackProbeResult::kReserveBytes &&
        stackLimit <= result.initialStackLimit;
    const std::size_t bytes = g_js78LowestStackAddress < result.stackBase
        ? static_cast<std::size_t>(result.stackBase -
            g_js78LowestStackAddress) : 0u;
    const std::size_t committed = stackLimit <= result.stackBase
        ? static_cast<std::size_t>(result.stackBase - stackLimit) : 0u;
    result.maximumCommittedBytes =
        std::max(result.maximumCommittedBytes, committed);
    const bool samplePassed = passed && boundsValid &&
        committed < StackProbeResult::kReserveBytes;
    result.samples[result.sampleCount++] = StackSample{label, bytes,
        samplePassed};
    result.passed = result.passed && samplePassed;
}

bool measureStackScript(StackProbeResult& result,
    NavigatorScriptExecutionHarness& harness, const char* label,
    const std::string& source)
{
    volatile std::uint8_t marker = 0u;
    if (!beginStackSample(result,
            reinterpret_cast<std::uintptr_t>(&marker))) return false;
    const ScriptResult script = harness.execute(source);
    const bool passed = script.succeeded() && booleanIs(harness, "ok", true);
    finishStackSample(result, label, passed);
    return passed;
}

bool runStackWorkloads(StackProbeResult& result)
{
    bool passed = true;
    NavigatorScriptExecutionHarness ordinary;
    if (!loadHtml(ordinary,
            "<html><body><i class='row'></i><i class='row'></i>"
            "<i class='row'></i><li class='pos selected'></li>"
            "<li class='pos selected'></li><li class='pos selected'></li>"
            "<button id='a' type='button'>A</button>"
            "<button id='b' type='button'>B</button>"
            "<i class='event-a'></i><i class='event-b'></i>"
            "</body></html>", "JS78 stack ordinary fixture loads") ||
        !ordinary.execute("var rows=document.querySelectorAll('.row');"
            "var ok=false;").succeeded()) return false;
    passed = measureStackScript(result, ordinary, "empty-every",
        "var empty=document.querySelectorAll('.absent');var calls=0;"
        "var value=empty.every(function(){calls++;return false;});"
        "var ok=value===true&&calls===0;") && passed;
    passed = measureStackScript(result, ordinary, "first-falsy-every",
        "var calls=0;var value=rows.every(function(){calls++;return false;});"
        "var ok=value===false&&calls===1;") && passed;
    passed = measureStackScript(result, ordinary, "all-true-every",
        "var calls=0;var value=rows.every(function(){calls++;return true;});"
        "var ok=value===true&&calls===3;") && passed;
    passed = measureStackScript(result, ordinary, "filtered-every",
        "var filtered=document.querySelectorAll('.pos:nth-child(odd of .selected)');"
        "var calls=0;var value=filtered.every(function(){calls++;return true;});"
        "var ok=value===true&&calls===2;") && passed;
    passed = measureStackScript(result, ordinary, "nested-every",
        "var calls=0;var value=rows.every(function(){return rows.every(function(){"
        "calls++;return true;});});var ok=value===true&&calls===9;") && passed;
    passed = measureStackScript(result, ordinary, "every-inside-some",
        "var calls=0;var value=rows.some(function(){return rows.every(function(){"
        "calls++;return true;});});var ok=value===true&&calls===3;") && passed;
    passed = measureStackScript(result, ordinary, "some-inside-every",
        "var calls=0;var value=rows.every(function(){return rows.some(function(){"
        "calls++;return true;});});var ok=value===true&&calls===3;") && passed;

    NavigatorScriptExecutionHarness event;
    if (!loadHtml(event,
            "<html><body><button id='a' type='button'>A</button>"
            "<button id='b' type='button'>B</button>"
            "<i class='event-a'></i><i class='event-a'></i>"
            "<i class='event-b'></i><i class='event-b'></i>"
            "</body></html>", "JS78 nested Event stack fixture loads") ||
        !event.execute(
            "var a=document.querySelectorAll('#a').item(0);"
            "var b=document.querySelectorAll('#b').item(0);var ok=false;"
            "a.addEventListener('click',function(){var outer=document.querySelectorAll('.event-a');"
            "ok=outer.every(function(element,index){if(index===0)b.click();return true;});});"
            "b.addEventListener('click',function(){var inner=document.querySelectorAll('.event-b');"
            "inner.some(function(){var nested=document.querySelectorAll('.event-b');"
            "nested.forEach(function(){ });return true;});});").succeeded())
        return false;
    HostInstanceId target = 0u;
    for (const auto& element : event.document().structuralElements)
        if (element.id == "a") target = element.serial;
    volatile std::uint8_t marker = 0u;
    if (target == 0u || !beginStackSample(result,
            reinterpret_cast<std::uintptr_t>(&marker))) return false;
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool dispatched = event.dispatchClick(target, eventError);
    finishStackSample(result, "nested-Event-every-some-forEach",
        dispatched && eventError == RuntimeErrorCode::None &&
            booleanIs(event, "ok", true));
    return passed && result.passed;
}

DWORD WINAPI stackProbeThread(void* parameter)
{
    auto* result = static_cast<StackProbeResult*>(parameter);
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    result->stackBase = reinterpret_cast<std::uintptr_t>(tib->StackBase);
    result->initialStackLimit =
        reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    result->passed = result->stackBase > result->initialStackLimit &&
        runStackWorkloads(*result);
    return result->passed ? 0u : 1u;
}

bool runStackProbe()
{
    StackProbeResult result;
    DWORD threadId = 0u;
    HANDLE thread = CreateThread(nullptr, StackProbeResult::kReserveBytes,
        stackProbeThread, &result, STACK_SIZE_PARAM_IS_A_RESERVATION,
        &threadId);
    if (thread == nullptr) return false;
    const DWORD wait = WaitForSingleObject(thread, INFINITE);
    DWORD exitCode = 1u;
    const bool exitRead = GetExitCodeThread(thread, &exitCode) != 0;
    CloseHandle(thread);
    std::size_t maximum = 0u;
    for (std::size_t index = 0u; index < result.sampleCount; ++index) {
        const StackSample& sample = result.samples[index];
        maximum = std::max(maximum, sample.bytes);
        std::cout << "JS78 stack sample " << sample.label << ": observed="
            << sample.bytes << " B; " <<
            (sample.passed ? "PASS" : "FAIL") << '\n';
    }
    std::cout << "JS78 stack maximum instrumented=" << maximum
        << " B; maximum TEB committed=" << result.maximumCommittedBytes
        << " B / " << StackProbeResult::kReserveBytes << " B; status="
        << (result.passed && wait == WAIT_OBJECT_0 && exitRead && exitCode == 0u
            ? "PASS" : "FAIL") << '\n';
    return result.passed && wait == WAIT_OBJECT_0 && exitRead && exitCode == 0u &&
        maximum < StackProbeResult::kReserveBytes;
}
#endif

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
    const bool loaded = harness.loadHtml("file:///js78-native-fixture.html",
        html, error) && error == RuntimeErrorCode::None;
    expect(loaded, label);
    return loaded;
}

struct EveryProbe {
    std::size_t lookups = 0u;
    std::size_t callbacks = 0u;
    std::size_t entryLengthCalls = 0u;
    std::size_t entryLength = 0u;
    std::size_t lastIndex = 0u;
};

void countEveryLookup(void* context, std::size_t index)
{
    auto* probe = static_cast<EveryProbe*>(context);
    ++probe->lookups;
    probe->lastIndex = index;
}

void countEveryCallback(void* context, std::size_t index)
{
    auto* probe = static_cast<EveryProbe*>(context);
    ++probe->callbacks;
    probe->lastIndex = index;
}

void recordEntryLength(void* context, std::size_t length)
{
    auto* probe = static_cast<EveryProbe*>(context);
    ++probe->entryLengthCalls;
    probe->entryLength = length;
}

void setEveryProbes(NavigatorScriptExecutionHarness& harness,
    EveryProbe& probe)
{
    harness.hostAdapter().setSelectorCollectionEveryLookupProbe(
        countEveryLookup, &probe);
    harness.hostAdapter().setSelectorCollectionEveryCallbackProbe(
        countEveryCallback, &probe);
    harness.hostAdapter().setSelectorCollectionEveryLengthProbe(
        recordEntryLength, &probe);
}

void clearEveryProbes(NavigatorScriptExecutionHarness& harness)
{
    harness.hostAdapter().setSelectorCollectionEveryLookupProbe(nullptr, nullptr);
    harness.hostAdapter().setSelectorCollectionEveryCallbackProbe(nullptr, nullptr);
    harness.hostAdapter().setSelectorCollectionEveryLengthProbe(nullptr, nullptr);
}

std::string readFixture(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::string fixtureScript(const std::string& html)
{
    const std::size_t open = html.find("<script>");
    const std::size_t close = html.find("</script>", open);
    if (open == std::string::npos || close == std::string::npos || close < open)
        return std::string();
    return html.substr(open + 8u, close - open - 8u);
}

void testHostedFixture()
{
    const std::string html = readFixture("navigator-smoke/javascript-js78.html");
    const std::string source = fixtureScript(html);
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = !source.empty() && harness.loadHtml(
        "file:///js78-hosted-fixture.html", html, error) &&
        error == RuntimeErrorCode::None;
    expect(loaded, "JS78 hosted fixture loads in the native harness");
    if (!loaded) return;
    expect(source.find('<') == std::string::npos &&
        harness.document().scriptSources.size() == 1u &&
        harness.document().scriptSources[0] == source,
        "JS78 hosted inline source passes the HTML-subset integrity guard");
    const ScriptResult result = harness.execute(source);
    if (!result.succeeded() || numberIs(harness, "failures") != 0.0) {
        std::cerr << "  JS78 fixture status=" << scriptStatusName(result.status)
            << " runtime=" << runtimeErrorCodeName(result.runtimeError.code)
            << " parser=" << parserErrorCodeName(result.parserError.code)
            << '@' << result.parserError.location.line << ':'
            << result.parserError.location.column << " assertions="
            << numberIs(harness, "assertions") << " failures="
            << numberIs(harness, "failures") << " marker="
            << stringIs(harness, "initialMarker") << '\n';
    }
    expect(result.succeeded() && numberIs(harness, "assertions") >= 10.0 &&
        numberIs(harness, "failures") == 0.0 &&
        stringIs(harness, "initialMarker").find(
            "JS78:initial:assertions=") == 0u,
        "JS78 hosted fixture passes empty, traversal, conversion, mutation, adapters, and nested callback assertions");
    expect(stringIs(harness, "initialMarker").find(
            ":first=1:middle=3:last=5:empty=0") != std::string::npos,
        "JS78 hosted fixture reports first, middle, last, and empty callback counts");

    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    error = RuntimeErrorCode::None;
    const bool dispatched = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, error) &&
        error == RuntimeErrorCode::None;
    expect(dispatched && booleanIs(harness, "eventEvery", true) &&
        booleanIs(harness, "eventSome", true) &&
        booleanIs(harness, "eventForEach", true) &&
        booleanIs(harness, "eventMetadata", true),
        "JS78 every callback survives nested Event some/forEach dispatch and preserves Event metadata");
    if (!dispatched || !booleanIs(harness, "eventEvery", true) ||
        !booleanIs(harness, "eventSome", true) ||
        !booleanIs(harness, "eventForEach", true) ||
        !booleanIs(harness, "eventMetadata", true)) {
        std::cerr << "  JS78 event dispatched=" << dispatched
            << " error=" << runtimeErrorCodeName(error)
            << " every=" << booleanIs(harness, "eventEvery", true)
            << " some=" << booleanIs(harness, "eventSome", true)
            << " forEach=" << booleanIs(harness, "eventForEach", true)
            << " metadata=" << booleanIs(harness, "eventMetadata", true)
            << " marker=" << stringIs(harness, "result") << '\n';
    }
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS78 hosted callback and Event frames unwind to zero");
}

void testTruthinessArgumentsAndEmptyCollection()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='a' class='row'></i>"
            "<i id='b' class='row'></i><i id='c' class='row'></i></body></html>",
            "JS78 truthiness fixture loads")) return;
    const ScriptResult result = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "function truthyFunction(){}"
        "var trueValue=rows.every(function(){return true;});"
        "var falseValue=rows.every(function(){return false;});"
        "var zeroValue=rows.every(function(){return 0;});"
        "var negativeZeroValue=rows.every(function(){return -0;});"
        "var nonzeroValue=rows.every(function(){return 4;});"
        "var nanValue=rows.every(function(){return 0/0;});"
        "var emptyStringValue=rows.every(function(){return '';});"
        "var stringValue=rows.every(function(){return 'found';});"
        "var nullValue=rows.every(function(){return null;});"
        "var undefinedValue=rows.every(function(){return undefined;});"
        "var objectValue=rows.every(function(){return {};});"
        "var functionValue=rows.every(function(){return truthyFunction;});"
        "var hostValue=rows.every(function(element){return element;});"
        "var receiver={tag:'receiver'};var receiverCalls=0;"
        "var receiverValue=rows.every(function(element,index,collection){"
        "receiverCalls++;return this.tag==='receiver'&&element===rows[index]&&"
        "index>=0&&collection===rows;},receiver);"
        "var omittedReceiver=rows.every(function(){return this===undefined;});"
        "var empty=document.querySelectorAll('.absent');var emptyCalls=0;"
        "var emptyValue=empty.every(function(){emptyCalls++;return false;});"
        "var methodStable=rows.every===rows.every;var savedEvery=rows.every;"
        "var valuesOk=trueValue===true&&falseValue===false&&zeroValue===false&&"
        "negativeZeroValue===false&&nonzeroValue===true&&nanValue===false&&"
        "emptyStringValue===false&&stringValue===true&&nullValue===false&&"
        "undefinedValue===false&&objectValue===true&&functionValue===true&&"
        "hostValue===true;"
        "var argumentsOk=receiverValue===true&&receiverCalls===3&&"
        "omittedReceiver===true&&emptyValue===true&&emptyCalls===0&&"
        "methodStable&&savedEvery===rows.every;");
    expect(result.succeeded(), "JS78 truthiness and callback argument script runs");
    expect(booleanIs(harness, "valuesOk", true),
        "JS78 uses the existing JavaScript truthiness conversion for all supported values");
    expect(booleanIs(harness, "argumentsOk", true),
        "JS78 forwards canonical Element, index, original collection, thisArg, and empty semantics");

    const std::string invalidCases[] = {
        "empty.every()", "empty.every(null)", "empty.every(0)",
        "empty.every('invalid')", "rows.every(123)",
    };
    for (const std::string& invalid : invalidCases) {
        NavigatorScriptExecutionHarness invalidHarness;
        if (!loadHtml(invalidHarness,
                "<html><body><i class='row'></i></body></html>",
                "JS78 invalid callback fixture loads")) continue;
        EveryProbe probe;
        setEveryProbes(invalidHarness, probe);
        const ScriptResult setup = invalidHarness.execute(
            "var rows=document.querySelectorAll('.row');"
            "var empty=document.querySelectorAll('.absent');");
        const std::size_t environmentsBefore =
            invalidHarness.runtime().environmentCount();
        const std::size_t hostsBefore = invalidHarness.runtime().hostObjectCount();
        const ScriptResult failed = setup.succeeded()
            ? invalidHarness.execute(invalid + ";") : setup;
        clearEveryProbes(invalidHarness);
        const std::size_t environmentsAfter =
            invalidHarness.runtime().environmentCount();
        const std::size_t hostsAfter = invalidHarness.runtime().hostObjectCount();
        expect(!failed.succeeded() && failed.status == ScriptStatus::RuntimeFailure &&
            failed.runtimeError.code == RuntimeErrorCode::TypeError &&
            probe.lookups == 0u && probe.callbacks == 0u &&
            probe.entryLengthCalls == 0u &&
            environmentsAfter == environmentsBefore && hostsAfter == hostsBefore &&
            invalidHarness.runtime().activeCallFrameCount() == 0u,
            "JS78 invalid callback rejects before traversal or pool growth: " + invalid);
        const ScriptResult recovered = invalidHarness.execute(
            "var recovered=rows.every(function(){return true;});");
        expect(recovered.succeeded() && booleanIs(invalidHarness,
                "recovered", true),
            "JS78 invalid callback leaves a usable evaluator: " + invalid);
    }
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

void runCountCase(const std::string& html, const char* selector,
    std::size_t size, std::size_t falseIndex, bool allTruthy)
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, html,
            std::string("JS78 operation-count fixture loads ") + selector))
        return;
    EveryProbe probe;
    setEveryProbes(harness, probe);
    const std::string expectedResult = allTruthy ? "true" : "false";
    const std::size_t expectedVisits = allTruthy ? size : falseIndex + 1u;
    const std::string callbackResult = allTruthy ? "true"
        : "index!==" + std::to_string(falseIndex);
    const ScriptResult result = harness.execute(
        std::string("var rows=document.querySelectorAll('") + selector +
        "');var calls=0;var answer=rows.every(function(element,index){"
        "calls++;return " + callbackResult + ";});var ok=answer===" +
        expectedResult + "&&calls===" + std::to_string(expectedVisits) + ";");
    clearEveryProbes(harness);
    const bool passed = result.succeeded() &&
        booleanIs(harness, "answer", allTruthy) &&
        booleanIs(harness, "ok", true) &&
        numberIs(harness, "calls") == static_cast<double>(expectedVisits) &&
        probe.entryLengthCalls == 1u && probe.entryLength == size &&
        probe.lookups == expectedVisits && probe.callbacks == expectedVisits &&
        (expectedVisits == 0u || probe.lastIndex == expectedVisits - 1u);
    expect(passed, std::string("JS78 exact every count ") + selector +
        (allTruthy ? " all-true" : " falsy-at-" + std::to_string(falseIndex)));
    if (size == 100u && !allTruthy && falseIndex == 3u) {
        std::cout << "JS78 100-element false-at-3: entry_length="
            << probe.entryLength << " callbacks=" << probe.callbacks
            << " indexed_lookups=" << probe.lookups << " result=false\n";
    }
}

void testShortCircuitAndCapacity()
{
    const std::string html = capacityHtml();
    const std::pair<const char*, std::size_t> sizes[] = {
        {".absent", 0u}, {".s1", 1u}, {".s16", 16u},
        {".s100", 100u}, {".s128", 128u}, {".s512", 512u},
        {".s1024", 1024u},
    };
    for (const auto& sizeCase : sizes) {
        const std::size_t size = sizeCase.second;
        if (size == 0u) {
            runCountCase(html, sizeCase.first, size, 0u, true);
            continue;
        }
        runCountCase(html, sizeCase.first, size, 0u, false);
        runCountCase(html, sizeCase.first, size, size / 2u, false);
        runCountCase(html, sizeCase.first, size, size - 1u, false);
        runCountCase(html, sizeCase.first, size, 0u, true);
    }

    NavigatorScriptExecutionHarness full;
    if (!loadHtml(full, html, "JS78 full-capacity fixture loads")) return;
    EveryProbe probe;
    setEveryProbes(full, probe);
    const ScriptResult result = full.execute(
        "var rows=document.querySelectorAll('.all');var calls=0;"
        "var answer=rows.every(function(){calls++;return true;});"
        "var ok=rows.length===1024&&answer===true&&calls===1024;");
    clearEveryProbes(full);
    const std::size_t environments = full.runtime().environmentCount();
    const std::size_t hostObjects = full.runtime().hostObjectCount();
    expect(result.succeeded() && booleanIs(full, "ok", true) &&
        probe.entryLengthCalls == 1u && probe.entryLength == 1024u &&
        probe.lookups == 1024u && probe.callbacks == 1024u &&
        environments <= kDefaultMaxJavaScriptEnvironments &&
        hostObjects <= kDefaultMaxJavaScriptHostObjects &&
        full.runtime().activeCallFrameCount() == 0u,
        "JS78 traverses all 1,024 Elements within existing pools");
    std::cout << "JS78 full-capacity pools: environments=" << environments
        << '/' << kDefaultMaxJavaScriptEnvironments << " hostObjects="
        << hostObjects << '/' << kDefaultMaxJavaScriptHostObjects
        << " callbacks=" << probe.callbacks << " lookups=" << probe.lookups
        << '\n';
    expect(sizeof(NavigatorScriptSelectorDescriptor) == 1918u &&
        NavigatorScriptRelativeSelectorTestAccess::collectionBytes() == 1936u &&
        NavigatorScriptRelativeSelectorTestAccess::collectionBytes() *
            kNavigatorScriptMaxSelectorCollections == 247808u &&
        kNavigatorScriptMaxSelectorCollections == 128u &&
        kNavigatorScriptMaxDocumentNodes == 1024u &&
        kDefaultMaxJavaScriptEnvironments == 2048u &&
        kDefaultMaxJavaScriptHostObjects == 2048u,
        "JS78 preserves descriptor, collection, registry, and runtime pool capacities");
}

struct GenerationProbe {
    NavigatorScriptExecutionHarness* harness = nullptr;
    EveryProbe* counts = nullptr;
    bool changed = false;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    HostGenerationId initialGeneration = 0u;
    HostInstanceId initialSerial = 0u;
    HostInstanceId replacementSerial = 0u;
};

void replaceGenerationAfterEveryCallback(void* context, std::size_t index)
{
    auto* probe = static_cast<GenerationProbe*>(context);
    if (probe == nullptr) return;
    if (probe->counts != nullptr) {
        ++probe->counts->callbacks;
        probe->counts->lastIndex = index;
    }
    if (probe->harness == nullptr || probe->changed || index != 0u) return;
    probe->changed = probe->harness->invalidateDocumentGeneration(probe->error);
    if (!probe->changed) return;
    probe->harness->document() = gxos::web::parseHtml(
        "file:///js78-generation-replacement.html",
        "<html><body><i id='same' class='row'></i><i class='row'></i>"
        "</body></html>");
    probe->harness->hostAdapter().attachDocument(
        probe->harness->document(), probe->harness->runtime().hostGeneration());
    for (const auto& element : probe->harness->document().structuralElements)
        if (element.id == "same") probe->replacementSerial = element.serial;
}

void testMutationGenerationAndErrors()
{
    NavigatorScriptExecutionHarness mutation;
    if (!loadHtml(mutation,
            "<html><body><i id='l0' class='live'></i>"
            "<i id='l1' class='live'></i><i id='l2' class='live'></i>"
            "<i id='l3' class='live'></i><i id='x' class='candidate'></i>"
            "<i id='s0' class='shrink'></i><i id='s1' class='shrink'></i>"
            "<i id='s2' class='shrink'></i><i id='g0' class='grow'></i>"
            "<i id='g1' class='grow'></i><i id='g2' class='candidate'></i>"
            "<i id='g3' class='candidate'></i></body></html>",
            "JS78 mutation fixture loads")) return;
    EveryProbe probe;
    setEveryProbes(mutation, probe);
    const ScriptResult changed = mutation.execute(
        "var live=document.querySelectorAll('.live');var ids='';var indices='';"
        "var calls=0;var answer=live.every(function(element,index){calls++;"
        "ids=ids+element.id;indices=indices+index;"
        "if(index===0)element.removeAttribute('class');return index!==1;});"
        "var ok=answer===false&&calls===2&&ids==='l0l2'&&indices==='01';");
    clearEveryProbes(mutation);
    expect(changed.succeeded() && booleanIs(mutation, "ok", true) &&
        probe.entryLength == 4u && probe.lookups == 2u &&
        probe.callbacks == 2u && probe.lastIndex == 1u,
        "JS78 first-falsy stop observes current live identity at the original index");

    EveryProbe growProbe;
    setEveryProbes(mutation, growProbe);
    const ScriptResult grown = mutation.execute(
        "var grow=document.querySelectorAll('.grow');var ids='';var calls=0;"
        "var answer=grow.every(function(element,index){calls++;ids=ids+element.id;"
        "if(index===0)document.querySelectorAll('#g2').item(0).setAttribute('class','grow');"
        "if(index===1)document.querySelectorAll('#g3').item(0).setAttribute('class','grow');"
        "return true;});var ok=answer===true&&calls===2&&ids==='g0g1'&&grow.length===4;");
    clearEveryProbes(mutation);
    expect(grown.succeeded() && booleanIs(mutation, "ok", true) &&
        growProbe.entryLengthCalls == 1u && growProbe.entryLength == 2u &&
        growProbe.lookups == 2u && growProbe.callbacks == 2u,
        "JS78 growth affects live membership but cannot extend the entry bound");

    EveryProbe shrinkProbe;
    setEveryProbes(mutation, shrinkProbe);
    const ScriptResult shrunk = mutation.execute(
        "var shrink=document.querySelectorAll('.shrink');var calls=0;"
        "var answer=shrink.every(function(element,index){calls++;"
        "element.removeAttribute('class');"
        "document.querySelectorAll('#s1').item(0).removeAttribute('class');"
        "document.querySelectorAll('#s2').item(0).removeAttribute('class');"
        "return true;});var ok=answer===true&&calls===1&&shrink.length===0;");
    clearEveryProbes(mutation);
    expect(shrunk.succeeded() && booleanIs(mutation, "ok", true) &&
        shrinkProbe.entryLength == 3u && shrinkProbe.lookups == 3u &&
        shrinkProbe.callbacks == 1u,
        "JS78 skips missing live indices after shrinkage and keeps the fixed bound");

    NavigatorScriptExecutionHarness filtered;
    if (loadHtml(filtered,
            "<html><body><ul><li id='p0' class='pos selected'></li>"
            "<li id='p1' class='pos selected'></li>"
            "<li id='p2' class='pos selected'></li>"
            "<li id='p3' class='pos selected'></li></ul></body></html>",
            "JS78 filtered positional fixture loads")) {
        EveryProbe filteredProbe;
        setEveryProbes(filtered, filteredProbe);
        const ScriptResult position = filtered.execute(
            "var rows=document.querySelectorAll('.pos:nth-child(odd of .selected)');"
            "var ids='';var calls=0;var answer=rows.every(function(element,index,collection){"
            "calls++;ids=ids+element.id;"
            "if(index===0)document.querySelectorAll('#p0').item(0).removeAttribute('class');"
            "return element.id!=='p3'&&collection===rows;});"
            "var ok=answer===false&&calls===2&&ids==='p0p3';");
        clearEveryProbes(filtered);
        expect(position.succeeded() && booleanIs(filtered, "ok", true) &&
            filteredProbe.lookups == 2u && filteredProbe.callbacks == 2u,
            "JS78 retains filtered positional live lookup behavior");
    }

    NavigatorScriptExecutionHarness generation;
    if (!loadHtml(generation,
            "<html><body><i id='same' class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS78 generation boundary fixture loads")) return;
    GenerationProbe generationProbe;
    generationProbe.harness = &generation;
    generationProbe.initialGeneration = generation.hostAdapter().generation();
    for (const auto& element : generation.document().structuralElements)
        if (element.id == "same") generationProbe.initialSerial = element.serial;
    EveryProbe generationCounts;
    generationProbe.counts = &generationCounts;
    setEveryProbes(generation, generationCounts);
    generation.hostAdapter().setSelectorCollectionEveryCallbackProbe(
        replaceGenerationAfterEveryCallback, &generationProbe);
    const ScriptResult stale = generation.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "var answer=rows.every(function(){calls++;return true;});");
    generation.hostAdapter().setSelectorCollectionEveryCallbackProbe(
        nullptr, nullptr);
    clearEveryProbes(generation);
    expect(generationProbe.changed && generationProbe.error ==
            RuntimeErrorCode::None &&
        generation.hostAdapter().generation() != generationProbe.initialGeneration &&
        generationProbe.initialSerial != 0u &&
        generationProbe.replacementSerial == generationProbe.initialSerial,
        "JS78 generation probe replaces the document and reuses an Element serial");
    expect(!stale.succeeded() && stale.runtimeError.code ==
            RuntimeErrorCode::StaleHostObject &&
        numberIs(generation, "calls") == 1.0 &&
        generationCounts.lookups == 1u && generationCounts.callbacks == 1u &&
        generation.runtime().activeCallFrameCount() == 0u,
        "JS78 stale collection aborts after callback zero without exposing the replacement Element");

    NavigatorScriptExecutionHarness callbackError;
    if (!loadHtml(callbackError,
            "<html><body><i class='row'></i><i class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS78 callback error fixture loads")) return;
    EveryProbe errorCounts;
    setEveryProbes(callbackError, errorCounts);
    const ScriptResult failed = callbackError.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "rows.every(function(element,index){calls++;if(index<2)return true;"
        "notDefined();return true;});");
    clearEveryProbes(callbackError);
    expect(!failed.succeeded() && failed.runtimeError.code ==
            RuntimeErrorCode::UnknownIdentifier &&
        numberIs(callbackError, "calls") == 3.0 &&
        errorCounts.lookups == 3u && errorCounts.callbacks == 2u &&
        callbackError.runtime().activeCallFrameCount() == 0u,
        "JS78 callback errors after two true results propagate without Boolean conversion");

    NavigatorScriptExecutionHarness firstError;
    if (!loadHtml(firstError, "<html><body><i class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS78 first-callback error fixture loads")) return;
    EveryProbe firstErrorCounts;
    setEveryProbes(firstError, firstErrorCounts);
    const ScriptResult firstFailed = firstError.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "rows.every(function(){calls++;notDefined();return true;});");
    clearEveryProbes(firstError);
    expect(!firstFailed.succeeded() && firstFailed.runtimeError.code ==
            RuntimeErrorCode::UnknownIdentifier &&
        numberIs(firstError, "calls") == 1.0 &&
        firstErrorCounts.lookups == 1u && firstErrorCounts.callbacks == 0u &&
        firstError.runtime().activeCallFrameCount() == 0u,
        "JS78 first callback error aborts without probing or invoking later callbacks");

    NavigatorScriptExecutionHarness nestedError;
    if (!loadHtml(nestedError,
            "<html><body><i class='outer'></i><i class='inner'></i>"
            "<i class='inner'></i></body></html>",
            "JS78 nested error fixture loads")) return;
    const ScriptResult nestedFailed = nestedError.execute(
        "var outer=document.querySelectorAll('.outer');"
        "var inner=document.querySelectorAll('.inner');var calls=0;"
        "outer.every(function(){return inner.every(function(){calls++;"
        "notDefined();return true;});});");
    expect(!nestedFailed.succeeded() && nestedFailed.runtimeError.code ==
            RuntimeErrorCode::UnknownIdentifier &&
        numberIs(nestedError, "calls") == 1.0 &&
        nestedError.runtime().activeCallFrameCount() == 0u,
        "JS78 nested every callback error unwinds inner and outer traversal frames");

    NavigatorScriptExecutionHarness eventError;
    if (!loadHtml(eventError,
            "<html><body><button id='run' type='button'></button>"
            "<i class='row'></i><i class='row'></i></body></html>",
            "JS78 Event callback error fixture loads")) return;
    const ScriptResult eventSetup = eventError.execute(
        "var button=document.querySelectorAll('#run').item(0);"
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "button.addEventListener('click',function(){rows.every(function(){"
        "calls++;notDefined();return true;});});");
    HostInstanceId runSerial = 0u;
    for (const auto& element : eventError.document().structuralElements)
        if (element.id == "run") runSerial = element.serial;
    RuntimeErrorCode eventErrorCode = RuntimeErrorCode::None;
    const bool eventFailed = runSerial != 0u &&
        eventError.dispatchClick(runSerial, eventErrorCode);
    expect(eventSetup.succeeded() && !eventFailed &&
        eventErrorCode == RuntimeErrorCode::UnknownIdentifier &&
        numberIs(eventError, "calls") == 1.0 &&
        eventError.runtime().activeCallFrameCount() == 0u,
        "JS78 callback error inside Event dispatch propagates and restores Event/callback frames");
}

void testAdaptersScopesNestingAndRegistry()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><section id='root'><b id='b0' class='target'></b>"
            "<b id='b1' class='target'></b></section>"
            "<section id='left'><i id='l' class='scope'></i></section>"
            "<section id='right'><i id='r' class='scope'></i></section>"
            "<i class='outer'></i><i class='outer'></i>"
            "<i class='inner'></i><i class='inner'></i></body></html>",
            "JS78 adapters and nested fixture loads")) return;
    const ScriptResult result = harness.execute(
        "var root=document.getElementById('root');"
        "var tags=root.getElementsByTagName('b');"
        "var classes=root.getElementsByClassName('target');"
        "var tagOk=tags.every(function(element,index,list){"
        "return element===tags[index]&&list===tags&&index>=0;});"
        "var classOk=classes.every(function(element,index,list){"
        "return element===classes[index]&&list===classes&&index>=0;});"
        "var left=document.getElementById('left').querySelectorAll('.scope');"
        "var right=document.getElementById('right').querySelectorAll('.scope');"
        "var scopes=left.every(function(element,index,list){return element.id==='l'&&"
        "index===0&&list===left;})&&right.every(function(element,index,list){"
        "return element.id==='r'&&index===0&&list===right;});"
        "var outer=document.querySelectorAll('.outer');"
        "var inner=document.querySelectorAll('.inner');"
        "var outerCalls=0;var innerCalls=0;"
        "var nested=outer.every(function(element,index,list){outerCalls++;"
        "var innerResult=inner.every(function(item,innerIndex,items){innerCalls++;"
        "return items===inner&&innerIndex>=0;});"
        "var someResult=inner.some(function(item,innerIndex){return innerIndex===0;});"
        "inner.forEach(function(){});"
        "return list===outer&&index>=0&&innerResult&&someResult;});"
        "var everyInSome=outer.some(function(element,index,list){"
        "return inner.every(function(){return true;})&&list===outer&&index===0;});"
        "var ok=tagOk&&classOk&&scopes&&nested&&outerCalls===2&&"
        "innerCalls===4&&everyInSome;");
    expect(result.succeeded() && booleanIs(harness, "ok", true),
        "JS78 shares traversal across scoped, tag/class, nested every/some/forEach collections");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS78 nested callback frames return to zero");

    NavigatorScriptExecutionHarness registry;
    if (!loadHtml(registry, "<html><body></body></html>",
            "JS78 registry saturation fixture loads")) return;
    std::string createCollections;
    for (std::size_t index = 0u;
            index < kNavigatorScriptMaxSelectorCollections; ++index) {
        createCollections += "var rows" + std::to_string(index) +
            "=document.querySelectorAll('.absent" + std::to_string(index) +
            "');";
    }
    createCollections += "var full=rows0.length===0;";
    const ScriptResult saturated = registry.execute(createCollections);
    expect(saturated.succeeded() && booleanIs(registry, "full", true) &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            registry.hostAdapter()) == kNavigatorScriptMaxSelectorCollections,
        "JS78 keeps the existing 128-record retained collection registry");
    const ScriptResult reused = registry.execute(
        "var result=rows0.every(function(){return false;});"
        "var result2=rows0.every(function(){return true;});"
        "var ok=result===true&&result2===true;");
    expect(reused.succeeded() && booleanIs(registry, "ok", true) &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            registry.hostAdapter()) == kNavigatorScriptMaxSelectorCollections,
        "JS78 repeated empty every calls reuse their original collection record");
    const ScriptResult overflow = registry.execute(
        "document.querySelectorAll('.absent-overflow');");
    expect(!overflow.succeeded() && overflow.runtimeError.code ==
            RuntimeErrorCode::DocumentLookupLimitExceeded,
        "JS78 preserves collection registry capacity exhaustion behavior");
}

std::uint64_t median(const std::vector<std::uint64_t>& values)
{
    std::vector<std::uint64_t> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2u];
}

std::uint64_t timeWorkload(const std::string& html, const char* selector,
    const std::string& source, std::size_t expectedCalls,
    int expectedAnswer, const std::string& label)
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js78-benchmark.html",
        html, error) && error == RuntimeErrorCode::None;
    if (!loaded) {
        expect(false, "JS78 benchmark fixture loads " + label);
        return 0u;
    }
    const ScriptResult setup = harness.execute(
        std::string("var rows=document.querySelectorAll('") + selector + "');");
    if (!setup.succeeded()) {
        expect(false, "JS78 benchmark query succeeds " + label);
        return 0u;
    }
    const auto begin = std::chrono::steady_clock::now();
    const ScriptResult result = harness.execute(source);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin).count();
    const Value* answer = binding(harness, "answer");
    const bool answerOk = expectedAnswer < 0
        ? answer != nullptr && answer->isUndefined()
        : answer != nullptr && answer->isBoolean() &&
            answer->booleanValue() == (expectedAnswer != 0);
    if (!result.succeeded() || !answerOk ||
        numberIs(harness, "calls") != static_cast<double>(expectedCalls)) {
        std::cerr << "  JS78 benchmark failure " << label
            << " status=" << scriptStatusName(result.status)
            << " error=" << runtimeErrorCodeName(result.runtimeError.code)
            << " calls=" << numberIs(harness, "calls") << " expected="
            << expectedCalls << '\n';
        expect(false, "JS78 benchmark workload completes correctly " + label);
    }
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, micros));
}

void testPerformanceComparisons()
{
    const std::string html = capacityHtml();
    const std::pair<const char*, std::size_t> sizes[] = {
        {".absent", 0u}, {".s1", 1u}, {".s16", 16u},
        {".s128", 128u}, {".s512", 512u}, {".s1024", 1024u},
    };
    for (const auto& sizeCase : sizes) {
        const std::size_t targets[] = {0u, sizeCase.second / 2u,
            sizeCase.second == 0u ? 0u : sizeCase.second - 1u};
        const char* labels[] = {"first-false", "middle-false", "last-false"};
        for (std::size_t caseIndex = 0u; caseIndex < 4u; ++caseIndex) {
            const bool allTruthy = caseIndex == 3u;
            const std::size_t target = targets[caseIndex < 3u ? caseIndex : 0u];
            std::vector<std::uint64_t> samples;
            for (std::size_t run = 0u; run < 3u; ++run) {
                const std::string callbackResult = allTruthy ? "true"
                    : "index!==" + std::to_string(target);
                const std::string source =
                    "var calls=0;var answer=rows.every(function(element,index){"
                    "calls++;return " + callbackResult + ";});";
                const std::size_t expectedCalls = allTruthy
                    ? sizeCase.second : std::min(target + 1u, sizeCase.second);
                const int expectedAnswer = allTruthy || sizeCase.second == 0u
                    ? 1 : 0;
                samples.push_back(timeWorkload(html, sizeCase.first, source,
                    expectedCalls, expectedAnswer,
                    std::string(sizeCase.first) + ":" +
                        (allTruthy ? "all-true" : labels[caseIndex])));
            }
            std::cout << "JS78 benchmark size=" << sizeCase.second
                << " workload=" << (allTruthy ? "all-true" : labels[caseIndex])
                << " median_us=" << median(samples) << " range_us="
                << *std::min_element(samples.begin(), samples.end()) << '-'
                << *std::max_element(samples.begin(), samples.end()) << '\n';
        }
        for (const char* mode : {"forEach", "some"}) {
            std::vector<std::uint64_t> samples;
            for (std::size_t run = 0u; run < 3u; ++run) {
                const std::string source = std::string("var calls=0;var answer=rows.") +
                    mode + "(function(){calls++;return false;});";
                const std::size_t expectedCalls = sizeCase.second;
                const int expectedAnswer = mode[0] == 's' ? 0 : -1;
                samples.push_back(timeWorkload(html, sizeCase.first, source,
                    expectedCalls, expectedAnswer,
                    std::string(sizeCase.first) + ":" + mode));
            }
            std::cout << "JS78 benchmark size=" << sizeCase.second
                << " workload=" << mode << " median_us=" << median(samples)
                << " range_us=" << *std::min_element(samples.begin(), samples.end())
                << '-' << *std::max_element(samples.begin(), samples.end()) << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
#if defined(GXOS_JS78_STACK_INSTRUMENTED) && defined(_WIN32)
    if (argc > 1 && std::string(argv[1]) == "--stack-only")
        return runStackProbe() ? 0 : 1;
#endif
    (void)argc;
    (void)argv;
    testHostedFixture();
    testTruthinessArgumentsAndEmptyCollection();
    testShortCircuitAndCapacity();
    testMutationGenerationAndErrors();
    testAdaptersScopesNestingAndRegistry();
    testPerformanceComparisons();
    std::cout << "Navigator JavaScript JS78 checks: " << checks - failures
        << '/' << checks << " passed\n";
    return failures == 0u ? 0 : 1;
}
