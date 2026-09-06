#include <cstdio>
#include <cstring>

#include "kernel/core/compiler/compiler_diagnostics.h"
#include "kernel/core/compiler/compiler_linker.h"
#include "kernel/core/compiler/compiler_module.h"
#include "kernel/core/compiler/compiler_object.h"

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

static void put_u64(uint8_t* bytes, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>(value >> (i * 8U));
}

static void rechecksum(uint8_t* bytes, uint32_t count)
{
    put_u64(bytes + 92, 0);
    put_u64(bytes + 92, gxo_object_checksum(bytes, count));
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
    uint8_t first[COMPILER_MAX_OBJECT_BYTES] = {};
    uint8_t second[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t firstBytes = 0, secondBytes = 0;
    if (!require(serialize_gxo_object(original, first, sizeof(first), &firstBytes), "object serializes")) return false;
    if (!require(serialize_gxo_object(original, second, sizeof(second), &secondBytes), "object serializes deterministically")) return false;
    if (!require(firstBytes == secondBytes && same_bytes(first, second, firstBytes), "identical module has identical GXO bytes")) return false;
    GxoObjectHeaderView header = {};
    Diagnostics inspectDiagnostics;
    if (!require(inspect_gxo_header(first, firstBytes, &header, inspectDiagnostics), "object header validates")) return false;
    if (!require(header.formatVersion == COMPILER_OBJECT_FORMAT_VERSION &&
                 header.targetArchitecture == COMPILER_OBJECT_ARCH_AMD64 &&
                 header.targetAbi == COMPILER_OBJECT_TARGET_ABI_GUIDEXOS_C_V1 &&
                 header.compilerObjectAbiVersion == COMPILER_OBJECT_ABI_VERSION,
                 "object identity is explicit")) return false;
    CompiledModule restored = {};
    Diagnostics restoreDiagnostics;
    if (!deserialize_gxo_object(first, firstBytes, &restored, restoreDiagnostics)) {
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
                 restored.relocationCount == original.relocationCount,
                 "round-trip module is byte/metadata equivalent")) return false;
    if (!require(gxo_object_identity_matches(restored, "src/main.cpp", original.sourceBytes, original.sourceHash) &&
                 !gxo_object_identity_matches(restored, "tests/main.cpp", original.sourceBytes, original.sourceHash) &&
                 !gxo_object_identity_matches(restored, "src/main.cpp", original.sourceBytes, original.sourceHash ^ 1ULL),
                 "source path and hash are authoritative cache identity")) return false;

    CompiledModule modulesA[1] = {original};
    CompiledModule modulesB[1] = {restored};
    LinkedProgram linkedA = {}, linkedB = {};
    Diagnostics linkA, linkB;
    if (!require(link_modules(modulesA, 1, &linkedA, linkA), "original module links")) return false;
    if (!require(link_modules(modulesB, 1, &linkedB, linkB), "deserialized module links")) return false;
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
    if (!require(serialize_gxo_object(recursive, bytes, sizeof(bytes), &byteCount), "recursive module serializes")) return false;
    CompiledModule restored = {};
    Diagnostics diagnostics;
    if (!require(deserialize_gxo_object(bytes, byteCount, &restored, diagnostics), "recursive module deserializes")) return false;
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

static bool test_rejection_and_bounds()
{
    CompiledModule module = {};
    if (!require(compile_text("src/reject.cpp", "int gx_main(gx_app_context* ctx) { return 42; }\n", &module), "rejection fixture compiles")) return false;
    uint8_t bytes[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t byteCount = 0;
    if (!require(serialize_gxo_object(module, bytes, sizeof(bytes), &byteCount), "rejection fixture serializes")) return false;
    const uint8_t originalMagic = bytes[0];
    bytes[0] = 'X';
    Diagnostics diagnostics;
    CompiledModule restored = {};
    if (!require(!deserialize_gxo_object(bytes, byteCount, &restored, diagnostics), "wrong magic is rejected")) return false;
    bytes[0] = originalMagic;
    bytes[100] ^= 1;
    diagnostics = Diagnostics();
    if (!require(!deserialize_gxo_object(bytes, byteCount, &restored, diagnostics), "corrupt payload is rejected by checksum")) return false;

    if (!require(serialize_gxo_object(module, bytes, sizeof(bytes), &byteCount), "version fixture serializes")) return false;
    bytes[4] = static_cast<uint8_t>(COMPILER_OBJECT_FORMAT_VERSION - 1);
    rechecksum(bytes, byteCount);
    diagnostics = Diagnostics();
    if (!require(!deserialize_gxo_object(bytes, byteCount, &restored, diagnostics), "old object version is rejected")) return false;
    if (!require(serialize_gxo_object(module, bytes, sizeof(bytes), &byteCount), "architecture fixture serializes")) return false;
    bytes[8] = 2;
    rechecksum(bytes, byteCount);
    diagnostics = Diagnostics();
    if (!require(!deserialize_gxo_object(bytes, byteCount, &restored, diagnostics), "wrong architecture is rejected")) return false;
    if (!require(serialize_gxo_object(module, bytes, sizeof(bytes), &byteCount), "ABI fixture serializes")) return false;
    bytes[12] = 2;
    rechecksum(bytes, byteCount);
    diagnostics = Diagnostics();
    if (!require(!deserialize_gxo_object(bytes, byteCount, &restored, diagnostics), "wrong ABI is rejected")) return false;

    CompiledModule relocation = module;
    relocation.relocationCount = 1;
    relocation.relocations[0] = {};
    relocation.relocations[0].kind = RelocationKind::CallRel32;
    relocation.relocations[0].width = 4;
    relocation.relocations[0].patchOffset = relocation.codeBytes;
    std::strcpy(relocation.relocations[0].targetSymbolName, "gx_main");
    if (!require(serialize_gxo_object(relocation, bytes, sizeof(bytes), &byteCount), "malformed relocation serializes for validation test")) return false;
    diagnostics = Diagnostics();
    if (!require(!deserialize_gxo_object(bytes, byteCount, &restored, diagnostics), "out-of-range relocation is rejected")) return false;
    return true;
}

int main()
{
    if (!test_round_trip_and_determinism()) return 1;
    if (!test_rejection_and_bounds()) return 1;
    if (!test_recursive_metadata_round_trip()) return 1;
    std::puts("compiler_object_host_test: PASS");
    return 0;
}
