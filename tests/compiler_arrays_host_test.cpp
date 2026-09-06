// Focused Phase 27Q host proof for fixed signed-int arrays and bounded indexing.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "core/compiler/compiler_module.h"
#include "core/compiler/compiler_linker.h"
#include "core/compiler/compiler_object.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace kernel::compiler;

namespace {

static bool require(bool value, const char* message)
{
    if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

static bool has_expression(const TranslationUnitIR& unit, ExpressionKind kind)
{
    for (uint32_t f = 0; f < unit.functionCount; ++f)
        for (uint32_t e = 0; e < unit.functions[f].expressionCount; ++e)
            if (unit.functions[f].expressions[e].kind == kind) return true;
    return false;
}

static bool has_statement(const TranslationUnitIR& unit, StatementKind kind)
{
    for (uint32_t f = 0; f < unit.functionCount; ++f)
        for (uint32_t s = 0; s < unit.functions[f].statementCount; ++s)
            if (unit.functions[f].statements[s].kind == kind) return true;
    return false;
}

static bool parse_text(const char* source, TranslationUnitIR* unit, Diagnostics* diagnostics)
{
    Token tokens[COMPILER_MAX_TOKENS] = {};
    uint32_t tokenCount = 0;
    const bool lexed = lex_source(source, static_cast<uint32_t>(std::strlen(source)), tokens,
                      COMPILER_MAX_TOKENS, &tokenCount, *diagnostics) &&
        parse_translation_unit(source, tokens, tokenCount, unit, *diagnostics);
    return lexed;
}

struct HostCallResult { int32_t value; uint32_t status; uint32_t depth; };

static HostCallResult invoke(void* entry)
{
    HostCallResult result = {};
#if defined(__GNUC__) || defined(__clang__)
    asm volatile(
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "subq $64, %%rsp\n\t"
        "movq %[out], 32(%%rsp)\n\t"
        "xorq %%rcx, %%rcx\n\t"
        "call *%[entry]\n\t"
        "movq 32(%%rsp), %%r11\n\t"
        "movl %%r15d, %%r10d\n\t"
        "movl %%r10d, (%%r11)\n\t"
        "andl $255, %%r10d\n\t"
        "movl %%r10d, 4(%%r11)\n\t"
        "shrl $8, %%r15d\n\t"
        "movl %%r15d, 8(%%r11)\n\t"
        "addq $64, %%rsp\n\t"
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        : "=a"(result.value)
        : [entry] "r"(entry), [out] "r"(&result)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
#else
    result.value = reinterpret_cast<int32_t (*)(void*)>(entry)(nullptr);
#endif
    return result;
}

static bool emit_local(const char* source, uint8_t* code, uint32_t* codeBytes,
                       uint32_t* entryOffset, TranslationUnitIR* unit)
{
    Diagnostics diagnostics;
    if (!parse_text(source, unit, &diagnostics)) return false;
    return amd64::emit_translation_unit(*unit, 0, code, COMPILER_MAX_CODE_BYTES,
                                        codeBytes, entryOffset);
}

static bool test_parser_and_local_execution()
{
    const char* source =
        "int gx_main(gx_app_context* ctx) { int values[4]; int i = 0; "
        "while (i < 4) { values[i] = i + 10; i = i + 1; } "
        "return values[0] + values[1] + values[2] + values[3] - 4; }";
    static TranslationUnitIR unit = {};
    uint8_t code[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t codeBytes = 0, entryOffset = 0;
    if (!require(emit_local(source, code, &codeBytes, &entryOffset, &unit),
                 "dynamic local array source emits")) return false;
    const FunctionIR& function = unit.functions[0];
    if (!require(function.localCount == 2 && function.localStorageBytes == 20 &&
                 function.locals[0].kind == StorageKind::ArrayInt &&
                 function.locals[0].elementCount == 4 &&
                 has_expression(unit, ExpressionKind::LoadIndexed) &&
                 has_statement(unit, StatementKind::StoreIndexed),
                 "local arrays have explicit storage and indexed IR")) return false;
    amd64::FrameLayout frame = {};
    if (!require(amd64::calculate_frame_layout(0, function.localStorageBytes / 4U,
                                               function.maxTemporarySlots, true, &frame) &&
                 frame.localBytes == 20 && frame.contextDisplacement == -28,
                 "local array frame layout is deterministic")) return false;
#if defined(_WIN32)
    DWORD oldProtection = 0;
    if (!require(VirtualProtect(code, sizeof(code), PAGE_EXECUTE_READWRITE, &oldProtection) != 0,
                 "host array code is executable for the isolated host proof")) return false;
#endif
    HostCallResult result = invoke(code + entryOffset);
    return require(result.value == 42 && result.status == 0,
                   "dynamic local array execution returns 42");
}

static bool test_bounds_and_diagnostics()
{
    const char* oob = "int gx_main(gx_app_context* ctx) { int a[4]; int i = 4; return a[i]; }";
    static TranslationUnitIR unit = {};
    uint8_t code[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t codeBytes = 0, entryOffset = 0;
    if (!require(emit_local(oob, code, &codeBytes, &entryOffset, &unit),
                 "dynamic OOB source emits")) return false;
    HostCallResult result = invoke(code + entryOffset);
    if (!require(result.value == 0 && result.status == COMPILER_RUNTIME_STATUS_ARRAY_BOUNDS,
                 "dynamic OOB returns the distinct bounds status")) return false;

    const char* negativeOob = "int gx_main(gx_app_context* ctx) { int a[4]; int i = -1; return a[i]; }";
    if (!require(emit_local(negativeOob, code, &codeBytes, &entryOffset, &unit),
                 "negative dynamic OOB source emits")) return false;
    result = invoke(code + entryOffset);
    if (!require(result.value == 0 && result.status == COMPILER_RUNTIME_STATUS_ARRAY_BOUNDS,
                 "negative dynamic OOB returns the distinct bounds status")) return false;

    const char* invalid[] = {
        "int gx_main(gx_app_context* ctx) { int a[0]; return 0; }",
        "int gx_main(gx_app_context* ctx) { int a[4]; return a[4]; }",
        "int gx_main(gx_app_context* ctx) { int a[4]; return a; }",
        "int sum(int a[4]) { return 0; } int gx_main(gx_app_context* ctx) { return 0; }",
        "int gx_main(gx_app_context* ctx) { int a[4]; int b[4]; a = b; return 0; }",
    };
    for (const char* text : invalid) {
        Diagnostics diagnostics;
        static TranslationUnitIR rejected = {};
        if (!require(!parse_text(text, &rejected, &diagnostics) && diagnostics.count() != 0,
                     "array safety diagnostic is emitted")) return false;
    }
    return true;
}

static bool test_global_link_and_object()
{
    const char* state = "int values[4] = {10, 11, 12, 9};";
    const char* main = "extern int values[4]; int gx_main(gx_app_context* ctx) { return values[0] + values[1] + values[2] + values[3]; }";
    static CompiledModule modules[2] = {};
    Diagnostics diagnostics;
    if (!require(compile_module_from_source("src/main.cpp", main, static_cast<uint32_t>(std::strlen(main)),
                                            &modules[0], diagnostics), "array importer compiles")) {
        return false;
    }
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/state.cpp", state, static_cast<uint32_t>(std::strlen(state)),
                                            &modules[1], diagnostics), "array definition compiles")) return false;
    if (!require(modules[1].mutableDataBytes == 16 && modules[1].exports[0].kind == SymbolKind::DataArray &&
                 modules[0].imports[0].kind == SymbolKind::DataArray &&
                 modules[0].relocationCount != 0,
                 "global array metadata and base relocation are present")) return false;
    LinkedProgram linked = {};
    diagnostics = Diagnostics();
    if (!require(link_modules(modules, 2, &linked, diagnostics) && linked.mutableDataBytes == 16,
                 "cross-file global array links")) return false;
    uint8_t object[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t objectBytes = 0;
    if (!require(serialize_gxo_object(modules[1], object, sizeof(object), &objectBytes),
                 "array object serializes")) return false;
    uint8_t objectAgain[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t objectAgainBytes = 0;
    if (!require(serialize_gxo_object(modules[1], objectAgain, sizeof(objectAgain), &objectAgainBytes) &&
                 objectAgainBytes == objectBytes &&
                 std::memcmp(object, objectAgain, objectBytes) == 0,
                 "identical array modules have deterministic object bytes")) return false;
    static CompiledModule roundTrip = {};
    diagnostics = Diagnostics();
    if (!require(deserialize_gxo_object(object, objectBytes, &roundTrip, diagnostics) &&
                 roundTrip.exports[0].kind == SymbolKind::DataArray &&
                 roundTrip.exports[0].elementCount == 4 && roundTrip.mutableDataBytes == 16,
                 "array object round trip preserves signature and data")) return false;

    const char* mismatch = "extern int values[8]; int gx_main(gx_app_context* ctx) { return values[0]; }";
    static CompiledModule bad = {};
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/bad.cpp", mismatch, static_cast<uint32_t>(std::strlen(mismatch)),
                                            &bad, diagnostics), "mismatched importer compiles independently")) return false;
    CompiledModule mismatchModules[2] = {bad, modules[1]};
    diagnostics = Diagnostics();
    if (!require(!link_modules(mismatchModules, 2, &linked, diagnostics),
                 "array signature mismatch is rejected at link")) return false;

    const char* scalarState = "int values;";
    static CompiledModule scalar = {};
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/scalar.cpp", scalarState,
                                            static_cast<uint32_t>(std::strlen(scalarState)),
                                            &scalar, diagnostics),
                 "scalar conflict definition compiles independently")) return false;
    CompiledModule scalarConflictModules[2] = {modules[0], scalar};
    diagnostics = Diagnostics();
    return require(!link_modules(scalarConflictModules, 2, &linked, diagnostics),
                   "scalar and array global conflict is rejected at link");
}

} // namespace

int main()
{
    if (!test_parser_and_local_execution() || !test_bounds_and_diagnostics() ||
        !test_global_link_and_object()) return 1;
    std::puts("compiler_arrays_host_test: PASS");
    return 0;
}
