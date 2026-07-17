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

$backend = Read-Text 'kernel\core\virtio_gpu.cpp'
$gpuHeader = Read-Text 'kernel\core\include\kernel\virtio_gpu.h'
$eventsHeader = Read-Text 'virtio_gpu_display_events.h'
$service = Read-Text 'kernel\core\display_configuration_service.cpp'
$command = Read-Text 'display_configuration_command.h'
$proof = Read-Text 'kernel\core\qemu_display_events_proof.cpp'
$main = Read-Text 'kernel\core\main.cpp'
$options = Read-Text 'display_options.cpp'

Assert-Contains $gpuHeader 'struct GpuConfig' 'packed VirtIO-GPU device config representation is missing'
foreach ($needle in @('eventsReadLe', 'eventsClearLe', 'numScanoutsLe', 'numCapsetsLe', 'static_assert(sizeof(GpuConfig) == 16u')) {
    Assert-Contains $gpuHeader $needle "device config field/layout missing: $needle"
}
foreach ($needle in @('DEVICE_CONFIG_EVENTS_READ', 'DEVICE_CONFIG_EVENTS_CLEAR', 'VIRTIO_GPU_EVENT_DISPLAY', 'le32_to_cpu', 'cpu_to_le32')) {
    Assert-Contains $backend $needle "explicit device-config endian/event handling missing: $needle"
}
$eventsReadUses = [regex]::Matches($backend, 'DEVICE_CONFIG_EVENTS_READ')
foreach ($match in $eventsReadUses) {
    $lineStart = $backend.LastIndexOf("`n", $match.Index) + 1
    $lineEnd = $backend.IndexOf("`n", $match.Index)
    if ($lineEnd -lt 0) { $lineEnd = $backend.Length }
    $line = $backend.Substring($lineStart, $lineEnd - $lineStart)
    if (-not $line.Contains('mmio_read32')) { throw 'events_read is not read through the volatile MMIO helper' }
}
if ($backend -match 'mmio_write(?:8|16|32|64)\([^\r\n]*DEVICE_CONFIG_EVENTS_READ') {
    throw 'events_read must never be written'
}

foreach ($needle in @('read_virtio_gpu_config_snapshot_internal', 'firstGeneration', 'finalGeneration', 'retryCount', 'kDisplayEventConfigReadRetries', 'COMMON_CFG_GEN')) {
    Assert-Contains $backend $needle "coherent config-generation read requirement missing: $needle"
}
Assert-Contains $backend 'if (first == final)' 'coherent config reads must accept only matching generations'
Assert-Contains $backend 'config_generation changed across bounded device-config read' 'incoherent read failure reason is missing'

foreach ($needle in @('VirtioGpuDisplayEventObserver', 'initialized', 'enabled', 'polls', 'coherentReads', 'incoherentReads', 'eventsObserved', 'displayEventsObserved', 'unknownEventBitsObserved', 'displayEventsProcessed', 'eventClearWrites', 'lastEventsRead', 'lastEventsCleared', 'lastConfigGeneration', 'lastPollTick', 'pollInterval', 'rescanInProgress', 'pendingTopologyChange', 'lastTopologyGeneration', 'lastError', 'disabledReason')) {
    Assert-Contains $backend $needle "observer state field missing: $needle"
}
foreach ($needle in @('GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE', 'kDisplayEventPollIntervalTicks', 'nextPollTick', 'kDisplayEventRescanRetryLimit', 'kDisplayEventFailedRescanBackoffTicks')) {
    Assert-Contains $backend $needle "bounded observer gate/rate/retry missing: $needle"
}

$rescanStart = $backend.IndexOf('static bool process_pending_display_rescan', [StringComparison]::Ordinal)
$rescanEnd = $backend.IndexOf('static void display_event_observer_tick', $rescanStart, [StringComparison]::Ordinal)
if ($rescanStart -lt 0 -or $rescanEnd -le $rescanStart) { throw 'bounded display rescan function is missing' }
$rescan = $backend.Substring($rescanStart, $rescanEnd - $rescanStart)
Assert-Ordered $rescan @('submit_display_info_snapshot_request', 'read_virtio_gpu_config_snapshot_internal', 'build_detected_topology_snapshot', 'publish_detected_topology_change', 'clear_display_event_bit') 'GET_DISPLAY_INFO must precede event clear'
Assert-Contains $rescan 'display event retained' 'failed GET_DISPLAY_INFO must retain the event'
Assert-Contains $rescan 'activeMutation=no' 'rescan must report no active mutation'
foreach ($forbidden in @('destroy_resource', 'issue_set_scanout', 'persist_configuration', 'set_display_configuration_backend_presentation_paused')) {
    if ($rescan.Contains($forbidden)) { throw "event rescan must not perform active mutation: $forbidden" }
}
Assert-Contains $backend 'mmio_write32(device_cfg_addr(transport, DEVICE_CONFIG_EVENTS_CLEAR)' 'event clear must use events_clear'
Assert-Contains $backend 'cpu_to_le32(VIRTIO_GPU_EVENT_DISPLAY)' 'only the processed display bit may be cleared'
Assert-Contains $backend 'unknownBits' 'unknown event bits must remain pending'
Assert-Contains $backend 'reasserted' 'event-clear reassertion must be tracked'

foreach ($needle in @('VirtioGpuDetectedTopologySnapshot', 'VirtioGpuDisplayTopologyChange', 'kVirtioGpuDisplayEventMaxScanouts', 'activeConfigurationAffected', 'requiresResourceRebuild', 'requiresLayoutReconciliation', 'metadataOnly', 'supportedAutomatically')) {
    Assert-Contains $eventsHeader $needle "bounded topology model missing: $needle"
}
Assert-Contains $eventsHeader 'currently' 'detected/operational state separation documentation is missing'
Assert-Contains $service 'QueryDetectedTopologyChange' 'authoritative service topology query is missing'
Assert-Contains $service 'detectedTopologyChange' 'service query must return the detected topology record'
Assert-Contains $eventsHeader 'automaticApplyPerformed' 'service query record must report automaticApplyPerformed'
Assert-Contains $command 'RefreshDetectedTopology' 'controlled detected-topology refresh command is missing'
Assert-Contains $options 'Display hardware configuration changed. Review settings.' 'Display Options pending-change message is missing'
Assert-Contains $options 'refreshDetectedTopologyForDisplayOptions' 'Display Options safe-open refresh is missing'

Assert-Contains $proof 'injectedEvent=yes' 'injected event proof must be explicitly labeled'
Assert-Contains $proof 'realEventObserved=no' 'injected proof must not claim a real device event'
Assert-Contains $proof 'activeConfigurationUnchanged' 'runtime proof must verify active configuration stability'
Assert-Contains $main 'qemu_display_events_proof::run' 'event proof is not wired to the QEMU live pump'

foreach ($needle in @('No physical Intel GPU support', 'No real hardware GPU BAR access', 'No 3D, virgl, Venus, blob', 'No cursor queue', 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.')) {
    Assert-Contains $backend $needle "QEMU-only safety boundary missing: $needle"
}
foreach ($forbidden in @('EDID negotiation', 'GET_EDID', 'rotation', 'refresh-rate', 'IRQ')) {
    if ($rescan.Contains($forbidden) -or $proof.Contains($forbidden)) { throw "display-event path must not add unsupported feature: $forbidden" }
}

Write-Host 'virtio-gpu display-event source smoke passed.'
