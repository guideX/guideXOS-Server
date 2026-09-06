#include "navigator_script_host.h"

#include "guide_web_html_parser.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <limits>
#include <utility>
#include <vector>

namespace gxos {
namespace javascript {

namespace {

bool textEquals(SourceView text, const char* expected)
{
    const std::size_t length = std::char_traits<char>::length(expected);
    return text.data != nullptr && text.length == length &&
        std::string(text.data, text.length) == expected;
}

const gxos::web::HtmlElementRef* findElementInDocument(
    const gxos::web::WebDocument& document, HostInstanceId serial,
    std::size_t nodeLimit)
{
    const std::size_t count = std::min(nodeLimit,
        document.structuralElements.size());
    for (std::size_t index = 0; index < count; ++index) {
        const gxos::web::HtmlElementRef& element =
            document.structuralElements[index];
        if (element.serial == serial) return &element;
    }
    return nullptr;
}

bool isDescendantInDocument(const gxos::web::WebDocument& document,
    std::uint64_t serial, std::uint64_t ancestorSerial,
    std::size_t nodeLimit)
{
    if (serial == 0 || ancestorSerial == 0) return false;
    std::uint64_t current = serial;
    for (std::size_t steps = 0; steps < nodeLimit && current != 0; ++steps) {
        if (current == ancestorSerial) return true;
        const gxos::web::HtmlElementRef* element = findElementInDocument(
            document, current, nodeLimit);
        if (element == nullptr) return false;
        current = element->parentSerial;
    }
    return false;
}

bool appendBounded(std::string& target, const std::string& text,
    std::size_t& operations, std::size_t maxOperations, std::size_t maxBytes)
{
    if (operations >= maxOperations) return false;
    ++operations;
    if (text.size() > maxBytes || target.size() > maxBytes - text.size())
        return false;
    target += text;
    return true;
}

std::string canonicalTagName(const std::string& tagName)
{
    std::string result = tagName;
    for (char& character : result) {
        character = static_cast<char>(std::toupper(
            static_cast<unsigned char>(character)));
    }
    return result;
}

} // namespace

NavigatorScriptHostAdapter::NavigatorScriptHostAdapter(
    HostGenerationId generation, NavigatorScriptHostLimits limits)
    : generation_(generation), limits_(limits)
{
}

NavigatorScriptHostAdapter::NavigatorScriptHostAdapter(
    gxos::web::WebDocument& document, HostGenerationId generation,
    NavigatorScriptHostLimits limits)
    : document_(&document), generation_(generation), limits_(limits)
{
}

void NavigatorScriptHostAdapter::attachDocument(
    gxos::web::WebDocument& document, HostGenerationId generation)
{
    document_ = &document;
    generation_ = generation;
    clearClickHandlers();
    returnBuffer_.clear();
}

void NavigatorScriptHostAdapter::detachDocument()
{
    document_ = nullptr;
    clearClickHandlers();
    returnBuffer_.clear();
}

void NavigatorScriptHostAdapter::setGeneration(HostGenerationId generation)
{
    if (generation_ != generation) clearClickHandlers();
    generation_ = generation;
}

std::size_t NavigatorScriptHostAdapter::callbackLimit() const
{
    return std::min(limits_.maxClickHandlers, clickHandlers_.size());
}

std::size_t NavigatorScriptHostAdapter::listenerLimit() const
{
    return std::min(limits_.maxClickListeners, clickListeners_.size());
}

NavigatorScriptHostAdapter::ClickHandlerRecord*
NavigatorScriptHostAdapter::clickHandlerFor(HostInstanceId serial)
{
    for (std::size_t index = 0; index < clickOnclickRecordCount_; ++index) {
        if (clickHandlers_[index].serial == serial) return &clickHandlers_[index];
    }
    return nullptr;
}

const NavigatorScriptHostAdapter::ClickHandlerRecord*
NavigatorScriptHostAdapter::clickHandlerFor(HostInstanceId serial) const
{
    for (std::size_t index = 0; index < clickOnclickRecordCount_; ++index) {
        if (clickHandlers_[index].serial == serial) return &clickHandlers_[index];
    }
    return nullptr;
}

void NavigatorScriptHostAdapter::clearClickHandlers()
{
    for (ClickHandlerRecord& record : clickHandlers_) record = ClickHandlerRecord();
    for (ClickListenerRecord& record : clickListeners_)
        record = ClickListenerRecord();
    clickHandlerCount_ = 0;
    clickOnclickRecordCount_ = 0;
    clickListenerCount_ = 0;
    nextListenerRegistrationSequence_ = 1u;
    clickDispatchActive_ = false;
}

void NavigatorScriptHostAdapter::removeEmptyClickHandler(HostInstanceId serial)
{
    for (std::size_t index = 0; index < clickOnclickRecordCount_; ++index) {
        ClickHandlerRecord& record = clickHandlers_[index];
        if (record.serial != serial ||
            record.onclickFunction != kInvalidRuntimeFunctionId) continue;
        for (std::size_t move = index + 1; move < clickOnclickRecordCount_; ++move)
            clickHandlers_[move - 1] = clickHandlers_[move];
        clickHandlers_[clickOnclickRecordCount_ - 1] = ClickHandlerRecord();
        --clickOnclickRecordCount_;
        return;
    }
}

NavigatorScriptHostAdapter::ClickListenerRecord*
NavigatorScriptHostAdapter::clickListenerFor(HostObjectKind ownerKind,
    HostInstanceId serial, NavigatorScriptEventType eventType,
    RuntimeFunctionId function, bool capture)
{
    for (ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.eventType == eventType &&
            record.listenerFunction == function &&
            ((record.flags & kNavigatorClickListenerCaptureFlag) != 0u) ==
                capture)
            return &record;
    }
    return nullptr;
}

const NavigatorScriptHostAdapter::ClickListenerRecord*
NavigatorScriptHostAdapter::clickListenerFor(HostObjectKind ownerKind,
    HostInstanceId serial, NavigatorScriptEventType eventType,
    RuntimeFunctionId function, bool capture) const
{
    for (const ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.eventType == eventType &&
            record.listenerFunction == function &&
            ((record.flags & kNavigatorClickListenerCaptureFlag) != 0u) ==
                capture)
            return &record;
    }
    return nullptr;
}

const NavigatorScriptHostAdapter::ClickListenerRecord*
NavigatorScriptHostAdapter::clickListenerForSequence(
    HostObjectKind ownerKind, HostInstanceId serial,
    std::uint64_t registrationSequence) const
{
    for (const ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.registrationSequence == registrationSequence)
            return &record;
    }
    return nullptr;
}

