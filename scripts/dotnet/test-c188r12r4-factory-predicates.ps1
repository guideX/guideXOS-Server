$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r12r4-factory-predicates.ps1')
$fixtureRoot = Join-Path $PSScriptRoot 'fixtures/c188r12r4-factory'
$cases = 0
function Assert-C188R12R4([string]$Name, [string]$Fixture, [string]$Expected,
    [string]$CaptureState = 'complete') {
    $path = Join-Path $fixtureRoot $Fixture
    $text = Get-Content -LiteralPath $path -Raw
    $evidence = Get-C188R12R4FactoryEvidence -SerialText $text -CaptureState $CaptureState
    $script:cases++
    if ($evidence.outcome -ne $Expected) {
        throw "$Name expected $Expected but got $($evidence.outcome) (fixture $Fixture)."
    }
}

Assert-C188R12R4 'Factory entry without constructor entry' 'factory-entry-no-constructor.log' 'FACTORY_ENTRY_NO_CONSTRUCTOR_ENTRY'
Assert-C188R12R4 'Constructor entry without return' 'constructor-entry-no-return.log' 'CONSTRUCTOR_ENTRY_NO_RETURN'
Assert-C188R12R4 'Allocation slow path without result' 'allocation-request-no-result.log' 'ALLOCATION_REQUEST_NO_RESULT'
Assert-C188R12R4 'Explicit bounded allocator failure' 'explicit-allocation-failure.log' 'EXPLICIT_ALLOCATION_FAILURE'
$nativeAllocation = Get-C188R12R4FactoryEvidence `
    -SerialText (Get-Content -LiteralPath (Join-Path $fixtureRoot 'explicit-allocation-failure.log') -Raw) `
    -CaptureState 'complete'
if (-not $nativeAllocation.rootCauseProven -or
    $nativeAllocation.firstIncompleteOperation -ne 'allocation:array:fast-capacity-miss' -or
    $nativeAllocation.allocationEvents.Count -ne 1 -or
    (Convert-C188R12R4TraceNumber $nativeAllocation.allocationEvents[0].invocation) -ne '2') {
    throw 'Native fixed-width hexadecimal allocation markers were not correlated to the decimal factory invocation.'
}
Assert-C188R12R4 'Constructor success without factory return' 'constructor-success-factory-nonreturn.log' 'CONSTRUCTOR_SUCCESS_FACTORY_NONRETURN'
Assert-C188R12R4 'Factory return without completed registration' 'factory-success-registration-failure.log' 'FACTORY_SUCCESS_REGISTRATION_FAILURE'
Assert-C188R12R4 'Full Settings startup' 'complete-settings-startup.log' 'SETTINGS_STARTUP_SUCCESS'
Assert-C188R12R4 'A truncated capture cannot imply a stopped constructor' 'constructor-entry-no-return.log' 'CAPTURE_INCOMPLETE' 'incomplete'
Assert-C188R12R4 'Type initialization without return' 'type-initialization-no-return.log' 'TYPE_INITIALIZATION_NO_RETURN'
if ($cases -ne 9) { throw "C188R12R4 predicate case count mismatch: $cases" }
Write-Output "C188R12R4-FACTORY-PREDICATES cases=$cases result=PASS syntheticFixtures=true"
