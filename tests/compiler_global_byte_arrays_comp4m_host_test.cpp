// COMP4M hosted proof for persistent file-scope signed and unsigned byte arrays.
// It compiles, serializes, links, emits and validates the production ELF, then
// executes generated AMD64 against the image's writable global-data segment.

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
static CompiledModule g_bad[4] = {};
static LinkedProgram g_linked = {};
static uint8_t g_objects[2][COMPILER_MAX_OBJECT_BYTES] = {};
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
                const uint16_t swap = order[i]; order[i] = order[j]; order[j] = swap;
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
                relocationsValid = false; break;
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

static bool test_global_arrays_and_elf_execution()
{
    const char* source =
        "unsigned char data[4];\n"
        "int before = 305419896;\n"
        "int after = 1432778632;\n"
        "int mutate_byte(int value) { data[1] = value; return data[1]; }\n"
        "int gx_main(gx_app_context* ctx) {\n"
        "  if (data[0] || data[1] || data[2] || data[3]) return 10;\n"
        "  data[0] = 12; data[1] = 255; data[2] = 128; data[3] = 27;\n"
        "  if (data[0] != 12 || data[1] != 255 || data[2] != 128 || data[3] != 27) return 11;\n"
        "  if (mutate_byte(200) != 200 || data[1] != 200 || mutate_byte(456) != 200 || data[1] != 200) return 12;\n"
        "  data[0] = 257; if (data[0] != 1) return 15;\n"
        "  if (before != 305419896 || after != 1432778632) return 16;\n"
        "  return 42;\n"
        "}\n";
    Diagnostics diagnostics;
    if (!require(compile_text("src/main.c", source, &g_modules[0], diagnostics),
                 "production compiler accepts bounded global byte arrays")) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "compile diagnostic: %s\n", diagnostics.at(i).message);
        return false;
    }
    if (!require(g_modules[0].globalCount == 3 && g_modules[0].mutableDataBytes == 12,
                 "global arrays use bounded contiguous storage with integer alignment")) return false;
    const int32_t data = find_module_export(g_modules[0], "data");
    const int32_t before = find_module_export(g_modules[0], "before");
    const int32_t after = find_module_export(g_modules[0], "after");
    if (!require(data >= 0 && before >= 0 && after >= 0 &&
                 g_modules[0].exports[data].kind == SymbolKind::DataUnsignedCharArray &&
                 g_modules[0].exports[data].size == 4 && g_modules[0].exports[data].alignment == 1 &&
                 g_modules[0].exports[data].elementCount == 4 && g_modules[0].exports[data].elementSize == 1 &&
                 g_modules[0].exports[data].moduleDataOffset == 0 &&
                 g_modules[0].exports[before].moduleDataOffset == 4 &&
                 g_modules[0].exports[after].moduleDataOffset == 8,
                 "signedness, element size, alignment, and adjacent offsets are preserved")) return false;
    bool arraysZero = true;
    for (uint32_t i = 0; i < 4; ++i)
        arraysZero = arraysZero && g_modules[0].mutableData[i] == 0;
    if (!require(arraysZero && read_u32(g_modules[0].mutableData, 4) == 305419896U &&
                 read_u32(g_modules[0].mutableData, 8) == 1432778632U,
                 "every byte starts at zero and alignment padding does not overlap sentinels")) return false;

    if (!require(serialize_and_reopen(g_modules[0], g_objects[0], &g_objectBytes[0], &g_restored[0]),
                 "global byte array object serializes and reopens")) return false;
    ElfObjectHeaderView header = {};
    Diagnostics headerDiagnostics;
    if (!require(inspect_elf_object(g_objects[0], g_objectBytes[0], &header, headerDiagnostics) &&
                 header.formatVersion == 2 && header.compilerObjectAbiVersion == 17 &&
                 g_restored[0].mutableDataBytes == 12 &&
                 find_module_export(g_restored[0], "data") >= 0 &&
                 g_restored[0].exports[find_module_export(g_restored[0], "data")].kind ==
                     SymbolKind::DataUnsignedCharArray,
                 "format 2 ABI 17 object retains global byte-array metadata")) return false;
    uint32_t repeatedBytes = 0;
    if (!require(serialize_elf_object(g_modules[0], g_objectAgain, sizeof(g_objectAgain), &repeatedBytes) &&
                 repeatedBytes == g_objectBytes[0] &&
                 std::memcmp(g_objects[0], g_objectAgain, repeatedBytes) == 0,
                 "global array object serialization is deterministic")) return false;
    std::memcpy(g_objectAgain, g_objects[0], g_objectBytes[0]);
    g_objectAgain[header.metaOffset + 16U] = 16U;
    const uint64_t oldAbiChecksum = elf_object_checksum(g_objectAgain, g_objectBytes[0],
                                                        header.metaOffset + 112U);
    for (uint32_t i = 0; i < 8U; ++i)
        g_objectAgain[header.metaOffset + 112U + i] = static_cast<uint8_t>(oldAbiChecksum >> (8U * i));
    Diagnostics oldAbiDiagnostics;
    if (!require(!deserialize_elf_object(g_objectAgain, g_objectBytes[0], &g_bad[0], oldAbiDiagnostics) &&
                 diagnostic_contains(oldAbiDiagnostics, "compiler identity"),
                 "cached ABI 16 object is rejected by ABI 17 compiler")) return false;

    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_restored, 1, &g_linked, linkDiagnostics),
                 "serialized byte-array object links through production linker")) return false;
    ElfLayout layout = {};
    if (!require(build_final_elf(g_linked, &layout), "production ELF writer and validator accept global arrays")) return false;
    uint64_t rwOffset = 0, rwFileBytes = 0, rwMemoryBytes = 0;
    if (!require(find_rw_segment(g_elf, &rwOffset, &rwFileBytes, &rwMemoryBytes) == 1 &&
                 rwFileBytes == g_linked.mutableDataBytes && rwMemoryBytes == rwFileBytes &&
                 std::memcmp(g_elf + rwOffset, g_linked.mutableData, g_linked.mutableDataBytes) == 0,
                 "ELF writable segment stores zero-filled arrays as file-backed bytes")) return false;
    const uint8_t unsignedLoad[] = {0x0F, 0xB6, 0x02};
    const uint8_t byteStore[] = {0x88, 0x02};
    bool sawUnsignedLoad = false, sawByteStore = false;
    for (uint32_t i = 0; i + 3U <= g_restored[0].codeBytes; ++i) {
        sawUnsignedLoad = sawUnsignedLoad || std::memcmp(g_restored[0].code + i, unsignedLoad, sizeof(unsignedLoad)) == 0;
        if (i + sizeof(byteStore) <= g_restored[0].codeBytes)
            sawByteStore = sawByteStore || std::memcmp(g_restored[0].code + i, byteStore, sizeof(byteStore)) == 0;
    }
    if (!require(sawUnsignedLoad && sawByteStore,
                 "AMD64 emits unsigned MOVZX and byte-width stores for global arrays")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = 0;
    if (!require(execute_final_elf(g_linked, g_restored, 1, &result, postRun, sizeof(postRun)) && result == 42,
                 "generated AMD64 executes against production ELF global storage")) return false;
    if (!require(postRun[0] == 1 && postRun[1] == 200 && postRun[2] == 128 && postRun[3] == 27 &&
                 read_u32(postRun, 4) == 305419896U && read_u32(postRun, 8) == 1432778632U,
                 "repeated mutation preserves adjacent integer sentinels")) return false;
    return true;
}

