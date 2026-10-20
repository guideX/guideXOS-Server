#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace gxos::javascript {

struct NavigatorScriptIteratorTestAccess {
    using Collection = NavigatorScriptHostAdapter::SelectorCollectionRecord;
    using Iterator =
        NavigatorScriptHostAdapter::SelectorCollectionIteratorRecord;

    static std::size_t collectionBytes() { return sizeof(Collection); }
    static std::size_t iteratorBytes() { return sizeof(Iterator); }
    static bool isKeys(const Iterator& iterator)
    {
        return iterator.mode ==
            NavigatorScriptHostAdapter::SelectorCollectionIteratorMode::Keys;
    }
    static bool isValues(const Iterator& iterator)
    {
        return iterator.mode ==
            NavigatorScriptHostAdapter::SelectorCollectionIteratorMode::Values;
    }
    static std::size_t activeCollectionCount(
        const NavigatorScriptHostAdapter& adapter)
    {
        std::size_t count = 0u;
        for (const Collection& record : adapter.selectorCollections_)
            if (record.active) ++count;
        return count;
    }
    static std::size_t activeIteratorCount(
        const NavigatorScriptHostAdapter& adapter)
    {
        std::size_t count = 0u;
        for (const Iterator& record : adapter.selectorCollectionIterators_)
            if (record.active) ++count;
        return count;
    }
    static bool iteratorAt(const NavigatorScriptHostAdapter& adapter,
        std::size_t index, Iterator& result)
    {
        if (index >= adapter.selectorCollectionIterators_.size() ||
            !adapter.selectorCollectionIterators_[index].active) return false;
        result = adapter.selectorCollectionIterators_[index];
        return true;
    }
};

struct NavigatorScriptRuntimeTestAccess {
    using Object = RuntimeContext::RuntimeObject;
    using Property = RuntimeContext::RuntimeProperty;
    using HostObject = RuntimeContext::HostObjectRecord;
    using HostMethod = RuntimeContext::HostMethodRecord;

    static std::size_t objectBytes() { return sizeof(Object); }
    static std::size_t propertyBytes() { return sizeof(Property); }
    static std::size_t objectVectorCapacityBytes(
        const RuntimeContext& runtime)
    {
        return runtime.objects_.capacity() * sizeof(Object);
    }
    static std::size_t hostObjectBytes() { return sizeof(HostObject); }
    static std::size_t hostObjectVectorCapacityBytes(
        const RuntimeContext& runtime)
    {
        return runtime.hostObjects_.capacity() * sizeof(HostObject);
    }
    static std::size_t hostMethodBytes() { return sizeof(HostMethod); }
    static std::size_t hostMethodCount(const RuntimeContext& runtime)
    {
        return runtime.hostMethodCount_;
    }
    static std::size_t hostMethodVectorCapacityBytes(
        const RuntimeContext& runtime)
    {
        return runtime.hostMethods_.capacity() * sizeof(HostMethod);
    }
    static std::size_t resultObjectCount(const RuntimeContext& runtime)
    {
        std::size_t count = 0u;
        for (const Object& object : runtime.objects_) {
            bool hasValue = false;
            bool hasDone = false;
            for (const Property& property : object.properties) {
                hasValue = hasValue || property.key == "value";
                hasDone = hasDone || property.key == "done";
            }
            if (hasValue && hasDone) ++count;
        }
        return count;
    }
    static std::size_t iteratorResultPropertyCapacityBytes(
        const RuntimeContext& runtime)
    {
        std::size_t bytes = 0u;
        for (const Object& object : runtime.objects_) {
            bool hasValue = false;
            bool hasDone = false;
            for (const Property& property : object.properties) {
                hasValue = hasValue || property.key == "value";
                hasDone = hasDone || property.key == "done";
            }
            if (hasValue && hasDone)
                bytes += object.properties.capacity() * sizeof(Property);
        }
        return bytes;
    }
};

} // namespace gxos::javascript

namespace {

using namespace gxos::javascript;

std::size_t checks = 0u;
std::size_t failures = 0u;

void expect(bool condition, const std::string& label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "  FAIL: " << label << '\n';
}

const Value* binding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    return harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
}

bool booleanIs(const NavigatorScriptExecutionHarness& harness,
    const char* name, bool expected)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isBoolean() &&
        value->booleanValue() == expected;
}

double numberIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isNumber() ? value->numberValue() : -1.0;
}

std::string stringIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isString()
        ? harness.runtime().stringValue(*value) : std::string();
}

bool loadHtml(NavigatorScriptExecutionHarness& harness,
    const std::string& html, const std::string& label)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js79-native-fixture.html",
        html, error) && error == RuntimeErrorCode::None;
    expect(loaded, label);
    return loaded;
}

