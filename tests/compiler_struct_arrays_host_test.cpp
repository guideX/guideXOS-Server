// Focused host proof for Phase 27U arrays of structs and struct-pointer traversal.

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
    if (!parse_text(source, &unit, &diagnostics)) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "diagnostic: %s\n", diagnostics.at(i).message);
        return false;
    }
    uint8_t code[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t codeBytes = 0;
    uint32_t entryOffset = 0;
    if (!amd64::emit_translation_unit(unit, 0, code, sizeof(code), &codeBytes, &entryOffset)) {
        std::fprintf(stderr, "backend rejected struct-array test\n");
        return false;
    }
#if defined(_WIN32)
    DWORD oldProtection = 0;
    if (!VirtualProtect(code, sizeof(code), PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
#endif
    const CallResult result = invoke(code + entryOffset);
    if (observed) *observed = result;
    if (parsed) *parsed = unit;
    if (result.value != expected)
        std::fprintf(stderr, "observed %d expected %d status %u\n", result.value, expected, result.status);
    return result.value == expected;
}

static bool rejected(const char* source, const char* needle)
{
    TranslationUnitIR unit = {};
    Diagnostics diagnostics;
    return !parse_text(source, &unit, &diagnostics) && diagnostic_contains(diagnostics, needle);
}

static bool local_traversal()
{
    const char* indexedOnly =
        "struct Point { int x; int y; int tag; }; "
        "int gx_main(gx_app_context* ctx) { struct Point points[3]; points[0].x = 42; return points[0].x; }";
    if (!require(compile_and_run(indexedOnly, 42), "indexed struct field load/store compiles")) return false;
    const char* pointerIndexOnly =
        "struct Point { int x; int y; int tag; }; "
        "int gx_main(gx_app_context* ctx) { struct Point points[3]; struct Point* p; "
        "points[1].x = 42; p = &points[1]; return p->x; }";
    if (!require(compile_and_run(pointerIndexOnly, 42), "indexed struct address creates a valid pointer")) return false;
    const char* pointerOnly =
        "struct Point { int x; int y; int tag; }; "
        "int gx_main(gx_app_context* ctx) { struct Point points[3]; struct Point* p; "
        "points[0].x = 10; points[1].x = 20; p = &points[0]; p = p + 1; p->x = 42; return p->x; }";
    if (!require(compile_and_run(pointerOnly, 42), "struct pointer arithmetic and arrow access compile")) return false;
    const char* source =
        "struct Point { int x; int y; int tag; }; "
        "int gx_main(gx_app_context* ctx) { "
        "struct Point points[3]; "
        "points[0].x = 11; points[1].x = 21; points[2].x = 31; "
        "return points[0].x + points[1].x + points[2].x; }";
    TranslationUnitIR unit = {};
    if (!require(compile_and_run(source, 63, nullptr, &unit),
                 "three independent struct elements traverse through scaled struct pointers")) return false;
    if (!require(unit.structTypeCount == 1 && unit.structTypes[0].sizeBytes == 12 &&
                 unit.functions[0].locals[0].kind == StorageKind::ArrayStruct &&
                 unit.functions[0].locals[0].elementCount == 3 &&
                 unit.functions[0].locals[0].elementSize == 12 &&
                 unit.functions[0].locals[0].sizeBytes == 36,
                 "struct array storage records count, stride, and total size")) return false;
    return true;
}

static bool nested_addresses_and_globals()
{
    const char* nested =
        "struct Point { int x; int y; int tag; }; "
        "int gx_main(gx_app_context* ctx) { struct Point points[3]; int* px; "
        "points[1].x = 2; px = &points[1].x; *px = 99; return points[1].x; }";
    if (!require(compile_and_run(nested, 99),
                 "address of indexed struct field aliases the indexed field")) return false;

    const char* global =
        "struct Point { int x; int y; int tag; }; struct Point global_points[3]; "
        "int gx_main(gx_app_context* ctx) { global_points[1].x = 40; "
        "return global_points[1].x; }";
    CompiledModule module = {};
    Diagnostics diagnostics;
    if (!require(compile_module_from_source("src/global.cpp", global,
                 static_cast<uint32_t>(std::strlen(global)), &module, diagnostics),
                 "global struct array module compiles")) return false;
    if (!require(module.mutableDataBytes == 36 && module.exports[1].kind == SymbolKind::DataStruct &&
                 module.exports[1].elementCount == 3 && module.exports[1].elementSize == 12 &&
                 module.exports[1].size == 36,
                 "global struct array is represented as zero-filled DataStruct storage")) return false;
    return true;
}

static bool objects_and_cross_file()
{
    const char* worker =
        "struct Point { int x; int y; int tag; }; "
        "int point_value(struct Point* p) { return p->x + p->y + p->tag; }";
    const char* main =
        "struct Point { int x; int y; int tag; }; "
        "int point_value(struct Point* p); "
        "int gx_main(gx_app_context* ctx) { struct Point points[3]; "
        "points[0].x = 10; points[0].y = 20; points[0].tag = 12; "
        "return point_value(&points[0]); }";
    CompiledModule modules[2] = {};
    Diagnostics diagnostics;
    if (!require(compile_module_from_source("src/worker.cpp", worker,
                 static_cast<uint32_t>(std::strlen(worker)), &modules[0], diagnostics),
                 "cross-file struct-pointer worker compiles")) return false;
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/main.cpp", main,
                 static_cast<uint32_t>(std::strlen(main)), &modules[1], diagnostics),
                 "cross-file struct-pointer caller compiles")) return false;
    uint8_t first[COMPILER_MAX_OBJECT_BYTES] = {};
    uint8_t second[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t firstBytes = 0, secondBytes = 0;
    if (!require(serialize_elf_object(modules[1], first, sizeof(first), &firstBytes) &&
                 serialize_elf_object(modules[1], second, sizeof(second), &secondBytes) &&
                 firstBytes == secondBytes && std::memcmp(first, second, firstBytes) == 0,
                 "struct-array object output is deterministic")) return false;
    CompiledModule restored = {};
    diagnostics = Diagnostics();
    if (!require(deserialize_elf_object(first, firstBytes, &restored, diagnostics) &&
                 restored.structTypeCount == 1 && restored.structTypes[0].sizeBytes == 12,
                 "struct-array object reopens with layout metadata")) return false;
    CompiledModule linkedModules[2] = {modules[0], restored};
    LinkedProgram program = {};
    diagnostics = Diagnostics();
    return require(link_modules(linkedModules, 2, &program, diagnostics) && program.structTypeCount == 1,
                   "reopened struct-array object links across a struct-pointer boundary");
}

