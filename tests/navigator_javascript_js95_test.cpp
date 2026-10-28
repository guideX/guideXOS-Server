#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

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

void testBasicBehaviorIdentityAndThisArg()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var empty=[];var emptyCalls=0;var emptyResult=empty.filter(function(){"
        "emptyCalls=emptyCalls+1;return true;});"
        "var one=[8];var context={limit:8};var thisOk=false;"
        "var oneResult=one.filter(function(value,index,array){"
        "thisOk=this===context&&value===8&&index===0&&array===one;"
        "return true;},context);"
        "var rejected=[9].filter(function(){return false;});"
        "var source=[5,2,9,4,7,6];var calls=0;var indices='';"
        "var selected=source.filter(function(value,index,array){calls=calls+1;"
        "indices=indices+index;return value%2===0;});"
        "selected[0]=99;var sourceIndependent=source[1]===2&&"
        "selected!==source;"
        "var all=[3,1].filter(function(){return true;});"
        "var none=[3,1].filter(function(){return false;});"
        "var objectValue={tag:7};var nested=[4,5];"
        "var references=[objectValue,nested];"
        "var referenceResult=references.filter(function(){return true;});"
        "var strings=['a','b'];var stringResult=strings.filter(function(v){"
        "return v==='b';});"
        "var basicOk=emptyResult!==empty&&emptyResult.length===0&&"
        "oneResult.length===1&&oneResult[0]===8&&thisOk&&"
        "rejected.length===0&&selected.length===3&&selected[0]===99&&"
        "selected[1]===4&&selected[2]===6&&indices==='012345'&&calls===6&&"
        "sourceIndependent&&all.length===2&&all[0]===3&&all[1]===1&&"
        "none.length===0&&referenceResult!==references&&"
        "referenceResult[0]===objectValue&&referenceResult[1]===nested&&"
        "stringResult.length===1&&stringResult[0]==='b';");
    expect(result.succeeded() && booleanIs(runtime, "basicOk") &&
        numberIs(runtime, "emptyCalls", 0.0),
        "empty, single, all/none, ordered selection, callback arguments, thisArg, references, and result independence");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime,
            "filter"),
        "filter is installed as a writable-only native Array.prototype method");
    expect(NavigatorScriptRuntimeTestAccess::arrayLength(runtime,
            objectId(runtime, "selected")) == 3u &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "selected"), 0u) &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "selected"), 1u) &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "selected"), 2u),
        "filtered output is dense and has exactly the selected logical length");

    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    expect(runtime.readPropertyForTesting(runtime.arrayPrototypeId(), "filter",
            method, methodError) && method.isFunction() &&
        methodError == RuntimeErrorCode::None,
        "Array.prototype exposes filter as a genuine callable runtime function");
}

void testTruthiness()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var objectValue={tag:1};var arrayValue=[7];"
        "var inputs=[true,false,0,4,'','x',null,undefined,objectValue,arrayValue];"
        "var calls=0;var indices='';var kept=inputs.filter(function(value,index){"
        "calls=calls+1;indices=indices+index;return value;});"
        "var truthyOk=kept.length===5&&kept[0]===true&&kept[1]===4&&"
        "kept[2]==='x'&&kept[3]===objectValue&&kept[4]===arrayValue&&"
        "calls===10&&indices==='0123456789';");
    expect(result.succeeded() && booleanIs(runtime, "truthyOk"),
        "callback results use runtime truthiness for booleans, numbers, strings, null, undefined, objects, and Arrays");
    expect(NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "kept"), 0u) &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "kept"), 4u) &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "inputs"), 7u),
        "truthy reference values stay present and explicit undefined source elements are visited");
}

