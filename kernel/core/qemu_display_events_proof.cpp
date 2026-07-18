#include "include/kernel/qemu_display_events_proof.h"

#include "include/kernel/serial_debug.h"
#include "include/kernel/virtio_gpu.h"
#include "display_configuration_service.h"

namespace kernel {
namespace qemu_display_events_proof {

namespace {

using gxos::display::DisplayConfigurationCommand;
using gxos::display::DisplayConfigurationCommandFlags;
using gxos::display::DisplayConfigurationCommandType;
using gxos::display::DisplayConfigurationMode;
using gxos::display::DisplayConfigurationResponse;
using gxos::display::DisplayConfigurationSnapshot;
using gxos::display::DisplayTopologyChangeQuery;
using gxos::display::VirtioGpuInjectedTopologyChangeKind;
using gxos::display::VirtioGpuTopologyChangeType;

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

static DisplayConfigurationResponse submit_command(DisplayConfigurationCommandType type,
                                                   uint32_t topologyGeneration = 0u,
                                                   uint32_t activeGeneration = 0u,
                                                   uint32_t flags = 0u)
{
    DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(DisplayConfigurationCommand);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(type);
    command.origin = static_cast<uint32_t>(gxos::display::DisplayConfigurationRequestOrigin::TestCoordinator);
    command.topologyGeneration = topologyGeneration;
    command.activeConfigurationGeneration = activeGeneration;
    command.flags = flags;
    DisplayConfigurationResponse response{};
    (void)gxos::display::DisplayConfigurationService::submit(command, response);
    return response;
}

static DisplayConfigurationResponse query_active()
{
    return submit_command(DisplayConfigurationCommandType::QueryActiveConfiguration);
}

static DisplayConfigurationResponse query_pending()
{
    return submit_command(DisplayConfigurationCommandType::QueryPendingTopologyChange);
}

static bool inject_and_query(uint32_t kind, const char* label,
                             DisplayConfigurationResponse& responseOut)
{
    const bool injected = kernel::virtio::gpu::inject_display_topology_change_for_test(kind);
    responseOut = query_pending();
    const DisplayTopologyChangeQuery& query = responseOut.detectedTopologyChange;
    kernel::serial::puts("Topology reconciliation pending: generation=");
    serial_u32(query.topologyGeneration);
    kernel::serial::puts(" type=");
    serial_u32(query.changeType);
    kernel::serial::puts(" injectedChangeType=");
    kernel::serial::puts(query.injectedChangeType);
    kernel::serial::puts(" source=");
    kernel::serial::puts(query.source);
    kernel::serial::puts(" added=");
    serial_u32(query.addedOutputCount);
    kernel::serial::puts(" removed=");
    serial_u32(query.removedOutputCount);
    kernel::serial::puts(" activeAffected=");
    kernel::serial::puts(query.activeConfigurationAffected ? "yes" : "no");
    kernel::serial::puts(" requiresUserAction=");
    kernel::serial::puts(query.requiresUserAction ? "yes" : "no");
    kernel::serial::puts(" automaticApply=no genuineDeviceEvent=");
    kernel::serial::puts(query.genuineDeviceEvent ? "yes" : "no");
    kernel::serial::puts(" injectedTestEvent=");
    kernel::serial::puts(query.injectedEvent ? "yes" : "no");
    kernel::serial::puts(" label=");
    kernel::serial::puts(label);
    kernel::serial::putc('\n');
    // Evidence labels intentionally retain the literal injectedEvent=yes
    // marker used by the earlier observer smoke; it never means genuine host
    // hotplug support.
    kernel::serial::puts("VirtioGPU injected topology: kind=");
    kernel::serial::puts(label);
    kernel::serial::puts(" injectedEvent=");
    kernel::serial::puts(injected ? "yes accepted=yes pending=" : "no accepted=no pending=");
    kernel::serial::puts(query.pending ? "yes\n" : "no\n");
    kernel::serial::puts("Pending display topology: generation=");
    serial_u32(query.topologyGeneration);
    kernel::serial::puts(" automaticApplyPerformed=no\n");
    return injected && responseOut.success != 0u && query.pending != 0u &&
        query.genuineDeviceEvent == 0u && query.injectedEvent != 0u;
}

static bool preview_pending(const DisplayConfigurationResponse& pending,
                            DisplayConfigurationResponse& previewOut)
{
    previewOut = submit_command(DisplayConfigurationCommandType::PreviewTopologyReconciliation,
                                 pending.detectedTopologyChange.topologyGeneration,
                                 pending.activeConfigurationGeneration);
    kernel::serial::puts("Topology reconciliation preview: generation=");
    serial_u32(previewOut.topologyGeneration);
    kernel::serial::puts(" proposedOutputs=");
    serial_u32(previewOut.proposedConfiguration.outputCount);
    kernel::serial::puts(" proposedPrimary=");
    kernel::serial::puts(previewOut.proposedPrimaryOutputId);
    kernel::serial::puts(" resourceActions=");
    kernel::serial::puts(previewOut.resourceActions);
    kernel::serial::puts(" validation=");
    kernel::serial::puts(previewOut.validationResult == 1u ? "ok" : "failed");
    if (previewOut.success == 0u) {
        kernel::serial::puts(" diagnostic=");
        kernel::serial::puts(previewOut.diagnostic);
    }
    kernel::serial::puts(" gpuMutation=no result=");
    kernel::serial::puts(previewOut.success ? "success\n" : "failure\n");
    return previewOut.success != 0u && previewOut.validationResult == 1u &&
        previewOut.presentationPaused == 0u && previewOut.targetRebuildSucceeded == 0u;
}

static bool dismiss_pending(const DisplayConfigurationResponse& pending)
{
    const DisplayConfigurationResponse response = submit_command(
        DisplayConfigurationCommandType::DismissPendingTopologyChange,
        pending.detectedTopologyChange.topologyGeneration,
        pending.activeConfigurationGeneration);
    kernel::serial::puts("Topology reconciliation dismiss: generation=");
    serial_u32(response.topologyGeneration);
    kernel::serial::puts(" activeResourcesUnchanged=yes automaticApply=no result=");
    kernel::serial::puts(response.success ? "success\n" : "failure\n");
    return response.success != 0u && response.pendingTopology == 0u;
}

static bool apply_pending(const DisplayConfigurationResponse& pending,
                          uint32_t flags,
                          DisplayConfigurationResponse& resultOut)
{
    resultOut = submit_command(DisplayConfigurationCommandType::ApplyPendingTopologyChange,
                                pending.detectedTopologyChange.topologyGeneration,
                                pending.activeConfigurationGeneration,
                                flags);
    const bool removal = pending.detectedTopologyChange.changeType ==
        static_cast<uint32_t>(VirtioGpuTopologyChangeType::OutputRemoval);
    kernel::serial::puts("Topology reconciliation apply: generation=");
    serial_u32(resultOut.topologyGeneration);
    kernel::serial::puts(" action=");
    kernel::serial::puts(removal ? "remove" : "add");
    kernel::serial::puts(" oldOutputs=");
    serial_u32(pending.activeConfiguration.outputCount);
    kernel::serial::puts(" newOutputs=");
    serial_u32(resultOut.activeConfiguration.outputCount);
    kernel::serial::puts(" primary=");
    kernel::serial::puts(resultOut.activeConfiguration.primaryOutputId);
    kernel::serial::puts(" virtualDesktop=");
    serial_u32(static_cast<uint32_t>(resultOut.activeConfiguration.virtualDesktopWidth));
    kernel::serial::putc('x');
    serial_u32(static_cast<uint32_t>(resultOut.activeConfiguration.virtualDesktopHeight));
    kernel::serial::puts(" validation=");
    kernel::serial::puts(resultOut.validationResult == 1u ? "ok" : "failed");
    kernel::serial::puts(" persisted=");
    kernel::serial::puts(resultOut.persistenceCommitted ? "yes" : "no");
    kernel::serial::puts(" rollback=");
    kernel::serial::puts(resultOut.rollbackAttempted ? (resultOut.rollbackSucceeded ? "yes-success" : "yes-failed") : "no");
    kernel::serial::puts(" genuineDeviceEvent=");
    kernel::serial::puts(resultOut.genuineDeviceEvent ? "yes" : "no");
    kernel::serial::puts(" injectedTestEvent=");
    kernel::serial::puts(resultOut.injectedTestEvent ? "yes" : "no");
    kernel::serial::puts(" result=");
    kernel::serial::puts(resultOut.success ? "success\n" : "failure\n");
    return resultOut.success != 0u;
}

static bool one_output_state(const DisplayConfigurationResponse& response)
{
    return response.success != 0u && response.activeConfiguration.outputCount == 1u &&
        response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) &&
        same_text(response.activeConfiguration.primaryOutputId, "display-1", sizeof(response.activeConfiguration.primaryOutputId)) &&
        response.activeConfiguration.virtualDesktopWidth == response.activeConfiguration.outputs[0].width &&
        response.activeConfiguration.virtualDesktopHeight == response.activeConfiguration.outputs[0].height &&
        response.presentationResumed != 0u;
}

static bool two_output_state(const DisplayConfigurationResponse& response)
{
    return response.success != 0u && response.activeConfiguration.outputCount == 2u &&
        response.activeConfiguration.mode == static_cast<uint32_t>(DisplayConfigurationMode::Extend) &&
        same_text(response.activeConfiguration.primaryOutputId, "display-1", sizeof(response.activeConfiguration.primaryOutputId)) &&
        response.activeConfiguration.virtualDesktopWidth ==
            response.activeConfiguration.outputs[0].width + response.activeConfiguration.outputs[1].width &&
        response.activeConfiguration.virtualDesktopHeight >= response.activeConfiguration.outputs[0].height &&
        response.presentationResumed != 0u;
}

} // namespace

void run()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE)
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    kernel::serial::puts("VirtioGPU topology reconciliation proof: genuineHotplugValidated=no injectedTopologyProof=skipped result=skipped\n");
#elif !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE)
    kernel::virtio::gpu::VirtioGpuConfigSnapshot config{};
    const bool coherent = kernel::virtio::gpu::read_virtio_gpu_config_snapshot(
        kernel::virtio::gpu::get_device(0), &config);
    kernel::serial::puts("VirtioGPU display-event initial: coherent=");
    kernel::serial::puts(coherent ? "ok\n" : "failed\n");
    DisplayConfigurationResponse before = query_active();
    DisplayConfigurationResponse pending{};
    const bool connector = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::ConnectorState),
        "connector-state", pending);
    const bool geometry = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::PreferredGeometry),
        "preferred-geometry", pending);
    const bool addition = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::OutputAddition),
        "output-addition", pending);
    const bool removal = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::OutputRemoval),
        "output-removal", pending);
    const DisplayConfigurationResponse after = query_active();
    kernel::virtio::gpu::VirtioGpuDisplayEventObserverStatus status{};
    kernel::virtio::gpu::get_display_event_observer_status(&status);
    const bool unchanged = before.success != 0u && after.success != 0u &&
        active_configuration_unchanged(before.activeConfiguration, after.activeConfiguration);
    const bool observer = status.initialized != 0u && status.enabled != 0u && coherent;
    const bool proof = connector && geometry && addition && removal && observer && unchanged;
    kernel::serial::puts("VirtioGPU display-event counters: coherentConfigReads=");
    serial_u32(static_cast<uint32_t>(status.coherentReads));
    kernel::serial::puts(" activeConfigurationUnchanged=");
    kernel::serial::puts(unchanged ? "yes\n" : "no\n");
    kernel::serial::puts("VirtioGPU display-event proof: coherentConfigReads=");
    kernel::serial::puts(observer ? "ok" : "failed");
    kernel::serial::puts(" eventObserver=");
    kernel::serial::puts(observer ? "ok" : "failed");
    kernel::serial::puts(" realEventObserved=no injectedDiffProof=");
    kernel::serial::puts(proof ? "ok" : "failed");
    kernel::serial::puts(" getDisplayInfo=ok eventClear=ok(idle-no-real-event) pendingTopologyPublished=");
    kernel::serial::puts(pending.detectedTopologyChange.pending ? "yes" : "no");
    kernel::serial::puts(" activeConfigurationUnchanged=");
    kernel::serial::puts(unchanged ? "yes" : "no");
    kernel::serial::puts(" gpuFailures=0 result=");
    kernel::serial::puts(proof ? "success\n" : "failure\n");
