//
// Bare-metal compiler bootstrap driver.
//

#include "compiler_driver.h"

#include "compiler_diagnostics.h"
#include "compiler_linker.h"
#include "compiler_module.h"
#include "compiler_object.h"
#include "elf_writer.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

namespace kernel {
namespace compiler {
namespace {

static uint8_t s_source[COMPILER_MAX_SOURCE_BYTES + 1];
static uint8_t s_elf[COMPILER_MAX_OUTPUT_BYTES];
static uint8_t s_reopened[COMPILER_MAX_OUTPUT_BYTES];
static uint8_t s_compare[COMPILER_MAX_OUTPUT_BYTES];
static uint8_t s_object[COMPILER_MAX_OBJECT_BYTES];
static CompiledModule s_modules[COMPILER_MAX_TRANSLATION_UNITS] = {};
static LinkedProgram s_linked = {};

static uint64_t hash_bytes(const uint8_t* bytes, uint32_t count)
{
    // FNV-1a 64 is intentionally small and deterministic.  It is evidence of
    // reproducibility, not a cryptographic integrity mechanism.
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint32_t string_length(const char* value)
{
    uint32_t length = 0;
    if (value) while (value[length]) ++length;
    return length;
}

static void put_decimal_u64(uint64_t value)
{
    char digits[20];
    uint32_t count = 0;
    if (value == 0) {
        serial::putc('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (count != 0) serial::putc(digits[--count]);
}

static void put_hash(uint64_t hash)
{
    serial::put_hex64(hash);
}

static void print_code(const uint8_t* code, uint32_t codeBytes)
{
    serial::puts("Compiler: code_bytes=");
    put_decimal_u64(codeBytes);
    serial::puts(" bytes=");
    for (uint32_t i = 0; i < codeBytes; ++i) serial::put_hex8(code[i]);
    serial::putc('\n');
}

static void print_data(const uint8_t* data, uint32_t dataBytes)
{
    serial::puts("Compiler: data_bytes=");
    put_decimal_u64(dataBytes);
    serial::puts(" data_hash=fnv1a64:");
    put_hash(hash_bytes(data, dataBytes));
    serial::putc('\n');
}

static void append_diagnostics(const Diagnostics& diagnostics, CompileSummary* summary,
                               const char* sourcePath)
{
    if (!summary) return;
    const uint32_t available = COMPILER_MAX_DIAGNOSTICS - summary->diagnosticCount;
    const uint32_t take = diagnostics.count() < available ? diagnostics.count() : available;
    for (uint32_t i = 0; i < take; ++i) {
        const CompilerDiagnostic& source = diagnostics.at(i);
        CompileDiagnostic& destination = summary->diagnostics[summary->diagnosticCount++];
        destination = {};
        destination.location = source.location;
        uint32_t bytes = 0;
        while (bytes + 1 < sizeof(destination.message) && source.message && source.message[bytes]) {
            destination.message[bytes] = source.message[bytes];
            ++bytes;
        }
        destination.message[bytes] = '\0';
        bytes = 0;
        while (bytes + 1 < sizeof(destination.tokenKind) && source.tokenKind && source.tokenKind[bytes]) {
            destination.tokenKind[bytes] = source.tokenKind[bytes];
            ++bytes;
        }
        destination.tokenKind[bytes] = '\0';
        bytes = 0;
        while (bytes + 1 < sizeof(destination.sourcePath) && sourcePath && sourcePath[bytes]) {
            destination.sourcePath[bytes] = sourcePath[bytes];
            ++bytes;
        }
        destination.sourcePath[bytes] = '\0';
    }
    if (diagnostics.overflowed() || diagnostics.count() > take) summary->diagnosticsTruncated = true;
}

static bool same_code(const CompileSummary& left, const CompileSummary& right)
{
    if (left.codeBytes != right.codeBytes) return false;
    for (uint32_t i = 0; i < left.codeBytes; ++i) {
        if (left.code[i] != right.code[i]) return false;
    }
    return true;
}

static bool same_vfs_file_bytes(const char* leftPath, const char* rightPath)
{
    vfs::FileInfo leftInfo = {};
    vfs::FileInfo rightInfo = {};
    if (!leftPath || !rightPath ||
        vfs::stat(leftPath, &leftInfo) != vfs::VFS_OK ||
        vfs::stat(rightPath, &rightInfo) != vfs::VFS_OK ||
        leftInfo.type != vfs::FILE_TYPE_REGULAR ||
        rightInfo.type != vfs::FILE_TYPE_REGULAR ||
        leftInfo.size != rightInfo.size ||
        leftInfo.size > COMPILER_MAX_OUTPUT_BYTES) {
        return false;
    }

    const uint32_t fileBytes = static_cast<uint32_t>(leftInfo.size);
    const int32_t leftRead = vfs::read_file(leftPath, s_compare, fileBytes);
    const int32_t rightRead = vfs::read_file(rightPath, s_reopened, fileBytes);
    if (leftRead < 0 || rightRead < 0 ||
        static_cast<uint32_t>(leftRead) != fileBytes ||
        static_cast<uint32_t>(rightRead) != fileBytes) {
        return false;
    }

    for (uint32_t i = 0; i < fileBytes; ++i) {
        if (s_compare[i] != s_reopened[i]) return false;
    }
    return true;
}

static void print_smoke_result(const char* name, bool pass)
{
    serial::puts("Compiler: ");
    serial::puts(name);
    serial::puts(pass ? "=PASS\n" : "=FAIL\n");
}

static bool copy_text(char* output, uint32_t capacity, const char* input)
{
    if (!output || capacity == 0 || !input) return false;
    uint32_t i = 0;
    while (i + 1 < capacity && input[i]) { output[i] = input[i]; ++i; }
    if (input[i] != '\0') { output[0] = '\0'; return false; }
    output[i] = '\0';
    return true;
}

static bool temporary_object_path(const char* objectPath, char* output, uint32_t capacity)
{
    if (!copy_text(output, capacity, objectPath)) return false;
    uint32_t lastSlash = 0;
    uint32_t lastDot = 0xFFFFFFFFU;
    for (uint32_t i = 0; output[i] != '\0'; ++i) {
        if (output[i] == '/' || output[i] == '\\') lastSlash = i + 1U;
        else if (output[i] == '.') lastDot = i;
    }
    if (lastDot == 0xFFFFFFFFU || lastDot < lastSlash ||
        string_length(output) - lastDot != 4U) return false;
    output[lastDot + 1U] = 'g';
    output[lastDot + 2U] = 'x';
    output[lastDot + 3U] = 't';
    return true;
}

static bool load_cached_object(const char* objectPath, const char* sourceIdentityPath,
                               uint32_t sourceBytes, uint64_t sourceHash,
                               CompiledModule* module)
{
    if (!objectPath || !sourceIdentityPath || !module || objectPath[0] == '\0') return false;
    vfs::FileInfo info = {};
    if (vfs::stat(objectPath, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size < COMPILER_GXO_HEADER_BYTES || info.size > COMPILER_MAX_OBJECT_BYTES) return false;
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    if (vfs::read_file(objectPath, s_object, bytes) != static_cast<int32_t>(bytes)) return false;
    Diagnostics diagnostics;
    CompiledModule loaded = {};
    if (!deserialize_gxo_object(s_object, bytes, &loaded, diagnostics) ||
        !gxo_object_identity_matches(loaded, sourceIdentityPath, sourceBytes, sourceHash)) return false;
    *module = loaded;
    return true;
}

static bool publish_object(const char* objectPath, const CompiledModule& module)
{
    if (!objectPath || objectPath[0] == '\0') return false;
    uint32_t bytes = 0;
    if (!serialize_gxo_object(module, s_object, sizeof(s_object), &bytes)) return false;
    CompiledModule checked = {};
    Diagnostics diagnostics;
    if (!deserialize_gxo_object(s_object, bytes, &checked, diagnostics)) return false;
    char temporary[COMPILER_MAX_SOURCE_PATH_BYTES + 1] = {};
    // The bare-metal FAT volume currently supports 8.3 names.  A suffix such
    // as ".gxo.tmp" would therefore fail before the publication transaction
    // begins; use a sibling three-character extension instead.
    if (!temporary_object_path(objectPath, temporary, sizeof(temporary))) return false;
    if (vfs::exists(temporary)) (void)vfs::unlink(temporary);
    const int32_t temporaryWrite = vfs::write_file(temporary, s_object, bytes);
    if (temporaryWrite != static_cast<int32_t>(bytes)) {
        if (vfs::exists(temporary)) (void)vfs::unlink(temporary);
        return false;
    }
    // FAT rename is non-replacing.  Keep a valid existing object until the
    // new bytes have been fully serialized and verified, then replace it via
    // VFS overwrite.  A failed write cannot be reported as a cache hit.
    if (!vfs::exists(objectPath)) {
        if (vfs::rename(temporary, objectPath) == vfs::VFS_OK) return true;
    } else if (vfs::write_file(objectPath, s_object, bytes) == static_cast<int32_t>(bytes)) {
        (void)vfs::unlink(temporary);
        return true;
    }
    (void)vfs::unlink(temporary);
    return false;
}

} // namespace

static bool fail_project(CompileSummary* summary)
{
    if (summary) summary->success = false;
    serial::puts("Compiler: build FAIL\n");
    return false;
}

static bool compile_project_impl(const char* const* sourcePaths,
                                 const char* const* sourceIdentityPaths,
                                 const char* const* objectPaths,
                                 uint32_t sourceCount,
                                 const char* outputPath,
                                 CompileSummary* summary)
{
    if (summary) *summary = {};
    for (uint32_t i = 0; i < COMPILER_MAX_TRANSLATION_UNITS; ++i) s_modules[i] = {};
    s_linked = {};
    const SourceLocation driverLocation = {0, 1, 1};
    if (!sourcePaths || sourceCount == 0 || sourceCount > COMPILER_MAX_TRANSLATION_UNITS ||
        !outputPath || outputPath[0] == '\0') {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, "project source list and output path are required", "project");
        if (summary) append_diagnostics(diagnostics, summary, "<project>");
        return fail_project(summary);
    }

    serial::puts("Compiler: project_sources=");
    put_decimal_u64(sourceCount);
    serial::puts(" output=");
    serial::puts(outputPath);
    serial::putc('\n');

    if (summary) summary->sourceFileCount = sourceCount;
    bool compileFailed = false;
    uint32_t totalSourceBytes = 0;
    uint32_t totalTokenCount = 0;
    uint64_t projectSourceHash = 0;
    for (uint32_t i = 0; i < sourceCount; ++i) {
        const char* sourcePath = sourcePaths[i];
        const char* sourceIdentityPath = sourceIdentityPaths && sourceIdentityPaths[i]
            ? sourceIdentityPaths[i] : sourcePath;
        Diagnostics diagnostics;
        vfs::FileInfo sourceInfo = {};
        if (!sourcePath || sourcePath[0] == '\0' ||
            vfs::stat(sourcePath, &sourceInfo) != vfs::VFS_OK ||
            sourceInfo.type != vfs::FILE_TYPE_REGULAR) {
            diagnostics.error(driverLocation, "unable to read regular source file", "path");
            if (summary) append_diagnostics(diagnostics, summary, sourcePath ? sourcePath : "<null>");
            compileFailed = true;
            continue;
        }
        if (sourceInfo.size > COMPILER_MAX_SOURCE_BYTES) {
            diagnostics.error(driverLocation, "source exceeds 64 KiB compiler limit", "source");
            if (summary) append_diagnostics(diagnostics, summary, sourcePath);
            compileFailed = true;
            continue;
        }
        const uint32_t sourceBytes = static_cast<uint32_t>(sourceInfo.size);
        const int32_t readBytes = sourceBytes == 0 ? 0 : vfs::read_file(sourcePath, s_source, sourceBytes);
        if (readBytes < 0 || static_cast<uint32_t>(readBytes) != sourceBytes) {
            diagnostics.error(driverLocation, "source read was shorter than filesystem metadata", "filesystem");
            if (summary) append_diagnostics(diagnostics, summary, sourcePath);
            compileFailed = true;
            continue;
        }
        s_source[sourceBytes] = '\0';
        const uint64_t sourceHash = hash_bytes(s_source, sourceBytes);
        if (objectPaths && objectPaths[i] &&
            load_cached_object(objectPaths[i], sourceIdentityPath, sourceBytes, sourceHash, &s_modules[i])) {
            if (summary) {
                summary->moduleStatus[i] = COMPILE_MODULE_CACHE_HIT;
                ++summary->cachedModuleCount;
            }
            serial::puts("Compiler: cache_hit "); serial::puts(sourceIdentityPath); serial::putc('\n');
        } else if (!compile_module_from_source(sourceIdentityPath, reinterpret_cast<const char*>(s_source), sourceBytes,
                                               &s_modules[i], diagnostics)) {
            if (summary) append_diagnostics(diagnostics, summary, sourcePath);
            compileFailed = true;
            continue;
        } else {
            if (summary) {
                summary->moduleStatus[i] = COMPILE_MODULE_COMPILED;
                ++summary->compiledModuleCount;
            }
            if (objectPaths && objectPaths[i] && !publish_object(objectPaths[i], s_modules[i])) {
                diagnostics.error(driverLocation, "compiled object could not be published safely", "object");
                if (summary) append_diagnostics(diagnostics, summary, sourcePath);
                compileFailed = true;
                continue;
            }
            serial::puts("Compiler: compiled "); serial::puts(sourceIdentityPath); serial::putc('\n');
        }
        totalSourceBytes += sourceBytes;
        totalTokenCount += s_modules[i].tokenCount;
        projectSourceHash ^= hash_bytes(reinterpret_cast<const uint8_t*>(sourceIdentityPath),
                                        string_length(sourceIdentityPath)) ^
                             s_modules[i].sourceHash;
        serial::puts("Compiler: module functions=");
        put_decimal_u64(s_modules[i].functionCount);
        serial::puts(" code_bytes=");
        put_decimal_u64(s_modules[i].codeBytes);
        serial::puts(" relocations=");
        put_decimal_u64(s_modules[i].relocationCount);
        serial::putc('\n');
    }
    if (compileFailed) return fail_project(summary);

    Diagnostics linkerDiagnostics;
    serial::puts("Compiler: linking modules=");
    put_decimal_u64(sourceCount);
    serial::putc('\n');
    if (!link_modules(s_modules, sourceCount, &s_linked, linkerDiagnostics)) {
        if (summary) append_diagnostics(linkerDiagnostics, summary, "<link>");
        return fail_project(summary);
    }
    if (summary) {
        summary->linkedModuleCount = sourceCount;
        summary->linkedFromPersistedObjects = summary->cachedModuleCount != 0;
    }

    if (s_linked.dataBytes != 0) print_data(s_linked.data, s_linked.dataBytes);
    if (s_linked.mutableDataBytes != 0) print_data(s_linked.mutableData, s_linked.mutableDataBytes);
    print_code(s_linked.code, s_linked.codeBytes);

    ElfLayout layout = {};
    if (!write_bootstrap_elf(s_linked.code, s_linked.codeBytes, s_linked.data, s_linked.dataBytes,
                             s_linked.mutableData, s_linked.mutableDataBytes,
                             s_linked.entryCodeOffset, s_elf, sizeof(s_elf), &layout) ||
        layout.dataOffset != s_linked.dataFileOffset ||
        layout.mutableDataOffset != s_linked.mutableDataFileOffset) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, "ELF writer could not construct bounded linked image", "elf");
        if (summary) append_diagnostics(diagnostics, summary, "<link>");
        return fail_project(summary);
    }

    ElfValidationResult producedValidation = {};
    if (!validate_bootstrap_elf(s_elf, layout.outputBytes, layout.imageBase,
                                layout.codeOffset, s_linked.code, s_linked.codeBytes,
                                &producedValidation, s_linked.data, s_linked.dataBytes,
                                s_linked.mutableData, s_linked.mutableDataBytes,
                                s_linked.entryCodeOffset)) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, producedValidation.error, "elf");
        if (summary) append_diagnostics(diagnostics, summary, "<link>");
        return fail_project(summary);
    }
    serial::puts("Compiler: produced ELF validation PASS entry=0x");
    serial::put_hex64(layout.entryPoint);
    serial::putc('\n');

