#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptIteratorTestAccess {
    using Collection = NavigatorScriptHostAdapter::SelectorCollectionRecord;
    using Iterator =
        NavigatorScriptHostAdapter::SelectorCollectionIteratorRecord;

    static std::size_t activeIteratorCount(
        const NavigatorScriptHostAdapter& adapter)
    {
        std::size_t count = 0u;
        for (const Iterator& record : adapter.selectorCollectionIterators_)
            if (record.active) ++count;
        return count;
    }
    static bool iteratorAt(const NavigatorScriptHostAdapter& adapter,
        std::size_t index, Iterator& result)
    {
        if (index >= adapter.selectorCollectionIterators_.size() ||
            !adapter.selectorCollectionIterators_[index].active) return false;
        result = adapter.selectorCollectionIterators_[index];
        return true;
    }
    static std::size_t iteratorBytes() { return sizeof(Iterator); }
    static std::size_t collectionBytes() { return sizeof(Collection); }
    static bool isValues(const Iterator& record)
    {
        return record.mode ==
            NavigatorScriptHostAdapter::SelectorCollectionIteratorMode::Values;
    }
};

struct NavigatorScriptRuntimeTestAccess {
    using Property = RuntimeContext::RuntimeProperty;
    using Object = RuntimeContext::RuntimeObject;
    using HostObject = RuntimeContext::HostObjectRecord;
    using Function = RuntimeContext::FunctionRecord;
    using Key = RuntimeContext::PropertyKey;

    static HostObjectReference hostReference(const RuntimeContext& runtime,
        const Value& value)
    {
        if (!value.isHostObject()) return HostObjectReference();
        const std::size_t slot = static_cast<std::size_t>(
            value.hostObjectId() & 0xffffffffull);
        if (slot >= runtime.hostObjects_.size() ||
            !runtime.hostObjects_[slot].live) return HostObjectReference();
        return runtime.hostObjects_[slot].reference;
    }
    static std::uint32_t hostMethod(const RuntimeContext& runtime,
        const Value& value)
    {
        if (!value.isFunction() ||
            value.functionId() >= runtime.functions_.size()) return 0u;
        const Function& function = runtime.functions_[value.functionId()];
        return function.kind == Function::Kind::Host
            ? function.hostMethod : 0u;
    }
    static std::size_t symbolPropertyCount(const RuntimeContext& runtime)
    {
        std::size_t count = 0u;
        for (const Object& object : runtime.objects_)
            for (const Property& property : object.properties)
                if (property.symbolKey != WellKnownSymbol::None) ++count;
        return count;
    }
    static std::size_t objectBytes() { return sizeof(Object); }
    static std::size_t propertyBytes() { return sizeof(Property); }
    static std::size_t valueBytes() { return sizeof(Value); }
    static std::size_t propertyKeyBytes() { return sizeof(Key); }
    static std::size_t propertyKeyBytes(const RuntimeContext& runtime)
    {
        return runtime.totalPropertyKeyBytes_;
    }
};

} // namespace gxos::javascript

namespace {

using namespace gxos::javascript;

std::size_t checks = 0u;
std::size_t failures = 0u;

void expect(bool condition, const std::string& label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "  FAIL: " << label << '\n';
}

const Value* binding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    return harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
}

bool booleanIs(const NavigatorScriptExecutionHarness& harness,
    const char* name, bool expected)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isBoolean() &&
        value->booleanValue() == expected;
}

double numberIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isNumber() ? value->numberValue() : -1.0;
}

std::string stringIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isString()
        ? harness.runtime().stringValue(*value) : std::string();
}

bool loadHtml(NavigatorScriptExecutionHarness& harness,
    const std::string& html, const std::string& label)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js82-native-fixture.html",
        html, error) && error == RuntimeErrorCode::None;
    expect(loaded, label);
    return loaded;
}

