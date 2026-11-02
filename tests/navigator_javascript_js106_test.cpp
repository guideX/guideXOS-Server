#include "navigator_javascript/navigator_script_host.h"

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
    return harness.execute(SourceView(source.data(), source.size()));
}

std::string readFixture()
{
    std::ifstream input("navigator-smoke/javascript-js106.html",
        std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
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
        harness.document().structuralElements[
            static_cast<std::size_t>(serial - 1u)];
    return element.serial == serial ? &element : nullptr;
}

const gxos::web::HtmlElementContentMetadata* contentBySerial(
    const NavigatorScriptExecutionHarness& harness, std::uint64_t serial)
{
    if (serial == 0u || serial > harness.document().contentMetadata.size())
        return nullptr;
    const gxos::web::HtmlElementContentMetadata& content =
        harness.document().contentMetadata[static_cast<std::size_t>(serial - 1u)];
    return content.serial == serial ? &content : nullptr;
}

bool isConnected(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    const gxos::web::HtmlElementContentMetadata* content =
        contentBySerial(harness, serial);
    return content != nullptr && content->connected;
}

bool isDynamic(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    const gxos::web::HtmlElementContentMetadata* content =
        contentBySerial(harness, serial);
    return content != nullptr && content->dynamicallyCreated;
}

std::uint64_t inlineFlowForSerial(
    const NavigatorScriptExecutionHarness& harness, std::uint64_t serial)
{
    for (const gxos::web::WebInlineItem& item : harness.document().inlineItems) {
        if (item.ownerSerial == serial) return item.flowSerial;
    }
    return 0u;
}

std::string elementText(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    std::string text;
    if (!navigatorScriptElementTextContent(harness.document(), serial, text))
        return std::string();
    return text;
}

bool settle(NavigatorScriptExecutionHarness& harness)
{
    return !harness.documentDirty() || harness.relayout();
}

const Value* binding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    return harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
}

bool booleanBinding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isBoolean() && value->booleanValue();
}

double numberBinding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isNumber() ? value->numberValue() : -1.0;
}

bool inlineTextForSerial(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial, const std::string& value)
{
    for (const gxos::web::WebInlineItem& item :
        harness.document().inlineItems) {
        if (item.kind == gxos::web::InlineItemKind::TextRun &&
            item.ownerSerial == serial && item.text == value) return true;
    }
    return false;
}

std::size_t nonEmptyInlineRunsForSerial(
    const NavigatorScriptExecutionHarness& harness, std::uint64_t serial)
{
    std::size_t count = 0u;
    for (const gxos::web::WebInlineItem& item :
        harness.document().inlineItems) {
        if (item.kind == gxos::web::InlineItemKind::TextRun &&
            item.ownerSerial == serial && !item.text.empty()) ++count;
    }
    return count;
}

bool loadFixture(NavigatorScriptExecutionHarness& harness)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const std::string html = readFixture();
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///javascript-js106.html", html, error);
    if (!loaded || error != RuntimeErrorCode::None ||
        harness.document().scriptSources.size() != 1u) {
        std::cerr << "JS106 fixture load detail: loaded=" << loaded
            << " error=" << runtimeErrorCodeName(error)
            << " scripts=" << harness.document().scriptSources.size()
            << " html=" << html.size() << '\n';
        return false;
    }
    const ScriptResult result = run(harness,
        harness.document().scriptSources[0]);
    if (!result.succeeded()) {
        std::cerr << "JS106 fixture script detail: status="
            << static_cast<unsigned>(result.status) << " error="
            << runtimeErrorCodeName(result.runtimeError.code)
            << " parser=" << parserErrorCodeName(result.parserError.code)
            << " parser-line=" << result.parserError.location.line
            << " parser-column=" << result.parserError.location.column
            << " parser-actual=" << tokenTypeName(result.parserError.actual)
            << " parser-expected=" << tokenTypeName(result.parserError.expected)
            << " lexer=" << lexerErrorCodeName(result.lexerError.code)
            << " line=" << result.runtimeError.location.line
            << " column=" << result.runtimeError.location.column
            << " source=" << harness.document().scriptSources[0].size()
            << '\n';
    }
    return result.succeeded();
}

