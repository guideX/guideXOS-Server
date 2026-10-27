#include "kernel/core/compiler/compiler_build_job_manager.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using kernel::compiler::BuildJobManager::Manager;
using kernel::compiler::BuildJobManager::SourceSelection;

static_assert(GX_BUILD_ERROR_COMPILER_FAILED == 18, "existing build error codes remain stable");
static_assert(GX_BUILD_ERROR_COMPILER_LEXICAL == 22, "new compiler diagnostic codes append to v1");
static_assert(GX_BUILD_ERROR_COMPILER_INTERNAL == 33, "COMP4A diagnostic code remains stable");
static_assert(GX_BUILD_ERROR_BUILD_CANCELLED == 34, "Build cancellation code appends to v1");

namespace {

struct RequestFixture {
    char strings[8][kernel::compiler::BuildJobManager::kRequestStringCapacity];
    gx_build_request request;

    RequestFixture()
    {
        const char* values[8] = {
            "/workspace/comp3r3", "com.guidexos.comp3r3", "native-gui-application",
            "guidexos.amd64.baremetal.bootstrap.native",
            "guidexos-native-baremetal-bootstrap-v1", "", "build/bin/amd64/app.elf",
            "Debug"
        };
        for (uint32_t field = 0; field < 8U; ++field) {
            uint32_t i = 0;
            while (values[field][i] != '\0' && i + 1U < sizeof(strings[field])) {
                strings[field][i] = values[field][i];
                ++i;
            }
            strings[field][i] = '\0';
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

static SourceSelection valid_sources()
{
    SourceSelection selection = {};
    selection.count = 1U;
    std::strcpy(selection.paths[0], "/workspace/comp3r3/src/main.cpp");
    std::strcpy(selection.relatives[0], "src/main.cpp");
    std::strcpy(selection.objectPaths[0],
                "/workspace/comp3r3/build/obj/main.gxo");
    return selection;
}

static gx_build_snapshot failed_result(gx_build_handle handle)
{
    gx_build_snapshot snapshot = {};
    snapshot.size = sizeof(snapshot);
    snapshot.version = GX_BUILD_API_VERSION;
    snapshot.handle = handle;
    snapshot.state = GX_BUILD_FAILED;
    snapshot.errorCode = GX_BUILD_ERROR_COMPILER_FAILED;
    snapshot.warningCount = GX_BUILD_COUNT_UNAVAILABLE;
    snapshot.errorCount = 1U;
    std::strcpy(snapshot.errorMessage, "hosted lifecycle test failure result");
    snapshot.outputCount = 1U;
    snapshot.output[0].stream = 2U;
    std::strcpy(snapshot.output[0].text, "hosted compiler diagnostic");
    return snapshot;
}

static gx_build_snapshot succeeded_result(gx_build_handle handle)
{
    gx_build_snapshot snapshot = {};
    snapshot.size = sizeof(snapshot);
    snapshot.version = GX_BUILD_API_VERSION;
    snapshot.handle = handle;
    snapshot.state = GX_BUILD_SUCCEEDED;
    snapshot.warningCount = GX_BUILD_COUNT_UNAVAILABLE;
    snapshot.artifactValid = 1U;
    snapshot.artifactSize = 128U;
    snapshot.artifactEntryPoint = 1U;
    std::strcpy(snapshot.artifactPath, "build/bin/amd64/app.elf");
    return snapshot;
}

static void test_request_copy_validation_and_owner()
{
    const uint64_t owner = 0x1001U;
    const uint64_t otherOwner = 0x1002U;
    Manager manager;
    RequestFixture fixture;
    gx_build_handle handle = 77U;

    gx_build_request invalid = fixture.request;
    invalid.version = GX_BUILD_API_VERSION + 1U;
    assert(manager.begin_synchronous_job(owner, &invalid, &handle) ==
           GX_ERROR_INVALID_ARGUMENT);
    assert(handle == 0U);

    assert(manager.begin_synchronous_job(owner, &fixture.request, &handle) == GX_OK);
    assert(handle != 0U);
    const gx_build_request* retained = manager.request_for_execution(owner, handle);
    assert(retained && std::strcmp(retained->projectRoot, "/workspace/comp3r3") == 0);
    std::strcpy(fixture.strings[0], "/workspace/mutated-after-admission");
    std::strcpy(fixture.strings[6], "build/bin/amd64/changed.elf");
    assert(std::strcmp(retained->projectRoot, "/workspace/comp3r3") == 0);
    assert(std::strcmp(retained->expectedArtifact, "build/bin/amd64/app.elf") == 0);

    gx_build_snapshot snapshot = {};
    assert(manager.poll(otherOwner, handle, &snapshot) == GX_ERROR_PERMISSION_DENIED);
    assert(manager.release(owner, handle) == GX_ERROR_BUSY);
    gx_build_handle competing = 0;
    assert(manager.begin_synchronous_job(otherOwner, &fixture.request, &competing) ==
           GX_ERROR_BUSY);
    assert(competing == 0U);

    assert(manager.set_source_membership(owner, handle, valid_sources()) == GX_OK);
    assert(manager.queue_synchronous_job(owner, handle) == GX_OK);
    assert(manager.mark_running(owner, handle) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_RUNNING && snapshot.handle != 0U);
    assert(snapshot.warningCount == GX_BUILD_COUNT_UNAVAILABLE);
    gx_build_snapshot failure = failed_result(handle);
    assert(manager.complete_synchronous_job(owner, handle, &failure) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_FAILED && snapshot.errorCount == 1U &&
           snapshot.warningCount == GX_BUILD_COUNT_UNAVAILABLE);
    assert(std::strcmp(snapshot.errorMessage,
                       "hosted lifecycle test failure result") == 0);
    std::strcpy(failure.errorMessage, "mutated caller result");
    gx_build_snapshot afterMutation = {};
    assert(manager.poll(owner, handle, &afterMutation) == GX_OK);
    assert(std::strcmp(afterMutation.errorMessage,
                       "hosted lifecycle test failure result") == 0);

    assert(manager.release(owner, handle) == GX_OK);
    assert(manager.release(owner, handle) == GX_ERROR_INVALID_ARGUMENT);
    assert(manager.poll(owner, handle, &snapshot) == GX_ERROR_INVALID_ARGUMENT);

    gx_build_handle reused = 0;
    RequestFixture next;
    assert(manager.admit_test_job(owner + 1U, &next.request, &reused) == GX_OK);
    assert(reused != handle);
    assert(manager.poll(owner, handle, &snapshot) == GX_ERROR_INVALID_ARGUMENT);
    assert(manager.owner_closed(owner) == GX_OK);
    assert(manager.poll(owner + 1U, reused, &snapshot) == GX_OK);
    assert(manager.release(owner + 1U, reused) == GX_ERROR_BUSY);
    assert(manager.cancel_queued(owner + 1U, reused) == GX_OK);
    assert(manager.release(owner + 1U, reused) == GX_OK);
}

static void test_test_continuation_and_queued_cancel()
{
    const uint64_t owner = 0x2001U;
    Manager manager;
    RequestFixture fixture;
    gx_build_handle handle = 0;
    gx_build_snapshot snapshot = {};

    assert(manager.admit_test_job(owner, &fixture.request, &handle) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_PREPARING && snapshot.outputCount == 0U);
    // A nested desktop tick from an active NativeElf target must not run the
    // continuation. The ordinary desktop boundary advances exactly one unit.
    assert(manager.pump_one_test_step_at_desktop_boundary(true) == GX_ERROR_BUSY);
    gx_build_snapshot nested = {};
    assert(manager.poll(owner, handle, &nested) == GX_OK);
    assert(nested.state == GX_BUILD_PREPARING && nested.outputCount == 0U);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_RUNNING && snapshot.outputCount == 1U);
    assert(std::strstr(snapshot.output[0].text, "TEST ONLY") != nullptr &&
           std::strstr(snapshot.output[0].text, "step 1/3") != nullptr);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.outputCount == 2U &&
           std::strstr(snapshot.output[1].text, "step 2/3") != nullptr);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_FAILED && snapshot.outputCount == 3U);
    assert(snapshot.artifactValid == 0U && snapshot.artifactSize == 0U &&
           snapshot.artifactEntryPoint == 0U && snapshot.artifactPath[0] == '\0' &&
           snapshot.artifactSha256[0] == '\0');
    assert(std::strstr(snapshot.errorMessage, "no compiler or artifact work ran") != nullptr);
    const gx_build_snapshot terminal = snapshot;
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_ERROR_FAILED);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(std::memcmp(&snapshot, &terminal, sizeof(snapshot)) == 0);
    assert(manager.release(owner, handle) == GX_OK);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) ==
           GX_ERROR_INVALID_ARGUMENT);

    gx_build_handle reused = 0;
    assert(manager.admit_test_job(owner, &fixture.request, &reused) == GX_OK);
    assert(reused != handle);
    assert(manager.poll(owner, handle, &snapshot) == GX_ERROR_INVALID_ARGUMENT);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    assert(manager.poll(owner, reused, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_RUNNING && snapshot.outputCount == 1U &&
           std::strstr(snapshot.output[0].text, "step 1/3") != nullptr);
    assert(manager.owner_closed(owner) == GX_OK);
    assert(manager.poll(owner, reused, &snapshot) == GX_ERROR_INVALID_ARGUMENT);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) ==
           GX_ERROR_INVALID_ARGUMENT);

    gx_build_handle cancelledBetweenSteps = 0;
    assert(manager.admit_test_job(owner, &fixture.request,
                                  &cancelledBetweenSteps) == GX_OK);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    assert(manager.request_test_cancel(owner, cancelledBetweenSteps) == GX_OK);
    assert(manager.poll(owner, cancelledBetweenSteps, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_RUNNING && snapshot.outputCount == 1U);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    assert(manager.poll(owner, cancelledBetweenSteps, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_CANCELLED && snapshot.outputCount == 1U &&
           snapshot.artifactValid == 0U);
    const gx_build_snapshot cancelledSnapshot = snapshot;
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_ERROR_FAILED);
    assert(manager.poll(owner, cancelledBetweenSteps, &snapshot) == GX_OK);
    assert(std::memcmp(&snapshot, &cancelledSnapshot,
                       sizeof(snapshot)) == 0);
    assert(manager.release(owner, cancelledBetweenSteps) == GX_OK);

    gx_build_handle cancelled = 0;
    assert(manager.admit_test_job(owner, &fixture.request, &cancelled) == GX_OK);
    assert(manager.cancel_queued(owner, cancelled) == GX_OK);
    assert(manager.poll(owner, cancelled, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_CANCELLED && snapshot.artifactValid == 0U);
    assert(manager.release(owner, cancelled) == GX_OK);
}

static void test_owner_close_and_shutdown_barrier()
{
    const uint64_t owner = 0x3001U;
    Manager manager;
    RequestFixture fixture;
    gx_build_handle active = 0;
    gx_build_snapshot snapshot = {};
    assert(manager.begin_synchronous_job(owner, &fixture.request, &active) == GX_OK);
    assert(manager.owner_closed(owner) == GX_ERROR_BUSY);
    assert(manager.poll(owner, active, &snapshot) == GX_ERROR_PERMISSION_DENIED);
    assert(manager.release(owner, active) == GX_ERROR_PERMISSION_DENIED);
    gx_build_handle competing = 0;
    assert(manager.admit_test_job(owner + 1U, &fixture.request, &competing) ==
           GX_ERROR_BUSY);
    gx_build_snapshot failure = failed_result(active);
    assert(manager.complete_synchronous_job(owner, active, &failure) == GX_OK);
    assert(manager.poll(owner, active, &snapshot) == GX_ERROR_INVALID_ARGUMENT);

    gx_build_handle queued = 0;
    assert(manager.admit_test_job(owner + 1U, &fixture.request, &queued) == GX_OK);
    assert(manager.owner_closed(owner + 1U) == GX_OK);
    assert(manager.poll(owner + 1U, queued, &snapshot) == GX_ERROR_INVALID_ARGUMENT);

    gx_build_handle shuttingDown = 0;
    assert(manager.begin_synchronous_job(owner + 2U, &fixture.request,
                                         &shuttingDown) == GX_OK);
    assert(manager.shutdown() == GX_ERROR_BUSY);
    assert(manager.is_shutting_down());
    assert(manager.admit_test_job(owner + 3U, &fixture.request, &competing) ==
           GX_ERROR_FAILED);
    failure = failed_result(shuttingDown);
    assert(manager.complete_synchronous_job(owner + 2U, shuttingDown,
                                             &failure) == GX_OK);
    assert(manager.shutdown() == GX_OK);
    assert(manager.poll(owner + 2U, shuttingDown, &snapshot) ==
           GX_ERROR_INVALID_ARGUMENT);
    assert(manager.admit_test_job(owner + 4U, &fixture.request, &competing) ==
           GX_ERROR_FAILED);

    Manager betweenStepShutdown;
    gx_build_handle testJob = 0;
    RequestFixture testFixture;
    assert(betweenStepShutdown.admit_test_job(owner + 5U, &testFixture.request,
                                               &testJob) == GX_OK);
    assert(betweenStepShutdown.pump_one_test_step_at_desktop_boundary(false) ==
           GX_OK);
    assert(betweenStepShutdown.shutdown() == GX_OK);
    assert(betweenStepShutdown.pump_one_test_step_at_desktop_boundary(false) ==
           GX_ERROR_FAILED);
    assert(betweenStepShutdown.poll(owner + 5U, testJob, &snapshot) ==
           GX_ERROR_INVALID_ARGUMENT);
}

static void test_capacity_generation_wrap_and_concurrent_poll()
{
    const uint64_t owner = 0x4001U;
    Manager manager;
    RequestFixture fixture;
    gx_build_handle handle = 0;
    assert(manager.admit_test_job(owner, &fixture.request, &handle) == GX_OK);
    gx_build_handle second = 0;
    assert(manager.admit_test_job(owner + 1U, &fixture.request, &second) ==
           GX_ERROR_BUSY);

    std::atomic<uint32_t> pollFailures(0U);
    std::atomic<uint32_t> badAdmissionResults(0U);
    std::vector<std::thread> workers;
    for (uint32_t threadIndex = 0; threadIndex < 4U; ++threadIndex) {
        workers.emplace_back([&]() {
            for (uint32_t i = 0; i < 2000U; ++i) {
                gx_build_snapshot snapshot = {};
                if (manager.poll(owner, handle, &snapshot) != GX_OK ||
                    snapshot.handle != handle ||
                    snapshot.size != sizeof(gx_build_snapshot) ||
                    snapshot.outputCount > 3U) {
                    ++pollFailures;
                    continue;
                }
                for (uint32_t line = 0; line < snapshot.outputCount; ++line) {
                    if (std::strstr(snapshot.output[line].text, "TEST ONLY") == nullptr)
                        ++pollFailures;
                }
                if (snapshot.state == GX_BUILD_FAILED &&
                    (snapshot.outputCount != 3U || snapshot.artifactValid != 0U))
                    ++pollFailures;
            }
        });
    }
    for (uint32_t threadIndex = 0; threadIndex < 2U; ++threadIndex) {
        workers.emplace_back([&]() {
            for (uint32_t i = 0; i < 2000U; ++i) {
                gx_build_handle candidate = 0;
                const gx_result result = manager.admit_test_job(
                    owner + 1U, &fixture.request, &candidate);
                if (result != GX_ERROR_BUSY || candidate != 0U)
                    ++badAdmissionResults;
            }
        });
    }
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    assert(manager.pump_one_test_step_at_desktop_boundary(false) == GX_OK);
    for (std::thread& worker : workers) worker.join();
    assert(pollFailures.load() == 0U);
    assert(badAdmissionResults.load() == 0U);
    gx_build_snapshot snapshot = {};
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_FAILED && snapshot.outputCount == 3U);
    assert(manager.release(owner, handle) == GX_OK);

    Manager exhausted(0xFFFFFFFFU);
    assert(exhausted.admit_test_job(owner, &fixture.request, &second) ==
           GX_ERROR_FAILED);
    assert(second == 0U);
}