static bool test_cross_module_arrays()
{
    const char* stateSource =
        "unsigned char shared[4]; char signedShared[2]; int guard = 287454020;\n"
        "int update_state(int value) { shared[2] = value; signedShared[1] = value; return shared[2] + signedShared[1]; }\n";
    const char* mainSource =
        "extern unsigned char shared[4]; extern char signedShared[2]; extern int guard;\n"
        "int update_state(int value);\n"
        "int gx_main(gx_app_context* ctx) {\n"
        "  if (shared[0] || shared[1] || shared[2] || shared[3] || signedShared[0] || signedShared[1]) return 5;\n"
        "  shared[0] = 4; int value = update_state(255);\n"
        "  if (value != 254 || shared[0] != 4 || shared[2] != 255 || signedShared[1] != -1) return 6;\n"
        "  if (guard != 287454020) return 7; return 42;\n"
        "}\n";
    Diagnostics stateDiagnostics, mainDiagnostics;
    if (!require(compile_text("src/state.c", stateSource, &g_modules[0], stateDiagnostics) &&
                 compile_text("src/main.c", mainSource, &g_modules[1], mainDiagnostics),
                 "cross-module signed and unsigned byte-array declarations compile")) return false;
    for (uint32_t i = 0; i < 2; ++i) {
        if (!require(serialize_and_reopen(g_modules[i], g_objects[i], &g_objectBytes[i], &g_restored[i]),
                     "cross-module array objects serialize and reopen")) return false;
    }
    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_restored, 2, &g_linked, linkDiagnostics),
                 "typed global byte-array imports resolve across objects")) {
        for (uint32_t i = 0; i < linkDiagnostics.count(); ++i)
            std::fprintf(stderr, "link diagnostic: %s\n", linkDiagnostics.at(i).message);
        return false;
    }
    const int32_t shared = find_linked_export(g_linked, "shared");
    const int32_t signedShared = find_linked_export(g_linked, "signedShared");
    const int32_t guard = find_linked_export(g_linked, "guard");
    if (!require(shared >= 0 && signedShared >= 0 && guard >= 0 &&
                 g_linked.exports[shared].kind == SymbolKind::DataUnsignedCharArray &&
                 g_linked.exports[signedShared].kind == SymbolKind::DataCharArray &&
                 g_linked.exports[shared].elementCount == 4 && g_linked.exports[shared].elementSize == 1 &&
                 g_linked.exports[shared].size == 4 && g_linked.exports[shared].alignment == 1,
                 "linked symbol identity retains byte-array count, width, and signedness")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = 0;
    if (!require(execute_final_elf(g_linked, g_restored, 2, &result, postRun, sizeof(postRun)) && result == 42,
                 "cross-module global byte arrays execute after ELF-object linking")) return false;
    if (!require(postRun[g_linked.exports[shared].finalDataOffset] == 4 &&
                 postRun[g_linked.exports[shared].finalDataOffset + 2U] == 255 &&
                 postRun[g_linked.exports[signedShared].finalDataOffset + 1U] == 255 &&
                 read_u32(postRun, g_linked.exports[guard].finalDataOffset) == 287454020U,
                 "cross-module writes persist and preserve the adjacent integer guard")) return false;

    Diagnostics mismatchProvider, mismatchConsumer;
    if (!require(compile_text("src/provider.c", "unsigned char mismatch[3];", &g_bad[0], mismatchProvider) &&
                 compile_text("src/consumer.c", "extern char mismatch[3]; int gx_main(gx_app_context* ctx) { return mismatch[0]; }",
                              &g_bad[1], mismatchConsumer),
                 "array signedness mismatch inputs compile independently")) return false;
    Diagnostics mismatchLink;
    const bool mismatchRejected = !link_modules(g_bad, 2, &g_linked, mismatchLink);
    if (!mismatchRejected) {
        std::fprintf(stderr, "signedness mismatch unexpectedly linked\n");
    } else if (!diagnostic_contains(mismatchLink, "conflicting declaration for global 'mismatch'")) {
        for (uint32_t i = 0; i < mismatchLink.count(); ++i)
            std::fprintf(stderr, "mismatch diagnostic: %s\n", mismatchLink.at(i).message);
    }
    if (!require(mismatchRejected &&
                 diagnostic_contains(mismatchLink, "conflicting declaration for global 'mismatch'"),
                 "linker rejects signedness mismatch for byte arrays")) return false;

    Diagnostics sizeProvider, sizeConsumer;
    if (!require(compile_text("src/provider.c", "unsigned char sizeMismatch[3];", &g_bad[0], sizeProvider) &&
                 compile_text("src/consumer.c",
                     "extern unsigned char sizeMismatch[4]; int gx_main(gx_app_context* ctx) { return sizeMismatch[0]; }",
                     &g_bad[1], sizeConsumer),
                 "array extent mismatch inputs compile independently")) return false;
    Diagnostics sizeLink;
    if (!require(!link_modules(g_bad, 2, &g_linked, sizeLink) &&
                 diagnostic_contains(sizeLink, "conflicting declaration for global 'sizeMismatch'"),
                 "linker rejects byte-array extent mismatches")) return false;

    Diagnostics duplicateProvider, duplicateConsumer;
    if (!require(compile_text("src/a.c", "unsigned char duplicate[2];", &g_bad[2], duplicateProvider) &&
                 compile_text("src/b.c", "unsigned char duplicate[2];", &g_bad[3], duplicateConsumer),
                 "duplicate byte-array definitions compile independently")) return false;
    Diagnostics duplicateLink;
    if (!require(!link_modules(&g_bad[2], 2, &g_linked, duplicateLink) &&
                 diagnostic_contains(duplicateLink, "duplicate definition for global 'duplicate'"),
                 "linker rejects duplicate global byte-array definitions")) return false;
    return true;
}

