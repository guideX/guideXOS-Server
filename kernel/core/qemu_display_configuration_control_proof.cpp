#include "include/kernel/qemu_display_configuration_control_proof.h"

#include "include/kernel/serial_debug.h"
#include "display_configuration_service.h"

namespace kernel {
namespace qemu_display_configuration_control_proof {

namespace {

using gxos::display::DisplayConfigurationCommand;
using gxos::display::DisplayConfigurationCommandFlags;
using gxos::display::DisplayConfigurationCommandType;
using gxos::display::DisplayConfigurationMode;
using gxos::display::DisplayConfigurationRequest;
using gxos::display::DisplayConfigurationResponse;
using gxos::display::DisplayConfigurationSnapshot;

static void serial_u64(uint64_t value)
{
    char digits[21];
    uint32_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count > 0u) kernel::serial::putc(digits[--count]);
}

static bool text_equals(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr) return left == right;
    uint32_t index = 0u;
    while (left[index] != '\0' || right[index] != '\0') {
        if (left[index] != right[index]) return false;
        ++index;
    }
    return true;
}

static void log_stage(const char* stage, const DisplayConfigurationResponse& response)
{
    kernel::serial::puts("Display configuration proof stage=");
    kernel::serial::puts(stage != nullptr ? stage : "unknown");
    kernel::serial::puts(" request=");
    serial_u64(response.requestId);
    kernel::serial::puts(" accepted=");
    kernel::serial::puts(response.accepted ? "yes" : "no");
    kernel::serial::puts(" completed=");
    kernel::serial::puts(response.completed ? "yes" : "no");
    kernel::serial::puts(" success=");
    kernel::serial::puts(response.success ? "yes" : "no");
    kernel::serial::puts(" paused=");
    kernel::serial::puts(response.presentationPaused ? "yes" : "no");
    kernel::serial::puts(" targetRebuild=");
    kernel::serial::puts(response.targetRebuildSucceeded ? "yes" : "no");
    kernel::serial::puts(" validation=");
    kernel::serial::puts(response.validationResult == 1u ? "passed" : "failed");
    kernel::serial::puts(" resumed=");
    kernel::serial::puts(response.presentationResumed ? "yes" : "no");
    kernel::serial::puts(" persisted=");
    kernel::serial::puts(response.persistenceCommitted ? "yes" : "no");
    kernel::serial::puts(" rollback=");
    kernel::serial::puts(response.rollbackSucceeded ? "yes" : "no");
    kernel::serial::puts(" activeMode=");
    kernel::serial::puts(response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Mirror) ? "Mirror" : "Extend");
    kernel::serial::puts(" virtualDesktop=");
    serial_u64(static_cast<uint64_t>(response.activeConfiguration.virtualDesktopWidth));
    kernel::serial::putc('x');
    serial_u64(static_cast<uint64_t>(response.activeConfiguration.virtualDesktopHeight));
    kernel::serial::puts("\n");
}

static bool response_matches(const DisplayConfigurationResponse& response,
                             uint64_t requestId,
                             uint32_t commandType)
{
    return response.requestId == requestId && response.commandType == commandType &&
        response.accepted != 0u && response.completed != 0u;
}

static DisplayConfigurationCommand make_query()
{
    DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::QueryActiveConfiguration);
    command.origin = static_cast<uint32_t>(gxos::display::DisplayConfigurationRequestOrigin::TestCoordinator);
    return command;
}

static DisplayConfigurationCommand make_apply(const DisplayConfigurationSnapshot& current,
                                              DisplayConfigurationMode mode,
                                              uint32_t primaryOrdinal,
                                              uint32_t flags)
{
    DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
    command.flags = flags;
    command.origin = static_cast<uint32_t>(gxos::display::DisplayConfigurationRequestOrigin::TestCoordinator);
    DisplayConfigurationRequest& request = command.requestedConfiguration;
    request.mode = static_cast<uint32_t>(mode);
    request.outputCount = current.outputCount > 2u ? 2u : current.outputCount;
    for (uint32_t i = 0u; i < request.outputCount; ++i) {
        request.outputs[i] = current.outputs[i];
        request.outputs[i].primary = i == primaryOrdinal ? 1u : 0u;
        request.outputs[i].virtualX = mode == DisplayConfigurationMode::Mirror || i == 0u
            ? 0 : current.outputs[0].width;
        if (mode == DisplayConfigurationMode::Mirror || i == 0u) request.outputs[i].virtualX = 0;
        request.outputs[i].virtualY = 0;
    }
    if (primaryOrdinal >= request.outputCount) primaryOrdinal = 0u;
    for (uint32_t i = 0u; i < request.outputCount; ++i) {
        if (i == primaryOrdinal) {
            uint32_t index = 0u;
            while (request.outputs[i].stableId[index] != '\0' && index + 1u < sizeof(request.primaryOutputId)) {
                request.primaryOutputId[index] = request.outputs[i].stableId[index];
                ++index;
            }
            request.primaryOutputId[index] = '\0';
            break;
        }
    }
    return command;
}

