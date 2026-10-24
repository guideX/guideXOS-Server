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
    using ArrayElement = RuntimeContext::RuntimeArrayElement;

    static std::size_t objectBytes() { return sizeof(Object); }
    static std::size_t arrayElementBytes() { return sizeof(ArrayElement); }

    static bool arrayMetrics(const RuntimeContext& runtime, RuntimeObjectId id,
        std::size_t& length, std::size_t& occupied, std::size_t& capacity)
    {
        const Object* object = runtime.objectAt(id);
        if (object == nullptr || !object->array) return false;
        length = object->arrayLength;
        occupied = object->elements.size();
        capacity = object->elements.capacity();
        return true;
    }

    static bool hasOwnArrayElement(const RuntimeContext& runtime,
        RuntimeObjectId id, std::size_t index)
    {
        const Object* object = runtime.objectAt(id);
        if (object == nullptr || !object->array) return false;
        const std::size_t position =
            RuntimeContext::findArrayElementPosition(*object, index);
        return index < object->arrayLength &&
            position < object->elements.size() &&
            object->elements[position].index == index;
    }

    static void failNextArrayCreation(RuntimeContext& runtime)
    {
        runtime.failNextArrayCreationForTesting_ = true;
    }

    static void failNextArraySlotInsertion(RuntimeContext& runtime)
    {
        runtime.failNextArraySlotInsertionForTesting_ = true;
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

double numberIs(const RuntimeContext& runtime, const char* name)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isNumber() ? value->numberValue() : -1.0;
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
    const bool loaded = harness.loadHtml("file:///js88-native.html", html,
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

void testHostedFixtureAndNestedEvent()
{
    std::ifstream file("navigator-smoke/javascript-js88.html",
        std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, html, "JS88 hosted fixture loads natively")) return;
    const std::string source = fixtureSource(html);
    expect(!source.empty() && source.find('<') == std::string::npos &&
        harness.document().scriptSources.size() == 1u &&
        harness.document().scriptSources[0] == source,
        "JS88 hosted inline source preserves the HTML-subset integrity guard");
    const ScriptResult result = harness.execute(source);
    const Value* marker = binding(harness.runtime(), "marker");
    const std::string markerText = marker != nullptr && marker->isString()
        ? harness.runtime().stringValue(*marker) : std::string();
    if (!result.succeeded()) {
        std::cerr << "  JS88 hosted fixture runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << '@'
            << result.runtimeError.location.line << ':'
            << result.runtimeError.location.column << " parserAt="
            << result.parserError.location.line << ':'
            << result.parserError.location.column << " source=" <<
            source.substr(result.runtimeError.location.offset > 50u
                ? result.runtimeError.location.offset - 50u : 0u, 140u)
            << " marker=" << markerText
            << '\n';
    }
    if (!result.succeeded() ||
        markerText.find(":failures=0:") == std::string::npos) {
        std::cerr << "  JS88 marker=" << markerText << '\n';
    }
    expect(result.succeeded() && markerText.find("JS88:assertions=") == 0u &&
        markerText.find(":failures=0:") != std::string::npos,
        "JS88 hosted fixture reports passing sparse Arrays and writable length");

    HostInstanceId eventASerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventASerial = element.serial;
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    const bool clicked = eventASerial != 0u &&
        harness.dispatchClick(eventASerial, clickError);
    if (!(clicked && clickError == RuntimeErrorCode::None &&
            booleanIs(harness.runtime(), "nestedEventOk") &&
            booleanIs(harness.runtime(), "outerEventOk"))) {
        std::cerr << "  JS88 Event click=" << clicked << " error="
            << runtimeErrorCodeName(clickError) << " nested="
            << booleanIs(harness.runtime(), "nestedEventOk") << " outer="
            << booleanIs(harness.runtime(), "outerEventOk") << '\n';
    }
    expect(clicked && clickError == RuntimeErrorCode::None &&
        booleanIs(harness.runtime(), "nestedEventOk") &&
        booleanIs(harness.runtime(), "outerEventOk"),
        "JS88 sparse mutation survives nested Event callbacks and metadata");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS88 nested Event frames unwind completely");
}

void testHolesAssignmentsAndLiteralGrammar()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var sparseLiteral=[10,,30];"
        "var explicitUndefined=[10,undefined,30];"
        "var oneHole=[,];var twoHoles=[,,];"
        "var trailing=[1,];var trailingHole=[1,,];"
        "var nested=[[,,],10,,];"
        "var literalOk=sparseLiteral.length===3&&"
            "sparseLiteral[0]===10&&sparseLiteral[1]===undefined&&"
            "sparseLiteral[2]===30&&"
            "!sparseLiteral.hasOwnProperty('1')&&"
            "explicitUndefined.length===3&&"
            "explicitUndefined.hasOwnProperty('1')&&"
            "explicitUndefined[1]===undefined&&oneHole.length===1&&"
            "!oneHole.hasOwnProperty('0')&&twoHoles.length===2&&"
            "!twoHoles.hasOwnProperty('0')&&!twoHoles.hasOwnProperty('1')&&"
            "trailing.length===1&&trailing.hasOwnProperty('0')&&"
            "trailingHole.length===2&&trailingHole.hasOwnProperty('0')&&"
            "!trailingHole.hasOwnProperty('1')&&nested.length===3&&"
            "nested[0].length===2&&!nested[0].hasOwnProperty('0')&&"
            "!nested.hasOwnProperty('2');"
        "var holeCount=0;for(const value of sparseLiteral)holeCount++;"
        "var sparseWrite=[];sparseWrite[3]=40;"
        "var undefinedWrite=[];undefinedWrite[2]=undefined;"
        "var assignmentOk=sparseWrite.length===4&&"
            "sparseWrite[0]===undefined&&sparseWrite[2]===undefined&&"
            "sparseWrite[3]===40&&!sparseWrite.hasOwnProperty('0')&&"
            "!sparseWrite.hasOwnProperty('2')&&"
            "sparseWrite.hasOwnProperty('3')&&"
            "undefinedWrite.length===3&&undefinedWrite[2]===undefined&&"
            "undefinedWrite.hasOwnProperty('2');"
        "var sparsePop=[10,,];var popped=sparsePop.pop();"
        "var pushLength=sparsePop.push(30);"
        "var popPushOk=popped===undefined&&sparsePop.length===2&&"
            "sparsePop[1]===30&&sparsePop.hasOwnProperty('1')&&"
            "pushLength===2;");
    expect(result.succeeded() && booleanIs(runtime, "literalOk") &&
        booleanIs(runtime, "assignmentOk") && booleanIs(runtime, "popPushOk") &&
        numberIs(runtime, "holeCount") == 3.0,
        "JS88 preserves hole occupancy, explicit undefined, elision lengths, and Array value iteration");

    const RuntimeObjectId sparseId = objectId(runtime, "sparseWrite");
    std::size_t length = 0u;
    std::size_t occupied = 0u;
    std::size_t capacity = 0u;
    const bool metrics = NavigatorScriptRuntimeTestAccess::arrayMetrics(
        runtime, sparseId, length, occupied, capacity);
    expect(metrics && length == 4u && occupied == 1u &&
        NavigatorScriptRuntimeTestAccess::hasOwnArrayElement(runtime,
            sparseId, 3u) &&
        !NavigatorScriptRuntimeTestAccess::hasOwnArrayElement(runtime,
            sparseId, 2u),
        "native storage proves sparse assignment stores only the present target");

    std::size_t allHoleLength = 0u;
    std::size_t allHoleOccupied = 0u;
    std::size_t allHoleCapacity = 0u;
    const bool allHoleMetrics = NavigatorScriptRuntimeTestAccess::arrayMetrics(
        runtime, objectId(runtime, "twoHoles"), allHoleLength,
        allHoleOccupied, allHoleCapacity);
    expect(allHoleMetrics && allHoleLength == 2u && allHoleOccupied == 0u &&
        allHoleCapacity == 0u,
        "all-hole Array literals reserve no indexed storage records");

    RuntimeContext malformed;
    const ScriptResult invalid = run(malformed, "var broken=[1,,");
    expect(!invalid.succeeded() && invalid.status == ScriptStatus::ParseFailure,
        "unterminated elision literal fails during parsing without publishing an Array");
}

