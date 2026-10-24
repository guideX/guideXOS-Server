#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptIteratorTestAccess {
    using Iterator =
        NavigatorScriptHostAdapter::SelectorCollectionIteratorRecord;

    static std::size_t activeIteratorCount(
        const NavigatorScriptHostAdapter& adapter)
    {
        std::size_t count = 0u;
        for (const Iterator& record : adapter.selectorCollectionIterators_)
            if (record.active) ++count;
        return count;
    }
};

struct NavigatorScriptRuntimeTestAccess {
    using Object = RuntimeContext::RuntimeObject;

    static std::size_t objectBytes() { return sizeof(Object); }

    static bool iteratorState(const RuntimeContext& runtime,
        RuntimeObjectId id, RuntimeObjectId& source, std::size_t& nextIndex,
        bool& exhausted)
    {
        const Object* object = runtime.objectAt(id);
        if (object == nullptr || !object->arrayIterator) return false;
        source = object->arrayIteratorSource;
        nextIndex = object->arrayIteratorIndex;
        exhausted = object->arrayIteratorExhausted;
        return true;
    }

    static bool fillObjectPool(RuntimeContext& runtime)
    {
        const std::vector<Value> noElements;
        while (runtime.objectCount() < runtime.limits().maxObjects) {
            RuntimeObjectId object = kInvalidRuntimeObjectId;
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!runtime.createObject(false, noElements, object, error))
                return false;
        }
        return true;
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

double numberIs(const RuntimeContext& runtime, const char* name)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isNumber() ? value->numberValue() : -1.0;
}

bool booleanIs(const RuntimeContext& runtime, const char* name)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isBoolean() && value->booleanValue();
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

ScriptResult runSameRealm(RuntimeContext& runtime, const std::string& source)
{
    return runtime.executeInSameRealm(SourceView(source.data(), source.size()));
}

ScriptResult runSameRealm(RuntimeContext& runtime, const char* source)
{
    return runtime.executeInSameRealm(SourceView(source,
        std::char_traits<char>::length(source)));
}

bool loadHtml(NavigatorScriptExecutionHarness& harness,
    const std::string& html, const char* label)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js87-native.html", html,
        error) && error == RuntimeErrorCode::None;
    expect(loaded, label);
    return loaded;
}

std::string fixtureSource(const std::string& html)
{
    const std::size_t open = html.find("<script>");
    const std::size_t close = html.find("</script>", open);
    if (open == std::string::npos || close == std::string::npos) return {};
    return html.substr(open + 8u, close - open - 8u);
}

void testHostedFixture()
{
    std::ifstream file("navigator-smoke/javascript-js87.html",
        std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, html, "JS87 hosted fixture loads natively")) return;
    const std::string source = fixtureSource(html);
    expect(!source.empty() && source.find('<') == std::string::npos &&
        harness.document().scriptSources.size() == 1u &&
        harness.document().scriptSources[0] == source,
        "JS87 hosted inline source preserves the HTML-subset integrity guard");
    const ScriptResult result = harness.execute(source);
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    if (!result.succeeded()) {
        std::cerr << "  JS87 fixture runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << '@'
            << result.parserError.location.line << ':'
            << result.parserError.location.column << '\n';
    }
    expect(result.succeeded() && markerText.find("JS87:assertions=") == 0u &&
        markerText.find(":failures=0:") != std::string::npos,
        "JS87 hosted fixture reports passing Array and collection assertions");

    HostInstanceId eventASerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventASerial = element.serial;
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    const bool clicked = eventASerial != 0u &&
        harness.dispatchClick(eventASerial, clickError);
    expect(clicked && clickError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "JS87 Array iteration survives nested Event callbacks and metadata");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS87 Event iterator frames unwind completely");
}

