#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

    static std::size_t runtimeObjectCount(const RuntimeContext& runtime)
    {
        return runtime.objects_.size();
    }

    static std::size_t runtimeObjectSize()
    {
        return sizeof(RuntimeContext::RuntimeObject);
    }

    static std::size_t arrayElementSize()
    {
        return sizeof(RuntimeContext::RuntimeArrayElement);
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

void testBasicBehaviorArgumentsAndIdentity()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var values=[1,3,6,9];var calls=0;var indices='';var argsOk=true;"
        "var found=values.some(function(value,index,array){calls=calls+1;"
        "indices=indices+index;argsOk=argsOk&&array===values&&"
        "value===values[index];return value%2===0;});"
        "var empty=[];var emptyCalls=0;var emptyResult=empty.some(function(){"
        "emptyCalls=emptyCalls+1;return true;});"
        "var none=[1,3,5].some(function(){return false;});"
        "var first=[8].some(function(value){return value===8;});"
        "var singleFalse=[8].some(function(){return false;});"
        "var last=[1,3,9].some(function(value){return value===9;});"
        "var basicOk=found===true&&calls===3&&indices==='012'&&argsOk&&"
        "emptyResult===false&&emptyCalls===0&&none===false&&first===true&&"
        "singleFalse===false&&last===true;");
    expect(result.succeeded() && booleanIs(runtime, "basicOk") &&
        numberIs(runtime, "calls", 3.0),
        "some returns Booleans and visits through the first match only");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime, "some"),
        "some is installed as a writable-only native Array prototype method");
    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    expect(runtime.readPropertyForTesting(runtime.arrayPrototypeId(), "some",
            method, methodError) && method.isFunction() &&
        methodError == RuntimeErrorCode::None,
        "Array.prototype exposes some as a genuine callable runtime function");
}

void testThisArgTruthinessAndShortCircuit()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var context={target:6};var calls=0;var thisOk=false;"
        "var values=[1,3,6,9];var matched=values.some(function(value,index,array){"
        "calls=calls+1;thisOk=this===context&&array===values&&index===2&&value===6;"
        "return this.target===value;},context);"
        "var truthyValues=[0,2,'yes',null];var truthyCalls=0;"
        "var truthyResult=truthyValues.some(function(value){truthyCalls=truthyCalls+1;"
        "return value;});"
        "var skipFailureCalls=0;var skipFailure=[2,4,6].some(function(value){"
        "skipFailureCalls=skipFailureCalls+1;if(value===6)null.absent;return value===2;});"
        "var directBoolean=values.some(function(value){return value===6&&7;});"
        "var truthinessOk=matched===true&&thisOk&&truthyResult===true&&"
        "truthyCalls===2&&skipFailure===true&&skipFailureCalls===1&&"
        "directBoolean===true;");
    expect(result.succeeded() && booleanIs(runtime, "truthinessOk"),
        "thisArg and runtime truthiness are preserved; a short circuit skips a later callback error");
    expect(binding(runtime, "matched") != nullptr &&
        binding(runtime, "matched")->isBoolean() &&
        binding(runtime, "matched")->booleanValue(),
        "some returns a Boolean rather than the callback's truthy value");

    RuntimeContext failureBeforeMatch;
    const ScriptResult failure = run(failureBeforeMatch,
        "var calls=0;var result;[1,2,3].some(function(value){calls=calls+1;"
        "if(value===2)null.absent;return value===3;});");
    expect(failure.status == ScriptStatus::RuntimeFailure &&
        failure.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        numberIs(failureBeforeMatch, "calls", 2.0),
        "a callback error before a later matching value propagates immediately");

    RuntimeContext nestedFailure;
    const ScriptResult nested = run(nestedFailure,
        "var result;[[1]].some(function(values){return values.some(function(){"
        "return null.absent;});});");
    expect(nested.status == ScriptStatus::RuntimeFailure &&
        nested.runtimeError.code == RuntimeErrorCode::CannotReadProperty,
        "errors from a nested some callback propagate through both traversals");
}

