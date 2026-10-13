#if defined(GXOS_JS74_STACK_INSTRUMENTED) && defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <winternl.h>

#include <cstdint>
#include <limits>

namespace {

thread_local bool g_js74StackSampleActive = false;
thread_local std::uintptr_t g_js74LowestStackAddress =
    (std::numeric_limits<std::uintptr_t>::max)();

} // namespace

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void*, void*)
{
    if (!g_js74StackSampleActive) return;
    volatile std::uint8_t marker = 0u;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&marker);
    if (address < g_js74LowestStackAddress)
        g_js74LowestStackAddress = address;
}

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void*, void*)
{
}
#endif

#define main navigator_javascript_js73_compatibility_main
#include "navigator_javascript_js73_test.cpp"
#undef main

#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kJs74NestedEventDepth = 16u;

bool scriptSucceeded(gxos::javascript::NavigatorScriptExecutionHarness& harness,
    const std::string& source)
{
    const gxos::javascript::ScriptResult result = harness.execute(source);
    const gxos::javascript::Value* value = harness.runtime().lookup(
        gxos::javascript::SourceView("ok", 2u));
    return result.succeeded() && value != nullptr && value->isBoolean() &&
        value->booleanValue();
}

bool loadStackFixture(gxos::javascript::NavigatorScriptExecutionHarness& harness,
    const std::string& html)
{
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    return harness.loadHtml("file:///js74-stack-workload.html", html, error) &&
        error == gxos::javascript::RuntimeErrorCode::None;
}

std::string maximumCapacityHtml(bool sparse = false)
{
    std::string html = "<html><body><div id='load'>";
    html += "<li id='s0' class='selected'><i id='i0' class='selected'></i></li>";
    for (std::size_t index = 1u; index < 1020u; ++index) {
        html += "<li id='s" + std::to_string(index) + "'";
        if (!sparse || index % 2u == 0u) html += " class='selected'";
        html += "></li>";
    }
    html += "</div></body></html>";
    return html;
}

std::string eightSlotSelector()
{
    const std::string member =
        ":nth-child(1 of .selected)>:nth-child(1 of .selected)";
    return member + "," + member + "," + member + "," + member;
}

std::string fourSlotSelector()
{
    return "#load > :nth-child(1 of .selected),"
        "#load > :nth-child(2 of .selected),"
        "#load > :nth-child(3 of .selected),"
        "#load > :nth-child(4 of .selected)";
}

std::string nestedEventHtml(std::size_t depth)
{
    std::string html = "<html><body>";
    for (std::size_t index = 0u; index < depth; ++index) {
        html += "<div id='root-" + std::to_string(index) + "'>";
        html += "<i id='item-" + std::to_string(index) + "' class='pick-" +
            std::to_string(index) + "'></i>";
        html += "<i id='other-" + std::to_string(index) + "'></i>";
        html += "<button id='button-" + std::to_string(index) + "'></button>";
        html += "</div>";
    }
    html += "</body></html>";
    return html;
}

std::string nestedEventSetup(std::size_t depth)
{
    std::string source;
    for (std::size_t index = 0u; index < depth; ++index) {
        source += "var root" + std::to_string(index) +
            "=document.getElementById('root-" + std::to_string(index) +
            "');var item" + std::to_string(index) +
            "=document.getElementById('item-" + std::to_string(index) +
            "');var button" + std::to_string(index) +
            "=document.getElementById('button-" + std::to_string(index) + "');";
        source += "var stage" + std::to_string(index) + "=false;";
        source += "button" + std::to_string(index) +
            ".addEventListener('click',function(event" +
            std::to_string(index) + "){";
        source += "var rows" + std::to_string(index) + "=root" +
            std::to_string(index) + ".querySelectorAll(':scope > :nth-child(n of .pick-" +
            std::to_string(index) + ")');";
        source += "var before" + std::to_string(index) + "=rows" +
            std::to_string(index) + ".length===1&&rows" +
            std::to_string(index) + "[0]===item" + std::to_string(index) + ";";
        if (index + 1u < depth) {
            source += "button" + std::to_string(index + 1u) + ".click();";
        }
        source += "item" + std::to_string(index) +
            ".removeAttribute('class');var after" + std::to_string(index) +
            "=rows" + std::to_string(index) + ".length===0&&rows" +
            std::to_string(index) + "[0]===undefined;";
        source += "var metadata" + std::to_string(index) + "=event" +
            std::to_string(index) + ".target===button" +
            std::to_string(index) + "&&event" + std::to_string(index) +
            ".currentTarget===event" + std::to_string(index) +
            ".target&&event" + std::to_string(index) + ".eventPhase===2;";
        source += "stage" + std::to_string(index) + "=before" +
            std::to_string(index) + "&&after" + std::to_string(index) +
            "&&metadata" + std::to_string(index) + ";});";
    }
    source += "var ok=true;";
    return source;
}

