#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

    static bool iteratorAt(const NavigatorScriptHostAdapter& adapter,
        std::size_t index, Iterator& result)
    {
        if (index >= adapter.selectorCollectionIterators_.size() ||
            !adapter.selectorCollectionIterators_[index].active) return false;
        result = adapter.selectorCollectionIterators_[index];
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

const Value* binding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    return harness.runtime().lookup(SourceView(name,
        std::char_traits<char>::length(name)));
}

double numberIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isNumber() ? value->numberValue() : -1.0;
}

bool booleanIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isBoolean() && value->booleanValue();
}

bool loadHtml(NavigatorScriptExecutionHarness& harness,
    const std::string& html, const char* label)
{
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool loaded = harness.loadHtml("file:///js84-native.html", html,
        error) && error == RuntimeErrorCode::None;
    expect(loaded, label);
    return loaded;
}

std::string htmlWithRows(std::size_t count)
{
    std::string html;
    for (std::size_t index = 0u; index < count; ++index)
        html += "<i class='row'></i>";
    return html;
}

void testHostedFixture()
{
    std::ifstream file("navigator-smoke/javascript-js84.html",
        std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, html, "JS84 hosted fixture loads natively")) return;
    const auto& scripts = harness.document().scriptSources;
    expect(scripts.size() == 1u && scripts[0].find('<') == std::string::npos,
        "JS84 hosted inline script preserves the HTML-subset integrity rule");
    if (scripts.size() != 1u) return;
    const ScriptResult result = harness.execute(scripts[0]);
    const Value* marker = binding(harness, "marker");
    expect(result.succeeded() && marker != nullptr && marker->isString() &&
        harness.runtime().stringValue(*marker) ==
            "JS84:assertions=10:failures=0:loops=true:mutation=true:modes=true:references=true",
        "JS84 hosted fixture runs visible iteration and reference-retention assertions");

    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    const bool clicked = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, clickError);
    expect(clicked && clickError == RuntimeErrorCode::None &&
        numberIs(harness, "nestedEvents") == 3.0 &&
        booleanIs(harness, "eventOk"),
        "JS84 hosted fixture preserves nested Event iteration and metadata");
}

void testBasicLoopsAndBindings()
{
    NavigatorScriptExecutionHarness harness;
    const std::string html =
        "<html><body><i id='a' class='row'></i>"
        "<i id='b' class='row'></i><i id='c' class='row'></i>"
        "</body></html>";
    if (!loadHtml(harness, html, "JS84 basic fixture loads")) return;

    const ScriptResult result = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var row='outer';var visited=0;var identity=true;"
        "for(const row of document.querySelectorAll('.row')){"
        "identity=identity&&row===rows[visited];"
        "row.setAttribute('data-visited','yes');visited++;}"
        "var outerScope=row==='outer';"
        "var mutableVisits=0;for(let item of rows){"
        "mutableVisits++;item=null;}"
        "var continueVisits=0;var continueBodies=0;"
        "for(const item of rows){continueVisits++;"
        "if(continueVisits===2)continue;continueBodies++;}"
        "var nestedPairs=0;for(const item of rows){"
        "for(const item of rows){nestedPairs++;}}"
        "var innerBreakOuter=0;for(const left of rows){"
        "for(const right of rows){if(right===rows[0])break;"
        "innerBreakOuter++;}}"
        "var classicFor=0;for(var i=0;i<3;i++)classicFor++;"
        "var classicWhile=0;while(classicWhile<2)classicWhile++;"
        "var finalElement=rows[2].getAttribute('data-visited')==='yes';");
    expect(result.succeeded() && numberIs(harness, "visited") == 3.0 &&
        booleanIs(harness, "identity") && booleanIs(harness, "outerScope") &&
        numberIs(harness, "mutableVisits") == 3.0 &&
        numberIs(harness, "continueVisits") == 3.0 &&
        numberIs(harness, "continueBodies") == 2.0 &&
        numberIs(harness, "nestedPairs") == 9.0 &&
        numberIs(harness, "innerBreakOuter") == 0.0 &&
        numberIs(harness, "classicFor") == 3.0 &&
        numberIs(harness, "classicWhile") == 2.0 &&
        booleanIs(harness, "finalElement"),
        "JS84 const/let bindings, canonical values, scopes, continue, nested loops, break, and classic loops");

    NavigatorScriptExecutionHarness immutable;
    if (!loadHtml(immutable, html, "JS84 const assignment fixture loads"))
        return;
    const ScriptResult constWrite = immutable.execute(
        "var rows=document.querySelectorAll('.row');"
        "for(const element of rows){element=null;}");
    expect(!constWrite.succeeded() &&
        constWrite.runtimeError.code == RuntimeErrorCode::TypeError,
        "JS84 const iteration binding rejects assignment");
}

