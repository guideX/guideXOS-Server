#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <fstream>
#include <initializer_list>
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
    const std::string& expected)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isString() &&
        runtime.stringValue(*value) == expected;
}

void diagnose(const char* label, const RuntimeContext& runtime,
    const ScriptResult& result,
    std::initializer_list<const char*> names)
{
    std::cerr << "  JS102 diagnostic " << label << ": status="
        << static_cast<unsigned>(result.status) << " error="
        << static_cast<unsigned>(result.runtimeError.code) << " parse="
        << static_cast<unsigned>(result.parserError.code) << " parseOffset="
        << result.parserError.location.offset;
    for (const char* name : names) {
        const Value* value = binding(runtime, name);
        std::cerr << ' ' << name << '=';
        if (value == nullptr) {
            std::cerr << "<missing>";
        } else if (value->isNumber()) {
            std::cerr << value->numberValue();
        } else if (value->isBoolean()) {
            std::cerr << (value->booleanValue() ? "true" : "false");
        } else if (value->isString()) {
            std::cerr << runtime.stringValue(*value);
        } else if (value->isUndefined()) {
            std::cerr << "undefined";
        } else if (value->isNull()) {
            std::cerr << "null";
        } else if (value->isObject()) {
            std::cerr << "object#" << value->objectId();
        } else {
            std::cerr << "other";
        }
    }
    std::cerr << '\n';
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

void testBasicSearchAndCallbackContract()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var numbers=[5,12,50,130,44];var order='';var calls=0;"
        "var found=numbers.findLast(function(value,index,array){calls=calls+1;"
        "order=order+index;return value>45&&array===numbers;});"
        "var middle=[10,20,30].findLast(function(v){return v===20;});"
        "var low=[1,2,3].findLast(function(v){return v===1;});"
        "var multi=[7,8,7,9,8].findLast(function(v){return v===7||v===8;});"
        "var missCalls=0;var miss=[1,2,3].findLast(function(){"
        "missCalls=missCalls+1;return false;});"
        "var emptyCalls=0;var empty=[].findLast(function(){emptyCalls=emptyCalls+1;});"
        "var one=[9].findLast(function(v){return 'truthy';});"
        "var two=[1,2].findLast(function(v){return v===2;});"
        "var context={target:12};var thisCalls=0;var thisOk=true;var thisSource=[10,12,20];"
        "var thisValue=thisSource.findLast(function(v,i,a){thisCalls=thisCalls+1;"
        "thisOk=thisOk&&this===context&&a===thisSource;return v===this.target;},context);"
        "var valid=found===130&&order==='43'&&calls===2&&middle===20&&"
        "low===1&&multi===8&&miss===undefined&&missCalls===3&&"
        "empty===undefined&&emptyCalls===0&&one===9&&two===2&&"
        "thisValue===12&&thisCalls===2&&thisOk;");
    const bool basicOk = result.succeeded() && booleanIs(runtime, "valid");
    if (!basicOk) diagnose("basic", runtime, result,
        {"found", "order", "calls", "middle", "low", "multi", "missCalls",
            "emptyCalls", "thisValue", "thisCalls", "thisOk", "valid"});
    expect(basicOk,
        "findLast selects the highest matching index and forwards arguments, thisArg, and truthiness");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime,
            "findLast"),
        "findLast is installed as a writable-only native Array prototype method");
    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    expect(runtime.readPropertyForTesting(runtime.arrayPrototypeId(),
            "findLast", method, methodError) && method.isFunction() &&
        methodError == RuntimeErrorCode::None,
        "Array.prototype exposes findLast as a callable native function");
}

