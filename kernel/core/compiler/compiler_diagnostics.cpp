//
// Bounded diagnostics for the bare-metal compiler bootstrap.
//

#include "compiler_diagnostics.h"

#include "kernel/serial_debug.h"

namespace kernel {
namespace compiler {
namespace {

static void put_decimal(uint32_t value)
{
    char digits[11];
    uint32_t count = 0;
    if (value == 0) {
        serial::putc('0');
        return;
    }

    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (count != 0) serial::putc(digits[--count]);
}

} // namespace

Diagnostics::Diagnostics()
    : m_count(0), m_overflowed(false)
{
}

void Diagnostics::error(SourceLocation location, const char* message, const char* tokenKind)
{
    if (m_count >= COMPILER_MAX_DIAGNOSTICS) {
        m_overflowed = true;
        return;
    }

    m_items[m_count].location = location;
    m_items[m_count].message = message ? message : "compiler error";
    m_items[m_count].tokenKind = tokenKind ? tokenKind : "unknown";
    ++m_count;
}

bool Diagnostics::has_error() const
{
    return m_count != 0 || m_overflowed;
}

uint32_t Diagnostics::count() const
{
    return m_count;
}

const CompilerDiagnostic& Diagnostics::at(uint32_t index) const
{
    return m_items[index < m_count ? index : 0];
}

bool Diagnostics::overflowed() const
{
    return m_overflowed;
}

void print_diagnostics(const Diagnostics& diagnostics)
{
    for (uint32_t i = 0; i < diagnostics.count(); ++i) {
        const CompilerDiagnostic& diagnostic = diagnostics.at(i);
        serial::puts("error: line ");
        put_decimal(diagnostic.location.line);
        serial::puts(", column ");
        put_decimal(diagnostic.location.column);
        serial::puts(", offset ");
        put_decimal(diagnostic.location.offset);
        serial::puts(": ");
        serial::puts(diagnostic.message);
        serial::puts(" (token=");
        serial::puts(diagnostic.tokenKind);
        serial::puts(")\n");
    }
    if (diagnostics.overflowed()) {
        serial::puts("error: diagnostic limit exceeded\n");
    }
}

} // namespace compiler
} // namespace kernel
