#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

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
    ScriptResult result = harness.execute(SourceView(source.data(), source.size()));
    if (!result.succeeded()) {
        std::cerr << "JS108 script detail: runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << " at="
            << result.parserError.location.line << ':'
            << result.parserError.location.column << " actual="
            << tokenTypeName(result.parserError.actual) << " expected="
            << tokenTypeName(result.parserError.expected) << " lexer="
            << lexerErrorCodeName(result.lexerError.code) << " source="
            << source << '\n';
    }
    return result;
}

std::string readFixture()
{
    std::ifstream input("navigator-smoke/javascript-js108.html",
        std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

bool loadDocument(NavigatorScriptExecutionHarness& harness,
    bool executeScript)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string html = readFixture();
    if (html.empty() || !harness.loadHtml("file:///javascript-js108.html",
            html, error) || error != RuntimeErrorCode::None ||
        harness.document().scriptSources.size() != 1u) return false;
    return !executeScript || run(harness,
        harness.document().scriptSources[0]).succeeded();
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
        harness.document().structuralElements[static_cast<std::size_t>(
            serial - 1u)];
    return element.serial == serial ? &element : nullptr;
}

bool connected(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    if (serial == 0u || serial > harness.document().contentMetadata.size())
        return false;
    const gxos::web::HtmlElementContentMetadata& content =
        harness.document().contentMetadata[static_cast<std::size_t>(
            serial - 1u)];
    return content.serial == serial && content.connected;
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
    for (std::size_t index = 0u;
        index < harness.document().blocks.size(); ++index) {
        const gxos::web::DocBlock& block = harness.document().blocks[index];
        if (block.elementMetadata.serial == serial &&
            block.inlineFlowSerial == serial) return index;
    }
    return harness.document().blocks.size();
}

std::size_t inlineItemIndex(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    for (std::size_t index = 0u;
        index < harness.document().inlineItems.size(); ++index) {
        if (harness.document().inlineItems[index].ownerSerial == serial)
            return index;
    }
    return harness.document().inlineItems.size();
}

std::string textContent(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    std::string text;
    const gxos::web::HtmlElementRef* element = elementBySerial(harness, serial);
    if (element == nullptr || !connected(harness, serial)) return text;
    const gxos::web::HtmlElementContentMetadata& content =
        harness.document().contentMetadata[static_cast<std::size_t>(serial - 1u)];
    bool dynamicBlockProjection = false;
    if (content.dynamicallyCreated &&
        (element->tagName == "div" || element->tagName == "p")) {
        for (const gxos::web::DocBlock& block : harness.document().blocks) {
            if (block.elementMetadata.serial == serial &&
                block.inlineFlowSerial == serial) {
                dynamicBlockProjection = true;
                break;
            }
        }
    }
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
                text += item.text;
            else if (item.kind == gxos::web::InlineItemKind::ForcedBreak)
                text += '\n';
        }
        return text;
    }
    if (!navigatorScriptElementTextContent(harness.document(), serial, text))
        return std::string();
    return text;
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

std::string stateFingerprint(const NavigatorScriptExecutionHarness& harness)
{
    const gxos::web::WebDocument& document = harness.document();
    std::string state;
    for (const gxos::web::HtmlElementRef& element :
        document.structuralElements) {
        state += "E" + std::to_string(element.serial) + ":" +
            std::to_string(element.parentSerial) + ":" +
            std::to_string(element.childIndex) + ":" +
            std::to_string(element.childCount) + ";";
    }
    for (const gxos::web::HtmlElementContentMetadata& content :
        document.contentMetadata) {
        state += content.connected ? "C" : "D";
    }
    for (const gxos::web::DocBlock& block : document.blocks) {
        state += "B" + std::to_string(block.elementMetadata.serial) + ":" +
            std::to_string(block.inlineFlowSerial) + ":" + block.text + ";";
    }
    for (const gxos::web::WebInlineItem& item : document.inlineItems) {
        state += "I" + std::to_string(static_cast<unsigned>(item.kind)) + ":" +
            std::to_string(item.flowSerial) + ":" +
            std::to_string(item.ownerSerial) + ":" + item.text + ";";
    }
    state += document.layoutDirty ? "dirty" : "clean";
    state += ":" + std::to_string(document.layoutRevision) + ":" +
        std::to_string(document.scriptMutationCount);
    return state;
}

