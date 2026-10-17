#define GXOS_JS74_TEST_MAIN navigator_javascript_js74_compatibility_main
#include "navigator_javascript_js74_test.cpp"
#undef GXOS_JS74_TEST_MAIN

#include <cmath>
#include <limits>
#include <numeric>

namespace {

using namespace gxos::javascript;

bool readBoolean(NavigatorScriptExecutionHarness& harness, const char* name)
{
    const Value* value = harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
    return value != nullptr && value->isBoolean() && value->booleanValue();
}

void testJs75HostedFixture()
{
    std::ifstream fixtureFile("navigator-smoke/javascript-js75.html",
        std::ios::binary);
    const std::string fixtureHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = fixtureHtml.find("<script>");
    const std::size_t scriptClose = fixtureHtml.find("</script>", scriptOpen);
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = !fixtureHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        harness.loadHtml("file:///js75-hosted-fixture.html", fixtureHtml,
            error) && error == RuntimeErrorCode::None;
    expect(loaded, "JS75 hosted fixture loads in the native harness");
    if (!loaded) return;

    const std::string source = fixtureHtml.substr(scriptOpen + 8u,
        scriptClose - scriptOpen - 8u);
    const auto& capturedScripts = harness.document().scriptSources;
    expect(capturedScripts.size() == 1u && capturedScripts[0] == source,
        "JS75 inline script is preserved byte-for-byte");
    expect(source.find('<') == std::string::npos,
        "JS75 inline script avoids the hosted HTML subset terminator byte");
    if (capturedScripts.size() != 1u || capturedScripts[0] != source) return;

    const ScriptResult result = harness.execute(capturedScripts[0]);
    const Value* assertions = harness.runtime().lookup(
        SourceView("assertions", 10u));
    const Value* fixtureFailures = harness.runtime().lookup(
        SourceView("failures", 8u));
    const Value* fixtureFailureAt = harness.runtime().lookup(
        SourceView("failureAt", 9u));
    const bool scriptPassed = result.succeeded() && assertions != nullptr &&
        assertions->isNumber() && assertions->numberValue() >= 30.0 &&
        fixtureFailures != nullptr && fixtureFailures->isNumber() &&
        fixtureFailures->numberValue() == 0.0;
    if (!scriptPassed) {
        std::cerr << "  JS75 fixture status=" << scriptStatusName(result.status)
            << ", error=" << runtimeErrorCodeName(result.runtimeError.code)
            << ", parser=" << parserErrorCodeName(result.parserError.code)
            << '@' << result.parserError.location.line << ':'
            << result.parserError.location.column
            << ", assertions=";
        if (assertions != nullptr && assertions->isNumber())
            std::cerr << assertions->numberValue();
        else std::cerr << "missing";
        std::cerr << ", failures=";
        if (fixtureFailures != nullptr && fixtureFailures->isNumber())
            std::cerr << fixtureFailures->numberValue();
        else std::cerr << "missing";
        std::cerr << ", firstFailureAt=";
        if (fixtureFailureAt != nullptr && fixtureFailureAt->isNumber())
            std::cerr << fixtureFailureAt->numberValue();
        else std::cerr << "missing";
        std::cerr << '\n';
    }
    expect(scriptPassed,
        "JS75 hosted initial API, conversion, identity, scope, and live tests pass");

    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    error = RuntimeErrorCode::None;
    const bool dispatched = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, error) &&
        error == RuntimeErrorCode::None;
    expect(dispatched, "JS75 three-level nested Event dispatch completes");
    expect(dispatched && readBoolean(harness, "outerResult") &&
        readBoolean(harness, "middleResult") &&
        readBoolean(harness, "innerResult") &&
        readBoolean(harness, "metadataResult"),
        "JS75 nested item reads preserve scope, mutation, and Event metadata");

    expect(sizeof(NavigatorScriptSelectorDescriptor) == 1918u &&
        NavigatorScriptRelativeSelectorTestAccess::collectionBytes() == 1936u &&
        NavigatorScriptRelativeSelectorTestAccess::collectionBytes() *
            kNavigatorScriptMaxSelectorCollections == 247808u,
        "JS75 preserves selector descriptor, collection, and registry sizes");
}