std::string readFixture(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::string fixtureScript(const std::string& html)
{
    const std::size_t open = html.find("<script>");
    const std::size_t close = html.find("</script>", open);
    if (open == std::string::npos || close == std::string::npos || close < open)
        return std::string();
    return html.substr(open + 8u, close - open - 8u);
}

HostInstanceId elementSerial(const NavigatorScriptExecutionHarness& harness,
    const std::string& id)
{
    for (const auto& element : harness.document().structuralElements)
        if (element.id == id) return element.serial;
    return 0u;
}

void testHostedFixtureAndNestedEvents()
{
    const std::string html = readFixture("navigator-smoke/javascript-js82.html");
    const std::string source = fixtureScript(html);
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = !source.empty() && harness.loadHtml(
        "file:///js82-hosted-fixture.html", html, error) &&
        error == RuntimeErrorCode::None;
    expect(loaded, "JS82 fixture loads into the native Navigator harness");
    if (!loaded) return;
    expect(source.find('<') == std::string::npos &&
        harness.document().scriptSources.size() == 1u &&
        harness.document().scriptSources[0] == source,
        "JS82 inline source remains within the HTML-subset script-integrity guard");

    const ScriptResult result = harness.execute(source);
    if (!result.succeeded() || numberIs(harness, "failures") != 0.0) {
        std::cerr << "  JS82 fixture status=" << scriptStatusName(result.status)
            << " runtime=" << runtimeErrorCodeName(result.runtimeError.code)
            << " parser=" << parserErrorCodeName(result.parserError.code)
            << '@' << result.parserError.location.line << ':'
            << result.parserError.location.column << " assertions="
            << numberIs(harness, "assertions") << " failures="
            << numberIs(harness, "failures") << " marker="
            << stringIs(harness, "initialMarker") << '\n';
    }
    expect(result.succeeded() && numberIs(harness, "assertions") >= 17.0 &&
        numberIs(harness, "failures") == 0.0 &&
        stringIs(harness, "initialMarker").find(
            "JS82:initial:assertions=") == 0u &&
        stringIs(harness, "initialMarker").find(":callbacks=true") !=
            std::string::npos,
        "JS82 fixture qualifies typed keys, default values iteration, callback reentrancy, self identity, bounds, scopes, adapters, and filtered selectors");

    const bool clicked = harness.dispatchClick(elementSerial(harness,
        "event-a"), error);
    expect(clicked && error == RuntimeErrorCode::None &&
        booleanIs(harness, "nestedIteratorOk", true) &&
        booleanIs(harness, "eventAMetadata", true) &&
        booleanIs(harness, "eventBMetadata", true) &&
        harness.runtime().activeCallFrameCount() == 0u,
        "JS82 default iterators survive nested Event dispatch with Event metadata and clean frames");
}

void testCallableIdentityRegistryAndReceiverValidation()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='a' class='row'></i>"
            "<i id='b' class='row'></i></body></html>",
            "JS82 receiver fixture loads")) return;
    const ScriptResult setup = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var defaultMethod=rows[Symbol.iterator];"
        "var valuesMethod=rows.values;"
        "var cursor=rows[Symbol.iterator]();"
        "var selfMethod=cursor[Symbol.iterator];"
        "var cursorCountBefore=cursor.next;"
        "var same=cursor[Symbol.iterator]();"
        "var cursorCountAfter=cursor.next;");
    const std::size_t activeBeforeSteps =
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter());
    const Value* rowsValue = binding(harness, "rows");
    const Value* cursorValue = binding(harness, "cursor");
    const Value* selfMethod = binding(harness, "selfMethod");
    const HostObjectReference rowsReference = rowsValue == nullptr
        ? HostObjectReference()
        : NavigatorScriptRuntimeTestAccess::hostReference(
            harness.runtime(), *rowsValue);
    const HostObjectReference cursorReference = cursorValue == nullptr
        ? HostObjectReference()
        : NavigatorScriptRuntimeTestAccess::hostReference(
            harness.runtime(), *cursorValue);
    NavigatorScriptIteratorTestAccess::Iterator record;
    const bool recordAvailable =
        NavigatorScriptIteratorTestAccess::iteratorAt(harness.hostAdapter(),
            0u, record);
    const std::uint32_t selfMethodId = selfMethod == nullptr ? 0u
        : NavigatorScriptRuntimeTestAccess::hostMethod(harness.runtime(),
            *selfMethod);

    HostValue selfResult;
    const HostResult directSelf = harness.hostAdapter().call(&cursorReference,
        selfMethodId, nullptr, 0u, selfResult);
    HostValue wrongTypeResult;
    const HostResult wrongType = harness.hostAdapter().call(&rowsReference,
        selfMethodId, nullptr, 0u, wrongTypeResult);
    const HostObjectReference invalidIteratorReference{999u,
        cursorReference.generation,
        kNavigatorSelectorCollectionIteratorHostKind};
    HostValue invalidIteratorResult;
    const HostResult invalidIterator = harness.hostAdapter().call(
        &invalidIteratorReference, selfMethodId, nullptr, 0u,
        invalidIteratorResult);
    if (!setup.succeeded() || activeBeforeSteps != 1u || !recordAvailable ||
        !directSelf.succeeded() || wrongType.code != HostResultCode::InvalidValue ||
        selfResult.hostObject != cursorReference) {
        std::cerr << "  JS82 receiver debug: setup="
            << scriptStatusName(setup.status) << '/' <<
            runtimeErrorCodeName(setup.runtimeError.code) << " iterators="
            << activeBeforeSteps << " record=" << recordAvailable
            << " index=" << record.nextIndex << " mode="
            << static_cast<unsigned>(record.mode) << " selfMethodId="
            << selfMethodId << " cursorKind=" << cursorReference.kind
            << " directSelf=" << static_cast<unsigned>(directSelf.code)
            << " directResultType=" << static_cast<unsigned>(selfResult.type)
            << " sameRef=" << (selfResult.hostObject == cursorReference)
            << " wrong=" << static_cast<unsigned>(wrongType.code) << '\n';
    }
    expect(setup.succeeded() &&
        selfMethod != nullptr && selfMethod->isFunction() &&
        rowsReference.kind == kNavigatorSelectorCollectionHostKind &&
        cursorReference.kind == kNavigatorSelectorCollectionIteratorHostKind &&
        activeBeforeSteps == 1u && recordAvailable &&
        NavigatorScriptIteratorTestAccess::isValues(record) &&
        record.nextIndex == 0u && directSelf.succeeded() &&
        selfResult.type == HostValueType::HostObject &&
        selfResult.hostObject == cursorReference &&
        wrongType.code == HostResultCode::InvalidValue &&
        invalidIterator.code == HostResultCode::InvalidObject &&
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter()) == activeBeforeSteps,
        "JS82 self-return uses its original host handle, rejects a wrong host kind, and consumes no registry record");

    const ScriptResult identity = harness.execute(
        "var callableIdentity=defaultMethod===valuesMethod;"
        "var sameCursorIdentity=same===cursor;"
        "var nextMethodStable=cursorCountBefore===cursorCountAfter;"
        "var first=cursor.next();var second=same.next();"
        "var cursorProgress=first.value===rows[0]&&"
        "second.value===rows[1]&&first.done===false&&second.done===false;");
    if (!identity.succeeded() || !booleanIs(harness, "callableIdentity", true) ||
        !booleanIs(harness, "sameCursorIdentity", true) ||
        !booleanIs(harness, "cursorProgress", true)) {
        std::cerr << "  JS82 identity debug: status="
            << scriptStatusName(identity.status) << '/'
            << runtimeErrorCodeName(identity.runtimeError.code)
            << " callable=" << booleanIs(harness, "callableIdentity", true)
            << " same=" << booleanIs(harness, "sameCursorIdentity", true)
            << " progress=" << booleanIs(harness, "cursorProgress", true)
            << "\n";
    }
    expect(identity.succeeded() && booleanIs(harness, "callableIdentity", true) &&
        booleanIs(harness, "sameCursorIdentity", true) &&
        booleanIs(harness, "nextMethodStable", true) &&
        booleanIs(harness, "cursorProgress", true) &&
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter()) == activeBeforeSteps,
        "JS82 collection default method shares values callable identity and iterator self-return preserves cursor position");

    RuntimeErrorCode collectionInvokeError = RuntimeErrorCode::None;
    Value collectionInvokeResult;
    const Value* unboundCollectionMethod = binding(harness, "defaultMethod");
    const bool collectionInvoked = unboundCollectionMethod != nullptr &&
        harness.runtime().invokeFunctionForTesting(*unboundCollectionMethod,
            std::vector<Value>(), collectionInvokeResult,
            collectionInvokeError);
    expect(!collectionInvoked &&
        collectionInvokeError == RuntimeErrorCode::InvalidReceiver,
        "JS82 extracted collection default method rejects a missing receiver");

    const ScriptResult invalidReceiver = harness.execute(
        "var unboundSelf=cursor[Symbol.iterator];");
    const Value* unboundSelf = binding(harness, "unboundSelf");
    RuntimeErrorCode invokeError = RuntimeErrorCode::None;
    Value invokeResult;
    const bool invoked = unboundSelf != nullptr &&
        harness.runtime().invokeFunctionForTesting(*unboundSelf,
            std::vector<Value>(), invokeResult, invokeError);
    if (invoked || invokeError != RuntimeErrorCode::InvalidReceiver) {
        std::cerr << "  JS82 invalid receiver debug: extracted="
            << (unboundSelf != nullptr) << " type="
            << (unboundSelf == nullptr ? "missing"
                : valueTypeName(unboundSelf->type())) << " invoke=" << invoked
            << " error=" << runtimeErrorCodeName(invokeError) << '\n';
    }
    expect(invalidReceiver.succeeded() && !invoked &&
        invokeError == RuntimeErrorCode::InvalidReceiver,
        "JS82 extracted self method rejects a missing receiver through the established error path");
}