NavigatorScriptHostAdapter::ClickListenerRecord*
NavigatorScriptHostAdapter::clickListenerForSequence(
    HostObjectKind ownerKind, HostInstanceId serial,
    std::uint64_t registrationSequence)
{
    for (ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.registrationSequence == registrationSequence)
            return &record;
    }
    return nullptr;
}

bool NavigatorScriptHostAdapter::hasClickListener(
    HostObjectKind ownerKind, HostInstanceId serial) const
{
    for (const ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.listenerFunction != kInvalidRuntimeFunctionId)
            return true;
    }
    return false;
}

bool NavigatorScriptHostAdapter::hasAnyEventHandler(
    HostObjectKind ownerKind, HostInstanceId serial) const
{
    const ClickHandlerRecord* onclick = ownerKind == kNavigatorElementHostKind
        ? clickHandlerFor(serial) : nullptr;
    return (onclick != nullptr &&
        onclick->onclickFunction != kInvalidRuntimeFunctionId) ||
        hasClickListener(ownerKind, serial);
}

bool NavigatorScriptHostAdapter::hasClickHandler(HostInstanceId serial) const
{
    return hasAnyEventHandler(kNavigatorElementHostKind, serial);
}

void NavigatorScriptHostAdapter::removeClickListener(
    ClickListenerRecord& record)
{
    const HostInstanceId serial = record.serial;
    const HostObjectKind ownerKind = record.ownerKind;
    record = ClickListenerRecord();
    if (clickListenerCount_ > 0) --clickListenerCount_;
    if (!hasAnyEventHandler(ownerKind, serial) && clickHandlerCount_ > 0)
        --clickHandlerCount_;
}

void NavigatorScriptHostAdapter::resequenceListeners()
{
    std::array<std::size_t, kNavigatorScriptMaxClickHandlers> slots{};
    std::size_t count = 0;
    for (std::size_t index = 0; index < clickListeners_.size(); ++index) {
        if (clickListeners_[index].serial == 0 ||
            clickListeners_[index].listenerFunction == kInvalidRuntimeFunctionId)
            continue;
        slots[count++] = index;
    }

    // Insertion sort is intentional: the table is fixed at 64 entries and
    // this keeps the resequencing path allocation-free and deterministic.
    for (std::size_t index = 1; index < count; ++index) {
        const std::size_t slot = slots[index];
        std::size_t position = index;
        while (position > 0 &&
            clickListeners_[slots[position - 1]].registrationSequence >
                clickListeners_[slot].registrationSequence) {
            slots[position] = slots[position - 1];
            --position;
        }
        slots[position] = slot;
    }
    for (std::size_t index = 0; index < count; ++index)
        clickListeners_[slots[index]].registrationSequence =
            static_cast<std::uint64_t>(index + 1u);
    nextListenerRegistrationSequence_ =
        static_cast<std::uint64_t>(count + 1u);
}

bool NavigatorScriptHostAdapter::allocateListenerSequence(
    std::uint64_t& sequence)
{
    // Sequence zero is reserved for an unused record. A 64-bit sequence is
    // practically non-wrapping, but the boundary remains deterministic: if
    // it is reached outside dispatch, active records are compacted in logical
    // order; during dispatch, refusing the new registration protects any
    // already-captured snapshot from resequencing.
    if (nextListenerRegistrationSequence_ == 0u ||
        nextListenerRegistrationSequence_ ==
            std::numeric_limits<std::uint64_t>::max()) {
        if (clickDispatchActive_) return false;
        resequenceListeners();
    }
    sequence = nextListenerRegistrationSequence_++;
    return sequence != 0u;
}

bool NavigatorScriptHostAdapter::collectListenerSnapshot(
    HostObjectKind ownerKind, HostInstanceId serial,
    NavigatorScriptEventType eventType,
    std::array<ClickListenerSnapshotEntry,
        kNavigatorScriptMaxClickHandlers>& snapshot, std::size_t& count,
    bool capture) const
{
    count = 0;
    for (const ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind != ownerKind || record.serial != serial ||
            record.eventType != eventType ||
            record.listenerFunction == kInvalidRuntimeFunctionId ||
            (((record.flags & kNavigatorClickListenerCaptureFlag) != 0u) !=
                capture)) continue;
        if (count >= snapshot.size()) return false;
        ClickListenerSnapshotEntry entry{
            record.registrationSequence, record.listenerFunction};
        std::size_t position = count++;
        while (position > 0 &&
            snapshot[position - 1].registrationSequence >
                entry.registrationSequence) {
            snapshot[position] = snapshot[position - 1];
            --position;
        }
        snapshot[position] = entry;
    }
    return true;
}

bool NavigatorScriptHostAdapter::dispatchClick(RuntimeContext& runtime,
    HostInstanceId serial, RuntimeErrorCode& error, bool* defaultPrevented)
{
    const HostObjectReference target{
        serial, generation_, kNavigatorElementHostKind};
    return dispatchEvent(runtime, SourceView("click", 5u),
        NavigatorScriptEventType::Click, target, SourceView(), SourceView(),
        true, error, defaultPrevented);
}

bool NavigatorScriptHostAdapter::dispatchKeyboardEvent(
    RuntimeContext& runtime, HostInstanceId targetSerial, int keyCode,
    bool down, bool shiftPressed, RuntimeErrorCode& error,
    bool* defaultPrevented)
{
    std::string key;
    std::string code;
    if (!eventNameForKey(keyCode, shiftPressed, key, code)) {
        error = RuntimeErrorCode::InvalidHostReturn;
        if (defaultPrevented != nullptr) *defaultPrevented = false;
        return false;
    }
    const HostObjectReference target{
        targetSerial, generation_, targetSerial == 0
            ? kNavigatorDocumentHostKind : kNavigatorElementHostKind};
    const char* typeText = down ? "keydown" : "keyup";
    return dispatchEvent(runtime, SourceView(typeText, down ? 7u : 5u),
        down ? NavigatorScriptEventType::Keydown :
            NavigatorScriptEventType::Keyup, target,
        SourceView(key.data(), key.size()), SourceView(code.data(), code.size()),
        false, error, defaultPrevented);
}

