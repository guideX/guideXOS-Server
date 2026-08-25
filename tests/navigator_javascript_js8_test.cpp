#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using gxos::javascript::NavigatorScriptExecutionHarness;
using gxos::javascript::NavigatorScriptHostLimits;
using gxos::javascript::RuntimeErrorCode;
using gxos::javascript::RuntimeLimits;
using gxos::javascript::ScriptResult;
using gxos::javascript::Value;
using gxos::javascript::ValueType;

namespace {

int failures = 0;

const char* kFixture = R"HTML(
<html><body>
<div id="status">Waiting</div>
<p id="message">Hello</p>
<script>document.getElementById("status").textContent = "BAD";</script>
</body></html>
)HTML";

void expect(bool condition, const std::string& message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
}

const Value* binding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    const std::string key(name);
    return harness.runtime().lookup(
        gxos::javascript::SourceView(key.data(), key.size()));
}

void expectString(const NavigatorScriptExecutionHarness& harness,
    const char* name, const std::string& expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::String, label + ": String");
    if (value->isString()) {
        const std::string actual = harness.runtime().stringValue(*value);
        expect(actual == expected, label + ": value (actual=\"" + actual +
            "\")");
    }
}

void expectBoolean(const NavigatorScriptExecutionHarness& harness,
    const char* name, bool expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::Boolean, label + ": Boolean");
    if (value->isBoolean()) expect(value->booleanValue() == expected,
        label + ": value");
}

void expectNumber(const NavigatorScriptExecutionHarness& harness,
    const char* name, double expected, const std::string& label)
{
    const Value* value = binding(harness, name);
    expect(value != nullptr, label + ": binding exists");
    if (value == nullptr) return;
    expect(value->type() == ValueType::Number, label + ": Number");
    if (value->isNumber()) expect(value->numberValue() == expected,
        label + ": value");
}

void expectError(const ScriptResult& result, RuntimeErrorCode expected,
    const std::string& label)
{
    expect(!result.succeeded(), label + ": fails");
    expect(result.runtimeError.code == expected, label + ": error is " +
        gxos::javascript::runtimeErrorCodeName(result.runtimeError.code));
}

std::uint64_t statusSerial(const NavigatorScriptExecutionHarness& harness)
{
    for (const gxos::web::HtmlElementRef& element :
        harness.document().structuralElements) {
        if (element.id == "status") return element.serial;
    }
    return 0;
}

void testRealDocumentBridge()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js8-fixture.html", kFixture, error),
        "real document fixture loads");
    expect(error == RuntimeErrorCode::None, "fixture load has no runtime error");
    expect(harness.relayout(), "initial controlled relayout succeeds");
    const std::uint64_t status = statusSerial(harness);
    expect(status != 0, "status has authoritative structural serial");
    const std::uint64_t initialRevision = harness.layoutRevision();
    const std::size_t initialExtent = harness.layoutTextExtent();

    ScriptResult result = harness.execute(
        "var a = document; var b = document; var sameDocument = a === b;"
        "var status = document.getElementById(\"status\");"
        "var sameElement = status === document.getElementById(\"status\");"
        "var idResult = status.id; var tagResult = status.tagName;"
        "var textResult = status.textContent;"
        "var missing = document.getElementById(\"does-not-exist\");"
        "var missingResult = missing === null;");
    expect(result.succeeded(), "basic document/element lookup succeeds");
    expectBoolean(harness, "sameDocument", true, "document identity");
    expectBoolean(harness, "sameElement", true, "element identity");
    expectString(harness, "idResult", "status", "element id");
    expectString(harness, "tagResult", "DIV", "canonical tagName");
    expectString(harness, "textResult", "Waiting", "textContent read");
    expectBoolean(harness, "missingResult", true, "missing element is null");
    expectString(harness, "textResult", "Waiting", "HTML script remains inert");

    result = harness.execute(
        "function lookup(doc, id) { return doc.getElementById(id); }"
        "var argumentResult = lookup(document, \"status\").textContent;"
        "function find() { return document.getElementById(\"status\"); }"
        "var found = find(); found.textContent = \"Found\";"
        "var functionResult = found.textContent;");
    expect(result.succeeded(), "document/element user functions succeed");
    expectString(harness, "argumentResult", "Waiting",
        "document function argument");
    expectString(harness, "functionResult", "Found",
        "element returned from function");

    result = harness.execute(
        "function makeWriter(element) { function write() {"
        "element.textContent = \"Updated by closure\"; } return write; }"
        "var writer = makeWriter(status); writer();"
        "var closureResult = status.textContent;");
    expect(result.succeeded(), "element closure mutation succeeds");
    expectString(harness, "closureResult", "Updated by closure",
        "closure mutation");

    const std::size_t beforeMutationExtent = harness.layoutTextExtent();
    result = harness.execute(
        "status.textContent = \"This JavaScript-generated text is substantially longer.\";"
        "var result = status.textContent;");
    expect(result.succeeded(), "textContent mutation succeeds");
    expectString(harness, "result",
        "This JavaScript-generated text is substantially longer.",
        "textContent write readback");
    expect(harness.documentDirty(), "text mutation marks document dirty");
    std::string hostText;
    expect(gxos::javascript::navigatorScriptElementTextContent(
        harness.document(), status, hostText), "host-side text inspection succeeds");
    expect(hostText == "This JavaScript-generated text is substantially longer.",
        "host-side document contains mutation");
    expect(harness.relayout(), "post-script controlled relayout succeeds");
    expect(!harness.documentDirty(), "controlled relayout clears dirty state");
    expect(harness.layoutRevision() > initialRevision,
        "controlled relayout advances layout revision");
    expect(harness.layoutTextExtent() > beforeMutationExtent &&
        harness.layoutTextExtent() > initialExtent,
        "text mutation changes measurable layout extent");

    result = harness.execute(
        "var alias = status; alias.textContent = \"Changed\";"
        "var aliasResult = status.textContent;"
        "status.textContent = 123; var numberResult = status.textContent;");
    expect(result.succeeded(), "element aliases and primitive conversion succeed");
    expectString(harness, "aliasResult", "Changed", "element alias mutation");
    expectString(harness, "numberResult", "123", "number text conversion");
    expect(gxos::javascript::navigatorScriptElementTextContent(
        harness.document(), status, hostText) && hostText == "123",
        "independent host state follows alias mutation");

    expectError(harness.execute("status.id = \"other\";"),
        RuntimeErrorCode::HostPropertyReadOnly, "id is read-only");
    expectError(harness.execute("status.tagName = \"P\";"),
        RuntimeErrorCode::HostPropertyReadOnly, "tagName is read-only");
    expectError(harness.execute("status.foo = 1;"),
        RuntimeErrorCode::HostPropertyWriteFailed, "unknown host write rejected");
    expectError(harness.execute("var detached = document.getElementById; detached(\"status\");"),
        RuntimeErrorCode::InvalidReceiver, "detached document method rejected");

    const Value oldDocument = *binding(harness, "document");
    const Value oldElement = *binding(harness, "status");
    expect(harness.invalidateDocumentGeneration(error),
        "document generation invalidates");
    Value ignored;
    expect(!harness.runtime().readHostPropertyForTesting(
        oldDocument.hostObjectId(), "getElementById", ignored, error) &&
        error == RuntimeErrorCode::StaleHostObject,
        "old document handle is stale");
    expect(!harness.runtime().readHostPropertyForTesting(
        oldElement.hostObjectId(), "textContent", ignored, error) &&
        error == RuntimeErrorCode::StaleHostObject,
        "old element handle is stale");
    expectError(harness.execute("writer();"),
        RuntimeErrorCode::StaleHostObject, "stale closure through realm");

    expect(harness.replaceHtml("file:///js8-fixture-b.html",
        "<html><body><div id=\"status\">Replacement</div></body></html>", error),
        "replacement document loads");
    result = harness.execute(
        "var replacement = document.getElementById(\"status\");"
        "var replacementResult = replacement.textContent;");
    expect(result.succeeded(), "new document realm executes");
    expectString(harness, "replacementResult", "Replacement",
        "new generation document content");
}

