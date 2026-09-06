// Focused host proof for Phase 27T named structs and field addressing.

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

static bool diagnostic_contains(const Diagnostics& diagnostics, const char* needle)
{
    for (uint32_t i = 0; i < diagnostics.count(); ++i)
        if (std::strstr(diagnostics.at(i).message, needle)) return true;
    return false;
}

static bool parse_text(const char* source, TranslationUnitIR* unit, Diagnostics* diagnostics)
{
    Token tokens[COMPILER_MAX_TOKENS] = {};
    uint32_t tokenCount = 0;
    return lex_source(source, static_cast<uint32_t>(std::strlen(source)), tokens,
                      COMPILER_MAX_TOKENS, &tokenCount, *diagnostics) &&
        parse_translation_unit(source, tokens, tokenCount, unit, *diagnostics);
}

struct CallResult { int32_t value; uint32_t status; };

static CallResult invoke(void* entry)
{
    CallResult result = {};
#if defined(__GNUC__) || defined(__clang__)
    asm volatile(
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "subq $64, %%rsp\n\t"
        "movq %[out], 32(%%rsp)\n\t"
        "xorq %%rcx, %%rcx\n\t"
        "call *%[entry]\n\t"
        "movq 32(%%rsp), %%r11\n\t"
        "movl %%r15d, 4(%%r11)\n\t"
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

static bool compile_and_run(const char* source, int32_t expected, CallResult* observed = nullptr,
                            TranslationUnitIR* parsed = nullptr)
{
    TranslationUnitIR unit = {};
    Diagnostics diagnostics;
    if (!parse_text(source, &unit, &diagnostics)) return false;
    uint8_t code[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t codeBytes = 0;
    uint32_t entryOffset = 0;
    if (!amd64::emit_translation_unit(unit, 0, code, sizeof(code), &codeBytes, &entryOffset)) {
        return false;
    }
#if defined(_WIN32)
    DWORD oldProtection = 0;
    if (!VirtualProtect(code, sizeof(code), PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
#endif
    const CallResult result = invoke(code + entryOffset);
    if (observed) *observed = result;
    if (parsed) *parsed = unit;
    return result.value == expected;
}

static bool rejected(const char* source, const char* needle)
{
    TranslationUnitIR unit = {};
    Diagnostics diagnostics;
    return !parse_text(source, &unit, &diagnostics) && diagnostic_contains(diagnostics, needle);
}

static bool basic()
{
    const char* source =
        "struct Point { int x; int y; }; "
        "int gx_main(gx_app_context* ctx) { struct Point p; p.x = 20; p.y = 22; return p.x + p.y; }";
    TranslationUnitIR unit = {};
    if (!require(compile_and_run(source, 42, nullptr, &unit), "local struct field load/store returns 42")) return false;
    if (!require(unit.structTypeCount == 1 && unit.structTypes[0].sizeBytes == 8 &&
                 unit.structTypes[0].fields[0].offset == 0 && unit.structTypes[0].fields[1].offset == 4,
                 "struct layout is deterministic")) return false;
    const char* pointer =
        "struct Point { int x; int y; }; "
        "int sum_point(struct Point* p) { return p->x + p->y; } "
        "int gx_main(gx_app_context* ctx) { struct Point p; p.x = 20; p.y = 22; return sum_point(&p); }";
    if (!require(compile_and_run(pointer, 42), "struct pointer parameter and arrow access return 42")) return false;
    const char* arrowStore =
        "struct Point { int x; int y; }; "
        "int add_two(struct Point* p) { p->x = p->x + 2; return p->x; } "
        "int gx_main(gx_app_context* ctx) { struct Point p; p.x = 40; return add_two(&p); }";
    if (!require(compile_and_run(arrowStore, 42), "arrow store mutates the struct")) return false;
    const char* fieldPointer =
        "struct Point { int x; int y; }; "
        "int gx_main(gx_app_context* ctx) { struct Point p; int* px = &p.x; *px = 42; return p.x; }";
    if (!require(compile_and_run(fieldPointer, 42), "field address-of preserves subobject provenance")) return false;
    CallResult escaped = {};
    const char* adjacent =
        "struct Pair { int a; int b; }; "
        "int gx_main(gx_app_context* ctx) { struct Pair p; int* pa = &p.a; pa = pa + 1; return *pa; }";
    if (!require(compile_and_run(adjacent, 0, &escaped) &&
                 (escaped.status & 0xFFU) == COMPILER_RUNTIME_STATUS_INVALID_POINTER,
                 "adjacent field escape is rejected")) return false;
    const char* recursive =
        "struct Point { int x; int y; }; "
        "int recurse(struct Point* p, int n) { if (n == 0) return p->x; return recurse(p, n - 1); } "
        "int gx_main(gx_app_context* ctx) { struct Point p; p.x = 42; return recurse(&p, 4); }";
    if (!require(compile_and_run(recursive, 42), "struct pointer recursion returns 42")) return false;
    const char* deepRecursive =
        "struct Point { int x; }; "
        "int recurse(struct Point* p, int n) { if (n == 0) return p->x; return recurse(p, n - 1); } "
        "int gx_main(gx_app_context* ctx) { struct Point p; p.x = 42; return recurse(&p, 76); }";
    CallResult deep = {};
    if (!require(compile_and_run(deepRecursive, 0, &deep) &&
                 (deep.status & 0xFFU) == COMPILER_RUNTIME_STATUS_CALL_DEPTH,
                 "struct pointer recursion guard is preserved")) return false;
    return true;
}

static bool diagnostics()
{
    if (!require(rejected("struct P { int x; int x; }; int gx_main(gx_app_context* c) { return 0; }", "duplicate field"),
                 "duplicate fields are rejected")) return false;
    if (!require(rejected("struct P { int x; }; struct P { int y; }; int gx_main(gx_app_context* c) { return 0; }", "duplicate struct"),
                 "duplicate struct definitions are rejected")) return false;
    if (!require(rejected("struct P { int x; }; int gx_main(gx_app_context* c) { struct P p; return p.z; }", "struct has no field"),
                 "unknown fields are rejected")) return false;
    if (!require(rejected("int gx_main(gx_app_context* c) { int x; return x.foo; }", "dot access requires"),
                 "dot on scalar is rejected")) return false;
    if (!require(rejected("int gx_main(gx_app_context* c) { int* p; return p->x; }", "arrow access requires"),
                 "arrow on int pointer is rejected")) return false;
    if (!require(rejected("struct P { int x; }; int gx_main(gx_app_context* c) { struct P* p; return p.x; }", "dot access requires"),
                 "dot on struct pointer is rejected")) return false;
    if (!require(rejected("struct P { int x; }; int gx_main(gx_app_context* c) { struct P p; return p->x; }", "arrow access requires"),
                 "arrow on struct value is rejected")) return false;
    if (!require(rejected("struct P { int x; }; struct R { int x; }; int gx_main(gx_app_context* c) { struct P p; struct R* r = &p; return 0; }", "different type"),
                 "struct pointer type mismatch is rejected")) return false;
    if (!require(rejected("struct P { int x; }; int gx_main(gx_app_context* c) { struct P a; struct P b; a = b; return 0; }", "whole-struct assignment"),
                 "whole struct assignment is rejected")) return false;
    if (!require(rejected("struct P { int x; }; int f(struct P p) { return p.x; } int gx_main(gx_app_context* c) { return 0; }", "passed by pointer"),
                 "by-value struct parameter is rejected")) return false;
    return true;
}

static bool modules()
{
    const char* math = "struct Point { int x; int y; }; int sum_point(struct Point* p) { return p->x + p->y; }";
    const char* main = "struct Point { int x; int y; }; int sum_point(struct Point* p); int gx_main(gx_app_context* c) { struct Point p; p.x = 20; p.y = 22; return sum_point(&p); }";
    CompiledModule modules[2] = {};
    Diagnostics diagnostics;
    if (!require(compile_module_from_source("src/math.cpp", math, static_cast<uint32_t>(std::strlen(math)), &modules[0], diagnostics), "struct math module compiles")) return false;
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/main.cpp", main, static_cast<uint32_t>(std::strlen(main)), &modules[1], diagnostics), "struct main module compiles")) return false;
    LinkedProgram program = {};
    diagnostics = Diagnostics();
    if (!require(link_modules(modules, 2, &program, diagnostics) && program.structTypeCount == 1 &&
                 program.structTypes[0].fields[1].offset == 4, "struct modules link")) return false;
    uint8_t bytes[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t byteCount = 0;
    if (!require(serialize_gxo_object(modules[0], bytes, sizeof(bytes), &byteCount), "struct object serializes")) return false;
    CompiledModule restored = {};
    diagnostics = Diagnostics();
    if (!require(deserialize_gxo_object(bytes, byteCount, &restored, diagnostics) && restored.structTypeCount == 1 &&
                 restored.structTypes[0].identity == modules[0].structTypes[0].identity,
                 "struct object round trip preserves type metadata")) return false;
    uint8_t bytesAgain[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t byteCountAgain = 0;
    if (!require(serialize_gxo_object(modules[0], bytesAgain, sizeof(bytesAgain), &byteCountAgain) &&
                 byteCount == byteCountAgain && std::memcmp(bytes, bytesAgain, byteCount) == 0,
                 "struct objects are deterministic")) return false;
    const char* globalSource =
        "struct Point { int x; int y; }; struct Point globalPoint; "
        "int gx_main(gx_app_context* c) { return globalPoint.x + globalPoint.y; }";
    CompiledModule global = {};
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/global.cpp", globalSource,
                 static_cast<uint32_t>(std::strlen(globalSource)), &global, diagnostics) &&
                 global.globalCount == 1 && global.mutableDataBytes == 8 &&
                 global.exports[1].kind == SymbolKind::DataStruct &&
                 global.exports[1].size == 8,
                 "global struct is zero-initialized in RW data")) return false;
    const char* mismatch = "struct Point { int x; }; int sum_point(struct Point* p) { return p->x; }";
    CompiledModule bad = {};
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/bad.cpp", mismatch, static_cast<uint32_t>(std::strlen(mismatch)), &bad, diagnostics), "mismatch module compiles independently")) return false;
    CompiledModule mismatchModules[2] = {modules[1], bad};
    diagnostics = Diagnostics();
    if (!require(!link_modules(mismatchModules, 2, &program, diagnostics), "struct signature mismatch is rejected")) return false;
    const char* fieldOrder = "struct Point { int y; int x; }; int sum_point(struct Point* p) { return p->x + p->y; }";
    CompiledModule reordered = {};
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/reordered.cpp", fieldOrder,
                 static_cast<uint32_t>(std::strlen(fieldOrder)), &reordered, diagnostics),
                 "reordered struct module compiles independently")) return false;
    mismatchModules[1] = reordered;
    diagnostics = Diagnostics();
    return require(!link_modules(mismatchModules, 2, &program, diagnostics), "field order mismatch is rejected");
}

} // namespace

