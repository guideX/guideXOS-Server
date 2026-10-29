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

void testBasicBehaviorAndBuiltin()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var values=[5,12,8,130,44];var calls=0;var indices='';"
        "var argsOk=true;var found=values.find(function(value,index,array){"
        "calls=calls+1;indices=indices+index;argsOk=argsOk&&"
        "array===values&&value===values[index];return value>10;});"
        "var empty=[];var emptyCalls=0;var emptyResult=empty.find(function(){"
        "emptyCalls=emptyCalls+1;return true;});"
        "var one=[8].find(function(value){return value===8;});"
        "var missCalls=0;var miss=[1,2,3].find(function(value){"
        "missCalls=missCalls+1;return false;});"
        "var first=[4,5,6].find(function(value){return value===4;});"
        "var last=[4,5,6].find(function(value){return value===6;});"
        "var truthySource=[4,8,12].find(function(){return 100;});"
        "var directValues=[null,'text',{},[9]];"
        "var nullValue=directValues.find(function(){return true;});"
        "var stringValue=directValues.find(function(value){return value==='text';});"
        "var objectValue=directValues.find(function(value){return value===directValues[2];});"
        "var arrayValue=directValues.find(function(value){return value===directValues[3];});"
        "var falsyCalls=0;var falsy=[0,'',null,undefined,false].find(function(value){"
        "falsyCalls=falsyCalls+1;return value;});"
        "var basicOk=found===12&&calls===2&&indices==='01'&&argsOk&&"
        "emptyResult===undefined&&emptyCalls===0&&one===8&&miss===undefined&&"
        "missCalls===3&&first===4&&last===6&&truthySource===4&&"
        "nullValue===null&&stringValue==='text'&&objectValue===directValues[2]&&"
        "arrayValue===directValues[3]&&falsy===undefined&&falsyCalls===5;");
    expect(result.succeeded() && booleanIs(runtime, "basicOk"),
        "find returns the first source value, uses predicate truthiness, and completes basic cases");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime, "find"),
        "find is installed as a writable-only native Array prototype method");
    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    expect(runtime.readPropertyForTesting(runtime.arrayPrototypeId(), "find",
            method, methodError) && method.isFunction() &&
        methodError == RuntimeErrorCode::None,
        "Array.prototype exposes find as a genuine callable runtime function");
}

void testThisArgAndShortCircuit()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var context={target:7};var calls=0;var thisOk=false;"
        "var values=[6,7,8];var found=values.find(function(value,index,array){"
        "calls=calls+1;thisOk=this===context&&array===values&&"
        "index===calls-1&&value===index+6;return this.target===value;},context);"
        "var firstCalls=0;var first=[1,2,3].find(function(value){"
        "firstCalls=firstCalls+1;return value===1;});"
        "var middleCalls=0;var middle=[1,2,3].find(function(value){"
        "middleCalls=middleCalls+1;return value===2;});"
        "var lastCalls=0;var last=[1,2,3].find(function(value){"
        "lastCalls=lastCalls+1;return value===3;});"
        "var suppressCalls=0;var suppressEffects=0;var suppress="
        "[1,2,3].find(function(value){suppressCalls=suppressCalls+1;"
        "suppressEffects=suppressEffects+1;if(value===3)null.absent;"
        "return value===1;});"
        "var shortOk=found===7&&calls===2&&thisOk&&first===1&&"
        "firstCalls===1&&middle===2&&middleCalls===2&&last===3&&lastCalls===3&&"
        "suppress===1&&suppressCalls===1&&suppressEffects===1;");
    expect(result.succeeded() && booleanIs(runtime, "shortOk"),
        "find honors thisArg and stops at the first truthy predicate without reaching a later error");
    expect(numberIs(runtime, "middleCalls", 2.0) &&
        numberIs(runtime, "lastCalls", 3.0),
        "first, middle, and final matches invoke callbacks only through the match");

    RuntimeContext failureBeforeMatch;
    const ScriptResult failure = run(failureBeforeMatch,
        "var calls=0;[1,2,3].find(function(value){calls=calls+1;"
        "if(value===2)null.absent;return value===3;});");
    expect(failure.status == ScriptStatus::RuntimeFailure &&
        failure.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        numberIs(failureBeforeMatch, "calls", 2.0),
        "a callback error reached before a match propagates immediately");

    RuntimeContext nestedFailure;
    const ScriptResult nested = run(nestedFailure,
        "[[1]].find(function(values){return values.find(function(){"
        "return null.absent;});});");
    expect(nested.status == ScriptStatus::RuntimeFailure &&
        nested.runtimeError.code == RuntimeErrorCode::CannotReadProperty,
        "a nested find callback failure propagates through both invocations");
}

