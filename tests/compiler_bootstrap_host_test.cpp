// Focused host-side checks for the freestanding compiler bootstrap components.
// These tests do not exercise the VFS or replace the required QEMU proof.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "core/compiler/elf_writer.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>

using namespace kernel::compiler;

namespace {

static bool require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

static bool compile_text(const char* source,
                         FunctionIR* function,
                         uint8_t* code,
                         uint32_t* codeBytes,
                         uint8_t* elf,
                         uint32_t* elfBytes)
{
    Token tokens[COMPILER_MAX_TOKENS] = {};
    Diagnostics diagnostics;
    uint32_t tokenCount = 0;
    if (!lex_source(source, static_cast<uint32_t>(std::strlen(source)), tokens,
                    COMPILER_MAX_TOKENS, &tokenCount, diagnostics)) return false;
    if (!parse_function(source, tokens, tokenCount, function, diagnostics)) return false;
    if (!amd64::emit_function(*function, code, 16, codeBytes)) return false;
    ElfLayout layout = {};
    if (!write_bootstrap_elf(code, *codeBytes, elf, BOOTSTRAP_MAX_ELF_BYTES, &layout)) return false;
    *elfBytes = layout.outputBytes;
    ElfValidationResult validation = {};
    return validate_bootstrap_elf(elf, *elfBytes, layout.imageBase, layout.codeOffset,
                                  code, *codeBytes, &validation);
}

} // namespace

int main()
{
    const char* return42 = "int gx_main(void* ctx) { return 42; }";
    const char* return41 = "int gx_main(void* ctx) { return 41; }";
    FunctionIR function42 = {};
    FunctionIR function41 = {};
    uint8_t code42[16] = {};
    uint8_t code41[16] = {};
    uint8_t elf42[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint8_t elf42Again[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint8_t elf41[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint32_t code42Bytes = 0;
    uint32_t code41Bytes = 0;
    uint32_t elf42Bytes = 0;
    uint32_t elf42AgainBytes = 0;
    uint32_t elf41Bytes = 0;

    if (!require(compile_text(return42, &function42, code42, &code42Bytes,
                              elf42, &elf42Bytes), "return 42 pipeline")) return 1;
    if (!require(function42.returnConstant == 42, "IR return constant 42")) return 1;
    if (!require(code42Bytes == 6 && code42[0] == 0xB8 && code42[1] == 0x2A &&
                 code42[2] == 0 && code42[3] == 0 && code42[4] == 0 && code42[5] == 0xC3,
                 "AMD64 bytes for return 42")) return 1;

    if (!require(compile_text(return42, &function42, code42, &code42Bytes,
                              elf42Again, &elf42AgainBytes), "deterministic pipeline")) return 1;
    if (!require(elf42Bytes == elf42AgainBytes &&
                 std::memcmp(elf42, elf42Again, elf42Bytes) == 0,
                 "identical source produces identical ELF")) return 1;

    if (!require(compile_text(return41, &function41, code41, &code41Bytes,
                              elf41, &elf41Bytes), "return 41 pipeline")) return 1;
    if (!require(function41.returnConstant == 41 && code41[1] == 0x29,
                 "IR and AMD64 immediate for return 41")) return 1;
    if (!require(elf42Bytes == elf41Bytes && std::memcmp(elf42, elf41, elf42Bytes) != 0,
                 "return value changes ELF bytes")) return 1;

    const char* invalid = "int gx_main(void* ctx) { return banana; }";
    Token invalidTokens[COMPILER_MAX_TOKENS] = {};
    Diagnostics invalidDiagnostics;
    uint32_t invalidTokenCount = 0;
    if (!require(lex_source(invalid, static_cast<uint32_t>(std::strlen(invalid)),
                            invalidTokens, COMPILER_MAX_TOKENS, &invalidTokenCount,
                            invalidDiagnostics), "invalid source lexes for parser test")) return 1;
    FunctionIR invalidFunction = {};
    if (!require(!parse_function(invalid, invalidTokens, invalidTokenCount,
                                 &invalidFunction, invalidDiagnostics),
                 "invalid return expression is rejected")) return 1;

    const char* otherFunction = "int other_function(void* ctx) { return 42; }";
    Token otherTokens[COMPILER_MAX_TOKENS] = {};
    Diagnostics otherDiagnostics;
    uint32_t otherTokenCount = 0;
    if (!require(lex_source(otherFunction, static_cast<uint32_t>(std::strlen(otherFunction)),
                            otherTokens, COMPILER_MAX_TOKENS, &otherTokenCount,
                            otherDiagnostics), "other function lexes for parser test")) return 1;
    if (!require(!parse_function(otherFunction, otherTokens, otherTokenCount,
                                 &invalidFunction, otherDiagnostics),
                 "unsupported function name is rejected")) return 1;

    uint8_t malformed[BOOTSTRAP_MAX_ELF_BYTES] = {};
    std::memcpy(malformed, elf42, elf42Bytes);
    malformed[0] = 0;
    ElfValidationResult malformedResult = {};
    if (!require(!validate_bootstrap_elf(malformed, elf42Bytes, BOOTSTRAP_IMAGE_BASE,
                                         BOOTSTRAP_CODE_OFFSET, code42, code42Bytes,
                                         &malformedResult), "malformed ELF is rejected")) return 1;

    std::puts("compiler_bootstrap_host_test: PASS");
    return 0;
}
