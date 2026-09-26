param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
    [string]$PythonExe = "",
    [int]$FreshBootCount = 3,
    [int]$TimeoutSeconds = 360,
    [switch]$SkipManagedBuild,
    [switch]$SkipKernelBuild,
    [switch]$IncrementalKernelBuild,
    [switch]$SkipQemu
)

$arguments = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "run-c120-managed-control-host.ps1"),
    "-RepoRoot", $RepoRoot,
    "-ProofPhase", "C139",
    "-FreshBootCount", $FreshBootCount,
    "-TimeoutSeconds", $TimeoutSeconds
)
if ($EvidenceRoot) { $arguments += @("-EvidenceRoot", $EvidenceRoot) }
if ($CompositeElfPath) { $arguments += @("-CompositeElfPath", $CompositeElfPath) }
if ($PythonExe) { $arguments += @("-PythonExe", $PythonExe) }
if ($SkipManagedBuild) { $arguments += "-SkipManagedBuild" }
if ($SkipKernelBuild) { $arguments += "-SkipKernelBuild" }
if ($IncrementalKernelBuild) { $arguments += "-IncrementalKernelBuild" }
if ($SkipQemu) { $arguments += "-SkipQemu" }

& powershell @arguments
exit $LASTEXITCODE
