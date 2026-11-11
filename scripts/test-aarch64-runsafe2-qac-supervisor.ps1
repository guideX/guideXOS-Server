[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'aarch64-runsafe2-qac-supervisor.psm1') -Force

$script:assertions = 0
function Assert-Case([bool]$Condition, [string]$Name) {
    if (!$Condition) { throw "FAIL: $Name" }
    ++$script:assertions
    Write-Host "PASS: $Name"
}

function New-Observation {
    param([hashtable]$Overrides = @{})
    $value = @{
        NowUtc = [DateTimeOffset]::Parse('2026-11-11T12:00:00Z')
        QacCommunicationOk = $true
        QacDiagnostic = ''
        ReservationOwnership = 'Owned'
        ReservationState = 'Waiting'
        ChildStatus = 'Pending'
        ChildPid = 0
        ChildStartUtc = ''
        GuestPass = $false
        StopRequested = $false
        RuntimeExpired = $false
        CleanupExpired = $false
        ReleaseDeadlineExpired = $false
        QacChildExitVerified = $false
        QacReleaseVerified = $false
        QacProcessExited = $false
        QacExitCode = $null
        FinalNoOwnedChild = $false
        FinalQacStateVerified = $false
        LogFinalizationOk = $true
    }
    foreach ($key in $Overrides.Keys) { $value[$key] = $Overrides[$key] }
    return $value
}

function Step([System.Collections.IDictionary]$Session, [hashtable]$Values = @{}) {
    $observation = New-Observation $Values
    return Step-QacSupervisor -Session $Session -Observation $observation
}

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Starting' }
Assert-Case ($session.phase -eq 'ChildStarting' -and $action -eq 'WaitForChildRegistration') 'Starting remains supervised while registration is pending'
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 321; ChildStartUtc = '2026-11-11T11:59:59Z' }
Assert-Case ($session.phase -eq 'ChildRunning' -and $action -eq 'MonitorChild') 'Starting transitions to Running with the exact child identity'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 322; ChildStartUtc = '2026-11-11T11:59:59Z' }
$action = Step $session @{ ReservationState = 'Starting'; ChildStatus = 'OwnedActive'; ChildPid = 322; ChildStartUtc = '2026-11-11T11:59:59Z' }
Assert-Case ($action -eq 'MonitorStartingChild' -and !$session.qualifying) 'Temporary Running to Starting preserves child supervision'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Pending' }
Assert-Case ($session.phase -eq 'ChildRegistrationPending' -and $action -eq 'WaitForChildRegistration') 'Delayed child registration does not end supervision'
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 323; ChildStartUtc = '2026-11-11T11:59:59Z' }
Assert-Case ($session.child_pid -eq 323 -and $action -eq 'MonitorChild') 'Delayed registration adopts only the verified direct child'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 324; ChildStartUtc = '2026-11-11T11:59:59Z' }
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Exited'; QacChildExitVerified = $true }
Assert-Case ($session.child_exit_verified -and $action -eq 'WaitForReservationRelease') 'Normal child exit waits for QAC reservation release'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 325; ChildStartUtc = '2026-11-11T11:59:59Z' }
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 325; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
Assert-Case ($session.guest_pass -and !$session.child_exit_verified -and !$session.qualifying -and $action -eq 'RequestGracefulStop') 'Guest PASS starts shutdown but does not qualify a live child'
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 325; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
Assert-Case ($action -eq 'RequestGracefulStop' -and !$session.qualifying) 'Repeated cleanup polls retry graceful QMP shutdown'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 326; ChildStartUtc = '2026-11-11T11:59:59Z' }
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 326; ChildStartUtc = '2026-11-11T11:59:59Z'; RuntimeExpired = $true }
Assert-Case ($session.runtime_timed_out -and !$session.qualifying -and $action -eq 'RequestGracefulStop') 'Runtime deadline requests QAC-authorized graceful termination and cannot pass'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 327; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 327; ChildStartUtc = '2026-11-11T11:59:59Z'; CleanupExpired = $true }
Assert-Case ($session.phase -eq 'CleanupUnverified' -and $action -eq 'RetainQacOwnership' -and !$session.qualifying) 'Cleanup deadline expiration retains QAC ownership and fails acceptance'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Ambiguous' }
Assert-Case ($session.phase -eq 'ChildIdentityAmbiguous' -and !$session.qualifying -and $action -eq 'ContinueSupervision') 'Ambiguous child status keeps monitoring without process control'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 328; ChildStartUtc = '2026-11-11T11:59:59Z'; ReservationOwnership = 'Lost' }
Assert-Case ($session.phase -eq 'ReservationOwnershipUncertain' -and !$session.qualifying) 'Lost reservation ownership cannot become a pass'

