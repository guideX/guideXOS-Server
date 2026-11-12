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
        QmpShutdownRequested = $false
        QmpShutdownRequestTransmitted = $false
        QmpShutdownAcknowledged = $false
        QmpShutdownResponseKind = 'NotAttempted'
        QmpShutdownError = ''
        QmpShutdownEventReason = ''
        RuntimeExpired = $false
        CleanupExpired = $false
        ReleaseDeadlineExpired = $false
        QacChildExitVerified = $false
        ManagedChildExitCode = $null
        QacReleaseVerified = $false
        QacProcessExited = $false
        QacExitCode = $null
        FinalNoOwnedChild = $false
        FinalQacStateVerified = $false
        LogFinalizationOk = $true
        ChildCleanupDeadlineMet = $false
        ReservationCleanupDeadlineMet = $false
    }
    foreach ($key in $Overrides.Keys) { $value[$key] = $Overrides[$key] }
    return $value
}

function Step([System.Collections.IDictionary]$Session, [hashtable]$Values = @{}) {
    $observation = New-Observation $Values
    return Step-QacSupervisor -Session $Session -Observation $observation
}

function Complete-SyntheticSession {
    param(
        [string]$ResponseKind = 'Acknowledged',
        [bool]$RequestTransmitted = $true,
        [bool]$QacChildExitRecord = $true,
        [int]$ManagedChildExitCode = 0,
        [bool]$ReservationReleased = $true,
        [bool]$ChildCleanupDeadlineMet = $true,
        [bool]$ReservationCleanupDeadlineMet = $true,
        [bool]$RuntimeExpired = $false,
        [string]$QmpError = ''
    )
    $session = New-QacSupervisorState
    $null = Step $session @{
        ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 8801
        ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true
        QmpShutdownRequested = $RequestTransmitted
        QmpShutdownRequestTransmitted = $RequestTransmitted
        QmpShutdownAcknowledged = ($ResponseKind -eq 'Acknowledged')
        QmpShutdownResponseKind = $ResponseKind; QmpShutdownError = $QmpError
    }
    $null = Step $session @{
        ReservationState = 'Running'; ChildStatus = 'Exited'
        QacChildExitVerified = $QacChildExitRecord
        ManagedChildExitCode = if ($QacChildExitRecord) { $ManagedChildExitCode } else { $null }
        QmpShutdownRequested = $RequestTransmitted
        QmpShutdownRequestTransmitted = $RequestTransmitted
        QmpShutdownAcknowledged = ($ResponseKind -eq 'Acknowledged')
        QmpShutdownResponseKind = $ResponseKind; QmpShutdownError = $QmpError
    }
    $releaseState = if ($ReservationReleased) { 'Released' } else { 'Running' }
    $action = Step $session @{
        ReservationState = $releaseState; ChildStatus = 'Exited'
        QacChildExitVerified = $QacChildExitRecord
        ManagedChildExitCode = if ($QacChildExitRecord) { $ManagedChildExitCode } else { $null }
        QacReleaseVerified = $ReservationReleased; QacProcessExited = $true; QacExitCode = 0
        QmpShutdownRequested = $RequestTransmitted
        QmpShutdownRequestTransmitted = $RequestTransmitted
        QmpShutdownAcknowledged = ($ResponseKind -eq 'Acknowledged')
        QmpShutdownResponseKind = $ResponseKind; QmpShutdownError = $QmpError
        RuntimeExpired = $RuntimeExpired
        ChildCleanupDeadlineMet = $ChildCleanupDeadlineMet
        ReservationCleanupDeadlineMet = $ReservationCleanupDeadlineMet
        FinalNoOwnedChild = $true; FinalQacStateVerified = $ReservationReleased
        LogFinalizationOk = $true
    }
    return [pscustomobject]@{ Session = $session; Action = $action }
}

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Starting' }
Assert-Case ($session.phase -eq 'ChildStarting' -and $action -eq 'WaitForChildRegistration') 'Starting remains supervised while registration is pending'
$action = Step $session @{ ReservationState = 'Starting' }
Assert-Case ($session.phase -eq 'ChildStarting' -and $action -eq 'WaitForChildRegistration') 'Repeated Starting observations retain supervision'
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
Assert-Case ($action -eq 'RequestGracefulStop' -and !$session.qualifying) 'Repeated cleanup polls retain exact-child shutdown supervision'

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
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 329; ChildStartUtc = '2026-11-11T11:59:59Z'; RuntimeExpired = $true }
Assert-Case ($session.child_pid -eq 329 -and $action -eq 'RequestGracefulStop' -and !$session.qualifying) 'Temporary QAC communication loss does not replace child identity'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Unexpected'; ChildPid = 330 }
Assert-Case ($action -eq 'DoNotTouchUnexpectedChild' -and !$session.qualifying) 'Unexpected managed-child identity is never adopted or stopped'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Starting'; ChildStatus = 'UnrelatedOnly' }
Assert-Case ($action -eq 'WaitForChildRegistration' -and $null -eq $session.child_pid -and !$session.qualifying) 'Unrelated active QEMU is not mistaken for this session child'
$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'UnrelatedOnly'; GuestPass = $true; QmpShutdownResponseKind = 'EndpointIdentityMismatch' }
Assert-Case ($action -eq 'WaitForChildRegistration' -and $null -eq $session.child_pid -and !$session.qmp_shutdown_request_transmitted) 'Another managed QEMU session remains untouched when its child identity does not match'

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
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $false -ListenerOwnerPids @() -Greeting $null `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'session-r2'
Assert-Case (!$verification.ReadyForIdentityCheck -and !$verification.Verified) 'A missing QMP endpoint greeting is rejected'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55497 -ListenerQueryReliable $false -ListenerOwnerPids @() -Greeting $qmpGreeting `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'session-r2'
Assert-Case (!$verification.ReadyForIdentityCheck -and !$verification.Verified) 'A command line without the exact session endpoint is rejected'
$verification = Get-QacQmpEndpointVerification -ExpectedChildPid 554 -ObservedChildPid 554 -CommandLine $qmpCommand `
    -QmpPort 55496 -ListenerQueryReliable $true -ListenerOwnerPids @(554, 555) -Greeting $qmpGreeting `
    -ExpectedQemuName 'session-r2' -ObservedQemuName 'session-r2'