    const int32_t written = vfs::write_file(outputPath, s_elf, layout.outputBytes);
    if (written < 0 || static_cast<uint32_t>(written) != layout.outputBytes) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, "filesystem did not write complete ELF image", "filesystem");
        if (summary) append_diagnostics(diagnostics, summary, "<filesystem>");
        return fail_project(summary);
    }
    const uint64_t outputHash = hash_bytes(s_elf, layout.outputBytes);
    serial::puts("Compiler: output_bytes=");
    put_decimal_u64(layout.outputBytes);
    serial::puts(" output_hash=fnv1a64:");
    put_hash(outputHash);
    serial::putc('\n');

    uint8_t closeHandle = vfs::open(outputPath, vfs::OPEN_READ);
    if (closeHandle == 0xFF || vfs::close(closeHandle) != vfs::VFS_OK) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, "generated ELF could not be closed through VFS", "filesystem");
        if (summary) append_diagnostics(diagnostics, summary, "<filesystem>");
        return fail_project(summary);
    }
    uint8_t reopenHandle = vfs::open(outputPath, vfs::OPEN_READ);
    if (reopenHandle == 0xFF) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, "generated ELF could not be reopened through VFS", "filesystem");
        if (summary) append_diagnostics(diagnostics, summary, "<filesystem>");
        return fail_project(summary);
    }
    const int64_t reopenedSize = vfs::file_size(reopenHandle);
    const int32_t reopenedBytes = reopenedSize <= 0 || reopenedSize > COMPILER_MAX_OUTPUT_BYTES
        ? vfs::VFS_ERR_INVALID
        : vfs::read(reopenHandle, s_reopened, static_cast<uint32_t>(reopenedSize));
    const vfs::Status reopenCloseStatus = vfs::close(reopenHandle);
    if (reopenCloseStatus != vfs::VFS_OK || reopenedBytes < 0 ||
        static_cast<uint32_t>(reopenedBytes) != layout.outputBytes) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, "reopened ELF readback was incomplete", "filesystem");
        if (summary) append_diagnostics(diagnostics, summary, "<filesystem>");
        return fail_project(summary);
    }
    ElfValidationResult reopenedValidation = {};
    if (!validate_bootstrap_elf(s_reopened, static_cast<uint32_t>(reopenedBytes),
                                layout.imageBase, layout.codeOffset, s_linked.code, s_linked.codeBytes,
                                &reopenedValidation, s_linked.data, s_linked.dataBytes,
                                s_linked.mutableData, s_linked.mutableDataBytes,
                                s_linked.entryCodeOffset)) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, reopenedValidation.error, "elf");
        if (summary) append_diagnostics(diagnostics, summary, "<filesystem>");
        return fail_project(summary);
    }
    const uint64_t reopenedHash = hash_bytes(s_reopened, static_cast<uint32_t>(reopenedBytes));
    serial::puts("Compiler: reopened_bytes=");
    put_decimal_u64(static_cast<uint32_t>(reopenedBytes));
    serial::puts(" reopened_hash=fnv1a64:");
    put_hash(reopenedHash);
    serial::puts(" readback ELF validation PASS\n");
    if (reopenedHash != outputHash) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, "reopened ELF hash differs from produced ELF hash", "filesystem");
        if (summary) append_diagnostics(diagnostics, summary, "<filesystem>");
        return fail_project(summary);
    }

    if (summary) {
        summary->success = true;
        summary->reopenedAndValidated = true;
        summary->sourceBytes = totalSourceBytes;
        summary->tokenCount = totalTokenCount;
        summary->functionCount = 0;
        summary->recursiveSccCount = s_linked.recursiveSccCount;
        summary->entryCodeOffset = s_linked.entryCodeOffset;
        summary->codeBytes = s_linked.codeBytes;
        summary->dataBytes = s_linked.dataBytes;
        summary->outputBytes = layout.outputBytes;
        summary->sourceHash = sourceCount == 1 ? s_modules[0].sourceHash : projectSourceHash;
        summary->outputHash = outputHash;
        summary->reopenedHash = reopenedHash;
        summary->dataHash = hash_bytes(s_linked.data, s_linked.dataBytes);
        for (uint32_t i = 0; i < sourceCount; ++i) {
            summary->functionCount += s_modules[i].functionCount;
        }
        for (uint32_t i = 0; i < COMPILER_MAX_PROJECT_EXPORTS &&
                           i < COMPILER_MAX_FUNCTIONS; ++i)
            summary->recursiveFunction[i] = s_linked.recursiveFunction[i];
        for (uint32_t i = 0; i < s_linked.codeBytes && i < sizeof(summary->code); ++i)
            summary->code[i] = s_linked.code[i];
        for (uint32_t i = 0; i < s_linked.dataBytes && i < sizeof(summary->data); ++i)
            summary->data[i] = s_linked.data[i];
        for (uint32_t i = 0; i < sourceCount; ++i) {
            for (uint32_t e = 0; e < s_modules[i].exportCount; ++e) {
                if (!s_modules[i].exports[e].isEntry) continue;
                summary->hasHostLog = s_modules[i].hasHostLog;
                summary->returnConstantValid = s_modules[i].returnConstantValid;
                summary->returnConstant = s_modules[i].returnConstant;
            }
        }
    }
    serial::puts("Compiler: ELF validation PASS\n");
    serial::puts("Compiler: build PASS\n");
    return true;
}

