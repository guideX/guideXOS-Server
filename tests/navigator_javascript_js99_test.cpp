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

void testBasicSemanticsAndBuiltin()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var values=[10,20,30];var calls=0;var argsOk=true;"
        "var middle=values.findIndex(function(value,index,array){calls=calls+1;"
        "argsOk=argsOk&&array===values&&value===array[index]&&index===calls-1;"
        "return value===20;});"
        "var zero=[10,20].findIndex(function(value){return value===10;});"
        "var last=[10,20,30].findIndex(function(value){return value===30;});"
        "var missCalls=0;var miss=[10,20,30].findIndex(function(){"
        "missCalls=missCalls+1;return false;});"
        "var multiple=[4,8,8].findIndex(function(value){return value===8;});"
        "var one=[8].findIndex(function(value){return value===8;});"
        "var oneMiss=[8].findIndex(function(){return false;});"
        "var truthyObject=[1].findIndex(function(){return {};});"
        "var truthyArray=[1].findIndex(function(){return [];});"
        "var truthyNumber=[1].findIndex(function(){return 2;});"
        "var truthyString=[1].findIndex(function(){return 'yes';});"
        "var falsyCalls=0;var falsy=[0,'',null,undefined,false].findIndex(function(value){"
        "falsyCalls=falsyCalls+1;return value;});"
        "var emptyCalls=0;var empty=[].findIndex(function(){"
        "emptyCalls=emptyCalls+1;return true;});"
        "var basicOk=middle===1&&calls===2&&argsOk&&zero===0&&last===2&&"
        "miss===-1&&missCalls===3&&multiple===1&&one===0&&oneMiss===-1&&"
        "truthyObject===0&&truthyArray===0&&truthyNumber===0&&"
        "truthyString===0&&falsy===-1&&falsyCalls===5&&empty===-1&&emptyCalls===0;");
    expect(result.succeeded() && booleanIs(runtime, "basicOk"),
        "findIndex returns first numeric indexes, -1 on misses, and uses callback truthiness");
    const Value* middle = binding(runtime, "middle");
    expect(middle != nullptr && middle->isNumber() &&
        middle->numberValue() == 1.0,
        "successful findIndex results have numeric type, including index one");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime,
            "findIndex"),
        "findIndex is installed as a writable-only native Array prototype method");
    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    expect(runtime.readPropertyForTesting(runtime.arrayPrototypeId(),
            "findIndex", method, methodError) && method.isFunction() &&
        methodError == RuntimeErrorCode::None,
        "Array.prototype exposes findIndex as a genuine callable function");
}

void testThisArgAndShortCircuit()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var context={target:7};var calls=0;var thisOk=false;var values=[6,7,8];"
        "var result=values.findIndex(function(value,index,array){calls=calls+1;"
        "thisOk=this===context&&array===values&&index===calls-1;"
        "return this.target===value;},context);"
        "var firstCalls=0;var first=[1,2,3].findIndex(function(value){"
        "firstCalls=firstCalls+1;if(value===3)null.late;return value===1;});"
        "var shortOk=result===1&&calls===2&&thisOk&&first===0&&firstCalls===1;");
    expect(result.succeeded() && booleanIs(runtime, "shortOk"),
        "findIndex forwards thisArg and skips callbacks after the first match");
    expect(numberIs(runtime, "first", 0.0),
        "findIndex returns numeric zero before a hypothetical later callback error");

    RuntimeContext failureBeforeMatch;
    const ScriptResult failure = run(failureBeforeMatch,
        "var calls=0;[1,2,3].findIndex(function(value){calls=calls+1;"
        "if(value===2)null.absent;return value===3;});");
    expect(failure.status == ScriptStatus::RuntimeFailure &&
        failure.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        numberIs(failureBeforeMatch, "calls", 2.0),
        "a callback error before a match propagates instead of becoming -1");
}

