#pragma once

#include "guide_web_document.h"
#include "host.h"
#include "runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gxos {
namespace javascript {

// JS8 deliberately uses separate kinds even though the runtime's registry
// already keeps handles opaque. The instance ID is a document serial for an
// Element and the fixed document instance for the document root.
constexpr HostObjectKind kNavigatorDocumentHostKind = 0x4A530801u;
constexpr HostObjectKind kNavigatorElementHostKind = 0x4A530802u;
constexpr HostInstanceId kNavigatorDocumentHostInstance = 1u;

constexpr std::uint32_t kNavigatorGetElementByIdMethod = 1u;
constexpr std::uint32_t kNavigatorAddEventListenerMethod = 2u;
constexpr std::uint32_t kNavigatorRemoveEventListenerMethod = 3u;

constexpr std::size_t kNavigatorScriptMaxDocumentIdLength = 256u;
constexpr std::size_t kNavigatorScriptMaxTextContentAssignment = 64u * 1024u;
constexpr std::size_t kNavigatorScriptMaxTextAggregationOperations = 1024u;
constexpr std::size_t kNavigatorScriptMaxElementHostObjects = 1024u;
constexpr std::size_t kNavigatorScriptMaxDocumentMutations = 1024u;
constexpr std::size_t kNavigatorScriptMaxDocumentNodes = 1024u;
constexpr std::size_t kNavigatorScriptMaxClickHandlers = 64u;
constexpr std::uint32_t kNavigatorClickListenerOnceFlag = 1u;
constexpr std::uint32_t kNavigatorClickListenerCaptureFlag = 2u;
// JS13/JS17 snapshots at most 32 serials, including the clicked Element and the
// document's html/body ancestors. The path is deliberately smaller than the
// 1024-node document metadata bound so dispatch cannot consume an unbounded
// native traversal stack.
constexpr std::size_t kNavigatorScriptMaxPropagationDepth = 32u;

struct NavigatorScriptHostLimits {
    std::size_t maxDocumentIdLength = kNavigatorScriptMaxDocumentIdLength;
    std::size_t maxTextContentAssignment =
        kNavigatorScriptMaxTextContentAssignment;
    std::size_t maxTextAggregationOperations =
        kNavigatorScriptMaxTextAggregationOperations;
    std::size_t maxDocumentMutations = kNavigatorScriptMaxDocumentMutations;
    std::size_t maxDocumentNodes = kNavigatorScriptMaxDocumentNodes;
    std::size_t maxClickHandlers = kNavigatorScriptMaxClickHandlers;
    std::size_t maxClickListeners = kNavigatorScriptMaxClickHandlers;
};

// The adapter never stores a JavaScript pointer and never creates a
// JavaScript DOM tree. Element handles identify WebDocument structural
// elements by serial and are checked against the current host generation.
class NavigatorScriptHostAdapter final : public HostAdapter {
public:
    explicit NavigatorScriptHostAdapter(HostGenerationId generation = 1u,
        NavigatorScriptHostLimits limits = NavigatorScriptHostLimits());
    NavigatorScriptHostAdapter(gxos::web::WebDocument& document,
        HostGenerationId generation = 1u,
        NavigatorScriptHostLimits limits = NavigatorScriptHostLimits());

    void attachDocument(gxos::web::WebDocument& document,
        HostGenerationId generation);
    void detachDocument();
    void setGeneration(HostGenerationId generation);
    HostGenerationId generation() const { return generation_; }
    gxos::web::WebDocument* document() const { return document_; }
    const NavigatorScriptHostLimits& limits() const { return limits_; }

    HostResult validate(const HostObjectReference& object) override;
    HostResult getProperty(const HostObjectReference& object,
        SourceView property, HostValue& result) override;
    HostResult setProperty(const HostObjectReference& object,
        SourceView property, const HostValue& value) override;
    HostResult call(const HostObjectReference* receiver,
        std::uint32_t methodId, const HostValue* arguments,
        std::size_t argumentCount, HostValue& result) override;
    HostResult callWithRuntime(RuntimeContext& runtime,
        const HostObjectReference* receiver, std::uint32_t methodId,
        const HostValue* arguments, std::size_t argumentCount,
        HostValue& result) override;

