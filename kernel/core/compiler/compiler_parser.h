//
// Parser for the intentionally tiny compiler bootstrap grammar.
//

#pragma once

#include "compiler_diagnostics.h"
#include "compiler_ir.h"
#include "compiler_lexer.h"

namespace kernel {
namespace compiler {

bool parse_translation_unit(const char* source,
                            const Token* tokens,
                            uint32_t tokenCount,
                            TranslationUnitIR* output,
                            Diagnostics& diagnostics);

// The caller-owned arenas make translation-unit isolation explicit. The
// legacy overload above retains its original compatibility arena.
bool parse_translation_unit(const char* source,
                            const Token* tokens,
                            uint32_t tokenCount,
                            TranslationUnitIR* output,
                            Diagnostics& diagnostics,
                            CallSite* callStorage,
                            uint16_t* callArgumentStorage);

// Compatibility entry point for the pre-27L host tests and callers that
// intentionally compile a one-function source.  Multi-function sources use
// parse_translation_unit directly.
bool parse_function(const char* source,
                    const Token* tokens,
                    uint32_t tokenCount,
                    FunctionIR* output,
                    Diagnostics& diagnostics);

} // namespace compiler
} // namespace kernel
