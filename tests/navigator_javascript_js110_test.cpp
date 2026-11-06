#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptRuntimeTestAccess {
    static std::size_t runtimeObjectSize()
    {
        return sizeof(RuntimeContext::RuntimeObject);
    }

    static std::size_t runtimePropertySize()
    {
        return sizeof(RuntimeContext::RuntimeProperty);
    }

    static std::size_t arrayElementSize()
    {
        return sizeof(RuntimeContext::RuntimeArrayElement);
    }

    static std::size_t functionRecordSize()
    {
        return sizeof(RuntimeContext::FunctionRecord);
    }

    static bool fillObjectPool(RuntimeContext& runtime)
    {
        const std::vector<Value> noElements;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        while (runtime.objects_.size() < runtime.limits_.maxObjects) {
            RuntimeObjectId object = kInvalidRuntimeObjectId;
            if (!runtime.createObject(false, noElements, object, error))
                return false;
        }
        return true;
    }

    static bool setArrayPrototypeIndex(RuntimeContext& runtime,
        std::size_t index, Value value)
    {
        RuntimeErrorCode error = RuntimeErrorCode::None;
        return runtime.writeProperty(runtime.arrayPrototype_,
            std::to_string(index), value, error);
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

ScriptResult run(RuntimeContext& runtime, const std::string& source)
{
    const ScriptResult result = runtime.execute(SourceView(source.data(),
        source.size()));
    if (!result.succeeded()) {
        std::cerr << "JS110 script detail: status="
            << static_cast<unsigned>(result.status) << " runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << " at="
            << result.parserError.location.line << ':'
            << result.parserError.location.column << " lexer="
            << lexerErrorCodeName(result.lexerError.code) << " source="
            << source << '\n';
    }
    return result;
}

ScriptResult run(NavigatorScriptExecutionHarness& harness,
    const std::string& source)
{
    const ScriptResult result = harness.execute(SourceView(source.data(),
        source.size()));
    if (!result.succeeded()) {
        std::cerr << "JS110 hosted script detail: status="
            << static_cast<unsigned>(result.status) << " runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << " at="
            << result.parserError.location.line << ':'
            << result.parserError.location.column << " lexer="
            << lexerErrorCodeName(result.lexerError.code) << " source="
            << source << '\n';
    }
    return result;
}

const Value* binding(const RuntimeContext& runtime, const char* name)
{
    return runtime.lookup(SourceView(name,
        std::char_traits<char>::length(name)));
}

bool booleanBinding(const RuntimeContext& runtime, const char* name)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isBoolean() && value->booleanValue();
}

bool numberBinding(const RuntimeContext& runtime, const char* name,
    double expected)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isNumber() &&
        value->numberValue() == expected;
}

bool stringBinding(const RuntimeContext& runtime, const char* name,
    const std::string& expected)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isString() &&
        runtime.stringValue(*value) == expected;
}

bool runRuntimeError(const std::string& source, RuntimeErrorCode expected,
    RuntimeLimits limits = RuntimeLimits())
{
    RuntimeContext runtime(limits);
    const ScriptResult result = runtime.execute(SourceView(source.data(),
        source.size()));
    return !result.succeeded() && result.runtimeError.code == expected;
}

