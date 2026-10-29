// COMP4L hosted proof for file-scope int, char, and unsigned char storage.
// Objects go through the production ELF-object serializer and linker. The
// execution harness loads the final ELF RW PT_LOAD bytes into writable host
// memory and runs the production AMD64 code with hosted data relocations.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_linker.h"
#include "core/compiler/compiler_module.h"
#include "core/compiler/compiler_object.h"
#include "core/compiler/elf_writer.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace kernel::compiler;

namespace {

static uint32_t g_assertions = 0;
static CompiledModule g_modules[2] = {};
static CompiledModule g_restored[2] = {};
static CompiledModule g_scratchModules[6] = {};
static LinkedProgram g_linked = {};
static uint8_t g_object[2][COMPILER_MAX_OBJECT_BYTES] = {};
static uint8_t g_objectAgain[COMPILER_MAX_OBJECT_BYTES] = {};
static uint32_t g_objectBytes[2] = {};
static uint8_t g_elf[BOOTSTRAP_MAX_ELF_BYTES] = {};

static bool require(bool condition, const char* message)
{
    ++g_assertions;
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

static bool compile_text(const char* path, const char* source, CompiledModule* module,
                         Diagnostics& diagnostics)
{
    return compile_module_from_source(path, source, static_cast<uint32_t>(std::strlen(source)),
                                      module, diagnostics);
}

static uint32_t read_u32(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1U]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2U]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3U]) << 24);
}

static uint64_t read_u64(const uint8_t* bytes, uint32_t offset)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[offset + i]) << (8U * i);
    return value;
}

static void write_u64(uint8_t* bytes, uint32_t offset, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (8U * i));
}

static int32_t find_linked_export(const LinkedProgram& program, const char* name)
{
    for (uint32_t i = 0; i < program.exportCount; ++i)
        if (std::strcmp(program.exports[i].name, name) == 0) return static_cast<int32_t>(i);
    return -1;
}

static int32_t find_module_export(const CompiledModule& module, const char* name)
{
    for (uint32_t i = 0; i < module.exportCount; ++i)
        if (std::strcmp(module.exports[i].name, name) == 0) return static_cast<int32_t>(i);
    return -1;
}

static uint32_t find_rw_segment(const uint8_t* elf, uint64_t* fileOffset,
                                uint64_t* fileBytes, uint64_t* memoryBytes)
{
    const uint16_t count = static_cast<uint16_t>(elf[56]) |
        static_cast<uint16_t>(static_cast<uint16_t>(elf[57]) << 8);
    uint32_t found = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint32_t header = 64U + static_cast<uint32_t>(i) * 56U;
        if (read_u32(elf, header + 4U) != 6U) continue;
        ++found;
        if (fileOffset) *fileOffset = read_u64(elf, header + 8U);
        if (fileBytes) *fileBytes = read_u64(elf, header + 32U);
        if (memoryBytes) *memoryBytes = read_u64(elf, header + 40U);
    }
    return found;
}