std::string nestedEventResult(std::size_t depth)
{
    std::string source = "var ok=true;";
    for (std::size_t index = 0u; index < depth; ++index)
        source += "ok=ok&&stage" + std::to_string(index) + ";";
    return source;
}

} // namespace

#if defined(GXOS_JS74_STACK_INSTRUMENTED) && defined(_WIN32)
namespace {

struct StackSample {
    const char* label = nullptr;
    std::size_t bytes = 0u;
    bool passed = false;
};

struct StackProbeResult {
    static constexpr std::size_t kReserveBytes = 1024u * 1024u;
    static constexpr std::size_t kMaxSamples = 16u;
    std::uintptr_t stackBase = 0u;
    std::uintptr_t initialStackLimit = 0u;
    StackSample samples[kMaxSamples]{};
    std::size_t sampleCount = 0u;
    std::size_t maximumCommittedBytes = 0u;
    bool passed = true;
};

bool recordSample(StackProbeResult& result, std::uintptr_t marker)
{
    if (result.sampleCount >= StackProbeResult::kMaxSamples ||
        marker >= result.stackBase) {
        result.passed = false;
        return false;
    }
    g_js74LowestStackAddress = marker;
    g_js74StackSampleActive = true;
    return true;
}

void finishSample(StackProbeResult& result, const char* label, bool passed)
{
    g_js74StackSampleActive = false;
    const std::uintptr_t lowest = g_js74LowestStackAddress;
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const std::uintptr_t currentStackLimit =
        reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    const bool boundsValid = lowest < result.stackBase;
    const std::size_t bytes = boundsValid
        ? static_cast<std::size_t>(result.stackBase - lowest) : 0u;
    const bool committedBoundsValid = currentStackLimit <= result.stackBase &&
        currentStackLimit >= result.stackBase -
            StackProbeResult::kReserveBytes &&
        currentStackLimit <= result.initialStackLimit;
    const std::size_t committedBytes = committedBoundsValid
        ? static_cast<std::size_t>(result.stackBase - currentStackLimit) : 0u;
    result.maximumCommittedBytes = std::max(
        result.maximumCommittedBytes, committedBytes);
    result.samples[result.sampleCount++] = StackSample{
        label, bytes, passed && boundsValid && committedBoundsValid &&
            committedBytes < StackProbeResult::kReserveBytes};
    result.passed = result.passed && passed && boundsValid &&
        committedBoundsValid && committedBytes < StackProbeResult::kReserveBytes;
}

bool measureScript(StackProbeResult& result,
    gxos::javascript::NavigatorScriptExecutionHarness& harness,
    const char* label, const std::string& source)
{
    volatile std::uint8_t marker = 0u;
    const std::uintptr_t markerAddress =
        reinterpret_cast<std::uintptr_t>(&marker);
    if (!recordSample(result, markerAddress)) return false;
    const bool passed = scriptSucceeded(harness, source);
    finishSample(result, label, passed);
    return passed;
}

bool measureNestedDispatch(StackProbeResult& result,
    gxos::javascript::NavigatorScriptExecutionHarness& harness,
    gxos::javascript::HostInstanceId targetSerial)
{
    volatile std::uint8_t marker = 0u;
    const std::uintptr_t markerAddress =
        reinterpret_cast<std::uintptr_t>(&marker);
    if (!recordSample(result, markerAddress))
        return false;
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    const bool dispatched = harness.dispatchClick(targetSerial, error);
    const bool passed = dispatched &&
        error == gxos::javascript::RuntimeErrorCode::None &&
        scriptSucceeded(harness, nestedEventResult(kJs74NestedEventDepth));
    finishSample(result, "event-depth-16", passed);
    return passed;
}

bool runStackWorkloads(StackProbeResult& result)
{
    using namespace gxos::javascript;
    NavigatorScriptExecutionHarness harness;
    if (!loadStackFixture(harness, maximumCapacityHtml()) ||
        harness.document().structuralElements.size() != 1024u) return false;
    const std::string setup = "var target=document.getElementById('s0');"
        "var one=document.querySelectorAll('#load > :nth-child(odd of .selected)');"
        "var four=document.querySelectorAll('" + fourSlotSelector() + "');"
        "var eight=document.querySelectorAll('" + eightSlotSelector() + "');"
        "var ok=one.length===510&&four.length===4&&eight.length===1;";
    if (!scriptSucceeded(harness, setup)) return false;

    bool passed = true;
    passed = measureScript(result, harness, "simple-matches",
        "var ok=target.matches('.selected');") && passed;
    passed = measureScript(result, harness, "query-selector",
        "var ok=document.querySelector('.selected')===target;") && passed;
    passed = measureScript(result, harness, "query-selector-all",
        "var simple=document.querySelectorAll('.selected');"
        "var ok=simple.length===1021;") && passed;
    passed = measureScript(result, harness, "retained-length-one-slot",
        "var ok=one.length===510;") && passed;
    passed = measureScript(result, harness, "retained-index-one-slot",
        "var ok=one[509].id==='s1018';") && passed;
    passed = measureScript(result, harness, "retained-length-four-slots",
        "var ok=four.length===4;") && passed;
    passed = measureScript(result, harness, "retained-length-eight-slots",
        "var ok=eight.length===1&&eight[0].id==='i0';") && passed;
    passed = measureScript(result, harness, "closest-scope",
        "var ok=target.closest(':scope')===target;") && passed;
    passed = measureScript(result, harness, "filtered-query-selector",
        "var ok=document.querySelector(':nth-child(1 of .selected)')===target;")
        && passed;

    const std::string class255(255u, 'a');
    const std::string class256(256u, 'a');
    NavigatorScriptExecutionHarness boundary;
    const std::string boundaryHtml = "<html><body><i id='valid' class='" +
        class255 + "'></i><i id='too-long' class='" + class256 +
        "'></i></body></html>";
    if (!loadStackFixture(boundary, boundaryHtml)) return false;
    passed = measureScript(result, boundary, "selector-input-256-bytes",
        "var valid=document.querySelectorAll('." + class255 +
        "');var tooLong=document.querySelectorAll('." + class256 +
        "');var ok=valid.length===1&&valid[0].id==='valid'&&tooLong.length===0;")
        && passed;

    NavigatorScriptExecutionHarness has;
    if (!loadStackFixture(has,
            "<html><body><div id='has-root'><section><i class='warning'></i>"
            "</section></div></body></html>")) return false;
    passed = measureScript(result, has, "nested-has-two-step",
        "var ok=document.querySelector(\"#has-root:has(> section > :is(.warning, .error))\")"
        "===document.getElementById('has-root');") && passed;

    NavigatorScriptExecutionHarness events;
    if (!loadStackFixture(events, nestedEventHtml(kJs74NestedEventDepth)) ||
        !scriptSucceeded(events, nestedEventSetup(kJs74NestedEventDepth)))
        return false;
    HostInstanceId targetSerial = 0u;
    for (const auto& element : events.document().structuralElements)
        if (element.id == "button-0") targetSerial = element.serial;
    if (targetSerial == 0u) return false;
    passed = measureNestedDispatch(result, events, targetSerial) && passed;
    return passed && result.passed;
}

DWORD WINAPI js74StackProbeThread(void* parameter)
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
        js74StackProbeThread, &result, STACK_SIZE_PARAM_IS_A_RESERVATION,
        &threadId);
    if (thread == nullptr) {
        std::cerr << "JS74 runtime stack probe: CreateThread failed, error="
            << GetLastError() << '\n';
        return false;
    }
    const DWORD wait = WaitForSingleObject(thread, INFINITE);
    DWORD exitCode = 1u;
    const bool exitRead = GetExitCodeThread(thread, &exitCode) != 0;
    CloseHandle(thread);
    const std::size_t committedAtStart = result.stackBase >
        result.initialStackLimit
        ? static_cast<std::size_t>(result.stackBase - result.initialStackLimit)
        : 0u;
    std::size_t maximum = 0u;
    std::cout << "JS74 runtime stack: API=instrumented function-entry stack-pointer "
        "watermark plus cumulative TEB committed-stack watermark on dedicated "
        "Windows thread; reserve="
        << StackProbeResult::kReserveBytes << " B; initial-committed="
        << committedAtStart << " B; TEB NT_TIB stack base; instrumented build\n";
    for (std::size_t index = 0u; index < result.sampleCount; ++index) {
        const StackSample& sample = result.samples[index];
        maximum = std::max(maximum, sample.bytes);
        std::cout << "JS74 stack sample " << sample.label << ": observed="
            << sample.bytes << " B; "
            << (sample.passed ? "PASS" : "FAIL") << '\n';
    }
    std::cout << "JS74 runtime stack maximum instrumented=" << maximum
        << " B; maximum TEB committed=" << result.maximumCommittedBytes
        << " B / " << StackProbeResult::kReserveBytes << " B; reserve-headroom="
        << (StackProbeResult::kReserveBytes > result.maximumCommittedBytes
            ? StackProbeResult::kReserveBytes - result.maximumCommittedBytes
            : 0u) << " B; status="
        << (result.passed && wait == WAIT_OBJECT_0 && exitRead && exitCode == 0u
            ? "PASS" : "FAIL") << '\n';
    return result.passed && wait == WAIT_OBJECT_0 && exitRead && exitCode == 0u &&
        maximum < StackProbeResult::kReserveBytes;
}

} // namespace
#endif

