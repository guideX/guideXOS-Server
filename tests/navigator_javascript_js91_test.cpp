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
    using Attributes = RuntimeContext::PropertyAttributes;
    using Object = RuntimeContext::RuntimeObject;

    static bool setAttributes(RuntimeContext& runtime, RuntimeObjectId id,
        const std::string& key, bool writable, bool enumerable,
        bool configurable)
    {
        Object* object = runtime.objectAt(id);
        if (object == nullptr) return false;
        for (RuntimeContext::RuntimeProperty& property : object->properties) {
            if (property.symbolKey == WellKnownSymbol::None &&
                property.key == key) {
                property.attributes.bits = static_cast<std::uint8_t>(
                    Attributes::WritableKnown | Attributes::EnumerableKnown |
                    Attributes::ConfigurableKnown |
                    (writable ? static_cast<std::uint8_t>(
                        Attributes::Writable) : 0u) |
                    (enumerable ? static_cast<std::uint8_t>(
                        Attributes::Enumerable) : 0u) |
                    (configurable ? static_cast<std::uint8_t>(
                        Attributes::Configurable) : 0u));
                return true;
            }
        }
        return false;
    }

    static bool setPartialAttributes(RuntimeContext& runtime,
        RuntimeObjectId id, const std::string& key)
    {
        Object* object = runtime.objectAt(id);
        if (object == nullptr) return false;
        for (RuntimeContext::RuntimeProperty& property : object->properties) {
            if (property.symbolKey == WellKnownSymbol::None &&
                property.key == key) {
                property.attributes = Attributes::writableOnly(false);
                return true;
            }
        }
        return false;
    }

    static bool setPartialSymbolAttributes(RuntimeContext& runtime,
        RuntimeObjectId id, WellKnownSymbol symbol)
    {
        Object* object = runtime.objectAt(id);
        if (object == nullptr) return false;
        for (RuntimeContext::RuntimeProperty& property : object->properties) {
            if (property.symbolKey == symbol) {
                property.attributes = Attributes::writableOnly(false);
                return true;
            }
        }
        return false;
    }

    static bool setArrayPrototypeIndex(RuntimeContext& runtime,
        const std::string& key, Value value)
    {
        RuntimeErrorCode error = RuntimeErrorCode::None;
        return runtime.writeProperty(runtime.arrayPrototype_, key, value,
            error, Attributes::ordinaryData()) &&
            error == RuntimeErrorCode::None;
    }

    static bool linkPrototype(RuntimeContext& runtime, RuntimeObjectId object,
        RuntimeObjectId prototype)
    {
        Object* target = runtime.objectAt(object);
        if (target == nullptr || runtime.objectAt(prototype) == nullptr)
            return false;
        target->prototype = prototype;
        return true;
    }

    static bool descriptorFieldsHaveOrdinaryAttributes(
        const RuntimeContext& runtime, RuntimeObjectId id)
    {
        const Object* object = runtime.objectAt(id);
        if (object == nullptr || object->array ||
            object->properties.size() != 4u) return false;
        const char* keys[] = {"value", "writable", "enumerable",
            "configurable"};
        for (std::size_t index = 0u; index < 4u; ++index) {
            const RuntimeContext::RuntimeProperty& property =
                object->properties[index];
            if (property.symbolKey != WellKnownSymbol::None ||
                property.key != keys[index] ||
                !property.attributes.isValid() ||
                !property.attributes.isFullyKnown() ||
                !property.attributes.writable() ||
                !property.attributes.enumerable() ||
                !property.attributes.configurable()) return false;
        }
        return true;
    }

    static bool readOwn(const RuntimeContext& runtime, RuntimeObjectId id,
        const std::string& key, Value& value)
    {
        const Object* object = runtime.objectAt(id);
        if (object == nullptr) return false;
        for (const RuntimeContext::RuntimeProperty& property :
            object->properties) {
            if (property.symbolKey == WellKnownSymbol::None &&
                property.key == key) {
                value = property.value;
                return true;
            }
        }
        return false;
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

    static void setStringValueLimit(RuntimeContext& runtime,
        std::size_t limit)
    {
        runtime.limits_.maxRuntimeStringValues = limit;
    }

    static std::size_t stringValueCount(const RuntimeContext& runtime)
    {
        return runtime.strings_.size();
    }

    static bool createEmptyObject(RuntimeContext& runtime,
        RuntimeObjectId& id)
    {
        const std::vector<Value> empty;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        return runtime.createObject(false, empty, id, error) &&
            error == RuntimeErrorCode::None;
    }

    static bool insertOrdinaryField(RuntimeContext& runtime,
        RuntimeObjectId id, const char* key, Value value)
    {
        RuntimeErrorCode error = RuntimeErrorCode::None;
        return runtime.writeProperty(id, std::string(key), value, error,
                Attributes::ordinaryData()) &&
            error == RuntimeErrorCode::None;
    }

    static bool validFullyKnownAttributes()
    {
        const Attributes attributes = Attributes::ordinaryData();
        return attributes.isValid() && attributes.isFullyKnown() &&
            attributes.writable() && attributes.enumerable() &&
            attributes.configurable();
    }

    static std::size_t objectBytes() { return sizeof(Object); }
    static std::size_t propertyBytes()
    {
        return sizeof(RuntimeContext::RuntimeProperty);
    }
    static std::size_t arrayElementBytes()
    {
        return sizeof(RuntimeContext::RuntimeArrayElement);
    }
    static std::size_t attributeBytes() { return sizeof(Attributes); }
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

bool numberField(const RuntimeContext& runtime, RuntimeObjectId object,
    const char* key, double expected)
{
    Value value;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    return NavigatorScriptRuntimeTestAccess::readOwn(runtime, object, key,
            value) && value.isNumber() && value.numberValue() == expected &&
        error == RuntimeErrorCode::None;
}

bool booleanField(const RuntimeContext& runtime, RuntimeObjectId object,
    const char* key, bool expected)
{
    Value value;
    return NavigatorScriptRuntimeTestAccess::readOwn(runtime, object, key,
        value) && value.isBoolean() && value.booleanValue() == expected;
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

void testOrdinaryAndKeySemantics()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var item={name:'Navigator',version:1,active:false};"
        "item['0']=10;item['01']=11;item['1.0']=12;item['-1']=13;"
        "item['4294967294']=14;item['4294967295']=15;"
        "item[Symbol.iterator]=16;"
        "var descriptor=Object.getOwnPropertyDescriptor(item,'name');"
        "var booleanDescriptor=Object.getOwnPropertyDescriptor(item,'active');"
        "var numeric=Object.getOwnPropertyDescriptor(item,0);"
        "var leading=Object.getOwnPropertyDescriptor(item,'01');"
        "var decimal=Object.getOwnPropertyDescriptor(item,'1.0');"
        "var negative=Object.getOwnPropertyDescriptor(item,'-1');"
        "var maxIndex=Object.getOwnPropertyDescriptor(item,'4294967294');"
        "var maxUint=Object.getOwnPropertyDescriptor(item,'4294967295');"
        "var symbol=Object.getOwnPropertyDescriptor(item,Symbol.iterator);"
        "var stringSymbol=Object.getOwnPropertyDescriptor(item,'Symbol.iterator');"
        "var arrayIterator=Object.getOwnPropertyDescriptor([1],Symbol.iterator);"
        "var missing=Object.getOwnPropertyDescriptor(item,'missing');"
        "var sameMethod=Object.getOwnPropertyDescriptor==="
            "Object['getOwnPropertyDescriptor'];"
        "var ordinaryOk=descriptor.value==='Navigator'&&"
            "descriptor.writable===true&&descriptor.enumerable===true&&"
            "descriptor.configurable===true&&numeric.value===10&&"
            "booleanDescriptor.value===false&&"
            "leading.value===11&&decimal.value===12&&negative.value===13&&"
            "maxIndex.value===14&&maxUint.value===15&&symbol.value===16&&"
            "stringSymbol===undefined&&arrayIterator===undefined&&"
            "missing===undefined&&sameMethod;"
        "var descriptorKeys=Object.keys(descriptor);");
    const RuntimeObjectId descriptor = objectId(runtime, "descriptor");
    const RuntimeObjectId symbol = objectId(runtime, "symbol");
    expect(result.succeeded() && booleanIs(runtime, "ordinaryOk"),
        "Object namespace dispatch and ordinary/numeric/typed-Symbol lookups preserve distinct keys");
    expect(descriptor != kInvalidRuntimeObjectId &&
        NavigatorScriptRuntimeTestAccess::descriptorFieldsHaveOrdinaryAttributes(
            runtime, descriptor) &&
        booleanField(runtime, descriptor, "writable", true) &&
        booleanField(runtime, descriptor, "enumerable", true) &&
        booleanField(runtime, descriptor, "configurable", true),
        "descriptor is a genuine ordinary object with four correctly typed, fully classified fields");
    const RuntimeObjectId booleanDescriptor = objectId(runtime,
        "booleanDescriptor");
    Value booleanValue;
    expect(NavigatorScriptRuntimeTestAccess::readOwn(runtime,
            booleanDescriptor, "value", booleanValue) &&
        booleanValue.isBoolean() && !booleanValue.booleanValue(),
        "descriptor value preserves false as a Boolean rather than converting it");
    expect(symbol != kInvalidRuntimeObjectId &&
        booleanField(runtime, symbol, "writable", true) &&
        booleanField(runtime, symbol, "enumerable", true) &&
        booleanField(runtime, symbol, "configurable", true),
        "a typed Symbol own property returns ordinary data attributes");
    expect(runSameRealm(runtime,
            "var descriptorKeysOk=descriptorKeys.length===4&&"
                "descriptorKeys[0]==='value'&&"
                "descriptorKeys[1]==='writable'&&"
                "descriptorKeys[2]==='enumerable'&&"
                "descriptorKeys[3]==='configurable';")
            .succeeded() && booleanIs(runtime, "descriptorKeysOk"),
        "descriptor fields interoperate with Object.keys in insertion order");
}