void testInsertOrderIdentityAndSelectors()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadDocument(harness, false), "JS108 parsed fixture loads");
    const std::uint64_t host = serialById(harness, "host");
    const std::uint64_t first = serialById(harness, "first");
    const std::uint64_t middle = serialById(harness, "middle");
    const std::uint64_t last = serialById(harness, "last");
    expect(host != 0u && first != 0u && middle != 0u && last != 0u,
        "fixture exposes parsed host and three ordered paragraph references");
    expect(settle(harness), "initial parsed blocks reach a clean layout revision");

    ScriptResult result = run(harness,
        "var first = document.getElementById(\"first\");"
        "var middle = document.getElementById(\"middle\");"
        "var last = document.getElementById(\"last\");"
        "var host = document.getElementById(\"host\");"
        "var inserted = document.createElement(\"p\");"
        "inserted.setAttribute(\"id\", \"inserted\");"
        "inserted.textContent = \"Inserted paragraph\";");
    const std::uint64_t inserted = serialById(harness, "inserted");
    expect(result.succeeded() && inserted > last && !connected(harness, inserted),
        "detached dynamic paragraph keeps a new stable serial identity");
    const std::size_t mutationsBeforeInsert =
        harness.document().scriptMutationCount;
    const std::uint64_t revisionBeforeInsert = harness.layoutRevision();
    result = run(harness,
        "var returnedInserted = host.insertBefore(inserted,"
        " document.getElementById(\"last\")) === inserted;"
        "var parsedReferenceOrder = host.querySelectorAll(\"p\")[0] ==="
        " document.getElementById(\"first\") &&"
        " host.querySelectorAll(\"p\")[2] === inserted &&"
        " document.getElementById(\"inserted\") === inserted;");
    expect(result.succeeded() && booleanBinding(harness, "returnedInserted") &&
        booleanBinding(harness, "parsedReferenceOrder") &&
        harness.document().scriptMutationCount == mutationsBeforeInsert + 1u &&
        elementBySerial(harness, inserted)->parentSerial == host &&
        elementBySerial(harness, inserted)->childIndex == 3u,
        "insertBefore returns the same node and orders dynamic content before a parsed reference");
    expect(projectionCount(harness, inserted) == 1u &&
        projectionIndex(harness, first) < projectionIndex(harness, middle) &&
        projectionIndex(harness, middle) < projectionIndex(harness, inserted) &&
        projectionIndex(harness, inserted) < projectionIndex(harness, last) &&
        textContent(harness, inserted) == "Inserted paragraph" &&
        harness.documentDirty() && settle(harness) &&
        harness.layoutRevision() > revisionBeforeInsert,
        "block projection and layout revision follow the inserted document order");

    const std::size_t beforeMoveCount = harness.document().scriptMutationCount;
    result = run(harness,
        "var sameInserted = host.insertBefore(inserted,"
        " host.firstElementChild.nextElementSibling) === inserted;"
        "var insertedMovedForward = host.querySelectorAll(\"p\")[1] === inserted;");
    expect(result.succeeded() && booleanBinding(harness, "sameInserted") &&
        booleanBinding(harness, "insertedMovedForward") &&
        harness.document().scriptMutationCount == beforeMoveCount + 1u &&
        projectionIndex(harness, inserted) < projectionIndex(harness, middle),
        "same-parent movement uses the reference identity rather than stale indices");

    const std::size_t beforeNullMoveCount = harness.document().scriptMutationCount;
    result = run(harness,
        "host.insertBefore(inserted, null);"
        "var insertedAtEnd = host.lastElementChild === inserted;");
    expect(result.succeeded() && booleanBinding(harness, "insertedAtEnd") &&
        harness.document().scriptMutationCount == beforeNullMoveCount + 1u &&
        projectionIndex(harness, last) < projectionIndex(harness, inserted),
        "explicit null moves an existing child to the end");

    const std::string beforeNoop = stateFingerprint(harness);
    result = run(harness,
        "var lastNodeNoop = host.insertBefore(inserted, null) === inserted;"
        "var selfNodeNoop = host.insertBefore(middle, middle) === middle;"
        "var adjacentNodeNoop = host.insertBefore(last, inserted) === last;");
    expect(result.succeeded() && booleanBinding(harness, "lastNodeNoop") &&
        booleanBinding(harness, "selfNodeNoop") &&
        booleanBinding(harness, "adjacentNodeNoop") &&
        stateFingerprint(harness) == beforeNoop,
        "already-last, self-reference, and immediate-next-sibling operations are no-ops");

    result = run(harness,
        "host.insertBefore(inserted, host.firstElementChild);"
        "var lastDynamicMovedFirst = host.querySelectorAll(\"p\")[0] === inserted;"
        "host.insertBefore(inserted, last);"
        "var dynamicMovedForward = host.querySelectorAll(\"p\")[2] === inserted;"
        "host.insertBefore(inserted, middle);"
        "var dynamicMovedBackward = host.querySelectorAll(\"p\")[1] === inserted;"
        "host.insertBefore(inserted, null);"
        "var dynamicMovedLast = host.lastElementChild === inserted;");
    expect(result.succeeded() && booleanBinding(harness, "lastDynamicMovedFirst") &&
        booleanBinding(harness, "dynamicMovedForward") &&
        booleanBinding(harness, "dynamicMovedBackward") &&
        booleanBinding(harness, "dynamicMovedLast") &&
        projectionIndex(harness, last) < projectionIndex(harness, inserted),
        "first-to-last and middle/last forward and backward moves preserve identities");
    expect(settle(harness), "reordered block projections settle after layout");

    const std::string orderBeforeFailure = stateFingerprint(harness);
    result = run(harness,
        "host.insertBefore(inserted,"
        " document.getElementById(\"other-first\"));");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        stateFingerprint(harness) == orderBeforeFailure,
        "a reference from another parent fails without structural, render, quota, or layout changes");
    result = run(harness, "host.insertBefore(inserted);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::TypeError &&
        stateFingerprint(harness) == orderBeforeFailure,
        "missing reference argument is rejected without changing document state");
    result = run(harness, "host.insertBefore(inserted, {});");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        stateFingerprint(harness) == orderBeforeFailure,
        "a non-node reference is rejected instead of treated as null");
    result = run(harness,
        "host.insertBefore(document.getElementById(\"first\"), null);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        stateFingerprint(harness) == orderBeforeFailure,
        "parser-owned nodes cannot be moved through the dynamic mutation API");
}

