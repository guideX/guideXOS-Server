#include "navigator_javascript/navigator_script_host.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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

std::string quoteJavaScript(const std::string& value)
{
    std::string result("\"");
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20u) {
                static const char hex[] = "0123456789abcdef";
                result += "\\u00";
                result.push_back(hex[(character >> 4u) & 0x0fu]);
                result.push_back(hex[character & 0x0fu]);
            } else {
                result.push_back(static_cast<char>(character));
            }
        }
    }
    result.push_back('"');
    return result;
}

ScriptResult run(RuntimeContext& runtime, const std::string& source)
{
    const ScriptResult result = runtime.execute(SourceView(source.data(),
        source.size()));
    if (!result.succeeded()) {
        std::cerr << "JS109 script detail: status="
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
        std::cerr << "JS109 hosted script detail: status="
            << static_cast<unsigned>(result.status) << " runtime="
            << runtimeErrorCodeName(result.runtimeError.code) << " parser="
            << parserErrorCodeName(result.parserError.code) << " lexer="
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

const Value* binding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    return binding(harness.runtime(), name);
}

bool booleanBinding(const RuntimeContext& runtime, const char* name)
{
    const Value* value = binding(runtime, name);
    return value != nullptr && value->isBoolean() && value->booleanValue();
}

bool booleanBinding(const NavigatorScriptExecutionHarness& harness,
    const char* name)
{
    return booleanBinding(harness.runtime(), name);
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
    std::ifstream input("navigator-smoke/javascript-js109.html",
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

std::size_t projectionCount(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    std::size_t count = 0u;
    for (const gxos::web::DocBlock& block : harness.document().blocks) {
        if (block.elementMetadata.serial == serial &&
            block.inlineFlowSerial == serial) ++count;
    }
    return count;
}

std::size_t projectionIndex(const NavigatorScriptExecutionHarness& harness,
    std::uint64_t serial)
{
    for (std::size_t index = 0u;
        index < harness.document().blocks.size(); ++index) {
        const gxos::web::DocBlock& block = harness.document().blocks[index];
        if (block.elementMetadata.serial == serial &&
            block.inlineFlowSerial == serial) return index;
    }
    return harness.document().blocks.size();
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

void testPrimitiveParsingAndNumbers()
{
    struct PrimitiveCase {
        const char* json;
        ValueType type;
        double number;
        bool boolean;
        const char* string;
    };
    const PrimitiveCase cases[] = {
        {"null", ValueType::Null, 0.0, false, ""},
        {"true", ValueType::Boolean, 0.0, true, ""},
        {"false", ValueType::Boolean, 0.0, false, ""},
        {"0", ValueType::Number, 0.0, false, ""},
        {"-0", ValueType::Number, -0.0, false, ""},
        {"-25", ValueType::Number, -25.0, false, ""},
        {"3.14", ValueType::Number, 3.14, false, ""},
        {"-0.25", ValueType::Number, -0.25, false, ""},
        {"1e3", ValueType::Number, 1000.0, false, ""},
        {"1E-3", ValueType::Number, 0.001, false, ""},
        {"-2.5e+4", ValueType::Number, -25000.0, false, ""},
        {"\"hello\"", ValueType::String, 0.0, false, "hello"},
    };
    for (const PrimitiveCase& test : cases) {
        RuntimeContext runtime;
        const std::string source = "JSON.parse(" +
            quoteJavaScript(test.json) + ");";
        const ScriptResult result = run(runtime, source);
        const Value& value = runtime.finalValue();
        bool matched = result.succeeded() && value.type() == test.type;
        if (matched && test.type == ValueType::Boolean)
            matched = value.booleanValue() == test.boolean;
        if (matched && test.type == ValueType::Number)
            matched = value.numberValue() == test.number &&
                (test.number != 0.0 ||
                    std::signbit(value.numberValue()) ==
                        std::signbit(test.number));
        if (matched && test.type == ValueType::String)
            matched = runtime.stringValue(value) == test.string;
        expect(matched, "JSON.parse accepts a strict primitive value");
    }

    RuntimeContext numeric;
    const ScriptResult range = run(numeric,
        "var large = JSON.parse(\"1.7976931348623157e308\");"
        "var overflow = JSON.parse(\"1e400\");"
        "var underflow = JSON.parse(\"1e-9999\");"
        "var subnormal = JSON.parse(\"4.9406564584124654e-324\");"
        "var subnormalText = JSON.stringify(subnormal);"
        "var subnormalAgain = JSON.parse(subnormalText);"
        "var roundedInteger = JSON.parse(\"9007199254740993\");"
        "var overflowText = JSON.stringify(overflow);");
    const Value* overflow = binding(numeric, "overflow");
    const Value* underflow = binding(numeric, "underflow");
    const Value* subnormal = binding(numeric, "subnormal");
    const Value* subnormalAgain = binding(numeric, "subnormalAgain");
    expect(range.succeeded() && numberBinding(numeric, "large",
            1.7976931348623157e308) && overflow != nullptr &&
        overflow->isNumber() && std::isinf(overflow->numberValue()) &&
        underflow != nullptr && underflow->isNumber() &&
        underflow->numberValue() == 0.0 &&
        subnormal != nullptr && subnormal->isNumber() &&
        subnormal->numberValue() == std::numeric_limits<double>::denorm_min() &&
        subnormalAgain != nullptr && subnormalAgain->isNumber() &&
        subnormalAgain->numberValue() == subnormal->numberValue() &&
        stringBinding(numeric, "overflowText", "null"),
        "JSON number conversion handles largest finite, subnormal round trips, overflow, underflow, and negative-zero representation");
    expect(overflow != nullptr && overflow->isNumber() &&
        std::isinf(overflow->numberValue()),
        "overflow is represented as a Number infinity and stringifies as null");
    expect(numberBinding(numeric, "roundedInteger", 9007199254740992.0),
        "JSON integer precision follows the existing binary64 Number representation");
}

void testStructuredParsingAndIntegration()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var data = JSON.parse(\"{\\\"name\\\":\\\"guideXOS\\\","
        "\\\"enabled\\\":true,\\\"values\\\":[2,4,6],"
        "\\\"nested\\\":{\\\"count\\\":3}}\");"
        "var mapped = data.values.map((value, index) => value * 3 + index);"
        "var filtered = mapped.filter(value => value > 10);"
        "var found = mapped.find(value => value === 13);"
        "var reduced = data.values.reduce((sum, value) => sum + value, 0);"
        "var keys = Object.keys(data);"
        "var integrationOk = data.name === \"guideXOS\" && data.enabled &&"
        " data.nested.count === 3 && mapped[0] === 6 && mapped[1] === 13 &&"
        " mapped[2] === 20 && filtered.length === 2 && found === 13 &&"
        " reduced === 12 && keys.length === 4 && keys[0] === \"name\";");
    expect(result.succeeded() && booleanBinding(runtime, "integrationOk"),
        "parsed objects and native Arrays work with property access, Object.keys, map, filter, find, reduce, and arrow callbacks");

    RuntimeContext duplicate;
    const ScriptResult duplicateResult = run(duplicate,
        "var data = JSON.parse(\"{\\\"a\\\":1,\\\"a\\\":2,"
        "\\\"\\\":4,\\\"2\\\":\\\"two\\\","
        "\\\"1\\\":\\\"one\\\",\\\"__proto__\\\":"
        "{\\\"polluted\\\":true}}\");"
        "var keys = Object.keys(data);"
        "var duplicateOk = data.a === 2 && data.hasOwnProperty(\"a\") &&"
        " data.hasOwnProperty(\"__proto__\") &&"
        " data.__proto__.polluted === true && data.polluted === undefined &&"
        " keys.length === 5 && keys[0] === \"1\" && keys[1] === \"2\" &&"
        " keys[2] === \"a\" && keys[3] === \"\" &&"
        " keys[4] === \"__proto__\";");
    expect(duplicateResult.succeeded() &&
        booleanBinding(duplicate, "duplicateOk"),
        "duplicate keys update one own property; integer keys enumerate first and __proto__ remains ordinary data");

    const Value* array = binding(duplicate, "keys");
    expect(array != nullptr && array->isObject(),
        "Object.keys returns a native Array for a parsed object");

    RuntimeContext nestedRoundTrip;
    const ScriptResult roundTrip = run(nestedRoundTrip,
        "var original = JSON.parse(\"{\\\"items\\\":["
        "{\\\"name\\\":\\\"Alpha\\\"},null,true,3.5],"
        "\\\"text\\\":\\\"escaped \\\\u0041\\\"}\");"
        "var encoded = JSON.stringify(original);"
        "var copy = JSON.parse(encoded);"
        "var roundTripOk = copy.items.length === 4 &&"
        " copy.items[0].name === \"Alpha\" && copy.items[1] === null &&"
        " copy.items[2] === true && copy.items[3] === 3.5 &&"
        " copy.text === \"escaped A\";");
    expect(roundTrip.succeeded() && booleanBinding(nestedRoundTrip,
            "roundTripOk"),
        "nested object/Array parse-stringify-parse round trip preserves supported values");
    expect(stringBinding(nestedRoundTrip, "encoded",
            "{\"items\":[{\"name\":\"Alpha\"},null,true,3.5],\"text\":\"escaped A\"}"),
        "stringify emits valid compact JSON in property insertion order");
}

void testStrictGrammarAndUnicode()
{
    const char* invalid[] = {
        "{foo:1}", "{'foo':1}", "{\"a\":1,}", "[1,2,]", "[1,,2]",
        "undefined", "NaN", "Infinity", "+1", "01", "1.", ".5",
        "0x10", "{ \"a\": 1", "[1,2", "true false",
        "{\"a\" 1}", "[1 2]", "/*comment*/1", "1e", "1e+",
        "--1", "\"\\x00\"", "\"\\u12G4\"", "\"\\uD800\"",
        "\"\\uDC00\"", "\"\\uD800\\u0041\"",
    };
    for (const char* json : invalid) {
        const bool rejected = runRuntimeError("JSON.parse(" +
            quoteJavaScript(json) + ");", RuntimeErrorCode::SyntaxError);
        expect(rejected, "strict JSON grammar rejects invalid or incomplete syntax");
    }

    const std::string rawControl = std::string("\"before\n") + "after\"";
    expect(runRuntimeError("JSON.parse(" + quoteJavaScript(rawControl) + ");",
            RuntimeErrorCode::SyntaxError),
        "raw control characters inside JSON strings are rejected");

    const std::string unicodeJson =
        R"json("quote: \" slash: \/ backslash: \\ controls:\b\f\n\r\t bmp:\u00e9 pair:\uD83D\uDE00")json";
    RuntimeContext unicode;
    const ScriptResult unicodeResult = run(unicode,
        "var value = JSON.parse(" + quoteJavaScript(unicodeJson) + ");"
        "var encoded = JSON.stringify(value);");
    const std::string expectedEncoded =
        R"json("quote: \" slash: / backslash: \\ controls:\b\f\n\r\t bmp:é pair:😀")json";
    expect(unicodeResult.succeeded() &&
        stringBinding(unicode, "encoded", expectedEncoded),
        "JSON string decoding combines surrogate pairs, validates UTF-8, and preserves BMP and multibyte text");

    RuntimeContext escapedControls;
    const std::string controls("\b\f\n\r\t\0", 6u);
    const std::string controlsJson = "\"\\b\\f\\n\\r\\t\\u0000\"";
    const ScriptResult controlsResult = run(escapedControls,
        "var value = JSON.parse(" + quoteJavaScript(controlsJson) + ");"
        "var encoded = JSON.stringify(value);");
    expect(controlsResult.succeeded() &&
        stringBinding(escapedControls, "value", controls) &&
        stringBinding(escapedControls, "encoded",
            "\"\\b\\f\\n\\r\\t\\u0000\""),
        "escaped JSON controls decode and serialize without raw control bytes");
}

void testDepthArraysAndAllocationRollback()
{
    std::string depth32(32u, '[');
    depth32 += '0';
    depth32.append(32u, ']');
    RuntimeContext depthOk;
    const ScriptResult depthResult = run(depthOk,
        "var value = JSON.parse(" + quoteJavaScript(depth32) + ");");
    expect(depthResult.succeeded(),
        "JSON parser accepts the configured maximum 32-container nesting depth");
    std::string depth33(33u, '[');
    depth33 += '0';
    depth33.append(33u, ']');
    expect(runRuntimeError("JSON.parse(" + quoteJavaScript(depth33) + ");",
            RuntimeErrorCode::JsonNestingLimitExceeded),
        "JSON parser rejects nesting beyond the configured limit");

    std::string fullArray = "[";
    for (std::size_t index = 0u; index < 1024u; ++index) {
        if (index != 0u) fullArray.push_back(',');
        fullArray.push_back('0');
    }
    fullArray.push_back(']');
    RuntimeContext arrayLimit;
    const ScriptResult arrayResult = run(arrayLimit,
        "var values = JSON.parse(" + quoteJavaScript(fullArray) + ");"
        "var length = values.length; var edge = values[1023];");
    expect(arrayResult.succeeded() && numberBinding(arrayLimit, "length",
            1024.0) && numberBinding(arrayLimit, "edge", 0.0),
        "JSON parser constructs a dense native Array at the existing 1024-element limit");

    RuntimeLimits properties;
    RuntimeContext propertyBaseline;
    properties.maxTotalProperties = propertyBaseline.propertyCount() + 1u;
    RuntimeContext propertyLimited(properties);
    const ScriptResult setup = run(propertyLimited,
        "var text = \"{\\\"a\\\":1,\\\"b\\\":2}\";");
    const std::size_t objectsBefore = propertyLimited.objectCount();
    const std::size_t propertiesBefore = propertyLimited.propertyCount();
    const std::size_t stringsBefore =
        propertyLimited.runtimeStringValueCount();
    const std::string parseText = "JSON.parse(text);";
    const ScriptResult propertyFailure = propertyLimited.executeInSameRealm(
        SourceView(parseText.data(), parseText.size()));
    expect(setup.succeeded() && !propertyFailure.succeeded() &&
        propertyFailure.runtimeError.code == RuntimeErrorCode::PropertyLimitExceeded &&
        propertyLimited.objectCount() == objectsBefore &&
        propertyLimited.propertyCount() == propertiesBefore &&
        propertyLimited.runtimeStringValueCount() == stringsBefore,
        "property-pool exhaustion rolls back all unpublished parse objects and strings");

    RuntimeLimits arrays;
    arrays.maxArrayElements = 2u;
    RuntimeContext arrayLimited(arrays);
    expect(runRuntimeError("JSON.parse(\"[1,2,3]\");",
            RuntimeErrorCode::ArrayLimitExceeded, arrays),
        "JSON parser enforces the existing per-Array element capacity");

    RuntimeLimits stringCapacity;
    stringCapacity.maxRuntimeStringValues = 1u;
    RuntimeContext strings(stringCapacity);
    const ScriptResult stringSetup = run(strings,
        "var text = \"[\\\"inside\\\"]\";");
    const std::size_t stringsBeforeFailure = strings.runtimeStringValueCount();
    const std::size_t objectsBeforeFailure = strings.objectCount();
    const ScriptResult stringFailure = strings.executeInSameRealm(
        SourceView(parseText.data(), parseText.size()));
    expect(stringSetup.succeeded() && !stringFailure.succeeded() &&
        stringFailure.runtimeError.code == RuntimeErrorCode::StringLimitExceeded &&
        strings.runtimeStringValueCount() == stringsBeforeFailure &&
        strings.objectCount() == objectsBeforeFailure,
        "runtime string-capacity exhaustion leaves no partially published JSON value");

    RuntimeLimits depthLimit;
    depthLimit.maxJsonNestingDepth = 1u;
    expect(runRuntimeError("JSON.parse(\"[[0]]\");",
            RuntimeErrorCode::JsonNestingLimitExceeded, depthLimit),
        "caller-configured JSON nesting limit is honored");
}

void testStringificationSemantics()
{
    RuntimeContext runtime;
    const ScriptResult result = run(runtime,
        "var numberText = JSON.stringify(10);"
        "var stringText = JSON.stringify(\"hello\");"
        "var trueText = JSON.stringify(true);"
        "var falseText = JSON.stringify(false);"
        "var nullText = JSON.stringify(null);"
        "var negativeZeroText = JSON.stringify(-0);"
        "var infinityText = JSON.stringify(JSON.parse(\"1e400\"));"
        "var nested = JSON.stringify({outer:{values:[1,null,true]}});");
    expect(result.succeeded() &&
        stringBinding(runtime, "numberText", "10") &&
        stringBinding(runtime, "stringText", "\"hello\"") &&
        stringBinding(runtime, "trueText", "true") &&
        stringBinding(runtime, "falseText", "false") &&
        stringBinding(runtime, "nullText", "null") &&
        stringBinding(runtime, "negativeZeroText", "0") &&
        stringBinding(runtime, "infinityText", "null") &&
        stringBinding(runtime, "nested",
            "{\"outer\":{\"values\":[1,null,true]}}"),
        "JSON.stringify handles primitive values, nested structures, negative zero, and non-finite numbers");

    RuntimeContext unsupportedValues;
    const ScriptResult unsupportedResult = run(unsupportedValues,
        "function callable() { return 1; }"
        "var undefinedText = JSON.stringify(undefined);"
        "var functionText = JSON.stringify(callable);"
        "var omit = JSON.stringify({a:undefined,b:function(){},c:null});");
    const Value* undefinedText = binding(unsupportedValues, "undefinedText");
    const Value* functionText = binding(unsupportedValues, "functionText");
    expect(unsupportedResult.succeeded() && undefinedText != nullptr &&
        undefinedText->isUndefined() && functionText != nullptr &&
        functionText->isUndefined() &&
        stringBinding(unsupportedValues, "omit", "{\"c\":null}"),
        "top-level undefined/functions return undefined and object properties are omitted");

    RuntimeContext sparseRuntime;
    const ScriptResult sparseResult = run(sparseRuntime,
        "function callable() { return 1; }"
        "var sparse = []; sparse.length = 4; sparse[2] = undefined;"
        "sparse[3] = callable; var sparseText = JSON.stringify(sparse);");
    expect(sparseResult.succeeded() && stringBinding(sparseRuntime,
            "sparseText", "[null,null,null,null]"),
        "sparse and undefined/function Array positions serialize as null");

    RuntimeContext order;
    const ScriptResult orderResult = run(order,
        "var obj = {\"10\":10, \"2\":2, alpha:1, beta:2};"
        "var ordered = JSON.stringify(obj);");
    expect(orderResult.succeeded() && stringBinding(order, "ordered",
            "{\"2\":2,\"10\":10,\"alpha\":1,\"beta\":2}"),
        "JSON.stringify sorts integer-index keys and retains other own-key insertion order");

    RuntimeContext cycles;
    const ScriptResult shared = run(cycles,
        "var shared = {value:1}; var data = {left:shared,right:shared};"
        "var sharedText = JSON.stringify(data);");
    expect(shared.succeeded() && stringBinding(cycles, "sharedText",
            "{\"left\":{\"value\":1},\"right\":{\"value\":1}}"),
        "repeated shared references serialize because they are not active-path cycles");
    expect(runRuntimeError("var cycle = {}; cycle.self = cycle;"
            "JSON.stringify(cycle);", RuntimeErrorCode::TypeError),
        "object self-cycle fails with TypeError-equivalent error");
    expect(runRuntimeError("var cycle = []; cycle[0] = cycle;"
            "JSON.stringify(cycle);", RuntimeErrorCode::TypeError),
        "Array self-cycle fails with TypeError-equivalent error");
    expect(runRuntimeError("var left = {}; var right = {};"
            "left.next = right; right.next = left; JSON.stringify(left);",
            RuntimeErrorCode::TypeError),
        "mutual object cycle fails within the active traversal path");
    expect(runRuntimeError("var array = []; var object = {array:array};"
            "array[0] = object; JSON.stringify(object);",
            RuntimeErrorCode::TypeError),
        "object-Array cycle fails without unbounded recursion");

    RuntimeContext toJson;
    const ScriptResult conversion = run(toJson,
        "var item = {value:3,toJSON:function(key){"
        "return {kind:key,value:this.value};}};"
        "var rootText = JSON.stringify(item);"
        "var nestedText = JSON.stringify({child:item});"
        "function makeValue(){var captured=\"held\";"
        "return {toJSON:function(key){return captured + key;}};}"
        "var closureText = JSON.stringify(makeValue());");
    expect(conversion.succeeded() &&
        stringBinding(toJson, "rootText",
            "{\"kind\":\"\",\"value\":3}") &&
        stringBinding(toJson, "nestedText",
            "{\"child\":{\"kind\":\"child\",\"value\":3}}") &&
        stringBinding(toJson, "closureText", "\"held\""),
        "JSON.stringify invokes callable toJSON with the property key and receiver through native function frames");

    RuntimeContext parseReviver;
    const ScriptResult parseReviverResult = run(parseReviver,
        "var parsed = JSON.parse(\"1\", function(key,value){return value;});");
    expect(parseReviverResult.succeeded() &&
        numberBinding(parseReviver, "parsed", 1.0),
        "JSON.parse accepts the JS110 two-argument reviver while preserving the JS109 primitive result");
    expect(runRuntimeError("JSON.stringify(1, function(key,value){return value;});",
            RuntimeErrorCode::UnsupportedFeature),
        "JSON.stringify explicitly rejects replacer arguments");
    expect(runRuntimeError("JSON.stringify(1, null, 2);",
            RuntimeErrorCode::UnsupportedFeature),
        "JSON.stringify explicitly rejects pretty-print spacing");
    expect(runRuntimeError("JSON.parse({});", RuntimeErrorCode::TypeError),
        "JSON.parse requires a bounded runtime String input");
    expect(runRuntimeError("JSON.parse();", RuntimeErrorCode::TypeError),
        "JSON.parse without text returns a deterministic TypeError");
    expect(runRuntimeError("var bad = {toJSON:function(){return missing;}};"
            "JSON.stringify(bad);", RuntimeErrorCode::UnknownIdentifier),
        "toJSON runtime failures propagate through stringify");

    RuntimeLimits outputLimit;
    outputLimit.maxRuntimeStringLength = 4u;
    expect(runRuntimeError("JSON.stringify(JSON.parse(\"1e-7\"));",
            RuntimeErrorCode::StringLimitExceeded, outputLimit),
        "JSON.stringify rejects output beyond the runtime String limit without returning partial text");
}

void testFailureBoundariesAndBudgets()
{
    expect(runRuntimeError("JSON.parse(\"12345\");",
            RuntimeErrorCode::StringLimitExceeded, [] {
                RuntimeLimits limits;
                limits.maxRuntimeStringLength = 4u;
                return limits;
            }()),
        "JSON input cannot exceed the existing runtime string length limit");

    RuntimeLimits stepLimit;
    stepLimit.maxExecutionSteps = 32u;
    const std::string longArray = "[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]";
    expect(runRuntimeError("JSON.parse(" + quoteJavaScript(longArray) + ");",
            RuntimeErrorCode::ExecutionBudgetExceeded, stepLimit),
        "JSON parsing consumes the shared runtime execution budget");

    std::string oversizedObject = "{";
    for (std::size_t index = 0u; index < 257u; ++index) {
        if (index != 0u) oversizedObject.push_back(',');
        oversizedObject += "\"k" + std::to_string(index) + "\":0";
    }
    oversizedObject.push_back('}');
    expect(runRuntimeError("JSON.parse(" + quoteJavaScript(oversizedObject) + ");",
            RuntimeErrorCode::PropertyLimitExceeded),
        "JSON objects honor the existing per-object property bound");

    RuntimeContext bindingShadow;
    const ScriptResult shadow = run(bindingShadow,
        "var JSON = {parse:function(text){return 7;}};"
        "var shadowResult = JSON.parse(\"null\");");
    expect(shadow.succeeded() && numberBinding(bindingShadow,
            "shadowResult", 7.0),
        "the virtual JSON global follows existing lexical/global shadowing");
}

void testHostedJsonToDom()
{
    NavigatorScriptExecutionHarness harness;
    RuntimeErrorCode loadError = RuntimeErrorCode::None;
    const std::string html = readFixture();
    const bool loaded = !html.empty() && harness.loadHtml(
        "file:///javascript-js109.html", html, loadError) &&
        loadError == RuntimeErrorCode::None &&
        harness.document().scriptSources.size() == 1u;
    expect(loaded,
        "hosted JS109 page fixture parses into one Navigator script");
    if (!loaded) return;

    const std::uint64_t initialRevision = harness.layoutRevision();
    const ScriptResult result = run(harness,
        harness.document().scriptSources[0]);
    expect(result.succeeded(),
        "hosted JS109 script parses JSON and runs native Array callbacks");
    const bool dirty = harness.documentDirty();
    const bool settled = !dirty || harness.relayout();
    expect(settled && harness.layoutRevision() > initialRevision,
        "JSON-driven paragraph insertions update and settle Navigator layout");
    expect(booleanBinding(harness, "hostedCallbacksOk") &&
        booleanBinding(harness, "hostedSelectorsOk"),
        "hosted parsed data passes map/filter/find/reduce and selector-order checks");

    const std::uint64_t first = serialById(harness, "item-0");
    const std::uint64_t second = serialById(harness, "item-1");
    const std::uint64_t third = serialById(harness, "item-2");
    const std::uint64_t serialized = serialById(harness, "serialized-json");
    expect(first != 0u && second != 0u && third != 0u && serialized != 0u &&
        projectionCount(harness, first) == 1u &&
        projectionCount(harness, second) == 1u &&
        projectionCount(harness, third) == 1u &&
        projectionCount(harness, serialized) == 1u &&
        projectionIndex(harness, first) < projectionIndex(harness, second) &&
        projectionIndex(harness, second) < projectionIndex(harness, third) &&
        projectionIndex(harness, third) < projectionIndex(harness, serialized),
        "hosted output appears once in selector and rendered DOM order");
    const std::string serializedText = renderedText(harness, serialized);
    if (serializedText !=
        "{\"items\":[\"Alpha\",\"Beta\",\"Gamma\"],"
        "\"meta\":{\"count\":3}}") {
        std::cerr << "  JS109 serialized DOM actual: [" << serializedText
            << "]\n";
    }
    const std::string firstText = renderedText(harness, first);
    const std::string secondText = renderedText(harness, second);
    const std::string thirdText = renderedText(harness, third);
    if (firstText != "Alpha" || secondText != "Beta" ||
        thirdText != "Gamma") {
        std::cerr << "  JS109 paragraph text actual: [" << firstText << "] ["
            << secondText << "] [" << thirdText << "]\n";
    }
    expect(firstText == "Alpha" && secondText == "Beta" &&
        thirdText == "Gamma" &&
        serializedText ==
            "{\"items\":[\"Alpha\",\"Beta\",\"Gamma\"],"
            "\"meta\":{\"count\":3}}",
        "hosted paragraphs show parsed values and compact serialized JSON");

    const ScriptResult repeated = run(harness,
        "var repeatedData = JSON.parse(JSON.stringify(data));"
        "var repeatedOk = repeatedData.items.length === 3 &&"
        " repeatedData.items[2] === \"Gamma\";");
    expect(repeated.succeeded() && booleanBinding(harness, "repeatedOk"),
        "repeated same-realm parse/stringify stays within existing realm budgets");

    const std::string hostSource = "JSON.stringify(document);";
    const ScriptResult hostValue = harness.execute(SourceView(
        hostSource.data(), hostSource.size()));
    expect(!hostValue.succeeded() &&
        hostValue.runtimeError.code == RuntimeErrorCode::TypeError,
        "unsupported DOM host references fail instead of exposing host state");
}

} // namespace

int main()
{
    testPrimitiveParsingAndNumbers();
    testStructuredParsingAndIntegration();
    testStrictGrammarAndUnicode();
    testDepthArraysAndAllocationRollback();
    testStringificationSemantics();
    testFailureBoundariesAndBudgets();
    testHostedJsonToDom();

    std::cout << "JS109 host records: std::string=" << sizeof(std::string)
        << " B RuntimeObject="
        << NavigatorScriptRuntimeTestAccess::runtimeObjectSize()
        << " B RuntimeProperty="
        << NavigatorScriptRuntimeTestAccess::runtimePropertySize()
        << " B RuntimeArrayElement="
        << NavigatorScriptRuntimeTestAccess::arrayElementSize()
        << " B FunctionRecord="
        << NavigatorScriptRuntimeTestAccess::functionRecordSize() << " B\n";
    if (failures != 0u) {
        std::cerr << "Navigator JavaScript JS109 checks: " << checks - failures
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS109 checks: " << checks << '/'
        << checks << " passed\n";
    return 0;
}
