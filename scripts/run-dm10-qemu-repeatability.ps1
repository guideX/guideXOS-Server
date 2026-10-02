<#
.SYNOPSIS
    Runs five consecutive fresh-image DM10 ATA/QEMU lifecycle proofs.

.DESCRIPTION
    Each attempt gets a newly created 600 MiB raw secondary image and its own
    preserved output directory. The packaged proof restarts QEMU with the same
    image and independently verifies its GPT/FAT32 contents. The first failure
    stops the gate and is preserved with the aggregate manifest.
#>
[CmdletBinding()]
param(
    [string]$EspSource = "ESP",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$PythonExecutable = "",
    [UInt64]$DiskSizeBytes = 629145600,
    [switch]$QemuDebug,
    [switch]$SkipBuild,
    [string]$WorkDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $WorkDir) {
    $WorkDir = "out\dm10-qemu-repeatability-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "WorkDir must be a new directory below $repoOut"
}
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir already contains files. Choose a new directory to preserve prior evidence."
    }
} else {
    New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null
}

$proofScript = Join-Path $Root "scripts\run-dm9-qemu-proof.ps1"
$summaryPath = Join-Path $WorkFull "repeatability-manifest.txt"
$passed = 0
$built = [bool]$SkipBuild
$entries = [System.Collections.Generic.List[string]]::new()
$entries.Add("proof=DM10-ATA-QEMU-FRESH-IMAGE-REPEATABILITY")
$entries.Add("attemptsRequired=5")
$entries.Add("timestampUtc=$([DateTime]::UtcNow.ToString('o'))")
$entries.Add("hostPhysicalDiskAttached=no")

for ($attempt = 1; $attempt -le 5; ++$attempt) {
    $attemptName = "attempt-{0:D2}" -f $attempt
    $attemptDir = Join-Path $WorkDir $attemptName
    $proofArgs = @{
        EspSource = $EspSource
        WorkDir = $attemptDir
        QemuExecutable = $QemuExecutable
        OvmfCode = $OvmfCode
        AttemptNumber = $attempt
        DiskSizeBytes = $DiskSizeBytes
        SkipBuild = $built
    }
    if ($PythonExecutable) { $proofArgs.PythonExecutable = $PythonExecutable }
    if ($QemuDebug) { $proofArgs.QemuDebug = $true }

    try {
        & $proofScript @proofArgs
        $built = $true
        ++$passed
        $childManifest = Join-Path (Join-Path $Root $attemptDir) "dm10-manifest.txt"
        $entries.Add("attempt.$attempt=result=PASS")
        $entries.Add("attempt.$attempt.manifest=$childManifest")
    } catch {
        $failureText = $_.Exception.Message -replace '[\r\n]+', ' '
        $entries.Add("attempt.$attempt=result=FAIL")
        $entries.Add("attempt.$attempt.detail=$failureText")
        $childManifest = Join-Path (Join-Path $Root $attemptDir) "dm10-manifest.txt"
        if (Test-Path -LiteralPath $childManifest) {
            $child = Get-Content -LiteralPath $childManifest
            foreach ($line in $child) {
                if ($line -match '^failedStage=') { $entries.Add("attempt.$attempt.$line") }
                if ($line -match '^secondaryInitialSha256=|^secondaryFinalSha256=|^writesOccurred=|^writeMayHaveReachedMedia=') {
                    $entries.Add("attempt.$attempt.$line")
                }
            }
        }
        break
    }
}

$outcome = if ($passed -eq 5) { "PASS" } else { "FAIL" }
$entries.Add("result=$outcome")
$entries.Add("passed=$passed/5")
$entries.Add("independentImageVerification=per-attempt")
$entries.Add("rebootRediscoveryAndRemount=per-attempt")
$entries | Set-Content -LiteralPath $summaryPath -Encoding ascii
Write-Host "DM10 QEMU repeatability gate: $outcome ($passed/5). Preserved evidence: $WorkFull"
if ($passed -ne 5) { throw "DM10 repeatability gate failed after $passed of 5 fresh attempts. See $summaryPath" }
