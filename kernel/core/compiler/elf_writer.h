//
// Bootstrap ELF64 executable writer and validator.
//
// This is deliberately a final-image writer, not a linker.  It emits one
// deterministic RX PT_LOAD segment and no section or dynamic tables.
//

#pragma once

#include "kernel/types.h"
#include "../native_elf/native_elf_contract.h"

namespace kernel {
namespace compiler {

static const uint64_t BOOTSTRAP_IMAGE_BASE = guidexos::native_elf::IMAGE_BASE;
static const uint32_t BOOTSTRAP_CODE_OFFSET = 0x1000;
static const uint32_t BOOTSTRAP_MAX_ELF_BYTES = 8192;

struct ElfLayout {
    uint64_t imageBase;
    uint64_t entryPoint;
    uint32_t codeOffset;
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
};

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout);

bool validate_bootstrap_elf(const uint8_t* image,
                            uint32_t imageBytes,
                            uint64_t expectedImageBase,
                            uint32_t expectedCodeOffset,
                            const uint8_t* expectedCode,
                            uint32_t expectedCodeBytes,
                            ElfValidationResult* result);

} // namespace compiler
} // namespace kernel