void testSparseAndInheritedProperties()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var holes=[];holes.length=3;var allCalls=0;var allIndices='';"
        "var allValues='';var noMatch=holes.find(function(value,index,array){"
        "allCalls=allCalls+1;allIndices=allIndices+index;"
        "if(value===undefined)allValues=allValues+'u';else allValues=allValues+value;"
        "return false;});"
        "var selectHoleCalls=0;var selectedHole=holes.find(function(value){"
        "selectHoleCalls=selectHoleCalls+1;return value===undefined;});"
        "var sparse=[];sparse.length=3;sparse[1]=7;var sparseCalls=0;"
        "var sparseIndices='';var sparseValues='';var sparseResult=sparse.find(function(value,index){"
        "sparseCalls=sparseCalls+1;sparseIndices=sparseIndices+index;"
        "if(value===undefined)sparseValues=sparseValues+'u';"
        "else sparseValues=sparseValues+value;return false;});"
        "var explicit=[1,undefined,3];var explicitCalls=0;"
        "var explicitResult=explicit.find(function(value,index){"
        "explicitCalls=explicitCalls+1;return index===1&&value===undefined;});"
        "var sparseOk=noMatch===undefined&&allCalls===3&&allIndices==='012'&&"
        "allValues==='uuu'&&selectedHole===undefined&&selectHoleCalls===1&&"
        "sparseResult===undefined&&sparseCalls===3&&sparseIndices==='012'&&"
        "sparseValues==='u7u'&&explicitResult===undefined&&explicitCalls===2;");
    expect(result.succeeded() && booleanIs(runtime, "sparseOk"),
        "find visits leading, middle, trailing, and all holes as undefined and distinguishes stop from completion");

    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[,9];var calls=0;var result;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "0", Value::number(33.0));
    const ScriptResult inheritedResult = installed
        ? runSameRealm(inherited,
            "result=source.find(function(value,index,array){calls=calls+1;"
            "return index===0&&value===33&&array===source;});")
        : ScriptResult();
    expect(inheritedResult.succeeded() && numberIs(inherited, "result", 33.0) &&
        numberIs(inherited, "calls", 1.0),
        "an inherited numeric property resolves before a sparse slot becomes undefined");
}

void testLiveMutationAndCapturedValues()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=[1,2,3];var replaceCalls=0;var replaceSeen='';"
        "var replaceResult=replaced.find(function(value,index,array){"
        "replaceCalls=replaceCalls+1;replaceSeen=replaceSeen+value;"
        "if(index===0)array[1]=9;return value===9;});"
        "var filled=[1,,3];var fillCalls=0;var fillResult=filled.find(function(value,index,array){"
        "fillCalls=fillCalls+1;if(index===0)array[1]=2;return value===2;});"
        "var shrunk=[7,8,9];var shrinkCalls=0;var shrinkIndices='';"
        "var shrinkValues='';var shrinkResult=shrunk.find(function(value,index,array){"
        "shrinkCalls=shrinkCalls+1;shrinkIndices=shrinkIndices+index;"
        "if(value===undefined)shrinkValues=shrinkValues+'u';"
        "else shrinkValues=shrinkValues+value;"
        "if(index===0)array.length=1;return false;});"
        "var appended=[4,5];var appendCalls=0;var appendResult=appended.find(function(value,index,array){"
        "appendCalls=appendCalls+1;if(index===0)array[2]=6;return value===6;});"
        "var current=[12];var currentResult=current.find(function(value,index,array){"
        "array[index]=99;return true;});"
        "var unrelated=[2,3];var unrelatedResult=[1].find(function(){"
        "unrelated[1]=12;return unrelated[1]===12;});"
        "var ref={n:3};var refSource=[ref,{}];var capturedRef=refSource.find(function(value,index,array){"
        "if(index===0){array[0]={};array.length=0;value.n=9;"
        "var noise=[{a:1},{b:2}].map(function(item){return {copy:item};});"
        "return true;}return false;});"
        "var arrayRef=[1,2];var arraySource=[arrayRef];var capturedArray=arraySource.find(function(value,index,array){"
        "array[0]=[];array.length=0;return true;});"
        "var mutationOk=replaceResult===9&&replaceCalls===2&&replaceSeen==='19'&&"
        "filled[1]===2&&fillResult===2&&fillCalls===2&&shrinkResult===undefined&&"
        "shrinkCalls===3&&shrinkIndices==='012'&&shrinkValues==='7uu'&&"
        "appendResult===undefined&&appendCalls===2&&appended[2]===6&&"
        "currentResult===12&&current[0]===99&&unrelatedResult===1&&unrelated[1]===12&&"
        "capturedRef===ref&&capturedRef.n===9&&refSource.length===0&&"
        "capturedArray===arrayRef&&arraySource.length===0;");
    expect(result.succeeded() && booleanIs(runtime, "mutationOk"),
        "find captures length and live values, visits removed slots, and returns the pre-callback value safely");

    RuntimeContext stopBeforeLaterCallbacks;
    const ScriptResult stopped = run(stopBeforeLaterCallbacks,
        "var source=[undefined,5,10];var calls=0;var result=source.find(function(value,index){"
        "calls=calls+1;return index===0;});"
        "var noMatchCalls=0;var noMatch=[undefined,5,10].find(function(){"
        "noMatchCalls=noMatchCalls+1;return false;});"
        "var ok=result===undefined&&calls===1&&noMatch===undefined&&noMatchCalls===3;");
    expect(stopped.succeeded() && booleanIs(stopBeforeLaterCallbacks, "ok"),
        "a matched undefined stops after one callback while an all-false search visits every index");
}

