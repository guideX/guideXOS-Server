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

using namespace kernel;

namespace {

static const uint64_t kOwner = 0x4B04U;
static const char kRoot[] = "/workspace/comp4b";
static const char kFinalArtifact[] = "/workspace/comp4b/build/bin/amd64/comp4b.elf";
static const char kTemporaryArtifact[] = "/workspace/comp4b/build/bin/amd64/gx4btmp.tmp";
static const char kBackupArtifact[] = "/workspace/comp4b/build/bin/amd64/gx4bbak.bak";

struct RequestFixture {
    char strings[8][kernel::compiler::BuildJobManager::kRequestStringCapacity];
    gx_build_request request;

    RequestFixture()
    {
        const char* values[8] = {
            kRoot,
            "com.guidexos.comp4b.fixture",
            "native-gui-application",
            "guidexos.amd64.baremetal.bootstrap.native",
            "guidexos-native-baremetal-bootstrap-v1",
            "",
            "build/bin/amd64/comp4b.elf",
            "Debug"
        };
        for (uint32_t field = 0; field < 8U; ++field) {
            const size_t count = std::strlen(values[field]);
            assert(count + 1U < sizeof(strings[field]));
            std::memcpy(strings[field], values[field], count + 1U);
        }
        request = {};
        request.size = sizeof(request);
        request.version = GX_BUILD_API_VERSION;
        request.projectRoot = strings[0];
        request.projectId = strings[1];
        request.projectKind = strings[2];
        request.targetProfile = strings[3];
        request.buildSystem = strings[4];
        request.buildScript = strings[5];
        request.expectedArtifact = strings[6];
        request.configuration = strings[7];
    }
};

static int32_t cancel_hook(uint64_t owner, uint64_t handle)
{
    return kernel::compiler::BareMetalBuildService::cancel_owned(owner, handle);
}

static gx_build_snapshot poll_build(gx_build_handle handle)
{
    gx_build_snapshot snapshot = {};
    assert(kernel::compiler::BareMetalBuildService::poll_owned(
        kOwner, handle, &snapshot) == GX_OK);
    assert(snapshot.handle == handle && snapshot.outputCount <= GX_BUILD_MAX_OUTPUT_LINES);
    assert(kernel::vfs::comp4b_open_file_count() == 0U);
    assert(kernel::vfs::comp4b_open_directory_count() == 0U);
    return snapshot;
}

static gx_build_snapshot run_to_terminal(gx_build_handle handle)
{
    gx_build_snapshot snapshot = {};
    for (uint32_t step = 0; step < 24U; ++step) {
        snapshot = poll_build(handle);
        if (snapshot.state == GX_BUILD_SUCCEEDED ||
            snapshot.state == GX_BUILD_FAILED ||
            snapshot.state == GX_BUILD_CANCELLED) return snapshot;
    }
    assert(false && "cooperative Build exceeded its bounded stage count");
    return snapshot;
}

static gx_build_handle start_build(const RequestFixture& fixture)
{
    gx_build_handle handle = 0;
    assert(kernel::compiler::BareMetalBuildService::request_owned(
        &fixture.request, kOwner, &handle) == GX_OK);
    assert(handle != 0U);
    return handle;
}

static bool output_contains(const gx_build_snapshot& snapshot,
                            const char* needle)
{
    for (uint32_t i = 0; i < snapshot.outputCount; ++i)
        if (std::strstr(snapshot.output[i].text, needle)) return true;
    return false;
}

static bool bytes_contain(const std::vector<uint8_t>& bytes,
                         const uint8_t* needle, uint32_t needleBytes)
{
    if (!needle || needleBytes == 0 || needleBytes > bytes.size()) return false;
    for (size_t i = 0; i + needleBytes <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, needle, needleBytes) == 0) return true;
    return false;
}

static void release_build(gx_build_handle handle)
{
    assert(kernel::compiler::BareMetalBuildService::release_owned(
        kOwner, handle) == GX_OK);
}