void testSparseSlotsAndUndefinedMatch()
{
    RuntimeContext inherited;
    const ScriptResult setup = run(inherited,
        "var source=[];source.length=4;source[1]=20;var indices='';"
        "var values='';var calls=0;var noMatch;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "2", Value::number(42.0));
    const ScriptResult visited = installed ? runSameRealm(inherited,
        "noMatch=source.findLast(function(value,index,array){calls=calls+1;"
        "indices=indices+index;if(value===undefined)values=values+'U,';"
        "else values=values+value+',';"
        "return false;});") : ScriptResult();
    const bool sparseOk = visited.succeeded() && numberIs(inherited, "calls", 4.0) &&
        stringIs(inherited, "indices", "3210") &&
        stringIs(inherited, "values", "U,42,20,U,") &&
        binding(inherited, "noMatch") != nullptr &&
        binding(inherited, "noMatch")->isUndefined();
    if (!sparseOk) diagnose("sparse", inherited, visited,
        {"calls", "indices", "values", "noMatch"});
    expect(sparseOk,
        "findLast visits trailing, middle, and leading holes in descending order and resolves inherited numeric values");

    RuntimeContext allHoles;
    const ScriptResult allHolesResult = run(allHoles,
        "var source=[];source.length=5;var indices='';var calls=0;"
        "var result=source.findLast(function(value,index){calls=calls+1;"
        "indices=indices+index;return false;});"
        "var reduceCalls=0;source.reduceRight(function(a){reduceCalls=reduceCalls+1;return a;},0);");
    expect(allHolesResult.succeeded() && numberIs(allHoles, "calls", 5.0) &&
        stringIs(allHoles, "indices", "43210") &&
        binding(allHoles, "result") != nullptr &&
        binding(allHoles, "result")->isUndefined() &&
        numberIs(allHoles, "reduceCalls", 0.0),
        "all-hole findLast visits every position while reduceRight retains its skip-holes policy");

    RuntimeContext undefinedMatch;
    const ScriptResult undefinedResult = run(undefinedMatch,
        "var source=[10,undefined,30];var indices='';var calls=0;"
        "var result=source.findLast(function(value,index){calls=calls+1;"
        "indices=indices+index;return index===1;});"
        "var explicit=[10,undefined,30];var explicitCalls=0;"
        "var explicitResult=explicit.findLast(function(value){"
        "explicitCalls=explicitCalls+1;return value===undefined;});"
        "var hole=[];hole.length=3;var holeCalls=0;var holeResult=hole.findLast(function(v){"
        "holeCalls=holeCalls+1;return v===undefined;});"
        "var allFalseCalls=0;[10,undefined,30].findLast(function(){"
        "allFalseCalls=allFalseCalls+1;return false;});");
    expect(undefinedResult.succeeded() &&
        binding(undefinedMatch, "result") != nullptr &&
        binding(undefinedMatch, "result")->isUndefined() &&
        stringIs(undefinedMatch, "indices", "21") &&
        numberIs(undefinedMatch, "calls", 2.0),
        "a successful undefined match stops after indexes two and one, unlike normal exhaustion");
    expect(binding(undefinedMatch, "explicitResult") != nullptr &&
        binding(undefinedMatch, "explicitResult")->isUndefined() &&
        numberIs(undefinedMatch, "explicitCalls", 2.0) &&
        binding(undefinedMatch, "holeResult") != nullptr &&
        binding(undefinedMatch, "holeResult")->isUndefined() &&
        numberIs(undefinedMatch, "holeCalls", 1.0) &&
        numberIs(undefinedMatch, "allFalseCalls", 3.0),
        "explicit undefined and missing-slot matches return undefined but short-circuit distinctly from no match");

    RuntimeContext crossMethods;
    const ScriptResult cross = run(crossMethods,
        "var source=[3,7,12,7,20];var forwardIndices='';var reverseIndices='';"
        "var first=source.find(function(v,i){forwardIndices=forwardIndices+i;return v===7;});"
        "var last=source.findLast(function(v,i){reverseIndices=reverseIndices+i;return v===7;});"
        "var index=source.findIndex(function(v){return v===12;});"
        "var found=source.findLast(function(v){return v===12;});");
    expect(cross.succeeded() && numberIs(crossMethods, "first", 7.0) &&
        numberIs(crossMethods, "last", 7.0) &&
        stringIs(crossMethods, "forwardIndices", "01") &&
        stringIs(crossMethods, "reverseIndices", "43") &&
        numberIs(crossMethods, "index", 2.0) &&
        numberIs(crossMethods, "found", 12.0),
        "find stays forward, findLast searches backward, findIndex returns an index, and findLast returns a value");
}

void testLiveReverseMutation()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=[1,2,3];var replacedCalls=0;var replacedResult=replaced.findLast(function(v,i,s){"
        "replacedCalls=replacedCalls+1;if(i===2)s[1]=20;return v===20;});"
        "var filled=[];filled.length=3;filled[2]=3;var fillCalls=0;"
        "var fillResult=filled.findLast(function(v,i,s){fillCalls=fillCalls+1;"
        "if(i===2)s[1]=20;return v===20;});"
        "var removed=[1,2,3];var removeCalls=0;var removeIndices='';"
        "var removeResult=removed.findLast(function(v,i,s){removeCalls=removeCalls+1;"
        "removeIndices=removeIndices+i;if(i===2){s.pop();s.pop();}"
        "return i===1&&v===undefined;});"
        "var shrunk=[1,2,3];var shrinkCalls=0;var shrinkResult=shrunk.findLast(function(v,i,s){"
        "shrinkCalls=shrinkCalls+1;if(i===2)s.length=1;return i===1&&v===undefined;});"
        "var appended=[1,2,3];var appendCalls=0;var appendIndices='';"
        "var appendResult=appended.findLast(function(v,i,s){appendCalls=appendCalls+1;"
        "appendIndices=appendIndices+i;if(i===2)s.push(4);return false;});"
        "var current=[10,20,30];var currentResult=current.findLast(function(v,i,s){"
        "if(i===2)s[i]=999;return true;});"
        "var higher=[10,20,30];var higherCalls=0;var higherResult=higher.findLast(function(v,i,s){"
        "higherCalls=higherCalls+1;if(i===1)s[2]=999;return i===1;});"
        "var other=[7,8];var otherResult=[1,2].findLast(function(v,i){"
        "if(i===1)other[1]=80;return false;});"
        "var mutationsOk=replacedResult===20&&replacedCalls===2&&fillResult===20&&"
        "fillCalls===2&&removeResult===undefined&&removeCalls===2&&removeIndices==='21'&&"
        "shrinkResult===undefined&&shrinkCalls===2&&appendResult===undefined&&"
        "appendCalls===3&&appendIndices==='210'&&currentResult===30&&current[2]===999&&"
        "higherResult===20&&higherCalls===2&&higher[2]===999&&other[1]===80;");
    const bool mutationOk = result.succeeded() && booleanIs(runtime, "mutationsOk");
    if (!mutationOk) diagnose("mutation", runtime, result,
        {"replacedResult", "replacedCalls", "fillResult", "fillCalls",
            "removeResult", "removeCalls", "removeIndices", "shrinkResult",
            "shrinkCalls", "appendResult", "appendCalls", "appendIndices",
            "currentResult", "higherResult", "higherCalls", "otherResult",
            "mutationsOk"});
    expect(mutationOk,
        "reverse search reads each slot live after lower-index replacement, fill, deletion, shrink, append, and unrelated mutation");

    RuntimeContext inheritedMutation;
    const ScriptResult setup = run(inheritedMutation,
        "var source=[];source.length=3;source[2]=30;var result;var calls=0;");
    const bool installed = setup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(
            inheritedMutation, "1", Value::number(10.0));
    const ScriptResult inheritedResult = installed ? runSameRealm(
        inheritedMutation,
        "result=source.findLast(function(v,i,s){calls=calls+1;"
        "if(i===2)s[1]=20;return v===20;});") : ScriptResult();
    expect(inheritedResult.succeeded() &&
        numberIs(inheritedMutation, "result", 20.0) &&
        numberIs(inheritedMutation, "calls", 2.0),
        "a lower own replacement is observed at its turn even when an inherited numeric property was present");
}

void testCapturedValueOwnership()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var numberSource=[10];var numberResult=numberSource.findLast(function(v,i,s){s[i]=99;return true;});"
        "var stringResult=['text'].findLast(function(){return true;});"
        "var booleanResult=[false].findLast(function(){return true;});"
        "var nullResult=[null].findLast(function(){return true;});"
        "var undefinedResult=[undefined].findLast(function(){return true;});"
        "var object={id:7};var objectSource=[object];var objectResult=objectSource.findLast(function(v,i,s){s[i]=null;return true;});"
        "var array=[];array.push(1);var arraySource=[array];var arrayResult=arraySource.findLast(function(v,i,s){s.length=0;return true;});"
        "var nestedSource=[{id:9}];var nestedObject=nestedSource[0];"
        "var nestedResult=nestedSource.findLast(function(v){var scratch=[{},[]];"
        "return scratch.length===2;});"
        "var ownershipOk=numberResult===10&&numberSource[0]===99&&stringResult==='text'&&"
        "booleanResult===false&&nullResult===null&&undefinedResult===undefined&&"
        "objectResult===object&&objectSource[0]===null&&arrayResult===array&&"
        "array.length===1&&arraySource.length===0&&arraySource[0]===undefined&&"
        "nestedResult===nestedObject;");
    const bool ownershipOk = result.succeeded() && booleanIs(runtime, "ownershipOk");
    if (!ownershipOk) diagnose("ownership", runtime, result,
        {"numberResult", "stringResult", "booleanResult", "nullResult",
            "undefinedResult", "objectResult", "arrayResult", "nestedResult",
            "ownershipOk"});
    expect(ownershipOk,
        "findLast retains primitive, object, and Array source values across overwrite, deletion, and nested allocation");
    expect(objectId(runtime, "objectResult") == objectId(runtime, "object") &&
        objectId(runtime, "arrayResult") == objectId(runtime, "array") &&
        objectId(runtime, "nestedResult") == objectId(runtime, "nestedObject"),
        "captured object and Array values preserve stable runtime identity");
}