void testInlineOrderingAndMixedContentAtomicity()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadDocument(harness, false), "inline-order fixture loads");
    ScriptResult result = run(harness,
        "var host = document.getElementById(\"host\");"
        "var inlineHost = document.createElement(\"div\");"
        "inlineHost.setAttribute(\"id\", \"inline-host\");"
        "var inlineParent = document.createElement(\"p\");"
        "inlineParent.setAttribute(\"id\", \"inline-parent\");"
        "inlineHost.appendChild(inlineParent);"
        "document.getElementById(\"page-body\").appendChild(inlineHost);");
    result = run(harness,
        "var lead = document.createElement(\"span\");"
        "lead.setAttribute(\"id\", \"lead-span\");"
        "lead.textContent = \"Lead\";"
        "inlineHost.insertBefore(lead, inlineParent);");
    result = run(harness,
        "var spanSecond = document.createElement(\"span\");"
        "spanSecond.setAttribute(\"id\", \"span-second\");"
        "spanSecond.textContent = \"Second\";"
        "inlineParent.appendChild(spanSecond);");
    result = run(harness,
        "var spanFirst = document.createElement(\"span\");"
        "spanFirst.setAttribute(\"id\", \"span-first\");"
        "spanFirst.textContent = \"First\";"
        "inlineParent.appendChild(spanFirst);");
    const std::uint64_t lead = serialById(harness, "lead-span");
    const std::uint64_t inlineParent = serialById(harness, "inline-parent");
    const std::uint64_t spanFirst = serialById(harness, "span-first");
    const std::uint64_t spanSecond = serialById(harness, "span-second");
    const std::uint64_t inlineHost = serialById(harness, "inline-host");
    expect(result.succeeded() && connected(harness, lead) &&
        connected(harness, inlineParent) &&
        elementBySerial(harness, lead)->parentSerial == inlineHost &&
        projectionCount(harness, inlineParent) == 1u,
        "span-before-block is represented by the existing div inline flow");
    expect(textContent(harness, inlineParent) == "SecondFirst",
        "initial inline-item order follows the dynamic child order");
    result = run(harness,
        "var inlineReturn = inlineParent.insertBefore(spanFirst, spanSecond)"
        " === spanFirst;"
        "var inlineChildren = inlineParent.children[0] === spanFirst &&"
        " inlineParent.children[1] === spanSecond;");
    expect(result.succeeded() && booleanBinding(harness, "inlineReturn") &&
        booleanBinding(harness, "inlineChildren") &&
        textContent(harness, inlineParent) == "FirstSecond" &&
        inlineItemIndex(harness, spanFirst) < inlineItemIndex(harness, spanSecond),
        "inline run projection follows reordered span identities");

    const std::string beforeInvalidMixedMove = stateFingerprint(harness);
    result = run(harness, "inlineHost.insertBefore(lead, null);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        stateFingerprint(harness) == beforeInvalidMixedMove,
        "moving a span after direct blocks is rejected atomically");
    result = run(harness,
        "var trailingSpan = document.createElement(\"span\");"
        "trailingSpan.textContent = \"Unsupported tail\";");
    const std::uint64_t trailingSpan = harness.document().structuralElements.back().serial;
    const std::string beforeTrailingFailure = stateFingerprint(harness);
    result = run(harness, "inlineHost.insertBefore(trailingSpan, null);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        elementBySerial(harness, trailingSpan)->parentSerial == 0u &&
        stateFingerprint(harness) == beforeTrailingFailure,
        "a new span cannot bypass the div block/inline ordering restriction");
    expect(settle(harness), "inline and block projections complete layout");
}