void testUndefinedSnapshotsAndArrayLength()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var item={count:10,child:{value:1}};item.name=undefined;"
        "var first=Object.getOwnPropertyDescriptor(item,'count');"
        "var second=Object.getOwnPropertyDescriptor(item,'count');"
        "var nameDescriptor=Object.getOwnPropertyDescriptor(item,'name');"
        "var childDescriptor=Object.getOwnPropertyDescriptor(item,'child');"
        "var childArray=[1,2];var arrayOwner={child:childArray};"
        "var childArrayDescriptor=Object.getOwnPropertyDescriptor(arrayOwner,'child');"
        "var child=item.child;item.count=20;"
        "var mutable=Object.getOwnPropertyDescriptor(item,'count');"
        "mutable.value=77;"
        "var values=[10,,undefined];"
        "var firstIndex=Object.getOwnPropertyDescriptor(values,0);"
        "var hole=Object.getOwnPropertyDescriptor(values,'1');"
        "var explicit=Object.getOwnPropertyDescriptor(values,'2');"
        "var length=Object.getOwnPropertyDescriptor(values,'length');"
        "values.length=1;values.length=3;"
        "var snapshotOk=first.value===10&&second.value===10&&"
            "first!==second&&nameDescriptor!==undefined&&"
            "nameDescriptor.value===undefined&&mutable.value===77&&"
            "item.count===20&&childDescriptor.value===child&&"
            "childArrayDescriptor.value===childArray&&"
            "values.length===3&&values[1]===undefined&&"
            "!values.hasOwnProperty('1')&&firstIndex.value===10&&"
            "hole===undefined&&explicit!==undefined&&"
            "explicit.value===undefined&&length.value===3&&"
            "length.writable===true&&length.enumerable===false&&"
            "length.configurable===false;");
    const RuntimeObjectId first = objectId(runtime, "first");
    const RuntimeObjectId second = objectId(runtime, "second");
    const RuntimeObjectId childDescriptor = objectId(runtime,
        "childDescriptor");
    const RuntimeObjectId childArrayDescriptor = objectId(runtime,
        "childArrayDescriptor");
    const Value* child = binding(runtime, "child");
    Value childValue;
    Value childArrayValue;
    expect(result.succeeded() && booleanIs(runtime, "snapshotOk"),
        "missing, present undefined, sparse holes, occupied indices, and specialized length are distinguished");
    expect(first != second && first != kInvalidRuntimeObjectId &&
        second != kInvalidRuntimeObjectId &&
        numberField(runtime, first, "value", 10.0) &&
        numberField(runtime, second, "value", 10.0),
        "descriptor objects are independent snapshots after source reassignment");
    expect(child != nullptr && child->isObject() &&
        NavigatorScriptRuntimeTestAccess::readOwn(runtime, childDescriptor,
            "value", childValue) && childValue.isObject() &&
        childValue.objectId() == child->objectId(),
        "object-valued descriptor fields retain the original object identity");
    const Value* childArray = binding(runtime, "childArray");
    expect(childArray != nullptr && childArray->isObject() &&
        NavigatorScriptRuntimeTestAccess::readOwn(runtime,
            childArrayDescriptor, "value", childArrayValue) &&
        childArrayValue.isObject() &&
        childArrayValue.objectId() == childArray->objectId(),
        "nested Array values retain the original Array identity in descriptor snapshots");
    runtime.reset();
    const ScriptResult afterReset = run(runtime,
        "var item={x:1};var resetDescriptor="
            "Object.getOwnPropertyDescriptor(item,'x');"
        "var resetOk=resetDescriptor.value===1&&"
            "resetDescriptor.configurable===true;");
    expect(afterReset.succeeded() && booleanIs(runtime, "resetOk"),
        "runtime reset reconstructs Object descriptor registration and metadata defaults");
}

