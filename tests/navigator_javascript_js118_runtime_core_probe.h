#pragma once

#include "navigator_javascript/environment.h"
#include "navigator_javascript/runtime_string_storage.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

struct Js118RuntimeCoreProbeResult {
    std::uint32_t status = 0u;
    std::uint8_t environmentError = 0u;
    std::uint8_t stringError = 0u;
    std::uint8_t valueType = 0u;
    std::uint8_t reserved = 0u;
    EnvironmentId root = kInvalidEnvironmentId;
    EnvironmentId child = kInvalidEnvironmentId;
    RuntimeStringId text = kInvalidRuntimeStringId;
    std::size_t environmentCount = 0u;
    std::size_t bindingCount = 0u;
    std::size_t nameBytesUsed = 0u;
    std::size_t stringCount = 0u;
    std::size_t stringBytesUsed = 0u;
    double observedNumber = 0.0;
    char observedText[2] = {};
};

} // namespace javascript
} // namespace gxos

extern "C" std::uint32_t gxos_js118_runtime_core_probe(
    gxos::javascript::EnvironmentRecord* environmentRecords,
    std::size_t environmentCapacity, gxos::javascript::Binding* bindings,
    std::size_t bindingCapacity, char* bindingNameBytes,
    std::size_t bindingNameCapacity,
    gxos::javascript::RuntimeStringRecord* stringRecords,
    std::size_t stringRecordCapacity, char* stringBytes,
    std::size_t stringByteCapacity, std::uint16_t generation,
    gxos::javascript::Js118RuntimeCoreProbeResult* result);
