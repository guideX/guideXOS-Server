Set-StrictMode -Version Latest

$script:QacReservationStates = @(
    'Waiting', 'Blocked', 'Granted', 'Starting', 'Running',
    'Released', 'Cancelled', 'Expired', 'Failed', 'Recovery Required'
)

function New-QacSupervisorState {
    return [ordered]@{
        phase = 'AdmissionPending'
        reservation_state = ''
        child_pid = $null
        child_start_utc = ''
        guest_pass = $false
        runtime_timed_out = $false
        stop_requested = $false
        qmp_shutdown_requested = $false
        qmp_shutdown_acknowledged = $false
        child_exit_verified = $false
        reservation_release_verified = $false
        qac_exit_verified = $false
        qac_exit_code = $null
        failure = ''
        identity_ambiguous = $false
        qualifying = $false
        last_action = 'Wait'
        transitions = [System.Collections.Generic.List[object]]::new()
    }
}

function Add-QacTransition {
    param(
        [Parameter(Mandatory = $true)] [System.Collections.IDictionary] $Session,
        [Parameter(Mandatory = $true)] [string] $Kind,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string] $Detail,
        [Parameter(Mandatory = $true)] [DateTimeOffset] $AtUtc
    )
    $Session.transitions.Add([ordered]@{
        at_utc = $AtUtc.ToUniversalTime().ToString('o')
        kind = $Kind
        detail = $Detail
    })
}

function Get-QacQmpEndpointVerification {
    param(
        [Parameter(Mandatory = $true)] [int] $ExpectedChildPid,
        [Parameter(Mandatory = $true)] [int] $ObservedChildPid,
        [Parameter(Mandatory = $true)] [string] $CommandLine,
        [Parameter(Mandatory = $true)] [int] $QmpPort,
        [Parameter(Mandatory = $true)] [bool] $ListenerQueryReliable,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [int[]] $ListenerOwnerPids,
        [Parameter(Mandatory = $true)] [AllowNull()] [object] $Greeting,
        [Parameter(Mandatory = $true)] [string] $ExpectedQemuName,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string] $ObservedQemuName
    )

    $result = [ordered]@{ ReadyForIdentityCheck = $false; Verified = $false; Method = ''; Diagnostic = '' }
    if ($ExpectedChildPid -le 0 -or $ObservedChildPid -ne $ExpectedChildPid) {
        $result.Diagnostic = 'The observed QEMU PID does not match the recorded QAC child.'
        return [pscustomobject]$result
    }
    $endpointArgument = "tcp:127.0.0.1:$QmpPort,server=on,wait=off"
    if (!$CommandLine.Contains($endpointArgument)) {
        $result.Diagnostic = 'The exact QAC child command line does not contain the expected QMP endpoint.'
        return [pscustomobject]$result
    }
    if (!$CommandLine.Contains("-name $ExpectedQemuName")) {
        $result.Diagnostic = 'The exact QAC child command line does not contain this session QEMU name.'
        return [pscustomobject]$result
    }
    if ($ListenerQueryReliable -and $ListenerOwnerPids.Count -gt 0 -and
        ($ListenerOwnerPids.Count -ne 1 -or $ListenerOwnerPids[0] -ne $ExpectedChildPid)) {
        $result.Diagnostic = "The socket table assigns QMP port $QmpPort to a different or ambiguous process."
        return [pscustomobject]$result
    }
    if (!$Greeting -or !($Greeting.PSObject.Properties.Name -contains 'QMP') -or !$Greeting.QMP.version) {
        $result.Diagnostic = 'The configured endpoint did not return a valid QEMU QMP greeting.'
        return [pscustomobject]$result
    }
    $result.ReadyForIdentityCheck = $true
    if ([string]::IsNullOrEmpty($ObservedQemuName) -or
        ![string]::Equals($ObservedQemuName, $ExpectedQemuName, [StringComparison]::Ordinal)) {
        $result.Diagnostic = "The QMP instance name does not match the exact session name '$ExpectedQemuName'."
        return [pscustomobject]$result
    }

    $result.Verified = $true
    if ($ListenerQueryReliable -and $ListenerOwnerPids.Count -eq 1 -and $ListenerOwnerPids[0] -eq $ExpectedChildPid) {
        $result.Method = 'SocketTableAndQmpInstanceName'
    } else {
        $result.Method = 'ExactChildCommandLineAndQmpInstanceName'
        if (!$ListenerQueryReliable) {
            $result.Diagnostic = 'The OS socket table was unavailable; the endpoint was checked against the exact QAC child command line, QMP greeting, and instance name.'
        } else {
            $result.Diagnostic = 'The OS socket table returned no owner for the configured endpoint; the endpoint was checked against the exact QAC child command line, QMP greeting, and instance name.'
        }
    }
    return [pscustomobject]$result
}

