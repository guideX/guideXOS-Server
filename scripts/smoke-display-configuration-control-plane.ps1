param()

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Read-Source([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Missing source file: $Path" }
    return Get-Content -LiteralPath $Path -Raw
}
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function RequireText([string]$Text, [string]$Needle, [string]$Message) {
    Require $Text.Contains($Needle) $Message
}
function RequireOrder([string]$Text, [string[]]$Needles, [string]$Message) {
    $last = -1
    foreach ($needle in $Needles) {
        $position = $Text.IndexOf($needle, [StringComparison]::Ordinal)
        Require ($position -gt $last) "$($Message): missing or out-of-order '$needle'"
        $last = $position
    }
}

$contract = Read-Source (Join-Path $Root 'display_configuration_command.h')
$serviceHeader = Read-Source (Join-Path $Root 'display_configuration_service.h')
$hostedService = Read-Source (Join-Path $Root 'display_configuration_service.cpp')
$kernelService = Read-Source (Join-Path $Root 'kernel\core\display_configuration_service.cpp')
$hostedOptions = Read-Source (Join-Path $Root 'display_options.cpp')
$hostedOptionsHeader = Read-Source (Join-Path $Root 'display_options.h')
$kernelOptions = Read-Source (Join-Path $Root 'kernel\core\kernel_apps.cpp')
$kernelOptionsHeader = Read-Source (Join-Path $Root 'kernel\core\include\kernel\kernel_apps.h')
$virtio = Read-Source (Join-Path $Root 'kernel\core\virtio_gpu.cpp')
$main = Read-Source (Join-Path $Root 'kernel\core\main.cpp')
$qemuSmoke = Read-Source (Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1')

RequireText $contract 'struct DisplayConfigurationCommand' 'versioned display command contract is missing'
RequireText $contract 'struct DisplayConfigurationResponse' 'versioned display response contract is missing'
RequireText $contract 'kDisplayConfigurationContractVersion' 'contract version is missing'
foreach ($field in @('structureSize', 'requestId', 'commandType', 'requestedConfiguration', 'accepted', 'completed', 'success', 'resultCode', 'validationResult', 'rollbackAttempted', 'rollbackSucceeded', 'persistenceCommitted', 'detectedConfiguration', 'activeConfiguration', 'diagnostic')) {
    RequireText $contract $field "contract field '$field' is missing"
}
Require (-not ($contract -match '\*')) 'shared display contract must not contain raw pointers'
foreach ($command in @('QueryDetectedConfiguration', 'QueryActiveConfiguration', 'QueryLastApplyResult', 'ApplyConfiguration', 'RestoreLastKnownGood', 'ForceValidationFrame')) {
    RequireText $contract $command "command '$command' is missing"
}
foreach ($resultCode in @('InvalidVersion', 'InvalidSize', 'InvalidCommand', 'InvalidConfiguration', 'BackendUnavailable', 'BackendBusy', 'OutputUnavailable', 'MirrorGeometryIncompatible', 'PresentationPauseTimeout', 'TargetRebuildFailed', 'ValidationFrameFailed', 'PersistenceFailed', 'RollbackSucceeded', 'RollbackFailed', 'QemuOnlyGateRequired', 'UnsupportedBackend')) {
    RequireText $contract $resultCode "result code '$resultCode' is missing"
}

RequireText $serviceHeader 'processPendingAtSafePoint' 'service safe-point hook is missing'
RequireText $hostedService 'std::atomic<bool> s_busy' 'hosted service must serialize transactions'
RequireText $hostedService 's_lastKnownGoodCommand' 'hosted service must retain last-known-good state'
RequireText $kernelService 'static bool s_busy' 'kernel service must serialize transactions'
RequireText $kernelService 'QEMU-only virtio-gpu backend' 'kernel service must retain the QEMU-only boundary'
RequireText $kernelService 'persist_configuration' 'kernel service persistence path is missing'
RequireText $kernelService 'apply_display_configuration_backend_layout' 'kernel service must call the backend adapter'
$kernelTransactionStart = $kernelService.IndexOf('kernel::serial::puts("Display config service: request="', [StringComparison]::Ordinal)
$kernelTransactionEnd = $kernelService.IndexOf('kernel::virtio::gpu::set_display_configuration_backend_presentation_paused(false)', $kernelTransactionStart, [StringComparison]::Ordinal)
Require ($kernelTransactionStart -ge 0 -and $kernelTransactionEnd -gt $kernelTransactionStart) 'kernel service transaction body is missing'
$kernelTransaction = $kernelService.Substring($kernelTransactionStart, $kernelTransactionEnd - $kernelTransactionStart)
RequireOrder $kernelTransaction @('set_display_configuration_backend_presentation_paused(true)', 'const bool applied = kernel::virtio::gpu::apply_display_configuration_backend_layout(', 'if (!update_input_layout', 'if (persist_configuration(response.activeConfiguration))') 'kernel service transaction ordering is invalid'
RequireText $virtio 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.' 'Mule Territory warning is missing from the active virtio-gpu path'
RequireText $virtio 'GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE' 'virtio-gpu backend must retain the QEMU probe gate'
RequireText $virtio 'GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE' 'virtio-gpu backend must retain the live QEMU gate'
RequireText $main 'qemu_display_configuration_control_proof::run' 'QEMU coordinator is not connected to the guest main path'

RequireText $hostedOptions 'DisplayConfigurationService::submit' 'hosted Display Options must use the service endpoint'
RequireText $hostedOptions 'applySelectedDisplaySettingsAndClose' 'hosted OK path is missing'
RequireText $hostedOptions 's_windowGeneration' 'hosted stale-window guard is missing'
RequireText $hostedOptions 'Cancel' 'hosted Cancel path is missing'
Require (-not $hostedOptions.Contains('Compositor::applyDisplayConfiguration(')) 'hosted Display Options must not own the compositor transaction'
RequireText $kernelOptions 'DisplayConfigurationService::submit' 'bare-metal Display Options must use the service endpoint'
RequireText $kernelOptions 'm_windowGeneration' 'bare-metal stale-window guard is missing'
RequireText $kernelOptionsHeader 'submitDisplayConfiguration(bool closeOnSuccess)' 'bare-metal Apply/OK adapter is missing'
RequireText $kernelOptions 'cancelDisplayConfiguration' 'bare-metal Cancel adapter is missing'

RequireText $qemuSmoke 'displayConfigurationControl' 'runtime smoke mode is missing'
RequireText $qemuSmoke 'DISPLAY_CONFIG_CAPTURE=' 'runtime smoke does not collect per-stage head captures'
RequireText $qemuSmoke 'GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE' 'runtime smoke is not gated to the QEMU proof build'
Require (-not $qemuSmoke.Contains('qmp.*guest') -and -not $qemuSmoke.Contains('QMP as internal')) 'QMP must remain a host-only harness mechanism'
Require (-not $contract.Contains('physicalAddress') -and -not $contract.Contains('resourcePointer') -and -not $contract.Contains('backingAddress')) 'contract must remain backend-neutral and must not expose address/resource fields'
Require (-not $virtio.Contains('VIRTIO_GPU_HOTPLUG') -and -not $virtio.Contains('CMD_HOTPLUG')) 'virtio display control path must not add hotplug commands'
Require (-not $virtio.Contains('RESOURCE_CREATE_3D') -and -not $virtio.Contains('RESOURCE_CREATE_BLOB')) 'virtio display control path must not add 3D/blob commands'
Require (-not $virtio.Contains('real Intel') -and -not $virtio.Contains('physical GPU BAR')) 'real-hardware GPU support must remain absent'

Write-Host 'display configuration control-plane source smoke passed.'
