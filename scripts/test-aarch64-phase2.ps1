[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase2'
& (Join-Path $PSScriptRoot 'build-aarch64-phase2.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 2 build failed' }

$requiredMarkers = @(
    '[guideXOS] AARCH64 kernel entry', '[guideXOS] execution level: EL1', '[guideXOS] stack: OK',
    '[guideXOS] firmware handoff: OK', '[guideXOS] DTB: OK', '[guideXOS] MMU tables: built',
    '[guideXOS] MMU: guideXOS tables active', '[guideXOS] exception vectors: OK',
    '[guideXOS] synchronous exception self-test: PASS', '[guideXOS] physical memory: OK',
    '[guideXOS] early allocator: PASS', '[guideXOS] GIC: OK', '[guideXOS] timer IRQ: PASS',
    'AARCH64_PHASE2_PASS'
)
$fatalPattern = 'AARCH64_PHASE2_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR'
function Assert-GoodBoot([string]$Text, [string]$Name) {
    if ($Text -match $fatalPattern) { throw "$Name contains a fatal/error marker" }
    $last = -1
    foreach ($marker in $requiredMarkers) {
        $index = $Text.IndexOf($marker, [StringComparison]::Ordinal)
        if ($index -lt 0) { throw "$Name is missing marker: $marker" }
        if ($index -le $last) { throw "$Name markers are out of order at: $marker" }
        $last = $index
    }
    if ($Text -notmatch '\[A64 UEFI\] DTB: copied from EFI configuration table') { throw "$Name lacks loader DTB-copy evidence" }
    if ($Text -notmatch 'timer IRQ: PASS count=1 \(returned from IRQ\)') { throw "$Name lacks post-IRQ return evidence" }
}

$logsDirectory = Join-Path $artifactDirectory 'logs'
$null = New-Item -ItemType Directory -Path $logsDirectory -Force
for ($run = 1; $run -le 3; ++$run) {
    $logPath = Join-Path $logsDirectory ("boot-{0}.log" -f $run)
    Write-Host "Starting fresh AArch64 Phase 2 QEMU boot $run/3..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot 'run-aarch64-phase2.ps1') -ArtifactDirectory $artifactDirectory -QemuPath $QemuPath -FirmwareCode $FirmwareCode -LogPath $logPath -TimeoutSeconds $TimeoutSeconds
    $runExit = $LASTEXITCODE
    $text = Get-Content -LiteralPath $logPath -Raw
    if ($runExit -ne 0) { throw "Fresh Phase 2 QEMU boot $run did not return a pass result" }
    Assert-GoodBoot $text ("fresh Phase 2 QEMU boot $run")
    Write-Host "Fresh Phase 2 QEMU boot $run/3: PASS" -ForegroundColor Green
}
Write-Host 'AARCH64 Phase 2 test suite: PASS (three fresh boots + host negative controls)' -ForegroundColor Green
exit 0
