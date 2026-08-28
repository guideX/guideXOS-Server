// Focused host-side checks for the bounded bootstrap compiler language.
// These tests exercise the lexer/parser/IR/backend without requiring QEMU.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "core/compiler/elf_writer.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace kernel::compiler;

namespace {

static bool require(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool parse_text(const char* source, FunctionIR* function, Diagnostics* diagnostics)
{
    if (!source || !function || !diagnostics) return false;
    Token tokens[COMPILER_MAX_TOKENS] = {};
    uint32_t tokenCount = 0;
    if (!lex_source(source, static_cast<uint32_t>(std::strlen(source)), tokens,
                    COMPILER_MAX_TOKENS, &tokenCount, *diagnostics)) return false;
    return parse_function(source, tokens, tokenCount, function, *diagnostics);
}

static bool flatten_strings(const FunctionIR& function, uint8_t* data,
                            uint32_t capacity, uint32_t* bytes)
{
    uint32_t offset = 0;
    for (uint32_t i = 0; i < function.stringCount; ++i) {
        if (function.stringOffsets[i] != offset || offset + function.strings[i].bytes + 1 > capacity) return false;
        for (uint32_t j = 0; j < function.strings[i].bytes; ++j)
            data[offset + j] = static_cast<uint8_t>(function.strings[i].data[j]);
        data[offset + function.strings[i].bytes] = 0;
        offset += function.strings[i].bytes + 1;
    }
    if (bytes) *bytes = offset;
    return offset == function.stringDataBytes;
}

static bool compile_text(const char* source, FunctionIR* function,
                         uint8_t* code, uint32_t* codeBytes,
                         uint8_t* elf, uint32_t* elfBytes)
{
    Diagnostics diagnostics;
    if (!parse_text(source, function, &diagnostics)) return false;
    uint8_t data[COMPILER_MAX_TOTAL_STRING_DATA] = {};
    uint32_t dataBytes = 0;
    if (!flatten_strings(*function, data, sizeof(data), &dataBytes)) return false;
    const uint64_t dataAddress = dataBytes == 0 ? 0 : BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_DATA_OFFSET;
    if (!amd64::emit_function(*function, dataAddress, code, COMPILER_MAX_CODE_BYTES, codeBytes)) return false;
    ElfLayout layout = {};
    if (!write_bootstrap_elf(code, *codeBytes, data, dataBytes, elf,
                             BOOTSTRAP_MAX_ELF_BYTES, &layout)) return false;
    *elfBytes = layout.outputBytes;
    ElfValidationResult validation = {};
    return validate_bootstrap_elf(elf, *elfBytes, layout.imageBase, layout.codeOffset,
                                  code, *codeBytes, &validation, data, dataBytes);
}

static bool has_bytes(const uint8_t* bytes, uint32_t count,
                      const uint8_t* needle, uint32_t needleCount)
{
    for (uint32_t i = 0; i + needleCount <= count; ++i) {
        bool same = true;
        for (uint32_t j = 0; j < needleCount; ++j) if (bytes[i + j] != needle[j]) same = false;
        if (same) return true;
    }
    return false;
}

} // namespace

int main()
{
    const char* literal = "int gx_main(void* ctx) { return 42; }";
    FunctionIR literalFunction = {};
    Diagnostics literalDiagnostics;
    Token literalTokens[COMPILER_MAX_TOKENS] = {};
    uint32_t literalTokenCount = 0;
    if (!require(lex_source(literal, static_cast<uint32_t>(std::strlen(literal)), literalTokens,
                            COMPILER_MAX_TOKENS, &literalTokenCount, literalDiagnostics),
                 "literal source lexes")) return 1;
    if (!require(literalTokens[0].kind == TokenKind::KeywordInt &&
                 literalTokens[1].kind == TokenKind::KeywordGxMain &&
                 literalTokens[3].kind == TokenKind::KeywordVoid &&
                 literalTokens[8].kind == TokenKind::KeywordReturn,
                 "keyword tokens are explicit")) return 1;
    if (!require(parse_function(literal, literalTokens, literalTokenCount,
                                &literalFunction, literalDiagnostics),
                 "literal source parses")) return 1;
    if (!require(literalFunction.returnConstantValid && literalFunction.returnConstant == 42 &&
                 literalFunction.localCount == 0 && literalFunction.statementCount == 1,
                 "literal IR remains compatible")) return 1;

    uint8_t literalCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t literalCodeBytes = 0;
    if (!require(amd64::emit_function(literalFunction, literalCode, sizeof(literalCode), &literalCodeBytes) &&
                 literalCodeBytes == 6 && literalCode[0] == 0xB8 && literalCode[1] == 42 && literalCode[5] == 0xC3,
                 "literal fast path emits legacy bytes")) return 1;

    const char* expressionSource = "int gx_main(gx_app_context* ctx) { return 20 + 22; }";
    FunctionIR expressionFunction = {};
    uint8_t expressionCode[COMPILER_MAX_CODE_BYTES] = {};
    uint8_t expressionElf[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint32_t expressionCodeBytes = 0;
    uint32_t expressionElfBytes = 0;
    if (!require(compile_text(expressionSource, &expressionFunction, expressionCode,
                              &expressionCodeBytes, expressionElf, &expressionElfBytes),
                 "literal expression pipeline")) return 1;
    if (!require(expressionFunction.returnConstantValid && expressionFunction.returnConstant == 42 &&
                 expressionFunction.expressionCount == 3 && expressionCodeBytes > 6,
                 "literal expression becomes arithmetic IR")) return 1;

    const char* localsSource =
        "int gx_main(gx_app_context* ctx) { int x = 20; int y = 22; int result = x + y; return result; }";
    FunctionIR locals = {};
    uint8_t localsCode[COMPILER_MAX_CODE_BYTES] = {};
    uint8_t localsElf[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint32_t localsCodeBytes = 0;
    uint32_t localsElfBytes = 0;
    if (!require(compile_text(localsSource, &locals, localsCode, &localsCodeBytes,
                              localsElf, &localsElfBytes), "local variable pipeline")) return 1;
    if (!require(locals.localCount == 3 && locals.statementCount == 4 &&
                 !locals.returnConstantValid && locals.returnExpression != COMPILER_INVALID_INDEX,
                 "locals and load references are represented")) return 1;
    amd64::FrameLayout frame = {};
    if (!require(amd64::calculate_frame_layout(3, &frame) && frame.frameBytes == 64 &&
                 frame.contextDisplacement == -20 && frame.localBytes == 12,
                 "stack slots and frame alignment are deterministic")) return 1;
    const uint8_t prologue[] = {0x55, 0x48, 0x89, 0xE5};
    const uint8_t loadLocal[] = {0x8B, 0x85};
    if (!require(has_bytes(localsCode, localsCodeBytes, prologue, sizeof(prologue)) &&
                 has_bytes(localsCode, localsCodeBytes, loadLocal, sizeof(loadLocal)),
                 "AMD64 output contains frame and local loads")) return 1;

    const char* assignmentSource =
        "int gx_main(gx_app_context* ctx) { int value = 10; value = value * 4; value = value + 2; return value; }";
    FunctionIR assignment = {};
    Diagnostics assignmentDiagnostics;
    if (!require(parse_text(assignmentSource, &assignment, &assignmentDiagnostics) &&
                 assignment.localCount == 1 && assignment.statementCount == 4,
                 "assignment statements resolve")) return 1;

    const char* precedenceSource = "int gx_main(void* ctx) { return (2 + 5) * 6; }";
    FunctionIR precedence = {};
    Diagnostics precedenceDiagnostics;
    if (!require(parse_text(precedenceSource, &precedence, &precedenceDiagnostics) &&
                 precedence.returnConstantValid && precedence.returnConstant == 42,
                 "precedence and parentheses evaluate deterministically")) return 1;

    const char* unarySource = "int gx_main(void* ctx) { int x = -20; return -x + 22; }";
    FunctionIR unary = {};
    Diagnostics unaryDiagnostics;
    if (!require(parse_text(unarySource, &unary, &unaryDiagnostics) && unary.localCount == 1 &&
                 unary.statementCount == 2 && !unary.returnConstantValid,
                 "unary negation and local use parse")) return 1;

    const char* logsSource =
        "int gx_main(gx_app_context* ctx) { log(ctx, \"First\"); log(ctx, \"Second\"); log(ctx, \"Third\"); return 42; }";
    FunctionIR logs = {};
    uint8_t logsCode[COMPILER_MAX_CODE_BYTES] = {};
    uint8_t logsElf[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint32_t logsCodeBytes = 0;
    uint32_t logsElfBytes = 0;
    if (!require(compile_text(logsSource, &logs, logsCode, &logsCodeBytes,
                              logsElf, &logsElfBytes), "multiple host-call pipeline")) return 1;
    if (!require(logs.hasHostLog && logs.stringCount == 3 && logs.stringOffsets[0] == 0 &&
                 logs.stringOffsets[1] == 6 && logs.stringOffsets[2] == 13 &&
                 logs.stringDataBytes == 19 && logsCodeBytes > 40,
                 "multiple logs use separate deterministic string slots")) return 1;

    const char* unknown = "int gx_main(gx_app_context* ctx) { return missing + 1; }";
    FunctionIR unknownFunction = {};
    Diagnostics unknownDiagnostics;
    if (!require(!parse_text(unknown, &unknownFunction, &unknownDiagnostics) &&
                 unknownDiagnostics.count() != 0 &&
                 std::strcmp(unknownDiagnostics.at(0).message, "unknown identifier 'missing'") == 0,
                 "unknown identifiers are rejected with names")) return 1;

    const char* duplicate = "int gx_main(gx_app_context* ctx) { int x = 1; int x = 2; return x; }";
    FunctionIR duplicateFunction = {};
    Diagnostics duplicateDiagnostics;
    if (!require(!parse_text(duplicate, &duplicateFunction, &duplicateDiagnostics) &&
                 std::strcmp(duplicateDiagnostics.at(0).message, "duplicate local 'x'") == 0,
                 "duplicate locals are rejected with names")) return 1;

    std::string oversized = "int gx_main(void* ctx) { int ";
    oversized.append(64, 'x');
    oversized += " = 1; return 1; }";
    FunctionIR oversizedFunction = {};
    Diagnostics oversizedDiagnostics;
    if (!require(!parse_text(oversized.c_str(), &oversizedFunction, &oversizedDiagnostics),
                 "oversized identifiers are rejected")) return 1;

    std::string nested = "int gx_main(void* ctx) { return ";
    nested.append(17, '(');
    nested += "1";
    nested.append(17, ')');
    nested += "; }";
    FunctionIR nestedFunction = {};
    Diagnostics nestedDiagnostics;
    if (!require(!parse_text(nested.c_str(), &nestedFunction, &nestedDiagnostics) &&
                 std::strcmp(nestedDiagnostics.at(0).message, "expression nesting limit exceeded") == 0,
                 "expression nesting is bounded")) return 1;

    std::string tooManyLocals = "int gx_main(void* ctx) {";
    for (uint32_t i = 0; i < COMPILER_MAX_LOCALS + 1; ++i) {
        tooManyLocals += " int local" + std::to_string(i) + " = 0;";
    }
    tooManyLocals += " return 0; }";
    FunctionIR tooManyLocalsFunction = {};
    Diagnostics tooManyLocalsDiagnostics;
    if (!require(!parse_text(tooManyLocals.c_str(), &tooManyLocalsFunction, &tooManyLocalsDiagnostics) &&
                 std::strcmp(tooManyLocalsDiagnostics.at(0).message, "too many local variables") == 0,
                 "local capacity is bounded")) return 1;

    std::string tooManyStatements = "int gx_main(void* ctx) { int value = 0;";
    for (uint32_t i = 0; i < COMPILER_MAX_STATEMENTS; ++i) tooManyStatements += " value = 0;";
    tooManyStatements += " return value; }";
    FunctionIR tooManyStatementsFunction = {};
    Diagnostics tooManyStatementsDiagnostics;
    if (!require(!parse_text(tooManyStatements.c_str(), &tooManyStatementsFunction,
                              &tooManyStatementsDiagnostics) &&
                 std::strcmp(tooManyStatementsDiagnostics.at(0).message, "too many statements") == 0,
                 "statement capacity is bounded")) return 1;

    std::string tooManyExpressions = "int gx_main(void* ctx) { return ";
    for (uint32_t i = 0; i < COMPILER_MAX_EXPRESSION_NODES / 2 + 1; ++i) {
        if (i != 0) tooManyExpressions += " + ";
        tooManyExpressions += "1";
    }
    tooManyExpressions += "; }";
    FunctionIR tooManyExpressionsFunction = {};
    Diagnostics tooManyExpressionsDiagnostics;
    if (!require(!parse_text(tooManyExpressions.c_str(), &tooManyExpressionsFunction,
                              &tooManyExpressionsDiagnostics) &&
                 std::strcmp(tooManyExpressionsDiagnostics.at(0).message, "too many expression nodes") == 0,
                 "expression-node capacity is bounded")) return 1;

    std::string tooManyStrings = "int gx_main(gx_app_context* ctx) {";
    for (uint32_t i = 0; i < COMPILER_MAX_STRING_LITERALS + 1; ++i) {
        tooManyStrings += " log(ctx, \"s" + std::to_string(i) + "\");";
    }
    tooManyStrings += " return 0; }";
    FunctionIR tooManyStringsFunction = {};
    Diagnostics tooManyStringsDiagnostics;
    if (!require(!parse_text(tooManyStrings.c_str(), &tooManyStringsFunction,
                              &tooManyStringsDiagnostics) &&
                 std::strcmp(tooManyStringsDiagnostics.at(0).message, "too many string literals") == 0,
                 "string capacity is bounded")) return 1;

    uint8_t deterministicCodeA[COMPILER_MAX_CODE_BYTES] = {};
    uint8_t deterministicCodeB[COMPILER_MAX_CODE_BYTES] = {};
    uint8_t deterministicElfA[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint8_t deterministicElfB[BOOTSTRAP_MAX_ELF_BYTES] = {};
    FunctionIR deterministicA = {};
    FunctionIR deterministicB = {};
    uint32_t deterministicCodeBytesA = 0, deterministicCodeBytesB = 0;
    uint32_t deterministicElfBytesA = 0, deterministicElfBytesB = 0;
    if (!require(compile_text(logsSource, &deterministicA, deterministicCodeA,
                              &deterministicCodeBytesA, deterministicElfA, &deterministicElfBytesA) &&
                 compile_text(logsSource, &deterministicB, deterministicCodeB,
                              &deterministicCodeBytesB, deterministicElfB, &deterministicElfBytesB) &&
                 deterministicCodeBytesA == deterministicCodeBytesB &&
                 deterministicElfBytesA == deterministicElfBytesB &&
                 std::memcmp(deterministicElfA, deterministicElfB, deterministicElfBytesA) == 0,
                 "identical multi-string source produces identical ELF")) return 1;

    std::puts("compiler_bootstrap_host_test: PASS");
    return 0;
}
