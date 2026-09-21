[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
    [string]$PythonExe = "",
    [int]$FreshBootCount = 3,
    [int]$TimeoutSeconds = 360,
    [switch]$SkipManagedBuild,
    [switch]$SkipKernelBuild,
    [switch]$SkipQemu
)

$ErrorActionPreference = "Stop"
$arguments = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "run-c120-managed-control-host.ps1"),
    "-RepoRoot", $RepoRoot,
    "-ProofPhase", "C132",
    "-FreshBootCount", $FreshBootCount,
    "-TimeoutSeconds", $TimeoutSeconds)
if (-not [string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $arguments += @("-EvidenceRoot", $EvidenceRoot)
}
if (-not [string]::IsNullOrWhiteSpace($CompositeElfPath)) {
    $arguments += @("-CompositeElfPath", $CompositeElfPath)
}
if (-not [string]::IsNullOrWhiteSpace($PythonExe)) {
    $arguments += @("-PythonExe", $PythonExe)
}
if ($SkipManagedBuild) { $arguments += "-SkipManagedBuild" }
if ($SkipKernelBuild) { $arguments += "-SkipKernelBuild" }
if ($SkipQemu) { $arguments += "-SkipQemu" }

& powershell @arguments
if ($LASTEXITCODE -ne 0) {
    throw "C132 managed RadioButton runner failed with exit code $LASTEXITCODE."
}