void testCrossParentSubtreeAndCycles()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadDocument(harness, false), "cross-parent fixture loads");
    ScriptResult result = run(harness,
        "var left = document.getElementById(\"host\");"
        "var right = document.getElementById(\"other-host\");"
        "var moving = document.createElement(\"p\");"
        "moving.setAttribute(\"id\", \"moving\");"
        "moving.textContent = \"Moving paragraph\";"
        "left.appendChild(moving);");
    const std::uint64_t moving = serialById(harness, "moving");
    const std::uint64_t left = serialById(harness, "host");
    const std::uint64_t right = serialById(harness, "other-host");
    expect(result.succeeded() && projectionCount(harness, moving) == 1u,
        "dynamic block connects with one render projection before transfer");
    result = run(harness,
        "var movedReference = right.querySelector(\"#other-last\");"
        "var movedIdentity = right.insertBefore(moving, movedReference) === moving;"
        "var oldParentClear = left.querySelector(\"#moving\") === null;"
        "var newParentSame = right.querySelector(\"#moving\") === moving &&"
        " moving.parentElement === right;");
    const std::uint64_t otherFirst = serialById(harness, "other-first");
    const std::uint64_t otherLast = serialById(harness, "other-last");
    expect(result.succeeded() && booleanBinding(harness, "movedIdentity") &&
        booleanBinding(harness, "oldParentClear") &&
        booleanBinding(harness, "newParentSame") &&
        elementBySerial(harness, moving)->parentSerial == right &&
        projectionCount(harness, moving) == 1u &&
        projectionIndex(harness, otherFirst) < projectionIndex(harness, moving) &&
        projectionIndex(harness, moving) < projectionIndex(harness, otherLast) &&
        textContent(harness, moving) == "Moving paragraph",
        "cross-parent movement preserves attributes, text, identity, selector ancestry, and render order");
    expect(settle(harness), "both source and destination block regions relayout");

    result = run(harness,
        "var subtree = document.createElement(\"div\");"
        "subtree.setAttribute(\"id\", \"subtree\");"
        "var descendant = document.createElement(\"p\");"
        "descendant.setAttribute(\"id\", \"subtree-descendant\");"
        "descendant.textContent = \"Nested text survives moves\";"
        "subtree.appendChild(descendant);"
        "var detachedInvisible = document.querySelector(\"#subtree\") === null &&"
        " document.querySelector(\"#subtree-descendant\") === null;"
        "left.insertBefore(subtree, left.firstElementChild);"
        "var connectedSame = document.querySelector(\"#subtree-descendant\") === descendant;");
    const std::uint64_t subtree = serialById(harness, "subtree");
    const std::uint64_t descendant = serialById(harness, "subtree-descendant");
    expect(result.succeeded() && booleanBinding(harness, "detachedInvisible") &&
        booleanBinding(harness, "connectedSame") && connected(harness, subtree) &&
        connected(harness, descendant) && projectionCount(harness, subtree) == 1u &&
        projectionCount(harness, descendant) == 1u,
        "detached subtree insertion restores descendants without cloning projections");
    result = run(harness,
        "right.insertBefore(subtree, right.querySelector(\"#other-first\"));"
        "var movedDescendantSame = document.querySelector(\"#subtree-descendant\") === descendant &&"
        " descendant.parentElement === subtree;"
        "right.removeChild(subtree);"
        "var subtreeHidden = document.querySelector(\"#subtree\") === null &&"
        " document.querySelector(\"#subtree-descendant\") === null;"
        "right.insertBefore(subtree, right.querySelector(\"#other-last\"));"
        "var subtreeRestored = document.querySelector(\"#subtree-descendant\") === descendant;");
    expect(result.succeeded() && booleanBinding(harness, "movedDescendantSame") &&
        booleanBinding(harness, "subtreeHidden") &&
        booleanBinding(harness, "subtreeRestored") &&
        elementBySerial(harness, subtree)->parentSerial == right &&
        projectionCount(harness, subtree) == 1u &&
        projectionCount(harness, descendant) == 1u &&
        textContent(harness, descendant) == "Nested text survives moves",
        "cross-parent subtree move, disconnect, and reinsertion retain descendant identities and text");
    expect(settle(harness), "subtree removal and reinsertion rebuild one clean render tree");

    result = run(harness,
        "var cycleRoot = document.createElement(\"div\");"
        "var cycleBranch = document.createElement(\"div\");"
        "var cycleLeaf = document.createElement(\"div\");"
        "cycleRoot.appendChild(cycleBranch);"
        "cycleBranch.appendChild(cycleLeaf);");
    const std::uint64_t cycleRoot = harness.document().structuralElements.back().serial - 2u;
    const std::uint64_t cycleBranch = cycleRoot + 1u;
    const std::uint64_t cycleLeaf = cycleRoot + 2u;
    const std::string beforeCycle = stateFingerprint(harness);
    result = run(harness, "cycleLeaf.insertBefore(cycleRoot, null);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        stateFingerprint(harness) == beforeCycle &&
        elementBySerial(harness, cycleBranch)->parentSerial == cycleRoot &&
        elementBySerial(harness, cycleLeaf)->parentSerial == cycleBranch,
        "ancestor-under-descendant insertion is rejected before changing the tree");
    result = run(harness, "cycleRoot.insertBefore(cycleRoot, null);");
    expect(!result.succeeded() && stateFingerprint(harness) == beforeCycle,
        "self-parenting does not detach or duplicate the node");
    (void)left;
    (void)moving;
}

