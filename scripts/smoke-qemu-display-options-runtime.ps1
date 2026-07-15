param(
    [int]$TimeoutSeconds = 120,
    [switch]$SkipRuntime
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Assert-True {
    param([Parameter(Mandatory = $true)][bool]$Condition, [Parameter(Mandatory = $true)][string]$Message)
    if (-not $Condition) { throw "[QemuDisplayOptionsRuntimeSmoke] $Message" }
}

function Read-Text {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { throw "Missing file: $Path" }
    return Get-Content -LiteralPath $Path -Raw
}

$configuration = Read-Text (Join-Path $Root 'display_configuration.h')
$compositor = Read-Text (Join-Path $Root 'compositor.cpp')
$options = Read-Text (Join-Path $Root 'display_options.cpp')
$probe = Read-Text (Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1')
$warning = 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.'

Assert-True $configuration.Contains('DisplayModeKind::Extend') 'Extend configuration must be represented'
Assert-True $configuration.Contains('DisplayModeKind::Mirror') 'Mirror configuration must be represented'
Assert-True $configuration.Contains('Mirror dimensions incompatible') 'Mirror incompatibility must be rejected'
Assert-True $configuration.Contains('requestedMonitorForPersistence') 'arrangement reconciliation must be present'
Assert-True $compositor.Contains('Display configuration request:') 'runtime request summary must be emitted'
Assert-True $compositor.Contains('Display configuration apply:') 'runtime apply summary must be emitted'
Assert-True $compositor.Contains('Active display configuration:') 'active configuration summary must be emitted'
Assert-True $compositor.Contains('presentationPaused') 'runtime apply must pause presentation'
Assert-True $compositor.Contains('rollbackSucceeded') 'runtime apply must expose rollback'
Assert-True ($options.Contains('Apply') -and $options.Contains('Cancel')) 'Display Options must expose Apply and Cancel'
Assert-True $options.Contains('Applied successfully') 'Display Options must show successful apply status'
Assert-True $probe.Contains('compositorLiveBounded') 'QEMU runtime probe must provide a bounded live mode'
Assert-True $probe.Contains('gpuOutputOperationalCount') 'QEMU runtime probe must capture operational output count'
Assert-True ($probe.Contains('gpuLiveTarget0Failures') -and $probe.Contains('gpuLiveTarget1Failures')) 'QEMU runtime probe must capture per-output GPU failures'
Assert-True $compositor.Contains($warning) 'runtime reconfiguration must retain the Mule Territory warning'

Write-Host '[QemuDisplayOptionsRuntimeSmoke] source checks passed.'

if (-not $SkipRuntime) {
    $probeScript = Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $probeScript `
        -Backends @('virtio-gpu') `
        -Mode compositorLiveBounded `
        -TimeoutSeconds $TimeoutSeconds
    if ($LASTEXITCODE -ne 0) {
        throw "[QemuDisplayOptionsRuntimeSmoke] bounded QEMU inventory/presentation runtime failed with exit code $LASTEXITCODE."
    }
}

Write-Host '[QemuDisplayOptionsRuntimeSmoke] passed.'
