#include "core/compiler/compiler_build_service.h"
#include "core/compiler/elf_writer.h"
#include "kernel/vfs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
struct Node {
    bool directory;
    std::vector<uint8_t> bytes;
};
struct Handle {
    bool open;
    std::string path;
    size_t position;
};

std::map<std::string, Node> files;
Handle handles[32] = {};
std::string failWritePath;

std::string parent(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    return slash == 0 ? "/" : path.substr(0, slash);
}

bool put(const char* path, const char* value)
{
    if (!path || !value) return false;
    Node node = {};
    node.directory = false;
    node.bytes.assign(value, value + std::strlen(value));
    files[path] = node;
    return true;
}

std::string text(const char* path)
{
    const auto found = files.find(path);
    return found == files.end() || found->second.directory ? std::string() :
        std::string(found->second.bytes.begin(), found->second.bytes.end());
}

bool check(bool condition, const char* name)
{
    if (condition) return true;
    std::fprintf(stderr, "compiler_build_service_host_test: FAIL: %s\n", name);
    return false;
}

gx_build_request request(const char* expected = "build/bin/arm64/app.elf")
{
    gx_build_request value = {};
    value.size = sizeof(value);
    value.version = GX_BUILD_API_VERSION;
    value.projectRoot = "/Workspace";
    value.projectId = "com.example.buildservice";
    value.projectKind = "native-gui-application";
    value.targetProfile = "guidexos.multi.baremetal.bootstrap.native";
    value.buildSystem = "guidexos-native-baremetal-bootstrap-v1";
    value.buildScript = "build.ps1";
    value.expectedArtifact = expected;
    value.configuration = "Debug";
    return value;
}

bool run(const gx_build_request& req, gx_build_snapshot* snapshot)
{
    gx_build_handle handle = 0;
    if (kernel::compiler::BareMetalBuildService::start(&req, &handle) != GX_OK || handle == 0) return false;
    gx_build_snapshot concurrent = {};
    gx_build_handle rejectedHandle = UINT64_MAX;
    const gx_build_request busyRequest = request();
    const bool busyRejected = kernel::compiler::BareMetalBuildService::start(&busyRequest, &rejectedHandle) == GX_ERROR_BUSY &&
        rejectedHandle == 0;
    snapshot->size = sizeof(*snapshot);
    snapshot->version = GX_BUILD_API_VERSION;
    const bool polled = kernel::compiler::BareMetalBuildService::poll(handle, snapshot) == GX_OK;
    const bool released = kernel::compiler::BareMetalBuildService::release(handle) == GX_OK;
    const bool staleHandleRejected = kernel::compiler::BareMetalBuildService::poll(handle, &concurrent) == GX_ERROR_INVALID_ARGUMENT;
    return busyRejected && polled && released && staleHandleRejected;
}

bool has_output(const gx_build_snapshot& snapshot, const char* needle)
{
    for (uint32_t i = 0; i < snapshot.outputCount; ++i)
        if (std::strstr(snapshot.output[i].text, needle)) return true;
    return false;
}

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + 4 > bytes.size()) return 0;
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint64_t read_u64(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint64_t>(read_u32(bytes, offset)) |
        (static_cast<uint64_t>(read_u32(bytes, offset + 4)) << 32);
}
} // namespace

extern "C" void phase3_serial_init() {}
extern "C" void phase3_serial_putc(char) {}
extern "C" void phase3_serial_print(const char*) {}
extern "C" void phase3_serial_hex(uint64_t) {}

