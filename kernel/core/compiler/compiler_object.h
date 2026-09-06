//
// Bounded persistent guideXOS relocatable object format.
//
#pragma once

#include "compiler_diagnostics.h"
#include "compiler_ir.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_GXO_HEADER_BYTES = 100;

struct GxoObjectHeaderView {
    uint16_t formatVersion;
    uint16_t headerSize;
    uint32_t targetArchitecture;
    uint32_t targetAbi;
    uint32_t compilerObjectAbiVersion;
    uint32_t flags;
    uint64_t sourceHash;
    uint32_t sourceSize;
    uint32_t sourcePathBytes;
    uint32_t codeSize;
    uint32_t rodataSize;
    uint32_t rwdataSize;
    uint32_t exportCount;
    uint32_t importCount;
    uint32_t relocationCount;
    uint32_t functionCount;
    uint32_t globalCount;
    uint32_t callGraphEdgeCount;
    uint16_t recursiveSccCount;
    uint32_t entryCodeOffset;
    uint32_t payloadBytes;
    uint32_t objectBytes;
    uint64_t objectChecksum;
};

// Serialize and deserialize the complete bounded linker input.  The format is
// little-endian and contains no pointers or host-native structure padding.
bool serialize_gxo_object(const CompiledModule& module,
                          uint8_t* output, uint32_t capacity, uint32_t* outputBytes);

bool deserialize_gxo_object(const uint8_t* bytes, uint32_t byteCount,
                            CompiledModule* module, Diagnostics& diagnostics);

bool inspect_gxo_header(const uint8_t* bytes, uint32_t byteCount,
                        GxoObjectHeaderView* header, Diagnostics& diagnostics);

uint64_t gxo_object_checksum(const uint8_t* bytes, uint32_t byteCount);

bool gxo_object_identity_matches(const CompiledModule& module,
                                 const char* normalizedSourcePath,
                                 uint32_t sourceBytes, uint64_t sourceHash);

} // namespace compiler
} // namespace kernel
