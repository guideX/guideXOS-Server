#include "development_run_service.h"

#include "app_manifest_loader.h"
#include "compositor.h"
#include "desktop_service.h"
#include "elf_validator.h"
#include "ipc.h"
#include "logger.h"
#include "native_app_process_table.h"
#include "native_app_debugger.h"
#include "process.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace gxos {
namespace apps {
namespace DevelopmentRunService {
namespace {

constexpr char kOwnerAppId[] = "com.guidexos.developerstudio";
constexpr char kProjectKind[] = "native-gui-application";
constexpr char kTargetProfile[] = "guidexos.amd64.hosted.native";
constexpr char kArchitecture[] = "amd64";
constexpr char kAbi[] = "guidexos-c-abi-v1";
constexpr char kManifestPath[] = "app/app.json";
constexpr uint32_t kMaxDeployments = 8;
constexpr uint32_t kMaxManifestBytes = 16u * 1024u;
constexpr uint64_t kMaxArtifactBytes = 64ull * 1024ull * 1024ull;

struct Deployment {
    gx_development_run_handle handle = 0;
    uint32_t slot = 0;
    uint32_t generation = 1;
    uint64_t ownerRuntimeId = 0;
    std::string projectId;
    std::string projectRoot;
    std::string applicationId;
    std::string displayName;
    std::string manifestPath;
    std::string artifactPath;
    std::string artifactSha256;
    uint64_t processId = 0;
    uint64_t nativeRuntimeId = 0;
    uint32_t createdWindowCount = 0;
    uint32_t windowCount = 0;
    int32_t exitCode = 0;
    gx_development_run_state state = GX_DEVELOPMENT_RUN_EMPTY;
    gx_development_run_error_code error = GX_DEVELOPMENT_RUN_ERROR_NONE;
    std::string errorMessage;
    bool closeRequested = false;
    bool cleanupComplete = false;
    bool appModelRegistered = false;
    bool debugControlled = false;
    bool debugExecutionReleased = false;
};

struct Slot {
    bool used = false;
    uint32_t generation = 1;
    Deployment deployment;
};

std::array<Slot, kMaxDeployments> g_slots;
std::mutex g_mutex;

uint32_t textLength(const char* value, uint32_t limit) {
    if (!value) return 0;
    uint32_t length = 0;
    while (length < limit && value[length] != '\0') ++length;
    return length;
}

void copyBounded(char* destination, size_t capacity, const std::string& value) {
    if (!destination || capacity == 0) return;
    const size_t count = std::min(capacity - 1, value.size());
    std::copy(value.begin(), value.begin() + static_cast<std::ptrdiff_t>(count), destination);
    destination[count] = '\0';
}

void clearSnapshot(gx_development_run_snapshot* snapshot) {
    if (!snapshot) return;
    *snapshot = gx_development_run_snapshot{};
    snapshot->size = sizeof(gx_development_run_snapshot);
    snapshot->version = GX_DEVELOPMENT_RUN_API_VERSION;
    snapshot->state = GX_DEVELOPMENT_RUN_EMPTY;
    snapshot->errorCode = GX_DEVELOPMENT_RUN_ERROR_NONE;
}

const char* errorName(gx_development_run_error_code error) {
    switch (error) {
    case GX_DEVELOPMENT_RUN_ERROR_NONE: return "none";
    case GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST: return "INVALID_REQUEST";
    case GX_DEVELOPMENT_RUN_ERROR_OWNER_NOT_ALLOWED: return "OWNER_NOT_ALLOWED";
    case GX_DEVELOPMENT_RUN_ERROR_NO_PROJECT: return "NO_PROJECT";
    case GX_DEVELOPMENT_RUN_ERROR_WORKSPACE_ONLY: return "WORKSPACE_ONLY";
    case GX_DEVELOPMENT_RUN_ERROR_UNSUPPORTED_PROJECT: return "UNSUPPORTED_PROJECT";
    case GX_DEVELOPMENT_RUN_ERROR_UNSUPPORTED_TARGET: return "UNSUPPORTED_TARGET";
    case GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID: return "PROJECT_INVALID";
    case GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISSING: return "MANIFEST_MISSING";
    case GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MALFORMED: return "MANIFEST_MALFORMED";
    case GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISMATCH: return "MANIFEST_MISMATCH";
    case GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_MISSING: return "ARTIFACT_MISSING";
    case GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_CHANGED: return "ARTIFACT_CHANGED";
    case GX_DEVELOPMENT_RUN_ERROR_BUILD_REQUIRED: return "BUILD_REQUIRED";
    case GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID: return "ARTIFACT_INVALID";
    case GX_DEVELOPMENT_RUN_ERROR_ENTRY_POINT_MISSING: return "ENTRY_POINT_MISSING";
    case GX_DEVELOPMENT_RUN_ERROR_ABI_MISMATCH: return "ABI_MISMATCH";
    case GX_DEVELOPMENT_RUN_ERROR_ARCHITECTURE_MISMATCH: return "ARCHITECTURE_MISMATCH";
    case GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_INSTALLED: return "APPLICATION_ID_INSTALLED";
    case GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_IN_USE: return "APPLICATION_ID_IN_USE";
    case GX_DEVELOPMENT_RUN_ERROR_DEPLOYMENT_ALREADY_ACTIVE: return "DEPLOYMENT_ALREADY_ACTIVE";
    case GX_DEVELOPMENT_RUN_ERROR_STALE_DEPLOYMENT: return "STALE_DEPLOYMENT";
    case GX_DEVELOPMENT_RUN_ERROR_OWNER_MISMATCH: return "OWNER_MISMATCH";
    case GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE: return "LAUNCH_UNAVAILABLE";
    case GX_DEVELOPMENT_RUN_ERROR_LAUNCH_FAILED: return "LAUNCH_FAILED";
    case GX_DEVELOPMENT_RUN_ERROR_RELEASED: return "RELEASED";
    case GX_DEVELOPMENT_RUN_ERROR_SERVICE_UNAVAILABLE: return "SERVICE_UNAVAILABLE";
    case GX_DEVELOPMENT_RUN_ERROR_INTERNAL: return "INTERNAL";
    default: return "UNKNOWN";
    }
}

void setSnapshotFromDeployment(const Deployment& deployment, gx_development_run_snapshot* snapshot) {
    if (!snapshot) return;
    clearSnapshot(snapshot);
    snapshot->handle = deployment.handle;
    snapshot->state = deployment.state;
    snapshot->errorCode = deployment.error;
    snapshot->processId = deployment.processId;
    snapshot->nativeRuntimeId = deployment.nativeRuntimeId;
    snapshot->windowCount = deployment.windowCount;
    snapshot->createdWindowCount = deployment.createdWindowCount;
    snapshot->exitCode = deployment.exitCode;
    snapshot->cleanupComplete = deployment.cleanupComplete ? 1u : 0u;
    copyBounded(snapshot->applicationId, sizeof(snapshot->applicationId), deployment.applicationId);
    copyBounded(snapshot->displayName, sizeof(snapshot->displayName), deployment.displayName);
    copyBounded(snapshot->artifactSha256, sizeof(snapshot->artifactSha256), deployment.artifactSha256);
    copyBounded(snapshot->errorMessage, sizeof(snapshot->errorMessage), deployment.errorMessage);
}

void setFailure(gx_development_run_snapshot* snapshot, gx_development_run_error_code error, const std::string& message) {
    if (!snapshot) return;
    clearSnapshot(snapshot);
    snapshot->state = GX_DEVELOPMENT_RUN_FAILED;
    snapshot->errorCode = error;
    copyBounded(snapshot->errorMessage, sizeof(snapshot->errorMessage), message.empty() ? errorName(error) : message);
}

bool decodeHandle(gx_development_run_handle handle, uint32_t& slot, uint32_t& generation) {
    slot = static_cast<uint32_t>(handle & 0xFFFFFFFFu);
    generation = static_cast<uint32_t>(handle >> 32);
    return handle != 0 && slot > 0 && slot <= kMaxDeployments && generation != 0;
}

gx_development_run_handle makeHandle(uint32_t slot, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(slot + 1);
}

Slot* findOwnedLocked(gx_development_run_handle handle, uint64_t ownerRuntimeId) {
    uint32_t encodedSlot = 0;
    uint32_t generation = 0;
    if (!decodeHandle(handle, encodedSlot, generation)) return nullptr;
    Slot& slot = g_slots[encodedSlot - 1];
    if (!slot.used || slot.generation != generation || slot.deployment.handle != handle) return nullptr;
    if (slot.deployment.ownerRuntimeId != ownerRuntimeId) return nullptr;
    return &slot;
}

bool isAbsoluteSafeRoot(const std::filesystem::path& root) {
    if (!root.is_absolute()) return false;
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(root, ec);
    return !ec && std::filesystem::is_directory(status);
}

bool hasSymlinkComponent(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        const auto status = std::filesystem::symlink_status(current, ec);
        if (std::filesystem::is_symlink(status)) return true;
        if (ec == std::errc::no_such_file_or_directory) { ec.clear(); continue; }
        if (ec) return true;
    }
    return false;
}

std::string lowerCopy(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character + ('a' - 'A'));
    }
    return value;
}

