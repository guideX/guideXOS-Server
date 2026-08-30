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

static bool has_expression_kind(const FunctionIR& function, ExpressionKind kind)
{
    for (uint32_t i = 0; i < function.expressionCount; ++i)
        if (function.expressions[i].kind == kind) return true;
    return false;
}

static bool has_statement_kind(const FunctionIR& function, StatementKind kind)
{
    for (uint32_t i = 0; i < function.statementCount; ++i)
        if (function.statements[i].kind == kind) return true;
    return false;
}

static bool has_backward_unconditional_branch(const uint8_t* bytes, uint32_t count)
{
    if (!bytes) return false;
    for (uint32_t i = 0; i + 5U <= count; ++i) {
        if (bytes[i] != 0xE9) continue;
        const int32_t displacement =
            static_cast<int32_t>(static_cast<uint32_t>(bytes[i + 1]) |
                                 (static_cast<uint32_t>(bytes[i + 2]) << 8) |
                                 (static_cast<uint32_t>(bytes[i + 3]) << 16) |
                                 (static_cast<uint32_t>(bytes[i + 4]) << 24));
        const int64_t target = static_cast<int64_t>(i + 5U) + displacement;
        if (displacement < 0 && target >= 0 && target < static_cast<int64_t>(i)) return true;
    }
    return false;
}

static uint32_t count_unconditional_branches(const uint8_t* bytes, uint32_t count,
                                             bool backward)
{
    uint32_t matches = 0;
    if (!bytes) return 0;
    for (uint32_t i = 0; i + 5U <= count; ++i) {
        if (bytes[i] != 0xE9) continue;
        const int32_t displacement =
            static_cast<int32_t>(static_cast<uint32_t>(bytes[i + 1]) |
                                 (static_cast<uint32_t>(bytes[i + 2]) << 8) |
                                 (static_cast<uint32_t>(bytes[i + 3]) << 16) |
                                 (static_cast<uint32_t>(bytes[i + 4]) << 24));
        const int64_t target = static_cast<int64_t>(i + 5U) + displacement;
        const bool isBackward = displacement < 0 && target >= 0 && target < static_cast<int64_t>(i);
        const bool isForward = displacement >= 0 && target > static_cast<int64_t>(i + 5U) &&
                               target < static_cast<int64_t>(count);
        if ((backward && isBackward) || (!backward && isForward)) ++matches;
    }
    return matches;
}

static bool has_forward_conditional_bypass(const uint8_t* bytes, uint32_t count,
                                           uint8_t secondOpcode)
{
    if (!bytes) return false;
    for (uint32_t i = 0; i + 6 <= count; ++i) {
        if (bytes[i] != 0x0F || bytes[i + 1] != secondOpcode) continue;
        const int32_t displacement =
            static_cast<int32_t>(static_cast<uint32_t>(bytes[i + 2]) |
                                 (static_cast<uint32_t>(bytes[i + 3]) << 8) |
                                 (static_cast<uint32_t>(bytes[i + 4]) << 16) |
                                 (static_cast<uint32_t>(bytes[i + 5]) << 24));
        const int64_t target = static_cast<int64_t>(i + 6) + displacement;
        if (target > static_cast<int64_t>(i + 6) && target < static_cast<int64_t>(count)) return true;
    }
    return false;
}

