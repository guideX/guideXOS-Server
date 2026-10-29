// COMP4N hosted proof for bounded compile-time file-scope byte-array lists.
// It uses the production parser, object, linker, ELF writer and AMD64 backend.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_linker.h"
#include "core/compiler/compiler_module.h"
#include "core/compiler/compiler_object.h"
#include "core/compiler/elf_writer.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace kernel::compiler;

namespace {

static uint32_t g_assertions = 0;
static CompiledModule g_modules[2] = {};
static CompiledModule g_reopened[2] = {};
static CompiledModule g_invalid = {};
static LinkedProgram g_linked = {};
static uint8_t g_objects[2][COMPILER_MAX_OBJECT_BYTES] = {};
static uint8_t g_repeatedObject[COMPILER_MAX_OBJECT_BYTES] = {};
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

static bool compile_text(const char* path, const char* source,
                         CompiledModule* module, Diagnostics& diagnostics)
{
    return compile_module_from_source(path, source,
        static_cast<uint32_t>(std::strlen(source)), module, diagnostics);
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
    for (uint32_t i = 0; i < 8U; ++i)
        value |= static_cast<uint64_t>(bytes[offset + i]) << (8U * i);
    return value;
}

static void write_u64(uint8_t* bytes, uint32_t offset, uint64_t value)
{
    for (uint32_t i = 0; i < 8U; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (8U * i));
}

static int32_t find_module_export(const CompiledModule& module, const char* name)
{
    for (uint32_t i = 0; i < module.exportCount; ++i)
        if (std::strcmp(module.exports[i].name, name) == 0) return static_cast<int32_t>(i);
    return -1;
}

static int32_t find_linked_export(const LinkedProgram& program, const char* name)
{
    for (uint32_t i = 0; i < program.exportCount; ++i)
        if (std::strcmp(program.exports[i].name, name) == 0) return static_cast<int32_t>(i);
    return -1;
}

static bool serialize_reopen(const CompiledModule& module, uint8_t* object,
                             uint32_t* objectBytes, CompiledModule* reopened)
{
    if (!serialize_elf_object(module, object, COMPILER_MAX_OBJECT_BYTES, objectBytes)) return false;
    Diagnostics diagnostics;
    return deserialize_elf_object(object, *objectBytes, reopened, diagnostics);
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

static int32_t call_entry(void* entry)
{
    int32_t result = 0;
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
        : "=a"(result)
        : [entry] "r"(entry)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
#else
    typedef int32_t (*EntryFunction)(void*);
    result = reinterpret_cast<EntryFunction>(entry)(nullptr);
#endif
    return result;
}

static bool make_elf(const LinkedProgram& linked, ElfLayout* layout)
{
    if (!write_bootstrap_elf(linked.code, linked.codeBytes,
        linked.data, linked.dataBytes, linked.mutableData, linked.mutableDataBytes,
        linked.entryCodeOffset, g_elf, sizeof(g_elf), layout)) return false;
    ElfValidationResult validation = {};
    return validate_bootstrap_elf(g_elf, layout->outputBytes, layout->imageBase,
        layout->codeOffset, linked.code, linked.codeBytes, &validation,
        linked.data, linked.dataBytes, linked.mutableData, linked.mutableDataBytes,
        linked.entryCodeOffset);
}

static bool execute_linked(const LinkedProgram& linked, const CompiledModule* modules,
                           uint32_t moduleCount, int32_t* result,
                           uint8_t* postRunData, uint32_t postRunCapacity)
{
#if defined(_WIN32)
    if (!modules || moduleCount == 0 || !result || linked.codeBytes == 0) return false;
    ElfLayout layout = {};
    if (!make_elf(linked, &layout)) return false;
    uint64_t rwOffset = 0, rwFileBytes = 0, rwMemoryBytes = 0;
    if (find_rw_segment(g_elf, &rwOffset, &rwFileBytes, &rwMemoryBytes) != 1U ||
        rwMemoryBytes == 0 || rwFileBytes > rwMemoryBytes ||
        rwOffset > layout.outputBytes || rwFileBytes > layout.outputBytes - rwOffset ||
        rwMemoryBytes > COMPILER_MAX_LINKED_DATA_BYTES ||
        (postRunData && postRunCapacity < rwMemoryBytes)) return false;
    void* dataMemory = VirtualAlloc(nullptr, static_cast<SIZE_T>(rwMemoryBytes),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!dataMemory) return false;
    std::memset(dataMemory, 0, static_cast<size_t>(rwMemoryBytes));
    std::memcpy(dataMemory, g_elf + rwOffset, static_cast<size_t>(rwFileBytes));
    void* codeMemory = VirtualAlloc(nullptr, linked.codeBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!codeMemory) {
        VirtualFree(dataMemory, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(codeMemory, linked.code, linked.codeBytes);
    if (moduleCount > COMPILER_MAX_TRANSLATION_UNITS) {
        VirtualFree(codeMemory, 0, MEM_RELEASE);
        VirtualFree(dataMemory, 0, MEM_RELEASE);
        return false;
    }
    uint16_t order[COMPILER_MAX_TRANSLATION_UNITS] = {};
    uint32_t codeOffsets[COMPILER_MAX_TRANSLATION_UNITS] = {};
    for (uint32_t i = 0; i < moduleCount; ++i) order[i] = static_cast<uint16_t>(i);
    for (uint32_t i = 0; i < moduleCount; ++i)
        for (uint32_t j = i + 1U; j < moduleCount; ++j)
            if (std::strcmp(modules[order[j]].sourcePath, modules[order[i]].sourcePath) < 0) {
                const uint16_t swap = order[i]; order[i] = order[j]; order[j] = swap;
            }
    uint32_t codeOffset = 0;
    bool valid = true;
    for (uint32_t i = 0; i < moduleCount && valid; ++i) {
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
                valid = false;
                break;
            }
            const uint64_t address = reinterpret_cast<uint64_t>(dataMemory) +
                linked.exports[symbol].finalDataOffset;
            write_u64(static_cast<uint8_t*>(codeMemory),
                codeOffsets[order[i]] + relocation.patchOffset, address);
        }
        codeOffset += module.codeBytes;
    }
    if (!valid || codeOffset != linked.codeBytes) {
        VirtualFree(codeMemory, 0, MEM_RELEASE);
        VirtualFree(dataMemory, 0, MEM_RELEASE);
        return false;
    }
    DWORD oldProtection = 0;
    const bool executable = VirtualProtect(codeMemory, linked.codeBytes,
        PAGE_EXECUTE_READ, &oldProtection) != 0;
    FlushInstructionCache(GetCurrentProcess(), codeMemory, linked.codeBytes);
    if (executable) {
        *result = call_entry(static_cast<uint8_t*>(codeMemory) + linked.entryCodeOffset);
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

static bool expect_rejection(const char* path, const char* source,
                             CompilerDiagnosticKind kind, const char* needle,
                             const char* description)
{
    std::memset(&g_invalid, 0xA5, sizeof(g_invalid));
    Diagnostics diagnostics;
    const bool rejected = !compile_text(path, source, &g_invalid, diagnostics) &&
        diagnostics.count() != 0U && diagnostics.at(0).kind == kind &&
        diagnostics.at(0).location.line != 0U &&
        diagnostics.at(0).location.column != 0U &&
        diagnostic_contains(diagnostics, needle);
    if (!rejected)
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "unexpected diagnostic for %s: kind=%u %s\n", path,
                static_cast<unsigned>(diagnostics.at(i).kind), diagnostics.at(i).message);
    return require(rejected, description);
}

static bool test_initialized_storage_and_host_execution()
{
    const char* source =
        "int before = 287454020;\n"
        "unsigned char values[4] = { 1, 2, 3, 255, };\n"
        "char letters[4] = {'A', 'B', 'C', '\\0'};\n"
        "unsigned char partial[8] = {10, 20, 30};\n"
        "char signed_data[3] = {-1, 127, 128};\n"
        "unsigned char unsigned_data[3] = {-1, 255, 256};\n"
        "unsigned char empty[2] = {};\n"
        "unsigned char omitted[2];\n"
        "unsigned char mutable_data[2] = {9, 10};\n"
        "int after = 1432778632;\n"
        "int update(unsigned char *p, int value) { p[1] = value; return p[1]; }\n"
        "int gx_main(gx_app_context* ctx) {\n"
        " if (partial[7] + values[0] != 1) return 10;\n"
        " if (signed_data[0] + signed_data[1] + signed_data[2] != -2) return 13;\n"
        " if (unsigned_data[0] + unsigned_data[1] + unsigned_data[2] != 510) return 14;\n"
        " if (update(mutable_data, 255) != 255) return 16;\n"
        " if (update(mutable_data, -1) != 255) return 17;\n"
        " mutable_data[0] = 257; if (mutable_data[0] != 1) return 18;\n"
        " if (before != 287454020 || after != 1432778632) return 19;\n"
        " return 42; }\n";
    std::memset(&g_modules[0], 0xA5, sizeof(g_modules[0]));
    Diagnostics diagnostics;
    if (!require(compile_text("src/initialized.c", source, &g_modules[0], diagnostics),
                 "production parser accepts constant, partial, empty, and trailing-comma byte lists")) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "compile diagnostic: %s\n", diagnostics.at(i).message);
        return false;
    }
    if (!require(g_modules[0].globalCount == 10U && g_modules[0].mutableDataBytes == 36U,
                 "initialized and zero-initialized globals share bounded storage")) return false;
    const char* names[] = {"before", "values", "letters", "partial", "signed_data",
                           "unsigned_data", "empty", "omitted", "mutable_data", "after"};
    const uint32_t expectedOffsets[] = {0, 4, 8, 12, 20, 23, 26, 28, 30, 32};
    bool offsetsValid = true;
    for (uint32_t i = 0; i < 10U; ++i) {
        const int32_t symbol = find_module_export(g_modules[0], names[i]);
        offsetsValid = offsetsValid && symbol >= 0 &&
            g_modules[0].exports[symbol].moduleDataOffset == expectedOffsets[i];
    }
    if (!require(offsetsValid, "initialized globals retain declaration order, alignment, and non-overlapping offsets"))
        return false;
    const int32_t signedArray = find_module_export(g_modules[0], "signed_data");
    const int32_t unsignedArray = find_module_export(g_modules[0], "unsigned_data");
    if (!require(signedArray >= 0 && unsignedArray >= 0 &&
        g_modules[0].exports[signedArray].kind == SymbolKind::DataCharArray &&
        g_modules[0].exports[unsignedArray].kind == SymbolKind::DataUnsignedCharArray &&
        g_modules[0].exports[signedArray].elementCount == 3U &&
        g_modules[0].exports[signedArray].elementSize == 1U &&
        g_modules[0].exports[signedArray].alignment == 1U &&
        g_modules[0].exports[unsignedArray].size == 3U,
        "object symbols retain signedness, one-byte width, element count, and alignment")) return false;

    uint8_t expected[36] = {};
    const uint8_t fixture[] = {
        0x44, 0x33, 0x22, 0x11, 1, 2, 3, 255, 'A', 'B', 'C', 0,
        10, 20, 30, 0, 0, 0, 0, 0, 255, 127, 128, 255, 255, 0,
        0, 0, 0, 0, 9, 10, 0x88, 0x77, 0x66, 0x55
    };
    std::memcpy(expected, fixture, sizeof(expected));
    if (!require(std::memcmp(g_modules[0].mutableData, expected, sizeof(expected)) == 0,
                 "compiler emits exact initialized bytes, omitted zeroes, and untouched adjacent globals")) return false;
    const uint8_t signedLoad[] = {0x0F, 0xBE, 0x02};
    const uint8_t unsignedLoad[] = {0x0F, 0xB6, 0x02};
    bool sawSignedLoad = false;
    bool sawUnsignedLoad = false;
    for (uint32_t i = 0; i + 3U <= g_modules[0].codeBytes; ++i) {
        sawSignedLoad = sawSignedLoad ||
            std::memcmp(g_modules[0].code + i, signedLoad, sizeof(signedLoad)) == 0;
        sawUnsignedLoad = sawUnsignedLoad ||
            std::memcmp(g_modules[0].code + i, unsignedLoad, sizeof(unsignedLoad)) == 0;
    }
    if (!require(sawSignedLoad && sawUnsignedLoad,
        "initialized arrays retain signed MOVSX and unsigned MOVZX element loads")) return false;

    if (!require(serialize_reopen(g_modules[0], g_objects[0], &g_objectBytes[0], &g_reopened[0]),
                 "initialized data serializes and deserializes through production objects")) return false;
    ElfObjectHeaderView header = {};
    Diagnostics headerDiagnostics;
    if (!require(inspect_elf_object(g_objects[0], g_objectBytes[0], &header, headerDiagnostics) &&
        header.formatVersion == 2U && header.compilerObjectAbiVersion == 17U &&
        g_reopened[0].mutableDataBytes == sizeof(expected) &&
        std::memcmp(g_reopened[0].mutableData, expected, sizeof(expected)) == 0,
        "format 2 ABI 17 object round-trip preserves the initialized mutable bytes")) return false;
    uint32_t repeatedBytes = 0;
    if (!require(serialize_elf_object(g_modules[0], g_repeatedObject,
        sizeof(g_repeatedObject), &repeatedBytes) && repeatedBytes == g_objectBytes[0] &&
        std::memcmp(g_objects[0], g_repeatedObject, repeatedBytes) == 0,
        "initialized object serialization is deterministic")) return false;

    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_reopened, 1U, &g_linked, linkDiagnostics),
                 "initialized byte-array object links through the production linker")) return false;
    ElfLayout layout = {};
    if (!require(make_elf(g_linked, &layout), "production ELF writer and validator accept initialized arrays")) return false;
    uint64_t rwOffset = 0, rwFileBytes = 0, rwMemoryBytes = 0;
    if (!require(find_rw_segment(g_elf, &rwOffset, &rwFileBytes, &rwMemoryBytes) == 1U &&
        rwFileBytes == sizeof(expected) && rwMemoryBytes == rwFileBytes &&
        std::memcmp(g_elf + rwOffset, expected, sizeof(expected)) == 0,
        "validated writable ELF segment contains the exact compile-time data image")) return false;

    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = 0;
    if (!require(execute_linked(g_linked, g_reopened, 1U, &result, postRun, sizeof(postRun)) && result == 42,
                 "generated AMD64 executes against initialized production ELF data")) return false;
    if (!require(postRun[30] == 1U && postRun[31] == 255U &&
        read_u32(postRun, 0) == 287454020U && read_u32(postRun, 32) == 1432778632U,
        "array writes persist across calls and preserve adjacent scalar globals")) return false;
    return true;
}

