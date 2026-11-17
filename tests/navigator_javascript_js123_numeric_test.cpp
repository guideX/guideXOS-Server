#include "navigator_javascript/json_parser.h"
#include "navigator_javascript/json_number_impl.inl"
#include "navigator_javascript/json_writer.h"

#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace gxos::javascript;

namespace {

struct JsonFixture {
    std::vector<PortableObjectRecord> objectRecords{64u};
    std::vector<PortablePropertyRecord> propertyRecords{256u};
    std::vector<PortableArrayElementRecord> arrayRecords{128u};
    std::vector<char> propertyNames = std::vector<char>(16384u);
    std::vector<RuntimeStringRecord> stringRecords{128u};
    std::vector<char> stringBytes = std::vector<char>(65536u);
    PortableObjectStore objects;
    RuntimeStringStore strings;
    PortableObjectError objectError;
    RuntimeStringError stringError;

    JsonFixture()
    {
        PortableObjectLimits objectLimits;
        objectLimits.maxObjects = objectRecords.size();
        objectLimits.maxProperties = propertyRecords.size();
        objectLimits.maxPropertiesPerObject = 64u;
        objectLimits.maxPropertyNameLength = 256u;
        objectLimits.maxPropertyNameBytes = propertyNames.size();
        objectLimits.maxArrayElements = arrayRecords.size();
        objectLimits.maxTotalArrayElements = arrayRecords.size();
        objectLimits.maxArrayIndex = arrayRecords.size() - 1u;
        objectLimits.maxArrayLength = arrayRecords.size();
        objects.configure(PortableObjectStorageView{
                objectRecords.data(), objectRecords.size(),
                propertyRecords.data(), propertyRecords.size(),
                arrayRecords.data(), arrayRecords.size(), propertyNames.data(),
                propertyNames.size()}, objectLimits, 1u, objectError);
        strings.configure(RuntimeStringStorageView{
                stringRecords.data(), stringRecords.size(), stringBytes.data(),
                stringBytes.size()}, 1u, stringError);
    }

    bool ready() const
    {
        return objectError.code == PortableObjectErrorCode::None &&
            stringError.code == RuntimeStringErrorCode::None;
    }

    bool parse(SourceView input, Value& value, PortableJsonParseError& error,
        PortableJsonParseLimits limits = PortableJsonParseLimits(),
        PortableJsonParseStats* stats = nullptr)
    {
        std::vector<char> decoded(limits.maxStringLength);
        const std::size_t keyLimit = objects.limits().maxPropertyNameLength;
        std::vector<char> keys(limits.maxNestingDepth * keyLimit);
        return parsePortableJson(input, objects, strings, limits,
            decoded.data(), decoded.size(), keys.data(), keys.size(), nullptr,
            nullptr, value, error, stats);
    }
};

struct NumericFixture {
    const char* token;
    std::uint64_t expectedBits;
};

int checks = 0;
int failures = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

std::uint64_t numberBits(double number)
{
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &number, sizeof(bits));
    return bits;
}

bool rejectNumber(SourceView, double&)
{
    return false;
}

struct WriterStepBudget {
    std::size_t remaining = 0u;
};

bool consumeWriterStep(void* context)
{
    WriterStepBudget* budget = static_cast<WriterStepBudget*>(context);
    if (budget == nullptr || budget->remaining == 0u) return false;
    --budget->remaining;
    return true;
}

bool getOwn(JsonFixture& fixture, RuntimeObjectId object, const char* key,
    Value& result)
{
    bool found = false;
    PortableObjectError error;
    return fixture.objects.getOwnProperty(object,
        SourceView(key, std::strlen(key)), result, found, error) && found;
}

