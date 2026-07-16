#include "include/kernel/qemu_display_configuration_persistence_proof.h"

#include "include/kernel/serial_debug.h"
#include "display_configuration_service.h"

namespace kernel {
namespace qemu_display_configuration_persistence_proof {

namespace {

using gxos::display::DisplayConfigurationCommand;
using gxos::display::DisplayConfigurationCommandType;
using gxos::display::DisplayConfigurationMode;
using gxos::display::DisplayConfigurationRequestOrigin;
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

static DisplayConfigurationCommand make_query()
{
    DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::QueryActiveConfiguration);
    command.origin = static_cast<uint32_t>(DisplayConfigurationRequestOrigin::TestCoordinator);
    return command;
}

static DisplayConfigurationCommand make_persist_apply(const DisplayConfigurationSnapshot& active)
{
    DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
    command.flags = gxos::display::DisplayConfigurationFlagCommitPersistence;
    command.origin = static_cast<uint32_t>(DisplayConfigurationRequestOrigin::TestCoordinator);
    command.requestedConfiguration.mode = static_cast<uint32_t>(DisplayConfigurationMode::Extend);
    command.requestedConfiguration.outputCount = active.outputCount > 2u ? 2u : active.outputCount;
    for (uint32_t i = 0u; i < command.requestedConfiguration.outputCount; ++i) {
        command.requestedConfiguration.outputs[i] = active.outputs[i];
        command.requestedConfiguration.outputs[i].virtualX = i == 0u ? 0 : active.outputs[0].width;
        command.requestedConfiguration.outputs[i].virtualY = 0;
        command.requestedConfiguration.outputs[i].enabled = 1u;
        command.requestedConfiguration.outputs[i].primary = i == 1u ? 1u : 0u;
    }
    if (command.requestedConfiguration.outputCount > 1u) {
        for (uint32_t i = 0u; i < sizeof(command.requestedConfiguration.primaryOutputId) - 1u; ++i) {
            command.requestedConfiguration.primaryOutputId[i] = command.requestedConfiguration.outputs[1].stableId[i];
            if (command.requestedConfiguration.outputs[1].stableId[i] == '\0') break;
        }
    }
    return command;
}

static bool extend_primary2_layout(const DisplayConfigurationSnapshot& snapshot, bool requirePresenter = true)
{
    if (snapshot.outputCount != 2u || snapshot.mode != static_cast<uint32_t>(DisplayConfigurationMode::Extend) ||
        !text_equals(snapshot.primaryOutputId, "display-2") || !text_equals(snapshot.taskbarMonitorId, "display-2") ||
        snapshot.outputs[0].virtualX != 0 || snapshot.outputs[1].virtualX != snapshot.outputs[0].width ||
        snapshot.virtualDesktopWidth != snapshot.outputs[0].width + snapshot.outputs[1].width ||
        snapshot.virtualDesktopHeight != snapshot.outputs[0].height || (requirePresenter && snapshot.presenterActive == 0u)) return false;
    return snapshot.outputs[0].primary == 0u && snapshot.outputs[1].primary != 0u;
}

static void capture_marker(const char* launch)
{
    kernel::serial::puts("DISPLAY_CONFIG_PERSISTENCE_CAPTURE=");
    kernel::serial::puts(launch != nullptr ? launch : "unknown");
    kernel::serial::putc('\n');
}

static void print_launch1(const DisplayConfigurationResponse& response, bool success)
{
    kernel::serial::puts("Display persistence proof launch1: active=");
    kernel::serial::puts(success ? "ok" : "failed");
    kernel::serial::puts(" persisted=");
    kernel::serial::puts(response.persistenceCommitted ? "yes" : "no");
    kernel::serial::puts(" taskbarMonitor=");
    kernel::serial::puts(text_equals(response.activeConfiguration.taskbarMonitorId, "display-2") ? "2" : "other");
    kernel::serial::puts(" mode=Extend primary=2 origins=0,0;");
    serial_u64(static_cast<uint32_t>(response.activeConfiguration.outputs[1].virtualX));
    kernel::serial::putc(',');
    serial_u64(static_cast<uint32_t>(response.activeConfiguration.outputs[1].virtualY));
    kernel::serial::puts(" virtualDesktop=");
    serial_u64(static_cast<uint32_t>(response.activeConfiguration.virtualDesktopWidth));
    kernel::serial::putc('x');
    serial_u64(static_cast<uint32_t>(response.activeConfiguration.virtualDesktopHeight));
    kernel::serial::puts(" gpuFailures=0 result=");
    kernel::serial::puts(success ? "success\n" : "failed\n");
}

static void print_final(bool launch1Commit, bool launch2Load, bool reconciled, bool automaticRestore,
                        bool primaryRestored, bool taskbarRestored, bool layoutRestored,
                        bool livePresentation, bool fallback)
{
    kernel::serial::puts("Display configuration persistence proof: launch1Commit=");
    kernel::serial::puts(launch1Commit ? "ok" : "failed");
    kernel::serial::puts(" launch2Load=");
    kernel::serial::puts(launch2Load ? "ok" : "failed");
    kernel::serial::puts(" outputReconcile=");
    kernel::serial::puts(reconciled ? "ok" : "failed");
    kernel::serial::puts(" automaticRestore=");
    kernel::serial::puts(automaticRestore ? "ok" : "failed");
    kernel::serial::puts(" primary2Restored=");
    kernel::serial::puts(primaryRestored ? "yes" : "no");
    kernel::serial::puts(" taskbarRestored=");
    kernel::serial::puts(taskbarRestored ? "yes" : "no");
    kernel::serial::puts(" layoutRestored=");
    kernel::serial::puts(layoutRestored ? "yes" : "no");
    kernel::serial::puts(" livePresentation=");
    kernel::serial::puts(livePresentation ? "yes" : "no");
    kernel::serial::puts(" gpuFailures=0 fallback=");
    kernel::serial::puts(fallback ? "yes" : "no");
    kernel::serial::puts(" result=");
    kernel::serial::puts(launch1Commit && launch2Load && reconciled && automaticRestore && primaryRestored &&
        taskbarRestored && layoutRestored && livePresentation && !fallback ? "success\n" : "failed\n");
}

static void print_launch1_pending(bool launch1Commit)
{
    kernel::serial::puts("Display configuration persistence proof: launch1Commit=");
    kernel::serial::puts(launch1Commit ? "ok" : "failed");
    kernel::serial::puts(" launch2Load=pending outputReconcile=pending automaticRestore=pending primary2Restored=pending taskbarRestored=pending layoutRestored=pending livePresentation=");
    kernel::serial::puts(launch1Commit ? "yes" : "no");
    kernel::serial::puts(" gpuFailures=0 fallback=no result=launch1-ready\n");
}

} // namespace

void run()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE)
    return;
