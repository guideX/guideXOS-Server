// Hosted qualification for COMP4E character literals and byte-width locals.
// This compiles with the production lexer, parser, and AMD64 backend, inspects
// the emitted byte operations, and executes generated code on the host.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>

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

static CallResult execute_generated()
{
    CallResult result = {};
#if defined(_WIN32)
    void* memory = VirtualAlloc(nullptr, g_codeBytes, MEM_COMMIT | MEM_RESERVE,
                                PAGE_READWRITE);
    if (!memory) return result;
    std::memcpy(memory, g_code, g_codeBytes);
    DWORD oldProtection = 0;
    const bool executable = VirtualProtect(memory, g_codeBytes, PAGE_EXECUTE_READ,
                                           &oldProtection) != 0;
    FlushInstructionCache(GetCurrentProcess(), memory, g_codeBytes);
    if (executable)
        result = call_generated_entry(static_cast<uint8_t*>(memory) + g_entryOffset);
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    (void)g_code;
#endif
    return result;
}

static bool contains_bytes(const uint8_t* needle, uint32_t needleBytes)
{
    if (!needle || needleBytes == 0 || needleBytes > g_codeBytes) return false;
    for (uint32_t i = 0; i + needleBytes <= g_codeBytes; ++i)
        if (std::memcmp(g_code + i, needle, needleBytes) == 0) return true;
    return false;
}

static bool expect_rejection(const char* source, CompilerDiagnosticKind kind,
                             const char* message, uint32_t offset)
{
    Diagnostics diagnostics;
    const bool accepted = compile_source(source, &diagnostics);
    const bool valid = !accepted && diagnostics.count() != 0 &&
        diagnostics.at(0).kind == kind &&
        diagnostics.at(0).location.offset == offset &&
        diagnostics.at(0).location.line != 0 &&
        diagnostics.at(0).location.column != 0 &&
        diagnostic_contains(diagnostics, message);
    if (!valid) {
        std::fprintf(stderr, "rejection mismatch at expected offset %u, kind %u, message %s; accepted=%u count=%u\n",
                     offset, static_cast<unsigned>(kind), message, accepted ? 1U : 0U,
                     diagnostics.count());
        if (diagnostics.count())
            std::fprintf(stderr, "observed offset %u, line %u, column %u, kind %u, diagnostic %s\n",
                         diagnostics.at(0).location.offset, diagnostics.at(0).location.line,
                         diagnostics.at(0).location.column,
                         static_cast<unsigned>(diagnostics.at(0).kind), diagnostics.at(0).message);
    }
    return require(valid, "invalid character source has a classified, located diagnostic");
}

