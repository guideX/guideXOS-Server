#pragma once

#include "navigator_javascript/execution_core.h"
#include "navigator_javascript/frontend_storage.h"
#include "navigator_javascript/lexer.h"
#include "navigator_javascript/parser.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

struct Js125PortableCallbackStorage {
    Token* tokens = nullptr;
    std::size_t tokenCapacity = 0u;
    AstNode* nodes = nullptr;
    std::size_t nodeCapacity = 0u;
    AstNodeId* children = nullptr;
    std::size_t childCapacity = 0u;
    unsigned char* parserScratch = nullptr;
    std::size_t parserScratchCapacity = 0u;
    EnvironmentRecord* environments = nullptr;
    std::size_t environmentCapacity = 0u;
    Binding* bindings = nullptr;
    std::size_t bindingCapacity = 0u;
    char* bindingNames = nullptr;
    std::size_t bindingNameCapacity = 0u;
    RuntimeStringRecord* stringRecords = nullptr;
    std::size_t stringRecordCapacity = 0u;
    char* stringBytes = nullptr;
    std::size_t stringByteCapacity = 0u;
    FunctionRecord* functions = nullptr;
    std::size_t functionCapacity = 0u;
    PortableCallFrame* frames = nullptr;
    std::size_t frameCapacity = 0u;
    Value* argumentValues = nullptr;
    std::size_t argumentCapacity = 0u;
    PortableObjectRecord* objects = nullptr;
    std::size_t objectCapacity = 0u;
    PortablePropertyRecord* properties = nullptr;
    std::size_t propertyCapacity = 0u;
    PortableArrayElementRecord* arrayElements = nullptr;
    std::size_t arrayElementCapacity = 0u;
    char* propertyNameBytes = nullptr;
    std::size_t propertyNameCapacity = 0u;
    PortablePropertyKeySnapshotEntry* snapshotEntries = nullptr;
    std::size_t snapshotEntryCapacity = 0u;
    char* snapshotKeyBytes = nullptr;
    std::size_t snapshotKeyByteCapacity = 0u;
};

struct Js125PortableCallbackResult {
    std::uint32_t status = 0u;
    std::uint8_t lexerError = 0u;
    std::uint8_t parserError = 0u;
    std::uint8_t executionError = 0u;
    std::uint8_t invocationError = 0u;
    std::uint8_t finalValueType = 0u;
    std::size_t executionSteps = 0u;
    std::size_t callDepthHighWater = 0u;
    std::size_t argumentValuesHighWater = 0u;
    std::size_t snapshotEntryCount = 0u;
    std::size_t snapshotKeyBytes = 0u;
    std::size_t portableExecutionCoreBytes = 0u;
    std::size_t portableObjectStoreBytes = 0u;
    std::size_t callFrameBytes = 0u;
    std::size_t functionRecordBytes = 0u;
};

} // namespace javascript
} // namespace gxos

extern "C" std::uint32_t gxos_js125_portable_callback_probe(
    const char* source, std::size_t sourceLength,
    const gxos::javascript::Js125PortableCallbackStorage* storage,
    std::uint16_t generation,
    gxos::javascript::Js125PortableCallbackResult* result);