void testProtocolAndIteratorClose()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, "<html><body></body></html>",
            "JS84 custom iterator fixture loads")) return;
    const ScriptResult result = harness.execute(
        "var acquireReceiver=false;var nextReceiver=false;"
        "var customCursor={step:0,closed:0,next:function(){"
        "nextReceiver=this===customCursor;this.step++;"
        "return {value:this.step,done:this.step>2};},"
        "};customCursor['return']=function(){this.closed++;return {};};"
        "var custom={};custom[Symbol.iterator]=function(){"
        "acquireReceiver=this===custom;return customCursor;};"
        "var breakVisits=0;for(let value of custom){"
        "breakVisits++;break;}"
        "var breakConsumedOne=customCursor.step===1;"
        "var breakClosed=customCursor.closed===1;"
        "function returnFromLoop(){for(const value of custom){return value;}"
        "return -1;}var returned=returnFromLoop();"
        "var returnClosed=customCursor.closed===2;"
        "var normalCursor={step:0,closed:0,next:function(){"
        "this.step++;if(this.step===1)return {value:undefined,done:false};"
        "if(this.step===2)return {value:null,done:false};"
        "if(this.step===3)return {value:false,done:false};"
        "return {value:99,done:'finished'};},"
        "};normalCursor['return']=function(){this.closed++;return {};};"
        "var normal={};normal[Symbol.iterator]=function(){return normalCursor;};"
        "var normalVisits=0;var undefinedYield=false;var nullYield=false;"
        "var falseYield=false;for(let value of normal){normalVisits++;"
        "if(value===undefined)undefinedYield=true;"
        "if(value===null)nullYield=true;if(value===false)falseYield=true;}"
        "var normalClosed=normalCursor.closed===0&&normalCursor.step===4;"
        "var continueCursor={step:0,closed:0,next:function(){"
        "this.step++;return {value:this.step,done:this.step>2};},"
        "};continueCursor['return']=function(){this.closed++;return {};};"
        "var continued={};continued[Symbol.iterator]=function(){"
        "return continueCursor;};var continueVisits=0;"
        "for(let value of continued){continueVisits++;continue;}"
        "var continueNotClosed=continueCursor.closed===0&&"
        "continueCursor.step===3;");
    expect(result.succeeded() && booleanIs(harness, "acquireReceiver") &&
        booleanIs(harness, "nextReceiver") &&
        numberIs(harness, "breakVisits") == 1.0 &&
        booleanIs(harness, "breakConsumedOne") &&
        booleanIs(harness, "breakClosed") &&
        numberIs(harness, "returned") == 2.0 &&
        booleanIs(harness, "returnClosed") &&
        numberIs(harness, "normalVisits") == 3.0 &&
        booleanIs(harness, "undefinedYield") &&
        booleanIs(harness, "nullYield") && booleanIs(harness, "falseYield") &&
        booleanIs(harness, "normalClosed") &&
        numberIs(harness, "continueVisits") == 2.0 &&
        booleanIs(harness, "continueNotClosed"),
        "JS84 uses real Symbol.iterator and next calls with receivers, done truthiness, and applicable close");

    NavigatorScriptExecutionHarness bodyError;
    if (!loadHtml(bodyError, "<html><body></body></html>",
            "JS84 close-on-error fixture loads")) return;
    const ScriptResult failedBody = bodyError.execute(
        "var closed=0;var cursor={next:function(){"
        "return {value:1,done:false};}};"
        "cursor['return']=function(){closed++;return 1;};"
        "var iterable={};iterable[Symbol.iterator]=function(){return cursor;};"
        "for(let value of iterable){missingName;}");
    const Value* closed = binding(bodyError, "closed");
    expect(!failedBody.succeeded() &&
        failedBody.runtimeError.code == RuntimeErrorCode::UnknownIdentifier &&
        closed != nullptr && closed->isNumber() &&
        closed->numberValue() == 1.0,
        "JS84 body failure closes once and preserves its original runtime error");
}

void testInvalidProtocolValues()
{
    const char* values[] = {"null", "undefined", "123", "false"};
    for (const char* value : values) {
        NavigatorScriptExecutionHarness harness;
        if (!loadHtml(harness, "<html><body></body></html>",
                "JS84 non-iterable fixture loads")) continue;
        const ScriptResult result = harness.execute(std::string(
            "for(let element of ") + value + "){}" );
        expect(!result.succeeded() &&
            (result.runtimeError.code == RuntimeErrorCode::CannotReadProperty ||
                result.runtimeError.code == RuntimeErrorCode::NotCallable),
            "JS84 rejects null, undefined, and primitive non-iterables");
    }

    struct InvalidCase {
        const char* source;
        RuntimeErrorCode expected;
        const char* label;
    };
    const InvalidCase cases[] = {
        {"var bad={};bad[Symbol.iterator]=3;for(let x of bad){}",
            RuntimeErrorCode::NotCallable, "non-callable Symbol.iterator"},
        {"var bad={};bad[Symbol.iterator]=function(){return 1;};"
            "for(let x of bad){}", RuntimeErrorCode::NotObject,
            "primitive iterator result from acquisition"},
        {"var bad={};bad[Symbol.iterator]=function(){return {};};"
            "for(let x of bad){}", RuntimeErrorCode::NotCallable,
            "missing next method"},
        {"var bad={};bad[Symbol.iterator]=function(){return {"
            "next:function(){return 1;}};};for(let x of bad){}",
            RuntimeErrorCode::NotObject, "primitive next result"},
        {"var bad={};bad[Symbol.iterator]=function(){var cursor={"
            "next:function(){return {value:1,done:false};}};"
            "cursor['return']=3;return cursor;};"
            "for(let x of bad){break;}", RuntimeErrorCode::NotCallable,
            "non-callable IteratorClose method"},
    };
    for (const InvalidCase& test : cases) {
        NavigatorScriptExecutionHarness harness;
        if (!loadHtml(harness, "<html><body></body></html>",
                "JS84 invalid protocol fixture loads")) continue;
        const ScriptResult result = harness.execute(test.source);
        expect(!result.succeeded() && result.runtimeError.code == test.expected,
            test.label);
    }
}