void testIndependentlyAuthoredBinary64Fixtures()
{
    static const NumericFixture fixtures[] = {
        {"0", 0x0000000000000000ull},
        {"-0", 0x8000000000000000ull},
        {"1", 0x3ff0000000000000ull},
        {"-1", 0xbff0000000000000ull},
        {"0.1", 0x3fb999999999999aull},
        {"0.5", 0x3fe0000000000000ull},
        {"1.5", 0x3ff8000000000000ull},
        {"9007199254740991", 0x433fffffffffffffull},
        {"9007199254740992", 0x4340000000000000ull},
        {"9007199254740993", 0x4340000000000000ull},
        {"5e-324", 0x0000000000000001ull},
        {"1e-324", 0x0000000000000000ull},
        {"-1e-324", 0x8000000000000000ull},
        {"2.2250738585072014e-308", 0x0010000000000000ull},
        {"1.7976931348623157e308", 0x7fefffffffffffffull},
        {"1.7976931348623158e308", 0x7fefffffffffffffull},
        {"1.7976931348623159e308", 0x7ff0000000000000ull},
        {"1e309", 0x7ff0000000000000ull},
        {"-1e309", 0xfff0000000000000ull},
        {"1e-4000", 0x0000000000000000ull},
        {"-1e-4000", 0x8000000000000000ull},
        {"0.9999999999999999", 0x3fefffffffffffffull},
        {"1.0000000000000002", 0x3ff0000000000001ull},
        {"1.00000000000000011102230246251565404236316680908203125",
            0x3ff0000000000000ull},
        {"-1.00000000000000011102230246251565404236316680908203125",
            0xbff0000000000000ull},
        {"1.00000000000000033306690738754696212708950042724609375",
            0x3ff0000000000002ull},
        {"-1.00000000000000033306690738754696212708950042724609375",
            0xbff0000000000002ull},
        {"-0.999999999999999944488848768742172978818416595458984375",
            0xbff0000000000000ull},
        {"2.4703282292062327e-324", 0x0000000000000000ull},
        {"2.4703282292062328e-324", 0x0000000000000001ull},
        {"18446744073709551617", 0x43f0000000000000ull},
        {"123456789012345678901234567890", 0x45f8ee90ff6c373eull},
    };

    JsonFixture fixture;
    expect(fixture.ready(), "numeric fixture stores initialize");
    PortableJsonParseError error;
    Value value;
    for (const NumericFixture& test : fixtures) {
        const std::size_t length = std::strlen(test.token);
        const bool parsed = fixture.parse(SourceView(test.token, length), value,
            error);
        expect(parsed && value.isNumber() &&
            numberBits(value.numberValue()) == test.expectedBits,
            "JSON.parse number matches the independently authored binary64 bits");
    }
}

void testJsonPositionIntegrationAndRollback()
{
    JsonFixture fixture;
    PortableJsonParseError error;
    PortableJsonParseStats stats;
    const char* json =
        "{\"top\":123,\"items\":[1.5,-2,0.25,{\"nested\":42}],"
        "\"tiny\":5e-324,\"negativeZero\":-0}";
    Value root;
    expect(fixture.parse(SourceView(json, std::strlen(json)), root, error,
            PortableJsonParseLimits(), &stats) && root.isObject(),
        "shared parser converts top-level, object, Array, and nested numbers");
    Value value;
    expect(getOwn(fixture, root.objectId(), "top", value) &&
        value.isNumber() && value.numberValue() == 123.0,
        "top-level object property has a portable Number Value");
    expect(getOwn(fixture, root.objectId(), "items", value) && value.isObject(),
        "nested Array is retained in PortableObjectStore");
    RuntimeObjectId items = value.objectId();
    PortableObjectError objectError;
    std::size_t length = 0u;
    expect(fixture.objects.arrayLength(items, length, objectError) &&
        length == 4u, "numeric Array retains its full logical length");
    bool found = false;
    expect(fixture.objects.getOwnArrayElement(items, 2u, value, found,
            objectError) && found && value.isNumber() &&
        numberBits(value.numberValue()) == 0x3fd0000000000000ull,
        "Array fractional value uses the shared numeric Value representation");
    expect(fixture.objects.getOwnArrayElement(items, 3u, value, found,
            objectError) && found && value.isObject() &&
        getOwn(fixture, value.objectId(), "nested", value) && value.isNumber() &&
        value.numberValue() == 42.0,
        "nested object numeric property converts in place");
    expect(getOwn(fixture, root.objectId(), "tiny", value) && value.isNumber() &&
        numberBits(value.numberValue()) == 1u,
        "nested subnormal remains exact after shared-store construction");
    expect(getOwn(fixture, root.objectId(), "negativeZero", value) &&
        numberBits(value.numberValue()) == 0x8000000000000000ull,
        "nested negative zero retains its sign bit");

    PortableJsonParseLimits failingLimits;
    failingLimits.numberConverter = &rejectNumber;
    const char* failBeforeObject = "5";
    expect(!fixture.parse(SourceView(failBeforeObject, 1u), value, error,
            failingLimits) &&
        error.code == PortableJsonParseErrorCode::InvalidNumber,
        "numeric conversion failure before construction is explicit");

    const std::size_t oldObjects = fixture.objects.objectCount();
    const std::size_t oldStrings = fixture.strings.count();
    const char* failAfterString = "{\"label\":\"kept until failure\",\"n\":5}";
    expect(!fixture.parse(SourceView(failAfterString,
            std::strlen(failAfterString)), value, error, failingLimits) &&
        error.code == PortableJsonParseErrorCode::InvalidNumber &&
        fixture.objects.objectCount() == oldObjects &&
        fixture.strings.count() == oldStrings,
        "numeric conversion failure rolls back an object and earlier string");

    const char* failInsideArray = "[\"earlier\",1]";
    expect(!fixture.parse(SourceView(failInsideArray,
            std::strlen(failInsideArray)), value, error, failingLimits) &&
        error.code == PortableJsonParseErrorCode::InvalidNumber &&
        fixture.objects.objectCount() == oldObjects &&
        fixture.strings.count() == oldStrings,
        "numeric conversion failure inside Array restores parser marks");

    const char* failNearEnd = "{\"first\":\"x\",\"last\":[0]}";
    expect(!fixture.parse(SourceView(failNearEnd, std::strlen(failNearEnd)),
            value, error, failingLimits) &&
        error.code == PortableJsonParseErrorCode::InvalidNumber &&
        fixture.objects.objectCount() == oldObjects &&
        fixture.strings.count() == oldStrings,
        "last nested numeric failure does not publish a partial result");

    const char* trailingText = "{\"n\":1}x";
    expect(!fixture.parse(SourceView(trailingText, std::strlen(trailingText)),
            value, error) &&
        error.code == PortableJsonParseErrorCode::SyntaxError &&
        fixture.objects.objectCount() == oldObjects,
        "completion error after numeric object construction rolls back");

    expect(fixture.parse(SourceView("6", 1u), value, error) &&
        value.isNumber() && value.numberValue() == 6.0,
        "successful parse follows injected conversion failure");
}

