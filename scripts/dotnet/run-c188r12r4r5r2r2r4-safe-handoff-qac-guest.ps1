param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [ValidateRange(60,300)][int]$BootTimeoutSeconds = 180,
    [ValidateRange(1,60)][int]$AdmissionWindowSeconds = 30
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$candidateRootPrefix = [System.IO.Path]::GetFullPath(
    (Join-Path $repo 'out\dotnet\c188r12r4r5r4r5r2r2r4r2-safe-handoff')) +
    [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($candidateRootPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Safe-handoff guest qualification accepts only a staged C188R12R4R5R2R2R4 candidate.'
}
$manifestPath = Join-Path $candidate 'candidate-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Safe-handoff candidate manifest is missing: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([string]$manifest.mode -cne 'NativeAotSafeHandoff' -or
    [string]$manifest.result -cne 'SAFE_HANDOFF_HOSTED_READY' -or
    -not [bool]$manifest.c188R12R4R5R2R2R4SafeHandoff -or
    [bool]$manifest.qemuLaunched -or [bool]$manifest.qacReserved -or
    [string]$manifest.nextGuestPhase -cne 'C188R12R4R5R4R5R2R2R4R2R1_NativeAOT_SafeHandoff_Guest_Qualification') {
    throw 'Candidate manifest does not authorize the isolated safe-stop guest phase.'
}
$runner = Join-Path $PSScriptRoot 'run-c188r12r4r5-gc-ownership-qac-guest.ps1'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $runner `
    -CandidateRoot $candidate `
    -BootTimeoutSeconds $BootTimeoutSeconds `
    -AdmissionWindowSeconds $AdmissionWindowSeconds `
    -NativeAllocationGuestAttempt R2R2R4
$runnerExitCode = $LASTEXITCODE
exit $runnerExitCode
