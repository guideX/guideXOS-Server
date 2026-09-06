[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase1'
$buildScript = Join-Path $PSScriptRoot 'build-aarch64-phase1.ps1'
$runScript = Join-Path $PSScriptRoot 'run-aarch64-phase1.ps1'

Write-Host 'Rebuilding and staging fresh AArch64 Phase 1 artifacts...' -ForegroundColor Cyan
& $buildScript -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 1 build failed' }

$requiredMarkers = @(
    '[guideXOS] AARCH64 kernel entry',
    '[guideXOS] execution level: EL1',
    '[guideXOS] stack: OK',
    '[guideXOS] firmware handoff: OK',
    'AARCH64_PHASE1_PASS'
)
$fatalPattern = 'AARCH64_PHASE1_ERROR|\[guideXOS\].*(FAIL|ERROR)|\[A64 UEFI\] ERROR|FATAL'

function Assert-GoodBoot([string]$Text, [string]$Name) {
    if ($Text -match $fatalPattern) { throw "$Name contains an explicit fatal/error marker" }
    $last = -1
    foreach ($marker in $requiredMarkers) {
        $index = $Text.IndexOf($marker, [StringComparison]::Ordinal)
        if ($index -lt 0) { throw "$Name is missing marker: $marker" }
        if ($index -le $last) { throw "$Name markers are out of order at: $marker" }
        $last = $index
    }
}

$logsDirectory = Join-Path $artifactDirectory 'logs'
$null = New-Item -ItemType Directory -Path $logsDirectory -Force
for ($run = 1; $run -le 3; ++$run) {
    $logPath = Join-Path $logsDirectory ("boot-{0}.log" -f $run)
    Write-Host "Starting fresh QEMU boot $run/3..." -ForegroundColor Yellow
    & $runScript -ArtifactDirectory $artifactDirectory -QemuPath $QemuPath -FirmwareCode $FirmwareCode `
        -LogPath $logPath -TimeoutSeconds $TimeoutSeconds
    $runExit = $LASTEXITCODE
    $text = Get-Content -LiteralPath $logPath -Raw
    if ($runExit -ne 0) { throw "Fresh QEMU boot $run did not return a pass result" }
    Assert-GoodBoot $text ("fresh QEMU boot $run")
    Write-Host "Fresh QEMU boot $run/3: PASS" -ForegroundColor Green
}

Write-Host 'Running wrong-machine negative control...' -ForegroundColor Yellow
$negativeDirectory = Join-Path $artifactDirectory 'negative-machine'
if (Test-Path -LiteralPath $negativeDirectory) { Remove-Item -LiteralPath $negativeDirectory -Recurse -Force }
$null = New-Item -ItemType Directory -Path (Join-Path $negativeDirectory 'esp\EFI\BOOT') -Force
Copy-Item -LiteralPath (Join-Path $artifactDirectory 'esp\EFI\BOOT\BOOTAA64.EFI') `
    -Destination (Join-Path $negativeDirectory 'esp\EFI\BOOT\BOOTAA64.EFI') -Force
$validKernel = Join-Path $artifactDirectory 'esp\kernel.elf'
$wrongKernel = Join-Path $negativeDirectory 'esp\kernel.elf'
$wrongBytes = [IO.File]::ReadAllBytes($validKernel)
if ($wrongBytes.Length -lt 20) { throw 'Valid kernel fixture is unexpectedly truncated' }
$wrongBytes[18] = 0x3e
$wrongBytes[19] = 0x00
[IO.File]::WriteAllBytes($wrongKernel, $wrongBytes)
$negativeLog = Join-Path $negativeDirectory 'wrong-machine.log'
& $runScript -ArtifactDirectory $negativeDirectory -QemuPath $QemuPath -FirmwareCode $FirmwareCode `
    -LogPath $negativeLog -TimeoutSeconds $TimeoutSeconds
$negativeText = Get-Content -LiteralPath $negativeLog -Raw
if ($negativeText -match 'AARCH64_PHASE1_PASS') { throw 'Wrong-machine fixture unexpectedly reached kernel PASS' }
if ($negativeText -notmatch '\[A64 UEFI\] ERROR: incompatible ELF') {
    throw 'Loader did not reject the EM_X86_64 negative fixture before execution'
}
Write-Host 'Negative control: EM_X86_64 fixture rejected before execution' -ForegroundColor Green

Write-Host 'AARCH64 Phase 1 test suite: PASS (three fresh boots + negative control)' -ForegroundColor Green
exit 0
