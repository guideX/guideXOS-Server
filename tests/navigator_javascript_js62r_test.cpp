#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <iostream>
#include <string>

namespace gxos::javascript {

struct NavigatorScriptRelativeSelectorTestAccess {
    static NavigatorScriptSelectorMatchResult evaluate(
        const NavigatorScriptHostAdapter& adapter, HostObjectReference anchor,
        NavigatorScriptRelativeSelectorRelation relation,
        const NavigatorScriptSimpleSelectorCoreDescriptor& selector,
        const NavigatorScriptSelectorDescriptor& storage)
    {
        return adapter.selectorRelativeElementMatchResult(anchor, relation,
            selector, 0, 0, storage);
    }
};

} // namespace gxos::javascript

namespace {
int checks = 0;
int failures = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}

gxos::web::HtmlElementRef* find(gxos::web::WebDocument& document,
    const std::string& id)
{
    for (auto& element : document.structuralElements)
        if (element.id == id) return &element;
    return nullptr;
}

} // namespace

int main()
{
    using namespace gxos::javascript;
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    std::string html = "<html><body><div id='anchor'><i id='one'></i>";
    for (std::size_t index = 0; index < 1019u; ++index)
        html += "<i></i>";
    html += "<b id='last' class='target'></b></div></body></html>";
    expect(harness.loadHtml("file:///js62r-capacity.html", html, error),
        "near-capacity fixture loads");
    expect(harness.document().structuralElements.size() ==
        kNavigatorScriptMaxDocumentNodes,
        "fixture reaches the 1024 structural record cap");

    auto* anchor = find(harness.document(), "anchor");
    auto* targetElement = find(harness.document(), "last");
    expect(anchor != nullptr && targetElement != nullptr,
        "anchor and final target remain represented at capacity");
    NavigatorScriptSimpleSelectorCoreDescriptor target;
    target.valid = true;
    target.classTokenCount = 1u;
    target.classTokens[0].offset = 0u;
    target.classTokens[0].length = 6u;
    NavigatorScriptSelectorDescriptor storage;
    storage.text[0] = 't'; storage.text[1] = 'a'; storage.text[2] = 'r';
    storage.text[3] = 'g'; storage.text[4] = 'e'; storage.text[5] = 't';
    storage.textLength = 6u;
    if (anchor != nullptr) {
        const HostObjectReference handle{anchor->serial,
            harness.hostAdapter().generation(), kNavigatorElementHostKind};
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::Descendant, target,
            storage) == NavigatorScriptSelectorMatchResult::Match,
            "deep final descendant matches at capacity");

        const std::uint64_t savedParent = anchor->parentSerial;
        anchor->parentSerial = anchor->serial;
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::Descendant, target,
            storage) == NavigatorScriptSelectorMatchResult::Invalid,
            "self-parent anchor corruption fails closed");
        anchor->parentSerial = savedParent;
    }

    if (anchor != nullptr && targetElement != nullptr) {
        const std::uint64_t anchorSerial = anchor->serial;
        const std::uint64_t targetSerial = targetElement->serial;
        const HostObjectReference handle{anchorSerial,
            harness.hostAdapter().generation(), kNavigatorElementHostKind};
        anchor->parentSerial = targetSerial;
        targetElement->parentSerial = anchorSerial;
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::Descendant, target,
            storage) == NavigatorScriptSelectorMatchResult::Invalid,
            "two-node cycle in anchor ancestry is bounded and rejected");
    }

    for (const char* suffix : {".matches(':has(.target)')",
            ".matches(':has(> .target)')", ".matches(':has(+ .target)')",
            ".matches(':has(~ .target)')"}) {
        const std::string source = std::string("document.body") + suffix + ";";
        expect(!harness.execute(source).succeeded(),
            "public :has() remains unsupported");
    }

    if (failures != 0) return 1;
    std::cout << "Navigator JavaScript JS62R checks: " << checks << "/"
        << checks << " passed\n";
    return 0;
}
