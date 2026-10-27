#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

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

    static std::size_t arrayLength(const RuntimeContext& runtime,
        RuntimeObjectId id)
    {
        const Object* object = runtime.objectAt(id);
        return object != nullptr && object->array ? object->arrayLength : 0u;
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
        const Object* object = runtime.objectAt(runtime.arrayPrototype_);
        if (object == nullptr) return false;
        for (const RuntimeContext::RuntimeProperty& property :
            object->properties) {
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

    static std::size_t runtimeObjectCount(const RuntimeContext& runtime)
    {
        return runtime.objects_.size();
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

RuntimeObjectId objectId(const RuntimeContext& runtime, const char* name)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isObject()
        ? value->objectId() : kInvalidRuntimeObjectId;
}

ScriptResult run(RuntimeContext& runtime, const std::string& source)
{
    return runtime.execute(SourceView(source.data(), source.size()));
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

void testBasicBehaviorAndIdentity()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var empty=[];var emptyCalls=0;var emptyResult=empty.map(function(){"
        "emptyCalls=emptyCalls+1;return 9;});"
        "var one=[8];var context={factor:3};var oneArgs=false;"
        "var oneResult=one.map(function(value,index,array){"
        "oneArgs=this===context&&value===8&&index===0&&array===one;"
        "return value*this.factor+index;},context);"
        "var source=[2,4,6];var calls=0;var indices='';"
        "var mapped=source.map(function(value,index,array){calls=calls+1;"
        "indices=indices+index;return value*3;});"
        "mapped[0]=99;var independence=source[0]===2&&mapped!==source;"
        "var words=['a','b'];var wordResult=words.map(function(value,index){"
        "return value+index;});"
        "var objectValue={tag:7};var nested=[4,5];"
        "var references=[objectValue,nested];"
        "var referenceResult=references.map(function(value){return value;});"
        "var explicitUndefined=[undefined,2].map(function(value,index){"
        "if(index===0)return undefined;return value;});"
        "var identityOk=referenceResult[0]===objectValue&&"
        "referenceResult[1]===nested&&referenceResult[1]!==references;"
        "var mappedOk=emptyResult!==empty&&emptyResult.length===0&&"
        "oneResult.length===1&&oneResult[0]===24&&oneArgs&&"
        "mapped.length===3&&mapped[0]===99&&mapped[1]===12&&mapped[2]===18&&"
        "source[0]===2&&source[1]===4&&source[2]===6&&indices==='012'&&"
        "wordResult[0]==='a0'&&wordResult[1]==='b1'&&identityOk&&"
        "explicitUndefined[0]===undefined&&explicitUndefined[1]===2;");
    const Value* emptyCalls = binding(runtime, "emptyCalls");
    expect(result.succeeded() && booleanIs(runtime, "mappedOk") &&
        emptyCalls != nullptr && emptyCalls->isNumber() &&
        emptyCalls->numberValue() == 0.0,
        "empty, one-element, dense numeric and string mappings preserve distinct result identity and source independence");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime, "map"),
        "map is installed as a writable-only native Array.prototype method");
    expect(NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "explicitUndefined"), 0u) &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "explicitUndefined"), 1u),
        "explicit undefined callback results are stored as present own properties");

    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    expect(runtime.readPropertyForTesting(runtime.arrayPrototypeId(), "map",
            method, methodError) && method.isFunction() &&
        methodError == RuntimeErrorCode::None,
        "Array.prototype exposes map as a genuine callable runtime function");
}

