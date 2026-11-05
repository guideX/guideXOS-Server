param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$CandidateRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $CandidateRoot = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2-safe-handoff\candidate-20261105T054500000Z-linkedtrace'
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$manifestPath = Join-Path $candidate 'candidate-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$runnerPath = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$runner = Get-Content -LiteralPath $runnerPath -Raw

function Assert-RunnerContract([bool]$Condition, [string]$Label) {
    if (-not $Condition) { throw "Safe-handoff runner contract failed: $Label" }
}

Assert-RunnerContract ([string]$manifest.mode -ceq 'NativeAotSafeHandoff') 'candidate is the safe-handoff profile'
Assert-RunnerContract ([int]$manifest.nativeAllocationBackendCapacity -eq 9) 'candidate declares the root-level backend capacity'
Assert-RunnerContract ($runner -match '\$candidateManifest\.nativeAllocationBackendCapacity -ne 9') 'runner reads the declared backend capacity field'
Assert-RunnerContract ($runner -notmatch 'c188R12R4R5R4R5R2R2NativeAllocationBackendCapacity') 'runner does not read an undeclared capacity field'
$stageMapLoadIndex = $runner.IndexOf('$safeStageMap = Get-Content -LiteralPath $safeStageMapPath', [System.StringComparison]::Ordinal)
$stageMapUseIndex = $runner.IndexOf('[string]$safeStageMap.schema', [System.StringComparison]::Ordinal)
Assert-RunnerContract ($stageMapLoadIndex -ge 0 -and $stageMapUseIndex -gt $stageMapLoadIndex) 'runner loads the hash-pinned stage map before validating it'

$censusIndex = $runner.IndexOf('$before = Get-QacSnapshot ''qac-before''', [System.StringComparison]::Ordinal)
$clearIndex = $runner.IndexOf('if (-not $preflightPassed)', [System.StringComparison]::Ordinal)
$guardIndex = $runner.IndexOf('This CreateNew marker permanently prevents a second QAC invocation', [System.StringComparison]::Ordinal)
$launchIndex = $runner.IndexOf('$qacProcess = [System.Diagnostics.Process]::Start($psi)', [System.StringComparison]::Ordinal)
Assert-RunnerContract ($runner -match 'Get-WindowsQemuProcessCensus ''qac-before''' -and
    $runner -match 'Get-QacReservationInspections \$before') 'fresh QAC and Windows process ownership census is complete'
Assert-RunnerContract ($censusIndex -ge 0 -and $clearIndex -gt $censusIndex -and
    $guardIndex -gt $clearIndex -and $launchIndex -gt $guardIndex) 'census gates the one-boot guard and QAC launch'
Assert-RunnerContract ($runner -match 'qacArgs = @.{0,8}run.{0,30}guideXOSServer' -and
    $runner -match 'QAC did not provide a complete, stable, owner-verified exclusive process census' -and
    $runner -notmatch '\[System\.Diagnostics\.Process\]::Start\(\$qemu') 'QAC run owns admission and guest launch; no direct QEMU path is used'
Assert-RunnerContract ($runner -match 'maxGuestBoots=1;automaticRetry=\$false' -and
    $runner -match '\[System\.IO\.FileMode\]::CreateNew') 'guard is exclusive and permits one boot without retry'

$wrapperPath = Join-Path $root 'scripts\dotnet\run-c188r12r4r5r2r2r4-safe-handoff-qac-guest.ps1'
$analyzerPath = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r2r2r4-safe-handoff.ps1'
Assert-RunnerContract ((Get-FileHash -LiteralPath $wrapperPath -Algorithm SHA256).Hash -ceq
    [string]$manifest.artifactHashes.safeHandoffRunner) 'candidate-pinned safe-handoff wrapper matches'
Assert-RunnerContract ((Get-FileHash -LiteralPath $analyzerPath -Algorithm SHA256).Hash -ceq
    [string]$manifest.artifactHashes.safeHandoffAnalyzer) 'candidate-pinned safe-handoff analyzer matches'

Write-Output 'C188R12R4R5R2R2R4 safe-handoff runner contract: PASS (candidate capacity, fresh census, pre-launch one-boot guard, QAC-owned admission and launch, analyzer and wrapper identities).'
