#define GXOS_JS75_TEST_MAIN navigator_javascript_js75_compatibility_main
#include "navigator_javascript_js75_test.cpp"
#undef GXOS_JS75_TEST_MAIN

#include <chrono>
#include <numeric>

namespace {

using namespace gxos::javascript;

double readNumber(NavigatorScriptExecutionHarness& harness, const char* name)
{
    const Value* value = harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
    return value != nullptr && value->isNumber() ? value->numberValue() : -1.0;
}

std::string readString(NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
    return value != nullptr && value->isString()
        ? harness.runtime().stringValue(*value) : std::string();
}

void testJs76HostedFixture()
{
    std::ifstream fixtureFile("navigator-smoke/javascript-js76.html",
        std::ios::binary);
    const std::string fixtureHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = fixtureHtml.find("<script>");
    const std::size_t scriptClose = fixtureHtml.find("</script>", scriptOpen);
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = !fixtureHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        harness.loadHtml("file:///js76-hosted-fixture.html", fixtureHtml,
            error) && error == RuntimeErrorCode::None;
    expect(loaded, "JS76 hosted fixture loads in the native harness");
    if (!loaded) return;

    const std::string source = fixtureHtml.substr(scriptOpen + 8u,
        scriptClose - scriptOpen - 8u);
    expect(source.find('<') == std::string::npos,
        "JS76 inline source preserves the hosted HTML-subset integrity guard");
    const auto& capturedScripts = harness.document().scriptSources;
    expect(capturedScripts.size() == 1u && capturedScripts[0] == source,
        "JS76 inline script is captured byte-for-byte");
    if (capturedScripts.size() != 1u || capturedScripts[0] != source) return;

    const ScriptResult executed = harness.execute(capturedScripts[0]);
    const Value* assertions = harness.runtime().lookup(
        SourceView("assertions", 10u));
    const bool initialPassed = executed.succeeded() && assertions != nullptr &&
        assertions->isNumber() && assertions->numberValue() >= 20.0 &&
        readNumber(harness, "failures") == 0.0;
    if (!initialPassed) {
        std::cerr << "  JS76 fixture status=" << scriptStatusName(executed.status)
            << ", error=" << runtimeErrorCodeName(executed.runtimeError.code)
            << ", parser=" << parserErrorCodeName(executed.parserError.code)
            << '@' << executed.parserError.location.line << ':'
            << executed.parserError.location.column << ", assertions="
            << readNumber(harness, "assertions") << ", failures="
            << readNumber(harness, "failures") << ", firstFailureAt="
            << readNumber(harness, "failureAt") << '\n';
    }
    expect(initialPassed,
        "JS76 callback arguments, identity, thisArg, bounded mutation, nesting, adapters, and filtered selectors pass");

    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    error = RuntimeErrorCode::None;
    const bool dispatched = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, error) &&
        error == RuntimeErrorCode::None;
    expect(dispatched, "JS76 nested Event forEach dispatch completes");
    expect(dispatched && readBoolean(harness, "outerResult") &&
        readBoolean(harness, "middleResult") &&
        readBoolean(harness, "innerResult") &&
        readBoolean(harness, "metadataResult") &&
        readString(harness, "eventAIds") == "ea0ea1" &&
        readString(harness, "eventBIds") == "eb0eb1" &&
        readString(harness, "eventCIds") == "ec0",
        "JS76 nested Event dispatch preserves collection arguments, indices, identity, and metadata");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS76 callback and nested Event frames unwind to zero");

    expect(sizeof(NavigatorScriptSelectorDescriptor) == 1918u &&
        NavigatorScriptRelativeSelectorTestAccess::collectionBytes() == 1936u &&
        NavigatorScriptRelativeSelectorTestAccess::collectionBytes() *
            kNavigatorScriptMaxSelectorCollections == 247808u &&
        kDefaultMaxJavaScriptEnvironments >=
            kNavigatorScriptMaxDocumentNodes + 1u,
        "JS76 preserves selector storage and bounds the callback environment pool");
}

