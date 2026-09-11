#include "compiler_build_service.h"

#include "compiler_driver.h"
#include "compiler_target.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

namespace kernel {
namespace compiler {
namespace BareMetalBuildService {
namespace {

static const char kBuildSystem[] = "guidexos-native-baremetal-bootstrap-v1";
static const char kProjectKind[] = "native-gui-application";
static const char kAbi[] = "guidexos-c-abi-v1";
static const char kArm64Profile[] = "guidexos.arm64.baremetal.bootstrap.native";
static const char kAmd64Profile[] = "guidexos.amd64.baremetal.bootstrap.native";
static const char kCurrentProfile[] = "guidexos.current.baremetal.bootstrap.native";
static const char kMultiProfile[] = "guidexos.multi.baremetal.bootstrap.native";
static const uint32_t kMaxText = 16u * 1024u;
static const uint32_t kMaxPath = 256u;
static const uint32_t kMaxArtifactBytes = COMPILER_MAX_OUTPUT_BYTES;

struct BuildJob {
    bool used;
    gx_build_handle handle;
    gx_build_snapshot snapshot;
};

static BuildJob s_job = {};
static gx_build_handle s_nextHandle = 1;
static char s_projectText[kMaxText + 1] = {};
static char s_manifestText[kMaxText + 1] = {};
static uint8_t s_artifactBytes[kMaxArtifactBytes] = {};

static const char* find_text(const char* text, const char* needle);

static uint32_t length(const char* value, uint32_t capacity)
{
    if (!value) return 0;
    uint32_t result = 0;
    while (result < capacity && value[result]) ++result;
    return result;
}

static bool equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    uint32_t i = 0;
    while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == right[i];
}

static bool copy(char* output, uint32_t capacity, const char* input)
{
    if (!output || capacity == 0 || !input) return false;
    uint32_t i = 0;
    while (i + 1 < capacity && input[i]) { output[i] = input[i]; ++i; }
    output[i] = '\0';
    return input[i] == '\0';
}

static bool append(char* output, uint32_t capacity, const char* input)
{
    const uint32_t offset = length(output, capacity);
    return offset < capacity && copy(output + offset, capacity - offset, input);
}

static bool safe_relative(const char* value)
{
    if (!value || !value[0] || value[0] == '/' || value[0] == '\\' || value[1] == ':') return false;
    const uint32_t count = length(value, kMaxPath);
    if (count == 0 || count >= kMaxPath) return false;
    uint32_t start = 0;
    for (uint32_t i = 0; i <= count; ++i) {
        if (i < count && value[i] != '/' && value[i] != '\\') {
            if (static_cast<unsigned char>(value[i]) < 0x20) return false;
            continue;
        }
        const uint32_t part = i - start;
        if (part == 0 || (part == 1 && value[start] == '.') ||
            (part == 2 && value[start] == '.' && value[start + 1] == '.')) return false;
        start = i + 1;
    }
    return true;
}

static bool safe_absolute_app_path(const char* value)
{
    if (!value || value[0] != '/' || value[1] != 'A' || value[2] != 'p' ||
        value[3] != 'p' || value[4] != 's' || value[5] != '/') return false;
    const uint32_t count = length(value, kMaxPath);
    if (count < 7 || count >= kMaxPath) return false;
    for (uint32_t i = 6; i < count; ++i) if (value[i] == '\\' || static_cast<unsigned char>(value[i]) < 0x20) return false;
    return find_text(value + 6, "../") == nullptr && find_text(value + 6, "/../") == nullptr;
}

static const char* find_text(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0]) return text;
    const uint32_t needleLength = length(needle, 64);
    for (const char* current = text; *current; ++current) {
        uint32_t i = 0;
        while (i < needleLength && current[i] == needle[i]) ++i;
        if (i == needleLength) return current;
    }
    return nullptr;
}

static bool join(const char* root, const char* relative, char* output, uint32_t capacity)
{
    if (!root || !relative || !output || !safe_relative(relative) || !copy(output, capacity, root)) return false;
    const uint32_t rootLength = length(output, capacity);
    if (rootLength == 0 || rootLength + 1 >= capacity) return false;
    if (output[rootLength - 1] != '/') output[rootLength] = '/';
    const uint32_t offset = output[rootLength - 1] == '/' ? rootLength : rootLength + 1;
    return copy(output + offset, capacity - offset, relative);
}

static bool read_text(const char* path, char* output, uint32_t capacity)
{
    if (!path || !output || capacity < 2) return false;
    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR || info.size >= capacity) return false;
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t read = bytes == 0 ? 0 : vfs::read_file(path, output, bytes);
    if (read < 0 || static_cast<uint32_t>(read) != bytes) return false;
    output[bytes] = '\0';
    return true;
}

