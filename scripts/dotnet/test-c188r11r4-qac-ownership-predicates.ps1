$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R11R4 QAC ownership predicate tests require PowerShell Core.'
}
. (Join-Path $PSScriptRoot 'c188r11qac-ownership-predicates.ps1')

$script:caseCount = 0
function Assert-QacOwnership([string]$Name, [bool]$Condition) {
    if (-not $Condition) { throw "C188R11R4 QAC ownership predicate failed: $Name" }
    $script:caseCount++
}
function Copy-QacFixture([System.Collections.IDictionary]$Fixture) {
    $copy = [ordered]@{}
    foreach ($key in $Fixture.Keys) { $copy[$key] = $Fixture[$key] }
    return $copy
}

$reservationId = '0123456789abcdef0123456789abcdef'
$runId = 'abcdef0123456789abcdef0123456789'
$exe = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$time = '2026-10-26T15:11:42.1652124+00:00'
$status = @'
Coordinator: Online
Name: guideXOS QAC
Version: 2.0.0
PID: 16064
Started (UTC): 2026-10-23T11:10:30.5849382+00:00
Uptime: 273670s
QEMU processes: 1 (0 foreign)
Managed development limit: 2
Managed sessions: 1; queued: 0; blocked: 0
'@
$emptyList = @'
Coordinator: Online (guideXOS QAC 2.0.0)
Reservations: 0
Managed sessions: 0
'@
$inspection = [ordered]@{
    id=$reservationId; project='guideXOSServer'; owner='TEST\guideX (PID 100)'
    mode='acceptance'; state='Running'; requestedAtUtc=$time; grantedAtUtc=$time
    queuePosition=0; processId=200; processStartTimeUtc=$time
    processIdentity=$exe; durationSeconds=2; reason=$null; degraded=$false
    ownerProcessStartedAtUtc=$time; ownerLiveness='Verified'
}
$run = [ordered]@{
    runIdentityId=$runId; bootEvidencePath='D:\evidence\guest-001'
    expectedBootEvidencePath='D:\evidence\guest-001'
    requestReservationId=$reservationId; launchReservationId=$reservationId
    launchJsonReservationId=$reservationId; activeReservationId=$reservationId
    launchChildProcessId=200; launchExecutableIdentity=$exe; launchProcessStartTimeUtc=$time
    launchJsonChildProcessId=200; launchJsonExecutableIdentity=$exe; launchJsonProcessStartTimeUtc=$time
    ownerProcessId=100; ownerProcessStartTimeUtc=$time; ownerParentProcessId=90
    qacOwnerStartedAtUtc=$time; ownerUser='TEST\guideX'; ownerProcessAlive=$true
    runnerProcessId=90;runnerProcessAlive=$true;runnerProcessStartTimeUtc=$time
    expectedRunnerProcessStartTimeUtc=$time
    childProcessId=200; childProcessStartTimeUtc=$time; childExecutableIdentity=$exe
    childParentProcessId=100; childProcessAlive=$true
    expectedQemuExecutableIdentity=$exe
}

$emptyEvidence = Get-C188R11R4QacOwnershipEvidence $status $emptyList $inspection $run
Assert-QacOwnership 'empty list is accepted only with matching inspect, status, and verified process identities' (
    [bool]$emptyEvidence.accepted -and $emptyEvidence.reservationId -eq $reservationId -and
    $emptyEvidence.qacListReservationCount -eq 0 -and $emptyEvidence.qacStatusForeignCount -eq 0)

$fullList = @"
Coordinator: Online (guideXOS QAC 2.0.0)
QEMU PID 200 | $exe | $time | managed
  Note: Reservation $reservationId · guideXOSServer · acceptance
Reservations: 1
  $reservationId | guideXOSServer | acceptance | Running | queue 0 | TEST\guideX (PID 100) | owner Verified | child PID 200
Managed sessions: 1
Managed session: $reservationId | Running | guideXOSServer · TEST\guideX (PID 100)
"@
$fullEvidence = Get-C188R11R4QacOwnershipEvidence $status $fullList $inspection $run
Assert-QacOwnership 'matching independent list rows corroborate the inspected session and child' ([bool]$fullEvidence.accepted)

$contradictoryIdentity = Copy-QacFixture $run
$contradictoryIdentity.launchReservationId = 'fedcba9876543210fedcba9876543210'
$contradictoryEvidence = Get-C188R11R4QacOwnershipEvidence $status $emptyList $inspection $contradictoryIdentity
Assert-QacOwnership 'contradictory reservation identities fail closed' (-not [bool]$contradictoryEvidence.accepted)

$foreignStatus = $status.Replace('QEMU processes: 1 (0 foreign)', 'QEMU processes: 1 (1 foreign)')
$foreignEvidence = Get-C188R11R4QacOwnershipEvidence $foreignStatus $emptyList $inspection $run
Assert-QacOwnership 'a foreign QEMU census fails closed' (-not [bool]$foreignEvidence.accepted)

$staleInspection = Copy-QacFixture $inspection
$staleInspection.state = 'Released'
$staleEvidence = Get-C188R11R4QacOwnershipEvidence $status $emptyList $staleInspection $run
Assert-QacOwnership 'a stale or released session fails closed' (-not [bool]$staleEvidence.accepted)

$missingOwner = Copy-QacFixture $inspection
$missingOwner.ownerLiveness = 'Unknown'
$missingOwner.owner = 'TEST\guideX'
$missingEvidence = Get-C188R11R4QacOwnershipEvidence $status $emptyList $missingOwner $run
Assert-QacOwnership 'missing verified owner identity fails closed' (-not [bool]$missingEvidence.accepted)

$wrongChildOwner = Copy-QacFixture $run
$wrongChildOwner.childParentProcessId = 999
$wrongParentEvidence = Get-C188R11R4QacOwnershipEvidence $status $emptyList $inspection $wrongChildOwner
Assert-QacOwnership 'a child process not owned by the inspected runner fails closed' (-not [bool]$wrongParentEvidence.accepted)

$wrongSessionList = $fullList.Replace($reservationId, 'fedcba9876543210fedcba9876543210')
$wrongListEvidence = Get-C188R11R4QacOwnershipEvidence $status $wrongSessionList $inspection $run
Assert-QacOwnership 'a contradictory nonempty QAC enumeration fails closed' (-not [bool]$wrongListEvidence.accepted)

$missingChildIdentity = Copy-QacFixture $run
$missingChildIdentity.childProcessAlive = $false
$missingChildEvidence = Get-C188R11R4QacOwnershipEvidence $status $emptyList $inspection $missingChildIdentity
Assert-QacOwnership 'an unverified or missing QEMU child fails closed' (-not [bool]$missingChildEvidence.accepted)

Write-Output "C188R11R4-QAC-OWNERSHIP cases=$script:caseCount result=PASS"
