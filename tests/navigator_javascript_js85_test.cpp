#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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

bool booleanIs(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const Value* value = binding(harness, name);
    return value != nullptr && value->isBoolean() && value->booleanValue();
}

HostInstanceId elementSerial(const NavigatorScriptExecutionHarness& harness,
    const char* id)
{
    for (const auto& element : harness.document().structuralElements)
        if (element.id == id) return element.serial;
    return 0u;
}

bool resultAndYield(NavigatorScriptExecutionHarness& harness,
    const char* name, Value& result, Value& yielded)
{
    const Value* value = binding(harness, name);
    if (value == nullptr || !value->isObject()) return false;
    result = *value;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    return harness.runtime().readObjectPropertyForHost(result.objectId(),
        "value", yielded, error) && yielded.isHostObject() &&
        error == RuntimeErrorCode::None;
}

void testRetainedIteratorResultHostEdge()
{
    const std::string html =
        "<html><body><i id='same' class='row'></i>"
        "<i class='row'></i><i class='row'></i><i class='row'></i>"
        "<button id='event-a'></button><button id='event-b'></button>"
        "</body></html>";
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js85-retained-result.html", html, error) &&
        error == RuntimeErrorCode::None,
        "JS85 retained-result fixture loads");

    const ScriptResult setup = harness.execute(
        "var rows=document.querySelectorAll('.row');"
        "var cursor=rows.values();var saved=cursor.next();var alias=saved;"
        "var holder={result:saved};holder.self=holder;var list=[saved];"
        "function inner(){return cursor.next();}"
        "function outer(){return inner();}var escaped=outer();"
        "var escapedAlias=escaped;"
        "var eventA=document.getElementById('event-a');"
        "var eventB=document.getElementById('event-b');"
        "var eventAResult,eventAResultAlias,eventBResult,eventBResultAlias;"
        "var eventAOk=false,eventBOk=false,eventNestedOk=false;"
        "eventB.addEventListener('click',function(event){"
        "eventBResult=cursor.next();eventBResultAlias=eventBResult;"
        "eventBOk=event.target===eventB&&event.currentTarget===eventB;});"
        "eventA.addEventListener('click',function(event){"
        "eventAResult=cursor.next();eventAResultAlias=eventAResult;"
        "eventB.click();eventAOk=event.target===eventA&&"
        "event.currentTarget===eventA;eventNestedOk=eventAResult==="
        "eventAResultAlias&&eventBResult===eventBResultAlias;});");
    expect(setup.succeeded(),
        "JS85 creates direct, aliased, nested-return, and Event-held results");

    const HostInstanceId oldSerial = elementSerial(harness, "same");
    const HostGenerationId oldGeneration = harness.runtime().hostGeneration();
    const std::size_t objectsBeforeEvents = harness.runtime().objectCount();
    const HostInstanceId eventSerial = elementSerial(harness, "event-a");
    const bool clicked = eventSerial != 0u &&
        harness.dispatchClick(eventSerial, error);
    expect(clicked && error == RuntimeErrorCode::None &&
        booleanIs(harness, "eventAOk") && booleanIs(harness, "eventBOk") &&
        booleanIs(harness, "eventNestedOk"),
        "JS85 nested Event callbacks retain their iterator results");

    const ScriptResult identityBefore = harness.execute(
        "var retainedGraphOk=saved===alias&&holder.result===saved&&"
        "holder.self===holder&&list[0]===saved&&escaped===escapedAlias&&"
        "eventAResult===eventAResultAlias&&"
        "eventBResult===eventBResultAlias;");
    expect(identityBefore.succeeded() && booleanIs(harness, "retainedGraphOk") &&
        oldSerial != 0u && oldGeneration != kInvalidHostGenerationId &&
        harness.runtime().objectCount() > objectsBeforeEvents,
        "JS85 verifies property, Array, cycle, callback, and alias identities before replacement");

    const char* resultNames[] = {
        "saved", "escaped", "eventAResult", "eventBResult"};
    std::vector<Value> nativeResults;
    std::vector<Value> oldHostValues;
    std::vector<RuntimeObjectId> oldObjectIds;
    bool allResultEdgesReadable = true;
    for (const char* name : resultNames) {
        Value result;
        Value yielded;
        const bool readable = resultAndYield(harness, name, result, yielded);
        allResultEdgesReadable = allResultEdgesReadable && readable;
        if (readable) {
            oldObjectIds.push_back(result.objectId());
            nativeResults.push_back(result);
            oldHostValues.push_back(yielded);
        }
    }
    expect(allResultEdgesReadable && nativeResults.size() == 4u &&
        oldHostValues.size() == 4u && oldObjectIds.size() == 4u,
        "JS85 captures C++-held results and their stored Element handles");

    const std::size_t objectCountBeforeGeneration = harness.runtime().objectCount();
    const bool invalidated = harness.invalidateDocumentGeneration(error);
    harness.document() = gxos::web::parseHtml(
        "file:///js85-generation-replacement.html", html);
    harness.hostAdapter().attachDocument(harness.document(),
        harness.runtime().hostGeneration());
    const HostInstanceId newSerial = elementSerial(harness, "same");
    expect(invalidated && error == RuntimeErrorCode::None &&
        harness.runtime().hostGeneration() != oldGeneration &&
        oldSerial == newSerial && oldSerial != 0u,
        "JS85 replaces the document while reusing the Element serial");

    const ScriptResult identityAfter = harness.execute(
        "var retainedGraphAfterReplacement=saved===alias&&"
        "holder.result===saved&&holder.self===holder&&list[0]===saved&&"
        "escaped===escapedAlias&&eventAResult===eventAResultAlias&&"
        "eventBResult===eventBResultAlias&&saved.value===saved.value&&"
        "escaped.value===escaped.value&&eventAResult.value===eventAResult.value&&"
        "eventBResult.value===eventBResult.value;");
    bool retainedValuesUnchanged = identityAfter.succeeded() &&
        booleanIs(harness, "retainedGraphAfterReplacement") &&
        harness.runtime().objectCount() == objectCountBeforeGeneration;
    bool oldTokensRejected = nativeResults.size() == 4u &&
        oldHostValues.size() == 4u && oldObjectIds.size() == 4u;
    for (std::size_t index = 0u; index < nativeResults.size(); ++index) {
        Value yieldedAfter;
        RuntimeErrorCode readError = RuntimeErrorCode::None;
        const bool ownerStillSame = nativeResults[index].isObject() &&
            nativeResults[index].objectId() == oldObjectIds[index] &&
            harness.runtime().readObjectPropertyForHost(
                oldObjectIds[index], "value", yieldedAfter, readError) &&
            yieldedAfter.isHostObject() &&
            yieldedAfter.hostObjectId() ==
                oldHostValues[index].hostObjectId();
        retainedValuesUnchanged = retainedValuesUnchanged && ownerStillSame;

        Value ignored;
        readError = RuntimeErrorCode::None;
        const bool oldTokenRead = harness.runtime().readHostPropertyForTesting(
            oldHostValues[index].hostObjectId(), "id", ignored, readError);
        oldTokensRejected = oldTokensRejected && !oldTokenRead &&
            readError == RuntimeErrorCode::StaleHostObject;
    }
    expect(retainedValuesUnchanged,
        "JS85 retains published result identity and old handle tokens across replacement");
    expect(oldTokensRejected,
        "JS85 rejects every old-generation Element token held by a result or C++ Value");

    RuntimeHostObjectId newToken = kInvalidRuntimeHostObjectId;
    const bool newTokenCreated = harness.runtime().registerHostObject(
        newSerial, kNavigatorElementHostKind, newToken, error);
    Value newId;
    const bool newTokenReadable = newTokenCreated && error == RuntimeErrorCode::None &&
        harness.runtime().readHostPropertyForTesting(newToken, "id", newId,
            error) && newId.isString() &&
        harness.runtime().stringValue(newId) == "same";
    bool distinctFromOld = newTokenReadable;
    for (const Value& oldValue : oldHostValues)
        distinctFromOld = distinctFromOld && newToken != oldValue.hostObjectId();
    expect(newTokenReadable && distinctFromOld,
        "JS85 accepts a fresh-generation handle for the same serial without stale-token aliasing");

    const std::size_t replacementAttributeBytes =
        harness.document().retainedAttributeStorage.size();
    const ScriptResult staleCall = harness.execute(
        "saved.value.setAttribute('data-js85-stale','bad');");
    expect(staleCall.succeeded() &&
        harness.document().retainedAttributeStorage.size() ==
            replacementAttributeBytes,
        "JS85 keeps the existing stale-method no-op and does not mutate the replacement Element");

    const Value* savedAfterFailure = binding(harness, "saved");
    expect(oldObjectIds.size() == 4u && savedAfterFailure != nullptr &&
        savedAfterFailure->isObject() &&
        savedAfterFailure->objectId() == oldObjectIds[0] &&
        harness.runtime().objectCount() == objectCountBeforeGeneration,
        "JS85 leaves uncertain published objects allocated and does not reuse their slots");

    const bool replaced = harness.replaceHtml(
        "file:///js85-reset-boundary.html",
        "<html><body><i id='fresh' class='row'></i></body></html>", error);
    expect(replaced && error == RuntimeErrorCode::None &&
        binding(harness, "saved") == nullptr &&
        binding(harness, "eventAResult") == nullptr,
        "JS85 confirms reset clears realm bindings at the existing release boundary");
}

} // namespace

int main()
{
    testRetainedIteratorResultHostEdge();
    std::cout << "Navigator JavaScript JS85 checks: " << checks - failures
        << '/' << checks << " passed.\n";
    return failures == 0u ? 0 : 1;
}
