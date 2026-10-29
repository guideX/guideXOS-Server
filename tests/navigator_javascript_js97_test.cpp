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
        "var values=[2,4,6,9,10];var calls=0;var indices='';var argsOk=true;"
        "var allEven=values.every(function(value,index,array){calls=calls+1;"
        "indices=indices+index;argsOk=argsOk&&array===values&&"
        "value===values[index];return value%2===0;});"
        "var empty=[];var emptyCalls=0;var emptyResult=empty.every(function(){"
        "emptyCalls=emptyCalls+1;return false;});"
        "var oneTrue=[7].every(function(value){return value===7;});"
        "var oneFalse=[7].every(function(){return false;});"
        "var allTrue=[2,4,6].every(function(value){return value%2===0;});"
        "var allFalse=[1,3,5].every(function(value){return value%2===0;});"
        "var firstFalse=[1,2,3].every(function(value){return value>1;});"
        "var middleFalse=[1,2,3].every(function(value){return value!==2;});"
        "var lastFalse=[1,2,3].every(function(value){return value<3;});"
        "var basicOk=allEven===false&&calls===4&&indices==='0123'&&argsOk&&"
        "emptyResult===true&&emptyCalls===0&&oneTrue===true&&oneFalse===false&&"
        "allTrue===true&&allFalse===false&&firstFalse===false&&"
        "middleFalse===false&&lastFalse===false&&values[4]===10;");
    expect(result.succeeded() && booleanIs(runtime, "basicOk") &&
        numberIs(runtime, "calls", 4.0),
        "every returns a Boolean, visits through the first failure, and preserves its source");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime, "every"),
        "every is installed as a writable-only native Array prototype method");
    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    expect(runtime.readPropertyForTesting(runtime.arrayPrototypeId(), "every",
            method, methodError) && method.isFunction() &&
        methodError == RuntimeErrorCode::None,
        "Array.prototype exposes every as a genuine callable runtime function");
}

void testThisArgTruthinessAndShortCircuit()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var context={target:7};var calls=0;var thisOk=false;"
        "var values=[7,7];var all=values.every(function(value,index,array){"
        "calls=calls+1;thisOk=this===context&&array===values&&"
        "index===calls-1&&value===7;return this.target===value;},context);"
        "var truthyValues=[true,2,'yes',{},[]];var truthyCalls=0;"
        "var truthyResult=truthyValues.every(function(value){truthyCalls=truthyCalls+1;"
        "return value;});"
        "var falsyValues=[false,0,'',null,undefined];var falsyCalls=0;"
        "var falsyResult=falsyValues.every(function(value){falsyCalls=falsyCalls+1;"
        "return value;});"
        "var zeroFalse=[0].every(function(value){return value;});"
        "var emptyStringFalse=[''].every(function(value){return value;});"
        "var nullFalse=[null].every(function(value){return value;});"
        "var undefinedFalse=[undefined].every(function(value){return value;});"
        "var directBoolean=values.every(function(){return 7;});"
        "var skipFailureCalls=0;var skipFailure=[1,2,3].every(function(value){"
        "skipFailureCalls=skipFailureCalls+1;if(value===3)null.absent;"
        "return value<2;});"
        "var truthinessOk=all===true&&calls===2&&thisOk&&truthyResult===true&&"
        "truthyCalls===5&&falsyResult===false&&falsyCalls===1&&"
        "zeroFalse===false&&emptyStringFalse===false&&nullFalse===false&&"
        "undefinedFalse===false&&"
        "directBoolean===true&&skipFailure===false&&skipFailureCalls===2;");
    expect(result.succeeded() && booleanIs(runtime, "truthinessOk"),
        "thisArg and JavaScript truthiness are used; a falsy result skips later callbacks");
    expect(binding(runtime, "all") != nullptr && binding(runtime, "all")->isBoolean() &&
        binding(runtime, "all")->booleanValue(),
        "every returns a Boolean rather than the callback's truthy value");

    RuntimeContext failureBeforeFalse;
    const ScriptResult failure = run(failureBeforeFalse,
        "var calls=0;[1,2,3].every(function(value){calls=calls+1;"
        "if(value===2)null.absent;return value<2;});");
    expect(failure.status == ScriptStatus::RuntimeFailure &&
        failure.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        numberIs(failureBeforeFalse, "calls", 2.0),
        "a genuine callback error before a falsy predicate propagates immediately");

    RuntimeContext nestedFailure;
    const ScriptResult nested = run(nestedFailure,
        "[[1]].every(function(values){return values.every(function(){"
        "return null.absent;});});");
    expect(nested.status == ScriptStatus::RuntimeFailure &&
        nested.runtimeError.code == RuntimeErrorCode::CannotReadProperty,
        "a nested every callback failure propagates through both invocation boundaries");
}

