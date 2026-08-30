param(
    [int[]]$Stages = @(2, 3, 5, 7),
    [string]$VersionPrefix = '0.1.0-phase5-aida-i219',
    [switch]$CleanFirst,
    [string]$PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$BuildScript = Join-Path $RepositoryRoot 'build.ps1'
$ReleaseScript = Join-Path $RepositoryRoot 'scripts\create-release-iso.ps1'
$powershell = Get-Command powershell.exe -ErrorAction Stop

if ($Stages.Count -eq 0) { throw 'At least one Phase 5 stage is required.' }
foreach ($stage in $Stages) {
    if ($stage -lt 0 -or $stage -gt 8) {
        throw "Invalid I219 Phase 5 stage $stage. Use a value from 0 through 8."
    }
}

for ($index = 0; $index -lt $Stages.Count; ++$index) {
    $stage = $Stages[$index]
    $version = "$VersionPrefix-stage$stage"
    Write-Host "[phase5-iso] Building stage $stage ($version)" -ForegroundColor Cyan

    $buildArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $BuildScript,
        '-Arch', 'amd64', '-I219Phase5Stage', [string]$stage
    )
    if ($CleanFirst -and $index -eq 0) { $buildArgs += '-Clean' }
    & $powershell.Source @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "canonical build failed for Phase 5 stage $stage" }

    $releaseArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $ReleaseScript,
        '-Version', $version, '-Arch', 'amd64', '-SkipBuild', '-Force',
        '-IsoBackend', 'PyCdlib', '-I219Phase5Stage', [string]$stage
    )
    if (-not [string]::IsNullOrWhiteSpace($PythonPath)) {
        $releaseArgs += @('-PythonPath', $PythonPath)
    }
    & $powershell.Source @releaseArgs
    if ($LASTEXITCODE -ne 0) { throw "release ISO packaging failed for Phase 5 stage $stage" }
}

Write-Host "[phase5-iso] Completed stages: $($Stages -join ', ')" -ForegroundColor Green
