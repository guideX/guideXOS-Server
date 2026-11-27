#pragma once

#include "navigator_javascript/execution_core.h"
#include "navigator_javascript/frontend_storage.h"
#include "navigator_javascript/lexer.h"
#include "navigator_javascript/parser.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

struct Js129UnifiedCallEntryStorage {
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
    PortableCallFrame* callFrames = nullptr;
    std::size_t callFrameCapacity = 0u;
    Value* argumentValues = nullptr;
    std::size_t argumentCapacity = 0u;
    PortableExecutionFrame* executionFrames = nullptr;
    std::size_t executionFrameCapacity = 0u;
};

struct Js129UnifiedCallEntryResult {
    std::uint32_t status = 0u;
    std::uint8_t lexerError = 0u;
    std::uint8_t parserError = 0u;
    std::uint8_t executionError = 0u;
    std::uint8_t callbackError = 0u;
    double callbackNumber = 0.0;
    std::size_t executionSteps = 0u;
    std::size_t callDepthHighWater = 0u;
    std::size_t argumentValuesHighWater = 0u;
    std::size_t executionFrameHighWater = 0u;
    std::size_t preflightFrameHighWater = 0u;
    std::size_t preflightWorkUnits = 0u;
    std::size_t portableCallFrameBytes = 0u;
    std::size_t portableExecutionFrameBytes = 0u;
};

} // namespace javascript
} // namespace gxos

extern "C" std::uint32_t gxos_js129_unified_call_entry_probe(
    const char* source, std::size_t sourceLength,
    const gxos::javascript::Js129UnifiedCallEntryStorage* storage,
    std::uint16_t generation,
    gxos::javascript::Js129UnifiedCallEntryResult* result);