static void test_production_cooperative_lifecycle()
{
    const uint64_t owner = 0x5001U;
    Manager manager;
    RequestFixture fixture;
    gx_build_handle handle = 0;
    gx_build_handle competing = 0;
    gx_build_snapshot snapshot = {};
    assert(manager.begin_cooperative_job(owner, &fixture.request, &handle) == GX_OK);
    assert(manager.begin_cooperative_job(owner + 1U, &fixture.request,
                                         &competing) == GX_ERROR_BUSY);
    assert(competing == 0U);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_PREPARING && snapshot.handle == handle);

    const gx_build_request* retained = nullptr;
    bool cancelRequested = false;
    assert(manager.begin_cooperative_step(owner, handle, &retained, &snapshot,
                                          &cancelRequested) == GX_OK);
    assert(retained && !cancelRequested && snapshot.state == GX_BUILD_RUNNING);
    assert(manager.set_source_membership(owner, handle, valid_sources()) == GX_OK);
    std::strcpy(snapshot.output[0].text, "Compiling source module");
    snapshot.output[0].stream = 1U;
    snapshot.outputCount = 1U;
    bool cancelled = false;
    assert(manager.finish_cooperative_step(owner, handle, &snapshot, false,
                                           &cancelled) == GX_OK);
    assert(!cancelled);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_RUNNING && snapshot.outputCount == 1U);

    // Cancellation during a stage is observed when the step returns. The
    // request is idempotent and cannot target a later generation.
    assert(manager.begin_cooperative_step(owner, handle, &retained, &snapshot,
                                          &cancelRequested) == GX_OK);
    assert(!cancelRequested);
    assert(manager.request_cancel(owner + 1U, handle) == GX_ERROR_PERMISSION_DENIED);
    assert(manager.request_cancel(owner, handle) == GX_OK);
    assert(manager.request_cancel(owner, handle) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.outputCount == 2U &&
           std::strstr(snapshot.output[1].text, "Cancellation requested") != nullptr);
    snapshot.state = GX_BUILD_RUNNING;
    assert(manager.finish_cooperative_step(owner, handle, &snapshot, false,
                                           &cancelled) == GX_OK);
    assert(cancelled);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_CANCELLED &&
           snapshot.errorCode == GX_BUILD_ERROR_BUILD_CANCELLED &&
           snapshot.artifactValid == 0U && snapshot.artifactSize == 0U);
    assert(manager.request_cancel(owner, handle) == GX_OK);
    const gx_build_handle cancelledHandle = handle;
    assert(manager.release(owner, handle) == GX_OK);
    assert(manager.request_cancel(owner, cancelledHandle) == GX_ERROR_INVALID_ARGUMENT);

    // An accepted cancellation during a failing stage wins at the safe
    // boundary; the compiler's output remains available in the output ring.
    assert(manager.begin_cooperative_job(owner, &fixture.request, &handle) == GX_OK);
    assert(manager.begin_cooperative_step(owner, handle, &retained, &snapshot,
                                          &cancelRequested) == GX_OK);
    assert(manager.set_source_membership(owner, handle, valid_sources()) == GX_OK);
    assert(manager.request_cancel(owner, handle) == GX_OK);
    gx_build_snapshot failure = failed_result(handle);
    assert(manager.finish_cooperative_step(owner, handle, &failure, true,
                                           &cancelled) == GX_OK);
    assert(cancelled);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_CANCELLED &&
           snapshot.errorCode == GX_BUILD_ERROR_BUILD_CANCELLED &&
           std::strstr(snapshot.errorMessage, "cancelled") != nullptr &&
           snapshot.outputCount == 2U &&
           std::strstr(snapshot.output[0].text,
                       "hosted compiler diagnostic") != nullptr);
    assert(manager.release(owner, handle) == GX_OK);

    // A failure committed first wins over a later cancel and keeps its diagnostic.
    assert(manager.begin_cooperative_job(owner, &fixture.request, &handle) == GX_OK);
    assert(manager.begin_cooperative_step(owner, handle, &retained, &snapshot,
                                          &cancelRequested) == GX_OK);
    assert(manager.set_source_membership(owner, handle, valid_sources()) == GX_OK);
    failure = failed_result(handle);
    assert(manager.finish_cooperative_step(owner, handle, &failure, true,
                                           &cancelled) == GX_OK);
    assert(!cancelled);
    assert(manager.request_cancel(owner, handle) == GX_ERROR_BUSY);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_FAILED && snapshot.errorCount == 1U &&
           std::strstr(snapshot.errorMessage, "hosted lifecycle") != nullptr);
    assert(manager.release(owner, handle) == GX_OK);

    // A cancel accepted before the finalization claim wins without publishing.
    assert(manager.begin_cooperative_job(owner, &fixture.request, &handle) == GX_OK);
    assert(manager.request_cancel(owner, handle) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_CANCELLED && snapshot.artifactValid == 0U);
    assert(manager.release(owner, handle) == GX_OK);

    // Validation and finalization are distinct states. Once publication is
    // claimed, cancellation is rejected and the successful generation owns
    // the resulting artifact identity.
    assert(manager.begin_cooperative_job(owner, &fixture.request, &handle) == GX_OK);
    assert(manager.begin_cooperative_step(owner, handle, &retained, &snapshot,
                                          &cancelRequested) == GX_OK);
    assert(manager.set_source_membership(owner, handle, valid_sources()) == GX_OK);
    snapshot.state = GX_BUILD_VALIDATING_ARTIFACT;
    assert(manager.finish_cooperative_step(owner, handle, &snapshot, false,
                                           &cancelled) == GX_OK);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_VALIDATING_ARTIFACT);
    assert(manager.begin_cooperative_step(owner, handle, &retained, &snapshot,
                                          &cancelRequested) == GX_OK);
    assert(manager.claim_finalization(owner, handle) == GX_OK);
    assert(manager.request_cancel(owner, handle) == GX_ERROR_BUSY);
    gx_build_snapshot success = succeeded_result(handle);
    assert(manager.finish_cooperative_step(owner, handle, &success, true,
                                           &cancelled) == GX_OK);
    assert(!cancelled);
    assert(manager.poll(owner, handle, &snapshot) == GX_OK);
    assert(snapshot.state == GX_BUILD_SUCCEEDED && snapshot.artifactValid == 1U);
    assert(manager.release(owner, handle) == GX_OK);

    // Owner closure cancels an in-flight stage and releases its fixed slot
    // only after the active step reaches its safe return boundary.
    assert(manager.begin_cooperative_job(owner, &fixture.request, &handle) == GX_OK);
    assert(manager.begin_cooperative_step(owner, handle, &retained, &snapshot,
                                          &cancelRequested) == GX_OK);
    assert(manager.owner_closed(owner) == GX_ERROR_BUSY);
    snapshot.state = GX_BUILD_RUNNING;
    assert(manager.finish_cooperative_step(owner, handle, &snapshot, false,
                                           &cancelled) == GX_OK);
    assert(cancelled);
    assert(manager.poll(owner, handle, &snapshot) == GX_ERROR_INVALID_ARGUMENT);
}

}

int main()
{
    test_request_copy_validation_and_owner();
    test_test_continuation_and_queued_cancel();
    test_owner_close_and_shutdown_barrier();
    test_capacity_generation_wrap_and_concurrent_poll();
    test_production_cooperative_lifecycle();
    std::printf("Kernel build-job manager tests PASS | Manager bytes=%u | capacity=1\n",
                static_cast<unsigned>(sizeof(Manager)));
    return 0;
}
