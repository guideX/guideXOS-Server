#include <cstdio>
#include <cstring>

#include "kernel/core/compiler/compiler_diagnostics.h"
#include "kernel/core/compiler/compiler_linker.h"
#include "kernel/core/compiler/compiler_module.h"
#include "kernel/core/compiler/compiler_object.h"
#include "kernel/core/compiler/elf_writer.h"

using namespace kernel::compiler;

static bool require(bool value, const char* message)
{
    if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

static bool compile_text(const char* path, const char* source, CompiledModule* module)
{
    Diagnostics diagnostics;
    if (compile_module_from_source(path, source, static_cast<uint32_t>(std::strlen(source)), module, diagnostics)) return true;
    for (uint32_t i = 0; i < diagnostics.count(); ++i)
        std::fprintf(stderr, "diagnostic: %s\n", diagnostics.at(i).message);
    return false;
}

static bool same_bytes(const uint8_t* left, const uint8_t* right, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) if (left[i] != right[i]) return false;
    return true;
}

static uint16_t elf_u16(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint16_t>(bytes[offset]) |
        static_cast<uint16_t>(bytes[offset + 1]) << 8;
}

static uint32_t elf_u32(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
        static_cast<uint32_t>(bytes[offset + 1]) << 8 |
        static_cast<uint32_t>(bytes[offset + 2]) << 16 |
        static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

static uint64_t elf_u64(const uint8_t* bytes, uint32_t offset)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8U);
    return value;
}

static bool elf_string_equals(const uint8_t* bytes, uint32_t offset, uint32_t size,
                              uint32_t nameOffset, const char* expected)
{
    if (!bytes || !expected || nameOffset >= size) return false;
    uint32_t i = 0;
    while (nameOffset + i < size && bytes[offset + nameOffset + i] != 0 && expected[i] != 0) {
        if (bytes[offset + nameOffset + i] != static_cast<uint8_t>(expected[i])) return false;
        ++i;
    }
    return nameOffset + i < size && bytes[offset + nameOffset + i] == 0 && expected[i] == 0;
}

static bool elf_find_section(const uint8_t* bytes, uint32_t byteCount, const char* expected,
                             uint16_t* outIndex, uint32_t* outOffset, uint32_t* outSize)
{
    if (!bytes || !expected || byteCount < COMPILER_ELF_OBJECT_HEADER_BYTES) return false;
    const uint64_t sectionHeaderOffset = elf_u64(bytes, 40);
    const uint16_t sectionEntryBytes = elf_u16(bytes, 58);
    const uint16_t sectionCount = elf_u16(bytes, 60);
    const uint16_t shstrIndex = elf_u16(bytes, 62);
    if (sectionEntryBytes < 64 || sectionCount == 0 || shstrIndex >= sectionCount ||
        sectionHeaderOffset > byteCount ||
        static_cast<uint64_t>(sectionCount) >
            (static_cast<uint64_t>(byteCount) - sectionHeaderOffset) / sectionEntryBytes) return false;
    const uint32_t shstrHeader = static_cast<uint32_t>(sectionHeaderOffset) +
        static_cast<uint32_t>(shstrIndex) * sectionEntryBytes;
    const uint32_t shstrOffset = static_cast<uint32_t>(elf_u64(bytes, shstrHeader + 24));
    const uint32_t shstrSize = static_cast<uint32_t>(elf_u64(bytes, shstrHeader + 32));
    if (shstrOffset > byteCount || shstrSize > byteCount - shstrOffset) return false;
    for (uint16_t i = 0; i < sectionCount; ++i) {
        const uint32_t header = static_cast<uint32_t>(sectionHeaderOffset) +
            static_cast<uint32_t>(i) * sectionEntryBytes;
        const uint32_t offset = static_cast<uint32_t>(elf_u64(bytes, header + 24));
        const uint32_t size = static_cast<uint32_t>(elf_u64(bytes, header + 32));
        const uint32_t name = elf_u32(bytes, header);
        if (offset > byteCount || size > byteCount - offset) return false;
        if (!elf_string_equals(bytes, shstrOffset, shstrSize, name, expected)) continue;
        if (outIndex) *outIndex = i;
        if (outOffset) *outOffset = offset;
        if (outSize) *outSize = size;
        return true;
    }
    return false;
}

