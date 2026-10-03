#include "guide_web_html_parser.h"
#include "navigator_javascript/navigator_script_host.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;
std::string fixture;

void expect(bool condition, const std::string& label)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << label << "\n";
}

const gxos::web::HtmlElementRef* elementById(
    const gxos::web::WebDocument& document, const std::string& id)
{
    for (const gxos::web::HtmlElementRef& element : document.structuralElements)
        if (element.id == id) return &element;
    return nullptr;
}

const gxos::web::HtmlElementContentMetadata* contentFor(
    const gxos::web::WebDocument& document, std::uint64_t serial)
{
    for (const gxos::web::HtmlElementContentMetadata& content : document.contentMetadata)
        if (content.serial == serial) return &content;
    return nullptr;
}

bool directText(const gxos::web::WebDocument& document, const std::string& id)
{
    const gxos::web::HtmlElementRef* element = elementById(document, id);
    if (!element) return false;
    const gxos::web::HtmlElementContentMetadata* content =
        contentFor(document, element->serial);
    return content && content->hasDirectTextChild;
}

void expectMetadata(const gxos::web::WebDocument& document,
    const std::string& id, bool hasText, std::uint16_t elementChildren,
    const std::string& label)
{
    const gxos::web::HtmlElementRef* element = elementById(document, id);
    expect(element != nullptr, label + ": structural Element exists");
    if (!element) return;
    const gxos::web::HtmlElementContentMetadata* content =
        contentFor(document, element->serial);
    expect(content != nullptr, label + ": document content record exists");
    if (content) {
        expect(content->hasDirectTextChild == hasText,
            label + ": direct text presence");
        expect(content->elementChildCount == elementChildren,
            label + ": accepted structural child count");
        expect(content->hasElementChild == (elementChildren != 0u),
            label + ": Element-child summary agrees with structural insertion");
    }
    expect(element->childCount == elementChildren,
        label + ": structural child count");
}

void testFixtureFacts()
{
    const gxos::web::WebDocument document = gxos::web::parseHtml(
        "file:///javascript-js57.html", fixture);
    expect(!fixture.empty(), "JS57 hosted fixture is available");
    expect(document.structuralElements.size() == document.contentMetadata.size(),
        "one bounded content record exists per represented structural Element");
    expect(document.structuralElements.size() < 1024u,
        "fixture stays below the 1,024 Element bound");

    expectMetadata(document, "empty", false, 0u, "empty Element");
    expectMetadata(document, "text", true, 0u, "text-only Element");
    expectMetadata(document, "space", true, 0u, "space-only Element");
    expectMetadata(document, "tab", true, 0u, "tab entity");
    expectMetadata(document, "cr", true, 0u, "CR entity");
    expectMetadata(document, "lf", true, 0u, "LF entity");
    expectMetadata(document, "raw-tab", true, 0u, "raw tab character data");
    expectMetadata(document, "raw-cr", true, 0u, "raw CR character data");
    expectMetadata(document, "raw-lf", true, 0u, "raw LF character data");
    expectMetadata(document, "mixed-whitespace", true, 0u,
        "mixed ASCII whitespace entities");
    expectMetadata(document, "named-entity", true, 0u, "named entity");
    expectMetadata(document, "decimal-entity", true, 0u,
        "decimal whitespace entity");
    expectMetadata(document, "hex-entity", true, 0u,
        "hexadecimal whitespace entity");
    expectMetadata(document, "element-only", false, 1u,
        "Element-only child");
    expectMetadata(document, "mixed", true, 1u, "mixed text and Element");
    expectMetadata(document, "outer", false, 1u,
        "nested descendant text does not propagate");
    expectMetadata(document, "nested", true, 0u, "nested direct text owner");
    expectMetadata(document, "formatting", true, 1u,
        "formatting whitespace around child");
    expectMetadata(document, "comment", false, 0u,
        "comment bytes including greater-than are not text");
    expectMetadata(document, "comment-around", true, 0u,
        "comment between text runs preserves direct text fact");
    expectMetadata(document, "hidden-text", true, 0u,
        "display-none text remains document content");
    expectMetadata(document, "hidden-child-parent", false, 1u,
        "hidden Element child remains structural content");
    expectMetadata(document, "void-input", false, 0u, "void input");
    expectMetadata(document, "void-img", false, 0u, "void image");
    expect(elementById(document, "void-br") == nullptr,
        "br stays outside the represented structural Element set");
    expectMetadata(document, "void-hr", false, 0u, "void horizontal rule");
    expectMetadata(document, "textarea", true, 0u, "textarea source text");
    expectMetadata(document, "option", true, 0u, "option source text");
    expectMetadata(document, "body", true, 28u,
        "body direct formatting whitespace and Element children");

    const gxos::web::HtmlElementRef* text = elementById(document, "text");
    const gxos::web::HtmlElementRef* textParent = elementById(document, "body");
    const gxos::web::HtmlElementRef* nested = elementById(document, "nested");
    const gxos::web::HtmlElementRef* outer = elementById(document, "outer");
    expect(text && textParent && text->parentSerial == textParent->serial,
        "direct text Elements keep their structural body parent");
    expect(nested && outer && nested->parentSerial == outer->serial,
        "nested text belongs to the nested Element in the structural tree");

    bool textareaValuePreserved = false;
    bool optionValuePreserved = false;
    for (const gxos::web::DocBlock& block : document.blocks) {
        if (block.id == "textarea")
            textareaValuePreserved = block.inputValue == "seed & value" &&
                block.formControl.value == "seed & value";
        if (block.id == "select")
            optionValuePreserved = block.options.size() == 1u &&
                block.options[0].text == "Label" &&
                block.options[0].value == "Label";
    }
    expect(textareaValuePreserved, "textarea default/form projection is unchanged");
    expect(optionValuePreserved, "option text and implicit value are unchanged");
}