void testNativeAttributeClassesAndOwnBoundary()
{
    RuntimeContext runtime;
    const ScriptResult setup = run(runtime,
        "var item={hidden:2,locked:3};var uncertain={partial:4};"
        "var uncertainSymbol={};uncertainSymbol[Symbol.iterator]=5;"
        "var proto={inheritedName:42};var instance={};"
        "var values=[10,,30];"
        "var inherited=Object.getOwnPropertyDescriptor(values,'1');");
    const RuntimeObjectId item = objectId(runtime, "item");
    const RuntimeObjectId uncertain = objectId(runtime, "uncertain");
    const RuntimeObjectId uncertainSymbol = objectId(runtime,
        "uncertainSymbol");
    const RuntimeObjectId proto = objectId(runtime, "proto");
    const RuntimeObjectId instance = objectId(runtime, "instance");
    const bool classified = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setAttributes(runtime, item,
            "hidden", true, false, true) &&
        NavigatorScriptRuntimeTestAccess::setAttributes(runtime, item,
            "locked", false, true, false) &&
        NavigatorScriptRuntimeTestAccess::setPartialAttributes(runtime, uncertain,
            "partial") &&
        NavigatorScriptRuntimeTestAccess::setPartialSymbolAttributes(runtime,
            uncertainSymbol, WellKnownSymbol::Iterator) &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(runtime, "1",
            Value::number(99.0)) &&
        NavigatorScriptRuntimeTestAccess::linkPrototype(runtime, instance,
            proto);
    const ScriptResult check = classified ? runSameRealm(runtime,
        "var hiddenDescriptor=Object.getOwnPropertyDescriptor(item,'hidden');"
        "var lockedDescriptor=Object.getOwnPropertyDescriptor(item,'locked');"
        "var hiddenKeys=Object.keys(item);"
        "var lockedBefore=item.locked;item.locked=90;"
        "var attributesOk=hiddenDescriptor.value===2&&"
            "hiddenDescriptor.writable===true&&"
            "hiddenDescriptor.enumerable===false&&"
            "hiddenDescriptor.configurable===true&&"
            "lockedDescriptor.value===3&&"
            "lockedDescriptor.writable===false&&"
            "lockedDescriptor.enumerable===true&&"
            "lockedDescriptor.configurable===false&&item.locked===lockedBefore&&"
            "hiddenKeys.length===1&&hiddenKeys[0]==='locked'&&"
            "values[1]===99&&"
            "Object.getOwnPropertyDescriptor(values,'1')===undefined&&"
            "instance.inheritedName===42&&"
            "Object.getOwnPropertyDescriptor(instance,'inheritedName')===undefined;")
        : ScriptResult();
    expect(classified && check.succeeded() &&
        booleanIs(runtime, "attributesOk"),
        "nonwritable and nonenumerable metadata is reported without mutating source properties");
    expect(runSameRealm(runtime,
            "var unsupported=Object.getOwnPropertyDescriptor(uncertain,'partial');")
            .runtimeError.code == RuntimeErrorCode::UnsupportedFeature,
        "a present own property with unknown attributes fails closed instead of appearing absent");
    expect(runSameRealm(runtime,
            "Object.getOwnPropertyDescriptor(uncertainSymbol,Symbol.iterator);")
            .runtimeError.code == RuntimeErrorCode::UnsupportedFeature,
        "an incompletely classified typed Symbol property is rejected without string conversion");
    expect(runSameRealm(runtime,
            "Object.getOwnPropertyDescriptor(10,'value');")
            .runtimeError.code == RuntimeErrorCode::InvalidReceiver &&
        runSameRealm(runtime,
            "Object.getOwnPropertyDescriptor(item,Symbol);")
            .runtimeError.code == RuntimeErrorCode::InvalidPropertyKey,
        "invalid receivers and unsupported Symbol-key conversion fail deterministically");
}