void testProtocolValuesAndForOf()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='a' class='row'></i>"
            "<i id='b' class='row'></i></body></html>",
            "JS87 protocol fixture loads")) return;
    const std::string source =
        "var values=[10,20,30];"
        "var symbolMethod=values[Symbol.iterator];"
        "var valuesMethod=values.values;"
        "var symbolKeysSeparate=values['Symbol.iterator']===undefined&&"
            "values.iterator===undefined&&values[Symbol.iterator]!==undefined;"
        "var methodsShareIdentity=symbolMethod===valuesMethod;"
        "var cursor=values[Symbol.iterator]();var first=cursor.next();"
        "var same=cursor[Symbol.iterator]();var second=cursor.next();"
        "var third=cursor.next();var end=cursor.next();"
        "var endAgain=cursor.next();"
        "var selfAndValues=same===cursor&&first.value===10&&"
            "first.done===false&&second.value===20&&second.done===false&&"
            "third.value===30&&third.done===false&&end.value===undefined&&"
            "end.done===true&&endAgain.value===undefined&&"
            "endAgain.done===true&&first.value===10&&first.done===false;"
        "var left=values.values();var right=values[Symbol.iterator]();"
        "var independent=left.next().value===10&&right.next().value===10&&"
            "left.next().value===20&&right.next().value===20;"
        "var empty=[];var emptyCursor=empty.values();"
        "var emptyFirst=emptyCursor.next();var emptySecond=emptyCursor.next();"
        "var emptyOk=emptyFirst.value===undefined&&emptyFirst.done===true&&"
            "emptySecond.value===undefined&&emptySecond.done===true&&"
            "emptyFirst!==emptySecond;"
        "var element=document.querySelectorAll('.row')[0];"
        "var payload={marker:7};var inner=[1,2];"
        "var mixed=[0,'',false,null,undefined,payload,inner,element];"
        "var mixedCursor=mixed.values();"
        "var m0=mixedCursor.next();var m1=mixedCursor.next();"
        "var m2=mixedCursor.next();var m3=mixedCursor.next();"
        "var m4=mixedCursor.next();var m5=mixedCursor.next();"
        "var m6=mixedCursor.next();var m7=mixedCursor.next();"
        "var mixedTypes=m0.value===0&&m1.value===''&&m2.value===false&&"
            "m3.value===null&&m4.value===undefined&&m5.value===payload&&"
            "m6.value===inner&&m7.value===element&&m7.value===element;"
        "var sum=0;for(const value of values){sum=sum+value;}"
        "var mutableSum=0;for(let value of values){"
            "mutableSum=mutableSum+value;value=0;}"
        "var continued=0;var bodies=0;"
        "for(const value of values){continued++;if(value===20)continue;"
            "bodies++;}"
        "var broken=0;for(const value of values){broken++;break;}"
        "function firstValue(){for(const value of values){return value;}"
            "return -1;}"
        "var returned=firstValue();"
        "var closeCalls=0;var closeCursor=[1,2].values();"
        "closeCursor['return']=function(){closeCalls++;return {};};"
        "for(const value of closeCursor){break;}"
        "var closeOk=closeCalls===1;"
        "var groups=[[1,2],[3,4]];var nestedCount=0;var nestedSum=0;"
        "for(const group of groups){for(const number of group){"
            "nestedCount++;nestedSum=nestedSum+number;}}"
        "var forOfOk=sum===60&&mutableSum===60&&continued===3&&bodies===2&&"
            "broken===1&&returned===10&&closeOk&&nestedCount===4&&"
            "nestedSum===10;";
    const ScriptResult result = harness.execute(source);
    if (!result.succeeded()) {
        std::cerr << "  JS87 protocol runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << '@'
            << result.parserError.location.line << ':'
            << result.parserError.location.column;
        if (result.parserError.location.column > 0u) {
            const std::size_t offset = result.parserError.location.column - 1u;
            const std::size_t start = offset > 50u ? offset - 50u : 0u;
            std::cerr << " source=" << source.substr(start, 110u);
        }
        std::cerr << '\n';
    }
    expect(result.succeeded() && booleanIs(harness.runtime(),
        "symbolKeysSeparate") && booleanIs(harness.runtime(),
        "methodsShareIdentity") && booleanIs(harness.runtime(),
        "selfAndValues") && booleanIs(harness.runtime(), "independent"),
        "JS87 uses genuine Symbol lookup, callable identity, self-return, and independent cursors");
    expect(result.succeeded() && booleanIs(harness.runtime(), "emptyOk") &&
        booleanIs(harness.runtime(), "mixedTypes"),
        "JS87 preserves empty results and mixed JavaScript value identity and types");
    expect(result.succeeded() && booleanIs(harness.runtime(), "forOfOk"),
        "JS87 for-of covers const, let, continue, break, return, close, and nested Arrays");
}