void testSparseAndInheritedProperties()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var sparse=[];sparse.length=6;sparse[1]=10;sparse[4]=undefined;"
        "var calls=0;var indices='';var dense=sparse.filter(function(v,i){"
        "calls=calls+1;indices=indices+i;return true;});"
        "var holes=[];holes.length=4;var holeCalls=0;"
        "var holeResult=holes.filter(function(){holeCalls=holeCalls+1;"
        "return true;});"
        "var edge=[,5,,];var edgeResult=edge.filter(function(){return true;});"
        "var sparseOk=dense.length===2&&dense[0]===10&&"
        "dense[1]===undefined&&calls===2&&indices==='14'&&"
        "holeResult.length===0&&holeCalls===0&&edgeResult.length===1&&"
        "edgeResult[0]===5;");
    expect(result.succeeded() && booleanIs(runtime, "sparseOk"),
        "leading, middle, trailing, and all holes are skipped while explicit undefined is visited");
    expect(NavigatorScriptRuntimeTestAccess::arrayLength(runtime,
            objectId(runtime, "dense")) == 2u &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "dense"), 0u) &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "dense"), 1u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "dense"), 2u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "sparse"), 0u),
        "sparse source holes compact into dense own result indices");

    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[10,,30];var calls=0;var result;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "1", Value::number(20.0));
    const ScriptResult inheritedResult = installed
        ? runSameRealm(inherited,
            "result=source.filter(function(value,index,array){"
            "calls=calls+1;return true;});") : ScriptResult();
    Value inheritedValue;
    RuntimeErrorCode inheritedError = RuntimeErrorCode::None;
    const bool readInherited = inherited.readPropertyForTesting(
        objectId(inherited, "result"), "1", inheritedValue, inheritedError);
    expect(inheritedResult.succeeded() && numberIs(inherited, "calls", 3.0) &&
        NavigatorScriptRuntimeTestAccess::arrayLength(inherited,
            objectId(inherited, "result")) == 3u && readInherited &&
        inheritedValue.isNumber() && inheritedValue.numberValue() == 20.0,
        "inherited numeric properties are visited and copied into dense own result slots");
    expect(NavigatorScriptRuntimeTestAccess::hasOwnIndex(inherited,
            objectId(inherited, "result"), 1u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(inherited,
            objectId(inherited, "source"), 1u),
        "inherited source values become own properties of the filtered result");
}

void testLiveMutationAndNestedCalls()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=[1,2,3];var replaceResult=replaced.filter(function(v,i,a){"
        "if(i===0)a[1]=9;return true;});"
        "var removed=[1,2,3];var removeIndices='';"
        "var removeResult=removed.filter(function(v,i,a){removeIndices=removeIndices+i;"
        "if(i===0){a.length=1;a.length=3;a[2]=30;}return true;});"
        "var filled=[1,,3];var fillResult=filled.filter(function(v,i,a){"
        "if(i===0)a[1]=2;return true;});"
        "var appended=[4,5];var appendResult=appended.filter(function(v,i,a){"
        "if(i===0)a[2]=6;return true;});"
        "var shrunk=[7,8,9];var shrinkCalls=0;"
        "var shrinkResult=shrunk.filter(function(v,i,a){shrinkCalls=shrinkCalls+1;"
        "if(i===0)a.length=1;return true;});"
        "var current=[5];var currentResult=current.filter(function(v,i,a){"
        "a[0]=99;return true;});"
        "var other=[2,3];var otherSource=[1];"
        "var otherResult=otherSource.filter(function(v){other[1]=12;return true;});"
        "var innerA=[1,2];var innerB=[3];var outer=[innerA,innerB];"
        "var nested=outer.filter(function(items,index){"
        "var picked=items.filter(function(v){return v>1;});"
        "var mapped=items.map(function(v){return v*2;});"
        "var visited=0;mapped.forEach(function(){visited=visited+1;});"
        "if(index===0)return picked.length===1&&mapped.length===2&&visited===2;"
        "return true;});"
        "var replacementOk=replaceResult.length===3&&replaceResult[1]===9;"
        "var removalOk=removeIndices==='02'&&removeResult.length===2&&"
        "removeResult[0]===1&&removeResult[1]===30;"
        "var fillAppendOk=fillResult.length===3&&fillResult[1]===2&&"
        "appendResult.length===2&&appendResult[1]===5&&appended[2]===6;"
        "var shrinkOk=shrinkCalls===1&&shrinkResult.length===1&&"
        "shrinkResult[0]===7;"
        "var currentOk=currentResult[0]===5&&current[0]===99;"
        "var otherOk=otherResult.length===1&&other[1]===12;"
        "var nestedOk=nested.length===2&&nested[0]===innerA&&nested[1]===innerB;"
        "var mutationsOk=replaceResult.length===3&&replaceResult[1]===9&&"
        "removeIndices==='02'&&removeResult.length===2&&removeResult[0]===1&&"
        "removeResult[1]===30&&fillResult.length===3&&fillResult[1]===2&&"
        "appendResult.length===2&&appendResult[1]===5&&appended[2]===6&&"
        "shrinkCalls===1&&shrinkResult.length===1&&shrinkResult[0]===7&&"
        "currentResult[0]===5&&current[0]===99&&otherResult.length===1&&"
        "other[1]===12&&nested.length===2&&nested[0]===innerA&&"
        "nested[1]===innerB;");
    expect(result.succeeded() && booleanIs(runtime, "mutationsOk"),
        "live future replacement, removal, insertion, append bounds, shrink, current-value retention, unrelated mutation, and nested filter/map/forEach");
    expect(booleanIs(runtime, "replacementOk") && booleanIs(runtime, "removalOk") &&
        booleanIs(runtime, "fillAppendOk") && booleanIs(runtime, "shrinkOk") &&
        booleanIs(runtime, "currentOk") && booleanIs(runtime, "otherOk") &&
        booleanIs(runtime, "nestedOk"),
        "mutation subcases retain their expected source and nested results");
    expect(NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "removeResult"), 1u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "removeResult"), 2u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "appendResult"), 2u),
        "removed and beyond-captured-length source slots do not create result holes or extra slots");

    RuntimeContext callbackError;
    const ScriptResult errorResult = run(callbackError,
        "var source=[1,2,3];var calls=0;var result;"
        "source.filter(function(value,index,array){calls=calls+1;"
        "if(index===0)array[1]=99;if(index===1)null.value;return true;});");
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
        "callback errors retain completed source side effects and publish no partial result");
}

