#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "core/compiler/elf_writer.h"
#include "arch/arm64/compiler_backend.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>

using namespace kernel::compiler;

static uint32_t read_u32(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

int main()
{
    const char source[] = "int gx_main(void* ctx) { log(ctx, \"Phase 11 ARM64 source build.\"); log(ctx, \"computation: PASS\"); return 42; }";
    Token tokens[COMPILER_MAX_TOKENS] = {};
    Diagnostics diagnostics;
    uint32_t tokenCount = 0;
    if (!lex_source(source, static_cast<uint32_t>(std::strlen(source)), tokens, COMPILER_MAX_TOKENS, &tokenCount, diagnostics)) return 1;
    FunctionIR function = {};
    if (!parse_function(source, tokens, tokenCount, &function, diagnostics) || !function.hasLogCall) return 2;
    uint8_t code[arm64::ARM64_MAX_BOOTSTRAP_BYTES] = {};
    uint32_t codeBytes = 0;
    if (!arm64::emit_function(function, code, sizeof(code), &codeBytes) || function.logCount != 2 || codeBytes <= 40 ||
        code[0] != 0xF3 || code[1] != 0x7B || code[2] != 0xBF || code[3] != 0xA9) return 3;
    if (codeBytes < 64 || read_u32(code, 48) != 0x52800540 || read_u32(code, 52) != 0x72A00000) return 3;
    uint8_t elf[BOOTSTRAP_MAX_ELF_BYTES] = {};
    ElfLayout layout = {};
    if (!write_bootstrap_elf_for_target(code, codeBytes, CompilerTarget::Arm64, elf, sizeof(elf), &layout)) return 4;
    if (elf[18] != 183 || elf[19] != 0 || layout.imageBase != UINT64_C(0x50000000)) return 5;
    ElfValidationResult validation = {};
    if (!validate_bootstrap_elf_for_target(elf, layout.outputBytes, CompilerTarget::Arm64,
                                           layout.imageBase, layout.codeOffset, code, codeBytes, &validation)) return 6;
    uint8_t amd64Code[amd64::AMD64_MAX_BOOTSTRAP_BYTES] = {};
    uint32_t amd64Bytes = 0;
    if (!amd64::emit_function(function, amd64Code, sizeof(amd64Code), &amd64Bytes) || amd64Bytes <= 18) return 7;
    ElfLayout amd64Layout = {};
    uint8_t amd64Elf[BOOTSTRAP_MAX_ELF_BYTES] = {};
    if (!write_bootstrap_elf_for_target(amd64Code, amd64Bytes, CompilerTarget::Amd64,
                                        amd64Elf, sizeof(amd64Elf), &amd64Layout) || amd64Elf[18] != 62) return 8;
    ElfValidationResult amd64Validation = {};
    if (!validate_bootstrap_elf_for_target(amd64Elf, amd64Layout.outputBytes, CompilerTarget::Amd64,
                                           amd64Layout.imageBase, amd64Layout.codeOffset,
                                           amd64Code, amd64Bytes, &amd64Validation)) return 9;
    std::puts("compiler_arm64_host_test: PASS");
    return 0;
}