void testSparseAndInheritedProperties()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var holes=[];holes.length=5;var holeCalls=0;"
        "var holeResult=holes.some(function(){holeCalls=holeCalls+1;return true;});"
        "var sparse=[];sparse.length=5;sparse[1]=undefined;sparse[4]=9;"
        "var sparseCalls=0;var indices='';var explicitUndefined=false;"
        "var sparseResult=sparse.some(function(value,index,array){sparseCalls=sparseCalls+1;"
        "indices=indices+index;if(index===1)explicitUndefined=value===undefined&&"
        "array===sparse;return value===9;});"
        "var sparseOk=holeResult===false&&holeCalls===0&&sparseResult===true&&"
        "sparseCalls===2&&indices==='14'&&explicitUndefined;");
    expect(result.succeeded() && booleanIs(runtime, "sparseOk"),
        "holes are skipped, explicit undefined is visited, and source indices are retained");

    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[10,,30];var calls=0;var indices='';var result;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "1", Value::number(20.0));
    const ScriptResult inheritedResult = installed
        ? runSameRealm(inherited,
            "result=source.some(function(value,index,array){calls=calls+1;"
            "indices=indices+index;return value===20&&array===source;});")
        : ScriptResult();
    expect(inheritedResult.succeeded() && booleanIs(inherited, "result") &&
        numberIs(inherited, "calls", 2.0) &&
        binding(inherited, "indices") != nullptr &&
        inherited.stringValue(*binding(inherited, "indices")) == "01",
        "inherited numeric properties are visited with their original index");
}

void testLiveMutationAndTraversalBounds()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=[1,2,3];var replaceIndices='';var replaceCalls=0;"
        "var replaceResult=replaced.some(function(value,index,array){"
        "replaceCalls=replaceCalls+1;replaceIndices=replaceIndices+index;"
        "if(index===0)array[1]=9;return value===9;});"
        "var removed=[1,2,3];var removeIndices='';var removeResult="
        "removed.some(function(value,index,array){removeIndices=removeIndices+index;"
        "if(index===0){array.length=1;array.length=3;array[2]=30;}return false;});"
        "var filled=[1,,3];var fillIndices='';var fillResult=filled.some("
        "function(value,index,array){fillIndices=fillIndices+index;"
        "if(index===0)array[1]=2;return value===2;});"
        "var appended=[4,5];var appendCalls=0;var appendResult="
        "appended.some(function(value,index,array){appendCalls=appendCalls+1;"
        "if(index===0)array[2]=6;return value===6;});"
        "var shrunk=[7,8,9];var shrinkCalls=0;var shrinkResult="
        "shrunk.some(function(value,index,array){shrinkCalls=shrinkCalls+1;"
        "if(index===0)array.length=1;return false;});"
        "var current=[5];var currentValue=0;var currentResult=current.some("
        "function(value,index,array){array[0]=99;currentValue=value;return false;});"
        "var other=[2,3];var otherResult=[1].some(function(){other[1]=12;"
        "return other[1]===12;});"
        "var mutationOk=replaceResult===true&&replaceCalls===2&&"
        "replaceIndices==='01'&&removeResult===false&&removeIndices==='02'&&"
        "fillResult===true&&fillIndices==='01'&&appendResult===false&&"
        "appendCalls===2&&appended[2]===6&&shrinkResult===false&&"
        "shrinkCalls===1&&currentResult===false&&currentValue===5&&"
        "current[0]===99&&otherResult===true&&other[1]===12;");
    expect(result.succeeded() && booleanIs(runtime, "mutationOk"),
        "some uses live future properties, retains captured length, and observes current-value snapshots");

    RuntimeContext stopBeforeMutation;
    const ScriptResult stopped = run(stopBeforeMutation,
        "var source=[1,2,3];var calls=0;var result=source.some("
        "function(value,index,array){calls=calls+1;array[1]=9;return true;});");
    expect(stopped.succeeded() && booleanIs(stopBeforeMutation, "result") &&
        numberIs(stopBeforeMutation, "calls", 1.0),
        "successful early termination prevents every later source lookup and callback");
}

