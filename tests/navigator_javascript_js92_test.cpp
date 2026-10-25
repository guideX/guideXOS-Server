#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptRuntimeTestAccess {
    using Object = RuntimeContext::RuntimeObject;
    using Mode = RuntimeContext::ArrayIteratorMode;

    static std::size_t objectBytes() { return sizeof(Object); }
    static std::size_t valueBytes() { return sizeof(Value); }
    static std::size_t arrayElementBytes()
    {
        return sizeof(RuntimeContext::RuntimeArrayElement);
    }

    static bool cursorState(const RuntimeContext& runtime, RuntimeObjectId id,
        std::size_t& index, bool& exhausted, Mode& mode,
        RuntimeObjectId& source)
    {
        const Object* object = runtime.objectAt(id);
        if (object == nullptr || !object->arrayIterator) return false;
        index = object->arrayIteratorIndex;
        exhausted = object->arrayIteratorExhausted;
        mode = object->arrayIteratorMode;
        source = object->arrayIteratorSource;
        return true;
    }

    static bool hasOwnIndex(const RuntimeContext& runtime, RuntimeObjectId id,
        std::size_t index)
    {
        const Object* object = runtime.objectAt(id);
        if (object == nullptr || !object->array) return false;
        const std::size_t position =
            RuntimeContext::findArrayElementPosition(*object, index);
        return position < object->elements.size() &&
            object->elements[position].index == index;
    }

    static bool setArrayPrototypeIndex(RuntimeContext& runtime,
        const std::string& key, Value value)
    {
        RuntimeErrorCode error = RuntimeErrorCode::None;
        return runtime.writeProperty(runtime.arrayPrototype_, key, value,
                error, RuntimeContext::PropertyAttributes::ordinaryData()) &&
            error == RuntimeErrorCode::None;
    }

    static bool methodAttributes(const RuntimeContext& runtime,
        const char* name)
    {
        const Object* prototype = runtime.objectAt(runtime.arrayPrototype_);
        if (prototype == nullptr) return false;
        for (const RuntimeContext::RuntimeProperty& property :
            prototype->properties) {
            if (property.symbolKey == WellKnownSymbol::None &&
                property.key == name) {
                return property.value.isFunction() &&
                    property.attributes.isValid() &&
                    property.attributes.hasWritable() &&
                    property.attributes.writable() &&
                    !property.attributes.hasEnumerable() &&
                    !property.attributes.hasConfigurable();
            }
        }
        return false;
    }

    static void failNextArrayCreation(RuntimeContext& runtime)
    {
        runtime.failNextArrayCreationForTesting_ = true;
    }

    static void failNextArraySlotInsertion(RuntimeContext& runtime)
    {
        runtime.failNextArraySlotInsertionForTesting_ = true;
    }

    static void failArraySlotInsertionAfter(RuntimeContext& runtime,
        std::size_t successfulInsertions)
    {
        runtime.failArraySlotInsertionAfterCountForTesting_ = true;
        runtime.arraySlotInsertionsBeforeFailureForTesting_ =
            successfulInsertions;
    }

    static void failPropertyInsertionAfter(RuntimeContext& runtime,
        std::size_t successfulInsertions)
    {
        runtime.failPropertyInsertionAfterCountForTesting_ = true;
        runtime.propertyInsertionsBeforeFailureForTesting_ =
            successfulInsertions;
    }

    static void setObjectLimit(RuntimeContext& runtime, std::size_t limit)
    {
        runtime.limits_.maxObjects = limit;
    }

    static void setPropertyLimit(RuntimeContext& runtime, std::size_t limit)
    {
        runtime.limits_.maxTotalProperties = limit;
    }

    static void setArraySlotLimit(RuntimeContext& runtime, std::size_t limit)
    {
        runtime.limits_.maxTotalArrayElements = limit;
    }

    static std::size_t iteratorCursorCount(const RuntimeContext& runtime)
    {
        std::size_t count = 0u;
        for (const Object& object : runtime.objects_)
            if (object.arrayIterator) ++count;
        return count;
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

const Value* binding(const RuntimeContext& runtime, const char* name)
{
    return runtime.lookup(SourceView(name,
        std::char_traits<char>::length(name)));
}

RuntimeObjectId objectId(const RuntimeContext& runtime, const char* name)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isObject()
        ? value->objectId() : kInvalidRuntimeObjectId;
}

bool booleanIs(const RuntimeContext& runtime, const char* name,
    bool expected = true)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isBoolean() &&
        value->booleanValue() == expected;
}

bool numberIs(const RuntimeContext& runtime, const char* name, double expected)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isNumber() &&
        value->numberValue() == expected;
}

ScriptResult run(RuntimeContext& runtime, const char* source)
{
    return runtime.execute(SourceView(source,
        std::char_traits<char>::length(source)));
}

ScriptResult runSameRealm(RuntimeContext& runtime, const char* source)
{
    return runtime.executeInSameRealm(SourceView(source,
        std::char_traits<char>::length(source)));
}