void testJs75MissingArgument()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness,
            "<html><body><i class='row'></i></body></html>",
            "JS75 missing-argument fixture loads")) return;
    const ScriptResult result = harness.execute(
        "var rows=document.querySelectorAll('.row');rows.item();");
    expect(!result.succeeded() && result.status == ScriptStatus::RuntimeFailure &&
        result.runtimeError.code == RuntimeErrorCode::TypeError,
        "JS75 omitted required item argument reports TypeError");
}

void testJs75ReceiverSafety()
{
    NavigatorScriptExecutionHarness detached;
    if (!loadFixture(detached,
            "<html><body><i class='row'></i></body></html>",
            "JS75 detached-method fixture loads")) return;
    const ScriptResult detachedResult = detached.execute(
        "var rows=document.querySelectorAll('.row');"
        "var saved=rows.item;saved(0);");
    expect(!detachedResult.succeeded() &&
        detachedResult.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "JS75 detached method cannot read an unrelated collection");

    NavigatorScriptExecutionHarness objectArgument;
    if (!loadFixture(objectArgument,
            "<html><body><i class='row'></i></body></html>",
            "JS75 object-conversion fixture loads")) return;
    const ScriptResult objectResult = objectArgument.execute(
        "var rows=document.querySelectorAll('.row');rows.item({});");
    expect(!objectResult.succeeded() &&
        objectResult.runtimeError.code == RuntimeErrorCode::HostInvalidValue,
        "JS75 unsupported object ToPrimitive conversion fails closed");
}

void testJs75MethodAndCollectionCaching()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness,
            "<html><body><i class='row'></i><i class='row'></i></body></html>",
            "JS75 method-cache fixture loads")) return;
    const ScriptResult initial = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var ok=rows.item(0)===rows[0];");
    expect(initial.succeeded() && readBoolean(harness, "ok"),
        "JS75 cached method is callable on its collection");
    const std::size_t initialCollections =
        NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter());
    const std::size_t initialMethods = harness.runtime().hostMethodCount();
    expect(initialCollections == 1u,
        "JS75 initial method access retains one selector collection");
    runScript(harness,
        "var saved=rows.item;var ok=saved===rows.item;"
        "for(var pass=0;pass<8;pass++){ok=ok&&rows.item(pass%2)===rows[pass%2];}",
        "JS75 repeated method reads preserve function identity and Elements");
    expect(NavigatorScriptRelativeSelectorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == initialCollections,
        "JS75 item invocations do not allocate collection records");
    expect(harness.runtime().hostMethodCount() == initialMethods,
        "JS75 repeated item property reads reuse one native method value");
}

void testJs75GenerationSafety()
{
    const std::string html =
        "<html><body><i id='same' class='row'>Old</i></body></html>";
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness, html,
            "JS75 generation fixture loads")) return;
    const ScriptResult initial = harness.execute(
        "var oldRows=document.querySelectorAll('.row');"
        "var oldElement=oldRows.item(0);var ok=oldElement.id==='same';");
    expect(initial.succeeded() && readBoolean(harness, "ok"),
        "JS75 collection item returns the initial generation Element");
    const Value* oldRowsValue = harness.runtime().lookup(
        SourceView("oldRows", 7u));
    const RuntimeHostObjectId oldRowsObject = oldRowsValue != nullptr &&
        oldRowsValue->isHostObject() ? oldRowsValue->hostObjectId() :
            kInvalidRuntimeHostObjectId;

    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool replaced = harness.replaceHtml("file:///js75-replacement.html",
        "<html><body><i id='same' class='row'>New</i></body></html>", error) &&
        error == RuntimeErrorCode::None;
    expect(replaced, "JS75 document replacement reuses the structural serial");
    Value staleValue;
    RuntimeErrorCode staleError = RuntimeErrorCode::None;
    const bool staleRead = oldRowsObject != kInvalidRuntimeHostObjectId &&
        harness.runtime().readHostPropertyForTesting(oldRowsObject, "item",
            staleValue, staleError);
    expect(!staleRead && staleError == RuntimeErrorCode::StaleHostObject,
        "JS75 retained collection item cannot cross a generation boundary");
    const ScriptResult fresh = harness.execute(
        "var freshRows=document.querySelectorAll('.row');"
        "var ok=freshRows.item(0).textContent==='New';");
    expect(fresh.succeeded() && readBoolean(harness, "ok"),
        "JS75 new collection reads the replacement document");
}

