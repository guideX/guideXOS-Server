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

bool stringIs(const RuntimeContext& runtime, const char* name,
    const char* expected)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isString() &&
        runtime.stringValue(*value) == expected;
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

void testArithmeticArgumentsAndInitialValue()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var source=[10,20,30];var calls=0;var indexes='';var argsOk=true;"
        "var result=source.reduce(function(acc,value,index,array,extra){calls=calls+1;"
        "indexes=indexes+index;argsOk=argsOk&&array===source&&"
        "extra===undefined&&"
        "((index===0&&acc===0&&value===10)||"
        "(index===1&&acc===10&&value===20)||"
        "(index===2&&acc===31&&value===30));"
        "return acc+value+index;},0);"
        "var explicit= [2,4,6].reduce(function(acc,value){return acc+value;},10);"
        "var implicitCalls=0;var implicit=[2,4,6].reduce(function(acc,value){"
        "implicitCalls=implicitCalls+1;return acc+value;});"
        "var order=[20,5,2].reduce(function(acc,value){return acc-value;});"
        "var product=[2,3,4].reduce(function(acc,value){return acc*value;},1);"
        "var zeroCalls=0;var one=[5].reduce(function(acc,value){zeroCalls=zeroCalls+1;"
        "return acc+value;});"
        "var initCalls=0;var oneWithInitial=[5].reduce(function(acc,value){"
        "initCalls=initCalls+1;return acc+value;},10);"
        "var possibleThis={marker:10};var callbackThis=possibleThis;"
        "var thisValue=[1].reduce(function(acc,value){callbackThis=this;return acc+value;},"
        "0,possibleThis);var thisOk=thisValue===1&&callbackThis===undefined;");
    expect(result.succeeded() && numberIs(runtime, "result", 63.0) &&
        numberIs(runtime, "calls", 3.0) && stringIs(runtime, "indexes", "012") &&
        booleanIs(runtime, "argsOk"),
        "four callback values have the right accumulator, value, numeric index, and source identity");
    expect(numberIs(runtime, "explicit", 22.0) &&
        numberIs(runtime, "implicit", 12.0) &&
        numberIs(runtime, "implicitCalls", 2.0),
        "explicit and omitted initialValue select the expected seeds and callback counts");
    expect(numberIs(runtime, "order", 13.0) &&
        numberIs(runtime, "product", 24.0),
        "reduce keeps ascending order for subtraction and product");
    expect(numberIs(runtime, "one", 5.0) &&
        numberIs(runtime, "zeroCalls", 0.0) &&
        numberIs(runtime, "oneWithInitial", 15.0) &&
        numberIs(runtime, "initCalls", 1.0),
        "single-element reduction skips only the no-initial callback");
    expect(booleanIs(runtime, "thisOk"),
        "reduce treats only argument two as initialValue and does not add a thisArg");

    RuntimeContext presence;
    const ScriptResult explicitUndefined = run(presence,
        "var implicitCalls=0;var implicit=[5].reduce(function(acc,value){"
        "implicitCalls=implicitCalls+1;if(acc===undefined)return value;return acc;});"
        "var explicitCalls=0;var explicit=[5].reduce(function(acc,value){"
        "explicitCalls=explicitCalls+1;if(acc===undefined)return value;return acc;},undefined);"
        "var stringSeed='seed';var stringResult=[1].reduce(function(acc){return acc;},stringSeed);"
        "var booleanSeed=false;var booleanResult=[1].reduce(function(acc){return acc;},booleanSeed);"
        "var nullResult=[].reduce(function(){return 1;},null);");
    const Value* stringResult = binding(presence, "stringResult");
    const Value* booleanResult = binding(presence, "booleanResult");
    const Value* nullResult = binding(presence, "nullResult");
    expect(explicitUndefined.succeeded() &&
        numberIs(presence, "implicit", 5.0) &&
        numberIs(presence, "implicitCalls", 0.0) &&
        numberIs(presence, "explicit", 5.0) &&
        numberIs(presence, "explicitCalls", 1.0),
        "actual argument count distinguishes omitted from explicit undefined initialValue");
    expect(stringResult != nullptr && stringResult->isString() &&
        presence.stringValue(*stringResult) == "seed" &&
        booleanResult != nullptr && booleanResult->isBoolean() &&
        !booleanResult->booleanValue() && nullResult != nullptr &&
        nullResult->isNull(),
        "String, Boolean, and null initial values retain their runtime types without coercion");

    RuntimeContext falsy;
    const ScriptResult falsyReturns = run(falsy,
        "var calls=0;var result=[1,2,3].reduce(function(acc,value,index){"
        "calls=calls+1;if(index===0)return false;if(index===1)return 0;return null;},9);");
    const Value* finalFalsy = binding(falsy, "result");
    expect(falsyReturns.succeeded() && numberIs(falsy, "calls", 3.0) &&
        finalFalsy != nullptr && finalFalsy->isNull(),
        "false, zero, and null callback results remain accumulators without short-circuiting");

    RuntimeContext builtin;
    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    const bool methodFound = builtin.readPropertyForTesting(
        builtin.arrayPrototypeId(), "reduce", method, methodError);
    expect(methodFound && methodError == RuntimeErrorCode::None &&
        method.isFunction() &&
        NavigatorScriptRuntimeTestAccess::methodAttributes(builtin, "reduce"),
        "Array.prototype.reduce is a callable native with writable-only attributes");
}