bool NavigatorScriptHostAdapter::dispatchEvent(RuntimeContext& runtime,
    SourceView type, NavigatorScriptEventType eventType,
    const HostObjectReference& target, SourceView key, SourceView code,
    bool includeOnclick, RuntimeErrorCode& error, bool* defaultPrevented)
{
    error = RuntimeErrorCode::None;
    if (defaultPrevented != nullptr) *defaultPrevented = false;
    if (document_ == nullptr || !target.valid() ||
        target.generation != generation_ ||
        (target.kind == kNavigatorElementHostKind &&
            findElement(target.instanceId) == nullptr) ||
        (target.kind == kNavigatorDocumentHostKind &&
            target.instanceId != kNavigatorDocumentHostInstance) ||
        (target.kind != kNavigatorElementHostKind &&
            target.kind != kNavigatorDocumentHostKind)) {
        error = RuntimeErrorCode::StaleHostObject;
        return false;
    }
    if (clickDispatchActive_) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }

    // Snapshot the DOM ownership chain before entering user code. The serial
    // array is fixed-size and contains no native pointers; every subsequent
    // entry is revalidated against the same document and generation before it
    // can be used. Target is path[0]; ancestors follow toward the root.
    struct EventPathEntry {
        HostObjectKind kind = 0;
        HostInstanceId serial = 0;
    };
    std::array<EventPathEntry, kNavigatorScriptMaxPropagationDepth>
        propagationPath{};
    std::size_t propagationLength = 0;
    if (target.kind == kNavigatorElementHostKind) {
        HostInstanceId currentSerial = target.instanceId;
        while (currentSerial != 0) {
            if (propagationLength >= propagationPath.size()) {
                error = RuntimeErrorCode::PropagationPathLimitExceeded;
                return false;
            }
            const gxos::web::HtmlElementRef* element = findElement(currentSerial);
            if (element == nullptr || element->serial == 0) {
                error = RuntimeErrorCode::StaleHostObject;
                return false;
            }
            propagationPath[propagationLength++] = EventPathEntry{
                kNavigatorElementHostKind, element->serial};
            const HostInstanceId parentSerial = element->parentSerial;
            if (parentSerial == currentSerial) {
                error = RuntimeErrorCode::StaleHostObject;
                return false;
            }
            currentSerial = parentSerial;
        }
    }
    if (eventType != NavigatorScriptEventType::Click) {
        if (propagationLength >= propagationPath.size()) {
            error = RuntimeErrorCode::PropagationPathLimitExceeded;
            return false;
        }
        propagationPath[propagationLength++] = EventPathEntry{
            kNavigatorDocumentHostKind, kNavigatorDocumentHostInstance};
    }

    bool hasDispatchableHandler = false;
    for (std::size_t index = 0; index < propagationLength; ++index) {
        if (hasAnyEventHandler(propagationPath[index].kind,
                propagationPath[index].serial)) {
            hasDispatchableHandler = true;
            break;
        }
    }
    if (!hasDispatchableHandler) return true;

    const HostGenerationId dispatchGeneration = generation_;
    Value event;
    if (!runtime.createOrUpdateEventObject(type, target,
            HostObjectReference{propagationPath[0].serial,
                dispatchGeneration, propagationPath[0].kind}, key, code,
            event, error)) {
        return false;
    }
    clickDispatchActive_ = true;
    runtime.beginEventDispatch();
    std::vector<Value> arguments;
    try {
        arguments.reserve(1u);
        arguments.push_back(event);
    } catch (const std::bad_alloc&) {
        clickDispatchActive_ = false;
        runtime.endEventDispatch();
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    Value ignored;
    bool succeeded = true;
    bool dispatchAborted = false;
    RuntimeErrorCode firstError = RuntimeErrorCode::None;
    const auto invoke = [&](RuntimeFunctionId function) {
        if (function == kInvalidRuntimeFunctionId) return;
        RuntimeErrorCode callbackError = RuntimeErrorCode::None;
        if (!runtime.invokeFunctionInSameRealm(Value::function(function),
            arguments, ignored, callbackError)) {
            succeeded = false;
            if (firstError == RuntimeErrorCode::None) firstError = callbackError;
        }
    };
    // The phase comes from the propagation stage, never from the listener's
    // capture flag. Target capture listeners are therefore AT_TARGET.
    runtime.setEventPhase(kEventPhaseCapturing);
    // One fixed snapshot is reused for every node and phase. Capture and
    // bubble are intentionally collected at different times: mutations made
    // during capture can affect a later bubble snapshot, but never the active
    // snapshot for the current node and phase.
    std::array<ClickListenerSnapshotEntry,
        kNavigatorScriptMaxClickHandlers> listenerSnapshot{};
    std::size_t listenerSnapshotCount = 0;
    const auto collectListeners = [&](const EventPathEntry& current,
        bool capture) {
        if (!collectListenerSnapshot(current.kind, current.serial, eventType,
                listenerSnapshot, listenerSnapshotCount, capture)) {
            if (firstError == RuntimeErrorCode::None)
                firstError = RuntimeErrorCode::HostCallbackLimitExceeded;
            succeeded = false;
            dispatchAborted = true;
            return false;
        }
        return true;
    };
    const auto invokeCollectedListeners = [&](const EventPathEntry& current,
        bool capture) {
        if (listenerSnapshotCount == 0u) return true;
        const HostObjectReference currentTarget{
            current.serial, dispatchGeneration, current.kind};
        RuntimeErrorCode eventError = RuntimeErrorCode::None;
        if (!runtime.createOrUpdateEventObject(type, target, currentTarget,
                key, code, event, eventError)) {
            if (firstError == RuntimeErrorCode::None) firstError = eventError;
            succeeded = false;
            dispatchAborted = true;
            return false;
        }
        arguments[0] = event;
        for (std::size_t listenerIndex = 0;
             listenerIndex < listenerSnapshotCount; ++listenerIndex) {
            if (runtime.eventImmediatePropagationStopped()) break;
            const ClickListenerSnapshotEntry& captured =
                listenerSnapshot[listenerIndex];
            ClickListenerRecord* active = clickListenerForSequence(
                current.kind, current.serial, captured.registrationSequence);
            if (active == nullptr ||
                active->listenerFunction != captured.listenerFunction ||
                active->eventType != eventType ||
                (((active->flags & kNavigatorClickListenerCaptureFlag) != 0u) !=
                    capture)) continue;
            const RuntimeFunctionId listenerFunction =
                active->listenerFunction;
            if ((active->flags & kNavigatorClickListenerOnceFlag) != 0u)
                removeClickListener(*active);
            invoke(listenerFunction);
        }
        return true;
    };
    const auto invokeListeners = [&](const EventPathEntry& current,
        bool capture) {
        return collectListeners(current, capture) &&
            invokeCollectedListeners(current, capture);
    };
    const auto updateCurrentTarget = [&](const EventPathEntry& current) {
        const HostObjectReference currentTarget{
            current.serial, dispatchGeneration, current.kind};
        RuntimeErrorCode eventError = RuntimeErrorCode::None;
        if (!runtime.createOrUpdateEventObject(type, target, currentTarget,
                key, code, event, eventError)) {
            if (firstError == RuntimeErrorCode::None) firstError = eventError;
            succeeded = false;
            dispatchAborted = true;
            return false;
        }
        arguments[0] = event;
        return true;
    };
    const auto validatePathEntry = [&](const EventPathEntry& current) {
        const HostObjectReference reference{
            current.serial, dispatchGeneration, current.kind};
        if (generation_ == dispatchGeneration && document_ != nullptr &&
            validate(reference).succeeded()) return true;
        if (firstError == RuntimeErrorCode::None)
            firstError = RuntimeErrorCode::StaleHostObject;
        succeeded = false;
        dispatchAborted = true;
        return false;
    };

    // Capture walks the existing target-to-root path in reverse. Ancestor
    // onclick handlers are not part of this phase.
    for (std::size_t reverse = propagationLength; reverse > 1u; --reverse) {
        const EventPathEntry& current = propagationPath[reverse - 1u];
        if (!validatePathEntry(current) || !invokeListeners(current, true)) break;
        if (runtime.eventPropagationStopped()) break;
    }

    // The target has one dispatch stage. Its capture listeners, onclick, and
    // non-capture listeners share the target currentTarget. stopPropagation
    // still permits all later target handlers; immediate stop does not.
    if (!dispatchAborted && !runtime.eventPropagationStopped() &&
        validatePathEntry(propagationPath[0])) {
        runtime.setEventPhase(kEventPhaseAtTarget);
        if (invokeListeners(propagationPath[0], true) &&
            !runtime.eventImmediatePropagationStopped()) {
            // Preserve JS17's target mutation rule: the target bubble
            // snapshot is fixed before target onclick runs, while capture
            // mutations can still affect this later target-phase snapshot.
            if (collectListeners(propagationPath[0], false)) {
                const ClickHandlerRecord* record =
                    propagationPath[0].kind == kNavigatorElementHostKind
                    ? clickHandlerFor(propagationPath[0].serial) : nullptr;
                const RuntimeFunctionId onclickFunction = includeOnclick &&
                    record != nullptr ? record->onclickFunction :
                    kInvalidRuntimeFunctionId;
                if (onclickFunction != kInvalidRuntimeFunctionId) {
                    if (updateCurrentTarget(propagationPath[0]))
                        invoke(onclickFunction);
                }
                if (!dispatchAborted &&
                    !runtime.eventImmediatePropagationStopped()) {
                    invokeCollectedListeners(propagationPath[0], false);
                }
            }
        }
    }

    // Bubble uses the original forward path, starting with the target's
    // parent. Each ancestor gets a fresh non-capture snapshot at this point.
    if (!dispatchAborted && !runtime.eventPropagationStopped() &&
        !runtime.eventImmediatePropagationStopped()) {
        runtime.setEventPhase(kEventPhaseBubbling);
        for (std::size_t index = 1u; index < propagationLength; ++index) {
            const EventPathEntry& current = propagationPath[index];
            if (!validatePathEntry(current)) break;
            const ClickHandlerRecord* record =
                current.kind == kNavigatorElementHostKind
                ? clickHandlerFor(current.serial) : nullptr;
            const RuntimeFunctionId onclickFunction = includeOnclick &&
                record != nullptr ? record->onclickFunction :
                kInvalidRuntimeFunctionId;
            if (onclickFunction != kInvalidRuntimeFunctionId) {
                if (!updateCurrentTarget(current)) break;
                invoke(onclickFunction);
            }
            if (runtime.eventImmediatePropagationStopped()) break;
            if (!invokeListeners(current, false)) break;
            if (runtime.eventPropagationStopped()) break;
        }
    }
    const bool dispatchDefaultPrevented = runtime.eventDefaultPrevented();
    if (defaultPrevented != nullptr)
        *defaultPrevented = dispatchDefaultPrevented;
    runtime.endEventDispatch();
    clickDispatchActive_ = false;
    error = firstError;
    return succeeded;
}

