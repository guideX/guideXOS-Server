param(
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ProbeScript = Join-Path $Root 'scripts\smoke-qemu-display-probe.ps1'

Write-Host '[display-control] running the bounded QEMU guest control-plane coordinator'
$previousErrorAction = $ErrorActionPreference
$controlConfigStore = Join-Path $Root 'logs\display-control-config-store.img'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $controlConfigStore) | Out-Null
$persistenceSmoke = Join-Path $Root 'scripts\smoke-virtio-gpu-display-configuration-persistence.ps1'
$formatterOutput = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $persistenceSmoke -FormatOnly -ImagePath $controlConfigStore 2>&1 | ForEach-Object { [string]$_ })
if ($LASTEXITCODE -ne 0) { throw "failed to create the control-plane persistent config image: $($formatterOutput -join ' ')" }
$previousConfigStore = $env:GXOS_QEMU_DISPLAY_PROBE_CONFIG_DIR
$env:GXOS_QEMU_DISPLAY_PROBE_CONFIG_DIR = $controlConfigStore
$ErrorActionPreference = 'Continue'
$probeOutput = @()
$probeExitCode = 1
try {
    $probeOutput = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $ProbeScript `
        -Backends virtio-gpu -Mode displayConfigurationControl -TimeoutSeconds $TimeoutSeconds 2>&1 | ForEach-Object { [string]$_ })
    $probeExitCode = $LASTEXITCODE
} finally {
    if ([string]::IsNullOrWhiteSpace($previousConfigStore)) {
        Remove-Item Env:\GXOS_QEMU_DISPLAY_PROBE_CONFIG_DIR -ErrorAction SilentlyContinue
    } else {
        $env:GXOS_QEMU_DISPLAY_PROBE_CONFIG_DIR = $previousConfigStore
    }
}
$ErrorActionPreference = $previousErrorAction
$probeOutput | ForEach-Object { Write-Host $_ }
if ($probeExitCode -ne 0) { throw "QEMU display probe wrapper failed with exit code $probeExitCode" }

$serialLogs = @(Get-ChildItem -LiteralPath (Join-Path $Root 'logs') -Recurse -Filter 'serial.log' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending)
if ($serialLogs.Count -eq 0) { throw 'display control runtime smoke did not produce a serial log' }
$serialPath = $serialLogs[0].FullName
$serialText = Get-Content -LiteralPath $serialPath -Raw

$proofPattern = 'Display configuration control proof: query=ok mirrorApply=ok extendRestore=ok primary2Apply=ok taskbarMoved=yes primary1Restore=ok rollbackInjection=ok rollbackSucceeded=yes presentationResumed=yes gpuFailures=0 result=success'
if ($serialText -notmatch [regex]::Escape($proofPattern)) {
    throw "display configuration control proof failed or was incomplete. Serial log: $serialPath"
}

$captureRoot = Split-Path -Parent $serialPath
$captureRoot = Join-Path $captureRoot 'captures'
$stages = @('initial', 'mirror', 'extend', 'primary-2', 'primary-1', 'rollback')
$missing = @()
foreach ($stage in $stages) {
    foreach ($head in @(0, 1)) {
        $path = Join-Path $captureRoot ("display-{0}-head{1}.png" -f $stage, $head)
        if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -le 0) { $missing += $path }
    }
}
if ($missing.Count -gt 0) { throw "display control head captures missing: $($missing -join ', ')" }

$stageChecks = @(
    'Display configuration bridge: request=',
    'Display configuration proof stage=mirror',
    'Display configuration proof stage=extend',
    'Display configuration proof stage=primary-2',
    'Display configuration proof stage=primary-1',
    'Display configuration proof stage=rollback-injection',
    'injected-validation-failure'
)
foreach ($check in $stageChecks) {
    if ($serialText -notmatch [regex]::Escape($check)) { throw "display control serial evidence missing: $check" }
}
if ($serialText -match 'fallbackPatterns=yes|targetFailures=[1-9]|gpuFailures=[1-9]') {
    throw 'display control runtime reported a GPU failure or diagnostic fallback'
}

Write-Output "Virtio-gpu display configuration control smoke PASS. Serial log: $serialPath"
Write-Output "Head captures: $captureRoot"
