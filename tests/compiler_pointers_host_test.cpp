// Focused host proof for Phase 27R typed, bounded int* support.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_linker.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_module.h"
#include "core/compiler/compiler_object.h"
#include "core/compiler/compiler_parser.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace kernel::compiler;

namespace {

static Token g_tokens[COMPILER_MAX_TOKENS] = {};

static bool require(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool diagnostic_contains(const Diagnostics& diagnostics, const char* needle)
{
    for (uint32_t i = 0; i < diagnostics.count(); ++i)
        if (std::strstr(diagnostics.at(i).message, needle)) return true;
    return false;
}

static bool parse_source(const char* source, TranslationUnitIR* unit, Diagnostics* diagnostics)
{
    const uint32_t bytes = static_cast<uint32_t>(std::strlen(source));
    uint32_t tokenCount = 0;
    return lex_source(source, bytes, g_tokens, COMPILER_MAX_TOKENS, &tokenCount, *diagnostics) &&
        parse_translation_unit(source, g_tokens, tokenCount, unit, *diagnostics);
}

struct CallResult { int32_t value; uint32_t status; };

static CallResult call_entry(void* entry)
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
        "addq $64, %%rsp\n\t"
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        : "=a"(result.value)
        : [entry] "r"(entry), [output] "r"(&result)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
#else
    typedef int32_t (*Entry)(void*);
    result.value = reinterpret_cast<Entry>(entry)(nullptr);
#endif
    return result;
}

static bool compile_and_run(const char* source, int32_t expected, CallResult* observed = nullptr,
                            TranslationUnitIR* parsed = nullptr)
{
    TranslationUnitIR unit = {};
    Diagnostics diagnostics;
    if (!parse_source(source, &unit, &diagnostics)) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "pointer diagnostic: %s\n", diagnostics.at(i).message);
        return false;
    }
    uint8_t code[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t codeBytes = 0;
    uint32_t entryOffset = 0;
    if (!amd64::emit_translation_unit(unit, 0, code, sizeof(code), &codeBytes, &entryOffset)) return false;
#if defined(_WIN32)
    void* memory = VirtualAlloc(nullptr, codeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) return false;
    std::memcpy(memory, code, codeBytes);
    DWORD oldProtection = 0;
    const bool executable = VirtualProtect(memory, codeBytes, PAGE_EXECUTE_READ, &oldProtection) != 0;
    FlushInstructionCache(GetCurrentProcess(), memory, codeBytes);
    const CallResult result = executable ? call_entry(static_cast<uint8_t*>(memory) + entryOffset) : CallResult();
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    const CallResult result = call_entry(code + entryOffset);
#endif
    if (observed) *observed = result;
    if (parsed) *parsed = unit;
    return result.value == expected;
}

static bool rejected(const char* source, const char* message)
{
    TranslationUnitIR unit = {};
    Diagnostics diagnostics;
    const bool accepted = parse_source(source, &unit, &diagnostics);
    return !accepted && diagnostic_contains(diagnostics, message);
}

static bool compile_text(const char* path, const char* source, CompiledModule* module,
                         Diagnostics* diagnostics)
{
    return compile_module_from_source(path, source, static_cast<uint32_t>(std::strlen(source)),
                                      module, *diagnostics);
}

static int32_t find_export(const LinkedProgram& program, const char* name)
{
    for (uint32_t i = 0; i < program.exportCount; ++i)
        if (std::strcmp(program.exports[i].name, name) == 0) return static_cast<int32_t>(i);
    return -1;
}