void testJs75IndexConversionBoundaries()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness,
            "<html><body><i id='a' class='row'></i>"
            "<i id='b' class='row'></i><i id='c' class='row'></i>"
            "<i id='d' class='row'></i></body></html>",
            "JS75 conversion boundary fixture loads")) return;
    const ScriptResult initialized = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var ok=rows.item(0)===rows[0];");
    expect(initialized.succeeded() && readBoolean(harness, "ok"),
        "JS75 conversion boundary collection is live");
    const HostObjectReference receiver{1u,
        harness.hostAdapter().generation(), kNavigatorSelectorCollectionHostKind};
    const auto expectNumber = [&](double value, HostInstanceId expectedSerial,
            bool expectedNull, const char* label) {
        const HostValue argument = HostValue::number(value);
        HostValue result;
        const HostResult call = harness.hostAdapter().call(&receiver,
            kNavigatorSelectorCollectionItemMethod, &argument, 1u, result);
        const bool passed = call.succeeded() &&
            (expectedNull ? result.type == HostValueType::Null :
                result.type == HostValueType::HostObject &&
                result.hostObject.instanceId == expectedSerial);
        expect(passed, label);
    };
    HostInstanceId serials[4u] = {};
    std::size_t serialCount = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "a") serials[0u] = element.serial;
        else if (element.id == "b") serials[1u] = element.serial;
        else if (element.id == "c") serials[2u] = element.serial;
        else if (element.id == "d") serials[3u] = element.serial;
    }
    for (HostInstanceId serial : serials) if (serial != 0u) ++serialCount;
    expect(serialCount == 4u,
        "JS75 boundary fixture has four canonical Element serials");
    expectNumber(-0.0, serials[0], false,
        "JS75 negative zero converts to index zero");
    expectNumber(1.9, serials[1], false,
        "JS75 fractional positive index truncates toward zero");
    expectNumber(-1.9, 0u, true,
        "JS75 negative fractional index wraps unsigned and misses");
    expectNumber(4294967296.0, serials[0], false,
        "JS75 unsigned-long modulo boundary maps to zero");
    expectNumber(4294967297.0, serials[1], false,
        "JS75 value above unsigned-long boundary maps modulo 2^32");
    expectNumber(-4294967296.0, serials[0], false,
        "JS75 negative modulo boundary maps to zero");
    expectNumber(-4294967297.0, 0u, true,
        "JS75 negative value below modulo boundary misses without wrapping");
    expectNumber((std::numeric_limits<double>::max)(), serials[0u], false,
        "JS75 largest finite double converts modulo without integer overflow");
    expectNumber(-(std::numeric_limits<double>::max)(), serials[0u], false,
        "JS75 negative finite double converts modulo without integer overflow");
    expectNumber(std::numeric_limits<double>::quiet_NaN(), serials[0], false,
        "JS75 NaN converts to index zero");
    expectNumber(std::numeric_limits<double>::infinity(), serials[0], false,
        "JS75 positive infinity converts to index zero");
    expectNumber(-std::numeric_limits<double>::infinity(), serials[0], false,
        "JS75 negative infinity converts to index zero");
}

struct Js75Timing {
    bool valid = false;
    std::int64_t median = 0;
    std::int64_t minimum = 0;
    std::int64_t maximum = 0;
};

Js75Timing measureJs75Reads(const std::string& html,
    const std::string& selector, const std::string& accessor,
    std::size_t index, std::size_t matchCount)
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    if (!harness.loadHtml("file:///js75-benchmark.html", html, error) ||
        error != RuntimeErrorCode::None) return {};
    const bool inRange = index < matchCount;
    const std::string value = (accessor == "item" ? "rows.item(" : "rows[") +
        std::to_string(index) + (accessor == "item" ? ")" : "]");
    const std::string expected = inRange ? "rows[" +
        std::to_string(index) + "]" :
        accessor == "item" ? "null" : "undefined";
    const std::string source = "var rows=document.querySelectorAll('" +
        selector + "');var ok=true;for(var pass=0;pass<8;pass++){ok=ok&&" +
        value + "===" + expected + ";}";
    std::vector<std::int64_t> times;
    times.reserve(5u);
    for (std::size_t repeat = 0u; repeat < 5u; ++repeat) {
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(source);
        const auto stop = std::chrono::steady_clock::now();
        if (!result.succeeded() || !readBoolean(harness, "ok")) {
            std::cerr << "  JS75 timing execution failed accessor="
                << accessor << " selector=" << selector << " index="
                << index << ", status=" << scriptStatusName(result.status)
                << ", error="
                << runtimeErrorCodeName(result.runtimeError.code) << '\n';
            return {};
        }
        times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
            stop - start).count());
    }
    std::sort(times.begin(), times.end());
    return Js75Timing{true, times[times.size() / 2u], times.front(),
        times.back()};
}