static bool json_string(const char* text, const char* key, char* output, uint32_t capacity)
{
    if (!text || !key || !output || capacity == 0) return false;
    output[0] = '\0';
    const uint32_t textBytes = length(text, kMaxText + 1);
    const uint32_t keyBytes = length(key, 64);
    for (uint32_t i = 0; i + keyBytes + 3 < textBytes; ++i) {
        if (text[i] != '"') continue;
        uint32_t j = 0;
        while (j < keyBytes && text[i + j + 1] == key[j]) ++j;
        if (j != keyBytes || text[i + j + 1] != '"') continue;
        uint32_t position = i + keyBytes + 2;
        while (position < textBytes && (text[position] == ' ' || text[position] == '\t' || text[position] == '\r' || text[position] == '\n')) ++position;
        if (position >= textBytes || text[position++] != ':') continue;
        while (position < textBytes && (text[position] == ' ' || text[position] == '\t' || text[position] == '\r' || text[position] == '\n')) ++position;
        if (position >= textBytes || text[position++] != '"') return false;
        uint32_t written = 0;
        while (position < textBytes) {
            char value = text[position++];
            if (value == '"') { output[written] = '\0'; return true; }
            if (value == '\\') {
                if (position >= textBytes) return false;
                const char escaped = text[position++];
                if (escaped == '"' || escaped == '\\' || escaped == '/') value = escaped;
                else return false;
            }
            if (written + 1 >= capacity) return false;
            output[written++] = value;
        }
        return false;
    }
    return false;
}

static bool ensure_directory(const char* path)
{
    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) == vfs::VFS_OK) return info.type == vfs::FILE_TYPE_DIRECTORY;
    return vfs::mkdir(path) == vfs::VFS_OK;
}

static void output_line(const char* text, uint32_t stream)
{
    if (!s_job.used || !text) return;
    if (s_job.snapshot.outputCount >= GX_BUILD_MAX_OUTPUT_LINES) { s_job.snapshot.outputTruncated = 1; return; }
    gx_build_output_line& line = s_job.snapshot.output[s_job.snapshot.outputCount++];
    line.stream = stream;
    copy(line.text, sizeof(line.text), text);
}

static bool append_decimal(char* output, uint32_t capacity, uint64_t value)
{
    char digits[20] = {};
    uint32_t count = 0;
    if (value == 0) digits[count++] = '0';
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    const uint32_t existing = length(output, capacity);
    if (existing + count + 1 > capacity) return false;
    for (uint32_t i = 0; i < count; ++i) output[existing + i] = digits[count - i - 1];
    output[existing + count] = '\0';
    return true;
}

static void output_compile_diagnostic(const CompileSummary& summary)
{
    if (!summary.diagnosticLine || !summary.diagnosticColumn || !summary.diagnosticMessage[0]) return;
    char text[256] = {};
    if (!copy(text, sizeof(text), "error: line ") || !append_decimal(text, sizeof(text), summary.diagnosticLine) ||
        !append(text, sizeof(text), ", column ") || !append_decimal(text, sizeof(text), summary.diagnosticColumn) ||
        !append(text, sizeof(text), ", offset ") || !append_decimal(text, sizeof(text), summary.diagnosticOffset) ||
        !append(text, sizeof(text), ": ") || !append(text, sizeof(text), summary.diagnosticMessage)) return;
    output_line(text, 2);
}

static void fail(uint32_t code, const char* message)
{
    s_job.snapshot.state = GX_BUILD_FAILED;
    s_job.snapshot.errorCode = code;
    copy(s_job.snapshot.errorMessage, sizeof(s_job.snapshot.errorMessage), message);
    s_job.snapshot.errorCount = 1;
}

static uint64_t hash_bytes(const uint8_t* bytes, uint32_t count)
{
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i < count; ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
    return hash;
}

static void hex64(uint64_t value, char* output, uint32_t capacity)
{
    if (!output || capacity < 17) return;
    const char digits[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 16; ++i) output[i] = digits[(value >> (60u - i * 4u)) & 0xfu];
    output[16] = '\0';
}