void testPrototypeMethodsAndSparseSemantics()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var values=[10,,undefined];"
        "var keysMethod=values.keys;var entriesMethod=values.entries;"
        "var valuesMethod=values.values;"
        "var keyCursor=values.keys();var key0=keyCursor.next();"
        "var key1=keyCursor.next();var key2=keyCursor.next();"
        "var keyDone=keyCursor.next();"
        "var entryCursor=values.entries();"
        "var entry0=entryCursor.next().value;"
        "var entry1=entryCursor.next().value;"
        "var entry2=entryCursor.next().value;"
        "var entryDone=entryCursor.next();"
        "var names=Object.keys(values);"
        "var hole=Object.getOwnPropertyDescriptor(values,'1');"
        "var explicit=Object.getOwnPropertyDescriptor(values,'2');"
        "var protocolOk=keysMethod===values.keys&&"
            "entriesMethod===values.entries&&valuesMethod===values.values&&"
            "values[Symbol.iterator]===values.values;"
        "var valuesOk=key0.value===0&&key1.value===1&&key2.value===2&&"
            "keyDone.done===true&&entry0[0]===0&&entry0[1]===10&&"
            "entry1[0]===1&&entry1[1]===undefined&&"
            "entry2[0]===2&&entry2[1]===undefined&&entryDone.done===true;"
        "var ownershipOk=names.length===2&&names[0]==='0'&&"
            "names[1]==='2'&&hole===undefined&&"
            "!values.hasOwnProperty('1')&&explicit!==undefined&&"
            "explicit.value===undefined;"
        "var pairShapeOk=entry0.length===2&&entry1.length===2&&"
            "entry0.hasOwnProperty('0')&&entry0.hasOwnProperty('1')&&"
            "entry1.hasOwnProperty('0')&&entry1.hasOwnProperty('1');");
    expect(result.succeeded() && booleanIs(runtime, "protocolOk") &&
        booleanIs(runtime, "valuesOk"),
        "Array prototype lookup creates native numeric keys and genuine entry pairs");
    expect(booleanIs(runtime, "ownershipOk") &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "values"), 2u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "values"), 1u),
        "keys includes the hole while Object.keys and descriptors preserve own-property state");
    expect(booleanIs(runtime, "pairShapeOk"),
        "entry pair Arrays have length two and both occupied indexed slots");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime, "keys") &&
        NavigatorScriptRuntimeTestAccess::methodAttributes(runtime, "entries"),
        "new Array prototype methods use the existing native writable-only attribute convention");
    const Value* key = binding(runtime, "key1");
    Value keyValue;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(key != nullptr && key->isObject() &&
        runtime.readObjectPropertyForHost(key->objectId(), "value", keyValue,
            error) && keyValue.isNumber() && keyValue.numberValue() == 1.0,
        "keys yields a Number value rather than a string index");

    RuntimeContext inherited;
    const ScriptResult inheritedSetup = run(inherited,
        "var sparse=[10,,30];var inheritedEntryCursor=sparse.entries();"
        "var inheritedKey=sparse.keys();inheritedKey.next();"
        "var proto={};proto['1']=77;");
    const bool inheritedLinked = inheritedSetup.succeeded() &&
        inherited.setPrototypeForTesting(objectId(inherited, "sparse"),
            objectId(inherited, "proto"));
    Value inheritedDirect;
    RuntimeErrorCode inheritedReadError = RuntimeErrorCode::None;
    const bool inheritedRead = inheritedLinked &&
        inherited.readPropertyForTesting(objectId(inherited, "sparse"), "1",
            inheritedDirect, inheritedReadError);
    const ScriptResult inheritedResult = inheritedLinked
        ? runSameRealm(inherited,
            "var inheritedValue=sparse[1];"
            "var inherited0=inheritedEntryCursor.next().value;"
            "var inherited1=inheritedEntryCursor.next().value;"
            "var inheritedKey1=inheritedKey.next();"
            "var inheritedNames=Object.keys(sparse);"
            "var inheritedHole=Object.getOwnPropertyDescriptor(sparse,'1');"
            "var inheritedOk=inheritedValue===77&&"
                "inherited1[0]===1&&inherited1[1]===77&&"
                "inheritedKey1.value===1&&inheritedNames.length===2&&"
                "inheritedHole===undefined&&!sparse.hasOwnProperty('1');")
        : ScriptResult();
    expect(inheritedResult.succeeded() && booleanIs(inherited, "inheritedOk") &&
        inheritedRead && inheritedReadError == RuntimeErrorCode::None &&
        inheritedDirect.isNumber() && inheritedDirect.numberValue() == 77.0 &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(inherited,
            objectId(inherited, "sparse"), 1u),
        "entries uses prototype-aware indexed reads without occupying a source hole");
}