static void setup_project()
{
    kernel::vfs::g_directories.insert("/");
    kernel::vfs::g_directories.insert("/workspace");
    kernel::vfs::g_directories.insert(kRoot);
    kernel::vfs::g_directories.insert("/workspace/comp4b/src");
    kernel::vfs::g_directories.insert("/workspace/comp4b/app");

    put("/workspace/comp4b/guidexos.project",
        "{\"formatVersion\":1,\"projectId\":\"com.guidexos.comp4b.fixture\","
        "\"projectKind\":\"native-gui-application\","
        "\"defaultTargetProfile\":\"guidexos.amd64.baremetal.bootstrap.native\","
        "\"abi\":\"guidexos-c-abi-v1\",\"architecture\":\"amd64\","
        "\"entryPoint\":\"gx_main\",\"sourceRoot\":\"src\","
        "\"sources\":[\"main.cpp\",\"helper.cpp\"],\"outputName\":\"comp4b\"}");
    put("/workspace/comp4b/app/app.json",
        "{\"id\":\"com.guidexos.comp4b.fixture\",\"displayName\":\"COMP4B fixture\","
        "\"kind\":\"native-gui-application\",\"architecture\":\"amd64\","
        "\"path\":\"bin/amd64/comp4b.elf\",\"entryPoint\":\"gx_main\","
        "\"abi\":\"guidexos-c-abi-v1\"}");
    put("/workspace/comp4b/src/main.cpp",
        "#include \"helper.h\"\n"
        "int gx_main(gx_app_context* ctx) { return calculate_value() / 2 + 21; }\n");
    put("/workspace/comp4b/src/helper.cpp",
        "int calculate_value() { return 42; }\n");
    put("/workspace/comp4b/src/helper.h", "int calculate_value();\n");
    put(kFinalArtifact, "previous-successful-artifact");
}

static void test_queued_cancel_and_bounded_admission(RequestFixture& fixture)
{
    const std::vector<uint8_t> previous = read_vfs_bytes(kFinalArtifact);
    const gx_build_handle handle = start_build(fixture);
    assert(kernel::compiler::BareMetalBuildService::poll_owned(
        kOwner, handle, nullptr) == GX_ERROR_INVALID_ARGUMENT);
    gx_build_snapshot snapshot = {};
    gx_build_handle competing = 0;
    assert(kernel::compiler::BareMetalBuildService::request_owned(
        &fixture.request, kOwner + 1U, &competing) == GX_ERROR_BUSY);
    assert(competing == 0U);
    assert(kernel::compiler::BareMetalBuildService::cancel_owned(kOwner,
                                                                 handle) == GX_OK);
    assert(kernel::compiler::BareMetalBuildService::cancel_owned(kOwner,
                                                                 handle) == GX_OK);
    snapshot = poll_build(handle);
    assert(snapshot.state == GX_BUILD_CANCELLED &&
           snapshot.errorCode == GX_BUILD_ERROR_BUILD_CANCELLED &&
           !snapshot.artifactValid && !kernel::vfs::exists(kTemporaryArtifact));
    assert(read_vfs_bytes(kFinalArtifact) == previous);
    release_build(handle);
}