void testMutationAndDenseGapBehavior()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var values=[10,20];var cursor=values[Symbol.iterator]();"
        "var first=cursor.next();values.push(30);"
        "var second=cursor.next();var third=cursor.next();"
        "var terminal=cursor.next();values.push(40);"
        "var terminalAgain=cursor.next();"
        "var growthOk=first.value===10&&second.value===20&&"
            "third.value===30&&terminal.done===true&&"
            "terminalAgain.done===true&&terminalAgain.value===undefined;"
        "var before=[1];var beforeCursor=before.values();before.push(2);"
        "var beforeOk=beforeCursor.next().value===1&&"
            "beforeCursor.next().value===2;"
        "var shrinking=[1,2,3];var shrinkCursor=shrinking.values();"
        "var shrinkFirst=shrinkCursor.next();shrinking.pop();shrinking.pop();"
        "var shrinkDone=shrinkCursor.next();shrinking.push(9);"
        "var shrinkAgain=shrinkCursor.next();"
        "var shrinkOk=shrinkFirst.value===1&&shrinkDone.done===true&&"
            "shrinkAgain.done===true;"
        "var changing=[4,5];var changeCursor=changing.values();"
        "var changeFirst=changeCursor.next();changing[1]=8;"
        "var changeSecond=changeCursor.next();"
        "var changeOk=changeFirst.value===4&&changeSecond.value===8;"
        "var gap=[7];gap[3]=9;var gapCursor=gap.values();"
        "var gapValues=gapCursor.next().value===7&&"
            "gapCursor.next().value===undefined&&"
            "gapCursor.next().value===undefined&&gapCursor.next().value===9;");
    expect(result.succeeded() && booleanIs(runtime, "growthOk") &&
        booleanIs(runtime, "beforeOk") && booleanIs(runtime, "shrinkOk") &&
        booleanIs(runtime, "changeOk"),
        "JS87 Array cursors read current length and values across push, pop, and index writes");
    expect(result.succeeded() && booleanIs(runtime, "gapValues"),
        "JS87 dense gap slots yield undefined instead of ending iteration");

    RuntimeLimits limits;
    limits.maxArrayElements = 2u;
    limits.maxArrayIndex = 1u;
    RuntimeContext bounded(limits);
    const ScriptResult setup = run(bounded,
        "var values=[1,2];var cursor=values.values();");
    const ScriptResult growthFailure = runSameRealm(bounded,
        "values.push(3);");
    RuntimeObjectId source = kInvalidRuntimeObjectId;
    std::size_t index = 0u;
    bool exhausted = false;
    const Value* cursor = binding(bounded, "cursor");
    const bool stateSafe = cursor != nullptr && cursor->isObject() &&
        NavigatorScriptRuntimeTestAccess::iteratorState(bounded,
            cursor->objectId(), source, index, exhausted) && index == 0u &&
        !exhausted;
    expect(setup.succeeded() && !growthFailure.succeeded() &&
        growthFailure.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        stateSafe,
        "JS87 bounded Array growth fails deterministically without advancing its iterator");
}