void testWritableLengthAndReferenceLifetime()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var retained={marker:17};var nested=[retained];"
        "var values=[10,20,retained,40];var alias=values[2];"
        "var assigned=(values.length=4);"
        "values.length=2;var shrunk=values.length===2&&"
            "values[0]===10&&values[1]===20&&"
            "!values.hasOwnProperty('2')&&"
            "!values.hasOwnProperty('3');"
        "values.length=5;var grown=values.length===5&&"
            "values[2]===undefined&&values[4]===undefined&&"
            "!values.hasOwnProperty('2')&&!values.hasOwnProperty('4');"
        "values[4]=undefined;var explicitAfterGrowth="
            "values.hasOwnProperty('4')&&values[4]===undefined;"
        "values.length=1;values.length=4;"
        "var removedStayGone=values[1]===undefined&&"
            "!values.hasOwnProperty('1')&&values[2]===undefined&&"
            "!values.hasOwnProperty('2')&&retained.marker===17&&"
            "nested[0]===retained&&alias===retained;"
        "var returnLength=values.push(50);var popValue=values.pop();"
        "var pushPopOk=returnLength===5&&popValue===50&&values.length===4;");
    expect(result.succeeded() && booleanIs(runtime, "shrunk") &&
        booleanIs(runtime, "grown") &&
        booleanIs(runtime, "explicitAfterGrowth") &&
        booleanIs(runtime, "removedStayGone") &&
        booleanIs(runtime, "pushPopOk") &&
        numberIs(runtime, "assigned") == 4.0,
        "JS88 length shrink removes elements, growth creates holes, and removed values stay gone");
    expect(runtime.arrayElementCount() == 2u,
        "JS88 occupied-slot accounting releases removed entries without reclaiming objects");

    const ScriptResult malformed = runSameRealm(runtime,
        "values.length=0/0;");
    expect(!malformed.succeeded() &&
        malformed.runtimeError.code == RuntimeErrorCode::InvalidArrayLength,
        "NaN length is rejected with a deterministic invalid-length error");
    const ScriptResult preserved = runSameRealm(runtime,
        "var afterNaN=values.length===4&&values[0]===10&&"
            "values[3]===undefined&&!values.hasOwnProperty('3')&&"
            "retained.marker===17;");
    expect(preserved.succeeded() && booleanIs(runtime, "afterNaN"),
        "invalid length leaves old length, elements, and aliases unchanged");
}

