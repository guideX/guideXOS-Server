Set-StrictMode -Version Latest

function Get-QacSingleMatch {
    param([string]$Text, [string]$Pattern)
    $matches = [regex]::Matches($Text, $Pattern)
    if ($matches.Count -ne 1) { return $null }
    return $matches[0]
}

function ConvertFrom-QacStatusSnapshot {
    [CmdletBinding()]
    param([string]$Text, [int]$ExitCode = 0)

    $reasons = [System.Collections.Generic.List[string]]::new()
    if ($ExitCode -ne 0) { $reasons.Add('status-command-failed') }
    if ([string]::IsNullOrWhiteSpace($Text)) { $reasons.Add('status-response-empty') }

    $coordinator = Get-QacSingleMatch $Text '(?m)^Coordinator:\s+Online\s*$'
    $qemu = Get-QacSingleMatch $Text '(?m)^QEMU processes:\s*(?<count>\d+)\s+\((?<foreign>\d+) foreign\)\s*$'
    $sessions = Get-QacSingleMatch $Text '(?m)^Managed sessions:\s*(?<count>\d+);\s*queued:\s*(?<queued>\d+);\s*blocked:\s*(?<blocked>\d+)\s*$'
    if ($null -eq $coordinator) { $reasons.Add('status-coordinator-not-online-or-malformed') }
    if ($null -eq $qemu) { $reasons.Add('status-qemu-census-missing-or-malformed') }
    if ($null -eq $sessions) { $reasons.Add('status-session-census-missing-or-malformed') }
    if ($Text -match '(?m)^Recovery:\s+Required\b') { $reasons.Add('status-recovery-required') }

    return [pscustomobject]@{
        Valid = ($reasons.Count -eq 0)
        Reasons = @($reasons)
        CoordinatorOnline = ($null -ne $coordinator)
        QemuProcessCount = if ($null -ne $qemu) { [int]$qemu.Groups['count'].Value } else { $null }
        ForeignQemuCount = if ($null -ne $qemu) { [int]$qemu.Groups['foreign'].Value } else { $null }
        ManagedSessionCount = if ($null -ne $sessions) { [int]$sessions.Groups['count'].Value } else { $null }
        QueuedCount = if ($null -ne $sessions) { [int]$sessions.Groups['queued'].Value } else { $null }
        BlockedCount = if ($null -ne $sessions) { [int]$sessions.Groups['blocked'].Value } else { $null }
    }
}