void testCreationAndDetachedState()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadFixture(harness), "hosted JS106 fixture and one script load");
    const ScriptResult detached = run(harness,
        "var js106Detached = document.querySelector(\"#created-note\") === null;"
        "var js106InitialText = item.textContent;"
        "var js106DetachedIdentity = item === item;");
    expect(detached.succeeded(), "detached dynamic span remains a usable Element");
    expect(booleanBinding(harness, "js106Detached"),
        "detached created nodes remain absent from document selectors");
    const Value* initialText = binding(harness, "js106InitialText");
    expect(initialText != nullptr && initialText->isString() &&
        harness.runtime().stringValue(*initialText) == "Added by JavaScript",
        "text and attributes can be assigned before insertion");
    expect(booleanBinding(harness, "js106DetachedIdentity"),
        "created Element reference identity is stable");
    const std::uint64_t created = serialById(harness, "created-note");
    const gxos::web::HtmlElementRef* node = elementBySerial(harness, created);
    expect(created != 0u && node != nullptr && isDynamic(harness, created) &&
        !isConnected(harness, created) && node->parentSerial == 0u,
        "creation retains a bounded detached node in the document registry");

    const std::size_t nodesBeforeUnsupported =
        harness.document().structuralElements.size();
    const ScriptResult unsupported = run(harness,
        "var js106Unsupported = document.createElement(\"article\");");
    expect(!unsupported.succeeded() &&
        unsupported.runtimeError.code == RuntimeErrorCode::HostInvalidValue &&
        harness.document().structuralElements.size() == nodesBeforeUnsupported,
        "tags outside the bounded span/div/p subset fail without allocation");
}