void testEmptySparseAndInheritedProperties()
{
    RuntimeContext sparse;
    const ScriptResult result = run(sparse,
        "var source=[];source.length=5;source[1]=4;source[4]=6;"
        "var calls=0;var indexes='';var sum=source.reduce(function(acc,value,index){"
        "calls=calls+1;indexes=indexes+index;return acc+value;},0);"
        "var noInitCalls=0;var noInit=source.reduce(function(acc,value,index){"
        "noInitCalls=noInitCalls+1;return acc+value+index;});"
        "var explicit=[1,undefined,3];var explicitCalls=0;var sawUndefined=false;"
        "var explicitResult=explicit.reduce(function(acc,value,index){"
        "explicitCalls=explicitCalls+1;if(index===1&&value===undefined)sawUndefined=true;"
        "return acc;},0);"
        "var undefinedSeed=[undefined,5];var undefinedSeedCalls=0;"
        "var seedWasUndefined=false;var undefinedSeedResult=undefinedSeed.reduce(function(acc,value,index){"
        "undefinedSeedCalls=undefinedSeedCalls+1;if(index===1)seedWasUndefined=acc===undefined;"
        "return value;});");
    expect(result.succeeded() && numberIs(sparse, "sum", 10.0) &&
        numberIs(sparse, "calls", 2.0) && stringIs(sparse, "indexes", "14"),
        "sparse reduction skips holes and visits present indexes in order");
    expect(numberIs(sparse, "noInit", 14.0) &&
        numberIs(sparse, "noInitCalls", 1.0),
        "first present sparse slot seeds the accumulator without a callback");
    expect(numberIs(sparse, "explicitCalls", 3.0) &&
        booleanIs(sparse, "sawUndefined") &&
        numberIs(sparse, "explicitResult", 0.0),
        "explicit undefined slots are present values and participate");
    expect(numberIs(sparse, "undefinedSeedResult", 5.0) &&
        numberIs(sparse, "undefinedSeedCalls", 1.0) &&
        booleanIs(sparse, "seedWasUndefined"),
        "explicit undefined at the first present slot is a real no-initial seed");

    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[];source.length=5;source[4]=6;var sum;var calls=0;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "2", Value::number(4.0));
    const ScriptResult inheritedRun = installed
        ? runSameRealm(inherited,
            "sum=source.reduce(function(acc,value,index,array){calls=calls+1;"
            "return acc+value;});")
        : ScriptResult();
    expect(inheritedRun.succeeded() && numberIs(inherited, "sum", 10.0) &&
        numberIs(inherited, "calls", 1.0),
        "prototype-aware lookup selects an inherited first-present seed and later source slot");

    RuntimeContext inheritedVisit;
    const ScriptResult visitSetup = run(inheritedVisit,
        "var source=[];source.length=5;source[1]=2;var seen='';var total;");
    const bool visitInstalled = visitSetup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inheritedVisit,
            "4", Value::number(8.0));
    const ScriptResult visit = visitInstalled
        ? runSameRealm(inheritedVisit,
            "total=source.reduce(function(acc,value,index){seen=seen+index;"
            "return acc+value;},0);")
        : ScriptResult();
    expect(visit.succeeded() && numberIs(inheritedVisit, "total", 10.0) &&
        stringIs(inheritedVisit, "seen", "14"),
        "inherited numeric properties are visited after an explicit seed");

    RuntimeContext empties;
    const ScriptResult emptyInit = run(empties,
        "var calls=0;var empty=[].reduce(function(){calls=calls+1;},10);"
        "var emptyUndefined=[].reduce(function(){calls=calls+1;},undefined);");
    const Value* undefinedValue = binding(empties, "emptyUndefined");
    expect(emptyInit.succeeded() && numberIs(empties, "empty", 10.0) &&
        undefinedValue != nullptr && undefinedValue->isUndefined() &&
        numberIs(empties, "calls", 0.0),
        "empty arrays return the exact supplied value without callbacks");

    RuntimeContext emptyNoInitial;
    const ScriptResult emptyFailure = run(emptyNoInitial,
        "[].reduce(function(){return 1;});");
    expect(emptyFailure.status == ScriptStatus::RuntimeFailure &&
        emptyFailure.runtimeError.code == RuntimeErrorCode::TypeError,
        "empty Array without initialValue reports the runtime TypeError equivalent");

    RuntimeContext holesNoInitial;
    const ScriptResult holesFailure = run(holesNoInitial,
        "var source=[];source.length=3;source.reduce(function(){return 1;});");
    expect(holesFailure.status == ScriptStatus::RuntimeFailure &&
        holesFailure.runtimeError.code == RuntimeErrorCode::TypeError,
        "all-hole Array without initialValue is effectively empty");

    RuntimeContext highSingle;
    const ScriptResult high = run(highSingle,
        "var source=[];source.length=1024;source[1023]=7;var calls=0;"
        "var result=source.reduce(function(){calls=calls+1;return 0;});");
    expect(high.succeeded() && numberIs(highSingle, "result", 7.0) &&
        numberIs(highSingle, "calls", 0.0),
        "single present property at the final supported index seeds without callback");
}

