#include "core/compiler/compiler_driver.h"
#include "kernel/vfs.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace kernel;
using namespace kernel::compiler;

namespace kernel { namespace vfs {
namespace {
struct OpenFile { std::string path; uint32_t position; };
std::map<std::string, std::vector<uint8_t> > g_files;
std::set<std::string> g_directories;
std::map<uint8_t, OpenFile> g_openFiles;
std::map<uint8_t, std::vector<DirEntry> > g_openDirectories;
uint8_t g_nextHandle = 1;
bool g_corruptFinalArtifact = false;
typedef int32_t (*CancelHook)(uint64_t, uint64_t);
std::string g_cancelOnReadPath;
std::string g_cancelOnRenameSource;
std::string g_failRenameSource;
std::string g_failRenameDestination;
uint64_t g_cancelOwner = 0;
uint64_t g_cancelHandle = 0;
CancelHook g_cancelHook = nullptr;
int32_t g_cancelHookResult = 0;
bool g_cancelHookFired = false;
}

uint8_t open(const char* path, uint16_t)
{
    if (!path || g_files.find(path) == g_files.end() || g_nextHandle == 0xFF) return 0xFF;
    const uint8_t handle = g_nextHandle++;
    g_openFiles[handle] = OpenFile{path, 0};
    return handle;
}

Status close(uint8_t handle) { return g_openFiles.erase(handle) == 1 ? VFS_OK : VFS_ERR_INVALID; }

int32_t read(uint8_t handle, void* buffer, uint32_t size)
{
    std::map<uint8_t, OpenFile>::iterator opened = g_openFiles.find(handle);
    if (opened == g_openFiles.end() || !buffer) return VFS_ERR_INVALID;
    std::vector<uint8_t>& bytes = g_files[opened->second.path];
    const uint32_t available = opened->second.position <= bytes.size()
        ? static_cast<uint32_t>(bytes.size() - opened->second.position) : 0;
    const uint32_t take = size < available ? size : available;
    if (take) std::memcpy(buffer, &bytes[opened->second.position], take);
    opened->second.position += take;
    return static_cast<int32_t>(take);
}

int64_t file_size(uint8_t handle)
{
    std::map<uint8_t, OpenFile>::const_iterator opened = g_openFiles.find(handle);
    if (opened == g_openFiles.end()) return VFS_ERR_INVALID;
    return static_cast<int64_t>(g_files[opened->second.path].size());
}

bool exists(const char* path)
{
    return path && (g_files.find(path) != g_files.end() ||
                    g_directories.find(path) != g_directories.end());
}

Status stat(const char* path, FileInfo* info)
{
    if (!path || !info) return VFS_ERR_INVALID;
    *info = {};
    std::map<std::string, std::vector<uint8_t> >::const_iterator file = g_files.find(path);
    if (file == g_files.end()) {
        if (g_directories.find(path) == g_directories.end()) return VFS_ERR_NOT_FOUND;
        info->type = FILE_TYPE_DIRECTORY;
        return VFS_OK;
    }
    info->type = FILE_TYPE_REGULAR;
    info->size = file->second.size();
    return VFS_OK;
}

Status unlink(const char* path) { return path && g_files.erase(path) ? VFS_OK : VFS_ERR_NOT_FOUND; }

Status rename(const char* oldPath, const char* newPath)
{
    if (!oldPath || !newPath) return VFS_ERR_INVALID;
    if (g_failRenameSource == oldPath &&
        g_failRenameDestination == newPath) {
        g_failRenameSource.clear();
        g_failRenameDestination.clear();
        return VFS_ERR_IO;
    }
    if (g_files.find(newPath) != g_files.end() ||
        g_directories.find(newPath) != g_directories.end()) return VFS_ERR_EXISTS;
    if (!g_cancelHookFired && g_cancelHook &&
        g_cancelOnRenameSource == oldPath) {
        g_cancelHookFired = true;
        g_cancelHookResult = g_cancelHook(g_cancelOwner, g_cancelHandle);
    }
    std::map<std::string, std::vector<uint8_t> >::iterator oldFile = g_files.find(oldPath);
    if (oldFile == g_files.end()) return VFS_ERR_NOT_FOUND;
    g_files[newPath] = oldFile->second;
    g_files.erase(oldFile);
    return VFS_OK;
}

int32_t read_file(const char* path, void* buffer, uint32_t maxSize)
{
    if (!path || !buffer) return VFS_ERR_INVALID;
    if (!g_cancelHookFired && g_cancelHook && g_cancelOnReadPath == path) {
        g_cancelHookFired = true;
        g_cancelHookResult = g_cancelHook(g_cancelOwner, g_cancelHandle);
    }
    std::map<std::string, std::vector<uint8_t> >::const_iterator file = g_files.find(path);
    if (file == g_files.end() || file->second.size() > maxSize) return VFS_ERR_NOT_FOUND;
    if (!file->second.empty()) std::memcpy(buffer, &file->second[0], file->second.size());
    return static_cast<int32_t>(file->second.size());
}

Status mkdir(const char* path)
{
    if (!path || path[0] == '\0') return VFS_ERR_INVALID;
    if (g_directories.find(path) != g_directories.end()) return VFS_ERR_EXISTS;
    const std::string value(path);
    const size_t slash = value.find_last_of('/');
    const std::string parent = slash == 0 ? "/" : value.substr(0, slash);
    if (g_directories.find(parent) == g_directories.end()) return VFS_ERR_NOT_FOUND;
    g_directories.insert(value);
    return VFS_OK;
}

uint8_t opendir(const char* path)
{
    if (!path || g_directories.find(path) == g_directories.end()) return 0xFF;
    static uint8_t nextDirectory = 1;
    if (nextDirectory == 0xFF) nextDirectory = 1;
    const uint8_t handle = nextDirectory++;
    std::vector<DirEntry> entries;
    const std::string prefix = std::string(path) + "/";
    const auto addChild = [&](const std::string& candidate, FileType type,
                              uint64_t size) {
        if (candidate.compare(0, prefix.size(), prefix) != 0) return;
        const std::string child = candidate.substr(prefix.size());
        if (child.empty() || child.find('/') != std::string::npos) return;
        DirEntry entry = {};
        std::strncpy(entry.name, child.c_str(), sizeof(entry.name) - 1U);
        entry.type = type;
        entry.size = size;
        entries.push_back(entry);
    };
    for (const std::string& directory : g_directories)
        addChild(directory, FILE_TYPE_DIRECTORY, 0U);
    for (const auto& file : g_files)
        addChild(file.first, FILE_TYPE_REGULAR, file.second.size());
    g_openDirectories[handle] = entries;
    return handle;
}

bool readdir(uint8_t iterator, DirEntry* entry)
{
    static std::map<uint8_t, uint32_t> positions;
    if (!entry) return false;
    auto opened = g_openDirectories.find(iterator);
    if (opened == g_openDirectories.end()) return false;
    uint32_t& position = positions[iterator];
    if (position >= opened->second.size()) return false;
    *entry = opened->second[position++];
    return true;
}

void closedir(uint8_t iterator)
{
    g_openDirectories.erase(iterator);
}

void set_comp4b_cancel_hook(const char* readPath, const char* renameSource,
                            uint64_t owner, uint64_t handle,
                            CancelHook callback)
{
    g_cancelOnReadPath = readPath ? readPath : "";
    g_cancelOnRenameSource = renameSource ? renameSource : "";
    g_cancelOwner = owner;
    g_cancelHandle = handle;
    g_cancelHook = callback;
    g_cancelHookResult = 0;
    g_cancelHookFired = false;
}

void clear_comp4b_cancel_hook()
{
    g_cancelOnReadPath.clear();
    g_cancelOnRenameSource.clear();
    g_cancelOwner = 0;
    g_cancelHandle = 0;
    g_cancelHook = nullptr;
    g_cancelHookFired = false;
}

int32_t comp4b_cancel_hook_result() { return g_cancelHookResult; }

void fail_next_comp4b_rename(const char* oldPath, const char* newPath)
{
    g_failRenameSource = oldPath ? oldPath : "";
    g_failRenameDestination = newPath ? newPath : "";
}

uint32_t comp4b_open_file_count()
{
    return static_cast<uint32_t>(g_openFiles.size());
}

uint32_t comp4b_open_directory_count()
{
    return static_cast<uint32_t>(g_openDirectories.size());
}

int32_t write_file(const char* path, const void* buffer, uint32_t size)
{
    if (!path || (!buffer && size != 0)) return VFS_ERR_INVALID;
    std::vector<uint8_t>& bytes = g_files[path];
    bytes.resize(size);
    if (size) std::memcpy(&bytes[0], buffer, size);
    if (g_corruptFinalArtifact && std::strcmp(path, "/comp3/build/app.elf") == 0 && size)
        bytes[0] = 0;
    return static_cast<int32_t>(size);
}

void set_corrupt_final_artifact(bool corrupt) { g_corruptFinalArtifact = corrupt; }
} }