std::string readFixture(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::string fixtureScript(const std::string& html)
{
    const std::size_t open = html.find("<script>");
    const std::size_t close = html.find("</script>", open);
    if (open == std::string::npos || close == std::string::npos || close < open)
        return std::string();
    return html.substr(open + 8u, close - open - 8u);
}

HostInstanceId elementSerial(const NavigatorScriptExecutionHarness& harness,
    const std::string& id)
{
    for (const auto& element : harness.document().structuralElements)
        if (element.id == id) return element.serial;
    return 0u;
}

struct IteratorProbe {
    std::size_t lengthCalls = 0u;
    std::size_t entryLength = 0u;
    std::size_t lookupCount = 0u;
    std::size_t indices[2048u]{};
};

void iteratorLengthProbe(void* context, std::size_t entryLength)
{
    auto* probe = static_cast<IteratorProbe*>(context);
    ++probe->lengthCalls;
    probe->entryLength = entryLength;
}

void iteratorLookupProbe(void* context, std::size_t requestedIndex)
{
    auto* probe = static_cast<IteratorProbe*>(context);
    if (probe->lookupCount < sizeof(probe->indices) / sizeof(probe->indices[0]))
        probe->indices[probe->lookupCount] = requestedIndex;
    ++probe->lookupCount;
}

void setIteratorProbes(NavigatorScriptExecutionHarness& harness,
    IteratorProbe& probe)
{
    harness.hostAdapter().setSelectorCollectionIteratorLengthProbe(
        iteratorLengthProbe, &probe);
    harness.hostAdapter().setSelectorCollectionIteratorLookupProbe(
        iteratorLookupProbe, &probe);
}

void clearIteratorProbes(NavigatorScriptExecutionHarness& harness)
{
    harness.hostAdapter().setSelectorCollectionIteratorLengthProbe(nullptr,
        nullptr);
    harness.hostAdapter().setSelectorCollectionIteratorLookupProbe(nullptr,
        nullptr);
}

void testHostedFixture()
{
    const std::string html = readFixture("navigator-smoke/javascript-js80.html");
    const std::string source = fixtureScript(html);
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = !source.empty() && harness.loadHtml(
        "file:///js80-hosted-fixture.html", html, error) &&
        error == RuntimeErrorCode::None;
    expect(loaded, "JS80 hosted fixture loads in the native harness");
    if (!loaded) return;
    expect(source.find('<') == std::string::npos &&
        harness.document().scriptSources.size() == 1u &&
        harness.document().scriptSources[0] == source,
        "JS80 inline source passes the HTML-subset script integrity guard");
    const ScriptResult result = harness.execute(source);
    if (!result.succeeded() || numberIs(harness, "failures") != 0.0) {
        std::cerr << "  JS80 fixture status=" << scriptStatusName(result.status)
            << " runtime=" << runtimeErrorCodeName(result.runtimeError.code)
            << " parser=" << parserErrorCodeName(result.parserError.code)
            << '@' << result.parserError.location.line << ':'
            << result.parserError.location.column << " assertions="
            << numberIs(harness, "assertions") << " failures="
            << numberIs(harness, "failures") << " marker="
            << stringIs(harness, "initialMarker") << '\n';
    }
    expect(result.succeeded() && numberIs(harness, "assertions") >= 15.0 &&
        numberIs(harness, "failures") == 0.0 &&
        stringIs(harness, "initialMarker").find(
            "JS80:initial:assertions=") == 0u,
        "JS80 fixture covers records, identity, independent cursors, mutation, scopes, adapters, and filtered selectors");

    RuntimeErrorCode eventError = RuntimeErrorCode::None;
    const bool clicked = harness.dispatchClick(elementSerial(harness, "event-a"),
        eventError);
    expect(clicked && eventError == RuntimeErrorCode::None &&
        booleanIs(harness, "eventValues", true) &&
        booleanIs(harness, "eventMetadata", true) &&
        booleanIs(harness, "eventAMetadata", true) &&
        booleanIs(harness, "eventBMetadata", true),
        "JS80 interleaves cursors through nested Event and some() execution with Event metadata intact");
    expect(harness.runtime().activeCallFrameCount() == 0u,
        "JS80 nested Event iterator frames unwind to zero");
}

void testReceiversAndResultTypes()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='a' class='row'></i><i id='b' class='row'></i>"
            "</body></html>", "JS80 receiver fixture loads")) return;
    const ScriptResult valid = harness.execute(
        "var rows=document.querySelectorAll('.row');var cursor=rows.values();"
        "var first=cursor.next();var second=cursor.next();"
        "var terminal=cursor.next();var terminalAgain=cursor.next();"
        "var resultsAreObjects=first!==undefined&&second!==undefined&&"
        "terminal!==undefined&&first!==second&&terminal!==terminalAgain;"
        "var types=first.done===false&&second.done===false&&"
        "terminal.done===true&&terminalAgain.done===true&&"
        "first.value===rows.item(0)&&first.value===rows[0]&&"
        "second.value===rows[1]&&terminal.value===undefined&&"
        "terminalAgain.value===undefined;"
        "var firstSaved=first.value===rows[0]&&first.done===false;"
        "var empty=document.querySelectorAll('.absent').values();"
        "var emptyFirst=empty.next();var emptyAgain=empty.next();"
        "var emptyOk=emptyFirst.value===undefined&&emptyFirst.done===true&&"
        "emptyAgain.value===undefined&&emptyAgain.done===true&&"
        "emptyFirst!==emptyAgain;"
        "var single=document.querySelectorAll('#a').values();"
        "var one=single.next();var oneEnd=single.next();"
        "var oneOk=one.value===rows[0]&&one.done===false&&"
        "oneEnd.value===undefined&&oneEnd.done===true;"
        "var ok=resultsAreObjects&&types&&firstSaved&&emptyOk&&oneOk;");
    const Value* firstValue = binding(harness, "first");
    expect(valid.succeeded() && booleanIs(harness, "ok", true) &&
        firstValue != nullptr && firstValue->isObject(),
        "JS80 returns stable ordinary result objects with Boolean done and canonical Element or undefined values");
    expect(NavigatorScriptRuntimeTestAccess::resultObjectCount(
            harness.runtime()) == 8u,
        "JS80 creates a fresh result record for normal, empty, and terminal calls");

    const ScriptResult keys = harness.execute(
        "var keysMethodSame=rows.keys===rows.keys;var keyCursor=rows.keys();"
        "var keyFirst=keyCursor.next();var keySecond=keyCursor.next();"
        "var keyTerminal=keyCursor.next();var keyTerminalAgain=keyCursor.next();"
        "var keyStable=keyFirst.value===0&&keyFirst.done===false&&"
        "keySecond.value===1&&keySecond.done===false&&"
        "keyTerminal.value===undefined&&keyTerminal.done===true&&"
        "keyTerminalAgain.value===undefined&&keyTerminalAgain.done===true&&"
        "keyFirst.value===0&&keyFirst.done===false;"
        "var emptyKeys=document.querySelectorAll('.absent').keys();"
        "var emptyKeyFirst=emptyKeys.next();var emptyKeyAgain=emptyKeys.next();"
        "var emptyKeyOk=emptyKeyFirst.value===undefined&&"
        "emptyKeyFirst.done===true&&emptyKeyAgain.value===undefined&&"
        "emptyKeyAgain.done===true&&emptyKeyFirst!==emptyKeyAgain;"
        "var singleKeys=document.querySelectorAll('#a').keys();"
        "var oneKey=singleKeys.next();var oneKeyEnd=singleKeys.next();"
        "var oneKeyOk=oneKey.value===0&&oneKey.done===false&&"
        "oneKeyEnd.value===undefined&&oneKeyEnd.done===true;"
        "var keysOk=keysMethodSame&&keyStable&&emptyKeyOk&&oneKeyOk;");
    const Value* keyValue = binding(harness, "keyFirst");
    expect(keys.succeeded() && booleanIs(harness, "keysOk", true) &&
        keyValue != nullptr && keyValue->isObject(),
        "JS80 keys yields genuine Number values, Boolean done, and stable ordinary result objects");
    expect(NavigatorScriptRuntimeTestAccess::resultObjectCount(
            harness.runtime()) == 16u,
        "JS80 keys reuses the fresh ordinary result-object mechanism for empty, normal, and terminal reads");

    NavigatorScriptExecutionHarness invalid;
    if (!loadHtml(invalid, "<html><body><i class='row'></i></body></html>",
            "JS80 unbound receiver fixture loads")) return;
    const ScriptResult noThis = invalid.execute(
        "var rows=document.querySelectorAll('.row');var cursor=rows.values();"
        "var next=cursor.next;next();");
    expect(!noThis.succeeded() &&
        noThis.runtimeError.code == RuntimeErrorCode::InvalidReceiver &&
        invalid.runtime().activeCallFrameCount() == 0u,
        "JS80 next rejects an unbound method call through the runtime receiver check");

    NavigatorScriptExecutionHarness invalidKeys;
    if (!loadHtml(invalidKeys,
            "<html><body><i class='row'></i></body></html>",
            "JS80 unbound keys receiver fixture loads")) return;
    const ScriptResult noKeysThis = invalidKeys.execute(
        "var rows=document.querySelectorAll('.row');var keys=rows.keys;keys();");
    expect(!noKeysThis.succeeded() &&
        noKeysThis.runtimeError.code == RuntimeErrorCode::InvalidReceiver &&
        invalidKeys.runtime().activeCallFrameCount() == 0u,
        "JS80 keys rejects an unbound collection method through the runtime receiver check");
}

