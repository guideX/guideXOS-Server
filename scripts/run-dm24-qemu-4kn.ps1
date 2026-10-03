[CmdletBinding()]
param(
    [string]$EspSource = "ESP",
    [string]$WorkDir = "",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$PythonExecutable = "",
    [string]$KernelImage = "",
    [UInt64]$DiskSizeBytes = 671088640,
    [switch]$SkipBuild,
    [switch]$QemuDebug
)

$ErrorActionPreference = "Stop"
if (-not $WorkDir) {
    $WorkDir = "out\dm24-qemu-4kn-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
$runner = Join-Path $PSScriptRoot "run-dm15-qemu-proof.ps1"
$parameters = @{
    Stage = "Lifecycle"
    EspSource = $EspSource
    WorkDir = $WorkDir
    QemuExecutable = $QemuExecutable
    OvmfCode = $OvmfCode
    DiskSizeBytes = $DiskSizeBytes
    Dm24FourKnProof = $true
}
if ($PythonExecutable) { $parameters.PythonExecutable = $PythonExecutable }
if ($KernelImage) { $parameters.KernelImage = $KernelImage }
if ($SkipBuild) { $parameters.SkipBuild = $true }
if ($QemuDebug) { $parameters.QemuDebug = $true }
& $runner @parameters
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
    throw "DM24 USB 4Kn proof runner failed with exit code $LASTEXITCODE."
}