namespace {
static CompileSummary g_summary = {};
static const char* const g_sourcePaths[] = {"/comp3/src/main.cpp", "/comp3/src/helper.cpp"};
static const char* const g_identityPaths[] = {"src/main.cpp", "src/helper.cpp"};
static const char* const g_objectPaths[] = {
    "/comp3/build/obj/src/main.cpp.o", "/comp3/build/obj/src/helper.cpp.o"
};
static const char* const g_missingSourcePaths[] = {"/comp3/src/missing.cpp"};
static const char* const g_missingIdentityPaths[] = {"src/missing.cpp"};
static const char* const g_missingObjectPaths[] = {"/comp3/build/obj/src/missing.cpp.o"};

static void put(const char* path, const char* text)
{
    vfs::write_file(path, text, static_cast<uint32_t>(std::strlen(text)));
}

static bool build(uint32_t sourceCount = 2)
{
    return compile_project_incremental(g_sourcePaths, g_identityPaths, g_objectPaths,
                                       sourceCount, "/comp3/build/app.elf", &g_summary);
}

static const CompileDiagnostic* diagnostic_for(const char* path)
{
    for (uint32_t i = 0; i < g_summary.diagnosticCount; ++i)
        if (std::strcmp(g_summary.diagnostics[i].sourcePath, path) == 0) return &g_summary.diagnostics[i];
    return nullptr;
}

static std::vector<uint8_t> read_vfs_bytes(const char* path)
{
    vfs::FileInfo info = {};
    assert(vfs::stat(path, &info) == vfs::VFS_OK);
    std::vector<uint8_t> bytes(static_cast<size_t>(info.size));
    if (!bytes.empty())
        assert(vfs::read_file(path, &bytes[0], static_cast<uint32_t>(bytes.size())) ==
               static_cast<int32_t>(bytes.size()));
    return bytes;
}
}