void testReceiversPairsAndStaleElements()
{
    NavigatorScriptExecutionHarness unbound;
    if (!loadHtml(unbound, "<html><body></body></html>",
            "JS87 receiver fixture loads")) return;
    const ScriptResult unboundResult = unbound.execute(
        "var values=[1];var method=values.values;method();");
    expect(!unboundResult.succeeded() &&
        unboundResult.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "JS87 Array iterator factory rejects an unbound receiver");

    NavigatorScriptExecutionHarness wrongNext;
    if (!loadHtml(wrongNext, "<html><body></body></html>",
            "JS87 next receiver fixture loads")) return;
    const ScriptResult wrongNextResult = wrongNext.execute(
        "var cursor=[1].values();var next=cursor.next;next();");
    expect(!wrongNextResult.succeeded() &&
        wrongNextResult.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "JS87 Array iterator next rejects an unbound or incorrect receiver");

    NavigatorScriptExecutionHarness wrongSelf;
    if (!loadHtml(wrongSelf, "<html><body></body></html>",
            "JS87 self receiver fixture loads")) return;
    const ScriptResult wrongSelfResult = wrongSelf.execute(
        "var cursor=[1].values();var self=cursor[Symbol.iterator];self();");
    expect(!wrongSelfResult.succeeded() &&
        wrongSelfResult.runtimeError.code == RuntimeErrorCode::InvalidReceiver,
        "JS87 Array iterator self-return method rejects an unbound receiver");

    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='same' class='row'></i>"
            "<i id='other' class='row'></i></body></html>",
            "JS87 JS81 pair fixture loads")) return;
    const HostGenerationId oldGeneration = harness.hostAdapter().generation();
    const HostInstanceId oldSerial = [&harness]() {
        for (const auto& element : harness.document().structuralElements)
            if (element.id == "same") return element.serial;
        return HostInstanceId(0u);
    }();
    const ScriptResult pairResult = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var entries=rows.entries();var outer=entries.next();"
        "var pair=outer.value;var pairCursor=pair[Symbol.iterator]();"
        "var first=pairCursor.next();var second=pairCursor.next();"
        "var end=pairCursor.next();"
        "var pairOk=pair.length===2&&pair[0]===0&&pair[1]===rows[0]&&"
        "first.value===0&&first.value!=='0'&&"
            "second.value===rows[0]&&end.done===true&&end.value===undefined;"
        "var entry2=entries.next();"
        "var stable=outer.value===pair&&outer.value[0]===0&&"
            "outer.done===false&&pair[1]===rows[0]&&"
            "entry2.value[0]===1&&entry2.value[1]===rows[1];");
    if (!pairResult.succeeded()) {
        std::cerr << "  JS87 pair runtime="
            << runtimeErrorCodeName(pairResult.runtimeError.code)
            << " parser=" << parserErrorCodeName(pairResult.parserError.code)
            << '@' << pairResult.parserError.location.line << ':'
            << pairResult.parserError.location.column << '\n';
    }
    expect(pairResult.succeeded() && booleanIs(harness.runtime(), "pairOk") &&
        booleanIs(harness.runtime(), "stable"),
        "JS87 iterates genuine JS81 entry Arrays and retains pair and result identities");
    RuntimeErrorCode invalidationError = RuntimeErrorCode::None;
    const bool invalidated = harness.invalidateDocumentGeneration(
        invalidationError);
    harness.document() = gxos::web::parseHtml(
        "file:///js87-generation-replacement.html",
        "<html><body><i id='same' class='row'></i>"
        "<i id='other' class='row'></i></body></html>");
    harness.hostAdapter().attachDocument(harness.document(),
        harness.runtime().hostGeneration());
    const ScriptResult staleElement = harness.execute(
        "var staleId=pair[1].id;");
    expect(invalidated && invalidationError == RuntimeErrorCode::None &&
        harness.hostAdapter().generation() != oldGeneration && oldSerial != 0u &&
        !staleElement.succeeded() && staleElement.runtimeError.code ==
            RuntimeErrorCode::StaleHostObject,
        "JS87 pair Array iteration preserves stale Element protection after document replacement");
}

void testCollectionAndArrayIteratorCapacityAccounting()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i class='row'></i><i class='row'></i></body></html>",
            "JS87 mixed iterator-capacity fixture loads")) return;
    const ScriptResult collection = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var domCursor=rows.values();");
    const std::size_t objectsAfterDom = harness.runtime().objectCount();
    const std::size_t domRecords =
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter());
    const ScriptResult array = harness.execute(
        "var values=[1,2];var arrayCursor=values.values();");
    const std::size_t objectsAfterArray = harness.runtime().objectCount();
    const std::size_t recordsAfterArray =
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter());
    const ScriptResult interleaved = harness.execute(
        "var domValue=domCursor.next().value;"
        "var arrayValue=arrayCursor.next().value;"
        "var interleavedOk=domValue===rows[0]&&arrayValue===1;");
    expect(collection.succeeded() && array.succeeded() &&
        interleaved.succeeded() && domRecords == 1u &&
        objectsAfterArray == objectsAfterDom + 2u &&
        recordsAfterArray == domRecords &&
        booleanIs(harness.runtime(), "interleavedOk"),
        "JS87 Array cursors use bounded runtime objects while DOM cursors retain the existing 128-record registry");
}

