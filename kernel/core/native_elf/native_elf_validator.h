//
// Validator for the deliberately narrow NativeElf ET_EXEC subset.
//
#pragma once

#include "native_elf_contract.h"

namespace kernel {
namespace native_elf {

static const uint32_t PF_X = 1U;
static const uint32_t PF_W = 2U;
static const uint32_t PF_R = 4U;

struct NativeElfLoad {
    uint64_t fileOffset;
    uint64_t virtualAddress;
    uint64_t fileSize;
    uint64_t memorySize;
    uint32_t flags;
};

struct NativeElfValidationPolicy {
    uint64_t regionBase;
    uint64_t regionSize;
    uint32_t maxFileBytes;
    uint64_t maxMappedBytes;
    uint16_t maxLoadSegments;
};

struct NativeElfValidationResult {
    bool valid;
    const char* error;
    uint64_t entryPoint;
    uint64_t imageBase;
    uint64_t imageEnd;
    uint64_t mappedBytes;
    uint32_t loadCount;
    uint32_t executableLoadCount;
    uint32_t entryLoadIndex;
    NativeElfLoad loads[guidexos::native_elf::MAX_LOAD_SEGMENTS];
};

NativeElfValidationPolicy default_validation_policy();

bool validate_native_elf(const uint8_t* image,
                         uint32_t imageBytes,
                         const NativeElfValidationPolicy& policy,
                         NativeElfValidationResult* result);

} // namespace native_elf
} // namespace kernel