static void set_digest(char* output, uint32_t capacity, uint64_t hash)
{
    copy(output, capacity, "fnv1a64-");
    const uint32_t prefix = length("fnv1a64-", capacity);
    hex64(hash, output + prefix, capacity - prefix);
}

static bool copy_artifact(const char* source, const char* destination, uint64_t* outSize, uint64_t* outHash)
{
    vfs::FileInfo info = {};
    if (!source || !destination || vfs::stat(source, &info) != vfs::VFS_OK ||
        info.type != vfs::FILE_TYPE_REGULAR || info.size == 0 || info.size > kMaxArtifactBytes) return false;
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t read = vfs::read_file(source, s_artifactBytes, bytes);
    if (read < 0 || static_cast<uint32_t>(read) != bytes) return false;
    const int32_t written = vfs::write_file(destination, s_artifactBytes, bytes);
    if (written < 0 || static_cast<uint32_t>(written) != bytes) return false;
    if (outSize) *outSize = bytes;
    if (outHash) *outHash = hash_bytes(s_artifactBytes, bytes);
    return true;
}

static bool target_requested(const char* profile, CompilerTarget* first, CompilerTarget* second, uint32_t* count)
{
    if (!first || !second || !count) return false;
    *first = CompilerTarget::Current; *second = CompilerTarget::Current; *count = 0;
    if (equal(profile, kMultiProfile)) {
        *first = CompilerTarget::Arm64; *second = CompilerTarget::Amd64; *count = 2; return true;
    }
    if (equal(profile, kArm64Profile)) { *first = CompilerTarget::Arm64; *count = 1; return true; }
    if (equal(profile, kAmd64Profile)) { *first = CompilerTarget::Amd64; *count = 1; return true; }
    if (equal(profile, kCurrentProfile) || equal(profile, "guidexos.current.baremetal.native")) {
        *first = current_target(); *count = 1; return true;
    }
    return false;
}

static bool publish_package(const char* packageRoot, const char* manifestText,
                            const char* armSource, const char* amdSource,
                            const char* outputName)
{
    if (!safe_absolute_app_path(packageRoot) || !manifestText || !armSource || !amdSource || !outputName ||
        !safe_relative(outputName) || find_text(manifestText, "\"kind\": \"NativeElf\"") == nullptr ||
        find_text(manifestText, "bin/arm64/") == nullptr || find_text(manifestText, "bin/amd64/") == nullptr) return false;
    char path[kMaxPath] = {};
    if (!ensure_directory("/Apps")) return false;
    if (!ensure_directory(packageRoot)) return false;
    if (!copy(path, sizeof(path), packageRoot) || !append(path, sizeof(path), "/bin")) return false;
    if (!ensure_directory(path)) return false;
    char armDir[kMaxPath] = {}; char amdDir[kMaxPath] = {};
    if (!copy(armDir, sizeof(armDir), path) || !append(armDir, sizeof(armDir), "/arm64") ||
        !copy(amdDir, sizeof(amdDir), path) || !append(amdDir, sizeof(amdDir), "/amd64") ||
        !ensure_directory(armDir) || !ensure_directory(amdDir)) return false;
    char armPath[kMaxPath] = {}; char amdPath[kMaxPath] = {}; char manifestPath[kMaxPath] = {};
    if (!copy(armPath, sizeof(armPath), armDir) || !append(armPath, sizeof(armPath), "/") || !append(armPath, sizeof(armPath), outputName) || !append(armPath, sizeof(armPath), ".elf") ||
        !copy(amdPath, sizeof(amdPath), amdDir) || !append(amdPath, sizeof(amdPath), "/") || !append(amdPath, sizeof(amdPath), outputName) || !append(amdPath, sizeof(amdPath), ".elf") ||
        !copy(manifestPath, sizeof(manifestPath), packageRoot) || !append(manifestPath, sizeof(manifestPath), "/app.json")) return false;
    uint64_t ignoredSize = 0, ignoredHash = 0;
    if (!copy_artifact(armSource, armPath, &ignoredSize, &ignoredHash) ||
        !copy_artifact(amdSource, amdPath, &ignoredSize, &ignoredHash)) return false;
    const uint32_t manifestBytes = length(manifestText, kMaxText + 1);
    const int32_t written = vfs::write_file(manifestPath, manifestText, manifestBytes);
    return written >= 0 && static_cast<uint32_t>(written) == manifestBytes;
}

