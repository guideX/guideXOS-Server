// Focused host checks for Phase 27N's bounded module compiler and linker.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_linker.h"
#include "core/compiler/compiler_module.h"
#include "core/compiler/elf_writer.h"

#include <cstdio>
#include <cstring>

using namespace kernel::compiler;

namespace {

static bool require(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool diagnostic_contains(const Diagnostics& diagnostics, const char* needle)
{
    if (!needle) return false;
    for (uint32_t i = 0; i < diagnostics.count(); ++i)
        if (diagnostics.at(i).message && std::strstr(diagnostics.at(i).message, needle)) return true;
    return false;
}

static bool compile_text(const char* path, const char* source, CompiledModule* module,
                         Diagnostics* diagnostics)
{
    if (!path || !source || !module || !diagnostics) return false;
    return compile_module_from_source(path, source, static_cast<uint32_t>(std::strlen(source)),
                                      module, *diagnostics);
}

static int32_t read_rel32(const uint8_t* bytes, uint32_t offset)
{
    const uint32_t value = static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    return static_cast<int32_t>(value);
}

static bool same_bytes(const uint8_t* left, const uint8_t* right, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; ++i) if (left[i] != right[i]) return false;
    return true;
}

static int32_t find_export(const LinkedProgram& program, const char* name)
{
    for (uint32_t i = 0; i < program.exportCount; ++i)
        if (std::strcmp(program.exports[i].name, name) == 0) return static_cast<int32_t>(i);
    return -1;
}

static bool test_link_and_reproducibility()
{
    const char* mainSource =
        "int add(int a, int b); int add(int left, int right);\n"
        "int gx_main(gx_app_context* ctx) { return add(20, 22); }\n";
    const char* mathSource = "int add(int a, int b) { return a + b; }\n";
    CompiledModule mainModule = {};
    CompiledModule mathModule = {};
    Diagnostics mainDiagnostics;
    Diagnostics mathDiagnostics;
    if (!require(compile_text("src/main.cpp", mainSource, &mainModule, &mainDiagnostics),
                 "main translation unit compiles")) return false;
    if (!compile_text("src/math.cpp", mathSource, &mathModule, &mathDiagnostics)) {
        for (uint32_t i = 0; i < mathDiagnostics.count(); ++i)
            std::fprintf(stderr, "math diagnostic: %s\n", mathDiagnostics.at(i).message);
        return require(false, "math translation unit compiles");
    }
    if (!require(mainModule.importCount == 1 && mainModule.relocationCount == 1,
                 "external call creates one import and one relocation")) return false;
    if (!require(mainModule.relocations[0].kind == RelocationKind::CallRel32 &&
                 mainModule.code[mainModule.relocations[0].patchOffset] == 0 &&
                 mainModule.code[mainModule.relocations[0].patchOffset + 1] == 0 &&
                 mainModule.code[mainModule.relocations[0].patchOffset + 2] == 0 &&
                 mainModule.code[mainModule.relocations[0].patchOffset + 3] == 0,
                 "module call displacement remains a bounded placeholder")) return false;

    CompiledModule ordered[2] = {mathModule, mainModule};
    LinkedProgram first = {};
    Diagnostics linkDiagnostics;
    if (!require(link_modules(ordered, 2, &first, linkDiagnostics),
                 "two modules link in reverse input order")) {
        for (uint32_t i = 0; i < linkDiagnostics.count(); ++i)
            std::fprintf(stderr, "link diagnostic: %s\n", linkDiagnostics.at(i).message);
        return false;
    }
    if (!require(first.linked && first.moduleCount == 2 && first.importCount == 1 &&
                 first.relocationCount == 1 && first.entryCodeOffset < first.codeBytes,
                 "linked program records bounded project metadata")) return false;
    const int32_t addIndex = find_export(first, "add");
    if (!require(addIndex >= 0, "linked export table contains add")) return false;
    const uint32_t addOffset = first.exports[addIndex].finalCodeOffset;
    bool sawPatchedCall = false;
    for (uint32_t i = 0; i + 5U <= first.codeBytes; ++i) {
        if (first.code[i] != 0xE8) continue;
        const int64_t target = static_cast<int64_t>(i + 5U) + read_rel32(first.code, i + 1U);
        if (target == static_cast<int64_t>(addOffset)) sawPatchedCall = true;
    }
    if (!require(sawPatchedCall, "CallRel32 points at the linked export")) return false;

    CompiledModule forward[2] = {mainModule, mathModule};
    LinkedProgram second = {};
    Diagnostics secondDiagnostics;
    if (!require(link_modules(forward, 2, &second, secondDiagnostics),
                 "same modules link in forward input order")) return false;
    if (!require(first.codeBytes == second.codeBytes && first.dataBytes == second.dataBytes &&
                 same_bytes(first.code, second.code, first.codeBytes) &&
                 same_bytes(first.data, second.data, first.dataBytes),
                 "module ordering is deterministic")) return false;

    uint8_t elf[BOOTSTRAP_MAX_ELF_BYTES] = {};
    ElfLayout layout = {};
    if (!require(write_bootstrap_elf(first.code, first.codeBytes, first.data, first.dataBytes,
                                     first.entryCodeOffset, elf, sizeof(elf), &layout),
                 "linked program writes final ELF")) return false;
    if (!require(layout.dataOffset == 0 && first.dataBytes == 0,
                 "data-free linked program has no data segment")) return false;
    ElfValidationResult validation = {};
    return require(validate_bootstrap_elf(elf, layout.outputBytes, layout.imageBase,
                                          layout.codeOffset, first.code, first.codeBytes,
                                          &validation, first.data, first.dataBytes,
                                          first.entryCodeOffset),
                    "linked ELF validates");
}

static void minimal_module(CompiledModule* module, const char* path, const char* name, bool entry)
{
    *module = {};
    std::strcpy(module->sourcePath, path);
    module->codeBytes = 1;
    module->code[0] = 0xC3;
    module->functionCount = 1;
    module->exportCount = 1;
    std::strcpy(module->exports[0].name, name);
    module->exports[0].moduleCodeOffset = 0;
    module->exports[0].isEntry = entry;
}

static bool test_linker_bounds_and_reset()
{
    static CompiledModule module = {};
    LinkedProgram linked = {};
    Diagnostics diagnostics;

    static CompiledModule tooMany[COMPILER_MAX_TRANSLATION_UNITS + 1] = {};
    for (uint32_t i = 0; i < COMPILER_MAX_TRANSLATION_UNITS + 1; ++i) {
        char path[32] = {};
        char name[32] = {};
        std::snprintf(path, sizeof(path), "src/many%u.cpp", i);
        std::snprintf(name, sizeof(name), i == 0 ? "gx_main" : "many_%u", i);
        minimal_module(&tooMany[i], path, name, i == 0);
    }
    if (!require(!link_modules(tooMany, COMPILER_MAX_TRANSLATION_UNITS + 1, &linked, diagnostics),
                 "translation-unit capacity is rejected")) return false;

    minimal_module(&module, "src/oversize.cpp", "gx_main", true);
    module.codeBytes = COMPILER_MAX_CODE_BYTES + 1U;
    if (!require(!link_modules(&module, 1, &linked, diagnostics),
                 "module code capacity is rejected")) return false;

    minimal_module(&module, "src/recovered.cpp", "gx_main", true);
    diagnostics = Diagnostics();
    if (!require(link_modules(&module, 1, &linked, diagnostics) && linked.linked,
                 "linker state resets after a module-capacity failure")) return false;

    static CompiledModule duplicate[2] = {};
    minimal_module(&duplicate[0], "src/a.cpp", "gx_main", true);
    minimal_module(&duplicate[1], "src/b.cpp", "gx_main", true);
    diagnostics = Diagnostics();
    if (!require(!link_modules(duplicate, 2, &linked, diagnostics) &&
                 diagnostic_contains(diagnostics, "duplicate definition for function 'gx_main'"),
                 "duplicate definitions are rejected")) return false;

    minimal_module(&module, "src/no-entry.cpp", "helper", false);
    diagnostics = Diagnostics();
    if (!require(!link_modules(&module, 1, &linked, diagnostics) &&
                 diagnostic_contains(diagnostics, "missing gx_main entry function"),
                 "missing entry is rejected")) return false;

    minimal_module(&module, "src/bad-relocation.cpp", "gx_main", true);
    module.relocationCount = 1;
    module.relocations[0] = {};
    module.relocations[0].kind = RelocationKind::CallRel32;
    module.relocations[0].width = 4;
    module.relocations[0].patchOffset = 1;
    std::strcpy(module.relocations[0].targetSymbolName, "gx_main");
    diagnostics = Diagnostics();
    if (!require(!link_modules(&module, 1, &linked, diagnostics) &&
                 diagnostic_contains(diagnostics, "relocation patch range is out of bounds"),
                 "relocation patch bounds are rejected")) return false;

    minimal_module(&module, "src/big-data.cpp", "gx_main", true);
    module.dataBytes = COMPILER_MAX_LINKED_DATA_BYTES + 1U;
    diagnostics = Diagnostics();
    if (!require(!link_modules(&module, 1, &linked, diagnostics),
                 "module data capacity is rejected")) return false;

    static CompiledModule exportModules[COMPILER_MAX_TRANSLATION_UNITS] = {};
    for (uint32_t m = 0; m < COMPILER_MAX_TRANSLATION_UNITS; ++m) {
        std::memset(&exportModules[m], 0, sizeof(exportModules[m]));
        std::snprintf(exportModules[m].sourcePath, sizeof(exportModules[m].sourcePath), "src/cap%u.cpp", m);
        exportModules[m].codeBytes = 1;
        exportModules[m].code[0] = 0xC3;
        exportModules[m].functionCount = COMPILER_MAX_FUNCTIONS;
        exportModules[m].exportCount = COMPILER_MAX_FUNCTIONS;
        for (uint32_t e = 0; e < COMPILER_MAX_FUNCTIONS; ++e) {
            std::snprintf(exportModules[m].exports[e].name, sizeof(exportModules[m].exports[e].name),
                          m == 0 && e == 0 ? "gx_main" : "cap_%u_%u", m, e);
            exportModules[m].exports[e].moduleCodeOffset = 0;
            exportModules[m].exports[e].isEntry = m == 0 && e == 0;
        }
    }
    diagnostics = Diagnostics();
    if (!require(!link_modules(exportModules, COMPILER_MAX_TRANSLATION_UNITS, &linked, diagnostics) &&
                 diagnostic_contains(diagnostics, "project export capacity exceeded"),
                 "project export capacity is rejected")) return false;

    static CompiledModule relocationModules[5] = {};
    for (uint32_t m = 0; m < 5; ++m) {
        std::memset(&relocationModules[m], 0, sizeof(relocationModules[m]));
        std::snprintf(relocationModules[m].sourcePath, sizeof(relocationModules[m].sourcePath), "src/reloc%u.cpp", m);
        relocationModules[m].codeBytes = 256;
        relocationModules[m].functionCount = 1;
        relocationModules[m].exportCount = 1;
        std::snprintf(relocationModules[m].exports[0].name, sizeof(relocationModules[m].exports[0].name),
                      m == 0 ? "gx_main" : "reloc_%u", m);
        relocationModules[m].exports[0].isEntry = m == 0;
        relocationModules[m].relocationCount = COMPILER_MAX_MODULE_RELOCATIONS;
        for (uint32_t r = 0; r < relocationModules[m].relocationCount; ++r) {
            relocationModules[m].relocations[r].kind = RelocationKind::CallRel32;
            relocationModules[m].relocations[r].width = 4;
            relocationModules[m].relocations[r].patchOffset = r * 4;
            std::strcpy(relocationModules[m].relocations[r].targetSymbolName, "reloc_1");
        }
    }
    diagnostics = Diagnostics();
    return require(!link_modules(relocationModules, 5, &linked, diagnostics) &&
                   diagnostic_contains(diagnostics, "project relocation capacity exceeded"),
                   "project relocation capacity is rejected");
}

static bool test_single_unit_mutual_recursion()
{
    const char* source =
        "int even(int n) { if (n <= 0) { return 1; } return odd(n - 1); }\n"
        "int odd(int n) { if (n <= 0) { return 0; } return even(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { return even(6) * 42; }\n";
    CompiledModule module = {};
    Diagnostics compileDiagnostics;
    if (!require(compile_text("src/mutual.cpp", source, &module, &compileDiagnostics),
                 "single-unit mutual recursion compiles")) return false;
    if (!require(module.recursiveSccCount == 1 && module.recursiveFunction[0] &&
                 module.recursiveFunction[1],
                 "single-unit mutual recursion metadata is preserved")) return false;
    LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    if (!require(link_modules(&module, 1, &linked, linkDiagnostics),
                 "single-unit mutual recursion links")) return false;
    return require(linked.recursiveSccCount == 1 && linked.recursiveFunction[0] &&
                   linked.recursiveFunction[1],
                   "single-unit mutual recursion remains recursive after link");
}

static bool test_data_relocation()
{
    const char* mainSource =
        "int gx_main(gx_app_context* ctx) { log(ctx, \"multi-file\"); return 42; }\n";
    CompiledModule module = {};
    Diagnostics diagnostics;
    if (!compile_text("src/main.cpp", mainSource, &module, &diagnostics)) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "log diagnostic: %s\n", diagnostics.at(i).message);
        return require(false, "logging translation unit compiles");
    }
    if (!require(module.dataBytes == 11 && module.relocationCount == 1 &&
                 module.relocations[0].kind == RelocationKind::DataAddress64,
                 "host log creates data-address relocation")) return false;
    LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    if (!require(link_modules(&module, 1, &linked, linkDiagnostics),
                 "logging module links")) return false;
    if (!require(linked.dataFileOffset >= BOOTSTRAP_DATA_OFFSET,
                 "linked data layout is page aligned after code")) return false;
    const RelocationRecord& relocation = module.relocations[0];
    uint64_t address = 0;
    for (uint32_t i = 0; i < 8; ++i)
        address |= static_cast<uint64_t>(linked.code[relocation.patchOffset + i]) << (i * 8);
    const uint64_t expected = BOOTSTRAP_IMAGE_BASE + linked.dataFileOffset + relocation.dataOffset;
    return require(address == expected, "DataAddress64 points at linked read-only data");
}

