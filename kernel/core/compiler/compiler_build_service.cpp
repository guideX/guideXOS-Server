//
// Bare-metal Developer Studio build service.
//

#include "compiler_build_service.h"

#include "compiler_driver.h"
#include "elf_writer.h"
#include "../native_elf/native_elf_validator.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

namespace kernel {
namespace compiler {
namespace BareMetalBuildService {
namespace {

static const char kBareMetalTarget[] = "guidexos.amd64.baremetal.bootstrap.native";
static const char kBareMetalBuildSystem[] = "guidexos-native-baremetal-bootstrap-v1";
static const char kNativeGuiKind[] = "native-gui-application";
static const char kAbi[] = "guidexos-c-abi-v1";
static const char kArchitecture[] = "amd64";
static const uint32_t kMaxProjectText = 16u * 1024u;
static const uint32_t kMaxResolvedPath = 256u;
static const uint32_t kMaxSourceCandidates = 32u;

struct BuildJob {
    bool used;
    gx_build_handle handle;
    gx_build_snapshot snapshot;
};

static BuildJob s_job = {};
static gx_build_handle s_nextHandle = 1;
static char s_projectText[kMaxProjectText + 1];
static char s_manifestText[kMaxProjectText + 1];
static uint8_t s_artifact[COMPILER_MAX_OUTPUT_BYTES];

static uint32_t text_length(const char* value, uint32_t capacity)
{
    if (!value) return 0;
    uint32_t length = 0;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

static bool equal_text(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    uint32_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == right[i];
}

static bool copy_text(char* output, uint32_t capacity, const char* input)
{
    if (!output || capacity == 0 || !input) return false;
    uint32_t i = 0;
    while (i + 1 < capacity && input[i] != '\0') {
        output[i] = input[i];
        ++i;
    }
    if (input[i] != '\0') {
        output[0] = '\0';
        return false;
    }
    output[i] = '\0';
    return true;
}

static bool append_text(char* output, uint32_t capacity, const char* input)
{
    if (!output || !input) return false;
    const uint32_t offset = text_length(output, capacity);
    return offset < capacity && copy_text(output + offset, capacity - offset, input);
}

static bool is_slash(char value) { return value == '/' || value == '\\'; }

static bool safe_relative(const char* value)
{
    if (!value || value[0] == '\0' || value[0] == '/' || value[0] == '\\' || value[1] == ':') return false;
    const uint32_t length = text_length(value, kMaxResolvedPath);
    if (length == 0 || length >= kMaxResolvedPath) return false;
    uint32_t segmentStart = 0;
    for (uint32_t i = 0; i <= length; ++i) {
        if (i < length && !is_slash(value[i])) {
            if (static_cast<unsigned char>(value[i]) < 0x20) return false;
            continue;
        }
        const uint32_t segmentLength = i - segmentStart;
        if (segmentLength == 0 || (segmentLength == 1 && value[segmentStart] == '.') ||
            (segmentLength == 2 && value[segmentStart] == '.' && value[segmentStart + 1] == '.')) return false;
        segmentStart = i + 1;
    }
    return true;
}

static bool safe_output_name(const char* value)
{
    if (!value || value[0] == '\0' || value[0] == '.') return false;
    const uint32_t length = text_length(value, 128);
    if (length == 0 || length >= 128) return false;
    for (uint32_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

static bool join_path(const char* root, const char* relative, char* output, uint32_t capacity)
{
    if (!root || !relative || !output || !safe_relative(relative)) return false;
    if (!copy_text(output, capacity, root)) return false;
    const uint32_t length = text_length(output, capacity);
    if (length == 0 || length + 1 >= capacity) return false;
    if (output[length - 1] != '/') output[length] = '/';
    const uint32_t offset = output[length - 1] == '/' ? length : length + 1;
    return copy_text(output + offset, capacity - offset, relative);
}

static bool read_bounded(const char* path, char* output, uint32_t capacity, uint32_t* outBytes)
{
    if (!path || !output || capacity < 2) return false;
    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size >= capacity) return false;
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t read = bytes == 0 ? 0 : vfs::read_file(path, output, bytes);
    if (read < 0 || static_cast<uint32_t>(read) != bytes) return false;
    output[bytes] = '\0';
    if (outBytes) *outBytes = bytes;
    return true;
}

static void skip_space(const char* text, uint32_t length, uint32_t* position)
{
    if (!text || !position) return;
    while (*position < length) {
        const char c = text[*position];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++(*position);
        else break;
    }
}

static bool json_string_value(const char* text, uint32_t length, const char* key,
                              char* output, uint32_t capacity, bool optional)
{
    if (!text || !key || !output || capacity == 0) return false;
    output[0] = '\0';
    const uint32_t keyLength = text_length(key, 128);
    for (uint32_t i = 0; i + keyLength + 3 < length; ++i) {
        if (text[i] != '"') continue;
        uint32_t j = 0;
        while (j < keyLength && text[i + 1 + j] == key[j]) ++j;
        if (j != keyLength || text[i + 1 + j] != '"') continue;
        uint32_t position = i + keyLength + 2;
        skip_space(text, length, &position);
        if (position >= length || text[position++] != ':') continue;
        skip_space(text, length, &position);
        if (position >= length || text[position++] != '"') return false;
        uint32_t written = 0;
        while (position < length) {
            char value = text[position++];
            if (value == '"') {
                output[written] = '\0';
                return true;
            }
            if (value == '\\') {
                if (position >= length) return false;
                const char escaped = text[position++];
                if (escaped == '"' || escaped == '\\' || escaped == '/') value = escaped;
                else if (escaped == 'n') value = '\n';
                else if (escaped == 'r') value = '\r';
                else if (escaped == 't') value = '\t';
                else return false;
            }
            if (written + 1 >= capacity) return false;
            output[written++] = value;
        }
        return false;
    }
    return optional;
}

static bool append_dec(char* output, uint32_t capacity, uint32_t value)
{
    char digits[12] = {};
    uint32_t count = 0;
    if (value == 0) digits[count++] = '0';
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    }
    while (count != 0) {
        char one[2] = { digits[--count], '\0' };
        if (!append_text(output, capacity, one)) return false;
    }
    return true;
}

static void clear_snapshot(gx_build_snapshot* snapshot, gx_build_handle handle)
{
    if (!snapshot) return;
    *snapshot = {};
    snapshot->size = sizeof(gx_build_snapshot);
    snapshot->version = GX_BUILD_API_VERSION;
    snapshot->handle = handle;
    snapshot->state = GX_BUILD_VALIDATING;
    snapshot->errorCode = GX_BUILD_ERROR_NONE;
}

static void output_line(const char* text, uint32_t stream)
{
    if (!s_job.used || !text || s_job.snapshot.outputCount >= GX_BUILD_MAX_OUTPUT_LINES) {
        if (s_job.used) s_job.snapshot.outputTruncated = 1;
        return;
    }
    gx_build_output_line& line = s_job.snapshot.output[s_job.snapshot.outputCount++];
    line.stream = stream;
    copy_text(line.text, sizeof(line.text), text);
}

static void failure(uint32_t errorCode, const char* message)
{
    s_job.snapshot.state = GX_BUILD_FAILED;
    s_job.snapshot.errorCode = errorCode;
    copy_text(s_job.snapshot.errorMessage, sizeof(s_job.snapshot.errorMessage), message ? message : "bare-metal build failed");
}

static void sha256_rotr(uint32_t value, uint32_t count, uint32_t* result)
{
    *result = (value >> count) | (value << (32U - count));
}

class Sha256 {
public:
    Sha256() : m_bitCount(0), m_bufferBytes(0) {
        m_state[0] = 0x6A09E667U; m_state[1] = 0xBB67AE85U; m_state[2] = 0x3C6EF372U; m_state[3] = 0xA54FF53AU;
        m_state[4] = 0x510E527FU; m_state[5] = 0x9B05688CU; m_state[6] = 0x1F83D9ABU; m_state[7] = 0x5BE0CD19U;
    }
    void update(const uint8_t* bytes, uint32_t count) {
        while (count != 0) {
            const uint32_t room = 64U - m_bufferBytes;
            const uint32_t take = count < room ? count : room;
            for (uint32_t i = 0; i < take; ++i) m_buffer[m_bufferBytes + i] = bytes[i];
            m_bufferBytes += take; bytes += take; count -= take; m_bitCount += static_cast<uint64_t>(take) * 8ULL;
            if (m_bufferBytes == 64U) { transform(m_buffer); m_bufferBytes = 0; }
        }
    }
    void finish(char output[GX_BUILD_MAX_SHA256_BYTES]) {
        const uint32_t saved = m_bufferBytes;
        m_buffer[m_bufferBytes++] = 0x80;
        while (m_bufferBytes != 56U) {
            if (m_bufferBytes == 64U) { transform(m_buffer); m_bufferBytes = 0; }
            m_buffer[m_bufferBytes++] = 0;
        }
        for (uint32_t i = 0; i < 8; ++i) m_buffer[56U + i] = static_cast<uint8_t>(m_bitCount >> (56U - i * 8U));
        transform(m_buffer);
        const char hex[] = "0123456789abcdef";
        uint32_t out = 0;
        for (uint32_t i = 0; i < 8; ++i) for (uint32_t j = 0; j < 4; ++j) {
            const uint8_t byte = static_cast<uint8_t>(m_state[i] >> (24U - j * 8U));
            output[out++] = hex[byte >> 4]; output[out++] = hex[byte & 0x0FU];
        }
        output[out] = '\0';
        m_bufferBytes = saved;
    }
private:
    void transform(const uint8_t block[64]) {
        static const uint32_t k[64] = {
            0x428A2F98U,0x71374491U,0xB5C0FBCFU,0xE9B5DBA5U,0x3956C25BU,0x59F111F1U,0x923F82A4U,0xAB1C5ED5U,
            0xD807AA98U,0x12835B01U,0x243185BEU,0x550C7DC3U,0x72BE5D74U,0x80DEB1FEU,0x9BDC06A7U,0xC19BF174U,
            0xE49B69C1U,0xEFBE4786U,0x0FC19DC6U,0x240CA1CCU,0x2DE92C6FU,0x4A7484AAU,0x5CB0A9DCU,0x76F988DAU,
            0x983E5152U,0xA831C66DU,0xB00327C8U,0xBF597FC7U,0xC6E00BF3U,0xD5A79147U,0x06CA6351U,0x14292967U,
            0x27B70A85U,0x2E1B2138U,0x4D2C6DFCU,0x53380D13U,0x650A7354U,0x766A0ABBU,0x81C2C92EU,0x92722C85U,
            0xA2BFE8A1U,0xA81A664BU,0xC24B8B70U,0xC76C51A3U,0xD192E819U,0xD6990624U,0xF40E3585U,0x106AA070U,
            0x19A4C116U,0x1E376C08U,0x2748774CU,0x34B0BCB5U,0x391C0CB3U,0x4ED8AA4AU,0x5B9CCA4FU,0x682E6FF3U,
            0x748F82EEU,0x78A5636FU,0x84C87814U,0x8CC70208U,0x90BEFFFAU,0xA4506CEBU,0xBEF9A3F7U,0xC67178F2U
        };
        uint32_t w[64] = {};
        for (uint32_t i = 0; i < 16; ++i) w[i] = (static_cast<uint32_t>(block[i*4]) << 24) | (static_cast<uint32_t>(block[i*4+1]) << 16) | (static_cast<uint32_t>(block[i*4+2]) << 8) | block[i*4+3];
        for (uint32_t i = 16; i < 64; ++i) {
            uint32_t a = 0, b = 0; sha256_rotr(w[i-15], 7, &a); uint32_t c = 0; sha256_rotr(w[i-15], 18, &b); c = w[i-15] >> 3; const uint32_t s0 = a ^ b ^ c;
            sha256_rotr(w[i-2], 17, &a); sha256_rotr(w[i-2], 19, &b); c = w[i-2] >> 10; const uint32_t s1 = a ^ b ^ c;
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=m_state[0], b=m_state[1], c=m_state[2], d=m_state[3], e=m_state[4], f=m_state[5], g=m_state[6], h=m_state[7];
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t r1=0,r2=0,r3=0; sha256_rotr(e,6,&r1); sha256_rotr(e,11,&r2); sha256_rotr(e,25,&r3); const uint32_t s1=r1^r2^r3; const uint32_t ch=(e&f)^((~e)&g);
            sha256_rotr(a,2,&r1); sha256_rotr(a,13,&r2); sha256_rotr(a,22,&r3); const uint32_t s0=r1^r2^r3; const uint32_t maj=(a&b)^(a&c)^(b&c);
            const uint32_t t1=h+s1+ch+k[i]+w[i], t2=s0+maj; h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        m_state[0]+=a; m_state[1]+=b; m_state[2]+=c; m_state[3]+=d; m_state[4]+=e; m_state[5]+=f; m_state[6]+=g; m_state[7]+=h;
    }
    uint32_t m_state[8]; uint64_t m_bitCount; uint8_t m_buffer[64]; uint32_t m_bufferBytes;
};

static void artifact_sha256(const uint8_t* bytes, uint32_t count, char output[GX_BUILD_MAX_SHA256_BYTES])
{
    Sha256 hash; hash.update(bytes, count); hash.finish(output);
}

static bool choose_source(const char* root, const char* sourceRoot, const char* sourceEntry,
                          char* sourcePath, uint32_t sourcePathCapacity, char* relativePath, uint32_t relativeCapacity)
{
    if (!root || !sourceRoot || !safe_relative(sourceRoot)) return false;
    if (sourceEntry && sourceEntry[0] != '\0') {
        if (!safe_relative(sourceEntry) || !join_path(root, sourceRoot, sourcePath, sourcePathCapacity)) return false;
        const uint32_t sourceRootLength = text_length(sourcePath, sourcePathCapacity);
        if (sourceRootLength + 1 >= sourcePathCapacity || !append_text(sourcePath, sourcePathCapacity, "/") || !append_text(sourcePath, sourcePathCapacity, sourceEntry)) return false;
        if (!copy_text(relativePath, relativeCapacity, sourceRoot) || !append_text(relativePath, relativeCapacity, "/") || !append_text(relativePath, relativeCapacity, sourceEntry)) return false;
        vfs::FileInfo info = {};
        return vfs::stat(sourcePath, &info) == vfs::VFS_OK && info.type == vfs::FILE_TYPE_REGULAR;
    }

    char sourceDirectory[kMaxResolvedPath] = {};
    if (!join_path(root, sourceRoot, sourceDirectory, sizeof(sourceDirectory))) return false;
    const uint8_t iterator = vfs::opendir(sourceDirectory);
    if (iterator == 0xFF) return false;
    char candidates[kMaxSourceCandidates][vfs::VFS_MAX_FILENAME] = {};
    uint32_t count = 0;
    vfs::DirEntry entry = {};
    while (count < kMaxSourceCandidates && vfs::readdir(iterator, &entry)) {
        const uint32_t length = text_length(entry.name, sizeof(entry.name));
        const bool c = length >= 2 && entry.name[length - 2] == '.' && entry.name[length - 1] == 'c';
        const bool cpp = length >= 4 && entry.name[length - 4] == '.' && entry.name[length - 3] == 'c' && entry.name[length - 2] == 'p' && entry.name[length - 1] == 'p';
        if (entry.type == vfs::FILE_TYPE_REGULAR && (c || cpp)) copy_text(candidates[count++], sizeof(candidates[0]), entry.name);
    }
    vfs::closedir(iterator);
    for (uint32_t i = 0; i < count; ++i) for (uint32_t j = i + 1; j < count; ++j) {
        bool before = false; uint32_t k = 0; while (candidates[i][k] && candidates[j][k] && candidates[i][k] == candidates[j][k]) ++k;
        before = static_cast<unsigned char>(candidates[i][k]) < static_cast<unsigned char>(candidates[j][k]);
        if (!before) { char swap[vfs::VFS_MAX_FILENAME] = {}; copy_text(swap, sizeof(swap), candidates[i]); copy_text(candidates[i], sizeof(candidates[i]), candidates[j]); copy_text(candidates[j], sizeof(candidates[j]), swap); }
    }
    if (count != 1) return false;
    if (!join_path(root, sourceRoot, sourcePath, sourcePathCapacity) || !append_text(sourcePath, sourcePathCapacity, "/") || !append_text(sourcePath, sourcePathCapacity, candidates[0])) return false;
    return copy_text(relativePath, relativeCapacity, sourceRoot) && append_text(relativePath, relativeCapacity, "/") && append_text(relativePath, relativeCapacity, candidates[0]);
}

static bool ensure_directory(const char* path)
{
    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) == vfs::VFS_OK) return info.type == vfs::FILE_TYPE_DIRECTORY;
    return vfs::mkdir(path) == vfs::VFS_OK;
}

static bool ensure_output_directory(const char* root)
{
    char path[kMaxResolvedPath] = {};
    return join_path(root, "build", path, sizeof(path)) && ensure_directory(path) &&
        join_path(root, "build/bin", path, sizeof(path)) && ensure_directory(path) &&
        join_path(root, "build/bin/amd64", path, sizeof(path)) && ensure_directory(path);
}

static bool metadata_matches(const char* root, const gx_build_request* request,
                             char* sourcePath, char* sourceRelative, char* artifactPath,
                             uint32_t* outErrorCode)
{
    if (outErrorCode) *outErrorCode = GX_BUILD_ERROR_UNSUPPORTED_PROJECT;
    char metadataPath[kMaxResolvedPath] = {};
    if (!join_path(root, "guidexos.project", metadataPath, sizeof(metadataPath)) ||
        !read_bounded(metadataPath, s_projectText, sizeof(s_projectText), nullptr)) return false;
    char value[256] = {};
    if (!json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "projectKind", value, sizeof(value), false) || !equal_text(value, kNativeGuiKind) ||
        !json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "defaultTargetProfile", value, sizeof(value), false) || !equal_text(value, kBareMetalTarget) ||
        !json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "abi", value, sizeof(value), false) || !equal_text(value, kAbi) ||
        !json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "architecture", value, sizeof(value), false) || !equal_text(value, kArchitecture) ||
        !json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "entryPoint", value, sizeof(value), false) || !equal_text(value, "gx_main")) return false;
    char sourceRoot[128] = {}; char sourceEntry[256] = {};
    if (!json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "sourceRoot", sourceRoot, sizeof(sourceRoot), false) ||
        !json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "sourceEntry", sourceEntry, sizeof(sourceEntry), true) ||
        !choose_source(root, sourceRoot, sourceEntry, sourcePath, kMaxResolvedPath, sourceRelative, kMaxResolvedPath)) {
        if (outErrorCode) *outErrorCode = GX_BUILD_ERROR_SOURCE_SELECTION;
        return false;
    }
    char outputName[128] = {};
    if (!json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "outputName", outputName, sizeof(outputName), false) || !safe_output_name(outputName)) return false;
    if (!join_path(root, "build/bin/amd64", artifactPath, kMaxResolvedPath) || !append_text(artifactPath, kMaxResolvedPath, "/") || !append_text(artifactPath, kMaxResolvedPath, outputName) || !append_text(artifactPath, kMaxResolvedPath, ".elf")) return false;
    char expected[kMaxResolvedPath] = {};
    if (!request || !request->expectedArtifact || !request->projectId ||
        !copy_text(expected, sizeof(expected), root) || !append_text(expected, sizeof(expected), "/") ||
        !append_text(expected, sizeof(expected), request->expectedArtifact) || !equal_text(expected, artifactPath)) return false;
    char manifestPath[kMaxResolvedPath] = {};
    if (!join_path(root, "app/app.json", manifestPath, sizeof(manifestPath)) || !read_bounded(manifestPath, s_manifestText, sizeof(s_manifestText), nullptr)) return false;
    const uint32_t manifestBytes = text_length(s_manifestText, sizeof(s_manifestText));
    char manifestValue[256] = {};
    if (!json_string_value(s_manifestText, manifestBytes, "id", manifestValue, sizeof(manifestValue), false)) return false;
    char projectId[128] = {};
    if (!json_string_value(s_projectText, text_length(s_projectText, sizeof(s_projectText)), "projectId", projectId, sizeof(projectId), false) ||
        !equal_text(manifestValue, projectId) || !equal_text(request->projectId, projectId)) return false;
    if (!json_string_value(s_manifestText, manifestBytes, "entryPoint", manifestValue, sizeof(manifestValue), false) || !equal_text(manifestValue, "gx_main") ||
        !json_string_value(s_manifestText, manifestBytes, "abi", manifestValue, sizeof(manifestValue), false) || !equal_text(manifestValue, kAbi) ||
        !json_string_value(s_manifestText, manifestBytes, "architecture", manifestValue, sizeof(manifestValue), false) || !equal_text(manifestValue, kArchitecture) ||
        !json_string_value(s_manifestText, manifestBytes, "path", manifestValue, sizeof(manifestValue), false)) return false;
    char expectedManifestPath[160] = {};
    if (!copy_text(expectedManifestPath, sizeof(expectedManifestPath), "bin/amd64/") ||
        !append_text(expectedManifestPath, sizeof(expectedManifestPath), outputName) ||
        !append_text(expectedManifestPath, sizeof(expectedManifestPath), ".elf") ||
        !equal_text(manifestValue, expectedManifestPath)) return false;
    return true;
}