void testNestedTraversalAndErrors()
{
    RuntimeContext runtime;
    const ScriptResult nested = run(runtime,
        "var innerCalls=0;var outerCalls=0;var nested=[1,2,3].findLast(function(v,i){"
        "outerCalls=outerCalls+1;var inner=[v,9].findLast(function(x){innerCalls=innerCalls+1;return x===v;});"
        "return inner===2;});"
        "var inFind=[1,2].find(function(v){return [v,9].findLast(function(x){return x===v;})===2;});"
        "var findInLast=[1,2].findLast(function(v){return [v,9].find(function(x){return x===v;})===2;});"
        "var findIndexInLast=[1,2].findLast(function(v){return [v,9].findIndex(function(x){return x===v;})===0;});"
        "var inReduce=[1,2].reduce(function(a,v){return [a,v].findLast(function(x){return x===v;});},0);"
        "var inRight=[1,2].reduceRight(function(a,v){return [a,v].findLast(function(x){return x===v;});},0);"
        "var inMap=[1,2].map(function(v){return [v,9].findLast(function(x){return x===v;});});"
        "var inFilter=[1,2,3].filter(function(v){return [v,9].findLast(function(x){return x===v;})>1;});"
        "var inSome=[1,2].some(function(v){return [v,9].findLast(function(x){return x===v;})===2;});"
        "var inEvery=[1,2].every(function(v){return [v,9].findLast(function(x){return x===v;})>0;});"
        "var each=0;[1,2].forEach(function(v){each=each+[v,9].findLast(function(x){return x===v;});});"
        "var nestedOk=nested===2&&outerCalls===2&&innerCalls===4&&inFind===2&&"
        "findInLast===2&&findIndexInLast===2&&inReduce===2&&inRight===1&&"
        "inMap.length===2&&inMap[0]===1&&inMap[1]===2&&inFilter.length===2&&"
        "inFilter[0]===2&&inFilter[1]===3&&inSome&&inEvery&&each===3;");
    expect(nested.succeeded() && booleanIs(runtime, "nestedOk"),
        "nested findLast, find, findIndex, reduce, reduceRight, map, filter, some, every, and forEach retain independent state");

    RuntimeContext beforeMatchError;
    const ScriptResult before = run(beforeMatchError,
        "var calls=0;[1,2,3].findLast(function(v,i){calls=calls+1;"
        "if(i===1)null.absent;return v===1;});");
    expect(before.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        numberIs(beforeMatchError, "calls", 2.0) &&
        beforeMatchError.activeCallFrameCount() == 0u,
        "callback errors before a lower-index match propagate and unwind frames");

    RuntimeContext shortCircuit;
    const ScriptResult shortRun = run(shortCircuit,
        "var calls=0;var result=[1,2,3].findLast(function(v,i){calls=calls+1;"
        "if(i===0)null.absent;return i===2;});");
    expect(shortRun.succeeded() && numberIs(shortCircuit, "result", 3.0) &&
        numberIs(shortCircuit, "calls", 1.0),
        "a highest-index match suppresses later callbacks and hypothetical errors at lower indexes");

    RuntimeContext nestedError;
    const ScriptResult nestedFailure = run(nestedError,
        "[1].findLast(function(){return [2].findLast(function(){return null.value;});});");
    expect(nestedFailure.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        nestedError.activeCallFrameCount() == 0u,
        "nested callback failure propagates through both reverse-search frames");

    RuntimeLimits budgetLimits;
    budgetLimits.maxExecutionSteps = 48u;
    RuntimeContext budget(budgetLimits);
    const ScriptResult budgetFailure = run(budget,
        "[1].findLast(function(){while(true){};});");
    expect(budgetFailure.status == ScriptStatus::ExecutionBudgetExceeded &&
        budgetFailure.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded &&
        budget.activeCallFrameCount() == 0u,
        "findLast callback execution preserves the execution-step budget");

    RuntimeLimits depthLimits;
    depthLimits.maxCallDepth = 1u;
    RuntimeContext depth(depthLimits);
    const ScriptResult depthFailure = run(depth,
        "[1].findLast(function(){return 1;});");
    expect(depthFailure.runtimeError.code == RuntimeErrorCode::CallDepthExceeded &&
        depth.activeCallFrameCount() == 0u,
        "findLast callback invocation preserves the call-depth limit");

    RuntimeContext invalid;
    const char* invalidCalls[] = {
        "[].findLast();", "[].findLast(undefined);", "[].findLast(null);",
        "[].findLast('callback');", "[].findLast({});"
    };
    bool callbacksRejected = true;
    for (const char* source : invalidCalls) {
        RuntimeContext individual;
        const ScriptResult rejected = run(individual, source);
        callbacksRejected = callbacksRejected &&
            rejected.runtimeError.code == RuntimeErrorCode::NotCallable;
    }
    const ScriptResult receiver = run(invalid,
        "var a={findLast:[].findLast};a.findLast(function(){return true;});");
    expect(callbacksRejected &&
        receiver.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "callback validation occurs for empty sources and non-Array receivers are rejected");
}