void testJs76InvalidCallbacks()
{
    const std::pair<const char*, const char*> invalidCalls[] = {
        {"rows.forEach()", "missing callback"},
        {"rows.forEach(null)", "null callback"},
        {"rows.forEach(123)", "numeric callback"},
        {"rows.forEach('invalid')", "string callback"},
    };
    for (const auto& invalid : invalidCalls) {
        NavigatorScriptExecutionHarness harness;
        if (!loadFixture(harness,
                "<html><body><i class='row'></i></body></html>",
                "JS76 invalid callback fixture loads")) continue;
        const ScriptResult result = harness.execute(
            std::string("var rows=document.querySelectorAll('.row');") +
            invalid.first + ";");
        expect(!result.succeeded() &&
            result.status == ScriptStatus::RuntimeFailure &&
            result.runtimeError.code == RuntimeErrorCode::TypeError,
            (std::string("JS76 ") + invalid.second +
                " reports TypeError").c_str());
        expect(harness.runtime().activeCallFrameCount() == 0u,
            (std::string("JS76 ") + invalid.second +
                " leaves no active call frame").c_str());
    }
}

void testJs76ErrorContainmentAndReturn()
{
    NavigatorScriptExecutionHarness returns;
    if (!loadFixture(returns,
            "<html><body><i class='row'></i><i class='row'></i></body></html>",
            "JS76 return-value fixture loads")) return;
    const ScriptResult returned = returns.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "var result=rows.forEach(function(element,index,list){calls++;"
        "return index+17;});var ok=result===undefined&&calls===2;");
    expect(returned.succeeded() && readBoolean(returns, "ok"),
        "JS76 discards callback return values and returns undefined");

    NavigatorScriptExecutionHarness failure;
    if (!loadFixture(failure,
            "<html><body><i class='row'></i><i class='row'></i></body></html>",
            "JS76 callback failure fixture loads")) return;
    const ScriptResult failed = failure.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "rows.forEach(function(){calls++;notDefined();});");
    expect(!failed.succeeded() &&
        failed.runtimeError.code == RuntimeErrorCode::UnknownIdentifier &&
        readNumber(failure, "calls") == 1.0,
        "JS76 callback failure stops later callbacks and propagates the runtime error");
    expect(failure.runtime().activeCallFrameCount() == 0u,
        "JS76 callback failure unwinds the active evaluator frame");
}

#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
struct Js76GenerationProbe {
    NavigatorScriptExecutionHarness* harness = nullptr;
    bool changed = false;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    HostGenerationId initialGeneration = 0u;
    HostInstanceId initialSerial = 0u;
    HostInstanceId replacementSerial = 0u;
};

void invalidateGenerationAfterCallback(void* context, std::size_t index)
{
    auto* probe = static_cast<Js76GenerationProbe*>(context);
    if (probe == nullptr || probe->harness == nullptr || probe->changed ||
        index != 0u) return;
    probe->changed = probe->harness->invalidateDocumentGeneration(
        probe->error);
    if (probe->changed) {
        probe->harness->document() = gxos::web::parseHtml(
            "file:///js76-generation-replacement.html",
            "<html><body><i id='same' class='row'></i><i class='row'></i>"
            "</body></html>");
        probe->harness->hostAdapter().attachDocument(
            probe->harness->document(), probe->harness->runtime().hostGeneration());
        for (const auto& element : probe->harness->document().structuralElements)
            if (element.id == "same") probe->replacementSerial = element.serial;
    }
}
#endif