static bool valid_pointer_programs()
{
    const char* local =
        "int gx_main(gx_app_context* ctx) { int value = 40; int* p = &value; "
        "*p = *p + 2; return value; }";
    if (!require(compile_and_run(local, 42), "address-of local and indirect write execute")) return false;

    const char* array =
        "int gx_main(gx_app_context* ctx) { int values[4]; values[0] = 40; "
        "int* p = &values[0]; *p = *p + 2; return values[0]; }";
    TranslationUnitIR arrayUnit = {};
    if (!require(compile_and_run(array, 42, nullptr, &arrayUnit), "array element address executes")) return false;

    const char* dynamic =
        "int gx_main(gx_app_context* ctx) { int values[4]; int i = 2; values[2] = 40; "
        "int* p = &values[i]; *p = *p + 2; return values[2]; }";
    if (!require(compile_and_run(dynamic, 42), "dynamic element address executes with bounds guard")) return false;

    const char* copy =
        "int gx_main(gx_app_context* ctx) { int value = 40; int* a = &value; int* b = a; "
        "*b = *b + 2; return value; }";
    if (!require(compile_and_run(copy, 42), "pointer copy aliases the pointee")) return false;

    const char* assignment =
        "int gx_main(gx_app_context* ctx) { int a = 40; int b = 41; int* p = &a; "
        "p = &b; *p = *p + 1; return b; }";
    if (!require(compile_and_run(assignment, 42), "pointer assignment changes pointee")) return false;

    const char* parameter =
        "int increment(int* p) { *p = *p + 1; return *p; } "
        "int gx_main(gx_app_context* ctx) { int value = 41; increment(&value); return value; }";
    const char* parameterProbe =
        "int probe(int* p) { return 42; } "
        "int gx_main(gx_app_context* ctx) { int value = 41; return probe(&value); }";
    if (!require(compile_and_run(parameterProbe, 42), "pointer parameter call preserves return value")) return false;
    if (!require(compile_and_run(parameter, 42), "pointer parameter preserves aliasing")) return false;

    const char* recursive =
        "int recursive(int n) { int value = n; int* p = &value; if (n == 0) { return *p; } "
        "return *p + recursive(n - 1); } "
        "int gx_main(gx_app_context* ctx) { return recursive(6); }";
    if (!require(compile_and_run(recursive, 21), "recursive pointer locals are activation-local")) return false;
    return true;
}

static bool diagnostics_and_ir()
{
    if (!require(rejected("int gx_main(gx_app_context* ctx) { int x = 1; int* p = x; return 0; }",
                          "cannot initialize int*"), "integer to pointer conversion is rejected")) return false;
    if (!require(rejected("int gx_main(gx_app_context* ctx) { int x = 1; int* p = &x; x = p; return 0; }",
                          "cannot assign an int* expression to int"), "pointer to integer conversion is rejected")) return false;
    if (!require(rejected("int gx_main(gx_app_context* ctx) { int x = 1; return *x; }",
                          "cannot dereference non-pointer expression"), "non-pointer dereference is rejected")) return false;
    if (!require(rejected("int gx_main(gx_app_context* ctx) { int x = 1; int* p = &x; p = p + 1; return 0; }",
                          "arithmetic requires int expressions"), "pointer arithmetic is rejected")) return false;
    CallResult uninitialized = {};
    if (!require(compile_and_run("int gx_main(gx_app_context* ctx) { int* p; return *p; }", 0,
                                 &uninitialized) &&
                 (uninitialized.status & 0xFFU) == COMPILER_RUNTIME_STATUS_INVALID_POINTER,
                 "uninitialized pointer fails safely at runtime")) return false;
    if (!require(rejected("int gx_main(gx_app_context* ctx) { int a[2]; int* p = a; return 0; }",
                          "requires an index"), "array decay is rejected")) return false;
    if (!require(rejected("int* p; int gx_main(gx_app_context* ctx) { return 0; }",
                          "global pointer variables are not supported"), "global pointer is rejected")) return false;
    if (!require(rejected("int gx_main(gx_app_context* ctx) { return &42; }",
                          "expression is not addressable"), "invalid address-of is rejected")) return false;

    const char* source =
        "int update(int* p) { *p = *p + 2; return *p; } "
        "int gx_main(gx_app_context* ctx) { int value = 40; int* p = &value; update(p); return value; }";
    TranslationUnitIR unit = {};
    if (!require(compile_and_run(source, 42, nullptr, &unit), "pointer IR program executes")) return false;
    if (!require(unit.functions[0].parameters[0].kind == ParameterKind::Int32Pointer &&
                 unit.functions[0].expressions[0].kind == ExpressionKind::LoadPointer,
                 "pointer parameter and expression metadata are retained")) return false;
    return true;
}