void testInvalidLengthAssignments()
{
    const std::vector<std::pair<std::string, RuntimeErrorCode>> invalid = {
        {"-1", RuntimeErrorCode::InvalidArrayLength},
        {"2.5", RuntimeErrorCode::InvalidArrayLength},
        {"4294967296", RuntimeErrorCode::InvalidArrayLength},
        {"1/0", RuntimeErrorCode::InvalidArrayLength},
        {"'2'", RuntimeErrorCode::TypeError},
        {"null", RuntimeErrorCode::TypeError},
        {"1025", RuntimeErrorCode::ArrayLimitExceeded},
        {"4294967295", RuntimeErrorCode::ArrayLimitExceeded},
    };
    for (const auto& entry : invalid) {
        RuntimeContext runtime;
        const ScriptResult setup = run(runtime,
            "var values=[11,22,33];var object={marker:5};"
            "values[1]=object;");
        const ScriptResult rejected = setup.succeeded()
            ? runSameRealm(runtime, "values.length=" + entry.first + ";")
            : ScriptResult();
        const ScriptResult intact = rejected.succeeded()
            ? ScriptResult()
            : runSameRealm(runtime,
                "var intact=values.length===3&&values[0]===11&&"
                    "values[1]===object&&values[2]===33;");
        expect(setup.succeeded() && !rejected.succeeded() &&
            rejected.runtimeError.code == entry.second && intact.succeeded() &&
            booleanIs(runtime, "intact"),
            "invalid or unsupported Array length assignment is atomic");
    }

    RuntimeLimits limits;
    limits.maxArrayElements = 8u;
    limits.maxArrayIndex = 7u;
    limits.maxTotalArrayElements = 2u;
    RuntimeContext bounded(limits);
    const ScriptResult setup = run(bounded, "var values=[1,2];");
    const ScriptResult failedWrite = runSameRealm(bounded, "values[4]=9;");
    const ScriptResult boundedLength = runSameRealm(bounded,
        "var intact=values.length===2&&values[0]===1&&values[1]===2&&"
            "!values.hasOwnProperty('4');values.length=8;");
    const ScriptResult afterGrowth = runSameRealm(bounded,
        "var grown=values.length===8&&values[7]===undefined&&"
            "!values.hasOwnProperty('7');values.length=0;");
    const ScriptResult reusableSlot = runSameRealm(bounded,
        "values.length=8;values[7]=7;");
    expect(setup.succeeded() && !failedWrite.succeeded() &&
        failedWrite.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        boundedLength.succeeded() && booleanIs(bounded, "intact") &&
        afterGrowth.succeeded() && booleanIs(bounded, "grown") &&
        reusableSlot.succeeded() && bounded.arrayElementCount() == 1u,
        "slot-pool exhaustion preserves assignment state while bounded length growth allocates no holes");

    RuntimeContext loop;
    const ScriptResult loopSetup = run(loop,
        "var values=[1,2,3];var cursor=values.values();"
        "var first=cursor.next();");
    const ScriptResult invalidDuringLoop = runSameRealm(loop,
        "values.length=-1;");
    const ScriptResult loopContinues = runSameRealm(loop,
        "var next=cursor.next().value;var loopSafe=first.value===1&&"
            "next===2&&values.length===3;");
    expect(loopSetup.succeeded() && !invalidDuringLoop.succeeded() &&
        invalidDuringLoop.runtimeError.code ==
            RuntimeErrorCode::InvalidArrayLength &&
        loopContinues.succeeded() && booleanIs(loop, "loopSafe"),
        "invalid length assignment preserves the running iterator position and loop state");
}

