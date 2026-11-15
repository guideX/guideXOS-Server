#include "navigator_javascript/runtime.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace gxos::javascript {

struct NavigatorScriptRuntimeTestAccess {
    static bool backingCountsAgree(const RuntimeContext& runtime)
    {
        return runtime.objectStore_.objectCount() == runtime.objectCount() &&
            runtime.objectAdapters_.size() == runtime.objectCount() &&
            runtime.objectStore_.propertyCount() == runtime.propertyCount() &&
            runtime.objectStore_.arrayElementCount() ==
                runtime.arrayElementCount();
    }

    static bool storeRejectsPrototypeCycle(RuntimeContext& runtime,
        RuntimeObjectId object, RuntimeObjectId prototype)
    {
        PortableObjectError error;
        return !runtime.objectStore_.setPrototype(object, prototype, error) &&
            error.code == PortableObjectErrorCode::PrototypeCycle;
    }

    static bool storeOwnProperty(const RuntimeContext& runtime,
        RuntimeObjectId object, const char* key, Value& value, bool& found)
    {
        PortableObjectError error;
        const std::string spelling(key);
        return runtime.objectStore_.getOwnProperty(object,
                SourceView(spelling.data(), spelling.size()), value, found,
                error) && error.code == PortableObjectErrorCode::None;
    }

    static bool storeOwnSymbol(const RuntimeContext& runtime,
        RuntimeObjectId object, WellKnownSymbol symbol, Value& value,
        bool& found)
    {
        PortableObjectError error;
        return runtime.objectStore_.getOwnProperty(object, symbol, value,
                found, error) && error.code == PortableObjectErrorCode::None;
    }

    static bool arrayOwnIndex(const RuntimeContext& runtime,
        RuntimeObjectId object, std::size_t index, bool& found)
    {
        PortableObjectError error;
        return runtime.objectStore_.hasOwnArrayElement(object, index, found,
                error) && error.code == PortableObjectErrorCode::None;
    }

    static bool arrayLength(const RuntimeContext& runtime,
        RuntimeObjectId object, std::size_t& length)
    {
        PortableObjectError error;
        return runtime.objectStore_.arrayLength(object, length, error) &&
            error.code == PortableObjectErrorCode::None;
    }

    static bool arrayElement(const RuntimeContext& runtime,
        RuntimeObjectId object, std::size_t index, Value& value, bool& found)
    {
        PortableObjectError error;
        return runtime.objectStore_.getOwnArrayElement(object, index, value,
                found, error) && error.code == PortableObjectErrorCode::None;
    }

    static bool invalidObjectRejected(const RuntimeContext& runtime)
    {
        PortableObjectError error;
        bool found = false;
        Value value;
        return !runtime.objectStore_.getOwnProperty(
                kInvalidRuntimeObjectId, SourceView(), value, found, error) &&
            (error.code == PortableObjectErrorCode::InvalidObject ||
                error.code == PortableObjectErrorCode::StaleGeneration);
    }

    static bool handleIsStale(const RuntimeContext& runtime,
        RuntimeObjectId handle)
    {
        PortableObjectError error;
        return !runtime.objectStore_.validObject(handle, error) &&
            (error.code == PortableObjectErrorCode::StaleGeneration ||
                error.code == PortableObjectErrorCode::InvalidObject);
    }

    static bool propertyAttributes(const RuntimeContext& runtime,
        RuntimeObjectId object, const char* key, std::uint8_t& attributes)
    {
        PortablePropertyEntry entry;
        bool found = false;
        PortableObjectError error;
        const std::string spelling(key);
        if (!runtime.objectStore_.getOwnPropertyEntry(object,
                SourceView(spelling.data(), spelling.size()), entry, found,
                error) || error.code != PortableObjectErrorCode::None ||
            !found) return false;
        attributes = entry.attributes;
        return true;
    }

    static bool deleteProperty(RuntimeContext& runtime, RuntimeObjectId object,
        const char* key, bool& deleted)
    {
        RuntimeErrorCode error = RuntimeErrorCode::None;
        return runtime.deleteOwnProperty(object, std::string(key), deleted,
                error) && error == RuntimeErrorCode::None;
    }