namespace kernel { namespace vfs {
void init() {}
uint8_t mount(const char*, uint8_t) { return 0xff; }
uint8_t mount_partition(const char*, uint8_t, uint8_t) { return 0xff; }
uint8_t mount_type(const char*, uint8_t, FSType) { return 0xff; }
uint8_t mount_alias(const char*, const char*) { return 0xff; }
Status unmount(const char*) { return VFS_ERR_NOT_SUPPORTED; }
const MountPoint* get_mount(const char*) { return nullptr; }
uint8_t mount_index_for_path(const char*) { return 0xff; }
const MountPoint* get_mount_by_index(uint8_t) { return nullptr; }
uint8_t mount_count() { return 0; }
void normalize_path(const char* input, char* output, size_t capacity)
{
    if (!output || capacity == 0) return;
    std::snprintf(output, capacity, "%s", input ? input : "");
}
void parent_path(const char* path, char* output, size_t capacity)
{
    if (!output || capacity == 0) return;
    std::snprintf(output, capacity, "%s", path ? parent(path).c_str() : "");
}
const char* basename(const char* path)
{
    if (!path) return "";
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}
bool is_absolute(const char* path) { return path && path[0] == '/'; }
void join_path(const char* base, const char* name, char* output, size_t capacity)
{
    if (output && capacity) std::snprintf(output, capacity, "%s/%s", base ? base : "", name ? name : "");
}
uint8_t open(const char* path, uint16_t)
{
    const auto found = path ? files.find(path) : files.end();
    if (found == files.end() || found->second.directory) return 0xff;
    for (uint8_t i = 0; i < 32; ++i) if (!handles[i].open) {
        handles[i].open = true; handles[i].path = path; handles[i].position = 0; return i;
    }
    return 0xff;
}
Status close(uint8_t handle)
{
    if (handle >= 32 || !handles[handle].open) return VFS_ERR_INVALID;
    handles[handle] = {};
    return VFS_OK;
}
int32_t read(uint8_t handle, void* buffer, uint32_t capacity)
{
    if (handle >= 32 || !handles[handle].open || (!buffer && capacity)) return VFS_ERR_INVALID;
    const auto found = files.find(handles[handle].path);
    if (found == files.end() || found->second.directory) return VFS_ERR_NOT_FOUND;
    const size_t available = found->second.bytes.size() - handles[handle].position;
    const size_t count = available < capacity ? available : capacity;
    if (count) std::memcpy(buffer, found->second.bytes.data() + handles[handle].position, count);
    handles[handle].position += count;
    return static_cast<int32_t>(count);
}
int32_t write(uint8_t, const void*, uint32_t) { return VFS_ERR_NOT_SUPPORTED; }
Status seek(uint8_t handle, int64_t offset, SeekOrigin origin)
{
    if (handle >= 32 || !handles[handle].open) return VFS_ERR_INVALID;
    const auto found = files.find(handles[handle].path);
    if (found == files.end()) return VFS_ERR_NOT_FOUND;
    int64_t position = origin == SEEK_END ? static_cast<int64_t>(found->second.bytes.size()) :
        (origin == SEEK_CUR ? static_cast<int64_t>(handles[handle].position) : 0);
    position += offset;
    if (position < 0 || static_cast<uint64_t>(position) > found->second.bytes.size()) return VFS_ERR_INVALID;
    handles[handle].position = static_cast<size_t>(position);
    return VFS_OK;
}
int64_t tell(uint8_t handle) { return handle < 32 && handles[handle].open ? static_cast<int64_t>(handles[handle].position) : -1; }
int64_t file_size(uint8_t handle)
{
    if (handle >= 32 || !handles[handle].open) return -1;
    const auto found = files.find(handles[handle].path);
    return found == files.end() ? -1 : static_cast<int64_t>(found->second.bytes.size());
}
Status flush(uint8_t) { return VFS_OK; }
const FileHandle* get_handle(uint8_t) { return nullptr; }
uint8_t opendir(const char*) { return 0xff; }
bool readdir(uint8_t, DirEntry*) { return false; }
void closedir(uint8_t) {}
Status mkdir(const char* path)
{
    if (!path || files.find(path) != files.end()) return VFS_ERR_EXISTS;
    if (files.find(parent(path)) == files.end()) return VFS_ERR_NOT_FOUND;
    Node node = {}; node.directory = true; files[path] = node;
    return VFS_OK;
}
Status rmdir(const char*) { return VFS_ERR_NOT_SUPPORTED; }
bool exists(const char* path) { return path && files.find(path) != files.end(); }
Status stat(const char* path, FileInfo* info)
{
    if (!path || !info) return VFS_ERR_INVALID;
    const auto found = files.find(path);
    if (found == files.end()) return VFS_ERR_NOT_FOUND;
    *info = {};
    std::snprintf(info->name, sizeof(info->name), "%s", kernel::vfs::basename(path));
    info->type = found->second.directory ? FILE_TYPE_DIRECTORY : FILE_TYPE_REGULAR;
    info->size = found->second.bytes.size();
    return VFS_OK;
}
Status unlink(const char* path)
{
    if (!path) return VFS_ERR_INVALID;
    const auto found = files.find(path);
    if (found == files.end()) return VFS_ERR_NOT_FOUND;
    if (found->second.directory) return VFS_ERR_IS_DIR;
    files.erase(found);
    return VFS_OK;
}
Status rename(const char*, const char*) { return VFS_ERR_NOT_SUPPORTED; }
int32_t read_file(const char* path, void* buffer, uint32_t capacity)
{
    if (!path || (!buffer && capacity)) return VFS_ERR_INVALID;
    const auto found = files.find(path);
    if (found == files.end() || found->second.directory) return VFS_ERR_NOT_FOUND;
    const size_t count = found->second.bytes.size() < capacity ? found->second.bytes.size() : capacity;
    if (count) std::memcpy(buffer, found->second.bytes.data(), count);
    return static_cast<int32_t>(count);
}
int32_t write_file(const char* path, const void* buffer, uint32_t bytes)
{
    if (!path || (!buffer && bytes)) return VFS_ERR_INVALID;
    if (!failWritePath.empty() && failWritePath == path) {
        const uint32_t partial = bytes / 2;
        Node node = {}; node.directory = false;
        const uint8_t* data = static_cast<const uint8_t*>(buffer);
        node.bytes.assign(data, data + partial); files[path] = node;
        return VFS_ERR_IO;
    }
    Node node = {}; node.directory = false;
    const uint8_t* data = static_cast<const uint8_t*>(buffer);
    if (bytes) node.bytes.assign(data, data + bytes);
    files[path] = node;
    return static_cast<int32_t>(bytes);
}
int32_t create_file(const char*, const void*, uint32_t) { return VFS_ERR_NOT_SUPPORTED; }
int32_t append_file(const char*, const void*, uint32_t) { return VFS_ERR_NOT_SUPPORTED; }
uint64_t total_space(const char*) { return 0; }
uint64_t free_space(const char*) { return 0; }
const char* fs_type_name(FSType) { return "mock"; }
const char* status_name(Status) { return "mock"; }
} } // namespace kernel::vfs