void testInsertUpdateRemoveReinsertAndSelectors()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadFixture(harness), "JS106 fixture loads for lifecycle tests");
    const std::uint64_t container = serialById(harness, "dynamic-host");
    const std::uint64_t stepButton = serialById(harness, "step-button");
    expect(container != 0u && stepButton != 0u,
        "existing paragraph and event control have structural identities");
    settle(harness);
    std::uint64_t revision = harness.layoutRevision();

    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(stepButton, eventError) &&
        eventError == RuntimeErrorCode::None,
        "existing click callback inserts the created span");
    const std::uint64_t created = serialById(harness, "created-note");
    const gxos::web::HtmlElementRef* inserted = elementBySerial(harness, created);
    expect(created != 0u && inserted != nullptr && isConnected(harness, created) &&
        inserted->parentSerial == container,
        "appendChild connects the same node under its requested parent");
    expect(inlineTextForSerial(harness, created, "Added by JavaScript") &&
        inserted != nullptr && inlineFlowForSerial(harness, created) == container,
        "connected text is projected into the parent inline render flow");
    expect(harness.documentDirty() && settle(harness) &&
        harness.layoutRevision() > revision &&
        harness.layoutTextExtent() >= std::string("Added by JavaScript").size(),
        "insertion invalidates and consumes the hosted inline layout projection");
    revision = harness.layoutRevision();

    ScriptResult selectors = run(harness,
        "var js106ById = document.getElementById(\"created-note\") === item;"
        "var js106ByQuery = document.querySelector(\"#created-note\") === item;"
        "var js106ByTag = document.querySelector(\"span\") === item;"
        "var js106ByClass = container.querySelector(\".created-note\") === item;"
        "var js106Descendant = document.querySelector(\"p#dynamic-host > span.created-note\") === item;"
        "var js106Scoped = container.querySelectorAll(\"span\").length === 1;");
    expect(selectors.succeeded(), "supported selectors evaluate after insertion");
    const char* selectorBindings[] = {"js106ById", "js106ByQuery", "js106ByTag",
        "js106ByClass", "js106Descendant", "js106Scoped"};
    for (const char* name : selectorBindings) {
        expect(booleanBinding(harness, name),
            "ID, tag, class, descendant, and scoped selectors agree on identity");
    }

    eventError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(stepButton, eventError) &&
        eventError == RuntimeErrorCode::None && settle(harness),
        "second callback updates the connected element");
    expect(elementText(harness, created) == "Updated by JavaScript" &&
        nonEmptyInlineRunsForSerial(harness, created) == 1u &&
        inlineTextForSerial(harness, created, "Updated by JavaScript"),
        "textContent replaces the old inline text without duplicate runs");
    expect(harness.layoutRevision() > revision,
        "connected text replacement advances the render layout revision");
    revision = harness.layoutRevision();
    selectors = run(harness,
        "var js106UpdatedIdentity = document.querySelector(\"#created-note\") === item;"
        "var js106UpdatedText = item.textContent;");
    expect(selectors.succeeded() && elementText(harness, created) ==
        "Updated by JavaScript" && nonEmptyInlineRunsForSerial(harness, created) == 1u,
        "updated text reads back while selector identity remains stable");

    eventError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(stepButton, eventError) &&
        eventError == RuntimeErrorCode::None && settle(harness),
        "third callback removes the connected element");
    inserted = elementBySerial(harness, created);
    expect(inserted != nullptr && !isConnected(harness, created) &&
        inserted->parentSerial == 0u &&
        elementText(harness, created) == "Updated by JavaScript",
        "removal disconnects the node while preserving its object and text");
    selectors = run(harness,
        "var js106AbsentById = document.getElementById(\"created-note\") === null;"
        "var js106AbsentByQuery = document.querySelector(\"#created-note\") === null;"
        "var js106RetainedDetachedText = item.textContent;");
    expect(selectors.succeeded() &&
        elementText(harness, container) == "Dynamic content:",
        "removal hides the element from new document queries and parent text");
    expect(harness.layoutRevision() > revision,
        "removal invalidates the rendered inline projection");
    revision = harness.layoutRevision();

    eventError = RuntimeErrorCode::None;
    expect(harness.dispatchClick(stepButton, eventError) &&
        eventError == RuntimeErrorCode::None && settle(harness),
        "fourth callback reinserts the same object");
    inserted = elementBySerial(harness, created);
    expect(inserted != nullptr && isConnected(harness, created) &&
        inserted->parentSerial == container &&
        elementText(harness, created) == "Updated by JavaScript" &&
        harness.layoutRevision() > revision,
        "reinsertion restores visibility, state, identity, and layout");
    selectors = run(harness,
        "var js106ReinsertIdentity = document.querySelector(\"#created-note\") === item;"
        "var js106ReinsertText = item.textContent;");
    expect(selectors.succeeded() &&
        booleanBinding(harness, "js106ReinsertIdentity"),
        "reinsertion restores document selector membership with stable identity");

    ScriptResult multiple = run(harness,
        "var secondary = document.createElement(\"span\");"
        "secondary.setAttribute(\"id\", \"secondary-note\");"
        "secondary.textContent = \"Second\";"
        "var secondaryAppend = container.appendChild(secondary) === secondary;"
        "var repeatedAppend = container.appendChild(secondary) === secondary;"
        "var orderedFirst = container.children[0] === item;"
        "var orderedSecond = container.children[1] === secondary;"
        "var duplicateCount = container.childElementCount;");
    expect(multiple.succeeded(), "multiple supported siblings append successfully");
    const std::string appendedParentText = elementText(harness, container);
    const bool appendedLayoutSettled = settle(harness);
    if (appendedParentText != "Dynamic content:Updated by JavaScriptSecond")
        std::cerr << "JS106 append text detail: [" << appendedParentText
            << "]\n";
    expect(appendedParentText ==
        "Dynamic content:Updated by JavaScriptSecond" && appendedLayoutSettled,
        "rendered text follows sibling insertion order without duplicates");
    expect(numberBinding(harness, "duplicateCount") == 2.0 &&
        booleanBinding(harness, "orderedFirst") &&
        booleanBinding(harness, "orderedSecond"),
        "repeat append preserves one child and reports DOM sibling order");

    multiple = run(harness,
        "container.appendChild(item);"
        "var movedToLast = container.children[1] === item;"
        "var parentAfterMove = item.parentElement === container;"
        "var movedOrderText = container.textContent;");
    const std::string reorderedParentText = elementText(harness, container);
    const bool reorderedLayoutSettled = settle(harness);
    if (reorderedParentText != "Dynamic content:SecondUpdated by JavaScript")
        std::cerr << "JS106 reorder text detail: [" << reorderedParentText
            << "]\n";
    expect(multiple.succeeded() && reorderedParentText ==
        "Dynamic content:SecondUpdated by JavaScript" && reorderedLayoutSettled,
        "appending an existing child moves its text to the end of its siblings");
    expect(booleanBinding(harness, "movedToLast"),
        "same-parent append updates sibling order");

    const std::uint64_t eventHost = serialById(harness, "event-host");
    multiple = run(harness,
        "var movedAcrossParents = document.getElementById(\"event-host\").appendChild(item) === item;"
        "var oldScopeEmpty = container.querySelector(\"#created-note\") === null;"
        "var newScopeContains = document.getElementById(\"event-host\").querySelector(\"#created-note\") === item;");
    const gxos::web::HtmlElementRef* moved = elementBySerial(harness, created);
    const std::string afterReparentText = elementText(harness, container);
    if (!multiple.succeeded() || afterReparentText != "Dynamic content:Second")
        std::cerr << "JS106 reparent detail: mutation="
            << runtimeErrorCodeName(multiple.runtimeError.code)
            << " text=[" << afterReparentText << "] eventHost=" << eventHost
            << " childParent=" << (moved == nullptr ? 0u : moved->parentSerial)
            << "\n";
    expect(multiple.succeeded() && eventHost != 0u && moved != nullptr &&
        isConnected(harness, created) && moved->parentSerial == eventHost &&
        afterReparentText == "Dynamic content:Second" &&
        settle(harness),
        "reparenting preserves identity and updates both connected parents");
    expect(booleanBinding(harness, "oldScopeEmpty") &&
        booleanBinding(harness, "newScopeContains"),
        "scoped selector membership follows reparenting");

    multiple = run(harness,
        "var nestedItem = document.createElement(\"span\");"
        "nestedItem.setAttribute(\"id\", \"nested-note\");"
        "nestedItem.textContent = \"Nested\";"
        "var nestedAppend = item.appendChild(nestedItem) === nestedItem;"
        "var nestedFound = document.querySelector(\"#nested-note\") === nestedItem;");
    const std::uint64_t nestedSerial = serialById(harness, "nested-note");
    const gxos::web::HtmlElementRef* nested = elementBySerial(harness,
        nestedSerial);
    expect(multiple.succeeded() && nested != nullptr && isConnected(harness,
        nestedSerial) &&
        nested->parentSerial == created && settle(harness) &&
        inlineTextForSerial(harness, nestedSerial, "Nested") &&
        elementText(harness, created) == "Updated by JavaScriptNested",
        "nested supported spans share the connected parent's render flow");
    expect(booleanBinding(harness, "nestedAppend") &&
        booleanBinding(harness, "nestedFound"),
        "nested insertion preserves selector identity through the existing API");

    multiple = run(harness,
        "document.getElementById(\"event-host\").removeChild(item);"
        "var nestedHidden = document.querySelector(\"#nested-note\") === null;"
        "container.appendChild(item);"
        "var nestedRestored = document.querySelector(\"#nested-note\") === nestedItem;");
    const gxos::web::HtmlElementRef* reconnectedNested = elementBySerial(
        harness, nestedSerial);
    expect(multiple.succeeded() && settle(harness) &&
        reconnectedNested != nullptr && isConnected(harness, nestedSerial) &&
        reconnectedNested->parentSerial == created &&
        elementText(harness, container) ==
            "Dynamic content:SecondUpdated by JavaScriptNested",
        "removing and reinserting a subtree restores its complete rendered text");
    expect(booleanBinding(harness, "nestedHidden") &&
        booleanBinding(harness, "nestedRestored"),
        "subtree removal and reinsertion update document selector membership");
}