function ConvertFrom-QacListSnapshot {
    [CmdletBinding()]
    param([string]$Text, [int]$ExitCode = 0)

    $reasons = [System.Collections.Generic.List[string]]::new()
    if ($ExitCode -ne 0) { $reasons.Add('list-command-failed') }
    if ([string]::IsNullOrWhiteSpace($Text)) { $reasons.Add('list-response-empty') }
    if ($Text -notmatch '(?m)^Coordinator:\s+Online(?:\s+\([^\r\n]*\))?\s*$') {
        $reasons.Add('list-coordinator-not-online-or-malformed')
    }

    $reservationCountMatch = Get-QacSingleMatch $Text '(?m)^Reservations:\s*(?<count>\d+)\s*$'
    $sessionCountMatch = Get-QacSingleMatch $Text '(?m)^Managed sessions:\s*(?<count>\d+)\s*$'
    if ($null -eq $reservationCountMatch) { $reasons.Add('list-reservation-count-missing-or-malformed') }
    if ($null -eq $sessionCountMatch) { $reasons.Add('list-session-count-missing-or-malformed') }

    $lines = @($Text -split '\r?\n')
    $processes = [System.Collections.Generic.List[object]]::new()
    $processLineCount = 0
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch '^QEMU PID\s+') { continue }
        $processLineCount++
        $match = [regex]::Match($lines[$i], '^QEMU PID (?<pid>\d+) \| (?<path>[^|]+) \| (?<start>[^|]+) \| (?<ownership>[^\r\n]+)\r?$')
        if (-not $match.Success) {
            $reasons.Add('list-qemu-process-row-malformed')
            continue
        }
        $startTime = [DateTimeOffset]::MinValue
        if (-not [DateTimeOffset]::TryParse($match.Groups['start'].Value.Trim(), [ref]$startTime)) {
            $reasons.Add('list-qemu-process-start-time-missing-or-malformed')
            continue
        }
        $note = ''
        if ($i + 1 -lt $lines.Count -and $lines[$i + 1] -match '^\s+Note:\s*(?<note>.*)$') {
            $note = $Matches['note']
        }
        $processes.Add([pscustomobject]@{
            ProcessId = [int]$match.Groups['pid'].Value
            ExecutableIdentity = $match.Groups['path'].Value.Trim()
            StartTimeUtc = $startTime.ToUniversalTime()
            Ownership = $match.Groups['ownership'].Value.Trim()
            Note = $note
        })
    }

    $reservationRowMatches = [regex]::Matches($Text,
        '(?m)^\s*(?<id>[a-fA-F0-9]{32})\s+\|\s*(?<project>[^|]+?)\s*\|\s*(?<mode>[^|]+?)\s*\|\s*(?<state>[^|]+?)\s*\|\s*queue\s+(?<queue>\d+)\s*\|\s*(?<owner>.+?)\s*\|\s*owner\s+(?<liveness>[^|\r\n]+?)(?:\s*\|\s*child PID\s+(?<childPid>\d+))?\s*$')
    $reservationCandidateLines = [regex]::Matches($Text, '(?m)^\s*[a-fA-F0-9]{32}\s+\|').Count
    $reservations = [System.Collections.Generic.List[object]]::new()
    foreach ($match in $reservationRowMatches) {
        $childPid = $null
        if ($match.Groups['childPid'].Success) { $childPid = [int]$match.Groups['childPid'].Value }
        $reservations.Add([pscustomobject]@{
            Id = $match.Groups['id'].Value
            Project = $match.Groups['project'].Value.Trim()
            Mode = $match.Groups['mode'].Value.Trim()
            State = $match.Groups['state'].Value.Trim()
            QueuePosition = [int]$match.Groups['queue'].Value
            Owner = $match.Groups['owner'].Value.Trim()
            OwnerLiveness = $match.Groups['liveness'].Value.Trim()
            ChildProcessId = $childPid
        })
    }
    if ($reservationCandidateLines -ne $reservationRowMatches.Count) { $reasons.Add('list-reservation-row-malformed') }
    if ($null -ne $reservationCountMatch -and [int]$reservationCountMatch.Groups['count'].Value -ne $reservationRowMatches.Count) {
        $reasons.Add('list-reservation-count-does-not-match-rows')
    }

    $managedSessionRowMatches = [regex]::Matches($Text,
        '(?m)^Managed session:\s*(?<id>[a-fA-F0-9]{32})\s*\|\s*(?<state>[^|\r\n]+?)\s*\|\s*(?<owner>[^\r\n]+)\r?$')
    $managedSessionCandidateLines = [regex]::Matches($Text, '(?m)^Managed session:').Count
    $managedSessions = [System.Collections.Generic.List[object]]::new()
    foreach ($match in $managedSessionRowMatches) {
        $managedSessions.Add([pscustomobject]@{
            Id = $match.Groups['id'].Value
            State = $match.Groups['state'].Value.Trim()
            Owner = $match.Groups['owner'].Value.Trim()
        })
    }
    if ($managedSessionCandidateLines -ne $managedSessionRowMatches.Count) { $reasons.Add('list-session-row-malformed') }
    if ($null -ne $sessionCountMatch -and [int]$sessionCountMatch.Groups['count'].Value -ne $managedSessionRowMatches.Count) {
        $reasons.Add('list-session-count-does-not-match-rows')
    }

    $warnings = @([regex]::Matches($Text, '(?m)^Coordinator warning:') | ForEach-Object { $_.Value })
    return [pscustomobject]@{
        Valid = ($reasons.Count -eq 0)
        Reasons = @($reasons)
        CoordinatorOnline = ($Text -match '(?m)^Coordinator:\s+Online(?:\s+\([^\r\n]*\))?\s*$')
        ReservationCount = if ($null -ne $reservationCountMatch) { [int]$reservationCountMatch.Groups['count'].Value } else { $null }
        ReservationRows = @($reservations)
        ManagedSessionCount = if ($null -ne $sessionCountMatch) { [int]$sessionCountMatch.Groups['count'].Value } else { $null }
        ManagedSessionRows = @($managedSessions)
        QemuProcessCount = $processLineCount
        QemuProcesses = @($processes)
        CensusWarnings = $warnings
    }
}