#ifndef COMPILER_DRIVER_DIAGNOSTIC_HOST_TEST_NO_MAIN
int main()
{
    put("/comp3/src/main.cpp",
        "#include \"helper.h\"\n"
        "int gx_main(gx_app_context* ctx) { return calculate_value(); }\n");
    put("/comp3/src/helper.cpp", "int calculate_value() { return 42; }\n");
    put("/comp3/src/helper.h", "int calculate_value();\n");

    assert(build());
    assert(g_summary.failureStage == CompileFailureStage::None &&
           g_summary.compiledModuleCount == 2 && g_summary.cachedModuleCount == 0 &&
           g_summary.linkedModuleCount == 2 && g_summary.reopenedAndValidated);
    assert(build());
    assert(g_summary.compiledModuleCount == 0 && g_summary.cachedModuleCount == 2 &&
           g_summary.linkedModuleCount == 2);

    put("/comp3/src/helper.cpp", "int calculate_value() { return 43; }\n");
    assert(build());
    assert(g_summary.compiledModuleCount == 1 && g_summary.cachedModuleCount == 1);

    put("/comp3/src/helper.h", "int calculate_value();\nint unused_value();\n");
    assert(build());
    assert(g_summary.compiledModuleCount == 1 && g_summary.cachedModuleCount == 1);

    put("/comp3/src/helper.h",
        "int calculate_value();\n"
        "int invalid_header() { return missing_header_symbol; }\n");
    assert(!build());
    const CompileDiagnostic* headerError = diagnostic_for("src/helper.h");
    assert(g_summary.failureStage == CompileFailureStage::Semantic && headerError &&
           headerError->kind == CompilerDiagnosticKind::Semantic &&
           headerError->location.line == 2 && headerError->location.column > 0 &&
           std::strstr(headerError->message, "missing_header_symbol"));

    put("/comp3/src/helper.h", "int calculate_value();\n");
    put("/comp3/src/main.cpp",
        "#include \"helper.h\"\n"
        "int gx_main(gx_app_context* ctx) { return missing_main_symbol; }\n");
    assert(!build());
    const CompileDiagnostic* mainError = diagnostic_for("src/main.cpp");
    assert(g_summary.failureStage == CompileFailureStage::Semantic && mainError &&
           mainError->kind == CompilerDiagnosticKind::Semantic &&
           mainError->location.line == 2 && mainError->location.column > 0 &&
           std::strstr(mainError->message, "missing_main_symbol"));

    put("/comp3/src/main.cpp",
        "#include \"helper.h\"\n"
        "int gx_main(gx_app_context* ctx) { return calculate_value(); }\n");
    put("/comp3/src/helper.cpp",
        "int calculate_value() {\n"
        "    return missing_helper_symbol;\n"
        "}\n");
    assert(!build());
    const CompileDiagnostic* helperError = diagnostic_for("src/helper.cpp");
    assert(g_summary.failureStage == CompileFailureStage::Semantic && helperError &&
           helperError->kind == CompilerDiagnosticKind::Semantic &&
           helperError->location.line == 2 && helperError->location.column > 0 &&
           std::strstr(helperError->message, "missing_helper_symbol"));

    put("/comp3/src/helper.cpp", "int calculate_value() { return 44; }\n");
    assert(build());
    assert(g_summary.reopenedAndValidated && g_summary.compiledModuleCount == 1 &&
           g_summary.cachedModuleCount == 1 && g_summary.linkedModuleCount == 2);

    put("/comp3/src/main.cpp",
        "#include \"missing.h\"\n"
        "int gx_main(gx_app_context* ctx) { return 1; }\n");
    assert(!build(1));
    const CompileDiagnostic* headerReadError = diagnostic_for("src/main.cpp");
    assert(g_summary.failureStage == CompileFailureStage::SourceRead && headerReadError &&
           headerReadError->kind == CompilerDiagnosticKind::SourceRead &&
           std::strstr(headerReadError->message, "header not found"));

    const std::vector<uint8_t> lastSuccessfulArtifact = read_vfs_bytes("/comp3/build/app.elf");
    put("/comp3/src/main.cpp", "int gx_main(gx_app_context* ctx) { return (1 + ); }\n");
    assert(!build(1));
    const CompileDiagnostic* syntaxError = diagnostic_for("src/main.cpp");
    assert(g_summary.failureStage == CompileFailureStage::Syntax && syntaxError &&
           syntaxError->kind == CompilerDiagnosticKind::Syntax && syntaxError->location.line == 1 &&
           g_summary.success == false && g_summary.outputBytes == 0 && g_summary.outputHash == 0 &&
           g_summary.reopenedHash == 0 &&
           read_vfs_bytes("/comp3/build/app.elf") == lastSuccessfulArtifact);

    put("/comp3/src/main.cpp",
        "int* global_pointer;\n"
        "int gx_main(gx_app_context* ctx) { return 1; }\n");
    assert(!build(1));
    const CompileDiagnostic* unsupportedError = diagnostic_for("src/main.cpp");
    assert(g_summary.failureStage == CompileFailureStage::Unsupported && unsupportedError &&
           unsupportedError->kind == CompilerDiagnosticKind::Unsupported &&
           std::strstr(unsupportedError->message, "global pointer"));

    put("/comp3/src/main.cpp",
        "int gx_main(gx_app_context* ctx) { int value = 1; int* pointer = &value; int invalid = pointer; return 0; }\n");
    assert(!build(1));
    const CompileDiagnostic* typeError = diagnostic_for("src/main.cpp");
    assert(g_summary.failureStage == CompileFailureStage::Semantic && typeError &&
           typeError->kind == CompilerDiagnosticKind::Semantic &&
           std::strstr(typeError->message, "cannot initialize int with a pointer"));

    put("/comp3/src/main.cpp", "int gx_main(gx_app_context* ctx) { return @; }\n");
    assert(!build(1));
    const CompileDiagnostic* lexicalError = diagnostic_for("src/main.cpp");
    assert(g_summary.failureStage == CompileFailureStage::Lexical && lexicalError &&
           lexicalError->kind == CompilerDiagnosticKind::Lexical &&
           lexicalError->location.line == 1 && std::strlen(lexicalError->message) <
               COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES);

    const std::string longIdentifier(70, 'x');
    const std::string oversizedIdentifierSource =
        "int gx_main(gx_app_context* ctx) { int " + longIdentifier + " = 1; return 0; }\n";
    put("/comp3/src/main.cpp", oversizedIdentifierSource.c_str());
    assert(!build(1));
    const CompileDiagnostic* limitError = diagnostic_for("src/main.cpp");
    assert(g_summary.failureStage == CompileFailureStage::ResourceLimit && limitError &&
           limitError->kind == CompilerDiagnosticKind::ResourceLimit &&
           std::strlen(limitError->message) < COMPILER_MAX_DIAGNOSTIC_MESSAGE_BYTES);

    put("/comp3/src/main.cpp",
        "int missing_external();\n"
        "int gx_main(gx_app_context* ctx) { return missing_external(); }\n");
    assert(!compile_project_incremental(g_sourcePaths, g_identityPaths, g_objectPaths,
                                        1, "/comp3/build/app.elf", &g_summary));
    assert(g_summary.failureStage == CompileFailureStage::Linker && g_summary.diagnosticCount != 0 &&
           g_summary.diagnostics[0].kind == CompilerDiagnosticKind::Linker &&
           g_summary.diagnostics[0].location.line == 0 &&
           std::strcmp(g_summary.diagnostics[0].sourcePath, "<link>") == 0 &&
           std::strstr(g_summary.diagnostics[0].message, "missing_external"));

    assert(!compile_project_incremental(g_missingSourcePaths, g_missingIdentityPaths, g_missingObjectPaths,
                                        1, "/comp3/build/app.elf", &g_summary));
    assert(g_summary.failureStage == CompileFailureStage::SourceRead &&
           g_summary.diagnosticCount != 0 && g_summary.diagnostics[0].location.line == 0);

    put("/comp3/src/main.cpp", "int gx_main(gx_app_context* ctx) { return 1; }\n");
    vfs::set_corrupt_final_artifact(true);
    assert(!build(1));
    vfs::set_corrupt_final_artifact(false);
    assert(g_summary.failureStage == CompileFailureStage::ArtifactValidation &&
           g_summary.diagnostics[0].kind == CompilerDiagnosticKind::Artifact &&
           g_summary.diagnosticCount != 0 && g_summary.diagnostics[0].location.line == 0);

    std::puts("Compiler diagnostic kinds, source mapping, incremental cache, linker, freshness and artifact stages PASS");
    return 0;
}
#endif
