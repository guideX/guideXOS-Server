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
static uint8_t s_headerSource[COMPILER_MAX_INCLUDE_DEPTH][COMPILER_MAX_DECLARATION_FILE_BYTES + 1];
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

static bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    uint32_t i = 0;
    while (left[i] || right[i]) {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return true;
}

static bool valid_relative_path(const char* path)
{
    if (!path || path[0] == '\0' || path[0] == '/' || path[0] == '\\') return false;
    uint32_t length = 0;
    while (path[length]) {
        if (++length >= COMPILER_MAX_SOURCE_PATH_BYTES) return false;
    }
    uint32_t componentStart = 0;
    for (uint32_t i = 0; i <= length; ++i) {
        if (path[i] != '/' && path[i] != '\\' && path[i] != '\0') continue;
        const uint32_t componentBytes = i - componentStart;
        if (componentBytes == 0 || (componentBytes == 1 && path[componentStart] == '.') ||
            (componentBytes == 2 && path[componentStart] == '.' && path[componentStart + 1] == '.')) return false;
        componentStart = i + 1;
    }
    return true;
}

static bool normalize_relative_path(const char* input, char* output, uint32_t capacity)
{
    if (!input || !output || capacity == 0) return false;
    uint32_t written = 0;
    uint32_t componentStart = 0;
    const uint32_t inputBytes = string_length(input);
    if (!valid_relative_path(input)) return false;
    for (uint32_t i = 0; i <= inputBytes; ++i) {
        if (input[i] != '/' && input[i] != '\\' && input[i] != '\0') continue;
        const uint32_t componentBytes = i - componentStart;
        if (written != 0 && written + 1 >= capacity) return false;
        if (written != 0) output[written++] = '/';
        for (uint32_t j = 0; j < componentBytes; ++j) {
            if (written + 1 >= capacity) return false;
            output[written++] = input[componentStart + j];
        }
        componentStart = i + 1;
    }
    if (written == 0) return false;
    output[written] = '\0';
    return true;
}

static bool append_text_bounded(char* output, uint32_t capacity, uint32_t* used,
                                const char* text, uint32_t bytes)
{
    if (!output || !used || !text || *used > capacity || bytes > capacity - *used) return false;
    for (uint32_t i = 0; i < bytes; ++i) output[*used + i] = text[i];
    *used += bytes;
    return true;
}

static bool path_join(const char* root, const char* relative, char* output, uint32_t capacity)
{
    if (!root || !relative || !output || !valid_relative_path(relative)) return false;
    const uint32_t rootBytes = string_length(root);
    const bool slash = rootBytes != 0 && root[rootBytes - 1] != '/';
    if (rootBytes + (slash ? 1U : 0U) + string_length(relative) + 1U > capacity) return false;
    uint32_t at = 0;
    for (uint32_t i = 0; i < rootBytes; ++i) output[at++] = root[i];
    if (slash) output[at++] = '/';
    for (uint32_t i = 0; relative[i]; ++i) output[at++] = relative[i] == '\\' ? '/' : relative[i];
    output[at] = '\0';
    return true;
}

static bool project_root_for_source(const char* sourcePath, const char* sourceIdentityPath,
                                    char* root, uint32_t capacity)
{
    if (!sourcePath || !sourceIdentityPath || !root || capacity == 0) return false;
    const uint32_t sourceBytes = string_length(sourcePath);
    const uint32_t identityBytes = string_length(sourceIdentityPath);
    if (identityBytes == 0) return false;
    if (sourceBytes <= identityBytes || sourcePath[sourceBytes - identityBytes - 1] != '/') {
        // The native smoke fixtures call the compiler directly with the
        // absolute source path as both identity values.  Keep that legacy
        // form usable; the Developer Studio service supplies a project-
        // relative identity and takes the stricter branch above.
        uint32_t slash = 0xFFFFFFFFU;
        for (uint32_t i = 0; i < sourceBytes; ++i)
            if (sourcePath[i] == '/' || sourcePath[i] == '\\') slash = i;
        if (slash == 0xFFFFFFFFU || slash + 1U > capacity) return false;
        for (uint32_t i = 0; i < slash; ++i) root[i] = sourcePath[i];
        root[slash] = '\0';
        return true;
    }
    for (uint32_t i = 0; i < identityBytes; ++i) {
        const char actual = sourcePath[sourceBytes - identityBytes + i] == '\\' ? '/' : sourcePath[sourceBytes - identityBytes + i];
        if (actual != sourceIdentityPath[i]) return false;
    }
    const uint32_t rootBytes = sourceBytes - identityBytes - 1U;
    if (rootBytes + 1U > capacity) return false;
    for (uint32_t i = 0; i < rootBytes; ++i) root[i] = sourcePath[i];
    root[rootBytes] = '\0';
    return true;
}

