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
    const bool loaded = harness.loadHtml("file:///js83-native.html", html,
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
    std::ifstream file("navigator-smoke/javascript-js83.html",
        std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, html, "JS83 hosted fixture loads natively")) return;
    const auto& scripts = harness.document().scriptSources;
    expect(scripts.size() == 1u && scripts[0].find('<') == std::string::npos,
        "JS83 hosted inline script preserves the HTML-subset integrity rule");
    if (scripts.size() != 1u) return;
    const ScriptResult result = harness.execute(scripts[0]);
    const Value* marker = binding(harness, "marker");
    expect(result.succeeded() && marker != nullptr && marker->isString() &&
        harness.runtime().stringValue(*marker) ==
            "JS83:assertions=9:failures=0:loops=true:mutation=true:modes=true",
        "JS83 hosted fixture runs all initial visible assertions");

    HostInstanceId eventSerial = 0u;
    for (const auto& element : harness.document().structuralElements)
        if (element.id == "event-a") eventSerial = element.serial;
    RuntimeErrorCode clickError = RuntimeErrorCode::None;
    const bool clicked = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, clickError);
    expect(clicked && clickError == RuntimeErrorCode::None &&
        numberIs(harness, "nestedEvents") == 3.0 &&
        booleanIs(harness, "eventOk"),
        "JS83 hosted fixture preserves nested Event iteration and metadata");
}

void testBasicLoopsAndBindings()
{
    NavigatorScriptExecutionHarness harness;
    const std::string html =
        "<html><body><i id='a' class='row'></i>"
        "<i id='b' class='row'></i><i id='c' class='row'></i>"
        "</body></html>";
    if (!loadHtml(harness, html, "JS83 basic fixture loads")) return;

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
        "JS83 const/let bindings, canonical values, scopes, continue, nested loops, break, and classic loops");

    NavigatorScriptExecutionHarness immutable;
    if (!loadHtml(immutable, html, "JS83 const assignment fixture loads"))
        return;
    const ScriptResult constWrite = immutable.execute(
        "var rows=document.querySelectorAll('.row');"
        "for(const element of rows){element=null;}");
    expect(!constWrite.succeeded() &&
        constWrite.runtimeError.code == RuntimeErrorCode::TypeError,
        "JS83 const iteration binding rejects assignment");
}

void testProtocolAndIteratorClose()
{
    NavigatorScriptExecutionHarness harness;
    if (!loadHtml(harness, "<html><body></body></html>",
            "JS83 custom iterator fixture loads")) return;
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
        "JS83 uses real Symbol.iterator and next calls with receivers, done truthiness, and applicable close");

    NavigatorScriptExecutionHarness bodyError;
    if (!loadHtml(bodyError, "<html><body></body></html>",
            "JS83 close-on-error fixture loads")) return;
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
        "JS83 body failure closes once and preserves its original runtime error");
}

void testInvalidProtocolValues()
{
    const char* values[] = {"null", "undefined", "123", "false"};
    for (const char* value : values) {
        NavigatorScriptExecutionHarness harness;
        if (!loadHtml(harness, "<html><body></body></html>",
                "JS83 non-iterable fixture loads")) continue;
        const ScriptResult result = harness.execute(std::string(
            "for(let element of ") + value + "){}" );
        expect(!result.succeeded() &&
            (result.runtimeError.code == RuntimeErrorCode::CannotReadProperty ||
                result.runtimeError.code == RuntimeErrorCode::NotCallable),
            "JS83 rejects null, undefined, and primitive non-iterables");
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
                "JS83 invalid protocol fixture loads")) continue;
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
    if (!loadHtml(harness, html, "JS83 mutation and Event fixture loads"))
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
        "JS83 VALUES/KEYS/ENTRIES, cursor continuation, live mutation, callbacks, and nested Events");

    NavigatorScriptExecutionHarness entriesHarness;
    const std::string entriesHtml =
        "<html><body><i class='row'></i><i class='row'></i>"
        "<i class='row'></i></body></html>";
    if (!loadHtml(entriesHarness, entriesHtml,
            "JS83 ENTRIES allocation fixture loads")) return;
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
        "JS83 ENTRIES keeps three pair Arrays, six slots, four outer results, and per-iteration scopes");
}