bool NavigatorScriptHostAdapter::eventTypeFor(SourceView type,
    NavigatorScriptEventType& eventType) const
{
    if (textEquals(type, "click")) {
        eventType = NavigatorScriptEventType::Click;
        return true;
    }
    if (textEquals(type, "keydown")) {
        eventType = NavigatorScriptEventType::Keydown;
        return true;
    }
    if (textEquals(type, "keyup")) {
        eventType = NavigatorScriptEventType::Keyup;
        return true;
    }
    return false;
}

bool NavigatorScriptHostAdapter::eventNameForKey(int keyCode,
    bool shiftPressed, std::string& key, std::string& code) const
{
    key.clear();
    code.clear();
    if (keyCode >= 65 && keyCode <= 90) {
        key.assign(1, static_cast<char>(shiftPressed ? keyCode : keyCode + 32));
        code = "Key";
        code.push_back(static_cast<char>(keyCode));
        return true;
    }
    if (keyCode >= 48 && keyCode <= 57) {
        key.assign(1, static_cast<char>(keyCode));
        code = "Digit";
        code.push_back(static_cast<char>(keyCode));
        return true;
    }
    struct NamedKey { int code; const char* key; const char* name; };
    static const NamedKey named[] = {
        {13, "Enter", "Enter"}, {27, "Escape", "Escape"},
        {8, "Backspace", "Backspace"}, {9, "Tab", "Tab"},
        {32, " ", "Space"}, {37, "ArrowLeft", "ArrowLeft"},
        {38, "ArrowUp", "ArrowUp"}, {39, "ArrowRight", "ArrowRight"},
        {40, "ArrowDown", "ArrowDown"}, {16, "Shift", "Shift"},
        {17, "Control", "Control"}, {18, "Alt", "Alt"},
        {46, "Delete", "Delete"}, {36, "Home", "Home"},
        {35, "End", "End"}, {33, "PageUp", "PageUp"},
        {34, "PageDown", "PageDown"},
    };
    for (const NamedKey& namedKey : named) {
        if (namedKey.code != keyCode) continue;
        key = namedKey.key;
        code = namedKey.name;
        return true;
    }
    // The shared input bridge currently supplies virtual-key values only.
    // Unknown keys still dispatch through the normal path with the bounded
    // empty property values rather than failing the Navigator input loop.
    return true;
}

gxos::web::HtmlElementRef* NavigatorScriptHostAdapter::findElement(
    HostInstanceId serial)
{
    if (document_ == nullptr) return nullptr;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (document_->structuralElements[index].serial == serial)
            return &document_->structuralElements[index];
    }
    return nullptr;
}

const gxos::web::HtmlElementRef* NavigatorScriptHostAdapter::findElement(
    HostInstanceId serial) const
{
    if (document_ == nullptr) return nullptr;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (document_->structuralElements[index].serial == serial)
            return &document_->structuralElements[index];
    }
    return nullptr;
}

bool NavigatorScriptHostAdapter::isKnownElementSerial(
    HostInstanceId serial) const
{
    return findElement(serial) != nullptr;
}