void testNestedTraversalIsolationAndEvents()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var outerCalls=0;var innerCalls=0;var nested=[[1,2],[3,4]].some("
        "function(values){outerCalls=outerCalls+1;return values.some(function(value){"
        "innerCalls=innerCalls+1;return value===4;});});"
        "var mapped=[1,2].map(function(value){var inner=[value,value+1].some("
        "function(item){return item===value+1;});return inner&&value*2;});"
        "var filtered=[0,1,2].filter(function(value){return [value,value+1].some("
        "function(item){return item===2;});});"
        "var eachCalls=0;[1,2].forEach(function(value){var found=[value,3].some("
        "function(item){eachCalls=eachCalls+1;return item===3;});if(found)eachCalls=eachCalls+1;});"
        "var outerSomeCalls=0;var composed=[1,2,3].some(function(value){"
        "outerSomeCalls=outerSomeCalls+1;var mappedInner=[value,value+1].map(function(n){return n*2;});"
        "var filteredInner=mappedInner.filter(function(n){return n>2;});"
        "return filteredInner.length===2;});"
        "var nestedSomeOk=nested===true&&outerCalls===2&&innerCalls===4;"
        "var mapOk=mapped.length===2&&mapped[0]===2&&mapped[1]===4;"
        "var filterOk=filtered.length===2&&filtered[0]===1&&filtered[1]===2;"
        "var eachOk=eachCalls===6;var composedOk=composed===true&&outerSomeCalls===2;"
        "var nestedOk=nestedSomeOk&&mapOk&&filterOk&&eachOk&&composedOk;");
    if (!result.succeeded() || !booleanIs(runtime, "nestedOk")) {
        std::cerr << "  JS96 nested diagnostic: status="
            << static_cast<unsigned>(result.status) << " error="
            << static_cast<unsigned>(result.runtimeError.code)
            << " parse=" << static_cast<unsigned>(result.parserError.code)
            << " at=" << result.parserError.location.offset
            << " actual=" << static_cast<unsigned>(result.parserError.actual)
            << " expected=" << static_cast<unsigned>(result.parserError.expected)
            << " nestedSome=" << booleanIs(runtime, "nestedSomeOk")
            << " map=" << booleanIs(runtime, "mapOk")
            << " filter=" << booleanIs(runtime, "filterOk")
            << " each=" << booleanIs(runtime, "eachOk")
            << " composed=" << booleanIs(runtime, "composedOk") << '\n';
    }
    expect(result.succeeded() && booleanIs(runtime, "nestedOk"),
        "nested some/map/filter/forEach invocations keep independent traversal state");

    std::ifstream input("navigator-smoke/javascript-js96.html",
        std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js96-some.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult hosted = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    expect(loaded && loadError == RuntimeErrorCode::None && hosted.succeeded() &&
        markerText.find("JS96:assertions=14:failures=0") == 0u,
        "hosted JS96 fixture passes representative some callback assertions");
    expect(markerText.find(":shortCircuit=true:sparse=true:nested=true") !=
            std::string::npos,
        "hosted JS96 marker records short-circuit, sparse, and nested behavior");

    HostInstanceId eventA = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "some-event-a") eventA = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = eventA != 0u && harness.dispatchClick(eventA, eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Event dispatch runs some callbacks without terminating the outer traversal");
}