void testAccumulatorOwnershipAndMutation()
{
    RuntimeContext ownership;
    const ScriptResult result = run(ownership,
        "var same={total:0};var sameResult=[1,2].reduce(function(acc,value){"
        "acc.total=acc.total+value;return acc;},same);"
        "var sourceObject={value:7};var reference=[sourceObject];"
        "var existing=reference.reduce(function(acc,value){return value;},null);"
        "var allocated=[1,2].reduce(function(acc,value,index){if(index===0)return {value:value};"
        "acc.value=acc.value+value;return acc;},{});"
        "var initialArray=[];var arrayResult=[1,2].reduce(function(acc,value){"
        "acc.push(value);return acc;},initialArray);"
        "var replacedArray=[1,2].reduce(function(acc,value,index){"
        "if(index===0)return [value];return [value,acc[0]];},initialArray);"
        "var nested=[1,2].reduce(function(acc,value){return [acc,value].reduce(function(a,v){"
        "return a+v;},0);},0);"
        "var switched=[1,2,3].reduce(function(acc,value,index){"
        "if(index===0)return {value:value};if(index===1)return 12;return {value:acc+value};"
        "},0);"
        "var sawUndefined=false;var undefinedResult=[1,2].reduce(function(acc,value,index){"
        "if(index===0)return undefined;sawUndefined=acc===undefined;return acc;},0);"
        "var sameOk=sameResult===same&&same.total===3;"
        "var allocatedOk=allocated.value===3;"
        "var changed=[];var mutation=[1,2].reduce(function(acc,value,index){"
        "changed.push(value);return acc+value;},0);"
        "var arraysOk=arrayResult===initialArray&&arrayResult.length===2&&"
        "replacedArray!==initialArray&&replacedArray.length===2;"
        "var switchedOk=switched.value===15;"
        "var mutationOk=mutation===3&&changed.length===2;");
    expect(result.succeeded() && booleanIs(ownership, "sameOk"),
        "object accumulator identity and in-place mutation survive later callbacks");
    expect(objectId(ownership, "existing") == objectId(ownership, "sourceObject"),
        "callback can return an existing source element reference");
    expect(booleanIs(ownership, "allocatedOk"),
        "new callback-created object remains valid as the next accumulator");
    expect(booleanIs(ownership, "arraysOk"),
        "Array accumulators retain identity and safely replace with a new Array");
    expect(numberIs(ownership, "nested", 3.0) &&
        booleanIs(ownership, "switchedOk") &&
        booleanIs(ownership, "sawUndefined"),
        "nested results, primitive/reference changes, and undefined accumulators remain valid");
    expect(booleanIs(ownership, "mutationOk"),
        "Array accumulator mutation is visible across callbacks");

    RuntimeContext replace;
    const ScriptResult replacements = run(replace,
        "var source=[1,2,3];var calls=0;var replaced=source.reduce(function(acc,value,index){"
        "calls=calls+1;if(index===0){source[1]=20;source[2]=30;}return acc+value;},0);"
        "var filled=[];filled.length=3;filled[0]=1;var fillCalls=0;"
        "var fill=filled.reduce(function(acc,value,index,array){fillCalls=fillCalls+1;"
        "if(index===0)array[1]=2;return acc+value;},0);"
        "var shrunk=[1,2,3];var shrinkCalls=0;var shrink=shrunk.reduce(function(acc,value,index,array){"
        "shrinkCalls=shrinkCalls+1;if(index===0)array.length=1;return acc+value;},0);"
        "var appended=[1,2];var appendCalls=0;var append=appended.reduce(function(acc,value,index,array){"
        "appendCalls=appendCalls+1;if(index===0)array[2]=3;return acc+value;},0);"
        "var appendOk=append===3&&appendCalls===2&&appended.length===3;"
        "var current=[4,5];var currentValues='';var currentSum=current.reduce(function(acc,value,index,array){"
        "currentValues=currentValues+value;if(index===0)array[index]=40;return acc+value;},0);"
        "var removed=[1,2,3];var removedCalls=0;var removedValues='';"
        "var removedSum=removed.reduce(function(acc,value,index,array){"
        "removedCalls=removedCalls+1;removedValues=removedValues+value;"
        "if(index===0)array.pop();return acc+value;},0);"
        "var removedOk=removedSum===3&&removedCalls===2&&removedValues==='12';"
        "var unrelated=[7,8];var other=[1,2].reduce(function(acc,value,index){"
        "if(index===0)unrelated[1]=80;return acc+value;},0);"
        "var currentUpdated=current[0]===40;var unrelatedUpdated=unrelated[1]===80;"
        "var sourceAccumulator={value:7};var source=[sourceAccumulator,1,2];"
        "var retained=source.reduce(function(acc,value,index,array){"
        "if(index===1)array.length=0;return acc;});"
        "var retainedOk=retained===sourceAccumulator&&retained.value===7;");
    expect(replacements.succeeded() && numberIs(replace, "replaced", 51.0),
        "replacement of future present values is observed at their visit");
    expect(numberIs(replace, "fill", 3.0) && numberIs(replace, "fillCalls", 2.0),
        "filling a future hole causes that slot to be visited");
    expect(numberIs(replace, "shrink", 1.0) &&
        numberIs(replace, "shrinkCalls", 1.0),
        "shrinking source length removes future own slots from live lookup");
    expect(booleanIs(replace, "appendOk"),
        "appends beyond captured length are not visited");
    expect(numberIs(replace, "currentSum", 9.0) &&
        stringIs(replace, "currentValues", "45") &&
        booleanIs(replace, "currentUpdated") &&
        numberIs(replace, "other", 3.0) &&
        booleanIs(replace, "unrelatedUpdated") &&
        booleanIs(replace, "retainedOk") &&
        booleanIs(replace, "removedOk"),
        "current value is captured before callback and unrelated arrays can mutate");
}