void testObjectPoolAndFailedResult()
{
    RuntimeLimits limits;
    limits.maxObjects = 64u;
    RuntimeContext runtime(limits);
    const ScriptResult setup = run(runtime,
        "var values=[1,2];var cursor=values.values();");
    const Value* cursorValue = binding(runtime, "cursor");
    RuntimeObjectId source = kInvalidRuntimeObjectId;
    std::size_t index = 0u;
    bool exhausted = false;
    const bool beforeState = setup.succeeded() && cursorValue != nullptr &&
        cursorValue->isObject() &&
        NavigatorScriptRuntimeTestAccess::iteratorState(runtime,
            cursorValue->objectId(), source, index, exhausted) && index == 0u &&
        !exhausted;
    const bool filled = beforeState &&
        NavigatorScriptRuntimeTestAccess::fillObjectPool(runtime);
    const ScriptResult nextFailure = runSameRealm(runtime,
        "cursor.next();");
    const bool afterState = cursorValue != nullptr && cursorValue->isObject() &&
        NavigatorScriptRuntimeTestAccess::iteratorState(runtime,
            cursorValue->objectId(), source, index, exhausted) && index == 0u &&
        !exhausted;
    expect(filled && !nextFailure.succeeded() &&
        nextFailure.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
        afterState,
        "JS87 result-object exhaustion is deterministic and does not advance the Array cursor");
    std::cout << "JS87 object-pool limit=" << limits.maxObjects
        << ", full-pool objects=" << runtime.objectCount()
        << ", iterator-object-size="
        << NavigatorScriptRuntimeTestAccess::objectBytes() << " bytes\n";
}

std::string arrayLiteral(std::size_t count)
{
    std::string source = "var values=[";
    for (std::size_t index = 0u; index < count; ++index) {
        if (index != 0u) source += ",";
        source += std::to_string(index);
    }
    source += "];";
    return source;
}

std::int64_t medianMicroseconds(std::size_t size, const std::string& setup,
    const std::string& operation)
{
    std::vector<std::int64_t> samples;
    for (std::size_t sample = 0u; sample < 5u; ++sample) {
        RuntimeContext runtime;
        const ScriptResult setupResult = run(runtime, setup);
        if (!setupResult.succeeded()) return -1;
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult operationResult = runSameRealm(runtime,
            operation);
        const auto end = std::chrono::steady_clock::now();
        if (!operationResult.succeeded()) return -1;
        samples.push_back(std::chrono::duration_cast<
            std::chrono::microseconds>(end - start).count());
    }
    (void)size;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2u];
}