void testAllocationFailureAndObjectLifetime()
{
    RuntimeContext creation;
    const ScriptResult setup = run(creation, "var keep=[1];");
    const std::size_t objectsBefore = creation.objectCount();
    const std::size_t slotsBefore = creation.arrayElementCount();
    NavigatorScriptRuntimeTestAccess::failNextArrayCreation(creation);
    const ScriptResult failedCreate = runSameRealm(creation,
        "var unpublished=[1,,3];");
    expect(setup.succeeded() && !failedCreate.succeeded() &&
        failedCreate.runtimeError.code == RuntimeErrorCode::AllocationFailure &&
        creation.objectCount() == objectsBefore &&
        creation.arrayElementCount() == slotsBefore,
        "injected Array construction allocation failure publishes no partial Array or slots");

    RuntimeContext insertion;
    const ScriptResult insertSetup = run(insertion,
        "var values=[];values.length=4;");
    const std::size_t insertObjectsBefore = insertion.objectCount();
    const std::size_t insertSlotsBefore = insertion.arrayElementCount();
    NavigatorScriptRuntimeTestAccess::failNextArraySlotInsertion(insertion);
    const ScriptResult failedInsert = runSameRealm(insertion,
        "values[3]=8;");
    const ScriptResult insertPreserved = runSameRealm(insertion,
        "var intact=values.length===4&&values[3]===undefined&&"
            "!values.hasOwnProperty('3');");
    expect(insertSetup.succeeded() && !failedInsert.succeeded() &&
        failedInsert.runtimeError.code == RuntimeErrorCode::AllocationFailure &&
        insertPreserved.succeeded() && booleanIs(insertion, "intact") &&
        insertion.objectCount() == insertObjectsBefore &&
        insertion.arrayElementCount() == insertSlotsBefore,
        "injected sparse-slot allocation failure preserves logical length and occupancy");

    RuntimeContext lifetime;
    const ScriptResult lifetimeResult = run(lifetime,
        "var retained={id:71};var nested=[retained];"
        "var values=[retained,nested];var removed=values[0];"
        "values.length=0;var objectStillLive=removed===retained&&"
            "retained.id===71&&nested[0]===retained;");
    expect(lifetimeResult.succeeded() && booleanIs(lifetime,
        "objectStillLive") && lifetime.arrayElementCount() == 1u,
        "length shrink drops Array edges without reclaiming retained ordinary objects");
}

