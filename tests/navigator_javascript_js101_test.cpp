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
    const std::string& expected)
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

void testOrderArgumentsAndInitialValue()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var source=['A','B','C'];var indices='';var calls=0;var argsOk=true;"
        "var reversed=source.reduceRight(function(acc,value,index,array,extra){"
        "calls=calls+1;indices=indices+index;argsOk=argsOk&&array===source&&"
        "extra===undefined&&((index===2&&acc===''&&value==='C')||"
        "(index===1&&acc==='C'&&value==='B')||(index===0&&acc==='CB'&&value==='A'));"
        "return acc+value;},'');"
        "var omittedCalls=0;var omitted=[2,4,6].reduceRight(function(acc,value){"
        "omittedCalls=omittedCalls+1;return acc-value;});"
        "var explicitCalls=0;var explicit=[5].reduceRight(function(acc,value){"
        "explicitCalls=explicitCalls+1;if(acc===undefined)return value;return acc;},undefined);"
        "var absentCalls=0;var single=[5].reduceRight(function(){absentCalls=absentCalls+1;});"
        "var falsyCalls=0;var falsy=[1,2,3].reduceRight(function(acc,value,index){"
        "falsyCalls=falsyCalls+1;if(index===2)return 0;if(index===1)return false;return null;},9);"
        "var undefinedCalls=0;var sawUndefined=false;var undefinedResult=[1,2,3].reduceRight(function(a,v,i){"
        "undefinedCalls=undefinedCalls+1;if(i===2)return undefined;"
        "if(i===1)sawUndefined=a===undefined;return undefined;},9);");
    expect(result.succeeded() && stringIs(runtime, "reversed", "CBA") &&
        stringIs(runtime, "indices", "210") && numberIs(runtime, "calls", 3.0) &&
        booleanIs(runtime, "argsOk"),
        "callback receives accumulator, value, descending index, original Array, and four arguments");
    expect(numberIs(runtime, "omitted", 0.0) &&
        numberIs(runtime, "omittedCalls", 2.0) &&
        numberIs(runtime, "explicit", 5.0) &&
        numberIs(runtime, "explicitCalls", 1.0),
        "omitted seed is highest present value while explicit undefined is present initialValue");
    expect(numberIs(runtime, "single", 5.0) &&
        numberIs(runtime, "absentCalls", 0.0) &&
        numberIs(runtime, "falsyCalls", 3.0),
        "single present element seeds without callback and falsy callback results do not stop traversal");
    const Value* falsy = binding(runtime, "falsy");
    expect(falsy != nullptr && falsy->isNull(),
        "null callback result remains the final accumulator");
    const Value* undefinedResult = binding(runtime, "undefinedResult");
    expect(undefinedResult != nullptr && undefinedResult->isUndefined() &&
        numberIs(runtime, "undefinedCalls", 3.0) &&
        booleanIs(runtime, "sawUndefined"),
        "undefined callback result continues as the accumulator through remaining indexes");

    RuntimeContext types;
    const ScriptResult values = run(types,
        "var stringSeed='seed';var stringValue=[1].reduceRight(function(a){return a;},stringSeed);"
        "var falseValue=[1].reduceRight(function(a){return a;},false);"
        "var nullValue=[].reduceRight(function(){return 1;},null);"
        "var undefinedValue=[].reduceRight(function(){return 1;},undefined);"
        "var objectSeed={value:7};var objectValue=[1].reduceRight(function(a){return a;},objectSeed);"
        "var arraySeed=[];var arrayValue=[1].reduceRight(function(a){return a;},arraySeed);");
    const Value* stringValue = binding(types, "stringValue");
    const Value* falseValue = binding(types, "falseValue");
    const Value* nullValue = binding(types, "nullValue");
    const Value* undefinedValue = binding(types, "undefinedValue");
    expect(values.succeeded() && stringValue != nullptr &&
        stringValue->isString() && types.stringValue(*stringValue) == "seed" &&
        falseValue != nullptr && falseValue->isBoolean() &&
        !falseValue->booleanValue() && nullValue != nullptr &&
        nullValue->isNull() && undefinedValue != nullptr &&
        undefinedValue->isUndefined() &&
        objectId(types, "objectValue") == objectId(types, "objectSeed") &&
        objectId(types, "arrayValue") == objectId(types, "arraySeed"),
        "primitive and reference initial values retain their exact runtime types and identities");
}