struct LifetimeMeasurement {
    std::size_t objectDelta = 0u;
    std::size_t propertyDelta = 0u;
    std::size_t slotDelta = 0u;
    std::size_t hostDelta = 0u;
    bool scriptSucceeded = false;
};

LifetimeMeasurement measureLifetime(const std::string& html,
    const std::string& source, const char* label,
    NavigatorScriptExecutionHarness& harness)
{
    LifetimeMeasurement measurement;
    if (!loadHtml(harness, html, std::string("JS82 lifetime fixture loads: ") +
            label)) return measurement;
    const std::size_t objects = harness.runtime().objectCount();
    const std::size_t properties = harness.runtime().propertyCount();
    const std::size_t slots = harness.runtime().arrayElementCount();
    const std::size_t hosts = harness.runtime().hostObjectCount();
    const ScriptResult result = harness.execute(source);
    measurement.scriptSucceeded = result.succeeded();
    measurement.objectDelta = harness.runtime().objectCount() - objects;
    measurement.propertyDelta = harness.runtime().propertyCount() - properties;
    measurement.slotDelta = harness.runtime().arrayElementCount() - slots;
    measurement.hostDelta = harness.runtime().hostObjectCount() - hosts;
    return measurement;
}

void testIteratorResultLifetimeAccounting()
{
    const std::string html =
        "<html><body><i id='a' class='row'></i>"
        "<i id='b' class='row'></i></body></html>";
    NavigatorScriptExecutionHarness retained;
    const LifetimeMeasurement kept = measureLifetime(html,
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows[Symbol.iterator]();"
        "var first=cursor.next();var second=cursor.next();"
        "var retainedOk=first.value===rows[0]&&second.value===rows[1]&&"
        "first.done===false&&second.done===false;",
        "retained results", retained);
    expect(kept.scriptSucceeded && kept.objectDelta == 2u &&
        kept.propertyDelta == 4u && booleanIs(retained, "retainedOk", true),
        "JS82 retained values results remain stable and consume one result object plus two properties per step");

    NavigatorScriptExecutionHarness discarded;
    const LifetimeMeasurement dropped = measureLifetime(html,
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows[Symbol.iterator]();cursor.next();cursor.next();",
        "discarded results", discarded);
    expect(dropped.scriptSucceeded && dropped.objectDelta == 2u &&
        dropped.propertyDelta == 4u,
        "JS82 discarded results remain allocated until reset and consume the same bounded pool as retained results");

    NavigatorScriptExecutionHarness temporary;
    const LifetimeMeasurement propertyRead = measureLifetime(html,
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows[Symbol.iterator]();"
        "var element=cursor.next().value;"
        "var temporaryOk=element===rows[0];",
        "temporary .value read", temporary);
    expect(propertyRead.scriptSucceeded && propertyRead.objectDelta == 1u &&
        propertyRead.propertyDelta == 2u &&
        booleanIs(temporary, "temporaryOk", true),
        "JS82 temporary .value access keeps the canonical Element safe while retaining its outer result object");

    NavigatorScriptExecutionHarness entries;
    const LifetimeMeasurement entry = measureLifetime(html,
        "var rows=document.querySelectorAll('.row');"
        "var result=rows.entries().next();"
        "var entryOk=result.value.length===2&&result.value[0]===0&&"
        "result.value[1]===rows[0]&&result.done===false;",
        "retained entry pair", entries);
    expect(entry.scriptSucceeded && entry.objectDelta == 2u &&
        entry.propertyDelta == 2u && entry.slotDelta == 2u &&
        booleanIs(entries, "entryOk", true),
        "JS82 retained entries preserve the indexed two-slot Array and canonical Element under JS81 allocation semantics");

    NavigatorScriptExecutionHarness exhaustion;
    const LifetimeMeasurement terminal = measureLifetime(
        "<html><body><i class='row'></i></body></html>",
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows[Symbol.iterator]();var first=cursor.next();"
        "var end=cursor.next();var endAgain=cursor.next();"
        "var terminalsOk=first.done===false&&end.done===true&&"
        "endAgain.done===true&&end.value===undefined&&"
        "endAgain.value===undefined&&end!==endAgain;",
        "repeated terminal results", exhaustion);
    expect(terminal.scriptSucceeded && terminal.objectDelta == 3u &&
        terminal.propertyDelta == 6u &&
        booleanIs(exhaustion, "terminalsOk", true),
        "JS82 repeated exhaustion returns distinct stable results and consumes one object plus two properties per call");

    std::cout << "JS82 lifetime deltas: retained=" << kept.objectDelta
        << " objects/" << kept.propertyDelta << " properties; discarded="
        << dropped.objectDelta << '/' << dropped.propertyDelta
        << "; temporary-value=" << propertyRead.objectDelta << '/'
        << propertyRead.propertyDelta << "; entries=" << entry.objectDelta
        << " objects/" << entry.slotDelta << " slots/"
        << entry.propertyDelta << " properties; repeated-exhaustion="
        << terminal.objectDelta << '/' << terminal.propertyDelta << '\n';
}

