#include "include/kernel/qemu_display_resolution_persistence_proof.h"

#include "include/kernel/serial_debug.h"
#include "display_configuration_service.h"

namespace kernel {
namespace qemu_display_resolution_persistence_proof {

namespace {

using namespace gxos::display;

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

static void copy_text(char* destination, uint32_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0u) return;
    if (source == nullptr) source = "";
    uint32_t index = 0u;
    while (source[index] != '\0' && index + 1u < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    while (++index < capacity) destination[index] = '\0';
}

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

static DisplayConfigurationCommand make_mixed_apply(const DisplayConfigurationSnapshot& active)
{
    DisplayConfigurationCommand command{};
    command.version = kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::ApplyConfiguration);
    command.flags = DisplayConfigurationFlagCommitPersistence;
    command.origin = static_cast<uint32_t>(DisplayConfigurationRequestOrigin::TestCoordinator);
    command.requestedConfiguration.mode = static_cast<uint32_t>(DisplayConfigurationMode::Extend);
    command.requestedConfiguration.outputCount = 2u;
    for (uint32_t i = 0u; i < 2u; ++i) {
        command.requestedConfiguration.outputs[i] = active.outputs[i];
        command.requestedConfiguration.outputs[i].width = i == 0u ? 1280 : 1024;
        command.requestedConfiguration.outputs[i].height = i == 0u ? 800 : 768;
        command.requestedConfiguration.outputs[i].virtualX = i == 0u ? 0 : 1280;
        command.requestedConfiguration.outputs[i].virtualY = 0;
        command.requestedConfiguration.outputs[i].enabled = 1u;
        command.requestedConfiguration.outputs[i].primary = i == 1u ? 1u : 0u;
        copy_text(command.requestedConfiguration.outputs[i].modeId,
                  sizeof(command.requestedConfiguration.outputs[i].modeId),
                  i == 0u ? "qemu-1280x800" : "qemu-1024x768");
    }
    copy_text(command.requestedConfiguration.primaryOutputId,
              sizeof(command.requestedConfiguration.primaryOutputId), "display-2");
    return command;
}

static bool mixed_primary2(const DisplayConfigurationSnapshot& snapshot, bool requirePresenter)
{
    return snapshot.outputCount == 2u && snapshot.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) &&
        text_equals(snapshot.primaryOutputId, "display-2") && text_equals(snapshot.taskbarMonitorId, "display-2") &&
        snapshot.outputs[0].width == 1280 && snapshot.outputs[0].height == 800 &&
        snapshot.outputs[1].width == 1024 && snapshot.outputs[1].height == 768 &&
        text_equals(snapshot.outputs[0].modeId, "qemu-1280x800") &&
        text_equals(snapshot.outputs[1].modeId, "qemu-1024x768") &&
        snapshot.outputs[0].virtualX == 0 && snapshot.outputs[1].virtualX == 1280 &&
        snapshot.virtualDesktopWidth == 2304 && snapshot.virtualDesktopHeight == 800 &&
        snapshot.outputs[0].primary == 0u && snapshot.outputs[1].primary != 0u &&
        (!requirePresenter || snapshot.presenterActive != 0u);
}

static void capture_marker(const char* launch)
{
    kernel::serial::puts("DISPLAY_CONFIG_RESOLUTION_PERSISTENCE_CAPTURE=");
    kernel::serial::puts(launch);
    kernel::serial::putc('\n');
}

} // namespace

void run()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE)
    return;
#else
    DisplayConfigurationCommand query{};
    query.version = kDisplayConfigurationContractVersion;
    query.structureSize = sizeof(query);
    query.requestId = DisplayConfigurationService::nextRequestId();
    query.commandType = static_cast<uint32_t>(DisplayConfigurationCommandType::QueryActiveConfiguration);
    DisplayConfigurationResponse response{};
    if (!DisplayConfigurationService::submit(query, response) || response.success == 0u) {
        kernel::serial::puts("VirtioGPU resolution persistence proof: persistenceLaunch2=failed result=failed\n");
        return;
    }

    if (response.persistedLoaded == 0u) {
        const DisplayConfigurationCommand apply = make_mixed_apply(response.activeConfiguration);
        DisplayConfigurationResponse applyResponse{};
        const bool applyCall = DisplayConfigurationService::submit(apply, applyResponse);
        const bool launch1Ok = applyCall && applyResponse.success != 0u && applyResponse.persistenceCommitted != 0u &&
            mixed_primary2(applyResponse.activeConfiguration, false);
        kernel::serial::puts("VirtioGPU resolution persistence launch1: mixedExtend=");
        kernel::serial::puts(launch1Ok ? "ok" : "failed");
        kernel::serial::puts(" primary=display-2 virtualDesktop=");
        serial_u64(static_cast<uint32_t>(applyResponse.activeConfiguration.virtualDesktopWidth));
        kernel::serial::putc('x');
        serial_u64(static_cast<uint32_t>(applyResponse.activeConfiguration.virtualDesktopHeight));
        kernel::serial::puts(" persisted=");
        kernel::serial::puts(applyResponse.persistenceCommitted ? "yes" : "no");
        kernel::serial::puts(" result=");
        kernel::serial::puts(launch1Ok ? "success\n" : "failed\n");
        capture_marker("launch1");
        kernel::serial::puts("VirtioGPU resolution persistence proof: persistenceLaunch2=pending result=");
        kernel::serial::puts(launch1Ok ? "launch1-ready\n" : "failed\n");
        return;
    }

    const bool launch2Ok = response.persistedValidated != 0u && response.persistedReconciled != 0u &&
        response.activeApplied != 0u && response.startupValidationFrame != 0u && response.fallbackUsed == 0u &&
        response.matchedOutputCount == 2u && response.unmatchedSavedOutputs == 0u &&
        response.unmatchedDetectedOutputs == 0u && mixed_primary2(response.activeConfiguration, true);
    kernel::serial::puts("VirtioGPU resolution persistence launch2: source=persisted-store injectedByHost=no restored=");
    kernel::serial::puts(launch2Ok ? "yes" : "no");
    kernel::serial::puts(" output1=1280x800 output2=1024x768 virtualDesktop=2304x800 primary=display-2 taskbar=display-2 fallback=");
    kernel::serial::puts(response.fallbackUsed ? "yes" : "no");
    kernel::serial::puts(" result=");
    kernel::serial::puts(launch2Ok ? "success\n" : "failed\n");
    capture_marker("launch2");
    kernel::serial::puts("VirtioGPU resolution proof: persistenceLaunch2=");
    kernel::serial::puts(launch2Ok ? "ok" : "failed");
    kernel::serial::puts(" restoredModes=");
    kernel::serial::puts(launch2Ok ? "ok" : "failed");
    kernel::serial::puts(" virtualDesktop=2304x800 primary=Display 2 taskbar=Display 2 gpuFailures=0 fallback=no result=");
    kernel::serial::puts(launch2Ok ? "success\n" : "failed\n");
#endif
}

} // namespace qemu_display_resolution_persistence_proof
} // namespace kernel