bool NavigatorScriptHostAdapter::isDescendantOrSelf(
    std::uint64_t serial, std::uint64_t ancestorSerial) const
{
    return document_ != nullptr && isDescendantInDocument(*document_, serial,
        ancestorSerial, limits_.maxDocumentNodes);
}

HostResult NavigatorScriptHostAdapter::validate(
    const HostObjectReference& object)
{
    if (!object.valid()) return HostResult{HostResultCode::InvalidObject};
    if (object.generation != generation_)
        return HostResult{HostResultCode::StaleObject};
    if (document_ == nullptr) return HostResult{HostResultCode::InvalidObject};
    if (object.kind == kNavigatorDocumentHostKind &&
        object.instanceId == kNavigatorDocumentHostInstance) {
        return HostResult();
    }
    if (object.kind == kNavigatorElementHostKind &&
        isKnownElementSerial(object.instanceId)) {
        return HostResult();
    }
    return HostResult{HostResultCode::InvalidObject};
}

HostResult NavigatorScriptHostAdapter::getProperty(
    const HostObjectReference& object, SourceView property, HostValue& result)
{
    const HostResult validation = validate(object);
    if (!validation.succeeded()) return validation;

    if (object.kind == kNavigatorDocumentHostKind) {
        if (textEquals(property, "getElementById")) {
            result = HostValue::method(kNavigatorGetElementByIdMethod, true);
            return HostResult();
        }
        if (textEquals(property, "addEventListener")) {
            result = HostValue::method(kNavigatorAddEventListenerMethod, true,
                true);
            return HostResult();
        }
        if (textEquals(property, "removeEventListener")) {
            result = HostValue::method(kNavigatorRemoveEventListenerMethod,
                true, true);
            return HostResult();
        }
        return HostResult{HostResultCode::PropertyNotFound};
    }

    const gxos::web::HtmlElementRef* element = findElement(object.instanceId);
    if (element == nullptr) return HostResult{HostResultCode::InvalidObject};
    if (textEquals(property, "id")) {
        result = HostValue::string(SourceView(element->id.data(),
            element->id.size()));
        return HostResult();
    }
    if (textEquals(property, "tagName")) {
        returnBuffer_ = canonicalTagName(element->tagName);
        result = HostValue::string(SourceView(returnBuffer_.data(),
            returnBuffer_.size()));
        return HostResult();
    }
    if (textEquals(property, "textContent")) {
        std::string text;
        const HostResult content = textContentForElement(element->serial, text);
        if (!content.succeeded()) return content;
        returnBuffer_ = std::move(text);
        result = HostValue::string(SourceView(returnBuffer_.data(),
            returnBuffer_.size()));
        return HostResult();
    }
    if (textEquals(property, "onclick")) {
        const ClickHandlerRecord* record = clickHandlerFor(element->serial);
        result = record == nullptr ||
            record->onclickFunction == kInvalidRuntimeFunctionId
            ? HostValue::nullValue() : HostValue::function(record->onclickFunction);
        return HostResult();
    }
    if (textEquals(property, "addEventListener")) {
        result = HostValue::method(kNavigatorAddEventListenerMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "removeEventListener")) {
        result = HostValue::method(kNavigatorRemoveEventListenerMethod, true, true);
        return HostResult();
    }
    return HostResult{HostResultCode::PropertyNotFound};
}

HostResult NavigatorScriptHostAdapter::convertTextValue(
    const HostValue& value, std::string& result) const
{
    switch (value.type) {
    case HostValueType::String:
        if (value.stringValue.data == nullptr && value.stringValue.length != 0)
            return HostResult{HostResultCode::InvalidValue};
        result.assign(value.stringValue.data == nullptr ? "" :
            value.stringValue.data, value.stringValue.length);
        return HostResult();
    case HostValueType::Number: {
        if (std::isnan(value.numberValue)) {
            result = "NaN";
            return HostResult();
        }
        if (std::isinf(value.numberValue)) {
            result = std::signbit(value.numberValue) ? "-Infinity" :
                "Infinity";
            return HostResult();
        }
        if (value.numberValue == 0.0) {
            result = "0";
            return HostResult();
        }
        std::array<char, 128> buffer{};
        const auto conversion = std::to_chars(buffer.data(),
            buffer.data() + buffer.size(), value.numberValue);
        if (conversion.ec != std::errc())
            return HostResult{HostResultCode::InvalidValue};
        result.assign(buffer.data(), conversion.ptr);
        return HostResult();
    }
    case HostValueType::Boolean:
        result = value.booleanValue ? "true" : "false";
        return HostResult();
    case HostValueType::Null:
        result = "null";
        return HostResult();
    case HostValueType::Undefined:
        result = "undefined";
        return HostResult();
    case HostValueType::Object:
    case HostValueType::HostObject:
    case HostValueType::Method:
    case HostValueType::Function:
        return HostResult{HostResultCode::InvalidValue};
    }
    return HostResult{HostResultCode::InvalidValue};
}