static bool branch_skips_needle(const uint8_t* bytes, uint32_t count,
                                uint8_t secondOpcode, const uint8_t* needle,
                                uint32_t needleCount)
{
    if (!bytes || !needle || needleCount == 0) return false;
    for (uint32_t i = 0; i + 6 <= count; ++i) {
        if (bytes[i] != 0x0F || bytes[i + 1] != secondOpcode) continue;
        const int32_t displacement =
            static_cast<int32_t>(static_cast<uint32_t>(bytes[i + 2]) |
                                 (static_cast<uint32_t>(bytes[i + 3]) << 8) |
                                 (static_cast<uint32_t>(bytes[i + 4]) << 16) |
                                 (static_cast<uint32_t>(bytes[i + 5]) << 24));
        const int64_t target = static_cast<int64_t>(i + 6) + displacement;
        if (target <= static_cast<int64_t>(i + 6) || target > static_cast<int64_t>(count)) continue;
        for (uint32_t j = i + 6; j + needleCount <= count; ++j) {
            bool same = true;
            for (uint32_t k = 0; k < needleCount; ++k) if (bytes[j + k] != needle[k]) same = false;
            if (same && target > static_cast<int64_t>(j + needleCount)) return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    const char* literal = "int gx_main(void* ctx) { return 42; }";
    static FunctionIR literalFunction = {};
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

    const char* whileLex = "while whilex while (";
    static Token whileTokens[COMPILER_MAX_TOKENS] = {};
    uint32_t whileTokenCount = 0;
    Diagnostics whileLexDiagnostics;
    if (!require(lex_source(whileLex, static_cast<uint32_t>(std::strlen(whileLex)), whileTokens,
                            COMPILER_MAX_TOKENS, &whileTokenCount, whileLexDiagnostics) &&
                 whileTokens[0].kind == TokenKind::KeywordWhile &&
                 whileTokens[1].kind == TokenKind::Identifier && whileTokens[1].length == 6 &&
                 whileTokens[2].kind == TokenKind::KeywordWhile &&
                 whileTokens[3].kind == TokenKind::LeftParen && whileTokens[3].location.column == 20,
                 "while lexing preserves keyword boundaries and locations")) return 1;
    const char* malformedWhileLex = "while @";
    Diagnostics malformedWhileLexDiagnostics;
    uint32_t malformedWhileTokenCount = 0;
    if (!require(!lex_source(malformedWhileLex, static_cast<uint32_t>(std::strlen(malformedWhileLex)),
                             whileTokens, COMPILER_MAX_TOKENS, &malformedWhileTokenCount,
                             malformedWhileLexDiagnostics) && malformedWhileLexDiagnostics.count() != 0 &&
                 malformedWhileLexDiagnostics.at(0).location.column == 7,
                 "malformed while-adjacent syntax is source located")) return 1;
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
    static FunctionIR expressionFunction = {};
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
    static FunctionIR locals = {};
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
    static FunctionIR assignment = {};
    Diagnostics assignmentDiagnostics;
    if (!require(parse_text(assignmentSource, &assignment, &assignmentDiagnostics) &&
                 assignment.localCount == 1 && assignment.statementCount == 4,
                 "assignment statements resolve")) return 1;

    const char* precedenceSource = "int gx_main(void* ctx) { return (2 + 5) * 6; }";
    static FunctionIR precedence = {};
    Diagnostics precedenceDiagnostics;
    if (!require(parse_text(precedenceSource, &precedence, &precedenceDiagnostics) &&
                 precedence.returnConstantValid && precedence.returnConstant == 42,
                 "precedence and parentheses evaluate deterministically")) return 1;

    const char* unarySource = "int gx_main(void* ctx) { int x = -20; return -x + 22; }";
    static FunctionIR unary = {};
    Diagnostics unaryDiagnostics;
    if (!require(parse_text(unarySource, &unary, &unaryDiagnostics) && unary.localCount == 1 &&
                 unary.statementCount == 2 && !unary.returnConstantValid,
                 "unary negation and local use parse")) return 1;

    const char* comparisonTokens =
        "int gx_main(void* ctx) { if (1 <= 2) { return 1; } else { return 0; } }";
    Token comparisonTokenBuffer[COMPILER_MAX_TOKENS] = {};
    uint32_t comparisonTokenCount = 0;
    Diagnostics comparisonTokenDiagnostics;
    if (!require(lex_source(comparisonTokens, static_cast<uint32_t>(std::strlen(comparisonTokens)),
                            comparisonTokenBuffer, COMPILER_MAX_TOKENS, &comparisonTokenCount,
                            comparisonTokenDiagnostics), "comparison source lexes")) return 1;
    if (!require(comparisonTokenBuffer[8].kind == TokenKind::KeywordIf &&
                 comparisonTokenBuffer[10].kind == TokenKind::Integer &&
                 comparisonTokenBuffer[11].kind == TokenKind::LessEqual &&
                 comparisonTokenBuffer[12].kind == TokenKind::Integer &&
                 comparisonTokenBuffer[13].length == 1 &&
                 comparisonTokenBuffer[19].kind == TokenKind::KeywordElse,
                 "comparison tokens use longest matching and locations")) return 1;

    const char* comparisonsSource =
        "int gx_main(void* ctx) { return (1 == 1) + (1 != 2) + (1 < 2) + "
        "(2 <= 2) + (3 > 2) + (3 >= 3); }";
    static FunctionIR comparisons = {};
    Diagnostics comparisonsDiagnostics;
    if (!require(parse_text(comparisonsSource, &comparisons, &comparisonsDiagnostics) &&
                 comparisons.returnConstantValid && comparisons.returnConstant == 6 &&
                 has_expression_kind(comparisons, ExpressionKind::Equal) &&
                 has_expression_kind(comparisons, ExpressionKind::NotEqual) &&
                 has_expression_kind(comparisons, ExpressionKind::Less) &&
                 has_expression_kind(comparisons, ExpressionKind::LessEqual) &&
                 has_expression_kind(comparisons, ExpressionKind::Greater) &&
                 has_expression_kind(comparisons, ExpressionKind::GreaterEqual),
                 "all comparison IR operations and precedence are represented")) return 1;

    const char* signedComparisonSource = "int gx_main(void* ctx) { return -1 < 1; }";
    static FunctionIR signedComparison = {};
    Diagnostics signedComparisonDiagnostics;
    if (!require(parse_text(signedComparisonSource, &signedComparison, &signedComparisonDiagnostics) &&
                 signedComparison.returnConstantValid && signedComparison.returnConstant == 1,
                 "signed comparison constant folding is signed")) return 1;

    const char* conditionalSource =
        "int gx_main(gx_app_context* ctx) { int x = 20; int y = 22; int result = x + y; "
        "if (result == 42) { log(ctx, \"answer\"); return result; } "
        "else { log(ctx, \"unexpected\"); return -1; } }";
    static FunctionIR conditional = {};
    uint8_t conditionalCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t conditionalCodeBytes = 0;
    Diagnostics conditionalDiagnostics;
    if (!require(parse_text(conditionalSource, &conditional, &conditionalDiagnostics) &&
                 conditional.blockCount == 3 && conditional.returnCount == 2 &&
                 !conditional.returnConstantValid &&
                 amd64::emit_function(conditional, BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_DATA_OFFSET,
                                       conditionalCode, sizeof(conditionalCode), &conditionalCodeBytes),
                 "if/else and nested block IR emit")) return 1;
    const uint8_t cmp[] = {0x39, 0xC1};
    const uint8_t test[] = {0x85, 0xC0};
    const uint8_t conditionalJump[] = {0x0F, 0x84};
    const uint8_t unconditionalJump[] = {0xE9};
    const uint8_t setEqual[] = {0x0F, 0x94, 0xC0};
    if (!require(has_bytes(conditionalCode, conditionalCodeBytes, cmp, sizeof(cmp)) &&
                 has_bytes(conditionalCode, conditionalCodeBytes, test, sizeof(test)) &&
                 has_bytes(conditionalCode, conditionalCodeBytes, conditionalJump, sizeof(conditionalJump)) &&
                 has_bytes(conditionalCode, conditionalCodeBytes, unconditionalJump, sizeof(unconditionalJump)) &&
                 has_bytes(conditionalCode, conditionalCodeBytes, setEqual, sizeof(setEqual)) &&
                 has_bytes(conditionalCode, conditionalCodeBytes, reinterpret_cast<const uint8_t*>("\x0F\xB6\xC0"), 3),
                 "AMD64 comparisons and real branches are emitted")) return 1;

    const char* whileSource =
        "int gx_main(void* ctx) { int i = 0; while (i < 3) i = i + 1; return i; }";
    static FunctionIR whileFunction = {};
    static Diagnostics whileDiagnostics;
    static uint8_t whileCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t whileCodeBytes = 0;
    if (!require(parse_text(whileSource, &whileFunction, &whileDiagnostics) &&
                 whileFunction.blockCount == 2 && whileFunction.localCount == 1 &&
                 has_statement_kind(whileFunction, StatementKind::While) &&
                 amd64::emit_function(whileFunction, whileCode, sizeof(whileCode), &whileCodeBytes) &&
                 has_backward_unconditional_branch(whileCode, whileCodeBytes),
                 "single-statement while emits a real backward branch")) return 1;

    const char* nestedWhileSource =
        "int gx_main(void* ctx) { int outer = 0; int total = 0; "
        "while (outer < 3) { int inner = 0; while (inner < 2) { "
        "total = total + 7; inner = inner + 1; } outer = outer + 1; } return total; }";
    static FunctionIR nestedWhile = {};
    static Diagnostics nestedWhileDiagnostics;
    static uint8_t nestedWhileCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t nestedWhileCodeBytes = 0;
    if (!require(parse_text(nestedWhileSource, &nestedWhile, &nestedWhileDiagnostics) &&
                 nestedWhile.blockCount >= 3 && nestedWhile.localCount == 3 &&
                 has_statement_kind(nestedWhile, StatementKind::While) &&
                 amd64::emit_function(nestedWhile, nestedWhileCode, sizeof(nestedWhileCode),
                                      &nestedWhileCodeBytes) &&
                 has_backward_unconditional_branch(nestedWhileCode, nestedWhileCodeBytes),
                 "nested while loops remain target-neutral and emit")) return 1;

    const char* composedWhileSource =
        "int gx_main(void* ctx) { int i = 0; int enabled = 1; if (enabled) { "
        "while (i < 10 && enabled) { if (i < 5) { i = i + 1; } else { "
        "enabled = 0; } } } return i; }";
    static FunctionIR composedWhile = {};
    static Diagnostics composedWhileDiagnostics;
    if (!require(parse_text(composedWhileSource, &composedWhile, &composedWhileDiagnostics) &&
                 has_statement_kind(composedWhile, StatementKind::While) &&
                 has_statement_kind(composedWhile, StatementKind::If) &&
                 has_expression_kind(composedWhile, ExpressionKind::LogicalAnd),
                 "while composes with if and short-circuit conditions")) return 1;

    const char* returnInsideWhileSource =
        "int gx_main(void* ctx) { int x = 1; while (x) { if (x == 42) { return x; } "
        "x = x + 1; } return 0; }";
    static FunctionIR returnInsideWhile = {};
    static Diagnostics returnInsideWhileDiagnostics;
    static uint8_t returnInsideWhileCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t returnInsideWhileCodeBytes = 0;
    if (!require(parse_text(returnInsideWhileSource, &returnInsideWhile,
                            &returnInsideWhileDiagnostics) && returnInsideWhile.returnCount == 2 &&
                 amd64::emit_function(returnInsideWhile, returnInsideWhileCode,
                                      sizeof(returnInsideWhileCode), &returnInsideWhileCodeBytes),
                 "return inside while uses the shared epilogue path")) return 1;

    const char* missingWhileReturnSource =
        "int gx_main(void* ctx) { int x = 1; while (x) { return 42; } }";
    static FunctionIR missingWhileReturn = {};
    static Diagnostics missingWhileReturnDiagnostics;
    if (!require(!parse_text(missingWhileReturnSource, &missingWhileReturn,
                             &missingWhileReturnDiagnostics) &&
                 missingWhileReturnDiagnostics.count() != 0 &&
                 std::strcmp(missingWhileReturnDiagnostics.at(0).message,
                             "gx_main may reach end without returning a value") == 0,
                 "while remains conservative for return-path analysis")) return 1;

    std::string tooManyLoops = "int gx_main(void* ctx) { int value = 0; ";
    for (uint32_t i = 0; i < COMPILER_MAX_LOOP_NESTING + 1U; ++i)
        tooManyLoops += "while (value < 1) {";
    tooManyLoops += " value = 1; return value; ";
    for (uint32_t i = 0; i < COMPILER_MAX_LOOP_NESTING + 1U; ++i) tooManyLoops += "}";
    tooManyLoops += " return value; }";
    static FunctionIR tooManyLoopsFunction = {};
    static Diagnostics tooManyLoopsDiagnostics;
    if (!require(!parse_text(tooManyLoops.c_str(), &tooManyLoopsFunction,
                             &tooManyLoopsDiagnostics) && tooManyLoopsDiagnostics.count() != 0 &&
                 std::strcmp(tooManyLoopsDiagnostics.at(0).message,
                             "loop nesting limit exceeded") == 0,
                 "loop nesting capacity is bounded")) return 1;
    if (!require(COMPILER_MAX_LOOP_TARGET_DEPTH == COMPILER_MAX_LOOP_NESTING,
                 "loop target capacity matches loop nesting capacity")) return 1;

    const char* breakContinueSource =
        "int gx_main(void* ctx) { int i = 0; int total = 0; while (i < 10) { "
        "i = i + 1; if (i < 3) { continue; } if (i > 8) { break; } "
        "total = total + i; } return total + 9; }";
    static FunctionIR breakContinue = {};
    Diagnostics breakContinueDiagnostics;
    static uint8_t breakContinueCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t breakContinueCodeBytes = 0;
    if (!require(parse_text(breakContinueSource, &breakContinue, &breakContinueDiagnostics) &&
                 has_statement_kind(breakContinue, StatementKind::Break) &&
                 has_statement_kind(breakContinue, StatementKind::Continue) &&
                 amd64::emit_function(breakContinue, breakContinueCode,
                                      sizeof(breakContinueCode), &breakContinueCodeBytes) &&
                 count_unconditional_branches(breakContinueCode, breakContinueCodeBytes, true) >= 2 &&
                 count_unconditional_branches(breakContinueCode, breakContinueCodeBytes, false) >= 1,
                 "break and continue lower to bounded control-flow branches")) return 1;

    const char* nestedLoopControlSource =
        "int gx_main(void* ctx) { int outer = 0; int total = 0; while (outer < 3) { "
        "int inner = 0; while (inner < 4) { inner = inner + 1; "
        "if (inner < 3) { continue; } if (inner > 3) { break; } "
        "total = total + 7; } outer = outer + 1; } return total; }";
    static FunctionIR nestedLoopControl = {};
    Diagnostics nestedLoopControlDiagnostics;
    static uint8_t nestedLoopControlCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t nestedLoopControlCodeBytes = 0;
    if (!require(parse_text(nestedLoopControlSource, &nestedLoopControl,
                            &nestedLoopControlDiagnostics) &&
                 has_statement_kind(nestedLoopControl, StatementKind::While) &&
                 has_statement_kind(nestedLoopControl, StatementKind::Break) &&
                 has_statement_kind(nestedLoopControl, StatementKind::Continue) &&
                 amd64::emit_function(nestedLoopControl, nestedLoopControlCode,
                                      sizeof(nestedLoopControlCode), &nestedLoopControlCodeBytes) &&
                 count_unconditional_branches(nestedLoopControlCode, nestedLoopControlCodeBytes, true) >= 3 &&
                 count_unconditional_branches(nestedLoopControlCode, nestedLoopControlCodeBytes, false) >= 1,
                 "nested break and continue use innermost loop targets")) return 1;

    const char* breakOutside = "int gx_main(void* ctx) { break; return 42; }";
    const char* continueOutside = "int gx_main(void* ctx) { continue; return 42; }";
    static FunctionIR outsideFunction = {};
    Diagnostics breakOutsideDiagnostics;
    Diagnostics continueOutsideDiagnostics;
    if (!require(!parse_text(breakOutside, &outsideFunction, &breakOutsideDiagnostics) &&
                 breakOutsideDiagnostics.count() != 0 &&
                 std::strcmp(breakOutsideDiagnostics.at(0).message,
                             "'break' is only valid inside a loop") == 0 &&
                 breakOutsideDiagnostics.at(0).location.column > 0,
                 "break outside a loop is source-located and rejected")) return 1;
    if (!require(!parse_text(continueOutside, &outsideFunction, &continueOutsideDiagnostics) &&
                 continueOutsideDiagnostics.count() != 0 &&
                 std::strcmp(continueOutsideDiagnostics.at(0).message,
                             "'continue' is only valid inside a loop") == 0 &&
                 continueOutsideDiagnostics.at(0).location.column > 0,
                 "continue outside a loop is source-located and rejected")) return 1;

    const char* breakWithoutSemicolon =
        "int gx_main(void* ctx) { while (1) { break } return 0; }";
    const char* continueWithoutSemicolon =
        "int gx_main(void* ctx) { while (1) { continue } return 0; }";
    Diagnostics invalidBreakSyntaxDiagnostics;
    Diagnostics invalidContinueSyntaxDiagnostics;
    if (!require(!parse_text(breakWithoutSemicolon, &outsideFunction,
                             &invalidBreakSyntaxDiagnostics) &&
                 invalidBreakSyntaxDiagnostics.count() != 0 &&
                 std::strcmp(invalidBreakSyntaxDiagnostics.at(0).message,
                             "expected ';' after 'break'") == 0,
                 "break requires a semicolon")) return 1;
    if (!require(!parse_text(continueWithoutSemicolon, &outsideFunction,
                             &invalidContinueSyntaxDiagnostics) &&
                 invalidContinueSyntaxDiagnostics.count() != 0 &&
                 std::strcmp(invalidContinueSyntaxDiagnostics.at(0).message,
                             "expected ';' after 'continue'") == 0,
                 "continue requires a semicolon")) return 1;

    const char* loopKeywordBoundaries =
        "break breaker breakfast continue continued continueValue";
    Token loopKeywordTokens[COMPILER_MAX_TOKENS] = {};
    uint32_t loopKeywordTokenCount = 0;
    Diagnostics loopKeywordDiagnostics;
    if (!require(lex_source(loopKeywordBoundaries,
                            static_cast<uint32_t>(std::strlen(loopKeywordBoundaries)),
                            loopKeywordTokens, COMPILER_MAX_TOKENS,
                            &loopKeywordTokenCount, loopKeywordDiagnostics) &&
                 loopKeywordTokens[0].kind == TokenKind::KeywordBreak &&
                 loopKeywordTokens[1].kind == TokenKind::Identifier &&
                 loopKeywordTokens[2].kind == TokenKind::Identifier &&
                 loopKeywordTokens[3].kind == TokenKind::KeywordContinue &&
                 loopKeywordTokens[4].kind == TokenKind::Identifier &&
                 loopKeywordTokens[5].kind == TokenKind::Identifier,
                 "break and continue preserve identifier boundaries")) return 1;

    const char* breakMissingReturn =
        "int gx_main(void* ctx) { int x = 1; while (x) { break; } }";
    const char* continueMissingReturn =
        "int gx_main(void* ctx) { int x = 1; while (x) { continue; } }";
    Diagnostics breakMissingReturnDiagnostics;
    Diagnostics continueMissingReturnDiagnostics;
    if (!require(!parse_text(breakMissingReturn, &outsideFunction,
                             &breakMissingReturnDiagnostics) &&
                 breakMissingReturnDiagnostics.count() != 0 &&
                 std::strcmp(breakMissingReturnDiagnostics.at(0).message,
                             "gx_main may reach end without returning a value") == 0,
                 "break does not satisfy missing-return analysis")) return 1;
    if (!require(!parse_text(continueMissingReturn, &outsideFunction,
                             &continueMissingReturnDiagnostics) &&
                 continueMissingReturnDiagnostics.count() != 0 &&
                 std::strcmp(continueMissingReturnDiagnostics.at(0).message,
                             "gx_main may reach end without returning a value") == 0,
                 "continue does not satisfy missing-return analysis")) return 1;

    static uint8_t resetCodeA[COMPILER_MAX_CODE_BYTES] = {};
    static uint8_t resetCodeB[COMPILER_MAX_CODE_BYTES] = {};
    static uint8_t resetElfA[BOOTSTRAP_MAX_ELF_BYTES] = {};
    static uint8_t resetElfB[BOOTSTRAP_MAX_ELF_BYTES] = {};
    static FunctionIR resetA = {};
    static FunctionIR resetB = {};
    uint32_t resetCodeBytesA = 0, resetCodeBytesB = 0;
    uint32_t resetElfBytesA = 0, resetElfBytesB = 0;
    if (!require(compile_text(nestedLoopControlSource, &resetA, resetCodeA,
                              &resetCodeBytesA, resetElfA, &resetElfBytesA) &&
                 !parse_text(breakOutside, &outsideFunction, &breakOutsideDiagnostics) &&
                 compile_text(whileSource, &outsideFunction, resetCodeB,
                              &resetCodeBytesB, resetElfB, &resetElfBytesB) &&
                 compile_text(nestedLoopControlSource, &resetB, resetCodeB,
                              &resetCodeBytesB, resetElfB, &resetElfBytesB) &&
                 resetCodeBytesA == resetCodeBytesB && resetElfBytesA == resetElfBytesB &&
                 std::memcmp(resetCodeA, resetCodeB, resetCodeBytesA) == 0 &&
                 std::memcmp(resetElfA, resetElfB, resetElfBytesA) == 0,
                 "loop-target state resets after nested and failed compilations")) return 1;

    int32_t displacement = 0;
    if (!require(amd64::calculate_signed_rel32(100, 110, &displacement) && displacement == -10 &&
                 amd64::calculate_signed_rel32(110, 110, &displacement) && displacement == 0 &&
                 amd64::calculate_signed_rel32(110, 100, &displacement) && displacement == 10 &&
                 !amd64::calculate_signed_rel32(0, 0x100000000ULL, &displacement) &&
                 !amd64::calculate_signed_rel32(0x100000000ULL, 0, &displacement),
                 "signed rel32 handles backward, zero, forward, and overflow cases")) return 1;

    const char* genericConditionSource =
        "int gx_main(void* ctx) { int x = 1; if ((x + 1) >= 2) { if (x) { return 42; } } return 0; }";
    static FunctionIR genericCondition = {};
    Diagnostics genericConditionDiagnostics;
    if (!require(parse_text(genericConditionSource, &genericCondition, &genericConditionDiagnostics) &&
                 genericCondition.blockCount >= 3 && genericCondition.returnCount == 2,
                 "generic truth conditions and nested if parse")) return 1;

    const char* missingReturnSource =
        "int gx_main(void* ctx) { int x = 42; if (x == 42) { return 42; } }";
    static FunctionIR missingReturn = {};
    Diagnostics missingReturnDiagnostics;
    if (!require(!parse_text(missingReturnSource, &missingReturn, &missingReturnDiagnostics) &&
                 missingReturnDiagnostics.count() != 0 &&
                 std::strcmp(missingReturnDiagnostics.at(0).message,
                             "gx_main may reach end without returning a value") == 0,
                 "missing return paths are diagnosed")) return 1;

    const char* invalidConditionSource =
        "int gx_main(void* ctx) { if (x ==) { return 42; } return 0; }";
    static FunctionIR invalidCondition = {};
    Diagnostics invalidConditionDiagnostics;
    if (!require(!parse_text(invalidConditionSource, &invalidCondition, &invalidConditionDiagnostics) &&
                 invalidConditionDiagnostics.count() != 0 &&
                 invalidConditionDiagnostics.at(0).location.line == 1 &&
                 invalidConditionDiagnostics.at(0).location.column > 0,
                 "malformed conditions retain source locations")) return 1;

    std::string tooManyBlocks = "int gx_main(void* ctx) {";
    for (uint32_t i = 0; i < COMPILER_MAX_BLOCK_NESTING + 1; ++i) tooManyBlocks += " {";
    tooManyBlocks += " return 0; ";
    for (uint32_t i = 0; i < COMPILER_MAX_BLOCK_NESTING + 1; ++i) tooManyBlocks += "}";
    tooManyBlocks += " }";
    static FunctionIR tooManyBlocksFunction = {};
    Diagnostics tooManyBlocksDiagnostics;
    if (!require(!parse_text(tooManyBlocks.c_str(), &tooManyBlocksFunction, &tooManyBlocksDiagnostics) &&
                 tooManyBlocksDiagnostics.count() != 0 &&
                 std::strcmp(tooManyBlocksDiagnostics.at(0).message, "block nesting limit exceeded") == 0,
                 "block nesting capacity is bounded")) return 1;

    uint8_t tinyCode[32] = {};
    uint32_t tinyCodeBytes = 0;
    if (!require(!amd64::emit_function(conditional,
                                       BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_DATA_OFFSET,
                                       tinyCode, sizeof(tinyCode), &tinyCodeBytes),
                 "branch code-buffer overflow is rejected")) return 1;

    const char* logsSource =
        "int gx_main(gx_app_context* ctx) { log(ctx, \"First\"); log(ctx, \"Second\"); log(ctx, \"Third\"); return 42; }";
    static FunctionIR logs = {};
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
    static FunctionIR unknownFunction = {};
    Diagnostics unknownDiagnostics;
    if (!require(!parse_text(unknown, &unknownFunction, &unknownDiagnostics) &&
                 unknownDiagnostics.count() != 0 &&
                 std::strcmp(unknownDiagnostics.at(0).message, "unknown identifier 'missing'") == 0,
                 "unknown identifiers are rejected with names")) return 1;

    const char* duplicate = "int gx_main(gx_app_context* ctx) { int x = 1; int x = 2; return x; }";
    static FunctionIR duplicateFunction = {};
    Diagnostics duplicateDiagnostics;
    if (!require(!parse_text(duplicate, &duplicateFunction, &duplicateDiagnostics) &&
                 std::strcmp(duplicateDiagnostics.at(0).message, "duplicate local 'x'") == 0,
                 "duplicate locals are rejected with names")) return 1;

    std::string oversized = "int gx_main(void* ctx) { int ";
    oversized.append(64, 'x');
    oversized += " = 1; return 1; }";
    static FunctionIR oversizedFunction = {};
    Diagnostics oversizedDiagnostics;
    if (!require(!parse_text(oversized.c_str(), &oversizedFunction, &oversizedDiagnostics),
                 "oversized identifiers are rejected")) return 1;

    std::string nested = "int gx_main(void* ctx) { return ";
    nested.append(17, '(');
    nested += "1";
    nested.append(17, ')');
    nested += "; }";
    static FunctionIR nestedFunction = {};
    Diagnostics nestedDiagnostics;
    if (!require(!parse_text(nested.c_str(), &nestedFunction, &nestedDiagnostics) &&
                 std::strcmp(nestedDiagnostics.at(0).message, "expression nesting limit exceeded") == 0,
                 "expression nesting is bounded")) return 1;

    std::string tooManyLocals = "int gx_main(void* ctx) {";
    for (uint32_t i = 0; i < COMPILER_MAX_LOCALS + 1; ++i) {
        tooManyLocals += " int local" + std::to_string(i) + " = 0;";
    }
    tooManyLocals += " return 0; }";
    static FunctionIR tooManyLocalsFunction = {};
    Diagnostics tooManyLocalsDiagnostics;
    if (!require(!parse_text(tooManyLocals.c_str(), &tooManyLocalsFunction, &tooManyLocalsDiagnostics) &&
                 std::strcmp(tooManyLocalsDiagnostics.at(0).message, "too many local variables") == 0,
                 "local capacity is bounded")) return 1;

    std::string tooManyStatements = "int gx_main(void* ctx) { int value = 0;";
    for (uint32_t i = 0; i < COMPILER_MAX_STATEMENTS; ++i) tooManyStatements += " value = 0;";
    tooManyStatements += " return value; }";
    static FunctionIR tooManyStatementsFunction = {};
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
    static FunctionIR tooManyExpressionsFunction = {};
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
    static FunctionIR tooManyStringsFunction = {};
    Diagnostics tooManyStringsDiagnostics;
    if (!require(!parse_text(tooManyStrings.c_str(), &tooManyStringsFunction,
                              &tooManyStringsDiagnostics) &&
                 std::strcmp(tooManyStringsDiagnostics.at(0).message, "too many string literals") == 0,
                 "string capacity is bounded")) return 1;

    uint8_t deterministicCodeA[COMPILER_MAX_CODE_BYTES] = {};
    uint8_t deterministicCodeB[COMPILER_MAX_CODE_BYTES] = {};
    uint8_t deterministicElfA[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint8_t deterministicElfB[BOOTSTRAP_MAX_ELF_BYTES] = {};
    static FunctionIR deterministicA = {};
    static FunctionIR deterministicB = {};
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

    const char* logicalLex = "int gx_main(void* ctx) { return 1&&2||3; }";
    Token logicalTokens[COMPILER_MAX_TOKENS] = {};
    uint32_t logicalTokenCount = 0;
    Diagnostics logicalLexDiagnostics;
    if (!require(lex_source(logicalLex, static_cast<uint32_t>(std::strlen(logicalLex)), logicalTokens,
                            COMPILER_MAX_TOKENS, &logicalTokenCount, logicalLexDiagnostics),
                 "logical operators lex")) return 1;
    if (!require(logicalTokens[10].kind == TokenKind::LogicalAnd && logicalTokens[10].length == 2 &&
                 logicalTokens[11].kind == TokenKind::Integer && logicalTokens[12].kind == TokenKind::LogicalOr &&
                  logicalTokens[12].length == 2 && logicalTokens[12].location.column == 37,
                 "logical operators use longest matching and source locations")) return 1;

    const char* singleAnd = "int gx_main(void* ctx) { return 1 & 1; }";
    const char* singleOr = "int gx_main(void* ctx) { return 1 | 1; }";
    Token invalidOperatorTokens[COMPILER_MAX_TOKENS] = {};
    uint32_t invalidOperatorTokenCount = 0;
    Diagnostics singleAndDiagnostics;
    Diagnostics singleOrDiagnostics;
    if (!require(!lex_source(singleAnd, static_cast<uint32_t>(std::strlen(singleAnd)),
                             invalidOperatorTokens, COMPILER_MAX_TOKENS,
                             &invalidOperatorTokenCount, singleAndDiagnostics) &&
                 singleAndDiagnostics.count() != 0 &&
                 std::strcmp(singleAndDiagnostics.at(0).message,
                             "unexpected '&'; logical AND is '&&'") == 0,
                 "single ampersand is rejected with a focused diagnostic")) return 1;
    if (!require(!lex_source(singleOr, static_cast<uint32_t>(std::strlen(singleOr)),
                             invalidOperatorTokens, COMPILER_MAX_TOKENS,
                             &invalidOperatorTokenCount, singleOrDiagnostics) &&
                 singleOrDiagnostics.count() != 0 &&
                 std::strcmp(singleOrDiagnostics.at(0).message,
                             "unexpected '|'; logical OR is '||'") == 0,
                 "single pipe is rejected with a focused diagnostic")) return 1;

    const char* logicalSource = "int gx_main(void* ctx) { return 0 || 1 && 1; }";
    static FunctionIR logical = {};
    Diagnostics logicalDiagnostics;
    if (!require(parse_text(logicalSource, &logical, &logicalDiagnostics) &&
                 logical.returnConstantValid && logical.returnConstant == 1 &&
                 has_expression_kind(logical, ExpressionKind::LogicalAnd) &&
                 has_expression_kind(logical, ExpressionKind::LogicalOr),
                 "logical precedence and target-neutral IR are represented")) return 1;

    const char* logicalParentheses = "int gx_main(void* ctx) { return (0 || 1) && 0; }";
    static FunctionIR logicalParenthesesFunction = {};
    Diagnostics logicalParenthesesDiagnostics;
    if (!require(parse_text(logicalParentheses, &logicalParenthesesFunction,
                             &logicalParenthesesDiagnostics) &&
                 logicalParenthesesFunction.returnConstantValid &&
                 logicalParenthesesFunction.returnConstant == 0,
                 "logical parentheses override precedence")) return 1;

    const char* logicalComparisonsSource = "int gx_main(void* ctx) { return 20 == 20 && 22 == 22; }";
    static FunctionIR logicalComparisons = {};
    Diagnostics logicalComparisonsDiagnostics;
    if (!require(parse_text(logicalComparisonsSource, &logicalComparisons, &logicalComparisonsDiagnostics) &&
                 logicalComparisons.returnConstantValid && logicalComparisons.returnConstant == 1,
                 "comparisons bind tighter than logical AND")) return 1;

    const char* canonicalLogical = "int gx_main(void* ctx) { return 42 && 99; }";
    static FunctionIR canonicalLogicalFunction = {};
    uint8_t canonicalLogicalCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t canonicalLogicalCodeBytes = 0;
    if (!require(compile_text(canonicalLogical, &canonicalLogicalFunction, canonicalLogicalCode,
                              &canonicalLogicalCodeBytes, expressionElf, &expressionElfBytes) &&
                 canonicalLogicalFunction.returnConstantValid &&
                 canonicalLogicalFunction.returnConstant == 1,
                 "nonzero logical operands produce canonical one")) return 1;

    const char* logicalAssignmentSource =
        "int gx_main(gx_app_context* ctx) { int x = 20; int y = 22; "
        "int matched = x == 20 && y == 22; return matched * 42; }";
    static FunctionIR logicalAssignment = {};
    uint8_t logicalAssignmentCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t logicalAssignmentCodeBytes = 0;
    if (!require(compile_text(logicalAssignmentSource, &logicalAssignment,
                              logicalAssignmentCode, &logicalAssignmentCodeBytes,
                              expressionElf, &expressionElfBytes) &&
                 has_expression_kind(logicalAssignment, ExpressionKind::LogicalAnd) &&
                 logicalAssignmentCodeBytes != 0,
                 "logical expressions are value-producing assignments")) return 1;

    const char* shortCircuitAndSource =
        "int gx_main(void* ctx) { int left = 0; int right = 7; return left && right; }";
    static FunctionIR shortCircuitAnd = {};
    uint8_t shortCircuitAndCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t shortCircuitAndCodeBytes = 0;
    if (!require(compile_text(shortCircuitAndSource, &shortCircuitAnd, shortCircuitAndCode,
                              &shortCircuitAndCodeBytes, expressionElf, &expressionElfBytes),
                 "short-circuit AND pipeline")) return 1;
    const uint8_t rightLoad[] = {0x8B, 0x85, 0xF8, 0xFF, 0xFF, 0xFF};
    if (!require(has_forward_conditional_bypass(shortCircuitAndCode, shortCircuitAndCodeBytes, 0x84) &&
                 branch_skips_needle(shortCircuitAndCode, shortCircuitAndCodeBytes, 0x84,
                                     rightLoad, sizeof(rightLoad)),
                 "AND branch graph bypasses RHS load")) return 1;

    const char* shortCircuitOrSource =
        "int gx_main(void* ctx) { int left = 1; int right = 7; return left || right; }";
    static FunctionIR shortCircuitOr = {};
    uint8_t shortCircuitOrCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t shortCircuitOrCodeBytes = 0;
    if (!require(compile_text(shortCircuitOrSource, &shortCircuitOr, shortCircuitOrCode,
                              &shortCircuitOrCodeBytes, expressionElf, &expressionElfBytes),
                 "short-circuit OR pipeline")) return 1;
    if (!require(has_forward_conditional_bypass(shortCircuitOrCode, shortCircuitOrCodeBytes, 0x85) &&
                 branch_skips_needle(shortCircuitOrCode, shortCircuitOrCodeBytes, 0x85,
                                     rightLoad, sizeof(rightLoad)),
                 "OR branch graph bypasses RHS load")) return 1;

    const char* invalidLogicalAnd = "int gx_main(void* ctx) { if (1 &&) { return 42; } return 0; }";
    const char* invalidLogicalOr = "int gx_main(void* ctx) { if (|| 1) { return 42; } return 0; }";
    static FunctionIR invalidLogicalFunction = {};
    Diagnostics invalidLogicalDiagnostics;
    if (!require(!parse_text(invalidLogicalAnd, &invalidLogicalFunction, &invalidLogicalDiagnostics) &&
                 invalidLogicalDiagnostics.count() != 0 &&
                 invalidLogicalDiagnostics.at(0).location.column > 0,
                 "missing logical RHS is rejected with a source location")) return 1;
    invalidLogicalDiagnostics = Diagnostics();
    if (!require(!parse_text(invalidLogicalOr, &invalidLogicalFunction, &invalidLogicalDiagnostics) &&
                 invalidLogicalDiagnostics.count() != 0 &&
                 invalidLogicalDiagnostics.at(0).location.column > 0,
                 "missing logical LHS is rejected with a source location")) return 1;

    std::string tooManyLogicalBranches = "int gx_main(void* ctx) { return 1";
    for (uint32_t i = 0; i < 44; ++i) tooManyLogicalBranches += " && 1";
    tooManyLogicalBranches += "; }";
    static FunctionIR tooManyLogicalFunction = {};
    uint8_t tooManyLogicalCode[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t tooManyLogicalCodeBytes = 0;
    Diagnostics tooManyLogicalDiagnostics;
    if (!require(parse_text(tooManyLogicalBranches.c_str(), &tooManyLogicalFunction,
                             &tooManyLogicalDiagnostics) &&
                 !amd64::emit_function(tooManyLogicalFunction, tooManyLogicalCode,
                                        sizeof(tooManyLogicalCode), &tooManyLogicalCodeBytes),
                 "logical branch/fixup capacity is bounded")) return 1;

    std::puts("compiler_bootstrap_host_test: PASS");
    return 0;
}