void testValidationErrorsAndLimits()
{
    const char* invalidCallbacks[] = {
        "var values=[];values.some();",
        "var values=[];values.some(undefined);",
        "var values=[];values.some(null);",
        "var values=[];values.some('callback');",
        "var values=[];values.some({});",
    };
    for (const char* source : invalidCallbacks) {
        RuntimeContext runtime;
        const ScriptResult result = run(runtime, source);
        expect(result.status == ScriptStatus::RuntimeFailure &&
            result.runtimeError.code == RuntimeErrorCode::NotCallable,
            "missing and non-callable callbacks fail before empty traversal");
    }

    RuntimeContext unsupportedReceiver;
    const ScriptResult receiverSetup = run(unsupportedReceiver,
        "var source={};");
    const bool linked = receiverSetup.succeeded() &&
        unsupportedReceiver.setPrototypeForTesting(objectId(
            unsupportedReceiver, "source"),
            unsupportedReceiver.arrayPrototypeId());
    const ScriptResult receiver = linked
        ? runSameRealm(unsupportedReceiver,
            "source.some(function(){return true;});") : ScriptResult();
    expect(receiver.status == ScriptStatus::RuntimeFailure &&
        receiver.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "generic array-like receivers are rejected");

    RuntimeLimits stepLimits;
    stepLimits.maxExecutionSteps = 48u;
    RuntimeContext stepFailure(stepLimits);
    const ScriptResult budget = run(stepFailure,
        "[1].some(function(){while(true){}});");
    expect(budget.status == ScriptStatus::ExecutionBudgetExceeded &&
        budget.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded,
        "some propagates execution-budget exhaustion during bounded traversal");

    RuntimeLimits depthLimits;
    depthLimits.maxCallDepth = 1u;
    RuntimeContext depthFailure(depthLimits);
    const ScriptResult depth = run(depthFailure,
        "[1].some(function(){return true;});");
    expect(depth.status == ScriptStatus::RuntimeFailure &&
        depth.runtimeError.code == RuntimeErrorCode::CallDepthExceeded,
        "some callback invocation retains the runtime call-depth limit");

    RuntimeContext noResultArray;
    const ScriptResult setup = run(noResultArray,
        "var source=[1,2,3];var calls=0;var result;");
    const std::size_t objectsBefore =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(noResultArray);
    const std::size_t slotsBefore = noResultArray.arrayElementCount();
    const ScriptResult searched = setup.succeeded()
        ? runSameRealm(noResultArray,
            "result=source.some(function(value){calls=calls+1;return value===2;});")
        : ScriptResult();
    expect(searched.succeeded() && booleanIs(noResultArray, "result") &&
        numberIs(noResultArray, "calls", 2.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(noResultArray) ==
            objectsBefore && noResultArray.arrayElementCount() == slotsBefore,
        "some creates no result Array and retains no mapped-value slots");

    RuntimeContext overflow;
    const ScriptResult tooLong = run(overflow,
        "var source=[];source.length=1025;source.some(function(){return false;});");
    expect(tooLong.status == ScriptStatus::RuntimeFailure &&
        tooLong.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded,
        "the existing 1,025-position Array capacity rejection remains enforced");
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
    expect(NavigatorScriptRuntimeTestAccess::runtimeObjectSize() == 88u &&
        NavigatorScriptRuntimeTestAccess::arrayElementSize() == 48u,
        "some does not change RuntimeObject or Array element record sizes");

    RuntimeContext maximumFalse;
    const ScriptResult allFalse = run(maximumFalse,
        denseArraySource(1024u) +
        "var calls=0;var result=source.some(function(){calls=calls+1;return false;});");
    expect(allFalse.succeeded() && booleanIs(maximumFalse, "result", false) &&
        numberIs(maximumFalse, "calls", 1024.0) &&
        maximumFalse.arrayElementCount() == 1024u,
        "1,024-position all-false source visits every value without result storage");

    RuntimeContext maximumEarly;
    const ScriptResult early = run(maximumEarly,
        denseArraySource(1024u) +
        "var calls=0;var result=source.some(function(value){calls=calls+1;"
        "return value===5;});");
    expect(early.succeeded() && booleanIs(maximumEarly, "result") &&
        numberIs(maximumEarly, "calls", 6.0) &&
        maximumEarly.arrayElementCount() == 1024u,
        "maximum-length early match visits six values and skips the remaining 1,018");

    RuntimeContext sparseMaximum;
    const ScriptResult sparse = run(sparseMaximum,
        "var source=[];source.length=1024;var calls=0;"
        "var holes=source.some(function(){calls=calls+1;return true;});"
        "source[1023]=55;var found=source.some(function(value,index){"
        "calls=calls+1;return index===1023&&value===55;});");
    expect(sparse.succeeded() && booleanIs(sparseMaximum, "holes", false) &&
        booleanIs(sparseMaximum, "found") &&
        numberIs(sparseMaximum, "calls", 1.0) &&
        sparseMaximum.arrayElementCount() == 1u,
        "sparse maximum-length all-hole and one-element traversals stay bounded");
}

} // namespace

int main()
{
    testBasicBehaviorArgumentsAndIdentity();
    testThisArgTruthinessAndShortCircuit();
    testSparseAndInheritedProperties();
    testLiveMutationAndTraversalBounds();
    testNestedTraversalIsolationAndEvents();
    testValidationErrorsAndLimits();
    testMaximumBoundaries();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS96 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS96 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
