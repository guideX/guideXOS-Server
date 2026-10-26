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
        const Object* prototype = runtime.objectAt(runtime.arrayPrototype_);
        if (prototype == nullptr) return false;
        for (const RuntimeContext::RuntimeProperty& property :
            prototype->properties) {
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

    static std::size_t iteratorCount(const RuntimeContext& runtime)
    {
        std::size_t count = 0u;
        for (const Object& object : runtime.objects_)
            if (object.arrayIterator) ++count;
        return count;
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

void testMethodAndCallbackContract()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var values=[10,20,30];var total=0;var calls=0;"
        "var sameArray=true;var numericIndices=true;var expectedIndex=0;"
        "var returned=values.forEach(function(value,index,array){"
        "total=total+value;numericIndices=numericIndices&&index===expectedIndex;"
        "expectedIndex=expectedIndex+1;calls=calls+1;"
        "sameArray=sameArray&&array===values;"
        "return false;});"
        "var nativeCallback=[-4].forEach(Math.abs);");
    const Value* returned = binding(runtime, "returned");
    expect(result.succeeded() && numberIs(runtime, "total", 60.0) &&
        numberIs(runtime, "calls", 3.0),
        "dense traversal invokes once per present index and ignores false returns");
    expect(booleanIs(runtime, "sameArray") &&
        booleanIs(runtime, "numericIndices") && returned != nullptr &&
        returned->isUndefined(),
        "callback receives the original Array, numeric indices, and forEach returns undefined");
    expect(NavigatorScriptRuntimeTestAccess::methodAttributes(runtime,
        "forEach"),
        "forEach is registered on Array.prototype with native writable-only attributes");
    Value method;
    RuntimeErrorCode methodError = RuntimeErrorCode::None;
    expect(runtime.readPropertyForTesting(runtime.arrayPrototypeId(),
            "forEach", method, methodError) && method.isFunction() &&
        methodError == RuntimeErrorCode::None,
        "Array.prototype exposes a genuine callable native method");

    RuntimeContext thisArg;
    const ScriptResult thisResult = run(thisArg,
        "var values=[5];var context={value:77};var observed=false;"
        "values.forEach(function(value,index,array){"
        "observed=this===context&&value===5&&index===0&&array===values;"
        "},context);");
    expect(thisResult.succeeded() && booleanIs(thisArg, "observed"),
        "explicit object thisArg is bound by the existing callback receiver path");

    RuntimeContext defaultThis;
    const ScriptResult defaultThisResult = run(defaultThis,
        "var omitted=[1];var explicit=[1];var omittedOk=false;"
        "var explicitOk=false;"
        "omitted.forEach(function(){omittedOk=this===undefined;});"
        "explicit.forEach(function(){explicitOk=this===undefined;},undefined);");
    expect(defaultThisResult.succeeded() &&
        booleanIs(defaultThis, "omittedOk") &&
        booleanIs(defaultThis, "explicitOk"),
        "omitted and explicit undefined thisArg preserve Navigator's undefined receiver subset");

    RuntimeContext invalid;
    const ScriptResult invalidResult = run(invalid,
        "var empty=[];empty.forEach(3);");
    expect(invalidResult.status == ScriptStatus::RuntimeFailure &&
        invalidResult.runtimeError.code == RuntimeErrorCode::NotCallable,
        "noncallable callbacks fail validation even on an empty Array");

    RuntimeContext missing;
    const ScriptResult missingResult = run(missing,
        "var empty=[];empty.forEach();");
    expect(missingResult.status == ScriptStatus::RuntimeFailure &&
        missingResult.runtimeError.code == RuntimeErrorCode::NotCallable,
        "a missing callback uses the deterministic NotCallable error");

    const char* invalidCallbacks[] = {
        "var empty=[];empty.forEach(undefined);",
        "var empty=[];empty.forEach(null);",
        "var empty=[];empty.forEach('callback');",
        "var empty=[];empty.forEach({});",
    };
    for (const char* source : invalidCallbacks) {
        RuntimeContext invalidCallback;
        const ScriptResult invalidCallbackResult = run(invalidCallback, source);
        expect(invalidCallbackResult.status == ScriptStatus::RuntimeFailure &&
            invalidCallbackResult.runtimeError.code ==
                RuntimeErrorCode::NotCallable,
            "undefined, null, string, and object callbacks fail before empty traversal");
    }

    RuntimeContext earlyReturn;
    const ScriptResult earlyReturnResult = run(earlyReturn,
        "var values=[1,2,3];var called=0;"
        "values.forEach(function(value,index){called=called+1;"
        "if(index===0)return;});");
    expect(earlyReturnResult.succeeded() && numberIs(earlyReturn, "called", 3.0),
        "a callback's function-local early return does not stop outer traversal");

    RuntimeContext wrongReceiver;
    const ScriptResult setup = run(wrongReceiver, "var receiver={};");
    const bool linked = setup.succeeded() &&
        wrongReceiver.setPrototypeForTesting(objectId(wrongReceiver,
            "receiver"), wrongReceiver.arrayPrototypeId());
    const ScriptResult receiverResult = linked
        ? runSameRealm(wrongReceiver,
            "receiver.forEach(function(){});") : ScriptResult();
    expect(receiverResult.status == ScriptStatus::RuntimeFailure &&
        receiverResult.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "an ordinary object inheriting the method is rejected as a receiver");

    RuntimeContext undefinedReceiver;
    const ScriptResult undefinedSetup = run(undefinedReceiver,
        "var callback=function(){};");
    Value undefinedReceiverMethod;
    RuntimeErrorCode lookupError = RuntimeErrorCode::None;
    const bool methodRead = undefinedSetup.succeeded() &&
        undefinedReceiver.readPropertyForTesting(
            undefinedReceiver.arrayPrototypeId(), "forEach",
            undefinedReceiverMethod,
            lookupError);
    Value ignored;
    RuntimeErrorCode receiverError = RuntimeErrorCode::None;
    const Value* callback = binding(undefinedReceiver, "callback");
    const bool undefinedCall = methodRead && callback != nullptr &&
        undefinedReceiver.invokeFunctionForTesting(undefinedReceiverMethod,
            {*callback}, ignored, receiverError);
    expect(!undefinedCall && receiverError == RuntimeErrorCode::InvalidReceiver,
        "a direct native invocation with undefined receiver is rejected");
}