void testIterationMutationAndPrototypeLookup()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var values=[10,,30];var cursor=values[Symbol.iterator]();"
        "var first=cursor.next();var hole=cursor.next();"
        "var third=cursor.next();var end=cursor.next();"
        "var iterationOk=first.value===10&&hole.value===undefined&&"
            "hole.done===false&&third.value===30&&end.done===true;"
        "var live=[1];var liveCursor=live.values();"
        "var liveFirst=liveCursor.next();live.length=3;live[2]=3;"
        "var liveHole=liveCursor.next();var liveThird=liveCursor.next();"
        "var liveOk=liveFirst.value===1&&liveHole.value===undefined&&"
            "liveHole.done===false&&liveThird.value===3;"
        "var shrinking=[1,2,3];var shrinkCursor=shrinking.values();"
        "var shrinkFirst=shrinkCursor.next();shrinking.length=1;"
        "var shrinkEnd=shrinkCursor.next();shrinking.length=3;"
        "var stillEnd=shrinkCursor.next();"
        "var shrinkOk=shrinkFirst.value===1&&shrinkEnd.done===true&&"
            "stillEnd.done===true;"
        "var zero=[1,2];var zeroCursor=zero.values();"
        "var zeroFirst=zeroCursor.next();zero.length=0;"
        "var zeroEnd=zeroCursor.next();zero.length=2;"
        "var zeroAgain=zeroCursor.next();"
        "var zeroOk=zeroFirst.value===1&&zeroEnd.done===true&&"
            "zeroAgain.done===true;"
        "var forOfCount=0;for(const value of values)forOfCount++;"
        "var allOk=iterationOk&&liveOk&&shrinkOk&&zeroOk&&"
            "forOfCount===3;");
    expect(result.succeeded() && booleanIs(runtime, "allOk"),
        "JS88 Array iterators yield holes, observe live length, and remain terminal after exhaustion");

    const ScriptResult inheritedSetup = run(runtime,
        "var inherited=[10,,30];var inheritedCursor=inherited.values();"
        "var inheritedFirst=inheritedCursor.next();var proto={};"
        "proto['1']=77;");
    const RuntimeObjectId inheritedId = objectId(runtime, "inherited");
    const RuntimeObjectId prototypeId = objectId(runtime, "proto");
    const bool setPrototype = inheritedSetup.succeeded() &&
        runtime.setPrototypeForTesting(inheritedId, prototypeId);
    Value inheritedValue;
    RuntimeErrorCode readError = RuntimeErrorCode::None;
    const bool readInherited = setPrototype &&
        runtime.readPropertyForTesting(inheritedId, "1", inheritedValue,
            readError);
    const ScriptResult inheritedIteration = setPrototype
        ? runSameRealm(runtime,
            "var inheritedSecond=inheritedCursor.next();"
            "var ownHole=!inherited.hasOwnProperty('1');")
        : ScriptResult();
    expect(readInherited && readError == RuntimeErrorCode::None &&
        inheritedValue.isNumber() && inheritedValue.numberValue() == 77.0 &&
        inheritedIteration.succeeded() && booleanIs(runtime, "ownHole") &&
        binding(runtime, "inheritedSecond") != nullptr &&
        binding(runtime, "inheritedSecond")->isObject(),
        "hole reads and iterators preserve inherited indexed lookup while own occupancy stays absent");
    if (inheritedIteration.succeeded()) {
        Value secondValue;
        RuntimeErrorCode resultError = RuntimeErrorCode::None;
        const Value* result = binding(runtime, "inheritedSecond");
        bool yielded = result != nullptr && result->isObject() &&
            runtime.readPropertyForTesting(result->objectId(), "value",
                secondValue, resultError);
        expect(yielded && resultError == RuntimeErrorCode::None &&
            secondValue.isNumber() && secondValue.numberValue() == 77.0,
            "Array values iterator yields an inherited value for an own hole");
    }
}

