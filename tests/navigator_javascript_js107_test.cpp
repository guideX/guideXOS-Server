#include "navigator_javascript/navigator_script_host.h"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptRuntimeTestAccess {
    static std::size_t runtimeObjectSize()
    {
        return sizeof(RuntimeContext::RuntimeObject);
    }

    static std::size_t arrayElementSize()
    {
        return sizeof(RuntimeContext::RuntimeArrayElement);
    }
};

} // namespace gxos::javascript

namespace {

using namespace gxos::javascript;

std::size_t checks = 0u;
std::size_t failures = 0u;

void expect(bool condition, const char* label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "  FAIL: " << label << '\n';
}

ScriptResult run(NavigatorScriptExecutionHarness& harness,
    const std::string& source)
{
    return harness.execute(SourceView(source.data(), source.size()));
}

std::string readFixture()
{
    std::ifstream input("navigator-smoke/javascript-js107.html",
        std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

bool loadDocument(NavigatorScriptExecutionHarness& harness,
    bool executeScript)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string html = readFixture();
    if (html.empty() || !harness.loadHtml("file:///javascript-js107.html",
            html, error) || error != RuntimeErrorCode::None ||
        harness.document().scriptSources.size() != 1u) return false;
    return !executeScript ||
        run(harness, harness.document().scriptSources[0]).succeeded();
}

bool loadFixture(NavigatorScriptExecutionHarness& harness)
{
    return loadDocument(harness, true);
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
        harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0u;
}

const gxos::web::HtmlElementRef* elementBySerial(
    const NavigatorScriptExecutionHarness& harness, std::uint64_t serial)
{
    if (serial == 0u || serial > harness.document().structuralElements.size())
        return nullptr;
    const gxos::web::HtmlElementRef& element =
        harness.document().structuralElements[static_cast<std::size_t>(serial - 1u)];
    return element.serial == serial ? &element : nullptr;
}

bool connected(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    return serial != 0u && serial <= harness.document().contentMetadata.size() &&
        harness.document().contentMetadata[static_cast<std::size_t>(serial - 1u)].serial ==
            serial &&
        harness.document().contentMetadata[static_cast<std::size_t>(serial - 1u)].connected;
}

std::size_t projectionCount(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    std::size_t count = 0u;
    for (const gxos::web::DocBlock& block : harness.document().blocks) {
        if (block.elementMetadata.serial == serial &&
            block.inlineFlowSerial == serial) ++count;
    }
    return count;
}

std::size_t projectionIndex(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    for (std::size_t index = 0u; index < harness.document().blocks.size(); ++index) {
        const gxos::web::DocBlock& block = harness.document().blocks[index];
        if (block.elementMetadata.serial == serial &&
            block.inlineFlowSerial == serial) return index;
    }
    return harness.document().blocks.size();
}

std::uint64_t firstInlineFlow(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    for (const gxos::web::WebInlineItem& item : harness.document().inlineItems) {
        if (item.ownerSerial == serial) return item.flowSerial;
    }
    return 0u;
}

std::string textContent(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    const gxos::web::HtmlElementContentMetadata* content = serial != 0u &&
        serial <= harness.document().contentMetadata.size()
        ? &harness.document().contentMetadata[static_cast<std::size_t>(serial - 1u)]
        : nullptr;
    bool dynamicBlockProjection = false;
    if (content != nullptr && content->serial == serial &&
        content->dynamicallyCreated) {
        for (const gxos::web::DocBlock& block : harness.document().blocks) {
            if (block.elementMetadata.serial == serial &&
                block.inlineFlowSerial == serial) {
                dynamicBlockProjection = true;
                break;
            }
        }
    }
    std::string result;
    if (dynamicBlockProjection) {
        for (const gxos::web::WebInlineItem& item :
            harness.document().inlineItems) {
            bool descendant = item.ownerSerial == serial;
            std::uint64_t current = item.ownerSerial;
            for (std::size_t depth = 0u; !descendant && current != 0u &&
                depth < harness.document().structuralElements.size(); ++depth) {
                const gxos::web::HtmlElementRef* owner =
                    elementBySerial(harness, current);
                if (owner == nullptr) break;
                current = owner->parentSerial;
                descendant = current == serial;
            }
            if (!descendant) continue;
            if (item.kind == gxos::web::InlineItemKind::TextRun)
                result += item.text;
            else if (item.kind == gxos::web::InlineItemKind::ForcedBreak)
                result += '\n';
        }
        return result;
    }
    if (!navigatorScriptElementTextContent(harness.document(), serial, result))
        return std::string();
    return result;
}

bool settle(NavigatorScriptExecutionHarness& harness)
{
    return !harness.documentDirty() || harness.relayout();
}

bool booleanBinding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
    return value != nullptr && value->isBoolean() && value->booleanValue();
}

void testCreationProjectionOrderingAndRelationships()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadDocument(harness, false), "JS107 parsed fixture loads");
    const std::uint64_t container = serialById(harness, "container");
    const std::uint64_t before = serialById(harness, "before");
    const std::uint64_t after = serialById(harness, "after");
    const std::uint64_t other = serialById(harness, "second-container");
    expect(container != 0u && before != 0u && after != 0u && other != 0u,
        "fixture exposes parsed block parents and siblings");

    const std::size_t nodeCount = harness.document().structuralElements.size();
    ScriptResult result = run(harness,
        "var section = document.createElement(\"div\");"
        "section.setAttribute(\"id\", \"created-section\");"
        "section.setAttribute(\"class\", \"created-container\");"
        "var inlineChild = document.createElement(\"span\");"
        "inlineChild.setAttribute(\"class\", \"created-inline\");"
        "inlineChild.textContent = \"Lead\";"
        "var first = document.createElement(\"p\");"
        "first.setAttribute(\"id\", \"created-first\");"
        "first.setAttribute(\"class\", \"created-paragraph\");"
        "first.textContent = \"First block\";"
        "var second = document.createElement(\"p\");"
        "second.setAttribute(\"id\", \"created-second\");"
        "second.setAttribute(\"class\", \"created-paragraph\");"
        "second.textContent = \"Second block\";"
        "var detachedHidden = document.querySelector(\"#created-section\") === null;"
        "var stableSection = section === section;");
    if (!result.succeeded()) {
        std::cerr << "JS107 detached creation detail: runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << " lexer="
            << lexerErrorCodeName(result.lexerError.code) << '\n';
    }
    expect(result.succeeded(), "detached div, p, and span nodes are created");
    expect(harness.document().structuralElements.size() == nodeCount + 4u,
        "each supported element receives one shared registry identity");
    expect(booleanBinding(harness, "detachedHidden") &&
        booleanBinding(harness, "stableSection") &&
        projectionCount(harness, serialById(harness, "created-section")) == 0u,
        "detached nodes are stable but absent from selectors and render blocks");

    const std::uint64_t section = serialById(harness, "created-section");
    const std::uint64_t first = serialById(harness, "created-first");
    const std::uint64_t second = serialById(harness, "created-second");
    const std::uint64_t inlineChild = [&]() {
        for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
            if (element.tagName == "span" && element.className == "created-inline")
                return element.serial;
        }
        return std::uint64_t{0u};
    }();
    result = run(harness,
        "var container = document.getElementById(\"container\");"
        "section.appendChild(inlineChild);"
        "section.appendChild(first);"
        "section.appendChild(second);"
        "var sectionIdentity = container.appendChild(section) === section;"
        "var selectorSection = document.getElementById(\"created-section\") === section;"
        "var selectorDescendant = document.querySelector(\"#created-section .created-paragraph\") === first;"
        "var selectorTag = section.querySelectorAll(\"p\").length === 2;"
        "var childOrder = section.children[0] === inlineChild &&"
        " section.children[1] === first && section.children[2] === second;");
    if (!result.succeeded()) {
        std::cerr << "JS107 connection detail: runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << " lexer="
            << lexerErrorCodeName(result.lexerError.code) << '\n';
    }
    expect(result.succeeded(), "body/div/p/span relationship subset connects");
    expect(connected(harness, section) && connected(harness, first) &&
        connected(harness, second) && connected(harness, inlineChild),
        "connected subtree flags agree with shared structural ancestry");
    expect(booleanBinding(harness, "sectionIdentity") &&
        booleanBinding(harness, "selectorSection") &&
        booleanBinding(harness, "selectorDescendant") &&
        booleanBinding(harness, "selectorTag") && booleanBinding(harness, "childOrder"),
        "ID, tag, class, descendant, scoped, and child-order queries agree");
    result = run(harness,
        "first.setAttribute(\"class\", \"changed-paragraph\");"
        "var changedClassVisible = document.querySelector(\".changed-paragraph\") === first;"
        "first.removeAttribute(\"class\");"
        "var changedClassGone = document.querySelector(\".changed-paragraph\") === null;");
    expect(result.succeeded() && booleanBinding(harness, "changedClassVisible") &&
        booleanBinding(harness, "changedClassGone"),
        "connected attribute mutation updates the shared selector view");
    expect(projectionCount(harness, section) == 1u &&
        projectionCount(harness, first) == 1u &&
        projectionCount(harness, second) == 1u,
        "dynamic div and paragraphs each get one existing DocBlock projection");
    std::size_t connectedDynamicBlockCount = 0u;
    for (const gxos::web::DocBlock& block : harness.document().blocks) {
        if (block.elementMetadata.serial != 0u &&
            block.inlineFlowSerial == block.elementMetadata.serial &&
            (block.tagName == "div" || block.tagName == "p"))
            ++connectedDynamicBlockCount;
    }
    std::cout << "JS107 footprint: record-bytes element="
        << sizeof(gxos::web::HtmlElementRef) << " content="
        << sizeof(gxos::web::HtmlElementContentMetadata) << " block="
        << sizeof(gxos::web::DocBlock) << " inline="
        << sizeof(gxos::web::WebInlineItem) << " RuntimeObject="
        << NavigatorScriptRuntimeTestAccess::runtimeObjectSize()
        << " RuntimeArrayElement="
        << NavigatorScriptRuntimeTestAccess::arrayElementSize()
        << "; fixture-use nodes="
        << harness.document().structuralElements.size() << "/"
        << harness.document().structuralElements.capacity() << " blocks="
        << harness.document().blocks.size() << "/"
        << harness.document().blocks.capacity() << " inline-items="
        << harness.document().inlineItems.size() << "/"
        << harness.document().inlineItems.capacity() << " dynamic-blocks="
        << connectedDynamicBlockCount << "; max dynamic DocBlock record bytes="
        << kNavigatorScriptMaxDocumentNodes * sizeof(gxos::web::DocBlock)
        << '\n';
    expect(projectionIndex(harness, before) < projectionIndex(harness, after) &&
        projectionIndex(harness, after) < projectionIndex(harness, section) &&
        projectionIndex(harness, section) < projectionIndex(harness, first) &&
        projectionIndex(harness, first) < projectionIndex(harness, second),
        "render block order follows current tree order rather than serial allocation");
    expect(firstInlineFlow(harness, first) == first &&
        firstInlineFlow(harness, second) == second &&
        firstInlineFlow(harness, inlineChild) == section,
        "inline projection uses the nearest connected block flow");
    expect(textContent(harness, container).find("LeadFirst blockSecond block") !=
        std::string::npos && settle(harness),
        "connected dynamic text reaches the layout projection");

    result = run(harness,
        "section.appendChild(first);"
        "var reorderChildren = section.children[1] === second &&"
        " section.children[2] === first;");
    expect(result.succeeded() && booleanBinding(harness, "reorderChildren") &&
        projectionIndex(harness, second) < projectionIndex(harness, first),
        "same-parent append updates both sibling and render order");
    const std::uint64_t pOrderBefore = projectionIndex(harness, second);
    expect(settle(harness) && harness.document().blocks.size() > pOrderBefore,
        "repeated layout consumes the ordered projection deterministically");

    const std::size_t nodesBeforeUnsupported =
        harness.document().structuralElements.size();
    result = run(harness,
        "var unsupported = document.createElement(\"article\");");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        harness.document().structuralElements.size() == nodesBeforeUnsupported,
        "unsupported element tags fail without allocating registry storage");

    result = run(harness,
        "var invalidP = document.createElement(\"p\");"
        "var invalidDiv = document.createElement(\"div\");"
        "var rejectedPChild = first.appendChild(invalidP);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        elementBySerial(harness, serialById(harness, "created-first"))->childCount == 0u,
        "paragraph-to-block insertion is rejected before tree mutation");
    result = run(harness,
        "var rejectedDivChild = inlineChild.appendChild(invalidDiv);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        elementBySerial(harness, serialById(harness, "created-first"))->childCount == 0u,
        "span-to-block insertion is rejected before tree mutation");
    result = run(harness, "var rejectedCycle = section.appendChild(section);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::InvalidHostObject,
        "self-insertion fails before changing the tree");

    result = run(harness,
        "var nested = document.createElement(\"div\");"
        "var nestedParagraph = document.createElement(\"p\");"
        "nestedParagraph.textContent = \"Nested block\";"
        "nested.appendChild(nestedParagraph);"
        "var nestedIdentity = section.appendChild(nested) === nested;");
    expect(result.succeeded() && booleanBinding(harness, "nestedIdentity"),
        "dynamic div nesting reuses the same bounded block subset");
    std::uint64_t nestedSerial = 0u;
    std::uint64_t nestedParagraphSerial = 0u;
    for (const gxos::web::HtmlElementRef& element :
        harness.document().structuralElements) {
        if (element.tagName == "div" && element.serial > section &&
            element.id.empty()) nestedSerial = element.serial;
        if (element.tagName == "p" && element.serial > second &&
            element.id.empty()) nestedParagraphSerial = element.serial;
    }
    expect(nestedSerial != 0u && nestedParagraphSerial != 0u &&
        projectionCount(harness, nestedSerial) == 1u &&
        projectionCount(harness, nestedParagraphSerial) == 1u,
        "nested block projections remain connected and serial-addressed");
    expect(projectionIndex(harness, second) < projectionIndex(harness, nestedSerial) &&
        projectionIndex(harness, nestedSerial) <
            projectionIndex(harness, nestedParagraphSerial),
        "nested block flow retains parent-before-descendant order");
    result = run(harness, "var rejectedAncestorCycle = nested.appendChild(section);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        elementBySerial(harness, section)->parentSerial == container &&
        elementBySerial(harness, nestedSerial)->parentSerial == section,
        "ancestor-cycle insertion is rejected without changing either parent");

    result = run(harness,
        "var pageBody = document.getElementById(\"page-body\");"
        "var bodySection = document.createElement(\"div\");"
        "bodySection.setAttribute(\"id\", \"body-section\");"
        "var bodyChild = document.createElement(\"p\");"
        "bodyChild.setAttribute(\"id\", \"body-child\");"
        "bodyChild.textContent = \"Body child\";"
        "bodySection.appendChild(bodyChild);"
        "pageBody.appendChild(bodySection);"
        "var bodyParagraph = document.createElement(\"p\");"
        "bodyParagraph.setAttribute(\"id\", \"body-paragraph\");"
        "bodyParagraph.textContent = \"Direct body paragraph\";"
        "pageBody.appendChild(bodyParagraph);"
        "var parsedContainerParagraph = document.createElement(\"p\");"
        "parsedContainerParagraph.setAttribute(\"id\", \"parsed-container-paragraph\");"
        "parsedContainerParagraph.textContent = \"Parsed container child\";"
        "container.appendChild(parsedContainerParagraph);"
        "var inlineParagraph = document.createElement(\"p\");"
        "inlineParagraph.setAttribute(\"id\", \"inline-paragraph\");"
        "var paragraphSpan = document.createElement(\"span\");"
        "paragraphSpan.textContent = \"Paragraph inline\";"
        "inlineParagraph.appendChild(paragraphSpan);"
        "pageBody.appendChild(inlineParagraph);");
    const std::uint64_t bodySection = serialById(harness, "body-section");
    const std::uint64_t bodyChild = serialById(harness, "body-child");
    const std::uint64_t bodyParagraph = serialById(harness, "body-paragraph");
    const std::uint64_t parsedContainerParagraph = serialById(harness,
        "parsed-container-paragraph");
    const std::uint64_t inlineParagraph = serialById(harness, "inline-paragraph");
    const std::uint64_t paragraphSpan = [&]() {
        for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
            if (element.tagName == "span" &&
                element.serial > nestedParagraphSerial) return element.serial;
        }
        return std::uint64_t{0u};
    }();
    expect(result.succeeded() && bodySection != 0u && bodyChild != 0u &&
        bodyParagraph != 0u && parsedContainerParagraph != 0u &&
        inlineParagraph != 0u && paragraphSpan != 0u,
        "body and parsed-container parent paths accept block insertions");
    expect(projectionCount(harness, bodySection) == 1u &&
        projectionCount(harness, bodyChild) == 1u &&
        projectionCount(harness, bodyParagraph) == 1u &&
        projectionCount(harness, parsedContainerParagraph) == 1u &&
        projectionCount(harness, inlineParagraph) == 1u &&
        firstInlineFlow(harness, paragraphSpan) == inlineParagraph,
        "body div/p, parsed div/p, and dynamic p/span map to the proper flows");
    expect(projectionIndex(harness, bodySection) <
        projectionIndex(harness, bodyChild) &&
        projectionIndex(harness, bodyChild) <
            projectionIndex(harness, bodyParagraph),
        "body-level and nested blocks retain append order in the shared projection");

    std::string deepCreationSource;
    constexpr std::size_t deepBlockCount = 12u;
    for (std::size_t index = 0u; index < deepBlockCount; ++index) {
        const std::string name = "deepBlock" + std::to_string(index);
        deepCreationSource += "var " + name +
            " = document.createElement(\"div\");";
        if (index == 0u)
            deepCreationSource += name +
                ".setAttribute(\"id\", \"too-deep-root\");";
    }
    deepCreationSource +=
        "var deepParagraph = document.createElement(\"p\");";
    result = run(harness, deepCreationSource);
    bool deepStructureReady = result.succeeded();
    for (std::size_t index = 0u; deepStructureReady &&
        index + 1u < deepBlockCount; ++index) {
        result = run(harness, "deepBlock" + std::to_string(index) +
            ".appendChild(deepBlock" + std::to_string(index + 1u) + ");");
        deepStructureReady = result.succeeded();
    }
    if (deepStructureReady) {
        result = run(harness, "deepBlock11.appendChild(deepParagraph);");
        deepStructureReady = result.succeeded();
    }
    if (deepStructureReady)
        result = run(harness,
            "container.appendChild(deepBlock0);");
    const std::uint64_t deepRoot = serialById(harness, "too-deep-root");
    if (result.succeeded() || result.runtimeError.code !=
            RuntimeErrorCode::HostInvalidValue || deepRoot == 0u ||
            elementBySerial(harness, deepRoot)->parentSerial != 0u ||
            projectionCount(harness, deepRoot) != 0u) {
        const gxos::web::HtmlElementRef* rootElement =
            elementBySerial(harness, deepRoot);
        std::cerr << "JS107 deep-bound detail: success=" << result.succeeded()
            << " error=" << runtimeErrorCodeName(result.runtimeError.code)
            << " root=" << deepRoot << " parent="
            << (rootElement == nullptr ? 0u : rootElement->parentSerial)
            << " projections=" << projectionCount(harness, deepRoot)
            << " setup=" << deepStructureReady << '\n';
    }
    expect(deepStructureReady && !result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue && deepRoot != 0u &&
        elementBySerial(harness, deepRoot)->parentSerial == 0u &&
        projectionCount(harness, deepRoot) == 0u,
        "block ancestry beyond the renderer bound rejects before connection");
    (void)other;
}

