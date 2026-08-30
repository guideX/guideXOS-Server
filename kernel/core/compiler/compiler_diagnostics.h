//
// Bounded diagnostics for the bare-metal compiler bootstrap.
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_MAX_DIAGNOSTICS = 16;
static const uint32_t COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES = 128;

struct SourceLocation {
    uint32_t offset;
    uint32_t line;
    uint32_t column;
};

struct CompilerDiagnostic {
    SourceLocation location;
    const char* message;
    const char* tokenKind;
};

class Diagnostics {
public:
    Diagnostics();

    void error(SourceLocation location, const char* message, const char* tokenKind);
    void error_identifier(SourceLocation location, const char* prefix,
                          const char* identifier, uint32_t identifierBytes,
                          const char* tokenKind);
    void error_identifier_suffix(SourceLocation location, const char* prefix,
                                 const char* identifier, uint32_t identifierBytes,
                                 const char* suffix, const char* tokenKind);
    void error_function_argument_count(SourceLocation location, const char* name,
                                       uint32_t nameBytes, uint32_t expected,
                                       uint32_t actual);
    bool has_error() const;
    uint32_t count() const;
    const CompilerDiagnostic& at(uint32_t index) const;
    bool overflowed() const;

private:
    CompilerDiagnostic m_items[COMPILER_MAX_DIAGNOSTICS];
    char m_messageStorage[COMPILER_MAX_DIAGNOSTICS][COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES];
    uint32_t m_count;
    bool m_overflowed;
};

void print_diagnostics(const Diagnostics& diagnostics);

} // namespace compiler
} // namespace kernel