void testExplicitModesMutationAndCallbacks()
{
    NavigatorScriptExecutionHarness harness;
    const std::string html =
        "<html><body>"
        "<i id='a' class='row'></i><i id='b' class='row'></i>"
        "<i id='c' class='row'></i>"
        "<i id='g0' class='grow'></i><i id='g1' class='grow'></i>"
        "<i id='g2' class='candidate'></i>"
        "<i id='s0' class='shrink'></i><i id='s1' class='shrink'></i>"
        "<i id='s2' class='shrink'></i>"
        "<section id='scope-root'><i id='scope-row' class='scope-row'>"
        "</i></section><ul><li id='p0' class='pos selected'></li>"
        "<li id='p1' class='pos selected'></li>"
        "<li id='p2' class='pos selected'></li>"
        "<li id='p3' class='pos selected'></li></ul>"
        "<section id='adapter-root'><b id='tag0' class='target'></b>"
        "<b id='tag1' class='target'></b></section>"
        "<button id='event-a'></button><button id='event-b'></button>"
        "</body></html>";
    if (!loadHtml(harness, html, "JS84 mutation and Event fixture loads"))
        return;
    const ScriptResult result = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows.values();var first=cursor.next();var remaining=0;"
        "for(const element of cursor){remaining++;}"
        "var remainingOk=first.value===rows[0]&&remaining===2;"
        "var keyCount=0;for(const index of rows.keys()){"
        "if(index===keyCount)keyCount++;}"
        "var entryCount=0;for(const pair of rows.entries()){"
        "if(pair[0]===entryCount&&pair[1]===rows[entryCount])entryCount++;}"
        "var grow=document.querySelectorAll('.grow');var grew=0;"
        "for(const element of grow){grew++;if(grew===1)"
        "document.querySelectorAll('#g2').item(0).setAttribute('class','grow');}"
        "var newGrow=0;for(const element of grow){newGrow++;}"
        "var shrink=document.querySelectorAll('.shrink');var shrunk=0;"
        "var shrinkIds='';for(const element of shrink){shrunk++;"
        "shrinkIds=shrinkIds+element.id;if(shrunk===1)"
        "document.querySelectorAll('#s1').item(0).removeAttribute('class');}"
        "var callbackPairs=0;rows.forEach(function(outer){"
        "for(const inner of rows)callbackPairs++;});"
        "var scoped=document.getElementById('scope-root').querySelectorAll("
        "':scope > .scope-row');var scopedCount=0;"
        "for(const element of scoped){if(element.id==='scope-row')scopedCount++;}"
        "var filtered=document.querySelectorAll("
        "'.pos:nth-child(odd of .selected)');var filteredIds='';"
        "for(const element of filtered)filteredIds=filteredIds+element.id;"
        "var tags=document.getElementById('adapter-root')."
        "getElementsByTagName('b');var tagCount=0;"
        "for(const element of tags)tagCount++;"
        "var classes=document.getElementById('adapter-root')."
        "getElementsByClassName('target');var classCount=0;"
        "for(const element of classes)classCount++;"
        "var eventA=document.getElementById('event-a');"
        "var eventB=document.getElementById('event-b');var eventCount=0;"
        "var eventAOk=false;var eventBOk=false;"
        "eventB.addEventListener('click',function(event){"
        "for(const inner of rows)eventCount++;"
        "eventBOk=event.target===eventB&&event.currentTarget===eventB&&"
        "event.eventPhase===2;});"
        "eventA.addEventListener('click',function(event){var seen=0;"
        "for(const outer of rows){seen++;if(seen===1)eventB.click();}"
        "eventAOk=seen===3&&event.target===eventA&&"
        "event.currentTarget===eventA&&event.eventPhase===2;});");
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    const bool clicked = result.succeeded() && harness.dispatchClick(
        [&harness]() {
            for (const auto& element : harness.document().structuralElements)
                if (element.id == "event-a") return element.serial;
            return static_cast<HostInstanceId>(0u);
        }(), clickError);
    expect(result.succeeded() && booleanIs(harness, "remainingOk") &&
        numberIs(harness, "keyCount") == 3.0 &&
        numberIs(harness, "entryCount") == 3.0 &&
        numberIs(harness, "grew") == 2.0 &&
        numberIs(harness, "newGrow") == 3.0 &&
        numberIs(harness, "shrunk") == 2.0 &&
        binding(harness, "shrinkIds") != nullptr &&
        harness.runtime().stringValue(*binding(harness, "shrinkIds")) ==
            "s0s2" &&
        numberIs(harness, "callbackPairs") == 9.0 && clicked &&
        numberIs(harness, "scopedCount") == 1.0 &&
        binding(harness, "filteredIds") != nullptr &&
        harness.runtime().stringValue(*binding(harness, "filteredIds")) ==
            "p0p2" && numberIs(harness, "tagCount") == 2.0 &&
        numberIs(harness, "classCount") == 2.0 &&
        clickError == RuntimeErrorCode::None &&
        numberIs(harness, "eventCount") == 3.0 &&
        booleanIs(harness, "eventAOk") && booleanIs(harness, "eventBOk"),
        "JS84 VALUES/KEYS/ENTRIES, cursor continuation, live mutation, callbacks, and nested Events");

    NavigatorScriptExecutionHarness entriesHarness;
    const std::string entriesHtml =
        "<html><body><i class='row'></i><i class='row'></i>"
        "<i class='row'></i></body></html>";
    if (!loadHtml(entriesHarness, entriesHtml,
            "JS84 ENTRIES allocation fixture loads")) return;
    const std::size_t objectsBefore = entriesHarness.runtime().objectCount();
    const std::size_t propertiesBefore =
        entriesHarness.runtime().propertyCount();
    const std::size_t slotsBefore =
        entriesHarness.runtime().arrayElementCount();
    const std::size_t environmentsBefore =
        entriesHarness.runtime().environmentCount();
    const ScriptResult entriesResult = entriesHarness.execute(
        "var rows=document.querySelectorAll('.row');var pairs=0;"
        "for(const pair of rows.entries()){if(pair[0]===pairs)pairs++;}");
    expect(entriesResult.succeeded() && numberIs(entriesHarness, "pairs") == 3.0 &&
        entriesHarness.runtime().objectCount() - objectsBefore == 7u &&
        entriesHarness.runtime().propertyCount() - propertiesBefore == 8u &&
        entriesHarness.runtime().arrayElementCount() - slotsBefore == 6u &&
        entriesHarness.runtime().environmentCount() - environmentsBefore == 3u,
        "JS84 ENTRIES keeps three pair Arrays, six slots, four outer results, and per-iteration scopes");
}