std::string readFixture()
{
    std::ifstream input("navigator-smoke/javascript-js110.html",
        std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::uint64_t serialById(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const gxos::web::HtmlElementRef& element :
            harness.document().structuralElements) {
        if (element.id == id) return element.serial;
    }
    return 0u;
}

std::string renderedText(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    if (serial != 0u && serial <= harness.document().contentMetadata.size()) {
        const gxos::web::HtmlElementContentMetadata& content =
            harness.document().contentMetadata[static_cast<std::size_t>(
                serial - 1u)];
        bool hasDynamicBlock = false;
        if (content.serial == serial && content.dynamicallyCreated) {
            for (const gxos::web::DocBlock& block :
                    harness.document().blocks) {
                if (block.elementMetadata.serial == serial &&
                    block.inlineFlowSerial == serial) {
                    hasDynamicBlock = true;
                    break;
                }
            }
        }
        if (hasDynamicBlock) {
            std::string projected;
            for (const gxos::web::WebInlineItem& item :
                    harness.document().inlineItems) {
                if (item.ownerSerial != serial) continue;
                if (item.kind == gxos::web::InlineItemKind::TextRun)
                    projected += item.text;
                else if (item.kind == gxos::web::InlineItemKind::ForcedBreak)
                    projected.push_back('\n');
            }
            return projected;
        }
    }
    std::string result;
    if (!navigatorScriptElementTextContent(harness.document(), serial, result))
        return std::string();
    return result;
}

void testArgumentsAndCallbackContract()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var omitted = JSON.parse('\"ok\"');"
        "var withUndefined = JSON.parse('\"ok\"', undefined);"
        "var withNull = JSON.parse('\"ok\"', null);"
        "var withNumber = JSON.parse('\"ok\"', 4);"
        "var withString = JSON.parse('\"ok\"', 'ignored');"
        "var withObject = JSON.parse('\"ok\"', {});"
        "var traditional = JSON.parse('{\"n\":5}',"
        " function(key,value){if(key===\"n\")return value*2;return value;});"
        "var arrow = JSON.parse('{\"n\":5}',"
        " (key,value)=>{if(key===\"n\")return value+1;return value;});"
        "var traditionalOk=traditional.n===10; var arrowOk=arrow.n===6;");
    expect(result.succeeded() && stringBinding(runtime, "omitted", "ok") &&
        stringBinding(runtime, "withUndefined", "ok") &&
        stringBinding(runtime, "withNull", "ok") &&
        stringBinding(runtime, "withNumber", "ok") &&
        stringBinding(runtime, "withString", "ok") &&
        stringBinding(runtime, "withObject", "ok") &&
        booleanBinding(runtime, "traditionalOk") &&
        booleanBinding(runtime, "arrowOk"),
        "omitted and non-callable revivers are ignored while both callable forms run");

    RuntimeContext callbacks;
    const ScriptResult callbackResult = run(callbacks,
        "var ordinaryValueThis=false; var ordinaryRootThis=false;"
        "var arrowThis=false; var owner={run:function(){"
        "return JSON.parse('{\"n\":3}',function(key,value){"
        "if(key===\"n\")ordinaryValueThis=this.hasOwnProperty(key)&&this[key]===value;"
        "else ordinaryRootThis=key===\"\"&&this[key]===value;return value;});},"
        "runArrow:function(){return JSON.parse('{\"n\":3}',"
        "(key,value)=>{if(key===\"n\")arrowThis=this===owner;return value;});}};"
        "var ordinary=owner.run(); var lexical=owner.runArrow();"
        "var keyAndValueTypes=false; var argumentTypes=JSON.parse('{\"n\":3}',"
        "function(key,value){if(key===\"n\")keyAndValueTypes=key.length===1&&"
        "value===3;return value;});");
    expect(callbackResult.succeeded() && booleanBinding(callbacks,
            "ordinaryValueThis") && booleanBinding(callbacks,
            "ordinaryRootThis") && booleanBinding(callbacks, "arrowThis") &&
        booleanBinding(callbacks, "keyAndValueTypes"),
        "reviver gets string keys, current values, containing-object this, and arrow lexical this");

    expect(runRuntimeError("JSON.parse('1', null, 0);",
            RuntimeErrorCode::UnsupportedFeature),
        "arguments beyond the selected two-argument parse signature remain unsupported");
}

