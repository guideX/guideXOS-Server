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
    : m_count(0), m_overflowed(false), m_defaultKind(CompilerDiagnosticKind::Unknown)
{
}

namespace {

static void copy_message(char* output, uint32_t capacity, const char* input)
{
    if (!output || capacity == 0) return;
    uint32_t i = 0;
    if (input) while (i + 1 < capacity && input[i]) {
        output[i] = input[i];
        ++i;
    }
    output[i] = '\0';
}

static bool text_equals(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    uint32_t i = 0;
    while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == right[i];
}

} // namespace

void Diagnostics::set_default_kind(CompilerDiagnosticKind kind)
{
    m_defaultKind = kind;
}

void Diagnostics::error(SourceLocation location, const char* message, const char* tokenKind)
{
    CompilerDiagnosticKind kind = m_defaultKind;
    // The parser already marks type-checking failures with this token kind.
    if (kind == CompilerDiagnosticKind::Syntax && text_equals(tokenKind, "type"))
        kind = CompilerDiagnosticKind::Semantic;
    error_as(kind, location, message, tokenKind);
}

void Diagnostics::error_as(CompilerDiagnosticKind kind, SourceLocation location,
                           const char* message, const char* tokenKind)
{
    if (m_count >= COMPILER_MAX_DIAGNOSTICS) {
        m_overflowed = true;
        return;
    }

    m_items[m_count].location = location;
    copy_message(m_messageStorage[m_count], sizeof(m_messageStorage[m_count]),
                 message ? message : "compiler error");
    m_items[m_count].message = m_messageStorage[m_count];
    m_items[m_count].tokenKind = tokenKind ? tokenKind : "unknown";
    m_items[m_count].kind = kind;
    ++m_count;
}

void Diagnostics::error_identifier(SourceLocation location, const char* prefix,
                                   const char* identifier, uint32_t identifierBytes,
                                   const char* tokenKind)
{
    error_identifier_as(CompilerDiagnosticKind::Semantic, location, prefix,
                        identifier, identifierBytes, tokenKind);
}

void Diagnostics::error_identifier_as(CompilerDiagnosticKind kind, SourceLocation location,
                                      const char* prefix, const char* identifier,
                                      uint32_t identifierBytes, const char* tokenKind)
{
    if (m_count >= COMPILER_MAX_DIAGNOSTICS) {
        m_overflowed = true;
        return;
    }
    char* output = m_messageStorage[m_count];
    uint32_t written = 0;
    if (prefix) while (written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES && prefix[written]) output[written] = prefix[written], ++written;
    if (written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES) output[written++] = '\'';
    for (uint32_t i = 0; i < identifierBytes && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES; ++i)
        output[written++] = identifier ? identifier[i] : '?';
    if (written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES) output[written++] = '\'';
    output[written] = '\0';
    m_items[m_count].location = location;
    m_items[m_count].message = output;
    m_items[m_count].tokenKind = tokenKind ? tokenKind : "identifier";
    m_items[m_count].kind = kind;
    ++m_count;
}

void Diagnostics::error_identifier_suffix(SourceLocation location, const char* prefix,
                                          const char* identifier, uint32_t identifierBytes,
                                          const char* suffix, const char* tokenKind)
{
    error_identifier_suffix_as(CompilerDiagnosticKind::Semantic, location, prefix,
                               identifier, identifierBytes, suffix, tokenKind);
}