void testSameRealmScripts()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js8-same-realm.html", kFixture, error),
        "same-realm fixture loads");
    expect(harness.execute(
        "var status = document.getElementById(\"status\");"
        "status.textContent = \"First\"; var shared = 10;").succeeded(),
        "same-realm script one succeeds");
    expect(harness.execute(
        "shared += 5; status.textContent = \"Second\";"
        "var result = shared; var same = status === document.getElementById(\"status\");").succeeded(),
        "same-realm script two succeeds");
    expectNumber(harness, "result", 15.0, "same-realm global state");
    expectBoolean(harness, "same", true, "same-realm element identity");
    std::string text;
    expect(gxos::javascript::navigatorScriptElementTextContent(
        harness.document(), statusSerial(harness), text) && text == "Second",
        "same-realm host mutation");
}

void testDocumentLimits()
{
    NavigatorScriptHostLimits hostLimits;
    hostLimits.maxDocumentMutations = 1u;
    hostLimits.maxTextContentAssignment = 4u;
    NavigatorScriptExecutionHarness harness(RuntimeLimits(), hostLimits);
    RuntimeErrorCode error = RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///js8-limits.html", kFixture, error),
        "limit fixture loads");
    expect(harness.execute(
        "var status = document.getElementById(\"status\");"
        "status.textContent = \"One\";").succeeded(),
        "first bounded mutation succeeds");
    expectError(harness.execute("status.textContent = \"Two\";"),
        RuntimeErrorCode::DocumentMutationLimitExceeded,
        "document mutation budget enforced");
    std::string text;
    expect(gxos::javascript::navigatorScriptElementTextContent(
        harness.document(), statusSerial(harness), text) && text == "One",
        "failed mutation leaves prior host state coherent");
    expectError(harness.execute("status.textContent = \"12345\";"),
        RuntimeErrorCode::DocumentTextLimitExceeded,
        "textContent assignment bound enforced");

    RuntimeLimits runtimeLimits;
    runtimeLimits.maxHostOperations = 4u;
    NavigatorScriptExecutionHarness budget(runtimeLimits);
    expect(budget.loadHtml("file:///js8-budget.html", kFixture, error),
        "host budget fixture loads");
    expectError(budget.execute(
        "var status = document.getElementById(\"status\");"
        "var result = status.id;"),
        RuntimeErrorCode::HostOperationBudgetExceeded,
        "DOM host calls remain metered by JS7 budget");
}

} // namespace

int main()
{
    testRealDocumentBridge();
    testSameRealmScripts();
    testDocumentLimits();
    if (failures != 0) {
        std::cerr << failures << " JS8 test failure(s)\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS8 tests PASS\n";
    return 0;
}