function Get-QacReservationProperty {
    param([object]$Reservation, [string]$Name)
    if ($null -eq $Reservation) { return $null }
    if ($Reservation -is [System.Collections.IDictionary]) {
        if ($Reservation.Contains($Name)) { return $Reservation[$Name] }
        return $null
    }
    $property = $Reservation.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Test-QacTimestampNear {
    param([object]$Left, [object]$Right, [double]$ToleranceSeconds = 2.0)
    if ($null -eq $Left -or $null -eq $Right) { return $false }
    try {
        $leftTime = if ($Left -is [DateTimeOffset]) { $Left } elseif ($Left -is [DateTime]) { [DateTimeOffset]$Left } else { [DateTimeOffset]::Parse([string]$Left) }
        $rightTime = if ($Right -is [DateTimeOffset]) { $Right } elseif ($Right -is [DateTime]) { [DateTimeOffset]$Right } else { [DateTimeOffset]::Parse([string]$Right) }
        return [Math]::Abs(($leftTime.ToUniversalTime() - $rightTime.ToUniversalTime()).TotalSeconds) -le $ToleranceSeconds
    } catch { return $false }
}

function Test-QacCandidatePath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$CandidateRoot,
        [Parameter(Mandatory=$true)][string]$PhaseRoot,
        [switch]$RequireExisting
    )
    try {
        $candidateFull = [System.IO.Path]::GetFullPath($CandidateRoot)
        $phaseFull = [System.IO.Path]::GetFullPath($PhaseRoot)
        $relative = [System.IO.Path]::GetRelativePath($phaseFull,$candidateFull)
        if ($relative -eq '.' -or [System.IO.Path]::IsPathRooted($relative) -or
            $relative -eq '..' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar,
                [System.StringComparison]::OrdinalIgnoreCase) -or
            $relative.StartsWith('..' + [System.IO.Path]::AltDirectorySeparatorChar,
                [System.StringComparison]::OrdinalIgnoreCase)) { return $false }
        if (-not $RequireExisting) { return $true }
        if (-not (Test-Path -LiteralPath $phaseFull -PathType Container) -or
            -not (Test-Path -LiteralPath $candidateFull -PathType Container)) { return $false }
        $cursor = Get-Item -LiteralPath $candidateFull -Force
        while ($null -ne $cursor) {
            if (($cursor.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { return $false }
            if ([string]::Equals([System.IO.Path]::GetFullPath($cursor.FullName),$phaseFull,
                    [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
            $cursor = $cursor.Parent
        }
        return $false
    } catch { return $false }
}

function Test-QacClear {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][object]$Snapshot)
    $status = ConvertFrom-QacStatusSnapshot -Text ([string]$Snapshot.Status) -ExitCode ([int]$Snapshot.StatusExit)
    $list = ConvertFrom-QacListSnapshot -Text ([string]$Snapshot.List) -ExitCode ([int]$Snapshot.ListExit)
    return $status.Valid -and $list.Valid -and
        $status.QemuProcessCount -eq 0 -and $status.ForeignQemuCount -eq 0 -and
        $status.ManagedSessionCount -eq 0 -and $status.QueuedCount -eq 0 -and $status.BlockedCount -eq 0 -and
        $status.QemuProcessCount -eq $list.QemuProcessCount -and
        $status.ManagedSessionCount -eq $list.ManagedSessionCount -and
        $list.ReservationCount -eq 0 -and $list.ManagedSessionCount -eq 0 -and
        $list.QemuProcessCount -eq 0 -and $list.CensusWarnings.Count -eq 0
}

function Test-QacClearWithCensus {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][object]$Snapshot,
        [Parameter(Mandatory=$true)][bool]$WindowsCensusComplete,
        [Parameter(Mandatory=$true)][AllowEmptyCollection()][object[]]$LocalQemuProcesses,
        [Parameter(Mandatory=$true)][bool]$InspectionsComplete,
        [Parameter(Mandatory=$true)][int]$InspectionCount
    )
    if (-not (Test-QacClear $Snapshot) -or -not $WindowsCensusComplete -or
        @($LocalQemuProcesses).Count -ne 0 -or -not $InspectionsComplete) { return $false }
    $list = ConvertFrom-QacListSnapshot -Text ([string]$Snapshot.List) -ExitCode ([int]$Snapshot.ListExit)
    return $list.Valid -and $InspectionCount -eq $list.ReservationCount
}