void testSparseAndInheritedProperties()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var holes=[];holes.length=5;var holeCalls=0;"
        "var holeResult=holes.every(function(){holeCalls=holeCalls+1;return false;});"
        "var sparse=[];sparse.length=5;sparse[1]=undefined;sparse[4]=9;"
        "var sparseCalls=0;var indices='';var undefinedVisited=false;"
        "var sparseResult=sparse.every(function(value,index,array){"
        "sparseCalls=sparseCalls+1;indices=indices+index;"
        "if(index===1)undefinedVisited=value===undefined&&array===sparse;"
        "return value===undefined||value===9;});"
        "var edges=[,2,,];var edgeIndices='';var edgeResult="
        "edges.every(function(value,index){edgeIndices=edgeIndices+index;"
        "return value===2;});"
        "var sparseOk=holeResult===true&&holeCalls===0&&sparseResult===true&&"
        "sparseCalls===2&&indices==='14'&&undefinedVisited&&edgeResult===true&&"
        "edgeIndices==='1';");
    expect(result.succeeded() && booleanIs(runtime, "sparseOk"),
        "leading, middle, trailing, and all holes are skipped while explicit undefined is visited");

    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[10,,30];var calls=0;var indices='';var result;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "1", Value::number(20.0));
    const ScriptResult inheritedResult = installed
        ? runSameRealm(inherited,
            "result=source.every(function(value,index,array){calls=calls+1;"
            "indices=indices+index;return value<15&&array===source;});")
        : ScriptResult();
    expect(inheritedResult.succeeded() && booleanIs(inherited, "result", false) &&
        numberIs(inherited, "calls", 2.0) && binding(inherited, "indices") != nullptr &&
        inherited.stringValue(*binding(inherited, "indices")) == "01",
        "an inherited numeric value fills a hole, is visited at its source index, and can stop traversal");
}

