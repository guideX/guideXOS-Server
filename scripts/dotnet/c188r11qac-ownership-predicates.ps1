function Test-C188R11R4QacTimestamp([object]$Left, [object]$Right, [double]$ToleranceSeconds = 2.0) {
    if ($null -eq $Left -or $null -eq $Right) { return $false }
    try {
        # ConvertFrom-Json parses ISO timestamps into DateTime values by default.
        # Casting those values to string drops Kind/offset and can shift a UTC
        # timestamp by the machine's local offset when it is parsed again.
        $leftTime = if ($Left -is [DateTimeOffset]) { $Left } elseif ($Left -is [DateTime]) { [DateTimeOffset]$Left } else { [DateTimeOffset]::Parse([string]$Left) }
        $rightTime = if ($Right -is [DateTimeOffset]) { $Right } elseif ($Right -is [DateTime]) { [DateTimeOffset]$Right } else { [DateTimeOffset]::Parse([string]$Right) }
        $leftTime = $leftTime.ToUniversalTime()
        $rightTime = $rightTime.ToUniversalTime()
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
    $ownerChecks = [ordered]@{
        ownerRecordValid=$false;ownerPidMatchesRunIdentity=$false
        ownerParentMatchesRunner=$false;ownerProcessAlive=$false;runnerProcessAlive=$false
        ownerStartMatchesQac=$false;runnerStartMatchesCurrent=$false
        ownerUserMatches=$false;inspectionOwnerStartMatches=$false
    }
    $childChecks = [ordered]@{
        childPidMatchesAllLaunchEvidence=$false;childParentMatchesOwner=$false
        childProcessAlive=$false;childStartMatchesLocal=$false
        childStartMatchesLaunch=$false;childStartMatchesLaunchJson=$false
    }
    $localProcessEvidence = $null
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
    if ($null -ne $RunIdentity) {
        $localProcessCaptureError = $null
        if ($RunIdentity -is [System.Collections.IDictionary]) {
            if ($RunIdentity.Contains('localProcessCaptureError')) {
                $localProcessCaptureError = $RunIdentity['localProcessCaptureError']
            }
        } else {
            $captureErrorProperty = $RunIdentity.PSObject.Properties['localProcessCaptureError']
            if ($null -ne $captureErrorProperty) { $localProcessCaptureError = $captureErrorProperty.Value }
        }
        $localProcessEvidence = [ordered]@{
            ownerProcessId=$RunIdentity.ownerProcessId
            ownerProcessAlive=$RunIdentity.ownerProcessAlive
            ownerProcessStartTimeUtc=$RunIdentity.ownerProcessStartTimeUtc
            ownerParentProcessId=$RunIdentity.ownerParentProcessId
            ownerUser=$RunIdentity.ownerUser
            runnerProcessId=$RunIdentity.runnerProcessId
            runnerProcessAlive=$RunIdentity.runnerProcessAlive
            runnerProcessStartTimeUtc=$RunIdentity.runnerProcessStartTimeUtc
            expectedRunnerProcessStartTimeUtc=$RunIdentity.expectedRunnerProcessStartTimeUtc
            childProcessId=$RunIdentity.childProcessId
            childProcessAlive=$RunIdentity.childProcessAlive
            childProcessStartTimeUtc=$RunIdentity.childProcessStartTimeUtc
            childExecutableIdentity=$RunIdentity.childExecutableIdentity
            childParentProcessId=$RunIdentity.childParentProcessId
            localProcessCaptureError=$localProcessCaptureError
        }
    }

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
        $ownerChecks.ownerRecordValid = $owner.Success
        $ownerChecks.ownerPidMatchesRunIdentity = $owner.Success -and
            [int]$owner.Groups['pid'].Value -eq [int]$RunIdentity.ownerProcessId
        $ownerChecks.ownerParentMatchesRunner =
            [int]$RunIdentity.ownerParentProcessId -eq [int]$RunIdentity.runnerProcessId
        $ownerChecks.ownerProcessAlive = [bool]$RunIdentity.ownerProcessAlive
        $ownerChecks.runnerProcessAlive = [bool]$RunIdentity.runnerProcessAlive
        $ownerChecks.ownerStartMatchesQac = Test-C188R11R4QacTimestamp `
            $RunIdentity.ownerProcessStartTimeUtc $RunIdentity.qacOwnerStartedAtUtc
        $ownerChecks.runnerStartMatchesCurrent = Test-C188R11R4QacTimestamp `
            $RunIdentity.runnerProcessStartTimeUtc $RunIdentity.expectedRunnerProcessStartTimeUtc
        $ownerChecks.ownerUserMatches = $owner.Success -and
            [string]::Equals($owner.Groups['user'].Value, [string]$RunIdentity.ownerUser,
                [System.StringComparison]::OrdinalIgnoreCase)
        $ownerChecks.inspectionOwnerStartMatches = Test-C188R11R4QacTimestamp `
            $Inspection.ownerProcessStartedAtUtc $RunIdentity.ownerProcessStartTimeUtc
        $ownerFailures = @($ownerChecks.GetEnumerator() | Where-Object { -not [bool]$_.Value })
        if ($ownerFailures.Count -gt 0) {
            $reasons.Add('reservation-owner-does-not-match-current-runner-process')
        }

        $inspectionChildPid = if ($null -ne $Inspection.processId) { [int]$Inspection.processId } else { 0 }
        $launchChildPid = if ($null -ne $RunIdentity.launchChildProcessId) { [int]$RunIdentity.launchChildProcessId } else { 0 }
        $jsonChildPid = if ($null -ne $RunIdentity.launchJsonChildProcessId) { [int]$RunIdentity.launchJsonChildProcessId } else { 0 }
        $childChecks.childPidMatchesAllLaunchEvidence = $inspectionChildPid -gt 0 -and
            $inspectionChildPid -eq [int]$RunIdentity.childProcessId -and
            $inspectionChildPid -eq $launchChildPid -and $inspectionChildPid -eq $jsonChildPid
        $childChecks.childParentMatchesOwner =
            [int]$RunIdentity.childParentProcessId -eq [int]$RunIdentity.ownerProcessId
        $childChecks.childProcessAlive = [bool]$RunIdentity.childProcessAlive
        $childChecks.childStartMatchesLocal = Test-C188R11R4QacTimestamp `
            $Inspection.processStartTimeUtc $RunIdentity.childProcessStartTimeUtc
        $childChecks.childStartMatchesLaunch = Test-C188R11R4QacTimestamp `
            $Inspection.processStartTimeUtc $RunIdentity.launchProcessStartTimeUtc
        $childChecks.childStartMatchesLaunchJson = Test-C188R11R4QacTimestamp `
            $Inspection.processStartTimeUtc $RunIdentity.launchJsonProcessStartTimeUtc
        $childFailures = @($childChecks.GetEnumerator() | Where-Object { -not [bool]$_.Value })
        if ($childFailures.Count -gt 0) {
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
        ownerChecks = $ownerChecks
        childChecks = $childChecks
        localProcessEvidence = $localProcessEvidence
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