static bool test_elf_sections_symbols_and_relocations()
{
    const char* source =
        "extern int shared_value;\n"
        "int helper();\n"
        "int gx_main(gx_app_context* ctx) { return helper() + shared_value; }\n";
    CompiledModule module = {};
    if (!require(compile_text("src/consumer.cpp", source, &module), "external-data module compiles")) return false;
    uint8_t bytes[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t byteCount = 0;
    if (!require(serialize_elf_object(module, bytes, sizeof(bytes), &byteCount), "external-data ELF serializes")) return false;

    uint16_t textIndex = 0, relaIndex = 0, symtabIndex = 0, strtabIndex = 0;
    uint32_t textOffset = 0, textSize = 0, relaOffset = 0, relaSize = 0;
    if (!require(elf_find_section(bytes, byteCount, ".text", &textIndex, &textOffset, &textSize) &&
                 elf_find_section(bytes, byteCount, ".rela.text", &relaIndex, &relaOffset, &relaSize) &&
                 elf_find_section(bytes, byteCount, ".symtab", &symtabIndex, nullptr, nullptr) &&
                 elf_find_section(bytes, byteCount, ".strtab", &strtabIndex, nullptr, nullptr) &&
                 elf_find_section(bytes, byteCount, ".shstrtab", nullptr, nullptr, nullptr) &&
                 elf_find_section(bytes, byteCount, ".gx.meta", nullptr, nullptr, nullptr),
                 "required ELF sections exist")) return false;
    const uint64_t sectionHeaderOffset = elf_u64(bytes, 40);
    const uint16_t sectionEntryBytes = elf_u16(bytes, 58);
    const uint32_t symtabHeader = static_cast<uint32_t>(sectionHeaderOffset) +
        static_cast<uint32_t>(symtabIndex) * sectionEntryBytes;
    const uint32_t strtabHeader = static_cast<uint32_t>(sectionHeaderOffset) +
        static_cast<uint32_t>(strtabIndex) * sectionEntryBytes;
    const uint32_t symtabOffset = static_cast<uint32_t>(elf_u64(bytes, symtabHeader + 24));
    const uint32_t symtabSize = static_cast<uint32_t>(elf_u64(bytes, symtabHeader + 32));
    const uint32_t strtabOffset = static_cast<uint32_t>(elf_u64(bytes, strtabHeader + 24));
    const uint32_t strtabSize = static_cast<uint32_t>(elf_u64(bytes, strtabHeader + 32));
    if (!require(elf_u32(bytes, symtabHeader + 40) == strtabIndex &&
                 elf_u32(bytes, symtabHeader + 44) == elf_u16(bytes, 60) &&
                 symtabSize % 24U == 0 && strtabSize != 0,
                 "ELF symbol table links and local-symbol boundary are valid")) return false;

    bool foundEntry = false, foundUndefinedFunction = false, foundUndefinedData = false;
    const uint32_t symbolCount = symtabSize / 24U;
    for (uint32_t i = 0; i < symbolCount; ++i) {
        const uint32_t symbol = symtabOffset + i * 24U;
        const uint32_t name = elf_u32(bytes, symbol);
        const uint8_t info = bytes[symbol + 4];
        const uint16_t section = elf_u16(bytes, symbol + 6);
        if (i < elf_u32(bytes, symtabHeader + 44)) continue;
        if (elf_string_equals(bytes, strtabOffset, strtabSize, name, "gx_main"))
            foundEntry = (info >> 4) == 1 && (info & 0x0F) == 2 && section == textIndex;
        if (elf_string_equals(bytes, strtabOffset, strtabSize, name, "helper"))
            foundUndefinedFunction = (info >> 4) == 1 && (info & 0x0F) == 2 && section == 0;
        if (elf_string_equals(bytes, strtabOffset, strtabSize, name, "shared_value"))
            foundUndefinedData = (info >> 4) == 1 && (info & 0x0F) == 1 && section == 0;
    }
    if (!require(foundEntry && foundUndefinedFunction && foundUndefinedData,
                 "defined and undefined ELF symbols retain binding/type/section semantics")) return false;

    const uint32_t relaHeader = static_cast<uint32_t>(sectionHeaderOffset) +
        static_cast<uint32_t>(relaIndex) * sectionEntryBytes;
    if (!require(elf_u32(bytes, relaHeader + 40) == symtabIndex &&
                 elf_u32(bytes, relaHeader + 44) == textIndex &&
                 elf_u64(bytes, relaHeader + 56) == 24 && relaSize % 24U == 0,
                 "ELF relocations link to the symbol table and text section")) return false;
    bool sawPc32 = false, sawAbsolute64 = false;
    for (uint32_t offset = 0; offset < relaSize; offset += 24U) {
        const uint32_t relocation = relaOffset + offset;
        const uint32_t symbol = static_cast<uint32_t>(elf_u64(bytes, relocation + 8) >> 32);
        const uint32_t type = static_cast<uint32_t>(elf_u64(bytes, relocation + 8));
        if (symbol >= symbolCount || elf_u64(bytes, relocation) + (type == 2 ? 4U : 8U) > textSize) return false;
        sawPc32 |= type == 2;
        sawAbsolute64 |= type == 1;
    }
    return require(sawPc32 && sawAbsolute64, "required AMD64 relocation types reference valid symbols");
}

static bool test_round_trip_and_determinism()
{
    const char* source =
        "int answer = 40;\n"
        "extern int answer;\n"
        "int add_two() { answer = answer + 2; return answer; }\n"
        "int gx_main(gx_app_context* ctx) { add_two(); log(ctx, \"persisted\"); return answer; }\n";
    CompiledModule original = {};
    if (!require(compile_text("src/main.cpp", source, &original), "global/import module compiles")) return false;
    if (!require(original.sourceMapCount != 0, "module source mappings are produced")) return false;
    original.dependencyCount = 2;
    std::strcpy(original.dependencies[0].path, "include/common.h");
    original.dependencies[0].bytes = 26;
    original.dependencies[0].hash = 0x1111222233334444ULL;
    std::strcpy(original.dependencies[1].path, "include/point.h");
    original.dependencies[1].bytes = 102;
    original.dependencies[1].hash = 0x5555666677778888ULL;
    uint8_t first[COMPILER_MAX_OBJECT_BYTES] = {};
    uint8_t second[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t firstBytes = 0, secondBytes = 0;
    if (!require(serialize_elf_object(original, first, sizeof(first), &firstBytes), "ELF object serializes")) return false;
    if (!require(serialize_elf_object(original, second, sizeof(second), &secondBytes), "ELF object serializes deterministically")) return false;
    if (!require(firstBytes == secondBytes && same_bytes(first, second, firstBytes), "identical module has identical ELF object bytes")) return false;
    if (!require(elf_find_section(first, firstBytes, ".text", nullptr, nullptr, nullptr) &&
                 elf_find_section(first, firstBytes, ".rodata", nullptr, nullptr, nullptr) &&
                 elf_find_section(first, firstBytes, ".data", nullptr, nullptr, nullptr) &&
                 elf_find_section(first, firstBytes, ".rela.text", nullptr, nullptr, nullptr),
                 "content-bearing ELF sections are emitted when needed")) return false;
    ElfObjectHeaderView header = {};
    Diagnostics inspectDiagnostics;
    if (!require(inspect_elf_object(first, firstBytes, &header, inspectDiagnostics), "ELF object header validates")) return false;
    if (!require(header.elfType == 1 && header.machine == 62 && header.formatVersion == COMPILER_OBJECT_FORMAT_VERSION &&
                 header.targetArchitecture == COMPILER_OBJECT_ARCH_AMD64 &&
                 header.targetAbi == COMPILER_OBJECT_TARGET_ABI_GUIDEXOS_C_V1 &&
                 header.compilerObjectAbiVersion == COMPILER_OBJECT_ABI_VERSION &&
                 header.dependencyCount == 2 && header.dependencyMetadataBytes != 0,
                 "object identity is explicit")) return false;
    CompiledModule restored = {};
    Diagnostics restoreDiagnostics;
    if (!deserialize_elf_object(first, firstBytes, &restored, restoreDiagnostics)) {
        for (uint32_t i = 0; i < restoreDiagnostics.count(); ++i)
            std::fprintf(stderr, "restore diagnostic: %s\n", restoreDiagnostics.at(i).message);
        return require(false, "object deserializes");
    }
    if (!require(std::strcmp(restored.sourcePath, original.sourcePath) == 0 &&
                 restored.sourceBytes == original.sourceBytes && restored.sourceHash == original.sourceHash &&
                 restored.codeBytes == original.codeBytes && restored.dataBytes == original.dataBytes &&
                 restored.mutableDataBytes == original.mutableDataBytes &&
                 same_bytes(restored.code, original.code, original.codeBytes) &&
                 same_bytes(restored.data, original.data, original.dataBytes) &&
                 same_bytes(restored.mutableData, original.mutableData, original.mutableDataBytes) &&
                 restored.exportCount == original.exportCount && restored.importCount == original.importCount &&
                 restored.relocationCount == original.relocationCount &&
                 restored.sourceMapCount == original.sourceMapCount &&
                 restored.sourceMappings[0].line == original.sourceMappings[0].line &&
                 restored.sourceMappings[0].moduleCodeOffset == original.sourceMappings[0].moduleCodeOffset &&
                 restored.dependencyCount == original.dependencyCount &&
                 std::strcmp(restored.dependencies[0].path, original.dependencies[0].path) == 0 &&
                 std::strcmp(restored.dependencies[1].path, original.dependencies[1].path) == 0 &&
                 restored.dependencies[0].bytes == original.dependencies[0].bytes &&
                 restored.dependencies[1].bytes == original.dependencies[1].bytes &&
                 restored.dependencies[0].hash == original.dependencies[0].hash &&
                 restored.dependencies[1].hash == original.dependencies[1].hash,
                 "round-trip module is byte/metadata equivalent")) return false;
    if (!require(elf_object_identity_matches(restored, "src/main.cpp", original.sourceBytes, original.sourceHash) &&
                 !elf_object_identity_matches(restored, "tests/main.cpp", original.sourceBytes, original.sourceHash) &&
                 !elf_object_identity_matches(restored, "src/main.cpp", original.sourceBytes, original.sourceHash ^ 1ULL),
                 "source path and hash are authoritative cache identity")) return false;

    CompiledModule modulesA[1] = {original};
    CompiledModule modulesB[1] = {restored};
    LinkedProgram linkedA = {}, linkedB = {};
    Diagnostics linkA, linkB;
    if (!require(link_modules(modulesA, 1, &linkedA, linkA), "original module links")) return false;
    if (!require(link_modules(modulesB, 1, &linkedB, linkB), "deserialized module links")) return false;
    if (!require(linkedA.sourceMappingCount != 0 && linkedA.sourceFileCount == 1,
                 "linked source mappings retain source identity")) return false;
    uint8_t image[BOOTSTRAP_MAX_ELF_BYTES] = {};
    ElfLayout layout = {};
    if (!require(write_bootstrap_elf(linkedA.code, linkedA.codeBytes, linkedA.data, linkedA.dataBytes,
                                     linkedA.mutableData, linkedA.mutableDataBytes,
                                     linkedA.entryCodeOffset, image, sizeof(image), &layout) &&
                 append_bootstrap_source_map(linkedA, image, sizeof(image), &layout),
                 "final ELF source-map trailer is emitted")) return false;
    ResolvedSourceMapping resolved = {};
    const char* resolveError = nullptr;
    if (!require(resolve_bootstrap_source_mapping(image, layout.outputBytes, layout.imageBase,
                                                  layout.codeOffset, layout.codeBytes,
                                                  "src/main.cpp", 4, 0, &resolved,
                                                  &resolveError) && resolved.targetAddress > layout.entryPoint,
                 "final ELF source mapping resolves to a real mid-function address")) return false;
    ResolvedSourceMapping byAddress = {};
    if (!require(resolve_bootstrap_source_mapping_at_address(
                     image, layout.outputBytes, layout.imageBase, layout.codeOffset,
                     layout.codeBytes, resolved.targetAddress, &byAddress, &resolveError) &&
                 byAddress.line == resolved.line && byAddress.column == resolved.column &&
                 std::strcmp(byAddress.sourcePath, resolved.sourcePath) == 0 &&
                 std::strcmp(byAddress.functionName, resolved.functionName) == 0,
                 "architectural address resolves through the trusted source-map record")) return false;
    if (!require(!resolve_bootstrap_source_mapping_at_address(
                     image, layout.outputBytes, layout.imageBase, layout.codeOffset,
                     layout.codeBytes, layout.imageBase + layout.codeOffset + layout.codeBytes + 1U,
                     nullptr, &resolveError),
                 "an address outside executable code is rejected by source-map lookup")) return false;
    return require(linkedA.codeBytes == linkedB.codeBytes && linkedA.dataBytes == linkedB.dataBytes &&
                   linkedA.mutableDataBytes == linkedB.mutableDataBytes &&
                   same_bytes(linkedA.code, linkedB.code, linkedA.codeBytes) &&
                   same_bytes(linkedA.data, linkedB.data, linkedA.dataBytes) &&
                   same_bytes(linkedA.mutableData, linkedB.mutableData, linkedA.mutableDataBytes),
                   "original and deserialized modules produce identical linked output");
}

static bool test_recursive_metadata_round_trip()
{
    CompiledModule recursive = {};
    const char* recursiveSource =
        "int recurse() { return recurse(); }\n"
        "int gx_main(gx_app_context* ctx) { return recurse(); }\n";
    if (!require(compile_text("src/recursive.cpp", recursiveSource, &recursive), "recursive module compiles")) return false;
    if (!require(recursive.recursiveSccCount != 0 && recursive.recursiveFunction[0],
                 "recursive SCC metadata is produced")) return false;
    uint8_t bytes[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t byteCount = 0;
    if (!require(serialize_elf_object(recursive, bytes, sizeof(bytes), &byteCount), "recursive module serializes")) return false;
    CompiledModule restored = {};
    Diagnostics diagnostics;
    if (!require(deserialize_elf_object(bytes, byteCount, &restored, diagnostics), "recursive module deserializes")) return false;
    if (!require(restored.recursiveSccCount == recursive.recursiveSccCount &&
                 restored.recursiveFunction[0] == recursive.recursiveFunction[0] &&
                 restored.callGraph[0][0] == recursive.callGraph[0][0],
                 "recursive metadata survives round trip")) return false;
    CompiledModule linkedModules[1] = {restored};
    LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    return require(link_modules(linkedModules, 1, &linked, linkDiagnostics),
                   "deserialized recursive module links");
}

static bool test_source_map_relocation_and_multifile()
{
    const char* firstSource =
        "int gx_main(gx_app_context* ctx)\n"
        "{\n"
        "    log(ctx, \"relocate\");\n"
        "    return 42;\n"
        "}\n";
    const char* movedSource =
        "int gx_main(gx_app_context* ctx)\n"
        "{\n"
        "    int padding = 0;\n"
        "    log(ctx, \"relocate\");\n"
        "    return 42;\n"
        "}\n";
    CompiledModule first = {}, moved = {};
    if (!require(compile_text("src/main.cpp", firstSource, &first), "relocation baseline compiles") ||
        !require(compile_text("src/main.cpp", movedSource, &moved), "relocation rebuild compiles")) return false;
    CompiledModule firstModules[1] = {first};
    CompiledModule movedModules[1] = {moved};
    LinkedProgram firstLinked = {}, movedLinked = {};
    Diagnostics firstDiagnostics, movedDiagnostics;
    if (!require(link_modules(firstModules, 1, &firstLinked, firstDiagnostics) &&
                 link_modules(movedModules, 1, &movedLinked, movedDiagnostics),
                 "relocation modules link")) return false;
    static uint8_t firstImage[BOOTSTRAP_MAX_ELF_BYTES] = {};
    static uint8_t movedImage[BOOTSTRAP_MAX_ELF_BYTES] = {};
    ElfLayout firstLayout = {}, movedLayout = {};
    if (!require(write_bootstrap_elf(firstLinked.code, firstLinked.codeBytes,
                                     firstLinked.data, firstLinked.dataBytes,
                                     firstLinked.mutableData, firstLinked.mutableDataBytes,
                                     firstLinked.entryCodeOffset, firstImage, sizeof(firstImage),
                                     &firstLayout) &&
                 append_bootstrap_source_map(firstLinked, firstImage, sizeof(firstImage), &firstLayout) &&
                 write_bootstrap_elf(movedLinked.code, movedLinked.codeBytes,
                                     movedLinked.data, movedLinked.dataBytes,
                                     movedLinked.mutableData, movedLinked.mutableDataBytes,
                                     movedLinked.entryCodeOffset, movedImage, sizeof(movedImage),
                                     &movedLayout) &&
                 append_bootstrap_source_map(movedLinked, movedImage, sizeof(movedImage), &movedLayout),
                 "relocation images emit source maps")) return false;
    ResolvedSourceMapping firstResolved = {}, movedResolved = {};
    const char* error = nullptr;
    const bool relocationResolved = resolve_bootstrap_source_mapping(firstImage, firstLayout.outputBytes,
                                                   firstLayout.imageBase, firstLayout.codeOffset,
                                                   firstLayout.codeBytes, "src/main.cpp", 3, 0,
                                                   &firstResolved, &error) &&
                 resolve_bootstrap_source_mapping(movedImage, movedLayout.outputBytes,
                                                   movedLayout.imageBase, movedLayout.codeOffset,
                                                   movedLayout.codeBytes, "src/main.cpp", 4, 0,
                                                   &movedResolved, &error) &&
                 firstResolved.targetAddress != movedResolved.targetAddress &&
                 firstResolved.finalCodeOffset != movedResolved.finalCodeOffset;
    if (!require(relocationResolved,
                 "rebuild relocates the selected source line")) return false;
    if (!require(!resolve_bootstrap_source_mapping(firstImage, firstLayout.outputBytes,
                                                    firstLayout.imageBase, firstLayout.codeOffset,
                                                    firstLayout.codeBytes, "src/missing.cpp", 3, 0,
                                                    nullptr, &error) &&
                 !resolve_bootstrap_source_mapping(firstImage, firstLayout.outputBytes,
                                                    firstLayout.imageBase, firstLayout.codeOffset,
                                                    firstLayout.codeBytes, "src/main.cpp", 1, 0,
                                                    nullptr, &error),
                 "unknown and non-executable source lines are rejected")) return false;
    static uint8_t corruptImage[BOOTSTRAP_MAX_ELF_BYTES] = {};
    std::memcpy(corruptImage, firstImage, firstLayout.outputBytes);
    corruptImage[firstLayout.outputBytes - 1U] ^= 1U;
    if (!require(!resolve_bootstrap_source_mapping(corruptImage, firstLayout.outputBytes,
                                                    firstLayout.imageBase, firstLayout.codeOffset,
                                                    firstLayout.codeBytes, "src/main.cpp", 3, 0,
                                                    nullptr, &error),
                 "corrupt source-map trailer is rejected")) return false;

    const char* mainSource =
        "int helper();\n"
        "int gx_main(gx_app_context* ctx) { return helper(); }\n";
    const char* helperSource = "int helper() { return 1; }\n";
    CompiledModule mainModule = {}, helperModule = {};
    if (!require(compile_text("src/main.cpp", mainSource, &mainModule) &&
                 compile_text("src/helper.cpp", helperSource, &helperModule),
                 "multi-file source-map modules compile")) return false;
    CompiledModule multiModules[2] = {mainModule, helperModule};
    LinkedProgram multiLinked = {};
    Diagnostics multiDiagnostics;
    if (!require(link_modules(multiModules, 2, &multiLinked, multiDiagnostics) &&
                 multiLinked.sourceFileCount == 2,
                 "multi-file source-map identities remain distinct")) return false;
    static uint8_t multiImage[BOOTSTRAP_MAX_ELF_BYTES] = {};
    ElfLayout multiLayout = {};
    if (!require(write_bootstrap_elf(multiLinked.code, multiLinked.codeBytes,
                                     multiLinked.data, multiLinked.dataBytes,
                                     multiLinked.mutableData, multiLinked.mutableDataBytes,
                                     multiLinked.entryCodeOffset, multiImage, sizeof(multiImage),
                                     &multiLayout) &&
                 append_bootstrap_source_map(multiLinked, multiImage, sizeof(multiImage), &multiLayout) &&
                 resolve_bootstrap_source_mapping(multiImage, multiLayout.outputBytes,
                                                   multiLayout.imageBase, multiLayout.codeOffset,
                                                   multiLayout.codeBytes, "src/helper.cpp", 1, 0,
                                                   nullptr, &error),
                 "multi-file helper source mapping resolves")) return false;
    return true;
}

static bool test_rejection_and_bounds()
{
    CompiledModule module = {};
    if (!require(compile_text("src/reject.cpp", "int gx_main(gx_app_context* ctx) { return 42; }\n", &module), "rejection fixture compiles")) return false;
    uint8_t bytes[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t byteCount = 0;
    if (!require(serialize_elf_object(module, bytes, sizeof(bytes), &byteCount), "rejection fixture serializes")) return false;
    const uint8_t originalMagic = bytes[0];
    bytes[0] = 'X';
    Diagnostics diagnostics;
    CompiledModule restored = {};
    if (!require(!deserialize_elf_object(bytes, byteCount, &restored, diagnostics), "wrong magic is rejected")) return false;
    bytes[0] = originalMagic;
    bytes[100] ^= 1;
    diagnostics = Diagnostics();
    if (!require(!deserialize_elf_object(bytes, byteCount, &restored, diagnostics), "corrupt payload is rejected by checksum")) return false;

    if (!require(serialize_elf_object(module, bytes, sizeof(bytes), &byteCount), "version fixture serializes")) return false;
    ElfObjectHeaderView header = {};
    Diagnostics headerDiagnostics;
    if (!require(inspect_elf_object(bytes, byteCount, &header, headerDiagnostics), "version fixture header inspects")) return false;
    bytes[header.metaOffset + 4] = static_cast<uint8_t>(COMPILER_OBJECT_FORMAT_VERSION - 1);
    diagnostics = Diagnostics();
    if (!require(!deserialize_elf_object(bytes, byteCount, &restored, diagnostics), "old object version is rejected")) return false;
    if (!require(serialize_elf_object(module, bytes, sizeof(bytes), &byteCount), "architecture fixture serializes")) return false;
    inspect_elf_object(bytes, byteCount, &header, headerDiagnostics);
    bytes[header.metaOffset + 8] = 2;
    diagnostics = Diagnostics();
    if (!require(!deserialize_elf_object(bytes, byteCount, &restored, diagnostics), "wrong architecture is rejected")) return false;
    if (!require(serialize_elf_object(module, bytes, sizeof(bytes), &byteCount), "ABI fixture serializes")) return false;
    inspect_elf_object(bytes, byteCount, &header, headerDiagnostics);
    bytes[header.metaOffset + 12] = 2;
    diagnostics = Diagnostics();
    if (!require(!deserialize_elf_object(bytes, byteCount, &restored, diagnostics), "wrong ABI is rejected")) return false;
    if (!require(serialize_elf_object(module, bytes, sizeof(bytes), &byteCount), "section-header fixture serializes")) return false;
    bytes[58] = 0;
    bytes[59] = 0;
    diagnostics = Diagnostics();
    if (!require(!deserialize_elf_object(bytes, byteCount, &restored, diagnostics), "zero-sized section headers are rejected safely")) return false;

    CompiledModule relocation = module;
    relocation.relocationCount = 1;
    relocation.relocations[0] = {};
    relocation.relocations[0].kind = RelocationKind::CallRel32;
    relocation.relocations[0].width = 4;
    relocation.relocations[0].patchOffset = relocation.codeBytes;
    std::strcpy(relocation.relocations[0].targetSymbolName, "gx_main");
    if (!require(!serialize_elf_object(relocation, bytes, sizeof(bytes), &byteCount), "out-of-range relocation is rejected by writer")) return false;
    return true;
}

int main()
{
    if (!test_elf_sections_symbols_and_relocations()) return 1;
    if (!test_round_trip_and_determinism()) return 1;
    if (!test_rejection_and_bounds()) return 1;
    if (!test_recursive_metadata_round_trip()) return 1;
    if (!test_source_map_relocation_and_multifile()) return 1;
    std::puts("compiler_object_host_test: PASS");
    return 0;
}