namespace {

using namespace gxos::javascript;

void testJs74MaximumSelectorBoundary()
{
    const std::string class255(255u, 'a');
    const std::string class256(256u, 'a');
    const std::string html = "<html><body><i id='valid' class='" + class255 +
        "'></i><i id='too-long' class='" + class256 + "'></i></body></html>";
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness, html,
            "JS74 maximum selector-length fixture loads")) return;
    runScript(harness, "var valid=document.querySelectorAll('." + class255 +
        "');var tooLong=document.querySelectorAll('." + class256 +
        "');var ok=valid.length===1&&valid[0].id==='valid'&&tooLong.length===0;",
        "JS74 accepts a 256-byte selector and rejects a 257-byte selector");
}

void testJs74MaximumCollectionCapacity()
{
    constexpr std::size_t kCollectionCount = 128u;
    std::string html = "<html><body>";
    std::string source;
    std::string checks = "var ok=true;";
    for (std::size_t index = 0u; index < kCollectionCount; ++index) {
        const std::string suffix = std::to_string(index);
        html += "<i id='i" + suffix + "' class='f" + suffix + "'></i>";
        source += "var r" + suffix +
            "=document.querySelectorAll('.f" + suffix + "');";
        checks += "ok=ok&&r" + suffix + ".length===1&&r" + suffix +
            "[0].id==='i" + suffix + "';";
    }
    html += "</body></html>";
    source += checks;

    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness, html,
            "JS74 128-record collection fixture loads")) return;
    runScript(harness, source,
        "JS74 retains 128 distinct collection records at capacity");
}