void testLiveMutationAndCapturedLength()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=[1,2,3];var replaceIndices='';var replaceCalls=0;"
        "var seen='';var replaceResult=replaced.every(function(value,index,array){"
        "replaceCalls=replaceCalls+1;replaceIndices=replaceIndices+index;"
        "seen=seen+value;if(index===0)array[1]=9;return value<9;});"
        "var removed=[1,2,3];var removeIndices='';var removeCalls=0;"
        "var removeResult=removed.every(function(value,index,array){"
        "removeCalls=removeCalls+1;removeIndices=removeIndices+index;"
        "if(index===0){array.length=1;array.length=3;array[2]=30;}"
        "return value>0;});"
        "var filled=[1,,3];var fillIndices='';var fillCalls=0;"
        "var fillResult=filled.every(function(value,index,array){"
        "fillCalls=fillCalls+1;fillIndices=fillIndices+index;"
        "if(index===0)array[1]=2;return value>0;});"
        "var appended=[4,5];var appendCalls=0;var appendResult="
        "appended.every(function(value,index,array){appendCalls=appendCalls+1;"
        "if(index===0)array[2]=6;return value<6;});"
        "var shrunk=[7,8,9];var shrinkCalls=0;var shrinkResult="
        "shrunk.every(function(value,index,array){shrinkCalls=shrinkCalls+1;"
        "if(index===0)array.length=1;return value>0;});"
        "var current=[5];var currentValue=0;var currentResult=current.every("
        "function(value,index,array){array[0]=99;currentValue=value;return value===5;});"
        "var other=[2,3];var otherResult=[1].every(function(){other[1]=12;"
        "return other[1]===12;});"
        "var mutationOk=replaceResult===false&&replaceCalls===2&&"
        "replaceIndices==='01'&&seen==='19'&&replaced[2]===3&&"
        "removeResult===true&&removeCalls===2&&removeIndices==='02'&&"
        "fillResult===true&&fillCalls===3&&fillIndices==='012'&&"
        "appendResult===true&&appendCalls===2&&appended[2]===6&&"
        "shrinkResult===true&&shrinkCalls===1&&currentResult===true&&"
        "currentValue===5&&current[0]===99&&otherResult===true&&other[1]===12;");
    expect(result.succeeded() && booleanIs(runtime, "mutationOk"),
        "every observes live in-range mutation, skips removals, captures length, and keeps the current value snapshot");

    RuntimeContext stopBeforeLaterCallbacks;
    const ScriptResult stopped = run(stopBeforeLaterCallbacks,
        "var source=[1,2,3];var calls=0;var effects=0;"
        "var result=source.every(function(value,index,array){calls=calls+1;"
        "effects=effects+1;array[1]=9;return false;});"
        "var preserved=source[1]===9&&source[2]===3;");
    expect(stopped.succeeded() &&
        booleanIs(stopBeforeLaterCallbacks, "result", false) &&
        numberIs(stopBeforeLaterCallbacks, "calls", 1.0) &&
        numberIs(stopBeforeLaterCallbacks, "effects", 1.0) &&
        booleanIs(stopBeforeLaterCallbacks, "preserved"),
        "normal false-result termination retains completed side effects and skips all later callback work");
}