static bool module_pointers()
{
    const char* stateSource = "int answer = 40;\n";
    const char* globalMathSource =
        "extern int answer; int add_global() { int* p = &answer; *p = *p + 2; return *p; }\n";
    const char* globalMainSource =
        "extern int answer; int add_global(); int gx_main(gx_app_context* ctx) { add_global(); return answer; }\n";
    static CompiledModule globalModules[3] = {};
    Diagnostics globalDiagnostics[3];
    if (!require(compile_text("src/state.cpp", stateSource, &globalModules[0], &globalDiagnostics[0]),
                 "cross-file pointer global definition compiles") ||
        !require(compile_text("src/math.cpp", globalMathSource, &globalModules[1], &globalDiagnostics[1]),
                 "cross-file pointer global use compiles") ||
        !require(compile_text("src/main.cpp", globalMainSource, &globalModules[2], &globalDiagnostics[2]),
                 "cross-file pointer global caller compiles")) return false;
    LinkedProgram globalLinked = {};
    Diagnostics globalLinkDiagnostics;
    if (!require(link_modules(globalModules, 3, &globalLinked, globalLinkDiagnostics),
                 "cross-file pointer global links")) return false;
    const int32_t answer = find_export(globalLinked, "answer");
    const int32_t addGlobal = find_export(globalLinked, "add_global");
    if (!require(answer >= 0 && addGlobal >= 0 && globalModules[1].relocationCount != 0,
                 "cross-file pointer global retains data relocation")) return false;

    const char* pointerMathSource =
        "int add_two(int* p) { *p = *p + 2; return *p; }\n";
    const char* pointerMainSource =
        "int add_two(int* p); int gx_main(gx_app_context* ctx) { int value = 40; add_two(&value); return value; }\n";
    static CompiledModule pointerModules[2] = {};
    Diagnostics pointerDiagnostics[2];
    if (!require(compile_text("src/main.cpp", pointerMainSource, &pointerModules[0], &pointerDiagnostics[0]),
                 "cross-file pointer parameter caller compiles") ||
        !require(compile_text("src/math.cpp", pointerMathSource, &pointerModules[1], &pointerDiagnostics[1]),
                 "cross-file pointer parameter definition compiles")) return false;
    LinkedProgram pointerLinked = {};
    Diagnostics pointerLinkDiagnostics;
    if (!require(link_modules(pointerModules, 2, &pointerLinked, pointerLinkDiagnostics),
                 "cross-file pointer parameter links")) return false;
    const int32_t addTwo = find_export(pointerLinked, "add_two");
    if (!require(addTwo >= 0 && pointerLinked.exports[addTwo].parameterCount == 1 &&
                 pointerLinked.exports[addTwo].parameterKinds[0] == ParameterKind::Int32Pointer,
                 "linked pointer parameter signature is retained")) return false;

    const char* mismatchMain =
        "int f(int x); int gx_main(gx_app_context* ctx) { return f(1); }\n";
    const char* mismatchMath = "int f(int* x) { return *x; }\n";
    static CompiledModule mismatchModules[2] = {};
    Diagnostics mismatchDiagnostics[2];
    if (!require(compile_text("src/main.cpp", mismatchMain, &mismatchModules[0], &mismatchDiagnostics[0]) &&
                 compile_text("src/math.cpp", mismatchMath, &mismatchModules[1], &mismatchDiagnostics[1]),
                 "pointer signature mismatch inputs compile")) return false;
    LinkedProgram mismatchLinked = {};
    Diagnostics mismatchLinkDiagnostics;
    if (!require(!link_modules(mismatchModules, 2, &mismatchLinked, mismatchLinkDiagnostics) &&
                 diagnostic_contains(mismatchLinkDiagnostics, "conflicting declaration for function"),
                 "pointer signature mismatch is rejected by linker")) return false;

    uint8_t object[COMPILER_MAX_OBJECT_BYTES] = {};
    uint8_t objectAgain[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t objectBytes = 0;
    uint32_t objectAgainBytes = 0;
    if (!require(serialize_gxo_object(pointerModules[1], object, sizeof(object), &objectBytes) &&
                 serialize_gxo_object(pointerModules[1], objectAgain, sizeof(objectAgain), &objectAgainBytes) &&
                 objectBytes == objectAgainBytes && std::memcmp(object, objectAgain, objectBytes) == 0,
                 "pointer object is deterministic")) return false;
    CompiledModule restored = {};
    Diagnostics objectDiagnostics;
    if (!require(deserialize_gxo_object(object, objectBytes, &restored, objectDiagnostics) &&
                 restored.exports[0].parameterKinds[0] == ParameterKind::Int32Pointer,
                 "pointer object round trip preserves signature")) return false;
    GxoObjectHeaderView header = {};
    Diagnostics headerDiagnostics;
    return require(inspect_gxo_header(object, objectBytes, &header, headerDiagnostics) &&
                   header.compilerObjectAbiVersion == COMPILER_OBJECT_ABI_VERSION,
                   "pointer object ABI version is explicit");
}

} // namespace

int main()
{
    if (!valid_pointer_programs() || !diagnostics_and_ir() || !module_pointers()) return 1;
    std::puts("phase27r_pointer_host=PASS");
    std::puts("compiler_pointers_host_test: PASS");
    return 0;
}