void testKeyModesIndependenceAndAdapters()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='r0' class='row'></i>"
            "<i id='r1' class='row'></i><i id='r2' class='row'></i>"
            "<section id='left'><i class='scope'></i></section>"
            "<section id='right'><i class='scope'></i></section>"
            "<ul><li id='p0' class='pos selected'></li>"
            "<li id='p1' class='pos selected'></li>"
            "<li id='p2' class='pos selected'></li>"
            "<li id='p3' class='pos selected'></li></ul>"
            "<section id='adapters'><b class='target'></b>"
            "<b class='target'></b></section></body></html>",
            "JS80 mixed-mode cursor fixture loads")) return;

    const ScriptResult mixed = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var a=rows.keys();var b=rows.keys();"
        "var a0=a.next();var a1=a.next();var b0=b.next();"
        "var keysFirst=rows.keys();var valuesSecond=rows.values();"
        "var k0=keysFirst.next();var v0=valuesSecond.next();"
        "var k1=keysFirst.next();var v1=valuesSecond.next();"
        "var valuesFirst=rows.values();var keysSecond=rows.keys();"
        "var vFirst=valuesFirst.next();var kSecond=keysSecond.next();"
        "var kThird=keysSecond.next();var vNext=valuesFirst.next();"
        "var callbackKeys=rows.keys();var callbackCount=0;"
        "rows.forEach(function(element,index,collection){"
        "var result=callbackKeys.next();"
        "if(result.value===index&&result.done===false&&"
        "element===collection[index])callbackCount=callbackCount+1;});"
        "var someValues=rows.values();var someOk=rows.some(function(){"
        "return someValues.next().value===rows[0];});"
        "var everyKeys=rows.keys();var everyOk=rows.every(function(element,index){"
        "return everyKeys.next().value===index;});"
        "var left=document.getElementById('left').querySelectorAll(':scope > .scope');"
        "var right=document.getElementById('right').querySelectorAll(':scope > .scope');"
        "var tags=document.getElementById('adapters').getElementsByTagName('b');"
        "var classes=document.getElementById('adapters').getElementsByClassName('target');"
        "var tagKey=tags.keys().next();var classKey=classes.keys().next();"
        "var filtered=document.querySelectorAll('.pos:nth-child(odd of .selected)');"
        "var filteredKeys=filtered.keys();var filtered0=filteredKeys.next();"
        "document.querySelectorAll('#p0').item(0).removeAttribute('class');"
        "var filtered1=filteredKeys.next();"
        "var stable=k0.value===0&&k0.done===false&&k1.value===1&&"
        "k1.done===false&&k0.value===0&&k0.done===false;"
        "var ok=a0.value===0&&a1.value===1&&b0.value===0&&"
        "v0.value===rows.item(0)&&v0.value===rows[0]&&k0.value===0&&"
        "v1.value===rows.item(1)&&v1.value===rows[1]&&k1.value===1&&"
        "vFirst.value===rows[0]&&kSecond.value===0&&kThird.value===1&&"
        "vNext.value===rows[1]&&stable&&callbackCount===3&&someOk&&everyOk&&"
        "left.keys().next().value===0&&right.keys().next().value===0&&"
        "tagKey.value===0&&classKey.value===0&&filtered0.value===0&&"
        "filtered1.value===1;var cursorsOk=ok;");
    expect(mixed.succeeded() && booleanIs(harness, "cursorsOk", true),
        "JS80 independent keys and mixed keys/values cursors retain isolated indices, canonical values, stable results, scopes, adapters, filters, and callback state");

    NavigatorScriptIteratorTestAccess::Iterator keyRecord;
    NavigatorScriptIteratorTestAccess::Iterator valueRecord;
    const bool recordsRead =
        NavigatorScriptIteratorTestAccess::iteratorAt(
            harness.hostAdapter(), 0u, keyRecord) &&
        NavigatorScriptIteratorTestAccess::iteratorAt(
            harness.hostAdapter(), 3u, valueRecord);
    expect(recordsRead && NavigatorScriptIteratorTestAccess::isKeys(keyRecord) &&
        NavigatorScriptIteratorTestAccess::isValues(valueRecord) &&
        NavigatorScriptIteratorTestAccess::iteratorBytes() == 24u,
        "JS80 uses one 24-byte iterator registry record with isolated stored modes");

    NavigatorScriptExecutionHarness generation;
    if (!loadHtml(generation,
            "<html><body><i id='same' class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS80 key generation fixture loads")) return;
    const HostGenerationId oldGeneration = generation.hostAdapter().generation();
    const HostInstanceId oldSerial = elementSerial(generation, "same");
    const ScriptResult created = generation.execute(
        "var rows=document.querySelectorAll('.row');var keys=rows.keys();"
        "var first=keys.next();var firstKey=first.value;");
    RuntimeErrorCode generationError = RuntimeErrorCode::None;
    const bool invalidated = generation.invalidateDocumentGeneration(
        generationError);
    generation.document() = gxos::web::parseHtml(
        "file:///js80-generation-replacement.html",
        "<html><body><i id='same' class='row'></i><i class='row'></i>"
        "</body></html>");
    generation.hostAdapter().attachDocument(generation.document(),
        generation.runtime().hostGeneration());
    const ScriptResult stale = generation.execute("var staleKey=keys.next();");
    expect(created.succeeded() && numberIs(generation, "firstKey") == 0.0 &&
        invalidated && generationError == RuntimeErrorCode::None &&
        generation.hostAdapter().generation() != oldGeneration && oldSerial != 0u &&
        elementSerial(generation, "same") == oldSerial && !stale.succeeded() &&
        stale.runtimeError.code == RuntimeErrorCode::StaleHostObject,
        "JS80 stale numeric cursor fails closed after document replacement and structural serial reuse");
}

