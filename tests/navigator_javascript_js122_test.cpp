#include "navigator_javascript/json_parser.h"
#include "navigator_javascript/json_number_host.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace gxos::javascript;

namespace {

struct JsonFixture {
    std::vector<PortableObjectRecord> objectRecords;
    std::vector<PortablePropertyRecord> propertyRecords;
    std::vector<PortableArrayElementRecord> arrayRecords;
    std::vector<char> propertyNames;
    std::vector<RuntimeStringRecord> stringRecords;
    std::vector<char> stringBytes;
    PortableObjectStore objects;
    RuntimeStringStore strings;
    PortableObjectError objectError;
    RuntimeStringError stringError;

    explicit JsonFixture(std::size_t objectCapacity = 64u,
        std::size_t propertyCapacity = 256u,
        std::size_t arrayCapacity = 128u,
        std::size_t stringCapacity = 128u,
        std::size_t nameCapacity = 16384u,
        std::size_t stringByteCapacity = 65536u)
        : objectRecords(objectCapacity), propertyRecords(propertyCapacity),
          arrayRecords(arrayCapacity), propertyNames(nameCapacity),
          stringRecords(stringCapacity), stringBytes(stringByteCapacity)
    {
        PortableObjectLimits objectLimits;
        objectLimits.maxObjects = objectCapacity;
        objectLimits.maxProperties = propertyCapacity;
        objectLimits.maxPropertiesPerObject = 64u;
        objectLimits.maxPropertyNameLength = 256u;
        objectLimits.maxPropertyNameBytes = nameCapacity;
        objectLimits.maxArrayElements = arrayCapacity;
        objectLimits.maxTotalArrayElements = arrayCapacity;
        objectLimits.maxArrayIndex = arrayCapacity == 0u ? 0u
            : arrayCapacity - 1u;
        objectLimits.maxArrayLength = arrayCapacity;
        objects.configure(PortableObjectStorageView{
                objectRecords.data(), objectRecords.size(),
                propertyRecords.data(), propertyRecords.size(),
                arrayRecords.data(), arrayRecords.size(),
                propertyNames.data(), propertyNames.size()}, objectLimits,
            1u, objectError);
        strings.configure(RuntimeStringStorageView{
                stringRecords.data(), stringRecords.size(),
                stringBytes.data(), stringBytes.size()}, 1u, stringError);
    }

    bool ready() const
    {
        return objectError.code == PortableObjectErrorCode::None &&
            stringError.code == RuntimeStringErrorCode::None;
    }

