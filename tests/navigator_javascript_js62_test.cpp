#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
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

int failures = 0;
int checks = 0;

void expect(bool condition, const char* message)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const gxos::web::HtmlElementRef* byId(const gxos::web::WebDocument& document,
    const char* id)
{
    for (const auto& element : document.structuralElements)
        if (element.id == id) return &element;
    return nullptr;
}

} // namespace

int main()
{
    using namespace gxos::javascript;
    static_assert(sizeof(NavigatorScriptSimpleSelectorCoreDescriptor) == 32u,
        "JS62 must reuse the JS59-JS61 shared selector core");
    static_assert(sizeof(NavigatorScriptSimpleSelectorDescriptor) == 68u,
        "JS62 must not grow the simple selector descriptor");
    static_assert(sizeof(NavigatorScriptSelectorDescriptor) == 812u,
        "JS62 must not grow the four-member selector descriptor");
    static_assert(sizeof(gxos::web::HtmlElementRef) == 440u,
        "JS62 must not grow document Element records");
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string html =
        "<html><body><div id='anchor' class='self-only'><section id='section'>"
        "<span id='nested' class='target'></span></section>"
        "<span id='direct' class='direct'></span></div>"
        "<div id='siblings'><i id='before'></i><i id='start'></i>"
        "<b id='adjacent' class='target'></b><b id='later' class='later'></b>"
        "</div><div id='other-parent'><b id='unrelated' class='target'></b>"
        "</div></body></html>";
    expect(harness.loadHtml("file:///js62.html", html, error),
        "JS62 fixture loads");

    const NavigatorScriptSelectorDescriptor storage{};
    NavigatorScriptSimpleSelectorCoreDescriptor target;
    target.valid = true;
    target.classTokenCount = 1u;
    target.classTokens[0].offset = 0u;
    target.classTokens[0].length = 6u;
    NavigatorScriptSelectorDescriptor targetStorage;
    targetStorage.text[0] = 't'; targetStorage.text[1] = 'a';
    targetStorage.text[2] = 'r'; targetStorage.text[3] = 'g';
    targetStorage.text[4] = 'e'; targetStorage.text[5] = 't';
    targetStorage.textLength = 6u;
    const auto anchor = byId(harness.document(), "anchor");
    const auto start = byId(harness.document(), "start");
    const auto* root = &harness.document().documentElement;
    expect(anchor != nullptr && start != nullptr,
        "anchor and sibling fixture nodes exist");
    if (anchor != nullptr) {
        const HostObjectReference handle{anchor->serial,
            harness.hostAdapter().generation(), kNavigatorElementHostKind};
        targetStorage.text[0] = 't'; targetStorage.text[1] = 'a';
        targetStorage.text[2] = 'r'; targetStorage.text[3] = 'g';
        targetStorage.text[4] = 'e'; targetStorage.text[5] = 't';
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::Descendant, target,
            targetStorage) == NavigatorScriptSelectorMatchResult::Match,
            "strict descendant target matches");
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::Child, target,
            targetStorage) == NavigatorScriptSelectorMatchResult::NoMatch,
            "nested target is not a direct child");

        NavigatorScriptSimpleSelectorCoreDescriptor direct;
        direct.valid = true;
        direct.classTokenCount = 1u;
        direct.classTokens[0].offset = 0u;
        direct.classTokens[0].length = 6u;
        targetStorage.text[0] = 'd'; targetStorage.text[1] = 'i';
        targetStorage.text[2] = 'r'; targetStorage.text[3] = 'e';
        targetStorage.text[4] = 'c'; targetStorage.text[5] = 't';
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::Child, direct,
            targetStorage) == NavigatorScriptSelectorMatchResult::Match,
            "direct child target matches");
        targetStorage.text[0] = 's'; targetStorage.text[1] = 'e';
        targetStorage.text[2] = 'l'; targetStorage.text[3] = 'f';
        targetStorage.text[4] = '-'; targetStorage.text[5] = 'o';
        target.classTokens[0].length = 9u;
        targetStorage.text[6] = 'n'; targetStorage.text[7] = 'l';
        targetStorage.text[8] = 'y';
        targetStorage.textLength = 9u;
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::Descendant, target,
            targetStorage) == NavigatorScriptSelectorMatchResult::NoMatch,
            "descendant relation never counts the matching anchor itself");
        targetStorage.textLength = 6u;
    }
    if (start != nullptr) {
        const HostObjectReference handle{start->serial,
            harness.hostAdapter().generation(), kNavigatorElementHostKind};
        target.classTokenCount = 1u;
        target.universal = false;
        target.classTokens[0].length = 6u;
        targetStorage.text[0] = 't'; targetStorage.text[1] = 'a';
        targetStorage.text[2] = 'r'; targetStorage.text[3] = 'g';
        targetStorage.text[4] = 'e'; targetStorage.text[5] = 't';
        targetStorage.textLength = 6u;
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::AdjacentSibling, target,
            targetStorage) == NavigatorScriptSelectorMatchResult::Match,
            "adjacent following sibling matches");
        targetStorage.text[0] = 'l'; targetStorage.text[1] = 'a';
        targetStorage.text[2] = 't'; targetStorage.text[3] = 'e';
        targetStorage.text[4] = 'r'; targetStorage.text[5] = ' ';
        target.classTokens[0].length = 5u;
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::AdjacentSibling, target,
            targetStorage) == NavigatorScriptSelectorMatchResult::NoMatch,
            "adjacent relation excludes a later matching sibling");
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::GeneralSibling, target,
            targetStorage) == NavigatorScriptSelectorMatchResult::Match,
            "general sibling reaches later match after adjacent non-match");
        targetStorage.text[0] = 't'; targetStorage.text[1] = 'a';
        targetStorage.text[2] = 'r'; targetStorage.text[3] = 'g';
        targetStorage.text[4] = 'e'; targetStorage.text[5] = 't';
        target.classTokens[0].length = 6u;
        target.classTokenCount = 1u;
        target.universal = false;
        target.classTokenCount = 0u;
        target.universal = true;
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), handle,
            NavigatorScriptRelativeSelectorRelation::GeneralSibling, target,
            storage) == NavigatorScriptSelectorMatchResult::Match,
            "general following sibling matches");
    }
    if (root != nullptr) {
        const HostObjectReference stale{root->serial,
            static_cast<HostGenerationId>(harness.hostAdapter().generation() + 1u),
            kNavigatorElementHostKind};
        expect(NavigatorScriptRelativeSelectorTestAccess::evaluate(
            harness.hostAdapter(), stale,
            NavigatorScriptRelativeSelectorRelation::Descendant, target,
            storage) == NavigatorScriptSelectorMatchResult::Invalid,
            "stale generation anchor fails closed");
    }
    for (const char* supported : {".matches(':has(.target)')",
            ".matches(':has(> .target)')", ".matches(':has(+ .target)')",
            ".matches(':has(~ .target)')"}) {
        const std::string source = std::string("document.querySelector('body')") + supported + ";";
        expect(harness.execute(source).succeeded(),
            "public :has() relative forms parse through the selector API");
    }
    if (failures != 0) return 1;
    std::cout << "Navigator JavaScript JS62 checks: " << checks << "/" << checks
        << " passed\n";
    return 0;
}