void testKeyLiveMutationAndBounds()
{
    NavigatorScriptExecutionHarness shifted;
    if (!loadHtml(shifted,
            "<html><body><i id='s0' class='shift'></i>"
            "<i id='s1' class='shift'></i><i id='s2' class='shift'></i>"
            "</body></html>",
            "JS80 shifting key fixture loads")) return;
    IteratorProbe shiftProbe;
    setIteratorProbes(shifted, shiftProbe);
    const ScriptResult shift = shifted.execute(
        "var rows=document.querySelectorAll('.shift');var cursor=rows.keys();"
        "var first=cursor.next();"
        "document.querySelectorAll('#s0').item(0).removeAttribute('class');"
        "var second=cursor.next();var terminal=cursor.next();"
        "var terminalAgain=cursor.next();"
        "var ok=first.value===0&&first.done===false&&"
        "second.value===1&&second.done===false&&"
        "terminal.value===undefined&&terminal.done===true&&"
        "terminalAgain.value===undefined&&terminalAgain.done===true;");
    clearIteratorProbes(shifted);
    expect(shift.succeeded() && booleanIs(shifted, "ok", true) &&
        shiftProbe.lengthCalls == 1u && shiftProbe.entryLength == 3u &&
        shiftProbe.lookupCount == 3u && shiftProbe.indices[0] == 0u &&
        shiftProbe.indices[1] == 1u && shiftProbe.indices[2] == 2u,
        "JS80 preserves the next live numeric index after collection positions shift and stops at the captured bound");

    NavigatorScriptExecutionHarness growth;
    if (!loadHtml(growth,
            "<html><body><i id='g0' class='grow'></i>"
            "<i id='g1' class='grow'></i><i id='g2' class='candidate'></i>"
            "<i id='g3' class='future'></i></body></html>",
            "JS80 key growth fixture loads")) return;
    IteratorProbe growthProbe;
    setIteratorProbes(growth, growthProbe);
    const ScriptResult grown = growth.execute(
        "var rows=document.querySelectorAll('.grow');var old=rows.keys();"
        "document.querySelectorAll('#g2').item(0).setAttribute('class','grow');"
        "var first=old.next();var second=old.next();var end=old.next();"
        "document.querySelectorAll('#g3').item(0).setAttribute('class','grow');"
        "var endAgain=old.next();var fresh=rows.keys();"
        "var new0=fresh.next();var new1=fresh.next();"
        "var new2=fresh.next();var new3=fresh.next();var newEnd=fresh.next();"
        "var ok=first.value===0&&second.value===1&&end.done===true&&"
        "endAgain.value===undefined&&endAgain.done===true&&"
        "new0.value===0&&new1.value===1&&new2.value===2&&new3.value===3&&"
        "newEnd.value===undefined&&newEnd.done===true;");
    clearIteratorProbes(growth);
    expect(grown.succeeded() && booleanIs(growth, "ok", true) &&
        growthProbe.lengthCalls == 2u && growthProbe.entryLength == 4u &&
        growthProbe.lookupCount == 6u && growthProbe.indices[0] == 0u &&
        growthProbe.indices[1] == 1u && growthProbe.indices[2] == 0u &&
        growthProbe.indices[3] == 1u && growthProbe.indices[4] == 2u &&
        growthProbe.indices[5] == 3u,
        "JS80 growth cannot extend an old bound, exhausted cursors do not restart, and a new cursor captures the larger length");

    NavigatorScriptExecutionHarness shrunk;
    if (!loadHtml(shrunk,
            "<html><body><i id='x0' class='shrink'></i>"
            "<i id='x1' class='shrink'></i><i id='x2' class='shrink'></i>"
            "</body></html>",
            "JS80 key shrink fixture loads")) return;
    IteratorProbe shrinkProbe;
    setIteratorProbes(shrunk, shrinkProbe);
    const ScriptResult shrink = shrunk.execute(
        "var rows=document.querySelectorAll('.shrink');var cursor=rows.keys();"
        "document.querySelectorAll('#x0').item(0).removeAttribute('class');"
        "document.querySelectorAll('#x1').item(0).removeAttribute('class');"
        "document.querySelectorAll('#x2').item(0).removeAttribute('class');"
        "var result=cursor.next();var again=cursor.next();"
        "var ok=result.value===undefined&&result.done===true&&"
        "again.value===undefined&&again.done===true;");
    clearIteratorProbes(shrunk);
    expect(shrink.succeeded() && booleanIs(shrunk, "ok", true) &&
        shrinkProbe.lengthCalls == 1u && shrinkProbe.entryLength == 3u &&
        shrinkProbe.lookupCount == 3u && shrinkProbe.indices[0] == 0u &&
        shrinkProbe.indices[1] == 1u && shrinkProbe.indices[2] == 2u,
        "JS80 skips missing live indices without fabricating numeric keys and performs no lookups after terminal exhaustion");
}

void testLiveBoundsAndSelectors()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='l0' class='live'></i><i id='l1' class='live'></i>"
            "<i id='l2' class='live'></i><i id='g0' class='grow'></i>"
            "<i id='g1' class='grow'></i><i id='g2' class='candidate'></i>"
            "<i id='g3' class='future'></i>"
            "<i id='s0' class='shrink'></i><i id='s1' class='shrink'></i>"
            "<i id='s2' class='shrink'></i><section id='left'>"
            "<i id='left0' class='scope'></i></section><section id='right'>"
            "<i id='right0' class='scope'></i></section><ul>"
            "<li id='p0' class='pos selected'></li>"
            "<li id='p1' class='pos selected'></li>"
            "<li id='p2' class='pos selected'></li>"
            "<li id='p3' class='pos selected'></li></ul>"
            "<section id='adapters'><b id='b0' class='target'></b>"
            "<b id='b1' class='target'></b></section></body></html>",
            "JS80 live mutation and adapter fixture loads")) return;

    IteratorProbe liveProbe;
    setIteratorProbes(harness, liveProbe);
    const ScriptResult live = harness.execute(
        "var rows=document.querySelectorAll('.live');var cursor=rows.values();"
        "var first=cursor.next();first.value.removeAttribute('class');"
        "var second=cursor.next();var end=cursor.next();"
        "var ok=first.value.id==='l0'&&second.value.id==='l2'&&"
        "end.value===undefined&&end.done===true;");
    clearIteratorProbes(harness);
    if (!live.succeeded() || !booleanIs(harness, "ok", true) ||
        liveProbe.lengthCalls != 1u || liveProbe.entryLength != 3u ||
        liveProbe.lookupCount != 3u) {
        std::cerr << "  JS80 live diagnostic ok="
            << booleanIs(harness, "ok", true) << " lengths="
            << liveProbe.lengthCalls << '/' << liveProbe.entryLength
            << " lookups=" << liveProbe.lookupCount << " indices="
            << liveProbe.indices[0] << ',' << liveProbe.indices[1] << ','
            << liveProbe.indices[2] << '\n';
    }
    expect(live.succeeded() && booleanIs(harness, "ok", true) &&
        liveProbe.lengthCalls == 1u && liveProbe.entryLength == 3u &&
        liveProbe.lookupCount == 3u && liveProbe.indices[0] == 0u &&
        liveProbe.indices[1] == 1u && liveProbe.indices[2] == 2u,
        "JS80 resolves shifted live membership at the original next index and stops at the fixed bound");

    IteratorProbe growProbe;
    setIteratorProbes(harness, growProbe);
    const ScriptResult grown = harness.execute(
        "var grow=document.querySelectorAll('.grow');var old=grow.values();"
        "document.querySelectorAll('#g2').item(0).setAttribute('class','grow');"
        "var old0=old.next();var old1=old.next();var oldEnd=old.next();"
        "document.querySelectorAll('#g3').item(0).setAttribute('class','grow');"
        "var oldEndAgain=old.next();"
        "var fresh=grow.values();var new0=fresh.next();var new1=fresh.next();"
        "var new2=fresh.next();var new3=fresh.next();var newEnd=fresh.next();"
        "var ok=old0.value.id==='g0'&&old1.value.id==='g1'&&"
        "oldEnd.done===true&&oldEndAgain.done===true&&"
        "oldEndAgain.value===undefined&&new0.value.id==='g0'&&"
        "new1.value.id==='g1'&&new2.value.id==='g2'&&"
        "new3.value.id==='g3'&&newEnd.done===true;");
    clearIteratorProbes(harness);
    expect(grown.succeeded() && booleanIs(harness, "ok", true) &&
        growProbe.lengthCalls == 2u && growProbe.entryLength == 4u &&
        growProbe.lookupCount == 6u && growProbe.indices[0] == 0u &&
        growProbe.indices[1] == 1u && growProbe.indices[2] == 0u &&
        growProbe.indices[3] == 1u && growProbe.indices[4] == 2u &&
        growProbe.indices[5] == 3u,
        "JS80 growth affects live reads within the old bound, exhaustion stays terminal, and a new cursor sees later growth");

    IteratorProbe shrinkProbe;
    setIteratorProbes(harness, shrinkProbe);
    const ScriptResult shrunk = harness.execute(
        "var shrink=document.querySelectorAll('.shrink');"
        "var cursor=shrink.values();"
        "document.querySelectorAll('#s0').item(0).removeAttribute('class');"
        "document.querySelectorAll('#s1').item(0).removeAttribute('class');"
        "document.querySelectorAll('#s2').item(0).removeAttribute('class');"
        "var result=cursor.next();var again=cursor.next();"
        "var ok=result.value===undefined&&result.done===true&&"
        "again.value===undefined&&again.done===true;");
    clearIteratorProbes(harness);
    expect(shrunk.succeeded() && booleanIs(harness, "ok", true) &&
        shrinkProbe.lengthCalls == 1u && shrinkProbe.entryLength == 3u &&
        shrinkProbe.lookupCount == 3u && shrinkProbe.indices[0] == 0u &&
        shrinkProbe.indices[1] == 1u && shrinkProbe.indices[2] == 2u,
        "JS80 skips all missing positions after shrinkage and keeps terminal state");

    const ScriptResult selectors = harness.execute(
        "var filtered=document.querySelectorAll('.pos:nth-child(odd of .selected)');"
        "var filteredCursor=filtered.values();var first=filteredCursor.next();"
        "first.value.removeAttribute('class');var second=filteredCursor.next();"
        "var left=document.getElementById('left').querySelectorAll('.scope');"
        "var right=document.getElementById('right').querySelectorAll('.scope');"
        "var tags=document.getElementById('adapters').getElementsByTagName('b');"
        "var classes=document.getElementById('adapters').getElementsByClassName('target');"
        "var scopes=left.values().next().value.id==='left0'&&"
        "right.values().next().value.id==='right0';"
        "var adapters=tags.values().next().value===tags[0]&&"
        "classes.values().next().value===classes[0];"
        "var ok=first.value.id==='p0'&&second.value.id==='p3'&&scopes&&adapters;");
    expect(selectors.succeeded() && booleanIs(harness, "ok", true),
        "JS80 shares scoped, tag/class, and filtered positional collection lookup without rank caching");
}