void testSparseInheritedAndEmpty()
{
    RuntimeContext sparse;
    const ScriptResult sparseSetup = run(sparse,
        "var source=[];source.length=5;source[1]=10;source[4]=40;"
        "var indices='';var calls=0;var total;var noInitCalls=0;var noInit;");
    const bool installed = sparseSetup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(sparse,
            "3", Value::number(7.0));
    const ScriptResult result = installed ? runSameRealm(sparse,
        "total=source.reduceRight(function(a,v,i){"
        "calls=calls+1;indices=indices+i;return a+v;},0);"
        "noInit=source.reduceRight(function(a,v,i){"
        "noInitCalls=noInitCalls+1;return a-v;});") : ScriptResult();
    expect(result.succeeded() && numberIs(sparse, "total", 57.0) &&
        stringIs(sparse, "indices", "431") && numberIs(sparse, "calls", 3.0),
        "descending sparse traversal skips holes and visits inherited numeric properties live");
    expect(numberIs(sparse, "noInit", 23.0) &&
        numberIs(sparse, "noInitCalls", 2.0),
        "highest present own property seeds while inherited lower property receives callback");
    const ScriptResult crossMethod = runSameRealm(sparse,
        "var forwardOrder=source.reduce(function(a,v){return a+v+',';},'');"
        "var reverseOrder=source.reduceRight(function(a,v){return a+v+',';},'');");
    expect(crossMethod.succeeded() &&
        stringIs(sparse, "forwardOrder", "10,7,40,") &&
        stringIs(sparse, "reverseOrder", "40,7,10,"),
        "reduce and reduceRight agree on inherited sparse membership and preserve opposite orders");

    RuntimeContext inheritedSeed;
    const ScriptResult inheritedSetup = run(inheritedSeed,
        "var source=[];source.length=5;source[1]=2;var calls=0;var value;");
    const bool highInstalled = inheritedSetup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inheritedSeed,
            "4", Value::number(8.0));
    const ScriptResult inherited = highInstalled ? runSameRealm(inheritedSeed,
        "value=source.reduceRight(function(a,v){calls=calls+1;return a+v;});")
        : ScriptResult();
    expect(inherited.succeeded() && numberIs(inheritedSeed, "value", 10.0) &&
        numberIs(inheritedSeed, "calls", 1.0),
        "inherited highest-index property may seed omitted initialValue");

    RuntimeContext inheritedMutation;
    const ScriptResult inheritedMutationSetup = run(inheritedMutation,
        "var source=[];source.length=3;source[2]=3;var result;var seen='';");
    const bool inheritedMutationInstalled = inheritedMutationSetup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(
            inheritedMutation, "1", Value::number(10.0));
    const ScriptResult inheritedMutationRun = inheritedMutationInstalled
        ? runSameRealm(inheritedMutation,
            "result=source.reduceRight(function(a,v,i,s){seen=seen+v;"
            "if(i===2)s[1]=20;return a+v;},0);")
        : ScriptResult();
    expect(inheritedMutationRun.succeeded() &&
        numberIs(inheritedMutation, "result", 23.0) &&
        stringIs(inheritedMutation, "seen", "320"),
        "lower own write during callback replaces a future inherited numeric lookup");

    RuntimeContext empty;
    const ScriptResult seeded = run(empty,
        "var calls=0;var empty=[].reduceRight(function(){calls=calls+1;},10);"
        "var holes=[];holes.length=4;var holeSeed=holes.reduceRight(function(){"
        "calls=calls+1;},undefined);");
    const Value* holeSeed = binding(empty, "holeSeed");
    expect(seeded.succeeded() && numberIs(empty, "empty", 10.0) &&
        holeSeed != nullptr && holeSeed->isUndefined() &&
        numberIs(empty, "calls", 0.0),
        "empty and all-hole arrays return supplied initialValue without callback");

    RuntimeContext emptyNoSeed;
    const ScriptResult emptyFailure = run(emptyNoSeed,
        "[].reduceRight(function(){return 1;});");
    RuntimeContext holesNoSeed;
    const ScriptResult holeFailure = run(holesNoSeed,
        "var a=[];a.length=5;a.reduceRight(function(){return 1;});");
    expect(emptyFailure.runtimeError.code == RuntimeErrorCode::TypeError &&
        holeFailure.runtimeError.code == RuntimeErrorCode::TypeError,
        "empty and all-hole arrays without initialValue throw TypeError");

    RuntimeContext invalidCallback;
    const ScriptResult invalid = run(invalidCallback,
        "[].reduceRight(undefined,0);");
    expect(invalid.runtimeError.code == RuntimeErrorCode::NotCallable,
        "callback validation happens even when empty input would invoke no callback");
    RuntimeContext invalidReceiver;
    const ScriptResult receiver = run(invalidReceiver,
        "var a={reduceRight:[].reduceRight};a.reduceRight(function(x){return x;},0);");
    expect(receiver.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "reduceRight rejects a non-Array receiver");
}

