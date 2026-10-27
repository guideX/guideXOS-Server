function Test-C188R11R4QacTimestamp([object]$Left, [object]$Right, [double]$ToleranceSeconds = 2.0) {
    if ($null -eq $Left -or $null -eq $Right) { return $false }
    try {
        $leftTime = [DateTimeOffset]::Parse([string]$Left).ToUniversalTime()
        $rightTime = [DateTimeOffset]::Parse([string]$Right).ToUniversalTime()
        return [Math]::Abs(($leftTime - $rightTime).TotalSeconds) -le $ToleranceSeconds
    } catch {
        return $false
    }
}

function Get-C188R11R4QacOwnershipEvidence(
    [string]$StatusText,
    [string]$ListText,
    [object]$Inspection,
    [object]$RunIdentity
) {
    $reasons = [System.Collections.Generic.List[string]]::new()
    $processRows = @()
    if ($Inspection -is [string]) {
        try { $Inspection = $Inspection | ConvertFrom-Json -ErrorAction Stop }
        catch { $Inspection = $null }
    }
    if ($RunIdentity -is [string]) {
        try { $RunIdentity = $RunIdentity | ConvertFrom-Json -ErrorAction Stop }
        catch { $RunIdentity = $null }
    }
    if ($null -eq $Inspection) { $reasons.Add('reservation-inspection-missing-or-invalid') }
    if ($null -eq $RunIdentity) { $reasons.Add('current-run-identity-missing-or-invalid') }

    $statusQemu = [regex]::Match($StatusText, '(?m)^QEMU processes:\s*(?<count>\d+) \((?<foreign>\d+) foreign\)\s*$')
    $statusSessions = [regex]::Match($StatusText, '(?m)^Managed sessions:\s*(?<count>\d+); queued:\s*(?<queued>\d+); blocked:\s*(?<blocked>\d+)\s*$')
    if ($StatusText -notmatch '(?m)^Coordinator:\s+Online\s*$' -or
        -not $statusQemu.Success -or $statusQemu.Groups['count'].Value -ne '1' -or
        $statusQemu.Groups['foreign'].Value -ne '0' -or
        -not $statusSessions.Success -or $statusSessions.Groups['count'].Value -ne '1' -or
        $statusSessions.Groups['queued'].Value -ne '0' -or
        $statusSessions.Groups['blocked'].Value -ne '0' -or
        $StatusText -match '(?m)^Recovery:\s+Required') {
        $reasons.Add('qac-status-not-exclusive-single-nonforeign-session')
    }

    if ($null -ne $Inspection -and $null -ne $RunIdentity) {
        $owner = [regex]::Match([string]$Inspection.owner, '^(?<user>.+?) \(PID (?<pid>\d+)\)$')
        $expectedReservationIds = @(
            [string]$RunIdentity.requestReservationId,
            [string]$RunIdentity.launchReservationId,
            [string]$RunIdentity.launchJsonReservationId,
            [string]$RunIdentity.activeReservationId,
            [string]$Inspection.id
        ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        if ($expectedReservationIds.Count -lt 4 -or
            @($expectedReservationIds | Select-Object -Unique).Count -ne 1 -or
            [string]$Inspection.id -notmatch '^[a-fA-F0-9]{32}$') {
            $reasons.Add('reservation-identity-contradictory-or-missing')
        }
        if ($Inspection.project -ne 'guideXOSServer' -or $Inspection.mode -ne 'acceptance' -or
            $Inspection.state -ne 'Running' -or [bool]$Inspection.degraded -or
            $Inspection.ownerLiveness -ne 'Verified') {
            $reasons.Add('reservation-not-current-verified-acceptance-session')
        }
        if (-not $owner.Success -or
            [int]$owner.Groups['pid'].Value -ne [int]$RunIdentity.ownerProcessId -or
            [int]$RunIdentity.ownerParentProcessId -ne [int]$RunIdentity.runnerProcessId -or
            -not [bool]$RunIdentity.runnerProcessAlive -or
            -not [bool]$RunIdentity.ownerProcessAlive -or
            -not (Test-C188R11R4QacTimestamp $RunIdentity.ownerProcessStartTimeUtc $RunIdentity.qacOwnerStartedAtUtc) -or
            -not (Test-C188R11R4QacTimestamp $RunIdentity.runnerProcessStartTimeUtc $RunIdentity.expectedRunnerProcessStartTimeUtc) -or
            -not [string]::Equals($owner.Groups['user'].Value, [string]$RunIdentity.ownerUser,
                [System.StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-C188R11R4QacTimestamp $Inspection.ownerProcessStartedAtUtc $RunIdentity.ownerProcessStartTimeUtc)) {
            $reasons.Add('reservation-owner-does-not-match-current-runner-process')
        }

        $inspectionChildPid = if ($null -ne $Inspection.processId) { [int]$Inspection.processId } else { 0 }
        $launchChildPid = if ($null -ne $RunIdentity.launchChildProcessId) { [int]$RunIdentity.launchChildProcessId } else { 0 }
        $jsonChildPid = if ($null -ne $RunIdentity.launchJsonChildProcessId) { [int]$RunIdentity.launchJsonChildProcessId } else { 0 }
        if ($inspectionChildPid -le 0 -or
            $inspectionChildPid -ne [int]$RunIdentity.childProcessId -or
            $inspectionChildPid -ne $launchChildPid -or
            $inspectionChildPid -ne $jsonChildPid -or
            [int]$RunIdentity.childParentProcessId -ne [int]$RunIdentity.ownerProcessId -or
            -not [bool]$RunIdentity.childProcessAlive -or
            -not (Test-C188R11R4QacTimestamp $Inspection.processStartTimeUtc $RunIdentity.childProcessStartTimeUtc) -or
            -not (Test-C188R11R4QacTimestamp $Inspection.processStartTimeUtc $RunIdentity.launchProcessStartTimeUtc) -or
            -not (Test-C188R11R4QacTimestamp $Inspection.processStartTimeUtc $RunIdentity.launchJsonProcessStartTimeUtc)) {
            $reasons.Add('reservation-child-process-identity-not-verified')
        }

        $expectedExecutable = [string]$RunIdentity.expectedQemuExecutableIdentity
        $pathValues = @(
            [string]$Inspection.processIdentity,
            [string]$RunIdentity.launchExecutableIdentity,
            [string]$RunIdentity.launchJsonExecutableIdentity,
            [string]$RunIdentity.childExecutableIdentity
        )
        if ([string]::IsNullOrWhiteSpace($expectedExecutable) -or
            @($pathValues | Where-Object {
                -not [string]::Equals($_, $expectedExecutable, [System.StringComparison]::OrdinalIgnoreCase)
            }).Count -ne 0) {
            $reasons.Add('qemu-executable-identity-contradictory-or-missing')
        }
        if ([string]$Inspection.ownerLiveness -ne 'Verified') {
            $reasons.Add('qac-owner-liveness-not-verified')
        }
        if ([string]$RunIdentity.runIdentityId -notmatch '^[a-fA-F0-9]{32}$' -or
            [string]::IsNullOrWhiteSpace([string]$RunIdentity.bootEvidencePath) -or
            [string]$RunIdentity.bootEvidencePath -ne [string]$RunIdentity.expectedBootEvidencePath) {
            $reasons.Add('current-run-identity-not-correlated')
        }
    }

    $reservationCountMatch = [regex]::Match($ListText, '(?m)^Reservations:\s*(?<count>\d+)\s*$')
    $sessionCountMatch = [regex]::Match($ListText, '(?m)^Managed sessions:\s*(?<count>\d+)\s*$')
    if (-not $reservationCountMatch.Success -or -not $sessionCountMatch.Success) {
        $reasons.Add('qac-list-census-unparseable')
    } elseif ($null -ne $Inspection -and $null -ne $RunIdentity) {
        $listedReservationIds = @([regex]::Matches($ListText, '(?m)^\s*(?<id>[a-fA-F0-9]{32})\s*\|') |
            ForEach-Object { $_.Groups['id'].Value })
        if ($listedReservationIds.Count -ne [int]$reservationCountMatch.Groups['count'].Value -or
            ($listedReservationIds.Count -gt 0 -and
             ($listedReservationIds.Count -ne 1 -or $listedReservationIds[0] -ne [string]$Inspection.id))) {
            $reasons.Add('qac-list-enumerates-a-different-reservation')
        }
        $listedSessions = @([regex]::Matches($ListText,
            '(?m)^Managed session:\s*(?<id>[a-fA-F0-9]{32})\s*\|\s*(?<state>[^|\r\n]+)\|') )
        if ($listedSessions.Count -ne [int]$sessionCountMatch.Groups['count'].Value -or
            ($listedSessions.Count -gt 0 -and
             ($listedSessions.Count -ne 1 -or
              $listedSessions[0].Groups['id'].Value -ne [string]$Inspection.id -or
              $listedSessions[0].Groups['state'].Value.Trim() -ne 'Running'))) {
            $reasons.Add('qac-list-enumerates-a-different-managed-session')
        }

        $processRows = @([regex]::Matches($ListText,
            '(?m)^QEMU PID (?<pid>\d+) \| (?<path>[^|]+) \| (?<start>[^|]+) \| (?<ownership>[^\r\n]+)\r?$'))
        $processLineCount = [regex]::Matches($ListText, '(?m)^QEMU PID\s+').Count
        if ($processLineCount -ne $processRows.Count) {
            $reasons.Add('qac-list-qemu-census-unparseable')
        } elseif ($processRows.Count -gt 0) {
            if ($processRows.Count -ne 1 -or
                [int]$processRows[0].Groups['pid'].Value -ne [int]$RunIdentity.childProcessId -or
                $processRows[0].Groups['ownership'].Value.Trim() -ne 'managed' -or
                -not [string]::Equals($processRows[0].Groups['path'].Value.Trim(),
                    [string]$RunIdentity.expectedQemuExecutableIdentity,
                    [System.StringComparison]::OrdinalIgnoreCase) -or
                $ListText -notmatch ('(?m)^\s*Note: Reservation ' + [regex]::Escape([string]$Inspection.id) + '\b')) {
                $reasons.Add('qac-list-qemu-census-contradicts-inspected-child')
            }
        }
        if ($ListText -match '(?m)^Coordinator warning:') {
            $reasons.Add('qac-list-reports-census-warning')
        }
    }

    return [ordered]@{
        accepted = ($reasons.Count -eq 0)
        reasons = @($reasons)
        runIdentityId = if ($null -ne $RunIdentity) { [string]$RunIdentity.runIdentityId } else { $null }
        reservationId = if ($null -ne $Inspection) { [string]$Inspection.id } else { $null }
        ownerProcessId = if ($null -ne $RunIdentity) { [int]$RunIdentity.ownerProcessId } else { $null }
        qemuChildProcessId = if ($null -ne $RunIdentity) { [int]$RunIdentity.childProcessId } else { $null }
        qacListReservationCount = if ($reservationCountMatch.Success) { [int]$reservationCountMatch.Groups['count'].Value } else { $null }
        qacListSessionCount = if ($sessionCountMatch.Success) { [int]$sessionCountMatch.Groups['count'].Value } else { $null }
        qacListQemuProcessCount = if ($null -ne $processRows) { $processRows.Count } else { 0 }
        qacListReservationOmitted = $reservationCountMatch.Success -and [int]$reservationCountMatch.Groups['count'].Value -eq 0
        qacListSessionOmitted = $sessionCountMatch.Success -and [int]$sessionCountMatch.Groups['count'].Value -eq 0
        qacListQemuProcessOmitted = $null -ne $processRows -and $processRows.Count -eq 0
        qacStatusListSessionCountDisagrees = $statusSessions.Success -and $sessionCountMatch.Success -and
            [int]$statusSessions.Groups['count'].Value -ne [int]$sessionCountMatch.Groups['count'].Value
        qacStatusListQemuCountDisagrees = $statusQemu.Success -and $null -ne $processRows -and
            [int]$statusQemu.Groups['count'].Value -ne $processRows.Count
        qacStatusSessionCount = if ($statusSessions.Success) { [int]$statusSessions.Groups['count'].Value } else { $null }
        qacStatusQemuProcessCount = if ($statusQemu.Success) { [int]$statusQemu.Groups['count'].Value } else { $null }
        qacStatusForeignCount = if ($statusQemu.Success) { [int]$statusQemu.Groups['foreign'].Value } else { $null }
    }
}