void testJs76GenerationBoundary()
{
#ifdef GXOS_RELATIVE_SELECTOR_DIAGNOSTICS
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness,
            "<html><body><i id='same' class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS76 generation-replacement fixture loads")) return;
    Js76GenerationProbe probe;
    probe.harness = &harness;
    probe.initialGeneration = harness.hostAdapter().generation();
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "same") probe.initialSerial = element.serial;
    harness.hostAdapter().setSelectorCollectionForEachProbe(
        invalidateGenerationAfterCallback, &probe);
    const ScriptResult result = harness.execute(
        "var rows=document.querySelectorAll('.row');var calls=0;"
        "rows.forEach(function(){calls++;});");
    harness.hostAdapter().setSelectorCollectionForEachProbe(nullptr, nullptr);
    expect(probe.changed && probe.error == RuntimeErrorCode::None &&
        harness.hostAdapter().generation() != probe.initialGeneration &&
        probe.initialSerial != 0u &&
        probe.replacementSerial == probe.initialSerial,
        "JS76 callback boundary replaces the document and reuses its Element serial");
    expect(!result.succeeded() &&
        result.runtimeError.code == RuntimeErrorCode::StaleHostObject &&
        readNumber(harness, "calls") == 1.0,
        "JS76 stops after callback generation replacement without stale Element reuse");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS76 generation failure unwinds all active frames");
#else
    expect(false, "JS76 generation probe requires native diagnostics");
#endif
}

void testJs76CollectionRegistrySaturation()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness,
            "<html><body><i class='row'></i></body></html>",
            "JS76 registry-saturation fixture loads")) return;

    std::string createCollections;
    for (std::size_t index = 0u;
            index < kNavigatorScriptMaxSelectorCollections; ++index) {
        createCollections += "var rows" + std::to_string(index) +
            "=document.querySelectorAll('.absent" +
            std::to_string(index) + "');";
    }
    createCollections += "var filled=rows0.length===0;";
    const ScriptResult saturated = harness.execute(createCollections);
    expect(saturated.succeeded() && readBoolean(harness, "filled") &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == kNavigatorScriptMaxSelectorCollections,
        "JS76 fills the existing 128-record collection registry");

    const ScriptResult traversed = harness.execute(
        "var done=rows0.forEach(function(){});"
        "var ok=done===undefined&&rows0.length===0;");
    expect(traversed.succeeded() && readBoolean(harness, "ok") &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == kNavigatorScriptMaxSelectorCollections,
        "JS76 traverses a retained collection without registry growth at capacity");

    const ScriptResult overflow = harness.execute(
        "document.querySelectorAll('.absent-overflow');");
    expect(!overflow.succeeded() && overflow.runtimeError.code ==
            RuntimeErrorCode::DocumentLookupLimitExceeded,
        "JS76 preserves the existing selector collection registry limit");
    const ScriptResult preserved = harness.execute(
        "var done=rows0.forEach(function(){});var ok=done===undefined;");
    expect(preserved.succeeded() && readBoolean(harness, "ok") &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == kNavigatorScriptMaxSelectorCollections,
        "JS76 registry saturation does not corrupt existing handles");
}

std::string js76CapacityHtml()
{
    std::string html = "<html class='all'><body class='all'>";
    html += "<div class='all'>";
    for (std::size_t index = 0u; index < 1021u; ++index)
        html += "<i class='all'></i>";
    html += "</div></body></html>";
    return html;
}

void testJs76MaximumCapacity()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness, js76CapacityHtml(),
            "JS76 1,024-Element capacity fixture loads")) return;
    if (harness.document().structuralElements.size() != 1024u) {
        expect(false, "JS76 capacity fixture contains exactly 1,024 Elements");
        return;
    }
    const ScriptResult result = harness.execute(
        "var allRows=document.querySelectorAll('.all');var calls=0;"
        "var lastIndex=-1;var sameCollection=true;"
        "var beforeCollections=allRows.length;"
        "var done=allRows.forEach(function(element,index,collection){"
        "calls++;lastIndex=index;sameCollection=sameCollection&&"
        "collection===allRows;});"
        "var ok=beforeCollections===1024&&calls===1024&&lastIndex===1023&&"
        "sameCollection&&done===undefined;");
    if (!result.succeeded() || !readBoolean(harness, "ok")) {
        std::cerr << "  JS76 capacity status=" << scriptStatusName(result.status)
            << ", error=" << runtimeErrorCodeName(result.runtimeError.code)
            << ", length=" << readNumber(harness, "beforeCollections")
            << ", calls=" << readNumber(harness, "calls")
            << ", lastIndex=" << readNumber(harness, "lastIndex")
            << ", sameCollection=" << readBoolean(harness, "sameCollection")
            << ", activeFrames=" << harness.runtime().activeCallFrameCount()
            << '\n';
    }
    expect(result.succeeded() && readBoolean(harness, "ok"),
        "JS76 completes 1,024 callbacks with correct final index and collection");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS76 1,024 callback frames return to the baseline");
    expect(NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == 1u,
        "JS76 maximum traversal does not allocate collection registry records per callback");
}