void testJs74NestedEventDepth()
{
    std::size_t maximumSuccessfulDepth = 0u;
    for (std::size_t depth = 1u; depth <= kJs74NestedEventDepth; ++depth) {
        NavigatorScriptExecutionHarness harness;
        if (!loadFixture(harness, nestedEventHtml(depth),
                "JS74 nested Event depth fixture loads")) return;
        if (!scriptSucceeded(harness, nestedEventSetup(depth))) {
            std::cerr << "  JS74 Event handler setup failed at depth=" << depth
                << '\n';
            break;
        }
        HostInstanceId targetSerial = 0u;
        for (const auto& element : harness.document().structuralElements)
            if (element.id == "button-0") targetSerial = element.serial;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        const bool dispatched = targetSerial != 0u &&
            harness.dispatchClick(targetSerial, error);
        const bool stagesPassed = scriptSucceeded(harness,
            nestedEventResult(depth));
        std::cout << "JS74 Event depth probe: depth=" << depth
            << ", dispatched=" << dispatched << ", error="
            << runtimeErrorCodeName(error) << ", stages=" << stagesPassed
            << '\n';
        if (!dispatched || error != RuntimeErrorCode::None || !stagesPassed)
            break;
        maximumSuccessfulDepth = depth;
    }
    expect(maximumSuccessfulDepth == kJs74NestedEventDepth,
        "JS74 nested selector dispatch reaches the 16-event runtime bound");
}