void testValidationBoundsAndAllocation()
{
    const char* invalidCallbacks[] = {
        "var values=[];values.filter();",
        "var values=[];values.filter(undefined);",
        "var values=[];values.filter(null);",
        "var values=[];values.filter('callback');",
        "var values=[];values.filter({});",
    };
    for (const char* source : invalidCallbacks) {
        RuntimeContext runtime;
        const ScriptResult result = run(runtime, source);
        expect(result.status == ScriptStatus::RuntimeFailure &&
            result.runtimeError.code == RuntimeErrorCode::NotCallable,
            "missing and non-callable filter callbacks fail before empty traversal");
    }

    RuntimeContext unsupportedReceiver;
    const ScriptResult setup = run(unsupportedReceiver, "var receiver={};");
    const bool linked = setup.succeeded() &&
        unsupportedReceiver.setPrototypeForTesting(objectId(
            unsupportedReceiver, "receiver"),
            unsupportedReceiver.arrayPrototypeId());
    const ScriptResult receiverResult = linked
        ? runSameRealm(unsupportedReceiver,
            "receiver.filter(function(){return true;});")
        : ScriptResult();
    expect(receiverResult.status == ScriptStatus::RuntimeFailure &&
        receiverResult.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "generic array-like receivers are rejected");

    RuntimeContext failureInjection;
    const ScriptResult failureSetup = run(failureInjection,
        "var source=[1,2,3];var calls=0;var result;");
    const std::size_t beforeObjects =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(failureInjection);
    const std::size_t beforeSlots = failureInjection.arrayElementCount();
    NavigatorScriptRuntimeTestAccess::failNextArrayCreation(failureInjection);
    const ScriptResult allocationFailure = failureSetup.succeeded()
        ? runSameRealm(failureInjection,
            "result=source.filter(function(value){calls=calls+1;return true;});")
        : ScriptResult();
    expect(allocationFailure.status == ScriptStatus::RuntimeFailure &&
        allocationFailure.runtimeError.code == RuntimeErrorCode::AllocationFailure &&
        numberIs(failureInjection, "calls", 3.0),
        "destination allocation failure propagates after all callbacks complete");
    expect(NavigatorScriptRuntimeTestAccess::runtimeObjectCount(failureInjection) ==
            beforeObjects &&
        failureInjection.arrayElementCount() == beforeSlots &&
        binding(failureInjection, "result") != nullptr &&
        binding(failureInjection, "result")->isUndefined(),
        "failed destination allocation retains no result object or Array slots");

    RuntimeLimits capacityLimits;
    capacityLimits.maxTotalArrayElements = 2u;
    RuntimeContext capacityFailure(capacityLimits);
    const ScriptResult capacitySetup = run(capacityFailure,
        "var source=[1,2];var calls=0;var result;");
    const std::size_t capacityObjects =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(capacityFailure);
    const ScriptResult capacityResult = capacitySetup.succeeded()
        ? runSameRealm(capacityFailure,
            "result=source.filter(function(value){calls=calls+1;return true;});")
        : ScriptResult();
    expect(capacityResult.status == ScriptStatus::RuntimeFailure &&
        capacityResult.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        numberIs(capacityFailure, "calls", 2.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(capacityFailure) ==
            capacityObjects && capacityFailure.arrayElementCount() == 2u,
        "persistent slot exhaustion is bounded and publishes no partial result");

    RuntimeContext overflow;
    const ScriptResult overflowResult = run(overflow,
        "var source=[];source.length=1025;");
    expect(overflowResult.status == ScriptStatus::RuntimeFailure &&
        overflowResult.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded,
        "the established 1,025-position rejection boundary remains enforced");
}

std::string denseArraySource(std::size_t length)
{
    std::string source = "var source=[";
    for (std::size_t index = 0u; index < length; ++index) {
        if (index != 0u) source.push_back(',');
        source += std::to_string(index);
    }
    return source + "];";
}

void testMaximumBoundaries()
{
    RuntimeContext emptyRuntime;
    const ScriptResult empty = run(emptyRuntime,
        "var source=[];var result=source.filter(function(){return true;});"
        "var ok=result!==source&&result.length===0;");
    expect(empty.succeeded() && booleanIs(emptyRuntime, "ok"),
        "zero-position Array produces a distinct empty result");

    RuntimeContext sparseMaximum;
    const ScriptResult sparseResult = run(sparseMaximum,
        "var source=[];source.length=1024;source[800]=55;var calls=0;"
        "var result=source.filter(function(value){calls=calls+1;return true;});"
        "var ok=result.length===1&&result[0]===55&&calls===1;");
    expect(sparseResult.succeeded() && booleanIs(sparseMaximum, "ok") &&
        sparseMaximum.arrayElementCount() == 2u &&
        NavigatorScriptRuntimeTestAccess::hasOwnIndex(sparseMaximum,
            objectId(sparseMaximum, "result"), 0u),
        "a sparse maximum-length source stores only its selected dense value");

    RuntimeContext maximumPass;
    std::string allPassSource = denseArraySource(1024u) +
        "var calls=0;var result=source.filter(function(value,index,array){"
        "calls=calls+1;return true;});";
    const ScriptResult allPass = run(maximumPass, allPassSource);
    Value first;
    Value last;
    RuntimeErrorCode firstError = RuntimeErrorCode::None;
    RuntimeErrorCode lastError = RuntimeErrorCode::None;
    const bool endpoints = maximumPass.readPropertyForTesting(
        objectId(maximumPass, "result"), "0", first, firstError) &&
        maximumPass.readPropertyForTesting(objectId(maximumPass, "result"),
            "1023", last, lastError);
    expect(allPass.succeeded() && numberIs(maximumPass, "calls", 1024.0) &&
        NavigatorScriptRuntimeTestAccess::arrayLength(maximumPass,
            objectId(maximumPass, "result")) == 1024u && endpoints &&
        first.isNumber() && first.numberValue() == 0.0 &&
        last.isNumber() && last.numberValue() == 1023.0 &&
        maximumPass.arrayElementCount() == 2048u,
        "maximum-length all-pass filter preserves order and stores 1,024 slots");

    RuntimeContext maximumFail;
    const std::string allFailSource = denseArraySource(1024u) +
        "var calls=0;var result=source.filter(function(){calls=calls+1;"
        "return false;});var ok=result.length===0;";
    const ScriptResult allFail = run(maximumFail, allFailSource);
    expect(allFail.succeeded() && numberIs(maximumFail, "calls", 1024.0) &&
        booleanIs(maximumFail, "ok") && maximumFail.arrayElementCount() == 1024u,
        "maximum-length all-fail filter returns an empty result without result slots");
}

void testHostedFixtureAndEvents()
{
    std::ifstream input("navigator-smoke/javascript-js95.html",
        std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js95-filter.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult result = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    expect(loaded && loadError == RuntimeErrorCode::None && result.succeeded() &&
        markerText.find("JS95:assertions=14:failures=0") == 0u,
        "hosted JS95 fixture passes callback, truthiness, sparse, and mutation assertions");
    expect(markerText.find(":basic=true:sparse=true:mutation=true:nested=true") !=
            std::string::npos,
        "hosted JS95 marker records dense output and nested filter behavior");

    HostInstanceId event = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "filter-event-a") event = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = event != 0u && harness.dispatchClick(event, eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Events run filter callbacks while preserving Event contexts");
}

} // namespace

int main()
{
    testBasicBehaviorIdentityAndThisArg();
    testTruthiness();
    testSparseAndInheritedProperties();
    testLiveMutationAndNestedCalls();
    testValidationBoundsAndAllocation();
    testMaximumBoundaries();
    testHostedFixtureAndEvents();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS95 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS95 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