static void test_progress_and_cancel_before_publication(RequestFixture& fixture)
{
    const std::vector<uint8_t> previous = read_vfs_bytes(kFinalArtifact);
    const gx_build_handle handle = start_build(fixture);
    gx_build_snapshot snapshot = poll_build(handle);
    assert(snapshot.state == GX_BUILD_PREPARING &&
           output_contains(snapshot, "Validating project metadata"));

    uint32_t priorOutputCount = snapshot.outputCount;
    bool sawCompilerStage = false;
    for (uint32_t step = 0; step < 12U; ++step) {
        snapshot = poll_build(handle);
        assert(snapshot.outputCount >= priorOutputCount);
        priorOutputCount = snapshot.outputCount;
        if (output_contains(snapshot, "Running bounded compiler pipeline")) {
            sawCompilerStage = true;
            break;
        }
    }
    assert(sawCompilerStage);
    // The compiler ran during the polling call that exposed its stage line.
    assert(snapshot.state == GX_BUILD_RUNNING &&
           snapshot.compiledModuleCount == 2U && snapshot.linkedModuleCount == 2U &&
           kernel::vfs::exists(kTemporaryArtifact));
    assert(read_vfs_bytes(kFinalArtifact) == previous);

    snapshot = poll_build(handle); // Validate temporary output, not final path.
    assert(snapshot.state == GX_BUILD_VALIDATING_ARTIFACT &&
           !snapshot.artifactValid && kernel::vfs::exists(kTemporaryArtifact));
    assert(read_vfs_bytes(kFinalArtifact) == previous);
    assert(kernel::compiler::BareMetalBuildService::cancel_owned(kOwner,
                                                                 handle) == GX_OK);
    snapshot = poll_build(handle);
    assert(snapshot.state == GX_BUILD_CANCELLED && !snapshot.artifactValid &&
           !kernel::vfs::exists(kTemporaryArtifact));
    assert(read_vfs_bytes(kFinalArtifact) == previous);
    release_build(handle);
}

static void test_cancel_during_noninterruptible_compiler(RequestFixture& fixture)
{
    const std::vector<uint8_t> previous = read_vfs_bytes(kFinalArtifact);
    gx_build_handle handle = start_build(fixture);
    gx_build_snapshot snapshot = {};
    kernel::vfs::set_comp4b_cancel_hook(
        "/workspace/comp4b/src/main.cpp", nullptr, kOwner, handle,
        &cancel_hook);
    snapshot = poll_build(handle);
    while (snapshot.state != GX_BUILD_CANCELLED &&
           snapshot.state != GX_BUILD_FAILED &&
           snapshot.state != GX_BUILD_SUCCEEDED)
        snapshot = poll_build(handle);
    assert(kernel::vfs::comp4b_cancel_hook_result() == GX_OK);
    kernel::vfs::clear_comp4b_cancel_hook();
    assert(snapshot.state == GX_BUILD_CANCELLED && !snapshot.artifactValid &&
           !kernel::vfs::exists(kTemporaryArtifact));
    assert(read_vfs_bytes(kFinalArtifact) == previous);
    assert(kernel::compiler::BareMetalBuildService::cancel_owned(kOwner,
                                                                 handle) == GX_OK);
    release_build(handle);

    // A cancellation accepted during a compiler stage that subsequently
    // finds a syntax error remains Cancelled at the boundary, while its
    // already-produced diagnostic text is retained in the bounded output.
    put("/workspace/comp4b/src/main.cpp",
        "int gx_main(gx_app_context* ctx) { return (1 + ); }\n");
    handle = start_build(fixture);
    kernel::vfs::set_comp4b_cancel_hook(
        "/workspace/comp4b/src/main.cpp", nullptr, kOwner, handle,
        &cancel_hook);
    snapshot = run_to_terminal(handle);
    assert(kernel::vfs::comp4b_cancel_hook_result() == GX_OK);
    kernel::vfs::clear_comp4b_cancel_hook();
    assert(snapshot.state == GX_BUILD_CANCELLED &&
           snapshot.errorCode == GX_BUILD_ERROR_BUILD_CANCELLED &&
           output_contains(snapshot, "[syntax]") &&
           !snapshot.artifactValid &&
           !kernel::vfs::exists(kTemporaryArtifact) &&
           read_vfs_bytes(kFinalArtifact) == previous);
    release_build(handle);
    put("/workspace/comp4b/src/main.cpp",
        "#include \"helper.h\"\n"
        "int gx_main(gx_app_context* ctx) { return calculate_value(); }\n");
}