void testNestedTraversalIsolationAndEvents()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var outerCalls=0;var innerCalls=0;var innerFailureObserved=false;"
        "var nested=[[1,2],[3,4],[5]].every(function(values){"
        "outerCalls=outerCalls+1;var allSmall=values.every("
        "function(value){innerCalls=innerCalls+1;return value<3;});"
        "if(values[0]===3)innerFailureObserved=!allSmall;return true;});"
        "var everyInSomeCalls=0;var everyInSome=[1,2,3].some(function(value){"
        "everyInSomeCalls=everyInSomeCalls+1;return [value,value+1].every("
        "function(item){return item>0;});});"
        "var someInEveryCalls=0;var someInEvery=[1,2,3].every(function(value){"
        "return [value,value+1].some(function(item){someInEveryCalls=someInEveryCalls+1;"
        "return item===value+1;});});"
        "var mapped=[1,2].map(function(value){var ok=[value,value+1].every("
        "function(item){return item>0;});if(ok)return value*2;return 0;});"
        "var filtered=[0,1,2].filter(function(value){return [value+1].every("
        "function(item){return item>1;});});"
        "var eachCalls=0;[1,2,3].forEach(function(value){var inner=[value,3].every("
        "function(item){eachCalls=eachCalls+1;return item<3;});"
        "if(!inner)eachCalls=eachCalls+1;});"
        "var nestedMethods=[[1,2],[3,4]].every(function(items){"
        "var mappedInner=items.map(function(value){return value*2;});"
        "var filteredInner=mappedInner.filter(function(value){return value>2;});"
        "var visited=0;mappedInner.forEach(function(){visited=visited+1;});"
        "return mappedInner.length===2&&filteredInner.length>0&&visited===2;});"
        "var mapOk=mapped.length===2&&mapped[0]===2&&mapped[1]===4;"
        "var filterOk=filtered.length===2&&filtered[0]===1&&filtered[1]===2;"
        "var eachOk=eachCalls===8;var methodsOk=nestedMethods===true;"
        "var outerStopCalls=0;var outerStop=[1,2,3].every(function(value){"
        "outerStopCalls=outerStopCalls+1;return value<2;});"
        "var nestedOk=nested===true&&outerCalls===3&&innerCalls===4&&"
        "innerFailureObserved&&"
        "outerStop===false&&outerStopCalls===2&&"
        "everyInSome===true&&everyInSomeCalls===1&&someInEvery===true&&"
        "someInEveryCalls===6&&mapOk&&filterOk&&eachOk&&methodsOk;");
    if (!result.succeeded() || !booleanIs(runtime, "nestedOk")) {
        std::cerr << "  JS97 nested diagnostic: status="
            << static_cast<unsigned>(result.status) << " error="
            << static_cast<unsigned>(result.runtimeError.code)
            << " parse=" << static_cast<unsigned>(result.parserError.code)
            << " at=" << result.parserError.location.offset
            << " nested=" << booleanIs(runtime, "nested")
            << " outerCalls=" << numberIs(runtime, "outerCalls", 3.0)
            << " innerCalls=" << numberIs(runtime, "innerCalls", 4.0)
            << " innerFailure=" << booleanIs(runtime, "innerFailureObserved")
            << " everyInSome=" << booleanIs(runtime, "everyInSome")
            << " someInEvery=" << booleanIs(runtime, "someInEvery")
            << " mapped=" << booleanIs(runtime, "mapOk")
            << " filtered=" << booleanIs(runtime, "filterOk")
            << " each=" << numberIs(runtime, "eachCalls", 8.0)
            << " methods=" << booleanIs(runtime, "nestedMethods") << '\n';
    }
    expect(result.succeeded() && booleanIs(runtime, "nestedOk"),
        "nested every, some, map, filter, and forEach calls keep independent stop state");

    std::ifstream input("navigator-smoke/javascript-js97.html",
        std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js97-every.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult hosted = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    expect(loaded && loadError == RuntimeErrorCode::None && hosted.succeeded() &&
        markerText.find("JS97:assertions=14:failures=0") == 0u,
        "hosted JS97 fixture passes representative every callback assertions");
    if (!loaded || !hosted.succeeded() || markerText.empty()) {
        std::cerr << "  JS97 fixture diagnostic: loaded=" << loaded
            << " loadError=" << static_cast<unsigned>(loadError)
            << " status=" << static_cast<unsigned>(hosted.status)
            << " runtimeError=" << static_cast<unsigned>(hosted.runtimeError.code)
            << " parserError=" << static_cast<unsigned>(hosted.parserError.code)
            << " parserOffset=" << hosted.parserError.location.offset
            << " sourceLength=" << script.size()
            << " marker=" << markerText << '\n';
    }
    expect(markerText.find(":shortCircuit=false:sparse=true:nested=true") !=
            std::string::npos,
        "hosted JS97 marker records false short-circuiting, sparse success, and nesting");

    HostInstanceId eventA = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "every-event-a") eventA = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = eventA != 0u && harness.dispatchClick(eventA, eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Event dispatch keeps inner and outer every stop decisions independent");
    if (!clicked || !booleanIs(harness.runtime(), "nestedEventOk") ||
        !booleanIs(harness.runtime(), "outerEventOk")) {
        std::cerr << "  JS97 event diagnostic: clicked=" << clicked
            << " error=" << static_cast<unsigned>(eventError)
            << " nested=" << booleanIs(harness.runtime(), "nestedEventOk")
            << " outer=" << booleanIs(harness.runtime(), "outerEventOk") << '\n';
    }
}

