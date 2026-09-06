//
// Bare-metal Developer Studio Run service.
//

#include "native_elf_run_service.h"

#include "native_elf_loader.h"
#include "native_elf_validator.h"
#include "kernel/vfs.h"

namespace kernel {
namespace native_elf {
namespace NativeElfRunService {
namespace {

static const char kTarget[] = "guidexos.amd64.baremetal.bootstrap.native";
static const char kProjectKind[] = "native-gui-application";
static const char kAbi[] = "guidexos-c-abi-v1";
static const char kArchitecture[] = "amd64";
static const char kManifest[] = "app/app.json";
static const uint32_t kLegacySnapshotBytes =
    static_cast<uint32_t>(offsetof(gx_development_run_snapshot, outputCount));
static const uint32_t kMaxPath = 256U;
static const uint32_t kMaxProjectBytes = 16U * 1024U;

struct Operation {
    bool used;
    gx_development_run_handle handle;
    gx_development_run_state state;
    gx_development_run_error_code error;
    bool closeRequested;
    uint64_t artifactSize;
    char projectRoot[GX_DEVELOPMENT_RUN_MAX_PROJECT_ROOT_BYTES];
    char projectId[GX_DEVELOPMENT_RUN_MAX_PROJECT_ID_BYTES];
    char projectKind[64];
    char targetProfile[128];
    char manifestPath[GX_DEVELOPMENT_RUN_MAX_PATH_BYTES];
    char artifactPath[GX_DEVELOPMENT_RUN_MAX_PATH_BYTES];
    char artifactSha256[GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES];
    char artifactArchitecture[32];
    char artifactAbi[64];
    char displayName[GX_DEVELOPMENT_RUN_MAX_DISPLAY_NAME_BYTES];
    char resolvedArtifact[kMaxPath];
    NativeElfRunReport report;
    int32_t exitCode;
    char errorMessage[GX_DEVELOPMENT_RUN_MAX_ERROR_BYTES];
};

static Operation s_operation = {};
static gx_development_run_handle s_nextHandle = 1;
static char s_projectText[kMaxProjectBytes + 1] = {};
static char s_manifestText[kMaxProjectBytes + 1] = {};
static uint8_t s_artifact[NATIVE_APP_MAX_ELF_FILE_BYTES] = {};

static uint32_t text_length(const char* text, uint32_t capacity) {
    if (!text) return 0;
    uint32_t length = 0;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static bool copy_text(char* destination, uint32_t capacity, const char* source) {
    if (!destination || capacity == 0 || !source) return false;
    uint32_t i = 0;
    while (i + 1 < capacity && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    if (source[i] != '\0') {
        destination[0] = '\0';
        return false;
    }
    destination[i] = '\0';
    return true;
}

static bool equal_text(const char* left, const char* right) {
    if (!left || !right) return left == right;
    uint32_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == right[i];
}

static uint32_t snapshot_capacity(const gx_development_run_snapshot* snapshot) {
    if (!snapshot) return 0;
    return snapshot->size < sizeof(gx_development_run_snapshot)
        ? snapshot->size : static_cast<uint32_t>(sizeof(gx_development_run_snapshot));
}

static bool snapshot_has(const gx_development_run_snapshot* snapshot, uint32_t offset, uint32_t bytes) {
    const uint32_t capacity = snapshot_capacity(snapshot);
    return capacity >= offset && bytes <= capacity - offset;
}

static void clear_snapshot(gx_development_run_snapshot* snapshot) {
    if (!snapshot) return;
    const uint32_t capacity = snapshot_capacity(snapshot);
    if (capacity < kLegacySnapshotBytes) return;
    for (uint32_t i = 0; i < capacity; ++i) reinterpret_cast<uint8_t*>(snapshot)[i] = 0;
    snapshot->size = capacity;
    snapshot->version = GX_DEVELOPMENT_RUN_API_VERSION;
    snapshot->state = GX_DEVELOPMENT_RUN_EMPTY;
    snapshot->errorCode = GX_DEVELOPMENT_RUN_ERROR_NONE;
}

static void set_failure(gx_development_run_snapshot* snapshot,
                        gx_development_run_error_code error,
                        const char* message) {
    if (!snapshot || snapshot_capacity(snapshot) < kLegacySnapshotBytes) return;
    const uint32_t capacity = snapshot_capacity(snapshot);
    clear_snapshot(snapshot);
    snapshot->state = GX_DEVELOPMENT_RUN_FAILED;
    snapshot->errorCode = error;
    copy_text(snapshot->errorMessage, sizeof(snapshot->errorMessage), message ? message : "bare-metal Run failed");
    (void)capacity;
}

static void snapshot_operation(const Operation& operation, gx_development_run_snapshot* snapshot) {
    if (!snapshot || snapshot_capacity(snapshot) < kLegacySnapshotBytes) return;
    const uint32_t capacity = snapshot_capacity(snapshot);
    clear_snapshot(snapshot);
    snapshot->handle = operation.handle;
    snapshot->state = operation.state;
    snapshot->errorCode = operation.error;
    snapshot->exitCode = operation.exitCode;
    snapshot->cleanupComplete = operation.report.teardownComplete ? 1U : 0U;
    copy_text(snapshot->applicationId, sizeof(snapshot->applicationId), operation.projectId);
    copy_text(snapshot->displayName, sizeof(snapshot->displayName), operation.displayName);
    copy_text(snapshot->artifactSha256, sizeof(snapshot->artifactSha256), operation.artifactSha256);
    copy_text(snapshot->errorMessage, sizeof(snapshot->errorMessage), operation.errorMessage);
    if (snapshot_has(snapshot, offsetof(gx_development_run_snapshot, outputCount), sizeof(snapshot->outputCount))) {
        snapshot->outputCount = operation.report.hostLogCount > GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES
            ? GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES : operation.report.hostLogCount;
        snapshot->outputTruncated = operation.report.hostLogTruncated ||
            operation.report.hostLogCount > GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES ? 1U : 0U;
        const uint32_t available = capacity > offsetof(gx_development_run_snapshot, output)
            ? (capacity - static_cast<uint32_t>(offsetof(gx_development_run_snapshot, output))) /
                sizeof(gx_development_run_output_line) : 0U;
        if (snapshot->outputCount > available) {
            snapshot->outputCount = available;
            snapshot->outputTruncated = 1U;
        }
        for (uint32_t i = 0; i < snapshot->outputCount; ++i)
            copy_text(snapshot->output[i].text, sizeof(snapshot->output[i].text), operation.report.hostLog[i]);
    }
}

static bool is_slash(char value) { return value == '/' || value == '\\'; }

static bool safe_relative(const char* value) {
    if (!value || value[0] == '\0' || value[0] == '/' || value[0] == '\\' || value[1] == ':') return false;
    const uint32_t length = text_length(value, kMaxPath);
    if (length == 0 || length >= kMaxPath) return false;
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

static bool safe_name(const char* value) {
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

static bool join_path(const char* root, const char* relative, char* output, uint32_t capacity) {
    if (!root || !relative || !output || root[0] != '/' || !safe_relative(relative)) return false;
    if (!copy_text(output, capacity, root)) return false;
    uint32_t length = text_length(output, capacity);
    if (length + 1 >= capacity) return false;
    if (length == 0 || output[length - 1] != '/') output[length++] = '/';
    output[length] = '\0';
    return copy_text(output + length, capacity - length, relative);
}

static bool read_bounded(const char* path, char* output, uint32_t capacity) {
    if (!path || !output || capacity < 2) return false;
    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR || info.size >= capacity) return false;
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t read = bytes == 0 ? 0 : vfs::read_file(path, output, bytes);
    if (read < 0 || static_cast<uint32_t>(read) != bytes) return false;
    output[bytes] = '\0';
    return true;
}

static bool json_string(const char* text, uint32_t length, const char* key, char* output, uint32_t capacity) {
    if (!text || !key || !output || capacity == 0) return false;
    output[0] = '\0';
    const uint32_t keyLength = text_length(key, 96);
    for (uint32_t i = 0; i + keyLength + 3 < length; ++i) {
        if (text[i] != '"') continue;
        uint32_t j = 0;
        while (j < keyLength && text[i + 1 + j] == key[j]) ++j;
        if (j != keyLength || text[i + 1 + j] != '"') continue;
        uint32_t cursor = i + keyLength + 2;
        while (cursor < length && (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\r' || text[cursor] == '\n')) ++cursor;
        if (cursor >= length || text[cursor++] != ':') continue;
        while (cursor < length && (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\r' || text[cursor] == '\n')) ++cursor;
        if (cursor >= length || text[cursor++] != '"') return false;
        uint32_t written = 0;
        while (cursor < length) {
            const char value = text[cursor++];
            if (value == '"') { output[written] = '\0'; return true; }
            if (value == '\\' || static_cast<unsigned char>(value) < 0x20 || written + 1 >= capacity) return false;
            output[written++] = value;
        }
        return false;
    }
    return false;
}

static void sha256_rotr(uint32_t value, uint32_t count, uint32_t* result) {
    *result = (value >> count) | (value << (32U - count));
}

class Sha256 {
public:
    Sha256() : bitCount(0), bufferBytes(0) {
        state[0]=0x6A09E667U; state[1]=0xBB67AE85U; state[2]=0x3C6EF372U; state[3]=0xA54FF53AU;
        state[4]=0x510E527FU; state[5]=0x9B05688CU; state[6]=0x1F83D9ABU; state[7]=0x5BE0CD19U;
    }
    void update(const uint8_t* bytes, uint32_t count) {
        while (count != 0) {
            const uint32_t room = 64U - bufferBytes;
            const uint32_t take = count < room ? count : room;
            for (uint32_t i = 0; i < take; ++i) buffer[bufferBytes + i] = bytes[i];
            bufferBytes += take; bytes += take; count -= take; bitCount += static_cast<uint64_t>(take) * 8ULL;
            if (bufferBytes == 64U) { transform(buffer); bufferBytes = 0; }
        }
    }
    void finish(char output[GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES]) {
        buffer[bufferBytes++] = 0x80;
        while (bufferBytes != 56U) { if (bufferBytes == 64U) { transform(buffer); bufferBytes = 0; } buffer[bufferBytes++] = 0; }
        for (uint32_t i = 0; i < 8; ++i) buffer[56U + i] = static_cast<uint8_t>(bitCount >> (56U - i * 8U));
        transform(buffer);
        const char hex[] = "0123456789abcdef"; uint32_t out = 0;
        for (uint32_t i = 0; i < 8; ++i) for (uint32_t j = 0; j < 4; ++j) {
            const uint8_t value = static_cast<uint8_t>(state[i] >> (24U - j * 8U));
            output[out++] = hex[value >> 4]; output[out++] = hex[value & 0x0FU];
        }
        output[out] = '\0';
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
            0x748F82EEU,0x78A5636FU,0x84C87814U,0x8CC70208U,0x90BEFFFAU,0xA4506CEBU,0xBEF9A3F7U,0xC67178F2U};
        uint32_t words[64] = {};
        for (uint32_t i = 0; i < 16; ++i) words[i] = (static_cast<uint32_t>(block[i*4]) << 24) | (static_cast<uint32_t>(block[i*4+1]) << 16) | (static_cast<uint32_t>(block[i*4+2]) << 8) | block[i*4+3];
        for (uint32_t i = 16; i < 64; ++i) { uint32_t a=0,b=0,c=0; sha256_rotr(words[i-15],7,&a); sha256_rotr(words[i-15],18,&b); c=words[i-15]>>3; const uint32_t s0=a^b^c; sha256_rotr(words[i-2],17,&a); sha256_rotr(words[i-2],19,&b); c=words[i-2]>>10; words[i]=words[i-16]+s0+words[i-7]+(a^b^c); }
        uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (uint32_t i = 0; i < 64; ++i) { uint32_t r1=0,r2=0,r3=0; sha256_rotr(e,6,&r1); sha256_rotr(e,11,&r2); sha256_rotr(e,25,&r3); const uint32_t s1=r1^r2^r3; const uint32_t ch=(e&f)^((~e)&g); sha256_rotr(a,2,&r1); sha256_rotr(a,13,&r2); sha256_rotr(a,22,&r3); const uint32_t s0=r1^r2^r3; const uint32_t maj=(a&b)^(a&c)^(b&c); const uint32_t t1=h+s1+ch+k[i]+words[i],t2=s0+maj; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    uint32_t state[8]; uint64_t bitCount; uint8_t buffer[64]; uint32_t bufferBytes;
};

static bool validate_identity(Operation& operation) {
    vfs::FileInfo info = {};
    if (vfs::stat(operation.resolvedArtifact, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_MISSING;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact is missing");
        return false;
    }
    if (info.size != operation.artifactSize || info.size == 0 || info.size > sizeof(s_artifact)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_SIZE_CHANGED;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact size differs from BuildResult");
        return false;
    }
    const uint32_t bytes = static_cast<uint32_t>(info.size);
    const int32_t read = vfs::read_file(operation.resolvedArtifact, s_artifact, bytes);
    if (read < 0 || static_cast<uint32_t>(read) != bytes) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact could not be read");
        return false;
    }
    char actualHash[GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES] = {};
    Sha256 hash;
    hash.update(s_artifact, bytes);
    hash.finish(actualHash);
    if (!equal_text(actualHash, operation.artifactSha256)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_CHANGED;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact hash differs from BuildResult");
        return false;
    }
    NativeElfValidationResult validation = {};
    if (!validate_native_elf(s_artifact, bytes, default_validation_policy(), &validation) || validation.entryPoint == 0) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact failed NativeElf validation");
        return false;
    }
    return true;
}

static bool validate_request(Operation& operation, const gx_development_run_request& request) {
    if (request.size < sizeof(gx_development_run_request) || request.version != GX_DEVELOPMENT_RUN_API_VERSION ||
        !request.projectRoot || !request.projectId || !request.projectKind || !request.targetProfile ||
        !request.manifestPath || !request.artifactPath || !request.artifactSha256 ||
        !request.artifactArchitecture || !request.artifactAbi) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Bare-metal Run request is incomplete");
        return false;
    }
    if (!copy_text(operation.projectRoot, sizeof(operation.projectRoot), request.projectRoot) ||
        !copy_text(operation.projectId, sizeof(operation.projectId), request.projectId) ||
        !copy_text(operation.projectKind, sizeof(operation.projectKind), request.projectKind) ||
        !copy_text(operation.targetProfile, sizeof(operation.targetProfile), request.targetProfile) ||
        !copy_text(operation.manifestPath, sizeof(operation.manifestPath), request.manifestPath) ||
        !copy_text(operation.artifactPath, sizeof(operation.artifactPath), request.artifactPath) ||
        !copy_text(operation.artifactSha256, sizeof(operation.artifactSha256), request.artifactSha256) ||
        !copy_text(operation.artifactArchitecture, sizeof(operation.artifactArchitecture), request.artifactArchitecture) ||
        !copy_text(operation.artifactAbi, sizeof(operation.artifactAbi), request.artifactAbi)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Bare-metal Run request text is too long");
        return false;
    }
    operation.artifactSize = request.artifactSize;
    if (!equal_text(operation.projectKind, kProjectKind) || !equal_text(operation.targetProfile, kTarget) ||
        !equal_text(operation.artifactArchitecture, kArchitecture) || !equal_text(operation.artifactAbi, kAbi) ||
        !equal_text(operation.manifestPath, kManifest) || text_length(operation.artifactSha256, sizeof(operation.artifactSha256)) != 64 ||
        operation.artifactSize == 0 || !safe_relative(operation.artifactPath)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_UNSUPPORTED_TARGET;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Bare-metal Run supports bootstrap NativeElf AMD64 projects only");
        return false;
    }
    if (!join_path(operation.projectRoot, operation.artifactPath,
                   operation.resolvedArtifact, sizeof(operation.resolvedArtifact))) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Build artifact path is invalid");
        return false;
    }
    char projectPath[kMaxPath] = {};
    char manifestPath[kMaxPath] = {};
    if (!join_path(operation.projectRoot, "guidexos.project", projectPath, sizeof(projectPath)) ||
        !join_path(operation.projectRoot, operation.manifestPath, manifestPath, sizeof(manifestPath)) ||
        !read_bounded(projectPath, s_projectText, sizeof(s_projectText)) ||
        !read_bounded(manifestPath, s_manifestText, sizeof(s_manifestText))) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Project metadata or manifest is unavailable");
        return false;
    }
    char value[256] = {};
    const uint32_t projectBytes = text_length(s_projectText, sizeof(s_projectText));
    if (!json_string(s_projectText, projectBytes, "projectId", value, sizeof(value)) || !equal_text(value, operation.projectId) ||
        !json_string(s_projectText, projectBytes, "projectKind", value, sizeof(value)) || !equal_text(value, kProjectKind) ||
        !json_string(s_projectText, projectBytes, "defaultTargetProfile", value, sizeof(value)) || !equal_text(value, kTarget) ||
        !json_string(s_projectText, projectBytes, "entryPoint", value, sizeof(value)) || !equal_text(value, "gx_main") ||
        !json_string(s_projectText, projectBytes, "abi", value, sizeof(value)) || !equal_text(value, kAbi) ||
        !json_string(s_projectText, projectBytes, "architecture", value, sizeof(value)) || !equal_text(value, kArchitecture) ||
        !json_string(s_projectText, projectBytes, "outputName", value, sizeof(value)) || !safe_name(value)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Project metadata is not a supported bootstrap target");
        return false;
    }
    char expected[GX_DEVELOPMENT_RUN_MAX_PATH_BYTES] = "build/bin/amd64/";
    uint32_t expectedLength = text_length(expected, sizeof(expected));
    if (!copy_text(expected + expectedLength, sizeof(expected) - expectedLength, value)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID;
        return false;
    }
    expectedLength = text_length(expected, sizeof(expected));
    if (!copy_text(expected + expectedLength, sizeof(expected) - expectedLength, ".elf") || !equal_text(expected, operation.artifactPath)) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISMATCH;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "BuildResult artifact path does not match project output");
        return false;
    }
    const uint32_t manifestBytes = text_length(s_manifestText, sizeof(s_manifestText));
    if (!json_string(s_manifestText, manifestBytes, "id", value, sizeof(value)) || !equal_text(value, operation.projectId) ||
        !json_string(s_manifestText, manifestBytes, "displayName", operation.displayName, sizeof(operation.displayName)) ||
        !json_string(s_manifestText, manifestBytes, "path", value, sizeof(value)) ||
        !equal_text(value, operation.artifactPath + 6) ||
        !json_string(s_manifestText, manifestBytes, "entryPoint", value, sizeof(value)) || !equal_text(value, "gx_main") ||
        !json_string(s_manifestText, manifestBytes, "abi", value, sizeof(value)) || !equal_text(value, kAbi) ||
        !json_string(s_manifestText, manifestBytes, "runtime", value, sizeof(value)) || !equal_text(value, "native-elf")) {
        operation.error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISMATCH;
        copy_text(operation.errorMessage, sizeof(operation.errorMessage), "Application manifest does not match BuildResult");
        return false;
    }
    return validate_identity(operation);
}

static bool decode(gx_development_run_handle handle) {
    return s_operation.used && handle != 0 && handle == s_operation.handle;
}

} // namespace

gx_result prepare(const gx_development_run_request& request,
                  gx_development_run_handle* outHandle,
                  gx_development_run_snapshot* outSnapshot) {
    if (!outHandle || !outSnapshot || snapshot_capacity(outSnapshot) < kLegacySnapshotBytes) return GX_ERROR_INVALID_ARGUMENT;
    *outHandle = 0;
    clear_snapshot(outSnapshot);
    if (s_operation.used) {
        set_failure(outSnapshot, GX_DEVELOPMENT_RUN_ERROR_RUNTIME_BUSY, "Bare-metal NativeElf runtime is busy");
        return GX_OK;
    }
    s_operation = Operation();
    s_operation.used = true;
    s_operation.handle = s_nextHandle++;
    if (s_operation.handle == 0) s_operation.handle = s_nextHandle++;
    s_operation.state = GX_DEVELOPMENT_RUN_VALIDATING;
    s_operation.error = GX_DEVELOPMENT_RUN_ERROR_NONE;
    if (!validate_request(s_operation, request)) {
        s_operation.state = GX_DEVELOPMENT_RUN_FAILED;
        s_operation.report.teardownComplete = true;
        snapshot_operation(s_operation, outSnapshot);
        s_operation = Operation();
        return GX_OK;
    }
    s_operation.state = GX_DEVELOPMENT_RUN_PREPARED;
    *outHandle = s_operation.handle;
    snapshot_operation(s_operation, outSnapshot);
    return GX_OK;
}

gx_result start(gx_development_run_handle handle) {
    if (!decode(handle)) return GX_ERROR_FAILED;
    if (s_operation.state != GX_DEVELOPMENT_RUN_PREPARED) return GX_ERROR_BUSY;
    if (s_operation.closeRequested) {
        s_operation.state = GX_DEVELOPMENT_RUN_FAILED;
        s_operation.error = GX_DEVELOPMENT_RUN_ERROR_CANCELLED;
        copy_text(s_operation.errorMessage, sizeof(s_operation.errorMessage), "Bare-metal Run was cancelled before start");
        s_operation.report.teardownComplete = true;
        return GX_OK;
    }
    // Prepare records the exact BuildResult identity, but the file can still
    // be replaced before Start. Revalidate immediately before launch so a
    // stale or tampered artifact is never executed.
    if (!validate_identity(s_operation)) {
        s_operation.state = GX_DEVELOPMENT_RUN_FAILED;
        s_operation.report.teardownComplete = true;
        return GX_OK;
    }
    s_operation.state = GX_DEVELOPMENT_RUN_LAUNCHING;
    s_operation.state = GX_DEVELOPMENT_RUN_RUNNING;
    s_operation.report = NativeElfRunReport();
    s_operation.exitCode = 0;
    const bool success = run_file_nested(s_operation.resolvedArtifact, &s_operation.exitCode, &s_operation.report);
    if (!success) {
        s_operation.state = GX_DEVELOPMENT_RUN_FAILED;
        s_operation.error = s_operation.report.runtimeStatus == NativeRuntimeStatus::CallDepthExceeded
            ? GX_DEVELOPMENT_RUN_ERROR_CALL_DEPTH_EXCEEDED
            : (s_operation.report.runtimeStatus == NativeRuntimeStatus::ArrayBoundsExceeded
                ? GX_DEVELOPMENT_RUN_ERROR_ARRAY_BOUNDS_EXCEEDED
                : (s_operation.report.runtimeStatus == NativeRuntimeStatus::InvalidPointerDereference
                    ? GX_DEVELOPMENT_RUN_ERROR_INVALID_POINTER_DEREFERENCE
                    : GX_DEVELOPMENT_RUN_ERROR_LAUNCH_FAILED));
        copy_text(s_operation.errorMessage, sizeof(s_operation.errorMessage),
                  s_operation.report.error ? s_operation.report.error : "NativeElf application launch failed");
        return GX_OK;
    }
    s_operation.state = GX_DEVELOPMENT_RUN_EXITED;
    s_operation.state = GX_DEVELOPMENT_RUN_CLEANING_UP;
    s_operation.state = s_operation.report.teardownComplete ? GX_DEVELOPMENT_RUN_COMPLETED : GX_DEVELOPMENT_RUN_FAILED;
    s_operation.error = s_operation.report.teardownComplete ? GX_DEVELOPMENT_RUN_ERROR_NONE : GX_DEVELOPMENT_RUN_ERROR_INTERNAL;
    if (!s_operation.report.teardownComplete) copy_text(s_operation.errorMessage, sizeof(s_operation.errorMessage), "NativeElf runtime teardown failed");
    return GX_OK;
}

gx_result poll(gx_development_run_handle handle, gx_development_run_snapshot* outSnapshot) {
    if (!outSnapshot || snapshot_capacity(outSnapshot) < kLegacySnapshotBytes) return GX_ERROR_INVALID_ARGUMENT;
    if (!decode(handle)) return GX_ERROR_FAILED;
    snapshot_operation(s_operation, outSnapshot);
    return GX_OK;
}

gx_result request_close(gx_development_run_handle handle) {
    if (!decode(handle)) return GX_ERROR_FAILED;
    if (s_operation.state == GX_DEVELOPMENT_RUN_COMPLETED || s_operation.state == GX_DEVELOPMENT_RUN_FAILED) return GX_OK;
    if (s_operation.state == GX_DEVELOPMENT_RUN_PREPARED) {
        s_operation.closeRequested = true;
        s_operation.state = GX_DEVELOPMENT_RUN_FAILED;
        s_operation.error = GX_DEVELOPMENT_RUN_ERROR_CANCELLED;
        copy_text(s_operation.errorMessage, sizeof(s_operation.errorMessage), "Bare-metal Run cancelled before start");
        s_operation.report.teardownComplete = true;
        return GX_OK;
    }
    return GX_ERROR_UNSUPPORTED;
}

gx_result release(gx_development_run_handle handle) {
    if (!decode(handle)) return GX_ERROR_FAILED;
    if (s_operation.state != GX_DEVELOPMENT_RUN_COMPLETED && s_operation.state != GX_DEVELOPMENT_RUN_FAILED) return GX_ERROR_BUSY;
    s_operation = Operation();
    return GX_OK;
}

} // namespace NativeElfRunService
} // namespace native_elf
} // namespace kernel