void testStablePairsCursorsAndIteration()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var objectValue={count:1};var nested=[8,9];"
        "var typed=[\"text\",false,null,undefined,objectValue,nested];"
        "var typedCursor=typed.entries();"
        "var typed0=typedCursor.next().value;"
        "var typed1=typedCursor.next().value;"
        "var typed2=typedCursor.next().value;"
        "var typed3=typedCursor.next().value;"
        "var typed4=typedCursor.next().value;"
        "var typed5=typedCursor.next().value;"
        "var source=[10,20];var entries=source.entries();"
        "var first=entries.next();var second=entries.next();source[0]=99;"
        "var stableOk=first.value!==second.value&&first.value[0]===0&&"
            "first.value[1]===10&&second.value[0]===1&&"
            "second.value[1]===20&&source[0]===99;"
        "var typedOk=typed0[1]===\"text\"&&typed1[1]===false&&"
            "typed2[1]===null&&typed3[1]===undefined&&"
            "typed4[1]===objectValue&&typed5[1]===nested;"
        "var keyA=[4,5].keys();var keyB=[4,5].keys();"
        "var keyA0=keyA.next();var keyB0=keyB.next();"
        "var independentOk=keyA0.value===0&&keyB0.value===0&&"
            "keyA.next().value===1&&keyB.next().value===1;"
        "var selfEntry=entries[Symbol.iterator]();"
        "var selfOk=selfEntry===entries&&entries.next().done===true;"
        "var keySame=keyA[Symbol.iterator]();"
        "var selfPositionOk=keySame===keyA&&keyA.next().done===true;");
    expect(result.succeeded() && booleanIs(runtime, "typedOk"),
        "entries preserves primitive types and shallow object/Array references");
    expect(booleanIs(runtime, "stableOk"),
        "retained results and pair Arrays remain independent after advancement and source mutation");
    expect(booleanIs(runtime, "independentOk"),
        "multiple KEYS cursors over one source keep independent positions");
    expect(booleanIs(runtime, "selfOk") && booleanIs(runtime, "selfPositionOk"),
        "Array cursors return themselves from Symbol.iterator without reset");
    const RuntimeObjectId firstResult = objectId(runtime, "first");
    Value firstPair;
    RuntimeErrorCode readError = RuntimeErrorCode::None;
    expect(firstResult != kInvalidRuntimeObjectId &&
        runtime.readObjectPropertyForHost(firstResult, "value", firstPair,
            readError) && firstPair.isObject() &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            firstPair.objectId(), 0u) &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            firstPair.objectId(), 1u),
        "published entry is a genuine occupied two-slot Array pair");

    RuntimeContext live;
    const ScriptResult liveResult = run(live,
        "var growing=[2,4];var growingCursor=growing.entries();"
        "var grow0=growingCursor.next().value;growing.length=4;"
        "growing[3]=8;var grow1=growingCursor.next().value;"
        "var grow2=growingCursor.next().value;"
        "var grow3=growingCursor.next().value;"
        "var shrinking=[1,2,3];var shrinkCursor=shrinking.keys();"
        "shrinkCursor.next();shrinking.length=1;"
        "var shrinkDone=shrinkCursor.next();shrinking.length=3;"
        "var afterGrowth=shrinkCursor.next();"
        "var liveOk=grow0[0]===0&&grow1[0]===1&&grow1[1]===4&&"
            "grow2[0]===2&&grow2[1]===undefined&&"
            "grow3[0]===3&&grow3[1]===8&&shrinkDone.done===true&&"
            "afterGrowth.done===true;");
    expect(liveResult.succeeded() && booleanIs(live, "liveOk"),
        "current logical length growth and shrinkage apply until terminal exhaustion");

    RuntimeContext loops;
    const ScriptResult loopResult = run(loops,
        "var values=[10,,30];var keys=0;var entriesCount=0;"
        "var valuesCount=0;"
        "for(const key of values.keys()){keys=keys+1;}"
        "for(const pair of values.entries()){entriesCount=entriesCount+1;}"
        "for(const item of values.values()){valuesCount=valuesCount+1;}"
        "var continued=0;for(const key of values.keys()){"
            "if(key===1)continue;continued=continued+1;}"
        "function first(iterable){for(const item of iterable){return item;}"
            "return undefined;}"
        "var nested=0;for(const outer of values.keys()){"
            "for(const pair of values.entries()){"
                "if(outer===pair[0])nested=nested+1;}}"
        "var closeCount=0;var cursor=values.keys();"
        "cursor['return']=function(){closeCount=closeCount+1;"
            "return {value:undefined,done:true};};"
        "for(const key of cursor){break;}"
        "var loopOk=keys===3&&entriesCount===3&&valuesCount===3&&"
            "continued===2&&first(values.keys())===0&&"
            "first(values.entries())[0]===0&&nested===3&&closeCount===1;");
    expect(loopResult.succeeded() && booleanIs(loops, "loopOk"),
        "for-of supports all Array modes, continue, break, return, IteratorClose, and nested cursors");

    RuntimeContext invalid;
    const ScriptResult invalidSetup = run(invalid,
        "var source=[1];var factory=source.keys();"
        "var make=source.keys;var fake={make:make};"
        "var badNext={next:factory.next};"
        "var badSelf={self:factory[Symbol.iterator]};");
    expect(invalidSetup.succeeded(), "wrong-receiver fixtures load");
    const ScriptResult wrongFactory = runSameRealm(invalid, "fake.make();");
    expect(wrongFactory.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "Array iterator factory rejects an ordinary-object receiver");
    RuntimeContext invalidNext;
    run(invalidNext, "var source=[1];var cursor=source.entries();"
        "var fake={next:cursor.next};");
    const ScriptResult wrongNext = runSameRealm(invalidNext, "fake.next();");
    expect(wrongNext.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "Array cursor next rejects an incorrect receiver");
    RuntimeContext invalidSelf;
    run(invalidSelf, "var source=[1];var cursor=source.keys();"
        "var fake={self:cursor[Symbol.iterator]};");
    const ScriptResult wrongSelf = runSameRealm(invalidSelf, "fake.self();");
    expect(wrongSelf.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "Array cursor self-return rejects an incorrect receiver");
}