static void run_build(const gx_build_request* request)
{
    s_job.snapshot.state = GX_BUILD_PREPARING;
    output_line("Bare-metal build backend: kernel VFS compiler", 1);
    output_line("No host process or external toolchain is used", 1);
    char sourcePath[kMaxResolvedPath] = {}; char sourceRelative[kMaxResolvedPath] = {}; char artifactPath[kMaxResolvedPath] = {};
    uint32_t metadataError = GX_BUILD_ERROR_UNSUPPORTED_PROJECT;
    if (!request || !equal_text(request->targetProfile, kBareMetalTarget) || !equal_text(request->buildSystem, kBareMetalBuildSystem) ||
        !metadata_matches(request->projectRoot, request, sourcePath, sourceRelative, artifactPath, &metadataError)) {
        failure(metadataError, metadataError == GX_BUILD_ERROR_SOURCE_SELECTION
            ? "project source selection is not exactly one supported source file"
            : "project metadata is not a supported bare-metal bootstrap target");
        return;
    }
    if (!ensure_output_directory(request->projectRoot)) {
        failure(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project build output directories could not be created");
        return;
    }
    if (vfs::exists(artifactPath) && vfs::unlink(artifactPath) != vfs::VFS_OK) {
        failure(GX_BUILD_ERROR_ARTIFACT_INVALID, "stale artifact could not be removed");
        return;
    }
    s_job.snapshot.state = GX_BUILD_RUNNING;
    char sourceLine[GX_BUILD_MAX_OUTPUT_LINE_BYTES] = {};
    copy_text(sourceLine, sizeof(sourceLine), "Source: "); append_text(sourceLine, sizeof(sourceLine), sourceRelative); output_line(sourceLine, 1);
    static CompileSummary summary = {};
    summary = {};
    if (!compile(sourcePath, artifactPath, &summary)) {
        for (uint32_t i = 0; i < summary.diagnosticCount; ++i) {
            char line[GX_BUILD_MAX_OUTPUT_LINE_BYTES] = {};
            append_text(line, sizeof(line), sourceRelative); append_text(line, sizeof(line), "("); append_dec(line, sizeof(line), summary.diagnostics[i].location.line); append_text(line, sizeof(line), ","); append_dec(line, sizeof(line), summary.diagnostics[i].location.column); append_text(line, sizeof(line), "): error: "); append_text(line, sizeof(line), summary.diagnostics[i].message);
            output_line(line, 2);
        }
        s_job.snapshot.errorCount = summary.diagnosticCount == 0 ? 1 : summary.diagnosticCount;
        s_job.snapshot.warningCount = 0;
        failure(GX_BUILD_ERROR_COMPILER_FAILED, "bare-metal compiler rejected the saved source");
        return;
    }
    s_job.snapshot.state = GX_BUILD_VALIDATING_ARTIFACT;
    vfs::FileInfo info = {};
    const bool artifactRead = vfs::stat(artifactPath, &info) == vfs::VFS_OK && info.type == vfs::FILE_TYPE_REGULAR && info.size <= sizeof(s_artifact);
    const uint32_t bytes = artifactRead ? static_cast<uint32_t>(info.size) : 0;
    const int32_t readBytes = artifactRead ? vfs::read_file(artifactPath, s_artifact, bytes) : -1;
    native_elf::NativeElfValidationResult validation = {};
    const bool valid = artifactRead && readBytes == static_cast<int32_t>(bytes) && native_elf::validate_native_elf(s_artifact, bytes, native_elf::default_validation_policy(), &validation);
    if (!valid) {
        s_job.snapshot.errorCount = 1;
        failure(readBytes != static_cast<int32_t>(bytes) ? GX_BUILD_ERROR_ARTIFACT_MISSING : GX_BUILD_ERROR_ARTIFACT_INVALID, "produced artifact failed NativeElf validation");
        return;
    }
    s_job.snapshot.state = GX_BUILD_SUCCEEDED;
    s_job.snapshot.artifactSize = bytes;
    s_job.snapshot.artifactValid = 1;
    s_job.snapshot.artifactEntryPoint = static_cast<uint32_t>(validation.entryPoint);
    copy_text(s_job.snapshot.artifactPath, sizeof(s_job.snapshot.artifactPath), request->expectedArtifact);
    copy_text(s_job.snapshot.artifactArchitecture, sizeof(s_job.snapshot.artifactArchitecture), kArchitecture);
    artifact_sha256(s_artifact, bytes, s_job.snapshot.artifactSha256);
    char successLine[GX_BUILD_MAX_OUTPUT_LINE_BYTES] = {};
    append_text(successLine, sizeof(successLine), "NativeElf validation PASS | bytes="); append_dec(successLine, sizeof(successLine), bytes); append_text(successLine, sizeof(successLine), " | entry=0x10001000"); output_line(successLine, 1);
    s_job.snapshot.processExitCode = 0;
}

} // namespace

gx_result start(const gx_build_request* request, gx_build_handle* outHandle)
{
    if (!request || !outHandle || request->size < sizeof(gx_build_request) || request->version != GX_BUILD_API_VERSION) return GX_ERROR_INVALID_ARGUMENT;
    *outHandle = 0;
    if (s_job.used) return GX_ERROR_BUSY;
    s_job.used = true;
    s_job.handle = s_nextHandle++;
    if (s_job.handle == 0) s_job.handle = s_nextHandle++;
    clear_snapshot(&s_job.snapshot, s_job.handle);
    *outHandle = s_job.handle;
    run_build(request);
    serial::puts(s_job.snapshot.state == GX_BUILD_SUCCEEDED ? "BareMetalBuild: PASS\n" : "BareMetalBuild: FAIL\n");
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