void testTraversalOrderAndRoots()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var visited=''; var nested=JSON.parse('{\"outer\":{\"inner\":3}}',"
        "function(key,value){visited=visited+'['+key+']';return value;});"
        "var objectOrder=''; JSON.parse('{\"b\":1,\"10\":10,\"2\":2,\"a\":3}',"
        "function(key,value){objectOrder=objectOrder+'['+key+']';return value;});"
        "var mixedOrder=''; JSON.parse('{\"items\":[3,2,1]}',"
        "function(key,value){mixedOrder=mixedOrder+'['+key+']';return value;});"
        "var emptyOrder=''; JSON.parse('{\"\":1}',function(key,value){"
        "if(key===\"\"){if(value===1)emptyOrder=emptyOrder+\"[empty]\";"
        "else emptyOrder=emptyOrder+\"[root]\";}else emptyOrder=emptyOrder+key;return value;});"
        "var primitiveRootHolder=false; var primitiveRoot=JSON.parse('5',function(key,value){"
        "primitiveRootHolder=key===\"\"&&this[key]===value;return value*2;});"
        "var objectRoot=JSON.parse('{\"a\":1}',function(key,value){"
        "if(key===\"\")return 42;return value;});"
        "var arrayRoot=JSON.parse('[1]',function(key,value){"
        "if(key===\"\")return [value[0]+1];return value;});"
        "var arrayRootOk=arrayRoot.length===1&&arrayRoot[0]===2;"
        "var undefinedRoot=JSON.parse('{\"a\":1}',function(key,value){"
        "if(key===\"\")return undefined;return value;});");
    expect(result.succeeded() && stringBinding(runtime, "visited",
            "[inner][outer][]") &&
        stringBinding(runtime, "objectOrder", "[2][10][b][a][]") &&
        stringBinding(runtime, "mixedOrder", "[0][1][2][items][]") &&
        stringBinding(runtime, "emptyOrder", "[empty][root]") &&
        booleanBinding(runtime, "primitiveRootHolder") &&
        numberBinding(runtime, "primitiveRoot", 10.0) &&
        numberBinding(runtime, "objectRoot", 42.0) &&
        booleanBinding(runtime, "arrayRootOk") &&
        binding(runtime, "undefinedRoot") != nullptr &&
        binding(runtime, "undefinedRoot")->isUndefined(),
        "reviver traversal is child-first, index-aware, ordered, and root-last");

    RuntimeContext identities;
    const ScriptResult identityResult = run(identities,
        "var replacement={ok:7}; var rootReplacement=JSON.parse('{}',"
        "function(key,value){if(key===\"\")return replacement;return value;});"
        "var rootIdentity=rootReplacement===replacement;"
        "var arrayReplacement=JSON.parse('{\"x\":1}',function(key,value){"
        "if(key===\"\")return [value.x];return value;});"
        "var arrayRootIdentity=arrayReplacement.length===1&&arrayReplacement[0]===1;"
        "var calls=''; var childReplacement=JSON.parse('{\"v\":1}',"
        "function(key,value){calls=calls+'['+key+']';"
        "if(key===\"v\")return {created:2};return value;});");
    expect(identityResult.succeeded() && booleanBinding(identities,
            "rootIdentity") && booleanBinding(identities,
            "arrayRootIdentity") && stringBinding(identities, "calls",
            "[v][]"),
        "root replacement preserves object or Array identity and replacement children are not revisited");

    RuntimeContext replacementTypes;
    const ScriptResult replacementResult = run(replacementTypes,
        "var callableValue=function(){return 1;};"
        "var values=JSON.parse('{\"number\":1,\"string\":1,\"boolean\":1,\"null\":1,\"function\":1}',"
        "function(key,value){"
        "if(key===\"number\")return 2;"
        "if(key===\"string\")return \"two\";"
        "if(key===\"boolean\")return true;"
        "if(key===\"null\")return null;"
        "if(key===\"function\")return callableValue;return value;});"
        "var replacementTypesOk=values.number===2&&values.string===\"two\"&&"
        "values.boolean===true&&values[\"null\"]===null&&"
        "values[\"function\"]===callableValue;"
        "var arrayToObject=JSON.parse('[4]',function(key,value){"
        "if(key===\"\")return {value:value[0]};return value;});"
        "var objectToArray=JSON.parse('{\"value\":4}',function(key,value){"
        "if(key===\"\")return [value.value];return value;});"
        "var kindReplacementOk=arrayToObject.value===4&&objectToArray[0]===4;");
    expect(replacementResult.succeeded() &&
        booleanBinding(replacementTypes, "replacementTypesOk") &&
        booleanBinding(replacementTypes, "kindReplacementOk"),
        "reviver replacements preserve supported primitive, function, object, and Array values by reference");
}

