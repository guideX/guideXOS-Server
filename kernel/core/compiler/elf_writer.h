//
// Bootstrap ELF64 executable writer and validator.
//
// This is deliberately a final-image writer, not a linker.  It emits a small
// deterministic ET_EXEC image with an RX code load and, when requested, an R
// read-only data load.  There are no section or dynamic tables.
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
static const uint32_t BOOTSTRAP_MAX_ELF_BYTES = 12288;

struct ElfLayout {
    uint64_t imageBase;
    uint64_t entryPoint;
    uint32_t codeOffset;
    uint32_t dataOffset;
    uint64_t dataAddress;
    uint32_t dataBytes;
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
                             uint32_t expectedDataBytes = 0);

} // namespace compiler
} // namespace kernel