    static bool fillObjectPool(RuntimeContext& runtime)
    {
        const std::vector<Value> noElements;
        while (runtime.objectStore_.objectCount() < runtime.limits_.maxObjects) {
            RuntimeObjectId object = kInvalidRuntimeObjectId;
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!runtime.createObject(false, noElements, object, error) ||
                error != RuntimeErrorCode::None) return false;
        }
        return true;
    }

    static bool adapterContainsOnlyIteratorMetadata()
    {
        return sizeof(RuntimeContext::RuntimeObject) <=
            sizeof(PortableObjectRecord);
    }

    static std::size_t propertyNameBytesUsed(const RuntimeContext& runtime)
    {
        return runtime.objectStore_.propertyNameBytesUsed();
    }

    static std::size_t sharedStoreBackingBytes(const RuntimeContext& runtime)
    {
        return runtime.objectRecordStorage_.capacity() *
                sizeof(PortableObjectRecord) +
            runtime.objectPropertyStorage_.capacity() *
                sizeof(PortablePropertyRecord) +
            runtime.arrayElementStorage_.capacity() *
                sizeof(PortableArrayElementRecord) +
            runtime.propertyNameStorage_.capacity();
    }

    static std::size_t adapterBackingBytes(const RuntimeContext& runtime)
    {
        return runtime.objectAdapters_.capacity() *
            sizeof(RuntimeContext::RuntimeObject);
    }
};

} // namespace gxos::javascript

namespace {

using namespace gxos::javascript;

int checks = 0;
int failures = 0;
std::size_t peakObjects = 0u;
std::size_t peakProperties = 0u;
std::size_t peakNameBytes = 0u;
std::size_t peakArrayElements = 0u;

void captureHighWater(const RuntimeContext& runtime)
{
    peakObjects = std::max(peakObjects, runtime.objectCount());
    peakProperties = std::max(peakProperties, runtime.propertyCount());
    peakNameBytes = std::max(peakNameBytes,
        NavigatorScriptRuntimeTestAccess::propertyNameBytesUsed(runtime));
    peakArrayElements = std::max(peakArrayElements,
        runtime.arrayElementCount());
}

void expect(bool condition, const char* label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
}

const Value* binding(const RuntimeContext& runtime, const char* key)
{
    return runtime.lookup(SourceView(key, std::char_traits<char>::length(key)));
}

RuntimeObjectId objectId(const RuntimeContext& runtime, const char* key)
{
    const Value* value = binding(runtime, key);
    return value != nullptr && value->isObject()
        ? value->objectId() : kInvalidRuntimeObjectId;
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

bool booleanIs(const RuntimeContext& runtime, const char* key, bool expected)
{
    const Value* value = binding(runtime, key);
    return value != nullptr && value->isBoolean() &&
        value->booleanValue() == expected;
}

void testSharedObjectAndArrayRecords()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var shared={value:1};var alias=shared;"
        "shared.value=shared.value+2;"
        "var rows=[shared,,undefined];rows[5]=alias;"
        "var identityOk=shared===alias&&rows[5]===shared;"
        "var holesOk=rows.length===6&&rows.hasOwnProperty('0')&&"
            "!rows.hasOwnProperty('1')&&rows.hasOwnProperty('2')&&"
            "!rows.hasOwnProperty('4')&&rows.hasOwnProperty('5');"
        "var json=JSON.stringify(shared);"
        "var parsed=JSON.parse(json,function(key,value){return value;});"
        "var jsonOk=json==='{\\\"value\\\":3}'&&parsed.value===3;");
    if (!result.succeeded()) {
        std::cerr << "JS121 object fixture status=" << scriptStatusName(result.status)
            << " runtime=" << runtimeErrorCodeName(result.runtimeError.code)
            << " parser=" << parserErrorCodeName(result.parserError.code)
            << '@' << result.parserError.location.line << ':'
            << result.parserError.location.column << '\n';
    }
    expect(result.succeeded(), "hosted objects, sparse Arrays, and JSON execute");
    expect(booleanIs(runtime, "identityOk", true),
        "hosted aliases retain one object handle");
    expect(booleanIs(runtime, "holesOk", true),
        "shared Array records preserve holes and explicit undefined");
    expect(booleanIs(runtime, "jsonOk", true),
        "JSON reads and creates objects through shared records");