void testNestedReductionAndCrossMethodReentrancy()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var outerCalls=0;var innerCalls=0;var nested=[1,2].reduce(function(acc,value){"
        "outerCalls=outerCalls+1;var inside=[10,20].reduce(function(a,v){"
        "innerCalls=innerCalls+1;return a+v;},0);return acc+inside+value;},0);"
        "var map=[1,2].map(function(value){return [value,2].reduce(function(a,v){return a+v;},0);});"
        "var filter=[1,2,3].filter(function(value){return [value,2].reduce(function(a,v){return a+v;},0)>3;});"
        "var eachCalls=0;[1,2].forEach(function(value){eachCalls=eachCalls+[value].reduce(function(a,v){return a+v;},0);});"
        "var some=[1,2].some(function(value){return [value,1].reduce(function(a,v){return a+v;},0)===3;});"
        "var every=[1,2].every(function(value){return [value,1].reduce(function(a,v){return a+v;},0)>0;});"
        "var found=[1,2].find(function(value){return [value,2].reduce(function(a,v){return a+v;},0)===4;});"
        "var foundIndex=[1,2].findIndex(function(value){return [value,2].reduce(function(a,v){return a+v;},0)===3;});"
        "var methodsOk=nested===63&&outerCalls===2&&innerCalls===4&&map[0]===3&&map[1]===4&&"
        "filter.length===2&&filter[0]===2&&filter[1]===3&&eachCalls===3&&some&&every&&found===2&&foundIndex===0;");
    expect(result.succeeded() && booleanIs(runtime, "methodsOk"),
        "reduce reenters itself and map/filter/forEach/some/every/find/findIndex with isolated state");

    RuntimeContext failure;
    const ScriptResult nestedFailure = run(failure,
        "[1].reduce(function(acc){return [2].reduce(function(){return null.absent;},0);},0);");
    expect(nestedFailure.status == ScriptStatus::RuntimeFailure &&
        nestedFailure.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        failure.activeCallFrameCount() == 0u,
        "nested reduction error propagates and all active callback frames unwind");
}