function Test-QacRunningOwned {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][object]$Snapshot,
        [Parameter(Mandatory=$true)][string]$ReservationId,
        [Parameter(Mandatory=$true)][int]$OwnerPid,
        [Parameter(Mandatory=$true)][string]$OwnerUser,
        [Parameter(Mandatory=$true)][string]$QemuPath,
        [Parameter(Mandatory=$true)][object]$Inspection,
        [Parameter(Mandatory=$true)][object]$ExpectedOwnerStartTimeUtc
    )

    $status = ConvertFrom-QacStatusSnapshot -Text ([string]$Snapshot.Status) -ExitCode ([int]$Snapshot.StatusExit)
    $list = ConvertFrom-QacListSnapshot -Text ([string]$Snapshot.List) -ExitCode ([int]$Snapshot.ListExit)
    if (-not $status.Valid -or -not $list.Valid -or $list.CensusWarnings.Count -ne 0) { return $false }
    if ($status.QemuProcessCount -ne 1 -or $status.ForeignQemuCount -ne 0 -or
        $status.ManagedSessionCount -ne 1 -or $status.QueuedCount -ne 0 -or $status.BlockedCount -ne 0) { return $false }
    if ($status.QemuProcessCount -ne $list.QemuProcessCount -or
        $status.ManagedSessionCount -ne $list.ManagedSessionCount -or
        $list.ReservationCount -ne 1 -or $list.ManagedSessionCount -ne 1 -or $list.QemuProcessCount -ne 1) { return $false }

    $reservation = @($list.ReservationRows | Where-Object { $_.Id -ceq $ReservationId })
    $session = @($list.ManagedSessionRows | Where-Object { $_.Id -ceq $ReservationId })
    if ($reservation.Count -ne 1 -or $session.Count -ne 1 -or $list.QemuProcesses.Count -ne 1) { return $false }
    $reservation = $reservation[0]
    $session = $session[0]
    $process = $list.QemuProcesses[0]

    $ownerMatch = [regex]::Match($reservation.Owner, '^(?<user>.+?) \(PID (?<pid>\d+)\)$')
    if (-not $ownerMatch.Success -or [int]$ownerMatch.Groups['pid'].Value -ne $OwnerPid -or
        -not [string]::Equals($ownerMatch.Groups['user'].Value, $OwnerUser, [System.StringComparison]::OrdinalIgnoreCase)) { return $false }
    if ($reservation.Project -cne 'guideXOSServer' -or $reservation.Mode -cne 'acceptance' -or
        $reservation.State -cne 'Running' -or $reservation.QueuePosition -ne 0 -or
        $reservation.OwnerLiveness -cne 'Verified' -or $null -eq $reservation.ChildProcessId) { return $false }
    if ($session.State -cne 'Running' -or $process.ProcessId -ne $reservation.ChildProcessId -or
        $process.Ownership -cne 'managed' -or
        -not [string]::Equals($process.ExecutableIdentity, $QemuPath, [System.StringComparison]::OrdinalIgnoreCase) -or
        $process.Note -notmatch ('^Reservation\s+' + [regex]::Escape($ReservationId) + '\b')) { return $false }

    $inspectionId = Get-QacReservationProperty $Inspection 'id'
    $inspectionProject = Get-QacReservationProperty $Inspection 'project'
    $inspectionMode = Get-QacReservationProperty $Inspection 'mode'
    $inspectionState = Get-QacReservationProperty $Inspection 'state'
    $inspectionOwner = Get-QacReservationProperty $Inspection 'owner'
    $inspectionOwnerLiveness = Get-QacReservationProperty $Inspection 'ownerLiveness'
    $inspectionOwnerStart = Get-QacReservationProperty $Inspection 'ownerProcessStartedAtUtc'
    $inspectionQueuePosition = Get-QacReservationProperty $Inspection 'queuePosition'
    $inspectionChildPid = Get-QacReservationProperty $Inspection 'processId'
    $inspectionChildStart = Get-QacReservationProperty $Inspection 'processStartTimeUtc'
    $inspectionChildPath = Get-QacReservationProperty $Inspection 'processIdentity'
    $inspectionDegraded = Get-QacReservationProperty $Inspection 'degraded'
    if ([string]$inspectionId -cne $ReservationId -or [string]$inspectionProject -cne 'guideXOSServer' -or
        [string]$inspectionMode -cne 'acceptance' -or [string]$inspectionState -cne 'Running' -or
        [int]$inspectionQueuePosition -ne 0 -or
        [string]$inspectionOwnerLiveness -cne 'Verified' -or [bool]$inspectionDegraded -or
        [string]$inspectionOwner -cne $reservation.Owner -or
        [int]$inspectionChildPid -ne $process.ProcessId -or
        -not [string]::Equals([string]$inspectionChildPath, $QemuPath, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-QacTimestampNear $inspectionOwnerStart $ExpectedOwnerStartTimeUtc) -or
        -not (Test-QacTimestampNear $inspectionChildStart $process.StartTimeUtc)) { return $false }
    return $true
}