void Diagnostics::error_identifier_suffix_as(CompilerDiagnosticKind kind,
                                             SourceLocation location, const char* prefix,
                                             const char* identifier, uint32_t identifierBytes,
                                             const char* suffix, const char* tokenKind)
{
    if (m_count >= COMPILER_MAX_DIAGNOSTICS) {
        m_overflowed = true;
        return;
    }
    char* output = m_messageStorage[m_count];
    uint32_t written = 0;
    if (prefix) while (written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES && prefix[written]) output[written] = prefix[written], ++written;
    if (written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES) output[written++] = '\'';
    for (uint32_t i = 0; i < identifierBytes && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES; ++i)
        output[written++] = identifier ? identifier[i] : '?';
    if (written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES) output[written++] = '\'';
    if (suffix) for (uint32_t i = 0; suffix[i] && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES; ++i)
        output[written++] = suffix[i];
    output[written] = '\0';
    m_items[m_count].location = location;
    m_items[m_count].message = output;
    m_items[m_count].tokenKind = tokenKind ? tokenKind : "identifier";
    m_items[m_count].kind = kind;
    ++m_count;
}

void Diagnostics::error_function_argument_count(SourceLocation location, const char* name,
                                                uint32_t nameBytes, uint32_t expected,
                                                uint32_t actual)
{
    if (m_count >= COMPILER_MAX_DIAGNOSTICS) {
        m_overflowed = true;
        return;
    }
    char* output = m_messageStorage[m_count];
    uint32_t written = 0;
    const char prefix[] = "function '";
    const char middle[] = "' expects ";
    const char got[] = " arguments, got ";
    for (uint32_t i = 0; prefix[i] && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES; ++i) output[written++] = prefix[i];
    for (uint32_t i = 0; i < nameBytes && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES; ++i) output[written++] = name ? name[i] : '?';
    for (uint32_t i = 0; middle[i] && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES; ++i) output[written++] = middle[i];
    char digits[11];
    uint32_t digitCount = 0;
    if (expected == 0) digits[digitCount++] = '0';
    else {
        uint32_t value = expected;
        while (value != 0 && digitCount < sizeof(digits)) { digits[digitCount++] = static_cast<char>('0' + value % 10); value /= 10; }
        while (digitCount != 0 && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES) output[written++] = digits[--digitCount];
    }
    if (expected == 0 && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES) output[written++] = '0';
    for (uint32_t i = 0; got[i] && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES; ++i) output[written++] = got[i];
    digitCount = 0;
    if (actual == 0) digits[digitCount++] = '0';
    else {
        uint32_t value = actual;
        while (value != 0 && digitCount < sizeof(digits)) { digits[digitCount++] = static_cast<char>('0' + value % 10); value /= 10; }
        while (digitCount != 0 && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES) output[written++] = digits[--digitCount];
    }
    if (actual == 0 && written + 1 < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES) output[written++] = '0';
    output[written] = '\0';
    m_items[m_count].location = location;
    m_items[m_count].message = output;
    m_items[m_count].tokenKind = "call";
    m_items[m_count].kind = CompilerDiagnosticKind::Semantic;
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

const char* compiler_diagnostic_kind_name(CompilerDiagnosticKind kind)
{
    switch (kind) {
    case CompilerDiagnosticKind::Lexical: return "lexical";
    case CompilerDiagnosticKind::Syntax: return "syntax";
    case CompilerDiagnosticKind::Unsupported: return "unsupported";
    case CompilerDiagnosticKind::Semantic: return "semantic";
    case CompilerDiagnosticKind::ResourceLimit: return "resource limit";
    case CompilerDiagnosticKind::CodeGeneration: return "code generation";
    case CompilerDiagnosticKind::ObjectGeneration: return "object generation";
    case CompilerDiagnosticKind::Linker: return "linker";
    case CompilerDiagnosticKind::Artifact: return "artifact";
    case CompilerDiagnosticKind::Internal: return "compiler internal";
    case CompilerDiagnosticKind::SourceRead: return "source read";
    case CompilerDiagnosticKind::ProjectValidation: return "project validation";
    default: return "unknown";
    }
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
        if (diagnostic.kind != CompilerDiagnosticKind::Unknown) {
            serial::putc('[');
            serial::puts(compiler_diagnostic_kind_name(diagnostic.kind));
            serial::puts("] ");
        }
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