    const RuntimeObjectId shared = objectId(runtime, "shared");
    const RuntimeObjectId rows = objectId(runtime, "rows");
    Value stored;
    bool found = false;
    expect(shared != kInvalidRuntimeObjectId &&
        NavigatorScriptRuntimeTestAccess::storeOwnProperty(runtime, shared,
            "value", stored, found) && found && stored.isNumber() &&
        stored.numberValue() == 3.0,
        "PortableObjectStore directly observes the hosted property update");
    expect(NavigatorScriptRuntimeTestAccess::invalidObjectRejected(runtime),
        "invalid target check is rejected by the shared store");
    std::size_t length = 0u;
    bool hasHole = true;
    bool hasUndefined = false;
    expect(rows != kInvalidRuntimeObjectId &&
        NavigatorScriptRuntimeTestAccess::arrayLength(runtime, rows, length) &&
        length == 6u &&
        NavigatorScriptRuntimeTestAccess::arrayOwnIndex(runtime, rows, 1u,
            hasHole) && !hasHole &&
        NavigatorScriptRuntimeTestAccess::arrayOwnIndex(runtime, rows, 2u,
            hasUndefined) && hasUndefined,
        "shared Array record distinguishes a hole from own undefined");
    Value undefinedElement;
    expect(NavigatorScriptRuntimeTestAccess::arrayElement(runtime, rows, 2u,
            undefinedElement, found) && found && undefinedElement.isUndefined(),
        "PortableObjectStore directly observes the hosted Array element");
    expect(NavigatorScriptRuntimeTestAccess::backingCountsAgree(runtime) &&
        NavigatorScriptRuntimeTestAccess::adapterContainsOnlyIteratorMetadata(),
        "hosted counts and small iterator sidecars share the portable backing pools");
    captureHighWater(runtime);
}

void testPrototypeSymbolsAndArrayMethods()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var proto={inherited:7};var child={own:1};"
        "var symbolObject={};symbolObject[Symbol.iterator]=9;"
        "var symbolOk=symbolObject[Symbol.iterator]===9&&"
            "Object.keys(symbolObject).length===0;"
        "var values=[3,,5];values.push(8);var popped=values.pop();"
        "var iterator=values.values();var first=iterator.next();"
        "var iteratorOk=popped===8&&values.length===3&&"
            "first.value===3&&first.done===false;");
    if (!result.succeeded()) {
        std::cerr << "JS121 Array fixture status=" << scriptStatusName(result.status)
            << " runtime=" << runtimeErrorCodeName(result.runtimeError.code)
            << " parser=" << parserErrorCodeName(result.parserError.code)
            << '@' << result.parserError.location.line << ':'
            << result.parserError.location.column << '\n';
    }
    expect(result.succeeded() && booleanIs(runtime, "symbolOk", true) &&
        booleanIs(runtime, "iteratorOk", true),
        "symbols, Array methods, and iterator results use hosted shared records");

    const RuntimeObjectId child = objectId(runtime, "child");
    const RuntimeObjectId proto = objectId(runtime, "proto");
    expect(runtime.setPrototypeForTesting(child, proto),
        "hosted prototype adapter attaches the shared prototype record");
    const ScriptResult inherited = runSameRealm(runtime,
        "var inheritedValue=child.inherited;child.inherited=8;"
        "var shadowOk=child.hasOwnProperty('inherited')&&"
            "proto.inherited===7&&child.inherited===8;");
    expect(inherited.succeeded() && booleanIs(runtime, "shadowOk", true),
        "inherited reads and own-property shadowing use the same store");
    expect(NavigatorScriptRuntimeTestAccess::storeRejectsPrototypeCycle(
            runtime, proto, child),
        "prototype cycles remain rejected by the shared store");

    const RuntimeObjectId symbolObject = objectId(runtime, "symbolObject");
    Value symbolValue;
    bool found = false;
    expect(NavigatorScriptRuntimeTestAccess::storeOwnSymbol(runtime,
            symbolObject, WellKnownSymbol::Iterator, symbolValue, found) &&
        found && symbolValue.isNumber() && symbolValue.numberValue() == 9.0,
        "PortableObjectStore observes the typed Symbol property");
    std::uint8_t attributes = 0u;
    expect(NavigatorScriptRuntimeTestAccess::propertyAttributes(runtime,
            symbolObject, "missing", attributes) == false,
        "missing properties are not fabricated by the hosted adapter");
    expect(NavigatorScriptRuntimeTestAccess::backingCountsAgree(runtime),
        "prototype and iterator activity keep store counts synchronized");
    captureHighWater(runtime);
}

