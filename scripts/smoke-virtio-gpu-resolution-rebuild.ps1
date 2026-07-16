param(
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Invoke-BoundedSmoke([string]$ScriptPath, [string[]]$Arguments) {
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 0) { throw "Smoke failed: $ScriptPath (exit code $exitCode)" }
    return $output
}

function Latest-Serial([string]$Pattern) {
    $logs = @(Get-ChildItem -LiteralPath (Join-Path $Root 'logs') -Recurse -Filter 'serial.log' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like $Pattern } | Sort-Object LastWriteTime -Descending)
    if ($logs.Count -eq 0) { throw "No serial log matched $Pattern" }
    return $logs[0].FullName
}

function Require([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) { throw $Message }
}

$controlScript = Join-Path $Root 'scripts\smoke-virtio-gpu-display-configuration-control.ps1'
$persistenceScript = Join-Path $Root 'scripts\smoke-virtio-gpu-display-configuration-persistence.ps1'

Write-Host '[resolution-rebuild] running bounded QEMU-only control/rebuild proof'
$oldResolutionProofFlag = $env:GXOS_QEMU_DISPLAY_RESOLUTION_REBUILD_PROOF
try {
    $env:GXOS_QEMU_DISPLAY_RESOLUTION_REBUILD_PROOF = '1'
    [void](Invoke-BoundedSmoke $controlScript @('-TimeoutSeconds', [string]$TimeoutSeconds))
} finally {
    if ([string]::IsNullOrWhiteSpace($oldResolutionProofFlag)) {
        Remove-Item Env:\GXOS_QEMU_DISPLAY_RESOLUTION_REBUILD_PROOF -ErrorAction SilentlyContinue
    } else {
        $env:GXOS_QEMU_DISPLAY_RESOLUTION_REBUILD_PROOF = $oldResolutionProofFlag
    }
}
$controlSerial = Latest-Serial '*qemu-display-probe-*serial.log'
$controlText = Get-Content -LiteralPath $controlSerial -Raw

$finalProof = 'VirtioGPU resolution proof: equalExtend=ok mixedExtend=ok mirrorCompatible=ok mirrorMismatchRejected=ok primarySwitch=ok rollback=ok repeatedChanges=ok persistenceLaunch2=pending gpuFailures=0 fallback=no result=success'
Require $controlText ([regex]::Escape($finalProof)) 'the in-process QEMU logical-resolution proof did not complete successfully'
Require $controlText 'Display mode rebuild: output=2 scanout=1 old=1280x800 new=1024x768 oldResource=\d+ newResource=\d+ .*physicalCoverage=valid prepared=yes' 'mixed replacement-resource preparation evidence is missing'
Require $controlText 'Display configuration apply: mode=Extend output1=1280x800 output2=1024x768 virtualDesktop=2304x800 targets=2 validation=ok cleanup=ok' 'mixed Extend commit evidence is missing'
Require $controlText 'Display mode rebuild: result=failed stage=post-bind-validation rollback=yes.*provisionalReleased=yes' 'rollback/rebind evidence is missing'
Require $controlText 'Display resource lifecycle: created=\d+ committed=\d+ rolledBack=\d+ unreferenced=\d+ activeBacking=2 cleanupFailures=0' 'resource lifecycle cleanup evidence is missing'
if ($controlText -match 'gpuFailures=[1-9]|fallbackPatterns=yes|fallback=yes') { throw 'resolution control proof reported a GPU failure or fallback' }

$controlRunRoot = Split-Path -Parent $controlSerial
$controlCaptureRoot = Join-Path $controlRunRoot 'captures'
$stages = @('resolution-initial-inventory', 'resolution-equal-extend', 'resolution-mixed-extend', 'resolution-mirror-compatible', 'resolution-mirror-mismatch', 'resolution-primary-2', 'resolution-rollback', 'resolution-final-equal')
foreach ($stage in $stages) {
    foreach ($head in @(0, 1)) {
        $capture = Join-Path $controlCaptureRoot ("display-{0}-head{1}.png" -f $stage, $head)
        if (-not (Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).Length -le 0) {
            throw "missing resolution capture: $capture"
        }
    }
}

Write-Host '[resolution-rebuild] running separate-process per-output persistence proof'
[void](Invoke-BoundedSmoke $persistenceScript @('-TimeoutSeconds', [string]$TimeoutSeconds))
$persistenceSerials = @(Get-ChildItem -LiteralPath (Join-Path $Root 'logs') -Recurse -Filter 'launch2.serial.log' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending)
if ($persistenceSerials.Count -eq 0) { throw 'persistence smoke did not produce a launch 2 serial log' }
$launch2Serial = $persistenceSerials[0].FullName
$launch2Text = Get-Content -LiteralPath $launch2Serial -Raw
Require $launch2Text 'VirtioGPU resolution proof: persistenceLaunch2=ok restoredModes=ok virtualDesktop=2304x800 primary=Display 2 taskbar=Display 2 gpuFailures=0 fallback=no result=success' 'second-launch per-output resolution restoration evidence is missing'
if ($launch2Text -match 'gpuFailures=[1-9]|fallback=yes|fallbackPatterns=yes') { throw 'persistence launch 2 reported a GPU failure or fallback' }

$persistenceRunRoot = Split-Path -Parent $launch2Serial
foreach ($launch in @('launch1', 'launch2')) {
    foreach ($head in @(0, 1)) {
        $capture = Join-Path $persistenceRunRoot ("{0}\captures\head{1}.png" -f $launch, $head)
        if (-not (Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).Length -le 0) {
            throw "missing persistence capture: $capture"
        }
    }
}

Write-Output 'VirtioGPU resolution proof: equalExtend=ok mixedExtend=ok mirrorCompatible=ok mirrorMismatchRejected=ok primarySwitch=ok rollback=ok repeatedChanges=ok persistenceLaunch2=ok gpuFailures=0 fallback=no result=success'
Write-Output "Control serial: $controlSerial"
Write-Output "Persistence launch 2 serial: $launch2Serial"
