param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$PythonExe = "",
    [int]$TimeoutSeconds = 900,
    [switch]$ReuseBuiltProofKernel
)

$ErrorActionPreference = "Stop"
$runner = Join-Path $PSScriptRoot 'run-c158-managed-calculator.ps1'
$runnerParameters = @{
    RepoRoot = $RepoRoot
    PhaseC160 = $true
    TimeoutSeconds = $TimeoutSeconds
}
if ($EvidenceRoot) { $runnerParameters.EvidenceRoot = $EvidenceRoot }
if ($PythonExe) { $runnerParameters.PythonExe = $PythonExe }
if ($ReuseBuiltProofKernel) { $runnerParameters.ReuseBuiltProofKernel = $true }
& $runner @runnerParameters