void testSparseAndInheritedProperties()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var sparse=[];sparse.length=5;sparse[2]=50;var calls=0;"
        "var seenIndices='';var seenValues='';var match=sparse.findIndex(function(value,index){"
        "calls=calls+1;seenIndices=seenIndices+index;"
        "if(value===undefined)seenValues=seenValues+'u';else seenValues=seenValues+value;"
        "return value===50;});"
        "var holes=[];holes.length=4;var holeCalls=0;var holeIndices='';"
        "var noMatch=holes.findIndex(function(value,index){holeCalls=holeCalls+1;"
        "holeIndices=holeIndices+index;return false;});"
        "var selectedHoleCalls=0;var selectedHole=holes.findIndex(function(value){"
        "selectedHoleCalls=selectedHoleCalls+1;return value===undefined;});"
        "var explicit=[1,undefined,3];var explicitCalls=0;"
        "var explicitUndefined=explicit.findIndex(function(value,index){"
        "explicitCalls=explicitCalls+1;return value===undefined&&index===1;});"
        "var sparseOk=match===2&&calls===3&&seenIndices==='012'&&"
        "seenValues==='uu50'&&noMatch===-1&&holeCalls===4&&"
        "holeIndices==='0123'&&selectedHole===0&&selectedHoleCalls===1&&"
        "explicitUndefined===1&&explicitCalls===2;");
    expect(result.succeeded() && booleanIs(runtime, "sparseOk"),
        "findIndex visits holes as undefined, short-circuits at a sparse match, and distinguishes explicit undefined by index");
    expect(numberIs(runtime, "noMatch", -1.0),
        "an all-hole Array completes normally with numeric -1");

    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[];source.length=3;source[1]=8;var calls=0;var result;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "0", Value::number(33.0));
    const ScriptResult inheritedResult = installed
        ? runSameRealm(inherited,
            "result=source.findIndex(function(value,index,array){calls=calls+1;"
            "return index===0&&value===33&&array===source;});")
        : ScriptResult();
    expect(inheritedResult.succeeded() && numberIs(inherited, "result", 0.0) &&
        numberIs(inherited, "calls", 1.0),
        "an inherited numeric property resolves before visiting a sparse slot");
}

void testLiveMutationAndCapturedLength()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=[1,2,3];var replaceCalls=0;var replaceResult=replaced.findIndex(function(value,index,array){"
        "replaceCalls=replaceCalls+1;if(index===0)array[1]=9;return value===9;});"
        "var filled=[1,,3];var fillCalls=0;var fillResult=filled.findIndex(function(value,index,array){"
        "fillCalls=fillCalls+1;if(index===0)array[1]=2;return value===2;});"
        "var removed=[1,2,3];var removeCalls=0;var removeSeen='';"
        "var removeResult=removed.findIndex(function(value,index,array){removeCalls=removeCalls+1;"
        "if(value===undefined)removeSeen=removeSeen+'u';else removeSeen=removeSeen+value;"
        "if(index===0)array.pop();return index===2&&value===undefined;});"
        "var shrunk=[7,8,9];var shrinkCalls=0;var shrinkIndices='';var shrinkValues='';"
        "var shrinkResult=shrunk.findIndex(function(value,index,array){shrinkCalls=shrinkCalls+1;"
        "shrinkIndices=shrinkIndices+index;if(value===undefined)shrinkValues=shrinkValues+'u';"
        "else shrinkValues=shrinkValues+value;if(index===0)array.length=1;return false;});"
        "var appended=[4,5];var appendCalls=0;var appendResult=appended.findIndex(function(value,index,array){"
        "appendCalls=appendCalls+1;if(index===0)array[2]=6;return value===6;});"
        "var current=[12];var currentResult=current.findIndex(function(value,index,array){"
        "array[index]=99;return value===12;});"
        "var other=[2,3];var otherResult=[1].findIndex(function(){other[1]=12;"
        "return other[1]===12;});"
        "var mutationOk=replaceResult===1&&replaceCalls===2&&fillResult===1&&"
        "fillCalls===2&&removeResult===2&&removeCalls===3&&removeSeen==='12u'&&"
        "shrinkResult===-1&&shrinkCalls===3&&shrinkIndices==='012'&&"
        "shrinkValues==='7uu'&&appendResult===-1&&appendCalls===2&&"
        "appended[2]===6&&currentResult===0&&current[0]===99&&"
        "otherResult===0&&other[1]===12;");
    expect(result.succeeded() && booleanIs(runtime, "mutationOk"),
        "findIndex uses captured length and live slot values across replacement, filling, deletion, shrink, append, and current-slot mutation");
}

