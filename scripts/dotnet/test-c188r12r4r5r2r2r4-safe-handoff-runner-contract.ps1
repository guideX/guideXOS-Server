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

$safeHandoffProfileSelected = [string]$manifest.mode -ceq 'NativeAotSafeHandoff' -or
    ([string]$manifest.mode -ceq 'NativeAotInitializeModulesTransitionAttribution' -and
     [bool]$manifest.c188R12R4R5R2R2R4SafeHandoff -and
     [bool]$manifest.c188R12R4R5R2R3InitializeModulesAttribution)
Assert-RunnerContract $safeHandoffProfileSelected 'candidate selects the safe-handoff or explicitly gated transition profile'
Assert-RunnerContract ([int]$manifest.nativeAllocationBackendCapacity -eq 9) 'candidate declares the root-level backend capacity'
Assert-RunnerContract ($runner -match '\$candidateManifest\.nativeAllocationBackendCapacity -ne 9') 'runner reads the declared backend capacity field'
Assert-RunnerContract ($runner -notmatch 'c188R12R4R5R4R5R2R2NativeAllocationBackendCapacity') 'runner does not read an undeclared capacity field'
$stageMapLoadIndex = $runner.IndexOf('$safeStageMap = Get-Content -LiteralPath $safeStageMapPath', [System.StringComparison]::Ordinal)
$stageMapUseIndex = $runner.IndexOf('[string]$safeStageMap.schema', [System.StringComparison]::Ordinal)
Assert-RunnerContract ($stageMapLoadIndex -ge 0 -and $stageMapUseIndex -gt $stageMapLoadIndex) 'runner loads the hash-pinned stage map before validating it'

$censusIndex = $runner.IndexOf('$before = Get-QacSnapshot ''qac-before''', [System.StringComparison]::Ordinal)
$clearIndex = $runner.IndexOf('if (-not $preflightPassed)', [System.StringComparison]::Ordinal)
$guardIndex = $runner.IndexOf('[System.IO.File]::Open($attemptPath,[System.IO.FileMode]::CreateNew', [System.StringComparison]::Ordinal)
$launchIndex = $runner.IndexOf('$qacProcess = [System.Diagnostics.Process]::Start($psi)', [System.StringComparison]::Ordinal)
$postLaunchPollIndex = $runner.IndexOf('$snapshot = Get-QacSnapshot $prefix', $launchIndex, [System.StringComparison]::Ordinal)
$runningReservationIndex = $runner.IndexOf('if ($ownerRows[0].State', $postLaunchPollIndex, [System.StringComparison]::Ordinal)
$ownerInspectionIndex = $runner.IndexOf('Test-QacRunningOwned -Snapshot', $runningReservationIndex, [System.StringComparison]::Ordinal)
$localChildIndex = $runner.IndexOf('Test-QacLocalQemuChildIdentity -LocalProcess', $ownerInspectionIndex, [System.StringComparison]::Ordinal)
$qmpQuitIndex = $runner.IndexOf("[void](Invoke-Qmp 'quit')", $localChildIndex, [System.StringComparison]::Ordinal)
$releasePollIndex = $runner.IndexOf('$after = Get-QacSnapshot', $qmpQuitIndex, [System.StringComparison]::Ordinal)
$releaseVerifiedIndex = $runner.IndexOf('$releaseVerified = $true; break', $releasePollIndex, [System.StringComparison]::Ordinal)
Assert-RunnerContract ($runner -match 'Get-WindowsQemuProcessCensus ''qac-before''' -and
    $runner -match 'Get-QacReservationInspections \$before') 'fresh QAC and Windows process ownership census is complete'
Assert-RunnerContract ($censusIndex -ge 0 -and $clearIndex -gt $censusIndex -and
    $guardIndex -gt $clearIndex -and $launchIndex -gt $guardIndex) 'census gates the one-boot guard, then the QAC CLI starts'
Assert-RunnerContract ($postLaunchPollIndex -gt $launchIndex -and $runningReservationIndex -gt $postLaunchPollIndex -and
    $ownerInspectionIndex -gt $runningReservationIndex -and $localChildIndex -gt $ownerInspectionIndex -and
    $qmpQuitIndex -gt $localChildIndex -and $releasePollIndex -gt $qmpQuitIndex -and
    $releaseVerifiedIndex -gt $releasePollIndex) 'QAC reservation and child ownership are verified after launch, before owned QMP shutdown and release verification'