std::string fullCapacityHtml()
{
    std::string html = "<html class='all'><body class='all'><div class='all'>";
    for (std::size_t index = 0u; index < 1021u; ++index)
        html += "<i class='all'></i>";
    html += "</div></body></html>";
    return html;
}

void testDefaultIteratorPoolFailure()
{
    NavigatorScriptExecutionHarness baseline;
    RuntimeLimits narrowLimits;
    narrowLimits.maxTotalProperties = baseline.runtime().propertyCount() + 1u;
    NavigatorScriptExecutionHarness narrow(narrowLimits);
    if (!loadHtml(narrow, "<html><body><i class='row'></i></body></html>",
            "JS82 bounded property fixture loads")) return;
    const ScriptResult setup = narrow.execute(
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows[Symbol.iterator]();");
    const std::size_t objectsBefore = narrow.runtime().objectCount();
    const std::size_t propertiesBefore = narrow.runtime().propertyCount();
    const std::size_t hostsBefore = narrow.runtime().hostObjectCount();
    const ScriptResult failedStep = narrow.execute("var failed=cursor.next();");
    NavigatorScriptIteratorTestAccess::Iterator narrowRecord;
    const bool narrowRecordAvailable =
        NavigatorScriptIteratorTestAccess::iteratorAt(narrow.hostAdapter(),
            0u, narrowRecord);
    expect(setup.succeeded() && !failedStep.succeeded() &&
        failedStep.runtimeError.code == RuntimeErrorCode::PropertyLimitExceeded &&
        narrow.runtime().objectCount() == objectsBefore &&
        narrow.runtime().propertyCount() == propertiesBefore &&
        narrow.runtime().hostObjectCount() == hostsBefore &&
        narrowRecordAvailable && narrowRecord.nextIndex == 0u &&
        !narrowRecord.exhausted,
        "JS82 default iterator allocation failure rolls back the partial result and leaves the cursor unadvanced");

    NavigatorScriptExecutionHarness hostBaseline;
    if (!loadHtml(hostBaseline,
            "<html><body><i class='row'></i></body></html>",
            "JS82 host-handle baseline fixture loads")) return;
    const ScriptResult hostBaselineSetup = hostBaseline.execute(
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows[Symbol.iterator]();");
    RuntimeLimits hostLimits;
    hostLimits.maxHostObjects = hostBaseline.runtime().hostObjectCount();
    NavigatorScriptExecutionHarness hostLimited(hostLimits);
    if (!loadHtml(hostLimited,
            "<html><body><i class='row'></i></body></html>",
            "JS82 host-pool fixture loads")) return;
    const ScriptResult hostSetup = hostLimited.execute(
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows[Symbol.iterator]();");
    const std::size_t hostObjectsBefore =
        hostLimited.runtime().hostObjectCount();
    const std::size_t hostRuntimeObjectsBefore =
        hostLimited.runtime().objectCount();
    const std::size_t hostPropertiesBefore =
        hostLimited.runtime().propertyCount();
    const ScriptResult hostFailure = hostLimited.execute(
        "var unavailable=cursor.next();");
    NavigatorScriptIteratorTestAccess::Iterator hostRecord;
    const bool hostRecordAvailable =
        NavigatorScriptIteratorTestAccess::iteratorAt(
            hostLimited.hostAdapter(), 0u, hostRecord);
    expect(hostBaselineSetup.succeeded() && hostSetup.succeeded() &&
        !hostFailure.succeeded() &&
        hostFailure.runtimeError.code == RuntimeErrorCode::HostObjectLimitExceeded &&
        hostLimited.runtime().hostObjectCount() == hostObjectsBefore &&
        hostLimited.runtime().objectCount() == hostRuntimeObjectsBefore &&
        hostLimited.runtime().propertyCount() == hostPropertiesBefore &&
        hostRecordAvailable && hostRecord.nextIndex == 0u &&
        !hostRecord.exhausted,
        "JS82 Element-handle exhaustion rolls back the prebuilt result and preserves the next cursor position");

    NavigatorScriptExecutionHarness full;
    if (!loadHtml(full, fullCapacityHtml(),
            "JS82 default iterator saturation fixture loads")) return;
    const ScriptResult saturated = full.execute(
        "var rows=document.querySelectorAll('.all');"
        "var cursor=rows[Symbol.iterator]();var made=0;"
        "while(made<1024){cursor.next();made=made+1;}");
    NavigatorScriptIteratorTestAccess::Iterator fullRecord;
    const bool fullRecordAvailable =
        NavigatorScriptIteratorTestAccess::iteratorAt(full.hostAdapter(),
            0u, fullRecord);
    expect(!saturated.succeeded() &&
        saturated.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
        numberIs(full, "made") == 1020.0 &&
        full.runtime().objectCount() == kDefaultMaxJavaScriptRuntimeObjects &&
        fullRecordAvailable && fullRecord.nextIndex == 1020u &&
        !fullRecord.exhausted,
        "JS82 discarded traversal deterministically saturates the fixed object pool without advancing the failed cursor step");
    std::cout << "JS82 discarded saturation: " << numberIs(full, "made")
        << " successful results; objects=" << full.runtime().objectCount()
        << '/' << kDefaultMaxJavaScriptRuntimeObjects << "; properties="
        << full.runtime().propertyCount() << '/'
        << kDefaultMaxJavaScriptRuntimeProperties << "; iterator records="
        << kNavigatorScriptMaxCollectionIterators << " x "
        << NavigatorScriptIteratorTestAccess::iteratorBytes() << " B\n";
}

void testStaleAndSerialReuseProtection()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='same' class='row'></i></body></html>",
            "JS82 generation fixture loads")) return;
    const HostGenerationId oldGeneration = harness.hostAdapter().generation();
    const ScriptResult setup = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows[Symbol.iterator]();var first=cursor.next();");
    const HostInstanceId oldSerial = elementSerial(harness, "same");
    RuntimeErrorCode generationError = RuntimeErrorCode::None;
    const bool invalidated =
        harness.invalidateDocumentGeneration(generationError);
    harness.document() = gxos::web::parseHtml(
        "file:///js82-generation-replacement.html",
        "<html><body><i id='same' class='row'></i></body></html>");
    harness.hostAdapter().attachDocument(harness.document(),
        harness.runtime().hostGeneration());
    const ScriptResult stale = harness.execute(
        "var staleSelf=cursor[Symbol.iterator]();");
    expect(setup.succeeded() && invalidated &&
        generationError == RuntimeErrorCode::None &&
        harness.hostAdapter().generation() != oldGeneration && oldSerial != 0u &&
        elementSerial(harness, "same") == oldSerial && !stale.succeeded() &&
        stale.runtimeError.code == RuntimeErrorCode::StaleHostObject &&
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter()) == 0u,
        "JS82 self-return lookup fails closed across document-generation and structural-serial reuse");
}