static bool test_cross_module_initialized_arrays()
{
    const char* provider =
        "unsigned char shared[4] = {11, 22}; char signedShared[2] = {'Z'};\n"
        "int guard = 287454020;\n"
        "int update_shared(int value) { shared[2] = value; return shared[2]; }\n";
    const char* client =
        "extern unsigned char shared[4]; extern char signedShared[2]; extern int guard;\n"
        "int update_shared(int value);\n"
        "int gx_main(gx_app_context* ctx) {\n"
        " if (shared[0] != 11 || shared[1] != 22 || shared[2] || shared[3] || signedShared[0] != 'Z' || signedShared[1]) return 10;\n"
        " if (update_shared(255) != 255 || shared[2] != 255 || guard != 287454020) return 11;\n"
        " shared[0] = 99; return 42; }\n";
    Diagnostics providerDiagnostics, clientDiagnostics;
    if (!require(compile_text("src/state.c", provider, &g_modules[0], providerDiagnostics) &&
        compile_text("src/main.c", client, &g_modules[1], clientDiagnostics),
        "initialized providers and compatible extern arrays compile across modules")) return false;
    if (!require(g_modules[0].mutableDataBytes == 12U && g_modules[1].mutableDataBytes == 0U,
        "only the defining module allocates initialized array storage")) return false;
    const int32_t shared = find_module_export(g_modules[0], "shared");
    const int32_t signedShared = find_module_export(g_modules[0], "signedShared");
    if (!require(shared >= 0 && signedShared >= 0 &&
        g_modules[0].mutableData[0] == 11U && g_modules[0].mutableData[1] == 22U &&
        g_modules[0].mutableData[2] == 0U && g_modules[0].mutableData[3] == 0U &&
        g_modules[0].mutableData[4] == 'Z' && g_modules[0].mutableData[5] == 0U,
        "provider object stores initialized bytes and zero-fills omitted elements")) return false;
    for (uint32_t i = 0; i < 2U; ++i)
        if (!require(serialize_reopen(g_modules[i], g_objects[i], &g_objectBytes[i], &g_reopened[i]),
            "cross-module initialized and referencing objects round-trip")) return false;
    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_reopened, 2U, &g_linked, linkDiagnostics),
        "production linker resolves initialized byte arrays and applies data relocations")) return false;
    const int32_t linkedShared = find_linked_export(g_linked, "shared");
    if (!require(linkedShared >= 0 && g_linked.exports[linkedShared].size == 4U &&
        g_linked.exports[linkedShared].kind == SymbolKind::DataUnsignedCharArray &&
        g_linked.mutableDataBytes == 12U && g_linked.mutableData[0] == 11U &&
        g_linked.mutableData[1] == 22U,
        "linking retains one export and one initialized storage allocation")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = 0;
    if (!require(execute_linked(g_linked, g_reopened, 2U, &result, postRun, sizeof(postRun)) && result == 42,
        "cross-module generated code reads and mutates initialized arrays on the host")) return false;
    if (!require(postRun[g_linked.exports[linkedShared].finalDataOffset] == 99U &&
        postRun[g_linked.exports[linkedShared].finalDataOffset + 2U] == 255U,
        "cross-module writes preserve initialized values and shared storage identity")) return false;

    const char* badProvider = "char conflict[2] = {'a'}; int gx_main(gx_app_context* ctx) { return 0; }";
    const char* badClient = "extern unsigned char conflict[2]; int gx_main2() { return conflict[0]; }";
    Diagnostics badProviderDiagnostics, badClientDiagnostics;
    if (!require(compile_text("src/bad_state.c", badProvider, &g_modules[0], badProviderDiagnostics) &&
        compile_text("src/bad_client.c", badClient, &g_modules[1], badClientDiagnostics),
        "incompatible cross-module declarations reach typed linker validation")) return false;
    Diagnostics firstMismatch, secondMismatch;
    const bool firstRejected = !link_modules(g_modules, 2U, &g_linked, firstMismatch);
    const bool secondRejected = !link_modules(g_modules, 2U, &g_linked, secondMismatch);
    if (!require(firstRejected && secondRejected && firstMismatch.count() != 0U &&
        secondMismatch.count() != 0U && firstMismatch.at(0).kind == CompilerDiagnosticKind::Linker &&
        std::strcmp(firstMismatch.at(0).message, secondMismatch.at(0).message) == 0,
        "incompatible signedness produces a deterministic linker diagnostic")) return false;
    return true;
}