void testHostedFixtureAndNestedEvents()
{
    std::ifstream input("navigator-smoke/javascript-js100.html", std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js100-reduce.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult hosted = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    if (!hosted.succeeded() ||
        markerText.find("JS100:assertions=16:failures=0") != 0u) {
        std::cout << "  JS100 hosted diagnostic: status="
            << static_cast<unsigned>(hosted.status) << " error="
            << static_cast<unsigned>(hosted.runtimeError.code)
            << " parse=" << static_cast<unsigned>(hosted.parserError.code)
            << " offset=" << hosted.parserError.location.offset
            << " marker=" << markerText << '\n';
    }
    expect(loaded && loadError == RuntimeErrorCode::None && hosted.succeeded() &&
        markerText.find("JS100:assertions=16:failures=0") == 0u,
        "hosted JS100 page passes bounded reduce semantics and sparse/error assertions");

    HostInstanceId eventA = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "reduce-event-a") eventA = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = eventA != 0u && harness.dispatchClick(eventA, eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Event dispatch preserves inner and outer reduction accumulators");

    HostInstanceId errorEvent = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "reduce-error-event") errorEvent = element.serial;
    }
    RuntimeErrorCode callbackError = RuntimeErrorCode::None;
    const bool errorClicked = errorEvent != 0u &&
        harness.dispatchClick(errorEvent, callbackError);
    expect(!errorClicked && callbackError == RuntimeErrorCode::TypeError &&
        harness.runtime().activeCallFrameCount() == 0u,
        "hosted empty reduction error propagates through nested Event cleanup");

    const bool inheritedInstalled =
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(
            harness.runtime(), "1023", Value::number(8.0));
    const ScriptResult inherited = inheritedInstalled
        ? runSameRealm(harness.runtime(),
            "var inheritedSource=[];inheritedSource.length=1024;"
            "var inheritedCalls=0;var inheritedResult=inheritedSource.reduce(function(){"
            "inheritedCalls=inheritedCalls+1;return 0;});")
        : ScriptResult();
    expect(inherited.succeeded() &&
        numberIs(harness.runtime(), "inheritedResult", 8.0) &&
        numberIs(harness.runtime(), "inheritedCalls", 0.0),
        "hosted page realm reduction uses an inherited last-index property as its seed");
}