void testArrayBoundariesAndCapacityFailures()
{
    RuntimeContext runtime;
    const ScriptResult setup = run(runtime,
        "var values=[10,20];var named=[];"
        "named['01']=1;named['4294967295']=2;");
    const ScriptResult check = setup.succeeded() ? runSameRealm(runtime,
        "var canonical=Object.getOwnPropertyDescriptor(values,'1');"
        "var namedLeading=Object.getOwnPropertyDescriptor(named,'01');"
        "var namedMax=Object.getOwnPropertyDescriptor(named,'4294967295');"
        "var beyond=Object.getOwnPropertyDescriptor(values,'1024');"
        "var arrayOk=canonical.value===20&&namedLeading.value===1&&"
            "namedMax.value===2&&beyond===undefined;") : ScriptResult();
    expect(check.succeeded() && booleanIs(runtime, "arrayOk"),
        "canonical Array indices and distinct noncanonical names use bounded storage correctly");

    for (std::size_t insertion = 0u; insertion < 4u; ++insertion) {
        RuntimeContext failing;
        const ScriptResult source = run(failing, "var item={x:1};");
        const std::size_t objects = failing.objectCount();
        const std::size_t properties = failing.propertyCount();
        NavigatorScriptRuntimeTestAccess::failPropertyInsertionAfter(failing,
            insertion);
        const ScriptResult failed = source.succeeded()
            ? runSameRealm(failing,
                "var failedDescriptor=Object.getOwnPropertyDescriptor(item,'x');")
            : ScriptResult();
        expect(source.succeeded() &&
            failed.runtimeError.code == RuntimeErrorCode::AllocationFailure &&
            failing.objectCount() == objects &&
            failing.propertyCount() == properties &&
            objectId(failing, "item") != kInvalidRuntimeObjectId,
            "descriptor field insertion failure rolls back unpublished objects and properties");
    }

    RuntimeContext objectLimited;
    const ScriptResult objectSetup = run(objectLimited,
        "var item={x:1};");
    const std::size_t objects = objectLimited.objectCount();
    NavigatorScriptRuntimeTestAccess::setObjectLimit(objectLimited, objects);
    const ScriptResult noObject = objectSetup.succeeded()
        ? runSameRealm(objectLimited,
            "Object.getOwnPropertyDescriptor(item,'x');") : ScriptResult();
    expect(noObject.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
        objectLimited.objectCount() == objects,
        "descriptor object allocation respects the existing object-pool limit");

    RuntimeContext propertyLimited;
    const ScriptResult propertySetup = run(propertyLimited,
        "var item={x:1};");
    const std::size_t properties = propertyLimited.propertyCount();
    NavigatorScriptRuntimeTestAccess::setPropertyLimit(propertyLimited,
        properties + 3u);
    const ScriptResult noProperties = propertySetup.succeeded()
        ? runSameRealm(propertyLimited,
            "Object.getOwnPropertyDescriptor(item,'x');") : ScriptResult();
    expect(noProperties.runtimeError.code ==
            RuntimeErrorCode::PropertyLimitExceeded &&
        propertyLimited.propertyCount() == properties,
        "descriptor construction preflights and respects total-property capacity");

    RuntimeContext stringLimited;
    const ScriptResult stringSetup = run(stringLimited,
        "var item={x:1};var key='x';");
    const std::size_t strings =
        NavigatorScriptRuntimeTestAccess::stringValueCount(stringLimited);
    NavigatorScriptRuntimeTestAccess::setStringValueLimit(stringLimited,
        strings);
    const ScriptResult noStrings = stringSetup.succeeded()
        ? runSameRealm(stringLimited,
            "var retained=Object.getOwnPropertyDescriptor(item,key);")
        : ScriptResult();
    expect(noStrings.succeeded() &&
        objectId(stringLimited, "retained") != kInvalidRuntimeObjectId &&
        NavigatorScriptRuntimeTestAccess::stringValueCount(stringLimited) ==
            strings,
        "descriptor creation preserves existing string Values without consuming runtime-string slots");
}

