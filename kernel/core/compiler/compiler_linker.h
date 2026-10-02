//
// Minimal guideXOS-native linker for compiler bootstrap modules.
//
#pragma once

#include "compiler_diagnostics.h"
#include "compiler_ir.h"

namespace kernel {
namespace compiler {

bool link_modules(const CompiledModule* modules,
                 uint32_t moduleCount,
                 LinkedProgram* output,
                 Diagnostics& diagnostics);

// Hosted static-ELF consumers may select a page-aligned image base to avoid
// colliding with the host application. The kernel compiler continues to use
// the canonical NativeElf base through the overload above.
bool link_modules(const CompiledModule* modules,
                 uint32_t moduleCount,
                 LinkedProgram* output,
                 Diagnostics& diagnostics,
                 uint64_t imageBase);

} // namespace compiler
} // namespace kernel
