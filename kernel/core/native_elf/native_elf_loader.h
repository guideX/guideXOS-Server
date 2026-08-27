//
// Bare-metal NativeElf loader and diagnostic execution route.
//
#pragma once

#include <stdint.h>

#include "native_elf_validator.h"

namespace kernel {
namespace native_elf {

struct NativeElfExecutionContext {
    uint64_t pageTableRoot;
    uint64_t regionBase;
    uint64_t regionSize;
};

struct NativeElfRunReport {
    bool success;
    int32_t returnValue;
    uint64_t imageBase;
    uint64_t entryPoint;
    uint64_t mappedBytes;
    bool nxEnabled;
    const char* error;
};

bool configure_execution_context(const NativeElfExecutionContext& context);
bool execution_context_configured();

// Loads one validated ET_EXEC NativeElf file into the reusable reserved
// window, invokes its validated gx_main(void*) entry with nullptr, and returns
// only after control has returned to the kernel.
bool run_file(const char* path, int32_t* returnValue, NativeElfRunReport* report = nullptr);

} // namespace native_elf
} // namespace kernel