static bool test_signed_array_execution()
{
    const char* source =
        "char signedBytes[2];\n"
        "int gx_main(gx_app_context* ctx) {\n"
        "  if (signedBytes[0] || signedBytes[1]) return 1;\n"
        "  signedBytes[0] = 255; signedBytes[1] = 128;\n"
        "  if (signedBytes[0] != -1 || signedBytes[1] != -128) return 2;\n"
        "  return 42;\n"
        "}\n";
    Diagnostics diagnostics;
    if (!require(compile_text("src/signed.c", source, &g_modules[0], diagnostics),
                 "signed global byte array compiles")) return false;
    const int32_t signedBytes = find_module_export(g_modules[0], "signedBytes");
    if (!require(g_modules[0].mutableDataBytes == 2 && g_modules[0].mutableData[0] == 0 &&
                 g_modules[0].mutableData[1] == 0 && signedBytes >= 0 &&
                 g_modules[0].exports[signedBytes].kind == SymbolKind::DataCharArray,
                 "signed global array has two zero-filled one-byte elements")) return false;
    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_modules, 1, &g_linked, linkDiagnostics),
                 "signed global byte array links")) return false;
    const uint8_t signedLoad[] = {0x0F, 0xBE, 0x02};
    bool sawSignedLoad = false;
    for (uint32_t i = 0; i + sizeof(signedLoad) <= g_modules[0].codeBytes; ++i)
        sawSignedLoad = sawSignedLoad || std::memcmp(g_modules[0].code + i, signedLoad, sizeof(signedLoad)) == 0;
    if (!require(sawSignedLoad, "AMD64 emits MOVSX for signed global byte-array reads")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = 0;
    if (!require(execute_final_elf(g_linked, g_modules, 1, &result, postRun, sizeof(postRun)) &&
                 result == 42 && postRun[0] == 255 && postRun[1] == 128,
                 "signed global loads sign-extend and stores retain the low byte")) return false;
    return true;
}

