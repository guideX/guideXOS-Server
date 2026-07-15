param()

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Assert-True {
    param(
        [Parameter(Mandatory = $true)] [bool]$Condition,
        [Parameter(Mandatory = $true)] [string]$Message
    )

    if (-not $Condition) { throw $Message }
}

function Read-Text {
    param([Parameter(Mandatory = $true)] [string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { throw "Missing file: $Path" }
    return Get-Content -LiteralPath $Path -Raw
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)] [string]$Text,
        [Parameter(Mandatory = $true)] [string]$Needle,
        [Parameter(Mandatory = $true)] [string]$Message
    )

    Assert-True $Text.Contains($Needle) $Message
}

function Assert-Ordered {
    param(
        [Parameter(Mandatory = $true)] [string]$Text,
        [Parameter(Mandatory = $true)] [string[]]$Needles,
        [Parameter(Mandatory = $true)] [string]$Message
    )

    $last = -1
    foreach ($needle in $Needles) {
        $position = $Text.IndexOf($needle, [StringComparison]::Ordinal)
        Assert-True ($position -ge 0 -and $position -gt $last) "$($Message): missing or out-of-order '$needle'"
        $last = $position
    }
}

$configuration = Read-Text (Join-Path $Root 'display_configuration.h')
$model = Read-Text (Join-Path $Root 'display_model.h')
$compositor = Read-Text (Join-Path $Root 'compositor.cpp')
$compositorHeader = Read-Text (Join-Path $Root 'compositor.h')
$options = Read-Text (Join-Path $Root 'display_options.cpp')
$optionsHeader = Read-Text (Join-Path $Root 'display_options.h')
$backend = Read-Text (Join-Path $Root 'virtio_gpu_display_backend.h')
$kernelVirtio = Read-Text (Join-Path $Root 'kernel\core\virtio_gpu.cpp')
$applyStart = $compositor.IndexOf('DisplayApplyResult Compositor::applyDisplayConfiguration', [StringComparison]::Ordinal)
$applyEnd = $compositor.IndexOf('bool Compositor::setHostedDisplayViewport', $applyStart, [StringComparison]::Ordinal)
Assert-True ($applyStart -ge 0 -and $applyEnd -gt $applyStart) 'display apply transaction body is missing'
$applyBody = $compositor.Substring($applyStart, $applyEnd - $applyStart)

# Safety boundary and backend activation gates.
Assert-Contains $backend 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.' 'backend activation must retain the Mule Territory warning'
Assert-Contains $compositor 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.' 'runtime reconfiguration must retain the Mule Territory warning'
Assert-Contains $backend 'static bool isQemuOnly()' 'virtio-gpu backend must expose a QEMU-only gate'
Assert-Contains $compositor 'backendGateActive' 'runtime inventory must carry the backend gate state'
Assert-Contains $kernelVirtio 'GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE' 'kernel virtio-gpu activation must remain explicitly gated'
Assert-Contains $kernelVirtio 'GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE' 'kernel live presentation must remain explicitly gated'

# The three configuration layers must remain distinct.
Assert-Contains $configuration 'struct DetectedDisplayInventory' 'detected inventory model is missing'
Assert-Contains $configuration 'struct RequestedDisplayConfiguration' 'requested configuration model is missing'
Assert-Contains $configuration 'struct ActiveDisplayConfiguration' 'active configuration model is missing'
Assert-Contains $configuration 'struct DisplayApplyResult' 'apply result model is missing'
Assert-Contains $configuration 'struct DisplayReconfigurationState' 'explicit reconfiguration state model is missing'
Assert-Contains $compositorHeader 'detectedDisplayInventory()' 'compositor must expose detected inventory'
Assert-Contains $compositorHeader 'activeDisplayConfiguration()' 'compositor must expose active configuration'
Assert-Contains $optionsHeader 'applySelectedDisplaySettings' 'Display Options must apply through the runtime transaction'
Assert-Contains $optionsHeader 'cancelSelectedDisplaySettings' 'Display Options must preserve cancel semantics'

# Detection metadata keeps connector state separate from actual usability.
foreach ($field in @('sourceType', 'backendId', 'outputId', 'connectorEnabled', 'operational', 'presentationReady', 'assignedX', 'assignedY', 'assignedWidth', 'assignedHeight', 'preferredX', 'preferredY', 'preferredWidth', 'preferredHeight', 'primaryCapable', 'mirrorCapable', 'extendCapable')) {
    Assert-Contains $model $field "display monitor metadata '$field' is missing"
}
Assert-Contains $options 'monitor.operational' 'Display Options must classify operational outputs independently of connectorEnabled'
Assert-Contains $options 'Connector state unavailable/disabled' 'Display Options must show connector state separately'
Assert-Contains $options 'monitor.name' 'Display Options must render the backend-provided output name'
Assert-Contains $backend 'VirtIO-GPU Output ' 'virtio-gpu inventory must provide the real output name'
Assert-Contains $backend 'makeDetectedDisplayInventory' 'virtio-gpu inventory must bridge into the shared detected model'

