<#
.SYNOPSIS
    Runs fresh disposable writable USB lifecycles with DM23 scan checks.
.DESCRIPTION
    Each run creates a new 80 MiB blank raw image, performs the complete USB
    Disk Manager lifecycle and restart proof, requires the full-scan telemetry
    regression gate, and preserves all evidence under out/.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 5)]
    [int]$Count = 3,
    [string]$EvidenceRoot = "",
    [string]$KernelImage = "kernel\build\dm23-usb-quiet\bin\kernel.elf",
    [string]$BootloaderImage = "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe",
    [string]$QemuAccelerator = "whpx",
    [switch]$EnableQemuTrace
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
if (-not $EvidenceRoot) {
    $EvidenceRoot = "out\dm23-usb-cohort-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
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
} else { New-Item -ItemType Directory -Path $base | Out-Null }

$runner = Join-Path $Root "scripts\run-dm13-qemu-usb-lifecycle.ps1"
$summary = Join-Path $base "cohort-summary.txt"
$rows = [System.Collections.Generic.List[string]]::new()
$rows.Add("schema=DM23-USB-SCAN-COHORT-1")
$rows.Add("countRequested=$Count")
$rows.Add("kernel=$([IO.Path]::GetFullPath((Join-Path $Root $KernelImage)))")
$rows.Add("qemuTraceEnabled=$(if ($EnableQemuTrace) { 'yes' } else { 'no' })")
$rows.Add("diskImageBytes=83886080")
$rows.Add("physicalHostDisksPassedToQemu=none")

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
    $runnerParameters = @{
        UsbImage = $imageRelative
        WorkDir = $workDir
        KernelImage = $KernelImage
        BootloaderImage = $BootloaderImage
        QemuAccelerator = $QemuAccelerator
        RequireDm23ScanMetrics = $true
    }
    if (-not $EnableQemuTrace) { $runnerParameters.DisableQemuTrace = $true }
    try {
        & $runner @runnerParameters
        $manifest = Join-Path (Join-Path $Root $workDir) "manifest.txt"
        $proofRows = Get-Content -LiteralPath $manifest
        if ($proofRows -notcontains "result=PASS private-write=yes shared-write=yes durability=trusted lifecycle=yes cold-restart-persistence=yes independent-image-verification=yes host-physical-media=none") {
            throw "Lifecycle runner did not record a complete PASS result."
        }
        $metrics = @($proofRows | Where-Object {
            $_ -match '^dm23Scan(?:Bytes|Requests|MaxRequestBytes|ElapsedTicks|MilliMiBPerSecond)='
        }) -join ';'
        $rows.Add("$runName=PASS image=$imageRelative elapsedSeconds=$([int]([DateTime]::UtcNow-$started).TotalSeconds) $metrics")
    } catch {
        $rows.Add("$runName=FAIL image=$imageRelative elapsedSeconds=$([int]([DateTime]::UtcNow-$started).TotalSeconds) reason=$($_.Exception.Message -replace '[\r\n]+',' ')")
        $rows.Add("completedRuns=$i")
        $rows.Add("cohortResult=FAIL")
        $rows | Set-Content -LiteralPath $summary -Encoding ascii
        throw
    }
    $rows | Set-Content -LiteralPath $summary -Encoding ascii
}

$rows.Add("completedRuns=$Count")
$rows.Add("cohortResult=PASS")
$rows | Set-Content -LiteralPath $summary -Encoding ascii
Write-Host "DM23 USB lifecycle cohort passed $Count/$Count. Evidence: $base"