void testJs74HasStackWorkload()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadFixture(harness,
            "<html><body><div id='has-root'><section><i class='warning'></i>"
            "</section></div></body></html>",
            "JS74 nested :has() fixture loads")) return;
    runScript(harness,
        "var ok=document.querySelector(\"#has-root:has(> section > :is(.warning, .error))\")"
        "===document.getElementById('has-root');",
        "JS74 two-step correlated :has() and nested logical alternatives remain valid");
}

void testJs74HostedFixtureIntegrity()
{
    std::ifstream fixtureFile("navigator-smoke/javascript-js74.html",
        std::ios::binary);
    const std::string fixtureHtml((std::istreambuf_iterator<char>(fixtureFile)),
        std::istreambuf_iterator<char>());
    const std::size_t scriptOpen = fixtureHtml.find("<script>");
    const std::size_t scriptClose = fixtureHtml.find("</script>", scriptOpen);
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = !fixtureHtml.empty() &&
        scriptOpen != std::string::npos && scriptClose != std::string::npos &&
        harness.loadHtml("file:///js74-hosted-fixture.html", fixtureHtml,
            error) && error == RuntimeErrorCode::None;
    expect(loaded, "JS74 hosted fixture loads in the native HTML subset");
    if (!loaded) return;
    const std::string source = fixtureHtml.substr(scriptOpen + 8u,
        scriptClose - scriptOpen - 8u);
    const auto& captured = harness.document().scriptSources;
    const bool preserved = captured.size() == 1u && captured[0] == source &&
        source.find("<") == std::string::npos;
    if (!preserved) {
        std::cerr << "  JS74 source integrity: expected-bytes=" << source.size()
            << ", captured-count=" << captured.size();
        if (captured.size() == 1u) {
            std::size_t mismatch = 0u;
            while (mismatch < source.size() && mismatch < captured[0].size() &&
                source[mismatch] == captured[0][mismatch]) ++mismatch;
            std::cerr << ", captured-bytes=" << captured[0].size()
                << ", first-mismatch=" << mismatch;
        }
        std::cerr << '\n';
    }
    expect(preserved,
        "JS74 hosted script avoids literal less-than and preserves source bytes");
}