void testDetachedTextBoundaryAndFailureAtomicity()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadFixture(harness), "JS106 fixture loads for boundary tests");
    const std::uint64_t button = serialById(harness, "step-button");
    const std::uint64_t itemSerial = serialById(harness, "created-note");
    const std::uint64_t container = serialById(harness, "dynamic-host");
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(button, error), "initial insertion occurs before boundary checks");
    settle(harness);

    const ScriptResult emptyText = run(harness,
        "item.textContent = \"\"; var emptyRead = item.textContent;");
    expect(emptyText.succeeded() && elementText(harness, itemSerial).empty() &&
        nonEmptyInlineRunsForSerial(harness, itemSerial) == 0u,
        "empty textContent removes the previous inline text");
    expect(harness.documentDirty() && settle(harness),
        "empty text assignment still invalidates connected layout");

    const std::string boundary(4096u, 'b');
    const std::string setBoundary = "item.textContent = \"" + boundary + "\";";
    const ScriptResult boundaryResult = run(harness, setBoundary);
    expect(boundaryResult.succeeded() &&
        elementText(harness, itemSerial) == boundary && settle(harness),
        "dynamic inline text accepts the 4,096-byte render boundary");
    const std::string overBoundary(4097u, 'c');
    const std::string setOverBoundary = "item.textContent = \"" +
        overBoundary + "\";";
    const ScriptResult overResult = run(harness, setOverBoundary);
    expect(!overResult.succeeded() && overResult.runtimeError.code ==
        RuntimeErrorCode::DocumentTextLimitExceeded &&
        elementText(harness, itemSerial) == boundary,
        "text above the render boundary fails before replacing the prior value");

    const ScriptResult restored = run(harness,
        "item.textContent = \"Restored after the boundary check\";");
    expect(restored.succeeded() && settle(harness) &&
        inlineTextForSerial(harness, itemSerial,
            "Restored after the boundary check"),
        "repeated text writes replace rather than duplicate render blocks");

    const ScriptResult wrongParent = run(harness,
        "document.getElementById(\"event-host\").removeChild(item);");
    expect(!wrongParent.succeeded() &&
        wrongParent.runtimeError.code == RuntimeErrorCode::TypeError &&
        elementBySerial(harness, itemSerial)->parentSerial == container,
        "wrong-parent removal throws before changing the tree");
    const ScriptResult removeAndReinsert = run(harness,
        "container.removeChild(item);"
        "var detachedTextAfterRemoval = item.textContent;"
        "container.appendChild(item);"
        "var reinsertedTextAfterRemoval = item.textContent;");
    expect(removeAndReinsert.succeeded() && settle(harness) &&
        isConnected(harness, itemSerial) &&
        elementText(harness, itemSerial) ==
            "Restored after the boundary check",
        "text remains mutable and reinsertion-safe after removal");

    const std::size_t nodesBeforeInvalidParent =
        harness.document().structuralElements.size();
    const ScriptResult invalidParents = run(harness,
        "var detachedCycleRoot = document.createElement(\"span\");"
        "detachedCycleRoot.setAttribute(\"id\", \"cycle-root\");"
        "var detachedCycleChild = document.createElement(\"span\");"
        "detachedCycleRoot.appendChild(detachedCycleChild);");
    expect(invalidParents.succeeded(), "detached supported nesting is permitted");
    const ScriptResult cycle = run(harness,
        "detachedCycleChild.appendChild(detachedCycleRoot);");
    expect(!cycle.succeeded() && cycle.runtimeError.code ==
        RuntimeErrorCode::HostInvalidValue &&
        harness.document().structuralElements.size() == nodesBeforeInvalidParent + 2u,
        "cycle insertion is rejected without allocating or reconnecting nodes");
    const ScriptResult selfInsert = run(harness,
        "detachedCycleRoot.appendChild(detachedCycleRoot);");
    expect(!selfInsert.succeeded() && selfInsert.runtimeError.code ==
        RuntimeErrorCode::InvalidHostObject,
        "self insertion is rejected before tree mutation");
    const ScriptResult unsupportedParent = run(harness,
        "document.getElementById(\"step-button\").appendChild(detachedCycleRoot);");
    expect(!unsupportedParent.succeeded() &&
        unsupportedParent.runtimeError.code == RuntimeErrorCode::HostInvalidValue &&
        elementBySerial(harness, serialById(harness, "step-button"))->childCount == 0u,
        "unsupported parent type rejects insertion without partial state");
    const ScriptResult unanchoredParent = run(harness,
        "document.getElementById(\"empty-div\").appendChild(detachedCycleRoot);");
    if (unanchoredParent.succeeded() || unanchoredParent.runtimeError.code !=
            RuntimeErrorCode::HostCallFailed) {
        const std::uint64_t emptyDiv = serialById(harness, "empty-div");
        const gxos::web::HtmlElementRef* detached = elementBySerial(harness,
            serialById(harness, "cycle-root"));
        std::cerr << "JS106 unanchored detail: error="
            << runtimeErrorCodeName(unanchoredParent.runtimeError.code)
            << " emptyDiv=" << emptyDiv << " parentFlow="
            << inlineFlowForSerial(harness, emptyDiv)
            << " parentConnected=" << isConnected(harness, emptyDiv)
            << " childConnected=" << isConnected(harness,
                serialById(harness, "cycle-root"))
            << " childParent=" << (detached == nullptr ? 0u : detached->parentSerial)
            << "\n";
    }
    expect(!unanchoredParent.succeeded() && unanchoredParent.runtimeError.code ==
        RuntimeErrorCode::HostCallFailed &&
        elementBySerial(harness, serialById(harness, "cycle-root")) != nullptr &&
        !isConnected(harness, serialById(harness, "cycle-root")) &&
        elementBySerial(harness, serialById(harness,
            "cycle-root"))->parentSerial == 0u,
        "parent without a render flow anchor rejects before tree mutation");
}