static bool test_global_array_pointer_interop()
{
    const char* source =
        "unsigned char bytes[4]; int guard = 287454020;\n"
        "int store_byte(unsigned char* p, int value) { p[2] = value; return p[2]; }\n"
        "int gx_main(gx_app_context* ctx) {\n"
        "  unsigned char* p = bytes;\n"
        "  if (store_byte(p, 200) != 200 || p[2] != 200 || p[0] != 0) return 1;\n"
        "  if (guard != 287454020) return 2; return 42;\n"
        "}\n";
    Diagnostics diagnostics;
    if (!require(compile_text("src/global_pointer.c", source, &g_modules[0], diagnostics),
                 "global unsigned-byte array decays to a typed pointer")) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "pointer compile diagnostic: %s\n", diagnostics.at(i).message);
        return false;
    }
    if (!require(serialize_and_reopen(g_modules[0], g_objects[0], &g_objectBytes[0], &g_restored[0]),
                 "global-array pointer object serializes and reopens")) return false;
    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_restored, 1, &g_linked, linkDiagnostics),
                 "global-array pointer object links")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = 0;
    const int32_t bytes = find_linked_export(g_linked, "bytes");
    const int32_t guard = find_linked_export(g_linked, "guard");
    if (!require(bytes >= 0 && guard >= 0 &&
                 execute_final_elf(g_linked, g_restored, 1, &result, postRun, sizeof(postRun)) && result == 42,
                 "descriptor-backed global byte pointer executes through its function parameter")) return false;
    if (!require(postRun[g_linked.exports[bytes].finalDataOffset + 2U] == 200 &&
                 read_u32(postRun, g_linked.exports[guard].finalDataOffset) == 287454020U,
                 "global pointer writes are byte-scaled and preserve the adjacent scalar")) return false;
    return true;
}