void testReverseMutation()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=[1,2,3];var seen='';var replacedResult=replaced.reduceRight(function(a,v,i,s){"
        "seen=seen+v;if(i===2)s[1]=20;return a+v;},0);"
        "var filled=[];filled.length=3;filled[2]=3;var fillIndices='';"
        "var fillResult=filled.reduceRight(function(a,v,i,s){fillIndices=fillIndices+i;"
        "if(i===2)s[1]=2;return a+v;},0);"
        "var removed=[1,2,3];var removeCalls=0;var removeResult=removed.reduceRight(function(a,v,i,s){"
        "removeCalls=removeCalls+1;if(i===2)s.length=1;return a+v;},0);"
        "var shrunk=[1,2,3];var shrinkCalls=0;var shrinkResult=shrunk.reduceRight(function(a,v,i,s){"
        "shrinkCalls=shrinkCalls+1;if(i===2)s.length=1;return a+v;},0);"
        "var appended=[1,2,3];var appendCalls=0;var appendResult=appended.reduceRight(function(a,v,i,s){"
        "appendCalls=appendCalls+1;if(i===2)s.push(4);return a+v;},0);"
        "var current=[1,2,3];var currentSeen='';var currentResult=current.reduceRight(function(a,v,i,s){"
        "currentSeen=currentSeen+v;if(i===2)s[i]=30;return a+v;},0);"
        "var higher=[1,2,3];var higherSeen='';var higherResult=higher.reduceRight(function(a,v,i,s){"
        "higherSeen=higherSeen+v;if(i===1)s[2]=30;return a+v;},0);"
        "var unrelated=[7,8];var unrelatedResult=[1,2].reduceRight(function(a,v,i){"
        "if(i===1)unrelated[1]=80;return a+v;},0);var unrelatedValue=unrelated[1];"
        "var sourceObject={value:7};var refSource=[sourceObject,1,sourceObject];"
        "var retained=refSource.reduceRight(function(a,v,i,s){if(i===2)s.length=0;return v;});");
    expect(result.succeeded() && numberIs(runtime, "replacedResult", 24.0) &&
        stringIs(runtime, "seen", "3201"),
        "replacement of a lower unvisited element is observed on its turn");
    expect(numberIs(runtime, "fillResult", 5.0) &&
        stringIs(runtime, "fillIndices", "21"),
        "filling a lower unvisited hole causes a callback at that index");
    expect(numberIs(runtime, "removeResult", 4.0) &&
        numberIs(runtime, "removeCalls", 2.0) &&
        numberIs(runtime, "shrinkResult", 4.0) &&
        numberIs(runtime, "shrinkCalls", 2.0),
        "shrinking length removes future lower indexes and preserves captured upper bound");
    expect(numberIs(runtime, "appendResult", 6.0) &&
        numberIs(runtime, "appendCalls", 3.0),
        "appends beyond entry length are not visited");
    expect(numberIs(runtime, "currentResult", 6.0) &&
        stringIs(runtime, "currentSeen", "321") &&
        numberIs(runtime, "higherResult", 6.0) &&
        stringIs(runtime, "higherSeen", "321"),
        "current and already visited higher slots are not revisited after mutation");
    expect(numberIs(runtime, "unrelatedResult", 3.0) &&
        numberIs(runtime, "unrelatedValue", 80.0) &&
        objectId(runtime, "retained") == objectId(runtime, "sourceObject"),
        "unrelated Array mutation and accumulator references remain independent");
}