static bool diagnostics_and_bounds()
{
    if (!require(rejected("int gx_main(gx_app_context* c) { int x; return x->field; }", "arrow access requires"),
                 "integer arrow base is rejected")) return false;
    if (!require(rejected("int gx_main(gx_app_context* c) { int* p; return p->field; }", "arrow access requires"),
                 "int pointer arrow base is rejected")) return false;
    if (!require(rejected("struct Point { int x; }; int gx_main(gx_app_context* c) { struct Point* p; return p->nope; }", "struct has no field"),
                 "missing pointer field is rejected")) return false;
    if (!require(rejected("struct Point { int x; }; int gx_main(gx_app_context* c) { struct Point points[3]; return points[3].x; }", "out of bounds"),
                 "constant struct-array index bounds are rejected")) return false;
    if (!require(rejected("struct Point { int x; }; int gx_main(gx_app_context* c) { struct Point points[65]; return 0; }", "array length"),
                 "oversized struct array is rejected before code generation")) return false;
    return true;
}

} // namespace

int main()
{
    if (!local_traversal() || !nested_addresses_and_globals() ||
        !objects_and_cross_file() || !diagnostics_and_bounds()) return 1;
    std::puts("[DeveloperStudio] Phase 27U struct array layout: PASS");
    std::puts("[DeveloperStudio] Phase 27U indexed fields: PASS");
    std::puts("[DeveloperStudio] Phase 27U struct pointer: PASS");
    std::puts("[DeveloperStudio] Phase 27U pointer scaling: PASS");
    std::puts("[DeveloperStudio] Phase 27U arrow load/store: PASS");
    std::puts("[DeveloperStudio] Phase 27U element isolation: PASS");
    std::puts("[DeveloperStudio] Phase 27U object reopen: PASS");
    std::puts("[DeveloperStudio] Phase 27U native execution: PASS");
    std::puts("DEVELOPER_STUDIO_PHASE27U_HOST_PASS");
    return 0;
}