bool isContainedPath(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const std::string rootText = lowerCopy(root.lexically_normal().generic_string());
    const std::string candidateText = lowerCopy(candidate.lexically_normal().generic_string());
    return candidateText.size() >= rootText.size() && candidateText.compare(0, rootText.size(), rootText) == 0 &&
        (candidateText.size() == rootText.size() || candidateText[rootText.size()] == '/');
}

bool isSafeRelativePath(const std::string& value) {
    if (value.empty() || value.size() >= GX_DEVELOPMENT_RUN_MAX_PATH_BYTES || value[0] == '/' || value[0] == '\\') return false;
    if (value.size() >= 2 && value[1] == ':') return false;
    size_t start = 0;
    for (size_t i = 0; i <= value.size(); ++i) {
        if (i < value.size() && value[i] != '/' && value[i] != '\\') continue;
        if (i == start) return false;
        const std::string segment = value.substr(start, i - start);
        if (segment == "." || segment == "..") return false;
        start = i + 1;
    }
    return true;
}

bool isValidApplicationId(const std::string& value) {
    if (value.empty() || value.size() >= GX_DEVELOPMENT_RUN_MAX_APP_ID_BYTES || value.find("com.guidexos.") == 0) return false;
    size_t segmentStart = 0;
    uint32_t segments = 0;
    for (size_t i = 0; i <= value.size(); ++i) {
        if (i < value.size() && value[i] != '.') continue;
        if (i == segmentStart) return false;
        if (value[segmentStart] < 'a' || value[segmentStart] > 'z') return false;
        for (size_t j = segmentStart + 1; j < i; ++j) {
            const char c = value[j];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
            if (c == '-' && j + 1 == i) return false;
        }
        ++segments;
        segmentStart = i + 1;
    }
    return segments >= 2;
}

