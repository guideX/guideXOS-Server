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
static uint8_t s_artifactReadback[kMaxArtifactBytes] = {};

static const char* find_text(const char* text, const char* needle);

static uint32_t length(const char* value, uint32_t capacity)
{
    if (!value) return 0;
    uint32_t result = 0;
    while (result < capacity && value[result]) ++result;
    return result;
}

static uint32_t rotate_right(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32u - count));
}

static bool sha256_hex(const uint8_t* bytes, uint32_t count, char* output, uint32_t capacity)
{
    static const uint32_t kRound[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    if (!bytes || !output || capacity < 65u) return false;
    uint32_t state[8] = { 0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                          0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u };
    const uint64_t bitCount = static_cast<uint64_t>(count) * 8u;
    const uint32_t paddedBytes = ((count + 9u + 63u) / 64u) * 64u;
    for (uint32_t blockOffset = 0; blockOffset < paddedBytes; blockOffset += 64u) {
        uint32_t words[64] = {};
        for (uint32_t i = 0; i < 16u; ++i) {
            const uint32_t position = blockOffset + i * 4u;
            uint8_t octets[4] = {};
            for (uint32_t j = 0; j < 4u; ++j) {
                const uint32_t absolute = position + j;
                if (absolute < count) octets[j] = bytes[absolute];
                else if (absolute == count) octets[j] = 0x80u;
                else if (absolute >= paddedBytes - 8u)
                    octets[j] = static_cast<uint8_t>(bitCount >> ((paddedBytes - absolute - 1u) * 8u));
            }
            words[i] = (static_cast<uint32_t>(octets[0]) << 24) |
                       (static_cast<uint32_t>(octets[1]) << 16) |
                       (static_cast<uint32_t>(octets[2]) << 8) | octets[3];
        }
        for (uint32_t i = 16u; i < 64u; ++i) {
            const uint32_t x = words[i - 15u], y = words[i - 2u];
            const uint32_t small0 = rotate_right(x, 7u) ^ rotate_right(x, 18u) ^ (x >> 3u);
            const uint32_t small1 = rotate_right(y, 17u) ^ rotate_right(y, 19u) ^ (y >> 10u);
            words[i] = words[i - 16u] + small0 + words[i - 7u] + small1;
        }
        uint32_t a=state[0], b=state[1], c=state[2], d=state[3];
        uint32_t e=state[4], f=state[5], g=state[6], h=state[7];
        for (uint32_t i = 0; i < 64u; ++i) {
            const uint32_t big1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t t1 = h + big1 + choice + kRound[i] + words[i];
            const uint32_t big0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = big0 + majority;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }
    static const char kHex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 8u; ++i) {
        for (uint32_t nibble = 0; nibble < 8u; ++nibble) {
            const uint32_t shift = 28u - nibble * 4u;
            output[i * 8u + nibble] = kHex[(state[i] >> shift) & 0xfu];
        }
    }
    output[64] = '\0';
    return true;
}

static bool equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    for (uint32_t i = 0; i < kMaxPath; ++i) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
    }
    return false;
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
    if (!value || value[0] != '/') return false;
    const uint32_t count = length(value, kMaxPath);
    if (count < 7 || count >= kMaxPath || value[1] != 'A' || value[2] != 'p' ||
        value[3] != 'p' || value[4] != 's' || value[5] != '/') return false;
    uint32_t start = 6;
    for (uint32_t i = 6; i <= count; ++i) {
        if (i < count && value[i] != '/' && value[i] != '\\') {
            if (value[i] == ':' || static_cast<unsigned char>(value[i]) < 0x20) return false;
            continue;
        }
        const uint32_t part = i - start;
        if (part == 0 || (part == 1 && value[start] == '.') ||
            (part == 2 && value[start] == '.' && value[start + 1] == '.')) return false;
        start = i + 1;
    }
    return true;
}