void testAccumulatorAndNestedMethods()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var same={total:0};var sameResult=[1,2].reduceRight(function(a,v){"
        "a.total=a.total+v;return a;},same);"
        "var sourceObject={value:7};var source=[sourceObject,1,2];"
        "var existing=source.reduceRight(function(a,v,i){if(i===0)return v;return a;});"
        "var allocated=[1,2].reduceRight(function(a,v,i){if(i===1)return {value:v};"
        "a.value=a.value+v;return a;},{});"
        "var seed=[];var arrayResult=[1,2].reduceRight(function(a,v){a.push(v);return a;},seed);"
        "var switched=[1,2,3].reduceRight(function(a,v,i){if(i===2)return {value:v};"
        "if(i===1)return 12;return {value:a+v};},0);"
        "var nestedRight=[1,2].reduceRight(function(a,v){return [a,v].reduceRight(function(x,y){return x+y;},0);},0);"
        "var rightInForward=[1,2].reduce(function(a,v){return [a,v].reduceRight(function(x,y){return x+y;},0);},0);"
        "var forwardInRight=[1,2].reduceRight(function(a,v){return [a,v].reduce(function(x,y){return x+y;},0);},0);"
        "var map=[1,2].map(function(v){return [v,2].reduceRight(function(a,x){return a+x;},0);});"
        "var filter=[1,2,3].filter(function(v){return [v,2].reduceRight(function(a,x){return a+x;},0)>3;});"
        "var each=0;[1,2].forEach(function(v){each=each+[v].reduceRight(function(a,x){return a+x;},0);});"
        "var some=[1,2].some(function(v){return [v,1].reduceRight(function(a,x){return a+x;},0)===3;});"
        "var every=[1,2].every(function(v){return [v,1].reduceRight(function(a,x){return a+x;},0)>0;});"
        "var found=[1,2].find(function(v){return [v,2].reduceRight(function(a,x){return a+x;},0)===4;});"
        "var foundIndex=[1,2].findIndex(function(v){return [v,2].reduceRight(function(a,x){return a+x;},0)===3;});"
        "var methodsOk=sameResult===same&&same.total===3&&allocated.value===3&&"
        "arrayResult===seed&&seed[0]===2&&seed[1]===1&&switched.value===13&&"
        "nestedRight===3&&rightInForward===3&&forwardInRight===3&&map[0]===3&&map[1]===4&&"
        "filter.length===2&&filter[0]===2&&filter[1]===3&&each===3&&some&&every&&found===2&&foundIndex===0;");
    expect(result.succeeded() &&
        objectId(runtime, "existing") == objectId(runtime, "sourceObject"),
        "callback can replace accumulator with source object references safely");
    expect(booleanIs(runtime, "methodsOk"),
        "object and Array accumulators survive replacement and all nested callback methods");
}