static bool test_character_execution_and_bytes()
{
    const char* source =
        "int identity(int value) { return value; }\n"
        "int gx_main(gx_app_context* ctx) {\n"
        "  int before = 287454020;\n"
        "  char first = 'A'; char second = 'B';\n"
        "  int between = 1432778632;\n"
        "  char high = 127; char signedEdge = 128; char minusOne = 255;\n"
        "  char truncated = 258; char wrapped = -129; char terminator = '\\0';\n"
        "  char data[4];\n"
        "  data[0] = 'T'; data[1] = 'E'; data[2] = 'S'; data[3] = 'T';\n"
        "  if (before != 287454020 || between != 1432778632) return 1;\n"
        "  if (first != 'A' || second != 'B' || identity(first) != 'A') return 2;\n"
        "  first = 'Z'; if (first != 'Z') return 3;\n"
        "  if (high != 127 || signedEdge != -128 || minusOne != -1) return 4;\n"
        "  if (truncated != 2 || wrapped != 127) return 5;\n"
        "  if (terminator != '\\0') return 8;\n"
        "  if (data[0] != 'T' || data[1] != 'E' || data[2] != 'S' || data[3] != 'T') return 6;\n"
        "  if ('\\n' != 10 || '\\r' != 13 || '\\t' != 9 || '\\\\' != 92 ||\n"
        "      '\\'' != 39 || '\\0' != 0) return 7;\n"
        "  return data[2];\n"
        "}\n";
    Diagnostics diagnostics;
    if (!compile_source(source, &diagnostics)) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "character diagnostic: %s\n", diagnostics.at(i).message);
        return require(false, "character source parses and emits production AMD64 code");
    }
    const uint8_t localStore[] = {0x88, 0x85};
    const uint8_t localSignExtend[] = {0x0F, 0xBE, 0x85};
    const uint8_t indexedStore[] = {0x88, 0x02};
    const uint8_t indexedSignExtend[] = {0x0F, 0xBE, 0x02};
    if (!require(contains_bytes(localStore, sizeof(localStore)),
                 "scalar char assignments emit byte-width stack stores")) return false;
    if (!require(contains_bytes(localSignExtend, sizeof(localSignExtend)),
                 "scalar char reads emit signed byte extension")) return false;
    if (!require(contains_bytes(indexedStore, sizeof(indexedStore)),
                 "char array assignments emit byte-width indexed stores")) return false;
    if (!require(contains_bytes(indexedSignExtend, sizeof(indexedSignExtend)),
                 "char array reads emit signed byte extension")) return false;
    const FunctionIR& entry = g_unit.functions[g_unit.entryFunction];
    uint32_t characterStorageBytes = 0;
    for (uint32_t i = 0; i < entry.localCount; ++i)
        if (entry.locals[i].kind == StorageKind::ScalarChar ||
            entry.locals[i].kind == StorageKind::ArrayChar)
            characterStorageBytes += entry.locals[i].sizeBytes;
    if (!require(entry.localCount == 11 && characterStorageBytes == 12 &&
                 entry.localStorageBytes == 22,
                 "char locals occupy one byte each with only required alignment padding")) return false;
    const CallResult result = execute_generated();
    return require(result.value == 'S' && result.runtimeFailure == 0,
                   "generated AMD64 preserves adjacent ints, truncates char stores, promotes signed chars, and indexes bytes");
}

static bool test_array_bounds()
{
    Diagnostics diagnostics;
    if (!compile_source(
            "int gx_main(gx_app_context* ctx) { char data[2]; int index = 2; return data[index]; }",
            &diagnostics))
        return require(false, "bounded char array source compiles");
    const CallResult result = execute_generated();
    return require((result.runtimeFailure & 0xFFU) == COMPILER_RUNTIME_STATUS_ARRAY_BOUNDS,
                   "char array reads retain runtime bounds checks");
}