void testJS81EntryPairCompatibility()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='row-a' class='row'></i>"
            "<i id='row-b' class='row'></i></body></html>",
            "JS88 JS81 pair fixture loads")) return;
    const ScriptResult result = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var entries=rows.entries();var outer=entries.next();"
        "var pair=outer.value;var element=pair[1];"
        "var pairCursor=pair.values();var pairFirst=pairCursor.next();"
        "pair.length=1;var pairShrink=pair.length===1&&"
            "pair[0]===0&&pair[1]===undefined&&"
            "!pair.hasOwnProperty('1');"
        "var pairEnd=pairCursor.next();"
        "pair.length=2;var pairGrow=pair.length===2&&"
            "pair[1]===undefined&&!pair.hasOwnProperty('1');"
        "var pairFirstOk=pairFirst.value===0;"
        "var pairEndOk=pairEnd.done===true;"
        "var pairIdentityOk=element===rows[0]&&element.id==='row-a';"
        "var fresh=rows.entries().next().value;"
        "var pairFreshOk=fresh.length===2&&fresh.hasOwnProperty('0')&&"
            "fresh.hasOwnProperty('1');"
        "var pairOk=pairShrink&&pairGrow&&pairFirstOk&&pairEndOk&&"
            "pairIdentityOk&&pairFreshOk;");
    if (!result.succeeded()) {
        std::cerr << "  JS88 pair runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << '@'
            << result.parserError.location.line << ':'
            << result.parserError.location.column << '\n';
    }
    if (result.succeeded() && !booleanIs(harness.runtime(), "pairOk")) {
        std::cerr << "  JS88 pair checks: shrink="
            << booleanIs(harness.runtime(), "pairShrink") << " grow="
            << booleanIs(harness.runtime(), "pairGrow") << " first="
            << booleanIs(harness.runtime(), "pairFirstOk") << " end="
            << booleanIs(harness.runtime(), "pairEndOk") << " identity="
            << booleanIs(harness.runtime(), "pairIdentityOk") << " fresh="
            << booleanIs(harness.runtime(), "pairFreshOk") << '\n';
    }
    expect(result.succeeded() && booleanIs(harness.runtime(), "pairOk"),
        "JS81 native entry Arrays share writable length and regrow with a hole");
}

std::string denseArrayScript(std::size_t length)
{
    std::string source = "var values=[";
    for (std::size_t index = 0u; index < length; ++index) {
        if (index != 0u) source.push_back(',');
        source.push_back('1');
    }
    source += "];";
    return source;
}

std::string indexedWriteScript(std::size_t length, bool descending)
{
    std::string source = "var values=[];";
    if (descending) {
        source += "var index=" + std::to_string(length) +
            ";while(index>0){index--;values[index]=index;}";
    } else {
        source += "var index=0;while(index<" + std::to_string(length) +
            "){values[index]=index;index++;}";
    }
    return source;
}

std::string nestedIterationScript(std::size_t length)
{
    const std::string dense = denseArrayScript(length);
    const std::string arrayLiteral = dense.substr(
        std::string("var values=").size());
    return "var left=" + arrayLiteral + "var right=" + arrayLiteral +
        "var groups=[left,right];var total=0;"
        "for(const group of groups){for(const item of group)total=total+item;}";
}

std::uint64_t medianMicros(const std::string& source, std::size_t samples)
{
    std::vector<std::uint64_t> values;
    values.reserve(samples);
    for (std::size_t sample = 0u; sample < samples; ++sample) {
        RuntimeContext runtime;
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = run(runtime, source);
        const auto stop = std::chrono::steady_clock::now();
        expect(result.succeeded(), "performance qualification script executes");
        values.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                stop - start).count()));
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2u];
}

