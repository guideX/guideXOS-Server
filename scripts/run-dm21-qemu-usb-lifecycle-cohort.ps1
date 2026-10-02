[CmdletBinding()]
param(
    [int]$Count = 10,
    [string]$EvidenceRoot = "out\dm21-evidence\lifecycle-cohort",
    [string]$KernelImage = "kernel\build\dm21-diagnostic\bin\kernel.elf",
    [string]$BootloaderImage = "out\dm21-evidence\bootloader\guideXOSBootLoader.exe",
    [string]$QemuAccelerator = "whpx"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
if ($Count -lt 1 -or $Count -gt 30) { throw "Count must be between 1 and 30." }
$base = [IO.Path]::GetFullPath((Join-Path $Root $EvidenceRoot))
$out = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $base.StartsWith($out + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "EvidenceRoot must be below repository out/."
}
if (Test-Path -LiteralPath $base) {
    if ((Get-ChildItem -LiteralPath $base -Force | Measure-Object).Count -ne 0) {
        throw "EvidenceRoot must be new or empty; existing evidence is preserved."
    }
} else { New-Item -ItemType Directory -Path $base -Force | Out-Null }
$runner = Join-Path $Root "scripts\run-dm13-qemu-usb-lifecycle.ps1"
$summary = Join-Path $base "cohort-summary.txt"
$rows = @("schema=DM21-USB-LIFECYCLE-COHORT-1",
          "countRequested=$Count",
          "kernel=$([IO.Path]::GetFullPath((Join-Path $Root $KernelImage)))",
          "traceEnabled=automatic",
          "qmpTranscript=per-boot",
          "images=fresh-disposable-80MiB-raw-per-run")

for ($i = 1; $i -le $Count; ++$i) {
    $runName = "run-{0:D2}" -f $i
    $imageName = "$runName.raw"
    $image = Join-Path $base $imageName
    $stream = [IO.File]::Open($image, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.SetLength(80L * 1024L * 1024L) } finally { $stream.Dispose() }
    $workDir = Join-Path $EvidenceRoot $runName
    $imageRelative = Join-Path $EvidenceRoot $imageName
    $started = [DateTime]::UtcNow
    try {
        & $runner -UsbImage $imageRelative -WorkDir $workDir `
            -KernelImage $KernelImage -BootloaderImage $BootloaderImage `
            -QemuAccelerator $QemuAccelerator
        $manifest = Join-Path (Join-Path $Root $workDir) "manifest.txt"
        $proof = Get-Content -LiteralPath $manifest -Raw
        if ($proof -notmatch '(?m)^result=PASS\b') {
            throw "Lifecycle runner did not write a PASS result."
        }
        $rows += "$runName=PASS image=$imageRelative elapsedSeconds=$([int]([DateTime]::UtcNow-$started).TotalSeconds)"
    } catch {
        $rows += "$runName=FAIL image=$imageRelative elapsedSeconds=$([int]([DateTime]::UtcNow-$started).TotalSeconds) reason=$($_.Exception.Message -replace '[\r\n]+',' ')"
        $rows += "completedRuns=$i"
        $rows += "cohortResult=FAIL"
        $rows | Set-Content -LiteralPath $summary -Encoding ascii
        throw
    }
    $rows | Set-Content -LiteralPath $summary -Encoding ascii
}

$rows += "completedRuns=$Count"
$rows += "cohortResult=PASS"
$rows | Set-Content -LiteralPath $summary -Encoding ascii
Write-Host "DM21 lifecycle cohort passed $Count/$Count. Evidence: $base"