class NoopHostFunctionInvoker final : public HostFunctionInvoker {
public:
    bool invoke(const HostValue&, const HostValue*, std::size_t,
        const HostValue&, Value&, RuntimeErrorCode& error) override
    {
        error = RuntimeErrorCode::None;
        return false;
    }
    bool isTruthy(const Value& value) const override
    {
        return value.isBoolean() && value.booleanValue();
    }
};

void testGenerationAndInvalidNativeReceivers()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i id='same' class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS80 generation replacement fixture loads")) return;
    const HostGenerationId oldGeneration = harness.hostAdapter().generation();
    const HostInstanceId oldSerial = elementSerial(harness, "same");
    const ScriptResult first = harness.execute(
        "var rows=document.querySelectorAll('.row');var cursor=rows.values();"
        "var first=cursor.next();var firstId=first.value.id;"
        "var terminalCursor=rows.values();terminalCursor.next();"
        "terminalCursor.next();var terminalResult=terminalCursor.next();"
        "var terminalDone=terminalResult.done;");
    expect(first.succeeded() && stringIs(harness, "firstId") == "same",
        "JS80 generation fixture retains the first canonical Element result");
    expect(booleanIs(harness, "terminalDone", true),
        "JS80 generation fixture records a terminal cursor before replacement");

    HostObjectReference staleIterator{1u, oldGeneration,
        kNavigatorSelectorCollectionIteratorHostKind};
    HostObjectReference staleTerminalIterator{2u, oldGeneration,
        kNavigatorSelectorCollectionIteratorHostKind};
    RuntimeErrorCode generationError = RuntimeErrorCode::None;
    const bool invalidated = harness.invalidateDocumentGeneration(
        generationError);
    harness.document() = gxos::web::parseHtml(
        "file:///js79-generation-replacement.html",
        "<html><body><i id='same' class='row'></i><i class='row'></i>"
        "</body></html>");
    harness.hostAdapter().attachDocument(harness.document(),
        harness.runtime().hostGeneration());
    NoopHostFunctionInvoker invoker;
    RuntimeErrorCode callbackError = RuntimeErrorCode::None;
    HostValue hostResult;
    const HostResult staleNative = harness.hostAdapter().callWithRuntime(
        harness.runtime(), invoker, callbackError, &staleIterator,
        kNavigatorSelectorCollectionIteratorNextMethod, nullptr, 0u,
        hostResult);
    const HostResult staleTerminalNative =
        harness.hostAdapter().callWithRuntime(harness.runtime(), invoker,
            callbackError, &staleTerminalIterator,
            kNavigatorSelectorCollectionIteratorNextMethod, nullptr, 0u,
            hostResult);
    const ScriptResult staleScript = harness.execute(
        "var staleResult=cursor.next();");
    expect(invalidated && generationError == RuntimeErrorCode::None &&
        harness.hostAdapter().generation() != oldGeneration && oldSerial != 0u &&
        elementSerial(harness, "same") == oldSerial,
        "JS80 document replacement reuses the same structural serial in a new generation");
    expect(!staleScript.succeeded() &&
        staleScript.runtimeError.code == RuntimeErrorCode::StaleHostObject &&
        staleNative.code == HostResultCode::StaleObject &&
        staleTerminalNative.code == HostResultCode::StaleObject &&
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter()) == 0u,
        "JS80 exhausted and unexhausted stale cursors fail closed after generation replacement and serial reuse");

    NavigatorScriptExecutionHarness wrongReceiver;
    if (!loadHtml(wrongReceiver, "<html><body><i class='row'></i></body></html>",
            "JS80 invalid native receiver fixture loads")) return;
    const ScriptResult setup = wrongReceiver.execute(
        "var rows=document.querySelectorAll('.row');var cursor=rows.values();");
    const HostObjectReference wrongType{1u,
        wrongReceiver.hostAdapter().generation(), kNavigatorElementHostKind};
    callbackError = RuntimeErrorCode::None;
    const HostResult wrongKind = wrongReceiver.hostAdapter().callWithRuntime(
        wrongReceiver.runtime(), invoker, callbackError, &wrongType,
        kNavigatorSelectorCollectionIteratorNextMethod, nullptr, 0u,
        hostResult);
    const HostObjectReference invalidToken{999u,
        wrongReceiver.hostAdapter().generation(),
        kNavigatorSelectorCollectionIteratorHostKind};
    const HostResult invalidHandle = wrongReceiver.hostAdapter().callWithRuntime(
        wrongReceiver.runtime(), invoker, callbackError, &invalidToken,
        kNavigatorSelectorCollectionIteratorNextMethod, nullptr, 0u,
        hostResult);
    expect(setup.succeeded() && wrongKind.code == HostResultCode::InvalidObject &&
        invalidHandle.code == HostResultCode::InvalidObject,
        "JS80 native next validates both host-object type and bounded iterator token");
}