void testObjectDeletionAndMutation()
{
    RuntimeContext deletion;
    const std::size_t propertiesBefore = deletion.propertyCount();
    const ScriptResult result = run(deletion,
        "var holderSeen; var data=JSON.parse('{\"name\":\"Navigator\",\"secret\":123}',"
        "function(key,value){if(key===\"secret\"){holderSeen=this;return undefined;}return value;});"
        "var keys=Object.keys(data); var deletionOk=data.name===\"Navigator\"&&"
        "data.secret===undefined&&!data.hasOwnProperty(\"secret\")&&"
        "keys.length===1&&keys[0]===\"name\"&&"
        "JSON.stringify(data)===\"{\\\"name\\\":\\\"Navigator\\\"}\";"
        "var holderIdentity=holderSeen===data;");
    expect(result.succeeded() && booleanBinding(deletion, "deletionOk") &&
        booleanBinding(deletion, "holderIdentity"),
        "undefined removes an own object property from lookup, Object.keys, and JSON.stringify");
    expect(deletion.propertyCount() - propertiesBefore == 2u,
        "own-property deletion decrements live property storage accounting");

    RuntimeContext nestedDeletion;
    const ScriptResult nestedResult = run(nestedDeletion,
        "var nested=JSON.parse('{\"item\":{\"secret\":1,\"keep\":2}}',"
        "function(key,value){if(key===\"secret\")return undefined;return value;});"
        "var nestedDeleteOk=!nested.item.hasOwnProperty(\"secret\")&&"
        "nested.item.hasOwnProperty(\"keep\")&&"
        "JSON.stringify(nested)===\"{\\\"item\\\":{\\\"keep\\\":2}}\";"
        "var empty=JSON.parse('{\"a\":1,\"b\":2}',function(key,value){"
        "if(key!==\"\")return undefined;return value;});"
        "var emptyObjectOk=Object.keys(empty).length===0&&JSON.stringify(empty)===\"{}\";");
    expect(nestedResult.succeeded() &&
        booleanBinding(nestedDeletion, "nestedDeleteOk") &&
        booleanBinding(nestedDeletion, "emptyObjectOk"),
        "nested and complete object deletion update enumeration and serialization");

    RuntimeContext mutation;
    const ScriptResult mutationResult = run(mutation,
        "var objectVisits=''; var changed=JSON.parse('{\"first\":1,\"later\":2}',"
        "function(key,value){if(key===\"first\"){this.later=99;this.added=7;}"
        "objectVisits=objectVisits+'['+key+']';return value;});"
        "var addedOk=changed.later===99&&changed.added===7&&objectVisits===\"[first][later][]\";"
        "var removedLater=JSON.parse('{\"first\":1,\"later\":2}',"
        "function(key,value){if(key===\"first\")this.later=undefined;"
        "if(key===\"later\")return value;return value;});"
        "var removedLaterOk=!removedLater.hasOwnProperty(\"later\")&&"
        "Object.keys(removedLater).length===1;"
        "var nestedVisits=''; var nestedMutation=JSON.parse('{\"first\":0,\"nested\":{\"old\":1}}',"
        "function(key,value){if(key===\"first\")this.nested={fresh:2};"
        "if(key===\"fresh\")nestedVisits=nestedVisits+'fresh';return value;});"
        "var nestedMutationOk=nestedMutation.nested.fresh===2&&nestedVisits===\"fresh\";"
        "var alreadyVisits=0; var already=JSON.parse('{\"first\":1,\"last\":2}',"
        "function(key,value){if(key===\"first\")alreadyVisits++;"
        "if(key===\"last\")this.first=44;return value;});"
        "var alreadyOk=already.first===44&&alreadyVisits===1;");
    expect(mutationResult.succeeded() && booleanBinding(mutation,
            "addedOk") && booleanBinding(mutation, "removedLaterOk") &&
        booleanBinding(mutation, "nestedMutationOk") &&
        booleanBinding(mutation, "alreadyOk"),
        "key snapshots allow live replacement, deletion, addition, nested replacement, and prior-key mutation");

    RuntimeContext reinsertion;
    const ScriptResult reinsertionResult = run(reinsertion,
        "var restored=JSON.parse('{\"a\":1,\"b\":2}',function(key,value){"
        "if(key===\"a\")return undefined;"
        "if(key===\"b\")this.a=3;return value;});"
        "var restoredKeys=Object.keys(restored);"
        "var reinsertionOk=restoredKeys.length===2&&restoredKeys[0]===\"b\"&&"
        "restoredKeys[1]===\"a\"&&JSON.stringify(restored)===\"{\\\"b\\\":2,\\\"a\\\":3}\";");
    expect(reinsertionResult.succeeded() &&
        booleanBinding(reinsertion, "reinsertionOk"),
        "a deleted property can be reinserted as one new own key without revisiting the snapshot");
}