void testCallbackMutationAndGenerationSafety()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadFixture(harness), "JS106 fixture loads for callback tests");
    const std::uint64_t eventButton = serialById(harness, "event-button");
    const std::uint64_t eventHost = serialById(harness, "event-host");
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(eventButton, error) &&
        error == RuntimeErrorCode::None && settle(harness),
        "existing Event callback creates and appends a dynamic span");
    std::uint64_t callbackSerial = serialById(harness, "callback-note");
    const gxos::web::HtmlElementRef* callback = elementBySerial(harness,
        callbackSerial);
    expect(callback != nullptr && isConnected(harness, callbackSerial) &&
        callback->parentSerial == eventHost &&
        elementText(harness, callbackSerial) == "Created in click callback",
        "callback insertion survives event path and selector reentry");

    error = RuntimeErrorCode::None;
    expect(harness.dispatchClick(eventButton, error) &&
        error == RuntimeErrorCode::None && settle(harness),
        "later Event callback removes the same dynamic span");
    callback = elementBySerial(harness, callbackSerial);
    expect(callback != nullptr && !isConnected(harness, callbackSerial) &&
        elementText(harness, callbackSerial) == "Created in click callback",
        "callback removal preserves the element reference and current text");
    ScriptResult selectors = run(harness,
        "var js106CallbackAbsent = document.querySelector(\"#callback-note\") === null;");
    expect(selectors.succeeded() && booleanBinding(harness,
        "js106CallbackAbsent"),
        "callback removal updates document selector membership");

    const HostGenerationId oldGeneration = harness.hostAdapter().generation();
    const HostObjectReference oldParent{eventHost, oldGeneration,
        kNavigatorElementHostKind};
    const HostObjectReference oldChild{callbackSerial, oldGeneration,
        kNavigatorElementHostKind};
    expect(harness.invalidateDocumentGeneration(error) &&
        error == RuntimeErrorCode::None,
        "host generation can advance while retained node storage remains bounded");
    const HostResult stale = harness.hostAdapter().validate(oldChild);
    expect(stale.code == HostResultCode::StaleObject,
        "detached and connected references follow stale-generation rejection");
    HostValue staleAppendResult;
    const HostResult staleAppend = harness.hostAdapter().call(&oldParent,
        kNavigatorAppendChildMethod, nullptr, 0u, staleAppendResult);
    expect(staleAppend.code == HostResultCode::StaleObject,
        "stale parent references cannot append after navigation generation change");
}