std::uint64_t medianDescriptorMicros(const char* setupSource,
    const char* expression, bool makeNonenumerable = false)
{
    std::vector<std::uint64_t> samples;
    samples.reserve(5u);
    for (std::size_t sample = 0u; sample < 5u; ++sample) {
        RuntimeContext runtime;
        if (!run(runtime, setupSource).succeeded()) return 0u;
        if (makeNonenumerable &&
            !NavigatorScriptRuntimeTestAccess::setAttributes(runtime,
                objectId(runtime, "item"), "x", true, false, true)) {
            return 0u;
        }
        const std::string source = std::string("var i=0;var last;for(i=0;i<64;i++){") +
            "last=" + expression + ";}";
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = runtime.executeInSameRealm(
            SourceView(source.data(), source.size()));
        const auto finish = std::chrono::steady_clock::now();
        if (!result.succeeded()) return 0u;
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                finish - start).count()));
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2u];
}

void testCapacityAndPerformance()
{
    const std::uint64_t ordinaryRead = medianDescriptorMicros(
        "var item={x:1};var key='x';", "item.x");
    const std::uint64_t present = medianDescriptorMicros(
        "var item={x:1};var key='x';",
        "Object.getOwnPropertyDescriptor(item,key)");
    const std::uint64_t ownPresence = medianDescriptorMicros(
        "var item={x:1};var key='x';", "item.hasOwnProperty(key)");
    const std::uint64_t missing = medianDescriptorMicros(
        "var item={x:1};var key='missing';",
        "Object.getOwnPropertyDescriptor(item,key)");
    const std::uint64_t hidden = medianDescriptorMicros(
        "var item={x:1};var key='x';",
        "Object.getOwnPropertyDescriptor(item,key)", true);
    const std::uint64_t symbol = medianDescriptorMicros(
        "var item={};var key=Symbol.iterator;item[key]=1;",
        "Object.getOwnPropertyDescriptor(item,key)");
    const std::uint64_t numericKey = medianDescriptorMicros(
        "var item={'1':1};", "Object.getOwnPropertyDescriptor(item,1)");
    const std::uint64_t denseIndex = medianDescriptorMicros(
        "var item=[1,2,3];var key='1';",
        "Object.getOwnPropertyDescriptor(item,key)");
    const std::uint64_t sparseHole = medianDescriptorMicros(
        "var item=[1,,3];var key='1';",
        "Object.getOwnPropertyDescriptor(item,key)");
    const std::uint64_t arrayLength = medianDescriptorMicros(
        "var item=[1,2,3];var key='length';",
        "Object.getOwnPropertyDescriptor(item,key)");
    expect(ordinaryRead != 0u && ownPresence != 0u && present != 0u &&
        missing != 0u && hidden != 0u && symbol != 0u &&
        numericKey != 0u && denseIndex != 0u && sparseHole != 0u &&
        arrayLength != 0u,
        "five-sample property and descriptor performance workloads complete");
    std::cout << "JS91 five-sample median microseconds (64 operations; parse/evaluation included)\n"
        << "ordinary-property-read=" << ordinaryRead
        << ",own-property-presence=" << ownPresence
        << ",present-descriptor=" << present
        << ",missing-descriptor=" << missing
        << ",nonenumerable-descriptor=" << hidden
        << ",typed-symbol-descriptor=" << symbol
        << ",numeric-key-descriptor=" << numericKey
        << ",dense-index-descriptor=" << denseIndex
        << ",sparse-hole-lookup=" << sparseHole
        << ",array-length-descriptor=" << arrayLength << '\n';

    RuntimeContext direct;
    std::vector<RuntimeObjectId> allocated;
    allocated.reserve(64u);
    const std::size_t directInitialProperties = direct.propertyCount();
    const auto allocationStart = std::chrono::steady_clock::now();
    bool directOperationsSucceeded = true;
    for (std::size_t index = 0u; index < 64u; ++index) {
        RuntimeObjectId id = kInvalidRuntimeObjectId;
        directOperationsSucceeded =
            directOperationsSucceeded &&
            NavigatorScriptRuntimeTestAccess::createEmptyObject(direct, id);
        allocated.push_back(id);
    }
    const auto allocationFinish = std::chrono::steady_clock::now();
    const auto insertionStart = std::chrono::steady_clock::now();
    const char* fieldKeys[] = {"value", "writable", "enumerable",
        "configurable"};
    for (const RuntimeObjectId id : allocated) {
        for (const char* key : fieldKeys) {
            directOperationsSucceeded =
                directOperationsSucceeded &&
                NavigatorScriptRuntimeTestAccess::insertOrdinaryField(direct,
                    id, key, Value::undefined());
        }
    }
    const auto insertionFinish = std::chrono::steady_clock::now();
    const std::uint64_t objectAllocationNanoseconds =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                allocationFinish - allocationStart).count() / 64);
    const std::uint64_t fieldInsertionNanoseconds =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                insertionFinish - insertionStart).count() / 256);
    volatile std::size_t attributeValidationSink = 0u;
    const auto attributeStart = std::chrono::steady_clock::now();
    for (std::size_t index = 0u; index < 100000u; ++index) {
        if (NavigatorScriptRuntimeTestAccess::validFullyKnownAttributes())
            ++attributeValidationSink;
    }
    const auto attributeFinish = std::chrono::steady_clock::now();
    const std::uint64_t attributeValidationNanoseconds =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                attributeFinish - attributeStart).count() / 100000);
    expect(directOperationsSucceeded && allocated.size() == 64u &&
        direct.propertyCount() == directInitialProperties + 256u &&
        attributeValidationSink == 100000u,
        "isolated bounded object allocation, field insertion, and attribute-mask workloads complete");
    std::cout << "JS91 isolated medians (native test harness): object-allocation="
        << objectAllocationNanoseconds << " ns/object; descriptor-field-insertion="
        << fieldInsertionNanoseconds << " ns/field; attribute-mask-validation="
        << attributeValidationNanoseconds << " ns/classification; scratch-bytes=0\n";

    RuntimeContext maximum;
    const ScriptResult setup = run(maximum,
        "var item={x:1};var key='x';");
    const std::size_t initialObjects = maximum.objectCount();
    const std::size_t initialProperties = maximum.propertyCount();
    const std::size_t initialStrings =
        NavigatorScriptRuntimeTestAccess::stringValueCount(maximum);
    std::size_t successfulDescriptors = 0u;
    RuntimeErrorCode terminalError = RuntimeErrorCode::None;
    if (setup.succeeded()) {
        while (successfulDescriptors < 1100u) {
            const ScriptResult result = runSameRealm(maximum,
                "Object.getOwnPropertyDescriptor(item,key);");
            if (!result.succeeded()) {
                terminalError = result.runtimeError.code;
                break;
            }
            ++successfulDescriptors;
        }
    }
    const std::size_t addedObjects = maximum.objectCount() - initialObjects;
    const std::size_t addedProperties = maximum.propertyCount() -
        initialProperties;
    const std::size_t finalStrings =
        NavigatorScriptRuntimeTestAccess::stringValueCount(maximum);
    expect(setup.succeeded() && successfulDescriptors > 0u &&
        successfulDescriptors < 1100u &&
        addedObjects == successfulDescriptors &&
        addedProperties == successfulDescriptors * 4u &&
        finalStrings == initialStrings &&
        (terminalError == RuntimeErrorCode::ObjectLimitExceeded ||
            terminalError == RuntimeErrorCode::PropertyLimitExceeded),
        "maximum repeated descriptor workload stops at existing bounded pools with exact live accounting");
    std::cout << "JS91 capacity: successfulDescriptors="
        << successfulDescriptors << ", objects=" << initialObjects << "+"
        << addedObjects << '/' << maximum.limits().maxObjects
        << ", properties=" << initialProperties << "+"
        << addedProperties << '/' << maximum.limits().maxTotalProperties
        << ", strings=" << initialStrings << "->" << finalStrings
        << ", terminal=" << runtimeErrorCodeName(terminalError) << '\n';
}

