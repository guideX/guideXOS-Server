#include <cstdio>
#include <cstring>

#include "kernel/core/compiler/compiler_diagnostics.h"
#include "kernel/core/compiler/compiler_linker.h"
#include "kernel/core/compiler/compiler_module.h"
#include "kernel/core/compiler/compiler_object.h"
#include "kernel/core/compiler/elf_writer.h"

using namespace kernel::compiler;

namespace {

static const uint64_t HOSTED_FIXTURE_IMAGE_BASE = 0x20000000ULL;

static bool writeImage(const char* path, const uint8_t* bytes, uint32_t count) {
    FILE* output = std::fopen(path, "wb");
    if (!output) return false;
    const bool complete = std::fwrite(bytes, 1, count, output) == count;
    const bool closed = std::fclose(output) == 0;
    return complete && closed;
}

static void printDiagnostics(const Diagnostics& diagnostics) {
    for (uint32_t i = 0; i < diagnostics.count(); ++i) {
        const CompilerDiagnostic& diagnostic = diagnostics.at(i);
        std::fprintf(stderr, "compiler diagnostic line=%u column=%u: %s\n",
                     diagnostic.location.line, diagnostic.location.column,
                     diagnostic.message ? diagnostic.message : "unknown");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: generator <source-file> <project-relative-source-identity> <output-elf>\n");
        return 2;
    }

    FILE* input = std::fopen(argv[1], "rb");
    if (!input) {
        std::fprintf(stderr, "cannot open source file: %s\n", argv[1]);
        return 2;
    }
    static char source[COMPILER_MAX_SOURCE_BYTES + 1] = {};
    const size_t sourceSize = std::fread(source, 1, sizeof(source), input);
    const bool readFailed = std::ferror(input) != 0;
    const bool inputClosed = std::fclose(input) == 0;
    if (readFailed || !inputClosed || sourceSize == 0 || sourceSize > COMPILER_MAX_SOURCE_BYTES) {
        std::fprintf(stderr, "fixture source size is outside compiler bounds: %zu\n", sourceSize);
        return 2;
    }

    static CompiledModule compiled = {};
    Diagnostics compileDiagnostics;
    if (!compile_module_from_source(argv[2], source,
                                    static_cast<uint32_t>(sourceSize),
                                    &compiled, compileDiagnostics)) {
        printDiagnostics(compileDiagnostics);
        return 1;
    }

    // The fixture is passed through the same serialized compiler-object and
    // final-link stages used by the in-kernel project compiler. GXSM records
    // are created by code generation; this tool does not construct metadata.
    static uint8_t objectBytes[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t objectSize = 0;
    if (!serialize_elf_object(compiled, objectBytes, sizeof(objectBytes), &objectSize)) {
        std::fprintf(stderr, "production compiler object serialization failed\n");
        return 1;
    }
    static CompiledModule reopened = {};
    Diagnostics objectDiagnostics;
    if (!deserialize_elf_object(objectBytes, objectSize, &reopened, objectDiagnostics)) {
        printDiagnostics(objectDiagnostics);
        return 1;
    }

    static CompiledModule modules[1] = {};
    modules[0] = reopened;
    static LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    if (!link_modules(modules, 1, &linked, linkDiagnostics, HOSTED_FIXTURE_IMAGE_BASE)) {
        printDiagnostics(linkDiagnostics);
        return 1;
    }

    bool foundCounter = false;
    for (uint32_t i = 0; i < linked.debugVariableCount; ++i) {
        const LinkedProgram::LinkedDebugVariable& variable = linked.debugVariables[i];
        if (std::strcmp(variable.name, "counter") != 0) continue;
        foundCounter = true;
        std::printf("GXSM_COMPILER_VARIABLE record_index=%u name=%s type=%u kind=%u location=%u frame_offset=%d live_start=%u live_end=%u function=%s source=%s:%u\n",
                    i, variable.name, static_cast<unsigned>(variable.type),
                    static_cast<unsigned>(variable.kind),
                    static_cast<unsigned>(variable.location), variable.frameOffset,
                    variable.finalLiveStart, variable.finalLiveEnd,
                    linked.sourceMapFunctions[variable.functionIndex].name,
                    linked.sourceFiles[variable.sourceFileIndex].path,
                    variable.declaration.line);
    }
    if (!foundCounter) {
        std::fprintf(stderr, "production compiler emitted no counter debug-variable record\n");
        return 1;
    }
    for (uint32_t i = 0; i < linked.sourceMappingCount; ++i) {
        const LinkedProgram::LinkedSourceMapping& mapping = linked.sourceMappings[i];
        std::printf("GXSM_COMPILER_SOURCE record_index=%u function=%s source=%s:%u pc_offset=%u instruction_bytes=%u\n",
                    i, linked.sourceMapFunctions[mapping.functionIndex].name,
                    linked.sourceFiles[mapping.sourceFileIndex].path,
                    mapping.line, mapping.finalCodeOffset, mapping.instructionBytes);
    }
    std::printf("GXSM_COMPILER_ENTRY_OFFSET entry_code_offset=%u\n", linked.entryCodeOffset);

    static uint8_t image[BOOTSTRAP_MAX_ELF_BYTES] = {};
    ElfLayout layout = {};
    if (!write_bootstrap_elf(linked.code, linked.codeBytes,
                             linked.data, linked.dataBytes,
                             linked.mutableData, linked.mutableDataBytes,
                             linked.entryCodeOffset, HOSTED_FIXTURE_IMAGE_BASE,
                             image, sizeof(image), &layout) ||
        !append_bootstrap_source_map(linked, image, sizeof(image), &layout)) {
        std::fprintf(stderr, "production bootstrap ELF/GXSM writer failed\n");
        return 1;
    }
    ResolvedSourceMapping entryMapping = {};
    const char* entryMappingError = nullptr;
    const bool entryMapped = resolve_bootstrap_source_mapping_at_address(
        image, layout.outputBytes, layout.imageBase, layout.codeOffset,
        linked.codeBytes, layout.entryPoint, &entryMapping, &entryMappingError);
    const bool expectedEntryPrologueGap = !entryMapped && entryMappingError &&
        std::strcmp(entryMappingError, "source-map address is unmapped") == 0;
    std::printf("GXSM_COMPILER_ENTRY entry=0x%llX mapping=%s function=%s source=%s:%u result=%s error=%s\n",
                static_cast<unsigned long long>(layout.entryPoint),
                entryMapped ? "resolved" : "unmapped",
                entryMapping.functionName, entryMapping.sourcePath,
                entryMapping.line, entryMapped ? "PASS" : expectedEntryPrologueGap ? "EXPECTED_UNMAPPED_PROLOGUE" : "FAIL",
                entryMappingError ? entryMappingError : "<none>");
    if (!entryMapped && !expectedEntryPrologueGap) {
        std::fprintf(stderr, "bootstrap ELF entry mapping failed for an unexpected reason\n");
        return 1;
    }
    ElfValidationResult entryValidation = {};
    const bool entryValidated = validate_bootstrap_elf(
        image, layout.outputBytes, layout.imageBase, layout.codeOffset,
        nullptr, 0, &entryValidation, nullptr, 0, nullptr, 0,
        linked.entryCodeOffset);
    std::printf("GXSM_COMPILER_ENTRY_VALIDATION entry_offset=%u result=%s error=%s\n",
                linked.entryCodeOffset, entryValidated ? "PASS" : "FAIL",
                entryValidation.error ? entryValidation.error : "<none>");
    ElfValidationResult elfValidation = {};
    if (!validate_bootstrap_elf(image, layout.outputBytes, layout.imageBase,
                                layout.codeOffset, linked.code, linked.codeBytes,
                                &elfValidation, linked.data, linked.dataBytes,
                                linked.mutableData, linked.mutableDataBytes,
                                linked.entryCodeOffset)) {
        std::fprintf(stderr, "production bootstrap ELF validation failed: %s\n",
                     elfValidation.error ? elfValidation.error : "unknown");
        return 1;
    }

    if (!writeImage(argv[3], image, layout.outputBytes)) {
        std::fprintf(stderr, "cannot write fixture ELF: %s\n", argv[3]);
        return 1;
    }
    std::printf("GXSM_COMPILER_ARTIFACT path=%s bytes=%u source_records=%u variable_records=%u image_base=0x%llX code_offset=%u code_bytes=%u result=PASS\n",
                argv[3], layout.outputBytes, linked.sourceMappingCount,
                linked.debugVariableCount,
                static_cast<unsigned long long>(layout.imageBase),
                layout.codeOffset, layout.codeBytes);
    return 0;
}