void testSparseAndInheritedProperties()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var sparse=[10,,undefined,40];var sparseCount=0;"
        "var sparseIndices='';var explicitUndefined=false;"
        "sparse.forEach(function(value,index,array){"
        "sparseCount=sparseCount+1;sparseIndices=sparseIndices+index;"
        "if(index===2)explicitUndefined=value===undefined&&array===sparse;});"
        "var keys=Object.keys(sparse);"
        "var hole=Object.getOwnPropertyDescriptor(sparse,'1');"
        "var explicit=Object.getOwnPropertyDescriptor(sparse,'2');"
        "var occupancyOk=keys.length===3&&keys[0]==='0'&&"
        "keys[1]==='2'&&keys[2]==='3'&&hole===undefined&&"
        "explicit!==undefined&&explicit.value===undefined&&"
        "!sparse.hasOwnProperty('1')&&sparse.hasOwnProperty('2');");
    expect(result.succeeded() && numberIs(runtime, "sparseCount", 3.0) &&
        booleanIs(runtime, "occupancyOk") &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(runtime,
            objectId(runtime, "sparse"), 1u),
        "true holes are skipped without materialization while explicit undefined is visited");
    const Value* indices = binding(runtime, "sparseIndices");
    expect(indices != nullptr && indices->isString() &&
        runtime.stringValue(*indices) == "023" &&
        booleanIs(runtime, "explicitUndefined"),
        "sparse callback indices remain numeric positions rather than callback ordinals");

    RuntimeContext typed;
    const ScriptResult typedResult = run(typed,
        "var values=[undefined,null,false,0,''];var count=0;"
        "var typedOk=true;values.forEach(function(value,index,array){"
        "count=count+1;typedOk=typedOk&&array===values&&index<5;});");
    expect(typedResult.succeeded() && numberIs(typed, "count", 5.0) &&
        booleanIs(typed, "typedOk"),
        "undefined, null, false, zero, and empty string are all present values");

    RuntimeContext inherited;
    const ScriptResult inheritedSetup = run(inherited,
        "var sparse=[10,,30];var inheritedCount=0;var inheritedValue=0;");
    const bool installed = inheritedSetup.succeeded() &&
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            "1", Value::number(99.0));
    const ScriptResult inheritedResult = installed
        ? runSameRealm(inherited,
            "sparse.forEach(function(value,index,array){"
            "inheritedCount=inheritedCount+1;"
            "if(index===1)inheritedValue=value;});"
            "var inheritedOwn=Object.getOwnPropertyDescriptor(sparse,'1');"
            "var inheritedNames=Object.keys(sparse);") : ScriptResult();
    const Value* own = binding(inherited, "inheritedOwn");
    const Value* names = binding(inherited, "inheritedNames");
    expect(inheritedResult.succeeded() &&
        numberIs(inherited, "inheritedCount", 3.0) &&
        numberIs(inherited, "inheritedValue", 99.0) &&
        own != nullptr && own->isUndefined() && names != nullptr &&
        names->isObject() &&
        !NavigatorScriptRuntimeTestAccess::hasOwnIndex(inherited,
            objectId(inherited, "sparse"), 1u),
        "HasProperty visits a supported inherited index without changing own occupancy");

    RuntimeContext identities;
    const ScriptResult identityResult = run(identities,
        "var objectValue={marker:1};var nestedArray=[2,3];"
        "var values=[objectValue,nestedArray];var retained;var retainedNested;"
        "var arrayIdentity=true;values.forEach(function(value,index,array){"
        "arrayIdentity=arrayIdentity&&array===values;"
        "if(index===0)retained=value;if(index===1)retainedNested=value;});"
        "var retainedIdentity=retained===objectValue&&"
        "retainedNested===nestedArray&&arrayIdentity;");
    expect(identityResult.succeeded() &&
        booleanIs(identities, "retainedIdentity"),
        "ordinary objects, nested Arrays, original Array identity, and retained callback values remain shallow references");
}