std::int64_t medianFailureMicroseconds(const std::string& setup,
    const std::string& operation, RuntimeErrorCode expectedError)
{
    std::vector<std::int64_t> samples;
    for (std::size_t sample = 0u; sample < 5u; ++sample) {
        RuntimeContext runtime;
        if (!run(runtime, setup).succeeded()) return -1;
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult operationResult = runSameRealm(runtime, operation);
        const auto end = std::chrono::steady_clock::now();
        if (operationResult.succeeded() ||
            operationResult.runtimeError.code != expectedError) return -1;
        samples.push_back(std::chrono::duration_cast<
            std::chrono::microseconds>(end - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2u];
}

std::string advanceSource(std::size_t count)
{
    std::string source;
    for (std::size_t index = 0u; index < count; ++index)
        source += "cursor.next();";
    return source;
}

void testCapacityBenchmarksAndAccounting()
{
    std::cout << "JS87 median hosted native operation samples (microseconds, "
        "5 fresh realms; operation parse/evaluation included):\n";
    for (const std::size_t size : {16u, 64u, 128u, 256u, 512u}) {
        const std::string construction = arrayLiteral(size);
        const std::string withCursor = construction +
            "var cursor=values[Symbol.iterator]();";
        const std::string create = "var cursor=values.values();";
        const std::string nextSetup = withCursor;
        const std::string middleSetup = nextSetup + advanceSource(size / 2u);
        const std::string lastSetup = nextSetup + advanceSource(size - 1u);
        const std::string endSetup = nextSetup + advanceSource(size);
        const std::string endedSetup = endSetup + "cursor.next();";
        const std::string traversal = "var sum=0;for(const value of values){"
            "sum=sum+value;}";
        const std::string explicitTraversal =
            "var cursor=values.values();var sum=0;"
            "for(const value of cursor){sum=sum+value;}";
        const std::string mutationSetup = nextSetup + "cursor.next();";
        const std::string mutation =
            "values.push(999);var changed=cursor.next();";
        const std::string nestedSetup = "var groups=[" + construction.substr(
            std::string("var values=").size(), construction.size() -
                std::string("var values=").size() - 1u) + "];";
        const std::int64_t constructionUs = medianMicroseconds(size,
            "var values=[];", construction.substr(
                std::string("var values=").size()));
        const std::int64_t createUs = medianMicroseconds(size, withCursor,
            create);
        const std::int64_t firstUs = medianMicroseconds(size, nextSetup,
            "var step=cursor.next();");
        const std::int64_t middleUs = medianMicroseconds(size, middleSetup,
            "var step=cursor.next();");
        const std::int64_t lastUs = medianMicroseconds(size, lastSetup,
            "var step=cursor.next();");
        const std::int64_t exhaustionUs = medianMicroseconds(size, endSetup,
            "var step=cursor.next();");
        const std::int64_t repeatedUs = medianMicroseconds(size, endedSetup,
            "var step=cursor.next();");
        const std::int64_t forOfUs = medianMicroseconds(size, construction,
            traversal);
        const std::int64_t explicitUs = medianMicroseconds(size, construction,
            explicitTraversal);
        const std::int64_t nestedUs = medianMicroseconds(size, nestedSetup,
            "var count=0;for(const group of groups){"
                "for(const value of group){count++;}}");
        const std::int64_t mutationUs = medianMicroseconds(size,
            mutationSetup, mutation);
        expect(constructionUs >= 0 && createUs >= 0 && firstUs >= 0 &&
            middleUs >= 0 && lastUs >= 0 && exhaustionUs >= 0 &&
            repeatedUs >= 0 && forOfUs >= 0 && explicitUs >= 0 &&
            nestedUs >= 0 && mutationUs >= 0,
            "JS87 Array creation, next, exhaustion, traversal, and nested benchmarks complete");
        std::cout << "  n=" << size << " construct=" << constructionUs
            << " create=" << createUs << " first=" << firstUs
            << " middle=" << middleUs << " last=" << lastUs
            << " exhaust=" << exhaustionUs << " repeat=" << repeatedUs
            << " for-of=" << forOfUs << " explicit=" << explicitUs
            << " nested=" << nestedUs << " mutation=" << mutationUs
            << '\n';
    }

    const std::int64_t capacityFailureUs = medianFailureMicroseconds(
        arrayLiteral(1024u), "values.push(1024);",
        RuntimeErrorCode::ArrayLimitExceeded);
    expect(capacityFailureUs >= 0,
        "JS87 near-capacity Array growth fails within the configured bound");
    std::cout << "JS87 n=1024 near-capacity push failure="
        << capacityFailureUs << " microseconds\n";

    RuntimeContext accounting;
    const std::size_t objectsBefore = accounting.objectCount();
    const std::size_t propertiesBefore = accounting.propertyCount();
    const std::size_t slotsBefore = accounting.arrayElementCount();
    const ScriptResult constructed = run(accounting,
        "var values=[1,2,3];");
    const std::size_t objectsAfterArray = accounting.objectCount();
    const std::size_t slotsAfterArray = accounting.arrayElementCount();
    const ScriptResult iterator = runSameRealm(accounting,
        "var cursor=values.values();");
    const std::size_t objectsAfterIterator = accounting.objectCount();
    const std::size_t propertiesAfterIterator = accounting.propertyCount();
    const ScriptResult next = runSameRealm(accounting,
        "var first=cursor.next();");
    expect(constructed.succeeded() && iterator.succeeded() && next.succeeded() &&
        objectsAfterArray == objectsBefore + 1u &&
        slotsAfterArray == slotsBefore + 3u &&
        objectsAfterIterator == objectsAfterArray + 1u &&
        propertiesAfterIterator == propertiesBefore + 2u &&
        accounting.objectCount() == objectsAfterIterator + 1u &&
        accounting.propertyCount() == propertiesBefore + 4u,
        "JS87 accounting separates Array slots, iterator object, and result object plus two properties");
}

} // namespace

int main()
{
    testHostedFixture();
    testProtocolValuesAndForOf();
    testMutationAndDenseGapBehavior();
    testReceiversPairsAndStaleElements();
    testObjectPoolAndFailedResult();
    testCollectionAndArrayIteratorCapacityAccounting();
    testCapacityBenchmarksAndAccounting();
    std::cout << "Navigator JavaScript JS87 checks: " << checks - failures
        << '/' << checks << " passed\n";
    return failures == 0u ? 0 : 1;
}