bool readBoundedFile(const std::filesystem::path& path, uint32_t maxBytes, std::string& output) {
    output.clear();
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) return false;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > maxBytes) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    output.assign(static_cast<size_t>(size), '\0');
    if (size > 0) input.read(&output[0], static_cast<std::streamsize>(size));
    return static_cast<bool>(input) || size == 0;
}

bool extractJsonString(const std::string& json, const std::string& key, std::string& value) {
    const std::string needle = "\"" + key + "\"";
    const size_t position = json.find(needle);
    if (position == std::string::npos || json.find(needle, position + needle.size()) != std::string::npos) return false;
    size_t cursor = position + needle.size();
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
    if (cursor >= json.size() || json[cursor++] != ':') return false;
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
    if (cursor >= json.size() || json[cursor++] != '"') return false;
    value.clear();
    while (cursor < json.size()) {
        const char character = json[cursor++];
        if (character == '"') return true;
        if (character == '\\' || static_cast<unsigned char>(character) < 0x20) return false;
        value.push_back(character);
        if (value.size() >= GX_DEVELOPMENT_RUN_MAX_PATH_BYTES) return false;
    }
    return false;
}

bool extractJsonNumber(const std::string& json, const std::string& key, uint32_t& value) {
    const std::string needle = "\"" + key + "\"";
    const size_t position = json.find(needle);
    if (position == std::string::npos || json.find(needle, position + needle.size()) != std::string::npos) return false;
    size_t cursor = position + needle.size();
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
    if (cursor >= json.size() || json[cursor++] != ':') return false;
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
    if (cursor >= json.size() || json[cursor] < '0' || json[cursor] > '9') return false;
    uint64_t number = 0;
    while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9') {
        number = number * 10u + static_cast<uint32_t>(json[cursor++] - '0');
        if (number > std::numeric_limits<uint32_t>::max()) return false;
    }
    value = static_cast<uint32_t>(number);
    return true;
}