void testLiveMutationAndCapturedLength()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var appended=[10,20];var appendCount=0;"
        "appended.forEach(function(value,index,array){appendCount=appendCount+1;"
        "if(index===0)array[2]=30;});"
        "var replaced=[10,20,30];var replacement=0;"
        "replaced.forEach(function(value,index,array){"
        "if(index===0)array[1]=99;if(index===1)replacement=value;});"
        "var filled=[10,,30];var fillCount=0;"
        "filled.forEach(function(value,index,array){fillCount=fillCount+1;"
        "if(index===0)array[1]=20;});"
        "var removed=[1,2,3];var removedCount=0;"
        "removed.forEach(function(value,index,array){removedCount=removedCount+1;"
        "if(index===0)array.length=1;});"
        "var regrown=[0,1,2,3];var regrownIndices='';"
        "regrown.forEach(function(value,index,array){"
        "regrownIndices=regrownIndices+index;"
        "if(index===0){array.length=1;array.length=4;array[3]=33;}});");
    const Value* regrownIndices = binding(runtime, "regrownIndices");
    expect(result.succeeded() && numberIs(runtime, "appendCount", 2.0) &&
        numberIs(runtime, "replacement", 99.0) &&
        numberIs(runtime, "fillCount", 3.0),
        "initial length is captured while later value replacement and hole filling are observed");
    expect(numberIs(runtime, "removedCount", 1.0) &&
        regrownIndices != nullptr && regrownIndices->isString() &&
        runtime.stringValue(*regrownIndices) == "03",
        "shrinkage skips removed positions and shrink/regrowth follows current presence within the captured bound");

    RuntimeContext visited;
    const ScriptResult visitedResult = run(visited,
        "var values=[1,2];var seen='';values.forEach(function(v,i,a){"
        "seen=seen+i;if(i===0)a[0]=9;});");
    const Value* seen = binding(visited, "seen");
    expect(visitedResult.succeeded() && seen != nullptr && seen->isString() &&
        visited.stringValue(*seen) == "01",
        "mutating an already visited index does not invoke that index again");
}

