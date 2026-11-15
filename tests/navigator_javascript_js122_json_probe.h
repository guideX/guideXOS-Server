#pragma once

#include "navigator_javascript/json_parser.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

struct Js122PortableJsonStorage {
    PortableObjectRecord* objects = nullptr;
    std::size_t objectCapacity = 0u;
    PortablePropertyRecord* properties = nullptr;
    std::size_t propertyCapacity = 0u;
    PortableArrayElementRecord* arrayElements = nullptr;
    std::size_t arrayElementCapacity = 0u;
    char* propertyNameBytes = nullptr;
    std::size_t propertyNameCapacity = 0u;
    RuntimeStringRecord* stringRecords = nullptr;
    std::size_t stringRecordCapacity = 0u;
    char* stringBytes = nullptr;
    std::size_t stringByteCapacity = 0u;
    char* decodedScratch = nullptr;
    std::size_t decodedScratchCapacity = 0u;
    char* keyScratch = nullptr;
    std::size_t keyScratchCapacity = 0u;
};

struct Js122PortableJsonResult {
    std::uint32_t status = 0u;
    std::uint8_t jsonError = 0u;
    std::uint8_t objectError = 0u;
    std::uint8_t stringError = 0u;
    std::uint8_t rootType = 0u;
    RuntimeObjectId rootObject = kInvalidRuntimeObjectId;
    double rootNumber = 0.0;
    std::size_t objectCount = 0u;
    std::size_t propertyCount = 0u;
    std::size_t arrayElementCount = 0u;
    std::size_t stringCount = 0u;
    std::size_t stringBytes = 0u;
    std::size_t stepsConsumed = 0u;
};

} // namespace javascript
} // namespace gxos

extern "C" std::uint32_t gxos_js122_portable_json_probe(
    const char* input, std::size_t inputLength,
    const gxos::javascript::Js122PortableJsonStorage* storage,
    std::uint16_t generation,
    gxos::javascript::Js122PortableJsonResult* result);
