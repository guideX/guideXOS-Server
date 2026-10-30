// COMP4O exercises the production guideXOS compiler, linker, object writer,
// ELF writer, and generated AMD64 code. The source library is never host-built.

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
static const uint32_t kModuleCount = 11U;
static CompiledModule g_modules[kModuleCount] = {};
static CompiledModule g_reopened[kModuleCount] = {};
static CompiledModule g_mismatch = {};
static CompiledModule g_unsupported = {};
static LinkedProgram g_linked = {};
static uint8_t g_objects[kModuleCount][COMPILER_MAX_OBJECT_BYTES] = {};
static uint32_t g_objectBytes[kModuleCount] = {};
static uint8_t g_elf[BOOTSTRAP_MAX_ELF_BYTES] = {};
static char g_sources[kModuleCount][COMPILER_MAX_SOURCE_BYTES + 1U] = {};
static char g_publicHeader[COMPILER_MAX_DECLARATION_FILE_BYTES + 1U] = {};
static const int32_t g_expectedLocal[10] = {0, 17, 34, 51, 68, 85, 102, 119, 165, 90};

static bool require(bool condition, const char* message)
{
    ++g_assertions;
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool diagnostic_contains(const Diagnostics& diagnostics, const char* needle)
{
    if (!needle) return false;
    for (uint32_t i = 0; i < diagnostics.count(); ++i) {
        const char* message = diagnostics.at(i).message;
        if (message && std::strstr(message, needle)) return true;
    }
    return false;
}

static bool read_source(const char* path, char* output, uint32_t capacity,
                        uint32_t* outputBytes)
{
    if (!path || !output || capacity < 2U || !outputBytes) return false;
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    const size_t count = std::fread(output, 1U, capacity - 1U, file);
    const int extra = std::fgetc(file);
    const bool valid = !std::ferror(file) && extra == EOF;
    std::fclose(file);
    if (!valid) return false;
    output[count] = '\0';
    *outputBytes = static_cast<uint32_t>(count);
    return true;
}

static bool compile_source(const char* path, const char* source, uint32_t sourceBytes,
                           CompiledModule* module, Diagnostics& diagnostics)
{
    return module && source && compile_module_from_source(path, source, sourceBytes,
        module, diagnostics);
}

static int32_t find_export(const LinkedProgram& program, const char* name)
{
    for (uint32_t i = 0; i < program.exportCount; ++i)
        if (std::strcmp(program.exports[i].name, name) == 0) return static_cast<int32_t>(i);
    return -1;
}

static uint32_t read_u32(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<uint32_t>(bytes[offset + 3U]) << 24U);
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

static bool serialize_reopen(const CompiledModule& module, uint8_t* object,
                             uint32_t* objectBytes, CompiledModule* reopened,
                             ElfObjectHeaderView* header)
{
    if (!serialize_elf_object(module, object, COMPILER_MAX_OBJECT_BYTES, objectBytes)) return false;
    Diagnostics diagnostics;
    return inspect_elf_object(object, *objectBytes, header, diagnostics) &&
           deserialize_elf_object(object, *objectBytes, reopened, diagnostics);
}

static uint32_t find_rw_segment(const uint8_t* elf, uint64_t* fileOffset,
                                uint64_t* fileBytes, uint64_t* memoryBytes)
{
    const uint16_t count = static_cast<uint16_t>(elf[56]) |
        static_cast<uint16_t>(static_cast<uint16_t>(elf[57]) << 8U);
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

struct ExecutionResult {
    int32_t value;
    uint32_t runtimeStatus;
    uint32_t runtimeDepth;
};

static ExecutionResult call_entry(void* entry)
{
    ExecutionResult result = {};
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

static bool write_and_validate_elf(const LinkedProgram& linked, ElfLayout* layout)
{
    if (!layout || !write_bootstrap_elf(linked.code, linked.codeBytes,
        linked.data, linked.dataBytes, linked.mutableData, linked.mutableDataBytes,
        linked.entryCodeOffset, g_elf, sizeof(g_elf), layout)) return false;
    ElfValidationResult validation = {};
    return validate_bootstrap_elf(g_elf, layout->outputBytes, layout->imageBase,
        layout->codeOffset, linked.code, linked.codeBytes, &validation,
        linked.data, linked.dataBytes, linked.mutableData, linked.mutableDataBytes,
        linked.entryCodeOffset);
}

static bool execute_linked(const LinkedProgram& linked, const CompiledModule* modules,
                           uint32_t moduleCount, int32_t seedBytePatch,
                           uint8_t* postRunData, uint32_t postRunCapacity,
                           ExecutionResult* result)
{
#if defined(_WIN32)
    if (!modules || moduleCount == 0 || !result || linked.codeBytes == 0) return false;
    ElfLayout layout = {};
    if (!write_and_validate_elf(linked, &layout)) return false;
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
    const int32_t seed = find_export(linked, "gx_comp4o_seed");
    if (seedBytePatch >= 0 && seed >= 0 && linked.exports[seed].finalDataOffset < rwMemoryBytes)
        static_cast<uint8_t*>(dataMemory)[linked.exports[seed].finalDataOffset] =
            static_cast<uint8_t>(seedBytePatch);

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
            const int32_t symbol = find_export(linked, relocation.targetSymbolName);
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
    (void)linked; (void)modules; (void)moduleCount; (void)seedBytePatch;
    (void)postRunData; (void)postRunCapacity; (void)result;
    return false;
#endif
}

static bool check_i32_array(const LinkedProgram& linked, const uint8_t* data,
                            const char* name, const int32_t* expected, uint32_t count)
{
    const int32_t symbol = find_export(linked, name);
    if (symbol < 0 || linked.exports[symbol].size != count * 4U) return false;
    const uint32_t offset = linked.exports[symbol].finalDataOffset;
    for (uint32_t i = 0; i < count; ++i)
        if (static_cast<int32_t>(read_u32(data, offset + i * 4U)) != expected[i]) return false;
    return true;
}

static bool check_byte_array(const LinkedProgram& linked, const uint8_t* data,
                             const char* name, const uint8_t* expected, uint32_t count)
{
    const int32_t symbol = find_export(linked, name);
    if (symbol < 0 || linked.exports[symbol].size != count) return false;
    const uint32_t offset = linked.exports[symbol].finalDataOffset;
    for (uint32_t i = 0; i < count; ++i)
        if (data[offset + i] != expected[i]) return false;
    return true;
}

static bool test_library()
{
    uint32_t headerBytes = 0;
    const char* paths[kModuleCount] = {
        "sdk/libraries/bytes/gx_bytes_copy.c",
        "sdk/libraries/bytes/gx_bytes_fill.c",
        "sdk/libraries/bytes/gx_bytes_compare.c",
        "sdk/libraries/bytes/gx_bytes_strlen_bounded.c",
        "tests/fixtures/comp4o/copy_app.c",
        "tests/fixtures/comp4o/copy_local_app.c",
        "tests/fixtures/comp4o/copy_full_app.c",
        "tests/fixtures/comp4o/fill_app.c",
        "tests/fixtures/comp4o/compare_app.c",
        "tests/fixtures/comp4o/strlen_app.c",
        "tests/fixtures/comp4o/bytes_app.c"
    };
    uint32_t sourceBytes[kModuleCount] = {};
    for (uint32_t i = 0; i < kModuleCount; ++i) {
        if (!require(read_source(paths[i], g_sources[i], sizeof(g_sources[i]), &sourceBytes[i]),
                     "native source module is available")) return false;
    }
    if (!require(read_source("sdk/include/guidexos/bytes.h", g_publicHeader,
                             sizeof(g_publicHeader), &headerBytes),
                 "public declarations are available")) return false;
    (void)headerBytes;
    const char* functions[] = {"gx_bytes_copy", "gx_bytes_fill", "gx_bytes_compare",
                               "gx_bytes_strlen_bounded"};
    const char* declarations[] = {
        "int gx_bytes_copy(unsigned char* destination, unsigned char* source, int length);",
        "int gx_bytes_fill(unsigned char* destination, int value, int length);",
        "int gx_bytes_compare(unsigned char* left, unsigned char* right, int length);",
        "int gx_bytes_strlen_bounded(unsigned char* text, int max_length);"
    };
    bool declarationsMatch = true;
    const uint32_t declarationModules[4] = {4U, 7U, 8U, 9U};
    for (uint32_t i = 0; i < 4U; ++i) {
        if (!std::strstr(g_publicHeader, declarations[i]) ||
            !std::strstr(g_sources[declarationModules[i]], declarations[i])) declarationsMatch = false;
    }
    if (!std::strstr(g_sources[5], declarations[0]) || !std::strstr(g_sources[6], declarations[0]))
        declarationsMatch = false;
    if (!require(declarationsMatch, "public declarations match the separately compiled application modules")) return false;

    for (uint32_t i = 0; i < kModuleCount; ++i) {
        Diagnostics diagnostics;
        if (!require(compile_source(paths[i], g_sources[i], sourceBytes[i], &g_modules[i], diagnostics),
                     i < 4U ? "utility function compiles through production compiler" :
                              "application source compiles as a separate module")) {
            for (uint32_t d = 0; d < diagnostics.count(); ++d)
                std::fprintf(stderr, "%s diagnostic: %s\n", paths[i], diagnostics.at(d).message);
            return false;
        }
    }

    for (uint32_t i = 0; i < 4U; ++i) {
        bool exported = false;
        for (uint32_t e = 0; e < g_modules[i].exportCount; ++e)
            if (std::strcmp(g_modules[i].exports[e].name, functions[i]) == 0) exported = true;
        if (!require(exported, "library exports the named native implementation")) return false;
    }
    const uint32_t applicationModules[6] = {4U, 5U, 6U, 7U, 8U, 9U};
    const uint32_t utilityIndexes[6] = {0U, 0U, 0U, 1U, 2U, 3U};
    for (uint32_t i = 0; i < 6U; ++i) {
        const CompiledModule& appModule = g_modules[applicationModules[i]];
        const uint32_t utilityIndex = utilityIndexes[i];
        const char* wanted = functions[utilityIndex];
        bool signatureValid = appModule.importCount == 1U;
        if (signatureValid) {
            const ImportSymbol& symbol = appModule.imports[0];
            signatureValid = std::strcmp(symbol.name, wanted) == 0 &&
                symbol.expectedReturnKind == FunctionReturnKind::Integer &&
                symbol.expectedParameterCount >= 2U &&
                symbol.parameterKinds[0] == ParameterKind::UnsignedCharacterPointer;
            if (signatureValid && (utilityIndex == 0U || utilityIndex == 2U))
                signatureValid = symbol.expectedParameterCount == 3U &&
                    symbol.parameterKinds[1] == ParameterKind::UnsignedCharacterPointer &&
                    symbol.parameterKinds[2] == ParameterKind::Integer;
            else if (signatureValid && utilityIndex == 1U)
                signatureValid = symbol.expectedParameterCount == 3U &&
                    symbol.parameterKinds[1] == ParameterKind::Integer &&
                    symbol.parameterKinds[2] == ParameterKind::Integer;
            else if (signatureValid)
                signatureValid = symbol.expectedParameterCount == 2U &&
                    symbol.parameterKinds[1] == ParameterKind::Integer;
        }
        if (!require(signatureValid, "cross-module import retains its unsigned-byte pointer signature")) return false;
    }

    ElfObjectHeaderView headers[kModuleCount] = {};
    for (uint32_t i = 0; i < kModuleCount; ++i) {
        if (!require(serialize_reopen(g_modules[i], g_objects[i], &g_objectBytes[i],
                                      &g_reopened[i], &headers[i]),
                     "production object serializes and reopens")) return false;
        if (!require(headers[i].formatVersion == COMPILER_OBJECT_FORMAT_VERSION &&
                     headers[i].compilerObjectAbiVersion == COMPILER_OBJECT_ABI_VERSION &&
                     headers[i].formatVersion == 2U && headers[i].compilerObjectAbiVersion == 17U,
                     "object format 2 and ABI 17 remain compatible")) return false;
    }

    Diagnostics mismatchDiagnostics;
    const char* mismatchSource =
        "int gx_bytes_copy(char* destination, unsigned char* source, int length);\n"
        "int gx_main(gx_app_context* ctx) { unsigned char source[1]; "
        "return gx_bytes_copy(\"x\", source, 1); }\n";
    if (!require(compile_module_from_source("tests/fixtures/comp4o/mismatch.c", mismatchSource,
            static_cast<uint32_t>(std::strlen(mismatchSource)), &g_mismatch, mismatchDiagnostics),
            "incompatible prototype is represented for linker validation")) return false;
    CompiledModule mismatchModules[2] = {g_reopened[0], g_mismatch};
    LinkedProgram mismatchProgram = {};
    Diagnostics mismatchLinkDiagnostics;
    const bool mismatchRejected = !link_modules(mismatchModules, 2U, &mismatchProgram, mismatchLinkDiagnostics) &&
        diagnostic_contains(mismatchLinkDiagnostics, "conflicting declaration");
    if (!require(mismatchRejected, "linker rejects signed-character pointer mismatch")) {
        for (uint32_t i = 0; i < mismatchLinkDiagnostics.count(); ++i)
            std::fprintf(stderr, "mismatch link diagnostic: %s\n", mismatchLinkDiagnostics.at(i).message);
        return false;
    }

    Diagnostics unsupportedDiagnostics;
    const char* unsupportedSource =
        "int gx_bytes_strlen_bounded(unsigned char* text, int max_length);\n"
        "int gx_main(gx_app_context* ctx) { return gx_bytes_strlen_bounded(\"abc\", 4); }\n";
    const bool unsupportedRejected = !compile_module_from_source("tests/fixtures/comp4o/unsupported.c",
        unsupportedSource, static_cast<uint32_t>(std::strlen(unsupportedSource)),
        &g_unsupported, unsupportedDiagnostics) && unsupportedDiagnostics.count() != 0U;
    if (!require(unsupportedRejected,
                 "string literals are diagnosed as incompatible with unsigned-byte pointers")) return false;

    if (!require(link_modules(g_reopened, kModuleCount, &g_linked, mismatchLinkDiagnostics),
                 "production linker resolves utility imports")) {
        for (uint32_t i = 0; i < mismatchLinkDiagnostics.count(); ++i)
            std::fprintf(stderr, "link diagnostic: %s\n", mismatchLinkDiagnostics.at(i).message);
        return false;
    }
    if (!require(g_linked.linked && g_linked.moduleCount == kModuleCount &&
                 g_linked.entryCodeOffset < g_linked.codeBytes,
                 "linked program contains utility and application modules")) return false;
    for (uint32_t i = 0; i < 4U; ++i) {
        const int32_t exported = find_export(g_linked, functions[i]);
        if (!require(exported >= 0 && g_linked.exports[exported].finalCodeOffset < g_linked.codeBytes,
                     "final linked code contains each native utility")) return false;
    }

    bool sawUnsignedLoad = false;
    bool sawByteStore = false;
    for (uint32_t i = 0; i + 2U < g_linked.codeBytes; ++i) {
        if (g_linked.code[i] == 0x0F && g_linked.code[i + 1U] == 0xB6) sawUnsignedLoad = true;
        if (g_linked.code[i] == 0x88) sawByteStore = true;
    }
    if (!require(sawUnsignedLoad && sawByteStore,
                 "generated code contains zero-extending byte loads and byte stores")) return false;

    ElfLayout layout = {};
    if (!require(write_and_validate_elf(g_linked, &layout),
                 "linked application writes and validates as native ELF")) return false;
    if (!require(layout.outputBytes <= sizeof(g_elf), "native ELF stays inside writer capacity")) return false;
    uint8_t firstRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    uint8_t secondRun[COMPILER_MAX_LINKED_DATA_BYTES] = {};
    ExecutionResult first = {};
    ExecutionResult second = {};
    if (!require(execute_linked(g_linked, g_reopened, kModuleCount, -1,
            firstRun, sizeof(firstRun), &first),
            "generated AMD64 executes with production utility implementations")) return false;
    if (!require(first.value == 42 && first.runtimeStatus == 0U,
                 "first generated-code run returns 42 without descriptor failure")) return false;
    if (!require(execute_linked(g_linked, g_reopened, kModuleCount, 33,
            secondRun, sizeof(secondRun), &second),
            "same ELF executes again with a distinct global source buffer value")) return false;
    if (!require(second.value == 42 && second.runtimeStatus == 0U,
                 "second generated-code run returns 42 without descriptor failure")) return false;

    const int32_t expectedCopyResults[4] = {0, 0, -1, -2};
    const int32_t expectedFillResults[12] = {0, -1, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const int32_t expectedCompareResults[10] = {0, -1, 1, -1, 1, 1, 0, -2, -2, 0};
    const int32_t expectedStrings[7] = {0, 3, -1, -1, -2, -3, 255};
    if (!require(check_i32_array(g_linked, firstRun, "gx_comp4o_copy_results",
                                  expectedCopyResults, 4U),
                 "copy results match zero, negative and excessive length boundaries")) return false;
    if (!require(check_i32_array(g_linked, firstRun, "gx_comp4o_fill_results",
                                  expectedFillResults, 12U),
                 "fill results match zero, negative, excessive and maximum boundaries")) return false;
    if (!require(check_i32_array(g_linked, firstRun, "gx_comp4o_compare_results",
                                  expectedCompareResults, 10U),
                 "comparison results match unsigned lexical ordering and invalid lengths")) return false;
    if (!require(check_i32_array(g_linked, firstRun, "gx_comp4o_string_results",
                                  expectedStrings, 7U),
                 "bounded string results include empty, final terminator, missing and limit cases")) return false;
    if (!require(check_i32_array(g_linked, firstRun, "gx_comp4o_local_observed",
                                  g_expectedLocal, 10U),
                 "local source and destination interoperate and neighboring bytes stay intact")) return false;

    const uint8_t expectedSeed[8] = {65, 66, 0, 68, 255, 128, 1, 2};
    const uint8_t changedSeedCopy[8] = {33, 66, 0, 68, 255, 128, 1, 2};
    const uint8_t fillExpected[7] = {0, 1, 127, 128, 255, 0, 255};
    const uint8_t fillGuardExpected[9] = {90, 128, 128, 128, 128, 128, 128, 128, 91};
    const uint8_t zeroExpected[4] = {55, 56, 57, 58};
    const uint8_t rejectCopyExpected[4] = {10, 11, 12, 13};
    const uint8_t rejectFillExpected[4] = {20, 21, 22, 23};
    if (!require(check_byte_array(g_linked, firstRun, "gx_comp4o_global_copy", expectedSeed, 8U) &&
                 check_byte_array(g_linked, secondRun, "gx_comp4o_global_copy", changedSeedCopy, 8U),
                 "global copy observes initialized data and a distinct second-run buffer")) return false;
    if (!require(check_byte_array(g_linked, firstRun, "gx_comp4o_fill_values", fillExpected, 7U),
                 "fill narrows 0, 1, 127, 128, 255, 256 and -1 to eight bits")) return false;
    if (!require(check_byte_array(g_linked, firstRun, "gx_comp4o_fill_guarded", fillGuardExpected, 9U),
                 "bounded global fill preserves adjacent elements")) return false;
    if (!require(check_byte_array(g_linked, firstRun, "gx_comp4o_zero_fill", zeroExpected, 4U) &&
                 check_byte_array(g_linked, firstRun, "gx_comp4o_reject_copy", rejectCopyExpected, 4U) &&
                 check_byte_array(g_linked, firstRun, "gx_comp4o_reject_fill", rejectFillExpected, 4U),
                 "zero and rejected lengths leave their buffers unchanged")) return false;
    uint8_t compareUntouched[256] = {};
    compareUntouched[0] = 17;
    if (!require(check_byte_array(g_linked, firstRun, "gx_comp4o_cmp_left",
                                  compareUntouched, 256U) &&
                 check_byte_array(g_linked, firstRun, "gx_comp4o_cmp_right",
                                  compareUntouched, 256U),
                 "comparison leaves input buffers unchanged")) return false;

    uint8_t fullExpected[256] = {};
    uint8_t fullFillExpected[256] = {};
    for (uint32_t i = 0; i < 255U; ++i) fullExpected[i] = static_cast<uint8_t>(i + 1U);
    fullExpected[255] = 0;
    for (uint32_t i = 0; i < 256U; ++i) fullFillExpected[i] = 171U;
    if (!require(check_byte_array(g_linked, firstRun, "gx_comp4o_full_source", fullExpected, 256U) &&
                 check_byte_array(g_linked, firstRun, "gx_comp4o_full_copy", fullExpected, 256U) &&
                 check_byte_array(g_linked, firstRun, "gx_comp4o_full_fill", fullFillExpected, 256U),
                 "copy, fill, compare and scan work correctly at the 256-byte limit")) return false;
    if (!require(check_byte_array(g_linked, firstRun, "gx_comp4o_string_max", fullExpected, 256U),
                 "maximum bounded scan preserves the string bytes")) return false;
    const int32_t maxCopyStatus = find_export(g_linked, "gx_comp4o_copy_full_result");
    if (!require(maxCopyStatus >= 0 &&
                 read_u32(firstRun, g_linked.exports[maxCopyStatus].finalDataOffset) == 0U,
                 "full-length copy returns success at the 256-byte work limit")) return false;
    const int32_t guard = find_export(g_linked, "gx_comp4o_copy_guard");
    if (!require(guard >= 0 && read_u32(firstRun, g_linked.exports[guard].finalDataOffset) == 123456U,
                 "global copy preserves the neighboring initialized integer")) return false;

    return true;
}

static bool save_elf(const char* path)
{
    if (!path) return false;
    ElfLayout layout = {};
    if (!write_and_validate_elf(g_linked, &layout)) return false;
    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const size_t written = std::fwrite(g_elf, 1U, layout.outputBytes, file);
    const bool success = written == layout.outputBytes && std::fclose(file) == 0;
    return success;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: compiler-native-bytes-comp4o-host-test <elf-output>\n");
        return 2;
    }
    if (!test_library()) return 1;
    if (!require(save_elf(argv[1]), "validated generated native ELF is preserved as test output")) return 1;
    std::printf("COMP4O_NATIVE_BYTES_ASSERTIONS=%u ELF=%s ABI=%u FORMAT=%u\n",
        g_assertions, argv[1], COMPILER_OBJECT_ABI_VERSION, COMPILER_OBJECT_FORMAT_VERSION);
    return 0;
}