void testStrictGrammarLimitsAndLongNumbers()
{
    static const char* invalid[] = {
        "+1", "01", "1.", ".5", "1e", "1e+", "--1", "Infinity",
        "NaN", "0x10"
    };
    JsonFixture fixture;
    PortableJsonParseError error;
    Value value;
    for (const char* token : invalid) {
        const bool parsed = fixture.parse(SourceView(token, std::strlen(token)),
            value, error);
        expect(!parsed && error.code == PortableJsonParseErrorCode::SyntaxError,
            "strict JSON grammar rejects invalid numeric spellings");
    }

    std::string longMantissa("1.0000000000000001");
    longMantissa.append(65536u - longMantissa.size(), '0');
    expect(longMantissa.size() == 65536u &&
        fixture.parse(SourceView(longMantissa.data(), longMantissa.size()),
            value, error) && value.isNumber() &&
        numberBits(value.numberValue()) == 0x3ff0000000000000ull,
        "64 KiB mantissa with rounding-tail digits has bounded exact conversion");

    std::string longPositiveExponent("1e");
    longPositiveExponent.append(65536u - longPositiveExponent.size(), '9');
    expect(fixture.parse(SourceView(longPositiveExponent.data(),
            longPositiveExponent.size()), value, error) && value.isNumber() &&
        numberBits(value.numberValue()) == 0x7ff0000000000000ull,
        "64 KiB positive exponent saturates to positive infinity");

    std::string longNegativeExponent("-1e-");
    longNegativeExponent.append(65536u - longNegativeExponent.size(), '9');
    expect(fixture.parse(SourceView(longNegativeExponent.data(),
            longNegativeExponent.size()), value, error) && value.isNumber() &&
        numberBits(value.numberValue()) == 0x8000000000000000ull,
        "64 KiB negative exponent saturates to negative zero");

    PortableJsonParseLimits stepLimit;
    stepLimit.maxSteps = 2u;
    const std::size_t beforeObjects = fixture.objects.objectCount();
    expect(!fixture.parse(SourceView("123", 3u), value, error, stepLimit) &&
        error.code == PortableJsonParseErrorCode::ExecutionBudgetExceeded &&
        fixture.objects.objectCount() == beforeObjects,
        "parser step exhaustion remains distinct and leaves stores unchanged");
    expect(fixture.parse(SourceView("7", 1u), value, error) &&
        value.isNumber() && value.numberValue() == 7.0,
        "bounded failure is followed by a successful conversion");
}