void testFunctionsCallbacksOrderingAndReviver()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "function makeObject(seed){return {value:seed};}"
        "var returned=makeObject(4);"
        "function makeCapture(){var retained={value:1};"
            "return function(){retained.value=retained.value+1;"
                "return retained;};}"
        "var closure=makeCapture();var captured=closure();"
        "var callbackRows=[1,2,3];var visited=[];"
        "callbackRows.forEach(function(value,index){"
            "if(index===0)callbackRows[1]=9;"
            "visited.push(callbackRows[index]);});"
        "var holder={rows:callbackRows};"
        "var callbackOk=returned.value===4&&captured.value===2&&"
            "closure()===captured&&captured.value===3&&"
            "visited.length===3&&visited[0]===1&&visited[1]===9&&"
            "visited[2]===3&&holder.rows===callbackRows;");
    expect(result.succeeded() && booleanIs(runtime, "callbackOk", true),
        "function returns, closure references, nested Arrays, and live callbacks share the store");

    const ScriptResult ordered = runSameRealm(runtime,
        "var ordered={'b':1,'10':2,'2':3,'a':4,'drop':5};"
        "var beforeDelete=Object.keys(ordered);"
        "var initialOrder=beforeDelete.length===5&&"
            "beforeDelete[0]==='2'&&beforeDelete[1]==='10'&&"
            "beforeDelete[2]==='b'&&beforeDelete[3]==='a'&&"
            "beforeDelete[4]==='drop';");
    expect(ordered.succeeded() && booleanIs(runtime, "initialOrder", true),
        "hosted enumeration orders numeric keys before insertion-ordered strings");
    const std::size_t beforeDelete = runtime.propertyCount();
    bool deleted = false;
    expect(NavigatorScriptRuntimeTestAccess::deleteProperty(runtime,
            objectId(runtime, "ordered"), "drop", deleted) && deleted &&
        runtime.propertyCount() + 1u == beforeDelete,
        "named-property deletion releases a shared property record");
    const ScriptResult deletedKeys = runSameRealm(runtime,
        "var deletedOrder=Object.keys(ordered);");
    expect(deletedKeys.succeeded() &&
        runtime.propertyCount() + 1u == beforeDelete,
        "deletion leaves the remaining object keys enumerable");
    const ScriptResult recreated = runSameRealm(runtime,
        "ordered.drop=6;var finalOrder=Object.keys(ordered);"
        "var recreateOrder=finalOrder.length===5&&"
            "finalOrder[0]==='2'&&finalOrder[1]==='10'&&"
            "finalOrder[2]==='b'&&finalOrder[3]==='a'&&"
            "finalOrder[4]==='drop';");
    expect(recreated.succeeded() && booleanIs(runtime, "recreateOrder", true),
        "a deleted then recreated string key moves to the insertion-order tail");

    const ScriptResult revived = runSameRealm(runtime,
        "var revived=JSON.parse('{\\\"drop\\\":1,\\\"rows\\\":[10,20]}',"
            "function(key,value){if(key==='drop'||key==='0')return undefined;"
            "return value;});"
        "var reviverOk=!revived.hasOwnProperty('drop')&&"
            "revived.rows.length===2&&!revived.rows.hasOwnProperty('0')&&"
            "revived.rows[1]===20;");
    expect(revived.succeeded() && booleanIs(runtime, "reviverOk", true),
        "reviver deletion removes object properties and Array slots through the store");
    Value removed;
    bool found = true;
    expect(NavigatorScriptRuntimeTestAccess::storeOwnProperty(runtime,
            objectId(runtime, "revived"), "drop", removed, found) && !found,
        "reviver-deleted properties are absent in the store view");
    bool arrayZero = true;
    std::size_t revivedLength = 0u;
    expect(objectId(runtime, "revived") != kInvalidRuntimeObjectId &&
        NavigatorScriptRuntimeTestAccess::backingCountsAgree(runtime),
        "JSON-created graph remains in shared object storage");
    const Value* revivedValue = binding(runtime, "revived");
    Value rowsValue;
    bool rowsFound = false;
    expect(revivedValue != nullptr && revivedValue->isObject() &&
        NavigatorScriptRuntimeTestAccess::storeOwnProperty(runtime,
            revivedValue->objectId(), "rows", rowsValue, rowsFound) &&
        rowsFound && rowsValue.isObject() &&
        NavigatorScriptRuntimeTestAccess::arrayLength(runtime,
            rowsValue.objectId(), revivedLength) && revivedLength == 2u &&
        NavigatorScriptRuntimeTestAccess::arrayOwnIndex(runtime,
            rowsValue.objectId(), 0u, arrayZero) && !arrayZero,
        "reviver Array deletion leaves length intact and creates a real hole");
    captureHighWater(runtime);
}

