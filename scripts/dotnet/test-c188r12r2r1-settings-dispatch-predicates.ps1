param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'c188r12r2r1-settings-dispatch-predicates.ps1')
$fixtureRoot = Join-Path $PSScriptRoot 'fixtures\c188r12r2r1'
$script:cases = 0
function Assert-C188R12R2R1([string]$Name, [bool]$Condition) {
    $script:cases++
    if (-not $Condition) { throw "C188R12R2R1 dispatch predicate failed: $Name" }
}

$successText = Get-Content -LiteralPath (Join-Path $fixtureRoot 'settings-success.serial.txt') -Raw
$success = Get-C188R12R2R1SettingsDispatchEvidence $successText $true
Assert-C188R12R2R1 'actual C102 output syntax and complete stage order accepted' `
    ($success.outcome -eq 'DISPATCH_SUCCEEDED' -and $success.markerCount -eq 13 -and
     $success.markerOrderingValid -and $success.dispatchReturned -and
     $success.generation -eq 2 -and $null -eq $success.firstUnobservedBoundary)
Assert-C188R12R2R1 'application entry and return are separate observations' `
    (@($success.events | Where-Object stage -eq 'application-entry').Count -eq 1 -and
     @($success.events | Where-Object stage -eq 'application-return').Count -eq 1)

$registrationText = Get-Content -LiteralPath (Join-Path $fixtureRoot 'settings-registration-rejected.serial.txt') -Raw
$registration = Get-C188R12R2R1SettingsDispatchEvidence $registrationText $true
Assert-C188R12R2R1 'registration failure is explicit and preserves return code' `
    ($registration.outcome -eq 'EXPLICIT_FAILURE' -and
     $registration.firstExplicitFailure.stage -eq 'registration-lookup' -and
     $registration.firstExplicitFailure.code -eq -8 -and
     $registration.dispatchReturned)
Assert-C188R12R2R1 'registration rejection skips initialization without inventing it' `
    ($null -eq $registration.firstUnobservedBoundary -and
     -not $registration.lifecycleMarkerObserved)

$runtimeText = Get-Content -LiteralPath (Join-Path $fixtureRoot 'settings-runtime-init-rejected.serial.txt') -Raw
$runtime = Get-C188R12R2R1SettingsDispatchEvidence $runtimeText $true
Assert-C188R12R2R1 'runtime initialization failure is localized' `
    ($runtime.firstExplicitFailure.stage -eq 'runtime-settings-init' -and
     $runtime.firstExplicitFailure.code -eq -12 -and $runtime.dispatchReturned)

$entryText = Get-Content -LiteralPath (Join-Path $fixtureRoot 'settings-entry-rejected.serial.txt') -Raw
$entry = Get-C188R12R2R1SettingsDispatchEvidence $entryText $true
Assert-C188R12R2R1 'application failure result is not mistaken for dispatch success' `
    ($entry.outcome -eq 'EXPLICIT_FAILURE' -and
     $entry.firstExplicitFailure.stage -eq 'application-return' -and
     $entry.firstExplicitFailure.code -eq -5 -and $entry.dispatchReturned -and
     $null -eq $entry.firstUnobservedBoundary)

$contextText = Get-Content -LiteralPath (Join-Path $fixtureRoot 'settings-dispatch-context-only.serial.txt') -Raw
$contextOnly = Get-C188R12R2R1SettingsDispatchEvidence $contextText $true
Assert-C188R12R2R1 'absence after context is incomplete rather than a failed call' `
    ($contextOnly.outcome -eq 'INCOMPLETE' -and
     $contextOnly.firstUnobservedBoundary -eq 'dispatch-prelude' -and
     $null -eq $contextOnly.firstExplicitFailure)

$outdatedText = '[C102-MANAGED-OUTPUT] C188R12R2-LAUNCH stage=dispatch-context result=PASS'
$outdated = Get-C188R12R2R1SettingsDispatchEvidence $outdatedText $true
Assert-C188R12R2R1 'outdated hyphenated marker is not accepted as R2 evidence' `
    ($outdated.markerCount -eq 0 -and $outdated.firstUnobservedBoundary -eq 'dispatch-context')

$wrongIdentityText = $successText.Replace('appId=com.guidexos.apps.managed.settingscenter','appId=com.guidexos.apps.managed.notes')
$wrongIdentity = Get-C188R12R2R1SettingsDispatchEvidence $wrongIdentityText $true
Assert-C188R12R2R1 'wrong application identity is rejected as malformed' `
    ($wrongIdentity.outcome -eq 'INVALID_MARKERS' -and
     $wrongIdentity.malformedMarkerCount -eq 13 -and -not $wrongIdentity.markerOrderingValid)

$noLifecycle = Get-C188R12R2R1SettingsDispatchEvidence $successText.Replace(
    ($successText -split "`r?`n" | Where-Object { $_ -match 'stage=managed-lifecycle-tests' }) -join "`n", '') $false
Assert-C188R12R2R1 'conditional lifecycle stage can be omitted only when the build does not require it' `
    ($noLifecycle.outcome -eq 'DISPATCH_SUCCEEDED' -and -not $noLifecycle.lifecycleMarkerObserved)

$badCodeText = $registrationText.Replace('result=FAIL code=-8','result=FAIL code=0')
$badCode = Get-C188R12R2R1SettingsDispatchEvidence $badCodeText $true
Assert-C188R12R2R1 'failure without a nonzero result code is rejected' `
    ($badCode.outcome -eq 'INVALID_MARKERS' -and -not $badCode.markerOrderingValid)

Write-Output "C188R12R2R1-DISPATCH-PREDICATES cases=$script:cases result=PASS"