static bool safe_workspace_root(const char* value)
{
    if (!value || value[0] != '/') return false;
    const uint32_t count = length(value, kMaxPath);
    if (count < 2 || count >= kMaxPath) return false;
    uint32_t start = 1;
    for (uint32_t i = 1; i <= count; ++i) {
        if (i < count && value[i] != '/' && value[i] != '\\') {
            if (value[i] == ':' || static_cast<unsigned char>(value[i]) < 0x20) return false;
            continue;
        }
        const uint32_t part = i - start;
        if (part == 0 || (part == 1 && value[start] == '.') ||
            (part == 2 && value[start] == '.' && value[start + 1] == '.')) return false;
        start = i + 1;
    }
    return true;
}

static bool safe_output_name(const char* value)
{
    if (!value) return false;
    // Pinned Studio derives outputName with a -guidexos suffix. Keep the
    // logical name bounded and path-safe; artifact_storage_path_impl maps its
    // ELF output to an 8.3 FAT entry for actual VFS storage.
    const uint32_t count = length(value, 96);
    if (count == 0 || count >= 96 || value[0] == '.') return false;
    for (uint32_t i = 0; i < count; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
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

static char fold_ascii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

static bool equal_ascii_folded(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    uint32_t i = 0;
    while (left[i] && right[i] && fold_ascii(left[i]) == fold_ascii(right[i])) ++i;
    return left[i] == right[i];
}

static bool artifact_storage_path_impl(const char* logicalPath, char* storagePath, uint32_t capacity)
{
    if (!logicalPath || !storagePath || capacity == 0) return false;
    storagePath[0] = '\0';
    const char* basename = logicalPath;
    for (const char* current = logicalPath; *current; ++current)
        if (*current == '/') basename = current + 1;
    const char* extension = nullptr;
    for (const char* current = basename; *current; ++current)
        if (*current == '.') extension = current;
    if (!extension || !equal_ascii_folded(extension, ".elf")) return false;
    const uint32_t stemBytes = static_cast<uint32_t>(extension - basename);
    if (stemBytes == 0) return false;
    if (stemBytes <= 8u) return copy(storagePath, capacity, logicalPath);

    // FAT create_file_path currently publishes one 8.3 directory entry. Keep
    // the Studio logical artifact path in the build snapshot while storing a
    // deterministic, path-scoped short basename for VFS write/readback.
    const uint32_t pathBytes = length(logicalPath, kMaxPath);
    if (pathBytes == 0 || pathBytes >= kMaxPath) return false;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < pathBytes; ++i) {
        hash ^= static_cast<uint8_t>(fold_ascii(logicalPath[i]));
        hash *= 16777619u;
    }
    const uint32_t prefixBytes = static_cast<uint32_t>(basename - logicalPath);
    if (prefixBytes + 12u >= capacity) return false;
    for (uint32_t i = 0; i < prefixBytes; ++i) storagePath[i] = logicalPath[i];
    static const char kHex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < 8u; ++i)
        storagePath[prefixBytes + i] = kHex[(hash >> (28u - i * 4u)) & 0xFu];
    storagePath[prefixBytes + 8u] = '.';
    storagePath[prefixBytes + 9u] = 'E';
    storagePath[prefixBytes + 10u] = 'L';
    storagePath[prefixBytes + 11u] = 'F';
    storagePath[prefixBytes + 12u] = '\0';
    return true;
}

static const char* storage_alias_for(const char* logicalPath)
{
    const char* basename = logicalPath;
    for (const char* current = logicalPath; current && *current; ++current)
        if (*current == '/') basename = current + 1;
    if (equal_ascii_folded(basename, "guidexos.project")) return "GUIDEXOS.PJT";
    if (equal_ascii_folded(basename, "app.json")) return "APP.JSN";
    return nullptr;
}