static bool test_global_array_pointer_bounds()
{
    const char* source =
        "unsigned char bytes[4]; int sentinel = 287454020;\n"
        "int read_at(unsigned char* p, int index) { return p[index]; }\n"
        "int gx_main(gx_app_context* ctx) { unsigned char* p = bytes; return read_at(p, 4); }\n";
    Diagnostics diagnostics;
    if (!require(compile_text("src/global_pointer_bounds.c", source, &g_modules[0], diagnostics),
                 "global-array pointer runtime-bounds fixture compiles")) return false;
    if (!require(serialize_and_reopen(g_modules[0], g_objects[0], &g_objectBytes[0], &g_restored[0]),
                 "global-array pointer bounds object round-trips")) return false;
    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_restored, 1, &g_linked, linkDiagnostics),
                 "global-array pointer bounds object links")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = -1;
    if (!require(execute_final_elf(g_linked, g_restored, 1, &result, postRun, sizeof(postRun)) && result == 0,
                 "out-of-range global-array pointer dereference takes its descriptor failure path")) return false;
    const int32_t sentinel = find_linked_export(g_linked, "sentinel");
    if (!require(sentinel >= 0 && read_u32(postRun, g_linked.exports[sentinel].finalDataOffset) == 287454020U,
                 "global-array pointer bounds failure cannot access the neighboring scalar")) return false;
    return true;
}

static bool test_runtime_bounds_check()
{
    const char* source =
        "unsigned char bytes[4]; int sentinel = 287454020;\n"
        "int read_at(int index) { return bytes[index]; }\n"
        "int gx_main(gx_app_context* ctx) { return read_at(4); }\n";
    Diagnostics diagnostics;
    if (!require(compile_text("src/runtime_bounds.c", source, &g_modules[0], diagnostics),
                 "variable global-array index compiles with runtime bounds checks")) return false;
    if (!require(serialize_and_reopen(g_modules[0], g_objects[0], &g_objectBytes[0], &g_restored[0]),
                 "runtime-bounds global-array object serializes")) return false;
    Diagnostics linkDiagnostics;
    if (!require(link_modules(g_restored, 1, &g_linked, linkDiagnostics),
                 "runtime-bounds global-array module links")) return false;
    uint8_t postRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    int32_t result = -1;
    if (!require(execute_final_elf(g_linked, g_restored, 1, &result, postRun, sizeof(postRun)) && result == 0,
                 "dynamic out-of-range access takes the generated bounds-failure path")) return false;
    const int32_t sentinel = find_linked_export(g_linked, "sentinel");
    if (!require(sentinel >= 0 && read_u32(postRun, g_linked.exports[sentinel].finalDataOffset) == 287454020U,
                 "runtime bounds failure does not read or alter the adjacent global")) return false;
    return true;
}

static bool expect_rejection(const char* path, const char* source, const char* needle,
                             const char* message)
{
    CompiledModule module = {};
    Diagnostics diagnostics;
    const bool rejected = !compile_text(path, source, &module, diagnostics) && diagnostics.count() != 0 &&
        diagnostics.at(0).location.line != 0 && diagnostic_contains(diagnostics, needle);
    return require(rejected, message);
}