struct IncludeExpansion {
    char projectRoot[COMPILER_MAX_SOURCE_PATH_BYTES];
    char output[COMPILER_MAX_DECLARATION_BYTES + 1];
    uint32_t outputBytes;
    uint32_t includedBytes;
    uint16_t dependencyCount;
    DeclarationDependency dependencies[COMPILER_MAX_DECLARATION_DEPENDENCIES];
    char active[COMPILER_MAX_INCLUDE_DEPTH][COMPILER_MAX_SOURCE_PATH_BYTES];
    uint16_t activeCount;
};

static IncludeExpansion s_includeExpansion = {};

static int32_t dependency_index(const IncludeExpansion& expansion, const char* path)
{
    for (uint32_t i = 0; i < expansion.dependencyCount; ++i)
        if (text_equal(expansion.dependencies[i].path, path)) return static_cast<int32_t>(i);
    return -1;
}

static int32_t active_index(const IncludeExpansion& expansion, const char* path)
{
    for (uint32_t i = 0; i < expansion.activeCount; ++i)
        if (text_equal(expansion.active[i], path)) return static_cast<int32_t>(i);
    return -1;
}

static bool header_candidate(const char* includingRelative, const char* requested, uint32_t variant, char* relative,
                             uint32_t capacity)
{
    char directory[COMPILER_MAX_SOURCE_PATH_BYTES] = {};
    const uint32_t includingBytes = string_length(includingRelative);
    uint32_t slash = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < includingBytes; ++i)
        if (includingRelative[i] == '/') slash = i;
    if (variant == 0 && slash != 0xFFFFFFFFU) {
        if (slash + 1U >= sizeof(directory)) return false;
        for (uint32_t i = 0; i < slash; ++i) directory[i] = includingRelative[i];
        directory[slash] = '\0';
        char joined[COMPILER_MAX_SOURCE_PATH_BYTES] = {};
        if (!copy_text(joined, sizeof(joined), directory)) return false;
        if (string_length(joined) + 1U + string_length(requested) + 1U > sizeof(joined)) return false;
        const uint32_t at = string_length(joined);
        joined[at] = '/'; joined[at + 1] = '\0';
        if (!copy_text(joined + at + 1U, sizeof(joined) - at - 1U, requested)) return false;
        return normalize_relative_path(joined, relative, capacity);
    }
    if (variant == 1) return normalize_relative_path(requested, relative, capacity);
    char underInclude[COMPILER_MAX_SOURCE_PATH_BYTES] = {};
    if (string_length(requested) + 9U >= sizeof(underInclude)) return false;
    copy_text(underInclude, sizeof(underInclude), "include/");
    const uint32_t at = string_length(underInclude);
    if (!copy_text(underInclude + at, sizeof(underInclude) - at, requested)) return false;
    return normalize_relative_path(underInclude, relative, capacity);
}

