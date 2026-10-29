$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r12r3-lifetime-predicates.ps1')
$script:cases = 0

function Assert-C188R12R3([string]$Name, [bool]$Condition) {
    $script:cases++
    if (-not $Condition) { throw "C188R12R3 lifetime predicate failed: $Name" }
}

function Get-CompleteLifetimeEvidence([string]$Text) {
    return Get-C188R12R3LifetimeEvidence -SerialText $Text -CaptureState 'complete'
}

function New-LifeLine([string]$Stage, [int]$Code = 0,
    [string]$State = 'pass', [uint32]$Count = 0,
    [uint32]$Mask = 0) {
    return "[C102-MANAGED-OUTPUT] C188R12R3_LIFETIME invocation=2 stage=$Stage selector=5 identity=com.guidexos.apps.managed.settingscenter count=$Count mask=$Mask generation=2 code=$Code state=$State"
}

function New-R2Line([string]$Stage, [string]$Result = 'PASS',
    [int]$Code = 0) {
    return "[C102-MANAGED-OUTPUT] C188R12R2_LAUNCH stage=$Stage appId=com.guidexos.apps.managed.settingscenter selector=5 generation=2 result=$Result code=$Code"
}

function New-StandardPrefix {
    return @(
        (New-R2Line 'dispatch-context'),
        (New-R2Line 'dispatch-prelude'),
        (New-R2Line 'registration-lookup'),
        (New-R2Line 'runtime-settings-init')
    )
}

function New-InternalSuccess {
    return @(
        (New-LifeLine 'enter' -1 'preflight'),
        (New-LifeLine 'existing-resident' 1 'miss'),
        (New-LifeLine 'capacity-check' 0 'available'),
        (New-LifeLine 'generation-check' 0 'valid'),
        (New-LifeLine 'factory-resolve' 0 'resolved'),
        (New-LifeLine 'factory-enter' -1 'invoking'),
        (New-LifeLine 'factory-return' 0 'created'),
        (New-LifeLine 'resident-register-enter' -1 'free-slot'),
        (New-LifeLine 'resident-register-return' 0 'registered' 1 1),
        (New-LifeLine 'owner-assign' 0 'generation-committed' 1 1),
        (New-LifeLine 'exit-success' 0 'resident' 1 1)
    )
}