void testNestedCallsErrorsAndCapacity()
{
    RuntimeContext nested;
    const ScriptResult nestedResult = run(nested,
        "var values=[1,2];var other=[7,8,9];var trace='';"
        "var outerContext={};var innerContext={};var outerThisOk=true;"
        "var innerThisOk=true;"
        "values.forEach(function(v,i,a){trace=trace+'o'+i;"
        "outerThisOk=outerThisOk&&this===outerContext;"
        "if(i===0){a.forEach(function(iv,ii){trace=trace+'i'+ii;"
        "innerThisOk=innerThisOk&&this===innerContext;},innerContext);"
        "other.forEach(function(ov,oi){trace=trace+'d'+oi;});}"
        "outerThisOk=outerThisOk&&this===outerContext;"
        "trace=trace+'r'+i;},outerContext);");
    const Value* trace = binding(nested, "trace");
    expect(nestedResult.succeeded() && trace != nullptr && trace->isString() &&
        nested.stringValue(*trace) == "o0i0i1d0d1d2r0o1r1",
        "same-Array and different-Array nested calls keep independent traversal state");
    expect(booleanIs(nested, "outerThisOk") &&
        booleanIs(nested, "innerThisOk"),
        "nested callbacks preserve independent thisArg receivers and restore the outer receiver");

    RuntimeContext errorRuntime;
    const ScriptResult errorResult = run(errorRuntime,
        "var values=[1,2,3];var called=0;"
        "values.forEach(function(value,index,array){called=called+1;"
        "if(index===0)array[1]=99;if(index===1)null.value;});");
    Value mutated;
    RuntimeErrorCode readError = RuntimeErrorCode::None;
    const bool mutationRetained = errorRuntime.readPropertyForTesting(
        objectId(errorRuntime, "values"), "1", mutated, readError);
    expect(errorResult.status == ScriptStatus::RuntimeFailure &&
        errorResult.runtimeError.code == RuntimeErrorCode::CannotReadProperty &&
        numberIs(errorRuntime, "called", 2.0) && mutationRetained &&
        mutated.isNumber() && mutated.numberValue() == 99.0,
        "callback runtime errors propagate, stop later calls, and preserve prior user mutations");

    RuntimeLimits environmentLimits;
    environmentLimits.maxEnvironments = 2u;
    RuntimeContext environmentFailure(environmentLimits);
    const ScriptResult setup = run(environmentFailure,
        "var values=[1,2,3];var called=0;"
        "function callback(){called=called+1;}values.forEach(callback);");
    const ScriptResult allocationResult = setup;
    expect(allocationResult.status == ScriptStatus::RuntimeFailure &&
        allocationResult.runtimeError.code ==
            RuntimeErrorCode::EnvironmentLimitExceeded &&
        numberIs(environmentFailure, "called", 1.0) &&
        environmentFailure.activeCallFrameCount() == 0u,
        "bounded callback-environment exhaustion stops traversal and restores active frames");

    RuntimeContext stress;
    std::string source = "var values=[";
    for (std::size_t index = 0u; index < 1024u; ++index) {
        if (index != 0u) source.push_back(',');
        source += std::to_string(index);
    }
    source += "];var calls=0;values.forEach(function(v){calls=calls+1;});";
    const ScriptResult stressResult = run(stress, source);
    expect(stressResult.succeeded() && numberIs(stress, "calls", 1024.0) &&
        stress.environmentCount() <= stress.limits().maxEnvironments,
        "the maximum 1,024-element dense Array completes within retained environment capacity");
    const RuntimeDiagnostics& diagnostics = stress.diagnostics();
    std::cout << "JS93 max dense workload: positions=1024 callbacks=1024 objectHighWater="
        << diagnostics.objectPoolHighWater << " propertyHighWater="
        << diagnostics.propertyPoolHighWater << " arraySlotHighWater="
        << diagnostics.arraySlotHighWater << " environmentHighWater="
        << diagnostics.environmentPoolHighWater << " hostHandles="
        << stress.hostObjectCount() << " runtimeStrings="
        << stress.runtimeStringValueCount() << '\n';

    RuntimeContext holes;
    const ScriptResult holeResult = run(holes,
        "var values=[];values.length=1024;var calls=0;"
        "values.forEach(function(){calls=calls+1;});");
    expect(holeResult.succeeded() && numberIs(holes, "calls", 0.0) &&
        holes.arrayElementCount() == 0u,
        "a 1,024-position all-hole Array examines the captured bound and invokes zero callbacks");
    expect(NavigatorScriptRuntimeTestAccess::iteratorCount(stress) == 0u &&
        stress.diagnostics().iteratorResultObjectsAllocated == 0u &&
        stress.diagnostics().iteratorResultObjectsPublished == 0u,
        "forEach allocates no Array iterator cursor or iterator-result objects");

    RuntimeContext beyond;
    const ScriptResult beyondResult = run(beyond,
        "var values=[];values.length=1025;");
    expect(beyondResult.status == ScriptStatus::RuntimeFailure &&
        beyondResult.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded,
        "the runtime's 1,024 logical-position Array bound fails deterministically");
}

