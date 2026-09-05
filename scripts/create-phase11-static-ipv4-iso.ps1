param(
    [switch]$CleanFirst,
    [string]$PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$BuildScript = Join-Path $RepositoryRoot 'build.ps1'
$ReleaseScript = Join-Path $RepositoryRoot 'scripts\create-release-iso.ps1'
$powershell = Get-Command powershell.exe -ErrorAction Stop
$version = '0.1.0-phase11-aida-dhcp-tx'

Write-Host "[phase11-iso] Building DHCP TX provenance image ($version)" -ForegroundColor Cyan

$buildArgs = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $BuildScript,
    '-Arch', 'amd64', '-I219Phase5Stage', '8',
    '-I219Phase6Stage', '0', '-I219Phase7Stage', '4'
)
if ($CleanFirst) { $buildArgs += '-Clean' }
& $powershell.Source @buildArgs
if ($LASTEXITCODE -ne 0) { throw 'canonical build failed for Phase 11 DHCP TX image' }

$releaseArgs = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $ReleaseScript,
    '-Version', $version, '-Arch', 'amd64', '-SkipBuild', '-Force',
    '-IsoBackend', 'PyCdlib', '-I219Phase5Stage', '8',
    '-I219Phase6Stage', '0', '-I219Phase7Stage', '4'
)
if (-not [string]::IsNullOrWhiteSpace($PythonPath)) {
    $releaseArgs += @('-PythonPath', $PythonPath)
}
& $powershell.Source @releaseArgs
if ($LASTEXITCODE -ne 0) { throw 'release ISO packaging failed for Phase 11 DHCP TX image' }

Write-Host "[phase11-iso] Created guideXOS-Server-v$version-amd64.iso with SHA-256 and manifest" -ForegroundColor Green