void testArrayHolesAndMutation()
{
    RuntimeContext runtime;
    const std::size_t elementsBefore = runtime.arrayElementCount();
    const ScriptResult result = run(runtime,
        "var sparse=JSON.parse('[10,20,30]',function(key,value){"
        "if(key===\"1\")return undefined;return value;});"
        "var mapCalls=0; var mapped=sparse.map(function(value){mapCalls++;return value*2;});"
        "var filterCalls=0; var filtered=sparse.filter(function(){filterCalls++;return true;});"
        "var findHole=sparse.findIndex(function(value,index){return index===1&&value===undefined;});"
        "var findCalls=0; sparse.find(function(value,index){if(index===1&&value===undefined)findCalls++;return false;});"
        "var reduceCalls=0; var sum=sparse.reduce(function(total,value){reduceCalls++;return total+value;},0);"
        "var rightCalls=0; var rightSum=sparse.reduceRight(function(total,value){rightCalls++;return total+value;},0);"
        "var sparseOk=sparse.length===3&&sparse.hasOwnProperty(\"0\")&&!sparse.hasOwnProperty(\"1\")&&"
        "sparse.hasOwnProperty(\"2\")&&JSON.stringify(sparse)===\"[10,null,30]\"&&"
        "!mapped.hasOwnProperty(\"1\")&&mapCalls===2&&filtered.length===2&&filterCalls===2&&"
        "findHole===1&&findCalls===1&&sum===40&&reduceCalls===2&&rightSum===40&&rightCalls===2;"
        "var allHoles=JSON.parse('[1,2,3]',function(key,value){if(key===\"\")return value;return undefined;});"
        "var allHolesOk=allHoles.length===3&&!allHoles.hasOwnProperty(\"0\")&&"
        "!allHoles.hasOwnProperty(\"1\")&&!allHoles.hasOwnProperty(\"2\")&&"
        "JSON.stringify(allHoles)===\"[null,null,null]\";"
        "var arrayVisits=''; var changed=JSON.parse('[1,2,3]',function(key,value){"
        "if(key===\"0\"){this[1]=99;this.push(4);}"
        "arrayVisits=arrayVisits+'['+key+']';return value;});"
        "var arrayMutationOk=changed.length===4&&changed[1]===99&&"
        "arrayVisits===\"[0][1][2][]\";"
        "var shrinkVisits=''; var shrunk=JSON.parse('[1,2,3]',function(key,value){"
        "if(key===\"0\")this.length=1;shrinkVisits=shrinkVisits+'['+key+']';return value;});"
        "var shrinkOk=shrunk.length===1&&shrinkVisits===\"[0][1][2][]\";");
    expect(result.succeeded() && booleanBinding(runtime, "sparseOk") &&
        booleanBinding(runtime, "allHolesOk") &&
        booleanBinding(runtime, "arrayMutationOk") &&
        booleanBinding(runtime, "shrinkOk"),
        "Array deletion leaves holes and length intact while callbacks honor captured length and live values");
    expect(runtime.arrayElementCount() == elementsBefore + 11u,
        "sparse Array erasure releases live element quota without shifting indices");

    RuntimeContext edgeHoles;
    const ScriptResult edgeResult = run(edgeHoles,
        "var edges=JSON.parse('[1,2,3]',function(key,value){"
        "if(key===\"0\"||key===\"2\")return undefined;return value;});"
        "var edgeHolesOk=edges.length===3&&!edges.hasOwnProperty(\"0\")&&"
        "edges[1]===2&&!edges.hasOwnProperty(\"2\")&&"
        "JSON.stringify(edges)===\"[null,2,null]\";");
    expect(edgeResult.succeeded() && booleanBinding(edgeHoles, "edgeHolesOk"),
        "deleting first and last Array indices preserves length and does not shift the middle value");

    RuntimeContext inherited;
    const bool prototypePrepared =
        NavigatorScriptRuntimeTestAccess::setArrayPrototypeIndex(inherited,
            1u, Value::number(99.0));
    const std::string inheritedSource =
        "var seenInherited=false;"
        "var values=JSON.parse('[1,2,3]',function(key,value){"
        "if(key===\"0\")this.length=0;"
        "if(key===\"1\")seenInherited=value===99;return value;});"
        "var inheritedOk=seenInherited&&values.hasOwnProperty(\"1\")&&"
        "values[1]===99&&values.length===2;";
    const ScriptResult inheritedResult = inherited.executeInSameRealm(
        SourceView(inheritedSource.data(), inheritedSource.size()));
    expect(prototypePrepared && inheritedResult.succeeded() && booleanBinding(inherited,
            "inheritedOk"),
        "a deleted or shrunk future Array slot uses prototype-aware Get before replacement");
}

