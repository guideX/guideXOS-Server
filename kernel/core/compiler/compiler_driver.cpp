//
// Bare-metal compiler bootstrap driver.
//

#include "compiler_driver.h"

#include "compiler_diagnostics.h"
#include "compiler_lexer.h"
#include "compiler_parser.h"
#include "elf_writer.h"
#if defined(__x86_64__)
#include "arch/amd64/compiler_backend.h"
#endif
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

namespace kernel {
namespace compiler {
namespace {

static uint8_t s_source[COMPILER_MAX_SOURCE_BYTES + 1];
static Token s_tokens[COMPILER_MAX_TOKENS];
static uint8_t s_code[COMPILER_MAX_CODE_BYTES];
static uint8_t s_data[COMPILER_MAX_DATA_BYTES];
static uint8_t s_elf[COMPILER_MAX_OUTPUT_BYTES];
static uint8_t s_reopened[COMPILER_MAX_OUTPUT_BYTES];
static uint8_t s_compare[COMPILER_MAX_OUTPUT_BYTES];

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

static void put_decimal_i32(int32_t value)
{
    if (value < 0) {
        serial::putc('-');
        const uint32_t magnitude = static_cast<uint32_t>(-(value + 1)) + 1u;
        put_decimal_u64(magnitude);
    } else {
        put_decimal_u64(static_cast<uint32_t>(value));
    }
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

static bool flatten_string_table(TranslationUnitIR& unit, uint8_t* output,
                                 uint32_t capacity, uint32_t* outputBytes)
{
    if (!output || !outputBytes || unit.functionCount > COMPILER_MAX_FUNCTIONS) return false;
    uint32_t offset = 0;
    for (uint32_t f = 0; f < unit.functionCount; ++f) {
        FunctionIR& function = unit.functions[f];
        const uint32_t functionStart = offset;
        function.dataOffset = functionStart;
        for (uint32_t i = 0; i < function.stringCount; ++i) {
            if (function.stringOffsets[i] != offset - functionStart ||
                offset + function.strings[i].bytes + 1U > capacity) return false;
            for (uint32_t j = 0; j < function.strings[i].bytes; ++j)
                output[offset + j] = static_cast<uint8_t>(function.strings[i].data[j]);
            output[offset + function.strings[i].bytes] = 0;
            offset += function.strings[i].bytes + 1U;
        }
    }
    *outputBytes = offset;
    return true;
}

static void copy_diagnostics(const Diagnostics& diagnostics, CompileSummary* summary)
{
    if (!summary) return;
    summary->diagnosticCount = diagnostics.count() < COMPILER_MAX_DIAGNOSTICS
        ? diagnostics.count() : COMPILER_MAX_DIAGNOSTICS;
    summary->diagnosticsTruncated = diagnostics.overflowed();
    for (uint32_t i = 0; i < summary->diagnosticCount; ++i) {
        const CompilerDiagnostic& source = diagnostics.at(i);
        CompileDiagnostic& destination = summary->diagnostics[i];
        destination.location = source.location;
        uint32_t messageBytes = 0;
        if (source.message) {
            while (messageBytes + 1 < sizeof(destination.message) && source.message[messageBytes] != '\0') {
                destination.message[messageBytes] = source.message[messageBytes];
                ++messageBytes;
            }
        }
        destination.message[messageBytes] = '\0';
        uint32_t tokenBytes = 0;
        if (source.tokenKind) {
            while (tokenBytes + 1 < sizeof(destination.tokenKind) && source.tokenKind[tokenBytes] != '\0') {
                destination.tokenKind[tokenBytes] = source.tokenKind[tokenBytes];
                ++tokenBytes;
            }
        }
        destination.tokenKind[tokenBytes] = '\0';
    }
}

static bool fail_build(Diagnostics& diagnostics, CompileSummary* summary)
{
    print_diagnostics(diagnostics);
    serial::puts("Compiler: build FAIL\n");
    if (summary) {
        summary->success = false;
        copy_diagnostics(diagnostics, summary);
    }
    return false;
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

} // namespace

bool compile(const char* sourcePath,
             const char* outputPath,
             CompileSummary* summary)
{
    CompileSummary local = {};
    if (summary) *summary = local;

    Diagnostics diagnostics;
    const SourceLocation driverLocation = {0, 1, 1};
    if (!sourcePath || !outputPath || sourcePath[0] == '\0' || outputPath[0] == '\0') {
        diagnostics.error(driverLocation, "source and output paths are required", "path");
        return fail_build(diagnostics, summary);
    }

    serial::puts("Compiler: source=");
    serial::puts(sourcePath);
    serial::puts(" output=");
    serial::puts(outputPath);
    serial::putc('\n');

    vfs::FileInfo sourceInfo = {};
    if (vfs::stat(sourcePath, &sourceInfo) != vfs::VFS_OK ||
        sourceInfo.type != vfs::FILE_TYPE_REGULAR) {
        diagnostics.error(driverLocation, "unable to read regular source file", "path");
        return fail_build(diagnostics, summary);
    }
    if (sourceInfo.size > COMPILER_MAX_SOURCE_BYTES) {
        diagnostics.error(driverLocation, "source exceeds 64 KiB compiler limit", "source");
        return fail_build(diagnostics, summary);
    }

    const uint32_t sourceBytes = static_cast<uint32_t>(sourceInfo.size);
    const int32_t readBytes = sourceBytes == 0 ? 0 : vfs::read_file(sourcePath, s_source, sourceBytes);
    if (readBytes < 0 || static_cast<uint32_t>(readBytes) != sourceBytes) {
        diagnostics.error(driverLocation, "source read was shorter than filesystem metadata", "filesystem");
        return fail_build(diagnostics, summary);
    }
    s_source[sourceBytes] = '\0';
    const uint64_t sourceHash = hash_bytes(s_source, sourceBytes);

    serial::puts("Compiler: source_bytes=");
    put_decimal_u64(sourceBytes);
    serial::puts(" source_hash=fnv1a64:");
    put_hash(sourceHash);
    serial::putc('\n');

    uint32_t tokenCount = 0;
    if (!lex_source(reinterpret_cast<const char*>(s_source), sourceBytes,
                    s_tokens, COMPILER_MAX_TOKENS, &tokenCount, diagnostics)) {
        return fail_build(diagnostics, summary);
    }
    serial::puts("Compiler: tokens=");
    put_decimal_u64(tokenCount);
    serial::putc('\n');

    static TranslationUnitIR unit = {};
    unit = {};
    if (!parse_translation_unit(reinterpret_cast<const char*>(s_source), s_tokens, tokenCount,
                                &unit, diagnostics)) {
        return fail_build(diagnostics, summary);
    }
    FunctionIR& function = unit.functions[unit.entryFunction];

    if (summary) {
        summary->recursiveSccCount = unit.recursiveSccCount;
        for (uint32_t i = 0; i < COMPILER_MAX_FUNCTIONS; ++i)
            summary->recursiveFunction[i] = unit.recursiveFunction[i];
    }

    serial::puts("Compiler: functions=");
    put_decimal_u64(unit.functionCount);
    serial::putc('\n');
#if defined(__x86_64__)
    serial::puts("Compiler: stack_policy frame_bytes=");
    put_decimal_u64(COMPILER_MAX_GENERATED_FRAME_BYTES);
    serial::puts(" transient_bytes=");
    put_decimal_u64(COMPILER_MAX_TRANSIENT_STACK_BYTES);
    serial::puts(" activation_bytes=");
    put_decimal_u64(COMPILER_MAX_GENERATED_ACTIVATION_STACK_COST);
    serial::puts(" max_depth=");
    put_decimal_u64(COMPILER_MAX_RUNTIME_CALL_DEPTH);
    serial::puts(" reserve_bytes=");
    put_decimal_u64(::kernel::native_elf::NATIVE_ELF_RUNTIME_SAFETY_RESERVE_BYTES);
    serial::putc('\n');
#endif
    serial::puts("Compiler: return_constant=");
    if (function.returnConstantValid) put_decimal_i32(function.returnConstant);
    else serial::puts("nonconstant");
    serial::putc('\n');

    uint32_t dataBytes = 0;
    if (!flatten_string_table(unit, s_data, sizeof(s_data), &dataBytes)) {
        diagnostics.error(driverLocation, "source string data exceeds compiler limit", "data");
        return fail_build(diagnostics, summary);
    }
    if (dataBytes != 0) print_data(s_data, dataBytes);

    uint32_t codeBytes = 0;
    uint32_t entryCodeOffset = 0;
#if defined(__x86_64__)
    const uint64_t dataAddress = dataBytes == 0 ? 0 : BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_DATA_OFFSET;
    if (!amd64::emit_translation_unit(unit, dataAddress, s_code, sizeof(s_code), &codeBytes,
                                      &entryCodeOffset)) {
        diagnostics.error(driverLocation, "AMD64 backend rejected target-neutral IR", "backend");
        return fail_build(diagnostics, summary);
    }
#else
    diagnostics.error(driverLocation, "bootstrap compiler backend is only available on AMD64", "backend");
    return fail_build(diagnostics, summary);
#endif
    print_code(s_code, codeBytes);

    ElfLayout layout = {};
    if (!write_bootstrap_elf(s_code, codeBytes, s_data, dataBytes, entryCodeOffset,
                             s_elf, sizeof(s_elf), &layout)) {
        diagnostics.error(driverLocation, "ELF writer could not construct bounded image", "elf");
        return fail_build(diagnostics, summary);
    }

    ElfValidationResult producedValidation = {};
    if (!validate_bootstrap_elf(s_elf, layout.outputBytes, layout.imageBase,
                                layout.codeOffset, s_code, codeBytes,
                                 &producedValidation, s_data, dataBytes, entryCodeOffset)) {
        diagnostics.error(driverLocation, producedValidation.error, "elf");
        return fail_build(diagnostics, summary);
    }
    serial::puts("Compiler: produced ELF validation PASS entry=0x");
    serial::put_hex64(layout.entryPoint);
    serial::putc('\n');

    const int32_t written = vfs::write_file(outputPath, s_elf, layout.outputBytes);
    if (written < 0 || static_cast<uint32_t>(written) != layout.outputBytes) {
        diagnostics.error(driverLocation, "filesystem did not write complete ELF image", "filesystem");
        return fail_build(diagnostics, summary);
    }
    serial::puts("Compiler: output_bytes=");
    put_decimal_u64(layout.outputBytes);
    serial::puts(" output_hash=fnv1a64:");
    const uint64_t outputHash = hash_bytes(s_elf, layout.outputBytes);
    put_hash(outputHash);
    serial::putc('\n');

    // write_file is the existing FAT path-level create/overwrite helper.  The
    // explicit handle close below establishes that a VFS file can be closed,
    // reopened, read back, and validated after that write operation.
    uint8_t closeHandle = vfs::open(outputPath, vfs::OPEN_READ);
    if (closeHandle == 0xFF || vfs::close(closeHandle) != vfs::VFS_OK) {
        diagnostics.error(driverLocation, "generated ELF could not be closed through VFS", "filesystem");
        return fail_build(diagnostics, summary);
    }

    uint8_t reopenHandle = vfs::open(outputPath, vfs::OPEN_READ);
    if (reopenHandle == 0xFF) {
        diagnostics.error(driverLocation, "generated ELF could not be reopened through VFS", "filesystem");
        return fail_build(diagnostics, summary);
    }
    const int64_t reopenedSize = vfs::file_size(reopenHandle);
    const int32_t reopenedBytes = reopenedSize <= 0 || reopenedSize > COMPILER_MAX_OUTPUT_BYTES
        ? vfs::VFS_ERR_INVALID
        : vfs::read(reopenHandle, s_reopened, static_cast<uint32_t>(reopenedSize));
    const vfs::Status reopenCloseStatus = vfs::close(reopenHandle);
    if (reopenCloseStatus != vfs::VFS_OK || reopenedBytes < 0 ||
        static_cast<uint32_t>(reopenedBytes) != layout.outputBytes) {
        diagnostics.error(driverLocation, "reopened ELF readback was incomplete", "filesystem");
        return fail_build(diagnostics, summary);
    }

    ElfValidationResult reopenedValidation = {};
    if (!validate_bootstrap_elf(s_reopened, static_cast<uint32_t>(reopenedBytes),
                                layout.imageBase, layout.codeOffset, s_code, codeBytes,
                                 &reopenedValidation, s_data, dataBytes, entryCodeOffset)) {
        diagnostics.error(driverLocation, reopenedValidation.error, "elf");
        return fail_build(diagnostics, summary);
    }
    const uint64_t reopenedHash = hash_bytes(s_reopened, static_cast<uint32_t>(reopenedBytes));
    serial::puts("Compiler: reopened_bytes=");
    put_decimal_u64(static_cast<uint32_t>(reopenedBytes));
    serial::puts(" reopened_hash=fnv1a64:");
    put_hash(reopenedHash);
    serial::puts(" readback ELF validation PASS\n");
    if (reopenedHash != outputHash) {
        diagnostics.error(driverLocation, "reopened ELF hash differs from produced ELF hash", "filesystem");
        return fail_build(diagnostics, summary);
    }

    if (summary) {
        summary->success = true;
        summary->reopenedAndValidated = true;
        summary->sourceBytes = sourceBytes;
        summary->tokenCount = tokenCount;
        summary->functionCount = unit.functionCount;
        summary->entryCodeOffset = entryCodeOffset;
        summary->returnConstantValid = function.returnConstantValid;
        summary->returnConstant = function.returnConstant;
        summary->codeBytes = codeBytes;
        summary->hasHostLog = function.hasHostLog;
        for (uint32_t i = 0; i < codeBytes && i < sizeof(summary->code); ++i) summary->code[i] = s_code[i];
        summary->outputBytes = layout.outputBytes;
        summary->dataBytes = dataBytes;
        for (uint32_t i = 0; i < dataBytes && i < sizeof(summary->data); ++i) summary->data[i] = s_data[i];
        summary->sourceHash = sourceHash;
        summary->outputHash = outputHash;
        summary->reopenedHash = reopenedHash;
        summary->dataHash = hash_bytes(s_data, dataBytes);
    }

    serial::puts("Compiler: ELF validation PASS\n");
    serial::puts("Compiler: build PASS\n");
    return true;
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
