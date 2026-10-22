#include "core/compiler/compiler_driver.h"
#include "kernel/vfs.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace kernel;
using namespace kernel::compiler;

namespace kernel { namespace vfs {
namespace {
struct OpenFile { std::string path; uint32_t position; };
std::map<std::string, std::vector<uint8_t> > g_files;
std::map<uint8_t, OpenFile> g_openFiles;
uint8_t g_nextHandle = 1;
bool g_corruptFinalArtifact = false;
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

bool exists(const char* path) { return path && g_files.find(path) != g_files.end(); }

Status stat(const char* path, FileInfo* info)
{
    if (!path || !info) return VFS_ERR_INVALID;
    std::map<std::string, std::vector<uint8_t> >::const_iterator file = g_files.find(path);
    if (file == g_files.end()) return VFS_ERR_NOT_FOUND;
    *info = {};
    info->type = FILE_TYPE_REGULAR;
    info->size = file->second.size();
    return VFS_OK;
}

Status unlink(const char* path) { return path && g_files.erase(path) ? VFS_OK : VFS_ERR_NOT_FOUND; }

Status rename(const char* oldPath, const char* newPath)
{
    if (!oldPath || !newPath) return VFS_ERR_INVALID;
    std::map<std::string, std::vector<uint8_t> >::iterator oldFile = g_files.find(oldPath);
    if (oldFile == g_files.end()) return VFS_ERR_NOT_FOUND;
    g_files[newPath] = oldFile->second;
    g_files.erase(oldFile);
    return VFS_OK;
}

int32_t read_file(const char* path, void* buffer, uint32_t maxSize)
{
    if (!path || !buffer) return VFS_ERR_INVALID;
    std::map<std::string, std::vector<uint8_t> >::const_iterator file = g_files.find(path);
    if (file == g_files.end() || file->second.size() > maxSize) return VFS_ERR_NOT_FOUND;
    if (!file->second.empty()) std::memcpy(buffer, &file->second[0], file->second.size());
    return static_cast<int32_t>(file->second.size());
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
}

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
    assert(g_summary.failureStage == CompileFailureStage::Frontend && headerError &&
           headerError->location.line == 2 && headerError->location.column > 0 &&
           std::strstr(headerError->message, "missing_header_symbol"));

    put("/comp3/src/helper.h", "int calculate_value();\n");
    put("/comp3/src/main.cpp",
        "#include \"helper.h\"\n"
        "int gx_main(gx_app_context* ctx) { return missing_main_symbol; }\n");
    assert(!build());
    const CompileDiagnostic* mainError = diagnostic_for("src/main.cpp");
    assert(g_summary.failureStage == CompileFailureStage::Frontend && mainError &&
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
    assert(g_summary.failureStage == CompileFailureStage::Frontend && helperError &&
           helperError->location.line == 2 && helperError->location.column > 0 &&
           std::strstr(helperError->message, "missing_helper_symbol"));

    put("/comp3/src/helper.cpp", "int calculate_value() { return 44; }\n");
    assert(build());
    assert(g_summary.reopenedAndValidated && g_summary.compiledModuleCount == 1 &&
           g_summary.cachedModuleCount == 1 && g_summary.linkedModuleCount == 2);

    put("/comp3/src/main.cpp",
        "int missing_external();\n"
        "int gx_main(gx_app_context* ctx) { return missing_external(); }\n");
    assert(!compile_project_incremental(g_sourcePaths, g_identityPaths, g_objectPaths,
                                        1, "/comp3/build/app.elf", &g_summary));
    assert(g_summary.failureStage == CompileFailureStage::Linker && g_summary.diagnosticCount != 0 &&
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
           g_summary.diagnosticCount != 0 && g_summary.diagnostics[0].location.line == 0);

    std::puts("Compiler driver diagnostics, source mapping, incremental cache, linker and artifact stages PASS");
    return 0;
}