static bool read_header(const IncludeExpansion& expansion, const char* relative,
                        uint8_t* buffer, uint32_t* bytes)
{
    char absolute[COMPILER_MAX_SOURCE_PATH_BYTES] = {};
    if (!path_join(expansion.projectRoot, relative, absolute, sizeof(absolute))) return false;
    vfs::FileInfo info = {};
    if (vfs::stat(absolute, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size > COMPILER_MAX_DECLARATION_FILE_BYTES) return false;
    const uint32_t count = static_cast<uint32_t>(info.size);
    if (vfs::read_file(absolute, buffer, count) != static_cast<int32_t>(count)) return false;
    if (bytes) *bytes = count;
    return true;
}

static bool parse_include_line(const char* line, uint32_t bytes, char* requested,
                               uint32_t capacity, bool* isDirective, Diagnostics& diagnostics)
{
    if (!line || !requested || !isDirective) return false;
    *isDirective = false;
    uint32_t at = 0;
    while (at < bytes && (line[at] == ' ' || line[at] == '\t')) ++at;
    if (at == bytes || line[at] != '#') return true;
    *isDirective = true;
    ++at;
    if (at + 7U > bytes || line[at] != 'i' || line[at + 1] != 'n' || line[at + 2] != 'c' ||
        line[at + 3] != 'l' || line[at + 4] != 'u' || line[at + 5] != 'd' || line[at + 6] != 'e') {
        diagnostics.error({0, 1, 1}, "unsupported preprocessor directive; only #include is supported", "include");
        return false;
    }
    at += 7;
    while (at < bytes && (line[at] == ' ' || line[at] == '\t')) ++at;
    if (at >= bytes || line[at] != '"') {
        diagnostics.error({0, 1, 1}, "quoted local header path is required", "include");
        return false;
    }
    ++at;
    const uint32_t start = at;
    while (at < bytes && line[at] != '"') ++at;
    if (at == bytes || at == start || at - start + 1U > capacity) {
        diagnostics.error({0, 1, 1}, "invalid local header path", "include");
        return false;
    }
    for (uint32_t i = start; i < at; ++i) requested[i - start] = line[i];
    requested[at - start] = '\0';
    ++at;
    while (at < bytes && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) ++at;
    if (at != bytes) {
        diagnostics.error({0, 1, 1}, "trailing text after #include is not supported", "include");
        return false;
    }
    if (!valid_relative_path(requested)) {
        diagnostics.error({0, 1, 1}, "invalid or escaping local header path", "include");
        return false;
    }
    return true;
}

static bool expand_include_file(IncludeExpansion& expansion, const char* relative,
                                const uint8_t* bytes, uint32_t byteCount, uint16_t depth,
                                Diagnostics& diagnostics)
{
    if (depth > COMPILER_MAX_INCLUDE_DEPTH) {
        diagnostics.error({0, 1, 1}, "include depth exceeded", "include");
        return false;
    }
    if (depth != 0) {
        if (active_index(expansion, relative) >= 0) {
            diagnostics.error({0, 1, 1}, "include cycle detected", "include");
            return false;
        }
        if (expansion.activeCount >= COMPILER_MAX_INCLUDE_DEPTH) {
            diagnostics.error({0, 1, 1}, "include depth exceeded", "include");
            return false;
        }
        copy_text(expansion.active[expansion.activeCount++], sizeof(expansion.active[0]), relative);
    }
    uint32_t lineStart = 0;
    while (lineStart < byteCount) {
        uint32_t lineEnd = lineStart;
        while (lineEnd < byteCount && bytes[lineEnd] != '\n' && bytes[lineEnd] != '\r') ++lineEnd;
        uint32_t lineBytes = lineEnd - lineStart;
        char requested[COMPILER_MAX_SOURCE_PATH_BYTES] = {};
        bool directive = false;
        if (!parse_include_line(reinterpret_cast<const char*>(bytes + lineStart), lineBytes,
                                requested, sizeof(requested), &directive, diagnostics)) return false;
        if (!directive) {
            if (!append_text_bounded(expansion.output, sizeof(expansion.output) - 1U,
                                     &expansion.outputBytes, reinterpret_cast<const char*>(bytes + lineStart), lineBytes)) return false;
            if (lineEnd < byteCount) {
                const uint32_t newlineBytes = bytes[lineEnd] == '\r' && lineEnd + 1U < byteCount && bytes[lineEnd + 1U] == '\n' ? 2U : 1U;
                if (!append_text_bounded(expansion.output, sizeof(expansion.output) - 1U,
                                         &expansion.outputBytes, "\n", 1)) return false;
                lineEnd += newlineBytes;
            }
        } else {
            char resolved[COMPILER_MAX_SOURCE_PATH_BYTES] = {};
            bool found = false;
            for (uint32_t variant = 0; variant < 3 && !found; ++variant) {
                if (!header_candidate(relative, requested, variant, resolved, sizeof(resolved))) continue;
                if (depth >= COMPILER_MAX_INCLUDE_DEPTH) {
                    diagnostics.error({0, 1, 1}, "include depth exceeded", "include");
                    return false;
                }
                uint8_t* scratch = s_headerSource[depth];
                for (uint32_t clear = 0; clear < COMPILER_MAX_DECLARATION_FILE_BYTES + 1U; ++clear) scratch[clear] = 0;
                uint32_t headerBytes = 0;
                if (read_header(expansion, resolved, scratch, &headerBytes)) {
                    found = true;
                    if (active_index(expansion, resolved) >= 0) {
                        diagnostics.error({0, 1, 1}, "include cycle detected", "include");
                        return false;
                    }
                    const int32_t existing = dependency_index(expansion, resolved);
                    if (existing < 0) {
                        if (expansion.dependencyCount >= COMPILER_MAX_DECLARATION_DEPENDENCIES ||
                            expansion.includedBytes > COMPILER_MAX_DECLARATION_BYTES - headerBytes) {
                            diagnostics.error({0, 1, 1}, "declaration dependency capacity exceeded", "include");
                            return false;
                        }
                        DeclarationDependency& dependency = expansion.dependencies[expansion.dependencyCount++];
                        dependency = {};
                        copy_text(dependency.path, sizeof(dependency.path), resolved);
                        dependency.bytes = headerBytes;
                        dependency.hash = hash_bytes(scratch, headerBytes);
                        expansion.includedBytes += headerBytes;
                        if (!append_text_bounded(expansion.output, sizeof(expansion.output) - 1U,
                                                 &expansion.outputBytes, "\n", 1) ||
                            !expand_include_file(expansion, resolved, scratch, headerBytes,
                                                 static_cast<uint16_t>(depth + 1U), diagnostics)) return false;
                    }
                    break;
                }
            }
            if (!found) {
                diagnostics.error({0, 1, 1}, "header not found", "include");
                return false;
            }
            if (!append_text_bounded(expansion.output, sizeof(expansion.output) - 1U,
                                     &expansion.outputBytes, "\n", 1)) return false;
            if (lineEnd < byteCount) lineEnd += bytes[lineEnd] == '\r' && lineEnd + 1U < byteCount && bytes[lineEnd + 1U] == '\n' ? 2U : 1U;
        }
        lineStart = lineEnd;
    }
    if (depth != 0 && expansion.activeCount != 0) --expansion.activeCount;
    return true;
}

static bool prepare_source_with_headers(const char* sourcePath, const char* sourceIdentityPath,
                                        const uint8_t* source, uint32_t sourceBytes,
                                        const char** expandedSource, uint32_t* expandedBytes,
                                        DeclarationDependency* dependencies, uint16_t* dependencyCount,
                                        Diagnostics& diagnostics)
{
    if (!sourcePath || !sourceIdentityPath || !source || !expandedSource || !expandedBytes ||
        !dependencies || !dependencyCount || sourceBytes > COMPILER_MAX_SOURCE_BYTES) return false;
    s_includeExpansion = {};
    IncludeExpansion& expansion = s_includeExpansion;
    if (!project_root_for_source(sourcePath, sourceIdentityPath, expansion.projectRoot, sizeof(expansion.projectRoot))) {
        diagnostics.error({0, 1, 1}, "source path is outside its project build boundary", "include");
        return false;
    }
    if (!expand_include_file(expansion, sourceIdentityPath, source, sourceBytes, 0, diagnostics)) return false;
    expansion.output[expansion.outputBytes] = '\0';
    *expandedSource = reinterpret_cast<const char*>(expansion.output);
    *expandedBytes = expansion.outputBytes;
    for (uint32_t i = 0; i < expansion.dependencyCount; ++i) {
        for (uint32_t j = i + 1; j < expansion.dependencyCount; ++j) {
            uint32_t at = 0;
            while (expansion.dependencies[i].path[at] && expansion.dependencies[j].path[at] &&
                   expansion.dependencies[i].path[at] == expansion.dependencies[j].path[at]) ++at;
            if (static_cast<unsigned char>(expansion.dependencies[j].path[at]) <
                static_cast<unsigned char>(expansion.dependencies[i].path[at])) {
                DeclarationDependency swap = expansion.dependencies[i];
                expansion.dependencies[i] = expansion.dependencies[j];
                expansion.dependencies[j] = swap;
            }
        }
    }
    *dependencyCount = expansion.dependencyCount;
    for (uint32_t i = 0; i < expansion.dependencyCount; ++i) dependencies[i] = expansion.dependencies[i];
    return true;
}

static bool declaration_dependencies_current(const CompiledModule& module,
                                             const char* sourcePath,
                                             const char* sourceIdentityPath)
{
    if (module.dependencyCount > COMPILER_MAX_DECLARATION_DEPENDENCIES) return false;
    char root[COMPILER_MAX_SOURCE_PATH_BYTES] = {};
    if (!project_root_for_source(sourcePath, sourceIdentityPath, root, sizeof(root))) return false;
    for (uint32_t i = 0; i < module.dependencyCount; ++i) {
        const DeclarationDependency& dependency = module.dependencies[i];
        if (!valid_relative_path(dependency.path) || dependency.bytes > COMPILER_MAX_DECLARATION_FILE_BYTES) return false;
        char absolute[COMPILER_MAX_SOURCE_PATH_BYTES] = {};
        if (!path_join(root, dependency.path, absolute, sizeof(absolute))) return false;
        vfs::FileInfo info = {};
        if (vfs::stat(absolute, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
            info.size != dependency.bytes || info.size > COMPILER_MAX_DECLARATION_FILE_BYTES) return false;
        uint8_t buffer[COMPILER_MAX_DECLARATION_FILE_BYTES + 1] = {};
        if (vfs::read_file(absolute, buffer, dependency.bytes) != static_cast<int32_t>(dependency.bytes) ||
            hash_bytes(buffer, dependency.bytes) != dependency.hash) return false;
    }
    return true;
}

static bool temporary_sibling_path(const char* objectPath, char* output, uint32_t capacity)
{
    if (!copy_text(output, capacity, objectPath)) return false;
    uint32_t lastDot = 0xFFFFFFFFU;
    for (uint32_t i = 0; output[i] != '\0'; ++i) {
        if (output[i] == '.') lastDot = i;
    }
    if (lastDot == 0xFFFFFFFFU) return false;
    if (lastDot + 4U >= capacity) return false;
    output[lastDot + 1U] = 't'; output[lastDot + 2U] = 'm'; output[lastDot + 3U] = 'p'; output[lastDot + 4U] = '\0';
    return true;
}

static bool load_cached_object(const char* objectPath, const char* sourcePath,
                               const char* sourceIdentityPath, uint32_t sourceBytes, uint64_t sourceHash,
                               CompiledModule* module)
{
    if (!objectPath || !sourcePath || !sourceIdentityPath || !module || objectPath[0] == '\0') return false;
    vfs::FileInfo info = {};
    if (vfs::stat(objectPath, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size < COMPILER_ELF_OBJECT_HEADER_BYTES || info.size > COMPILER_MAX_OBJECT_BYTES) return false;
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    if (vfs::read_file(objectPath, s_object, bytes) != static_cast<int32_t>(bytes)) return false;
    Diagnostics diagnostics;
    CompiledModule loaded = {};
    if (!deserialize_elf_object(s_object, bytes, &loaded, diagnostics)) {
        serial::puts("Compiler: cache_reject "); serial::puts(objectPath); serial::puts(" reason=malformed ELF object\n");
        return false;
    }
    if (!elf_object_identity_matches(loaded, sourceIdentityPath, sourceBytes, sourceHash)) {
        serial::puts("Compiler: cache_reject "); serial::puts(objectPath); serial::puts(" reason=source identity mismatch\n");
        return false;
    }
    if (!declaration_dependencies_current(loaded, sourcePath, sourceIdentityPath)) {
        serial::puts("Compiler: cache_reject "); serial::puts(objectPath); serial::puts(" reason=declaration dependency mismatch\n");
        return false;
    }
    *module = loaded;
    return true;
}

static bool publish_object(const char* objectPath, const CompiledModule& module)
{
    if (!objectPath || objectPath[0] == '\0') return false;
    uint32_t bytes = 0;
    if (!serialize_elf_object(module, s_object, sizeof(s_object), &bytes)) return false;
    CompiledModule checked = {};
    Diagnostics diagnostics;
    if (!deserialize_elf_object(s_object, bytes, &checked, diagnostics)) return false;
    char temporary[COMPILER_MAX_SOURCE_PATH_BYTES + 1] = {};
    if (!temporary_sibling_path(objectPath, temporary, sizeof(temporary))) return false;
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
    uint32_t compiledModuleCount = 0;
    uint32_t cachedModuleCount = 0;
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
            load_cached_object(objectPaths[i], sourcePath, sourceIdentityPath, sourceBytes, sourceHash, &s_modules[i])) {
            if (summary) {
                summary->moduleStatus[i] = COMPILE_MODULE_CACHE_HIT;
            }
            ++cachedModuleCount;
            serial::puts("Compiler: cache_hit "); serial::puts(sourceIdentityPath); serial::putc('\n');
        } else {
            const char* expandedSource = nullptr;
            uint32_t expandedBytes = 0;
            DeclarationDependency dependencies[COMPILER_MAX_DECLARATION_DEPENDENCIES] = {};
            uint16_t dependencyCount = 0;
            if (!prepare_source_with_headers(sourcePath, sourceIdentityPath, s_source, sourceBytes,
                                             &expandedSource, &expandedBytes, dependencies,
                                             &dependencyCount, diagnostics) ||
                !compile_module_from_source(sourceIdentityPath, expandedSource, expandedBytes,
                                            &s_modules[i], diagnostics)) {
            if (summary) append_diagnostics(diagnostics, summary, sourcePath);
            compileFailed = true;
            continue;
            }
            s_modules[i].sourceBytes = sourceBytes;
            s_modules[i].sourceHash = sourceHash;
            s_modules[i].dependencyCount = dependencyCount;
            for (uint32_t dependency = 0; dependency < dependencyCount; ++dependency)
                s_modules[i].dependencies[dependency] = dependencies[dependency];
            if (summary) {
                summary->moduleStatus[i] = COMPILE_MODULE_COMPILED;
            }
            ++compiledModuleCount;
            if (objectPaths && objectPaths[i] && !publish_object(objectPaths[i], s_modules[i])) {
                diagnostics.error(driverLocation, "compiled object could not be published safely", "object");
                if (summary) append_diagnostics(diagnostics, summary, sourcePath);
                compileFailed = true;
                continue;
            }
            if (objectPaths && objectPaths[i]) {
                // The compiler product is deliberately discarded before the
                // linker sees it. This makes the clean-build path exercise
                // the same close/reopen boundary as a cache hit.
                s_modules[i] = {};
                if (!load_cached_object(objectPaths[i], sourcePath, sourceIdentityPath, sourceBytes, sourceHash, &s_modules[i])) {
                    diagnostics.error(driverLocation, "published ELF object could not be reopened", "object");
                    if (summary) append_diagnostics(diagnostics, summary, sourcePath);
                    compileFailed = true;
                    continue;
                }
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
    // Keep the counters in scalar locals while the per-module compiler and
    // object reader use their large bounded work buffers.  Publish the final
    // values after all module transitions so the build-service snapshot sees
    // the authoritative counts even when a later source fails to compile.
    if (summary) {
        summary->sourceFileCount = sourceCount;
        summary->compiledModuleCount = compiledModuleCount;
        summary->cachedModuleCount = cachedModuleCount;
    }

    if (compileFailed) return fail_project(summary);

    if (objectPaths) {
        serial::puts("Compiler: incremental_counts compiled=");
        put_decimal_u64(compiledModuleCount);
        serial::puts(" reused=");
        put_decimal_u64(cachedModuleCount);
        serial::puts(" sources=");
        put_decimal_u64(sourceCount);
        serial::putc('\n');
        if (summary) summary->persistentObjectsReopened = true;
        serial::puts("[DeveloperStudio] relocatable object reopen: PASS\n");
    }

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
        summary->linkedFromPersistedObjects = cachedModuleCount != 0;
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

    if (!append_bootstrap_source_map(s_linked, s_elf, sizeof(s_elf), &layout)) {
        Diagnostics diagnostics;
        diagnostics.error(driverLocation, "ELF source map could not be appended", "source-map");
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
        summary->sourceFileCount = sourceCount;
        summary->compiledModuleCount = compiledModuleCount;
        summary->cachedModuleCount = cachedModuleCount;
        summary->linkedModuleCount = sourceCount;
        summary->linkedFromPersistedObjects = cachedModuleCount != 0;
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