HostResult NavigatorScriptHostAdapter::appendBoundedText(
    std::string& target, const std::string& text, std::size_t& operations) const
{
    if (!appendBounded(target, text, operations,
        limits_.maxTextAggregationOperations,
        limits_.maxTextContentAssignment)) {
        return HostResult{HostResultCode::DocumentTextLimitExceeded};
    }
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::textContentForElement(
    std::uint64_t serial, std::string& result) const
{
    if (document_ == nullptr || !isKnownElementSerial(serial))
        return HostResult{HostResultCode::InvalidObject};
    result.clear();
    const gxos::web::DocBlock* directBlock = nullptr;
    std::size_t directBlockCount = 0;
    for (const gxos::web::DocBlock& block : document_->blocks) {
        if (block.elementMetadata.serial != serial) continue;
        directBlock = &block;
        ++directBlockCount;
    }
    if (directBlockCount == 1u && directBlock != nullptr) {
        if (directBlock->text.size() > limits_.maxTextContentAssignment)
            return HostResult{HostResultCode::DocumentTextLimitExceeded};
        result = directBlock->text;
        return HostResult();
    }
    std::size_t operations = 0;
    bool matchedRun = false;
    for (const gxos::web::WebInlineItem& item : document_->inlineItems) {
        if (operations >= limits_.maxTextAggregationOperations)
            return HostResult{HostResultCode::DocumentTextLimitExceeded};
        ++operations;
        if (item.kind != gxos::web::InlineItemKind::TextRun &&
            item.kind != gxos::web::InlineItemKind::ForcedBreak) continue;
        if (!isDescendantOrSelf(item.ownerSerial, serial)) continue;
        matchedRun = true;
        const std::string text = item.kind == gxos::web::InlineItemKind::ForcedBreak
            ? "\n" : item.text;
        if (text.size() > limits_.maxTextContentAssignment ||
            result.size() > limits_.maxTextContentAssignment - text.size()) {
            return HostResult{HostResultCode::DocumentTextLimitExceeded};
        }
        result += text;
    }
    if (matchedRun) return HostResult();

    // A few existing compact blocks (notably table cells and controls) do not
    // emit a text-run item. Keep the host view authoritative for those blocks
    // without pretending that they are a general DOM tree.
    for (const gxos::web::DocBlock& block : document_->blocks) {
        bool matches = block.elementMetadata.serial == serial;
        if (!matches) {
            for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
                if (ancestor.serial == serial) {
                    matches = true;
                    break;
                }
            }
        }
        if (!matches) continue;
        const HostResult appended = appendBoundedText(result, block.text,
            operations);
        if (!appended.succeeded()) return appended;
    }
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::setElementTextContent(
    std::uint64_t serial, const std::string& text)
{
    if (document_ == nullptr || !isKnownElementSerial(serial))
        return HostResult{HostResultCode::InvalidObject};
    if (text.size() > limits_.maxTextContentAssignment)
        return HostResult{HostResultCode::DocumentTextLimitExceeded};
    if (document_->scriptMutationCount >= limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};

    std::vector<std::size_t> matchingItems;
    matchingItems.reserve(4u);
    std::size_t itemOperations = 0;
    for (std::size_t index = 0; index < document_->inlineItems.size(); ++index) {
        if (itemOperations >= limits_.maxTextAggregationOperations)
            return HostResult{HostResultCode::DocumentTextLimitExceeded};
        ++itemOperations;
        const gxos::web::WebInlineItem& item = document_->inlineItems[index];
        if ((item.kind == gxos::web::InlineItemKind::TextRun ||
                item.kind == gxos::web::InlineItemKind::ForcedBreak) &&
            isDescendantOrSelf(item.ownerSerial, serial)) {
            matchingItems.push_back(index);
        }
    }

    std::vector<std::size_t> matchingBlocks;
    matchingBlocks.reserve(4u);
    for (std::size_t index = 0; index < document_->blocks.size(); ++index) {
        const gxos::web::DocBlock& block = document_->blocks[index];
        bool matches = block.elementMetadata.serial != 0 &&
            isDescendantOrSelf(block.elementMetadata.serial, serial);
        if (!matches) {
            for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
                if (ancestor.serial == serial) {
                    matches = true;
                    break;
                }
            }
        }
        if (matches) matchingBlocks.push_back(index);
    }

    // Check vector capacity before changing anything. A successful mutation
    // is intentionally all-or-nothing for the host's bounded representation.
    if (matchingItems.empty() && !text.empty() &&
        document_->inlineItems.size() >= 2048u) {
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};
    }

    if (!matchingItems.empty()) {
        const std::size_t first = matchingItems.front();
        gxos::web::WebInlineItem& item = document_->inlineItems[first];
        item.kind = gxos::web::InlineItemKind::TextRun;
        item.text = text;
        item.ownerSerial = serial;
        const gxos::web::HtmlElementRef* element = findElement(serial);
        item.parentSerial = element == nullptr ? 0 : element->parentSerial;
        for (std::size_t position = 1; position < matchingItems.size(); ++position)
            document_->inlineItems[matchingItems[position]].text.clear();
    } else if (!text.empty()) {
        gxos::web::WebInlineItem item;
        item.kind = gxos::web::InlineItemKind::TextRun;
        item.ownerSerial = serial;
        item.flowSerial = serial;
        item.text = text;
        document_->inlineItems.push_back(std::move(item));
    }

    if (!matchingBlocks.empty()) {
        document_->blocks[matchingBlocks.front()].text = text;
        if (document_->blocks[matchingBlocks.front()].type == gxos::web::BlockType::FormSubmit) {
            document_->blocks[matchingBlocks.front()].submitLabel = text;
        }
        for (std::size_t position = 1; position < matchingBlocks.size(); ++position)
            document_->blocks[matchingBlocks[position]].text.clear();
    }

    ++document_->scriptMutationCount;
    document_->layoutDirty = true;
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::setProperty(
    const HostObjectReference& object, SourceView property,
    const HostValue& value)
{
    const HostResult validation = validate(object);
    if (!validation.succeeded()) return validation;
    if (object.kind != kNavigatorElementHostKind)
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (textEquals(property, "id") || textEquals(property, "tagName"))
        return HostResult{HostResultCode::PropertyReadOnly};
    if (textEquals(property, "onclick")) {
        const bool hadAnyHandler = hasAnyEventHandler(
            kNavigatorElementHostKind, object.instanceId);
        if (value.type == HostValueType::Null) {
            if (ClickHandlerRecord* record = clickHandlerFor(object.instanceId))
                record->onclickFunction = kInvalidRuntimeFunctionId;
            removeEmptyClickHandler(object.instanceId);
            if (hadAnyHandler && !hasAnyEventHandler(
                    kNavigatorElementHostKind, object.instanceId) &&
                clickHandlerCount_ > 0)
                --clickHandlerCount_;
            return HostResult();
        }
        if (value.type != HostValueType::Function ||
            value.functionId == kInvalidRuntimeFunctionId) {
            return HostResult{HostResultCode::InvalidValue};
        }
        if (ClickHandlerRecord* record = clickHandlerFor(object.instanceId)) {
            record->onclickFunction = value.functionId;
            return HostResult();
        }
        if (clickOnclickRecordCount_ >= callbackLimit())
            return HostResult{HostResultCode::CallbackLimitExceeded};
        clickHandlers_[clickOnclickRecordCount_++] = ClickHandlerRecord{
            object.instanceId, value.functionId};
        if (!hadAnyHandler) ++clickHandlerCount_;
        return HostResult();
    }
    if (!textEquals(property, "textContent"))
        return HostResult{HostResultCode::PropertyWriteFailed};

    std::string text;
    const HostResult conversion = convertTextValue(value, text);
    if (!conversion.succeeded()) return conversion;
    return setElementTextContent(object.instanceId, text);
}

HostResult NavigatorScriptHostAdapter::validateDocumentReceiver(
    const HostObjectReference* receiver)
{
    if (receiver == nullptr || receiver->kind != kNavigatorDocumentHostKind ||
        receiver->instanceId != kNavigatorDocumentHostInstance)
        return HostResult{HostResultCode::InvalidObject};
    return validate(*receiver);
}