struct Sha256 {
    uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint64_t length = 0;
    unsigned char buffer[64] = {};
    uint32_t used = 0;
    static uint32_t rotate(uint32_t value, uint32_t count) { return (value >> count) | (value << (32u - count)); }
    static uint32_t word(const unsigned char* bytes) { return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) | (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3]; }
    void transform(const unsigned char* bytes) {
        static const uint32_t k[64] = {0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69cbu,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,0xa831c66bu,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,0x27b70a3u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        uint32_t words[64] = {};
        for (uint32_t i = 0; i < 16; ++i) words[i] = word(bytes + i * 4);
        for (uint32_t i = 16; i < 64; ++i) { const uint32_t s0 = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^ (words[i - 15] >> 3); const uint32_t s1 = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^ (words[i - 2] >> 10); words[i] = words[i - 16] + s0 + words[i - 7] + s1; }
        uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (uint32_t i = 0; i < 64; ++i) { const uint32_t s1=rotate(e,6)^rotate(e,11)^rotate(e,25); const uint32_t choose=(e&f)^((~e)&g); const uint32_t t1=h+s1+choose+k[i]+words[i]; const uint32_t s0=rotate(a,2)^rotate(a,13)^rotate(a,22); const uint32_t majority=(a&b)^(a&c)^(b&c); const uint32_t t2=s0+majority; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    void update(const unsigned char* bytes, size_t count) { length += count; while (count > 0) { const uint32_t take = std::min<uint32_t>(static_cast<uint32_t>(count), 64u - used); std::copy(bytes, bytes + take, buffer + used); used += take; bytes += take; count -= take; if (used == 64) { transform(buffer); used = 0; } } }
    std::string finish() { const uint64_t bitLength = length * 8u; buffer[used++] = 0x80; while (used != 56) { if (used == 64) { transform(buffer); used = 0; } buffer[used++] = 0; } for (int i = 7; i >= 0; --i) buffer[used++] = static_cast<unsigned char>(bitLength >> (i * 8)); transform(buffer); std::ostringstream output; output << std::hex << std::setfill('0'); for (uint32_t value : state) output << std::setw(8) << value; return output.str(); }
};

struct CorrectSha256 {
    uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint64_t length = 0;
    unsigned char buffer[64] = {};
    uint32_t used = 0;
    static uint32_t rotate(uint32_t value, uint32_t count) { return (value >> count) | (value << (32u - count)); }
    static uint32_t word(const unsigned char* bytes) { return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) | (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3]; }
    void transform(const unsigned char* bytes) {
        static const uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        uint32_t words[64] = {};
        for (uint32_t i = 0; i < 16; ++i) words[i] = word(bytes + i * 4);
        for (uint32_t i = 16; i < 64; ++i) { const uint32_t s0 = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^ (words[i - 15] >> 3); const uint32_t s1 = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^ (words[i - 2] >> 10); words[i] = words[i - 16] + s0 + words[i - 7] + s1; }
        uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (uint32_t i = 0; i < 64; ++i) { const uint32_t s1=rotate(e,6)^rotate(e,11)^rotate(e,25); const uint32_t choose=(e&f)^((~e)&g); const uint32_t t1=h+s1+choose+k[i]+words[i]; const uint32_t s0=rotate(a,2)^rotate(a,13)^rotate(a,22); const uint32_t majority=(a&b)^(a&c)^(b&c); const uint32_t t2=s0+majority; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    void update(const unsigned char* bytes, size_t count) { length += count; while (count > 0) { const uint32_t take = std::min<uint32_t>(static_cast<uint32_t>(count), 64u - used); std::copy(bytes, bytes + take, buffer + used); used += take; bytes += take; count -= take; if (used == 64) { transform(buffer); used = 0; } } }
    std::string finish() { const uint64_t bitLength = length * 8u; buffer[used++] = 0x80; while (used != 56) { if (used == 64) { transform(buffer); used = 0; } buffer[used++] = 0; } for (int i = 7; i >= 0; --i) buffer[used++] = static_cast<unsigned char>(bitLength >> (i * 8)); transform(buffer); std::ostringstream output; output << std::hex << std::setfill('0'); for (uint32_t value : state) output << std::setw(8) << value; return output.str(); }
};

bool readArtifact(const std::filesystem::path& path, std::vector<uint8_t>& bytes) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) return false;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxArtifactBytes) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.resize(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return static_cast<bool>(input);
}

std::string sha256(const std::vector<uint8_t>& bytes) {
    CorrectSha256 hash;
    if (!bytes.empty()) hash.update(bytes.data(), bytes.size());
    return hash.finish();
}

bool containsEntryPoint(const std::vector<uint8_t>& bytes) {
    static const unsigned char marker[] = {'g','x','_','m','a','i','n','\0'};
    return std::search(bytes.begin(), bytes.end(), marker, marker + sizeof(marker)) != bytes.end();
}

bool hasOnlyNativeGuiPermissions(const AppManifest& manifest) {
    if (manifest.permissions.size() != 3) return false;
    return std::find(manifest.permissions.begin(), manifest.permissions.end(), "log") != manifest.permissions.end() &&
        std::find(manifest.permissions.begin(), manifest.permissions.end(), "window") != manifest.permissions.end() &&
        std::find(manifest.permissions.begin(), manifest.permissions.end(), "draw") != manifest.permissions.end();
}

bool validateProjectAndArtifact(const gx_development_run_request& request, Deployment& deployment, gx_development_run_error_code& error, std::string& message) {
    if (!request.projectRoot || !request.projectId || !request.projectKind || !request.targetProfile || !request.manifestPath || !request.artifactPath || !request.artifactSha256) {
        error = GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST;
        message = errorName(error);
        return false;
    }
    deployment.projectRoot = request.projectRoot;
    deployment.projectId = request.projectId;
    deployment.manifestPath = request.manifestPath;
    deployment.artifactPath = request.artifactPath;
    deployment.artifactSha256 = request.artifactSha256;
    if (deployment.projectRoot.size() >= GX_DEVELOPMENT_RUN_MAX_PROJECT_ROOT_BYTES || deployment.projectId.size() >= GX_DEVELOPMENT_RUN_MAX_PROJECT_ID_BYTES || deployment.manifestPath.size() >= GX_DEVELOPMENT_RUN_MAX_PATH_BYTES || deployment.artifactPath.size() >= GX_DEVELOPMENT_RUN_MAX_PATH_BYTES || deployment.artifactSha256.size() != 64) {
        error = GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST;
        message = errorName(error);
        return false;
    }
    if (std::string(request.projectKind) != kProjectKind) { error = GX_DEVELOPMENT_RUN_ERROR_UNSUPPORTED_PROJECT; message = errorName(error); return false; }
    if (std::string(request.targetProfile) != kTargetProfile) { error = GX_DEVELOPMENT_RUN_ERROR_UNSUPPORTED_TARGET; message = errorName(error); return false; }
    if (!isValidApplicationId(deployment.projectId) || deployment.manifestPath != kManifestPath || !isSafeRelativePath(deployment.artifactPath)) { error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID; message = errorName(error); return false; }

    const std::filesystem::path root(deployment.projectRoot);
    if (!isAbsoluteSafeRoot(root) || hasSymlinkComponent(root)) { error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID; message = errorName(error); return false; }
    std::string metadata;
    if (!readBoundedFile(root / "guidexos.project", kMaxManifestBytes, metadata)) { error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID; message = errorName(error); return false; }
    uint32_t formatVersion = 0;
    std::string metadataId, metadataKind, metadataManifest, metadataTarget, metadataEntry, metadataAbi, metadataArchitecture, outputName;
    if (!extractJsonNumber(metadata, "formatVersion", formatVersion) || formatVersion != 1 ||
        !extractJsonString(metadata, "projectId", metadataId) || !extractJsonString(metadata, "projectKind", metadataKind) ||
        !extractJsonString(metadata, "applicationManifest", metadataManifest) || !extractJsonString(metadata, "defaultTargetProfile", metadataTarget) ||
        !extractJsonString(metadata, "entryPoint", metadataEntry) || !extractJsonString(metadata, "abi", metadataAbi) ||
        !extractJsonString(metadata, "architecture", metadataArchitecture) || !extractJsonString(metadata, "outputName", outputName) ||
        metadataId != deployment.projectId || metadataKind != kProjectKind || metadataManifest != kManifestPath || metadataTarget != kTargetProfile ||
        metadataEntry != "gx_main" || metadataAbi != kAbi || metadataArchitecture != kArchitecture || outputName.empty()) {
        error = GX_DEVELOPMENT_RUN_ERROR_PROJECT_INVALID;
        message = errorName(error);
        return false;
    }

    const std::filesystem::path manifestPath = root / std::filesystem::path(deployment.manifestPath);
    if (!isContainedPath(root, manifestPath) || hasSymlinkComponent(manifestPath)) { error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISSING; message = errorName(error); return false; }
    std::string manifestBytes;
    if (!readBoundedFile(manifestPath, kMaxManifestBytes, manifestBytes)) { error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISSING; message = errorName(error); return false; }
    AppManifestLoadResult manifestLoad = AppManifestLoader::LoadFromFile(manifestPath);
    if (!manifestLoad.valid) { error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MALFORMED; message = errorName(error); return false; }
    const AppManifest& manifest = manifestLoad.manifest;
    if (manifest.id != deployment.projectId || manifest.displayName.empty() || manifest.kind != AppKind::NativeElf ||
        manifest.supportedArchitectures.size() != 1 || manifest.supportedArchitectures[0] != kArchitecture || manifest.entries.size() != 1 ||
        manifest.entries[0].architecture != kArchitecture || manifest.entries[0].entryPoint != "gx_main" || manifest.entries[0].abi != kAbi ||
        manifest.entries[0].runtime != "native-elf" || manifest.entries[0].path.empty() || !isSafeRelativePath(manifest.entries[0].path) ||
        !hasOnlyNativeGuiPermissions(manifest) || !manifest.fileAssociations.empty() || !manifest.desktopRegistryHints.empty()) {
        error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISMATCH;
        message = errorName(error);
        return false;
    }
    deployment.displayName = manifest.displayName;
    deployment.applicationId = manifest.id;
    const std::string expectedArtifact = std::string("build/") + manifest.entries[0].path;
    if (deployment.artifactPath != expectedArtifact) { error = GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MISMATCH; message = errorName(error); return false; }

    const std::filesystem::path artifactPath = root / std::filesystem::path(deployment.artifactPath);
    if (!isContainedPath(root, artifactPath) || hasSymlinkComponent(artifactPath)) { error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID; message = errorName(error); return false; }
    std::vector<uint8_t> bytes;
    if (!readArtifact(artifactPath, bytes)) { error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_MISSING; message = errorName(error); return false; }
    const std::string actualHash = sha256(bytes);
    if (actualHash != deployment.artifactSha256) { error = GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_CHANGED; message = errorName(error); return false; }
    const ElfValidationResult validation = ElfValidator::Validate(bytes, kArchitecture);
    if (!validation.valid || validation.elfClass != "ELF64" || validation.elfType != "ET_EXEC" || validation.architecture != kArchitecture) {
        error = validation.architecture != kArchitecture ? GX_DEVELOPMENT_RUN_ERROR_ARCHITECTURE_MISMATCH : GX_DEVELOPMENT_RUN_ERROR_ARTIFACT_INVALID;
        message = errorName(error);
        return false;
    }
    if (!containsEntryPoint(bytes)) { error = GX_DEVELOPMENT_RUN_ERROR_ENTRY_POINT_MISSING; message = errorName(error); return false; }

    deployment.state = GX_DEVELOPMENT_RUN_VALIDATING;
    return true;
}

void closeOwnedWindows(uint64_t processId) {
    if (processId == 0) return;
    const auto windows = gui::Compositor::debugWindowsSnapshot();
    for (const auto& window : windows) {
        if (window.ownerPid != processId) continue;
        ipc::Message close;
        close.srcPid = processId;
        close.type = static_cast<uint32_t>(gui::MsgType::MT_Close);
        const std::string payload = std::to_string(window.id);
        close.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(close), false);
    }
}