void testSparseAndInheritedProperties()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var sparse=[];sparse.length=5;sparse[2]=7;sparse[4]=undefined;"
        "var calls=0;var indices='';var mapped=sparse.map(function(value,index){"
        "calls=calls+1;indices=indices+index;"
        "if(index===4)return value;return value*2;});"
        "var allHoles=[];allHoles.length=4;var holeCalls=0;"
        "var holeResult=allHoles.map(function(){holeCalls=holeCalls+1;});"
        "var leading=[,,5].map(function(value){return value+1;});"
        "var trailing=[9,,].map(function(value){return value+1;});"
        "var sparseOk=mapped.length===5&&mapped[2]===14&&"
        "mapped[4]===undefined&&holeResult.length===4&&"
        "leading.length===3&&leading[2]===6&&trailing.length===2&&"
        "trailing[0]===10&&calls===2&&indices==='24'&&holeCalls===0;");
    expect(result.succeeded() && booleanIs(runtime, "sparseOk"),
        "leading, middle, trailing, and all-hole positions retain holes while explicit undefined is visited");
    expect(!NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "mapped"), 0u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "mapped"), 1u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "mapped"), 3u) &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "mapped"), 4u),
        "result occupancy distinguishes holes from a mapped explicit undefined");
    expect(NavigatorScriptRuntimeTestAccess::arrayLength(runtime,
            objectId(runtime, "holeResult")) == 4u &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "holeResult"), 0u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "holeResult"), 3u),
        "an all-hole mapped Array preserves its captured logical length without materializing slots");

    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[10,,30];var calls=0;var inheritedResult;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "1", Value::number(20.0));
    const ScriptResult inheritedResult = installed
        ? runSameRealm(inherited,
            "inheritedResult=source.map(function(value){calls=calls+1;"
            "return value*2;});") : ScriptResult();
    expect(inheritedResult.succeeded() && numberIs(inherited, "calls", 3.0) &&
        NavigatorScriptRuntimeTestAccess::arrayLength(inherited,
            objectId(inherited, "inheritedResult")) == 3u,
        "a supported inherited numeric index participates in the live traversal");
    expect(NavigatorScriptRuntimeTestAccess::hasOwnIndex(inherited,
            objectId(inherited, "inheritedResult"), 1u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(inherited,
            objectId(inherited, "source"), 1u),
        "an inherited indexed value is materialized as an own mapped result slot");
    Value inheritedMapped;
    RuntimeErrorCode inheritedReadError = RuntimeErrorCode::None;
    expect(inherited.readPropertyForTesting(objectId(inherited,
            "inheritedResult"), "1", inheritedMapped,
            inheritedReadError) && inheritedMapped.isNumber() &&
        inheritedMapped.numberValue() == 40.0,
        "the inherited numeric value is passed to the callback and mapped correctly");
}

void testLiveMutationAndNestedMap()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=[1,2,3];var replaceResult=replaced.map(function(v,i,a){"
        "if(i===0)a[1]=9;return v*10;});"
        "var deleted=[1,2,3];var deleteIndices='';"
        "var deleteResult=deleted.map(function(v,i,a){"
        "deleteIndices=deleteIndices+i;"
        "if(i===0){a.length=1;a.length=3;a[2]=30;}return v;});"
        "var filled=[1,,3];var fillResult=filled.map(function(v,i,a){"
        "if(i===0)a[1]=2;return v*5;});"
        "var appended=[4,5];var appendResult=appended.map(function(v,i,a){"
        "if(i===0)a[2]=6;return v;});"
        "var shrunk=[7,8,9];var shrinkCalls=0;"
        "var shrinkResult=shrunk.map(function(v,i,a){shrinkCalls=shrinkCalls+1;"
        "if(i===0)a.length=1;return v;});"
        "var other=[2,3];var another=[1];var otherResult=another.map(function(v){"
        "other[1]=12;return v+other[1];});"
        "var innerA=[1,2];var innerB=[3];var outer=[innerA,innerB];"
        "var nested=outer.map(function(items){return items.map(function(v){"
        "return v*2;});});"
        "var mutationsOk=replaceResult[1]===90&&deleteIndices==='02'&&"
        "deleteResult.length===3&&deleteResult[0]===1&&"
        "fillResult[1]===10&&fillResult.length===3&&"
        "appendResult.length===2&&appendResult[1]===5&&appended[2]===6&&"
        "shrinkCalls===1&&shrinkResult.length===3&&"
        "otherResult[0]===13&&other[1]===12&&"
        "nested[0][0]===2&&nested[0][1]===4&&nested[1][0]===6&&"
        "nested[0]!==innerA&&nested[1]!==innerB;");
    expect(result.succeeded() && booleanIs(runtime, "mutationsOk"),
        "map observes live replacement, deletion, hole filling, another Array mutation, and nested traversals");
    expect(!NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "deleteResult"), 1u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "appendResult"), 2u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "shrinkResult"), 1u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "shrinkResult"), 2u),
        "deleted or shrunken future source positions remain holes in the result");

    RuntimeContext callbackError;
    const ScriptResult errorResult = run(callbackError,
        "var source=[1,2,3];var calls=0;var result;"
        "source.map(function(value,index,array){calls=calls+1;"
        "if(index===0)array[1]=99;if(index===1)null.value;return value;});");
    Value mutated;
    RuntimeErrorCode readError = RuntimeErrorCode::None;
    const bool mutationRetained = callbackError.readPropertyForTesting(
        objectId(callbackError, "source"), "1", mutated, readError);
    expect(errorResult.status == ScriptStatus::RuntimeFailure &&
        errorResult.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        numberIs(callbackError, "calls", 2.0) && mutationRetained &&
        mutated.isNumber() && mutated.numberValue() == 99.0 &&
        binding(callbackError, "result") != nullptr &&
        binding(callbackError, "result")->isUndefined(),
        "callback errors stop traversal, preserve earlier source mutations, and publish no result");
}