    bool parse(SourceView input, Value& value, PortableJsonParseError& error,
        PortableJsonParseLimits limits = PortableJsonParseLimits(),
        PortableJsonParseStats* stats = nullptr,
        bool hostedNumberConverter = true)
    {
        if (hostedNumberConverter && limits.numberConverter == nullptr)
            limits.numberConverter = &parseHostedJsonBinary64;
        std::vector<char> decoded(limits.maxStringLength);
        const std::size_t keyLimit = objects.limits().maxPropertyNameLength;
        std::vector<char> keys(limits.maxNestingDepth * keyLimit);
        return parsePortableJson(input, objects, strings, limits,
            decoded.data(), decoded.size(), keys.data(), keys.size(), nullptr,
            nullptr, value, error, stats);
    }
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

bool sameText(SourceView value, const char* expected, std::size_t length)
{
    return value.length == length &&
        (length == 0u || (value.data != nullptr &&
            std::memcmp(value.data, expected, length) == 0));
}

bool own(JsonFixture& fixture, RuntimeObjectId object, const char* key,
    Value& result)
{
    bool found = false;
    PortableObjectError error;
    return fixture.objects.getOwnProperty(object,
        SourceView(key, std::strlen(key)), result, found, error) && found;
}

void testPrimitivesAndNumbers()
{
    JsonFixture fixture;
    PortableJsonParseError error;
    Value value;
    expect(fixture.ready(), "portable parser storage initializes");
    expect(fixture.parse(SourceView("-0", 2u), value, error) &&
        value.isNumber() && value.numberValue() == 0.0 &&
        std::signbit(value.numberValue()),
        "negative zero retains its binary64 sign");
    expect(fixture.parse(SourceView("true", 4u), value, error) &&
        value.isBoolean() && value.booleanValue(),
        "Boolean primitive parses into a shared Value");
    expect(fixture.parse(SourceView("null", 4u), value, error) &&
        value.isNull(), "null primitive parses into a shared Value");
    expect(fixture.parse(SourceView("-12.5e+2", 8u), value, error) &&
        value.isNumber() && value.numberValue() == -1250.0,
        "decimal and exponent syntax use the shared binary64 converter");
    expect(fixture.parse(SourceView("1e400", 5u), value, error) &&
        std::isinf(value.numberValue()), "overflow maps to binary64 infinity");
    expect(fixture.parse(SourceView("1e-9999", 7u), value, error) &&
        value.numberValue() == 0.0, "underflow maps to positive zero");
    expect(fixture.parse(SourceView("4.9406564584124654e-324", 23u), value,
        error) && value.numberValue() ==
            std::numeric_limits<double>::denorm_min(),
        "minimum subnormal JSON number is preserved");
}

void testObjectsArraysUnicodeAndOrdering()
{
    JsonFixture fixture;
    PortableJsonParseError error;
    PortableJsonParseStats stats;
    std::string input =
        "{\"__proto__\":{\"x\":1},\"2\":\"two\",\"1\":\"one\","
        "\"label\":\"\\uD83D\\uDE03\",\"empty\":\"\","
        "\"dup\":1,\"dup\":2,\"arr\":[true,null,-0,1e400]}";
    Value rootValue;
    expect(fixture.parse(SourceView(input.data(), input.size()), rootValue,
        error, PortableJsonParseLimits(), &stats) && rootValue.isObject(),
        "nested JSON creates objects and Arrays in PortableObjectStore");
    expect(stats.objectsCreated == 3u && stats.arrayElementsCreated == 4u &&
        stats.stringsCreated == 4u,
        "parser reports bounded object, Array, and string construction");

    Value value;
    expect(own(fixture, rootValue.objectId(), "dup", value) &&
        value.isNumber() && value.numberValue() == 2.0,
        "duplicate object keys update the original own property");
    bool hasProto = false;
    PortableObjectError storeError;
    const bool protoQuery = fixture.objects.hasOwnProperty(rootValue.objectId(),
        SourceView("__proto__", 9u), hasProto, storeError);
    RuntimeObjectId prototype = kInvalidRuntimeObjectId;
    const bool prototypeRead = fixture.objects.prototypeOf(rootValue.objectId(),
        prototype, storeError);
    expect(protoQuery && hasProto && prototypeRead &&
        prototype == kInvalidRuntimeObjectId,
        "__proto__ remains an own field and does not mutate the prototype");

    PortablePropertyEntry entry;
    bool found = false;
    const char* orderedKeys[] = {"1", "2", "__proto__", "label", "empty",
        "dup", "arr"};
    for (std::size_t index = 0u; index < 7u; ++index) {
        expect(fixture.objects.getOwnPropertyAt(rootValue.objectId(), index,
                entry, found, storeError) && found &&
            sameText(entry.key, orderedKeys[index],
                std::strlen(orderedKeys[index])),
            "object enumeration orders canonical indices before insertion strings");
    }

    expect(own(fixture, rootValue.objectId(), "label", value) &&
        value.isString(), "parsed Unicode string is retained in shared storage");
    SourceView label;
    expect(fixture.strings.view(value.stringId(), label,
        fixture.stringError) && sameText(label, "\xf0\x9f\x98\x83", 4u),
        "valid surrogate pair decodes to its UTF-8 scalar");
    expect(own(fixture, rootValue.objectId(), "empty", value) &&
        value.isString() && fixture.strings.view(value.stringId(), label,
            fixture.stringError) && label.length == 0u,
        "empty JSON strings retain their zero-length identity");

    expect(own(fixture, rootValue.objectId(), "arr", value) &&
        value.isObject(), "parsed Array is a shared object handle");
    std::size_t length = 0u;
    expect(fixture.objects.arrayLength(value.objectId(), length, storeError) &&
        length == 4u, "parsed Array length matches its element count");
    Value first;
    Value second;
    Value third;
    Value fourth;
    bool elementFound = false;
    expect(fixture.objects.getOwnArrayElement(value.objectId(), 0u, first,
        elementFound, storeError) && elementFound && first.isBoolean() &&
        first.booleanValue(), "Array boolean element uses shared indexed storage");
    expect(fixture.objects.getOwnArrayElement(value.objectId(), 1u, second,
        elementFound, storeError) && elementFound && second.isNull(),
        "Array null element uses shared indexed storage");
    expect(fixture.objects.getOwnArrayElement(value.objectId(), 2u, third,
        elementFound, storeError) && elementFound && third.isNumber() &&
        std::signbit(third.numberValue()), "Array retains negative zero");
    expect(fixture.objects.getOwnArrayElement(value.objectId(), 3u, fourth,
        elementFound, storeError) && elementFound && fourth.isNumber() &&
        std::isinf(fourth.numberValue()),
        "Array retains overflowed binary64 number semantics");

    std::fill(input.begin(), input.end(), '#');
    expect(own(fixture, rootValue.objectId(), "label", value) &&
        fixture.strings.view(value.stringId(), label, fixture.stringError) &&
        sameText(label, "\xf0\x9f\x98\x83", 4u),
        "parsed string lifetime is independent of the caller input buffer");
    expect(fixture.objects.hasOwnProperty(rootValue.objectId(),
        SourceView("label", 5u), hasProto, storeError) && hasProto,
        "parsed property names are copied into bounded object-store storage");
}

void testStrictSyntaxAndRollback()
{
    JsonFixture fixture;
    PortableJsonParseError error;
    Value baseline;
    expect(fixture.parse(SourceView("{\"keep\":\"ok\"}", 13u),
        baseline, error), "rollback fixture has a published baseline value");
    const std::size_t objectsBefore = fixture.objects.objectCount();
    const std::size_t propertiesBefore = fixture.objects.propertyCount();
    const std::size_t elementsBefore = fixture.objects.arrayElementCount();
    const std::size_t stringsBefore = fixture.strings.count();
    const std::size_t namesBefore = fixture.objects.propertyNameBytesUsed();

    const char* invalid[] = {"[1,]", "{\"x\":1,}", "/*comment*/1", "01",
        "+1", "-", "1.", "1e", "\"bad\\q\"", "true false", "["};
    for (const char* text : invalid) {
        Value rejected;
        const bool parsed = fixture.parse(SourceView(text, std::strlen(text)),
            rejected, error);
        expect(!parsed && rejected.isUndefined() &&
            error.code == PortableJsonParseErrorCode::SyntaxError,
            "strict JSON rejects malformed grammar without publishing a root");
        expect(fixture.objects.objectCount() == objectsBefore &&
            fixture.objects.propertyCount() == propertiesBefore &&
            fixture.objects.arrayElementCount() == elementsBefore &&
            fixture.strings.count() == stringsBefore &&
            fixture.objects.propertyNameBytesUsed() == namesBefore,
            "syntax failure rolls back only unpublished JSON allocations");
    }
    Value retained;
    expect(own(fixture, baseline.objectId(), "keep", retained) &&
        retained.isString(), "rollback leaves earlier live values readable");

    const char highSurrogate[] = {'"', '\\', 'u', 'D', '8', '0', '0', '"'};
    Value rejected;
    expect(!fixture.parse(SourceView(highSurrogate, sizeof(highSurrogate)),
        rejected, error) &&
        error.code == PortableJsonParseErrorCode::InvalidUnicode &&
        rejected.isUndefined(), "unpaired high surrogate is rejected");
    const char lowSurrogate[] = {'"', '\\', 'u', 'D', 'C', '0', '0', '"'};
    expect(!fixture.parse(SourceView(lowSurrogate, sizeof(lowSurrogate)),
        rejected, error) &&
        error.code == PortableJsonParseErrorCode::InvalidUnicode,
        "unpaired low surrogate is rejected");
    const char invalidUtf8[] = {'"', static_cast<char>(0xc0), '"'};
    expect(!fixture.parse(SourceView(invalidUtf8, sizeof(invalidUtf8)),
        rejected, error) &&
        error.code == PortableJsonParseErrorCode::InvalidUnicode,
        "malformed UTF-8 is rejected");

    const char embeddedZero[] = {'"', '\\', 'u', '0', '0', '0', '0', 'x', '"'};
    expect(fixture.parse(SourceView(embeddedZero, sizeof(embeddedZero)),
        rejected, error) && rejected.isString(),
        "escaped embedded zero parses as a length-counted runtime string");
    SourceView zeroText;
    expect(fixture.strings.view(rejected.stringId(), zeroText,
        fixture.stringError) && zeroText.length == 2u &&
        zeroText.data[0] == '\0' && zeroText.data[1] == 'x',
        "embedded zero does not truncate decoded string storage");
}

void testExplicitLimitsAndCapacityRecovery()
{
    {
        JsonFixture fixture(2u, 16u, 8u, 8u, 1024u, 1024u);
        PortableJsonParseError error;
        Value value;
        const char* nested = "{\"a\":{\"b\":{}}}";
        expect(!fixture.parse(SourceView(nested, std::strlen(nested)), value,
                error) && error.code ==
                    PortableJsonParseErrorCode::ObjectStoreFailure &&
                error.objectError ==
                    PortableObjectErrorCode::ObjectCapacityExceeded &&
                fixture.objects.objectCount() == 0u,
            "object exhaustion is explicit and partial object trees roll back");
    }
    {
        JsonFixture fixture(8u, 1u, 8u, 8u, 1024u, 1024u);
        PortableJsonParseError error;
        Value value;
        const char* members = "{\"a\":1,\"b\":2}";
        expect(!fixture.parse(SourceView(members, std::strlen(members)), value,
                error) && error.code ==
                    PortableJsonParseErrorCode::ObjectStoreFailure &&
                error.objectError ==
                    PortableObjectErrorCode::PropertyCapacityExceeded &&
                fixture.objects.objectCount() == 0u,
            "property capacity exhaustion returns an explicit store error");
    }
    {
        JsonFixture fixture(8u, 16u, 8u, 1u, 1024u, 1024u);
        PortableJsonParseError error;
        Value value;
        const char* strings = "{\"a\":\"one\",\"b\":\"two\"}";
        expect(!fixture.parse(SourceView(strings, std::strlen(strings)), value,
                error) && error.code ==
                    PortableJsonParseErrorCode::StringLimitExceeded &&
                fixture.objects.objectCount() == 0u &&
                fixture.strings.count() == 0u,
            "string exhaustion rolls back parsed object and earlier strings");
        expect(fixture.parse(SourceView("null", 4u), value, error) &&
            value.isNull(), "parser recovers after deterministic exhaustion");
    }
    {
        JsonFixture fixture;
        PortableJsonParseLimits limits;
        limits.maxNestingDepth = 1u;
        PortableJsonParseError error;
        Value value;
        expect(!fixture.parse(SourceView("[[]]", 4u), value, error, limits) &&
            error.code == PortableJsonParseErrorCode::NestingLimitExceeded &&
            fixture.objects.objectCount() == 0u,
            "nesting limit rejects deep input and restores object storage");
        limits = PortableJsonParseLimits();
        limits.maxInputBytes = 2u;
        expect(!fixture.parse(SourceView("true", 4u), value, error, limits) &&
            error.code == PortableJsonParseErrorCode::InputLimitExceeded,
            "input byte limit is reported distinctly");
        limits = PortableJsonParseLimits();
        limits.maxSteps = 2u;
        expect(!fixture.parse(SourceView("[1,2]", 5u), value, error, limits) &&
            error.code == PortableJsonParseErrorCode::ExecutionBudgetExceeded &&
            fixture.objects.objectCount() == 0u,
            "step exhaustion fails explicitly and rolls back partial arrays");
        limits = PortableJsonParseLimits();
        expect(!fixture.parse(SourceView("5", 1u), value, error, limits,
                nullptr, false) &&
            error.code ==
                PortableJsonParseErrorCode::NumericConversionUnavailable,
            "portable parser reports its hosted number-conversion dependency");
    }
}

} // namespace

int main()
{
    testPrimitivesAndNumbers();
    testObjectsArraysUnicodeAndOrdering();
    testStrictSyntaxAndRollback();
    testExplicitLimitsAndCapacityRecovery();
    if (failures != 0) {
        std::cerr << "JS122 portable JSON checks: " << (checks - failures)
            << '/' << checks << " passed; " << failures << " failed\n";
        return 1;
    }
    std::cout << "JS122 portable JSON checks: " << checks << '/' << checks
        << " passed\n";
    return 0;
}