void testUnrepresentedRawTextAndDeclarations()
{
    const std::string raw = "<!DOCTYPE html><html><head>"
        "<script>var rawText = 1;</script>"
        "<style>body { color: red; }</style>"
        "</head><body id=body></body></html>";
    const gxos::web::WebDocument document = gxos::web::parseHtml(
        "file:///raw-text.html", raw);
    expect(!directText(document, "body"),
        "unrepresented script/style bodies do not leak onto body");
    expect(document.scriptSources.size() == 1u &&
        document.scriptSources[0] == "var rawText = 1;",
        "script raw-text capture remains intact");
    expect(!document.styleRules.empty(), "style raw-text parsing remains intact");
    expect(document.hasDocumentElement &&
        document.documentElement.childCount == 1u,
        "doctype and metadata declarations do not create Element children");

    const std::string rawWhitespace = "<body>"
        "<div id=single> </div>"
        "<div id=raw-tab>\t</div>"
        "<div id=raw-cr>\r</div>"
        "<div id=raw-lf>\n</div>"
        "<div id=raw-mixed> \t\r\n </div>"
        "</body>";
    const gxos::web::WebDocument whitespaceDocument = gxos::web::parseHtml(
        "file:///raw-whitespace.html", rawWhitespace);
    expectMetadata(whitespaceDocument, "single", true, 0u,
        "single raw ASCII space");
    expectMetadata(whitespaceDocument, "raw-tab", true, 0u,
        "raw tab byte");
    expectMetadata(whitespaceDocument, "raw-cr", true, 0u,
        "raw carriage return byte");
    expectMetadata(whitespaceDocument, "raw-lf", true, 0u,
        "raw line feed byte");
    expectMetadata(whitespaceDocument, "raw-mixed", true, 0u,
        "raw mixed ASCII whitespace run");

    const gxos::web::WebDocument unterminatedComment = gxos::web::parseHtml(
        "file:///comment-eof.html",
        "<body><div id=comment><!-- ignored > through EOF");
    expectMetadata(unterminatedComment, "comment", false, 0u,
        "unterminated comment bytes remain non-character parser input");
}

void testLongTextAndTextRunBound()
{
    std::string longHtml = "<html><body><div id=long>";
    longHtml.append(12000u, 'x');
    longHtml += "</div></body></html>";
    const gxos::web::WebDocument longDocument = gxos::web::parseHtml(
        "file:///long-text.html", longHtml);
    const gxos::web::HtmlElementRef* longElement =
        elementById(longDocument, "long");
    const gxos::web::HtmlElementContentMetadata* longContent = longElement
        ? contentFor(longDocument, longElement->serial) : nullptr;
    expect(longContent && longContent->hasDirectTextChild,
        "long text stays authoritative after render-summary truncation");
    expect(longContent && longContent->visibleTextByteCount == 1024u,
        "legacy visible-text summary remains bounded at 1,024 bytes");

    std::string manyRuns = "<html><body><div id=runs>";
    for (std::size_t index = 0u; index < 1000u; ++index)
        manyRuns += "x<span></span>";
    manyRuns += "</div></body></html>";
    const gxos::web::WebDocument runDocument = gxos::web::parseHtml(
        "file:///many-runs.html", manyRuns);
    expectMetadata(runDocument, "runs", true, 1000u,
        "1,000 alternating text and Element runs");
    expect(runDocument.contentMetadata.size() ==
        runDocument.structuralElements.size(),
        "text run count creates no per-run document metadata records");
}