void testNestedTraversalIsolation()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var outerCalls=0;var innerCalls=0;var nested=[1,2].findIndex(function(value){"
        "outerCalls=outerCalls+1;var inner=[10,20,30].findIndex(function(item){"
        "innerCalls=innerCalls+1;return item>10;});return value===2&&inner===1;});"
        "var findInsideFind=[1,2].find(function(value){return [7,8].findIndex(function(item){"
        "return item===8;})===1&&value===2;});"
        "var findInsideIndex=[1,2].findIndex(function(value){return [4,5].find(function(item){"
        "return item===5;})===5&&value===2;});"
        "var findIndexInSome=[1,2].some(function(value){return [value,9].findIndex(function(item){"
        "return item===9;})===1;});"
        "var findIndexInEvery=[1,2].every(function(value){return [value,9].findIndex(function(item){"
        "return item===9;})===1;});"
        "var findIndexInMap=[1,2].map(function(value){return [value,value+10].findIndex(function(item){"
        "return item>value;});});"
        "var findIndexInFilter=[1,2,3].filter(function(value){return [value,0].findIndex(function(item){"
        "return item===0;})===1;});"
        "var eachCalls=0;[1,2].forEach(function(value){var inner=[value,9].findIndex(function(item){"
        "eachCalls=eachCalls+1;return item===9;});if(inner===1)eachCalls=eachCalls+1;});"
        "var nestedOk=nested===1&&outerCalls===2&&innerCalls===4&&"
        "findInsideFind===2&&findInsideIndex===1&&findIndexInSome&&findIndexInEvery&&"
        "findIndexInMap.length===2&&findIndexInMap[0]===1&&findIndexInMap[1]===1&&"
        "findIndexInFilter.length===3&&eachCalls===6;");
    if (!result.succeeded() || !booleanIs(runtime, "nestedOk")) {
        std::cerr << "  JS99 nested diagnostic: status="
            << static_cast<unsigned>(result.status) << " error="
            << static_cast<unsigned>(result.runtimeError.code)
            << " parse=" << static_cast<unsigned>(result.parserError.code)
            << " offset=" << result.parserError.location.offset << '\n';
    }
    expect(result.succeeded() && booleanIs(runtime, "nestedOk"),
        "nested findIndex and find calls preserve each traversal result across all existing callback methods");

    RuntimeContext nestedFailure;
    const ScriptResult failure = run(nestedFailure,
        "[1].findIndex(function(){return [2].findIndex(function(){"
        "return null.absent;})===-1;});");
    expect(failure.status == ScriptStatus::RuntimeFailure &&
        failure.runtimeError.code == RuntimeErrorCode::CannotReadProperty,
        "nested callback errors propagate through the correct traversal invocation");
}