int main()
{
    using namespace kernel::vfs;
    using kernel::compiler::BareMetalBuildService::start;
    files["/"] = { true, {} };
    files["/Apps"] = { true, {} };
    files["/Workspace"] = { true, {} };
    files["/Workspace/src"] = { true, {} };
    files["/Workspace/app"] = { true, {} };
    const char* project = "{\"projectId\":\"com.example.buildservice\",\"projectKind\":\"native-gui-application\",\"abi\":\"guidexos-c-abi-v1\",\"sourceRoot\":\"src\",\"sourceEntry\":\"main.c\",\"outputName\":\"app\",\"packageRoot\":\"/Apps/TestPkg\"}";
    const char* manifest = "{\"id\":\"com.example.buildservice\",\"kind\": \"NativeElf\",\"entries\":[{\"path\":\"bin/arm64/app.elf\"},{\"path\":\"bin/amd64/app.elf\"}]}";
    // Studio's FAT 8.3 adapter stores these two logical names under their
    // reserved aliases. The resident service must read the same files.
    put("/Workspace/GUIDEXOS.PJT", project);
    put("/Workspace/app/APP.JSN", manifest);
    put("/Workspace/src/main.c", "int gx_main(void* ctx) { return 42; }");
    put("/Workspace/main.c", "this is a decoy and must not be compiled");
    put("/Workspace/build/bin/arm64/app.elf", "stale-arm64");
    put("/Workspace/build/bin/amd64/app.elf", "stale-amd64");

    gx_build_snapshot result = {};
    if (!check(run(request(), &result), "valid build dispatch/poll/release")) return 1;
    if (!check(result.state == GX_BUILD_SUCCEEDED && result.artifactValid && result.artifactEntryPoint,
               "resident compiler reports a completed validated artifact")) return 2;
    if (!check(result.packageWritten && result.compiledModuleCount == 2 && result.linkedModuleCount == 2,
               "multi-target package publication")) return 3;
    if (!check(std::strlen(result.artifactSha256) == 64 && std::strspn(result.artifactSha256, "0123456789abcdef") == 64,
               "snapshot contains a SHA-256 digest")) return 4;
    const auto arm = files.find("/Workspace/build/bin/arm64/app.elf");
    if (!check(arm != files.end() && arm->second.bytes.size() >= 64, "ARM64 output created in selected source project")) return 5;
    const std::vector<uint8_t>& elf = arm->second.bytes;
    if (!check(elf[0] == 0x7f && elf[1] == 'E' && elf[2] == 'L' && elf[3] == 'F' &&
               elf[4] == 2 && elf[5] == 1 && elf[16] == 2 && elf[17] == 0 &&
               elf[18] == 183 && elf[19] == 0 && read_u64(elf, 24) == UINT64_C(0x51001000),
               "ARM64 ELF64 LE ET_EXEC identity and fixed entry")) return 6;
    if (!check(text("/Apps/TestPkg/app.json") == manifest && exists("/Apps/TestPkg/bin/arm64/app.elf") &&
               exists("/Apps/TestPkg/bin/amd64/app.elf"), "validated package VFS publication")) return 7;

    // Canonical logical files remain readable when present without aliases.
    const std::string projectBytes = text("/Workspace/GUIDEXOS.PJT");
    const std::string manifestBytes = text("/Workspace/app/APP.JSN");
    files.erase("/Workspace/GUIDEXOS.PJT");
    files.erase("/Workspace/app/APP.JSN");
    put("/Workspace/guidexos.project", projectBytes.c_str());
    put("/Workspace/app/app.json", manifestBytes.c_str());
    gx_build_snapshot canonical = {};
    if (!check(run(request(), &canonical) && canonical.state == GX_BUILD_SUCCEEDED,
               "canonical logical metadata works without FAT aliases")) return 23;

    // Presenting both forms is ambiguous and must fail closed.
    put("/Workspace/GUIDEXOS.PJT", projectBytes.c_str());
    gx_build_snapshot ambiguousProject = {};
    if (!check(run(request(), &ambiguousProject) && ambiguousProject.state == GX_BUILD_FAILED &&
               ambiguousProject.errorCode == GX_BUILD_ERROR_INVALID_PROJECT_ROOT,
               "canonical project metadata plus its short alias is rejected as ambiguous")) return 24;
    files.erase("/Workspace/guidexos.project");
    files.erase("/Workspace/app/app.json");
    put("/Workspace/app/APP.JSN", manifestBytes.c_str());
    put("/Workspace/app/app.json", manifestBytes.c_str());
    gx_build_snapshot ambiguousManifest = {};
    if (!check(run(request(), &ambiguousManifest) && ambiguousManifest.state == GX_BUILD_FAILED &&
               ambiguousManifest.errorCode == GX_BUILD_ERROR_INVALID_PROJECT_ROOT,
               "canonical application manifest plus its short alias is rejected as ambiguous")) return 25;
    files.erase("/Workspace/app/app.json");

    // Pinned Studio's new-project generator does not emit packageRoot for a
    // single-target workspace. That field is required only for multi-target
    // package publication, where the service writes both ELF payloads.
    const char* studioProject = "{\"projectId\":\"com.example.buildservice\",\"projectKind\":\"native-gui-application\",\"abi\":\"guidexos-c-abi-v1\",\"sourceRoot\":\"src\",\"sourceEntry\":\"main.c\",\"outputName\":\"r2b1b01-guidexos\"}";
    put("/Workspace/GUIDEXOS.PJT", studioProject);
    gx_build_request singleTargetRequest = request();
    singleTargetRequest.targetProfile = "guidexos.arm64.baremetal.bootstrap.native";
    singleTargetRequest.expectedArtifact = "build/bin/arm64/r2b1b01-guidexos.elf";
    char singleTargetStoragePath[256] = {};
    gx_build_snapshot singleTarget = {};
    if (!check(kernel::compiler::BareMetalBuildService::artifact_storage_path(
                   "/Workspace/build/bin/arm64/r2b1b01-guidexos.elf", singleTargetStoragePath,
                   sizeof(singleTargetStoragePath)) &&
               run(singleTargetRequest, &singleTarget) && singleTarget.state == GX_BUILD_SUCCEEDED &&
               singleTarget.artifactValid && !singleTarget.packageWritten &&
               exists(singleTargetStoragePath) &&
               std::strcmp(singleTarget.artifactPath, singleTargetRequest.expectedArtifact) == 0,
               "Studio long output name maps to FAT storage while preserving the logical artifact path")) return 26;
    char shortArtifactPath[128] = {};
    if (!check(kernel::compiler::BareMetalBuildService::artifact_storage_path(
                   "/Workspace/build/bin/arm64/app.elf", shortArtifactPath, sizeof(shortArtifactPath)) &&
               std::strcmp(shortArtifactPath, "/Workspace/build/bin/arm64/app.elf") == 0,
               "short ELF output names pass through unchanged")) return 28;
    gx_build_snapshot multiMissingPackageRoot = {};
    if (!check(run(request(), &multiMissingPackageRoot) && multiMissingPackageRoot.state == GX_BUILD_FAILED &&
               multiMissingPackageRoot.errorCode == GX_BUILD_ERROR_UNSUPPORTED_PROJECT,
               "multi-target package build still requires packageRoot")) return 27;
    put("/Workspace/GUIDEXOS.PJT", projectBytes.c_str());

    const std::string priorDigest = result.artifactSha256;

    put("/Workspace/src/main.c", "int gx_main(void* ctx) { return 41; }");
    gx_build_snapshot replaced = {};
    if (!check(run(request(), &replaced) && replaced.state == GX_BUILD_SUCCEEDED &&
               std::strcmp(priorDigest.c_str(), replaced.artifactSha256) != 0,
               "repeat build replaces old artifact and updates digest")) return 8;

    put("/Workspace/src/main.c", "int gx_main(void* ctx) { return missing_symbol; }");
    gx_build_snapshot syntax = {};
    if (!check(run(request(), &syntax) && syntax.state == GX_BUILD_FAILED &&
               syntax.errorCode == GX_BUILD_ERROR_COMPILER_FAILED && syntax.artifactValid == 0 &&
               has_output(syntax, "error: line") && !exists("/Workspace/build/bin/arm64/app.elf") &&
               !exists("/Workspace/build/bin/amd64/app.elf"),
               "syntax error propagates diagnostics and removes stale build outputs")) return 9;
    if (!check(exists("/Apps/TestPkg/bin/arm64/app.elf"), "failed rebuild preserves previous published package")) return 10;

    const char* projectMissingSource = "{\"projectId\":\"com.example.buildservice\",\"projectKind\":\"native-gui-application\",\"abi\":\"guidexos-c-abi-v1\",\"sourceRoot\":\"src\",\"sourceEntry\":\"missing.c\",\"outputName\":\"app\",\"packageRoot\":\"/Apps/TestPkg\"}";
    put("/Workspace/GUIDEXOS.PJT", projectMissingSource);
    gx_build_snapshot missing = {};
    if (!check(run(request(), &missing) && missing.state == GX_BUILD_FAILED &&
               missing.errorCode == GX_BUILD_ERROR_COMPILER_FAILED && !exists("/Workspace/build/bin/arm64/app.elf"),
               "missing source is a failed build with no stale output")) return 11;

    put("/Workspace/GUIDEXOS.PJT", project);
    put("/Workspace/src/main.c", "");
    gx_build_snapshot empty = {};
    if (!check(run(request(), &empty) && empty.state == GX_BUILD_FAILED &&
               empty.errorCode == GX_BUILD_ERROR_COMPILER_FAILED && has_output(empty, "error: line"),
               "empty source reports a compiler diagnostic")) return 12;

    put("/Workspace/src/main.c", "int gx_main(void* ctx) { return 42; }");
    gx_build_snapshot badPath = {};
    if (!check(run(request("build/bin/arm64/../app.elf"), &badPath) &&
               badPath.state == GX_BUILD_FAILED && badPath.errorCode == GX_BUILD_ERROR_INVALID_REQUEST,
               "traversal output path is rejected")) return 13;
    const char* badNameProject = "{\"projectId\":\"com.example.buildservice\",\"projectKind\":\"native-gui-application\",\"abi\":\"guidexos-c-abi-v1\",\"sourceRoot\":\"src\",\"sourceEntry\":\"main.c\",\"outputName\":\"../escape\",\"packageRoot\":\"/Apps/TestPkg\"}";
    put("/Workspace/GUIDEXOS.PJT", badNameProject);
    gx_build_snapshot badName = {};
    if (!check(run(request(), &badName) && badName.state == GX_BUILD_FAILED &&
               badName.errorCode == GX_BUILD_ERROR_UNSUPPORTED_PROJECT,
               "output-name path traversal is rejected")) return 14;

    put("/Workspace/GUIDEXOS.PJT", project);
    failWritePath = "/Workspace/build/bin/arm64/app.elf";
    gx_build_snapshot interrupted = {};
    const bool interruptionHandled = run(request(), &interrupted) && interrupted.state == GX_BUILD_FAILED &&
        interrupted.errorCode == GX_BUILD_ERROR_COMPILER_FAILED && !exists("/Workspace/build/bin/arm64/app.elf");
    failWritePath.clear();
    if (!check(interruptionHandled, "partial VFS write failure leaves no exposed partial ELF")) return 15;
    gx_build_snapshot recovered = {};
    if (!check(run(request(), &recovered) && recovered.state == GX_BUILD_SUCCEEDED,
               "subsequent build recovers after VFS interruption")) return 16;
    {
        const auto finalArtifact = files.find("/Workspace/build/bin/arm64/app.elf");
        if (finalArtifact == files.end()) return 21;
        std::ofstream exported("compiled-arm64.elf", std::ios::binary);
        exported.write(reinterpret_cast<const char*>(finalArtifact->second.bytes.data()),
                       static_cast<std::streamsize>(finalArtifact->second.bytes.size()));
        if (!exported) return 22;
        std::printf("compiler_build_service_host_test: artifact_sha256=%s\n", recovered.artifactSha256);
    }

    gx_build_request shortRequest = request();
    shortRequest.size = sizeof(shortRequest) - 1;
    gx_build_handle invalidHandle = UINT64_MAX;
    if (!check(start(&shortRequest, &invalidHandle) == GX_ERROR_INVALID_ARGUMENT && invalidHandle == 0,
               "request structure size is checked before dispatch")) return 17;
    gx_build_request escapedRoot = request();
    escapedRoot.projectRoot = "/Workspace/../outside";
    invalidHandle = UINT64_MAX;
    if (!check(start(&escapedRoot, &invalidHandle) == GX_OK && invalidHandle != 0,
               "bounded API start accepts request then reports path validation through poll")) return 18;
    gx_build_snapshot rootFailure = {};
    rootFailure.size = sizeof(rootFailure); rootFailure.version = GX_BUILD_API_VERSION;
    const gx_build_handle escapedHandle = invalidHandle;
    if (!check(kernel::compiler::BareMetalBuildService::poll(escapedHandle, &rootFailure) == GX_OK &&
               rootFailure.state == GX_BUILD_FAILED && rootFailure.errorCode == GX_BUILD_ERROR_INVALID_PROJECT_ROOT,
               "workspace root traversal is rejected by service")) return 19;
    if (!check(kernel::compiler::BareMetalBuildService::release(escapedHandle) == GX_OK,
               "failed request releases its result snapshot")) return 20;

    std::puts("compiler_build_service_host_test: PASS");
    return 0;
}
