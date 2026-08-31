//
// Bounded translation-unit compilation into the guideXOS internal module form.
//

#pragma once

#include "compiler_diagnostics.h"
#include "compiler_ir.h"

namespace kernel {
namespace compiler {

bool compile_module_from_source(const char* sourcePath,
                                const char* source,
                                uint32_t sourceBytes,
                                CompiledModule* module,
                                Diagnostics& diagnostics);

} // namespace compiler
} // namespace kernel