HostResult NavigatorScriptHostAdapter::call(
    const HostObjectReference* receiver, std::uint32_t methodId,
    const HostValue* arguments, std::size_t argumentCount, HostValue& result)
{
    if ((methodId == kNavigatorAddEventListenerMethod ||
            methodId == kNavigatorRemoveEventListenerMethod) &&
        argumentCount == 3u) {
        if (arguments == nullptr) return HostResult{HostResultCode::InvalidValue};
        // Boolean options are a host-call compatibility shorthand. The
        // runtime-aware path below handles ordinary options objects; keeping
        // this small direct-call path in sync makes the adapter contract
        // deterministic for embedders that do not have a RuntimeContext.
        if (arguments[2].type == HostValueType::Boolean) {
            return callInternal(receiver, methodId, arguments, argumentCount,
                result, false, arguments[2].booleanValue, true);
        }
        if (arguments[2].type == HostValueType::Undefined) {
            return callInternal(receiver, methodId, arguments, argumentCount,
                result, false, false, true);
        }
    }
    return callInternal(receiver, methodId, arguments, argumentCount, result,
        false, false, false);
}

HostResult NavigatorScriptHostAdapter::callWithRuntime(
    RuntimeContext& runtime, const HostObjectReference* receiver,
    std::uint32_t methodId, const HostValue* arguments,
    std::size_t argumentCount, HostValue& result)
{
    bool optionsSupplied = false;
    bool once = false;
    bool capture = false;
    if ((methodId == kNavigatorAddEventListenerMethod ||
            methodId == kNavigatorRemoveEventListenerMethod) &&
        argumentCount == 3u) {
        if (arguments == nullptr)
            return HostResult{HostResultCode::InvalidValue};
        if (arguments[2].type == HostValueType::Boolean) {
            // The Boolean third argument represents capture only. In
            // particular, it never enables once.
            capture = arguments[2].booleanValue;
            optionsSupplied = true;
        } else if (arguments[2].type == HostValueType::Undefined) {
            // Explicit undefined is the same normalized options state as an
            // omitted third argument. Null and all other primitives remain
            // invalid, preserving the JS18-21 host contract.
            optionsSupplied = true;
        } else if (arguments[2].type == HostValueType::Object) {
            Value onceValue;
            RuntimeErrorCode optionError = RuntimeErrorCode::None;
            if (!runtime.readObjectPropertyForHost(arguments[2].objectId,
                    "once", onceValue, optionError)) {
                return HostResult{HostResultCode::InvalidValue};
            }
            if (onceValue.isUndefined()) {
                once = false;
            } else if (onceValue.isBoolean()) {
                once = onceValue.booleanValue();
            } else {
                return HostResult{HostResultCode::InvalidValue};
            }
            Value captureValue;
            optionError = RuntimeErrorCode::None;
            if (!runtime.readObjectPropertyForHost(arguments[2].objectId,
                    "capture", captureValue, optionError)) {
                return HostResult{HostResultCode::InvalidValue};
            }
            if (captureValue.isUndefined()) {
                capture = false;
            } else if (captureValue.isBoolean()) {
                capture = captureValue.booleanValue();
            } else {
                return HostResult{HostResultCode::InvalidValue};
            }
            optionsSupplied = true;
        } else {
            return HostResult{HostResultCode::InvalidValue};
        }
    }
    return callInternal(receiver, methodId, arguments, argumentCount, result,
        once, capture, optionsSupplied);
}

HostResult NavigatorScriptHostAdapter::callInternal(
    const HostObjectReference* receiver,
    std::uint32_t methodId, const HostValue* arguments,
    std::size_t argumentCount, HostValue& result, bool once,
    bool capture, bool optionsSupplied)
{
    if (receiver == nullptr) return HostResult{HostResultCode::InvalidObject};
    const HostResult receiverResult = validate(*receiver);
    if (!receiverResult.succeeded()) return receiverResult;
    if (methodId == kNavigatorAddEventListenerMethod) {
        if ((receiver->kind != kNavigatorElementHostKind &&
                receiver->kind != kNavigatorDocumentHostKind) ||
            (argumentCount != 2u &&
                (!optionsSupplied || argumentCount != 3u)) ||
            arguments == nullptr ||
            arguments[0].type != HostValueType::String ||
            arguments[1].type != HostValueType::Function ||
            arguments[1].functionId == kInvalidRuntimeFunctionId) {
            return HostResult{HostResultCode::InvalidValue};
        }
        if (arguments[0].stringValue.data == nullptr &&
            arguments[0].stringValue.length != 0) {
            return HostResult{HostResultCode::InvalidValue};
        }
        NavigatorScriptEventType eventType = NavigatorScriptEventType::Click;
        if (!eventTypeFor(arguments[0].stringValue, eventType))
            return HostResult{HostResultCode::InvalidValue};
        // The exact (owner, event type, Function ID, capture) tuple is a
        // duplicate no-op. once is deliberately excluded from identity, so a
        // second call cannot change the first registration's once behavior.
        if (clickListenerFor(receiver->kind, receiver->instanceId, eventType,
                arguments[1].functionId, capture) != nullptr) {
            result = HostValue::undefined();
            return HostResult();
        }
        if (clickListenerCount_ >= listenerLimit())
            return HostResult{HostResultCode::CallbackLimitExceeded};

        ClickListenerRecord* freeRecord = nullptr;
        for (ClickListenerRecord& candidate : clickListeners_) {
            if (candidate.serial == 0 ||
                candidate.listenerFunction == kInvalidRuntimeFunctionId) {
                freeRecord = &candidate;
                break;
            }
        }
        if (freeRecord == nullptr)
            return HostResult{HostResultCode::CallbackLimitExceeded};

        std::uint64_t sequence = 0;
        if (!allocateListenerSequence(sequence))
            return HostResult{HostResultCode::CallbackLimitExceeded};
        const bool hadAnyHandler = hasAnyEventHandler(
            receiver->kind, receiver->instanceId);
        freeRecord->serial = receiver->instanceId;
        freeRecord->listenerFunction = arguments[1].functionId;
        freeRecord->flags = (once ? kNavigatorClickListenerOnceFlag : 0u) |
            (capture ? kNavigatorClickListenerCaptureFlag : 0u);
        freeRecord->registrationSequence = sequence;
        freeRecord->ownerKind = receiver->kind;
        freeRecord->eventType = eventType;
        ++clickListenerCount_;
        if (!hadAnyHandler) ++clickHandlerCount_;
        result = HostValue::undefined();
        return HostResult();
    }
    if (methodId == kNavigatorRemoveEventListenerMethod) {
        if ((receiver->kind != kNavigatorElementHostKind &&
                receiver->kind != kNavigatorDocumentHostKind) ||
            (argumentCount != 2u &&
                (!optionsSupplied || argumentCount != 3u)) ||
            arguments == nullptr ||
            arguments[0].type != HostValueType::String ||
            arguments[1].type != HostValueType::Function ||
            arguments[1].functionId == kInvalidRuntimeFunctionId) {
            return HostResult{HostResultCode::InvalidValue};
        }
        if (arguments[0].stringValue.data == nullptr &&
            arguments[0].stringValue.length != 0) {
            return HostResult{HostResultCode::InvalidValue};
        }
        NavigatorScriptEventType eventType = NavigatorScriptEventType::Click;
        if (!eventTypeFor(arguments[0].stringValue, eventType))
            return HostResult{HostResultCode::InvalidValue};

        // Removal is deliberately a lookup only. It never creates a record,
        // and function IDs provide JavaScript function identity within this
        // same realm; source text or function shape is never compared.
        ClickListenerRecord* record = clickListenerFor(receiver->kind,
            receiver->instanceId, eventType, arguments[1].functionId,
            capture);
        if (record != nullptr) {
            removeClickListener(*record);
        }
        result = HostValue::undefined();
        return HostResult();
    }
    if (methodId != kNavigatorGetElementByIdMethod ||
        receiver->kind != kNavigatorDocumentHostKind || argumentCount != 1u ||
        arguments == nullptr || arguments[0].type != HostValueType::String) {
        return HostResult{HostResultCode::CallFailed};
    }
    if (arguments[0].stringValue.data == nullptr &&
        arguments[0].stringValue.length != 0) {
        return HostResult{HostResultCode::InvalidValue};
    }
    if (arguments[0].stringValue.length > limits_.maxDocumentIdLength)
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    const std::string id(arguments[0].stringValue.data == nullptr ? "" :
        arguments[0].stringValue.data, arguments[0].stringValue.length);

    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t index = 0; index < count; ++index) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[index];
        if (element.serial != 0 && element.id == id) {
            result = HostValue::fromHostObject(HostObjectReference{
                element.serial, generation_, kNavigatorElementHostKind});
            return HostResult();
        }
    }
    if (document_->structuralElements.size() > limits_.maxDocumentNodes)
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    // DOM-like getElementById uses Null for not found; Undefined is reserved
    // for an unknown host property in the generic JS7 contract.
    result = HostValue::nullValue();
    return HostResult();
}