static void test_failure_diagnostics_and_recovery(RequestFixture& fixture)
{
    const std::vector<uint8_t> previous = read_vfs_bytes(kFinalArtifact);
    put("/workspace/comp4b/src/main.cpp",
        "int gx_main(gx_app_context* ctx) { return 7 / 0; }\n");
    gx_build_handle handle = start_build(fixture);
    gx_build_snapshot snapshot = run_to_terminal(handle);
    assert(snapshot.state == GX_BUILD_FAILED && !snapshot.artifactValid &&
           snapshot.errorCode == GX_BUILD_ERROR_COMPILER_SEMANTIC &&
           snapshot.errorCount != 0U && output_contains(snapshot, "[semantic]") &&
           output_contains(snapshot, "constant division by zero"));
    assert(!kernel::vfs::exists(kTemporaryArtifact));
    assert(read_vfs_bytes(kFinalArtifact) == previous);
    release_build(handle);

    put("/workspace/comp4b/src/main.cpp",
        "#include \"helper.h\"\n"
        "int gx_main(gx_app_context* ctx) { return calculate_value() / 2 + 21; }\n");
    handle = start_build(fixture);
    snapshot = run_to_terminal(handle);
    assert(snapshot.state == GX_BUILD_SUCCEEDED && snapshot.artifactValid &&
           snapshot.sourceFileCount == 2U && snapshot.linkedModuleCount == 2U);
    const std::vector<uint8_t> artifact = read_vfs_bytes(kFinalArtifact);
    assert(artifact.size() >= 4U && artifact[0] == 0x7FU && artifact[1] == 'E' &&
           artifact[2] == 'L' && artifact[3] == 'F');
    const uint8_t linkedIdiv[] = {0x89, 0xC1, 0x58, 0x99, 0xF7, 0xF9};
    assert(bytes_contain(artifact, linkedIdiv, sizeof(linkedIdiv)));
    assert(!kernel::vfs::exists(kTemporaryArtifact));
    release_build(handle);
}

static void test_cancel_during_validation_and_finalization_race(
    RequestFixture& fixture)
{
    std::vector<uint8_t> previous = read_vfs_bytes(kFinalArtifact);
    gx_build_handle handle = start_build(fixture);
    gx_build_snapshot snapshot = {};
    bool compilerComplete = false;
    for (uint32_t step = 0; step < 16U; ++step) {
        snapshot = poll_build(handle);
        if (output_contains(snapshot, "Running bounded compiler pipeline")) {
            compilerComplete = true;
            break;
        }
    }
    assert(compilerComplete && kernel::vfs::exists(kTemporaryArtifact));
    kernel::vfs::set_comp4b_cancel_hook(kTemporaryArtifact, nullptr,
        kOwner, handle, &cancel_hook);
    // The next poll reads and validates the temporary artifact. The fake VFS
    // injects the cancellation while that production stage is in progress.
    snapshot = poll_build(handle);
    assert(snapshot.state == GX_BUILD_CANCELLED &&
           kernel::vfs::comp4b_cancel_hook_result() == GX_OK &&
           !snapshot.artifactValid && !kernel::vfs::exists(kTemporaryArtifact));
    kernel::vfs::clear_comp4b_cancel_hook();
    assert(read_vfs_bytes(kFinalArtifact) == previous);
    release_build(handle);

    put("/workspace/comp4b/src/helper.cpp",
        "int calculate_value() { return 45; }\n");
    handle = start_build(fixture);
    bool readyToPublish = false;
    for (uint32_t step = 0; step < 16U; ++step) {
        snapshot = poll_build(handle);
        if (snapshot.state == GX_BUILD_VALIDATING_ARTIFACT &&
            kernel::vfs::exists(kTemporaryArtifact)) {
            readyToPublish = true;
            break;
        }
    }
    assert(readyToPublish);
    kernel::vfs::set_comp4b_cancel_hook(nullptr, kFinalArtifact,
        kOwner, handle, &cancel_hook);
    snapshot = poll_build(handle);
    assert(kernel::vfs::comp4b_cancel_hook_result() == GX_ERROR_BUSY);
    kernel::vfs::clear_comp4b_cancel_hook();
    assert(snapshot.state == GX_BUILD_SUCCEEDED && snapshot.artifactValid &&
           !kernel::vfs::exists(kTemporaryArtifact));
    const std::vector<uint8_t> published = read_vfs_bytes(kFinalArtifact);
    assert(published.size() >= 4U && published[0] == 0x7FU &&
           published[1] == 'E' && published[2] == 'L' && published[3] == 'F');
    release_build(handle);
}