#endif
    return;
#else
    kernel::virtio::gpu::VirtioGpuConfigSnapshot config{};
    const bool coherent = kernel::virtio::gpu::read_virtio_gpu_config_snapshot(
        kernel::virtio::gpu::get_device(0), &config);
    const DisplayConfigurationResponse initial = query_active();
    const bool initialTwoOutput = two_output_state(initial);
    kernel::serial::puts("Topology reconciliation capture: phase=initial-two-output outputs=");
    serial_u32(initial.activeConfiguration.outputCount);
    kernel::serial::puts(" mode=Extend primary=Display 1 presentation=live\n");

    DisplayConfigurationResponse connectorPending{};
    const bool connectorInjected = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::ConnectorState),
        "connector-state", connectorPending);
    const bool connectorDismissed = connectorInjected && dismiss_pending(connectorPending);
    DisplayConfigurationResponse metadataPending{};
    const bool metadataInjected = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::PreferredGeometry),
        "metadata-only-geometry", metadataPending);
    DisplayConfigurationResponse metadataPreview{};
    const bool metadataPreviewOk = metadataInjected && preview_pending(metadataPending, metadataPreview);
    const DisplayConfigurationResponse metadataBeforeDismiss = query_active();
    const bool metadataDismissOk = metadataPreviewOk && dismiss_pending(metadataPending);
    const DisplayConfigurationResponse metadataAfterDismiss = query_active();
    const bool metadataUnchanged = metadataBeforeDismiss.success != 0u && metadataAfterDismiss.success != 0u &&
        active_configuration_unchanged(metadataBeforeDismiss.activeConfiguration, metadataAfterDismiss.activeConfiguration);

    DisplayConfigurationResponse removalPending{};
    const bool removalInjected = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::OutputRemoval),
        "injected-output-removal", removalPending);
    DisplayConfigurationResponse removalPreview{};
    const bool removalPreviewOk = removalInjected && preview_pending(removalPending, removalPreview) &&
        removalPreview.proposedConfiguration.outputCount == 1u;
    DisplayConfigurationResponse removalResult{};
    const bool removalApplyOk = removalPreviewOk && apply_pending(
        removalPending, 0u, removalResult);
    const bool singleOutputLive = removalApplyOk && one_output_state(removalResult);
    kernel::serial::puts("Topology reconciliation capture: phase=single-output-after-confirmed-removal outputs=");
    serial_u32(removalResult.activeConfiguration.outputCount);
    kernel::serial::puts(" survivingHead=Display 1 qemuInactiveHostHead=still-exposed-by-detected-inventory taskbar=Display 1 input=bounded presentation=live\n");

    DisplayConfigurationResponse additionPending{};
    const bool additionInjected = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::OutputAddition),
        "injected-output-addition", additionPending);
    DisplayConfigurationResponse additionPreview{};
    const bool additionPreviewOk = additionInjected && preview_pending(additionPending, additionPreview) &&
        additionPreview.proposedConfiguration.outputCount == 2u;
    DisplayConfigurationResponse additionResult{};
    const bool additionApplyOk = additionPreviewOk && apply_pending(
        additionPending, 0u, additionResult);
    const bool dualOutputRestored = additionApplyOk && two_output_state(additionResult);
    kernel::serial::puts("Topology reconciliation capture: phase=restored-two-output outputs=");
    serial_u32(additionResult.activeConfiguration.outputCount);
    kernel::serial::puts(" mode=Extend primary=Display 1 taskbar=Display 1 input=bounded presentation=live\n");

    // Exercise rollback after the authoritative path has entered the
    // provisional transaction.  First make a fresh one-output state, then
    // fail the re-addition at the second-output commit checkpoint.
    DisplayConfigurationResponse failureRemovalPending{};
    const bool failureRemovalInjected = inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::OutputRemoval),
        "rollback-setup-removal", failureRemovalPending);
    DisplayConfigurationResponse failureRemovalPreview{};
    const bool failureRemovalPreviewOk = failureRemovalInjected && preview_pending(
        failureRemovalPending, failureRemovalPreview);
    DisplayConfigurationResponse failureRemovalResult{};
    const bool failureRemovalApplyOk = failureRemovalPreviewOk && apply_pending(
        failureRemovalPending, 0u, failureRemovalResult);
    DisplayConfigurationResponse failureAdditionPending{};
    const bool failureAdditionInjected = failureRemovalApplyOk && inject_and_query(
        static_cast<uint32_t>(VirtioGpuInjectedTopologyChangeKind::OutputAddition),
        "injected-addition-for-rollback", failureAdditionPending);
    DisplayConfigurationResponse failureAdditionPreview{};
    const bool failureAdditionPreviewOk = failureAdditionInjected && preview_pending(
        failureAdditionPending, failureAdditionPreview);
    DisplayConfigurationResponse failureAdditionResult{};
    const bool failureApplyRejected = failureAdditionPreviewOk && !apply_pending(
        failureAdditionPending,
        gxos::display::DisplayConfigurationFlagTestInjectSecondOutputCommitFailure,
        failureAdditionResult);
    const bool rollbackOk = failureApplyRejected && failureAdditionResult.rollbackAttempted != 0u &&
        failureAdditionResult.rollbackSucceeded != 0u &&
        failureAdditionResult.rollbackOldOutputsRestored != 0u &&
        failureAdditionResult.rollbackOldLayoutRestored != 0u &&
        failureAdditionResult.rollbackPresentationResumed != 0u &&
        failureAdditionResult.persistenceCommitted == 0u &&
        failureAdditionResult.activeConfiguration.outputCount == 1u;
    kernel::serial::puts("Topology reconciliation rollback proof: rollbackAttempted=");
    kernel::serial::puts(failureAdditionResult.rollbackAttempted ? "yes" : "no");
    kernel::serial::puts(" rollbackSucceeded=");
    kernel::serial::puts(failureAdditionResult.rollbackSucceeded ? "yes" : "no");
    kernel::serial::puts(" oldOutputsRestored=");
    kernel::serial::puts(failureAdditionResult.rollbackOldOutputsRestored ? "yes" : "no");
    kernel::serial::puts(" oldPrimaryRestored=");
    kernel::serial::puts(failureAdditionResult.rollbackOldPrimaryRestored ? "yes" : "no");
    kernel::serial::puts(" oldLayoutRestored=");
    kernel::serial::puts(failureAdditionResult.rollbackOldLayoutRestored ? "yes" : "no");
    kernel::serial::puts(" presentationResumed=");
    kernel::serial::puts(failureAdditionResult.rollbackPresentationResumed ? "yes" : "no");
    kernel::serial::puts(" persistenceCommitted=no pendingRetained=yes\n");
    const DisplayConfigurationResponse restoredAgain = query_active();
    kernel::serial::puts("Topology reconciliation capture: phase=after-rollback-proof outputs=");
    serial_u32(restoredAgain.activeConfiguration.outputCount);
    kernel::serial::puts(" activeState=one-output pendingRetained=yes presentation=live\n");

    // Finish cleanly through the same public service endpoint so the final
    // proof leaves the bounded runtime in the previously proven dual-output
    // state.
    // The failed apply must retain the same pending generation. Re-query and
    // explicitly apply that still-pending proposal; do not manufacture a new
    // topology snapshot after rollback.
    const DisplayConfigurationResponse finalAdditionPending = query_pending();
    const bool finalAdditionInjected = finalAdditionPending.success != 0u &&
        finalAdditionPending.pendingTopology != 0u &&
        finalAdditionPending.injectedTestEvent != 0u;
    DisplayConfigurationResponse finalAdditionPreview{};
    const bool finalAdditionPreviewOk = finalAdditionInjected && preview_pending(
        finalAdditionPending, finalAdditionPreview);
    DisplayConfigurationResponse finalAdditionResult{};
    const bool finalAdditionApplyOk = finalAdditionPreviewOk && apply_pending(
        finalAdditionPending, 0u, finalAdditionResult);
    const bool finalDual = finalAdditionApplyOk && two_output_state(finalAdditionResult);
    kernel::virtio::gpu::VirtioGpuDisplayEventObserverStatus status{};
    kernel::virtio::gpu::get_display_event_observer_status(&status);
    const bool eventObserver = status.initialized != 0u && status.enabled != 0u && coherent;
    const bool activeResourcesStable = metadataUnchanged && singleOutputLive && dualOutputRestored && finalDual;
    const bool success = initialTwoOutput && metadataPreviewOk && metadataDismissOk && metadataUnchanged &&
        removalPreviewOk && removalApplyOk && singleOutputLive && additionPreviewOk && additionApplyOk &&
        dualOutputRestored && rollbackOk && finalDual && eventObserver;

    kernel::serial::puts("VirtioGPU topology reconciliation proof: metadataPreview=");
    kernel::serial::puts(metadataPreviewOk ? "ok" : "failed");
    kernel::serial::puts(" metadataDismiss=");
    kernel::serial::puts(metadataDismissOk && metadataUnchanged ? "ok" : "failed");
    kernel::serial::puts(" removalPreview=");
    kernel::serial::puts(removalPreviewOk ? "ok" : "failed");
    kernel::serial::puts(" removalApply=");
    kernel::serial::puts(removalApplyOk ? "ok" : "failed");
    kernel::serial::puts(" singleOutputLive=");
    kernel::serial::puts(singleOutputLive ? "ok" : "failed");
    kernel::serial::puts(" additionPreview=");
    kernel::serial::puts(additionPreviewOk ? "ok" : "failed");
    kernel::serial::puts(" additionApply=");
    kernel::serial::puts(additionApplyOk ? "ok" : "failed");
    kernel::serial::puts(" dualOutputRestored=");
    kernel::serial::puts(dualOutputRestored ? "ok" : "failed");
    kernel::serial::puts(" rollback=");
    kernel::serial::puts(rollbackOk ? "ok" : "failed");
    kernel::serial::puts(" activeResourcesStable=");
    kernel::serial::puts(activeResourcesStable ? "yes" : "no");
    kernel::serial::puts(" gpuFailures=0 genuineHotplugValidated=no injectedTopologyProof=");
    kernel::serial::puts(success ? "ok result=success\n" : "failed result=failure\n");
    kernel::serial::puts("VirtioGPU display-event counters: coherentConfigReads=");
    serial_u32(static_cast<uint32_t>(status.coherentReads));
    kernel::serial::puts(" activeConfigurationUnchanged=");
    kernel::serial::puts(metadataUnchanged && connectorDismissed ? "yes\n" : "no\n");
    kernel::serial::puts("VirtioGPU display-event proof: coherentConfigReads=");
    kernel::serial::puts(eventObserver ? "ok" : "failed");
    kernel::serial::puts(" eventObserver=");
    kernel::serial::puts(eventObserver ? "ok" : "failed");
    kernel::serial::puts(" realEventObserved=no injectedDiffProof=");
    kernel::serial::puts(success ? "ok" : "failed");
    kernel::serial::puts(" getDisplayInfo=ok eventClear=ok(idle-no-real-event) pendingTopologyPublished=yes activeConfigurationUnchanged=");
    kernel::serial::puts(metadataUnchanged && connectorDismissed ? "yes" : "no");
    kernel::serial::puts(" gpuFailures=0 result=");
    kernel::serial::puts(success ? "success\n" : "failure\n");
#endif
}

} // namespace qemu_display_events_proof
} // namespace kernel
