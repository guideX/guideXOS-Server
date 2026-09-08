param(
    [switch]$CleanFirst,
    [string]$PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildScript = Join-Path $repositoryRoot 'build.ps1'
$releaseScript = Join-Path $repositoryRoot 'scripts\create-release-iso.ps1'
$powershell = Get-Command powershell.exe -ErrorAction Stop
$version = '0.1.0-phase16-aida-i219-tx-dma-placement'

Write-Host "[phase16-iso] Building I219 controlled TX DMA-placement image ($version)" -ForegroundColor Cyan

$buildArgs = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript,
    '-Arch', 'amd64', '-I219Phase5Stage', '8',
    '-I219Phase6Stage', '0', '-I219Phase7Stage', '4',
    '-I219TxDmaPlacementExperiment'
)
if ($CleanFirst) { $buildArgs += '-Clean' }
& $powershell.Source @buildArgs
if ($LASTEXITCODE -ne 0) { throw 'canonical build failed for Phase 16 I219 TX DMA-placement image' }

$releaseArgs = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $releaseScript,
    '-Version', $version, '-Arch', 'amd64', '-SkipBuild', '-Force',
    '-IsoBackend', 'PyCdlib', '-I219Phase5Stage', '8',
    '-I219Phase6Stage', '0', '-I219Phase7Stage', '4',
    '-I219TxDmaPlacementExperiment'
)
if (-not [string]::IsNullOrWhiteSpace($PythonPath)) {
    $releaseArgs += @('-PythonPath', $PythonPath)
}
& $powershell.Source @releaseArgs
if ($LASTEXITCODE -ne 0) { throw 'release ISO packaging failed for Phase 16 I219 TX DMA-placement image' }

Write-Host "[phase16-iso] Created guideXOS-Server-v$version-amd64.iso with SHA-256 and manifest" -ForegroundColor Green
