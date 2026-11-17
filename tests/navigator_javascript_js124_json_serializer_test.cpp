#include "navigator_javascript/json_number_impl.inl"
#include "navigator_javascript/json_parser.h"
#include "navigator_javascript/json_serializer.h"
#include "navigator_javascript/json_writer.h"
#include "tests/navigator_javascript_js124_number_expected.h"

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

std::uint64_t bitsOf(double value)
{
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double valueOf(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

struct SerializeCall {
    PortableJsonSerializeResult result;
    std::string output;
    std::size_t writerSize = 0u;
};

struct JsonFixture {
    std::vector<PortableObjectRecord> objectRecords{128u};
    std::vector<PortablePropertyRecord> propertyRecords{512u};
    std::vector<PortableArrayElementRecord> arrayRecords{256u};
    std::vector<char> propertyNames = std::vector<char>(32768u);
    std::vector<RuntimeStringRecord> stringRecords{256u};
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
        expect(objects.configure(PortableObjectStorageView{
                objectRecords.data(), objectRecords.size(),
                propertyRecords.data(), propertyRecords.size(),
                arrayRecords.data(), arrayRecords.size(), propertyNames.data(),
                propertyNames.size()}, objectLimits, 1u, objectError),
            "object store configures");
        expect(strings.configure(RuntimeStringStorageView{
                stringRecords.data(), stringRecords.size(), stringBytes.data(),
                stringBytes.size()}, 1u, stringError),
            "string store configures");
    }

    bool parse(const std::string& input, Value& value,
        PortableJsonParseError& error)
    {
        PortableJsonParseLimits limits;
        limits.maxInputBytes = 64u * 1024u;
        limits.maxStringLength = 64u * 1024u;
        limits.maxStringValues = stringRecords.size();
        limits.maxStringBytes = stringBytes.size();
        limits.maxNestingDepth = 32u;
        limits.maxSteps = 100000u;
        limits.numberConverter = &parsePortableJsonBinary64;
        std::vector<char> decoded(limits.maxStringLength);
        std::vector<char> keys(limits.maxNestingDepth *
            objects.limits().maxPropertyNameLength);
        return parsePortableJson(SourceView(input.data(), input.size()),
            objects, strings, limits, decoded.data(), decoded.size(),
            keys.data(), keys.size(), nullptr, nullptr, value, error);
    }

    bool createObject(RuntimeObjectId& object)
    {
        PortableObjectError error;
        return objects.createObject(kInvalidRuntimeObjectId, object, error);
    }

    bool createArray(std::size_t length, RuntimeObjectId& object)
    {
        PortableObjectError error;
        return objects.createArray(length, kInvalidRuntimeObjectId, object,
            error);
    }

    bool set(RuntimeObjectId object, const char* key, Value value,
        std::uint8_t attributes = 0x07u)
    {
        PortableObjectError error;
        const SourceView name(key, std::strlen(key));
        return objects.setOwnProperty(object, name, value, error, attributes);
    }

    bool setArray(RuntimeObjectId object, std::size_t index, Value value)
    {
        PortableObjectError error;
        return objects.setArrayElement(object, index, value, error);
    }

    bool createString(const std::string& text, Value& value)
    {
        RuntimeStringId id = kInvalidRuntimeStringId;
        RuntimeStringError error;
        if (!strings.create(SourceView(text.data(), text.size()),
                64u * 1024u, id, error)) return false;
        value = Value::string(id);
        return true;
    }

    SerializeCall serialize(Value value, std::size_t outputCapacity =
            64u * 1024u,
        PortableJsonSerializeLimits limits = PortableJsonSerializeLimits(),
        std::size_t frameCapacityOverride = static_cast<std::size_t>(-1),
        PortableJsonObjectValidator objectValidator = nullptr,
        void* objectValidatorContext = nullptr)
    {
        std::vector<char> outputBuffer(outputCapacity);
        std::vector<PortableJsonTraversalFrame> frames(
            frameCapacityOverride == static_cast<std::size_t>(-1)
                ? limits.maxNestingDepth : frameCapacityOverride);
        char numberScratch[384] = {};
        PortableJsonOutputWriter writer(outputBuffer.data(),
            outputBuffer.size());
        SerializeCall call;
        call.result = serializePortableJson(value, objects, strings, writer,
            PortableJsonSerializeStorageView{frames.data(), frames.size(),
                numberScratch, sizeof(numberScratch)}, limits, nullptr, nullptr,
            objectValidator, objectValidatorContext);
        call.writerSize = writer.size();
        if (call.result.status == PortableJsonSerializeStatus::Success)
            call.output.assign(call.result.output.data,
                call.result.output.length);
        return call;
    }
};

PortableJsonObjectDisposition hostedOnlyObject(void*, RuntimeObjectId)
{
    return PortableJsonObjectDisposition::HostedOperationRequired;
}

void testNumberFormatting()
{
    for (std::size_t index = 0u;
        index < kJs124NumberExpectedFixtureCount; ++index) {
        const Js124NumberExpectedFixture& fixture =
            kJs124NumberExpectedFixtures[index];
        char output[384] = {};
        std::size_t length = 0u;
        const PortableJsonNumberFormatCode code =
            formatPortableJsonBinary64(valueOf(fixture.bits), output,
                sizeof(output), length);
        if (std::string(fixture.expected) == "null") {
            expect(code == PortableJsonNumberFormatCode::NonFinite,
                "non-finite Node fixture is explicitly classified");
            continue;
        }
        const bool formatted = code == PortableJsonNumberFormatCode::None &&
            std::string(output, length) == fixture.expected;
        expect(formatted, "portable Number formatting matches Node bytes");
        if (!formatted) continue;

        double parsed = 0.0;
        const bool parsedSuccessfully = parsePortableJsonBinary64(
            SourceView(output, length), parsed);
        const std::uint64_t expectedBits = fixture.expected[0] == '0' &&
                fixture.expected[1] == '\0' && fixture.bits ==
                    0x8000000000000000ull
            ? 0u : fixture.bits;
        expect(parsedSuccessfully && bitsOf(parsed) == expectedBits,
            "formatted finite Number round-trips through shared ffc parser");
    }
}

void testParsedObjectParity()
{
    JsonFixture fixture;
    const std::string input =
        "{\"b\":1,\"10\":10,\"01\":1,\"2\":2,\"a\":1e-7,"
        "\"z\":-0,\"m\":1e20,\"n\":1e21}";
    Value value;
    PortableJsonParseError parseError;
    expect(fixture.parse(input, value, parseError),
        "portable JSON parser creates shared source object");
    const SerializeCall call = fixture.serialize(value);
    expect(call.result.status == PortableJsonSerializeStatus::Success &&
            call.output ==
                "{\"2\":2,\"10\":10,\"b\":1,\"01\":1,\"a\":1e-7,"
                "\"z\":0,\"m\":100000000000000000000,\"n\":1e+21}",
        "shared serializer matches canonical object bytes and Number spelling");

    Value escaped;
    PortableJsonParseError escapedError;
    expect(fixture.parse("{\"text\":\"quote\\\" \\\\ tab\\t nul\\u0000 snowman \\u2603\"}",
            escaped, escapedError), "escaped JSON input parses");
    const SerializeCall escapedCall = fixture.serialize(escaped);
    const std::string expectedEscaped =
        "{\"text\":\"quote\\\" \\\\ tab\\t nul\\u0000 snowman \xe2\x98\x83\"}";
    expect(escapedCall.result.status == PortableJsonSerializeStatus::Success &&
            escapedCall.output == expectedEscaped,
        "shared writer preserves UTF-8 and canonical JSON escaping");
}

void testObjectSemantics()
{
    JsonFixture fixture;
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    expect(fixture.createObject(object), "ordinary object creates");
    expect(fixture.set(object, "b", Value::number(1.0)) &&
            fixture.set(object, "10", Value::number(10.0)) &&
            fixture.set(object, "01", Value::number(2.0)) &&
            fixture.set(object, "2", Value::number(2.0)) &&
            fixture.set(object, "a", Value::number(3.0)) &&
            fixture.set(object, "z", Value::number(5.0)),
        "object properties insert into shared store");
    PortableObjectError objectError;
    bool deleted = false;
    expect(fixture.objects.deleteOwnProperty(object, SourceView("a", 1u),
            deleted, objectError) && deleted,
        "property deletion updates the shared store");
    expect(fixture.set(object, "a", Value::number(6.0)),
        "deleted string property reinserts");
    expect(fixture.set(object, "b", Value::number(4.0)),
        "updating a property preserves its insertion slot");
    expect(fixture.set(object, "hidden", Value::number(9.0), 0x01u),
        "non-enumerable data property inserts");
    expect(fixture.set(object, "u", Value::undefined()) &&
            fixture.set(object, "f", Value::function(17u)) &&
            fixture.set(object, "s", Value::wellKnownSymbol(
                WellKnownSymbol::Iterator)),
        "unrepresentable property values insert");
    const SerializeCall call = fixture.serialize(Value::object(object));
    expect(call.result.status == PortableJsonSerializeStatus::Success &&
            call.output ==
                "{\"2\":2,\"10\":10,\"b\":4,\"01\":2,\"z\":5,"
                "\"a\":6}",
        "numeric keys sort and other enumerable string keys retain insertion order");

    const SerializeCall undefined = fixture.serialize(Value::undefined());
    expect(undefined.result.status == PortableJsonSerializeStatus::NoResult &&
            undefined.output.empty() && undefined.writerSize == 0u,
        "top-level undefined returns a distinct no-result outcome");
    const SerializeCall function = fixture.serialize(Value::function(9u));
    expect(function.result.status == PortableJsonSerializeStatus::NoResult,
        "top-level function returns no JSON result");
    const SerializeCall symbol = fixture.serialize(Value::wellKnownSymbol(
        WellKnownSymbol::Iterator));
    expect(symbol.result.status == PortableJsonSerializeStatus::NoResult,
        "top-level Symbol returns no JSON result");

    RuntimeObjectId empty = kInvalidRuntimeObjectId;
    expect(fixture.createObject(empty), "empty object creates");
    const SerializeCall emptyCall = fixture.serialize(Value::object(empty));
    expect(emptyCall.result.status == PortableJsonSerializeStatus::Success &&
            emptyCall.output == "{}", "empty object serializes");
}

void testArraysAndNestedValues()
{
    JsonFixture fixture;
    RuntimeObjectId array = kInvalidRuntimeObjectId;
    expect(fixture.createArray(3u, array), "logical Array length is created");
    expect(fixture.setArray(array, 0u, Value::number(1.0)) &&
            fixture.setArray(array, 2u, Value::number(3.0)),
        "sparse Array slots are stored");
    PortableObjectError objectError;
    bool deleted = false;
    expect(fixture.objects.deleteArrayElement(array, 2u, deleted,
            objectError) && deleted,
        "last Array element deletes without reducing length");
    std::size_t length = 0u;
    expect(fixture.objects.arrayLength(array, length, objectError) &&
            length == 3u, "deleting the last slot preserves logical length");
    const SerializeCall sparse = fixture.serialize(Value::object(array));
    expect(sparse.result.status == PortableJsonSerializeStatus::Success &&
            sparse.output == "[1,null,null]",
        "holes and deleted Array slots serialize as null");

    RuntimeObjectId dense = kInvalidRuntimeObjectId;
    expect(fixture.createArray(4u, dense), "dense Array creates");
    expect(fixture.setArray(dense, 0u, Value::number(1.0)) &&
            fixture.setArray(dense, 1u, Value::undefined()) &&
            fixture.setArray(dense, 2u, Value::function(18u)) &&
            fixture.setArray(dense, 3u, Value::number(
                std::numeric_limits<double>::infinity())),
        "undefined, function, and non-finite Array values insert");
    const SerializeCall denseCall = fixture.serialize(Value::object(dense));
    expect(denseCall.result.status == PortableJsonSerializeStatus::Success &&
            denseCall.output == "[1,null,null,null]",
        "Array undefined, functions, and infinities serialize as null");

    RuntimeObjectId shared = kInvalidRuntimeObjectId;
    RuntimeObjectId root = kInvalidRuntimeObjectId;
    expect(fixture.createObject(shared) && fixture.createObject(root) &&
            fixture.set(shared, "x", Value::number(1.0)) &&
            fixture.set(root, "a", Value::object(shared)) &&
            fixture.set(root, "b", Value::object(shared)),
        "shared noncyclic object graph builds with stable identity");
    const SerializeCall repeated = fixture.serialize(Value::object(root));
    expect(repeated.result.status == PortableJsonSerializeStatus::Success &&
            repeated.output == "{\"a\":{\"x\":1},\"b\":{\"x\":1}}",
        "repeated references are serialized more than once without false cycles");

    RuntimeObjectId nestedArray = kInvalidRuntimeObjectId;
    RuntimeObjectId wrapper = kInvalidRuntimeObjectId;
    expect(fixture.createArray(1u, nestedArray) &&
            fixture.setArray(nestedArray, 0u, Value::object(shared)) &&
            fixture.createObject(wrapper) &&
            fixture.set(wrapper, "items", Value::object(nestedArray)),
        "nested object and Array graph builds");
    const SerializeCall nested = fixture.serialize(Value::object(wrapper));
    expect(nested.result.status == PortableJsonSerializeStatus::Success &&
            nested.output == "{\"items\":[{\"x\":1}]}",
        "nested Array and object traversal shares the same stores");
}

void testCyclesCallbacksAndRecovery()
{
    JsonFixture fixture;
    RuntimeObjectId self = kInvalidRuntimeObjectId;
    expect(fixture.createObject(self) &&
            fixture.set(self, "self", Value::object(self)),
        "self-cycle builds");
    const SerializeCall selfCycle = fixture.serialize(Value::object(self));
    expect(selfCycle.result.status == PortableJsonSerializeStatus::Error &&
            selfCycle.result.error.code ==
                PortableJsonSerializeErrorCode::CycleDetected &&
            selfCycle.result.output.length == 0u && selfCycle.writerSize == 0u,
        "self-cycle fails atomically with a cycle result");

    RuntimeObjectId left = kInvalidRuntimeObjectId;
    RuntimeObjectId right = kInvalidRuntimeObjectId;
    expect(fixture.createObject(left) && fixture.createObject(right) &&
            fixture.set(left, "next", Value::object(right)) &&
            fixture.set(right, "next", Value::object(left)),
        "indirect cycle builds");
    const SerializeCall indirect = fixture.serialize(Value::object(left));
    expect(indirect.result.error.code ==
            PortableJsonSerializeErrorCode::CycleDetected,
        "indirect cycle is detected on the active ancestor path");

    RuntimeObjectId callbackObject = kInvalidRuntimeObjectId;
    expect(fixture.createObject(callbackObject) &&
            fixture.set(callbackObject, "toJSON", Value::function(20u)),
        "toJSON capability is represented in shared store");
    const SerializeCall callback = fixture.serialize(
        Value::object(callbackObject));
    expect(callback.result.error.code ==
            PortableJsonSerializeErrorCode::UnsupportedCallback &&
            callback.result.output.length == 0u,
        "callable toJSON is explicitly unsupported without silent omission");

    RuntimeObjectId prototype = kInvalidRuntimeObjectId;
    RuntimeObjectId child = kInvalidRuntimeObjectId;
    PortableObjectError objectError;
    expect(fixture.createObject(prototype) && fixture.createObject(child) &&
            fixture.set(prototype, "toJSON", Value::function(21u)),
        "inherited callback prototype builds");
    expect(fixture.objects.setPrototype(child, prototype, objectError),
        "shared object prototype attaches");
    const SerializeCall inheritedCallback = fixture.serialize(
        Value::object(child));
    expect(inheritedCallback.result.error.code ==
            PortableJsonSerializeErrorCode::UnsupportedCallback,
        "inherited callable toJSON also blocks portable traversal");

    RuntimeObjectId hostedOnly = kInvalidRuntimeObjectId;
    expect(fixture.createObject(hostedOnly), "host-only object creates");
    const SerializeCall hostedCall = fixture.serialize(Value::object(hostedOnly),
        64u * 1024u, PortableJsonSerializeLimits(),
        static_cast<std::size_t>(-1), &hostedOnlyObject);
    expect(hostedCall.result.error.code ==
            PortableJsonSerializeErrorCode::HostedOperationRequired,
        "host-only value operations return a distinct fallback status");

    const SerializeCall recovered = fixture.serialize(Value::boolean(true));
    expect(recovered.result.status == PortableJsonSerializeStatus::Success &&
            recovered.output == "true" && recovered.writerSize == 4u,
        "serializer and bounded writer recover after traversal failures");
}

void testBoundsAndStaleHandles()
{
    JsonFixture fixture;
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    expect(fixture.createObject(object) &&
            fixture.set(object, "large", Value::string(
                kInvalidRuntimeStringId)),
        "object with stale string handle builds");
    const SerializeCall invalidString = fixture.serialize(Value::object(object));
    expect(invalidString.result.error.code ==
            PortableJsonSerializeErrorCode::StaleStringHandle &&
            invalidString.writerSize == 0u,
        "invalid string reference fails without exposing partial bytes");

    RuntimeObjectId outputObject = kInvalidRuntimeObjectId;
    expect(fixture.createObject(outputObject) &&
            fixture.set(outputObject, "long", Value::number(123456.0)),
        "output bound object builds");
    const SerializeCall outputLimit = fixture.serialize(
        Value::object(outputObject), 4u);
    expect(outputLimit.result.error.code ==
            PortableJsonSerializeErrorCode::OutputCapacityExceeded &&
            outputLimit.writerSize == 0u,
        "output exhaustion is bounded and failure is atomic");

    PortableJsonSerializeLimits depthLimits;
    depthLimits.maxNestingDepth = 1u;
    RuntimeObjectId parent = kInvalidRuntimeObjectId;
    RuntimeObjectId child = kInvalidRuntimeObjectId;
    expect(fixture.createObject(parent) && fixture.createObject(child) &&
            fixture.set(parent, "child", Value::object(child)),
        "depth-bound graph builds");
    const SerializeCall depth = fixture.serialize(Value::object(parent),
        64u * 1024u, depthLimits);
    expect(depth.result.error.code ==
            PortableJsonSerializeErrorCode::DepthLimitExceeded,
        "depth beyond configured nesting limit fails deterministically");

    PortableJsonSerializeLimits traversalLimits;
    traversalLimits.maxNestingDepth = 32u;
    const SerializeCall traversal = fixture.serialize(Value::object(parent),
        64u * 1024u, traversalLimits, 0u);
    expect(traversal.result.error.code ==
            PortableJsonSerializeErrorCode::TraversalCapacityExceeded,
        "caller-owned traversal storage capacity is enforced");

    PortableJsonSerializeLimits stepLimits;
    stepLimits.maxSteps = 1u;
    const SerializeCall steps = fixture.serialize(Value::object(parent),
        64u * 1024u, stepLimits);
    expect(steps.result.error.code ==
            PortableJsonSerializeErrorCode::StepBudgetExceeded,
        "bounded serializer operation budget is enforced");

    JsonFixture arrayFixture;
    RuntimeObjectId array = kInvalidRuntimeObjectId;
    expect(arrayFixture.createArray(5u, array), "over-limit Array creates");
    PortableJsonSerializeLimits arrayLimits;
    arrayLimits.maxArrayLength = 4u;
    const SerializeCall arrayLimit = arrayFixture.serialize(
        Value::object(array), 64u * 1024u, arrayLimits);
    expect(arrayLimit.result.error.code ==
            PortableJsonSerializeErrorCode::ArrayLimitExceeded,
        "Array logical length limit is enforced");

    JsonFixture staleObjectFixture;
    RuntimeObjectId staleObject = kInvalidRuntimeObjectId;
    expect(staleObjectFixture.createObject(staleObject),
        "stale object fixture creates object");
    expect(staleObjectFixture.objects.reset(2u,
            staleObjectFixture.objectError), "object store generation advances");
    const SerializeCall staleObjectCall = staleObjectFixture.serialize(
        Value::object(staleObject));
    expect(staleObjectCall.result.error.code ==
            PortableJsonSerializeErrorCode::StaleObjectHandle,
        "stale object handle is rejected by generation check");

    JsonFixture staleStringFixture;
    Value staleString;
    expect(staleStringFixture.createString("old", staleString),
        "stale string fixture creates string");
    staleStringFixture.strings.reset(2u);
    const SerializeCall staleStringCall = staleStringFixture.serialize(
        staleString);
    expect(staleStringCall.result.error.code ==
            PortableJsonSerializeErrorCode::StaleStringHandle,
        "stale string handle is rejected by generation check");
}

} // namespace

int main()
{
    testNumberFormatting();
    testParsedObjectParity();
    testObjectSemantics();
    testArraysAndNestedValues();
    testCyclesCallbacksAndRecovery();
    testBoundsAndStaleHandles();
    std::cout << "Navigator JavaScript JS124 serializer checks: " << checks
              << '/' << checks << " passed; failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