$session = New-QacSupervisorState
$action = Step $session @{ QacCommunicationOk = $false; QacDiagnostic = 'simulated pipe failure'; ChildStatus = 'OwnedActive'; ChildPid = 329; ChildStartUtc = '2026-11-11T11:59:59Z'; RuntimeExpired = $true }
Assert-Case ($session.phase -eq 'QacCommunicationUncertain' -and $action -eq 'RequestGracefulStop' -and !$session.qualifying) 'QAC communication failure retains supervision and timeout cleanup'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Unexpected'; ChildPid = 330 }
Assert-Case ($action -eq 'DoNotTouchUnexpectedChild' -and !$session.qualifying) 'Unexpected managed-child identity is never adopted or stopped'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Starting'; ChildStatus = 'UnrelatedOnly' }
Assert-Case ($action -eq 'WaitForChildRegistration' -and $null -eq $session.child_pid -and !$session.qualifying) 'Unrelated active QEMU is not mistaken for this session child'

$qmpGreeting = [pscustomobject]@{ QMP = [pscustomobject]@{ version = [pscustomobject]@{ qemu = '11.0' } } }
$qmpCommand = 'qemu-system-aarch64.exe -qmp tcp:127.0.0.1:55496,server=on,wait=off -name session-r2 -machine virt'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $false -ListenerOwnerPids @() -Greeting $qmpGreeting `
    -ExpectedQemuName 'session-r2' -ObservedQemuName ''
Assert-Case ($verification.ReadyForIdentityCheck -and !$verification.Verified) 'Socket-table fallback remains provisional until QMP reports the unique instance name'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $false -ListenerOwnerPids @() -Greeting $qmpGreeting `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'session-r2'
Assert-Case ($verification.Verified -and $verification.Method -eq 'ExactChildCommandLineAndQmpInstanceName') 'A valid QMP name safely falls back when the OS socket table is unavailable'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $true -ListenerOwnerPids @(554) -Greeting $qmpGreeting `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'session-r2'
Assert-Case ($verification.Verified -and $verification.Method -eq 'SocketTableAndQmpInstanceName') 'Matching socket owner and QMP instance name verify the endpoint'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $true -ListenerOwnerPids @(555) -Greeting $qmpGreeting `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'session-r2'
Assert-Case (!$verification.Verified) 'A foreign socket owner cannot receive QMP shutdown commands'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 556 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $false -ListenerOwnerPids @() -Greeting $qmpGreeting `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'session-r2'
Assert-Case (!$verification.Verified) 'A QMP greeting cannot bypass the exact child PID check'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $false -ListenerOwnerPids @() -Greeting $qmpGreeting `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'foreign-instance'
Assert-Case (!$verification.Verified) 'A foreign QMP instance name cannot receive shutdown commands'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $false -ListenerOwnerPids @() -Greeting ([pscustomobject]@{ greeting = 'not QMP' }) `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'session-r2'
Assert-Case (!$verification.ReadyForIdentityCheck) 'An invalid endpoint greeting cannot receive QMP commands'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 331; ChildStartUtc = '2026-11-11T11:59:59Z'; QacCommunicationOk = $false }
Assert-Case ($action -eq 'ContinueSupervision' -and !$session.qualifying) 'QAC communication failure without a stop condition remains nonqualifying'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 332; ChildStartUtc = '2026-11-11T11:59:59Z' }
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 333; ChildStartUtc = '2026-11-11T11:59:58Z'; GuestPass = $true }
Assert-Case ($action -eq 'DoNotTouchUnexpectedChild' -and $session.child_pid -eq 332 -and !$session.qualifying) 'Changed child identity cannot replace the owned child record'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 334; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Exited'; QacChildExitVerified = $true }
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Exited'; QacChildExitVerified = $true; ReleaseDeadlineExpired = $true }
Assert-Case ($session.phase -eq 'ReleaseUnverified' -and $action -eq 'PreserveReleaseUnverified' -and !$session.qualifying) 'Reservation release deadline is separate and finite'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Exited'; ChildStatus = 'Exited'; QacChildExitVerified = $true }
Assert-Case ($session.phase -eq 'ReservationStateAmbiguous' -and !$session.qualifying) 'QAC child exit is evidence, not an invented reservation state'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Waiting'; ChildStatus = 'Pending' }
$action = Step $session @{ ReservationState = 'Cancelled'; ChildStatus = 'Pending'; QacProcessExited = $true; QacExitCode = 130 }
Assert-Case (!$session.qualifying -and $action -eq 'PreserveAmbiguousTerminal') 'Cancelled queued admission cannot become a qualifying guest session'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Recovery Required'; ChildStatus = 'Ambiguous' }
Assert-Case ($session.phase -eq 'ChildIdentityAmbiguous' -and !$session.qualifying) 'QAC Recovery Required remains held and nonqualifying'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 335; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Exited'; QacChildExitVerified = $true }
$action = Step $session @{ ReservationState = 'Released'; ChildStatus = 'Exited'; QacChildExitVerified = $true; QacReleaseVerified = $true; QacProcessExited = $true; QacExitCode = 0; FinalNoOwnedChild = $true; FinalQacStateVerified = $true; LogFinalizationOk = $false }
Assert-Case (!$session.qualifying -and $action -ne 'Complete') 'Failed log finalization cannot report lifecycle acceptance'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 336; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Exited'; QacChildExitVerified = $true }
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Exited'; QacChildExitVerified = $true; FinalNoOwnedChild = $true; FinalQacStateVerified = $false; ReleaseDeadlineExpired = $true }
Assert-Case (!$session.qualifying -and $action -eq 'PreserveReleaseUnverified') 'Missing terminal status cannot report success'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 337; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Exited'; QacChildExitVerified = $true }
$action = Step $session @{ ReservationState = 'Released'; ChildStatus = 'Exited'; QacChildExitVerified = $true; QacReleaseVerified = $true; QacProcessExited = $true; QacExitCode = 0; FinalNoOwnedChild = $true; FinalQacStateVerified = $true; LogFinalizationOk = $true }
Assert-Case ($session.qualifying -and $session.phase -eq 'Qualified' -and $action -eq 'Complete') 'Only complete child exit and reservation release can qualify a session'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 338; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
$action = Step $session @{ ReservationState = 'Released'; ChildStatus = 'OwnedActive'; ChildPid = 338; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true; QacChildExitVerified = $true; QacReleaseVerified = $true; QacProcessExited = $true; QacExitCode = 0; FinalNoOwnedChild = $false; FinalQacStateVerified = $true; LogFinalizationOk = $true }
Assert-Case (!$session.qualifying -and $action -eq 'RetainQacOwnership') 'QAC release cannot qualify while the owned QEMU process is still active'

Write-Host "QAC supervision state-machine regressions: PASS ($script:assertions assertions)" -ForegroundColor Green
