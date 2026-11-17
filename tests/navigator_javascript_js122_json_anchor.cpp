#include "navigator_javascript_js122_json_probe.h"
#include "navigator_javascript/json_number_impl.inl"

using namespace gxos::javascript;

static_assert(sizeof(Value) == 16u, "JS122 AMD64 Value size");
static_assert(sizeof(PortableObjectRecord) == 28u,
    "JS122 AMD64 object record size");
static_assert(sizeof(PortablePropertyRecord) == 40u,
    "JS122 AMD64 property record size");
static_assert(sizeof(PortableArrayElementRecord) == 32u,
    "JS122 AMD64 Array element record size");
static_assert(sizeof(RuntimeStringRecord) == 16u,
    "JS122 AMD64 string record size");
static_assert(alignof(Value) == 8u, "JS122 AMD64 Value alignment");

extern "C" std::uint32_t gxos_js122_portable_json_probe(
    const char* input, std::size_t inputLength,
    const Js122PortableJsonStorage* supplied, std::uint16_t generation,
    Js122PortableJsonResult* output)
{
    if (output == nullptr) return 1u;
    *output = Js122PortableJsonResult();
    if (supplied == nullptr || (input == nullptr && inputLength != 0u) ||
        supplied->objects == nullptr || supplied->properties == nullptr ||
        supplied->arrayElements == nullptr ||
        supplied->propertyNameBytes == nullptr ||
        supplied->stringRecords == nullptr || supplied->stringBytes == nullptr ||
        supplied->decodedScratch == nullptr || supplied->keyScratch == nullptr ||
        generation == kInvalidRuntimeGeneration) {
        output->status = 1u;
        return output->status;
    }

    PortableObjectLimits objectLimits;
    objectLimits.maxObjects = supplied->objectCapacity;
    objectLimits.maxProperties = supplied->propertyCapacity;
    objectLimits.maxArrayElements = supplied->arrayElementCapacity;
    objectLimits.maxTotalArrayElements = supplied->arrayElementCapacity;
    objectLimits.maxPropertyNameBytes = supplied->propertyNameCapacity;
    objectLimits.maxArrayLength = supplied->arrayElementCapacity;
    objectLimits.maxArrayIndex = supplied->arrayElementCapacity == 0u ? 0u
        : supplied->arrayElementCapacity - 1u;

    PortableObjectStore objects;
    PortableObjectError objectError;
    if (!objects.configure(PortableObjectStorageView{
            supplied->objects, supplied->objectCapacity,
            supplied->properties, supplied->propertyCapacity,
            supplied->arrayElements, supplied->arrayElementCapacity,
            supplied->propertyNameBytes, supplied->propertyNameCapacity},
            objectLimits, generation, objectError)) {
        output->status = 2u;
        output->objectError = static_cast<std::uint8_t>(objectError.code);
        return output->status;
    }

    RuntimeStringStore strings;
    RuntimeStringError stringError;
    if (!strings.configure(RuntimeStringStorageView{
            supplied->stringRecords, supplied->stringRecordCapacity,
            supplied->stringBytes, supplied->stringByteCapacity}, generation,
            stringError)) {
        output->status = 3u;
        output->stringError = static_cast<std::uint8_t>(stringError.code);
        return output->status;
    }

    PortableJsonParseLimits limits;
    limits.maxStringLength = supplied->decodedScratchCapacity;
    limits.maxStringBytes = supplied->stringByteCapacity;
    limits.maxStringValues = supplied->stringRecordCapacity;
    limits.maxArrayLength = objectLimits.maxArrayLength;
    limits.maxArrayElements = objectLimits.maxArrayElements;
    limits.maxTotalArrayElements = objectLimits.maxTotalArrayElements;
    PortableJsonParseError jsonError;
    PortableJsonParseStats stats;
    Value root;
    const bool succeeded = parsePortableJson(SourceView(input, inputLength),
        objects, strings, limits, supplied->decodedScratch,
        supplied->decodedScratchCapacity, supplied->keyScratch,
        supplied->keyScratchCapacity, nullptr, nullptr, root, jsonError,
        &stats);
    output->status = succeeded ? 0u : 4u;
    output->jsonError = static_cast<std::uint8_t>(jsonError.code);
    output->objectError = static_cast<std::uint8_t>(jsonError.objectError);
    output->stringError = static_cast<std::uint8_t>(jsonError.stringError);
    output->rootType = static_cast<std::uint8_t>(root.type());
    output->rootObject = root.objectId();
    if (root.isNumber()) output->rootNumber = root.numberValue();
    output->objectCount = objects.objectCount();
    output->propertyCount = objects.propertyCount();
    output->arrayElementCount = objects.arrayElementCount();
    output->stringCount = strings.count();
    output->stringBytes = strings.bytesUsed();
    output->stepsConsumed = stats.stepsConsumed;
    return output->status;
}
