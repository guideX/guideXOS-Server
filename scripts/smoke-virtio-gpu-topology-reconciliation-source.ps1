param()

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Read-Text([string]$RelativePath) {
    $path = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing source file: $path" }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if (-not $Text.Contains($Needle)) { throw $Message }
}

function Assert-Ordered([string]$Text, [string[]]$Needles, [string]$Message) {
    $last = -1
    foreach ($needle in $Needles) {
        $position = $Text.IndexOf($needle, [StringComparison]::Ordinal)
        if ($position -lt 0 -or $position -le $last) { throw "${Message}: '$needle'" }
        $last = $position
    }
}

$command = Read-Text 'display_configuration_command.h'
$events = Read-Text 'virtio_gpu_display_events.h'
$service = Read-Text 'kernel\core\display_configuration_service.cpp'
$gpu = Read-Text 'kernel\core\virtio_gpu.cpp'
$gpuHeader = Read-Text 'kernel\core\include\kernel\virtio_gpu.h'
$compositor = Read-Text 'kernel\core\kernel_compositor.cpp'
$compositorHeader = Read-Text 'kernel\core\include\kernel\kernel_compositor.h'
$proof = Read-Text 'kernel\core\qemu_display_events_proof.cpp'
$options = Read-Text 'kernel\core\kernel_apps.cpp'

foreach ($needle in @(
    'QueryPendingTopologyChange', 'PreviewTopologyReconciliation', 'ApplyPendingTopologyChange',
    'DismissPendingTopologyChange', 'TopologyGenerationStale', 'topologyGeneration',
    'activeConfigurationGeneration', 'DisplayTopologyReconciliationPlan',
    'proposedRequestedConfiguration', 'addedOutputs', 'removedOutputs', 'resourceActions',
    'scanoutActions', 'rejectionReason')) {
    Assert-Contains $command $needle "typed topology contract field/command missing: $needle"
}
Assert-Contains $command 'displayConfigurationCommandRequiresTopologyGeneration' 'generation requirement helper missing'
Assert-Contains $events 'genuineDeviceEvent' 'genuine event distinction missing'
Assert-Contains $events 'injectedTopologyGeneration' 'injected topology generation missing'
Assert-Contains $events 'automaticApplyPerformed' 'automatic apply diagnostic missing'

foreach ($needle in @(
    'build_topology_request', 'validate_topology_request', 'fill_pending_response',
    'PreviewTopologyReconciliation', 'ApplyPendingTopologyChange', 'DismissPendingTopologyChange',
    'TopologyGenerationStale', 'pending topology dismissed',
    'set_display_configuration_backend_presentation_paused(true)',
    'apply_display_configuration_backend_layout', 'update_input_layout',
    'reconcileDisplayTopology', 'apply_detected_topology_for_service',
    'persist_configuration', 'rollbackOldOutputsRestored', 'rollbackOldLayoutRestored',
    'pending topology changed before commit')) {
    Assert-Contains $service $needle "authoritative topology service behavior missing: $needle"
}

$previewStart = $service.IndexOf('type == DisplayConfigurationCommandType::PreviewTopologyReconciliation', [StringComparison]::Ordinal)
$previewEnd = $service.IndexOf('} else if (type == DisplayConfigurationCommandType::RefreshDetectedTopology)', $previewStart, [StringComparison]::Ordinal)
if ($previewStart -lt 0 -or $previewEnd -le $previewStart) { throw 'topology command service branch is missing' }
$topologyBranch = $service.Substring($previewStart, $previewEnd - $previewStart)
$pauseIndex = $topologyBranch.IndexOf('set_display_configuration_backend_presentation_paused(true)', [StringComparison]::Ordinal)
$previewMutationIndex = $topologyBranch.IndexOf('type == DisplayConfigurationCommandType::PreviewTopologyReconciliation', [StringComparison]::Ordinal)
if ($pauseIndex -lt 0 -or $previewMutationIndex -lt 0 -or $pauseIndex -lt $previewMutationIndex) {
    throw 'preview must complete before the authoritative presentation pause'
}
if ($topologyBranch -notmatch 'no GPU mutation') { throw 'preview must explicitly report no GPU mutation' }

Assert-Contains $gpu 'issue_clear_scanout' 'removal must use the VirtIO-GPU scanout clear operation'
Assert-Contains $gpu 'scanoutUnbound' 'removed scanout binding must remain rollback-aware'
Assert-Contains $gpu 'activeOutputCount' 'backend must represent a one-output active desktop'
Assert-Contains $gpu 'resourceRetention=rollback-safe' 'removed resource retention diagnostic missing'
Assert-Contains $gpu 'provisionalReleased' 'resource cleanup lifecycle diagnostic missing'
Assert-Contains $gpuHeader 'apply_detected_topology_for_service' 'public apply endpoint missing'
Assert-Contains $gpuHeader 'dismiss_detected_topology_for_service' 'public dismiss endpoint missing'
Assert-Contains $compositorHeader 'reconcileDisplayTopology' 'window/taskbar topology reconciliation API missing'
Assert-Contains $compositor 's_dragState = DragState()' 'drag/capture state is not released during reconciliation'
Assert-Contains $compositor 'TaskbarManager::setLayout' 'taskbar routing is not reconciled'

foreach ($needle in @(
    'Display hardware configuration changed.', 'Review', 'Apply', 'Refresh', 'Keep',
    'previewPendingTopologyChange', 'applyPendingTopologyChange', 'dismissPendingTopologyChange',
    'active state is unchanged', 'm_displayLocalEdits')) {
    Assert-Contains $options $needle "Display Options pending-change UX missing: $needle"
}

Assert-Contains $proof 'DisplayConfigurationService::submit' 'proof must use the public typed service endpoint'
Assert-Contains $proof 'genuineHotplugValidated=no' 'proof must disclaim genuine host hotplug'
Assert-Contains $proof 'injectedTopologyProof=' 'injected proof label missing'
Assert-Contains $proof 'rollbackAttempted' 'rollback evidence missing'
Assert-Contains $proof 'pendingRetained=yes' 'failed apply must retain pending topology'
Assert-Contains $proof 'qemuInactiveHostHead=still-exposed-by-detected-inventory' 'inactive host-head distinction missing'
if ($proof -match 'hotplug succeeded|automaticApplyPerformed=yes|apply_detected_topology_change') { throw 'proof contains an automatic/genuine hotplug claim' }

foreach ($source in @($service, $gpu, $proof)) {
    foreach ($forbidden in @('EDID', 'GET_EDID', 'device configuration interrupt', 'QMP')) {
        if ($source.Contains($forbidden)) { throw "topology reconciliation path must not add unsupported feature: $forbidden" }
    }
}
foreach ($needle in @(
    'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.',
    'QEMU-only', 'automaticApply=no')) {
    Assert-Contains $service $needle "QEMU-only safety boundary missing: $needle"
}

Write-Host 'virtio-gpu topology reconciliation source smoke passed.'
