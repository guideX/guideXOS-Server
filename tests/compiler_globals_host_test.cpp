// Focused host checks for Phase 27O global data symbols and relocations.

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
    for (uint32_t i = 0; i < diagnostics.count(); ++i)
        if (diagnostics.at(i).message && std::strstr(diagnostics.at(i).message, needle)) return true;
    return false;
}

static bool compile_text(const char* path, const char* source, CompiledModule* module,
                         Diagnostics* diagnostics)
{
    return compile_module_from_source(path, source, static_cast<uint32_t>(std::strlen(source)),
                                      module, *diagnostics);
}

static uint64_t read_u64(const uint8_t* bytes, uint32_t offset)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
    return value;
}

static uint32_t read_u32(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static int32_t find_export(const LinkedProgram& program, const char* name)
{
    for (uint32_t i = 0; i < program.exportCount; ++i)
        if (std::strcmp(program.exports[i].name, name) == 0) return static_cast<int32_t>(i);
    return -1;
}

static bool test_same_file_globals()
{
    const char* source =
        "int answer = 40;\n"
        "int gx_main(gx_app_context* ctx) { answer = answer + 2; return answer; }\n";
    CompiledModule module = {};
    Diagnostics diagnostics;
    if (!require(compile_text("src/main.cpp", source, &module, &diagnostics),
                 "same-file global source compiles")) return false;
    if (!require(module.globalCount == 1 && module.mutableDataBytes == 4 &&
                 module.mutableData[0] == 40 && module.mutableData[1] == 0 &&
                 module.relocationCount == 3, "same-file global emits initialized storage and relocations")) {
        std::fprintf(stderr, "same-file fields: globals=%u mutable=%u reloc=%u code=%u\n",
                     module.globalCount, module.mutableDataBytes, module.relocationCount, module.codeBytes);
        return false;
    }
    for (uint32_t i = 0; i < module.relocationCount; ++i)
        if (!require(module.relocations[i].kind == RelocationKind::GlobalDataAddress64 &&
                     std::strcmp(module.relocations[i].targetSymbolName, "answer") == 0,
                     "same-file relocation remains symbolic global address")) return false;
    LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    if (!require(link_modules(&module, 1, &linked, linkDiagnostics), "same-file global links")) return false;
    const int32_t answer = find_export(linked, "answer");
    return require(answer >= 0 && linked.exports[answer].kind == SymbolKind::Data &&
                   linked.mutableDataBytes == 4 && linked.mutableData[0] == 40,
                   "same-file global is a linked data export");
}

static bool test_cross_file_globals_and_elf()
{
    const char* stateSource = "int answer = 40;\n";
    const char* mathSource =
        "extern int answer;\n"
        "int add_two() { answer = answer + 2; return answer; }\n";
    const char* mainSource =
        "extern int answer; int add_two();\n"
        "int gx_main(gx_app_context* ctx) { add_two(); log(ctx, \"global\"); return answer; }\n";
    static CompiledModule modules[3] = {};
    Diagnostics diagnostics[3];
    if (!require(compile_text("src/state.cpp", stateSource, &modules[0], &diagnostics[0]),
                 "data-only state module compiles")) return false;
    if (!require(compile_text("src/math.cpp", mathSource, &modules[1], &diagnostics[1]),
                 "cross-file mutator compiles")) return false;
    if (!require(compile_text("src/main.cpp", mainSource, &modules[2], &diagnostics[2]),
                 "cross-file entry compiles")) return false;
    if (!require(modules[0].codeBytes == 0 && modules[0].exportCount == 1 &&
                 modules[0].exports[0].kind == SymbolKind::Data,
                 "state.cpp is a data-only module")) return false;
    LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    if (!require(link_modules(modules, 3, &linked, linkDiagnostics), "cross-file globals link")) {
        for (uint32_t i = 0; i < linkDiagnostics.count(); ++i)
            std::fprintf(stderr, "link diagnostic: %s\n", linkDiagnostics.at(i).message);
        return false;
    }
    const int32_t answer = find_export(linked, "answer");
    const int32_t addTwo = find_export(linked, "add_two");
    if (!require(answer >= 0 && addTwo >= 0 && linked.exports[answer].kind == SymbolKind::Data &&
                 linked.mutableDataBytes == 4 && linked.mutableData[0] == 40,
                 "cross-file global and function exports resolve")) return false;
    const uint64_t expectedAddress = BOOTSTRAP_IMAGE_BASE + linked.mutableDataFileOffset +
                                     linked.exports[answer].finalDataOffset;
    uint32_t globalRelocations = 0;
    uint32_t codeOffset = 0;
    const CompiledModule* ordered[] = {&modules[2], &modules[1], &modules[0]};
    for (uint32_t m = 0; m < 3; ++m) {
        const uint32_t moduleCodeOffset = (codeOffset + 15U) & ~15U;
        for (uint32_t r = 0; r < ordered[m]->relocationCount; ++r) {
            const RelocationRecord& relocation = ordered[m]->relocations[r];
            if (relocation.kind != RelocationKind::GlobalDataAddress64) continue;
            const uint64_t patchedAddress = read_u64(linked.code, moduleCodeOffset + relocation.patchOffset);
            if (!require(relocation.width == 8 && patchedAddress == expectedAddress,
                         "global relocation patches the linked mutable-data address")) return false;
            ++globalRelocations;
        }
        codeOffset += ordered[m]->codeBytes;
    }
    if (!require(globalRelocations == 4 && linked.mutableDataFileOffset != 0,
                 "cross-file reads and writes produce global data relocations")) return false;

    uint8_t elf[BOOTSTRAP_MAX_ELF_BYTES] = {};
    ElfLayout layout = {};
    if (!require(write_bootstrap_elf(linked.code, linked.codeBytes, linked.data, linked.dataBytes,
                                     linked.mutableData, linked.mutableDataBytes,
                                     linked.entryCodeOffset, elf, sizeof(elf), &layout),
                 "global linked program writes three-segment ELF")) return false;
    ElfValidationResult writerValidation = {};
    if (!require(validate_bootstrap_elf(elf, layout.outputBytes, layout.imageBase,
                                       layout.codeOffset, linked.code, linked.codeBytes,
                                       &writerValidation, linked.data, linked.dataBytes,
                                       linked.mutableData, linked.mutableDataBytes,
                                       linked.entryCodeOffset), "three-segment ELF validates")) return false;
    const uint16_t phCount = static_cast<uint16_t>(elf[56]) |
        static_cast<uint16_t>(static_cast<uint16_t>(elf[57]) << 8);
    if (!require(phCount == 3, "ELF has code, rodata, and rwdata PT_LOAD segments")) return false;
    uint32_t rx = 0, ro = 0, rw = 0;
    for (uint16_t i = 0; i < phCount; ++i) {
        const uint32_t offset = 64U + i * 56U;
        const uint32_t flags = read_u32(elf, offset + 4U);
        if (flags == 5U) ++rx;
        else if (flags == 4U) ++ro;
        else if (flags == 6U) ++rw;
        else return require(false, "ELF contains only expected non-RWX permissions");
    }
    if (!require(rx == 1 && ro == 1 && rw == 1 && layout.mutableDataOffset != 0 &&
                 (layout.dataOffset & 0xFFFU) == 0 && (layout.mutableDataOffset & 0xFFFU) == 0 &&
                 read_u32(elf, layout.mutableDataOffset) == 40,
                 "ELF permissions and initial mutable bytes are correct")) return false;

    uint8_t malformed[BOOTSTRAP_MAX_ELF_BYTES] = {};
    std::memcpy(malformed, elf, layout.outputBytes);
    const uint32_t rwProgramHeader = 64U + 2U * 56U;
    malformed[rwProgramHeader + 4U] = 7U;
    ElfValidationResult rwxValidation = {};
    if (!require(!validate_bootstrap_elf(malformed, layout.outputBytes, layout.imageBase,
                                         layout.codeOffset, linked.code, linked.codeBytes,
                                         &rwxValidation, linked.data, linked.dataBytes,
                                         linked.mutableData, linked.mutableDataBytes,
                                         linked.entryCodeOffset),
                  "ELF validator rejects writable executable data")) return false;

    std::memcpy(malformed, elf, layout.outputBytes);
    std::memcpy(malformed + rwProgramHeader + 16U, malformed + 64U + 56U + 16U, 8U);
    ElfValidationResult overlapValidation = {};
    if (!require(!validate_bootstrap_elf(malformed, layout.outputBytes, layout.imageBase,
                                         layout.codeOffset, linked.code, linked.codeBytes,
                                         &overlapValidation, linked.data, linked.dataBytes,
                                         linked.mutableData, linked.mutableDataBytes,
                                         linked.entryCodeOffset),
                  "ELF validator rejects overlapping PT_LOAD ranges")) return false;
    return true;
}

static bool test_declarations_bounds_and_reset()
{
    const char* compatibleSource =
        "extern int answer; extern int answer; int answer = -2;\n"
        "int gx_main(gx_app_context* ctx) { return answer; }\n";
    CompiledModule compatible = {};
    Diagnostics compatibleDiagnostics;
    if (!require(compile_text("src/compatible.cpp", compatibleSource, &compatible, &compatibleDiagnostics),
                 "repeated extern declarations and a definition compile")) return false;
    if (!require(compatible.globalCount == 1 && compatible.mutableDataBytes == 4 &&
                 compatible.mutableData[0] == 0xFE && compatible.mutableData[1] == 0xFF,
                 "compatible extern declarations keep one signed global definition")) return false;

    CompiledModule unused = {};
    Diagnostics unusedDiagnostics;
    if (!require(compile_text("src/unused.cpp",
                              "extern int unused; int gx_main(gx_app_context* ctx) { return 42; }",
                              &unused, &unusedDiagnostics),
                 "unused extern declaration compiles")) return false;
    if (!require(unused.importCount == 0, "unused extern does not become a required data import")) return false;

    char tooMany[4096] = {};
    uint32_t used = 0;
    for (uint32_t i = 0; i < COMPILER_MAX_GLOBALS + 1U; ++i) {
        const int written = std::snprintf(tooMany + used, sizeof(tooMany) - used,
                                          "int g%u = %u;\n", i, i);
        if (written < 0 || static_cast<uint32_t>(written) >= sizeof(tooMany) - used) return false;
        used += static_cast<uint32_t>(written);
    }
    CompiledModule capacity = {};
    Diagnostics capacityDiagnostics;
    if (!require(!compile_text("src/capacity.cpp", tooMany, &capacity, &capacityDiagnostics) &&
                 diagnostic_contains(capacityDiagnostics, "global capacity exceeded"),
                 "global declaration capacity is bounded")) return false;

    CompiledModule aligned = {};
    Diagnostics alignedDiagnostics;
    if (!require(compile_text("src/aligned.cpp",
                              "int first = 1; int second = -2; int gx_main(gx_app_context* ctx) { return first + second; }",
                              &aligned, &alignedDiagnostics),
                 "multiple globals compile")) return false;
    if (!require(aligned.mutableDataBytes == 8 && aligned.mutableData[0] == 1 &&
                 aligned.mutableData[4] == 0xFE && aligned.mutableData[5] == 0xFF,
                 "globals use deterministic four-byte alignment")) return false;

    CompiledModule undefined = {};
    Diagnostics undefinedDiagnostics;
    if (!require(compile_text("src/undefined.cpp",
                              "extern int missing; int gx_main(gx_app_context* ctx) { return missing; }",
                              &undefined, &undefinedDiagnostics),
                 "undefined global reference compiles before linking")) return false;
    LinkedProgram reused = {};
    Diagnostics undefinedLink;
    if (!require(!link_modules(&undefined, 1, &reused, undefinedLink) &&
                 diagnostic_contains(undefinedLink, "undefined external global 'missing'"),
                 "undefined global link failure is recoverable")) return false;
    Diagnostics validLink;
    if (!require(link_modules(&compatible, 1, &reused, validLink) && reused.mutableDataBytes == 4,
                 "linker data state resets after an undefined global")) return false;
    Diagnostics functionOnlyLink;
    if (!require(link_modules(&unused, 1, &reused, functionOnlyLink) && reused.mutableDataBytes == 0 &&
                 find_export(reused, "answer") < 0,
                 "linker data exports reset for a function-only project")) return false;

    const char* recursionSource =
        "int calls = 0; int recurse(int n) { calls = calls + 1; if (n == 0) { return 0; } return recurse(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { recurse(5); return calls * 7; }\n";
    CompiledModule recursion = {};
    Diagnostics recursionDiagnostics;
    if (!require(compile_text("src/recursion.cpp", recursionSource, &recursion, &recursionDiagnostics),
                 "global recursion source compiles")) return false;
    Diagnostics recursionLink;
    if (!require(link_modules(&recursion, 1, &reused, recursionLink) &&
                 reused.mutableDataBytes == 4 && reused.recursiveSccCount == 1,
                 "global recursion preserves recursive linker analysis")) return false;
    return true;
}

static bool test_diagnostics_and_lookup()
{
    CompiledModule bad = {};
    Diagnostics diagnostics;
    if (!require(!compile_text("src/bad.cpp", "int x = 20 + 22; int gx_main(gx_app_context* ctx) { return 0; }", &bad, &diagnostics) &&
                 diagnostic_contains(diagnostics, "global initializer must be a constant integer"),
                 "nonconstant global initializer is rejected")) return false;

    CompiledModule duplicate[2] = {};
    Diagnostics first;
    Diagnostics second;
    if (!require(compile_text("src/a.cpp", "int answer = 42;", &duplicate[0], &first) &&
                 compile_text("src/b.cpp", "int answer = 41;", &duplicate[1], &second),
                 "duplicate global modules compile independently")) return false;
    LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    const bool duplicateLinked = link_modules(duplicate, 2, &linked, linkDiagnostics);
    if (!require(!duplicateLinked &&
                 diagnostic_contains(linkDiagnostics, "duplicate definition for global 'answer'"),
                 "duplicate global definitions are rejected")) {
        for (uint32_t i = 0; i < linkDiagnostics.count(); ++i)
            std::fprintf(stderr, "duplicate diagnostic: %s\n", linkDiagnostics.at(i).message);
        return false;
    }

    CompiledModule undefined = {};
    Diagnostics undefinedDiagnostics;
    if (!require(compile_text("src/main.cpp", "extern int answer; int gx_main(gx_app_context* ctx) { return answer; }",
                              &undefined, &undefinedDiagnostics), "undefined global reference compiles")) return false;
    Diagnostics undefinedLink;
    if (!require(!link_modules(&undefined, 1, &linked, undefinedLink) &&
                 diagnostic_contains(undefinedLink, "undefined external global 'answer'"),
                 "undefined global is a link diagnostic")) return false;

    CompiledModule conflict[2] = {};
    Diagnostics conflictA;
    Diagnostics conflictB;
    if (!require(compile_text("src/a.cpp", "int answer() { return 1; }", &conflict[0], &conflictA) &&
                 compile_text("src/b.cpp", "int answer = 42;", &conflict[1], &conflictB),
                 "function/data conflict modules compile independently")) return false;
    Diagnostics conflictLink;
    if (!require(!link_modules(conflict, 2, &linked, conflictLink) &&
                 diagnostic_contains(conflictLink, "defined as both function and global"),
                 "function/data namespace collision is rejected")) return false;

    const char* shadow = "int value = 40; int gx_main(gx_app_context* ctx) { int value = 42; return value; }";
    CompiledModule shadowModule = {};
    Diagnostics shadowDiagnostics;
    if (!require(compile_text("src/shadow.cpp", shadow, &shadowModule, &shadowDiagnostics),
                 "local shadows global source compiles")) return false;
    return require(shadowModule.globalCount == 1, "local shadowing preserves separate global symbol");
}

static bool test_deterministic_data_layout()
{
    CompiledModule firstModule = {};
    CompiledModule secondModule = {};
    Diagnostics firstDiagnostics;
    Diagnostics secondDiagnostics;
    if (!require(compile_text("src/a.cpp", "int first = -2;", &firstModule, &firstDiagnostics) &&
                 compile_text("src/main.cpp", "extern int first; int gx_main(gx_app_context* ctx) { return first; }",
                              &secondModule, &secondDiagnostics), "deterministic global inputs compile")) return false;
    CompiledModule forward[2] = {firstModule, secondModule};
    CompiledModule reverse[2] = {secondModule, firstModule};
    LinkedProgram a = {};
    LinkedProgram b = {};
    Diagnostics da;
    Diagnostics db;
    if (!require(link_modules(forward, 2, &a, da) && link_modules(reverse, 2, &b, db),
                 "global inputs link in both orders")) return false;
    return require(a.mutableDataBytes == 4 && b.mutableDataBytes == 4 &&
                   std::memcmp(a.mutableData, b.mutableData, 4) == 0 &&
                   a.mutableDataFileOffset == b.mutableDataFileOffset &&
                   a.entryCodeOffset == b.entryCodeOffset &&
                   a.codeBytes == b.codeBytes && std::memcmp(a.code, b.code, a.codeBytes) == 0 &&
                   a.mutableData[0] == 0xFE && a.mutableData[1] == 0xFF,
                   "global data layout is deterministic and order independent");
}

} // namespace

int main()
{
    if (!test_same_file_globals()) return 1;
    if (!test_cross_file_globals_and_elf()) return 1;
    if (!test_declarations_bounds_and_reset()) return 1;
    if (!test_diagnostics_and_lookup()) return 1;
    if (!test_deterministic_data_layout()) return 1;
    std::puts("compiler_globals_host_test: PASS");
    return 0;
}