void testBoundedRegistriesAndCollectionCapacity()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i class='cursor'></i><i class='cursor'></i>"
            "</body></html>",
            "JS80 iterator registry fixture loads")) return;
    const std::string createCursors = [&]() {
        std::string source =
            "var cursorRows=document.querySelectorAll('.cursor');";
        for (std::size_t index = 0u;
            index < kNavigatorScriptMaxCollectionIterators; ++index) {
            source += "var cursor" + std::to_string(index) +
                (index % 2u == 0u ? "=cursorRows.keys();" : "=cursorRows.values();");
        }
        return source;
    }();
    const ScriptResult saturated = harness.execute(createCursors);
    expect(saturated.succeeded() &&
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter()) == kNavigatorScriptMaxCollectionIterators &&
        NavigatorScriptIteratorTestAccess::activeCollectionCount(
            harness.hostAdapter()) == 1u,
        "JS80 creates independent cursors without allocating extra collection records");
    const ScriptResult overflow = harness.execute(
        "var overflow=cursorRows.keys();");
    expect(!overflow.succeeded() && overflow.runtimeError.code ==
            RuntimeErrorCode::HostCallbackLimitExceeded &&
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter()) == kNavigatorScriptMaxCollectionIterators,
        "JS80 shared keys/values registry saturates deterministically without overwriting active cursors");
    const ScriptResult originalStillWorks = harness.execute(
        "var firstKey=cursor0.next();var secondValue=cursor1.next();"
        "var cursorOk=firstKey.value===0&&firstKey.done===false&&"
        "secondValue.value===cursorRows[0]&&secondValue.done===false;");
    expect(originalStillWorks.succeeded() &&
        booleanIs(harness, "cursorOk", true),
        "JS80 active cursor state survives capacity exhaustion and retains terminal results");

    NavigatorScriptExecutionHarness collections;
    if (!loadHtml(collections, "<html><body></body></html>",
            "JS80 collection registry compatibility fixture loads")) return;
    std::string queries;
    for (std::size_t index = 0u;
        index < kNavigatorScriptMaxSelectorCollections; ++index) {
        queries += "var collection" + std::to_string(index) +
            "=document.querySelectorAll('.missing" +
            std::to_string(index) + "');";
    }
    const ScriptResult collectionSaturation = collections.execute(queries);
    const ScriptResult withCursor = collections.execute(
        "var cursor=collection0.values();var terminal=cursor.next();"
        "var terminalOk=terminal.value===undefined&&terminal.done===true;");
    const ScriptResult collectionOverflow = collections.execute(
        "document.querySelectorAll('.missing-overflow');");
    expect(collectionSaturation.succeeded() && withCursor.succeeded() &&
        booleanIs(collections, "terminalOk", true) &&
        NavigatorScriptIteratorTestAccess::activeCollectionCount(
            collections.hostAdapter()) == kNavigatorScriptMaxSelectorCollections &&
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            collections.hostAdapter()) == 1u &&
        !collectionOverflow.succeeded() && collectionOverflow.runtimeError.code ==
            RuntimeErrorCode::DocumentLookupLimitExceeded,
        "JS80 preserves the existing 128-record collection registry and overflow behavior");
}

std::string fullCapacityHtml()
{
    std::string html = "<html class='all'><body class='all'><div class='all'>";
    for (std::size_t index = 0u; index < 1021u; ++index)
        html += "<i class='all'></i>";
    html += "</div></body></html>";
    return html;
}