void testBoundedNodeAndHostObjectPools()
{
    const std::string html =
        "<html><body><p id=\"host\">base</p></body></html>";
    NavigatorScriptExecutionHarness countHarness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(countHarness.loadHtml("file:///js106-capacity.html", html, error),
        "capacity baseline parses");
    const std::size_t baseline = countHarness.document().structuralElements.size();
    NavigatorScriptHostLimits oneSlot;
    oneSlot.maxDocumentNodes = baseline + 1u;
    NavigatorScriptExecutionHarness nodeLimited(RuntimeLimits(), oneSlot);
    error = RuntimeErrorCode::None;
    expect(nodeLimited.loadHtml("file:///js106-capacity.html", html, error),
        "one-slot node pool fixture loads");
    expect(run(nodeLimited,
        "var firstCapacityNode = document.createElement(\"span\");").succeeded(),
        "first dynamic node consumes the last available document slot");
    const std::size_t beforeExhaustion =
        nodeLimited.document().structuralElements.size();
    const ScriptResult exhaustedNode = run(nodeLimited,
        "var secondCapacityNode = document.createElement(\"span\");");
    expect(!exhaustedNode.succeeded() && exhaustedNode.runtimeError.code ==
        RuntimeErrorCode::DocumentLookupLimitExceeded &&
        nodeLimited.document().structuralElements.size() == beforeExhaustion,
        "document node exhaustion fails deterministically without a partial node");

    NavigatorScriptHostLimits twoSlots;
    twoSlots.maxDocumentNodes = baseline + 2u;
    NavigatorScriptExecutionHarness retained(RuntimeLimits(), twoSlots);
    error = RuntimeErrorCode::None;
    expect(retained.loadHtml("file:///js106-capacity.html", html, error),
        "retained-node pool fixture loads");
    const ScriptResult cycles = run(retained,
        "var retainedA = document.createElement(\"span\");"
        "var retainedB = document.createElement(\"span\");"
        "var retainedHost = document.getElementById(\"host\");"
        "retainedHost.appendChild(retainedA); retainedHost.removeChild(retainedA);"
        "retainedHost.appendChild(retainedB); retainedHost.removeChild(retainedB);");
    expect(cycles.succeeded() && retained.document().structuralElements.size() ==
        baseline + 2u, "create/remove cycles retain bounded node identities");
    const ScriptResult retainedExhausted = run(retained,
        "var retainedC = document.createElement(\"span\");");
    expect(!retainedExhausted.succeeded() &&
        retainedExhausted.runtimeError.code ==
            RuntimeErrorCode::DocumentLookupLimitExceeded &&
        retained.document().structuralElements.size() == baseline + 2u,
        "removed nodes are not silently reclaimed or reused before teardown");

    RuntimeLimits noElementHandles;
    noElementHandles.maxHostObjects = 1u;
    NavigatorScriptExecutionHarness handleLimited(noElementHandles);
    error = RuntimeErrorCode::None;
    expect(handleLimited.loadHtml("file:///js106-handles.html", html, error),
        "small host-object registry admits the document handle");
    const std::size_t handlesBaseline =
        handleLimited.document().structuralElements.size();
    const ScriptResult noHandle = run(handleLimited,
        "var noRuntimeHandle = document.createElement(\"span\");");
    expect(!noHandle.succeeded() && noHandle.runtimeError.code ==
        RuntimeErrorCode::HostObjectLimitExceeded &&
        handleLimited.document().structuralElements.size() == handlesBaseline,
        "runtime host-reference exhaustion is preflighted before node allocation");

    NavigatorScriptHostLimits mutationLimit;
    mutationLimit.maxDocumentMutations = 1u;
    NavigatorScriptExecutionHarness mutationLimited(RuntimeLimits(),
        mutationLimit);
    error = RuntimeErrorCode::None;
    expect(mutationLimited.loadHtml("file:///js106-mutations.html", html,
        error), "mutation-limit fixture loads");
    const ScriptResult firstMutation = run(mutationLimited,
        "var mutationNode = document.createElement(\"span\");");
    const ScriptResult secondMutation = run(mutationLimited,
        "mutationNode.textContent = \"x\";");
    expect(firstMutation.succeeded() && !secondMutation.succeeded() &&
        secondMutation.runtimeError.code ==
            RuntimeErrorCode::DocumentMutationLimitExceeded,
        "document mutation budget includes create and text operations");
}

