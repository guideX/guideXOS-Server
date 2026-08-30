param(
    [object[]]$Stages = @(1, 2, 3, 4),
    [string]$VersionPrefix = '0.1.0-phase7-aida-i219',
    [switch]$CleanFirst,
    [string]$PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$BuildScript = Join-Path $RepositoryRoot 'build.ps1'
$ReleaseScript = Join-Path $RepositoryRoot 'scripts\create-release-iso.ps1'
$powershell = Get-Command powershell.exe -ErrorAction Stop
$requestedStages = New-Object 'System.Collections.Generic.List[int]'
foreach ($stageValue in @($Stages)) {
    foreach ($stageToken in ([string]$stageValue -split '[,;\s]+')) {
        if ([string]::IsNullOrWhiteSpace($stageToken)) { continue }
        $parsedStage = 0
        if (-not [int]::TryParse($stageToken, [ref]$parsedStage)) {
            throw "Invalid I219 Phase 7 stage value '$stageToken'. Use values from 1 through 4."
        }
        $requestedStages.Add($parsedStage)
    }
}
$stageNames = @{
    1 = 'mac'
    2 = 'phy'
    3 = 'dma'
    4 = 'register'
}

if ($requestedStages.Count -eq 0) { throw 'At least one Phase 7 stage is required.' }
foreach ($stage in $requestedStages) {
    if (-not $stageNames.ContainsKey($stage)) {
        throw "Invalid I219 Phase 7 stage $stage. Use a value from 1 through 4."
    }
}

for ($index = 0; $index -lt $requestedStages.Count; ++$index) {
    $stage = $requestedStages[$index]
    $name = $stageNames[$stage]
    $version = "$VersionPrefix-$name"
    Write-Host "[phase7-iso] Building stage $stage ($name; $version)" -ForegroundColor Cyan

    $buildArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $BuildScript,
        '-Arch', 'amd64', '-I219Phase5Stage', '8',
        '-I219Phase6Stage', '0', '-I219Phase7Stage', [string]$stage
    )
    if ($CleanFirst -and $index -eq 0) { $buildArgs += '-Clean' }
    & $powershell.Source @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "canonical build failed for Phase 7 stage $stage" }

    $releaseArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $ReleaseScript,
        '-Version', $version, '-Arch', 'amd64', '-SkipBuild', '-Force',
        '-IsoBackend', 'PyCdlib', '-I219Phase5Stage', '8',
        '-I219Phase6Stage', '0', '-I219Phase7Stage', [string]$stage
    )
    if (-not [string]::IsNullOrWhiteSpace($PythonPath)) {
        $releaseArgs += @('-PythonPath', $PythonPath)
    }
    & $powershell.Source @releaseArgs
    if ($LASTEXITCODE -ne 0) { throw "release ISO packaging failed for Phase 7 stage $stage" }
}

Write-Host "[phase7-iso] Completed stages: $($requestedStages -join ', ')" -ForegroundColor Green