void testNestedTraversalIsolationAndEvents()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var outerCalls=0;var innerCalls=0;var nested=[1,2].find(function(value){"
        "outerCalls=outerCalls+1;var inner=[10,20,30].find(function(item){"
        "innerCalls=innerCalls+1;return item>10;});return value===2&&inner===20;});"
        "var findInSome=[1,2].some(function(value){return [value,2].find(function(item){"
        "return item===2;})===2;});"
        "var findInEvery=[1,2].every(function(value){return [value,2].find(function(item){"
        "return item===2;})===2;});"
        "var findInMap=[1,2].map(function(value){return [value,value+10].find(function(item){"
        "return item>value;});});"
        "var findInFilter=[1,2,3].filter(function(value){return [value,0].find(function(item){"
        "return item===0;})===0;});"
        "var eachCalls=0;[1,2].forEach(function(value){var inner=[value,9].find(function(item){"
        "eachCalls=eachCalls+1;return item===9;});if(inner===9)eachCalls=eachCalls+1;});"
        "var methodsInsideFind=[1,2,3].find(function(value){var mapped=[value,4].map(function(item){"
        "return item*2;});var filtered=mapped.filter(function(item){return item>4;});"
        "var some=mapped.some(function(item){return item===8;});"
        "var every=[value].every(function(item){return item>0;});"
        "var visited=0;mapped.forEach(function(){visited=visited+1;});"
        "return value===2&&filtered.length===1&&some&&every&&visited===2;});"
        "var nestedOk=nested===2&&outerCalls===2&&innerCalls===4&&findInSome&&"
        "findInEvery&&findInMap.length===2&&findInMap[0]===11&&findInMap[1]===12&&"
        "findInFilter.length===3&&eachCalls===6&&methodsInsideFind===2;");
    if (!result.succeeded() || !booleanIs(runtime, "nestedOk")) {
        std::cerr << "  JS98 nested diagnostic: status="
            << static_cast<unsigned>(result.status) << " error="
            << static_cast<unsigned>(result.runtimeError.code)
            << " parse=" << static_cast<unsigned>(result.parserError.code)
            << " offset=" << result.parserError.location.offset << '\n';
    }
    expect(result.succeeded() && booleanIs(runtime, "nestedOk"),
        "nested find and other Array methods keep their stop state and sparse policies independent");

    std::ifstream input("navigator-smoke/javascript-js98.html", std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js98-find.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult hosted = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    expect(loaded && loadError == RuntimeErrorCode::None && hosted.succeeded() &&
        markerText.find("JS98:assertions=18:failures=0") == 0u,
        "hosted JS98 find fixture passes its inline assertions");
    expect(markerText.find(":shortCircuit=true:sparse=true:nested=true") !=
            std::string::npos,
        "hosted JS98 marker records short-circuit, hole visitation, and nested traversal");

    HostInstanceId eventA = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "find-event-a") eventA = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = eventA != 0u && harness.dispatchClick(eventA, eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Event dispatch keeps inner and outer find results independent");
}

void testValidationErrorsAndLimits()
{
    const char* invalidCallbacks[] = {
        "var values=[];values.find();",
        "var values=[];values.find(undefined);",
        "var values=[];values.find(null);",
        "var values=[];values.find('callback');",
        "var values=[];values.find({});",
    };
    for (const char* source : invalidCallbacks) {
        RuntimeContext runtime;
        const ScriptResult result = run(runtime, source);
        expect(result.status == ScriptStatus::RuntimeFailure &&
            result.runtimeError.code == RuntimeErrorCode::NotCallable,
            "missing and non-callable callbacks fail before empty traversal");
    }

    RuntimeContext unsupportedReceiver;
    const ScriptResult receiverSetup = run(unsupportedReceiver, "var source={};");
    const bool linked = receiverSetup.succeeded() &&
        unsupportedReceiver.setPrototypeForTesting(objectId(
            unsupportedReceiver, "source"), unsupportedReceiver.arrayPrototypeId());
    const ScriptResult receiver = linked
        ? runSameRealm(unsupportedReceiver, "source.find(function(){return true;});")
        : ScriptResult();
    expect(receiver.status == ScriptStatus::RuntimeFailure &&
        receiver.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "generic array-like receivers are rejected");

    RuntimeLimits stepLimits;
    stepLimits.maxExecutionSteps = 48u;
    RuntimeContext stepFailure(stepLimits);
    const ScriptResult budget = run(stepFailure,
        "[1].find(function(){while(true){}});");
    expect(budget.status == ScriptStatus::ExecutionBudgetExceeded &&
        budget.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded,
        "find propagates execution-budget exhaustion during callback execution");

    RuntimeLimits depthLimits;
    depthLimits.maxCallDepth = 1u;
    RuntimeContext depthFailure(depthLimits);
    const ScriptResult depth = run(depthFailure,
        "[1].find(function(){return true;});");
    expect(depth.status == ScriptStatus::RuntimeFailure &&
        depth.runtimeError.code == RuntimeErrorCode::CallDepthExceeded,
        "find callback invocation retains the runtime call-depth limit");

    RuntimeContext noResultArray;
    const ScriptResult setup = run(noResultArray,
        "var source=[1,2,3];var calls=0;var result;");
    const std::size_t objectsBefore =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(noResultArray);
    const std::size_t slotsBefore = noResultArray.arrayElementCount();
    const ScriptResult checked = setup.succeeded()
        ? runSameRealm(noResultArray,
            "result=source.find(function(value){calls=calls+1;return value>10;});")
        : ScriptResult();
    expect(checked.succeeded() && binding(noResultArray, "result") != nullptr &&
        binding(noResultArray, "result")->isUndefined() &&
        numberIs(noResultArray, "calls", 3.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(noResultArray) ==
            objectsBefore && noResultArray.arrayElementCount() == slotsBefore,
        "find creates no result Array and retains no staged values");

    RuntimeContext overflow;
    const ScriptResult tooLong = run(overflow,
        "var source=[];source.length=1025;source.find(function(){return true;});");
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
        "find does not change RuntimeObject or Array element record sizes");

    RuntimeContext maximumDense;
    const ScriptResult dense = run(maximumDense, denseArraySource(1024u) +
        "var calls=0;var result=source.find(function(){calls=calls+1;return false;});");
    expect(dense.succeeded() && binding(maximumDense, "result") != nullptr &&
        binding(maximumDense, "result")->isUndefined() &&
        numberIs(maximumDense, "calls", 1024.0) &&
        maximumDense.arrayElementCount() == 1024u,
        "no-match dense traversal invokes 1,024 callbacks at the capacity boundary");

    RuntimeContext maximumHoles;
    const ScriptResult holes = run(maximumHoles,
        "var source=[];source.length=1024;var calls=0;"
        "var result=source.find(function(value,index){calls=calls+1;"
        "return value===undefined&&index===1024;});");
    expect(holes.succeeded() && binding(maximumHoles, "result") != nullptr &&
        binding(maximumHoles, "result")->isUndefined() &&
        numberIs(maximumHoles, "calls", 1024.0) &&
        maximumHoles.arrayElementCount() == 0u,
        "no-match all-hole traversal invokes 1,024 callbacks without filling slots");

    RuntimeContext maximumEarly;
    const ScriptResult early = run(maximumEarly, denseArraySource(1024u) +
        "var calls=0;var result=source.find(function(value,index){calls=calls+1;"
        "return index===5;});");
    expect(early.succeeded() && numberIs(maximumEarly, "result", 5.0) &&
        numberIs(maximumEarly, "calls", 6.0) &&
        maximumEarly.arrayElementCount() == 1024u,
        "maximum-length early match stops at index five");
}

} // namespace

int main()
{
    testBasicBehaviorAndBuiltin();
    testThisArgAndShortCircuit();
    testSparseAndInheritedProperties();
    testLiveMutationAndCapturedValues();
    testNestedTraversalIsolationAndEvents();
    testValidationErrorsAndLimits();
    testMaximumBoundaries();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS98 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS98 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