void testTextMovementRemovalAndAtomicFailures()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadDocument(harness, false), "JS107 parsed fixture reloads for mutation checks");
    ScriptResult result = run(harness,
        "var section = document.createElement(\"div\");"
        "section.setAttribute(\"id\", \"mutation-section\");"
        "var first = document.createElement(\"p\");"
        "first.setAttribute(\"id\", \"mutation-first\");"
        "first.textContent = \"Original\";"
        "var second = document.createElement(\"p\");"
        "second.setAttribute(\"id\", \"mutation-second\");"
        "second.textContent = \"Second\";"
        "section.appendChild(first); section.appendChild(second);"
        "document.getElementById(\"container\").appendChild(section);");
    const std::uint64_t section = serialById(harness, "mutation-section");
    const std::uint64_t first = serialById(harness, "mutation-first");
    const std::uint64_t second = serialById(harness, "mutation-second");
    expect(result.succeeded() && section != 0u && first != 0u && second != 0u &&
        projectionCount(harness, section) == 1u && projectionCount(harness, first) == 1u,
        "connected dynamic subtree has render projections before mutation");
    expect(settle(harness), "initial block layout reaches a clean revision");
    std::uint64_t revision = harness.layoutRevision();

    result = run(harness, "first.textContent = \"Updated\";");
    const std::string updatedText = textContent(harness, first);
    const std::size_t updatedIndex = projectionIndex(harness, first);
    const bool oneProjection = projectionCount(harness, first) == 1u;
    const bool updatedBlock = updatedIndex < harness.document().blocks.size() &&
        harness.document().blocks[updatedIndex].text == "Updated";
    expect(result.succeeded() && updatedText == "Updated" && oneProjection &&
        updatedBlock,
        "paragraph text mutation replaces the existing inline and block output");
    result = run(harness, "var js107ParagraphReadback = first.textContent;");
    const Value* readback = harness.runtime().lookup(SourceView(
        "js107ParagraphReadback", 22u));
    expect(result.succeeded() && readback != nullptr && readback->isString() &&
        harness.runtime().stringValue(*readback) == "Updated",
        "Element.textContent getter reads each dynamic block text exactly once");
    expect(settle(harness) && harness.layoutRevision() > revision,
        "text replacement invalidates and consumes block layout state");
    revision = harness.layoutRevision();

    result = run(harness,
        "var leaf = document.createElement(\"div\");"
        "leaf.textContent = \"Leaf container\";"
        "document.getElementById(\"container\").appendChild(leaf);");
    const std::uint64_t leaf = [&]() {
        for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
            if (element.tagName == "div" && element.serial > section &&
                element.id.empty()) return element.serial;
        }
        return std::uint64_t{0u};
    }();
    expect(result.succeeded() && leaf != 0u &&
        projectionCount(harness, leaf) == 1u && textContent(harness, leaf) ==
            "Leaf container", "leaf dynamic div text has a block projection");

    result = run(harness,
        "var beforeFailedText = section.textContent;"
        "var failedContainerText = section.textContent = \"replacement\";");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        textContent(harness, section) == "UpdatedSecond",
        "container text replacement with descendants rejects without stale projections");
    expect(projectionCount(harness, section) == 1u &&
        projectionCount(harness, first) == 1u &&
        projectionCount(harness, second) == 1u,
        "failed descendant replacement preserves all render projections");

    const std::uint64_t secondContainer = serialById(harness, "second-container");
    result = run(harness,
        "document.getElementById(\"second-container\").appendChild(first);"
        "var oldScopeClear = section.querySelector(\"#mutation-first\") === null;"
        "var newScopeSame = document.getElementById(\"second-container\")"
        ".querySelector(\"#mutation-first\") === first;");
    expect(result.succeeded() && booleanBinding(harness, "oldScopeClear") &&
        booleanBinding(harness, "newScopeSame") &&
        elementBySerial(harness, first)->parentSerial == secondContainer &&
        projectionCount(harness, first) == 1u &&
        projectionIndex(harness, serialById(harness, "second-before")) <
            projectionIndex(harness, first),
        "reparenting preserves identity, selector ancestry, and new render order");
    expect(settle(harness) && harness.layoutRevision() > revision,
        "reparenting invalidates old and new block layout projections");
    revision = harness.layoutRevision();

    result = run(harness,
        "section.removeChild(second);"
        "var removedHidden = document.querySelector(\"#mutation-second\") === null;"
        "var retainedText = second.textContent;");
    expect(result.succeeded() && booleanBinding(harness, "removedHidden") &&
        !connected(harness, second) && projectionCount(harness, second) == 0u &&
        textContent(harness, second) == "Second",
        "block removal hides output and selectors while retaining node identity/text");
    expect(settle(harness) && harness.layoutRevision() > revision,
        "block removal closes remaining layout and advances the revision");
    revision = harness.layoutRevision();
    result = run(harness,
        "section.appendChild(second);"
        "var restoredSame = document.querySelector(\"#mutation-second\") === second;");
    expect(result.succeeded() && booleanBinding(harness, "restoredSame") &&
        connected(harness, second) && projectionCount(harness, second) == 1u &&
        elementBySerial(harness, second)->parentSerial == section &&
        textContent(harness, second) == "Second",
        "reinsertion restores the same paragraph, text, selectors, and projection");
    expect(settle(harness) && harness.layoutRevision() > revision,
        "paragraph reinsertion rebuilds a single current block projection");
    result = run(harness,
        "section.appendChild(first);"
        "section.appendChild(second);"
        "var sameParentOrder = section.children[0] === first &&"
        " section.children[1] === second;");
    expect(result.succeeded() && booleanBinding(harness, "sameParentOrder") &&
        elementBySerial(harness, first)->parentSerial == section &&
        elementBySerial(harness, second)->parentSerial == section &&
        projectionIndex(harness, first) < projectionIndex(harness, second) &&
        projectionCount(harness, first) == 1u &&
        projectionCount(harness, second) == 1u,
        "moving back and appending a same-parent child reorders one projection each");
    expect(settle(harness) && harness.layoutRevision() > revision,
        "same-parent reorder updates layout revision");
    revision = harness.layoutRevision();

    result = run(harness,
        "document.getElementById(\"container\").removeChild(section);"
        "var subtreeHidden = document.querySelector(\"#mutation-section\") === null;"
        "var descendantHidden = document.querySelector(\"#mutation-second\") === null;");
    expect(result.succeeded() && booleanBinding(harness, "subtreeHidden") &&
        booleanBinding(harness, "descendantHidden") && !connected(harness, section) &&
        !connected(harness, second) && projectionCount(harness, section) == 0u &&
        projectionCount(harness, second) == 0u,
        "removing a dynamic div disconnects descendants and removes their projections");
    result = run(harness,
        "document.getElementById(\"container\").appendChild(section);"
        "var subtreeRestored = document.querySelector(\"#mutation-section #mutation-second\") === second;");
    expect(result.succeeded() && booleanBinding(harness, "subtreeRestored") &&
        connected(harness, section) && connected(harness, second) &&
        projectionCount(harness, section) == 1u && projectionCount(harness, second) == 1u,
        "subtree reinsertion restores descendant queries and one projection per block");
    expect(settle(harness) && harness.layoutRevision() > revision,
        "subtree connection rebuilds layout without accumulating old blocks");
    const std::size_t blockCount = harness.document().blocks.size();
    expect(harness.relayout() && harness.document().blocks.size() == blockCount &&
        projectionCount(harness, section) == 1u && projectionCount(harness, second) == 1u,
        "repeated layout does not duplicate dynamic block projections");
}