$factoryEntered = @(
    (New-StandardPrefix) +
    (New-R2Line 'lifetime-start' 'BEGIN') +
    (New-LifeLine 'enter' -1 'preflight') +
    (New-LifeLine 'existing-resident' 1 'miss') +
    (New-LifeLine 'capacity-check' 0 'available') +
    (New-LifeLine 'generation-check' 0 'valid') +
    (New-LifeLine 'factory-resolve' 0 'resolved') +
    (New-LifeLine 'factory-enter' -1 'invoking')
) -join "`n"
$factoryEnteredResult = Get-CompleteLifetimeEvidence $factoryEntered
Assert-C188R12R3 'factory-enter without factory-return remains an unobserved boundary' `
    ($factoryEnteredResult.outcome -eq 'FACTORY_ENTERED_NO_RETURN' -and
     $factoryEnteredResult.earliestIncompleteStage -eq 'factory-invocation' -and
     $factoryEnteredResult.factoryStatus -eq 'entered-no-return-observed' -and
     $factoryEnteredResult.lastCompletedStage -eq 'factory-resolve' -and
     $factoryEnteredResult.traceEndedWithinInstrumentedInterval)

$factoryError = @(
    (New-StandardPrefix) +
    (New-LifeLine 'enter' -1 'preflight') +
    (New-LifeLine 'existing-resident' 1 'miss') +
    (New-LifeLine 'capacity-check' 0 'available') +
    (New-LifeLine 'generation-check' 0 'valid') +
    (New-LifeLine 'factory-resolve' 0 'resolved') +
    (New-LifeLine 'factory-enter' -1 'invoking') +
    (New-LifeLine 'factory-return' 4 'null') +
    (New-LifeLine 'exit-failure' 4 'factory-null')
) -join "`n"
$factoryErrorResult = Get-CompleteLifetimeEvidence $factoryError
Assert-C188R12R3 'factory return with an error is explicit' `
    ($factoryErrorResult.outcome -eq 'FACTORY_RETURNED_ERROR' -and
     $factoryErrorResult.earliestIncompleteStage -eq 'factory-return' -and
     $factoryErrorResult.factoryStatus -eq 'returned-error')

$registrationEntryOnly = @(
    (New-StandardPrefix) +
    (New-LifeLine 'enter' -1 'preflight') +
    (New-LifeLine 'existing-resident' 1 'miss') +
    (New-LifeLine 'capacity-check' 0 'available') +
    (New-LifeLine 'generation-check' 0 'valid') +
    (New-LifeLine 'factory-resolve' 0 'resolved') +
    (New-LifeLine 'factory-enter' -1 'invoking') +
    (New-LifeLine 'factory-return' 0 'created') +
    (New-LifeLine 'resident-register-enter' -1 'free-slot')
) -join "`n"
$registrationEntryResult = Get-CompleteLifetimeEvidence $registrationEntryOnly
Assert-C188R12R3 'registration entry is not treated as completed registration' `
    ($registrationEntryResult.outcome -eq 'FACTORY_SUCCEEDED_REGISTRATION_INCOMPLETE' -and
     $registrationEntryResult.earliestIncompleteStage -eq 'resident-registration' -and
     $registrationEntryResult.factoryStatus -eq 'returned-successfully' -and
     $registrationEntryResult.registrationStatus -eq 'entered-no-return-observed')

$registrationReturned = @(
    (New-StandardPrefix) +
    (New-InternalSuccess | Select-Object -SkipLast 1)
) -join "`n"
$registrationReturnedResult = Get-CompleteLifetimeEvidence $registrationReturned
Assert-C188R12R3 'registration return without exit identifies follow-up boundary' `
    ($registrationReturnedResult.outcome -eq 'REGISTRATION_SUCCEEDED_FOLLOWUP_INCOMPLETE' -and
     $registrationReturnedResult.earliestIncompleteStage -eq 'try-start-return')

$callerDidNotContinue = @(
    (New-StandardPrefix) +
    (New-R2Line 'lifetime-start') +
    (New-InternalSuccess) +
    (New-R2Line 'lifetime-start' 'PASS')
) -join "`n"
$callerDidNotContinueResult = Get-CompleteLifetimeEvidence $callerDidNotContinue
Assert-C188R12R3 'successful TryStart followed by no application entry is distinguished' `
    ($callerDidNotContinueResult.outcome -eq 'TRY_START_RETURNED_CALLER_NOT_CONTINUED' -and
     $callerDidNotContinueResult.earliestIncompleteStage -eq 'application-entry')

$successfulLaunch = @(
    (New-StandardPrefix) +
    (New-R2Line 'lifetime-start') +
    (New-InternalSuccess) +
    (New-R2Line 'lifetime-start' 'PASS') +
    (New-R2Line 'application-entry' 'BEGIN') +
    (New-R2Line 'application-return' 'PASS') +
    (New-R2Line 'dispatch-return' 'PASS') +
    '[C150-SURFACE] action=create appId=com.guidexos.apps.managed.settingscenter generation=00000002 window=000003E8 result=PASS'
) -join "`n"
$successfulLaunchResult = Get-CompleteLifetimeEvidence $successfulLaunch
Assert-C188R12R3 'complete application return and surface evidence pass' `
    ($successfulLaunchResult.outcome -eq 'SETTINGS_LAUNCH_SUCCEEDED' -and
     $successfulLaunchResult.settingsSurfaceObserved -and
     $successfulLaunchResult.callerContinued)

$incomplete = @(
    (New-StandardPrefix) +
    (New-R2Line 'lifetime-start' 'BEGIN')
) -join "`n"
$incompleteResult = Get-CompleteLifetimeEvidence $incomplete
Assert-C188R12R3 'missing internal trace stays incomplete rather than inferred failure' `
    ($incompleteResult.outcome -eq 'MISSING_SETTINGS_LIFETIME_MARKER' -and
     $incompleteResult.earliestIncompleteStage -eq 'settings-lifetime-entry')

$captureTruncatedAfterFactoryEntry = $factoryEntered + "`n[C102-MANAGED-OUTPUT] C188R12R3_LIFETIME invocation=2 stage=factory-ret"
$truncatedCaptureResult = Get-C188R12R3LifetimeEvidence `
    -SerialText $captureTruncatedAfterFactoryEntry -CaptureState 'incomplete'
Assert-C188R12R3 'partial serial capture cannot be called an execution stop' `
    ($truncatedCaptureResult.outcome -eq 'INSUFFICIENT_CAPTURE' -and
     $truncatedCaptureResult.observedBoundaryOutcome -eq 'INVALID_MARKERS' -and
     $truncatedCaptureResult.captureState -eq 'incomplete' -and
     $truncatedCaptureResult.factoryStatus -eq 'entered-capture-incomplete' -and
     -not $truncatedCaptureResult.traceEndedWithinInstrumentedInterval)