bool compile_project(const char* const* sourcePaths,
                     uint32_t sourceCount,
                     const char* outputPath,
                     CompileSummary* summary)
{
    return compile_project_impl(sourcePaths, nullptr, nullptr, sourceCount, outputPath, summary);
}

bool compile_project_incremental(const char* const* sourcePaths,
                                 const char* const* sourceIdentityPaths,
                                 const char* const* objectPaths,
                                 uint32_t sourceCount,
                                 const char* outputPath,
                                 CompileSummary* summary)
{
    return compile_project_impl(sourcePaths, sourceIdentityPaths, objectPaths, sourceCount, outputPath, summary);
}

bool compile(const char* sourcePath, const char* outputPath, CompileSummary* summary)
{
    const char* sources[1] = {sourcePath};
    return compile_project(sources, 1, outputPath, summary);
}

void run_bootstrap_smoke()
{
    serial::puts("Compiler: Phase 27B bare-metal smoke begin\n");
    serial::puts("Compiler: limits source=65536 tokens=2048 diagnostics=16 identifiers=63 functions=16 parameters=4 calls=32 call_args=128 call_edges=128 call_nesting=8 temporaries=64 strings=255 locals=32 statements=256 expressions=1024 blocks=32 block_depth=16 condition_depth=16 loop_depth=8 labels=128 fixups=128 code=24576 data=2048 output=32768\n");

    CompileSummary return42 = {};
    CompileSummary deterministic = {};
    CompileSummary return41 = {};
    CompileSummary invalid = {};

    const bool proof42 = compile("/r42.c", "/r42.elf", &return42);
    const bool proofDeterministicBuild = compile("/r42.c", "/r42b.elf", &deterministic);
    const bool proof41 = compile("/r41.c", "/r41.elf", &return41);

    if (vfs::exists("/bad.elf")) (void)vfs::unlink("/bad.elf");
    const bool invalidRejected = !compile("/bad.c", "/bad.elf", &invalid);
    const bool invalidPublished = vfs::exists("/bad.elf");

    const bool deterministicProof = proof42 && proofDeterministicBuild &&
        return42.sourceHash == deterministic.sourceHash &&
        return42.outputHash == deterministic.outputHash &&
        return42.outputBytes == deterministic.outputBytes &&
        same_code(return42, deterministic) &&
        same_vfs_file_bytes("/r42.elf", "/r42b.elf");
    const bool returnValueProof = proof42 && proof41 &&
        return42.returnConstant == 42 && return41.returnConstant == 41 &&
        return42.sourceHash != return41.sourceHash &&
        return42.outputHash != return41.outputHash &&
        return42.codeBytes == 6 && return41.codeBytes == 6 &&
        return42.code[1] == 0x2A && return41.code[1] == 0x29;
    const bool invalidProof = invalidRejected && !invalidPublished;

    print_smoke_result("proof_return42", proof42);
    print_smoke_result("proof_return41", proof41);
    print_smoke_result("proof_deterministic_rebuild", deterministicProof);
    print_smoke_result("deterministic_elf_bytes", deterministicProof);
    print_smoke_result("proof_invalid_source", invalidProof);
    print_smoke_result("proof_close_reopen", proof42 && return42.reopenedAndValidated &&
                        return42.outputHash == return42.reopenedHash);
    serial::puts("Compiler: generated application execution=NOT_ATTEMPTED\n");
    const bool allPassed = returnValueProof && deterministicProof && invalidProof &&
                           return42.reopenedAndValidated && return41.reopenedAndValidated;
    print_smoke_result("phase27b", allPassed);
    serial::puts(allPassed ? "Compiler: Phase 27B smoke PASS\n" : "Compiler: Phase 27B smoke FAIL\n");
}

} // namespace compiler
} // namespace kernel