void testQuotaFailureAndStaleGeneration()
{
    RuntimeLimits runtimeLimits;
    NavigatorScriptHostLimits hostLimits;
    hostLimits.maxDocumentMutations = 2u;
    NavigatorScriptExecutionHarness quotaHarness(runtimeLimits, hostLimits);
    expect(loadDocument(quotaHarness, false), "mutation-quota fixture loads");
    ScriptResult result = run(quotaHarness,
        "var host = document.getElementById(\"host\");"
        "var quotaNode = document.createElement(\"p\");"
        "host.appendChild(quotaNode);");
    const std::uint64_t quotaNode = quotaHarness.document().structuralElements.back().serial;
    expect(result.succeeded() && quotaHarness.document().scriptMutationCount == 2u,
        "append and creation share the existing document mutation quota");
    const std::string beforeQuotaNoop = stateFingerprint(quotaHarness);
    result = run(quotaHarness,
        "var quotaNoop = host.insertBefore(quotaNode, quotaNode) === quotaNode;");
    expect(result.succeeded() && booleanBinding(quotaHarness, "quotaNoop") &&
        stateFingerprint(quotaHarness) == beforeQuotaNoop,
        "self-reference remains a no-op after the mutation quota is exhausted");
    const std::string beforeQuotaFailure = stateFingerprint(quotaHarness);
    result = run(quotaHarness,
        "host.insertBefore(quotaNode, host.firstElementChild);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::DocumentMutationLimitExceeded &&
        elementBySerial(quotaHarness, quotaNode)->parentSerial ==
            serialById(quotaHarness, "host") &&
        stateFingerprint(quotaHarness) == beforeQuotaFailure,
        "a new reorder at quota exhaustion leaves structure and renderer unchanged");

    NavigatorScriptExecutionHarness staleHarness;
    expect(loadDocument(staleHarness, false), "stale-handle fixture loads");
    result = run(staleHarness,
        "var staleHost = document.getElementById(\"host\");"
        "var staleNode = document.createElement(\"p\");"
        "staleHost.appendChild(staleNode);"
        "var staleReference = staleHost.firstElementChild;");
    const std::uint64_t staleHost = serialById(staleHarness, "host");
    const std::uint64_t staleNode = staleHarness.document().structuralElements.back().serial;
    const std::uint64_t staleReference = serialById(staleHarness, "first");
    const HostGenerationId generation = staleHarness.hostAdapter().generation();
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(result.succeeded() && staleHarness.invalidateDocumentGeneration(error) &&
        error == RuntimeErrorCode::None &&
        staleHarness.hostAdapter().validate(HostObjectReference{
            staleHost, generation, kNavigatorElementHostKind}).code ==
            HostResultCode::StaleObject &&
        staleHarness.hostAdapter().validate(HostObjectReference{
            staleNode, generation, kNavigatorElementHostKind}).code ==
            HostResultCode::StaleObject &&
        staleHarness.hostAdapter().validate(HostObjectReference{
            staleReference, generation, kNavigatorElementHostKind}).code ==
            HostResultCode::StaleObject,
        "parent, new-node, and reference handles fail generation validation after navigation");
    const std::string beforeStaleCall = stateFingerprint(staleHarness);
    result = run(staleHarness,
        "staleHost.insertBefore(staleNode, staleReference);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::StaleHostObject &&
        stateFingerprint(staleHarness) == beforeStaleCall,
        "a retained stale insertion call fails without touching the replacement generation");
}

