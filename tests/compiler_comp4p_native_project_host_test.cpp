#define COMPILER_DRIVER_DIAGNOSTIC_HOST_TEST_NO_MAIN
#include "compiler_driver_diagnostic_host_test.cpp"

#include "kernel/core/compiler/compiler_build_service.h"
#include "kernel/core/compiler/compiler_build_job_manager.h"
#include "kernel/core/native_elf/native_elf_executor.h"
#include "kernel/core/native_elf/native_elf_scheduler.h"
#include "kernel/core/native_elf/native_elf_validator.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

static const uint64_t kOwner = 0x4C04U;
static const char kRoot[] = "/workspace/comp4p";
static const char kArtifact[] = "/workspace/comp4p/build/bin/amd64/hello-world.elf";
static const char kProjectId[] = "dev.guidexos.comp4p.hello-world";
static const char kSampleRoot[] = "sdk/samples/native-application-c-subset/";

struct BuildRequestFixture {
    char values[8][kernel::compiler::BuildJobManager::kRequestStringCapacity];
    gx_build_request request;

    BuildRequestFixture()
    {
        const char* fields[8] = {
            kRoot,
            kProjectId,
            "native-gui-application",
            "guidexos.amd64.baremetal.bootstrap.native",
            "guidexos-native-baremetal-bootstrap-v1",
            "",
            "build/bin/amd64/hello-world.elf",
            "Debug"
        };
        for (uint32_t i = 0; i < 8U; ++i) {
            const size_t length = std::strlen(fields[i]);
            assert(length + 1U <= sizeof(values[i]));
            std::memcpy(values[i], fields[i], length + 1U);
        }
        request = {};
        request.size = sizeof(request);
        request.version = GX_BUILD_API_VERSION;
        request.projectRoot = values[0];
        request.projectId = values[1];
        request.projectKind = values[2];
        request.targetProfile = values[3];
        request.buildSystem = values[4];
        request.buildScript = values[5];
        request.expectedArtifact = values[6];
        request.configuration = values[7];
    }
};

static std::string readTextFile(const std::string& path)
{
    std::FILE* input = std::fopen(path.c_str(), "rb");
    assert(input != nullptr);
    assert(std::fseek(input, 0, SEEK_END) == 0);
    const long length = std::ftell(input);
    assert(length >= 0);
    assert(std::fseek(input, 0, SEEK_SET) == 0);
    std::string content(static_cast<size_t>(length), '\0');
    if (length != 0)
        assert(std::fread(&content[0], 1, static_cast<size_t>(length), input) ==
               static_cast<size_t>(length));
    assert(std::fclose(input) == 0);
    return content;
}

static void stageFile(const char* guestPath, const std::string& content)
{
    assert(!content.empty());
    kernel::vfs::write_file(guestPath, content.data(),
                            static_cast<uint32_t>(content.size()));
}

static void stageProject()
{
    using namespace kernel::vfs;
    g_directories.insert("/");
    g_directories.insert("/workspace");
    g_directories.insert(kRoot);
    g_directories.insert("/workspace/comp4p/app");
    g_directories.insert("/workspace/comp4p/include");
    g_directories.insert("/workspace/comp4p/src");

    stageFile("/workspace/comp4p/guidexos.project",
              readTextFile(std::string(kSampleRoot) + "guidexos.project"));
    stageFile("/workspace/comp4p/app/app.json",
              readTextFile(std::string(kSampleRoot) + "app/app.json"));
    stageFile("/workspace/comp4p/include/guidexos_app.h",
              readTextFile(std::string(kSampleRoot) + "include/guidexos_app.h"));
    stageFile("/workspace/comp4p/src/main.c",
              readTextFile(std::string(kSampleRoot) + "src/main.c"));
}

static gx_build_snapshot pollBuild(gx_build_handle handle)
{
    gx_build_snapshot snapshot = {};
    assert(kernel::compiler::BareMetalBuildService::poll_owned(
        kOwner, handle, &snapshot) == GX_OK);
    assert(snapshot.handle == handle && snapshot.outputCount <= GX_BUILD_MAX_OUTPUT_LINES);
    assert(kernel::vfs::comp4b_open_file_count() == 0U);
    assert(kernel::vfs::comp4b_open_directory_count() == 0U);
    return snapshot;
}

static gx_build_snapshot buildToTerminal(const BuildRequestFixture& fixture,
                                        gx_build_handle* outHandle)
{
    gx_build_handle handle = 0;
    assert(kernel::compiler::BareMetalBuildService::request_owned(
        &fixture.request, kOwner, &handle) == GX_OK);
    assert(handle != 0U);
    if (outHandle) *outHandle = handle;

    gx_build_snapshot snapshot = {};
    for (uint32_t step = 0; step < 24U; ++step) {
        snapshot = pollBuild(handle);
        if (snapshot.state == GX_BUILD_SUCCEEDED ||
            snapshot.state == GX_BUILD_FAILED ||
            snapshot.state == GX_BUILD_CANCELLED) return snapshot;
    }
    assert(false && "COMP4P project build exceeded the bounded cooperative stages");
    return snapshot;
}

static bool outputContains(const gx_build_snapshot& snapshot, const char* text)
{
    for (uint32_t i = 0; i < snapshot.outputCount; ++i)
        if (std::strstr(snapshot.output[i].text, text)) return true;
    return false;
}