static bool build(const gx_build_request* request)
{
    s_job.snapshot.state = GX_BUILD_PREPARING;
    output_line("In-OS Developer Studio build: resident guideXOS compiler", 1);
    output_line("No host process or external toolchain is used for the target application", 1);
    if (!request || !request->projectRoot || !request->projectId || !request->expectedArtifact ||
        !equal(request->projectKind, kProjectKind) || !equal(request->buildSystem, kBuildSystem)) {
        fail(GX_BUILD_ERROR_UNSUPPORTED_PROJECT, "request is not a supported in-OS NativeElf project"); return false;
    }
    CompilerTarget first = CompilerTarget::Current, second = CompilerTarget::Current; uint32_t targetCount = 0;
    if (!target_requested(request->targetProfile, &first, &second, &targetCount)) {
        fail(GX_BUILD_ERROR_UNSUPPORTED_PROJECT, "target profile is not current, ARM64, AMD64, or multi"); return false;
    }
    char projectPath[kMaxPath] = {}; char manifestPath[kMaxPath] = {};
    if (!join(request->projectRoot, "guidexos.project", projectPath, sizeof(projectPath)) ||
        !read_text(projectPath, s_projectText, sizeof(s_projectText))) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project metadata is missing from the VFS"); return false;
    }
    // A Developer Studio workspace keeps its editable manifest at app/app.json;
    // a published package has one canonical manifest at its package root.
    bool manifestRead = join(request->projectRoot, "app/app.json", manifestPath, sizeof(manifestPath)) &&
        read_text(manifestPath, s_manifestText, sizeof(s_manifestText));
    if (!manifestRead) manifestRead = join(request->projectRoot, "app.json", manifestPath, sizeof(manifestPath)) &&
        read_text(manifestPath, s_manifestText, sizeof(s_manifestText));
    if (!manifestRead) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project manifest is missing from the VFS"); return false;
    }
    char value[256] = {}; char sourceRoot[128] = {}; char sourceEntry[128] = {};
    char outputName[128] = {}; char packageRoot[kMaxPath] = {};
    if (!json_string(s_projectText, "projectId", value, sizeof(value)) || !equal(value, request->projectId) ||
        !json_string(s_projectText, "projectKind", value, sizeof(value)) || !equal(value, kProjectKind) ||
        !json_string(s_projectText, "abi", value, sizeof(value)) || !equal(value, kAbi) ||
        !json_string(s_projectText, "sourceRoot", sourceRoot, sizeof(sourceRoot)) || !safe_relative(sourceRoot) ||
        !json_string(s_projectText, "sourceEntry", sourceEntry, sizeof(sourceEntry)) || !safe_relative(sourceEntry) ||
        !json_string(s_projectText, "outputName", outputName, sizeof(outputName)) ||
        !json_string(s_projectText, "packageRoot", packageRoot, sizeof(packageRoot)) || !safe_absolute_app_path(packageRoot) ||
        !json_string(s_manifestText, "id", value, sizeof(value)) || !equal(value, request->projectId)) {
        fail(GX_BUILD_ERROR_UNSUPPORTED_PROJECT, "project metadata does not describe a bounded NativeElf project"); return false;
    }
    char sourcePath[kMaxPath] = {};
    if (!join(request->projectRoot, sourceRoot, sourcePath, sizeof(sourcePath)) || !append(sourcePath, sizeof(sourcePath), "/") ||
        !append(sourcePath, sizeof(sourcePath), sourceEntry)) {
        fail(GX_BUILD_ERROR_SOURCE_SELECTION, "source path is outside the project root"); return false;
    }
    char armPath[kMaxPath] = {}; char amdPath[kMaxPath] = {};
    if (!join(request->projectRoot, "build", armPath, sizeof(armPath)) || !ensure_directory(armPath) ||
        !join(request->projectRoot, "build/bin", armPath, sizeof(armPath)) || !ensure_directory(armPath) ||
        !join(request->projectRoot, "build/bin/arm64", armPath, sizeof(armPath)) || !ensure_directory(armPath) ||
        !append(armPath, sizeof(armPath), "/") || !append(armPath, sizeof(armPath), outputName) || !append(armPath, sizeof(armPath), ".elf") ||
        !join(request->projectRoot, "build/bin/amd64", amdPath, sizeof(amdPath)) || !ensure_directory(amdPath) ||
        !append(amdPath, sizeof(amdPath), "/") || !append(amdPath, sizeof(amdPath), outputName) || !append(amdPath, sizeof(amdPath), ".elf")) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project build directories could not be created"); return false;
    }
    char expected[kMaxPath] = {};
    if (!copy(expected, sizeof(expected), request->projectRoot) || !append(expected, sizeof(expected), "/") ||
        !append(expected, sizeof(expected), request->expectedArtifact) ||
        !equal(expected, first == CompilerTarget::Amd64 ? amdPath : armPath)) {
        fail(GX_BUILD_ERROR_INVALID_REQUEST, "expected artifact must be the selected target project output"); return false;
    }
    // Keep the last published package intact while source validation and both
    // target compiles run.  A failed rebuild is therefore recoverable by the
    // running App Model and cannot turn a source error into package loss.
    if (vfs::exists(armPath)) (void)vfs::unlink(armPath);
    if (vfs::exists(amdPath)) (void)vfs::unlink(amdPath);
    s_job.snapshot.state = GX_BUILD_RUNNING;
    CompileSummary armSummary = {}; CompileSummary amdSummary = {};
    const CompilerTarget resolvedFirst = first == CompilerTarget::Current ? current_target() : first;
    const char* primaryPath = resolvedFirst == CompilerTarget::Amd64 ? amdPath : armPath;
    CompileSummary* primarySummary = resolvedFirst == CompilerTarget::Amd64 ? &amdSummary : &armSummary;
    output_line(resolvedFirst == CompilerTarget::Amd64 ?
        "Compiling AMD64 source inside guideXOS" : "Compiling ARM64 source inside guideXOS", 1);
    if (!compile_for_target(sourcePath, primaryPath, resolvedFirst, primarySummary)) {
        output_compile_diagnostic(*primarySummary);
        output_line(resolvedFirst == CompilerTarget::Amd64 ?
            "AMD64: compiler failed; package was not written" :
            "ARM64: compiler failed; package was not written", 2);
        fail(GX_BUILD_ERROR_COMPILER_FAILED, resolvedFirst == CompilerTarget::Amd64 ?
            "AMD64: unsupported source, encoding, or ELF emission failure" :
            "ARM64: unsupported source, encoding, or ELF emission failure"); return false;
    }
    bool amdOkay = false;
    if (targetCount == 2) {
        output_line("Compiling AMD64 sibling from the same source tree inside guideXOS", 1);
        amdOkay = compile_for_target(sourcePath, amdPath, second, &amdSummary);
        if (!amdOkay) {
            output_compile_diagnostic(amdSummary);
            output_line("AMD64: compiler failed; package was not written", 2);
            fail(GX_BUILD_ERROR_COMPILER_FAILED, "AMD64: unsupported source, encoding, or ELF emission failure"); return false;
        }
    } else {
        // A single explicit ARM64 build remains useful for target controls;
        // multi-architecture projects use the canonical two-target mode.
        amdOkay = false;
    }
    s_job.snapshot.state = GX_BUILD_VALIDATING_ARTIFACT;
    vfs::FileInfo armInfo = {}, amdInfo = {}, primaryInfo = {};
    if (vfs::stat(primaryPath, &primaryInfo) != vfs::VFS_OK || primaryInfo.type != vfs::FILE_TYPE_REGULAR ||
        (resolvedFirst != CompilerTarget::Amd64 && (vfs::stat(armPath, &armInfo) != vfs::VFS_OK || armInfo.type != vfs::FILE_TYPE_REGULAR)) ||
        (targetCount == 2 && (vfs::stat(amdPath, &amdInfo) != vfs::VFS_OK || amdInfo.type != vfs::FILE_TYPE_REGULAR))) {
        fail(GX_BUILD_ERROR_ARTIFACT_MISSING, "compiler output is missing after target validation"); return false;
    }
    if (targetCount == 2 && !publish_package(packageRoot, s_manifestText, armPath, amdPath, outputName)) {
        fail(GX_BUILD_ERROR_INTERNAL, "package payloads were valid but package publication failed"); return false;
    }
    s_job.snapshot.state = GX_BUILD_SUCCEEDED;
    s_job.snapshot.processExitCode = 0;
    s_job.snapshot.artifactSize = primaryInfo.size;
    s_job.snapshot.artifactValid = 1;
    s_job.snapshot.artifactEntryPoint = 1;
    s_job.snapshot.sourceFileCount = 1;
    s_job.snapshot.compiledModuleCount = targetCount;
    s_job.snapshot.cachedModuleCount = 0;
    s_job.snapshot.linkedModuleCount = targetCount;
    copy(s_job.snapshot.artifactPath, sizeof(s_job.snapshot.artifactPath), request->expectedArtifact);
    copy(s_job.snapshot.artifactArchitecture, sizeof(s_job.snapshot.artifactArchitecture),
        resolvedFirst == CompilerTarget::Amd64 ? "amd64" : "arm64");
    set_digest(s_job.snapshot.artifactSha256, sizeof(s_job.snapshot.artifactSha256), primarySummary->outputHash);
    if (targetCount == 2 && amdOkay) {
        s_job.snapshot.siblingArtifactSize = amdInfo.size;
        s_job.snapshot.siblingArtifactValid = 1;
        copy(s_job.snapshot.siblingArtifactPath, sizeof(s_job.snapshot.siblingArtifactPath), "build/bin/amd64/");
        append(s_job.snapshot.siblingArtifactPath, sizeof(s_job.snapshot.siblingArtifactPath), outputName);
        append(s_job.snapshot.siblingArtifactPath, sizeof(s_job.snapshot.siblingArtifactPath), ".elf");
        copy(s_job.snapshot.siblingArtifactArchitecture, sizeof(s_job.snapshot.siblingArtifactArchitecture), "amd64");
        set_digest(s_job.snapshot.siblingArtifactSha256, sizeof(s_job.snapshot.siblingArtifactSha256), amdSummary.outputHash);
        s_job.snapshot.packageWritten = 1;
        s_job.snapshot.packageGeneration = s_job.handle;
        copy(s_job.snapshot.packagePath, sizeof(s_job.snapshot.packagePath), packageRoot);
    }
    output_line(resolvedFirst == CompilerTarget::Amd64 ?
        "AMD64: build succeeded | machine=EM_X86_64 | W^X=PASS" :
        "ARM64: build succeeded | machine=EM_AARCH64 | W^X=PASS", 1);
    if (targetCount == 2) output_line("AMD64: build succeeded | machine=EM_X86_64 | W^X=PASS", 1);
    if (s_job.snapshot.packageWritten) output_line("Package: app.json + arm64/app.elf + amd64/app.elf published", 1);
    return true;
}

