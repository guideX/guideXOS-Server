//
// Bounded persistent ELF64 relocatable object boundary.
//
#pragma once

#include "compiler_diagnostics.h"
#include "compiler_ir.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_ELF_OBJECT_HEADER_BYTES = 64;
static const uint32_t COMPILER_ELF_OBJECT_META_HEADER_BYTES = 112;

struct ElfObjectHeaderView {
    uint16_t elfType;
    uint16_t machine;
    uint16_t formatVersion;
    uint16_t metaHeaderSize;
    uint32_t targetArchitecture;
    uint32_t targetAbi;
    uint32_t compilerObjectAbiVersion;
    uint32_t flags;
    uint64_t sourceHash;
    uint32_t sourceSize;
    uint32_t sourcePathBytes;
    uint32_t tokenCount;
    int32_t returnConstant;
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
    uint16_t structTypeCount;
    uint16_t sectionCount;
    uint32_t symbolCount;
    uint32_t metaOffset;
    uint32_t dependencyCount;
    uint32_t dependencyMetadataBytes;
    uint32_t sourceMapCount;
    uint32_t sourceMapMetadataBytes;
    uint32_t metaBytes;
    uint32_t objectBytes;
    uint64_t objectChecksum;
};

// Serialize and deserialize the complete bounded linker input as a genuine
// ELF64 ET_REL/EM_X86_64 object.  The standard ELF sections carry machine code,
// data, symbols, and relocations.  .gx.meta carries only the extra bounded
// compiler contract needed by the existing in-OS linker.
bool serialize_elf_object(const CompiledModule& module,
                          uint8_t* output, uint32_t capacity, uint32_t* outputBytes);

bool deserialize_elf_object(const uint8_t* bytes, uint32_t byteCount,
                             CompiledModule* module, Diagnostics& diagnostics);

bool inspect_elf_object(const uint8_t* bytes, uint32_t byteCount,
                        ElfObjectHeaderView* header, Diagnostics& diagnostics);

uint64_t elf_object_checksum(const uint8_t* bytes, uint32_t byteCount,
                             uint32_t checksumOffset);

bool elf_object_identity_matches(const CompiledModule& module,
                                 const char* normalizedSourcePath,
                                 uint32_t sourceBytes, uint64_t sourceHash);

// Source compatibility for the immediately preceding 27P host fixtures.
// These names now read and write ELF64 ET_REL bytes; no GXO format remains.
using GxoObjectHeaderView = ElfObjectHeaderView;
inline bool serialize_gxo_object(const CompiledModule& module, uint8_t* output,
                                 uint32_t capacity, uint32_t* outputBytes)
{ return serialize_elf_object(module, output, capacity, outputBytes); }
inline bool deserialize_gxo_object(const uint8_t* bytes, uint32_t byteCount,
                                   CompiledModule* module, Diagnostics& diagnostics)
{ return deserialize_elf_object(bytes, byteCount, module, diagnostics); }
inline bool inspect_gxo_header(const uint8_t* bytes, uint32_t byteCount,
                               ElfObjectHeaderView* header, Diagnostics& diagnostics)
{ return inspect_elf_object(bytes, byteCount, header, diagnostics); }
inline bool gxo_object_identity_matches(const CompiledModule& module,
                                        const char* path, uint32_t bytes, uint64_t hash)
{ return elf_object_identity_matches(module, path, bytes, hash); }

} // namespace compiler
} // namespace kernel