function Test-QacLocalQemuChildIdentity {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][object]$LocalProcess,
        [Parameter(Mandatory=$true)][object]$Inspection,
        [Parameter(Mandatory=$true)][int]$OwnerPid,
        [Parameter(Mandatory=$true)][string]$QemuPath,
        [Parameter(Mandatory=$true)][int]$QmpPort
    )
    $localPid = Get-QacReservationProperty $LocalProcess 'ProcessId'
    $parentPid = Get-QacReservationProperty $LocalProcess 'ParentProcessId'
    $localPath = Get-QacReservationProperty $LocalProcess 'ExecutablePath'
    $commandLine = Get-QacReservationProperty $LocalProcess 'CommandLine'
    $creationTime = Get-QacReservationProperty $LocalProcess 'CreationDate'
    $inspectionPid = Get-QacReservationProperty $Inspection 'processId'
    $inspectionStart = Get-QacReservationProperty $Inspection 'processStartTimeUtc'
    $inspectionPath = Get-QacReservationProperty $Inspection 'processIdentity'
    if ($null -eq $localPid -or $null -eq $parentPid -or $null -eq $localPath -or
        $null -eq $commandLine -or $null -eq $creationTime -or $null -eq $inspectionPid -or
        $null -eq $inspectionStart -or $null -eq $inspectionPath) { return $false }
    $localPath = [string]$localPath
    $commandLine = [string]$commandLine
    if ([int]$localPid -ne [int]$inspectionPid -or [int]$parentPid -ne $OwnerPid -or
        -not [string]::Equals($localPath, $QemuPath, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals([string]$inspectionPath, $QemuPath, [System.StringComparison]::OrdinalIgnoreCase) -or
        $commandLine -notmatch ('127\.0\.0\.1:' + [int]$QmpPort + ',server,nowait')) { return $false }
    if (-not (Test-QacTimestampNear $creationTime $inspectionStart)) { return $false }
    return $true
}

function ConvertFrom-QmpQueryStatusResponse {
    [CmdletBinding()]
    param([string]$Text)
    $reasons = [System.Collections.Generic.List[string]]::new()
    $message = $null
    try { $message = $Text | ConvertFrom-Json -ErrorAction Stop }
    catch { $reasons.Add('qmp-response-malformed-json') }
    if ($null -ne $message) {
        $errorField = $message.PSObject.Properties['error']
        $returnField = $message.PSObject.Properties['return']
        if ($null -ne $errorField -and $null -ne $errorField.Value) { $reasons.Add('qmp-response-contains-error') }
        if ($null -eq $returnField -or $null -eq $returnField.Value) { $reasons.Add('qmp-return-missing') }
        else {
            $statusField = $returnField.Value.PSObject.Properties['status']
            $runningField = $returnField.Value.PSObject.Properties['running']
            if ($null -eq $statusField -or [string]::IsNullOrWhiteSpace([string]$statusField.Value)) {
                $reasons.Add('qmp-status-missing')
            }
            if ($null -eq $runningField -or $runningField.Value -isnot [bool]) {
                $reasons.Add('qmp-running-boolean-missing-or-malformed')
            }
        }
    }
    $returnValue = if ($null -ne $message -and $null -ne $message.PSObject.Properties['return']) {
        $message.PSObject.Properties['return'].Value
    } else { $null }
    return [pscustomobject]@{
        Valid = ($reasons.Count -eq 0)
        Reasons = @($reasons)
        Status = if ($null -ne $returnValue -and $null -ne $returnValue.PSObject.Properties['status']) { [string]$returnValue.status } else { $null }
        Running = if ($null -ne $returnValue -and $null -ne $returnValue.PSObject.Properties['running'] -and
                $returnValue.running -is [bool]) { [bool]$returnValue.running } else { $null }
    }
}

Export-ModuleMember -Function ConvertFrom-QacStatusSnapshot, ConvertFrom-QacListSnapshot, Get-QacReservationProperty, Test-QacTimestampNear, Test-QacCandidatePath, Test-QacClear, Test-QacClearWithCensus, Test-QacRunningOwned, Test-QacLocalQemuChildIdentity, ConvertFrom-QmpQueryStatusResponse