void testMalformedOwnershipAndDocumentReplacement()
{
    const gxos::web::WebDocument malformed = gxos::web::parseHtml(
        "file:///malformed.html",
        "<body id=body><div id=outer><span id=inner>nested</div>tail</body>");
    expectMetadata(malformed, "outer", false, 1u,
        "malformed close keeps parser-stack direct ownership");
    expectMetadata(malformed, "inner", true, 0u,
        "malformed nested text remains assigned to current Element");
    expect(directText(malformed, "body"),
        "text after recovery close belongs to the parser's body owner");

    const gxos::web::WebDocument first = gxos::web::parseHtml(
        "file:///generation-a.html", "<html><body><div id=target>old</div></body></html>");
    const gxos::web::WebDocument second = gxos::web::parseHtml(
        "file:///generation-b.html", "<html><body><div id=target></div></body></html>");
    const gxos::web::HtmlElementRef* oldTarget = elementById(first, "target");
    const gxos::web::HtmlElementRef* newTarget = elementById(second, "target");
    expect(oldTarget && newTarget && oldTarget->serial == newTarget->serial,
        "document replacement reuses the same structural serial");
    expect(directText(first, "target") && !directText(second, "target"),
        "document replacement clears parser-owned direct-text state");

    bool cyclesStable = true;
    for (std::size_t cycle = 0u; cycle < 300u; ++cycle) {
        const bool hasText = (cycle % 2u) == 0u;
        const std::string html = hasText
            ? "<html><body><div id=target>x</div></body></html>"
            : "<html><body><div id=target></div></body></html>";
        const gxos::web::WebDocument reparsed = gxos::web::parseHtml(
            "file:///reuse.html", html);
        const gxos::web::HtmlElementRef* target = elementById(reparsed, "target");
        cyclesStable = cyclesStable && target != nullptr &&
            directText(reparsed, "target") == hasText &&
            target->serial == newTarget->serial;
    }
    expect(cyclesStable, "300 parse generations reset flags and reuse serials safely");
}

void testStructuralCapacityAtomicity()
{
    std::string html = "<body id=body>";
    for (std::size_t index = 0u; index < 1022u; ++index)
        html += "<span></span>";
    html += "<div id=last-parent><span id=rejected-child></span></div></body>";
    const gxos::web::WebDocument document = gxos::web::parseHtml(
        "file:///capacity.html", html);
    expect(document.structuralElements.size() == 1024u,
        "structural registry remains capped at 1,024 Elements");
    expectMetadata(document, "last-parent", false, 0u,
        "rejected child does not set a false structural-child fact");
    expect(elementById(document, "rejected-child") == nullptr,
        "child rejected at structural capacity is not represented");

    std::string acceptedHtml = "<body><div id=parent>";
    for (std::size_t index = 0u; index < 1000u; ++index)
        acceptedHtml += "<span></span>";
    acceptedHtml += "</div></body>";
    const gxos::web::WebDocument accepted = gxos::web::parseHtml(
        "file:///near-capacity.html", acceptedHtml);
    expectMetadata(accepted, "parent", false, 1000u,
        "near-capacity accepted children remain counted");
}

