#include "navigator_javascript/navigator_script_host.h"

#include <iostream>
#include <string>

using namespace gxos::javascript;

namespace {
int checks = 0;
int failures = 0;

void expect(NavigatorScriptExecutionHarness& harness, const std::string& source,
    const char* label)
{
    ++checks;
    const ScriptResult result = harness.execute(source);
    const Value* value = harness.runtime().lookup(SourceView("ok", 2));
    if (result.succeeded() && value != nullptr && value->isBoolean() &&
        value->booleanValue()) return;
    ++failures;
    std::cerr << "FAIL: " << label << " (status="
        << static_cast<unsigned>(result.status) << ", error="
        << static_cast<unsigned>(result.runtimeError.code) << ")\n";
}
}

int main()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string html =
        "<html><body><div id='root'><section id='panel' class='panel'>"
        "<button id='first' class='item hot' data-state='ready'></button>"
        "<i id='middle'></i><b id='later' class='target'></b>"
        "<div id='deep'><em id='deep-target' class='deep'></em></div>"
        "</section><aside id='outside' class='target'></aside>"
        "<form id='form'><input id='check' type='checkbox' checked></form>"
        "</div></body></html>";
    if (!harness.loadHtml("file:///js63.html", html, error) ||
        error != RuntimeErrorCode::None) {
        std::cerr << "FAIL: fixture loads\n";
        return 1;
    }
    expect(harness, R"JS(
var panel = document.querySelector('#panel');
var first = document.querySelector('#first');
var middle = document.querySelector('#middle');
var later = document.querySelector('#later');
var deep = document.querySelector('#deep');
var root = document.querySelector('#root');
var ok = panel.matches(':has(.item)') && panel.matches(':has(> .item)') &&
 !first.matches(':has(.item)') && panel.matches(':has(.deep)') &&
 !panel.matches(':has(> .deep)') && first.matches(':has(+ #middle)') &&
 !first.matches(':has(+ .target)') && first.matches(':has(~ .target)') &&
 !later.matches(':has(~ .item)') && !panel.matches(':has(+ .target)') &&
 !panel.matches(':has(~ .target)') && deep.matches(':has(em.deep)');
)JS", "four relation match boundaries and strict descendant semantics");
    expect(harness, R"JS(
var q = document.querySelector('section:has(> button.item[data-state=ready])');
var outerRelation = document.querySelector('#root > section:has(.item)');
var all = document.querySelectorAll('section:has(.item), div:has(.deep)');
var scoped = panel.querySelectorAll(':has(> .item)');
var scopedSibling = panel.querySelectorAll(':has(+ #middle)');
var qok = q === panel;
var outerRelationOK = outerRelation === panel;
var allok = all.length === 3 && all[0] === document.querySelector('#root') &&
 all[1] === panel && all[2] === deep;
var scopedok = scoped.length === 0 && panel.querySelector(':has(> .item)') === null;
var scopedSiblingOK = scopedSibling.length === 1 && scopedSibling[0] === first;
var matchok = first.matches('button.item[data-state=ready]');
var closestok = first.closest('section:has(.item)') === panel && first.closest(':has(+ #middle)') === first;
var ok = qok && outerRelationOK && allok && scopedok && scopedSiblingOK && matchok && closestok;
)JS", "querySelector, ordered querySelectorAll, scoped queries, matches, closest");
    expect(harness, R"JS(
var form = document.querySelector('#form');
var check = document.querySelector('#check');
var live = document.querySelectorAll('form:has(:checked)');
check.checked = false;
var attrLive = document.querySelectorAll('section:has([data-live=yes])');
document.querySelector('#first').setAttribute('data-live', 'yes');
var ok = live.length === 0 && attrLive.length === 1;
)JS", "live checked state in relative core");
    expect(harness, R"JS(
var bad = [':has()', ':has( )', ':has(>)', ':has(+)', ':has(~)',
 ':has(> + .x)', ':has(|| .x)', ':has(>> .x)', ':has(++ .x)',
 ':has(.a, .b)', ':has(.a .b)', ':has(.a > .b)', ':has(> .a .b)',
 ':has(:not(.x))', ':has(:is(.x))', ':has(:where(.x))',
 ':has(:has(.x))', ':has(:nth-child(2))', ':has(.x):focus'];
var ok = true;
for (var i = 0; i < bad.length; i++)
 if (document.querySelector(bad[i]) !== null) ok = false;
)JS", "malformed, chained, list, nested functional, and multiple-pseudo rejection");
    const ScriptResult eventSetup = harness.execute(R"JS(
var runs = 0;
document.querySelector('#first').addEventListener('click', function(e) {
 if (e.target.matches(':has(+ #middle)')) runs = runs + 1;
});
)JS");
    ++checks;
    if (!eventSetup.succeeded()) {
        ++failures;
        std::cerr << "FAIL: public :has listener registration\n";
    }
    const HostInstanceId firstSerial = [&harness]() {
        for (const auto& element : harness.document().structuralElements)
            if (element.id == "first") return element.serial;
        return HostInstanceId{0};
    }();
    bool defaultPrevented = false;
    const bool dispatched = harness.dispatchClick(firstSerial, error,
        &defaultPrevented);
    ++checks;
    const Value* eventMatch = harness.runtime().lookup(SourceView("runs", 4));
    if (!dispatched || eventMatch == nullptr || !eventMatch->isNumber() ||
        eventMatch->numberValue() != 1) {
        ++failures;
        std::cerr << "FAIL: :has works in dispatched Event callback\n";
    }

    std::string nearCapacityHtml =
        "<html><body><div id='capacity'>";
    for (std::size_t index = 0; index < 1020u; ++index)
        nearCapacityHtml += "<i></i>";
    nearCapacityHtml += "<b id='tail'></b></div></body></html>";
    NavigatorScriptExecutionHarness nearCapacityHarness;
    error = RuntimeErrorCode::None;
    const bool nearCapacityLoaded = nearCapacityHarness.loadHtml(
        "file:///js63-near-capacity.html", nearCapacityHtml, error);
    ++checks;
    if (!nearCapacityLoaded || error != RuntimeErrorCode::None ||
        nearCapacityHarness.document().structuralElements.size() !=
            kNavigatorScriptMaxDocumentNodes) {
        ++failures;
        std::cerr << "FAIL: JS63 public fixture reaches the 1,024-node bound\n";
    }
    expect(nearCapacityHarness, R"JS(
var capacity = document.querySelector('#capacity');
var tail = document.querySelector('#tail');
var fullFalse = !capacity.matches(':has(.target)');
tail.setAttribute('class', 'target');
var lateTrue = capacity.matches(':has(.target)');
var selected = document.querySelector('div:has(.target)');
var all = document.querySelectorAll('div:has(.target)');
var ok = fullFalse && lateTrue && selected === capacity &&
 all.length === 1 && all[0] === capacity;
)JS", "public :has full-false and late-true at the 1,024-node bound");

    std::cout << "Navigator JavaScript JS63 checks: " << checks - failures
        << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