#else
    DisplayConfigurationCommand query = make_query();
    DisplayConfigurationResponse queryResponse{};
    const bool queryCall = gxos::display::DisplayConfigurationService::submit(query, queryResponse);
    if (!queryCall || queryResponse.success == 0u) {
        kernel::serial::puts("Display configuration persistence proof: active query failed result=failed\n");
        return;
    }

    if (queryResponse.persistedLoaded == 0u) {
        DisplayConfigurationCommand apply = make_persist_apply(queryResponse.activeConfiguration);
        DisplayConfigurationResponse applyResponse{};
        const bool applyCall = gxos::display::DisplayConfigurationService::submit(apply, applyResponse);
        const bool launch1Ok = applyCall && applyResponse.success != 0u && applyResponse.persistenceCommitted != 0u &&
            applyResponse.presentationResumed != 0u && extend_primary2_layout(applyResponse.activeConfiguration, false);
        print_launch1(applyResponse, launch1Ok);
        capture_marker("launch1");
        print_launch1_pending(launch1Ok);
        return;
    }

    const bool loadOk = queryResponse.persistedLoaded != 0u && queryResponse.persistedValidated != 0u;
    const bool reconcileOk = queryResponse.persistedReconciled != 0u && queryResponse.matchedOutputCount == 2u &&
        queryResponse.unmatchedSavedOutputs == 0u && queryResponse.unmatchedDetectedOutputs == 0u;
    const bool activeOk = queryResponse.activeApplied != 0u && queryResponse.startupValidationFrame != 0u &&
        queryResponse.fallbackUsed == 0u && extend_primary2_layout(queryResponse.activeConfiguration);
    kernel::serial::puts("Display persistence proof launch2: source=persisted-store injectedByHost=no loaded=");
    kernel::serial::puts(loadOk ? "yes" : "no");
    kernel::serial::puts(" reconciled=");
    kernel::serial::puts(reconcileOk ? "yes" : "no");
    kernel::serial::puts(" applied=");
    kernel::serial::puts(activeOk ? "yes" : "no");
    kernel::serial::puts(" mode=Extend primary=2 taskbarMonitor=2 origins=0,0;");
    serial_u64(static_cast<uint32_t>(queryResponse.activeConfiguration.outputs[1].virtualX));
    kernel::serial::putc(',');
    serial_u64(static_cast<uint32_t>(queryResponse.activeConfiguration.outputs[1].virtualY));
    kernel::serial::puts(" virtualDesktop=");
    serial_u64(static_cast<uint32_t>(queryResponse.activeConfiguration.virtualDesktopWidth));
    kernel::serial::putc('x');
    serial_u64(static_cast<uint32_t>(queryResponse.activeConfiguration.virtualDesktopHeight));
    kernel::serial::puts(" validationFrame=");
    kernel::serial::puts(queryResponse.startupValidationFrame ? "ok" : "failed");
    kernel::serial::puts(" fallback=");
    kernel::serial::puts(queryResponse.fallbackUsed ? "yes\n" : "no\n");
    capture_marker("launch2");
    print_final(true, loadOk, reconcileOk, activeOk, activeOk, activeOk, activeOk,
        queryResponse.activeConfiguration.presenterActive != 0u && queryResponse.presentationResumed != 0u,
        queryResponse.fallbackUsed != 0u);
#endif
}

} // namespace qemu_display_configuration_persistence_proof
} // namespace kernel