void testHostedFixtureAndEvents()
{
    std::ifstream input("navigator-smoke/javascript-js101.html", std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///js101-reduce-right.html", html, loadError);
    const std::string& script = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    const ScriptResult hosted = loaded && !script.empty()
        ? harness.execute(script) : ScriptResult();
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    if (!hosted.succeeded() ||
        markerText.find("JS101:assertions=20:failures=0") != 0u) {
        std::cout << "  JS101 hosted diagnostic: status="
            << static_cast<unsigned>(hosted.status) << " error="
            << static_cast<unsigned>(hosted.runtimeError.code)
            << " parse=" << static_cast<unsigned>(hosted.parserError.code)
            << " offset=" << hosted.parserError.location.offset
            << " marker=" << markerText << '\n';
    }
    expect(loaded && loadError == RuntimeErrorCode::None && hosted.succeeded() &&
        markerText.find("JS101:assertions=20:failures=0") == 0u &&
        markerText.find(":arguments=true:sparse=true:mutation=true:nested=true") !=
            std::string::npos,
        "hosted JS101 fixture verifies reverse order, sparse data, mutation, and nested reduction");

    HostInstanceId eventA = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "reduce-right-event-a") eventA = element.serial;
    }
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = eventA != 0u && harness.dispatchClick(eventA, eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Event dispatch preserves independent reverse accumulators and event metadata");

    HostInstanceId errorEvent = 0u;
    for (const auto& element : harness.document().structuralElements) {
        if (element.id == "reduce-right-error-event") errorEvent = element.serial;
    }
    RuntimeErrorCode callbackError = RuntimeErrorCode::None;
    const bool errorClicked = errorEvent != 0u &&
        harness.dispatchClick(errorEvent, callbackError);
    expect(!errorClicked && callbackError == RuntimeErrorCode::TypeError &&
        harness.runtime().activeCallFrameCount() == 0u,
        "empty reduceRight callback error propagates through hosted Event cleanup");

    const bool inheritedInstalled =
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(
            harness.runtime(), "1023", Value::number(8.0));
    const ScriptResult inherited = inheritedInstalled
        ? runSameRealm(harness.runtime(),
            "var inheritedSource=[];inheritedSource.length=1024;"
            "var inheritedCalls=0;var inheritedResult=inheritedSource.reduceRight(function(){"
            "inheritedCalls=inheritedCalls+1;return 0;});")
        : ScriptResult();
    expect(inherited.succeeded() &&
        numberIs(harness.runtime(), "inheritedResult", 8.0) &&
        numberIs(harness.runtime(), "inheritedCalls", 0.0),
        "hosted page realm uses inherited highest numeric property as seed");
}