void testHostedFixtureAndEvents()
{
    std::ifstream file("navigator-smoke/javascript-js91.html", std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js91-native.html", html,
        loadError) && loadError == RuntimeErrorCode::None;
    expect(loaded, "JS91 hosted fixture parses through the supported HTML subset");
    if (!loaded) return;
    const std::string& source = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    expect(harness.document().scriptSources.size() == 1u &&
        source.find('<') == std::string::npos,
        "hosted JS91 inline source preserves the HTML-subset integrity guard");
    const ScriptResult result = harness.execute(source);
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    expect(result.succeeded() && markerText.find("JS91:assertions=") == 0u &&
        markerText.find(":failures=0:") != std::string::npos,
        "hosted JS91 descriptor assertions pass in the native Navigator harness");
    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    const bool clicked = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, clickError);
    expect(clicked && clickError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "descriptor reads inside nested callbacks preserve Event and Array state");
    expect(runSameRealm(harness.runtime(),
            "Object.getOwnPropertyDescriptor(savedEvent,'target');")
            .runtimeError.code == RuntimeErrorCode::UnsupportedFeature,
        "partially classified host Event properties reject descriptors without exposing internals");
    expect(runSameRealm(harness.runtime(),
            "Object.getOwnPropertyDescriptor(document,'body');")
            .runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "host wrappers remain outside the ordinary descriptor receiver boundary");
    RuntimeErrorCode generationError = RuntimeErrorCode::None;
    const bool invalidated =
        harness.invalidateDocumentGeneration(generationError);
    const ScriptResult retainedIdentity = runSameRealm(harness.runtime(),
        "var retainedElementIdentity=pairSecond.value===savedElement;");
    const ScriptResult staleElementRead = runSameRealm(harness.runtime(),
        "pairSecond.value.id;");
    expect(invalidated && generationError == RuntimeErrorCode::None &&
        retainedIdentity.succeeded() &&
        booleanIs(harness.runtime(), "retainedElementIdentity") &&
        staleElementRead.runtimeError.code == RuntimeErrorCode::StaleHostObject,
        "descriptor-held Element references retain identity while stale handles remain generation-protected");
}

} // namespace

int main()
{
    testOrdinaryAndKeySemantics();
    testUndefinedSnapshotsAndArrayLength();
    testNativeAttributeClassesAndOwnBoundary();
    testArrayBoundariesAndCapacityFailures();
    testCapacityAndPerformance();
    testHostedFixtureAndEvents();

    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS91 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS91 checks: " << checks << '/'
        << checks << " passed\n";
    std::cout << "JS91 storage: RuntimeObject="
        << NavigatorScriptRuntimeTestAccess::objectBytes()
        << " B; RuntimeProperty="
        << NavigatorScriptRuntimeTestAccess::propertyBytes()
        << " B; ArrayElement="
        << NavigatorScriptRuntimeTestAccess::arrayElementBytes()
        << " B; AttributeMask="
        << NavigatorScriptRuntimeTestAccess::attributeBytes() << " B\n";
    return 0;
}