void testGenerationAndSerialReuse()
{
    NavigatorScriptExecutionHarness harness;
    const std::string firstHtml =
        "<html><body><i id='same' class='row'></i></body></html>";
    if (!loadHtml(harness, firstHtml,
            "JS84 generation fixture loads")) return;
    const HostGenerationId oldGeneration = harness.hostAdapter().generation();
    const ScriptResult setup = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows.values();var first=cursor.next();var seen=0;");
    HostInstanceId oldSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "same") oldSerial = element.serial;
    RuntimeErrorCode generationError = RuntimeErrorCode::None;
    const bool invalidated =
        harness.invalidateDocumentGeneration(generationError);
    harness.document() = gxos::web::parseHtml(
        "file:///js84-generation-replacement.html", firstHtml);
    harness.hostAdapter().attachDocument(harness.document(),
        harness.runtime().hostGeneration());
    HostInstanceId newSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "same") newSerial = element.serial;
    const ScriptResult stale = harness.execute(
        "for(const row of cursor){seen++;}");
    expect(setup.succeeded() && invalidated &&
        generationError == RuntimeErrorCode::None && oldSerial != 0u &&
        oldSerial == newSerial &&
        harness.hostAdapter().generation() != oldGeneration &&
        !stale.succeeded() &&
        stale.runtimeError.code == RuntimeErrorCode::StaleHostObject &&
        numberIs(harness, "seen") == 0.0 &&
        NavigatorScriptIteratorTestAccess::activeIteratorCount(
            harness.hostAdapter()) == 0u,
        "JS84 rejects a stale explicit cursor when structural serials are reused");
}

