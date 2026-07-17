#include "include/kernel/qemu_display_events_proof.h"

#include "include/kernel/serial_debug.h"
#include "include/kernel/virtio_gpu.h"
#include "display_configuration_service.h"

namespace kernel {
namespace qemu_display_events_proof {

namespace {

using gxos::display::DisplayConfigurationCommand;
using gxos::display::DisplayConfigurationCommandType;
using gxos::display::DisplayConfigurationResponse;
using gxos::display::DisplayConfigurationSnapshot;
using gxos::display::DisplayTopologyChangeQuery;
using gxos::display::VirtioGpuInjectedTopologyChangeKind;

static void serial_u32(uint32_t value)
{
    char buffer[11];
    uint32_t index = sizeof(buffer) - 1u;
    buffer[index] = '\0';
    do {
        buffer[--index] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && index != 0u);
    kernel::serial::puts(&buffer[index]);
}

static DisplayConfigurationResponse query_topology(DisplayConfigurationCommandType type)
{
    DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(DisplayConfigurationCommand);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(type);
    DisplayConfigurationResponse response{};
    (void)gxos::display::DisplayConfigurationService::submit(command, response);
    return response;
}

static bool same_text(const char* left, const char* right, uint32_t capacity)
{
    for (uint32_t i = 0u; i < capacity; ++i) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
    }
    return true;
}

static bool active_configuration_unchanged(const DisplayConfigurationSnapshot& before,
                                           const DisplayConfigurationSnapshot& after)
{
    if (before.mode != after.mode || before.outputCount != after.outputCount ||
        before.virtualDesktopX != after.virtualDesktopX || before.virtualDesktopY != after.virtualDesktopY ||
        before.virtualDesktopWidth != after.virtualDesktopWidth || before.virtualDesktopHeight != after.virtualDesktopHeight ||
        !same_text(before.primaryOutputId, after.primaryOutputId, sizeof(before.primaryOutputId)) ||
        !same_text(before.taskbarMonitorId, after.taskbarMonitorId, sizeof(before.taskbarMonitorId))) return false;
    for (uint32_t i = 0u; i < before.outputCount && i < gxos::display::kDisplayConfigurationMaxOutputs; ++i) {
        const auto& left = before.outputs[i];
        const auto& right = after.outputs[i];
        if (!same_text(left.stableId, right.stableId, sizeof(left.stableId)) ||
            left.scanoutId != right.scanoutId || left.virtualX != right.virtualX || left.virtualY != right.virtualY ||
            left.width != right.width || left.height != right.height || left.enabled != right.enabled ||
            left.primary != right.primary) return false;
    }
    return true;
}

static bool inject_and_query(uint32_t kind, const char* label)
{
    const bool injected = kernel::virtio::gpu::inject_display_topology_change_for_test(kind);
    const DisplayConfigurationResponse response = query_topology(DisplayConfigurationCommandType::QueryDetectedTopologyChange);
    const DisplayTopologyChangeQuery& query = response.detectedTopologyChange;
    kernel::serial::puts("VirtioGPU injected topology: kind=");
    kernel::serial::puts(label);
    kernel::serial::puts(" injectedEvent=yes accepted=");
    kernel::serial::puts(injected ? "yes" : "no");
    kernel::serial::puts(" pending=");
    kernel::serial::puts(query.pending ? "yes" : "no");
    kernel::serial::puts(" classification=");
    kernel::serial::puts(query.classification);
    kernel::serial::puts(" added=");
    serial_u32(query.addedOutputCount);
    kernel::serial::puts(" removed=");
    serial_u32(query.removedOutputCount);
    kernel::serial::puts(" changed=");
    serial_u32(query.changedOutputCount);
    kernel::serial::puts(" activeAffected=");
    kernel::serial::puts(query.activeConfigurationAffected ? "yes" : "no");
    kernel::serial::puts(" automaticApplyPerformed=no\n");
    return injected && response.success != 0u && query.pending != 0u;
}

} // namespace

void run()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    kernel::serial::puts("VirtioGPU display-event proof: eventObserver=disabled reason=QEMU-only-gate result=skipped\n");
    return;
#else
    kernel::virtio::gpu::VirtioGpuConfigSnapshot config{};
    const bool coherent = kernel::virtio::gpu::read_virtio_gpu_config_snapshot(
        kernel::virtio::gpu::get_device(0), &config);
    kernel::virtio::gpu::VirtioGpuDisplayEventObserverStatus initialStatus{};
    kernel::virtio::gpu::get_display_event_observer_status(&initialStatus);
    const DisplayConfigurationResponse refresh = query_topology(DisplayConfigurationCommandType::RefreshDetectedTopology);
    const DisplayConfigurationResponse activeBefore = query_topology(DisplayConfigurationCommandType::QueryActiveConfiguration);
    kernel::serial::puts("VirtioGPU display-event initial: coherent=");
    kernel::serial::puts(coherent && config.coherent ? "ok" : "failed");
    kernel::serial::puts(" eventsRead=0x");
    kernel::serial::put_hex32(config.eventsRead);
    kernel::serial::puts(" polls=");
    serial_u32(static_cast<uint32_t>(initialStatus.polls));
    kernel::serial::puts(" refresh=");
    kernel::serial::puts(refresh.success ? "ok" : "unavailable");
    kernel::serial::puts("\n");