$incompleteFactoryCaptureResult = Get-C188R12R3LifetimeEvidence `
    -SerialText $factoryEntered -CaptureState 'incomplete'
Assert-C188R12R3 'a missing factory return is inconclusive without complete capture' `
    ($incompleteFactoryCaptureResult.outcome -eq 'INSUFFICIENT_CAPTURE' -and
     $incompleteFactoryCaptureResult.observedBoundaryOutcome -eq 'FACTORY_ENTERED_NO_RETURN' -and
     $incompleteFactoryCaptureResult.factoryStatus -eq 'entered-capture-incomplete' -and
     -not $incompleteFactoryCaptureResult.traceEndedWithinInstrumentedInterval)

$inProgressFactoryCaptureResult = Get-C188R12R3LifetimeEvidence `
    -SerialText $factoryEntered -CaptureState 'in-progress'
Assert-C188R12R3 'live capture keeps factory entry in progress pending its return marker' `
    ($inProgressFactoryCaptureResult.outcome -eq 'OBSERVATION_IN_PROGRESS' -and
     $inProgressFactoryCaptureResult.factoryStatus -eq 'entered-awaiting-return' -and
     -not $inProgressFactoryCaptureResult.traceEndedWithinInstrumentedInterval)

$emptyIncompleteCaptureResult = Get-C188R12R3LifetimeEvidence `
    -SerialText '' -CaptureState 'incomplete'
Assert-C188R12R3 'no marker in an incomplete serial capture is insufficient evidence' `
    ($emptyIncompleteCaptureResult.outcome -eq 'INSUFFICIENT_CAPTURE' -and
     $emptyIncompleteCaptureResult.observedBoundaryOutcome -eq 'MISSING_SETTINGS_LIFETIME_MARKER' -and
     $emptyIncompleteCaptureResult.captureReason -eq 'serial-capture-not-proven-complete')

$badOrder = @(
    (New-StandardPrefix) +
    (New-LifeLine 'factory-return' 0 'created') +
    (New-LifeLine 'factory-enter' -1 'invoking')
) -join "`n"
$badOrderResult = Get-CompleteLifetimeEvidence $badOrder
Assert-C188R12R3 'impossible marker order is rejected' `
    ($badOrderResult.outcome -eq 'INVALID_MARKERS' -and
     -not $badOrderResult.markerOrderingValid)

$malformed = (New-LifeLine 'enter' -1 'preflight') -replace 'identity=com.guidexos.apps.managed.settingscenter', 'identity=Managed Settings Center'
$malformedResult = Get-CompleteLifetimeEvidence $malformed
Assert-C188R12R3 'ambiguous identity formatting is rejected' `
    ($malformedResult.outcome -eq 'INVALID_MARKERS' -and
     $malformedResult.malformedMarkerCount -eq 1)

Write-Output "C188R12R3-LIFETIME-PREDICATES cases=$script:cases result=PASS"