Assert-Case (!$verification.Verified) 'Ambiguous socket ownership is rejected even when one owner is the expected child'

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

$completed = Complete-SyntheticSession -ResponseKind 'Acknowledged'
$session = $completed.Session
Assert-Case ($session.qualifying -and $session.phase -eq 'Qualified' -and $completed.Action -eq 'Complete') 'Acknowledged quit with verified child exit and release qualifies'
Assert-Case ($session.qmp_shutdown_acknowledged -and $session.child_exit_code -eq 0 -and $session.reservation_release_verified -and $session.child_cleanup_deadline_met -and $session.reservation_cleanup_deadline_met) 'Acknowledgement, child exit code, release, and both cleanup deadlines remain independent evidence'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 339; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true; QmpShutdownRequested = $true; QmpShutdownAcknowledged = $true }
Assert-Case ($session.qmp_shutdown_requested -and $session.qmp_shutdown_acknowledged -and !$session.child_exit_verified -and !$session.qualifying) 'QMP quit acknowledgement is recorded while the live child remains supervised'

$completed = Complete-SyntheticSession -ResponseKind 'PrematureEofAfterTransmission'
$session = $completed.Session
Assert-Case ($session.qualifying -and $session.qmp_shutdown_request_transmitted -and !$session.qmp_shutdown_acknowledged -and $completed.Action -eq 'Complete') 'EOF after a flushed quit qualifies after verified bounded child exit and release'

$completed = Complete-SyntheticSession -ResponseKind 'ReadTimeoutAfterTransmission'
Assert-Case ($completed.Session.qualifying -and $completed.Session.qmp_shutdown_response_kind -eq 'ReadTimeoutAfterTransmission' -and !$completed.Session.qmp_shutdown_acknowledged) 'Read timeout after transmission qualifies after verified child exit'

$session = New-QacSupervisorState
$action = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 8802; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true; QmpShutdownRequestTransmitted = $true; QmpShutdownResponseKind = 'ReadTimeoutAfterTransmission' }
Assert-Case (!$session.qualifying -and !$session.child_exit_verified -and $action -eq 'RequestGracefulStop') 'Read timeout with the managed child still alive remains nonqualifying'

$completed = Complete-SyntheticSession -ResponseKind 'ConnectionFailureBeforeTransmission' -RequestTransmitted $false
$session = $completed.Session
Assert-Case (!$session.qualifying -and !$session.qmp_shutdown_request_transmitted -and (Test-QacSupervisorPartial -Session $session -GuestPass $true -FinalQacStateVerified $true -RuntimeExpired $false -ChildCleanupExpired $false -ReservationCleanupExpired $false)) 'Failure before quit transmission stays partial despite a bounded unrelated child exit'

$completed = Complete-SyntheticSession -ResponseKind 'ExplicitError' -QmpError 'command rejected'
$session = $completed.Session
Assert-Case (!$session.qualifying -and $session.qmp_shutdown_request_transmitted -and $session.qmp_shutdown_error -eq 'command rejected' -and !(Test-QacSupervisorPartial -Session $session -GuestPass $true -FinalQacStateVerified $true -RuntimeExpired $false -ChildCleanupExpired $false -ReservationCleanupExpired $false)) 'Explicit QMP error is preserved and cannot be silently accepted'

$completed = Complete-SyntheticSession -ResponseKind 'Acknowledged' -QacChildExitRecord $false
Assert-Case (!$completed.Session.qualifying -and !$completed.Session.child_exit_verified -and $completed.Action -ne 'Complete') 'Missing QAC child-exit record blocks lifecycle completion'