Js75Timing measureJs75SequentialReads(const std::string& html,
    const char* accessor)
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    if (!harness.loadHtml("file:///js75-sequential-benchmark.html", html,
            error) || error != RuntimeErrorCode::None) return {};
    const std::string expression = accessor == std::string("item")
        ? "rows.item(pass)" : "rows[pass]";
    const std::string source = "var rows=document.querySelectorAll('.selected');"
        "var ok=true;for(var pass=0;pass<16;pass++){ok=ok&&" + expression +
        "===rows[pass];}";
    std::vector<std::int64_t> times;
    times.reserve(5u);
    for (std::size_t repeat = 0u; repeat < 5u; ++repeat) {
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(source);
        const auto stop = std::chrono::steady_clock::now();
        if (!result.succeeded() || !readBoolean(harness, "ok")) return {};
        times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
            stop - start).count());
    }
    std::sort(times.begin(), times.end());
    return Js75Timing{true, times[times.size() / 2u], times.front(),
        times.back()};
}

Js75Timing measureJs75NestedEventReads(const char* accessor)
{
    const std::string html =
        "<html><body><section id='root-a'><i id='item-a' class='pick'></i>"
        "<button id='event-a'>A</button></section>"
        "<section id='root-b'><i id='item-b' class='pick'></i>"
        "<button id='event-b'>B</button></section>"
        "<section id='root-c'><i id='item-c' class='pick'></i>"
        "<button id='event-c'>C</button></section></body></html>";
    const std::string itemRead = accessor == std::string("item")
        ? "rows.item(0)" : "rows[0]";
    const std::string source =
        "var rootA=document.getElementById('root-a');"
        "var rootB=document.getElementById('root-b');"
        "var rootC=document.getElementById('root-c');"
        "var itemA=document.getElementById('item-a');"
        "var itemB=document.getElementById('item-b');"
        "var itemC=document.getElementById('item-c');"
        "var eventA=document.getElementById('event-a');"
        "var eventB=document.getElementById('event-b');"
        "var eventC=document.getElementById('event-c');"
        "var okA=false;var okB=false;var okC=false;"
        "eventA.addEventListener('click',function(event){"
        "var rows=rootA.querySelectorAll('.pick');"
        "var before=" + itemRead + "===itemA;eventB.click();"
        "okA=before&&" + itemRead + "===itemA;});"
        "eventB.addEventListener('click',function(event){"
        "var rows=rootB.querySelectorAll('.pick');"
        "var before=" + itemRead + "===itemB;eventC.click();"
        "okB=before&&" + itemRead + "===itemB;});"
        "eventC.addEventListener('click',function(event){"
        "var rows=rootC.querySelectorAll('.pick');"
        "okC=" + itemRead + "===itemC;});";
    std::vector<std::int64_t> times;
    times.reserve(5u);
    for (std::size_t repeat = 0u; repeat < 5u; ++repeat) {
        NavigatorScriptExecutionHarness harness;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!harness.loadHtml("file:///js75-event-benchmark.html", html,
                error) || error != RuntimeErrorCode::None) return {};
        const ScriptResult setup = harness.execute(source);
        if (!setup.succeeded()) return {};
        HostInstanceId targetSerial = 0u;
        for (const auto& element : harness.document().structuralElements)
            if (element.id == "event-a") targetSerial = element.serial;
        if (targetSerial == 0u) return {};
        const auto start = std::chrono::steady_clock::now();
        const bool dispatched = harness.dispatchClick(targetSerial, error);
        const auto stop = std::chrono::steady_clock::now();
        if (!dispatched || error != RuntimeErrorCode::None ||
            !readBoolean(harness, "okA") || !readBoolean(harness, "okB") ||
            !readBoolean(harness, "okC")) return {};
        times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
            stop - start).count());
    }
    std::sort(times.begin(), times.end());
    return Js75Timing{true, times[times.size() / 2u], times.front(),
        times.back()};
}

