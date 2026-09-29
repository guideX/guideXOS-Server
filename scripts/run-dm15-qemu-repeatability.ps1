<#
.SYNOPSIS
    Gates DM15 AHCI writes and flush before five fresh lifecycle runs.

.DESCRIPTION
    First builds and runs the private read-only-registration proof, which writes
    one zero sector, flushes, reads it back, restores it, and checks the entire
    raw-image hash. Only after that succeeds does the first lifecycle build
    register normal shared AHCI writes. Five new raw images then run the full
    initialize/create/format/file/remount/restart verification sequence.
#>
[CmdletBinding()]
param(
    [string]$EspSource = "ESP",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$PythonExecutable = "",
    [switch]$QemuDebug,
    [string]$WorkDir = "",
    [string]$EspCacheDirectory = "",
    [string]$PriorPrivateProofManifest = "",
    [string[]]$PriorLifecycleManifest = @()
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
if (-not $WorkDir) { $WorkDir = "out\dm15-qemu-repeatability-$(Get-Date -Format 'yyyyMMdd-HHmmss')" }
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "WorkDir must be below $repoOut" }
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir already contains files; select a fresh output directory."
    }
} else { New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null }

$proofScript = Join-Path $Root "scripts\run-dm15-qemu-proof.ps1"
$lifecycleEspCache = if ($EspCacheDirectory) { $EspCacheDirectory } else {
    Join-Path $WorkDir "lifecycle-esp-cache"
}
$summaryPath = Join-Path $WorkFull "repeatability-manifest.txt"
$entries = [System.Collections.Generic.List[string]]::new()
$entries.Add("proof=DM15-AHCI-QEMU-FRESH-IMAGE-REPEATABILITY")
$entries.Add("timestampUtc=$([DateTime]::UtcNow.ToString('o'))")
$entries.Add("lifecycleAttemptsRequired=5")
$entries.Add("hostPhysicalDiskAttached=no")
$passed = 0
$sharedBuildReady = $false

function Read-ManifestValue([string]$Text, [string]$Key) {
    $match = [regex]::Match($Text, "(?m)^$([regex]::Escape($Key))=(.*)$")
    if (-not $match.Success) { throw "Prior proof manifest is missing '$Key'." }
    return $match.Groups[1].Value.Trim()
}

function Resolve-PriorManifest([string]$Path, [string]$ExpectedProof) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Prior $ExpectedProof manifest does not exist: $Path"
    }
    $full = (Resolve-Path -LiteralPath $Path).Path
    $text = Get-Content -LiteralPath $full -Raw
    if ((Read-ManifestValue $text "proof") -ne $ExpectedProof) {
        throw "Prior manifest is not proof '$ExpectedProof': $full"
    }
    return [pscustomobject]@{ Path=$full; Text=$text }
}