void testStorageAccountingAndPerformance()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var dense=[1,2,3,4];var sparse=[];sparse[3]=4;"
        "var grown=[];grown.length=4;");
    std::size_t denseLength = 0u;
    std::size_t denseOccupied = 0u;
    std::size_t denseCapacity = 0u;
    std::size_t sparseLength = 0u;
    std::size_t sparseOccupied = 0u;
    std::size_t sparseCapacity = 0u;
    std::size_t grownLength = 0u;
    std::size_t grownOccupied = 0u;
    std::size_t grownCapacity = 0u;
    const bool measured = result.succeeded() &&
        NavigatorScriptRuntimeTestAccess::arrayMetrics(runtime,
            objectId(runtime, "dense"), denseLength, denseOccupied,
            denseCapacity) &&
        NavigatorScriptRuntimeTestAccess::arrayMetrics(runtime,
            objectId(runtime, "sparse"), sparseLength, sparseOccupied,
            sparseCapacity) &&
        NavigatorScriptRuntimeTestAccess::arrayMetrics(runtime,
            objectId(runtime, "grown"), grownLength, grownOccupied,
            grownCapacity);
    expect(measured && denseLength == 4u && denseOccupied == 4u &&
        sparseLength == 4u && sparseOccupied == 1u &&
        grownLength == 4u && grownOccupied == 0u &&
        runtime.arrayElementCount() == 5u,
        "dense, sparse, and length-grown Arrays account only present slots");
    std::cout << "JS88 storage: RuntimeObject="
        << NavigatorScriptRuntimeTestAccess::objectBytes() << " B; slot="
        << NavigatorScriptRuntimeTestAccess::arrayElementBytes()
        << " B; dense(length=4,occupied=4,capacity=" << denseCapacity
        << "); sparse(length=4,occupied=1,capacity=" << sparseCapacity
        << "); grown(length=4,occupied=" << grownOccupied
        << ",capacity=" << grownCapacity << "); totalSlots="
        << runtime.arrayElementCount() << '/' <<
            kDefaultMaxJavaScriptArrayElementCount << '\n';

    const std::vector<std::size_t> sizes = {0u, 1u, 16u, 64u, 128u,
        256u, 512u, 1024u};
    std::cout << "JS88 benchmark median microseconds (5 fresh realms; parse/evaluation included)\n"
        << "length,denseCreate,sparseCreate,indexWrite,lengthRead,shrinkGrow,holeRead,forOf,nearAscWrite,nearDescWrite,nestedForOf\n";
    for (const std::size_t length : sizes) {
        const std::string dense = denseArrayScript(length);
        std::string sparse = "var values=[];values.length=" +
            std::to_string(length) + ";";
        if (length != 0u) sparse += "values[" +
            std::to_string(length - 1u) + "]=1;";
        const std::string indexWrite = length == 0u
            ? sparse : dense + "values[0]=2;";
        const std::string lengthRead = sparse + "var read=values.length;";
        const std::string shrinkGrow = dense + "values.length=0;values.length=" +
            std::to_string(length) + ";";
        const std::string holeRead = sparse + "var read=values[0];";
        const std::string iteration = dense +
            "var sum=0;for(const item of values)sum=sum+item;";
        const std::string ascendingWrites = indexedWriteScript(length, false);
        const std::string descendingWrites = indexedWriteScript(length, true);
        const std::string nestedIteration = nestedIterationScript(length);
        const std::size_t sampleCount = 5u;
        const std::uint64_t denseUs = medianMicros(dense, sampleCount);
        const std::uint64_t sparseUs = medianMicros(sparse, sampleCount);
        const std::uint64_t writeUs = medianMicros(indexWrite, sampleCount);
        const std::uint64_t lengthUs = medianMicros(lengthRead, sampleCount);
        const std::uint64_t shrinkUs = medianMicros(shrinkGrow, sampleCount);
        const std::uint64_t holeUs = medianMicros(holeRead, sampleCount);
        const std::uint64_t forOfUs = length <= 512u
            ? medianMicros(iteration, sampleCount) : 0u;
        const std::uint64_t ascendingUs = length == 0u
            ? 0u : medianMicros(ascendingWrites, 3u);
        const std::uint64_t descendingUs = length == 0u
            ? 0u : medianMicros(descendingWrites, 3u);
        const std::uint64_t nestedUs = length <= 256u
            ? medianMicros(nestedIteration, 3u) : 0u;
        std::cout << length << ',' << denseUs << ',' << sparseUs << ','
            << writeUs << ',' << lengthUs << ',' << shrinkUs << ',' << holeUs
            << ',' << forOfUs << ',' << ascendingUs << ',' << descendingUs
            << ',' << nestedUs << '\n';
    }
}

} // namespace

int main()
{
    testHostedFixtureAndNestedEvent();
    testHolesAssignmentsAndLiteralGrammar();
    testWritableLengthAndReferenceLifetime();
    testInvalidLengthAssignments();
    testAllocationFailureAndObjectLifetime();
    testIterationMutationAndPrototypeLookup();
    testJS81EntryPairCompatibility();
    testStorageAccountingAndPerformance();

    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS88 checks: " << checks - failures
            << '/' << checks << " passed (" << failures << " failed)\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS88 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
