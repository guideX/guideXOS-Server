//
// Bootstrap ELF64 executable writer and validator.
//
// This is deliberately a final-image writer, not a linker.  It emits a small
// deterministic ET_EXEC image with separate RX code, R read-only data, and RW
// mutable-data loads.  There are no section or dynamic tables.
//

#pragma once

#include "kernel/types.h"
#include "compiler_ir.h"
#include "../native_elf/native_elf_contract.h"

namespace kernel {
namespace compiler {

static const uint64_t BOOTSTRAP_IMAGE_BASE = guidexos::native_elf::IMAGE_BASE;
static const uint32_t BOOTSTRAP_CODE_OFFSET = 0x1000;
static const uint32_t BOOTSTRAP_DATA_OFFSET = 0x2000;
static const uint32_t BOOTSTRAP_MAX_ELF_BYTES = 98304;
static const uint32_t BOOTSTRAP_SOURCE_MAP_HEADER_BYTES = 40;
static const uint32_t BOOTSTRAP_SOURCE_MAP_FILE_BYTES = COMPILER_MAX_SOURCE_PATH_BYTES + 12;
static const uint32_t BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES = COMPILER_FUNCTION_NAME_CAPACITY;
static const uint32_t BOOTSTRAP_SOURCE_MAP_RECORD_BYTES = 24;

struct ElfLayout {
    uint64_t imageBase;
    uint64_t entryPoint;
    uint32_t codeOffset;
    uint32_t codeBytes;
    uint32_t entryCodeOffset;
    uint32_t dataOffset;
    uint64_t dataAddress;
    uint32_t dataBytes;
    uint32_t mutableDataOffset;
    uint64_t mutableDataAddress;
    uint32_t mutableDataBytes;
    uint32_t outputBytes;
};

struct ElfValidationResult {
    bool valid;
    const char* error;
    uint64_t entryPoint;
    uint64_t imageBase;
    uint32_t loadCount;
    uint32_t executableLoadCount;
    uint32_t codeFileOffset;
    uint32_t dataFileOffset;
    uint64_t dataVirtualAddress;
    uint32_t dataBytes;
    uint32_t mutableDataFileOffset;
    uint64_t mutableDataVirtualAddress;
    uint32_t mutableDataBytes;
};

struct ResolvedSourceMapping {
    uint64_t targetAddress;
    uint32_t finalCodeOffset;
    uint32_t instructionBytes;
    uint32_t line;
    uint32_t column;
    uint32_t sourceBytes;
    uint64_t sourceHash;
    char sourcePath[COMPILER_MAX_SOURCE_PATH_BYTES];
    char functionName[COMPILER_FUNCTION_NAME_CAPACITY];
};

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout);

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         const uint8_t* readOnlyData,
                         uint32_t readOnlyDataBytes,
                         const uint8_t* mutableData,
                         uint32_t mutableDataBytes,
                         uint32_t entryCodeOffset,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout);

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         const uint8_t* readOnlyData,
                         uint32_t readOnlyDataBytes,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout);

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         const uint8_t* readOnlyData,
                         uint32_t readOnlyDataBytes,
                         uint32_t entryCodeOffset,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout);

bool validate_bootstrap_elf(const uint8_t* image,
                            uint32_t imageBytes,
                            uint64_t expectedImageBase,
                            uint32_t expectedCodeOffset,
                            const uint8_t* expectedCode,
                             uint32_t expectedCodeBytes,
                            ElfValidationResult* result,
                            const uint8_t* expectedData = nullptr,
                            uint32_t expectedDataBytes = 0,
                            uint32_t expectedEntryCodeOffset = 0);

// Append and resolve a deterministic, non-loadable source-map trailer.  The
// trailer is part of the BuildResult identity but never changes PT_LOAD
// ranges, permissions, or the executable code bytes.
bool append_bootstrap_source_map(const LinkedProgram& program,
                                 uint8_t* output, uint32_t outputCapacity,
                                 ElfLayout* layout);

bool resolve_bootstrap_source_mapping(const uint8_t* image, uint32_t imageBytes,
                                      uint64_t imageBase, uint32_t codeFileOffset,
                                      uint32_t codeBytes, const char* sourcePath,
                                      uint32_t line, uint32_t column,
                                      ResolvedSourceMapping* result,
                                      const char** error);

bool validate_bootstrap_elf(const uint8_t* image,
                            uint32_t imageBytes,
                            uint64_t expectedImageBase,
                            uint32_t expectedCodeOffset,
                            const uint8_t* expectedCode,
                            uint32_t expectedCodeBytes,
                            ElfValidationResult* result,
                            const uint8_t* expectedData,
                            uint32_t expectedDataBytes,
                            const uint8_t* expectedMutableData,
                            uint32_t expectedMutableDataBytes,
                            uint32_t expectedEntryCodeOffset = 0);

} // namespace compiler
} // namespace kernel
