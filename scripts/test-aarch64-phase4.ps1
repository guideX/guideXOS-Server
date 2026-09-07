[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase4'
& (Join-Path $PSScriptRoot 'build-aarch64-phase4.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 4 build failed' }

$requiredMarkers = @(
    '[guideXOS] AARCH64 kernel entry', '[guideXOS] execution level: EL1', '[guideXOS] stack: OK',
    '[guideXOS] firmware handoff: OK', '[guideXOS] DTB: OK', '[guideXOS] MMU: guideXOS tables active',
    '[guideXOS] exception vectors: OK', '[guideXOS] common boot resources: OK',
    '[guideXOS] common physical allocator: PASS', '[guideXOS] kernel heap: PASS',
    '[guideXOS] ramdisk: mounted', '[guideXOS] VFS root: OK',
    '[guideXOS] VFS directory enumeration: PASS', '[guideXOS] VFS file read: PASS',
    '[guideXOS] VFS write/read: PASS', '[guideXOS] common kernel entry: OK',
    '[guideXOS] architecture interface: ARM64', '[guideXOS] scheduler: initialized',
    '[guideXOS] ARM64 thread context: OK', '[guideXOS] cooperative scheduling: PASS',
    '[guideXOS] scheduler/VFS integration: PASS', '[guideXOS] memory/VFS durability: PASS',
    'AARCH64_PHASE4_PASS'
)
function Assert-GoodBoot([string]$Text, [string]$Name) {
    if ($Text -match 'AARCH64_PHASE[234]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') {
        throw "$Name contains an error marker"
    }
    $last = -1
    foreach ($marker in $requiredMarkers) {
        $index = $Text.IndexOf($marker, [StringComparison]::Ordinal)
        if ($index -lt 0) { throw "$Name is missing marker: $marker" }
        if ($index -le $last) { throw "$Name markers are out of order at: $marker" }
        $last = $index
    }
    if ($Text -notmatch 'common physical allocator: PASS pages-total=[1-9][0-9]* pages-free=[1-9][0-9]*') { throw "$Name lacks allocator statistics" }
    if ($Text -notmatch 'heap-alloc-cycles=1000') { throw "$Name lacks heap cycle statistics" }
    if ($Text -notmatch 'scheduler/VFS integration: PASS reads=[1-9][0-9]* enumerations=[1-9][0-9]*') { throw "$Name lacks repeated VFS workload statistics" }
    if ($Text -notmatch 'preemptions=1[0-9]{4,}') { throw "$Name lacks the 10,000-preemption proof" }
    if ($Text -notmatch 'unexpected-irq=0 exceptions=0') { throw "$Name reports an unexpected IRQ or exception" }
}

$logsDirectory = Join-Path $artifactDirectory 'logs'
$null = New-Item -ItemType Directory -Path $logsDirectory -Force
$varsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
if (!(Test-Path -LiteralPath $varsTemplate -PathType Leaf)) { throw "AArch64 UEFI variable template not found: $varsTemplate" }
for ($run = 1; $run -le 3; ++$run) {
    $varsPath = Join-Path $artifactDirectory ("edk2-aarch64-vars-{0}.fd" -f $run)
    Copy-Item -LiteralPath $varsTemplate -Destination $varsPath -Force
    $logPath = Join-Path $logsDirectory ("boot-{0}.log" -f $run)
    Write-Host "Starting fresh AArch64 Phase 4 QEMU boot $run/3..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot 'run-aarch64-phase4.ps1') -ArtifactDirectory $artifactDirectory -QemuPath $QemuPath -FirmwareCode $FirmwareCode -FirmwareVars $varsPath -LogPath $logPath -TimeoutSeconds $TimeoutSeconds
    $runExit = $LASTEXITCODE
    $text = Get-Content -LiteralPath $logPath -Raw
    if ($runExit -ne 0) { throw "Fresh Phase 4 QEMU boot $run did not return a pass result" }
    Assert-GoodBoot $text ("fresh Phase 4 QEMU boot $run")
    Write-Host "Fresh Phase 4 QEMU boot $run/3: PASS" -ForegroundColor Green
}
Write-Host 'AARCH64 Phase 4 test suite: PASS (host controls + three fresh boots)' -ForegroundColor Green
exit 0