void testResultCapacityAndMemory()
{
    RuntimeContext baseline;
    RuntimeLimits narrowLimits;
    narrowLimits.maxTotalProperties = baseline.propertyCount() + 1u;
    NavigatorScriptExecutionHarness narrow(narrowLimits);
    if (!loadHtml(narrow, "<html><body><i class='row'></i></body></html>",
            "JS80 partial result-property fixture loads")) return;
    const ScriptResult setup = narrow.execute(
        "var rows=document.querySelectorAll('.row');var cursor=rows.values();");
    const std::size_t objectsBefore = narrow.runtime().objectCount();
    const std::size_t propertiesBefore = narrow.runtime().propertyCount();
    const ScriptResult propertyFailure = narrow.execute(
        "var unavailable=cursor.next();");
    NavigatorScriptIteratorTestAccess::Iterator cursorRecord;
    const bool recordRead = NavigatorScriptIteratorTestAccess::iteratorAt(
        narrow.hostAdapter(), 0u, cursorRecord);
    expect(setup.succeeded() && !propertyFailure.succeeded() &&
        propertyFailure.runtimeError.code == RuntimeErrorCode::PropertyLimitExceeded &&
        narrow.runtime().objectCount() == objectsBefore &&
        narrow.runtime().propertyCount() == propertiesBefore && recordRead &&
        cursorRecord.nextIndex == 0u && !cursorRecord.exhausted,
        "JS80 result property allocation rolls back partial objects and leaves the cursor unadvanced");

    NavigatorScriptExecutionHarness narrowKeys(narrowLimits);
    if (!loadHtml(narrowKeys,
            "<html><body><i class='row'></i></body></html>",
            "JS80 key result-property fixture loads")) return;
    const ScriptResult keySetup = narrowKeys.execute(
        "var rows=document.querySelectorAll('.row');var cursor=rows.keys();");
    const std::size_t keyObjectsBefore = narrowKeys.runtime().objectCount();
    const std::size_t keyPropertiesBefore = narrowKeys.runtime().propertyCount();
    const ScriptResult keyPropertyFailure = narrowKeys.execute(
        "var unavailable=cursor.next();");
    NavigatorScriptIteratorTestAccess::Iterator keyCursorRecord;
    const bool keyRecordRead = NavigatorScriptIteratorTestAccess::iteratorAt(
        narrowKeys.hostAdapter(), 0u, keyCursorRecord);
    expect(keySetup.succeeded() && !keyPropertyFailure.succeeded() &&
        keyPropertyFailure.runtimeError.code == RuntimeErrorCode::PropertyLimitExceeded &&
        narrowKeys.runtime().objectCount() == keyObjectsBefore &&
        narrowKeys.runtime().propertyCount() == keyPropertiesBefore &&
        keyRecordRead && keyCursorRecord.nextIndex == 0u &&
        !keyCursorRecord.exhausted &&
        NavigatorScriptIteratorTestAccess::isKeys(keyCursorRecord),
        "JS80 key result allocation failure rolls back the ordinary result and preserves cursor state");

    NavigatorScriptExecutionHarness full;
    if (!loadHtml(full, fullCapacityHtml(),
            "JS80 1,024-element result capacity fixture loads")) return;
    IteratorProbe probe;
    setIteratorProbes(full, probe);
    const ScriptResult retained = full.execute(
        "var rows=document.querySelectorAll('.all');var cursor=rows.keys();"
        "var held=[];var made=0;while(made<1024){held.push(cursor.next());"
        "made=made+1;}");
    clearIteratorProbes(full);
    const bool resultLimit = !retained.succeeded() &&
        retained.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded;
    const ScriptResult retainedCheck = full.execute(
        "var retainedOk=rows.length===1024&&held.length===1019&&"
        "held[0].value===0&&held[1018].value===1018&&"
        "held[0].done===false&&held[1018].done===false;");
    const std::size_t resultCount =
        NavigatorScriptRuntimeTestAccess::resultObjectCount(full.runtime());
    const std::size_t objectBytes =
        NavigatorScriptRuntimeTestAccess::objectBytes();
    const std::size_t propertyBytes =
        NavigatorScriptRuntimeTestAccess::propertyBytes();
    const std::size_t resultPayload = resultCount *
        (objectBytes + 2u * propertyBytes);
    const std::size_t propertyStorage =
        NavigatorScriptRuntimeTestAccess::iteratorResultPropertyCapacityBytes(
            full.runtime());
    const std::size_t objectVectorStorage =
        NavigatorScriptRuntimeTestAccess::objectVectorCapacityBytes(
            full.runtime());
    const std::size_t hostObjectVectorStorage =
        NavigatorScriptRuntimeTestAccess::hostObjectVectorCapacityBytes(
            full.runtime());
    const std::size_t hostMethodVectorStorage =
        NavigatorScriptRuntimeTestAccess::hostMethodVectorCapacityBytes(
            full.runtime());
    const std::size_t environments = full.runtime().environmentCount();
    const std::size_t hostObjects = full.runtime().hostObjectCount();
    if (!resultLimit || !retainedCheck.succeeded() ||
        !booleanIs(full, "retainedOk", true)) {
        std::cerr << "  JS80 capacity runtime="
            << runtimeErrorCodeName(retained.runtimeError.code)
            << " objects=" << full.runtime().objectCount()
            << " properties=" << full.runtime().propertyCount()
            << " held=" << numberIs(full, "made") << '\n';
    }
    expect(resultLimit && retainedCheck.succeeded() &&
        booleanIs(full, "retainedOk", true) && resultCount == 1019u &&
        full.runtime().objectCount() == kDefaultMaxJavaScriptRuntimeObjects &&
        probe.lengthCalls == 1u && probe.entryLength == 1024u &&
        probe.lookupCount == 1020u && probe.indices[1019] == 1019u &&
        environments <= kDefaultMaxJavaScriptEnvironments &&
        hostObjects <= kDefaultMaxJavaScriptHostObjects &&
        full.runtime().activeCallFrameCount() == 0u,
        "JS80 retains every result up to the existing runtime-object bound and fails before advancing the next cursor position");
    NavigatorScriptIteratorTestAccess::Iterator fullCursor;
    const bool fullRecordRead = NavigatorScriptIteratorTestAccess::iteratorAt(
        full.hostAdapter(), 0u, fullCursor);
    expect(fullRecordRead && fullCursor.nextIndex == 1019u &&
        !fullCursor.exhausted &&
        NavigatorScriptIteratorTestAccess::isKeys(fullCursor),
        "JS80 key result-object exhaustion preserves the next unreturned index and mode");

    std::cout << "JS80 capacity: collection="
        << NavigatorScriptIteratorTestAccess::collectionBytes() << " B x "
        << kNavigatorScriptMaxSelectorCollections << "="
        << NavigatorScriptIteratorTestAccess::collectionBytes() *
            kNavigatorScriptMaxSelectorCollections
        << " B; iterator=" << NavigatorScriptIteratorTestAccess::iteratorBytes()
        << " B x " << kNavigatorScriptMaxCollectionIterators << "="
        << NavigatorScriptIteratorTestAccess::iteratorBytes() *
            kNavigatorScriptMaxCollectionIterators
        << " B; results=" << resultCount << "; runtimeObject=" << objectBytes
        << " B; runtimeProperty=" << propertyBytes << " B; resultRecordPayload="
        << resultPayload << " B; resultPropertyVectorCapacity=" << propertyStorage
        << " B; objectVectorCapacity=" << objectVectorStorage
        << " B; objects=" << full.runtime().objectCount() << '/'
        << kDefaultMaxJavaScriptRuntimeObjects << "; properties="
        << full.runtime().propertyCount() << '/'
        << kDefaultMaxJavaScriptRuntimeProperties << "; environments="
        << environments << '/' << kDefaultMaxJavaScriptEnvironments
        << "; hostObjects=" << hostObjects << '/'
        << kDefaultMaxJavaScriptHostObjects << " (record="
        << NavigatorScriptRuntimeTestAccess::hostObjectBytes()
        << " B, allocated=" << hostObjectVectorStorage << " B); hostMethods="
        << NavigatorScriptRuntimeTestAccess::hostMethodCount(full.runtime())
        << " (record=" << NavigatorScriptRuntimeTestAccess::hostMethodBytes()
        << " B, allocated=" << hostMethodVectorStorage << " B)\n";
}

std::uint64_t median(std::vector<std::uint64_t> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2u];
}

std::uint64_t timeScript(const std::string& html, const std::string& setup,
    const std::string& action, const std::string& label,
    std::size_t sampleCount = 7u)
{
    std::vector<std::uint64_t> samples;
    for (std::size_t repeat = 0u; repeat < sampleCount; ++repeat) {
        NavigatorScriptExecutionHarness harness;
        if (!loadHtml(harness, html, "JS80 performance fixture loads " + label))
            return 0u;
        if (!harness.execute(setup).succeeded()) {
            expect(false, "JS80 performance setup succeeds " + label);
            return 0u;
        }
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(action);
        const auto end = std::chrono::steady_clock::now();
        expect(result.succeeded(), "JS80 performance workload succeeds " + label);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(end - start).count();
        samples.push_back(static_cast<std::uint64_t>(elapsed));
    }
    return median(samples);
}

std::string repeatedCalls(const char* expression, std::size_t count)
{
    std::string source;
    for (std::size_t index = 0u; index < count; ++index) {
        source += expression;
        source += ';';
    }
    return source;
}

