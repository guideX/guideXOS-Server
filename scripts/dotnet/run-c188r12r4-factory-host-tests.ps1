[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$EvidenceRoot = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R12R4 hosted qualification requires PowerShell Core.'
}
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c188r12r4-factory-attribution\hosted'
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowed = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet')).TrimEnd('\','/') + '\'
if (-not $EvidenceRoot.StartsWith($allowed, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C188R12R4 hosted evidence must remain under $allowed"
}
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runRoot = Join-Path $EvidenceRoot $runId
if (Test-Path -LiteralPath $runRoot) { throw "Refusing to overwrite hosted evidence: $runRoot" }
$null = New-Item -ItemType Directory -Path $runRoot
$dotnet = Get-Command 'dotnet.exe' -ErrorAction Stop | Select-Object -First 1
$project = Join-Path $RepoRoot 'samples\host\ManagedUiRegression\ManagedUiRegression.csproj'
$artifacts = Join-Path $runRoot 'dotnet-artifacts'
$hostLog = Join-Path $runRoot 'factory-host-tests.log'
$hostOutput = (& $dotnet.Source run --project $project --configuration Release `
    --artifacts-path $artifacts -- '--c188r12r4-factory' 2>&1 | Out-String)
$hostExitCode = $LASTEXITCODE
Set-Content -LiteralPath $hostLog -Value $hostOutput -Encoding utf8
$hostSummary = [regex]::Match($hostOutput,
    '(?m)^C188R12R4-FACTORY cases=(\d+) firstFailedCase=(\d+) result=(PASS|FAIL)\s*$')
if ($hostExitCode -ne 0 -or -not $hostSummary.Success -or
    [int]$hostSummary.Groups[1].Value -ne 14 -or
    $hostSummary.Groups[3].Value -ne 'PASS') {
    throw "C188R12R4 focused hosted suite failed (exit=$hostExitCode). See $hostLog"
}

$predicateScript = Join-Path $PSScriptRoot 'test-c188r12r4-factory-predicates.ps1'
$predicateLog = Join-Path $runRoot 'factory-predicates.log'
$predicateOutput = (& $PSHOME\pwsh.exe -NoProfile -File $predicateScript 2>&1 | Out-String)
$predicateExitCode = $LASTEXITCODE
Set-Content -LiteralPath $predicateLog -Value $predicateOutput -Encoding utf8
$predicateSummary = [regex]::Match($predicateOutput,
    '(?m)^C188R12R4-FACTORY-PREDICATES cases=(\d+) result=(PASS|FAIL) syntheticFixtures=true\s*$')
if ($predicateExitCode -ne 0 -or -not $predicateSummary.Success -or
    [int]$predicateSummary.Groups[1].Value -ne 9 -or
    $predicateSummary.Groups[2].Value -ne 'PASS') {
    throw "C188R12R4 synthetic runner predicates failed (exit=$predicateExitCode). See $predicateLog"
}

$linkBoundaryScript = Join-Path $PSScriptRoot 'test-c188r12r4r1-link-boundary.ps1'
$linkBoundaryLog = Join-Path $runRoot 'link-boundary.log'
$linkBoundaryOutput = (& $PSHOME\pwsh.exe -NoProfile -File $linkBoundaryScript -DotNetExe $dotnet.Source 2>&1 | Out-String)
$linkBoundaryExitCode = $LASTEXITCODE
Set-Content -LiteralPath $linkBoundaryLog -Value $linkBoundaryOutput -Encoding utf8
if ($linkBoundaryExitCode -ne 0 -or
    $linkBoundaryOutput -notmatch '(?m)^C188R12R4R1-LINK-BOUNDARY cases=9 guestDirect=true hostedDirect=false productionDirect=false allocationLineMaxBytes=\d+ firstIncompleteParser=true oneClickRunner=true allocationSummary=true result=PASS\s*$') {
    throw "C188R12R4R1 NativeAOT link boundary qualification failed (exit=$linkBoundaryExitCode). See $linkBoundaryLog"
}

$warnings = @([regex]::Matches($hostOutput + "`n" + $predicateOutput + "`n" + $linkBoundaryOutput,
    '(?im)^.*\bwarning\b.*$') | ForEach-Object { $_.Value.Trim() })
$manifest = [ordered]@{
    result='PASS'
    runRoot=$runRoot
    focusedFactoryCases=[int]$hostSummary.Groups[1].Value
    focusedFactoryFirstFailedCase=[int]$hostSummary.Groups[2].Value
    syntheticPredicateCases=[int]$predicateSummary.Groups[1].Value
    hostLog=$hostLog
    hostLogSha256=(Get-FileHash -LiteralPath $hostLog -Algorithm SHA256).Hash
    predicateLog=$predicateLog
    predicateLogSha256=(Get-FileHash -LiteralPath $predicateLog -Algorithm SHA256).Hash
    linkBoundaryLog=$linkBoundaryLog
    linkBoundaryLogSha256=(Get-FileHash -LiteralPath $linkBoundaryLog -Algorithm SHA256).Hash
    linkBoundaryCases=9
    warnings=$warnings
    syntheticFixturesOnly=$true
}
$manifestPath = Join-Path $runRoot 'hosted-qualification.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188R12R4-HOSTED=PASS factoryCases=$($manifest.focusedFactoryCases) predicateCases=$($manifest.syntheticPredicateCases) linkBoundaryCases=$($manifest.linkBoundaryCases) warnings=$($warnings.Count)"
Write-Output "C188R12R4-HOSTED-MANIFEST=$manifestPath"
