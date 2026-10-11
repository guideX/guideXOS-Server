#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace gxos::javascript;

constexpr std::size_t kTimingRuns = 7u;
constexpr std::size_t kQueriesPerRun = 20u;

std::string makeCapacityHtml()
{
    std::string html = "<html><body><section id='panel' class='panel'>";
    for (std::size_t index = 0u; index < 495u; ++index)
        html += "<div class='row'><i></i></div>";
    html += "<div class='row'><div id='query-root'>";
    for (std::size_t index = 0u; index < 15u; ++index)
        html += index == 0u
            ? "<i id='first-target' class='target'></i>"
            : "<i class='target'></i>";
    html += "</div>";
    for (std::size_t index = 0u; index < 14u; ++index) html += "<i></i>";
    html += "</section></body></html>";
    return html;
}

bool load(NavigatorScriptExecutionHarness& harness, const std::string& html)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    return harness.loadHtml("file:///js70-benchmark.html", html, error) &&
        error == RuntimeErrorCode::None;
}

bool isTrue(const NavigatorScriptExecutionHarness& harness)
{
    const Value* value = harness.runtime().lookup(SourceView("ok", 2u));
    return value != nullptr && value->isBoolean() && value->booleanValue();
}

std::string repeatCheck(const std::string& expression)
{
    return "var workloadOk = true; for (var workloadIndex = 0; "
        "workloadIndex < 20; workloadIndex = workloadIndex + 1) { "
        "if (!(" + expression + ")) workloadOk = false; } "
        "var ok = workloadOk;";
}

bool measure(NavigatorScriptExecutionHarness& harness, const char* name,
    const std::string& source)
{
    std::vector<std::int64_t> samples;
    samples.reserve(kTimingRuns);
    for (std::size_t run = 0u; run < kTimingRuns; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(source);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() -
                start).count();
        if (!result.succeeded() || !isTrue(harness)) {
            std::cerr << "benchmark workload failed: " << name
                << " run=" << run << '\n';
            return false;
        }
        samples.push_back(elapsed);
    }
    std::sort(samples.begin(), samples.end());
    std::cout << "workload=" << name << " runs=" << kTimingRuns
        << " queries-per-run=" << kQueriesPerRun << " median-us="
        << samples[samples.size() / 2u] << " range-us=" << samples.front()
        << "-" << samples.back() << '\n';
    return true;
}

bool measureCollections()
{
    std::vector<std::int64_t> samples;
    samples.reserve(kTimingRuns);
    for (std::size_t run = 0u; run < kTimingRuns; ++run) {
        auto harnessStorage =
            std::make_unique<NavigatorScriptExecutionHarness>();
        if (!load(*harnessStorage,
                "<html><body><div></div></body></html>")) return false;
        const std::string source = R"JS(
var capacityOk = true;
for (var n = 0; n < 127; n = n + 1)
  if (document.querySelectorAll('[data-probe=v' + n + '], .missing').length !== 0)
    capacityOk = false;
var lastCollection = document.querySelectorAll('[data-probe=last], .missing');
var ok = capacityOk && lastCollection.length === 0;
)JS";
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harnessStorage->execute(source);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() -
                start).count();
        if (!result.succeeded() || !isTrue(*harnessStorage)) return false;
        samples.push_back(elapsed);
    }
    std::sort(samples.begin(), samples.end());
    std::cout << "workload=128-collection-saturation runs=" << kTimingRuns
        << " queries-per-run=128 median-us=" << samples[samples.size() / 2u]
        << " range-us=" << samples.front() << "-" << samples.back() << '\n';
    return true;
}

bool measureNestedEvents()
{
    const std::string html =
        "<html><body><div id='panel'><div class='row'>"
        "<button id='event-a'></button><button id='event-b'></button>"
        "</div></div></body></html>";
    const std::string source = R"JS(
var outerCount = 0;
var innerCount = 0;
var selectorsOk = true;
document.querySelector('#event-a').addEventListener('click', function(event) {
  outerCount = outerCount + 1;
  selectorsOk = selectorsOk && document.querySelector('#panel').matches(
    ':has(> .row > :not(.disabled, .hidden))');
  document.querySelector('#event-b').click();
});
document.querySelector('#event-b').addEventListener('click', function(event) {
  innerCount = innerCount + 1;
  selectorsOk = selectorsOk && document.querySelector('#panel').matches(
    ':has(> .row > :is(.missing, *))');
});
for (var eventIndex = 0; eventIndex < 20; eventIndex = eventIndex + 1)
  document.querySelector('#event-a').click();
var ok = outerCount === 20 && innerCount === 20 && selectorsOk;
)JS";
    std::vector<std::int64_t> samples;
    samples.reserve(kTimingRuns);
    for (std::size_t run = 0u; run < kTimingRuns; ++run) {
        auto harnessStorage =
            std::make_unique<NavigatorScriptExecutionHarness>();
        if (!load(*harnessStorage, html)) return false;
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harnessStorage->execute(source);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() -
                start).count();
        if (!result.succeeded() || !isTrue(*harnessStorage)) return false;
        samples.push_back(elapsed);
    }
    std::sort(samples.begin(), samples.end());
    std::cout << "workload=nested-event-dispatch runs=" << kTimingRuns
        << " events-per-run=40 median-us=" << samples[samples.size() / 2u]
        << " range-us=" << samples.front() << "-" << samples.back() << '\n';
    return true;
}

} // namespace

int main()
{
    auto harnessStorage =
        std::make_unique<NavigatorScriptExecutionHarness>();
    auto& harness = *harnessStorage;
    if (!load(harness, makeCapacityHtml()) ||
        harness.document().structuralElements.size() !=
            kNavigatorScriptMaxDocumentNodes) {
        std::cerr << "benchmark fixture did not reach 1,024 Elements\n";
        return 1;
    }
    std::cout << "fixture-elements=" <<
        harness.document().structuralElements.size() << '\n';

    const std::vector<std::pair<const char*, std::string>> workloads = {
        {"simple-tag", "document.querySelector('i') !== null"},
        {"simple-class", "document.querySelector('.target') !== null"},
        {"four-member-list", "document.querySelectorAll("
            "'.missing, .absent, .target, .nothing').length === 15"},
        {"single-step-has", "document.querySelector('#panel').matches("
            "':has(.target)')"},
        {"two-alternative-has", "document.querySelector('#panel').matches("
            "':has(.missing, .target)')"},
        {"two-step-chain", "document.querySelector('#panel').matches("
            "':has(> .row > i)')"},
        {"explicit-scope", "document.querySelector('#query-root').matches("
            "':scope')"},
        {"scope-inside-has", "!document.querySelector('#panel').matches("
            "':has(> .row > :scope)')"},
        {"nested-logical", "document.querySelector('#panel').matches("
            "':has(> .row > :is(#query-root, .missing))')"},
        {"two-member-not", "document.querySelector('#panel').matches("
            "':has(> .row > :not(.missing, .absent))')"},
        {"large-query-all", "document.querySelectorAll('.row').length === 496"},
        {"1024-element-false", "!document.querySelector('#panel').matches("
            "':has(> .row > .absent)')"},
        {"1024-element-late-success", "document.querySelector('#panel').matches("
            "':has(> .row > #query-root)')"},
    };

    bool passed = true;
    for (const auto& workload : workloads)
        passed = measure(harness, workload.first,
            repeatCheck(workload.second)) && passed;
    passed = measureCollections() && passed;
    passed = measureNestedEvents() && passed;
    return passed ? 0 : 1;
}
