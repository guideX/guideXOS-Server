#pragma once

#include "navigator_javascript/execution_core.h"
#include "navigator_javascript/frontend_storage.h"
#include "navigator_javascript/lexer.h"
#include "navigator_javascript/object_storage.h"
#include "navigator_javascript/parser.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

struct Js120PortableObjectArrayStorage {
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
};

struct Js120PortableObjectArrayResult {
    std::uint32_t status = 0u;
    std::uint8_t lexerError = 0u;
    std::uint8_t parserError = 0u;
    std::uint8_t executionError = 0u;
    std::uint8_t finalValueType = 0u;
    std::size_t executionSteps = 0u;
    std::size_t objectCount = 0u;
    std::size_t propertyCount = 0u;
    std::size_t propertyNameBytes = 0u;
    std::size_t arrayElementCount = 0u;
    std::size_t arrayLength = 0u;
    double finalNumber = 0.0;
    std::size_t objectRecordBytes = 0u;
    std::size_t propertyRecordBytes = 0u;
    std::size_t arrayElementRecordBytes = 0u;
    std::size_t objectStoreBytes = 0u;
    std::size_t executionCoreBytes = 0u;
};

} // namespace javascript
} // namespace gxos

extern "C" std::uint32_t gxos_js120_portable_object_array_probe(
    const char* source, std::size_t sourceLength,
    const gxos::javascript::Js120PortableObjectArrayStorage* storage,
    std::uint16_t generation,
    gxos::javascript::Js120PortableObjectArrayResult* result);