std::string denseSource(std::size_t length, const char* traversal)
{
    std::string source = "var values=[";
    for (std::size_t index = 0u; index < length; ++index) {
        if (index != 0u) source.push_back(',');
        source += std::to_string(index);
    }
    source += "];var count=0;";
    source += traversal;
    return source;
}

long long median(std::vector<long long> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2u];
}

long long measure(const std::string& source,
    RuntimeErrorCode* failureCode = nullptr)
{
    const auto start = std::chrono::steady_clock::now();
    RuntimeContext runtime;
    const ScriptResult result = run(runtime, source);
    const auto end = std::chrono::steady_clock::now();
    if (!result.succeeded()) {
        if (failureCode != nullptr) *failureCode = result.runtimeError.code;
        return -1;
    }
    if (failureCode != nullptr) *failureCode = RuntimeErrorCode::None;
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start)
        .count();
}

long long measureInheritedIndex()
{
    RuntimeContext runtime;
    if (!NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(runtime,
            "31", Value::number(42.0))) return -1;
    const std::string source =
        "var values=[];values.length=64;var calls=0;"
        "values.forEach(function(value){calls=calls+1;});";
    const auto start = std::chrono::steady_clock::now();
    const ScriptResult result = run(runtime, source);
    const auto end = std::chrono::steady_clock::now();
    if (!result.succeeded()) return -1;
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start)
        .count();
}

void testPerformanceAndStackObservations()
{
    const std::size_t lengths[] = {0u, 16u, 64u, 128u, 256u, 512u, 1024u};
    for (std::size_t length : lengths) {
        const std::string each = denseSource(length,
            "values.forEach(function(v){count=count+1;});");
        const std::string forOf = denseSource(length,
            "for(const value of values){count=count+1;}");
        std::vector<long long> eachSamples;
        std::vector<long long> forOfSamples;
        RuntimeErrorCode forOfFailureCode = RuntimeErrorCode::None;
        for (std::size_t sample = 0u; sample < 5u; ++sample) {
            eachSamples.push_back(measure(each));
            forOfSamples.push_back(measure(forOf, &forOfFailureCode));
        }
        const long long eachMedian = median(eachSamples);
        const long long forOfMedian = median(forOfSamples);
        const bool forOfExpected = length <= 512u
            ? forOfMedian >= 0
            : forOfMedian < 0 && forOfFailureCode ==
                RuntimeErrorCode::ObjectLimitExceeded;
        expect(eachMedian >= 0 && forOfExpected,
            "forEach and for-of performance workloads complete or fail at the recorded retained-object boundary");
        std::cout << "JS93 median us length=" << length << " forEach="
            << eachMedian << " forOf=" << forOfMedian
            << " forOfFailure="
            << static_cast<unsigned>(forOfFailureCode)
            << " samples=5 setup+parse+evaluation included\n";
    }

    std::string explicitUndefined = "var values=[";
    for (std::size_t index = 0u; index < 64u; ++index) {
        if (index != 0u) explicitUndefined.push_back(',');
        explicitUndefined += "undefined";
    }
    explicitUndefined += "];var count=0;"
        "values.forEach(function(v){count=count+1;});";
    const std::pair<const char*, std::string> workloads[] = {
        {"sparse-two-of-64",
            "var values=[];values.length=64;values[0]=1;values[63]=2;"
            "var count=0;values.forEach(function(v){count=count+1;});"},
        {"all-hole-1024",
            "var values=[];values.length=1024;var count=0;"
            "values.forEach(function(v){count=count+1;});"},
        {"explicit-undefined-64", explicitUndefined},
        {"mutate-later-64", denseSource(64u,
            "values.forEach(function(v,i,a){if(i===0)a[63]=99;"
            "count=count+1;});")},
        {"nested-16x16",
            "var outer=[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15];"
            "var inner=[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15];"
            "var count=0;outer.forEach(function(a){inner.forEach(function(b){"
            "count=count+1;});});"},
    };
    for (const auto& workload : workloads) {
        std::vector<long long> samples;
        for (std::size_t sample = 0u; sample < 5u; ++sample)
            samples.push_back(measure(workload.second));
        const long long workloadMedian = median(samples);
        expect(workloadMedian >= 0,
            "sparse, all-hole, explicit-undefined, mutation, and nested benchmark workload completes");
        std::cout << "JS93 median us workload=" << workload.first
            << " value=" << workloadMedian
            << " samples=5 setup+parse+evaluation included\n";
    }
    std::vector<long long> inheritedSamples;
    for (std::size_t sample = 0u; sample < 5u; ++sample)
        inheritedSamples.push_back(measureInheritedIndex());
    const long long inheritedMedian = median(inheritedSamples);
    expect(inheritedMedian >= 0,
        "prototype-inherited indexed benchmark workload completes");
    std::cout << "JS93 median us workload=inherited-index-31-of-64 value="
        << inheritedMedian << " samples=5 setup+parse+evaluation included\n";
}