static bool test_cross_module_recursion()
{
    const char* mainSource =
        "int even(int n); int gx_main(gx_app_context* ctx) { return even(4); }\n";
    const char* evenSource =
        "int odd(int n); int even(int n) { if (n == 0) return 1; return odd(n - 1); }\n";
    const char* oddSource =
        "int even(int n); int odd(int n) { if (n == 0) return 0; return even(n - 1); }\n";
    CompiledModule modules[3] = {};
    Diagnostics diagnostics[3];
    if (!require(compile_text("src/main.cpp", mainSource, &modules[0], &diagnostics[0]),
                 "cross-module recursion entry compiles")) return false;
    if (!require(compile_text("src/even.cpp", evenSource, &modules[1], &diagnostics[1]),
                 "even module compiles")) return false;
    if (!require(compile_text("src/odd.cpp", oddSource, &modules[2], &diagnostics[2]),
                 "odd module compiles")) return false;

    LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    if (!require(link_modules(modules, 3, &linked, linkDiagnostics),
                 "cross-module recursion links")) {
        for (uint32_t i = 0; i < linkDiagnostics.count(); ++i)
            std::fprintf(stderr, "recursion link diagnostic: %s\n", linkDiagnostics.at(i).message);
        return false;
    }
    const int32_t evenIndex = find_export(linked, "even");
    const int32_t oddIndex = find_export(linked, "odd");
    return require(evenIndex >= 0 && oddIndex >= 0 && linked.recursiveSccCount == 1 &&
                   linked.recursiveFunction[evenIndex] && linked.recursiveFunction[oddIndex],
                   "cross-module recursion is marked as one recursive SCC");
}