static bool test_diagnostics()
{
    const char* unterminated = "int gx_main(gx_app_context* ctx) { return 'A; }";
    const char* badEscape = "int gx_main(gx_app_context* ctx) { return '\\x'; }";
    const char* multi = "int gx_main(gx_app_context* ctx) { return 'AB'; }";
    const char* wide = "int gx_main(gx_app_context* ctx) { return L'A'; }";
    const char* utf8Prefix = "int gx_main(gx_app_context* ctx) { return u8'A'; }";
    const char* nonAscii = "int gx_main(gx_app_context* ctx) { return '\xC3\xA9'; }";
    const char* empty = "int gx_main(gx_app_context* ctx) { return ''; }";
    const char* wrongType = "int gx_main(gx_app_context* ctx) { char letter = \"A\"; return 0; }";
    const char* wrongAssignment =
        "int gx_main(gx_app_context* ctx) { char letter = 'A'; letter = \"A\"; return 0; }";
    const char* pointer = "int gx_main(gx_app_context* ctx) { char* text; return 0; }";
    const char* decay = "int gx_main(gx_app_context* ctx) { char data[2]; int* value = data; return 0; }";
    const char* arrayInitializer =
        "int gx_main(gx_app_context* ctx) { char data[2] = 'A'; return 0; }";
    const char* charAddress = "int gx_main(gx_app_context* ctx) { char value = 'A'; int* p = &value; return 0; }";
    const char* global = "char value; int gx_main(gx_app_context* ctx) { return 0; }";
    const char* tooMany = "int gx_main(gx_app_context* ctx) { char data[65]; return 0; }";
    const uint32_t lexical = static_cast<uint32_t>(std::strchr(unterminated, '\'') - unterminated);
    const uint32_t escape = static_cast<uint32_t>(std::strchr(badEscape, '\'') - badEscape);
    const uint32_t multiOffset = static_cast<uint32_t>(std::strchr(multi, '\'') - multi);
    const uint32_t wideOffset = static_cast<uint32_t>(std::strchr(wide, 'L') - wide);
    const uint32_t utf8Offset = static_cast<uint32_t>(std::strstr(utf8Prefix, "u8") - utf8Prefix);
    const uint32_t nonAsciiOffset = static_cast<uint32_t>(std::strchr(nonAscii, '\'') - nonAscii);
    const uint32_t emptyOffset = static_cast<uint32_t>(std::strchr(empty, '\'') - empty);
    const uint32_t wrongTypeOffset = static_cast<uint32_t>(std::strstr(wrongType, "letter") - wrongType);
    const char* assignmentName = std::strstr(wrongAssignment, "letter") + 6;
    assignmentName = std::strstr(assignmentName, "letter");
    const uint32_t wrongAssignmentOffset = static_cast<uint32_t>(assignmentName - wrongAssignment);
    const uint32_t pointerOffset = static_cast<uint32_t>(std::strstr(pointer, "char*") + 4 - pointer);
    const char* decayUse = std::strstr(decay, "data") + 4;
    decayUse = std::strstr(decayUse, "data");
    const uint32_t decayOffset = static_cast<uint32_t>(decayUse - decay);
    const uint32_t arrayInitializerOffset =
        static_cast<uint32_t>(std::strstr(arrayInitializer, "data") - arrayInitializer);
    const uint32_t addressOffset = static_cast<uint32_t>(std::strchr(charAddress, '&') - charAddress);
    const uint32_t globalOffset = 0;
    const uint32_t limitOffset = static_cast<uint32_t>(std::strstr(tooMany, "65") - tooMany);
    return expect_rejection(unterminated, CompilerDiagnosticKind::Lexical,
                            "unterminated character literal", lexical) &&
           expect_rejection(badEscape, CompilerDiagnosticKind::Unsupported,
                            "unsupported escape", escape) &&
           expect_rejection(multi, CompilerDiagnosticKind::Unsupported,
                            "multi-character", multiOffset) &&
           expect_rejection(wide, CompilerDiagnosticKind::Unsupported,
                            "wide and encoded", wideOffset) &&
           expect_rejection(utf8Prefix, CompilerDiagnosticKind::Unsupported,
                            "wide and encoded", utf8Offset) &&
           expect_rejection(nonAscii, CompilerDiagnosticKind::Unsupported,
                            "non-ASCII character encodings", nonAsciiOffset) &&
           expect_rejection(empty, CompilerDiagnosticKind::Lexical,
                            "exactly one ASCII", emptyOffset) &&
           expect_rejection(wrongType, CompilerDiagnosticKind::Semantic,
                            "cannot initialize char", wrongTypeOffset) &&
           expect_rejection(wrongAssignment, CompilerDiagnosticKind::Semantic,
                            "cannot assign a non-int expression to char", wrongAssignmentOffset) &&
           expect_rejection(pointer, CompilerDiagnosticKind::Unsupported,
                            "local char pointers", pointerOffset) &&
           expect_rejection(decay, CompilerDiagnosticKind::Unsupported,
                            "char arrays do not convert", decayOffset) &&
           expect_rejection(arrayInitializer, CompilerDiagnosticKind::Unsupported,
                            "char array initializers", arrayInitializerOffset) &&
           expect_rejection(charAddress, CompilerDiagnosticKind::Unsupported,
                            "address of char storage", addressOffset) &&
           expect_rejection(global, CompilerDiagnosticKind::Unsupported,
                            "global char storage", globalOffset) &&
           expect_rejection(tooMany, CompilerDiagnosticKind::ResourceLimit,
                            "array length exceeds", limitOffset);
}

} // namespace

int main()
{
    if (!test_character_execution_and_bytes() || !test_array_bounds() || !test_diagnostics()) return 1;
    std::puts("compiler_character_bytes_host_test: PASS");
    return 0;
}