void expectFailedEntryStep(RuntimeContext& runtime, RuntimeErrorCode expected,
    const char* label, bool injectionAfterPair)
{
    const std::size_t objectCount = runtime.objectCount();
    const std::size_t propertyCount = runtime.propertyCount();
    const std::size_t slotCount = runtime.arrayElementCount();
    std::size_t index = 99u;
    bool exhausted = true;
    RuntimeObjectId source = kInvalidRuntimeObjectId;
    NavigatorScriptRuntimeTestAccess::Mode mode =
        NavigatorScriptRuntimeTestAccess::Mode::Values;
    const bool cursorValid = NavigatorScriptRuntimeTestAccess::cursorState(
        runtime, objectId(runtime, "cursor"), index, exhausted, mode, source);
    const ScriptResult failed = runSameRealm(runtime, "cursor.next();");
    const bool unchanged = cursorValid && failed.status ==
            ScriptStatus::RuntimeFailure &&
        failed.runtimeError.code == expected &&
        runtime.objectCount() == objectCount &&
        runtime.propertyCount() == propertyCount &&
        runtime.arrayElementCount() == slotCount &&
        NavigatorScriptRuntimeTestAccess::cursorState(runtime,
            objectId(runtime, "cursor"), index, exhausted, mode, source) &&
        index == 0u && !exhausted &&
        mode == NavigatorScriptRuntimeTestAccess::Mode::Entries &&
        source == objectId(runtime, "source");
    expect(unchanged, label);
    if (injectionAfterPair) {
        expect(runtime.diagnostics().arrayIteratorEntryRollbacks > 0u,
            "unpublished entry pair transaction records its rollback");
    }
}