void testMeasuredRepresentation()
{
    expect(sizeof(NavigatorScriptSelectorDescriptor) == 1918u &&
        NavigatorScriptIteratorTestAccess::collectionBytes() == 1936u &&
        NavigatorScriptIteratorTestAccess::iteratorBytes() == 24u &&
        kNavigatorScriptMaxSelectorCollections == 128u &&
        kNavigatorScriptMaxCollectionIterators == 128u &&
        kDefaultMaxJavaScriptRuntimeObjects == 1024u &&
        kDefaultMaxJavaScriptRuntimeProperties == 4096u &&
        kDefaultMaxJavaScriptArrayElementCount == 4096u &&
        kDefaultMaxJavaScriptHostObjects == 2048u,
        "JS82 preserves the qualified collection, iterator, object, property, slot, and host-object capacities");
    std::cout << "JS82 representation: Value="
        << NavigatorScriptRuntimeTestAccess::valueBytes() << " B; property="
        << NavigatorScriptRuntimeTestAccess::propertyBytes()
        << " B; typed-key scratch="
        << NavigatorScriptRuntimeTestAccess::propertyKeyBytes()
        << " B; object="
        << NavigatorScriptRuntimeTestAccess::objectBytes()
        << " B; iterator="
        << NavigatorScriptIteratorTestAccess::iteratorBytes()
        << " B x " << kNavigatorScriptMaxCollectionIterators << "="
        << NavigatorScriptIteratorTestAccess::iteratorBytes() *
            kNavigatorScriptMaxCollectionIterators
        << " B; symbol identity=1 enum value; dynamic symbol allocation=0\n";
}

} // namespace

int main()
{
    testHostedFixtureAndNestedEvents();
    testCallableIdentityRegistryAndReceiverValidation();
    testIteratorResultLifetimeAccounting();
    testDefaultIteratorPoolFailure();
    testStaleAndSerialReuseProtection();
    testMeasuredRepresentation();
    std::cout << "Navigator JavaScript JS82 checks: " << checks - failures
        << '/' << checks << " passed\n";
    return failures == 0u ? 0 : 1;
}