void testFlowCapacityFailure()
{
    std::string html = "<html><body><div id=\"capacity-host\">";
    for (std::size_t index = 0u;
        index < gxos::web::kWebMaxInlineLayoutFlows; ++index) {
        html += "<p>Parsed flow " + std::to_string(index) + "</p>";
    }
    html += "</div></body></html>";
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js108-flow-capacity.html", html, error) &&
        error == RuntimeErrorCode::None,
        "renderer flow-capacity fixture parses");
    ScriptResult result = run(harness,
        "var capacityHost = document.getElementById(\"capacity-host\");"
        "var capacityNode = document.createElement(\"p\");"
        "capacityNode.textContent = \"Cannot exceed existing flow capacity\";");
    const std::uint64_t node = harness.document().structuralElements.back().serial;
    const std::string before = stateFingerprint(harness);
    result = run(harness,
        "capacityHost.insertBefore(capacityNode,"
        " capacityHost.firstElementChild);");
    expect(!result.succeeded() && result.runtimeError.code ==
        RuntimeErrorCode::DocumentMutationLimitExceeded &&
        elementBySerial(harness, node)->parentSerial == 0u &&
        projectionCount(harness, node) == 0u &&
        stateFingerprint(harness) == before,
        "projection-flow capacity failure preserves detached structure and renderer state");
}

