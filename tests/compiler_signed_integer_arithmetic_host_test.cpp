// Hosted qualification for COMP4C signed int division and remainder.
// The test compiles with the production parser and AMD64 backend, inspects
// emitted IDIV bytes, and executes the generated entry on Windows.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace kernel::compiler;

namespace {

static TranslationUnitIR g_unit = {};
static Token g_tokens[COMPILER_MAX_TOKENS] = {};
static CallSite g_calls[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_CALL_EXPRESSIONS] = {};
static uint16_t g_arguments[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_CALL_ARGUMENT_NODES] = {};
static uint8_t g_code[COMPILER_MAX_CODE_BYTES] = {};
static uint32_t g_codeBytes = 0;
static uint32_t g_entryOffset = 0;

static bool require(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool diagnostic_contains(const Diagnostics& diagnostics, const char* needle)
{
    for (uint32_t i = 0; i < diagnostics.count(); ++i) {
        const char* message = diagnostics.at(i).message;
        if (message && std::strstr(message, needle)) return true;
    }
    return false;
}

static bool compile_source(const char* source, Diagnostics* diagnostics)
{
    if (!source || !diagnostics) return false;
    const uint32_t sourceBytes = static_cast<uint32_t>(std::strlen(source));
    uint32_t tokenCount = 0;
    g_unit = {};
    g_codeBytes = 0;
    g_entryOffset = 0;
    if (!lex_source(source, sourceBytes, g_tokens, COMPILER_MAX_TOKENS,
                    &tokenCount, *diagnostics) ||
        !parse_translation_unit(source, g_tokens, tokenCount, &g_unit, *diagnostics,
                                &g_calls[0][0], &g_arguments[0][0])) return false;
    return amd64::emit_translation_unit(g_unit, 0, g_code, sizeof(g_code),
                                        &g_codeBytes, &g_entryOffset);
}

struct CallResult {
    int32_t value;
    uint32_t runtimeFailure;
    uint32_t runtimeDepth;
};

static CallResult call_generated_entry(void* entry)
{
    CallResult result = {};
#if defined(__GNUC__) || defined(__clang__)
    asm volatile(
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "subq $64, %%rsp\n\t"
        "movq %[output], 32(%%rsp)\n\t"
        "xorq %%rcx, %%rcx\n\t"
        "call *%[entry]\n\t"
        "movq 32(%%rsp), %%r11\n\t"
        "movl %%r15d, 4(%%r11)\n\t"
        "movl %%r15d, %%r10d\n\t"
        "shrl $8, %%r10d\n\t"
        "movl %%r10d, 8(%%r11)\n\t"
        "addq $64, %%rsp\n\t"
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        : "=a"(result.value)
        : [entry] "r"(entry), [output] "r"(&result)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
#else
    typedef int32_t (*EntryFunction)(void*);
    result.value = reinterpret_cast<EntryFunction>(entry)(nullptr);
#endif
    return result;
}

static bool execute_generated(int32_t expected)
{
#if defined(_WIN32)
    void* memory = VirtualAlloc(nullptr, g_codeBytes, MEM_COMMIT | MEM_RESERVE,
                                PAGE_READWRITE);
    if (!memory) return false;
    std::memcpy(memory, g_code, g_codeBytes);
    DWORD oldProtection = 0;
    const bool executable = VirtualProtect(memory, g_codeBytes, PAGE_EXECUTE_READ,
                                           &oldProtection) != 0;
    FlushInstructionCache(GetCurrentProcess(), memory, g_codeBytes);
    const CallResult result = executable
        ? call_generated_entry(static_cast<uint8_t*>(memory) + g_entryOffset)
        : CallResult();
    VirtualFree(memory, 0, MEM_RELEASE);
    return executable && result.value == expected && result.runtimeFailure == 0;
#else
    (void)expected;
    return true;
#endif
}

static bool contains_bytes(const uint8_t* needle, uint32_t needleBytes)
{
    if (!needle || needleBytes == 0 || needleBytes > g_codeBytes) return false;
    for (uint32_t i = 0; i + needleBytes <= g_codeBytes; ++i)
        if (std::memcmp(g_code + i, needle, needleBytes) == 0) return true;
    return false;
}

static bool expect_semantic_rejection(const char* source, const char* message)
{
    Diagnostics diagnostics;
    const bool accepted = compile_source(source, &diagnostics);
    const char* operation = std::strchr(source, '/');
    const char* remainder = std::strchr(source, '%');
    if (!operation || (remainder && remainder < operation)) operation = remainder;
    return !accepted && diagnostics.count() != 0 &&
        diagnostics.at(0).kind == CompilerDiagnosticKind::Semantic &&
        diagnostics.at(0).location.line != 0 && diagnostics.at(0).location.column != 0 &&
        operation && diagnostics.at(0).location.offset ==
            static_cast<uint32_t>(operation - source) &&
        diagnostic_contains(diagnostics, message);
}

static bool test_execution_and_code_generation()
{
    const char* source =
        "int divide(int a, int b) { return a / b; }\n"
        "int remainder(int a, int b) { return a % b; }\n"
        "int local_division(int a, int b) { int x = a; int y = b; return x / y; }\n"
        "int nested(int a, int b) { return (a + 5) / (b * 2) - (a % b); }\n"
        "int precedence() { return 2 + 12 / 3 * 2; }\n"
        "int parenthesized() { return (20 - 3) / (3 + 2); }\n"
        "int repeated() { return 100 / 7 + 100 % 7 + (3 * 8) / 4; }\n"
        "int three() { return 3; }\n"
        "int four() { return 4; }\n"
        "int call_operands() { return divide(100 + three(), 2 + three()); }\n"
        "int loop_division(int n) { int sum = 0; int i = 1; "
        "while (i <= n) { sum = sum + i / 2; i = i + 1; } return sum; }\n"
        "int gx_main(gx_app_context* ctx) {\n"
        "  if (divide(17, 5) != 3) return 1;\n"
        "  if (divide(-17, 5) != -3) return 2;\n"
        "  if (divide(17, -5) != -3) return 3;\n"
        "  if (divide(-17, -5) != 3) return 4;\n"
        "  if (remainder(17, 5) != 2) return 5;\n"
        "  if (remainder(-17, 5) != -2) return 6;\n"
        "  if (remainder(17, -5) != 2) return 7;\n"
        "  if (remainder(-17, -5) != -2) return 8;\n"
        "  if (local_division(99, 9) != 11) return 9;\n"
        "  if (nested(17, 5) != 0) return 10;\n"
        "  if (precedence() != 10) return 11;\n"
        "  if (parenthesized() != 3) return 12;\n"
        "  if (repeated() != 22) return 13;\n"
        "  if (call_operands() != 20) return 14;\n"
        "  if (loop_division(5) != 6) return 15;\n"
        "  if (divide(-2147483648, 1) != -2147483648) return 16;\n"
        "  if (remainder(-2147483648, 3) != -2) return 17;\n"
        "  return 42;\n"
        "}\n";
    Diagnostics diagnostics;
    if (!compile_source(source, &diagnostics)) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "arithmetic diagnostic: %s\n", diagnostics.at(i).message);
        return require(false, "signed arithmetic source parses and emits AMD64 code");
    }
    const uint8_t idivSequence[] = {0x89, 0xC1, 0x58, 0x99, 0xF7, 0xF9};
    const uint8_t remainderMove[] = {0xF7, 0xF9, 0x89, 0xD0};
    if (!require(contains_bytes(idivSequence, sizeof(idivSequence)),
                 "generated AMD64 uses ECX divisor, EAX dividend and CDQ before IDIV")) return false;
    if (!require(contains_bytes(remainderMove, sizeof(remainderMove)),
                 "generated AMD64 moves IDIV remainder from EDX to EAX")) return false;
    if (!require(execute_generated(42),
                 "generated signed division and remainder return all expected results")) return false;

    uint8_t tiny[1] = {};
    uint32_t tinyBytes = 0, tinyEntry = 0;
    return require(!amd64::emit_translation_unit(g_unit, 0, tiny, sizeof(tiny),
                                                  &tinyBytes, &tinyEntry),
                   "backend code-size resource limit rejects undersized output");
}

static bool test_diagnostics_and_boundaries()
{
    if (!require(expect_semantic_rejection(
            "int gx_main(gx_app_context* ctx) { return 7 / (3 - 3); }",
            "constant division by zero"), "constant division by zero is semantic error")) return false;
    if (!require(expect_semantic_rejection(
            "int gx_main(gx_app_context* ctx) { return 7 % (3 - 3); }",
            "constant remainder by zero"), "constant remainder by zero is semantic error")) return false;
    if (!require(expect_semantic_rejection(
            "int gx_main(gx_app_context* ctx) { return -2147483648 / -1; }",
            "constant signed division or remainder overflow"), "INT_MIN / -1 is rejected")) return false;
    if (!require(expect_semantic_rejection(
            "int gx_main(gx_app_context* ctx) { return -2147483648 % -1; }",
            "constant signed division or remainder overflow"), "INT_MIN % -1 is rejected")) return false;
    if (!require(expect_semantic_rejection(
            "int invalid(int* value) { return value / 2; } "
            "int gx_main(gx_app_context* ctx) { return 0; }",
            "division requires int expressions"), "pointer division is rejected")) return false;
    if (!require(expect_semantic_rejection(
            "int invalid() { return \"x\" % 2; } "
            "int gx_main(gx_app_context* ctx) { return 0; }",
            "remainder requires int expressions"), "string remainder is rejected")) return false;

    Diagnostics diagnostics;
    if (!require(compile_source(
            "int quotient(int a, int b) { return a / b; } "
            "int gx_main(gx_app_context* ctx) { return quotient(8, 2); }", &diagnostics),
            "runtime-dependent divisor compiles to IDIV")) return false;
    const uint8_t idiv[] = {0xF7, 0xF9};
    if (!require(contains_bytes(idiv, sizeof(idiv)), "dynamic divisor emits signed IDIV")) return false;

    std::string tooDeep = "int gx_main(gx_app_context* ctx) { return ";
    for (uint32_t i = 0; i < COMPILER_MAX_EXPRESSION_NESTING + 2U; ++i) tooDeep += "(";
    tooDeep += "1";
    for (uint32_t i = 0; i < COMPILER_MAX_EXPRESSION_NESTING + 2U; ++i) tooDeep += ")";
    tooDeep += "; }";
    diagnostics = Diagnostics();
    if (!require(!compile_source(tooDeep.c_str(), &diagnostics) &&
                 diagnostics.count() != 0 &&
                 diagnostic_contains(diagnostics, "expression nesting limit exceeded"),
                 "expression nesting limit is diagnosed")) return false;

    diagnostics = Diagnostics();
    if (!require(!compile_source("int gx_main(gx_app_context* ctx) { return 3 / ; }",
                                 &diagnostics) && diagnostics.count() != 0 &&
                 diagnostics.at(0).kind == CompilerDiagnosticKind::Syntax &&
                 diagnostics.at(0).location.column != 0,
                 "malformed division retains syntax classification and source location")) return false;
    diagnostics = Diagnostics();
    return require(compile_source(
        "int gx_main(gx_app_context* ctx) { return 9 / 3; }", &diagnostics),
        "valid source compiles after malformed arithmetic source");
}

} // namespace

int main()
{
    if (!test_execution_and_code_generation() || !test_diagnostics_and_boundaries()) return 1;
    std::puts("compiler_signed_integer_arithmetic_host_test: PASS");
    return 0;
}