void testPrototypeSafetyAndReentrancy()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var replaced=JSON.parse('{\"__proto__\":{\"polluted\":1},\"keep\":2}',"
        "function(key,value){if(key===\"__proto__\")return {polluted:7};return value;});"
        "var replacedOk=replaced.hasOwnProperty(\"__proto__\")&&"
        "replaced.__proto__.polluted===7&&({}).polluted===undefined&&"
        "JSON.stringify(replaced)===\"{\\\"__proto__\\\":{\\\"polluted\\\":7},\\\"keep\\\":2}\";"
        "var removed=JSON.parse('{\"__proto__\":{\"polluted\":1},\"keep\":2}',"
        "function(key,value){if(key===\"__proto__\")return undefined;return value;});"
        "var removedOk=!removed.hasOwnProperty(\"__proto__\")&&"
        "JSON.stringify(removed)===\"{\\\"keep\\\":2}\"&&({}).polluted===undefined;"
        "var otherObject={count:0}; var nestedRuns=0; var nestedText='';"
        "var nestedCallbackOk=false;"
        "var outer=JSON.parse('{\"value\":1}',function(key,value){"
        "if(key===\"value\"){var inner=JSON.parse('{\"n\":2}',"
        "function(innerKey,innerValue){nestedRuns++;return innerValue;});"
        "nestedText=JSON.stringify(inner); var arr=[1,2,3].map(x=>x+1);"
        "var filtered=arr.filter(x=>x>2); var total=filtered.reduce((a,x)=>a+x,0);"
        "nestedCallbackOk=nestedText===\"{\\\"n\\\":2}\"&&total===7;"
        "otherObject.count=otherObject.count+1; return {items:[total]};}return value;});"
        "var replacementReferenceOk=outer.value.items[0]===7&&otherObject.count===1;");
    expect(result.succeeded() && booleanBinding(runtime, "replacedOk") &&
        booleanBinding(runtime, "removedOk") &&
        booleanBinding(runtime, "nestedCallbackOk") &&
        numberBinding(runtime, "nestedRuns", 2.0) &&
        booleanBinding(runtime, "replacementReferenceOk"),
        "__proto__ remains a data key while nested parse, stringify, Array callbacks, and fresh replacement values are safe");
    const Value* nested = binding(runtime, "outer");
    const Value* other = binding(runtime, "otherObject");
    expect(nested != nullptr && nested->isObject() && other != nullptr &&
        other->isObject(),
        "reviver-produced object and Array references are retained without copying");
}

void testFailuresAndBounds()
{
    RuntimeContext sideEffects;
    const ScriptResult callbackFailure = run(sideEffects,
        "var sideEffect=0; JSON.parse('{\"a\":1}',function(key,value){"
        "if(key===\"a\"){sideEffect=9;return missingName;}return value;});");
    expect(!callbackFailure.succeeded() &&
        callbackFailure.runtimeError.code == RuntimeErrorCode::UnknownIdentifier &&
        numberBinding(sideEffects, "sideEffect", 9.0),
        "reviver runtime errors propagate and callback side effects remain visible");

    expect(runRuntimeError("function again(key,value){return JSON.parse('1',again);}"
            "JSON.parse('1',again);", RuntimeErrorCode::CallDepthExceeded),
        "nested revivers remain subject to the existing call-depth limit");
    expect(runRuntimeError("var data=JSON.parse('[0,0]',function(key,value){"
            "if(key===\"0\")this[1]={a:{b:1}};return value;});",
            RuntimeErrorCode::JsonNestingLimitExceeded, [] {
                RuntimeLimits limits;
                limits.maxJsonNestingDepth = 1u;
                return limits;
            }()),
        "callback-created nested values remain within the configured traversal-depth bound");

    RuntimeLimits stepLimit;
    stepLimit.maxExecutionSteps = 48u;
    expect(runRuntimeError("JSON.parse('[0,0,0,0,0,0,0,0,0,0,0,0]',"
            "function(key,value){return value;});",
            RuntimeErrorCode::ExecutionBudgetExceeded, stepLimit),
        "reviver traversal consumes the shared execution budget");

    RuntimeLimits objectLimit;
    objectLimit.maxObjects = 12u;
    RuntimeContext full(objectLimit);
    const std::size_t beforeFill = full.objectCount();
    const bool filled = NavigatorScriptRuntimeTestAccess::fillObjectPool(full);
    const std::size_t beforeFailure = full.objectCount();
    const std::string source = "JSON.parse('1',function(key,value){return value;});";
    const ScriptResult allocationFailure = full.executeInSameRealm(
        SourceView(source.data(), source.size()));
    expect(filled && beforeFill < beforeFailure &&
        !allocationFailure.succeeded() &&
        allocationFailure.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
        full.objectCount() == beforeFailure,
        "root-holder capacity failure is reported before callbacks without corrupting retained storage");

    RuntimeContext retained;
    const std::string repeatSource =
        "var out=JSON.parse('{\"a\":1,\"b\":2}',"
        "function(key,value){if(key===\"a\")return undefined;"
        "if(key===\"b\")this.a=value+1;return value;});"
        "var keys=Object.keys(out); var reinsertionOrderOk=keys.length===2&&"
        "keys[0]===\"b\"&&keys[1]===\"a\";";
    std::size_t netProperties = 0u;
    bool repeatedOk = true;
    for (std::size_t index = 0u; index < 20u; ++index) {
        const std::size_t before = retained.propertyCount();
        const ScriptResult repeated = retained.executeInSameRealm(
            SourceView(repeatSource.data(), repeatSource.size()));
        if (!repeated.succeeded() ||
            retained.propertyCount() - before != 3u ||
            !booleanBinding(retained, "reinsertionOrderOk")) {
            repeatedOk = false;
            break;
        }
        ++netProperties;
    }
    expect(repeatedOk && netProperties == 20u,
        "repeated deletion and reinsertion never duplicates visible properties or wraps live quotas");
}