void testCapacityFailureRecovery()
{
    RuntimeContext runtime;
    expect(run(runtime, "var first={value:1};").succeeded(),
        "capacity fixture publishes an ordinary object before exhaustion");
    expect(NavigatorScriptRuntimeTestAccess::fillObjectPool(runtime),
        "capacity fixture fills the shared object pool through RuntimeContext");
    const ScriptResult exhausted = runSameRealm(runtime, "var second={value:2};");
    expect(!exhausted.succeeded() &&
        exhausted.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
        objectId(runtime, "first") != kInvalidRuntimeObjectId &&
        runtime.objectCount() == runtime.limits().maxObjects,
        "object capacity failure preserves the published object and reports the limit");
    const ScriptResult recovered = runSameRealm(runtime,
        "first.value=9;var recoveryOk=first.value===9;");
    expect(recovered.succeeded() && booleanIs(runtime, "recoveryOk", true) &&
        NavigatorScriptRuntimeTestAccess::backingCountsAgree(runtime),
        "valid property operations still work after object allocation exhaustion");
}

void testResetInvalidatesPriorHandles()
{
    RuntimeContext runtime;
    expect(run(runtime, "var item={value:4};").succeeded(),
        "pre-reset realm creates shared records");
    const RuntimeObjectId oldHandle = objectId(runtime, "item");
    runtime.reset();
    expect(NavigatorScriptRuntimeTestAccess::handleIsStale(runtime, oldHandle),
        "reset invalidates handles held from the previous realm");
    expect(NavigatorScriptRuntimeTestAccess::backingCountsAgree(runtime),
        "reset clears object records and hosted sidecars together");
}

} // namespace

int main()
{
    RuntimeContext initialized;
    std::cout << "JS121 shared object store: initializationObjects="
        << initialized.objectCount() << " initializationProperties="
        << initialized.propertyCount() << " storeBackingBytes="
        << NavigatorScriptRuntimeTestAccess::sharedStoreBackingBytes(initialized)
        << " iteratorAdapterBackingBytes="
        << NavigatorScriptRuntimeTestAccess::adapterBackingBytes(initialized)
        << " recordBytes=" << sizeof(PortableObjectRecord) << ','
        << sizeof(PortablePropertyRecord) << ','
        << sizeof(PortableArrayElementRecord) << '\n';
    testSharedObjectAndArrayRecords();
    testPrototypeSymbolsAndArrayMethods();
    testFunctionsCallbacksOrderingAndReviver();
    testCapacityFailureRecovery();
    testResetInvalidatesPriorHandles();
    std::cout << "JS121 representative high-water: objects=" << peakObjects
        << " properties=" << peakProperties << " propertyNameBytes="
        << peakNameBytes << " arrayElements=" << peakArrayElements << '\n';
    if (failures != 0) {
        std::cerr << "Navigator JavaScript JS121 checks: "
            << (checks - failures) << '/' << checks << " passed ("
            << failures << " failed)\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS121 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