void testErrorsLimitsAndCleanup()
{
    const char* invalidCallbacks[] = {
        "var source=[];source.reduce();",
        "var source=[];source.reduce(undefined);",
        "var source=[];source.reduce(null);",
        "var source=[];source.reduce('callback');",
        "var source=[];source.reduce({});",
    };
    for (const char* source : invalidCallbacks) {
        RuntimeContext runtime;
        const ScriptResult result = run(runtime, source);
        expect(result.status == ScriptStatus::RuntimeFailure &&
            result.runtimeError.code == RuntimeErrorCode::NotCallable,
            "missing/non-callable callback fails even when no callback would run");
    }

    RuntimeContext receiver;
    const ScriptResult invalidReceiver = run(receiver,
        "var object={reduce:[].reduce};object.reduce(function(acc){return acc;},0);");
    expect(invalidReceiver.status == ScriptStatus::RuntimeFailure &&
        invalidReceiver.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "reduce rejects non-Array receivers");

    RuntimeLimits budgetLimits;
    budgetLimits.maxExecutionSteps = 48u;
    RuntimeContext budget(budgetLimits);
    const ScriptResult budgetFailure = run(budget,
        "[1].reduce(function(){while(true){}},0);");
    expect(budgetFailure.status == ScriptStatus::ExecutionBudgetExceeded &&
        budgetFailure.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded &&
        budget.activeCallFrameCount() == 0u,
        "execution-budget exhaustion propagates and unwinds callback frames");

    RuntimeLimits depthLimits;
    depthLimits.maxCallDepth = 1u;
    RuntimeContext depth(depthLimits);
    const ScriptResult depthFailure = run(depth,
        "[1].reduce(function(){return 1;},0);");
    expect(depthFailure.status == ScriptStatus::RuntimeFailure &&
        depthFailure.runtimeError.code == RuntimeErrorCode::CallDepthExceeded &&
        depth.activeCallFrameCount() == 0u,
        "reduce callback retains the configured call-depth limit");

    RuntimeContext callbackFailure;
    const ScriptResult thrown = run(callbackFailure,
        "var lastAcc;[1,2].reduce(function(acc,value){lastAcc=acc;"
        "if(value===2)null.absent;return acc;},{});");
    expect(thrown.status == ScriptStatus::RuntimeFailure &&
        thrown.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        callbackFailure.activeCallFrameCount() == 0u &&
        objectId(callbackFailure, "lastAcc") != kInvalidRuntimeObjectId,
        "callback failure leaves retained accumulator references valid and unwinds native frames");

    RuntimeContext nestedCallbackFailure;
    const ScriptResult nestedFailure = run(nestedCallbackFailure,
        "[1].reduce(function(){return [2].map(function(){return null.x;})[0];},0);");
    expect(nestedFailure.status == ScriptStatus::RuntimeFailure &&
        nestedFailure.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        nestedCallbackFailure.activeCallFrameCount() == 0u,
        "nested callback failure propagates through reduce cleanup");

    RuntimeLimits arraySlots;
    arraySlots.maxTotalArrayElements = 2u;
    RuntimeContext slotFailure(arraySlots);
    const ScriptResult slot = run(slotFailure,
        "[1,2].reduce(function(acc,value){return [value];},0);");
    expect(slot.status == ScriptStatus::RuntimeFailure &&
        slot.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        slotFailure.activeCallFrameCount() == 0u,
        "array-slot exhaustion in a callback propagates without returning a partial accumulator");

    RuntimeLimits objects;
    RuntimeContext baseline;
    objects.maxObjects = baseline.objectCount() + 1u;
    RuntimeContext objectFailure(objects);
    const ScriptResult allocation = run(objectFailure,
        "[1,2].reduce(function(acc,value){return {};},0);");
    expect(allocation.status == ScriptStatus::RuntimeFailure &&
        allocation.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
        objectFailure.activeCallFrameCount() == 0u,
        "object allocation failure inside callback propagates and unwinds reduce");
}