void testFailureAtomicityAndCapacity()
{
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.entries();");
        NavigatorScriptRuntimeTestAccess::failNextArrayCreation(runtime);
        expectFailedEntryStep(runtime, RuntimeErrorCode::AllocationFailure,
            "pair Array allocation failure preserves cursor and all active pool counters",
            false);
        const ScriptResult retry = runSameRealm(runtime,
            "var retry=cursor.next();");
        expect(retry.succeeded() && objectId(runtime, "retry") !=
                kInvalidRuntimeObjectId &&
            objectId(runtime, "cursor") != kInvalidRuntimeObjectId,
            "runtime remains usable after pair Array allocation failure");
    }
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.entries();");
        NavigatorScriptRuntimeTestAccess::failNextArraySlotInsertion(runtime);
        expectFailedEntryStep(runtime, RuntimeErrorCode::AllocationFailure,
            "first pair-slot insertion failure rolls back the unpublished pair",
            true);
        const ScriptResult retry = runSameRealm(runtime,
            "var retry=cursor.next();var retryOk=retry.value[0]===0&&"
                "retry.value[1]===10;");
        expect(retry.succeeded() && booleanIs(runtime, "retryOk"),
            "first pair-slot failure leaves the cursor at index zero");
    }
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.entries();");
        NavigatorScriptRuntimeTestAccess::failArraySlotInsertionAfter(
            runtime, 1u);
        expectFailedEntryStep(runtime, RuntimeErrorCode::AllocationFailure,
            "second pair-slot insertion failure rolls back the first occupied slot",
            true);
        const ScriptResult retry = runSameRealm(runtime,
            "var retry=cursor.next();var retryOk=retry.value[0]===0&&"
                "retry.value[1]===10;");
        expect(retry.succeeded() && booleanIs(runtime, "retryOk"),
            "second pair-slot failure leaves the cursor at index zero");
    }
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.entries();");
        const std::size_t before = runtime.objectCount();
        NavigatorScriptRuntimeTestAccess::setObjectLimit(runtime, before + 1u);
        expectFailedEntryStep(runtime, RuntimeErrorCode::ObjectLimitExceeded,
            "outer result object exhaustion rolls back the already built pair",
            true);
        NavigatorScriptRuntimeTestAccess::setObjectLimit(runtime,
            kDefaultMaxJavaScriptRuntimeObjects);
        const ScriptResult retry = runSameRealm(runtime,
            "var retry=cursor.next();var retryOk=retry.value[1]===10;");
        expect(retry.succeeded() && booleanIs(runtime, "retryOk"),
            "outer result allocation failure does not advance the cursor");
    }
    for (std::size_t inserted = 0u; inserted <= 1u; ++inserted) {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.entries();");
        NavigatorScriptRuntimeTestAccess::failPropertyInsertionAfter(
            runtime, inserted);
        expectFailedEntryStep(runtime, RuntimeErrorCode::AllocationFailure,
            inserted == 0u
                ? "first iterator-result property failure rolls back pair and result"
                : "second iterator-result property failure rolls back pair and partial result",
            true);
        const ScriptResult retry = runSameRealm(runtime,
            "var retry=cursor.next();var retryOk=retry.value[1]===10;");
        expect(retry.succeeded() && booleanIs(runtime, "retryOk"),
            "iterator-result property failure preserves the source position");
    }
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.entries();");
        NavigatorScriptRuntimeTestAccess::setPropertyLimit(runtime,
            runtime.propertyCount() + 1u);
        expectFailedEntryStep(runtime, RuntimeErrorCode::PropertyLimitExceeded,
            "property-pool exhaustion rolls back the partially built result and pair",
            true);
    }
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.entries();");
        NavigatorScriptRuntimeTestAccess::setArraySlotLimit(runtime,
            runtime.arrayElementCount() + 1u);
        expectFailedEntryStep(runtime, RuntimeErrorCode::ArrayLimitExceeded,
            "global Array-slot exhaustion rolls back the partially inserted pair",
            true);
    }
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.entries();");
        const ScriptResult first = runSameRealm(runtime,
            "var first=cursor.next();");
        expect(first.succeeded(), "one entry can be produced before terminal failure injection");
        const std::size_t objectCount = runtime.objectCount();
        std::size_t index = 0u;
        bool exhausted = false;
        RuntimeObjectId source = kInvalidRuntimeObjectId;
        NavigatorScriptRuntimeTestAccess::Mode mode =
            NavigatorScriptRuntimeTestAccess::Mode::Values;
        NavigatorScriptRuntimeTestAccess::cursorState(runtime,
            objectId(runtime, "cursor"), index, exhausted, mode, source);
        NavigatorScriptRuntimeTestAccess::setObjectLimit(runtime, objectCount);
        const ScriptResult terminalFailure = runSameRealm(runtime,
            "cursor.next();");
        std::size_t afterIndex = 0u;
        bool afterExhausted = true;
        NavigatorScriptRuntimeTestAccess::cursorState(runtime,
            objectId(runtime, "cursor"), afterIndex, afterExhausted, mode,
            source);
        expect(terminalFailure.runtimeError.code ==
                RuntimeErrorCode::ObjectLimitExceeded &&
            afterIndex == 1u && !afterExhausted &&
            runtime.objectCount() == objectCount,
            "terminal result allocation failure is reported and does not commit exhaustion");
        NavigatorScriptRuntimeTestAccess::setObjectLimit(runtime,
            kDefaultMaxJavaScriptRuntimeObjects);
        const ScriptResult terminal = runSameRealm(runtime,
            "var terminal=cursor.next();var terminalOk=terminal.done===true;");
        expect(terminal.succeeded() && booleanIs(runtime, "terminalOk"),
            "terminal completion is returned after capacity is restored");
        runSameRealm(runtime, "source.length=3;");
        const ScriptResult repeated = runSameRealm(runtime,
            "var repeated=cursor.next();var repeatedOk=repeated.done===true;");
        expect(repeated.succeeded() && booleanIs(runtime, "repeatedOk"),
            "growth after successful terminal completion does not restart the cursor");
    }
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var factory=source.keys;");
        const std::size_t before = runtime.objectCount();
        NavigatorScriptRuntimeTestAccess::setObjectLimit(runtime, before);
        const ScriptResult failure = runSameRealm(runtime, "source.keys();");
        expect(failure.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
            runtime.objectCount() == before,
            "Array cursor creation failure is bounded and does not publish a cursor");
        NavigatorScriptRuntimeTestAccess::setObjectLimit(runtime,
            kDefaultMaxJavaScriptRuntimeObjects);
        const ScriptResult rejected = runSameRealm(runtime,
            "var fake={factory:factory};fake.factory();");
        expect(rejected.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
            "Array iterator factory validates the actual method receiver");
    }
    {
        RuntimeContext runtime;
        run(runtime, "var source=[10];var cursor=source.keys();");
        const std::size_t before = runtime.objectCount();
        NavigatorScriptRuntimeTestAccess::setObjectLimit(runtime, before);
        const ScriptResult failure = runSameRealm(runtime,
            "cursor.next();");
        std::size_t index = 99u;
        bool exhausted = true;
        RuntimeObjectId source = kInvalidRuntimeObjectId;
        NavigatorScriptRuntimeTestAccess::Mode mode =
            NavigatorScriptRuntimeTestAccess::Mode::Values;
        NavigatorScriptRuntimeTestAccess::cursorState(runtime,
            objectId(runtime, "cursor"), index, exhausted, mode, source);
        expect(failure.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
            index == 0u && !exhausted,
            "KEYS result-object exhaustion is not mistaken for end of iteration");
    }
}

std::string makeAllHoleTraversal(std::size_t length, const char* mode)
{
    return "var values=[];values.length=" + std::to_string(length) +
        ";var cursor=values." + mode + "();var yielded=0;"
        "for(const item of cursor){yielded=yielded+1;}";
}