static bool test_bounds_initializers_and_limits()
{
    if (!expect_rejection("src/zero.c", "unsigned char bytes[0];", "array length",
                          "zero-length global arrays receive a located diagnostic")) return false;
    if (!expect_rejection("src/negative.c", "char bytes[-1];", "array length",
                          "negative global dimensions receive a located diagnostic")) return false;
    if (!expect_rejection("src/large.c", "unsigned char bytes[65535];", "array length",
                          "dimensions beyond the array limit are rejected before size arithmetic")) return false;
    CompiledModule maximum = {};
    Diagnostics maximumDiagnostics;
    if (!require(compile_text("src/maximum.c", "unsigned char bytes[256];", &maximum, maximumDiagnostics) &&
                 maximum.mutableDataBytes == 256 && maximum.globalCount == 1 &&
                 find_module_export(maximum, "bytes") >= 0 &&
                 maximum.exports[find_module_export(maximum, "bytes")].elementCount == COMPILER_MAX_ARRAY_ELEMENTS,
                 "maximum bounded byte-array dimension uses 256 bytes")) return false;
    if (!expect_rejection("src/malformed.c", "unsigned char bytes[4;", "expected ']'",
                          "malformed byte-array declarations receive a located diagnostic")) return false;
    if (!expect_rejection("src/brace_init.c", "char bytes[2] = {1, 2};", "array initializers are not supported",
                          "unsupported byte-array list initializers are diagnosed")) return false;
    if (!expect_rejection("src/scalar_init.c", "unsigned char bytes[2] = 1;", "array initializers are not supported",
                          "unsupported scalar-form byte-array initializers are diagnosed")) return false;
    if (!expect_rejection("src/string_init.c", "char bytes[4] = \"abc\";", "array initializers are not supported",
                          "unsupported string byte-array initializers are diagnosed")) return false;
    if (!expect_rejection("src/type.c", "float bytes[4];", "expected",
                          "unsupported global element types receive a located diagnostic")) return false;
    if (!expect_rejection("src/oob.c",
                          "unsigned char bytes[4]; int gx_main(gx_app_context* ctx) { return bytes[4]; }",
                          "out of bounds", "statically provable global-array indexes are rejected")) return false;
    if (!expect_rejection("src/oob_write.c",
                          "char bytes[2]; int gx_main(gx_app_context* ctx) { bytes[-1] = 1; return 0; }",
                          "out of bounds", "statically provable negative indexes are rejected")) return false;
    if (!expect_rejection("src/invalid_assignment.c",
                          "unsigned char bytes[2]; int gx_main(gx_app_context* ctx) { bytes[0] = bytes; return 0; }",
                          "require an int expression", "array-element assignments reject pointer values")) return false;
    if (!expect_rejection("src/array_assignment.c",
                          "unsigned char bytes[2]; int gx_main(gx_app_context* ctx) { bytes = 1; return 0; }",
                          "array value cannot be assigned", "whole-array assignment is rejected")) return false;
    if (!expect_rejection("src/signed_decay.c",
                          "char bytes[2]; int gx_main(gx_app_context* ctx) { unsigned char* p = bytes; return 0; }",
                          "signed char arrays do not convert", "signed byte arrays cannot decay to unsigned pointers")) return false;

    if (!expect_rejection("src/duplicate.c", "unsigned char value[2]; unsigned char value[2];",
                          "duplicate definition for global", "duplicate file-scope array definitions are rejected")) return false;
    char storageOverflowSource[4096] = {};
    uint32_t storageOverflowBytes = 0;
    for (uint32_t i = 0; i < COMPILER_MAX_GLOBALS - 1U; ++i) {
        const int written = std::snprintf(storageOverflowSource + storageOverflowBytes,
            sizeof(storageOverflowSource) - storageOverflowBytes, "unsigned char b%u[256];\n", i);
        if (written < 0 || static_cast<uint32_t>(written) >= sizeof(storageOverflowSource) - storageOverflowBytes)
            return require(false, "overflow fixture fits its bounded source buffer");
        storageOverflowBytes += static_cast<uint32_t>(written);
    }
    const int finalWritten = std::snprintf(storageOverflowSource + storageOverflowBytes,
        sizeof(storageOverflowSource) - storageOverflowBytes, "int words[256];\n");
    if (finalWritten < 0 || static_cast<uint32_t>(finalWritten) >=
        sizeof(storageOverflowSource) - storageOverflowBytes)
        return require(false, "overflow fixture's final declaration fits");
    CompiledModule overflow = {};
    Diagnostics overflowDiagnostics;
    if (!require(!compile_text("src/storage_overflow.c", storageOverflowSource,
                               &overflow, overflowDiagnostics) &&
                 overflowDiagnostics.count() != 0 &&
                 overflowDiagnostics.at(0).kind == CompilerDiagnosticKind::ResourceLimit &&
                 diagnostic_contains(overflowDiagnostics, "mutable global data capacity exceeded"),
                 "aggregate global storage overflow is rejected before layout arithmetic wraps")) return false;
    return true;
}

} // namespace

int main()
{
    if (!test_global_arrays_and_elf_execution() || !test_signed_array_execution() ||
        !test_global_array_pointer_interop() || !test_global_array_pointer_bounds() ||
        !test_cross_module_arrays() ||
        !test_runtime_bounds_check() || !test_bounds_initializers_and_limits()) return 1;
    std::printf("COMP4M_GLOBAL_BYTE_ARRAY_ASSERTIONS=%u result=PASS\n", g_assertions);
    return 0;
}