static bool containsBytes(const std::vector<uint8_t>& bytes, const char* text)
{
    if (bytes.empty() || !text) return false;
    const std::string image(reinterpret_cast<const char*>(&bytes[0]), bytes.size());
    return image.find(text) != std::string::npos;
}

static void releaseBuild(gx_build_handle handle)
{
    assert(kernel::compiler::BareMetalBuildService::release_owned(
        kOwner, handle) == GX_OK);
}

static void saveArtifact(const std::vector<uint8_t>& bytes, const char* path)
{
    std::FILE* output = std::fopen(path, "wb");
    assert(output != nullptr);
    assert(std::fwrite(&bytes[0], 1, bytes.size(), output) == bytes.size());
    assert(std::fclose(output) == 0);
}

static void testProjectBuildAndFreshness(const BuildRequestFixture& fixture,
                                         const char* artifactOutput)
{
    gx_build_handle firstHandle = 0;
    const gx_build_snapshot first = buildToTerminal(fixture, &firstHandle);
    assert(first.state == GX_BUILD_SUCCEEDED && first.artifactValid &&
           first.sourceFileCount == 1U && first.compiledModuleCount == 1U &&
           first.linkedModuleCount == 1U);
    assert(std::strcmp(first.artifactPath, "build/bin/amd64/hello-world.elf") == 0);
    assert(first.artifactSha256[0] != '\0' &&
           outputContains(first, "Running bounded compiler pipeline") &&
           outputContains(first, "NativeElf validation PASS"));

    const std::vector<uint8_t> firstArtifact = read_vfs_bytes(kArtifact);
    assert(containsBytes(firstArtifact, "Hello World"));
    kernel::native_elf::NativeElfValidationResult validation = {};
    assert(kernel::native_elf::validate_native_elf(
        &firstArtifact[0], static_cast<uint32_t>(firstArtifact.size()),
        kernel::native_elf::default_validation_policy(), &validation));
    assert(validation.valid && validation.entryPoint == first.artifactEntryPoint);
    releaseBuild(firstHandle);

    std::string broken = readTextFile(std::string(kSampleRoot) + "src/main.c");
    const std::string insertion = "    @\n";
    const size_t call = broken.find("gx_window_set_text");
    assert(call != std::string::npos);
    broken.insert(call, insertion);
    stageFile("/workspace/comp4p/src/main.c", broken);

    gx_build_handle failedHandle = 0;
    const gx_build_snapshot failed = buildToTerminal(fixture, &failedHandle);
    assert(failed.state == GX_BUILD_FAILED && !failed.artifactValid &&
           failed.artifactPath[0] == '\0' && failed.artifactSha256[0] == '\0' &&
           failed.errorCount != 0U && outputContains(failed, "src/main.c(") &&
           outputContains(failed, "[lexical]"));
    assert(read_vfs_bytes(kArtifact) == firstArtifact);
    releaseBuild(failedHandle);

    std::string edited = readTextFile(std::string(kSampleRoot) + "src/main.c");
    const size_t greeting = edited.find("Hello World");
    assert(greeting != std::string::npos);
    edited.replace(greeting, std::strlen("Hello World"), "Hello World 2");
    stageFile("/workspace/comp4p/src/main.c", edited);

    gx_build_handle editedHandle = 0;
    const gx_build_snapshot editedBuild = buildToTerminal(fixture, &editedHandle);
    assert(editedBuild.state == GX_BUILD_SUCCEEDED && editedBuild.artifactValid &&
           editedBuild.handle != first.handle &&
           std::strcmp(editedBuild.artifactPath, first.artifactPath) == 0 &&
           std::strcmp(editedBuild.artifactSha256, first.artifactSha256) != 0);
    const std::vector<uint8_t> editedArtifact = read_vfs_bytes(kArtifact);
    assert(containsBytes(editedArtifact, "Hello World 2") &&
           editedArtifact != firstArtifact);
    kernel::native_elf::NativeElfValidationResult editedValidation = {};
    assert(kernel::native_elf::validate_native_elf(
        &editedArtifact[0], static_cast<uint32_t>(editedArtifact.size()),
        kernel::native_elf::default_validation_policy(), &editedValidation));
    saveArtifact(editedArtifact, artifactOutput);
    releaseBuild(editedHandle);
}

} // namespace

namespace kernel { namespace native_elf {

bool NativeElfRunService::native_elf_scheduler_in_target() { return false; }

extern "C" bool invoke_native_entry_on_stack(
    uint64_t entryPoint, void* context, uint64_t,
    NativeElfTrampolineResult* result)
{
    if (entryPoint == 0 || !result) return false;
    typedef int32_t (GX_CALL *Entry)(void*);
    Entry entry = reinterpret_cast<Entry>(entryPoint);
    result->returnValue = entry(context);
    result->runtimeStatus = NativeRuntimeStatus::None;
    return true;
}

} } // namespace kernel::native_elf

int main(int argc, char** argv)
{
    assert(argc == 2 && argv[1] && argv[1][0] != '\0');
    stageProject();
    BuildRequestFixture fixture;
    testProjectBuildAndFreshness(fixture, argv[1]);
    std::puts("COMP4P sample project production Build service, compiler, diagnostics, ELF and freshness PASS");
    return 0;
}