void testCrossMethodConsistencyAndStorage()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var values=[3,6,9];var found=values.find(function(value){return value>5;});"
        "var index=values.findIndex(function(value){return value>5;});"
        "var notFound=values.find(function(){return false;});"
        "var notFoundIndex=values.findIndex(function(){return false;});"
        "var holes=[];holes.length=3;var foundUndefined=holes.find(function(value){"
        "return value===undefined;});var undefinedIndex=holes.findIndex(function(value){"
        "return value===undefined;});var calls=0;"
        "var crossOk=found===6&&index===1&&notFound===undefined&&notFoundIndex===-1&&"
        "foundUndefined===undefined&&undefinedIndex===0;");
    expect(result.succeeded() && booleanIs(runtime, "crossOk"),
        "find and findIndex agree on the first matching sparse/dense position and retain distinct no-match results");

    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[];source.length=2;var found;var index;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "0", Value::number(42.0));
    const ScriptResult inheritedResult = installed
        ? runSameRealm(inherited,
            "found=source.find(function(value){return value===42;});"
            "index=source.findIndex(function(value){return value===42;});")
        : ScriptResult();
    expect(inheritedResult.succeeded() && numberIs(inherited, "found", 42.0) &&
        numberIs(inherited, "index", 0.0),
        "find and findIndex resolve equivalent inherited numeric matches");

    RuntimeContext storage;
    const ScriptResult storageSetup = run(storage,
        "var source=[1,2,3];var calls=0;var result;");
    const std::size_t objectsBefore =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(storage);
    const std::size_t slotsBefore = storage.arrayElementCount();
    const ScriptResult searched = storageSetup.succeeded()
        ? runSameRealm(storage,
            "result=source.findIndex(function(value){calls=calls+1;return value>10;});")
        : ScriptResult();
    expect(searched.succeeded() && numberIs(storage, "result", -1.0) &&
        numberIs(storage, "calls", 3.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(storage) ==
            objectsBefore && storage.arrayElementCount() == slotsBefore,
        "findIndex allocates no result Array or persistent result object");
    expect(NavigatorScriptRuntimeTestAccess::runtimeObjectSize() == 88u &&
        NavigatorScriptRuntimeTestAccess::arrayElementSize() == 48u,
        "findIndex leaves RuntimeObject and Array element storage sizes unchanged");
}