uint64_t findNativeRuntimeId(uint64_t processId) {
    if (processId == 0) return 0;
    const std::vector<NativeAppProcessInfo> processes = NativeAppProcessTable::List();
    for (const NativeAppProcessInfo& process : processes) {
        if (process.processId == processId) return process.runtimeId;
    }
    return 0;
}

void refreshWindowCounts(Deployment& deployment) {
    deployment.windowCount = 0;
    if (deployment.processId == 0) return;
    for (const auto& window : gui::Compositor::debugWindowsSnapshot()) {
        if (window.ownerPid == deployment.processId) ++deployment.windowCount;
    }
    if (deployment.windowCount > deployment.createdWindowCount) deployment.createdWindowCount = deployment.windowCount;
}

void unregisterDeployment(Deployment& deployment) {
    if (!deployment.appModelRegistered) return;
    gui::DesktopService::UnregisterDevelopmentApp(deployment.applicationId, deployment.ownerRuntimeId, deployment.generation);
    deployment.appModelRegistered = false;
}

} // namespace

gx_result Prepare(NativeAppRuntimeContext& owner, const gx_development_run_request& request,
                  gx_development_run_handle* outHandle, gx_development_run_snapshot* outSnapshot) {
    if (outHandle) *outHandle = 0;
    clearSnapshot(outSnapshot);
    const size_t requiredRequestBytes = offsetof(gx_development_run_request, artifactSha256) + sizeof(const char*);
    if (!outHandle || !outSnapshot || request.size < requiredRequestBytes || request.version != GX_DEVELOPMENT_RUN_API_VERSION || owner.runtimeId == 0 || owner.appId != kOwnerAppId) {
        setFailure(outSnapshot, owner.appId == kOwnerAppId ? GX_DEVELOPMENT_RUN_ERROR_INVALID_REQUEST : GX_DEVELOPMENT_RUN_ERROR_OWNER_NOT_ALLOWED, "development Run is hosted-only and owner-bound");
        return GX_OK;
    }

    Deployment candidate;
    candidate.ownerRuntimeId = owner.runtimeId;
    candidate.debugControlled = request.size >= sizeof(gx_development_run_request) && (request.flags & GX_DEVELOPMENT_RUN_FLAG_DEBUG_CONTROLLED) != 0;
    candidate.projectId = request.projectId ? request.projectId : std::string();
    candidate.applicationId = candidate.projectId;
    gx_development_run_error_code error = GX_DEVELOPMENT_RUN_ERROR_NONE;
    std::string message;
    if (!validateProjectAndArtifact(request, candidate, error, message)) {
        setFailure(outSnapshot, error, message);
        Logger::write(LogLevel::Warn, std::string("[DevelopmentRun] prepare failed reason=") + errorName(error));
        return GX_OK;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (gui::DesktopService::IsInstalledAppId(candidate.applicationId)) {
        setFailure(outSnapshot, GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_INSTALLED, errorName(GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_INSTALLED));
        return GX_OK;
    }
    for (const Slot& slot : g_slots) {
        if (slot.used && slot.deployment.applicationId == candidate.applicationId) {
            setFailure(outSnapshot, slot.deployment.ownerRuntimeId == owner.runtimeId ? GX_DEVELOPMENT_RUN_ERROR_DEPLOYMENT_ALREADY_ACTIVE : GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_IN_USE, errorName(slot.deployment.ownerRuntimeId == owner.runtimeId ? GX_DEVELOPMENT_RUN_ERROR_DEPLOYMENT_ALREADY_ACTIVE : GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_IN_USE));
            return GX_OK;
        }
    }

    uint32_t slotIndex = kMaxDeployments;
    for (uint32_t i = 0; i < kMaxDeployments; ++i) if (!g_slots[i].used) { slotIndex = i; break; }
    if (slotIndex == kMaxDeployments) {
        setFailure(outSnapshot, GX_DEVELOPMENT_RUN_ERROR_SERVICE_UNAVAILABLE, "development deployment limit reached");
        return GX_OK;
    }

    Slot& slot = g_slots[slotIndex];
    candidate.slot = slotIndex;
    candidate.generation = slot.generation == 0 ? 1 : slot.generation;
    candidate.handle = makeHandle(slotIndex, candidate.generation);
    candidate.state = GX_DEVELOPMENT_RUN_REGISTERED;
    candidate.error = GX_DEVELOPMENT_RUN_ERROR_NONE;
    candidate.errorMessage.clear();
    RegisteredApp temporary;
    temporary.manifestPath = std::filesystem::path(candidate.projectRoot) / candidate.manifestPath;
    temporary.appDirectory = std::filesystem::path(candidate.projectRoot) / "build";
    temporary.sourceKind = AppSourceKind::DevelopmentTemporary;
    temporary.temporaryDevelopment = true;
    temporary.temporaryOwnerRuntimeId = owner.runtimeId;
    temporary.temporaryGeneration = candidate.generation;
    AppManifestLoadResult loaded = AppManifestLoader::LoadFromFile(temporary.manifestPath);
    if (!loaded.valid) {
        setFailure(outSnapshot, GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MALFORMED, errorName(GX_DEVELOPMENT_RUN_ERROR_MANIFEST_MALFORMED));
        return GX_OK;
    }
    temporary.manifest = loaded.manifest;
    std::string registrationError;
    if (!gui::DesktopService::RegisterDevelopmentApp(temporary, registrationError)) {
        const gx_development_run_error_code registrationCode = registrationError == "APPLICATION_ID_INSTALLED" ? GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_INSTALLED :
            (registrationError == "APPLICATION_ID_IN_USE" ? GX_DEVELOPMENT_RUN_ERROR_APPLICATION_ID_IN_USE : GX_DEVELOPMENT_RUN_ERROR_DEPLOYMENT_ALREADY_ACTIVE);
        setFailure(outSnapshot, registrationCode, registrationError);
        return GX_OK;
    }
    candidate.appModelRegistered = true;
    slot.deployment = candidate;
    slot.used = true;
    *outHandle = candidate.handle;
    setSnapshotFromDeployment(slot.deployment, outSnapshot);
    Logger::write(LogLevel::Info, "[DevelopmentRun] deployment prepared appId=" + candidate.applicationId + " handle=" + std::to_string(candidate.handle));
    return GX_OK;
}

gx_result Start(NativeAppRuntimeContext& owner, gx_development_run_handle handle) {
    std::string appId;
    uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot* slot = findOwnedLocked(handle, owner.runtimeId);
        if (!slot) {
            Logger::write(LogLevel::Warn, "[DevelopmentRun] start rejected stale-or-owner-mismatched handle=" + std::to_string(handle) + " ownerRuntimeId=" + std::to_string(owner.runtimeId));
            return GX_ERROR_FAILED;
        }
        if (slot->deployment.state != GX_DEVELOPMENT_RUN_REGISTERED) {
            Logger::write(LogLevel::Warn, "[DevelopmentRun] start rejected state=" + std::to_string(static_cast<uint32_t>(slot->deployment.state)) +
                " handle=" + std::to_string(handle) + " ownerRuntimeId=" + std::to_string(owner.runtimeId));
            return GX_ERROR_FAILED;
        }
        slot->deployment.state = GX_DEVELOPMENT_RUN_LAUNCHING;
        appId = slot->deployment.applicationId;
        generation = slot->deployment.generation;
    }

    std::string error;
    uint64_t processId = 0;
    bool debugControlled = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot* slot = findOwnedLocked(handle, owner.runtimeId);
        if (slot) debugControlled = slot->deployment.debugControlled;
        else Logger::write(LogLevel::Warn, "[DevelopmentRun] start lost deployment before launch handle=" + std::to_string(handle) +
            " ownerRuntimeId=" + std::to_string(owner.runtimeId));
    }
    if (!gui::DesktopService::LaunchDevelopmentApp(appId, owner.runtimeId, generation, debugControlled, error, processId)) {
        Logger::write(LogLevel::Warn, "[DevelopmentRun] launch failed appId=" + appId + " reason=" + error);
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot* slot = findOwnedLocked(handle, owner.runtimeId);
        if (slot) {
            slot->deployment.state = GX_DEVELOPMENT_RUN_FAILED;
            slot->deployment.error = error == "LAUNCH_UNAVAILABLE" ? GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE : GX_DEVELOPMENT_RUN_ERROR_LAUNCH_FAILED;
            slot->deployment.errorMessage = error;
            slot->deployment.cleanupComplete = true;
            unregisterDeployment(slot->deployment);
        }
        return GX_OK;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    Slot* slot = findOwnedLocked(handle, owner.runtimeId);
    if (!slot) {
        Logger::write(LogLevel::Warn, "[DevelopmentRun] start lost deployment handle=" + std::to_string(handle) + " ownerRuntimeId=" + std::to_string(owner.runtimeId));
        return GX_ERROR_FAILED;
    }
    slot->deployment.processId = processId;
    if (slot->deployment.closeRequested) closeOwnedWindows(processId);
    return GX_OK;
}