void testHostedCallbacks()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadDocument(harness, true), "JS108 fixture script registers parsed button callbacks");
    const std::uint64_t insertButton = serialById(harness, "insert-button");
    const std::uint64_t moveButton = serialById(harness, "move-button");
    const std::uint64_t appendButton = serialById(harness, "append-button");
    const std::uint64_t subtreeButton = serialById(harness, "subtree-button");
    const std::uint64_t invalidButton = serialById(harness, "invalid-button");
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(insertButton != 0u && harness.dispatchClick(insertButton, error) &&
        error == RuntimeErrorCode::None && settle(harness),
        "parsed insert control callback inserts and renders before the first paragraph");
    const std::uint64_t inserted = serialById(harness, "inserted");
    expect(inserted != 0u && projectionCount(harness, inserted) == 1u &&
        projectionIndex(harness, inserted) < projectionIndex(
            harness, serialById(harness, "first")) &&
        textContent(harness, inserted) == "Inserted before first",
        "event insertion updates document selectors and the visible block projection");

    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(moveButton, error) &&
        error == RuntimeErrorCode::None && settle(harness),
        "parsed move control callback moves the last paragraph to the first position");
    const std::uint64_t lastDynamic = serialById(harness, "movable");
    expect(lastDynamic != 0u &&
        projectionIndex(harness, lastDynamic) < projectionIndex(harness, inserted),
        "event movement changes block order without duplicating paragraph projections");

    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(appendButton, error) &&
        error == RuntimeErrorCode::None && settle(harness),
        "parsed append control callback uses explicit null and completes layout");
    const std::uint64_t appended = serialById(harness, "appended");
    expect(appended != 0u && projectionCount(harness, appended) == 1u &&
        projectionIndex(harness, appended) > projectionIndex(
            harness, serialById(harness, "last")),
        "null callback appends one visible dynamic paragraph");

    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(subtreeButton, error) &&
        error == RuntimeErrorCode::None && settle(harness),
        "parsed subtree control creates, connects, and moves one supported subtree");
    const std::uint64_t subtree = serialById(harness, "moved-subtree");
    const std::uint64_t descendant = serialById(harness, "subtree-child");
    const std::uint64_t otherHost = serialById(harness, "other-host");
    expect(subtree != 0u && descendant != 0u && connected(harness, subtree) &&
        connected(harness, descendant) &&
        elementBySerial(harness, subtree)->parentSerial == otherHost &&
        projectionCount(harness, subtree) == 1u &&
        projectionCount(harness, descendant) == 1u &&
        textContent(harness, descendant) == "Subtree text remains attached",
        "event subtree movement retains child text, identity, and one render projection each");

    const std::string beforeInvalidCallback = stateFingerprint(harness);
    error = RuntimeErrorCode::None;
    const bool invalidDispatched = harness.dispatchClick(invalidButton, error);
    expect(!invalidDispatched && error == RuntimeErrorCode::HostInvalidValue &&
        stateFingerprint(harness) == beforeInvalidCallback,
        "invalid reference callback reports the host error and preserves the completed page state");
}

} // namespace

int main()
{
    testInsertOrderIdentityAndSelectors();
    testInlineOrderingAndMixedContentAtomicity();
    testCrossParentSubtreeAndCycles();
    testQuotaFailureAndStaleGeneration();
    testFlowCapacityFailure();
    testHostedCallbacks();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS108 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS108 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