function Step-QacSupervisor {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] [System.Collections.IDictionary] $Session,
        [Parameter(Mandatory = $true)] [System.Collections.IDictionary] $Observation
    )

    $now = if ($Observation.Contains('NowUtc')) { [DateTimeOffset]$Observation.NowUtc } else { [DateTimeOffset]::UtcNow }
    $Session.qualifying = $false
    $action = 'ContinueSupervision'
    $reservationState = [string]$Observation.ReservationState
    $childStatus = [string]$Observation.ChildStatus

    if ($Observation.Contains('QmpShutdownRequested') -and $Observation.QmpShutdownRequested) {
        if (!$Session.qmp_shutdown_requested) {
            Add-QacTransition $Session 'QmpShutdownRequested' 'The validated QMP endpoint received the graceful quit command.' $now
        }
        $Session.qmp_shutdown_requested = $true
    }
    if ($Observation.Contains('QmpShutdownAcknowledged') -and $Observation.QmpShutdownAcknowledged) {
        if (!$Session.qmp_shutdown_acknowledged) {
            Add-QacTransition $Session 'QmpShutdownAcknowledged' 'QMP acknowledged the graceful quit command.' $now
        }
        $Session.qmp_shutdown_acknowledged = $true
    }

    if ($Observation.QacCommunicationOk -ne $true) {
        if ($Session.phase -ne 'QacCommunicationUncertain') {
            Add-QacTransition $Session 'QacCommunicationFailure' ([string]$Observation.QacDiagnostic) $now
        }
        $Session.phase = 'QacCommunicationUncertain'
        if ([string]::IsNullOrWhiteSpace([string]$Session.failure)) {
            $Session.failure = 'QAC communication failed; ownership remains unresolved.'
        }
        if ($Observation.GuestPass) { $Session.guest_pass = $true }
        if ($Observation.RuntimeExpired) { $Session.runtime_timed_out = $true }
        if ($Observation.StopRequested) { $Session.stop_requested = $true }
        if ($childStatus -eq 'OwnedActive' -and ($Observation.GuestPass -or $Observation.RuntimeExpired -or $Observation.StopRequested)) {
            $Session.stop_requested = $true
            $Session.runtime_timed_out = [bool]($Session.runtime_timed_out -or $Observation.RuntimeExpired)
            $action = 'RequestGracefulStop'
        }
        elseif ($Observation.CleanupExpired -and $childStatus -eq 'OwnedActive') {
            $Session.phase = 'CleanupUnverified'
            $Session.failure = 'Child cleanup deadline expired; QAC ownership is retained.'
            $action = 'RetainQacOwnership'
        }
        $Session.last_action = $action
        return $action
    }

    if ($Observation.ReservationOwnership -eq 'Lost') {
        if ($Session.phase -ne 'ReservationOwnershipUncertain') {
            Add-QacTransition $Session 'ReservationOwnershipLost' 'The exact reservation was not present under this QAC owner.' $now
        }
        $Session.phase = 'ReservationOwnershipUncertain'
        if ([string]::IsNullOrWhiteSpace([string]$Session.failure)) {
            $Session.failure = 'Reservation ownership could not be verified; keep supervision active.'
        }
        if ($Observation.GuestPass) { $Session.guest_pass = $true }
        if ($Observation.RuntimeExpired) { $Session.runtime_timed_out = $true }
        if ($Observation.StopRequested) { $Session.stop_requested = $true }
        if ($childStatus -eq 'OwnedActive' -and ($Observation.GuestPass -or $Observation.RuntimeExpired -or $Observation.StopRequested)) {
            $Session.stop_requested = $true
            $Session.runtime_timed_out = [bool]($Session.runtime_timed_out -or $Observation.RuntimeExpired)
            $action = 'RequestGracefulStop'
        }
        elseif ($Observation.CleanupExpired -and $childStatus -eq 'OwnedActive') {
            $Session.phase = 'CleanupUnverified'
            $Session.failure = 'Child cleanup deadline expired; QAC ownership is retained.'
            $action = 'RetainQacOwnership'
        }
        $Session.last_action = $action
        return $action
    }

    if ($reservationState -notin $script:QacReservationStates) {
        if ($Session.phase -ne 'ReservationStateAmbiguous') {
            Add-QacTransition $Session 'UnexpectedReservationState' ("QAC returned state '$reservationState'.") $now
        }
        $Session.phase = 'ReservationStateAmbiguous'
        if ([string]::IsNullOrWhiteSpace([string]$Session.failure)) {
            $Session.failure = "Unexpected or missing QAC reservation state '$reservationState'; supervision is retained."
        }
        if ($Observation.GuestPass) { $Session.guest_pass = $true }
        if ($Observation.RuntimeExpired) { $Session.runtime_timed_out = $true }
        if ($Observation.StopRequested) { $Session.stop_requested = $true }
        if ($childStatus -eq 'OwnedActive' -and ($Observation.GuestPass -or $Observation.RuntimeExpired -or $Observation.StopRequested)) {
            $Session.stop_requested = $true
            $Session.runtime_timed_out = [bool]($Session.runtime_timed_out -or $Observation.RuntimeExpired)
            $action = 'RequestGracefulStop'
        }
        elseif ($Observation.CleanupExpired -and $childStatus -eq 'OwnedActive') {
            $Session.phase = 'CleanupUnverified'
            $Session.failure = 'Child cleanup deadline expired; QAC ownership is retained.'
            $action = 'RetainQacOwnership'
        }
        $Session.last_action = $action
        return $action
    }

    if ($Session.reservation_state -ne $reservationState) {
        Add-QacTransition $Session 'ReservationState' ("$($Session.reservation_state) -> $reservationState") $now
        $Session.reservation_state = $reservationState
    }

    if ($Observation.GuestPass) { $Session.guest_pass = $true }
    if ($Observation.StopRequested) { $Session.stop_requested = $true }
    if ($Observation.RuntimeExpired) {
        $Session.runtime_timed_out = $true
        if ([string]::IsNullOrWhiteSpace([string]$Session.failure)) {
            $Session.failure = 'Guest runtime deadline expired.'
        }
    }
    if ($Observation.QacChildExitVerified) { $Session.child_exit_verified = $true }
    if ($Observation.QacReleaseVerified -or $reservationState -eq 'Released') {
        $Session.reservation_release_verified = $true
    }
    if ($Observation.QacProcessExited) {
        $Session.qac_exit_verified = $true
        if ($null -ne $Observation.QacExitCode) { $Session.qac_exit_code = [int]$Observation.QacExitCode }
    }

    if ($reservationState -eq 'Recovery Required') {
        $Session.phase = 'QacRecoveryRequired'
        if ([string]::IsNullOrWhiteSpace([string]$Session.failure)) {
            $Session.failure = 'QAC reports Recovery Required; ownership remains held and lifecycle acceptance is refused.'
        }
    }

    if ($childStatus -eq 'Unexpected') {
        $Session.phase = 'ChildIdentityAmbiguous'
        $Session.identity_ambiguous = $true
        $Session.failure = 'An unexpected QEMU identity was observed; it was not adopted or stopped.'
        $action = 'DoNotTouchUnexpectedChild'
    }
    elseif ($childStatus -eq 'Ambiguous') {
        $Session.phase = 'ChildIdentityAmbiguous'
        if ([string]::IsNullOrWhiteSpace([string]$Session.failure)) {
            $Session.failure = 'Managed-child identity is ambiguous; supervision continues without process control.'
        }
        $Session.identity_ambiguous = $true
        $action = 'ContinueSupervision'
    }
    elseif ($childStatus -eq 'OwnedActive') {
        $pidValue = [int]$Observation.ChildPid
        $startValue = [string]$Observation.ChildStartUtc
        if ($null -ne $Session.child_pid -and [int]$Session.child_pid -ne $pidValue) {
            $Session.phase = 'ChildIdentityAmbiguous'
            $Session.identity_ambiguous = $true
            $Session.failure = 'The observed managed-child PID changed during one reservation.'
            $action = 'DoNotTouchUnexpectedChild'
        }
        else {
            if ($null -eq $Session.child_pid) {
                $Session.child_pid = $pidValue
                $Session.child_start_utc = $startValue
                Add-QacTransition $Session 'ManagedChildIdentified' ("PID $pidValue start=$startValue") $now
            }
            if ($reservationState -eq 'Released') {
                $Session.phase = 'OwnershipConflict'
                $Session.failure = 'QAC reported Released while the exact managed QEMU child is still active.'
                $action = 'RetainQacOwnership'
            }
            elseif ($Session.stop_requested -or $Session.guest_pass -or $Session.runtime_timed_out) {
                $Session.stop_requested = $true
                $Session.phase = 'ChildStopping'
                $action = 'RequestGracefulStop'
            }
            elseif ($reservationState -eq 'Starting') {
                $Session.phase = 'ChildStarting'
                $action = 'MonitorStartingChild'
            }
            else {
                $Session.phase = 'ChildRunning'
                $action = 'MonitorChild'
            }
        }
    }
    elseif ($childStatus -eq 'Exited') {
        if (!$Session.child_exit_verified -and !$Observation.QacChildExitVerified) {
            $Session.phase = 'ChildExitUnverified'
            if ([string]::IsNullOrWhiteSpace([string]$Session.failure)) {
                $Session.failure = 'Child is absent but QAC has not verified its exit.'
            }
            $action = 'ContinueSupervision'
        }
        else {
            $Session.child_exit_verified = $true
            if ($reservationState -eq 'Released' -and $Session.reservation_release_verified) {
                $Session.phase = 'ReservationReleased'
                $action = 'VerifyFinalState'
            }
            elseif ($Observation.ReleaseDeadlineExpired) {
                $Session.phase = 'ReleaseUnverified'
                $Session.failure = 'Reservation release deadline expired after child exit.'
                $action = 'PreserveReleaseUnverified'
            }
            else {
                $Session.phase = 'ChildExitedAwaitingRelease'
                $action = 'WaitForReservationRelease'
            }
        }
    }
    elseif ($childStatus -eq 'UnrelatedOnly' -or $childStatus -eq 'Missing' -or $childStatus -eq 'Pending') {
        if ($reservationState -eq 'Starting' -or $reservationState -eq 'Granted' -or $reservationState -eq 'Running') {
            $Session.phase = if ($reservationState -eq 'Running') { 'ChildRegistrationPending' } else { 'ChildStarting' }
            $action = 'WaitForChildRegistration'
        }
        elseif ($reservationState -in @('Waiting', 'Blocked')) {
            $Session.phase = 'AdmissionPending'
            $action = 'WaitForAdmission'
        }
        elseif ($reservationState -eq 'Recovery Required') {
            $Session.phase = 'QacRecoveryRequired'
            $action = 'ContinueSupervision'
        }
        elseif ($reservationState -in @('Released', 'Cancelled', 'Expired', 'Failed')) {
            if (!$Session.child_exit_verified) {
                $Session.phase = 'TerminalWithoutChildExit'
                $Session.failure = 'QAC ended the reservation without child-exit evidence.'
                $action = 'PreserveAmbiguousTerminal'
            }
            else {
                $Session.phase = 'ReservationReleased'
                $Session.reservation_release_verified = [bool]($reservationState -eq 'Released' -or $Observation.QacReleaseVerified)
                $action = 'VerifyFinalState'
            }
        }
        else {
            $Session.phase = 'ChildRegistrationPending'
            $action = 'WaitForChildRegistration'
        }
    }
    else {
        $Session.phase = 'ChildStatusAmbiguous'
        if ([string]::IsNullOrWhiteSpace([string]$Session.failure)) {
            $Session.failure = "Unrecognized child status '$childStatus'; supervision continues."
        }
        $action = 'ContinueSupervision'
    }

    if ($Observation.CleanupExpired -and $childStatus -eq 'OwnedActive') {
        $Session.phase = 'CleanupUnverified'
        $Session.failure = 'Child cleanup deadline expired; QAC ownership is retained.'
        $action = 'RetainQacOwnership'
    }
    elseif ($Observation.ReleaseDeadlineExpired -and !$Session.reservation_release_verified -and $Session.child_exit_verified) {
        $Session.phase = 'ReleaseUnverified'
        $Session.failure = 'Reservation release deadline expired after child exit.'
        $action = 'PreserveReleaseUnverified'
    }

    if ($Session.child_exit_verified -and $Session.reservation_release_verified -and
        $Session.qac_exit_verified -and $Session.qac_exit_code -eq 0 -and
        $Session.guest_pass -and $Session.qmp_shutdown_requested -and $Session.qmp_shutdown_acknowledged -and !$Session.runtime_timed_out -and
        !$Session.identity_ambiguous -and
        $Observation.FinalNoOwnedChild -and $Observation.FinalQacStateVerified -and
        $Observation.LogFinalizationOk -and !$Observation.CleanupExpired -and
        !$Observation.ReleaseDeadlineExpired) {
        $Session.phase = 'Qualified'
        $Session.qualifying = $true
        $action = 'Complete'
    }

    $Session.last_action = $action
    return $action
}