void testErrorsLimitsAndStorage()
{
    RuntimeLimits budgetLimits;
    budgetLimits.maxExecutionSteps = 48u;
    RuntimeContext budget(budgetLimits);
    const ScriptResult budgetFailure = run(budget,
        "[1].reduceRight(function(){while(true){}},0);");
    expect(budgetFailure.status == ScriptStatus::ExecutionBudgetExceeded &&
        budgetFailure.runtimeError.code == RuntimeErrorCode::ExecutionBudgetExceeded &&
        budget.activeCallFrameCount() == 0u,
        "execution budget exhaustion propagates and unwinds reverse callback frames");

    RuntimeLimits depthLimits;
    depthLimits.maxCallDepth = 1u;
    RuntimeContext depth(depthLimits);
    const ScriptResult depthFailure = run(depth,
        "[1].reduceRight(function(){return 1;},0);");
    expect(depthFailure.runtimeError.code == RuntimeErrorCode::CallDepthExceeded &&
        depth.activeCallFrameCount() == 0u,
        "reduceRight preserves configured call-depth protection");

    RuntimeContext callbackFailure;
    const ScriptResult thrown = run(callbackFailure,
        "var lastAcc;[1,2].reduceRight(function(acc,value){lastAcc=acc;"
        "if(value===2)null.absent;return acc;},{});");
    expect(thrown.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        callbackFailure.activeCallFrameCount() == 0u &&
        objectId(callbackFailure, "lastAcc") != kInvalidRuntimeObjectId,
        "callback failure preserves earlier side effects and accumulator references");

    RuntimeContext nestedFailure;
    const ScriptResult nested = run(nestedFailure,
        "[1].reduceRight(function(){return [2].reduce(function(){return null.x;},0);},0);");
    expect(nested.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        nestedFailure.activeCallFrameCount() == 0u,
        "nested callback errors propagate and unwind all call frames");

    RuntimeLimits arraySlots;
    arraySlots.maxTotalArrayElements = 2u;
    RuntimeContext slotFailure(arraySlots);
    const ScriptResult slot = run(slotFailure,
        "[1,2].reduceRight(function(a,v){return [v];},0);");
    expect(slot.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        slotFailure.activeCallFrameCount() == 0u,
        "Array slot exhaustion inside callback propagates without partial accumulator result");

    RuntimeContext baseline;
    RuntimeLimits objectLimits;
    objectLimits.maxObjects = baseline.objectCount() + 1u;
    RuntimeContext objectFailure(objectLimits);
    const ScriptResult allocation = run(objectFailure,
        "[1,2].reduceRight(function(a,v){return {};},0);");
    expect(allocation.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
        objectFailure.activeCallFrameCount() == 0u,
        "object allocation failure in callback propagates through native reducer");

    RuntimeContext storage;
    const ScriptResult setup = run(storage,
        "var source=[1,2,3];var result;var calls=0;");
    const std::size_t objectsBefore =
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(storage);
    const std::size_t slotsBefore = storage.arrayElementCount();
    const ScriptResult reduction = setup.succeeded()
        ? runSameRealm(storage,
            "result=source.reduceRight(function(a,v){calls=calls+1;return a+v;},0);")
        : ScriptResult();
    expect(reduction.succeeded() && numberIs(storage, "result", 6.0) &&
        numberIs(storage, "calls", 3.0) &&
        NavigatorScriptRuntimeTestAccess::runtimeObjectCount(storage) == objectsBefore &&
        storage.arrayElementCount() == slotsBefore,
        "successful reduction allocates no result Array or persistent staging slots");
    expect(NavigatorScriptRuntimeTestAccess::runtimeObjectSize() == 88u &&
        NavigatorScriptRuntimeTestAccess::arrayElementSize() == 48u &&
        NavigatorScriptRuntimeTestAccess::methodAttributes(storage, "reduceRight"),
        "reduceRight preserves object layouts and writable-only native method attributes");
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
    const ScriptResult denseRun = run(dense, denseArraySource(1024u) +
        "var calls=0;var orderOk=true;var expected=1023;var indexTrace=[];"
        "var total=source.reduceRight(function(a,v,i){calls=calls+1;"
        "if(i!==expected)orderOk=false;expected=expected-1;indexTrace.push(i);return a+v;},0);"
        "var firstIndex=indexTrace[0];var lastIndex=indexTrace[1023];");
    expect(denseRun.succeeded() && numberIs(dense, "calls", 1024.0) &&
        numberIs(dense, "total", 523776.0) && booleanIs(dense, "orderOk") &&
        numberIs(dense, "expected", -1.0) &&
        numberIs(dense, "firstIndex", 1023.0) &&
        numberIs(dense, "lastIndex", 0.0) &&
        dense.arrayElementCount() == 2048u,
        "maximum dense Array visits all 1,024 indexes in descending order and terminates at zero");

    RuntimeContext denseNoSeed;
    const ScriptResult seedRun = run(denseNoSeed, denseArraySource(1024u) +
        "var calls=0;var orderOk=true;var expected=1022;var indexTrace=[];"
        "var total=source.reduceRight(function(a,v,i){calls=calls+1;"
        "if(i!==expected)orderOk=false;expected=expected-1;indexTrace.push(i);return a-v;});"
        "var firstIndex=indexTrace[0];var lastIndex=indexTrace[1022];");
    expect(seedRun.succeeded() && numberIs(denseNoSeed, "calls", 1023.0) &&
        numberIs(denseNoSeed, "total", -521730.0) &&
        booleanIs(denseNoSeed, "orderOk") &&
        numberIs(denseNoSeed, "expected", -1.0) &&
        numberIs(denseNoSeed, "firstIndex", 1022.0) &&
        numberIs(denseNoSeed, "lastIndex", 0.0),
        "maximum dense Array without seed calls back from 1,022 through zero");

    RuntimeContext edges;
    const ScriptResult edgeRun = run(edges,
        "var emptyCalls=0;var empty=[].reduceRight(function(){emptyCalls=emptyCalls+1;},4);"
        "var oneCalls=0;var one=[9].reduceRight(function(){oneCalls=oneCalls+1;});"
        "var two=[4,9];var twoOrder='';var twoResult=two.reduceRight(function(a,v,i){"
        "twoOrder=twoOrder+i;return a-v;},20);"
        "var low=[];low.length=1024;low[0]=7;var lowCalls=0;var lowResult=low.reduceRight(function(){"
        "lowCalls=lowCalls+1;return 0;});"
        "var high=[];high.length=1024;high[1023]=8;var highCalls=0;"
        "var highResult=high.reduceRight(function(){highCalls=highCalls+1;return 0;});");
    expect(edgeRun.succeeded() && numberIs(edges, "empty", 4.0) &&
        numberIs(edges, "emptyCalls", 0.0) && numberIs(edges, "one", 9.0) &&
        numberIs(edges, "oneCalls", 0.0) && numberIs(edges, "twoResult", 7.0) &&
        stringIs(edges, "twoOrder", "10"),
        "length zero, one, and two obey callback and reverse ordering semantics");
    expect(numberIs(edges, "lowResult", 7.0) &&
        numberIs(edges, "lowCalls", 0.0) &&
        numberIs(edges, "highResult", 8.0) &&
        numberIs(edges, "highCalls", 0.0),
        "only index zero or the highest supported index seeds with zero callbacks");

    RuntimeContext sparse;
    const ScriptResult sparseRun = run(sparse,
        "var source=[];source.length=1024;source[1]=2;source[1022]=3;"
        "var indices='';var calls=0;var result=source.reduceRight(function(a,v,i){"
        "indices=indices+i+',';calls=calls+1;return a+v;},0);");
    expect(sparseRun.succeeded() && numberIs(sparse, "result", 5.0) &&
        numberIs(sparse, "calls", 2.0) && stringIs(sparse, "indices", "1022,1,"),
        "maximum sparse Array visits only occupied indexes from right to left");

    RuntimeContext holes;
    const ScriptResult holesSeed = run(holes,
        "var source=[];source.length=1024;var calls=0;"
        "var result=source.reduceRight(function(){calls=calls+1;return 1;},7);");
    expect(holesSeed.succeeded() && numberIs(holes, "result", 7.0) &&
        numberIs(holes, "calls", 0.0),
        "maximum all-hole Array with initialValue invokes no callbacks");
    RuntimeContext holesError;
    const ScriptResult noSeed = run(holesError,
        "var source=[];source.length=1024;source.reduceRight(function(){return 1;});");
    expect(noSeed.runtimeError.code == RuntimeErrorCode::TypeError,
        "maximum all-hole Array without initialValue throws TypeError");

    RuntimeContext tooLong;
    const ScriptResult rejected = run(tooLong,
        "var source=[];var calls=0;source.length=1025;"
        "source.reduceRight(function(){calls=calls+1;return 0;},0);");
    expect(rejected.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        numberIs(tooLong, "calls", 0.0),
        "existing 1,025-position Array rejection is retained");
}

} // namespace

int main()
{
    testOrderArgumentsAndInitialValue();
    testSparseInheritedAndEmpty();
    testReverseMutation();
    testAccumulatorAndNestedMethods();
    testHostedFixtureAndEvents();
    testErrorsLimitsAndStorage();
    testBoundaries();
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS101 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS101 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