void testErrorsBoundsAndAllocation()
{
    const char* invalidCallbacks[] = {
        "var values=[];values.map();",
        "var values=[];values.map(undefined);",
        "var values=[];values.map(null);",
        "var values=[];values.map('callback');",
        "var values=[];values.map({});",
    };
    for (const char* source : invalidCallbacks) {
        RuntimeContext runtime;
        const ScriptResult result = run(runtime, source);
        expect(result.status == ScriptStatus::RuntimeFailure &&
            result.runtimeError.code == RuntimeErrorCode::NotCallable,
            "missing and non-callable callbacks fail before empty traversal");
    }

    RuntimeContext unsupportedReceiver;
    const ScriptResult setup = run(unsupportedReceiver, "var receiver={};");
    const bool linked = setup.succeeded() &&
        unsupportedReceiver.setPrototypeForTesting(objectId(
            unsupportedReceiver, "receiver"),
            unsupportedReceiver.arrayPrototypeId());
    const ScriptResult receiverResult = linked
        ? runSameRealm(unsupportedReceiver, "receiver.map(function(){});")
        : ScriptResult();
    expect(receiverResult.status == ScriptStatus::RuntimeFailure &&
        receiverResult.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "an ordinary object inheriting map is rejected as a receiver");

    RuntimeContext failureInjection;
    const ScriptResult failureSetup = run(failureInjection,
        "var source=[1,2,3];var calls=0;var result;");
    const std::size_t beforeObjects =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(failureInjection);
    const std::size_t beforeSlots = failureInjection.arrayElementCount();
    NavigatorScriptRuntimeTestAccess::failNextArrayCreation(failureInjection);
    const ScriptResult allocationFailure = failureSetup.succeeded()
        ? runSameRealm(failureInjection,
            "result=source.map(function(value){calls=calls+1;return value;});")
        : ScriptResult();
    expect(allocationFailure.status == ScriptStatus::RuntimeFailure &&
        allocationFailure.runtimeError.code == RuntimeErrorCode::AllocationFailure &&
        numberIs(failureInjection, "calls", 3.0),
        "destination allocation failure propagates after completed callbacks");
    expect(NavigatorScriptRuntimeTestAccess::runtimeObjectCount(failureInjection) ==
            beforeObjects &&
        failureInjection.arrayElementCount() == beforeSlots &&
        binding(failureInjection, "result") != nullptr &&
        binding(failureInjection, "result")->isUndefined(),
        "failed destination construction retains no unpublished object or Array slots");

    RuntimeLimits capacityLimits;
    capacityLimits.maxTotalArrayElements = 2u;
    RuntimeContext capacityFailure(capacityLimits);
    const ScriptResult capacitySetup = run(capacityFailure,
        "var source=[1,2];var calls=0;var result;");
    const std::size_t capacityObjects =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(capacityFailure);
    const ScriptResult capacityResult = capacitySetup.succeeded()
        ? runSameRealm(capacityFailure,
            "result=source.map(function(value){calls=calls+1;return value;});")
        : ScriptResult();
    expect(capacityResult.status == ScriptStatus::RuntimeFailure &&
        capacityResult.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        numberIs(capacityFailure, "calls", 2.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(capacityFailure) ==
            capacityObjects && capacityFailure.arrayElementCount() == 2u,
        "total Array-slot exhaustion is bounded, deterministic, and leaves no partial result");

    RuntimeContext boundary;
    const ScriptResult empty = run(boundary,
        "var source=[];source.length=0;var result=source.map(function(){"
        "return 1;});var boundaryOk=result!==source&&result.length===0;");
    expect(empty.succeeded() && booleanIs(boundary, "boundaryOk"),
        "zero-position input produces a distinct zero-length Array");

    RuntimeContext overflow;
    const ScriptResult overflowResult = run(overflow,
        "var source=[];source.length=1025;");
    expect(overflowResult.status == ScriptStatus::RuntimeFailure &&
        overflowResult.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded,
        "the existing 1,025-position boundary is rejected deterministically");

    RuntimeContext sparseMaximum;
    const ScriptResult sparseMaximumResult = run(sparseMaximum,
        "var source=[];source.length=1024;var calls=0;"
        "var result=source.map(function(){calls=calls+1;});"
        "var sparseMaximumOk=result.length===1024&&calls===0;");
    expect(sparseMaximumResult.succeeded() &&
        booleanIs(sparseMaximum, "sparseMaximumOk") &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(sparseMaximum,
            objectId(sparseMaximum, "result"), 1023u),
        "a maximum-length sparse source maps without materializing holes");

    RuntimeContext denseMaximum;
    std::string denseSource = "var source=[";
    for (std::size_t index = 0u; index < 1024u; ++index) {
        if (index != 0u) denseSource.push_back(',');
        denseSource += std::to_string(index);
    }
    denseSource += "];var calls=0;var result=source.map(function(value,index,array){"
        "calls=calls+1;return value+1;});";
    const ScriptResult denseResult = run(denseMaximum, denseSource);
    Value first;
    Value last;
    RuntimeErrorCode firstError = RuntimeErrorCode::None;
    RuntimeErrorCode lastError = RuntimeErrorCode::None;
    const bool endpoints = denseMaximum.readPropertyForTesting(
        objectId(denseMaximum, "result"), "0", first, firstError) &&
        denseMaximum.readPropertyForTesting(objectId(denseMaximum, "result"),
            "1023", last, lastError);
    expect(denseResult.succeeded() && numberIs(denseMaximum, "calls", 1024.0) &&
        endpoints && first.isNumber() && first.numberValue() == 1.0 &&
        last.isNumber() && last.numberValue() == 1024.0 &&
        denseMaximum.arrayElementCount() == 2048u,
        "a fully populated 1,024-position source maps completely within the existing slot limit");
}

void testHostedFixtureAndEvents()
{
    std::ifstream input("navigator-smoke/javascript-js94.html",
        std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js94-map.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult result = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    expect(loaded && loadError == RuntimeErrorCode::None && result.succeeded() &&
        markerText.find("JS94:assertions=14:failures=0") == 0u,
        "hosted JS94 fixture passes its mapping, sparse, mutation, and Event assertions");
    expect(markerText.find(":basic=true:sparse=true:mutation=true:nested=true") !=
            std::string::npos,
        "hosted JS94 marker records distinct results, holes, live mutation, and nested map");

    HostInstanceId event = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "map-event-a") event = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = event != 0u && harness.dispatchClick(event, eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Events run map callbacks while preserving both Event contexts");
}

} // namespace

int main()
{
    testBasicBehaviorAndIdentity();
    testSparseAndInheritedProperties();
    testLiveMutationAndNestedMap();
    testErrorsBoundsAndAllocation();
    testHostedFixtureAndEvents();

    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS94 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS94 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