void testHostedFixtureAndEventCallback()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const std::string html = readFixture();
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///javascript-js110.html", html, loadError) &&
        loadError == RuntimeErrorCode::None &&
        harness.document().scriptSources.size() == 1u;
    expect(loaded,
        "hosted JS110 fixture parses into one Navigator script");
    if (!loaded) return;

    const std::uint64_t revisionBefore = harness.layoutRevision();
    const ScriptResult page = run(harness,
        harness.document().scriptSources[0]);
    const bool settled = !harness.documentDirty() || harness.relayout();
    expect(page.succeeded() && settled &&
        harness.layoutRevision() > revisionBefore,
        "reviver-driven DOM insertion updates and settles Navigator layout");
    expect(booleanBinding(harness.runtime(), "hostedReviverOk") &&
        booleanBinding(harness.runtime(), "hostedHoleOk") &&
        booleanBinding(harness.runtime(), "hostedOrderOk"),
        "hosted fixture confirms callback order, deletion, serialization, and sparse callbacks");

    const std::uint64_t alpha = serialById(harness, "item-0");
    const std::uint64_t beta = serialById(harness, "item-1");
    const std::uint64_t gamma = serialById(harness, "item-2");
    const std::uint64_t serialized = serialById(harness, "serialized-json");
    expect(alpha != 0u && beta != 0u && gamma != 0u && serialized != 0u &&
        renderedText(harness, alpha) == "Alpha item" &&
        renderedText(harness, beta) == "Beta item" &&
        renderedText(harness, gamma) == "Gamma item" &&
        renderedText(harness, serialized) ==
            "{\"items\":[{\"name\":\"Alpha item\"},"
            "{\"name\":\"Beta item\"},{\"name\":\"Gamma item\"}]}",
        "hosted paragraphs and compact JSON show transformed values in document order");

    const std::uint64_t button = serialById(harness, "event-button");
    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    expect(button != 0u && harness.dispatchClick(button, eventError) &&
        eventError == RuntimeErrorCode::None &&
        booleanBinding(harness.runtime(), "hostedEventOk"),
        "a supported Navigator Event callback can run nested reviver traversal");
}

} // namespace

int main()
{
    testArgumentsAndCallbackContract();
    testTraversalOrderAndRoots();
    testObjectDeletionAndMutation();
    testArrayHolesAndMutation();
    testPrototypeSafetyAndReentrancy();
    testFailuresAndBounds();
    testHostedFixtureAndEventCallback();

    std::cout << "JS110 host records: std::string=" << sizeof(std::string)
        << " B RuntimeObject="
        << NavigatorScriptRuntimeTestAccess::runtimeObjectSize()
        << " B RuntimeProperty="
        << NavigatorScriptRuntimeTestAccess::runtimePropertySize()
        << " B RuntimeArrayElement="
        << NavigatorScriptRuntimeTestAccess::arrayElementSize()
        << " B FunctionRecord="
        << NavigatorScriptRuntimeTestAccess::functionRecordSize() << " B\n";
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS110 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS110 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
