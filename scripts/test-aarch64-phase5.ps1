[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$TimeoutSeconds = 180,
    [int]$HistoricalTimeoutSeconds = 120,
    [switch]$SkipHistoricalRegressions
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase5'
& (Join-Path $PSScriptRoot 'build-aarch64-phase5.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 5 build failed' }

$appElf = Join-Path $artifactDirectory 'phase5-arm64-proof.elf'
$appReadobj = Get-Content -Raw (Join-Path $artifactDirectory 'phase5-app-llvm-readobj.txt')
if ($appReadobj -notmatch 'Type: Executable' -or $appReadobj -notmatch 'Machine: EM_AARCH64' -or $appReadobj -notmatch 'Name: gx_main') { throw 'Phase 5 application artifact metadata is incomplete' }
if (!(Test-Path (Join-Path $artifactDirectory 'ramdisk-root\Apps\Phase5Arm64Proof\app.json'))) { throw 'staged application manifest is missing from the ramdisk staging root' }

$requiredMarkers = @(
    '[guideXOS] VFS root: OK','[guideXOS] common kernel entry: OK','[guideXOS] App Model: initialized',
    '[guideXOS] App discovery: ARM64 proof app found','[guideXOS] NativeElf architecture: ARM64',
    '[guideXOS] NativeElf ELF validation: PASS','[guideXOS] NativeElf segments: loaded',
    '[guideXOS] NativeElf icache sync: PASS','[guideXOS] NativeElf application stack: OK',
    '[phase5-app] gx_main entered','[phase5-app] architecture: arm64','[phase5-app] ABI: OK',
    '[phase5-app] computation: PASS','[phase5-app] returning 42','[guideXOS] NativeElf ARM64 return: 42',
    '[guideXOS] NativeElf cleanup: PASS','[guideXOS] ARM64 App Model relaunch: PASS',
    '[guideXOS] NativeElf wrong architecture: rejected','[guideXOS] App Model durability: PASS','AARCH64_PHASE5_PASS')
function Assert-GoodBoot([string]$Text, [string]$Name) {
    if ($Text -match 'AARCH64_PHASE[2345]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') { throw "$Name contains an error marker" }
    $last = -1; foreach ($marker in $requiredMarkers) { $index = $Text.IndexOf($marker, [StringComparison]::Ordinal); if ($index -lt 0) { throw "$Name is missing marker: $marker" }; if ($index -le $last) { throw "$Name markers are out of order at: $marker" }; $last = $index }
    if ($Text -notmatch 'App Model durability: PASS launches=100 completed=100 allocator-delta-pages=0') { throw "$Name lacks durability statistics" }
    if ($Text -notmatch 'preemptions=1[0-9]{4,}') { throw "$Name lacks active preemption statistics" }
    if ($Text -notmatch 'unexpected-irq=0 exceptions=0') { throw "$Name reports unexpected IRQs or exceptions" }
    if (([regex]::Matches($Text, '\[phase5-app\] gx_main entered')).Count -lt 100) { throw "$Name did not enter gx_main 100 times" }
}
$logsDirectory = Join-Path $artifactDirectory 'logs'; $null = New-Item -ItemType Directory -Path $logsDirectory -Force
$varsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'; if (!(Test-Path $varsTemplate)) { throw 'AArch64 UEFI variable template not found' }
for ($run = 1; $run -le 3; ++$run) {
    $varsPath = Join-Path $artifactDirectory ("edk2-aarch64-vars-{0}.fd" -f $run); Copy-Item $varsTemplate $varsPath -Force
    $logPath = Join-Path $logsDirectory ("boot-{0}.log" -f $run); Write-Host "Starting fresh AArch64 Phase 5 QEMU boot $run/3..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot 'run-aarch64-phase5.ps1') -ArtifactDirectory $artifactDirectory -QemuPath $QemuPath -FirmwareCode $FirmwareCode -FirmwareVars $varsPath -LogPath $logPath -TimeoutSeconds $TimeoutSeconds
    $exitCode = $LASTEXITCODE; $text = Get-Content -Raw $logPath; if ($exitCode -ne 0) { throw "Fresh Phase 5 QEMU boot $run did not pass" }; Assert-GoodBoot $text "fresh Phase 5 QEMU boot $run"; Write-Host "Fresh Phase 5 QEMU boot $run/3: PASS" -ForegroundColor Green
}
if (!$SkipHistoricalRegressions) {
    foreach ($phase in 1..4) {
        $script = Join-Path $PSScriptRoot "test-aarch64-phase$phase.ps1"
        $phaseTimeout = $HistoricalTimeoutSeconds
        if ($phase -eq 4 -and $phaseTimeout -lt 240) { $phaseTimeout = 240 }
        & $script -TimeoutSeconds $phaseTimeout
        if ($LASTEXITCODE -ne 0) { throw "Historical Phase $phase regression failed" }
    }
}
Write-Host 'AARCH64 Phase 5 test suite: PASS (host controls + three fresh boots + regressions)' -ForegroundColor Green