void testPerformance()
{
    std::string html = "<html><body>";
    for (std::size_t index = 0u; index < 64u; ++index)
        html += "<i class='perf'></i>";
    html += "</body></html>";
    const std::string baseSetup =
        "var rows=document.querySelectorAll('.perf');var cursor=rows.values();";
    const std::string keysSetup =
        "var rows=document.querySelectorAll('.perf');var cursor=rows.keys();";
    const std::uint64_t create = timeScript(html,
        "var rows=document.querySelectorAll('.perf');",
        "var cursor=rows.values();", "creation");
    const std::uint64_t first = timeScript(html, baseSetup,
        "var result=cursor.next();", "first-next");
    const std::uint64_t middle = timeScript(html,
        baseSetup + repeatedCalls("cursor.next()", 31u),
        "var result=cursor.next();", "middle-next");
    const std::uint64_t last = timeScript(html,
        baseSetup + repeatedCalls("cursor.next()", 63u),
        "var result=cursor.next();", "last-next");
    const std::uint64_t exhausted = timeScript(html,
        baseSetup + repeatedCalls("cursor.next()", 64u),
        "var result=cursor.next();", "exhausted-next");
    const std::uint64_t repeatedExhausted = timeScript(html,
        baseSetup + repeatedCalls("cursor.next()", 65u),
        "var result=cursor.next();", "repeated-exhausted-next");
    const std::uint64_t keyCreate = timeScript(html,
        "var rows=document.querySelectorAll('.perf');",
        "var cursor=rows.keys();", "keys-creation");
    const std::uint64_t keyFirst = timeScript(html, keysSetup,
        "var result=cursor.next();", "keys-first-next");
    const std::uint64_t keyMiddle = timeScript(html,
        keysSetup + repeatedCalls("cursor.next()", 31u),
        "var result=cursor.next();", "keys-middle-next");
    const std::uint64_t keyLast = timeScript(html,
        keysSetup + repeatedCalls("cursor.next()", 63u),
        "var result=cursor.next();", "keys-last-next");
    const std::uint64_t keyExhausted = timeScript(html,
        keysSetup + repeatedCalls("cursor.next()", 64u),
        "var result=cursor.next();", "keys-exhausted-next");
    const std::uint64_t keyRepeatedExhausted = timeScript(html,
        keysSetup + repeatedCalls("cursor.next()", 65u),
        "var result=cursor.next();", "keys-repeated-exhausted-next");
    const std::uint64_t valuesTraversal = timeScript(html,
        baseSetup,
        "var index=0;while(index<rows.length){var item=cursor.next();"
        "index=index+1;}", "full-values-traversal");
    const std::uint64_t keysTraversal = timeScript(html,
        keysSetup,
        "var index=0;while(index<rows.length){var item=cursor.next();"
        "index=index+1;}", "full-keys-traversal");
    const std::uint64_t itemLoop = timeScript(html,
        "var rows=document.querySelectorAll('.perf');",
        "var index=0;while(index<rows.length){var item=rows.item(index);"
        "index=index+1;}", "item-loop");
    const std::uint64_t forEach = timeScript(html,
        "var rows=document.querySelectorAll('.perf');",
        "var calls=0;rows.forEach(function(){calls=calls+1;});",
        "forEach-traversal");
    const std::uint64_t filtered = timeScript(html,
        "var rows=document.querySelectorAll('.perf:nth-child(odd of .perf)');"
        "var cursor=rows.values();",
        "var index=0;while(index<rows.length){var item=cursor.next();"
        "index=index+1;}", "filtered-positional-traversal");
    const std::uint64_t filteredKeys = timeScript(html,
        "var rows=document.querySelectorAll('.perf:nth-child(odd of .perf)');"
        "var cursor=rows.keys();",
        "var index=0;while(index<rows.length){var item=cursor.next();"
        "index=index+1;}", "filtered-positional-keys-traversal");
    const std::uint64_t interleaved = timeScript(html,
        "var rows=document.querySelectorAll('.perf');var a=rows.values();"
        "var b=rows.values();",
        repeatedCalls("a.next()", 32u) + repeatedCalls("b.next()", 32u),
        "interleaved-cursors");
    const std::uint64_t mixed = timeScript(html,
        "var rows=document.querySelectorAll('.perf');var a=rows.keys();"
        "var b=rows.values();",
        repeatedCalls("a.next()", 32u) + repeatedCalls("b.next()", 32u),
        "mixed-keys-values-cursors");
    const std::uint64_t mutation = timeScript(html, baseSetup,
        "var first=cursor.next();first.value.removeAttribute('class');"
        "var second=cursor.next();", "mutation-between-reads");
    const std::uint64_t keyMutation = timeScript(html, keysSetup,
        "var first=cursor.next();rows.item(0).removeAttribute('class');"
        "var second=cursor.next();", "keys-mutation-between-reads");

    const std::string mediumHtml = [&]() {
        std::string result = "<html><body>";
        for (std::size_t index = 0u; index < 512u; ++index)
            result += "<i class='perf'></i>";
        result += "</body></html>";
        return result;
    }();
    const std::uint64_t keys512 = timeScript(mediumHtml,
        "var rows=document.querySelectorAll('.perf');var cursor=rows.keys();",
        "var index=0;while(index<512){var item=cursor.next();"
        "index=index+1;}", "keys-traversal-512");
    const std::string maxHtml = [&]() {
        std::string result = "<html><body>";
        for (std::size_t index = 0u; index < 1019u; ++index)
            result += "<i class='perf'></i>";
        result += "</body></html>";
        return result;
    }();
    const std::uint64_t keysNearCapacity = timeScript(maxHtml,
        "var rows=document.querySelectorAll('.perf');var cursor=rows.keys();",
        "var index=0;while(index<1019){var item=cursor.next();"
        "index=index+1;}", "keys-near-capacity-1019", 3u);

    std::cout << "JS80 median harness script time (7 samples, 64 Elements, us; includes parse/evaluation): valuesCreate="
        << create << " first=" << first << " middle=" << middle << " last="
        << last << " exhausted=" << exhausted << " repeatedExhausted="
        << repeatedExhausted << " keysCreate=" << keyCreate << " keyFirst="
        << keyFirst << " keyMiddle=" << keyMiddle << " keyLast=" << keyLast
        << " keyExhausted=" << keyExhausted << " keyRepeatedExhausted="
        << keyRepeatedExhausted << " valuesTraversal=" << valuesTraversal
        << " keysTraversal=" << keysTraversal << " itemLoop=" << itemLoop
        << " forEach=" << forEach << " filteredValues=" << filtered
        << " filteredKeys=" << filteredKeys << " interleavedValues="
        << interleaved << " mixedKeysValues=" << mixed << " valueMutation="
        << mutation << " keyMutation=" << keyMutation << "\nJS80 traversal medians: keys512="
        << keys512 << " us (7 samples); keysNearCapacity1019="
        << keysNearCapacity << " us (3 samples)\n";
}

void testMeasuredCapacities()
{
    expect(sizeof(NavigatorScriptSelectorDescriptor) == 1918u &&
        NavigatorScriptIteratorTestAccess::collectionBytes() == 1936u &&
        NavigatorScriptIteratorTestAccess::collectionBytes() *
            kNavigatorScriptMaxSelectorCollections == 247808u &&
        NavigatorScriptIteratorTestAccess::iteratorBytes() == 24u &&
        kNavigatorScriptMaxCollectionIterators == 128u &&
        kNavigatorScriptMaxDocumentNodes == 1024u &&
        kDefaultMaxJavaScriptEnvironments == 2048u &&
        kDefaultMaxJavaScriptHostObjects == 2048u &&
        kDefaultMaxJavaScriptRuntimeObjects == 1024u,
        "JS80 preserves selector, collection, structure, and runtime pool capacities");
}

} // namespace

int main()
{
    testHostedFixture();
    testReceiversAndResultTypes();
    testKeyModesIndependenceAndAdapters();
    testKeyLiveMutationAndBounds();
    testLiveBoundsAndSelectors();
    testGenerationAndInvalidNativeReceivers();
    testBoundedRegistriesAndCollectionCapacity();
    testResultCapacityAndMemory();
    testPerformance();
    testMeasuredCapacities();
    std::cout << "Navigator JavaScript JS80 checks: " << checks - failures
        << '/' << checks << " passed\n";
    return failures == 0u ? 0 : 1;
}
