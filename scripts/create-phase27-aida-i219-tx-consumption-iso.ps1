param([string]$PythonPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildScript = Join-Path $repositoryRoot 'build.ps1'
$releaseScript = Join-Path $PSScriptRoot 'create-release-iso.ps1'
$powershell = Get-Command powershell.exe -ErrorAction Stop
$version = '0.1.0-navgen-phase27-i219-tx-consumption'

Write-Host "[phase27-iso] Building bounded I219 TX-consumption diagnostic ($version)" -ForegroundColor Cyan

$buildArgs = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript,
    '-Arch', 'amd64', '-I219Phase5Stage', '8',
    '-I219Phase6Stage', '0', '-I219Phase7Stage', '4',
    '-I219TxDmaPlacementExperiment', '-I219LanInitDiagnostics',
    '-SkipPacManPackageBuild'
)
& $powershell.Source @buildArgs
if ($LASTEXITCODE -ne 0) { throw 'canonical AMD64 kernel/UEFI build failed for Phase 27 diagnostics' }

$releaseArgs = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $releaseScript,
    '-Version', $version, '-Arch', 'amd64', '-SkipBuild',
    '-IsoBackend', 'PyCdlib', '-Force', '-I219Phase5Stage', '8',
    '-I219Phase6Stage', '0', '-I219Phase7Stage', '4',
    '-I219TxDmaPlacementExperiment', '-I219LanInitDiagnostics',
    '-SkipPacManPackageBuild'
)
if (-not [string]::IsNullOrWhiteSpace($PythonPath)) {
    $releaseArgs += @('-PythonPath', $PythonPath)
}
& $powershell.Source @releaseArgs
if ($LASTEXITCODE -ne 0) { throw 'ISO packaging/verification failed for Phase 27 diagnostics' }

Write-Host "[phase27-iso] Created a distinct guideXOS Server v$version AMD64 ISO" -ForegroundColor Green
