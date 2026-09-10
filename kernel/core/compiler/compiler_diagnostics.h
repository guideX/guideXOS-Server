//
// Bounded diagnostics for the bare-metal compiler bootstrap.
//
// This intentionally stores pointers to static messages rather than allocating
// formatted strings.  Diagnostics are deterministic and safe in the kernel.
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_MAX_DIAGNOSTICS = 8;

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
    bool has_error() const;
    uint32_t count() const;
    const CompilerDiagnostic& at(uint32_t index) const;
    bool overflowed() const;

private:
    CompilerDiagnostic m_items[COMPILER_MAX_DIAGNOSTICS];
    uint32_t m_count;
    bool m_overflowed;
};

void print_diagnostics(const Diagnostics& diagnostics);

} // namespace compiler
} // namespace kernel