void testHostedFixtureAndNestedEvents()
{
    std::ifstream input("navigator-smoke/javascript-js99.html", std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js99-find-index.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult hosted = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    if (!hosted.succeeded() ||
        markerText.find("JS99:assertions=19:failures=0") != 0u) {
        std::cout << "  JS99 hosted diagnostic: status="
            << static_cast<unsigned>(hosted.status) << " error="
            << static_cast<unsigned>(hosted.runtimeError.code)
            << " parse=" << static_cast<unsigned>(hosted.parserError.code)
            << " offset=" << hosted.parserError.location.offset
            << " marker=" << markerText << '\n';
    }
    expect(loaded && loadError == RuntimeErrorCode::None && hosted.succeeded() &&
        markerText.find("JS99:assertions=19:failures=0") == 0u,
        "hosted JS99 fixture passes first/middle/last, sparse, mutation, and nested assertions");
    expect(markerText.find(":shortCircuit=true:sparse=true:nested=true") !=
            std::string::npos,
        "hosted JS99 marker records short-circuit, sparse visitation, and nested traversal");

    HostInstanceId eventA = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "find-index-event-a") eventA = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = eventA != 0u && harness.dispatchClick(eventA, eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Event dispatch keeps inner and outer findIndex results independent");
}

void testValidationErrorsAndLimits()
{
    const char* invalidCallbacks[] = {
        "var values=[];values.findIndex();",
        "var values=[];values.findIndex(undefined);",
        "var values=[];values.findIndex(null);",
        "var values=[];values.findIndex('callback');",
        "var values=[];values.findIndex({});",
    };
    for (const char* source : invalidCallbacks) {
        RuntimeContext runtime;
        const ScriptResult result = run(runtime, source);
        expect(result.status == ScriptStatus::RuntimeFailure &&
            result.runtimeError.code == RuntimeErrorCode::NotCallable,
            "missing and non-callable callbacks fail even on an empty Array");
    }

    RuntimeContext unsupportedReceiver;
    const ScriptResult receiverSetup = run(unsupportedReceiver, "var source={};");
    const bool linked = receiverSetup.succeeded() &&
        unsupportedReceiver.setPrototypeForTesting(objectId(
            unsupportedReceiver, "source"), unsupportedReceiver.arrayPrototypeId());
    const ScriptResult receiver = linked
        ? runSameRealm(unsupportedReceiver,
            "source.findIndex(function(){return true;});")
        : ScriptResult();
    expect(receiver.status == ScriptStatus::RuntimeFailure &&
        receiver.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "generic array-like receivers are rejected");

    RuntimeLimits stepLimits;
    stepLimits.maxExecutionSteps = 48u;
    RuntimeContext stepFailure(stepLimits);
    const ScriptResult budget = run(stepFailure,
        "[1].findIndex(function(){while(true){}});");
    expect(budget.status == ScriptStatus::ExecutionBudgetExceeded &&
        budget.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded,
        "findIndex propagates execution-budget exhaustion during callback execution");

    RuntimeLimits depthLimits;
    depthLimits.maxCallDepth = 1u;
    RuntimeContext depthFailure(depthLimits);
    const ScriptResult depth = run(depthFailure,
        "[1].findIndex(function(){return true;});");
    expect(depth.status == ScriptStatus::RuntimeFailure &&
        depth.runtimeError.code == RuntimeErrorCode::CallDepthExceeded,
        "findIndex callback invocation retains the runtime call-depth limit");

    RuntimeContext sparseFailure;
    const ScriptResult sparseError = run(sparseFailure,
        "var source=[];source.length=3;var calls=0;source.findIndex(function(value){"
        "calls=calls+1;return null.absent;});");
    expect(sparseError.status == ScriptStatus::RuntimeFailure &&
        sparseError.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        numberIs(sparseFailure, "calls", 1.0),
        "a callback error on the first sparse slot propagates after one visit");

    RuntimeContext overflow;
    const ScriptResult tooLong = run(overflow,
        "var source=[];source.length=1025;source.findIndex(function(){return true;});");
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
    RuntimeContext maximumDense;
    const ScriptResult dense = run(maximumDense, denseArraySource(1024u) +
        "var calls=0;var result=source.findIndex(function(){calls=calls+1;return false;});");
    expect(dense.succeeded() && numberIs(maximumDense, "result", -1.0) &&
        numberIs(maximumDense, "calls", 1024.0) &&
        maximumDense.arrayElementCount() == 1024u,
        "no-match dense traversal invokes exactly 1,024 callbacks at the limit");

    RuntimeContext maximumHoles;
    const ScriptResult holes = run(maximumHoles,
        "var source=[];source.length=1024;var calls=0;"
        "var result=source.findIndex(function(value){calls=calls+1;"
        "return value===undefined&&calls===1025;});");
    expect(holes.succeeded() && numberIs(maximumHoles, "result", -1.0) &&
        numberIs(maximumHoles, "calls", 1024.0) &&
        maximumHoles.arrayElementCount() == 0u,
        "no-match all-hole traversal invokes 1,024 callbacks without filling slots");

    RuntimeContext maximumLast;
    const ScriptResult last = run(maximumLast, denseArraySource(1024u) +
        "var calls=0;var result=source.findIndex(function(value,index){calls=calls+1;"
        "return index===1023;});");
    expect(last.succeeded() && numberIs(maximumLast, "result", 1023.0) &&
        numberIs(maximumLast, "calls", 1024.0),
        "a match at the last valid index returns 1,023 after 1,024 callbacks");

    RuntimeContext maximumSparseEarly;
    const ScriptResult early = run(maximumSparseEarly,
        "var source=[];source.length=1024;source[1000]=7;var calls=0;"
        "var result=source.findIndex(function(value,index){calls=calls+1;"
        "return value===7;});");
    expect(early.succeeded() && numberIs(maximumSparseEarly, "result", 1000.0) &&
        numberIs(maximumSparseEarly, "calls", 1001.0) &&
        maximumSparseEarly.arrayElementCount() == 1u,
        "maximum-length sparse traversal short-circuits at the occupied match");

    RuntimeContext first;
    const ScriptResult firstMatch = run(first,
        "var calls=0;var result=[9].findIndex(function(){calls=calls+1;return true;});");
    expect(firstMatch.succeeded() && numberIs(first, "result", 0.0) &&
        numberIs(first, "calls", 1.0),
        "index zero is a successful match after exactly one callback");
}

} // namespace

int main()
{
    testBasicSemanticsAndBuiltin();
    testThisArgAndShortCircuit();
    testSparseAndInheritedProperties();
    testLiveMutationAndCapturedLength();
    testNestedTraversalIsolation();
    testCrossMethodConsistencyAndStorage();
    testHostedFixtureAndNestedEvents();
    testValidationErrorsAndLimits();
    testMaximumBoundaries();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS99 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS99 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