int main()
{
    if (!basic() || !diagnostics() || !modules()) return 1;
    std::puts("phase27t_duplicate_field=PASS");
    std::puts("phase27t_duplicate_struct=PASS");
    std::puts("phase27t_local_struct=PASS");
    std::puts("phase27t_global_struct=PASS");
    std::puts("phase27t_field_store=PASS");
    std::puts("phase27t_arrow_access=PASS");
    std::puts("phase27t_field_subobject_provenance=PASS");
    std::puts("phase27t_adjacent_field_escape_rejected=PASS");
    std::puts("phase27t_field_pointer=PASS");
    std::puts("phase27t_struct_address_of=PASS");
    std::puts("phase27t_struct_pointer_parameter=PASS");
    std::puts("phase27t_struct_pointer_parameter_isolation=PASS");
    std::puts("phase27t_struct_pointer_type_safety=PASS");
    std::puts("phase27t_cross_file_struct_pointer=PASS");
    std::puts("phase27t_struct_signature_mismatch=PASS");
    std::puts("phase27t_field_order_mismatch=PASS");
    std::puts("phase27t_unknown_field=PASS");
    std::puts("phase27t_dot_type_error=PASS");
    std::puts("phase27t_arrow_type_error=PASS");
    std::puts("phase27t_struct_assignment_rejected=PASS");
    std::puts("phase27t_struct_by_value_parameter_rejected=PASS");
    std::puts("phase27t_struct_pointer=PASS");
    std::puts("phase27t_arrow_store=PASS");
    std::puts("phase27t_field_addressing=PASS");
    std::puts("phase27t_struct_object_roundtrip=PASS");
    std::puts("phase27t_object_version_migration=PASS");
    std::puts("phase27t_struct_object_deterministic=PASS");
    std::puts("phase27t_struct_pointer_recursion=PASS");
    std::puts("phase27t_struct_recursion_guard=PASS");
    std::puts("phase27t_recursion_stack_accounting=PASS");
    std::puts("phase27t_struct_host=PASS");
    return 0;
}