void testCapacityAndAllocationAccounting()
{
    const std::size_t lengths[] = {0u, 1u, 2u, 16u, 64u, 128u, 256u,
        512u, 1024u};
    const char* modes[] = {"values", "keys", "entries"};
    std::size_t maximumCompleted[3] = {0u, 0u, 0u};
    std::size_t firstFailureYielded[3] = {0u, 0u, 0u};
    std::size_t finalObjectHighWater[3] = {0u, 0u, 0u};
    std::size_t finalPropertyHighWater[3] = {0u, 0u, 0u};
    std::size_t finalArraySlotHighWater[3] = {0u, 0u, 0u};
    for (std::size_t modeIndex = 0u; modeIndex < 3u; ++modeIndex) {
        bool failureRecorded = false;
        for (std::size_t length : lengths) {
            RuntimeContext runtime;
            const std::string source = makeAllHoleTraversal(length,
                modes[modeIndex]);
            const ScriptResult result = runtime.execute(SourceView(
                source.data(), source.size()));
            const Value* yielded = binding(runtime, "yielded");
            const std::size_t count = yielded != nullptr && yielded->isNumber()
                ? static_cast<std::size_t>(yielded->numberValue()) : 0u;
            const bool complete = result.succeeded();
            expect(complete ? count == length : count < length,
                "bounded mode either reaches terminal done or reports a real capacity failure");
            if (complete) maximumCompleted[modeIndex] = length;
            if (!complete && !failureRecorded) {
                failureRecorded = true;
                firstFailureYielded[modeIndex] = count;
                expect(result.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded ||
                    result.runtimeError.code == RuntimeErrorCode::PropertyLimitExceeded ||
                    result.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded,
                    "near-capacity traversal fails with a deterministic pool error");
            }
            const RuntimeDiagnostics& diagnostics = runtime.diagnostics();
            finalObjectHighWater[modeIndex] = std::max(
                finalObjectHighWater[modeIndex], diagnostics.objectPoolHighWater);
            finalPropertyHighWater[modeIndex] = std::max(
                finalPropertyHighWater[modeIndex], diagnostics.propertyPoolHighWater);
            finalArraySlotHighWater[modeIndex] = std::max(
                finalArraySlotHighWater[modeIndex], diagnostics.arraySlotHighWater);
            if (length == 1024u) {
                std::size_t cursorIndex = 0u;
                bool cursorExhausted = true;
                RuntimeObjectId cursorSource = kInvalidRuntimeObjectId;
                NavigatorScriptRuntimeTestAccess::Mode cursorMode =
                    NavigatorScriptRuntimeTestAccess::Mode::Values;
                const bool cursorStateValid =
                    NavigatorScriptRuntimeTestAccess::cursorState(runtime,
                        objectId(runtime, "cursor"), cursorIndex,
                        cursorExhausted, cursorMode, cursorSource);
                expect(cursorStateValid && cursorIndex == count &&
                    !cursorExhausted && cursorMode ==
                        static_cast<NavigatorScriptRuntimeTestAccess::Mode>(
                            modeIndex) && cursorSource ==
                        objectId(runtime, "values"),
                    "actual capacity failure preserves the current cursor position");
                std::cout << "JS92 capacity mode=" << modes[modeIndex]
                    << " sourceLength=" << length << " yielded=" << count
                    << " terminal=" << (complete ? "yes" : "no")
                    << " error=" << static_cast<unsigned>(result.runtimeError.code)
                    << " cursorIndex=" << cursorIndex
                    << " cursorExhausted=" << (cursorExhausted ? "yes" : "no")
                    << " objects=" << diagnostics.objectPoolHighWater
                    << " properties=" << diagnostics.propertyPoolHighWater
                    << " arraySlots=" << diagnostics.arraySlotHighWater
                    << " environments=" << diagnostics.environmentPoolHighWater
                    << " cursors=" << NavigatorScriptRuntimeTestAccess::iteratorCursorCount(runtime)
                    << " strings=" << runtime.runtimeStringValueCount() << '\n';
            }
        }
    }
    expect(maximumCompleted[0] >= 512u && maximumCompleted[1] >= 512u &&
        maximumCompleted[2] >= 256u,
        "VALUES, KEYS, and ENTRIES complete representative bounded all-hole traversals");
    expect(firstFailureYielded[0] != 0u && firstFailureYielded[1] != 0u &&
        firstFailureYielded[2] != 0u,
        "near-capacity runs expose their first real result-allocation failure");

    for (std::size_t modeIndex = 0u; modeIndex < 3u; ++modeIndex) {
        RuntimeContext runtime;
        const std::string setup = "var values=[10];var cursor=values." +
            std::string(modes[modeIndex]) + "();";
        run(runtime, setup.c_str());
        const std::size_t objectsBefore = runtime.objectCount();
        const std::size_t propertiesBefore = runtime.propertyCount();
        const std::size_t slotsBefore = runtime.arrayElementCount();
        runSameRealm(runtime, "var firstResult=cursor.next();");
        const std::size_t objectDelta = runtime.objectCount() - objectsBefore;
        const std::size_t propertyDelta = runtime.propertyCount() - propertiesBefore;
        const std::size_t slotDelta = runtime.arrayElementCount() - slotsBefore;
        const std::size_t expectedObjectDelta = modeIndex == 2u ? 2u : 1u;
        const std::size_t expectedSlotDelta = modeIndex == 2u ? 2u : 0u;
        expect(objectDelta == expectedObjectDelta && propertyDelta == 2u &&
            slotDelta == expectedSlotDelta,
            "one mode step has the measured bounded object/property/Array-slot cost");
        std::cout << "JS92 allocation mode=" << modes[modeIndex]
            << " cursorObjects=1 cursorProperties=2 lengthChecks=1 indexedReads="
            << (modeIndex == 1u ? 0 : 1)
            << " pairObjects=" << (modeIndex == 2u ? 1 : 0)
            << " pairSlots=" << expectedSlotDelta
            << " resultObjects=1 resultProperties=2 objectDelta=" << objectDelta
            << " cursorStateUpdates=1 propertyDelta=" << propertyDelta
            << " slotDelta=" << slotDelta
            << '\n';
    }

    std::cout << "JS92 maximum completed all-hole traversal: VALUES="
        << maximumCompleted[0] << " KEYS=" << maximumCompleted[1]
        << " ENTRIES=" << maximumCompleted[2]
        << "; first failure yields VALUES=" << firstFailureYielded[0]
        << " KEYS=" << firstFailureYielded[1]
        << " ENTRIES=" << firstFailureYielded[2] << '\n';
    std::cout << "JS92 pool high-water maxima: VALUES="
        << finalObjectHighWater[0] << '/' << finalPropertyHighWater[0] << '/'
        << finalArraySlotHighWater[0] << " KEYS=" << finalObjectHighWater[1]
        << '/' << finalPropertyHighWater[1] << '/' << finalArraySlotHighWater[1]
        << " ENTRIES=" << finalObjectHighWater[2] << '/'
        << finalPropertyHighWater[2] << '/' << finalArraySlotHighWater[2] << '\n';
}