void testJavaScriptSurfaceRegression()
{
    gxos::javascript::NavigatorScriptExecutionHarness harness;
    gxos::javascript::RuntimeErrorCode error =
        gxos::javascript::RuntimeErrorCode::None;
    expect(harness.loadHtml("file:///javascript-js57.html", fixture, error),
        "JavaScript harness loads the JS57 regression fixture");
    expect(error == gxos::javascript::RuntimeErrorCode::None,
        "JS57 fixture load has no runtime error");
    const std::size_t sourceCount = harness.document().scriptSources.size();
    bool fixtureScriptsRun = sourceCount == 3u;
    for (const std::string& source : harness.document().scriptSources)
        fixtureScriptsRun = fixtureScriptsRun && harness.execute(source).succeeded();
    expect(fixtureScriptsRun,
        "head/body JS57 fixture scripts execute without parser or runtime errors");
    expect(directText(harness.document(), "result"),
        "textContent presentation mutation does not clear parser-owned text presence");
    expect(harness.relayout(), "JS57 regression fixture relayout succeeds");
    const gxos::javascript::ScriptResult selectors = harness.execute(R"JS(
var root = document.querySelector(":root");
var empty = document.querySelector("#empty");
var mixed = document.querySelector("#mixed");
var textOnly = document.querySelector("#text");
var emptyStillUnsupported = document.querySelector(":empty") === null &&
  document.querySelectorAll(":empty").length === 0 &&
  empty.matches(":empty") === false;
var elementTraversalOnly = textOnly.children.length === 0 &&
  textOnly.childElementCount === 0 && mixed.children.length === 1 &&
  mixed.childElementCount === 1 &&
  mixed.firstElementChild === document.querySelector("#mixed-child");
var rootAndRelationsUnchanged = root === document.querySelector("html") &&
  root.matches(":root") &&
  document.querySelector("#nested").closest(":root") === root;
var textareaProjectionUnchanged =
  document.querySelector("#textarea").value === "seed & value";
)JS");
    expect(selectors.succeeded(), "existing selector and form APIs execute");
    auto expectBoolean = [&](const char* name, bool expected) {
        const std::size_t length = std::strlen(name);
        const gxos::javascript::Value* value = harness.runtime().lookup(
            gxos::javascript::SourceView(name, length));
        expect(value && value->isBoolean() &&
            value->booleanValue() == expected,
            std::string("JavaScript regression: ") + name);
    };
    expectBoolean("emptyStillUnsupported", true);
    expectBoolean("elementTraversalOnly", true);
    expectBoolean("rootAndRelationsUnchanged", true);
    expectBoolean("textareaProjectionUnchanged", true);
    const std::uint64_t buttonSerial = [&]() {
        const gxos::web::HtmlElementRef* button =
            elementById(harness.document(), "click");
        return button ? button->serial : 0u;
    }();
    const gxos::javascript::ScriptResult eventSetup = harness.execute(
        "var clickPreserved = false; document.querySelector('#click').addEventListener('click', function (event) { clickPreserved = event.target === document.querySelector('#click'); });");
    expect(eventSetup.succeeded() && buttonSerial != 0u,
        "existing Event handler registration remains available");
    expect(harness.dispatchClick(buttonSerial, error) &&
        error == gxos::javascript::RuntimeErrorCode::None,
        "existing Event dispatch remains available");
    expectBoolean("clickPreserved", true);

    const std::size_t selectorSimple = sizeof(
        gxos::javascript::NavigatorScriptSimpleSelectorDescriptor);
    const std::size_t selectorDescriptor = sizeof(
        gxos::javascript::NavigatorScriptSelectorDescriptor);
    const std::size_t collectionRecord =
        (16u + selectorDescriptor + 7u) / 8u * 8u;
    expect(selectorSimple == 36u && selectorDescriptor == 556u &&
        collectionRecord == 576u && collectionRecord *
            gxos::javascript::kNavigatorScriptMaxSelectorCollections == 73728u,
        "selector descriptor, collection, and registry memory stay unchanged");
}

} // namespace

int main()
{
    std::ifstream input("navigator-smoke/javascript-js57.html", std::ios::binary);
    if (input) fixture.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    testFixtureFacts();
    testUnrepresentedRawTextAndDeclarations();
    testLongTextAndTextRunBound();
    testMalformedOwnershipAndDocumentReplacement();
    testStructuralCapacityAtomicity();
    testJavaScriptSurfaceRegression();
    if (failures != 0) {
        std::cerr << failures << " JS57 test failure(s) across " << checks
            << " checks\n";
        return 1;
    }
    const std::size_t recordBytes =
        sizeof(gxos::web::HtmlElementContentMetadata);
    std::cout << "Navigator JavaScript JS57 checks: " << checks << "/"
        << checks << " passed; structuralElement="
        << sizeof(gxos::web::HtmlElementRef) << " bytes contentRecord="
        << recordBytes << " bytes maxContentRecords=1024 maxContentBytes="
        << recordBytes * 1024u << " bytes; scriptSelectorGrammar=unchanged\n";
    return 0;
}