# Transaction ordering and bounded pause/rollback contract.
Assert-Ordered $applyBody @(
    'g_presentationPaused.store(true',
    'g_presentationBusy.load',
    'buildActiveDisplayConfiguration',
    'g_cfg = nextConfig',
    'result.validationFrameResult = nextActive.valid()'
) 'display apply transaction ordering is invalid'
Assert-Contains $applyBody 'g_displayReconfigurationRequested' 'apply state must track reconfigurationRequested'
Assert-Contains $applyBody 'g_displayReconfigurationInProgress' 'apply state must track reconfigurationInProgress'
Assert-Contains $applyBody 'presentationPaused' 'apply result must report presentation pause state'
Assert-Contains $applyBody 'rollbackAttempted' 'apply result must report rollback attempts'
Assert-Contains $applyBody 'rollbackSucceeded' 'apply result must report rollback success'
Assert-Contains $applyBody 'oldConfiguration' 'apply state must snapshot the old active configuration'
Assert-Contains $applyBody 'requestedConfiguration' 'apply state must retain the requested configuration'
Assert-Contains $applyBody 'appliedConfiguration' 'apply state must retain the applied configuration'
Assert-Contains $applyBody 'attempt < 32' 'apply path should use a bounded busy wait'
Assert-Contains $applyBody 'requestRepaint()' 'successful reconfiguration must force a validation repaint'
Assert-Contains $compositor 'g_presentationBusy.store(true' 'presenters must expose busy state'
Assert-Contains $compositor 'g_presentationPaused.load' 'presenters must stop while reconfiguration is paused'

# Extend is an assigned-geometry layout; Mirror is one logical viewport.
Assert-Contains $configuration 'requested.mode == DisplayModeKind::Extend' 'Extend validation path is missing'
Assert-Contains $configuration 'monitor.virtualX = entry->virtualX' 'Extend must consume persisted virtual origins'
Assert-Contains $configuration 'monitor.virtualY = entry->virtualY' 'Extend must consume persisted virtual origins'
Assert-Contains $configuration 'requested.mode == DisplayModeKind::Mirror' 'Mirror validation path is missing'
Assert-Contains $configuration 'monitor.virtualX = 0' 'Mirror outputs must use a shared logical origin'
Assert-Contains $configuration 'target.viewportOriginX = 0' 'Mirror targets must use one logical viewport'
Assert-Contains $configuration 'Mirror dimensions incompatible' 'Mirror must reject incompatible assigned dimensions'
Assert-Contains $configuration 'arbitrary resolution change is not supported' 'arbitrary mode changes must remain rejected'
Assert-Contains $configuration 'resourceId = 0' 'persisted monitor copies must clear temporary resource ids'
Assert-Contains $configuration 'backingVirtualAddress = 0' 'persisted monitor copies must clear temporary backing addresses'

# Primary selection owns taskbar routing, including mirrored duplication.
Assert-Contains $configuration 'active.taskbarMonitorId = active.primaryOutputId' 'active configuration must identify the taskbar monitor from primary output'
Assert-Contains $compositor 'hostedPrimaryTaskbarVisibleInViewport' 'taskbar visibility must consume the active display mode'
Assert-Contains $compositor 'syntheticMirrorModeActive' 'Mirror must be represented as a single logical desktop'
Assert-Contains $options 'Primary display' 'Display Options must show primary designation'
Assert-Contains $options 's_displayPrimaryDisplayId' 'Display Options must edit primary without immediate persistence'
Assert-Contains $options 's_displayStatus = "Applied successfully"' 'Display Options must show apply result status'

# Persistence is committed only after validation and excludes volatile backend state.
Assert-Contains $compositor 'if (commitPersistence)' 'runtime apply must make persistence an explicit commit step'
Assert-Contains $compositor 'DisplayOptionsStore::Save' 'runtime persistence must use the existing display store'
Assert-Contains $options 'DisplayOptionsStore::Save' 'Display Options must preserve the existing persisted model'
Assert-Contains $options 'publish(MsgType::MT_DesktopConfigReload' 'non-display settings must retain their existing reload path'
Assert-Contains $configuration 'requestedMonitorForPersistence' 'persisted arrangement must be backend-neutral'
Assert-Contains $configuration 'requested.resourceId = 0' 'persistent display identities must clear temporary resource ids'
Assert-Contains $configuration 'requested.backingVirtualAddress = 0' 'persistent display identities must clear virtual addresses'

# Keep the explicit safety exclusions out of this configuration path.
foreach ($forbidden in @('CMD_UPDATE_CURSOR', 'CMD_MOVE_CURSOR', 'RESOURCE_CREATE_3D', 'RESOURCE_CREATE_BLOB', 'CONTEXT_INIT', 'hotplug', 'HOTPLUG')) {
    Assert-True (-not $configuration.Contains($forbidden)) "display configuration must not add forbidden virtio-gpu feature '$forbidden'"
}
Assert-True (-not $configuration.Contains('resource resize')) 'display configuration must not resize resources'
Assert-True (-not $configuration.Contains('mode setting')) 'display configuration must not add arbitrary mode setting'
Assert-True (-not $compositor.Contains('presentVirtioGpuTarget')) 'hosted compositor must not bypass the QEMU-only backend gate'

Write-Host 'virtio-gpu Display Options source smoke passed.'