void testJs75ReadPerformance()
{
    const std::string html = sharedParentHtml("alternating");
    const std::size_t indices[] = {0u, 255u, 510u, 511u, 512u};
    for (std::size_t index : indices) {
        for (const char* accessor : {"bracket", "item"}) {
            const Js75Timing timing = measureJs75Reads(html, ".selected",
                accessor, index, 511u);
            expect(timing.valid,
                "JS75 comparable indexed-read timing completes");
            std::cout << "JS75 timing accessor=" << accessor << " index="
                << index << " median-us=" << timing.median << " min-us="
                << timing.minimum << " max-us=" << timing.maximum
                << " repetitions=5\n";
        }
    }
    for (const char* accessor : {"bracket", "item"}) {
        const Js75Timing timing = measureJs75Reads(html,
            ":nth-child(odd of .selected)", accessor, 255u, 256u);
        expect(timing.valid,
            "JS75 filtered positional indexed-read timing completes");
        std::cout << "JS75 timing accessor=" << accessor
            << " filtered-index=255 median-us=" << timing.median
            << " min-us=" << timing.minimum << " max-us=" << timing.maximum
            << " repetitions=5\n";
    }
    for (const char* accessor : {"bracket", "item"}) {
        for (const auto& sample : {
                std::pair<const char*, std::size_t>{".missing", 0u},
                std::pair<const char*, std::size_t>{"#s0", 1u}}) {
            const Js75Timing timing = measureJs75Reads(html, sample.first,
                accessor, 0u, sample.second);
            expect(timing.valid,
                "JS75 empty and single-result indexed-read timings complete");
            std::cout << "JS75 timing accessor=" << accessor << " selector="
                << sample.first << " index=0 median-us=" << timing.median
                << " min-us=" << timing.minimum << " max-us="
                << timing.maximum << " repetitions=5\n";
        }
    }
    const std::string manyParents = manyParentHtml();
    for (const char* accessor : {"bracket", "item"}) {
        const Js75Timing timing = measureJs75Reads(manyParents, ".selected",
            accessor, 127u, 255u);
        expect(timing.valid,
            "JS75 many-parent indexed-read timing completes");
        std::cout << "JS75 timing accessor=" << accessor
            << " many-parents-index=127 median-us=" << timing.median
            << " min-us=" << timing.minimum << " max-us=" << timing.maximum
            << " repetitions=5\n";
        const Js75Timing sequential = measureJs75SequentialReads(html,
            accessor);
        expect(sequential.valid,
            "JS75 sequential indexed-read timing completes");
        std::cout << "JS75 timing accessor=" << accessor
            << " sequential-reads=16 median-us=" << sequential.median
            << " min-us=" << sequential.minimum << " max-us="
            << sequential.maximum << " repetitions=5\n";
        const Js75Timing eventTiming = measureJs75NestedEventReads(accessor);
        expect(eventTiming.valid,
            "JS75 nested Event indexed-read timing completes");
        std::cout << "JS75 timing accessor=" << accessor
            << " nested-event-depth=3 median-us=" << eventTiming.median
            << " min-us=" << eventTiming.minimum << " max-us="
            << eventTiming.maximum << " repetitions=5\n";
    }
}

} // namespace

#ifdef GXOS_JS75_TEST_MAIN
int GXOS_JS75_TEST_MAIN(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
#if defined(GXOS_JS74_STACK_INSTRUMENTED) && defined(_WIN32)
    if (argc > 1 && std::string(argv[1]) == "--stack-only")
        return runStackProbe() ? 0 : 1;
#endif
    testSemantics();
    testCompactScratchSlots();
    testHostedFixtureSourceIntegrity();
    testMaximumCapacityAndCounters();
    testRepeatedIndexedReadCounters();
    testMaximumCapacityVariants();
    testInvalidStructuresAndGenerationReuse();
    std::cout << "JS73 inherited compatibility checks passed\n";

    const std::size_t start = checks;
    const std::size_t failureStart = failures;
    testJs75HostedFixture();
    testJs75MissingArgument();
    testJs75ReceiverSafety();
    testJs75MethodAndCollectionCaching();
    testJs75GenerationSafety();
    testJs75IndexConversionBoundaries();
    if (argc <= 1 || std::string(argv[1]) != "--no-bench")
        testJs75ReadPerformance();
    const std::size_t total = checks - start;
    const std::size_t passed = total - (failures - failureStart);
    std::cout << "Navigator JavaScript JS75 checks: " << passed << '/'
        << total << " passed\n";
    return failures == 0u ? 0 : 1;
}