void testHostedFixtureAndEvents()
{
    std::ifstream input("navigator-smoke/javascript-js102.html",
        std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js102-find-last.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult hosted = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    const bool hostedOk = loaded && loadError == RuntimeErrorCode::None && hosted.succeeded() &&
        markerText.find("JS102:assertions=16:failures=0") == 0u &&
        markerText.find(":reverse=true:sparse=true:mutation=true:ownership=true") !=
            std::string::npos;
    if (!hostedOk) std::cerr << "  JS102 hosted diagnostic: loaded=" << loaded
        << " loadError=" << static_cast<unsigned>(loadError)
        << " status=" << static_cast<unsigned>(hosted.status)
        << " error=" << static_cast<unsigned>(hosted.runtimeError.code)
        << " parse=" << static_cast<unsigned>(hosted.parserError.code)
        << " marker=" << markerText << '\n';
    expect(hostedOk,
        "hosted JS102 fixture covers reverse order, sparse slots, mutation, and retained values");

    HostInstanceId eventA = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "find-last-event-a") eventA = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = eventA != 0u && harness.dispatchClick(eventA, eventError);
    const bool eventOk = clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "innerEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk");
    if (!eventOk) std::cerr << "  JS102 Event diagnostic: click=" << clicked
        << " error=" << static_cast<unsigned>(eventError)
        << " nested=" << booleanIs(harness.runtime(), "nestedEventOk")
        << " outer=" << booleanIs(harness.runtime(), "outerEventOk")
        << " innerCalls=" << (binding(harness.runtime(), "innerEventCalls") != nullptr
            ? binding(harness.runtime(), "innerEventCalls")->numberValue() : -1.0)
        << " innerIndices=" << (binding(harness.runtime(), "innerIndices") != nullptr
            ? harness.runtime().stringValue(*binding(harness.runtime(), "innerIndices")) : "<missing>")
        << " outerCalls=" << (binding(harness.runtime(), "outerEventCalls") != nullptr
            ? binding(harness.runtime(), "outerEventCalls")->numberValue() : -1.0)
        << " outerIndices=" << (binding(harness.runtime(), "outerIndices") != nullptr
            ? harness.runtime().stringValue(*binding(harness.runtime(), "outerIndices")) : "<missing>")
        << '\n';
    expect(eventOk,
        "nested Event dispatch retains independent findLast results and indices");

    HostInstanceId errorEvent = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "find-last-error-event") errorEvent = element.serial;
    }
    RuntimeErrorCode callbackError = RuntimeErrorCode::None;
    const bool errorClicked = errorEvent != 0u &&
        harness.dispatchClick(errorEvent, callbackError);
    const bool eventErrorOk = !errorClicked &&
        callbackError == RuntimeErrorCode::CannotReadProperty &&
        harness.runtime().activeCallFrameCount() == 0u;
    if (!eventErrorOk) std::cerr << "  JS102 Event error diagnostic: click="
        << errorClicked << " error=" << static_cast<unsigned>(callbackError)
        << " frames=" << harness.runtime().activeCallFrameCount() << '\n';
    expect(eventErrorOk,
        "findLast callback errors propagate through hosted Event cleanup");
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