static bool test_initializer_grammar_and_diagnostics()
{
    if (!expect_rejection("src/scalar_array.c", "unsigned char bytes[2] = 1;",
        CompilerDiagnosticKind::Unsupported, "bounded integer list",
        "scalar-form byte-array initialization is rejected at its source location")) return false;
    if (!expect_rejection("src/string_array.c", "char bytes[4] = \"abc\";",
        CompilerDiagnosticKind::Unsupported, "bounded integer list",
        "string byte-array initialization is rejected instead of silently accepted")) return false;
    if (!expect_rejection("src/nested.c", "char bytes[2] = {{1}};",
        CompilerDiagnosticKind::Unsupported, "constant integer",
        "nested initializer lists are rejected with an unsupported diagnostic")) return false;
    if (!expect_rejection("src/runtime_call.c",
        "int value(); unsigned char bytes[1] = {value()};",
        CompilerDiagnosticKind::Unsupported, "constant integer",
        "runtime function calls are rejected in file-scope initializers")) return false;
    if (!expect_rejection("src/runtime_value.c",
        "int value = 1; unsigned char bytes[1] = {value};",
        CompilerDiagnosticKind::Unsupported, "constant integer",
        "runtime-dependent variables are rejected in file-scope initializers")) return false;
    if (!expect_rejection("src/arithmetic.c", "unsigned char bytes[2] = {1 + 2};",
        CompilerDiagnosticKind::Unsupported, "expressions and missing separators",
        "unsupported arithmetic expressions are rejected rather than evaluated")) return false;
    if (!expect_rejection("src/missing_comma.c", "char bytes[2] = {1 2};",
        CompilerDiagnosticKind::Unsupported, "missing separators",
        "missing list separators receive a bounded unsupported diagnostic")) return false;
    if (!expect_rejection("src/unterminated.c", "char bytes[2] = {1, 2;",
        CompilerDiagnosticKind::Syntax, "expected '}'",
        "unterminated initializer lists are rejected with a syntax diagnostic")) return false;
    if (!expect_rejection("src/malformed_literal.c", "char bytes[1] = {'\\q'};",
        CompilerDiagnosticKind::Unsupported, "escape",
        "malformed character literals retain lexical diagnostics")) return false;
    if (!expect_rejection("src/invalid_type.c", "float bytes[2] = {1, 2};",
        CompilerDiagnosticKind::Syntax, "expected external declaration",
        "invalid initializer element types are rejected")) return false;
    if (!expect_rejection("src/local.c",
        "int gx_main(gx_app_context* ctx) { char bytes[2] = {'a', 'b'}; return 0; }",
        CompilerDiagnosticKind::Unsupported, "array initializers are not supported",
        "local byte-array initialization remains explicitly deferred")) return false;

    CompiledModule module = {};
    Diagnostics diagnostics;
    if (!require(compile_text("src/empty_list.c", "unsigned char bytes[3] = {};", &module, diagnostics) &&
        module.mutableDataBytes == 3U && module.mutableData[0] == 0U &&
        module.mutableData[1] == 0U && module.mutableData[2] == 0U,
        "empty global initializer lists produce a complete zero-filled array")) return false;
    if (!expect_rejection("src/trailing.c", "char bytes[2] = {1, 2, 3};",
        CompilerDiagnosticKind::Syntax, "more elements than its declared length",
        "excess initializers are never truncated")) return false;
    return true;
}

