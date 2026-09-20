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
$sharedRunner = Join-Path $PSScriptRoot "run-c120-managed-control-host.ps1"
$arguments = @(
    "-RepoRoot", $RepoRoot,
    "-ProofPhase", "C124",
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

& powershell -ExecutionPolicy Bypass -File $sharedRunner @arguments
if ($LASTEXITCODE -ne 0) {
    throw "C124 managed radio-button runner failed with exit code $LASTEXITCODE."
}