#ifdef GXOS_JAVASCRIPT_LIFETIME_DIAGNOSTICS
void testLifetimeDiagnosticsAndRetention()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness,
            "<html><body><i class='row'></i><i class='row'></i>"
            "<i class='row'></i></body></html>",
            "JS84 discarded-result fixture loads")) return;
    const ScriptResult setup = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows.values();");
    const RuntimeDiagnostics before = harness.runtime().diagnostics();
    const std::size_t objectsBefore = harness.runtime().objectCount();
    const std::size_t propertiesBefore = harness.runtime().propertyCount();
    const ScriptResult discarded = harness.execute(
        "cursor.next();cursor.next();");
    const RuntimeDiagnostics afterDiscard = harness.runtime().diagnostics();
    expect(setup.succeeded() && discarded.succeeded() &&
        harness.runtime().objectCount() == objectsBefore + 2u &&
        harness.runtime().propertyCount() == propertiesBefore + 4u &&
        harness.runtime().finalValue().isObject() &&
        harness.runtime().finalValue().objectId() ==
            static_cast<RuntimeObjectId>(objectsBefore + 1u) &&
        afterDiscard.iteratorResultObjectsAllocated -
            before.iteratorResultObjectsAllocated == 2u &&
        afterDiscard.iteratorResultObjectsPublished -
            before.iteratorResultObjectsPublished == 2u &&
        afterDiscard.runtimePropertiesAllocated -
            before.runtimePropertiesAllocated == 4u &&
        afterDiscard.hostHandlesCreated - before.hostHandlesCreated == 2u &&
        afterDiscard.hostHandleLiveHighWater >=
            harness.runtime().hostObjectCount() &&
        afterDiscard.hostHandleRegistryHighWater >=
            harness.runtime().hostObjectCount() &&
        afterDiscard.objectPoolHighWater >= harness.runtime().objectCount() &&
        afterDiscard.propertyPoolHighWater >= harness.runtime().propertyCount(),
        "JS84 discarded next() results remain allocated and are attributed after their expression frames return");

    const RuntimeDiagnostics beforeNested = harness.runtime().diagnostics();
    const ScriptResult retained = harness.execute(
        "var kept=cursor.next();var alias=kept;var list=[kept];"
        "var holder={};holder.result=kept;holder.self=holder;"
        "var referencesStable=kept===alias&&list[0]===kept&&"
        "holder.result===kept&&holder.self===holder;"
        "function inner(){return cursor.next();}"
        "function outer(){return inner();}var nestedResult=outer();");
    const Value* keptValue = binding(harness, "kept");
    const Value* aliasValue = binding(harness, "alias");
    const Value* listValue = binding(harness, "list");
    const Value* holderValue = binding(harness, "holder");
    const Value* stableValue = binding(harness, "referencesStable");
    const Value* nestedResult = binding(harness, "nestedResult");
    RuntimeErrorCode propertyError = RuntimeErrorCode::None;
    Value arraySlot;
    Value propertyValue;
    Value cycleValue;
    const bool retainedPathsRead = keptValue != nullptr && keptValue->isObject() &&
        listValue != nullptr && listValue->isObject() &&
        holderValue != nullptr && holderValue->isObject() &&
        harness.runtime().readObjectPropertyForHost(listValue->objectId(),
            "0", arraySlot, propertyError) && arraySlot.isObject() &&
        harness.runtime().readObjectPropertyForHost(holderValue->objectId(),
            "result", propertyValue, propertyError) && propertyValue.isObject() &&
        harness.runtime().readObjectPropertyForHost(holderValue->objectId(),
            "self", cycleValue, propertyError) && cycleValue.isObject();
    expect(retained.succeeded() && aliasValue != nullptr &&
        stableValue != nullptr && stableValue->isBoolean() &&
        stableValue->booleanValue() && keptValue != nullptr &&
        keptValue->isObject() && aliasValue->isObject() &&
        keptValue->objectId() == aliasValue->objectId() &&
        nestedResult != nullptr && nestedResult->isObject() &&
        retainedPathsRead && arraySlot.objectId() == keptValue->objectId() &&
        propertyValue.objectId() == keptValue->objectId() &&
        cycleValue.objectId() == holderValue->objectId(),
        "JS84 retained aliases, Array and property edges, cycles, and nested return values preserve object identity");
    expect(harness.runtime().diagnostics().environmentsAllocated -
            beforeNested.environmentsAllocated == 2u &&
        harness.runtime().diagnostics().environmentPoolHighWater >=
            harness.runtime().environmentCount(),
        "JS84 call environments remain retained and are counted after nested return");

    const Value savedOutsideRuntime = keptValue == nullptr
        ? Value::undefined() : *keptValue;
    Value savedYield;
    const bool readSavedBefore = savedOutsideRuntime.isObject() &&
        harness.runtime().readObjectPropertyForHost(
            savedOutsideRuntime.objectId(), "value", savedYield, propertyError) &&
        savedYield.isHostObject();
    const ScriptResult terminal = harness.execute("cursor.next();");
    Value savedYieldAfter;
    const bool readSavedAfter = savedOutsideRuntime.isObject() &&
        harness.runtime().readObjectPropertyForHost(
            savedOutsideRuntime.objectId(), "value", savedYieldAfter,
            propertyError) && savedYieldAfter.isHostObject();
    expect(terminal.succeeded() && readSavedBefore && readSavedAfter &&
        savedYield.hostObjectId() == savedYieldAfter.hostObjectId() &&
        harness.runtime().diagnostics().objectPoolHighWater ==
            harness.runtime().objectCount(),
        "JS84 host-held result and canonical yielded Element survive later allocations without slot reuse");

    const std::string cursorSource = [&]() {
        std::string source = "var registryRows=document.querySelectorAll('.row');";
        for (std::size_t index = 0u;
            index < kNavigatorScriptMaxCollectionIterators; ++index) {
            source += "var registryCursor" + std::to_string(index) +
                "=registryRows.values();";
        }
        return source;
    }();
    NavigatorScriptExecutionHarness registry;
    if (!loadHtml(registry, "<html><body><i class='row'></i></body></html>",
            "JS84 iterator-registry fixture loads")) return;
    const ScriptResult fullRegistry = registry.execute(cursorSource);
    const ScriptResult registryOverflow = registry.execute(
        "var registryOverflow=registryRows.keys();");
    expect(fullRegistry.succeeded() && !registryOverflow.succeeded() &&
        registryOverflow.runtimeError.code ==
            RuntimeErrorCode::HostCallbackLimitExceeded &&
        registry.hostAdapter().activeSelectorCollectionIteratorCount() ==
            kNavigatorScriptMaxCollectionIterators &&
        registry.hostAdapter().selectorCollectionIteratorHighWater() ==
            kNavigatorScriptMaxCollectionIterators &&
        registry.hostAdapter().selectorCollectionIteratorAllocations() ==
            kNavigatorScriptMaxCollectionIterators &&
        registry.hostAdapter().selectorCollectionIteratorAllocationFailures() ==
            1u,
        "JS84 fixed iterator-registry diagnostics report saturation without overwriting live cursors");

    RuntimeContext baseline;
    RuntimeLimits propertyLimits;
    propertyLimits.maxTotalProperties = baseline.propertyCount() + 1u;
    RuntimeContext rollback(propertyLimits);
    const std::size_t rollbackObjects = rollback.objectCount();
    const std::size_t rollbackProperties = rollback.propertyCount();
    const std::size_t rollbackArraySlots = rollback.arrayElementCount();
    Value unpublished;
    RuntimeErrorCode rollbackError = RuntimeErrorCode::None;
    const bool entryCreated = rollback.createCollectionIteratorEntryResult(
        0u, HostValue::number(1.0), unpublished, rollbackError);
    const RuntimeDiagnostics rollbackDiagnostics = rollback.diagnostics();
    expect(rollback.builtInsInitialized() &&
        !entryCreated && rollbackError == RuntimeErrorCode::PropertyLimitExceeded &&
        unpublished.isUndefined() && rollback.objectCount() == rollbackObjects &&
        rollback.propertyCount() == rollbackProperties &&
        rollback.arrayElementCount() == rollbackArraySlots &&
        rollbackDiagnostics.collectionResultRollbacks == 1u &&
        rollbackDiagnostics.collectionEntryRollbacks == 1u &&
        rollbackDiagnostics.iteratorResultObjectsAllocated == 1u &&
        rollbackDiagnostics.iteratorResultObjectsPublished == 0u &&
        rollbackDiagnostics.iteratorEntryArraysAllocated == 1u &&
        rollbackDiagnostics.iteratorEntryArraysPublished == 0u &&
        rollbackDiagnostics.propertyPoolHighWater == rollbackProperties + 1u &&
        rollbackDiagnostics.failedPropertyAllocations == 1u,
        "JS84 allocation rollback remains separate from publication and records transient high-water use");

    RuntimeLimits arrayLimits;
    arrayLimits.maxArrayElements = 2u;
    arrayLimits.maxTotalArrayElements = 2u;
    arrayLimits.maxArrayIndex = 1u;
    RuntimeContext arrayLimited(arrayLimits);
    const std::string arraySource = "var values=[1,2];values.push(3);";
    const ScriptResult arrayFailure = arrayLimited.executeInSameRealm(
        SourceView(arraySource.data(), arraySource.size()));
    expect(!arrayFailure.succeeded() &&
        arrayFailure.runtimeError.code == RuntimeErrorCode::ArrayLimitExceeded &&
        arrayLimited.arrayElementCount() == 2u &&
        arrayLimited.diagnostics().arraySlotsAllocated == 2u &&
        arrayLimited.diagnostics().arraySlotHighWater == 2u &&
        arrayLimited.diagnostics().failedArraySlotAllocations == 1u,
        "JS84 Array-slot diagnostics retain deterministic exhaustion accounting");

    NavigatorScriptExecutionHarness hostBaseline;
    const std::string hostHtml =
        "<html><body><i class='row'></i></body></html>";
    if (!loadHtml(hostBaseline, hostHtml,
            "JS84 host-handle baseline fixture loads")) return;
    const std::string hostSetup =
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows.entries();";
    const ScriptResult hostBaselineSetup = hostBaseline.execute(hostSetup);
    const std::size_t hostHandleBaseline =
        hostBaseline.runtime().hostObjectCount();
    RuntimeLimits hostLimits;
    hostLimits.maxHostObjects = hostHandleBaseline;
    NavigatorScriptExecutionHarness hostLimited(hostLimits);
    if (!loadHtml(hostLimited, hostHtml,
            "JS84 host-handle saturation fixture loads")) return;
    const ScriptResult hostSetupResult = hostLimited.execute(hostSetup);
    const std::size_t hostObjectsBefore =
        hostLimited.runtime().hostObjectCount();
    const std::size_t runtimeObjectsBefore =
        hostLimited.runtime().objectCount();
    const std::size_t arraySlotsBefore =
        hostLimited.runtime().arrayElementCount();
    const ScriptResult hostFailure = hostLimited.execute(
        "var unavailable=cursor.next();");
    expect(hostBaselineSetup.succeeded() && hostHandleBaseline > 0u &&
        hostSetupResult.succeeded() && !hostFailure.succeeded() &&
        hostFailure.runtimeError.code ==
            RuntimeErrorCode::HostObjectLimitExceeded &&
        hostLimited.runtime().hostObjectCount() == hostObjectsBefore &&
        hostLimited.runtime().objectCount() == runtimeObjectsBefore &&
        hostLimited.runtime().arrayElementCount() == arraySlotsBefore &&
        hostLimited.runtime().diagnostics().failedHostHandleAllocations == 1u &&
        hostLimited.runtime().diagnostics().collectionEntryRollbacks == 1u,
        "JS84 host-handle exhaustion is attributed and rolls back before publishing or advancing an entry result");

    RuntimeLimits environmentLimits;
    environmentLimits.maxEnvironments = 1u;
    RuntimeContext environmentLimited(environmentLimits);
    const std::string environmentSource =
        "function bounded(){return 1;}bounded();";
    const ScriptResult environmentFailure =
        environmentLimited.executeInSameRealm(
            SourceView(environmentSource.data(), environmentSource.size()));
    expect(!environmentFailure.succeeded() &&
        environmentFailure.runtimeError.code ==
            RuntimeErrorCode::EnvironmentLimitExceeded &&
        environmentLimited.diagnostics().environmentPoolHighWater == 1u &&
        environmentLimited.diagnostics().failedEnvironmentAllocations == 1u,
        "JS84 environment high-water and exhaustion counters preserve the no-reuse scope contract");

    RuntimeContext direct;
    Value result;
    RuntimeErrorCode directError = RuntimeErrorCode::None;
    const bool directCreated = direct.createCollectionIteratorResult(
        HostValue::number(7.0), false, result, directError);
    Value directValue;
    Value directDone;
    const bool directReadable = directCreated && result.isObject() &&
        direct.readObjectPropertyForHost(result.objectId(), "value",
            directValue, directError) &&
        direct.readObjectPropertyForHost(result.objectId(), "done",
            directDone, directError);
    const RuntimeDiagnostics directDiagnostics = direct.diagnostics();
    expect(directReadable && directValue.isNumber() &&
        directValue.numberValue() == 7.0 && directDone.isBoolean() &&
        !directDone.booleanValue() &&
        directDiagnostics.iteratorResultObjectsAllocated == 1u &&
        directDiagnostics.iteratorResultObjectsPublished == 1u &&
        directDiagnostics.runtimePropertiesAllocated >= 2u,
        "JS84 ordinary iterator-result diagnostics distinguish successful publication");

    direct.reset();
    expect(direct.diagnostics().iteratorResultObjectsAllocated == 0u &&
        direct.diagnostics().objectPoolHighWater == direct.objectCount() &&
        direct.diagnostics().runtimeObjectsAllocated == direct.objectCount() &&
        direct.diagnostics().environmentPoolHighWater == 1u,
        "JS84 runtime reset releases realm pools and starts fresh bounded diagnostics");
}
#endif