try {
    if ($PriorPrivateProofManifest) {
        $priorPrivate = Resolve-PriorManifest $PriorPrivateProofManifest "DM15-AHCI-PrivateWrite"
        $privateResult = Read-ManifestValue $priorPrivate.Text "result"
        if ($privateResult -ne "PASS tier=1-private-write-readback-flush-restore" -or
            (Read-ManifestValue $priorPrivate.Text "sharedWriteRegistration") -ne "disabled" -or
            (Read-ManifestValue $priorPrivate.Text "imageRestoredByteForByte") -ne "yes" -or
            (Read-ManifestValue $priorPrivate.Text "secondaryInitialSha256") -ne
                (Read-ManifestValue $priorPrivate.Text "secondaryFinalSha256")) {
            throw "Prior private proof did not pass the private-write, flush, readback, and restore gate."
        }
        $entries.Add("privateWriteProof=PASS sourceManifest=$($priorPrivate.Path) imageRestoredByteForByte=yes sharedWriteRegistration=disabled")
    } else {
        $privateArgs = @{
            Stage = "PrivateWrite"
            EspSource = $EspSource
            EspCacheDirectory = Join-Path $WorkDir "private-write-esp-cache"
            WorkDir = Join-Path $WorkDir "private-write"
            QemuExecutable = $QemuExecutable
            OvmfCode = $OvmfCode
            AttemptNumber = 1
        }
        if ($PythonExecutable) { $privateArgs.PythonExecutable = $PythonExecutable }
        if ($QemuDebug) { $privateArgs.QemuDebug = $true }
        & $proofScript @privateArgs
        $entries.Add("privateWriteProof=PASS imageRestoredByteForByte=yes sharedWriteRegistration=disabled")
    }

    if ($PriorLifecycleManifest.Count -gt 0) {
        foreach ($priorPath in $PriorLifecycleManifest) {
            if ($passed -ge 5) { throw "At most five prior lifecycle manifests may be counted." }
            $priorLifecycle = Resolve-PriorManifest $priorPath "DM15-AHCI-Lifecycle"
            if ((Read-ManifestValue $priorLifecycle.Text "result") -ne "PASS tier=2-full-lifecycle-and-restart-rediscovery" -or
                (Read-ManifestValue $priorLifecycle.Text "inspection") -ne "PASS read-only-GPT-FAT32-independent-verifier" -or
                (Read-ManifestValue $priorLifecycle.Text "failedStage") -ne "none" -or
                (Read-ManifestValue $priorLifecycle.Text "writesOccurred") -ne "yes") {
                throw "Prior lifecycle manifest is missing the full lifecycle and image-verification pass."
            }
            $priorKernel = Read-ManifestValue $priorLifecycle.Text "kernelSha256"
            $priorBootloader = Read-ManifestValue $priorLifecycle.Text "bootloaderSha256"
            $currentKernel = (Get-FileHash -LiteralPath (Join-Path $Root "kernel\build\amd64\bin\kernel.elf") -Algorithm SHA256).Hash
            $currentBootloader = (Get-FileHash -LiteralPath (Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe") -Algorithm SHA256).Hash
            if ($priorKernel -ne $currentKernel -or $priorBootloader -ne $currentBootloader) {
                throw "Prior lifecycle binaries do not match the current shared-write kernel and UEFI bootloader."
            }
            $priorImage = Read-ManifestValue $priorLifecycle.Text "secondaryImage"
            $priorFirstSerial = Join-Path (Split-Path -Parent $priorLifecycle.Path) (Read-ManifestValue $priorLifecycle.Text "firstBootSerial")
            $priorRediscoverySerial = Join-Path (Split-Path -Parent $priorLifecycle.Path) (Read-ManifestValue $priorLifecycle.Text "rediscoverySerial")
            $priorInspection = Join-Path (Split-Path -Parent $priorLifecycle.Path) "disk-inspection.txt"
            if (-not (Test-Path -LiteralPath $priorImage -PathType Leaf) -or
                -not (Test-Path -LiteralPath $priorFirstSerial -PathType Leaf) -or
                -not (Test-Path -LiteralPath $priorRediscoverySerial -PathType Leaf) -or
                -not (Test-Path -LiteralPath $priorInspection -PathType Leaf)) {
                throw "Prior lifecycle artifacts are incomplete; refusing to count the attempt."
            }
            if ((Get-Content -LiteralPath $priorFirstSerial -Raw) -notmatch '\[DM15-QEMU\] lifecycle=PASS' -or
                (Get-Content -LiteralPath $priorRediscoverySerial -Raw) -notmatch '\[DM15-QEMU\] reboot-rediscovery=PASS') {
                throw "Prior lifecycle serial logs do not contain both required success markers."
            }
            $attemptNumber = $passed + 1
            $entries.Add("attempt.$attemptNumber=result=PASS")
            $entries.Add("attempt.$attemptNumber.manifest=$($priorLifecycle.Path)")
            ++$passed
        }
        $sharedBuildReady = $true
    }

    $firstAttempt = $passed + 1
    for ($attempt = $firstAttempt; $attempt -le 5; ++$attempt) {
        $attemptDir = Join-Path $WorkDir ("attempt-{0:D2}" -f $attempt)
        $proofArgs = @{
            Stage = "Lifecycle"
            EspSource = $EspSource
            EspCacheDirectory = $lifecycleEspCache
            WorkDir = $attemptDir
            QemuExecutable = $QemuExecutable
            OvmfCode = $OvmfCode
            AttemptNumber = $attempt
        }
        if ($sharedBuildReady) { $proofArgs.SkipBuild = $true }
        if ($PythonExecutable) { $proofArgs.PythonExecutable = $PythonExecutable }
        if ($QemuDebug) { $proofArgs.QemuDebug = $true }
        try {
            & $proofScript @proofArgs
            $sharedBuildReady = $true
            ++$passed
            $entries.Add("attempt.$attempt=result=PASS")
            $entries.Add("attempt.$attempt.manifest=$(Join-Path (Join-Path $Root $attemptDir) 'dm15-manifest.txt')")
        } catch {
            $detail = $_.Exception.Message -replace '[\r\n]+', ' '
            $entries.Add("attempt.$attempt=result=FAIL detail=$detail")
            break
        }
    }
} catch {
    $detail = $_.Exception.Message -replace '[\r\n]+', ' '
    $entries.Add("gateFailure=$detail")
}

$outcome = if ($passed -eq 5) { "PASS" } else { "FAIL" }
$entries.Add("result=$outcome")
$entries.Add("lifecyclePassed=$passed/5")
$entries.Add("initialPrivateWriteFlushRestore=required-before-shared-write-build")
$entries.Add("independentImageVerification=per-lifecycle-attempt")
$entries.Add("restartRediscoveryAndRemount=per-lifecycle-attempt")
$entries | Set-Content -LiteralPath $summaryPath -Encoding ascii
Write-Host "DM15 AHCI repeatability gate: $outcome ($passed/5 lifecycle runs). Evidence: $WorkFull"
if ($passed -ne 5) { throw "DM15 repeatability gate failed after $passed of 5 lifecycle runs. See $summaryPath" }