bool navigatorScriptElementTextContent(
    const gxos::web::WebDocument& document, std::uint64_t serial,
    std::string& result, std::size_t maxOperations, std::size_t maxBytes)
{
    if (findElementInDocument(document, serial, document.structuralElements.size()) == nullptr)
        return false;
    result.clear();
    const gxos::web::DocBlock* directBlock = nullptr;
    std::size_t directBlockCount = 0;
    for (const gxos::web::DocBlock& block : document.blocks) {
        if (block.elementMetadata.serial != serial) continue;
        directBlock = &block;
        ++directBlockCount;
    }
    if (directBlockCount == 1u && directBlock != nullptr) {
        if (directBlock->text.size() > maxBytes) return false;
        result = directBlock->text;
        return true;
    }
    std::size_t operations = 0;
    bool matched = false;
    for (const gxos::web::WebInlineItem& item : document.inlineItems) {
        if (operations >= maxOperations) return false;
        ++operations;
        if (item.kind != gxos::web::InlineItemKind::TextRun &&
            item.kind != gxos::web::InlineItemKind::ForcedBreak) continue;
        if (!isDescendantInDocument(document, item.ownerSerial, serial,
            document.structuralElements.size())) continue;
        matched = true;
        const std::string text = item.kind == gxos::web::InlineItemKind::ForcedBreak
            ? "\n" : item.text;
        if (text.size() > maxBytes || result.size() > maxBytes - text.size())
            return false;
        result += text;
    }
    return matched || result.empty();
}

NavigatorScriptExecutionHarness::NavigatorScriptExecutionHarness(
    RuntimeLimits runtimeLimits, NavigatorScriptHostLimits hostLimits)
    : runtime_(runtimeLimits), adapter_(1u, hostLimits)
{
    runtime_.setHostAdapter(&adapter_);
}

bool NavigatorScriptExecutionHarness::installDocumentGlobal(
    RuntimeErrorCode& error)
{
    return runtime_.installHostGlobal("document",
        kNavigatorDocumentHostInstance, kNavigatorDocumentHostKind, error);
}

bool NavigatorScriptExecutionHarness::loadParsedDocument(
    gxos::web::WebDocument document, bool resetRealm, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (resetRealm) {
        runtime_.reset();
        if (!runtime_.lastResult().succeeded()) {
            error = runtime_.lastResult().runtimeError.code;
            return false;
        }
    }
    document_ = std::move(document);
    adapter_.attachDocument(document_, runtime_.hostGeneration());
    if (!installDocumentGlobal(error)) return false;
    loaded_ = true;
    return true;
}

bool NavigatorScriptExecutionHarness::loadHtml(const std::string& url,
    const std::string& html, RuntimeErrorCode& error)
{
    const gxos::web::WebDocument document = gxos::web::parseHtml(url, html);
    return loadParsedDocument(document, loaded_, error);
}

bool NavigatorScriptExecutionHarness::replaceHtml(const std::string& url,
    const std::string& html, RuntimeErrorCode& error)
{
    const gxos::web::WebDocument document = gxos::web::parseHtml(url, html);
    return loadParsedDocument(document, true, error);
}

bool NavigatorScriptExecutionHarness::invalidateDocumentGeneration(
    RuntimeErrorCode& error)
{
    if (!runtime_.invalidateHostGeneration(error)) return false;
    adapter_.setGeneration(runtime_.hostGeneration());
    return true;
}

ScriptResult NavigatorScriptExecutionHarness::execute(SourceView source)
{
    return runtime_.executeInSameRealm(source);
}

ScriptResult NavigatorScriptExecutionHarness::execute(const std::string& source)
{
    return execute(SourceView(source.data(), source.size()));
}

bool NavigatorScriptExecutionHarness::dispatchClick(std::uint64_t serial,
    RuntimeErrorCode& error, bool* defaultPrevented)
{
    return adapter_.dispatchClick(runtime_, serial, error, defaultPrevented);
}

bool NavigatorScriptExecutionHarness::relayout()
{
    if (!loaded_) return false;
    if (!document_.layoutDirty && document_.layoutRevision != 0) return true;
    gxos::web::recomputeDocumentStyles(document_);
    std::size_t extent = 0;
    for (const gxos::web::DocBlock& block : document_.blocks)
        extent = std::max(extent, block.text.size());
    document_.layoutTextExtent = std::min<std::size_t>(8192u, extent);
    document_.layoutDirty = false;
    ++document_.layoutRevision;
    return true;
}

} // namespace javascript
} // namespace gxos