Assert-RunnerContract ($runner -match 'qacArgs = @.{0,8}run.{0,30}guideXOSServer' -and
    $runner -match 'QAC did not provide a complete, stable, owner-verified exclusive process census' -and
    $runner -notmatch '\[System\.Diagnostics\.Process\]::Start\(\$qemu') 'QAC run owns admission and guest launch; no direct QEMU path is used'
Assert-RunnerContract ($runner -match 'maxGuestBoots=1;automaticRetry=\$false' -and
    $runner -match '\[System\.IO\.FileMode\]::CreateNew') 'guard is exclusive and permits one boot without retry'

# `qac run` establishes the reservation and owns child launch. The guard is
# persisted before Process.Start; the runner observes the grant afterward.
$archiveRoot = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2-safe-handoff'
$guardRecordPath = Join-Path $archiveRoot 'guest-launch-attempt-C188R12R4R5R4R5R2R2R4R2R1.json'
$grantRecordPath = Join-Path $archiveRoot 'qac-guest-r2r2r4r2r1-001\qac-during-001.inspect.json'
$guardDocument = [System.Text.Json.JsonDocument]::Parse(
    [System.IO.File]::ReadAllText($guardRecordPath))
$grantDocument = [System.Text.Json.JsonDocument]::Parse(
    [System.IO.File]::ReadAllText($grantRecordPath))
$guardTimeText = $guardDocument.RootElement.GetProperty('attemptedAtUtc').GetString()
$grantTimeText = $grantDocument.RootElement.GetProperty('grantedAtUtc').GetString()
$childStartTimeText = $grantDocument.RootElement.GetProperty('processStartTimeUtc').GetString()
$guardDocument.Dispose()
$grantDocument.Dispose()
$dateStyles = [System.Globalization.DateTimeStyles]::RoundtripKind
$invariantCulture = [System.Globalization.CultureInfo]::InvariantCulture
$guardTime = [DateTimeOffset]::Parse($guardTimeText,$invariantCulture,$dateStyles)
$grantTime = [DateTimeOffset]::Parse($grantTimeText,$invariantCulture,$dateStyles)
$childStartTime = [DateTimeOffset]::Parse($childStartTimeText,$invariantCulture,$dateStyles)
$guardToGrantMs = ($grantTime - $guardTime).TotalMilliseconds
Assert-RunnerContract ($guardTime -lt $grantTime -and $grantTime -lt $childStartTime -and
    $guardToGrantMs -gt 200 -and $guardToGrantMs -lt 400) 'archived run records guard creation before QAC grant and child launch; the roughly 0.282-second interval follows qac run admission'

$wrapperPath = Join-Path $root 'scripts\dotnet\run-c188r12r4r5r2r2r4-safe-handoff-qac-guest.ps1'
$candidateDiagnosticRoot = Join-Path $candidate 'staged\diagnostic'
$candidateWrapperPath = Join-Path $candidateDiagnosticRoot 'run-c188r12r4r5r2r2r4-safe-handoff-qac-guest.ps1'
$candidateAnalyzerPath = Join-Path $candidateDiagnosticRoot 'analyze-c188r12r4r5r2r2r4-safe-handoff.ps1'
Assert-RunnerContract ((Get-FileHash -LiteralPath $wrapperPath -Algorithm SHA256).Hash -ceq
    [string]$manifest.artifactHashes.safeHandoffRunner -and
    (Get-FileHash -LiteralPath $candidateWrapperPath -Algorithm SHA256).Hash -ceq
    [string]$manifest.artifactHashes.safeHandoffRunner) 'live and candidate-pinned safe-handoff wrapper matches'
Assert-RunnerContract ((Get-FileHash -LiteralPath $candidateAnalyzerPath -Algorithm SHA256).Hash -ceq
    [string]$manifest.artifactHashes.safeHandoffAnalyzer) 'candidate-pinned safe-handoff analyzer matches'

Write-Output 'C188R12R4R5R2R2R4 safe-handoff runner contract: PASS (candidate capacity, fresh census, guard before qac run, post-launch reservation and child ownership, owned QMP shutdown and release verification, archived guard-to-grant timing, analyzer and wrapper identities).'