void benchmarkJs74Case(const TimingSample& sample)
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    if (!harness.loadHtml("file:///js74-benchmark.html", sample.html, error) ||
        error != RuntimeErrorCode::None) {
        std::cout << "JS74 timing " << sample.label << " fixture=FAIL\n";
        ++failures;
        return;
    }
    std::vector<std::int64_t> values;
    values.reserve(5u);
    for (std::size_t iteration = 0u; iteration < 5u; ++iteration) {
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(sample.source);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
        const Value* resultValue = harness.runtime().lookup(SourceView("ok", 2u));
        if (!result.succeeded() || resultValue == nullptr ||
            !resultValue->isBoolean() || !resultValue->booleanValue()) {
            std::cout << "JS74 timing " << sample.label << " result=FAIL, status="
                << scriptStatusName(result.status) << ", error="
                << runtimeErrorCodeName(result.runtimeError.code) << '\n';
            ++failures;
            return;
        }
        values.push_back(elapsed);
    }
    std::sort(values.begin(), values.end());
    std::cout << "JS74 timing " << sample.label << ": median-us=" <<
        values[values.size() / 2u] << ", min-us=" << values.front() <<
        ", max-us=" << values.back() << ", repetitions=" << values.size()
        << '\n';
}

void benchmarkJs74NestedEvents()
{
    constexpr std::size_t kDepth = 16u;
    std::vector<std::int64_t> values;
    values.reserve(5u);
    for (std::size_t iteration = 0u; iteration < 5u; ++iteration) {
        NavigatorScriptExecutionHarness harness;
        if (!loadFixture(harness, nestedEventHtml(kDepth),
                "JS74 nested Event benchmark fixture loads") ||
            !scriptSucceeded(harness, nestedEventSetup(kDepth))) {
            ++failures;
            return;
        }
        HostInstanceId targetSerial = 0u;
        for (const auto& element : harness.document().structuralElements)
            if (element.id == "button-0") targetSerial = element.serial;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        const auto start = std::chrono::steady_clock::now();
        const bool dispatched = targetSerial != 0u &&
            harness.dispatchClick(targetSerial, error);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
        if (!dispatched || error != RuntimeErrorCode::None ||
            !scriptSucceeded(harness, nestedEventResult(kDepth))) {
        std::cout << "JS74 timing nested-event-depth-16 result=FAIL\n";
            ++failures;
            return;
        }
        values.push_back(elapsed);
    }
    std::sort(values.begin(), values.end());
    std::cout << "JS74 timing nested-event-depth-16: median-us=" <<
        values[values.size() / 2u] << ", min-us=" << values.front() <<
        ", max-us=" << values.back() << ", repetitions=" << values.size()
        << '\n';
}

void runJs74Benchmarks()
{
    const std::string dense = maximumCapacityHtml();
    const std::string sparse = maximumCapacityHtml(true);
    benchmarkJs74Case({"simple-matches", dense,
        "var target=document.getElementById('s0');"
        "var ok=target.matches('.selected');"});
    benchmarkJs74Case({"one-slot-forward-qsa", dense,
        "var rows=document.querySelectorAll('#load > :nth-child(odd of .selected)');"
        "var ok=rows.length===510&&rows[0].id==='s0';"});
    benchmarkJs74Case({"one-slot-reverse-qsa", dense,
        "var rows=document.querySelectorAll('#load > :nth-last-child(odd of .selected)');"
        "var ok=rows.length===510&&rows[0].id==='s1';"});
    benchmarkJs74Case({"four-slot-qsa", dense,
        "var rows=document.querySelectorAll('" + fourSlotSelector() +
        "');var ok=rows.length===4;"});
    benchmarkJs74Case({"eight-slot-qsa", dense,
        "var rows=document.querySelectorAll('" + eightSlotSelector() +
        "');var ok=rows.length===1&&rows[0].id==='i0';"});
    benchmarkJs74Case({"dense-filter-qsa", dense,
        "var rows=document.querySelectorAll('#load > :nth-child(odd of .selected)');"
        "var ok=rows.length===510;"});
    benchmarkJs74Case({"sparse-filter-qsa", sparse,
        "var rows=document.querySelectorAll('#load > :nth-child(odd of .selected)');"
        "var ok=rows.length===255;"});
    benchmarkJs74Case({"repeated-indexed-reads", dense,
        "var rows=document.querySelectorAll('#load > :nth-child(n of .selected)');"
        "var ok=rows[0].id==='s0'&&rows[255].id==='s255'&&"
        "rows[509].id==='s509';"});
    benchmarkJs74NestedEvents();
}

} // namespace