void testStaleMethodArgument()
{
    NavigatorScriptExecutionHarness harness;
    expect(loadFixture(harness), "JS106 fixture loads for stale argument test");
    const std::uint64_t parent = serialById(harness, "dynamic-host");
    const std::uint64_t created = serialById(harness, "created-note");
    const HostGenerationId oldGeneration = harness.hostAdapter().generation();
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.invalidateDocumentGeneration(error) &&
        error == RuntimeErrorCode::None,
        "document host generation advances");
    const HostObjectReference currentParent{parent,
        harness.hostAdapter().generation(), kNavigatorElementHostKind};
    const HostValue staleChild = HostValue::fromHostObject(HostObjectReference{
        created, oldGeneration, kNavigatorElementHostKind});
    HostValue result;
    const HostResult stale = harness.hostAdapter().call(&currentParent,
        kNavigatorAppendChildMethod, &staleChild, 1u, result);
    expect(stale.code == HostResultCode::StaleObject,
        "append rejects a stale child reference before changing the tree");
}

} // namespace

int main()
{
    testCreationAndDetachedState();
    testInsertUpdateRemoveReinsertAndSelectors();
    testDetachedTextBoundaryAndFailureAtomicity();
    testCallbackMutationAndGenerationSafety();
    testBoundedNodeAndHostObjectPools();
    testStaleMethodArgument();

    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS106 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS106 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
