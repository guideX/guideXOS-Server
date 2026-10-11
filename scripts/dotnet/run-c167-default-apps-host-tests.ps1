param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c167-default-apps\host-validation'
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet')).TrimEnd('\', '/') + '\'
if (-not $EvidenceRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C167 host evidence must remain under $allowedRoot"
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null

$dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $dotnet) { throw 'C167 host tests require the .NET SDK.' }
$project = Join-Path $RepoRoot 'samples\host\C167SettingsDefaultApps\C167SettingsDefaultApps.csproj'
$hostLog = Join-Path $EvidenceRoot 'c167-host-tests.txt'
$hostOutput = & $dotnet.Source run --project $project --configuration Release --no-restore 2>&1 | Out-String
$hostExit = $LASTEXITCODE
Set-Content -LiteralPath $hostLog -Value $hostOutput -Encoding utf8
if ($hostExit -ne 0 -or $hostOutput -notmatch 'C167 host UI/state cases=20 stress=60 result=PASS') {
    throw "C167 focused managed host tests failed; see $hostLog"
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

$hostManifestPath = Join-Path $RepoRoot 'out\dotnet\c167-default-apps\host-validation\c166r9-host-manifest.json'
$hostManifest = if (Test-Path -LiteralPath $hostManifestPath -PathType Leaf) {
    Get-Content -LiteralPath $hostManifestPath -Raw | ConvertFrom-Json
} else { $null }
$manifest = [ordered]@{
    phase = 'C167-host'
    c167UiStateCases = 20
    stressIterations = 60
    managedSettingsAssociationSlotReferences = $slotReferences.Count
    settingsFormat = [ordered]@{ version = 2; encodedFileBytes = 26 }
    c166 = if ($hostManifest) {
        [ordered]@{ nativeCases = $hostManifest.nativeCases; managedCases = $hostManifest.managedCases;
            dualSlotCases = $hostManifest.dualSlotCaseCount; persistenceAudits = $hostManifest.persistedImagesAudited;
            status = if ($hostManifest.hostValidationComplete) { 'PASS' } else { 'FAIL' } }
    } else { [ordered]@{ status = 'not-invoked-by-this-runner' } }
    status = 'PASS'
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c167-host-manifest.json') -Encoding utf8
Write-Host $hostOutput.Trim()
Write-Host 'C167 managed Settings slot boundary scan=PASS directSlotReferences=0'
Write-Host "C167 host evidence: $EvidenceRoot"