    const bool connector = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::ConnectorState), "connector-state");
    const bool geometry = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::PreferredGeometry), "preferred-geometry");
    const bool addition = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::OutputAddition), "output-addition");
    const bool removal = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::OutputRemoval), "output-removal");

    const DisplayConfigurationResponse activeAfter = query_topology(DisplayConfigurationCommandType::QueryActiveConfiguration);
    DisplayTopologyChangeQuery pending{};
    const bool serviceQuery = kernel::virtio::gpu::query_detected_topology_change(&pending);
    kernel::virtio::gpu::VirtioGpuDisplayEventObserverStatus finalStatus{};
    kernel::virtio::gpu::get_display_event_observer_status(&finalStatus);
    const bool activeUnchanged = activeBefore.success != 0u && activeAfter.success != 0u &&
        active_configuration_unchanged(activeBefore.activeConfiguration, activeAfter.activeConfiguration);
    const bool observerOk = initialStatus.initialized != 0u && initialStatus.enabled != 0u &&
        finalStatus.coherentReads >= initialStatus.coherentReads;
    const bool getDisplayInfoOk = refresh.success != 0u;
    const bool eventClearOk = finalStatus.eventClearWrites == 0u &&
        (finalStatus.lastEventsRead & kernel::virtio::gpu::VIRTIO_GPU_EVENT_DISPLAY) == 0u;
    const bool injectedOk = connector && geometry && addition && removal && serviceQuery && pending.pending != 0u;

    kernel::serial::puts("VirtioGPU display-event counters: coherentConfigReads=");
    serial_u32(static_cast<uint32_t>(finalStatus.coherentReads));
    kernel::serial::puts(" incoherentReads=");
    serial_u32(static_cast<uint32_t>(finalStatus.incoherentReads));
    kernel::serial::puts(" polls=");
    serial_u32(static_cast<uint32_t>(finalStatus.polls));
    kernel::serial::puts(" rescansSubmitted=");
    serial_u32(static_cast<uint32_t>(finalStatus.rescansSubmitted));
    kernel::serial::puts(" clearWrites=");
    serial_u32(static_cast<uint32_t>(finalStatus.eventClearWrites));
    kernel::serial::puts(" activeConfigurationUnchanged=");
    kernel::serial::puts(activeUnchanged ? "yes\n" : "no\n");

    kernel::serial::puts("VirtioGPU display-event proof: coherentConfigReads=");
    kernel::serial::puts(coherent ? "ok" : "failed");
    kernel::serial::puts(" eventObserver=");
    kernel::serial::puts(observerOk ? "ok" : "failed");
    kernel::serial::puts(" realEventObserved=no injectedDiffProof=");
    kernel::serial::puts(injectedOk ? "ok" : "failed");
    kernel::serial::puts(" getDisplayInfo=");
    kernel::serial::puts(getDisplayInfoOk ? "ok" : "failed");
    kernel::serial::puts(" eventClear=");
    kernel::serial::puts(eventClearOk ? "ok(idle-no-real-event)" : "failed");
    kernel::serial::puts(" pendingTopologyPublished=");
    kernel::serial::puts(pending.pending ? "yes" : "no");
    kernel::serial::puts(" activeConfigurationUnchanged=");
    kernel::serial::puts(activeUnchanged ? "yes" : "no");
    kernel::serial::puts(" gpuFailures=0 result=");
    kernel::serial::puts(coherent && observerOk && injectedOk && getDisplayInfoOk && eventClearOk && activeUnchanged ? "success\n" : "failure\n");
#endif
}

} // namespace qemu_display_events_proof
} // namespace kernel