function Test-QacSupervisorPartial {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] [System.Collections.IDictionary] $Session,
        [Parameter(Mandatory = $true)] [bool] $GuestPass,
        [Parameter(Mandatory = $true)] [bool] $FinalQacStateVerified,
        [Parameter(Mandatory = $true)] [bool] $RuntimeExpired,
        [Parameter(Mandatory = $true)] [bool] $ChildCleanupExpired,
        [Parameter(Mandatory = $true)] [bool] $ReservationCleanupExpired
    )

    return [bool](!$Session.qualifying -and
        $Session.qmp_shutdown_requested -and !$Session.qmp_shutdown_acknowledged -and
        $GuestPass -and $Session.child_exit_verified -and
        $Session.reservation_release_verified -and $Session.qac_exit_verified -and
        $Session.qac_exit_code -eq 0 -and $FinalQacStateVerified -and
        !$RuntimeExpired -and !$ChildCleanupExpired -and !$ReservationCleanupExpired)
}

function Get-QacManagedChildLifetimeSeconds {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] [string] $ChildStartUtc,
        [Parameter(Mandatory = $true)] [string] $ChildExitUtc
    )

    $start = [DateTimeOffset]::MinValue
    $exit = [DateTimeOffset]::MinValue
    if (![DateTimeOffset]::TryParse($ChildStartUtc, [ref]$start) -or
        ![DateTimeOffset]::TryParse($ChildExitUtc, [ref]$exit) -or $exit -lt $start) {
        return $null
    }
    return [Math]::Round(($exit.ToUniversalTime() - $start.ToUniversalTime()).TotalSeconds, 3)
}

Export-ModuleMember -Function New-QacSupervisorState, Add-QacTransition, Get-QacQmpEndpointVerification, Step-QacSupervisor, Test-QacSupervisorPartial, Get-QacManagedChildLifetimeSeconds