int main(int argc, char** argv)
{
    using namespace gxos::javascript;
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 1918u,
        "JS74 preserves the compact selector descriptor");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::collectionBytes()
            == 1936u,
        "JS74 preserves the compact selector collection record");
    static_assert(NavigatorScriptRelativeSelectorTestAccess::
            scratchBytesForSlots<1u>() == 4232u &&
        NavigatorScriptRelativeSelectorTestAccess::
            scratchBytesForSlots<4u>() == 10376u &&
        NavigatorScriptRelativeSelectorTestAccess::scratchBytes() == 18568u,
        "JS74 preserves invocation-local scratch capacities for 1/4/8 slots");
    static_assert(kNavigatorScriptMaxSelectorLength == 256u &&
        kNavigatorScriptMaxSelectorListMembers == 4u &&
        kNavigatorScriptMaxHasRelativeCores == 24u &&
        kNavigatorScriptMaxSelectorCollections == 128u &&
        kNavigatorScriptMaxDocumentNodes == 1024u,
        "JS74 preserves existing selector, collection, and document bounds");

    if (argc > 1 && std::string(argv[1]) == "--stack-only") {
#if defined(GXOS_JS74_STACK_INSTRUMENTED) && defined(_WIN32)
        return runStackProbe() ? 0 : 1;
#else
        std::cerr << "JS74 runtime stack sampling requires the instrumented "
            "Windows build.\n";
        return 2;
#endif
    }

    const std::size_t js73ChecksStart = checks;
    const std::size_t js73FailuresStart = failures;
    testSemantics();
    testCompactScratchSlots();
    testHostedFixtureSourceIntegrity();
    testMaximumCapacityAndCounters();
    testRepeatedIndexedReadCounters();
    testMaximumCapacityVariants();
    testInvalidStructuresAndGenerationReuse();
    std::cout << "JS73 inherited compatibility checks: "
        << checks - js73ChecksStart - (failures - js73FailuresStart) << '/'
        << checks - js73ChecksStart << " passed\n";

    const std::size_t js74ChecksStart = checks;
    const std::size_t js74FailuresStart = failures;
    testJs74MaximumSelectorBoundary();
    testJs74MaximumCollectionCapacity();
    testJs74NestedEventDepth();
    testJs74HasStackWorkload();
    testJs74HostedFixtureIntegrity();
    std::cout << "Navigator JavaScript JS74 checks: "
        << checks - js74ChecksStart - (failures - js74FailuresStart) << '/'
        << checks - js74ChecksStart << " passed\n";
    std::cout << "JS74 scratch layout: descriptor="
        << sizeof(NavigatorScriptSelectorDescriptor) << " B, collection="
        << NavigatorScriptRelativeSelectorTestAccess::collectionBytes()
        << " B, registry="
        << NavigatorScriptRelativeSelectorTestAccess::collectionBytes() *
            kNavigatorScriptMaxSelectorCollections
        << " B, scratch1=" <<
            NavigatorScriptRelativeSelectorTestAccess::scratchBytesForSlots<1u>()
        << " B, scratch4=" <<
            NavigatorScriptRelativeSelectorTestAccess::scratchBytesForSlots<4u>()
        << " B, scratch8=" <<
            NavigatorScriptRelativeSelectorTestAccess::scratchBytes() << " B\n";

    if (argc <= 1 || std::string(argv[1]) != "--no-bench") {
        runBenchmarks();
        runJs74Benchmarks();
    }
    return failures == 0u ? 0 : 1;
}