std::string js76BenchmarkHtml()
{
    std::string html = "<html class='r1 r16 r128 r512 r1024 selected'>"
        "<body class='r16 r128 r512 r1024 selected'>";
    html += "<i class='r16 r128 r512 r1024 selected'></i>";
    for (std::size_t index = 3u; index < 1024u; ++index) {
        std::string classes;
        for (std::size_t bound : {16u, 128u, 512u, 1024u})
            if (index < bound) classes += "r" + std::to_string(bound) + " ";
        if (index % 2u == 0u) classes += "selected ";
        html += "<i class='" + classes + "'></i>";
    }
    html += "</body></html>";
    return html;
}

struct Js76Timing {
    bool valid = false;
    std::int64_t median = 0;
    std::size_t callbackCount = 0u;
};

Js76Timing measureJs76ForEach(const std::string& selector,
    std::size_t expectedCount)
{
    std::vector<std::int64_t> samples;
    samples.reserve(5u);
    for (std::size_t repeat = 0u; repeat < 5u; ++repeat) {
        NavigatorScriptExecutionHarness harness;
        if (!loadFixture(harness, js76BenchmarkHtml(),
                "JS76 performance fixture loads")) return {};
        const ScriptResult setup = harness.execute(
            "var rows=document.querySelectorAll('" + selector + "');"
            "var callbackCount=0;"
            "function visitForEach(element,index,collection){callbackCount++;}");
        if (!setup.succeeded()) return {};
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(
            "callbackCount=0;var result=rows.forEach(visitForEach);");
        const auto stop = std::chrono::steady_clock::now();
        const double callbackCount = readNumber(harness, "callbackCount");
        if (!result.succeeded() || callbackCount !=
                static_cast<double>(expectedCount) ||
            harness.runtime().activeCallFrameCount() != 0u) {
            std::cerr << "  JS76 timing failure selector=" << selector
                << ", status=" << scriptStatusName(result.status)
                << ", error=" << runtimeErrorCodeName(
                    result.runtimeError.code)
                << ", callbacks=" << callbackCount
                << ", expected=" << expectedCount << ", frames="
                << harness.runtime().activeCallFrameCount() << '\n';
            return {};
        }
        samples.push_back(std::chrono::duration_cast<
            std::chrono::microseconds>(stop - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return Js76Timing{true, samples[samples.size() / 2u], expectedCount};
}

Js76Timing measureJs76ItemLoop(const std::string& selector,
    std::size_t expectedCount)
{
    std::vector<std::int64_t> samples;
    samples.reserve(5u);
    for (std::size_t repeat = 0u; repeat < 5u; ++repeat) {
        NavigatorScriptExecutionHarness harness;
        if (!loadFixture(harness, js76BenchmarkHtml(),
                "JS76 item-loop comparison fixture loads")) return {};
        const ScriptResult setup = harness.execute(
            "var rows=document.querySelectorAll('" + selector + "');"
            "var callbackCount=0;");
        if (!setup.succeeded()) return {};
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(
            "callbackCount=0;var itemLength=rows.length;"
            "for(var index=0;index<itemLength;index++)"
            "if(rows.item(index)!==null)callbackCount++;");
        const auto stop = std::chrono::steady_clock::now();
        const double callbackCount = readNumber(harness, "callbackCount");
        if (!result.succeeded() || callbackCount !=
                static_cast<double>(expectedCount) ||
            harness.runtime().activeCallFrameCount() != 0u) {
            std::cerr << "  JS76 item-loop timing failure selector="
                << selector << ", status=" << scriptStatusName(result.status)
                << ", error=" << runtimeErrorCodeName(
                    result.runtimeError.code)
                << ", items=" << callbackCount << ", expected="
                << expectedCount << ", frames="
                << harness.runtime().activeCallFrameCount() << '\n';
            return {};
        }
        samples.push_back(std::chrono::duration_cast<
            std::chrono::microseconds>(stop - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return Js76Timing{true, samples[samples.size() / 2u], expectedCount};
}

void testJs76Performance()
{
    const std::pair<const char*, std::size_t> cases[] = {
        {".absent", 0u}, {".r1", 1u}, {".r16", 16u},
        {".r128", 128u}, {".r512", 512u}, {".r1024", 1024u},
    };
    for (const auto& testCase : cases) {
        const Js76Timing timing = measureJs76ForEach(testCase.first,
            testCase.second);
        expect(timing.valid,
            "JS76 comparable iteration timing completes at requested size");
        std::cout << "JS76 timing selector=" << testCase.first
            << " callbacks=" << timing.callbackCount << " median-us="
            << timing.median << " repetitions=5; lookup strategy=one initial "
            << "length plus one live indexed lookup per bounded index\n";
    }
    const Js76Timing filtered = measureJs76ForEach(
        ".selected:nth-child(odd of .selected)", 257u);
    expect(filtered.valid,
        "JS76 filtered positional iteration timing completes");
    std::cout << "JS76 timing selector=filtered-positional callbacks="
        << filtered.callbackCount << " median-us=" << filtered.median
        << " repetitions=5; rank scratch is rebuilt within each live index read\n";

    for (const auto& comparison : {
            std::pair<const char*, std::size_t>{".r1024", 1024u},
            std::pair<const char*, std::size_t>{
                ".selected:nth-child(odd of .selected)", 257u}}) {
        const Js76Timing forEach = measureJs76ForEach(comparison.first,
            comparison.second);
        const Js76Timing itemLoop = measureJs76ItemLoop(comparison.first,
            comparison.second);
        expect(forEach.valid && itemLoop.valid,
            "JS76 forEach and item-loop comparison timings complete");
        std::cout << "JS76 comparable timing selector=" << comparison.first
            << " indexed-reads=" << comparison.second
            << " forEach-median-us=" << forEach.median
            << " item-loop-median-us=" << itemLoop.median
            << " repetitions=5; both capture length once and perform one live indexed lookup per index\n";
    }
}

void testJs76CollectionRegistryReuse()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness,
            "<html><body><i class='row'></i><i class='row'></i></body></html>",
            "JS76 registry-reuse fixture loads")) return;
    const ScriptResult setup = harness.execute(
        "var rows=document.querySelectorAll('.row');var saved=rows.forEach;"
        "var methodIdentity=saved===rows.forEach;");
    expect(setup.succeeded() && readBoolean(harness, "methodIdentity"),
        "JS76 collection method identity is stable");
    const std::size_t collectionsBefore =
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter());
    const std::size_t methodsBefore = harness.runtime().hostMethodCount();
    const ScriptResult traversed = harness.execute(
        "for(var pass=0;pass<8;pass++)rows.forEach(function(){});");
    expect(traversed.succeeded() &&
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == collectionsBefore,
        "JS76 repeated traversal reuses its retained collection record");
    expect(harness.runtime().hostMethodCount() == methodsBefore,
        "JS76 repeated method reads reuse the cached host method value");
}

#if defined(GXOS_JS74_STACK_INSTRUMENTED) && defined(_WIN32)
bool measureJs76NestedEventStack(StackProbeResult& result,
    NavigatorScriptExecutionHarness& harness, HostInstanceId targetSerial,
    const char* label)
{
    volatile std::uint8_t marker = 0u;
    const std::uintptr_t markerAddress =
        reinterpret_cast<std::uintptr_t>(&marker);
    if (!recordSample(result, markerAddress)) return false;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool dispatched = harness.dispatchClick(targetSerial, error);
    const bool passed = dispatched && error == RuntimeErrorCode::None &&
        readBoolean(harness, "ok");
    finishSample(result, label, passed);
    return passed;
}

bool runJs76StackWorkloads(StackProbeResult& result)
{
    NavigatorScriptExecutionHarness ordinary;
    if (!loadStackFixture(ordinary,
            "<html><body><i class='row'></i><i class='row'></i>"
            "<i class='row'></i><i class='late'></i>"
            "<i id='candidate'></i></body></html>") ||
        !ordinary.execute("var rows=document.querySelectorAll('.row');"
            "var calls=0;var ok=true;").succeeded()) return false;
    bool passed = measureScript(result, ordinary, "foreach-ordinary",
        "calls=0;var done=rows.forEach(function(element,index,list){"
        "calls++;ok=ok&&index>=0&&list===rows;});"
        "ok=ok&&calls===3&&done===undefined;");
    passed = measureScript(result, ordinary, "foreach-filtered",
        "var filtered=document.querySelectorAll("
        "'.row:nth-child(odd of .row)');var count=0;"
        "filtered.forEach(function(){count++;});var ok=count===2;") && passed;
    passed = measureScript(result, ordinary, "foreach-nested",
        "var outer=document.querySelectorAll('.row');var inner=outer;"
        "var count=0;outer.forEach(function(){inner.forEach(function(){"
        "count++;});});var ok=count===9;") && passed;
    passed = measureScript(result, ordinary, "foreach-mutation-nested-query",
        "var target=document.querySelectorAll('.late');var candidate="
        "document.querySelector('#candidate');var count=0;"
        "target.forEach(function(element,index,list){count++;"
        "candidate.setAttribute('class','late');"
        "var nested=document.querySelectorAll('.late');"
        "ok=list===target&&nested.length===2;});"
        "var ok=ok&&count===1&&target.length===2;") && passed;

    NavigatorScriptExecutionHarness event;
    if (!loadStackFixture(event,
            "<html><body><button id='event-a' type='button'>A</button>"
            "<i class='event-row'></i><i class='event-row'></i></body></html>") ||
        !event.execute("var rows=document.querySelectorAll('.event-row');"
            "var calls=0;var ok=false;"
            "document.getElementById('event-a').addEventListener('click',"
            "function(){rows.forEach(function(){calls++;});"
            "ok=calls===2;});").succeeded()) return false;
    HostInstanceId eventTarget = 0u;
    for (const auto& element : event.document().structuralElements)
        if (element.id == "event-a") eventTarget = element.serial;
    if (eventTarget == 0u) return false;
    passed = measureJs76NestedEventStack(result, event, eventTarget,
        "foreach-from-event") && passed;

    constexpr std::size_t nestedEventDepth = 16u;
    NavigatorScriptExecutionHarness nestedEvents;
    std::string nestedHtml = "<html><body>";
    std::string nestedSource = "var ok=false;";
    for (std::size_t index = 0u; index < nestedEventDepth; ++index) {
        const std::string suffix = std::to_string(index);
        nestedHtml += "<button id='event-" + suffix +
            "' type='button'>Nested event</button><i class='rows-" +
            suffix + "'></i>";
        nestedSource += "var event" + suffix +
            "=document.getElementById('event-" + suffix +
            "');event" + suffix + ".addEventListener('click',function(){";
        nestedSource += "document.querySelectorAll('.rows-" + suffix +
            "').forEach(function(){});";
        if (index + 1u < nestedEventDepth)
            nestedSource += "event" + std::to_string(index + 1u) + ".click();";
        else
            nestedSource += "ok=true;";
        nestedSource += "});";
    }
    nestedHtml += "</body></html>";
    if (!loadStackFixture(nestedEvents, nestedHtml) ||
        !nestedEvents.execute(nestedSource).succeeded()) return false;
    eventTarget = 0u;
    for (const auto& element : nestedEvents.document().structuralElements)
        if (element.id == "event-0") eventTarget = element.serial;
    if (eventTarget == 0u) return false;
    passed = measureJs76NestedEventStack(result, nestedEvents, eventTarget,
        "nested-events-depth16-with-foreach") && passed;

    NavigatorScriptExecutionHarness capacity;
    if (!loadStackFixture(capacity, maximumCapacityHtml()) ||
        capacity.document().structuralElements.size() != 1024u ||
        !capacity.execute("var rows=document.querySelectorAll('*');"
            "var calls=0;var ok=false;").succeeded()) return false;
    passed = measureScript(result, capacity, "foreach-capacity-1024",
        "calls=0;rows.forEach(function(element,index){calls++;"
        "ok=index===1023||ok;});ok=ok&&calls===1024;") && passed;
    return passed && result.passed;
}

DWORD WINAPI js76StackProbeThread(void* parameter)
{
    auto* result = static_cast<StackProbeResult*>(parameter);
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    result->stackBase = reinterpret_cast<std::uintptr_t>(tib->StackBase);
    result->initialStackLimit =
        reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    result->passed = result->stackBase > result->initialStackLimit &&
        runJs76StackWorkloads(*result);
    return result->passed ? 0u : 1u;
}

bool runJs76StackProbe()
{
    StackProbeResult result;
    DWORD threadId = 0u;
    HANDLE thread = CreateThread(nullptr, StackProbeResult::kReserveBytes,
        js76StackProbeThread, &result, STACK_SIZE_PARAM_IS_A_RESERVATION,
        &threadId);
    if (thread == nullptr) {
        std::cerr << "JS76 stack probe CreateThread failed, error="
            << GetLastError() << '\n';
        return false;
    }
    const DWORD wait = WaitForSingleObject(thread, INFINITE);
    DWORD exitCode = 1u;
    const bool exitRead = GetExitCodeThread(thread, &exitCode) != 0;
    CloseHandle(thread);
    std::size_t maximum = 0u;
    for (std::size_t index = 0u; index < result.sampleCount; ++index) {
        const StackSample& sample = result.samples[index];
        maximum = std::max(maximum, sample.bytes);
        std::cout << "JS76 stack sample " << sample.label << ": observed="
            << sample.bytes << " B; "
            << (sample.passed ? "PASS" : "FAIL") << '\n';
    }
    std::cout << "JS76 stack maximum instrumented=" << maximum
        << " B; maximum TEB committed=" << result.maximumCommittedBytes
        << " B / " << StackProbeResult::kReserveBytes
        << " B; status="
        << (result.passed && wait == WAIT_OBJECT_0 && exitRead && exitCode == 0u
            ? "PASS" : "FAIL") << '\n';
    return result.passed && wait == WAIT_OBJECT_0 && exitRead && exitCode == 0u &&
        maximum < StackProbeResult::kReserveBytes;
}
#endif

int runJs76Tests()
{
    const std::size_t start = checks;
    const std::size_t failureStart = failures;
    testJs76HostedFixture();
    testJs76InvalidCallbacks();
    testJs76ErrorContainmentAndReturn();
    testJs76GenerationBoundary();
    testJs76CollectionRegistryReuse();
    testJs76CollectionRegistrySaturation();
    testJs76MaximumCapacity();
    testJs76Performance();
    const std::size_t total = checks - start;
    const std::size_t passed = total - (failures - failureStart);
    std::cout << "Navigator JavaScript JS76 checks: " << passed << '/'
        << total << " passed\n";
    return failures == 0u ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
#if defined(GXOS_JS74_STACK_INSTRUMENTED) && defined(_WIN32)
    if (argc > 1 && std::string(argv[1]) == "--stack-only")
        return runJs76StackProbe() ? 0 : 1;
#endif
    char compatibilityArg0[] = "navigator_javascript_js75_compatibility";
    char compatibilityArg1[] = "--no-bench";
    char* compatibilityArgs[] = {compatibilityArg0, compatibilityArg1};
    if (navigator_javascript_js75_compatibility_main(2,
            compatibilityArgs) != 0)
        return 1;
    (void)argc;
    (void)argv;
    return runJs76Tests();
}