static int32_t call_generated_entry(void* entry)
{
    int32_t value = 0;
#if defined(__GNUC__) || defined(__clang__)
    asm volatile(
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "subq $64, %%rsp\n\t"
        "xorq %%rcx, %%rcx\n\t"
        "call *%[entry]\n\t"
        "addq $64, %%rsp\n\t"
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        : "=a"(value)
        : [entry] "r"(entry)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
#else
    typedef int32_t (*EntryFunction)(void*);
    value = reinterpret_cast<EntryFunction>(entry)(nullptr);
#endif
    return value;
}

static bool serialize_and_reopen(const CompiledModule& module, uint8_t* bytes,
                                 uint32_t* byteCount, CompiledModule* reopened)
{
    if (!serialize_elf_object(module, bytes, COMPILER_MAX_OBJECT_BYTES, byteCount)) return false;
    Diagnostics diagnostics;
    return deserialize_elf_object(bytes, *byteCount, reopened, diagnostics);
}

static bool build_final_elf(const LinkedProgram& linked, ElfLayout* layout)
{
    if (!write_bootstrap_elf(linked.code, linked.codeBytes, linked.data, linked.dataBytes,
                             linked.mutableData, linked.mutableDataBytes,
                             linked.entryCodeOffset, g_elf, sizeof(g_elf), layout)) return false;
    ElfValidationResult validation = {};
    return validate_bootstrap_elf(g_elf, layout->outputBytes, layout->imageBase,
                                  layout->codeOffset, linked.code, linked.codeBytes,
                                  &validation, linked.data, linked.dataBytes,
                                  linked.mutableData, linked.mutableDataBytes,
                                  linked.entryCodeOffset);
}

static bool execute_final_elf(const LinkedProgram& linked, const CompiledModule* modules,
                              uint32_t moduleCount, int32_t* result,
                              uint8_t* postRunData, uint32_t postRunCapacity)
{
#if defined(_WIN32)
    if (!modules || moduleCount == 0 || !result || linked.codeBytes == 0) return false;
    ElfLayout layout = {};
    if (!build_final_elf(linked, &layout)) return false;
    uint64_t rwFileOffset = 0, rwFileBytes = 0, rwMemoryBytes = 0;
    if (find_rw_segment(g_elf, &rwFileOffset, &rwFileBytes, &rwMemoryBytes) != 1 ||
        rwMemoryBytes == 0 || rwFileBytes > rwMemoryBytes ||
        rwFileOffset > layout.outputBytes || rwFileBytes > layout.outputBytes - rwFileOffset ||
        rwMemoryBytes > COMPILER_MAX_LINKED_DATA_BYTES ||
        (postRunData && postRunCapacity < rwMemoryBytes)) return false;

    void* dataMemory = VirtualAlloc(nullptr, static_cast<SIZE_T>(rwMemoryBytes),
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!dataMemory) return false;
    std::memset(dataMemory, 0, static_cast<size_t>(rwMemoryBytes));
    std::memcpy(dataMemory, g_elf + rwFileOffset, static_cast<size_t>(rwFileBytes));

    void* codeMemory = VirtualAlloc(nullptr, linked.codeBytes, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
    if (!codeMemory) {
        VirtualFree(dataMemory, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(codeMemory, linked.code, linked.codeBytes);

    // Match the linker's deterministic path ordering and 16-byte module code
    // alignment so each production relocation is adjusted at its final offset.
    uint16_t order[COMPILER_MAX_TRANSLATION_UNITS] = {};
    uint32_t codeOffsets[COMPILER_MAX_TRANSLATION_UNITS] = {};
    if (moduleCount > COMPILER_MAX_TRANSLATION_UNITS) {
        VirtualFree(codeMemory, 0, MEM_RELEASE);
        VirtualFree(dataMemory, 0, MEM_RELEASE);
        return false;
    }
    for (uint32_t i = 0; i < moduleCount; ++i) order[i] = static_cast<uint16_t>(i);
    for (uint32_t i = 0; i < moduleCount; ++i) {
        for (uint32_t j = i + 1U; j < moduleCount; ++j) {
            if (std::strcmp(modules[order[j]].sourcePath, modules[order[i]].sourcePath) < 0) {
                const uint16_t swap = order[i];
                order[i] = order[j];
                order[j] = swap;
            }
        }
    }
    uint32_t codeOffset = 0;
    bool relocationsValid = true;
    for (uint32_t i = 0; i < moduleCount && relocationsValid; ++i) {
        const CompiledModule& module = modules[order[i]];
        codeOffset = (codeOffset + 15U) & ~15U;
        codeOffsets[order[i]] = codeOffset;
        for (uint32_t r = 0; r < module.relocationCount; ++r) {
            const RelocationRecord& relocation = module.relocations[r];
            if (relocation.kind != RelocationKind::GlobalDataAddress64) continue;
            const int32_t symbol = find_linked_export(linked, relocation.targetSymbolName);
            if (symbol < 0 || linked.exports[symbol].finalDataOffset > rwMemoryBytes ||
                linked.exports[symbol].size > rwMemoryBytes - linked.exports[symbol].finalDataOffset ||
                codeOffsets[order[i]] > linked.codeBytes ||
                relocation.patchOffset > linked.codeBytes - codeOffsets[order[i]] ||
                relocation.width > linked.codeBytes - codeOffsets[order[i]] - relocation.patchOffset) {
                relocationsValid = false;
                break;
            }
            const uint64_t address = reinterpret_cast<uint64_t>(dataMemory) +
                linked.exports[symbol].finalDataOffset;
            write_u64(static_cast<uint8_t*>(codeMemory),
                      codeOffsets[order[i]] + relocation.patchOffset, address);
        }
        codeOffset += module.codeBytes;
    }
    if (!relocationsValid || codeOffset != linked.codeBytes) {
        VirtualFree(codeMemory, 0, MEM_RELEASE);
        VirtualFree(dataMemory, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldProtection = 0;
    const bool executable = VirtualProtect(codeMemory, linked.codeBytes,
                                           PAGE_EXECUTE_READ, &oldProtection) != 0;
    FlushInstructionCache(GetCurrentProcess(), codeMemory, linked.codeBytes);
    if (executable) {
        *result = call_generated_entry(static_cast<uint8_t*>(codeMemory) + linked.entryCodeOffset);
        if (postRunData) std::memcpy(postRunData, dataMemory, static_cast<size_t>(rwMemoryBytes));
    }
    VirtualFree(codeMemory, 0, MEM_RELEASE);
    VirtualFree(dataMemory, 0, MEM_RELEASE);
    return executable;
#else
    (void)linked; (void)modules; (void)moduleCount; (void)result;
    (void)postRunData; (void)postRunCapacity;
    return false;
#endif
}

static bool test_scalar_storage_and_execution()
{
    const char* source =
        "int counter = 27;\n"
        "char signedValue = 'A';\n"
        "unsigned char unsignedValue = 255;\n"
        "char leftGuard = 49;\n"
        "unsigned char rightGuard = 114;\n"
        "int zeroCounter;\n"
        "char zeroSigned;\n"
        "unsigned char zeroUnsigned;\n"
        "int tailGuard = 324478056;\n"
        "int read_signed() { return signedValue; }\n"
        "int read_unsigned() { return unsignedValue; }\n"
        "int mutate() { counter = counter + 1; signedValue = signedValue + 1; "
            "unsignedValue = unsignedValue + 1; return counter + signedValue + unsignedValue; }\n"
        "int gx_main(gx_app_context* ctx) {\n"
        "  if (zeroCounter != 0 || zeroSigned != 0 || zeroUnsigned != 0) return 11;\n"
        "  if (read_signed() != 65 || read_unsigned() != 255) return 12;\n"
        "  signedValue = 128; if (signedValue != -128) return 13;\n"
        "  signedValue = 255; unsignedValue = 256;\n"
        "  if (signedValue != -1 || unsignedValue != 0) return 14;\n"
        "  if (mutate() != 29 || mutate() != 32) return 15;\n"
        "  if (counter != 29 || signedValue != 1 || unsignedValue != 2) return 16;\n"
        "  if (leftGuard != 49 || rightGuard != 114 || tailGuard != 324478056) return 17;\n"
        "  return 42;\n"
        "}\n";
    Diagnostics diagnostics;
    if (!require(compile_text("src/main.c", source, &g_modules[0], diagnostics),
                 "typed scalar global fixture compiles")) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "compile diagnostic: %s\n", diagnostics.at(i).message);
        return false;
    }
    if (!require(g_modules[0].globalCount == 9 && g_modules[0].mutableDataBytes == 20,
                 "mixed globals use bounded byte layout with integer alignment")) return false;
    if (!require(g_modules[0].mutableData[0] == 27 && g_modules[0].mutableData[4] == 65 &&
                 g_modules[0].mutableData[5] == 255 && g_modules[0].mutableData[6] == 49 &&
                 g_modules[0].mutableData[7] == 114 && g_modules[0].mutableData[8] == 0 &&
                 g_modules[0].mutableData[12] == 0 && g_modules[0].mutableData[13] == 0 &&
                 g_modules[0].mutableData[16] == 0x68 && g_modules[0].mutableData[19] == 0x13,
                 "initial values and deterministic zero storage occupy expected bytes")) return false;
    const int32_t counter = find_module_export(g_modules[0], "counter");
    const int32_t signedValue = find_module_export(g_modules[0], "signedValue");
    const int32_t unsignedValue = find_module_export(g_modules[0], "unsignedValue");
    const int32_t tailGuard = find_module_export(g_modules[0], "tailGuard");
    if (!require(counter >= 0 && signedValue >= 0 && unsignedValue >= 0 && tailGuard >= 0 &&
                 g_modules[0].exports[counter].moduleDataOffset == 0 &&
                 g_modules[0].exports[signedValue].moduleDataOffset == 4 &&
                 g_modules[0].exports[unsignedValue].moduleDataOffset == 5 &&
                 g_modules[0].exports[tailGuard].moduleDataOffset == 16,
                 "global symbols carry their exact mixed-width offsets")) return false;

    uint32_t bytes = 0;
    if (!require(serialize_and_reopen(g_modules[0], g_object[0], &bytes, &g_scratchModules[0]),
                 "typed global object serializes and reopens")) return false;
    ElfObjectHeaderView header = {};
    Diagnostics headerDiagnostics;
    const int32_t reopenedSigned = find_module_export(g_scratchModules[0], "signedValue");
    const int32_t reopenedUnsigned = find_module_export(g_scratchModules[0], "unsignedValue");
    if (!require(inspect_elf_object(g_object[0], bytes, &header, headerDiagnostics) &&
                 header.formatVersion == 2 && header.compilerObjectAbiVersion == 16 &&
                 g_scratchModules[0].mutableDataBytes == 20 && reopenedSigned >= 0 && reopenedUnsigned >= 0 &&
                 g_scratchModules[0].exports[reopenedSigned].kind == SymbolKind::DataChar &&
                 g_scratchModules[0].exports[reopenedUnsigned].kind == SymbolKind::DataUnsignedChar,
                 "object ABI 16 preserves format 2 byte signedness and layout")) return false;
    uint32_t repeatBytes = 0;
    if (!require(serialize_elf_object(g_modules[0], g_objectAgain, sizeof(g_objectAgain), &repeatBytes) &&
                 repeatBytes == bytes && std::memcmp(g_object[0], g_objectAgain, bytes) == 0,
                 "typed global object serialization is deterministic")) return false;
    std::memcpy(g_objectAgain, g_object[0], bytes);
    g_objectAgain[header.metaOffset + 16U] = 15U;
    const uint64_t oldAbiChecksum = elf_object_checksum(g_objectAgain, bytes, header.metaOffset + 112U);
    for (uint32_t i = 0; i < 8U; ++i)
        g_objectAgain[header.metaOffset + 112U + i] = static_cast<uint8_t>(oldAbiChecksum >> (8U * i));
    Diagnostics oldAbiDiagnostics;
    if (!require(!deserialize_elf_object(g_objectAgain, bytes, &g_scratchModules[1], oldAbiDiagnostics) &&
                 diagnostic_contains(oldAbiDiagnostics, "compiler identity"),
                 "ABI 15 cached objects are rejected")) return false;

    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_modules, 1, &g_linked, linkDiagnostics),
                 "typed scalar globals link through production linker")) return false;
    ElfLayout layout = {};
    if (!require(build_final_elf(g_linked, &layout), "production writer validates the linked ELF")) return false;
    uint64_t rwOffset = 0, rwFileBytes = 0, rwMemoryBytes = 0;
    if (!require(find_rw_segment(g_elf, &rwOffset, &rwFileBytes, &rwMemoryBytes) == 1 &&
                 rwFileBytes == 20 && rwMemoryBytes == 20 &&
                 std::memcmp(g_elf + rwOffset, g_linked.mutableData, 20) == 0,
                 "RW PT_LOAD is writable file-backed data including the zero bytes")) return false;
    const uint8_t signedLoad[] = {0x0F, 0xBE, 0x02};
    const uint8_t unsignedLoad[] = {0x0F, 0xB6, 0x02};
    const uint8_t byteStore[] = {0x88, 0x02};
    bool sawSignedLoad = false, sawUnsignedLoad = false, sawByteStore = false;
    for (uint32_t i = 0; i + 3U <= g_modules[0].codeBytes; ++i) {
        sawSignedLoad = sawSignedLoad || std::memcmp(g_modules[0].code + i, signedLoad, sizeof(signedLoad)) == 0;
        sawUnsignedLoad = sawUnsignedLoad || std::memcmp(g_modules[0].code + i, unsignedLoad, sizeof(unsignedLoad)) == 0;
        if (i + sizeof(byteStore) <= g_modules[0].codeBytes)
            sawByteStore = sawByteStore || std::memcmp(g_modules[0].code + i, byteStore, sizeof(byteStore)) == 0;
    }
    if (!require(sawSignedLoad && sawUnsignedLoad && sawByteStore,
                 "AMD64 uses signed byte loads, unsigned byte loads, and byte stores")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = 0;
    if (!require(execute_final_elf(g_linked, g_modules, 1, &result,
                                   postRun, sizeof(postRun)) && result == 42,
                 "production AMD64 executes against writable ELF global data")) return false;
    if (!require(read_u32(postRun, 0) == 29 && postRun[4] == 1 && postRun[5] == 2 &&
                 postRun[6] == 49 && postRun[7] == 114 && read_u32(postRun, 8) == 0 &&
                 postRun[12] == 0 && postRun[13] == 0 && read_u32(postRun, 16) == 324478056U,
                 "repeated byte mutations preserve adjacent globals and sentinels")) return false;
    return true;
}

static bool test_cross_module_data_symbols()
{
    const char* stateSource = "char shared = -1; unsigned char sharedFlags = 254; int guard = 287454020;\n";
    const char* mainSource =
        "extern char shared; extern unsigned char sharedFlags; extern int guard;\n"
        "int increment_state() { shared = shared + 2; sharedFlags = sharedFlags + 3; return shared + sharedFlags; }\n"
        "int gx_main(gx_app_context* ctx) { int value = increment_state(); if (guard != 287454020) return 91; return value; }\n";
    Diagnostics stateDiagnostics, mainDiagnostics;
    if (!require(compile_text("src/state.c", stateSource, &g_modules[0], stateDiagnostics) &&
                 compile_text("src/main.c", mainSource, &g_modules[1], mainDiagnostics),
                 "cross-module char declarations compile")) return false;
    for (uint32_t i = 0; i < 2; ++i) {
        if (!require(serialize_and_reopen(g_modules[i], g_object[i], &g_objectBytes[i], &g_restored[i]),
                     "cross-module data object round trips")) return false;
    }
    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_restored, 2, &g_linked, linkDiagnostics),
                 "typed signed and unsigned byte imports resolve across objects")) {
        for (uint32_t i = 0; i < linkDiagnostics.count(); ++i)
            std::fprintf(stderr, "link diagnostic: %s\n", linkDiagnostics.at(i).message);
        return false;
    }
    const int32_t shared = find_linked_export(g_linked, "shared");
    const int32_t flags = find_linked_export(g_linked, "sharedFlags");
    if (!require(shared >= 0 && flags >= 0 && g_linked.exports[shared].kind == SymbolKind::DataChar &&
                 g_linked.exports[flags].kind == SymbolKind::DataUnsignedChar &&
                 g_linked.exports[shared].size == 1 && g_linked.exports[flags].size == 1,
                 "linked symbol identity preserves signed byte widths")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = 0;
    if (!require(execute_final_elf(g_linked, g_restored, 2, &result,
                                   postRun, sizeof(postRun)) && result == 2,
                 "cross-module byte reads and writes execute after object linking")) return false;
    const int32_t guard = find_linked_export(g_linked, "guard");
    if (!require(guard >= 0 && postRun[g_linked.exports[shared].finalDataOffset] == 1 &&
                 postRun[g_linked.exports[flags].finalDataOffset] == 1 &&
                 read_u32(postRun, g_linked.exports[guard].finalDataOffset) == 287454020U,
                 "cross-module writes truncate to one byte and preserve integer guard")) return false;

    Diagnostics mismatchState, mismatchMain;
    if (!require(compile_text("src/state.c", "char shared = 1;", &g_scratchModules[2], mismatchState) &&
                 compile_text("src/main.c", "extern unsigned char shared; int gx_main(gx_app_context* ctx) { return shared; }",
                              &g_scratchModules[3], mismatchMain),
                 "signedness mismatch inputs compile independently")) return false;
    Diagnostics mismatchLink;
    if (!require(!link_modules(&g_scratchModules[2], 2, &g_linked, mismatchLink) &&
                 diagnostic_contains(mismatchLink, "conflicting declaration for global 'shared'"),
                 "linker rejects signed and unsigned byte declaration mismatch")) return false;

    Diagnostics duplicateA, duplicateB;
    if (!require(compile_text("src/a.c", "unsigned char flag = 1;", &g_scratchModules[4], duplicateA) &&
                 compile_text("src/b.c", "unsigned char flag = 2;", &g_scratchModules[5], duplicateB),
                 "duplicate byte definitions compile independently")) return false;
    Diagnostics duplicateLink;
    if (!require(!link_modules(&g_scratchModules[4], 2, &g_linked, duplicateLink) &&
                 diagnostic_contains(duplicateLink, "duplicate definition for global 'flag'"),
                 "linker rejects duplicate byte global definitions")) return false;
    return true;
}