static bool test_failures_and_isolation()
{
    CompiledModule bad = {};
    Diagnostics badDiagnostics;
    if (!require(!compile_text("src/bad.cpp", "int gx_main(gx_app_context* ctx) { return missing(); }",
                              &bad, &badDiagnostics) &&
                 diagnostic_contains(badDiagnostics, "unknown function 'missing'"),
                 "unknown external without declaration is rejected")) return false;
    CompiledModule good = {};
    Diagnostics goodDiagnostics;
    if (!require(compile_text("src/good.cpp", "int gx_main(gx_app_context* ctx) { return 42; }",
                              &good, &goodDiagnostics),
                 "valid module compiles after failed module")) return false;

    CompiledModule declaration = {};
    CompiledModule definition = {};
    Diagnostics declarationDiagnostics;
    Diagnostics definitionDiagnostics;
    const bool declarationOk = compile_text(
        "src/main.cpp", "int helper(int a, int b); int gx_main(gx_app_context* ctx) { return helper(1, 2); }",
        &declaration, &declarationDiagnostics);
    const bool definitionOk = compile_text(
        "src/helper.cpp", "int helper(int value) { return value; }", &definition, &definitionDiagnostics);
    if (!require(declarationOk && definitionOk, "incompatible-link test modules compile independently")) return false;
    CompiledModule mismatch[2] = {declaration, definition};
    LinkedProgram linked = {};
    Diagnostics mismatchDiagnostics;
    if (!require(!link_modules(mismatch, 2, &linked, mismatchDiagnostics) &&
                 diagnostic_contains(mismatchDiagnostics, "conflicting declaration for function 'helper'"),
                 "cross-module arity mismatch is a linker diagnostic")) return false;

    CompiledModule undefined = {};
    Diagnostics undefinedDiagnostics;
    if (!require(compile_text("src/main.cpp",
                              "int helper(int value); int gx_main(gx_app_context* ctx) { return helper(1); }",
                              &undefined, &undefinedDiagnostics),
                 "undefined-symbol main module compiles")) return false;
    Diagnostics undefinedLinkDiagnostics;
    if (!require(!link_modules(&undefined, 1, &linked, undefinedLinkDiagnostics) &&
                 diagnostic_contains(undefinedLinkDiagnostics, "undefined external function 'helper'"),
                 "undefined external is a linker diagnostic")) return false;
    return true;
}

} // namespace

int main()
{
    if (!test_link_and_reproducibility()) return 1;
    if (!test_single_unit_mutual_recursion()) return 1;
    if (!test_data_relocation()) return 1;
    if (!test_cross_module_recursion()) return 1;
    if (!test_failures_and_isolation()) return 1;
    if (!test_linker_bounds_and_reset()) return 1;
    std::puts("compiler_multifile_host_test: PASS");
    return 0;
}
