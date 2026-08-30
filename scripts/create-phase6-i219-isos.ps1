param(
    [int[]]$Stages = @(1, 2, 3, 4, 5, 6),
    [string]$VersionPrefix = '0.1.0-phase6-aida-i219',
    [switch]$CleanFirst,
    [string]$PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$BuildScript = Join-Path $RepositoryRoot 'build.ps1'
$ReleaseScript = Join-Path $RepositoryRoot 'scripts\create-release-iso.ps1'
$powershell = Get-Command powershell.exe -ErrorAction Stop
$stageNames = @{
    1 = 'mask'
    2 = 'rctl'
    3 = 'tctl'
    4 = 'ctrl-read'
    5 = 'ctrl-rst-write'
    6 = 'reset'
}

if ($Stages.Count -eq 0) { throw 'At least one Phase 6 micro-stage is required.' }
foreach ($stage in $Stages) {
    if (-not $stageNames.ContainsKey($stage)) {
        throw "Invalid I219 Phase 6 micro-stage $stage. Use a value from 1 through 6."
    }
}

for ($index = 0; $index -lt $Stages.Count; ++$index) {
    $stage = $Stages[$index]
    $name = $stageNames[$stage]
    $version = "$VersionPrefix-$name"
    Write-Host "[phase6-iso] Building micro-stage $stage ($name; $version)" -ForegroundColor Cyan

    $buildArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $BuildScript,
        '-Arch', 'amd64', '-I219Phase5Stage', '8',
        '-I219Phase6Stage', [string]$stage
    )
    if ($CleanFirst -and $index -eq 0) { $buildArgs += '-Clean' }
    & $powershell.Source @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "canonical build failed for Phase 6 micro-stage $stage" }

    $releaseArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $ReleaseScript,
        '-Version', $version, '-Arch', 'amd64', '-SkipBuild', '-Force',
        '-IsoBackend', 'PyCdlib', '-I219Phase5Stage', '8',
        '-I219Phase6Stage', [string]$stage
    )
    if (-not [string]::IsNullOrWhiteSpace($PythonPath)) {
        $releaseArgs += @('-PythonPath', $PythonPath)
    }
    & $powershell.Source @releaseArgs
    if ($LASTEXITCODE -ne 0) { throw "release ISO packaging failed for Phase 6 micro-stage $stage" }
}

Write-Host "[phase6-iso] Completed micro-stages: $($Stages -join ', ')" -ForegroundColor Green