    // Navigator calls this only after its normal hit test has selected a
    // document element serial. The callback is invoked in the supplied,
    // already-installed realm; no source is reparsed and no new realm is
    // created for the click.
    bool dispatchClick(RuntimeContext& runtime, HostInstanceId serial,
        RuntimeErrorCode& error, bool* defaultPrevented = nullptr);
    bool hasClickHandler(HostInstanceId serial) const;
    std::size_t clickListenerCount() const { return clickListenerCount_; }
    // Compatibility diagnostic: the number of Elements with at least one
    // onclick or addEventListener registration. Listener capacity is exposed
    // separately by clickListenerCount().
    std::size_t clickHandlerCount() const { return clickHandlerCount_; }
    void clearClickHandlers();

private:
    struct ClickHandlerRecord {
        HostInstanceId serial = 0;
        RuntimeFunctionId onclickFunction = kInvalidRuntimeFunctionId;
    };
    static_assert(sizeof(ClickHandlerRecord) == 16u,
        "Navigator click records must remain 16 bytes");

    // A listener record is one global registration slot. The 64-bit sequence
    // is both its registration identity and its logical dispatch order; it is
    // never reused while an older registration could be present in a node
    // snapshot. The physical slot may be reused immediately after removal.
    struct ClickListenerRecord {
        HostInstanceId serial = 0;
        RuntimeFunctionId listenerFunction = kInvalidRuntimeFunctionId;
        std::uint32_t flags = 0;
        std::uint64_t registrationSequence = 0;
    };
    static_assert(sizeof(ClickListenerRecord) == 24u,
        "Navigator listener records must remain 24 bytes");

    struct ClickListenerSnapshotEntry {
        std::uint64_t registrationSequence = 0;
        RuntimeFunctionId listenerFunction = kInvalidRuntimeFunctionId;
    };
    static_assert(sizeof(ClickListenerSnapshotEntry) == 16u,
        "Navigator listener snapshots must remain 16 bytes");

    std::size_t callbackLimit() const;
    std::size_t listenerLimit() const;
    ClickHandlerRecord* clickHandlerFor(HostInstanceId serial);
    const ClickHandlerRecord* clickHandlerFor(HostInstanceId serial) const;
    void removeEmptyClickHandler(HostInstanceId serial);
    ClickListenerRecord* clickListenerFor(HostInstanceId serial,
        RuntimeFunctionId function, bool capture);
    const ClickListenerRecord* clickListenerFor(
        HostInstanceId serial, RuntimeFunctionId function, bool capture) const;
    const ClickListenerRecord* clickListenerForSequence(
        HostInstanceId serial, std::uint64_t registrationSequence) const;
    ClickListenerRecord* clickListenerForSequence(
        HostInstanceId serial, std::uint64_t registrationSequence);
    bool collectListenerSnapshot(HostInstanceId serial,
        std::array<ClickListenerSnapshotEntry,
            kNavigatorScriptMaxClickHandlers>& snapshot,
        std::size_t& count, bool capture) const;
    bool hasClickListener(HostInstanceId serial) const;
    bool hasAnyClickHandler(HostInstanceId serial) const;
    void removeClickListener(ClickListenerRecord& record);
    void resequenceListeners();
    bool allocateListenerSequence(std::uint64_t& sequence);
    HostResult callInternal(const HostObjectReference* receiver,
        std::uint32_t methodId, const HostValue* arguments,
        std::size_t argumentCount, HostValue& result, bool once,
        bool capture, bool optionsSupplied);
    gxos::web::HtmlElementRef* findElement(HostInstanceId serial);
    const gxos::web::HtmlElementRef* findElement(HostInstanceId serial) const;
    bool isKnownElementSerial(HostInstanceId serial) const;
    bool isDescendantOrSelf(std::uint64_t serial,
        std::uint64_t ancestorSerial) const;
    HostResult validateDocumentReceiver(const HostObjectReference* receiver);
    HostResult textContentForElement(std::uint64_t serial,
        std::string& result) const;
    HostResult convertTextValue(const HostValue& value,
        std::string& result) const;
    HostResult setElementTextContent(std::uint64_t serial,
        const std::string& text);
    HostResult appendBoundedText(std::string& target, const std::string& text,
        std::size_t& operations) const;