std::string denseTraversalSource(std::size_t length, const char* mode)
{
    std::string source = "var values=[";
    for (std::size_t index = 0u; index < length; ++index) {
        if (index != 0u) source += ',';
        source += std::to_string(index);
    }
    source += "];var cursor=values.";
    source += mode;
    source += "();var count=0;for(const value of cursor){count=count+1;}";
    return source;
}

long long median(std::vector<long long> samples)
{
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2u];
}

void testPerformanceAndStackObservations()
{
    const char* modes[] = {"values", "keys", "entries"};
    for (const char* mode : modes) {
        std::vector<long long> createSamples;
        std::vector<long long> firstSamples;
        std::vector<long long> middleSamples;
        std::vector<long long> terminalSamples;
        std::vector<long long> repeatedTerminalSamples;
        std::vector<long long> traversalSamples;
        std::vector<long long> sparseTraversalSamples;
        std::vector<long long> allHoleTraversalSamples;
        std::vector<long long> nearCapacityFailureSamples;
        const std::string fullTraversal = denseTraversalSource(64u, mode);
        const std::string sparseTraversal = "var values=[0,,2];"
            "values.length=64;var cursor=values." + std::string(mode) +
            "();var count=0;for(const value of cursor){count=count+1;}";
        const std::string allHoleTraversal = "var values=[];values.length=64;"
            "var cursor=values." + std::string(mode) +
            "();var count=0;for(const value of cursor){count=count+1;}";
        const std::string nearCapacityTraversal = makeAllHoleTraversal(1024u,
            mode);
        const std::size_t cursorMarker = fullTraversal.find(";var cursor=");
        const std::string arraySource = fullTraversal.substr(0u,
            cursorMarker) + ";";
        const std::string createSource = arraySource + "var cursor=values." +
            mode + "();";
        std::string advance32;
        std::string advance64;
        for (std::size_t index = 0u; index < 32u; ++index)
            advance32 += "cursor.next();";
        for (std::size_t index = 0u; index < 64u; ++index)
            advance64 += "cursor.next();";
        const std::string middleSource = createSource + advance32 +
            "var middle=cursor.next();";
        const std::string terminalSource = createSource + advance64 +
            "var terminal=cursor.next();";
        const std::string repeatedSource = terminalSource +
            "var repeated=cursor.next();";

        const auto measure = [](const std::string& source,
            std::vector<long long>& samples, const char* label,
            double expectedCount) {
            RuntimeContext runtime;
            const auto start = std::chrono::steady_clock::now();
            const ScriptResult result = runtime.execute(SourceView(
                source.data(), source.size()));
            samples.push_back(std::chrono::duration_cast<
                std::chrono::microseconds>(std::chrono::steady_clock::now() -
                    start).count());
            expect(result.succeeded() && (expectedCount < 0.0 ||
                    numberIs(runtime, "count", expectedCount)), label);
        };
        for (std::size_t sample = 0u; sample < 5u; ++sample) {
            measure(createSource, createSamples,
                "timed cursor creation succeeds", -1.0);
            measure(createSource + "var first=cursor.next();", firstSamples,
                "timed first advancement succeeds", -1.0);
            measure(middleSource, middleSamples,
                "timed middle advancement succeeds", -1.0);
            measure(terminalSource, terminalSamples,
                "timed terminal result succeeds", -1.0);
            measure(repeatedSource, repeatedTerminalSamples,
                "timed repeated terminal result succeeds", -1.0);
            measure(fullTraversal, traversalSamples,
                "timed dense 64-element for-of traversal reaches terminal done",
                64.0);
            measure(sparseTraversal, sparseTraversalSamples,
                "timed sparse 64-position traversal reaches terminal done",
                64.0);
            measure(allHoleTraversal, allHoleTraversalSamples,
                "timed all-hole 64-position traversal reaches terminal done",
                64.0);

            RuntimeContext failingRuntime;
            const auto failureStart = std::chrono::steady_clock::now();
            const ScriptResult capacityFailure = failingRuntime.execute(
                SourceView(nearCapacityTraversal.data(),
                    nearCapacityTraversal.size()));
            nearCapacityFailureSamples.push_back(std::chrono::duration_cast<
                std::chrono::microseconds>(std::chrono::steady_clock::now() -
                    failureStart).count());
            expect(!capacityFailure.succeeded() &&
                (capacityFailure.runtimeError.code ==
                        RuntimeErrorCode::ObjectLimitExceeded ||
                    capacityFailure.runtimeError.code ==
                        RuntimeErrorCode::PropertyLimitExceeded ||
                    capacityFailure.runtimeError.code ==
                        RuntimeErrorCode::ArrayLimitExceeded),
                "timed near-capacity traversal reports its bounded pool failure");
        }
        std::vector<long long> nestedPairSamples;
        if (std::string(mode) == "entries") {
            const std::string nestedPairTraversal =
                "var values=[0,1,2,3,4,5,6,7];var count=0;"
                "for(const pair of values.entries()){"
                    "for(const item of pair.values()){count=count+1;}}";
            for (std::size_t sample = 0u; sample < 5u; ++sample) {
                measure(nestedPairTraversal, nestedPairSamples,
                    "timed nested pair traversal reaches terminal done", 16.0);
            }
        }
        std::cout << "JS92 median microseconds mode=" << mode
            << " create=" << median(createSamples)
            << " firstNext=" << median(firstSamples)
            << " middleNext=" << median(middleSamples)
            << " terminalNext=" << median(terminalSamples)
            << " repeatedTerminal=" << median(repeatedTerminalSamples)
            << " forOf64=" << median(traversalSamples)
            << " sparseForOf64=" << median(sparseTraversalSamples)
            << " allHoleForOf64=" << median(allHoleTraversalSamples)
            << " nearCapacityFailure=" << median(nearCapacityFailureSamples)
            << (nestedPairSamples.empty() ? std::string() :
                " nestedPairForOf=" +
                    std::to_string(median(nestedPairSamples)))
            << " (five fresh hosted realms; source setup and parse/evaluation included)\n";
    }
    std::cout << "JS92 layout: RuntimeObject="
        << NavigatorScriptRuntimeTestAccess::objectBytes()
        << " B; Value=" << NavigatorScriptRuntimeTestAccess::valueBytes()
        << " B; RuntimeArrayElement="
        << NavigatorScriptRuntimeTestAccess::arrayElementBytes() << " B\n";
}