void testCallbacksAndCapacityFailures()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadFixture(harness), "JS107 event fixture loads");
    const std::uint64_t addButton = serialById(harness, "add-button");
    const std::uint64_t updateButton = serialById(harness, "update-button");
    const std::uint64_t removeButton = serialById(harness, "remove-button");
    const std::uint64_t reinsertButton = serialById(harness, "reinsert-button");
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(addButton != 0u && harness.dispatchClick(addButton, error) &&
        error == RuntimeErrorCode::None && settle(harness),
        "existing button callback connects and renders dynamic blocks");
    const std::uint64_t section = serialById(harness, "dynamic-section");
    const std::uint64_t first = serialById(harness, "dynamic-first");
    const std::uint64_t second = serialById(harness, "dynamic-second");
    expect(section != 0u && first != 0u && second != 0u &&
        projectionCount(harness, section) == 1u &&
        projectionCount(harness, first) == 1u &&
        projectionCount(harness, second) == 1u &&
        projectionIndex(harness, first) < projectionIndex(harness, second),
        "callback-generated paragraphs appear once in source order");
    const std::size_t afterFirstClick = harness.document().blocks.size();
    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(addButton, error) && error == RuntimeErrorCode::None &&
        settle(harness) && harness.document().blocks.size() == afterFirstClick &&
        projectionCount(harness, first) == 1u,
        "repeated Add callback does not duplicate nodes or rendered blocks");

    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(updateButton, error) && error == RuntimeErrorCode::None &&
        settle(harness) && textContent(harness, first) ==
            "Updated first dynamic paragraph",
        "Update callback replaces paragraph text in the rendered flow");
    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(removeButton, error) && error == RuntimeErrorCode::None &&
        settle(harness) && !connected(harness, first) && projectionCount(harness, first) == 0u,
        "Remove callback detaches the paragraph and removes its projection");
    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(reinsertButton, error) &&
        error == RuntimeErrorCode::None && settle(harness) && connected(harness, first) &&
        projectionCount(harness, first) == 1u &&
        serialById(harness, "dynamic-first") == first,
        "Reinsert callback restores the original paragraph identity");
    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(removeButton, error) && error == RuntimeErrorCode::None &&
        settle(harness) && !connected(harness, section) && !connected(harness, second) &&
        projectionCount(harness, section) == 0u && projectionCount(harness, second) == 0u,
        "second Remove callback disconnects the complete dynamic section");
    error = RuntimeErrorCode::None;
    const bool sectionReinserted = harness.dispatchClick(reinsertButton, error) &&
        error == RuntimeErrorCode::None && settle(harness) && connected(harness, section) &&
        connected(harness, first) && connected(harness, second) &&
        projectionCount(harness, first) == 1u && projectionCount(harness, second) == 1u;
    if (!sectionReinserted) {
        std::cerr << "JS107 callback reinsert detail: error="
            << runtimeErrorCodeName(error) << " section=" << connected(harness, section)
            << "/" << projectionCount(harness, section) << " first="
            << connected(harness, first) << "/" << projectionCount(harness, first)
            << " second=" << connected(harness, second) << "/"
            << projectionCount(harness, second) << " parent=";
        const gxos::web::HtmlElementRef* node = elementBySerial(harness, section);
        std::cerr << (node == nullptr ? 0u : node->parentSerial) << '\n';
    }
    expect(sectionReinserted,
        "second Reinsert callback restores the same supported subtree");

    const std::string html = "<html><body><div id=\"host\"><p>base</p></div></body></html>";
    NavigatorScriptExecutionHarness baselineHarness;
    error = RuntimeErrorCode::None;
    expect(baselineHarness.loadHtml("file:///js107-capacity.html", html, error),
        "mixed parsed/dynamic capacity baseline parses");
    const std::size_t baselineNodes =
        baselineHarness.document().structuralElements.size();
    NavigatorScriptHostLimits oneNode;
    oneNode.maxDocumentNodes = baselineNodes + 1u;
    NavigatorScriptExecutionHarness nodeLimited(RuntimeLimits(), oneNode);
    error = RuntimeErrorCode::None;
    expect(nodeLimited.loadHtml("file:///js107-capacity.html", html, error),
        "bounded dynamic block node pool loads");
    ScriptResult capacity = run(nodeLimited,
        "var lastBlockSlot = document.createElement(\"p\");"
        "lastBlockSlot.textContent = \"kept detached\";");
    expect(capacity.succeeded(), "last available dynamic block slot is usable");
    const std::size_t fullNodes = nodeLimited.document().structuralElements.size();
    capacity = run(nodeLimited,
        "var beyondBlockPool = document.createElement(\"div\");");
    expect(!capacity.succeeded() && capacity.runtimeError.code ==
        RuntimeErrorCode::DocumentLookupLimitExceeded &&
        nodeLimited.document().structuralElements.size() == fullNodes &&
        projectionCount(nodeLimited, fullNodes) == 0u,
        "node exhaustion fails before registry or render projection mutation");

    NavigatorScriptHostLimits oneMutation;
    oneMutation.maxDocumentMutations = 1u;
    NavigatorScriptExecutionHarness mutationLimited(RuntimeLimits(), oneMutation);
    error = RuntimeErrorCode::None;
    expect(mutationLimited.loadHtml("file:///js107-mutation.html", html, error),
        "mixed-content mutation quota fixture loads");
    capacity = run(mutationLimited,
        "var quotaBlock = document.createElement(\"div\");"
        "document.getElementById(\"host\").appendChild(quotaBlock);");
    const std::size_t expectedQuotaNodes = mutationLimited.document().
        structuralElements.size();
    expect(!capacity.succeeded() && capacity.runtimeError.code ==
        RuntimeErrorCode::DocumentMutationLimitExceeded &&
        expectedQuotaNodes == baselineNodes + 1u &&
        projectionCount(mutationLimited,
            static_cast<std::uint64_t>(expectedQuotaNodes)) == 0u,
        "mutation quota rejects connection without publishing a partial projection");

    std::string flowHtml = "<html><body><div id=\"flow-host\">";
    for (std::size_t index = 0u;
        index < gxos::web::kWebMaxInlineLayoutFlows; ++index)
        flowHtml += "<p>Parsed flow " + std::to_string(index) + "</p>";
    flowHtml += "</div></body></html>";
    NavigatorScriptExecutionHarness flowLimited;
    error = RuntimeErrorCode::None;
    expect(flowLimited.loadHtml("file:///js107-flow-capacity.html",
        flowHtml, error), "existing inline-flow capacity fixture parses");
    capacity = run(flowLimited,
        "var beyondFlowCapacity = document.createElement(\"p\");"
        "beyondFlowCapacity.setAttribute(\"id\", \"beyond-flow-cap\");"
        "document.getElementById(\"flow-host\").appendChild(beyondFlowCapacity);");
    const std::uint64_t beyondFlowSerial = serialById(flowLimited,
        "beyond-flow-cap");
    const gxos::web::HtmlElementRef* beyondFlow =
        elementBySerial(flowLimited, beyondFlowSerial);
    expect(!capacity.succeeded() && capacity.runtimeError.code ==
        RuntimeErrorCode::DocumentMutationLimitExceeded && beyondFlow != nullptr &&
        beyondFlow->parentSerial == 0u &&
        projectionCount(flowLimited, beyondFlowSerial) == 0u,
        "dynamic block flow exhaustion rejects connection before projection mutation");

    NavigatorScriptExecutionHarness staleHarness;
    expect(loadFixture(staleHarness), "generation fixture loads before stale block test");
    ScriptResult staleCreate = run(staleHarness,
        "var staleParent = document.getElementById(\"container\");"
        "var staleSection = document.createElement(\"div\");"
        "staleSection.setAttribute(\"id\", \"stale-section\");"
        "var staleDescendant = document.createElement(\"p\");"
        "staleDescendant.setAttribute(\"id\", \"stale-descendant\");"
        "staleDescendant.textContent = \"old document\";"
        "staleSection.appendChild(staleDescendant);"
        "staleParent.appendChild(staleSection);");
    const std::uint64_t staleParentSerial = serialById(staleHarness, "container");
    const std::uint64_t staleSectionSerial = serialById(staleHarness, "stale-section");
    const std::uint64_t staleDescendantSerial = serialById(staleHarness,
        "stale-descendant");
    std::uint64_t staleDetachedSerial = 0u;
    HostGenerationId oldGeneration = staleHarness.hostAdapter().generation();
    ScriptResult staleDetachedCreate = run(staleHarness,
        "var staleDetached = document.createElement(\"p\");");
    staleDetachedSerial = staleHarness.document().structuralElements.empty()
        ? 0u : staleHarness.document().structuralElements.back().serial;
    const std::array<HostObjectReference, 4u> staleReferences{{
        {staleParentSerial, oldGeneration, kNavigatorElementHostKind},
        {staleSectionSerial, oldGeneration, kNavigatorElementHostKind},
        {staleDescendantSerial, oldGeneration, kNavigatorElementHostKind},
        {staleDetachedSerial, oldGeneration, kNavigatorElementHostKind}}};
    error = RuntimeErrorCode::None;
    const bool generationInvalidated =
        staleCreate.succeeded() && staleDetachedCreate.succeeded() &&
        staleHarness.invalidateDocumentGeneration(error) &&
        error == RuntimeErrorCode::None;
    bool allReferencesStale = generationInvalidated;
    for (const HostObjectReference& reference : staleReferences) {
        allReferencesStale = allReferencesStale &&
            staleHarness.hostAdapter().validate(reference).code ==
                HostResultCode::StaleObject;
    }
    expect(generationInvalidated && allReferencesStale,
        "old parent, subtree, descendant, and detached handles fail generation validation");
}

} // namespace

int main()
{
    testCreationProjectionOrderingAndRelationships();
    testTextMovementRemovalAndAtomicFailures();
    testCallbacksAndCapacityFailures();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS107 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS107 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