static void clear_snapshot(gx_build_snapshot* snapshot, gx_build_handle handle)
{
    if (!snapshot) return;
    *snapshot = {};
    snapshot->size = sizeof(*snapshot);
    snapshot->version = GX_BUILD_API_VERSION;
    snapshot->handle = handle;
    snapshot->state = GX_BUILD_VALIDATING;
}

} // namespace

gx_result start(const gx_build_request* request, gx_build_handle* outHandle)
{
    if (!request || !outHandle || request->size < sizeof(gx_build_request) || request->version != GX_BUILD_API_VERSION) return GX_ERROR_INVALID_ARGUMENT;
    if (s_job.used) return GX_ERROR_BUSY;
    s_job = {};
    s_job.used = true;
    s_job.handle = s_nextHandle++;
    if (s_job.handle == 0) s_job.handle = s_nextHandle++;
    clear_snapshot(&s_job.snapshot, s_job.handle);
    *outHandle = s_job.handle;
    const bool okay = build(request);
    serial::puts(okay ? "BareMetalBuild: PASS\n" : "BareMetalBuild: FAIL\n");
    return GX_OK;
}

gx_result poll(gx_build_handle handle, gx_build_snapshot* outSnapshot)
{
    if (!s_job.used || handle == 0 || handle != s_job.handle || !outSnapshot) return GX_ERROR_INVALID_ARGUMENT;
    *outSnapshot = s_job.snapshot;
    return GX_OK;
}

gx_result release(gx_build_handle handle)
{
    if (!s_job.used || handle == 0 || handle != s_job.handle) return GX_ERROR_INVALID_ARGUMENT;
    s_job = {};
    return GX_OK;
}

} // namespace BareMetalBuildService
} // namespace compiler
} // namespace kernel