void testBoundariesAndStorage()
{
    RuntimeContext empty;
    const ScriptResult emptyResult = run(empty,
        "var calls=0;var result=[].findLast(function(){calls=calls+1;return true;});");
    expect(emptyResult.succeeded() &&
        binding(empty, "result") != nullptr && binding(empty, "result")->isUndefined() &&
        numberIs(empty, "calls", 0.0),
        "length zero returns undefined with no callback and validates the callable");

    RuntimeContext denseNoMatch;
    const ScriptResult denseNoMatchRun = run(denseNoMatch,
        denseArraySource(1024u) +
        "var calls=0;var expected=1023;var orderOk=true;"
        "var result=source.findLast(function(v,i){calls=calls+1;"
        "if(i!==expected)orderOk=false;expected=expected-1;return false;});");
    RuntimeContext denseHighest;
    const ScriptResult denseHighestRun = run(denseHighest,
        denseArraySource(1024u) +
        "var calls=0;var result=source.findLast(function(v,i){calls=calls+1;return i===1023;});");
    RuntimeContext denseLowest;
    const ScriptResult denseLowestRun = run(denseLowest,
        denseArraySource(1024u) +
        "var calls=0;var result=source.findLast(function(v,i){calls=calls+1;return i===0;});");
    const bool denseOk = denseNoMatchRun.succeeded() &&
        numberIs(denseNoMatch, "calls", 1024.0) &&
        numberIs(denseNoMatch, "expected", -1.0) &&
        booleanIs(denseNoMatch, "orderOk") &&
        binding(denseNoMatch, "result") != nullptr &&
        binding(denseNoMatch, "result")->isUndefined() &&
        denseHighestRun.succeeded() && numberIs(denseHighest, "calls", 1.0) &&
        numberIs(denseHighest, "result", 1023.0) &&
        denseLowestRun.succeeded() && numberIs(denseLowest, "calls", 1024.0) &&
        numberIs(denseLowest, "result", 0.0);
    if (!denseOk) diagnose("dense-no-match", denseNoMatch, denseNoMatchRun,
        {"calls", "expected", "orderOk", "result"});
    expect(denseOk,
        "maximum dense source completes at index zero with exact no-match, highest-match, and lowest-match counts");
    expect(NavigatorScriptRuntimeTestAccess::runtimeObjectCount(denseNoMatch) >= 1u,
        "maximum dense search keeps runtime records valid without allocating a result Array");

    RuntimeContext sparseNoMatch;
    const ScriptResult sparseRun = run(sparseNoMatch,
        "var source=[];source.length=1024;var calls=0;var expected=1023;var orderOk=true;"
        "var result=source.findLast(function(v,i){calls=calls+1;"
        "if(i!==expected)orderOk=false;expected=expected-1;return false;});");
    RuntimeContext sparseHighest;
    const ScriptResult sparseHighestRun = run(sparseHighest,
        "var source=[];source.length=1024;var calls=0;"
        "var result=source.findLast(function(){calls=calls+1;return true;});");
    expect(sparseRun.succeeded() && numberIs(sparseNoMatch, "calls", 1024.0) &&
        booleanIs(sparseNoMatch, "orderOk") &&
        numberIs(sparseNoMatch, "expected", -1.0) &&
        binding(sparseNoMatch, "result") != nullptr &&
        binding(sparseNoMatch, "result")->isUndefined() &&
        sparseHighestRun.succeeded() && numberIs(sparseHighest, "calls", 1.0) &&
        binding(sparseHighest, "result") != nullptr &&
        binding(sparseHighest, "result")->isUndefined(),
        "maximum all-hole source visits all indexes, returns undefined on exhaustion, and stops on its first hole match");

    RuntimeContext tooLong;
    const ScriptResult rejected = run(tooLong,
        "var source=[];var calls=0;source.length=1025;"
        "source.findLast(function(){calls=calls+1;return true;});");
    expect(rejected.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        numberIs(tooLong, "calls", 0.0),
        "existing 1,025-position Array rejection remains in force");

    RuntimeContext storage;
    const ScriptResult setup = run(storage,
        "var source=[{id:1},2,3];var result;var calls=0;");
    const std::size_t objectsBefore =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(storage);
    const std::size_t slotsBefore = storage.arrayElementCount();
    const ScriptResult search = setup.succeeded() ? runSameRealm(storage,
        "result=source.findLast(function(v){calls=calls+1;return v===2;});")
        : ScriptResult();
    expect(search.succeeded() && numberIs(storage, "result", 2.0) &&
        numberIs(storage, "calls", 2.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(storage) == objectsBefore &&
        storage.arrayElementCount() == slotsBefore,
        "successful search allocates no result Array or persistent staging slots");
    expect(NavigatorScriptRuntimeTestAccess::runtimeObjectSize() == 88u &&
        NavigatorScriptRuntimeTestAccess::arrayElementSize() == 48u,
        "findLast leaves RuntimeObject and RuntimeArrayElement layouts unchanged");
}

} // namespace

int main()
{
    testBasicSearchAndCallbackContract();
    testSparseSlotsAndUndefinedMatch();
    testLiveReverseMutation();
    testCapturedValueOwnership();
    testNestedTraversalAndErrors();
    testHostedFixtureAndEvents();
    testBoundariesAndStorage();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS102 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS102 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