    gxos::web::WebDocument* document_ = nullptr;
    HostGenerationId generation_ = 1u;
    NavigatorScriptHostLimits limits_;
    std::array<ClickHandlerRecord, kNavigatorScriptMaxClickHandlers>
        clickHandlers_{};
    std::array<ClickListenerRecord, kNavigatorScriptMaxClickHandlers>
        clickListeners_{};
    // Number of Elements represented by either table, retained for the
    // historical clickHandlerCount() diagnostic.
    std::size_t clickHandlerCount_ = 0;
    std::size_t clickOnclickRecordCount_ = 0;
    std::size_t clickListenerCount_ = 0;
    std::uint64_t nextListenerRegistrationSequence_ = 1u;
    bool clickDispatchActive_ = false;
    // Returned strings are copied synchronously by RuntimeContext. Keeping
    // one adapter-owned scratch value avoids exposing mutable document memory.
    mutable std::string returnBuffer_;
};

// Deterministic JS8 proof path. It parses a known fixture into the real
// WebDocument model, installs document through the JS7 global-host API, keeps
// one realm for multiple explicit scripts, and exposes an explicit relayout
// checkpoint. It never reads or executes HTML <script> nodes.
class NavigatorScriptExecutionHarness final {
public:
    explicit NavigatorScriptExecutionHarness(
        RuntimeLimits runtimeLimits = RuntimeLimits(),
        NavigatorScriptHostLimits hostLimits = NavigatorScriptHostLimits());

    bool loadHtml(const std::string& url, const std::string& html,
        RuntimeErrorCode& error);
    bool replaceHtml(const std::string& url, const std::string& html,
        RuntimeErrorCode& error);
    bool invalidateDocumentGeneration(RuntimeErrorCode& error);

    ScriptResult execute(SourceView source);
    ScriptResult execute(const std::string& source);
    // Production-boundary proof hook: feed the authoritative document
    // element serial returned by a Navigator hit test into the real adapter.
    bool dispatchClick(std::uint64_t serial, RuntimeErrorCode& error,
        bool* defaultPrevented = nullptr);
    bool relayout();

    RuntimeContext& runtime() { return runtime_; }
    const RuntimeContext& runtime() const { return runtime_; }
    NavigatorScriptHostAdapter& hostAdapter() { return adapter_; }
    const NavigatorScriptHostAdapter& hostAdapter() const { return adapter_; }
    gxos::web::WebDocument& document() { return document_; }
    const gxos::web::WebDocument& document() const { return document_; }
    bool loaded() const { return loaded_; }
    bool documentDirty() const { return document_.layoutDirty; }
    std::uint64_t layoutRevision() const { return document_.layoutRevision; }
    std::size_t layoutTextExtent() const { return document_.layoutTextExtent; }

private:
    bool installDocumentGlobal(RuntimeErrorCode& error);
    bool loadParsedDocument(gxos::web::WebDocument document,
        bool resetRealm, RuntimeErrorCode& error);

    RuntimeContext runtime_;
    NavigatorScriptHostAdapter adapter_;
    gxos::web::WebDocument document_;
    bool loaded_ = false;
};

// Host-side inspection helper used by proof tests. It reads the same bounded
// inline text runs that Navigator's renderer consumes.
bool navigatorScriptElementTextContent(const gxos::web::WebDocument& document,
    std::uint64_t serial, std::string& result,
    std::size_t maxOperations = kNavigatorScriptMaxTextAggregationOperations,
    std::size_t maxBytes = kNavigatorScriptMaxTextContentAssignment);

} // namespace javascript
} // namespace gxos