void testBoundedTraversalAndOwnership()
{
    const std::size_t sizes[] = {0u, 1u, 16u, 64u, 128u, 256u, 512u,
        1019u, 1020u, 1024u};
    std::cout << "JS84 traversal capacity measurements:\n";
    for (const std::size_t size : sizes) {
        NavigatorScriptExecutionHarness harness;
        const bool loaded = loadHtml(harness, htmlWithRows(size),
            "JS84 bounded traversal fixture loads");
        if (!loaded) continue;
        const std::size_t objectsBefore = harness.runtime().objectCount();
        const std::size_t propertiesBefore = harness.runtime().propertyCount();
        const std::size_t environmentsBefore =
            harness.runtime().environmentCount();
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult result = harness.execute(
            "var rows=document.querySelectorAll('.row');var bodyCount=0;"
            "for(const row of rows)bodyCount++;");
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() -
                start).count();
        const bool exhausted = size >= 1020u;
        const bool expected = exhausted
            ? !result.succeeded() &&
                result.runtimeError.code == RuntimeErrorCode::ObjectLimitExceeded &&
                numberIs(harness, "bodyCount") == 1020.0
            : result.succeeded() &&
                numberIs(harness, "bodyCount") == static_cast<double>(size);
        expect(expected, "JS84 bounded traversal completes or fails at the object pool limit");
#ifdef GXOS_JAVASCRIPT_LIFETIME_DIAGNOSTICS
        const RuntimeDiagnostics& diagnostics =
            harness.runtime().diagnostics();
        expect(diagnostics.objectPoolHighWater ==
                harness.runtime().objectCount() &&
            diagnostics.propertyPoolHighWater >=
                harness.runtime().propertyCount() &&
            diagnostics.environmentPoolHighWater >=
                harness.runtime().environmentCount() &&
            (!exhausted || diagnostics.failedObjectAllocations == 1u),
            "JS84 bounded pool high-water and deterministic failed-allocation counters match traversal state");
#endif
        NavigatorScriptIteratorTestAccess::Iterator iterator;
        const bool hasIterator =
            NavigatorScriptIteratorTestAccess::iteratorAt(
                harness.hostAdapter(), 0u, iterator);
        expect(hasIterator && iterator.nextIndex == (exhausted ? 1020u : size),
            "JS84 failed next allocation does not advance or masquerade as exhaustion");
        std::cout << "  elements=" << size << " status="
            << (result.succeeded() ? "complete" :
                runtimeErrorCodeName(result.runtimeError.code))
            << " body=" << numberIs(harness, "bodyCount")
            << " objects=" << (harness.runtime().objectCount() - objectsBefore)
            << " properties=" << (harness.runtime().propertyCount() - propertiesBefore)
            << " environments=" <<
                (harness.runtime().environmentCount() - environmentsBefore)
            << " hostObjects=" << harness.runtime().hostObjectCount()
            << " iterators=" <<
                NavigatorScriptIteratorTestAccess::activeIteratorCount(
                    harness.hostAdapter())
#ifdef GXOS_JAVASCRIPT_LIFETIME_DIAGNOSTICS
            << " iteratorHighWater=" <<
                harness.hostAdapter().selectorCollectionIteratorHighWater()
#endif
            << " elapsedUs=" << elapsed << '\n';
    }

    struct Benchmark {
        bool succeeded = false;
        double visited = -1.0;
        std::size_t objects = 0u;
        std::size_t environments = 0u;
        std::size_t hostOperations = 0u;
        long long elapsedUs = 0;
    };
    const auto measure = [](const std::string& source,
        const std::string& html) {
        Benchmark result;
        long long samples[5] = {};
        for (std::size_t sample = 0u; sample < 5u; ++sample) {
            NavigatorScriptExecutionHarness harness;
            RuntimeErrorCode loadError = RuntimeErrorCode::None;
            if (!harness.loadHtml("file:///js84-benchmark.html", html,
                    loadError) || loadError != RuntimeErrorCode::None) return result;
            const std::size_t objectsBefore = harness.runtime().objectCount();
            const std::size_t environmentsBefore =
                harness.runtime().environmentCount();
            const std::size_t operationsBefore =
                harness.runtime().hostOperationCount();
            const auto start = std::chrono::steady_clock::now();
            const ScriptResult scriptResult = harness.execute(source);
            samples[sample] = std::chrono::duration_cast<
                std::chrono::microseconds>(std::chrono::steady_clock::now() -
                    start).count();
            result.succeeded = scriptResult.succeeded();
            result.visited = numberIs(harness, "visited");
            result.objects = harness.runtime().objectCount() - objectsBefore;
            result.environments = harness.runtime().environmentCount() -
                environmentsBefore;
            result.hostOperations = harness.runtime().hostOperationCount() -
                operationsBefore;
            if (!result.succeeded) return result;
        }
        for (std::size_t index = 1u; index < 5u; ++index) {
            const long long value = samples[index];
            std::size_t insertion = index;
            while (insertion > 0u && samples[insertion - 1u] > value) {
                samples[insertion] = samples[insertion - 1u];
                --insertion;
            }
            samples[insertion] = value;
        }
        result.elapsedUs = samples[2];
        return result;
    };
    const Benchmark forOf = measure(
        "var rows=document.querySelectorAll('.row');var visited=0;"
        "for(let element of rows)visited++;", htmlWithRows(128u));
    const Benchmark forEach = measure(
        "var rows=document.querySelectorAll('.row');var visited=0;"
        "rows.forEach(function(element){visited++;});", htmlWithRows(128u));
    const Benchmark valuesNext = measure(
        "var rows=document.querySelectorAll('.row');var cursor=rows.values();"
        "var visited=0;var step=cursor.next();while(!step.done){"
        "visited++;step=cursor.next();}", htmlWithRows(128u));
    const Benchmark emptyLoop = measure(
        "var rows=document.querySelectorAll('.row');var visited=0;"
        "for(const element of rows)visited++;", htmlWithRows(0u));
    const Benchmark nestedLoops = measure(
        "var rows=document.querySelectorAll('.row');var visited=0;"
        "for(const outer of rows){for(const inner of rows)visited++;}",
        htmlWithRows(8u));
    const Benchmark earlyBreak = measure(
        "var rows=document.querySelectorAll('.row');var visited=0;"
        "for(const element of rows){visited++;break;}", htmlWithRows(128u));
    const Benchmark continueLoop = measure(
        "var rows=document.querySelectorAll('.row');var visited=0;"
        "for(const element of rows){visited++;continue;}", htmlWithRows(128u));
    const Benchmark closeLoop = measure(
        "var closed=0;var cursor={next:function(){return {value:1,done:false};}};"
        "cursor['return']=function(){closed++;return {};};var iterable={};"
        "iterable[Symbol.iterator]=function(){return cursor;};var visited=0;"
        "for(const item of iterable){visited++;break;}",
        "<html><body></body></html>");
    std::string filteredHtml = "<html><body>";
    for (std::size_t index = 0u; index < 128u; ++index) {
        filteredHtml += index % 2u == 0u
            ? "<i class='pos selected'></i>" : "<i class='pos'></i>";
    }
    filteredHtml += "</body></html>";
    const Benchmark filteredLoop = measure(
        "var rows=document.querySelectorAll("
        "'.pos:nth-child(odd of .selected)');var visited=0;"
        "for(const element of rows)visited++;", filteredHtml);
    std::cout << "JS84 benchmark visit counts: for-of=" << forOf.visited
        << " forEach=" << forEach.visited << " values=" << valuesNext.visited
        << " empty=" << emptyLoop.visited << " nested=" << nestedLoops.visited
        << " break=" << earlyBreak.visited << " continue="
        << continueLoop.visited << " close=" << closeLoop.visited
        << " filtered=" << filteredLoop.visited << '\n';
    expect(forOf.succeeded && forEach.succeeded && valuesNext.succeeded &&
        emptyLoop.succeeded && nestedLoops.succeeded && earlyBreak.succeeded &&
        continueLoop.succeeded && closeLoop.succeeded && filteredLoop.succeeded &&
        forOf.visited == 128.0 && forEach.visited == 128.0 &&
        valuesNext.visited == 128.0 && emptyLoop.visited == 0.0 &&
        nestedLoops.visited == 64.0 && earlyBreak.visited == 1.0 &&
        continueLoop.visited == 128.0 && closeLoop.visited == 1.0 &&
        filteredLoop.visited == 32.0,
        "JS84 benchmark scenarios complete for result allocation, empty/nested loops, break, continue, close, callbacks, and filtered selectors");
    std::cout << "JS84 median of five fresh-context traversal samples (microseconds):\n"
        << "  for-of=" << forOf.elapsedUs << " objects=" << forOf.objects
        << " environments=" << forOf.environments << " hostOperations="
        << forOf.hostOperations << '\n'
        << "  forEach=" << forEach.elapsedUs << " objects=" << forEach.objects
        << " environments=" << forEach.environments << " hostOperations="
        << forEach.hostOperations << '\n'
        << "  values().next()=" << valuesNext.elapsedUs << " objects="
        << valuesNext.objects << " environments=" << valuesNext.environments
        << " hostOperations=" << valuesNext.hostOperations << '\n'
        << "  empty-loop=" << emptyLoop.elapsedUs
        << " nested-8x8=" << nestedLoops.elapsedUs
        << " early-break=" << earlyBreak.elapsedUs
        << " continue=" << continueLoop.elapsedUs
        << " iterator-close=" << closeLoop.elapsedUs
        << " filtered-selector=" << filteredLoop.elapsedUs << '\n';

    long long resultAllocationSamples[5] = {};
    bool resultAllocationSucceeded = true;
    for (std::size_t sample = 0u; sample < 5u; ++sample) {
        RuntimeContext resultAllocation;
        const auto resultAllocationStart = std::chrono::steady_clock::now();
        RuntimeErrorCode resultAllocationError = RuntimeErrorCode::None;
        Value lastResult;
        for (std::size_t index = 0u; index < 100u; ++index) {
            if (!resultAllocation.createCollectionIteratorResult(
                    HostValue::number(static_cast<double>(index)), false,
                    lastResult, resultAllocationError)) {
                resultAllocationSucceeded = false;
                break;
            }
        }
        resultAllocationSamples[sample] = std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() -
                resultAllocationStart).count();
        if (!resultAllocationSucceeded) break;
    }
    for (std::size_t index = 1u; index < 5u; ++index) {
        const long long value = resultAllocationSamples[index];
        std::size_t insertion = index;
        while (insertion > 0u &&
            resultAllocationSamples[insertion - 1u] > value) {
            resultAllocationSamples[insertion] =
                resultAllocationSamples[insertion - 1u];
            --insertion;
        }
        resultAllocationSamples[insertion] = value;
    }
    expect(resultAllocationSucceeded,
        "JS84 isolated iterator-result and property initialization benchmark completes");
    std::cout << "  median 100 result-object allocations plus 200 properties="
        << resultAllocationSamples[2] << " us\n";
}

} // namespace

int main()
{
    testHostedFixture();
    testBasicLoopsAndBindings();
    testProtocolAndIteratorClose();
    testInvalidProtocolValues();
    testExplicitModesMutationAndCallbacks();
    testGenerationAndSerialReuse();
#ifdef GXOS_JAVASCRIPT_LIFETIME_DIAGNOSTICS
    testLifetimeDiagnosticsAndRetention();
#endif
    testBoundedTraversalAndOwnership();
    std::cout << "Navigator JavaScript JS84 checks: " << checks - failures
        << '/' << checks << " passed\n";
    return failures == 0u ? 0 : 1;
}