static std::string make_byte_list(uint32_t count)
{
    std::string source = "unsigned char values[256] = {";
    for (uint32_t i = 0; i < count; ++i) {
        if (i) source += ',';
        source += static_cast<char>('0' + ((i / 100U) % 10U));
        source += static_cast<char>('0' + ((i / 10U) % 10U));
        source += static_cast<char>('0' + (i % 10U));
    }
    source += "};";
    return source;
}

static std::string make_storage_source(uint32_t finalIntArrayLength)
{
    std::string source;
    for (uint32_t i = 0; i < 31U; ++i) {
        char declaration[48] = {};
        std::snprintf(declaration, sizeof(declaration), "unsigned char b%u[256] = {};\n", i);
        source += declaration;
    }
    char finalDeclaration[64] = {};
    std::snprintf(finalDeclaration, sizeof(finalDeclaration), "int words[%u] = {};\n", finalIntArrayLength);
    source += finalDeclaration;
    return source;
}

static bool test_limits_and_narrowing()
{
    std::string maximumSource = make_byte_list(256U);
    CompiledModule maximum = {};
    Diagnostics maximumDiagnostics;
    if (!require(compile_text("src/maximum_initializer.c", maximumSource.c_str(),
        &maximum, maximumDiagnostics) && maximum.mutableDataBytes == 256U &&
        maximum.exports[find_module_export(maximum, "values")].elementCount == 256U,
        "256-element initializer reaches the established global-array bound")) return false;
    bool ordered = true;
    for (uint32_t i = 0; i < 256U; ++i)
        ordered = ordered && maximum.mutableData[i] == static_cast<uint8_t>(i % 256U);
    if (!require(ordered, "all 256 parsed values preserve order and low-byte narrowing")) return false;

    std::string tooManySource = make_byte_list(257U);
    if (!expect_rejection("src/too_many.c", tooManySource.c_str(),
        CompilerDiagnosticKind::Syntax, "more elements than its declared length",
        "the parser rejects the first initializer beyond the array extent")) return false;
    if (!expect_rejection("src/zero_length.c", "char bytes[0] = {};",
        CompilerDiagnosticKind::Syntax, "array length",
        "zero-length arrays remain unsupported")) return false;
    if (!expect_rejection("src/oversized.c", "char bytes[257] = {};",
        CompilerDiagnosticKind::Syntax, "array length",
        "oversized array declarations are rejected before storage calculation")) return false;
    if (!expect_rejection("src/huge.c", "char bytes[2147483647] = {};",
        CompilerDiagnosticKind::Syntax, "array length",
        "large dimensions are rejected before multiplication or allocation")) return false;
    if (!expect_rejection("src/unsized.c", "char bytes[] = {};",
        CompilerDiagnosticKind::Syntax, "array length",
        "unsized file-scope arrays are rejected")) return false;

    std::string atCapacitySource = make_storage_source(64U);
    CompiledModule atCapacity = {};
    Diagnostics atCapacityDiagnostics;
    if (!require(compile_text("src/data_at_capacity.c", atCapacitySource.c_str(),
        &atCapacity, atCapacityDiagnostics) && atCapacity.mutableDataBytes == 8192U,
        "initialized and zero-filled globals can exactly reach the 8,192-byte cap")) return false;
    std::string overCapacitySource = make_storage_source(65U);
    CompiledModule overCapacity = {};
    Diagnostics overCapacityDiagnostics;
    if (!require(!compile_text("src/data_over_capacity.c", overCapacitySource.c_str(),
        &overCapacity, overCapacityDiagnostics) && overCapacityDiagnostics.count() != 0U &&
        overCapacityDiagnostics.count() <= COMPILER_MAX_DIAGNOSTICS &&
        overCapacityDiagnostics.at(0).kind == CompilerDiagnosticKind::ResourceLimit &&
        diagnostic_contains(overCapacityDiagnostics, "mutable global data capacity exceeded"),
        "aggregate initialized storage overflow receives one bounded resource diagnostic")) return false;
    return true;
}

} // namespace

int main()
{
    if (!test_initialized_storage_and_host_execution() ||
        !test_cross_module_initialized_arrays() ||
        !test_initializer_grammar_and_diagnostics() ||
        !test_limits_and_narrowing()) return 1;
    std::printf("COMP4N_BOUNDED_ARRAY_INITIALIZER_ASSERTIONS=%u result=PASS\n", g_assertions);
    return 0;
}