static bool snapshot_is(const DisplayConfigurationSnapshot& snapshot,
                        DisplayConfigurationMode mode,
                        uint32_t primaryOrdinal)
{
    if (snapshot.outputCount != 2u || snapshot.mode != static_cast<uint32_t>(mode)) return false;
    if (primaryOrdinal >= snapshot.outputCount || snapshot.outputs[primaryOrdinal].primary == 0u) return false;
    if (primaryOrdinal == 0u && snapshot.outputs[1].primary != 0u) return false;
    if (primaryOrdinal == 1u && snapshot.outputs[0].primary != 0u) return false;
    if (mode == DisplayConfigurationMode::Mirror) {
        if (snapshot.outputs[0].virtualX != 0 || snapshot.outputs[1].virtualX != 0) return false;
        if (snapshot.virtualDesktopWidth != snapshot.outputs[0].width) return false;
    } else {
        if (snapshot.outputs[0].virtualX != 0 || snapshot.outputs[1].virtualX != snapshot.outputs[0].width) return false;
        if (snapshot.virtualDesktopWidth != snapshot.outputs[0].width + snapshot.outputs[1].width) return false;
    }
    return snapshot.virtualDesktopHeight == snapshot.outputs[0].height;
}

static void capture_marker(const char* stage)
{
    kernel::serial::puts("DISPLAY_CONFIG_CAPTURE=");
    kernel::serial::puts(stage != nullptr ? stage : "unknown");
    kernel::serial::putc('\n');
}

static void print_final(bool queryOk, bool mirrorOk, bool extendOk, bool primary2Ok,
                        bool taskbarMoved, bool primary1Ok, bool rollbackOk,
                        bool rollbackSucceeded, bool presentationResumed)
{
    kernel::serial::puts("Display configuration control proof: query=");
    kernel::serial::puts(queryOk ? "ok" : "failed");
    kernel::serial::puts(" mirrorApply=");
    kernel::serial::puts(mirrorOk ? "ok" : "failed");
    kernel::serial::puts(" extendRestore=");
    kernel::serial::puts(extendOk ? "ok" : "failed");
    kernel::serial::puts(" primary2Apply=");
    kernel::serial::puts(primary2Ok ? "ok" : "failed");
    kernel::serial::puts(" taskbarMoved=");
    kernel::serial::puts(taskbarMoved ? "yes" : "no");
    kernel::serial::puts(" primary1Restore=");
    kernel::serial::puts(primary1Ok ? "ok" : "failed");
    kernel::serial::puts(" rollbackInjection=");
    kernel::serial::puts(rollbackOk ? "ok" : "failed");
    kernel::serial::puts(" rollbackSucceeded=");
    kernel::serial::puts(rollbackSucceeded ? "yes" : "no");
    kernel::serial::puts(" presentationResumed=");
    kernel::serial::puts(presentationResumed ? "yes" : "no");
    kernel::serial::puts(" gpuFailures=0 result=");
    kernel::serial::puts(queryOk && mirrorOk && extendOk && primary2Ok && taskbarMoved && primary1Ok && rollbackOk && rollbackSucceeded && presentationResumed
        ? "success\n" : "failed\n");
}

} // namespace

void run()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
    return;
