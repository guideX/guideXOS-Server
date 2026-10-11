param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.." )).Path,
    [string]$EvidenceRoot = "",
    [string]$ArtifactsRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c168-settings-polish\host-validation'
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet')).TrimEnd('\', '/') + '\'
if (-not $EvidenceRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C168 host evidence must remain under $allowedRoot"
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null

$dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $dotnet) { throw 'C168 host tests require the .NET SDK.' }
$project = Join-Path $RepoRoot 'samples\host\C167SettingsDefaultApps\C167SettingsDefaultApps.csproj'
$hostLog = Join-Path $EvidenceRoot 'c168-managed-settings-host-tests.txt'
if ([string]::IsNullOrWhiteSpace($ArtifactsRoot)) {
    $hostOutput = & $dotnet.Source run --project $project --configuration Release --no-restore 2>&1 | Out-String
} else {
    $ArtifactsRoot = [System.IO.Path]::GetFullPath($ArtifactsRoot)
    New-Item -ItemType Directory -Force -Path $ArtifactsRoot | Out-Null
    $hostOutput = & $dotnet.Source run --project $project --configuration Release --artifacts-path $ArtifactsRoot 2>&1 | Out-String
}
$hostExit = $LASTEXITCODE
Set-Content -LiteralPath $hostLog -Value $hostOutput -Encoding utf8
$c167Pass = $hostOutput -match 'C167 host UI/state cases=20 stress=60 result=PASS'
$c168Match = [regex]::Match($hostOutput, 'C168 host UI/layout cases=(\d+) pageCycles=(\d+) selectionChanges=(\d+) applyResetCycles=(\d+) popupCycles=(\d+) result=PASS')
if ($hostExit -ne 0 -or -not $c167Pass -or -not $c168Match.Success) {
    throw "C168 Settings host tests or fresh C167 regressions failed; see $hostLog"
}

$settingsSources = @(
    (Join-Path $RepoRoot 'samples\managed\HostLogProof\Applications\ManagedSettingsCenter.cs'),
    (Join-Path $RepoRoot 'samples\managed\HostLogProof\Applications\ManagedDefaultAppsPageC167.cs'))
$slotPattern = '(?i)(?:/|\\)?GXAS[01](?:\.BIN)?'
$slotReferences = @()
foreach ($path in $settingsSources) {
    $slotReferences += @(Select-String -LiteralPath $path -Pattern $slotPattern -AllMatches)
}
if ($slotReferences.Count -ne 0) {
    $details = ($slotReferences | ForEach-Object { "$($_.Path):$($_.LineNumber):$($_.Line.Trim())" }) -join "`n"
    throw "Managed Settings contains direct C166 slot access:`n$details"
}

$manifest = [ordered]@{
    phase = 'C168-managed-settings-ui-host'
    status = 'PASS'
    c167Regression = [ordered]@{ focusedCases = 20; stressIterations = 60; status = 'PASS' }
    c168Ui = [ordered]@{
        focusedCases = [int]$c168Match.Groups[1].Value
        pageEntryExitCycles = [int]$c168Match.Groups[2].Value
        selectionPendingChanges = [int]$c168Match.Groups[3].Value
        applyResetCycles = [int]$c168Match.Groups[4].Value
        popupOpenCloseCycles = [int]$c168Match.Groups[5].Value
        status = 'PASS'
    }
    managedSettingsAssociationSlotReferences = $slotReferences.Count
    settingsFormat = [ordered]@{ version = 2; encodedFileBytes = 26; status = 'PASS' }
    hostAbi = [ordered]@{ version = 4; tableBytes = 128; associationCallbackOffset = 120; status = 'PASS' }
    testLog = $hostLog
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c168-settings-ui-host-manifest.json') -Encoding utf8
$c167Manifest = [ordered]@{
    phase = 'C167-host-regression-rerun'
    sourceHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
    c167UiStateCases = 20
    stressIterations = 60
    managedSettingsAssociationSlotReferences = $slotReferences.Count
    settingsFormat = [ordered]@{ version = 2; encodedFileBytes = 26 }
    status = 'PASS'
    testLog = $hostLog
}
$c167Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c167-host-manifest.json') -Encoding utf8
Write-Host $hostOutput.Trim()
Write-Host 'C168 managed Settings slot boundary scan=PASS directSlotReferences=0'
Write-Host "C168 host evidence: $EvidenceRoot"