gx_result Debug(NativeAppRuntimeContext& owner, const gx_development_debug_request& request,
                gx_development_debug_snapshot* outSnapshot) {
    if (!outSnapshot) return GX_ERROR_INVALID_ARGUMENT;
    std::string expectedArtifact;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot* slot = findOwnedLocked(request.handle, owner.runtimeId);
        if (!slot) return GX_ERROR_FAILED;
        if (!slot->deployment.debugControlled) return GX_ERROR_UNSUPPORTED;
        if (slot->deployment.processId != request.processId ||
            slot->deployment.nativeRuntimeId == 0 || slot->deployment.nativeRuntimeId != request.nativeRuntimeId) return GX_ERROR_FAILED;
        expectedArtifact = slot->deployment.artifactSha256;
    }
    const gx_result result = NativeAppDebugger::Command(request, expectedArtifact, outSnapshot);
    if (result == GX_OK && request.command == GX_DEVELOPMENT_DEBUG_RELEASE_EXECUTION) {
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot* slot = findOwnedLocked(request.handle, owner.runtimeId);
        if (slot) slot->deployment.debugExecutionReleased = true;
    }
    return result;
}

gx_result Poll(NativeAppRuntimeContext& owner, gx_development_run_handle handle, gx_development_run_snapshot* outSnapshot) {
    if (!outSnapshot) return GX_ERROR_INVALID_ARGUMENT;
    clearSnapshot(outSnapshot);
    uint64_t processId = 0;
    bool closeRequested = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot* slot = findOwnedLocked(handle, owner.runtimeId);
        if (!slot) {
            setFailure(outSnapshot, GX_DEVELOPMENT_RUN_ERROR_STALE_DEPLOYMENT, errorName(GX_DEVELOPMENT_RUN_ERROR_STALE_DEPLOYMENT));
            return GX_ERROR_FAILED;
        }
        if (slot->deployment.state == GX_DEVELOPMENT_RUN_REGISTERED || slot->deployment.state == GX_DEVELOPMENT_RUN_FAILED || slot->deployment.state == GX_DEVELOPMENT_RUN_COMPLETED) {
            setSnapshotFromDeployment(slot->deployment, outSnapshot);
            return GX_OK;
        }
        processId = slot->deployment.processId;
        closeRequested = slot->deployment.closeRequested;
    }

    bool running = false;
    int exitCode = 0;
    const bool statusAvailable = processId != 0 && ProcessTable::getStatus(processId, running, exitCode);
    if (!statusAvailable) running = false;
    if (closeRequested) closeOwnedWindows(processId);

    std::lock_guard<std::mutex> lock(g_mutex);
    Slot* slot = findOwnedLocked(handle, owner.runtimeId);
    if (!slot) {
        setFailure(outSnapshot, GX_DEVELOPMENT_RUN_ERROR_STALE_DEPLOYMENT, errorName(GX_DEVELOPMENT_RUN_ERROR_STALE_DEPLOYMENT));
        return GX_ERROR_FAILED;
    }
    refreshWindowCounts(slot->deployment);
    const uint64_t previousNativeRuntimeId = slot->deployment.nativeRuntimeId;
    slot->deployment.nativeRuntimeId = findNativeRuntimeId(processId);
    if (previousNativeRuntimeId == 0 && slot->deployment.nativeRuntimeId != 0) {
        Logger::write(LogLevel::Info, "[DevelopmentRun] target-created appId=" + slot->deployment.applicationId +
            " handle=" + std::to_string(slot->deployment.handle) + " processId=" + std::to_string(processId) +
            " nativeRuntimeId=" + std::to_string(slot->deployment.nativeRuntimeId) + " gate=closed");
    }
    if (!running) {
        slot->deployment.exitCode = exitCode;
        slot->deployment.state = GX_DEVELOPMENT_RUN_CLEANING_UP;
        unregisterDeployment(slot->deployment);
        slot->deployment.cleanupComplete = true;
        slot->deployment.error = exitCode == GX_OK ? GX_DEVELOPMENT_RUN_ERROR_NONE : GX_DEVELOPMENT_RUN_ERROR_LAUNCH_FAILED;
        slot->deployment.errorMessage = exitCode == GX_OK ? std::string() : "native application exited with failure";
        slot->deployment.state = exitCode == GX_OK ? GX_DEVELOPMENT_RUN_COMPLETED : GX_DEVELOPMENT_RUN_FAILED;
        Logger::write(LogLevel::Info, "[DevelopmentRun] application exited appId=" + slot->deployment.applicationId + " exitCode=" + std::to_string(exitCode) + " cleanup=PASS");
    } else if (slot->deployment.windowCount > 0 || (slot->deployment.debugControlled &&
                                                        slot->deployment.debugExecutionReleased &&
                                                        slot->deployment.nativeRuntimeId != 0)) {
        slot->deployment.state = GX_DEVELOPMENT_RUN_RUNNING;
    } else {
        slot->deployment.state = GX_DEVELOPMENT_RUN_LAUNCHING;
    }
    setSnapshotFromDeployment(slot->deployment, outSnapshot);
    return GX_OK;
}