static void test_publication_rollback_and_recovery(RequestFixture& fixture)
{
    const std::vector<uint8_t> previous = read_vfs_bytes(kFinalArtifact);
    put("/workspace/comp4b/src/helper.cpp",
        "int calculate_value() { return 46; }\n");
    gx_build_handle handle = start_build(fixture);
    gx_build_snapshot snapshot = {};
    bool readyToPublish = false;
    for (uint32_t step = 0; step < 16U; ++step) {
        snapshot = poll_build(handle);
        if (snapshot.state == GX_BUILD_VALIDATING_ARTIFACT &&
            kernel::vfs::exists(kTemporaryArtifact)) {
            readyToPublish = true;
            break;
        }
    }
    assert(readyToPublish);
    kernel::vfs::fail_next_comp4b_rename(kTemporaryArtifact, kFinalArtifact);
    snapshot = poll_build(handle);
    assert(snapshot.state == GX_BUILD_FAILED && !snapshot.artifactValid &&
           snapshot.errorCode == GX_BUILD_ERROR_COMPILER_ARTIFACT_PUBLICATION &&
           !kernel::vfs::exists(kTemporaryArtifact) &&
           !kernel::vfs::exists(kBackupArtifact));
    assert(read_vfs_bytes(kFinalArtifact) == previous);
    release_build(handle);

    handle = start_build(fixture);
    snapshot = run_to_terminal(handle);
    assert(snapshot.state == GX_BUILD_SUCCEEDED && snapshot.artifactValid);
    assert(!kernel::vfs::exists(kTemporaryArtifact) &&
           !kernel::vfs::exists(kBackupArtifact));
    release_build(handle);
}

} // namespace

namespace kernel { namespace native_elf {

NativeElfValidationPolicy default_validation_policy()
{
    NativeElfValidationPolicy policy = {};
    return policy;
}

bool validate_native_elf(const uint8_t* image, uint32_t imageBytes,
                         const NativeElfValidationPolicy&,
                         NativeElfValidationResult* result)
{
    if (!image || !result) return false;
    *result = {};
    if (imageBytes < 64U || image[0] != 0x7FU || image[1] != 'E' ||
        image[2] != 'L' || image[3] != 'F') {
        result->error = "host fake rejected non-ELF bytes";
        return false;
    }
    uint64_t entry = 0;
    for (uint32_t i = 0; i < 8U; ++i)
        entry |= static_cast<uint64_t>(image[24U + i]) << (8U * i);
    result->valid = true;
    result->entryPoint = entry;
    return true;
}

namespace NativeElfRunService {
bool native_elf_scheduler_in_target() { return false; }
}

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

int main()
{
    setup_project();
    RequestFixture fixture;
    test_queued_cancel_and_bounded_admission(fixture);
    test_progress_and_cancel_before_publication(fixture);
    test_cancel_during_noninterruptible_compiler(fixture);
    test_failure_diagnostics_and_recovery(fixture);
    test_cancel_during_validation_and_finalization_race(fixture);
    test_publication_rollback_and_recovery(fixture);
    std::puts("Production guest Build service + compiler cooperative lifecycle hosted tests PASS");
    return 0;
}