static bool test_rejections_and_limits()
{
    CompiledModule& bad = g_scratchModules[0];
    Diagnostics diagnostics;
    if (!require(!compile_text("src/dynamic.c", "int value = runtime_value();", &bad, diagnostics) &&
                 diagnostics.count() != 0 && diagnostics.at(0).location.line == 1 &&
                 diagnostics.at(0).location.column > 1 &&
                 diagnostic_contains(diagnostics, "global initializer must be a constant integer"),
                 "runtime-dependent global initializer is rejected at its source location")) return false;
    diagnostics = Diagnostics();
    if (!require(!compile_text("src/expression.c", "int value = 20 + 22;", &bad, diagnostics) &&
                 diagnostics.count() != 0 && diagnostics.at(0).location.column > 1 &&
                 diagnostic_contains(diagnostics, "global initializer must be a constant integer"),
                 "unsupported initializer expressions receive a source-located diagnostic")) return false;
    diagnostics = Diagnostics();
    if (!require(!compile_text("src/array.c", "unsigned char buffer[16];", &bad, diagnostics) &&
                 diagnostic_contains(diagnostics, "file-scope char arrays are not supported"),
                 "optional byte arrays are explicitly classified unsupported")) return false;
    diagnostics = Diagnostics();
    if (!require(!compile_text("src/file_static.c", "static int hidden = 1;", &bad, diagnostics) &&
                 diagnostics.count() != 0 && diagnostics.at(0).kind == CompilerDiagnosticKind::Unsupported &&
                 diagnostic_contains(diagnostics, "file-scope static linkage is not supported"),
                 "file-scope static linkage is explicitly diagnosed")) return false;
    diagnostics = Diagnostics();
    if (!require(!compile_text("src/local_static.c", "int gx_main(gx_app_context* ctx) { static int once = 1; return once; }",
                               &bad, diagnostics) && diagnostics.count() != 0 &&
                 diagnostics.at(0).kind == CompilerDiagnosticKind::Unsupported &&
                 diagnostic_contains(diagnostics, "function-local static storage is not supported"),
                 "function-local static storage is explicitly diagnosed")) return false;
    char many[4096] = {};
    uint32_t used = 0;
    for (uint32_t i = 0; i < COMPILER_MAX_GLOBALS + 1U; ++i) {
        const int written = std::snprintf(many + used, sizeof(many) - used,
                                          "unsigned char byte%u = 0;\n", i);
        if (written < 0 || static_cast<uint32_t>(written) >= sizeof(many) - used) return false;
        used += static_cast<uint32_t>(written);
    }
    diagnostics = Diagnostics();
    return require(!compile_text("src/capacity.c", many, &bad, diagnostics) &&
                   diagnostic_contains(diagnostics, "global capacity exceeded"),
                   "byte globals retain the existing 32-symbol resource limit");
}

} // namespace

int main()
{
    if (!test_scalar_storage_and_execution() || !test_cross_module_data_symbols() ||
        !test_rejections_and_limits()) return 1;
    std::printf("COMP4L_GLOBAL_STORAGE_ASSERTIONS=%u result=PASS\n", g_assertions);
    return 0;
}