void testPersistentStorageAndLayout()
{
    RuntimeContext runtime;
    const ScriptResult setup = run(runtime,
        "var source=[1,2,3];var result;var calls=0;");
    const std::size_t objectsBefore =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(runtime);
    const std::size_t slotsBefore = runtime.arrayElementCount();
    const ScriptResult reduction = setup.succeeded()
        ? runSameRealm(runtime,
            "result=source.reduce(function(acc,value){calls=calls+1;"
            "return acc+value;},0);")
        : ScriptResult();
    expect(reduction.succeeded() && numberIs(runtime, "result", 6.0) &&
        numberIs(runtime, "calls", 3.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(runtime) ==
            objectsBefore && runtime.arrayElementCount() == slotsBefore,
        "successful reduce retains no result object, Array, or persistent staging slots");
    expect(NavigatorScriptRuntimeTestAccess::runtimeObjectSize() == 88u &&
        NavigatorScriptRuntimeTestAccess::arrayElementSize() == 48u,
        "reduce leaves RuntimeObject and RuntimeArrayElement layouts unchanged");
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

void testBoundaries()
{
    RuntimeContext dense;
    const ScriptResult denseInit = run(dense, denseArraySource(1024u) +
        "var calls=0;var result=source.reduce(function(acc,value){calls=calls+1;"
        "return acc+value;},0);");
    expect(denseInit.succeeded() && numberIs(dense, "calls", 1024.0) &&
        numberIs(dense, "result", 523776.0) &&
        dense.arrayElementCount() == 1024u,
        "maximum dense Array with initialValue invokes exactly 1,024 callbacks");

    RuntimeContext denseNoInit;
    const ScriptResult denseSeed = run(denseNoInit, denseArraySource(1024u) +
        "var calls=0;var result=source.reduce(function(acc,value){calls=calls+1;"
        "return acc+value;});");
    expect(denseSeed.succeeded() && numberIs(denseNoInit, "calls", 1023.0) &&
        numberIs(denseNoInit, "result", 523776.0),
        "maximum dense Array without initialValue invokes exactly 1,023 callbacks");

    RuntimeContext holes;
    const ScriptResult holesInit = run(holes,
        "var source=[];source.length=1024;var calls=0;"
        "var result=source.reduce(function(acc){calls=calls+1;return acc;},10);");
    expect(holesInit.succeeded() && numberIs(holes, "result", 10.0) &&
        numberIs(holes, "calls", 0.0) && holes.arrayElementCount() == 0u,
        "maximum all-hole Array returns its seed without callbacks or element storage");

    RuntimeContext holesEmpty;
    const ScriptResult holesError = run(holesEmpty,
        "var source=[];source.length=1024;source.reduce(function(){return 0;});");
    expect(holesError.status == ScriptStatus::RuntimeFailure &&
        holesError.runtimeError.code == RuntimeErrorCode::TypeError,
        "maximum all-hole Array without initialValue reports TypeError");

    RuntimeContext overLimit;
    const ScriptResult tooLong = run(overLimit,
        "var source=[];var calls=0;source.length=1025;"
        "source.reduce(function(){calls=calls+1;return 0;},0);");
    expect(tooLong.status == ScriptStatus::RuntimeFailure &&
        tooLong.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        numberIs(overLimit, "calls", 0.0),
        "unsupported 1,025-position Array remains rejected before callback");
}

} // namespace

int main()
{
    testArithmeticArgumentsAndInitialValue();
    testEmptySparseAndInheritedProperties();
    testAccumulatorOwnershipAndMutation();
    testNestedReductionAndCrossMethodReentrancy();
    testHostedFixtureAndNestedEvents();
    testErrorsLimitsAndCleanup();
    testPersistentStorageAndLayout();
    testBoundaries();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS100 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS100 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