void testGenerationAndSerialReuse()
{
    NavigatorScriptExecutionHarness harness;
    const std::string firstHtml =
        "<html><body><i id='same' class='row'></i></body></html>";
    if (!loadHtml(harness, firstHtml,
            "JS83 generation fixture loads")) return;
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
        "file:///js83-generation-replacement.html", firstHtml);
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
        "JS83 rejects a stale explicit cursor when structural serials are reused");
}

void testBoundedTraversalAndOwnership()
{
    const std::size_t sizes[] = {0u, 1u, 16u, 64u, 128u, 256u, 512u,
        1019u, 1020u, 1024u};
    std::cout << "JS83 traversal capacity measurements:\n";
    for (const std::size_t size : sizes) {
        NavigatorScriptExecutionHarness harness;
        const bool loaded = loadHtml(harness, htmlWithRows(size),
            "JS83 bounded traversal fixture loads");
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
        expect(expected, "JS83 bounded traversal completes or fails at the object pool limit");
        NavigatorScriptIteratorTestAccess::Iterator iterator;
        const bool hasIterator =
            NavigatorScriptIteratorTestAccess::iteratorAt(
                harness.hostAdapter(), 0u, iterator);
        expect(hasIterator && iterator.nextIndex == (exhausted ? 1020u : size),
            "JS83 failed next allocation does not advance or masquerade as exhaustion");
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
    const auto measure = [](const std::string& source) {
        Benchmark result;
        NavigatorScriptExecutionHarness harness;
        RuntimeErrorCode loadError = RuntimeErrorCode::None;
        if (!harness.loadHtml("file:///js83-benchmark.html",
                htmlWithRows(128u), loadError) ||
            loadError != RuntimeErrorCode::None) return result;
        const std::size_t objectsBefore = harness.runtime().objectCount();
        const std::size_t environmentsBefore =
            harness.runtime().environmentCount();
        const std::size_t operationsBefore =
            harness.runtime().hostOperationCount();
        const auto start = std::chrono::steady_clock::now();
        const ScriptResult scriptResult = harness.execute(source);
        result.elapsedUs = std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() -
                start).count();
        result.succeeded = scriptResult.succeeded();
        result.visited = numberIs(harness, "visited");
        result.objects = harness.runtime().objectCount() - objectsBefore;
        result.environments = harness.runtime().environmentCount() -
            environmentsBefore;
        result.hostOperations = harness.runtime().hostOperationCount() -
            operationsBefore;
        return result;
    };
    const Benchmark forOf = measure(
        "var rows=document.querySelectorAll('.row');var visited=0;"
        "for(let element of rows)visited++;");
    const Benchmark forEach = measure(
        "var rows=document.querySelectorAll('.row');var visited=0;"
        "rows.forEach(function(element){visited++;});");
    const Benchmark valuesNext = measure(
        "var rows=document.querySelectorAll('.row');var cursor=rows.values();"
        "var visited=0;var step=cursor.next();while(!step.done){"
        "visited++;step=cursor.next();}");
    expect(forOf.succeeded && forEach.succeeded && valuesNext.succeeded &&
        forOf.visited == 128.0 && forEach.visited == 128.0 &&
        valuesNext.visited == 128.0,
        "JS83 traversal comparison completes at 128 Elements for for-of, forEach, and values().next()");
    std::cout << "JS83 128-element single-run comparison (microseconds):\n"
        << "  for-of=" << forOf.elapsedUs << " objects=" << forOf.objects
        << " environments=" << forOf.environments << " hostOperations="
        << forOf.hostOperations << '\n'
        << "  forEach=" << forEach.elapsedUs << " objects=" << forEach.objects
        << " environments=" << forEach.environments << " hostOperations="
        << forEach.hostOperations << '\n'
        << "  values().next()=" << valuesNext.elapsedUs << " objects="
        << valuesNext.objects << " environments=" << valuesNext.environments
        << " hostOperations=" << valuesNext.hostOperations << '\n';
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
    testBoundedTraversalAndOwnership();
    std::cout << "Navigator JavaScript JS83 checks: " << checks - failures
        << '/' << checks << " passed\n";
    return failures == 0u ? 0 : 1;
}
