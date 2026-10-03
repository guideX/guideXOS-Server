[CmdletBinding()]
param(
    [string]$WorkDir = "",
    [string]$EspSource = "ESP",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$PythonExecutable = "",
    [string]$KernelImage = "",
    [switch]$SkipBuild,
    [switch]$QemuDebug
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot
$runner = Join-Path $PSScriptRoot "run-dm15-qemu-proof.ps1"
$parameters = @{
    Stage = "Lifecycle"
    EspSource = $EspSource
    QemuExecutable = $QemuExecutable
    OvmfCode = $OvmfCode
    Dm25PartitionDeleteProof = $true
}
if ($WorkDir) { $parameters.WorkDir = $WorkDir }
if ($PythonExecutable) { $parameters.PythonExecutable = $PythonExecutable }
if ($KernelImage) { $parameters.KernelImage = $KernelImage }
if ($SkipBuild) { $parameters.SkipBuild = $true }
if ($QemuDebug) { $parameters.QemuDebug = $true }
& $runner @parameters
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
    throw "DM25 AHCI partition deletion proof failed with exit code $LASTEXITCODE."
}