$completed = Complete-SyntheticSession -ResponseKind 'Acknowledged' -ReservationReleased $false
Assert-Case (!$completed.Session.qualifying -and !$completed.Session.reservation_release_verified -and $completed.Action -ne 'Complete') 'Reservation release failure blocks lifecycle completion'

$completed = Complete-SyntheticSession -ResponseKind 'Acknowledged' -ChildCleanupDeadlineMet $false
Assert-Case (!$completed.Session.qualifying -and !$completed.Session.child_cleanup_deadline_met) 'Child cleanup deadline overrun blocks lifecycle completion'

$completed = Complete-SyntheticSession -ResponseKind 'Acknowledged' -ReservationCleanupDeadlineMet $false
Assert-Case (!$completed.Session.qualifying -and !$completed.Session.reservation_cleanup_deadline_met) 'Reservation cleanup deadline overrun blocks lifecycle completion'

$completed = Complete-SyntheticSession -ResponseKind 'Acknowledged' -ManagedChildExitCode 1
Assert-Case (!$completed.Session.qualifying -and $completed.Session.child_exit_code -eq 1) 'Nonzero managed-child exit remains independent and nonqualifying'

$completed = Complete-SyntheticSession -ResponseKind 'Acknowledged' -RuntimeExpired $true
Assert-Case (!$completed.Session.qualifying -and $completed.Session.runtime_timed_out) 'Runtime deadline remains an independent acceptance gate'

Assert-Case (!(Test-QacSupervisorPartial -Session $session -GuestPass $true -FinalQacStateVerified $false -RuntimeExpired $false -ChildCleanupExpired $false -ReservationCleanupExpired $false)) 'An unresolved QAC final state cannot be classified as partial cleanup success'
Assert-Case ((Get-QacManagedChildLifetimeSeconds -ChildStartUtc '2026-11-11T19:41:30.6868455Z' -ChildExitUtc '2026-11-11T19:42:03.0492010Z') -eq 32.362) 'Managed child lifetime uses exact QAC launch and exit timestamps'
$invalidLifetime = Get-QacManagedChildLifetimeSeconds -ChildStartUtc '2026-11-11T19:42:03Z' -ChildExitUtc '2026-11-11T19:41:30Z'
Assert-Case ($null -eq $invalidLifetime) 'Invalid or reversed child timestamps do not produce a lifetime claim'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 341; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'Exited'; QacChildExitVerified = $true; ManagedChildExitCode = 0 }
$action = Step $session @{ ReservationState = 'Released'; ChildStatus = 'Exited'; QacChildExitVerified = $true; ManagedChildExitCode = 0; QacReleaseVerified = $true; QacProcessExited = $true; QacExitCode = 0; FinalNoOwnedChild = $true; FinalQacStateVerified = $true; ChildCleanupDeadlineMet = $true; ReservationCleanupDeadlineMet = $true; LogFinalizationOk = $true }
Assert-Case (!$session.qualifying -and $action -ne 'Complete') 'A child that exits before quit transmission cannot qualify'

$session = New-QacSupervisorState
$null = Step $session @{ ReservationState = 'Running'; ChildStatus = 'OwnedActive'; ChildPid = 338; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true }
$action = Step $session @{ ReservationState = 'Released'; ChildStatus = 'OwnedActive'; ChildPid = 338; ChildStartUtc = '2026-11-11T11:59:59Z'; GuestPass = $true; QacChildExitVerified = $true; QacReleaseVerified = $true; QacProcessExited = $true; QacExitCode = 0; FinalNoOwnedChild = $false; FinalQacStateVerified = $true; LogFinalizationOk = $true }
Assert-Case (!$session.qualifying -and $action -eq 'RetainQacOwnership') 'QAC release cannot qualify while the owned QEMU process is still active'

$runnerSource = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'run-aarch64-runsafe2-qac.ps1'))
Assert-Case ($runnerSource.Contains("`$sessionPath + '.partial.json'") -and $runnerSource.Contains('partial_log_finalization_error')) 'A failed session save has a partial evidence preservation path'
Assert-Case ($runnerSource.Contains('$script:qmpQuitSent = $true') -and
    $runnerSource.Contains('$script:qmpQuitAcknowledged = $true') -and
    $runnerSource.Contains('$Writer.Flush()') -and
    $runnerSource.Contains('PrematureEofAfterTransmission') -and
    $runnerSource.Contains('ReadTimeoutAfterTransmission')) 'Runner records flushed transmission and allowed missing responses separately'
Assert-Case ($runnerSource.Contains('if ($script:qmpQuitCommandAttempted) { return $false }') -and
    $runnerSource.Contains('qmp_shutdown_cause_established')) 'Runner does not resend an ambiguous quit and records an observed shutdown cause'
Assert-Case ($runnerSource.Contains('if ($null -eq $line)')) 'Only true stream EOF is classified as the protocol-permitted missing response'

Write-Host "QAC supervision state-machine regressions: PASS ($script:assertions assertions)" -ForegroundColor Green