// Studio's 8.3-only create path stores the pinned logical project/manifest
// names under adapter aliases. Resolve them here with the same fail-closed
// rule as the ABI adapter: canonical only or alias only is valid; both is
// ambiguous and neither is missing.
static bool read_studio_text(const char* logicalPath, char* output, uint32_t capacity)
{
    const char* alias = storage_alias_for(logicalPath);
    if (!alias) return read_text(logicalPath, output, capacity);
    const char* basename = logicalPath;
    for (const char* current = logicalPath; current && *current; ++current)
        if (*current == '/') basename = current + 1;
    const uint32_t prefixBytes = static_cast<uint32_t>(basename - logicalPath);
    const uint32_t aliasBytes = length(alias, 32);
    if (prefixBytes + aliasBytes >= kMaxPath) return false;
    char aliasPath[kMaxPath] = {};
    for (uint32_t i = 0; i < prefixBytes; ++i) aliasPath[i] = logicalPath[i];
    for (uint32_t i = 0; i <= aliasBytes; ++i) aliasPath[prefixBytes + i] = alias[i];

    vfs::FileInfo logicalInfo = {}, aliasInfo = {};
    const vfs::Status logicalStatus = vfs::stat(logicalPath, &logicalInfo);
    const vfs::Status aliasStatus = vfs::stat(aliasPath, &aliasInfo);
    if ((logicalStatus != vfs::VFS_OK && logicalStatus != vfs::VFS_ERR_NOT_FOUND) ||
        (aliasStatus != vfs::VFS_OK && aliasStatus != vfs::VFS_ERR_NOT_FOUND) ||
        (logicalStatus == vfs::VFS_OK && aliasStatus == vfs::VFS_OK)) return false;
    if (logicalStatus == vfs::VFS_OK)
        return logicalInfo.type == vfs::FILE_TYPE_REGULAR && read_text(logicalPath, output, capacity);
    if (aliasStatus == vfs::VFS_OK)
        return aliasInfo.type == vfs::FILE_TYPE_REGULAR && read_text(aliasPath, output, capacity);
    return false;
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

static bool artifact_sha256(const char* path, uint64_t expectedSize, char* output, uint32_t capacity)
{
    vfs::FileInfo info = {};
    if (!path || vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size == 0 || info.size > kMaxArtifactBytes || info.size != expectedSize) return false;
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t read = vfs::read_file(path, s_artifactBytes, bytes);
    return read >= 0 && static_cast<uint32_t>(read) == bytes &&
        sha256_hex(s_artifactBytes, bytes, output, capacity);
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
    vfs::FileInfo copiedInfo = {};
    const int32_t copiedBytes = vfs::stat(destination, &copiedInfo) == vfs::VFS_OK &&
        copiedInfo.type == vfs::FILE_TYPE_REGULAR && copiedInfo.size == bytes
        ? vfs::read_file(destination, s_artifactReadback, bytes) : vfs::VFS_ERR_IO;
    if (copiedBytes < 0 || static_cast<uint32_t>(copiedBytes) != bytes) return false;
    for (uint32_t i = 0; i < bytes; ++i) if (s_artifactBytes[i] != s_artifactReadback[i]) return false;
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
        !safe_output_name(outputName) || find_text(manifestText, "\"kind\": \"NativeElf\"") == nullptr ||
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
    if (!safe_workspace_root(request->projectRoot)) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project root must be a confined absolute VFS path"); return false;
    }
    if (!safe_relative(request->expectedArtifact)) {
        fail(GX_BUILD_ERROR_INVALID_REQUEST, "expected artifact must be a confined project-relative path"); return false;
    }
    CompilerTarget first = CompilerTarget::Current, second = CompilerTarget::Current; uint32_t targetCount = 0;
    if (!target_requested(request->targetProfile, &first, &second, &targetCount)) {
        fail(GX_BUILD_ERROR_UNSUPPORTED_PROJECT, "target profile is not current, ARM64, AMD64, or multi"); return false;
    }
    char projectPath[kMaxPath] = {}; char manifestPath[kMaxPath] = {};
    if (!join(request->projectRoot, "guidexos.project", projectPath, sizeof(projectPath)) ||
        !read_studio_text(projectPath, s_projectText, sizeof(s_projectText))) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project metadata is missing from the VFS"); return false;
    }
    // A Developer Studio workspace keeps its editable manifest at app/app.json;
    // a published package has one canonical manifest at its package root.
    bool manifestRead = join(request->projectRoot, "app/app.json", manifestPath, sizeof(manifestPath)) &&
        read_studio_text(manifestPath, s_manifestText, sizeof(s_manifestText));
    if (!manifestRead) manifestRead = join(request->projectRoot, "app.json", manifestPath, sizeof(manifestPath)) &&
        read_studio_text(manifestPath, s_manifestText, sizeof(s_manifestText));
    if (!manifestRead) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project manifest is missing from the VFS"); return false;
    }
    char value[256] = {}; char sourceRoot[128] = {}; char sourceEntry[128] = {};
    char outputName[128] = {}; char packageRoot[kMaxPath] = {};
    const bool packageRootDeclared = find_text(s_projectText, "\"packageRoot\"") != nullptr;
    const bool hasPackageRoot = json_string(s_projectText, "packageRoot", packageRoot, sizeof(packageRoot));
    if (!json_string(s_projectText, "projectId", value, sizeof(value)) || !equal(value, request->projectId) ||
        !json_string(s_projectText, "projectKind", value, sizeof(value)) || !equal(value, kProjectKind) ||
        !json_string(s_projectText, "abi", value, sizeof(value)) || !equal(value, kAbi) ||
        !json_string(s_projectText, "sourceRoot", sourceRoot, sizeof(sourceRoot)) || !safe_relative(sourceRoot) ||
        !json_string(s_projectText, "sourceEntry", sourceEntry, sizeof(sourceEntry)) || !safe_relative(sourceEntry) ||
        !json_string(s_projectText, "outputName", outputName, sizeof(outputName)) || !safe_output_name(outputName) ||
        (packageRootDeclared && (!hasPackageRoot || !safe_absolute_app_path(packageRoot))) ||
        (!packageRootDeclared && targetCount == 2) ||
        !json_string(s_manifestText, "id", value, sizeof(value)) || !equal(value, request->projectId)) {
        fail(GX_BUILD_ERROR_UNSUPPORTED_PROJECT, "project metadata does not describe a bounded NativeElf project"); return false;
    }
    char sourcePath[kMaxPath] = {};
    if (!join(request->projectRoot, sourceRoot, sourcePath, sizeof(sourcePath)) || !append(sourcePath, sizeof(sourcePath), "/") ||
        !append(sourcePath, sizeof(sourcePath), sourceEntry)) {
        fail(GX_BUILD_ERROR_SOURCE_SELECTION, "source path is outside the project root"); return false;
    }
    char directory[kMaxPath] = {};
    char armDirectory[kMaxPath] = {}; char amdDirectory[kMaxPath] = {};
    if (!join(request->projectRoot, "build", directory, sizeof(directory)) || !ensure_directory(directory) ||
        !join(request->projectRoot, "build/bin", directory, sizeof(directory)) || !ensure_directory(directory) ||
        !join(request->projectRoot, "build/bin/arm64", armDirectory, sizeof(armDirectory)) || !ensure_directory(armDirectory) ||
        !join(request->projectRoot, "build/bin/amd64", amdDirectory, sizeof(amdDirectory)) || !ensure_directory(amdDirectory)) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project build directories could not be created"); return false;
    }
    char armLogicalPath[kMaxPath] = {}; char amdLogicalPath[kMaxPath] = {};
    if (!copy(armLogicalPath, sizeof(armLogicalPath), armDirectory) || !append(armLogicalPath, sizeof(armLogicalPath), "/") ||
        !append(armLogicalPath, sizeof(armLogicalPath), outputName) || !append(armLogicalPath, sizeof(armLogicalPath), ".elf") ||
        !copy(amdLogicalPath, sizeof(amdLogicalPath), amdDirectory) || !append(amdLogicalPath, sizeof(amdLogicalPath), "/") ||
        !append(amdLogicalPath, sizeof(amdLogicalPath), outputName) || !append(amdLogicalPath, sizeof(amdLogicalPath), ".elf")) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project artifact path exceeds the VFS path bound"); return false;
    }
    char armPath[kMaxPath] = {}; char amdPath[kMaxPath] = {};
    if (!artifact_storage_path_impl(armLogicalPath, armPath, sizeof(armPath)) ||
        !artifact_storage_path_impl(amdLogicalPath, amdPath, sizeof(amdPath))) {
        fail(GX_BUILD_ERROR_INVALID_PROJECT_ROOT, "project artifact name cannot be mapped to FAT storage"); return false;
    }
    char expected[kMaxPath] = {};
    if (!copy(expected, sizeof(expected), request->projectRoot) || !append(expected, sizeof(expected), "/") ||
        !append(expected, sizeof(expected), request->expectedArtifact) ||
        !equal(expected, first == CompilerTarget::Amd64 ? amdLogicalPath : armLogicalPath)) {
        fail(GX_BUILD_ERROR_INVALID_REQUEST, "expected artifact must be the selected target project output"); return false;
    }
    // Remove stale project-local build outputs before compiling so a failed
    // rebuild cannot be mistaken for a fresh artifact. The published package
    // remains intact until a complete replacement package is ready.
    const bool removedStaleOutputs = vfs::exists(armPath) || vfs::exists(amdPath);
    if (vfs::exists(armPath)) (void)vfs::unlink(armPath);
    if (vfs::exists(amdPath)) (void)vfs::unlink(amdPath);
    if (removedStaleOutputs) serial::puts("BareMetalBuild: stale target outputs removed before rebuild\n");
    s_job.snapshot.state = GX_BUILD_RUNNING;
    CompileSummary armSummary = {}; CompileSummary amdSummary = {};
    const CompilerTarget resolvedFirst = first == CompilerTarget::Current ? current_target() : first;
    const char* primaryPath = resolvedFirst == CompilerTarget::Amd64 ? amdPath : armPath;
    CompileSummary* primarySummary = resolvedFirst == CompilerTarget::Amd64 ? &amdSummary : &armSummary;
    serial::puts("BareMetalBuild: artifact storage path=");
    serial::puts(primaryPath);
    serial::putc('\n');
    output_line(resolvedFirst == CompilerTarget::Amd64 ?
        "Compiling AMD64 source inside guideXOS" : "Compiling ARM64 source inside guideXOS", 1);
    if (!compile_for_target(sourcePath, primaryPath, resolvedFirst, primarySummary)) {
        if (vfs::exists(armPath)) (void)vfs::unlink(armPath);
        if (vfs::exists(amdPath)) (void)vfs::unlink(amdPath);
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
            if (vfs::exists(armPath)) (void)vfs::unlink(armPath);
            if (vfs::exists(amdPath)) (void)vfs::unlink(amdPath);
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
        if (vfs::exists(armPath)) (void)vfs::unlink(armPath);
        if (vfs::exists(amdPath)) (void)vfs::unlink(amdPath);
        fail(GX_BUILD_ERROR_ARTIFACT_MISSING, "compiler output is missing after target validation"); return false;
    }
    if (targetCount == 2 && !publish_package(packageRoot, s_manifestText, armPath, amdPath, outputName)) {
        if (vfs::exists(armPath)) (void)vfs::unlink(armPath);
        if (vfs::exists(amdPath)) (void)vfs::unlink(amdPath);
        output_line("VFS: package output path was unavailable; no package was published", 2);
        serial::puts("BareMetalBuild: VFS package output path unavailable; package not published\n");
        fail(GX_BUILD_ERROR_INTERNAL, "VFS package output path was unavailable"); return false;
    }
    char primarySha256[GX_BUILD_MAX_SHA256_BYTES] = {};
    char siblingSha256[GX_BUILD_MAX_SHA256_BYTES] = {};
    if (!artifact_sha256(primaryPath, primaryInfo.size, primarySha256, sizeof(primarySha256)) ||
        (targetCount == 2 && !artifact_sha256(amdPath, amdInfo.size, siblingSha256, sizeof(siblingSha256)))) {
        if (vfs::exists(armPath)) (void)vfs::unlink(armPath);
        if (vfs::exists(amdPath)) (void)vfs::unlink(amdPath);
        fail(GX_BUILD_ERROR_ARTIFACT_MISSING, "compiled ELF could not be read back for its SHA-256 identity"); return false;
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
    copy(s_job.snapshot.artifactSha256, sizeof(s_job.snapshot.artifactSha256), primarySha256);
    if (targetCount == 2 && amdOkay) {
        s_job.snapshot.siblingArtifactSize = amdInfo.size;
        s_job.snapshot.siblingArtifactValid = 1;
        copy(s_job.snapshot.siblingArtifactPath, sizeof(s_job.snapshot.siblingArtifactPath), "build/bin/amd64/");
        append(s_job.snapshot.siblingArtifactPath, sizeof(s_job.snapshot.siblingArtifactPath), outputName);
        append(s_job.snapshot.siblingArtifactPath, sizeof(s_job.snapshot.siblingArtifactPath), ".elf");
        copy(s_job.snapshot.siblingArtifactArchitecture, sizeof(s_job.snapshot.siblingArtifactArchitecture), "amd64");
        copy(s_job.snapshot.siblingArtifactSha256, sizeof(s_job.snapshot.siblingArtifactSha256), siblingSha256);
        s_job.snapshot.packageWritten = 1;
        s_job.snapshot.packageGeneration = s_job.handle;
        copy(s_job.snapshot.packagePath, sizeof(s_job.snapshot.packagePath), packageRoot);
    }
    output_line(resolvedFirst == CompilerTarget::Amd64 ?
        "AMD64: build succeeded | machine=EM_X86_64 | W^X=PASS" :
        "ARM64: build succeeded | machine=EM_AARCH64 | W^X=PASS", 1);
    if (targetCount == 2) output_line("AMD64: build succeeded | machine=EM_X86_64 | W^X=PASS", 1);
    char digestLine[112] = {};
    copy(digestLine, sizeof(digestLine), resolvedFirst == CompilerTarget::Amd64 ? "AMD64: artifact SHA-256=" : "ARM64: artifact SHA-256=");
    append(digestLine, sizeof(digestLine), primarySha256);
    output_line(digestLine, 1);
    if (targetCount == 2) {
        copy(digestLine, sizeof(digestLine), "AMD64: artifact SHA-256=");
        append(digestLine, sizeof(digestLine), siblingSha256);
        output_line(digestLine, 1);
    }
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

bool artifact_storage_path(const char* logicalPath, char* storagePath, uint32_t capacity)
{
    return artifact_storage_path_impl(logicalPath, storagePath, capacity);
}

gx_result start(const gx_build_request* request, gx_build_handle* outHandle)
{
    if (!outHandle) return GX_ERROR_INVALID_ARGUMENT;
    *outHandle = 0;
    if (!request || request->size < sizeof(gx_build_request) || request->version != GX_BUILD_API_VERSION) return GX_ERROR_INVALID_ARGUMENT;
    if (s_job.used) return GX_ERROR_BUSY;
    s_job = {};
    s_job.used = true;
    s_job.handle = s_nextHandle++;
    if (s_job.handle == 0) s_job.handle = s_nextHandle++;
    clear_snapshot(&s_job.snapshot, s_job.handle);
    *outHandle = s_job.handle;
    const bool okay = build(request);
    serial::puts(okay ? "BareMetalBuild: PASS\n" : "BareMetalBuild: FAIL\n");
    if (okay) {
        serial::puts("BareMetalBuild: artifact=");
        serial::puts(s_job.snapshot.artifactPath);
        serial::putc('\n');
        serial::puts("BareMetalBuild: artifact SHA-256=");
        serial::puts(s_job.snapshot.artifactSha256);
        serial::putc('\n');
        if (s_job.snapshot.packageWritten) {
            serial::puts("BareMetalBuild: package=");
            serial::puts(s_job.snapshot.packagePath);
            serial::putc('\n');
        }
    } else if (s_job.snapshot.errorMessage[0]) {
        serial::puts("BareMetalBuild: error=");
        serial::puts(s_job.snapshot.errorMessage);
        serial::putc('\n');
    }
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