gx_result RequestClose(NativeAppRuntimeContext& owner, gx_development_run_handle handle) {
    uint64_t processId = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Slot* slot = findOwnedLocked(handle, owner.runtimeId);
        if (!slot) return GX_ERROR_FAILED;
        if (slot->deployment.state == GX_DEVELOPMENT_RUN_COMPLETED || slot->deployment.state == GX_DEVELOPMENT_RUN_FAILED) return GX_OK;
        slot->deployment.closeRequested = true;
        processId = slot->deployment.processId;
    }
    closeOwnedWindows(processId);
    NativeAppDebugger::CancelProcess(processId);
    return GX_OK;
}

gx_result Release(NativeAppRuntimeContext& owner, gx_development_run_handle handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Slot* slot = findOwnedLocked(handle, owner.runtimeId);
    if (!slot) return GX_ERROR_FAILED;
    if (slot->deployment.state != GX_DEVELOPMENT_RUN_COMPLETED && slot->deployment.state != GX_DEVELOPMENT_RUN_FAILED) return GX_ERROR_BUSY;
    unregisterDeployment(slot->deployment);
    slot->used = false;
    slot->deployment = Deployment();
    ++slot->generation;
    if (slot->generation == 0) slot->generation = 1;
    return GX_OK;
}

void ReleaseOwner(uint64_t ownerRuntimeId) {
    if (ownerRuntimeId == 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Slot& slot : g_slots) {
        if (!slot.used || slot.deployment.ownerRuntimeId != ownerRuntimeId) continue;
        closeOwnedWindows(slot.deployment.processId);
        NativeAppDebugger::CancelProcess(slot.deployment.processId);
        unregisterDeployment(slot.deployment);
        slot.used = false;
        slot.deployment = Deployment();
        ++slot.generation;
        if (slot.generation == 0) slot.generation = 1;
    }
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Slot& slot : g_slots) {
        if (!slot.used) continue;
        closeOwnedWindows(slot.deployment.processId);
        NativeAppDebugger::CancelProcess(slot.deployment.processId);
        unregisterDeployment(slot.deployment);
        slot.used = false;
        slot.deployment = Deployment();
        ++slot.generation;
        if (slot.generation == 0) slot.generation = 1;
    }
}

} // namespace DevelopmentRunService
} // namespace apps
} // namespace gxos