void testHostedFixtureAndDomInteroperability()
{
    std::ifstream file("navigator-smoke/javascript-js92.html", std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js92-native.html", html,
        loadError) && loadError == RuntimeErrorCode::None;
    expect(loaded, "JS92 hosted fixture loads through the supported HTML subset");
    if (!loaded) return;
    const std::string& source = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    expect(harness.document().scriptSources.size() == 1u &&
        source.find('<') == std::string::npos,
        "JS92 inline source preserves the HTML-subset integrity guard");
    const ScriptResult result = harness.execute(source);
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    expect(result.succeeded() && markerText.find("JS92:assertions=16:") == 0u &&
        markerText.find(":failures=0:") != std::string::npos,
        "hosted JS92 fixture passes keys, entries, protocol, length, and identity assertions");
    expect(markerText.find(":keys=true:entries=true:holes=true:length=true:typed=true") !=
            std::string::npos &&
        markerText.find(":identity=true:forof=true:events=ready") !=
            std::string::npos,
        "hosted JS92 marker reports the sparse, retained, and for-of behaviors");
    const Value* domPair = binding(harness.runtime(), "domPair");
    expect(domPair != nullptr && domPair->isObject() &&
        runSameRealm(harness.runtime(),
            "var domPairIdentity=domPair[1]===domFirst;").succeeded() &&
        booleanIs(harness.runtime(), "domPairIdentity"),
        "JS81 DOM entry pairs remain genuine Arrays and preserve canonical Element identity");
    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    const bool clicked = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, clickError);
    expect(clicked && clickError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Events preserve independent Array cursor modes and positions");
}

} // namespace

int main()
{
    testPrototypeMethodsAndSparseSemantics();
    testStablePairsCursorsAndIteration();
    testFailureAtomicityAndCapacity();
    testCapacityAndAllocationAccounting();
    testPerformanceAndStackObservations();
    testHostedFixtureAndDomInteroperability();

    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS92 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS92 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
