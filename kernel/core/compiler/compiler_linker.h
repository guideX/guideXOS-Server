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

} // namespace compiler
} // namespace kernel