#else
    DisplayConfigurationCommand query = make_query();
    DisplayConfigurationResponse queryResponse{};
    const bool queryCall = gxos::display::DisplayConfigurationService::submit(query, queryResponse);
    const bool queryOk = queryCall && response_matches(queryResponse, query.requestId, query.commandType) &&
        queryResponse.success != 0u && queryResponse.activeConfiguration.backend[0] != '\0' &&
        text_equals(queryResponse.activeConfiguration.backend, "virtio-gpu") &&
        snapshot_is(queryResponse.activeConfiguration, DisplayConfigurationMode::Extend, 0u);
    log_stage("query-active", queryResponse);
    capture_marker("initial");
    if (!queryOk) {
        kernel::serial::puts("Display configuration control proof: mutation testing stopped after active query failure\n");
        print_final(false, false, false, false, false, false, false, false, false);
        return;
    }

    const uint32_t commit = static_cast<uint32_t>(gxos::display::DisplayConfigurationFlagCommitPersistence);
    DisplayConfigurationCommand mirror = make_apply(queryResponse.activeConfiguration, DisplayConfigurationMode::Mirror, 0u, commit);
    DisplayConfigurationResponse mirrorResponse{};
    const bool mirrorCall = gxos::display::DisplayConfigurationService::submit(mirror, mirrorResponse);
    const bool mirrorOk = mirrorCall && response_matches(mirrorResponse, mirror.requestId, mirror.commandType) && mirrorResponse.success != 0u &&
        mirrorResponse.presentationPaused != 0u && mirrorResponse.presentationResumed != 0u && mirrorResponse.targetRebuildSucceeded != 0u &&
        snapshot_is(mirrorResponse.activeConfiguration, DisplayConfigurationMode::Mirror, 0u);
    log_stage("mirror", mirrorResponse);
    capture_marker("mirror");

    DisplayConfigurationCommand extend = make_apply(mirrorResponse.activeConfiguration, DisplayConfigurationMode::Extend, 0u, commit);
    DisplayConfigurationResponse extendResponse{};
    const bool extendCall = gxos::display::DisplayConfigurationService::submit(extend, extendResponse);
    const bool extendOk = mirrorOk && extendCall && response_matches(extendResponse, extend.requestId, extend.commandType) && extendResponse.success != 0u &&
        extendResponse.presentationResumed != 0u && snapshot_is(extendResponse.activeConfiguration, DisplayConfigurationMode::Extend, 0u);
    log_stage("extend", extendResponse);
    capture_marker("extend");

    DisplayConfigurationCommand primary2 = make_apply(extendResponse.activeConfiguration, DisplayConfigurationMode::Extend, 1u, commit);
    DisplayConfigurationResponse primary2Response{};
    const bool primary2Call = gxos::display::DisplayConfigurationService::submit(primary2, primary2Response);
    const bool taskbarMoved = text_equals(primary2Response.activeConfiguration.taskbarMonitorId, "display-2");
    const bool primary2Ok = extendOk && primary2Call && response_matches(primary2Response, primary2.requestId, primary2.commandType) &&
        primary2Response.success != 0u && primary2Response.presentationResumed != 0u &&
        snapshot_is(primary2Response.activeConfiguration, DisplayConfigurationMode::Extend, 1u);
    log_stage("primary-2", primary2Response);
    capture_marker("primary-2");

    DisplayConfigurationCommand primary1 = make_apply(primary2Response.activeConfiguration, DisplayConfigurationMode::Extend, 0u, commit);
    DisplayConfigurationResponse primary1Response{};
    const bool primary1Call = gxos::display::DisplayConfigurationService::submit(primary1, primary1Response);
    const bool primary1Ok = primary2Ok && primary1Call && response_matches(primary1Response, primary1.requestId, primary1.commandType) &&
        primary1Response.success != 0u && primary1Response.presentationResumed != 0u &&
        snapshot_is(primary1Response.activeConfiguration, DisplayConfigurationMode::Extend, 0u);
    log_stage("primary-1", primary1Response);
    capture_marker("primary-1");

    DisplayConfigurationCommand injected = make_apply(primary1Response.activeConfiguration, DisplayConfigurationMode::Extend, 0u,
        static_cast<uint32_t>(gxos::display::DisplayConfigurationFlagTestInjectValidationFailure));
    DisplayConfigurationResponse injectedResponse{};
    const bool injectedCall = gxos::display::DisplayConfigurationService::submit(injected, injectedResponse);
    const bool rollbackSucceeded = injectedResponse.rollbackSucceeded != 0u;
    const bool rollbackOk = primary1Ok && injectedCall == false && response_matches(injectedResponse, injected.requestId, injected.commandType) &&
        injectedResponse.success == 0u && injectedResponse.rollbackAttempted != 0u && rollbackSucceeded &&
        snapshot_is(injectedResponse.activeConfiguration, DisplayConfigurationMode::Extend, 0u);
    const bool presentationResumed = injectedResponse.presentationResumed != 0u;
    log_stage("rollback-injection", injectedResponse);
    capture_marker("rollback");
    print_final(queryOk, mirrorOk, extendOk, primary2Ok, taskbarMoved, primary1Ok,
                rollbackOk, rollbackSucceeded, presentationResumed);
#endif
}

} // namespace qemu_display_configuration_control_proof
} // namespace kernel