void testHostedFixtureAndEvents()
{
    std::ifstream file("navigator-smoke/javascript-js93.html", std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js93-native.html", html,
        loadError) && loadError == RuntimeErrorCode::None;
    expect(loaded, "JS93 hosted fixture loads through Navigator's HTML subset");
    if (!loaded) return;
    const std::string& source = harness.document().scriptSources.empty()
        ? std::string() : harness.document().scriptSources[0];
    expect(harness.document().scriptSources.size() == 1u &&
        source.find('<') == std::string::npos,
        "JS93 inline script preserves the established HTML-subset integrity guard");
    const ScriptResult result = harness.execute(source);
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    expect(result.succeeded() && markerText.find("JS93:assertions=") == 0u &&
        markerText.find(":failures=0:") != std::string::npos,
        "hosted JS93 assertions pass basic, sparse, mutation, and DOM callback coverage");
    if (!result.succeeded()) {
        std::cout << "JS93 hosted diagnostic status="
            << static_cast<unsigned>(result.status) << " error="
            << static_cast<unsigned>(result.runtimeError.code) << " marker="
            << markerText << '\n';
    }
    expect(markerText.find(":basic=true:sparse=true:length=true:mutation=true") !=
            std::string::npos &&
        markerText.find(":nested=true:dom=true:events=ready") !=
            std::string::npos,
        "hosted JS93 marker records callback arguments, captured length, nested calls, and DOM interoperability");

    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    const bool clicked = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, clickError);
    expect(clicked && clickError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "nested Events preserve forEach bounds, identities, and Event metadata");

    std::vector<long long> eventSamples;
    bool eventSamplesPassed = true;
    for (std::size_t sample = 0u; sample < 5u; ++sample) {
        NavigatorScriptExecutionHarness sampleHarness;
        RuntimeErrorCode sampleLoadError = RuntimeErrorCode::None;
        if (!sampleHarness.loadHtml("file:///js93-event-benchmark.html", html,
                sampleLoadError) || sampleLoadError != RuntimeErrorCode::None) {
            eventSamplesPassed = false;
            break;
        }
        const std::string& sampleSource =
            sampleHarness.document().scriptSources.empty()
            ? std::string() : sampleHarness.document().scriptSources[0];
        if (sampleSource.empty() ||
            !sampleHarness.execute(sampleSource).succeeded()) {
            eventSamplesPassed = false;
            break;
        }
        HostInstanceId sampleEvent = 0u;
        for (const auto& element : sampleHarness.document().structuralElements)
            if (element.id == "event-a") sampleEvent = element.serial;
        RuntimeErrorCode sampleClickError = RuntimeErrorCode::None;
        const auto start = std::chrono::steady_clock::now();
        const bool sampleClicked = sampleEvent != 0u &&
            sampleHarness.dispatchClick(sampleEvent, sampleClickError);
        const auto end = std::chrono::steady_clock::now();
        if (!sampleClicked || sampleClickError != RuntimeErrorCode::None ||
            !booleanIs(sampleHarness.runtime(), "nestedEventOk") ||
            !booleanIs(sampleHarness.runtime(), "outerEventOk")) {
            eventSamplesPassed = false;
            break;
        }
        eventSamples.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                .count());
    }
    expect(eventSamplesPassed && eventSamples.size() == 5u,
        "five fresh nested Event workloads complete for the performance sample");
    if (eventSamples.size() == 5u) {
        std::cout << "JS93 median us workload=nested-event value="
            << median(eventSamples)
            << " samples=5 dispatch-only; fixture setup excluded\n";
    }
}

} // namespace

int main()
{
    testMethodAndCallbackContract();
    testSparseAndInheritedProperties();
    testLiveMutationAndCapturedLength();
    testNestedCallsErrorsAndCapacity();
    testPerformanceAndStackObservations();
    testHostedFixtureAndEvents();

    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS93 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS93 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