void testLocaleIndependence()
{
    const char* previous = std::setlocale(LC_NUMERIC, nullptr);
    const std::string savedLocale = previous == nullptr ? "C" : previous;
    const char* changed = std::setlocale(LC_NUMERIC, "German_Germany.1252");
    JsonFixture fixture;
    PortableJsonParseError error;
    Value value;
    const bool parsed = fixture.parse(SourceView("1.5", 3u), value, error);
    std::setlocale(LC_NUMERIC, savedLocale.c_str());
    expect(changed == nullptr || (parsed && value.isNumber() &&
            numberBits(value.numberValue()) == 0x3ff8000000000000ull),
        "conversion remains locale independent when a decimal-comma locale exists");
}

void testBoundedSharedWriter()
{
    const char input[] = {
        'q', '"', '\\', '\n', '\t', '\r', '\0', '\x01', ' ',
        static_cast<char>(0xf0), static_cast<char>(0x9f),
        static_cast<char>(0x98), static_cast<char>(0x83)};
    const std::string expected =
        "\"q\\\"\\\\\\n\\t\\r\\u0000\\u0001 \xf0\x9f\x98\x83\"";
    std::vector<char> outputBuffer(expected.size());
    PortableJsonOutputWriter writer(outputBuffer.data(), outputBuffer.size());
    expect(writer.appendQuotedUtf8(SourceView(input, sizeof(input))) &&
        writer.size() == expected.size() &&
        writer.remaining() == 0u,
        "shared writer escapes controls and preserves valid UTF-8 in bounds");
    SourceView output;
    expect(writer.complete(output) && output.length == expected.size() &&
        std::memcmp(output.data, expected.data(), expected.size()) == 0,
        "completed output view contains the hand-authored escaped string");

    std::vector<char> shortBuffer(expected.size() - 1u);
    writer.reset(shortBuffer.data(), shortBuffer.size());
    expect(!writer.appendQuotedUtf8(SourceView(input, sizeof(input))) &&
        writer.error() == PortableJsonWriteErrorCode::CapacityExceeded &&
        !writer.complete(output) && output.data == nullptr &&
        output.length == 0u,
        "capacity failure keeps partial bytes unavailable as success");

    writer.reset();
    expect(writer.appendSpan("null", 4u) && writer.complete(output) &&
        output.length == 4u && std::memcmp(output.data, "null", 4u) == 0,
        "writer reset reuses caller storage after a failed write");

    const char invalidUtf8[] = {static_cast<char>(0xc0),
        static_cast<char>(0xaf)};
    writer.reset();
    expect(!writer.appendQuotedUtf8(SourceView(invalidUtf8,
            sizeof(invalidUtf8))) &&
        writer.error() == PortableJsonWriteErrorCode::InvalidUtf8 &&
        !writer.complete(output),
        "writer rejects malformed UTF-8 and withholds its partial result");

    writer.reset();
    WriterStepBudget oneStep{1u};
    expect(!writer.appendQuotedUtf8(SourceView("ab", 2u),
            &consumeWriterStep, &oneStep) &&
        writer.error() == PortableJsonWriteErrorCode::StepBudgetExceeded &&
        !writer.complete(output),
        "writer propagates caller step exhaustion without completing output");

    writer.reset();
    expect(writer.appendByte('[') && writer.appendSpan("true", 4u) &&
        writer.appendByte(']') && writer.complete(output) &&
        output.length == 6u && std::memcmp(output.data, "[true]", 6u) == 0,
        "writer supports bounded byte and span appends");
}

} // namespace

int main()
{
    testIndependentlyAuthoredBinary64Fixtures();
    testJsonPositionIntegrationAndRollback();
    testStrictGrammarLimitsAndLongNumbers();
    testLocaleIndependence();
    testBoundedSharedWriter();
    if (failures != 0) {
        std::cerr << "Navigator JavaScript JS123 checks: "
            << (checks - failures) << '/'
            << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "Navigator JavaScript JS123 checks: " << checks << '/' << checks
        << " passed\n";
    return 0;
}