void testValidationErrorsAndLimits()
{
    const char* invalidCallbacks[] = {
        "var values=[];values.every();",
        "var values=[];values.every(undefined);",
        "var values=[];values.every(null);",
        "var values=[];values.every('callback');",
        "var values=[];values.every({});",
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
            unsupportedReceiver, "source"),
            unsupportedReceiver.arrayPrototypeId());
    const ScriptResult receiver = linked
        ? runSameRealm(unsupportedReceiver,
            "source.every(function(){return true;});") : ScriptResult();
    expect(receiver.status == ScriptStatus::RuntimeFailure &&
        receiver.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "generic array-like receivers are rejected");

    RuntimeLimits stepLimits;
    stepLimits.maxExecutionSteps = 48u;
    RuntimeContext stepFailure(stepLimits);
    const ScriptResult budget = run(stepFailure,
        "[1].every(function(){while(true){}});");
    expect(budget.status == ScriptStatus::ExecutionBudgetExceeded &&
        budget.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded,
        "every propagates execution-budget exhaustion during callback execution");

    RuntimeLimits depthLimits;
    depthLimits.maxCallDepth = 1u;
    RuntimeContext depthFailure(depthLimits);
    const ScriptResult depth = run(depthFailure,
        "[1].every(function(){return true;});");
    expect(depth.status == ScriptStatus::RuntimeFailure &&
        depth.runtimeError.code == RuntimeErrorCode::CallDepthExceeded,
        "every callback invocation retains the runtime call-depth limit");

    RuntimeContext noResultArray;
    const ScriptResult setup = run(noResultArray,
        "var source=[1,2,3];var calls=0;var result;");
    const std::size_t objectsBefore =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(noResultArray);
    const std::size_t slotsBefore = noResultArray.arrayElementCount();
    const ScriptResult checked = setup.succeeded()
        ? runSameRealm(noResultArray,
            "result=source.every(function(value){calls=calls+1;return value<2;});")
        : ScriptResult();
    expect(checked.succeeded() && booleanIs(noResultArray, "result", false) &&
        numberIs(noResultArray, "calls", 2.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(noResultArray) ==
            objectsBefore && noResultArray.arrayElementCount() == slotsBefore,
        "every creates no result Array and retains no mapped-value slots");

    RuntimeContext overflow;
    const ScriptResult tooLong = run(overflow,
        "var source=[];source.length=1025;source.every(function(){return true;});");
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
        "every does not change RuntimeObject or Array element record sizes");

    RuntimeContext maximumTrue;
    const ScriptResult allTrue = run(maximumTrue,
        denseArraySource(1024u) +
        "var calls=0;var result=source.every(function(){calls=calls+1;return 1;});");
    expect(allTrue.succeeded() && booleanIs(maximumTrue, "result") &&
        numberIs(maximumTrue, "calls", 1024.0) &&
        maximumTrue.arrayElementCount() == 1024u,
        "1,024-position all-true traversal visits every present value");

    RuntimeContext maximumEarly;
    const ScriptResult early = run(maximumEarly,
        denseArraySource(1024u) +
        "var calls=0;var result=source.every(function(value){calls=calls+1;"
        "return value!==5;});");
    expect(early.succeeded() && booleanIs(maximumEarly, "result", false) &&
        numberIs(maximumEarly, "calls", 6.0) &&
        maximumEarly.arrayElementCount() == 1024u,
        "maximum-length early false visits six values and skips the remaining 1,018");

    RuntimeContext sparseMaximum;
    const ScriptResult sparse = run(sparseMaximum,
        "var source=[];source.length=1024;var calls=0;"
        "var holes=source.every(function(){calls=calls+1;return false;});"
        "source[1023]=55;var all=source.every(function(value,index){"
        "calls=calls+1;return index===1023&&value===55;});");
    expect(sparse.succeeded() && booleanIs(sparseMaximum, "holes") &&
        booleanIs(sparseMaximum, "all") &&
        numberIs(sparseMaximum, "calls", 1.0) &&
        sparseMaximum.arrayElementCount() == 1u,
        "sparse maximum-length all-hole and single-present traversals stay bounded");
}

} // namespace

int main()
{
    testBasicBehaviorAndBuiltin();
    testThisArgTruthinessAndShortCircuit();
    testSparseAndInheritedProperties();
    testLiveMutationAndCapturedLength();
    testNestedTraversalIsolationAndEvents();
    testValidationErrorsAndLimits();
    testMaximumBoundaries();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS97 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS97 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
