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

function Assert-QacTransitionCondition {
    param([bool]$Condition,[string]$Message)
    if (-not $Condition) { throw $Message }
}

function Get-QacTransitionTreeInventory {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Root)

    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,[System.IO.Path]::AltDirectorySeparatorChar)
    $rootItem = Get-Item -LiteralPath $rootFull -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer -or
        ($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Candidate root is missing or is a reparse point: $rootFull"
    }
    $files = [System.Collections.Generic.List[object]]::new()
    foreach ($item in @(Get-ChildItem -LiteralPath $rootFull -Force -Recurse -ErrorAction Stop)) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Candidate tree contains a reparse point: $($item.FullName)"
        }
        if ($item.PSIsContainer) { continue }
        $relative = [System.IO.Path]::GetRelativePath($rootFull,$item.FullName).Replace('\','/')
        $files.Add([pscustomobject]@{Path=$relative;FullName=$item.FullName;Length=[int64]$item.Length})
    }
    return @($files.ToArray())
}

function Test-QacCandidateIntegrityInventory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$CandidateRoot,
        [Parameter(Mandatory=$true)][string]$InventoryPath
    )

    $candidateFull = [System.IO.Path]::GetFullPath($CandidateRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,[System.IO.Path]::AltDirectorySeparatorChar)
    $inventoryFull = [System.IO.Path]::GetFullPath($InventoryPath)
    $requiredInventory = [System.IO.Path]::GetFullPath((Join-Path $candidateFull 'candidate-integrity-inventory.json'))
    if (-not [string]::Equals($inventoryFull,$requiredInventory,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Transition admission accepts only the integrity inventory stored at the candidate root.'
    }
    if (-not (Test-Path -LiteralPath $inventoryFull -PathType Leaf)) {
        throw "Candidate integrity inventory is missing: $inventoryFull"
    }
    $inventoryItem = Get-Item -LiteralPath $inventoryFull -Force -ErrorAction Stop
    if (($inventoryItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Candidate integrity inventory cannot be a reparse point.'
    }
    $manifestPath = Join-Path $candidateFull 'candidate-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Candidate manifest is missing: $manifestPath"
    }
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    $inventory = Get-Content -LiteralPath $inventoryFull -Raw | ConvertFrom-Json -ErrorAction Stop
    Assert-QacTransitionCondition ([string]$inventory.schema -ceq 'C188R12R4R5R1_CANDIDATE_INTEGRITY_INVENTORY_V1') `
        'Candidate integrity inventory schema is missing or unsupported.'
    Assert-QacTransitionCondition ([string]$inventory.candidateRoot -ceq $candidateFull) `
        'Candidate integrity inventory is bound to a different candidate root.'
    Assert-QacTransitionCondition ([string]$inventory.candidateManifest -ceq 'candidate-manifest.json') `
        'Candidate integrity inventory does not identify candidate-manifest.json.'
    Assert-QacTransitionCondition ([string]$inventory.candidateManifestSha256 -ceq $manifestHash) `
        'Candidate manifest hash does not match the integrity inventory.'

    $entries = @($inventory.files)
    Assert-QacTransitionCondition ($entries.Count -gt 0 -and [int]$inventory.fileCount -eq $entries.Count) `
        'Candidate integrity inventory file count is missing or inconsistent.'
    $entryByPath = [System.Collections.Generic.Dictionary[string,object]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $rootPrefix = $candidateFull + [System.IO.Path]::DirectorySeparatorChar
    foreach ($entry in $entries) {
        $relative = [string]$entry.path
        Assert-QacTransitionCondition (-not [string]::IsNullOrWhiteSpace($relative) -and
            -not [System.IO.Path]::IsPathRooted($relative) -and $relative -notmatch '(^|[\\/])\.\.([\\/]|$)') `
            "Candidate inventory contains an unsafe relative path: $relative"
        $relativeNative = $relative.Replace('/',[System.IO.Path]::DirectorySeparatorChar).Replace('\',[System.IO.Path]::DirectorySeparatorChar)
        $full = [System.IO.Path]::GetFullPath((Join-Path $candidateFull $relativeNative))
        Assert-QacTransitionCondition ($full.StartsWith($rootPrefix,[System.StringComparison]::OrdinalIgnoreCase)) `
            "Candidate inventory path escapes the candidate root: $relative"
        Assert-QacTransitionCondition (-not $entryByPath.ContainsKey($relative)) `
            "Candidate inventory contains a duplicate path: $relative"
        $entryByPath.Add($relative,$entry)
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
            throw "Candidate inventory file is missing: $relative"
        }
        $item = Get-Item -LiteralPath $full -Force -ErrorAction Stop
        Assert-QacTransitionCondition (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) `
            "Candidate inventory file is a reparse point: $relative"
        Assert-QacTransitionCondition ([int64]$item.Length -eq [int64]$entry.sizeBytes) `
            "Candidate inventory size mismatch: $relative"
        $actualHash = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToUpperInvariant()
        Assert-QacTransitionCondition ($actualHash -ceq ([string]$entry.sha256).ToUpperInvariant()) `
            "Candidate integrity mismatch: $relative"
    }

    $actualFiles = @(Get-QacTransitionTreeInventory -Root $candidateFull | Where-Object {
        -not [string]::Equals($_.Path,'candidate-integrity-inventory.json',[System.StringComparison]::OrdinalIgnoreCase)
    })
    Assert-QacTransitionCondition ($actualFiles.Count -eq $entries.Count) `
        'Candidate contains unlisted payload files or its integrity inventory is incomplete.'
    foreach ($actual in $actualFiles) {
        Assert-QacTransitionCondition ($entryByPath.ContainsKey([string]$actual.Path)) `
            "Candidate contains an unlisted payload file: $($actual.Path)"
    }
    Assert-QacTransitionCondition ($entryByPath.ContainsKey('candidate-manifest.json')) `
        'Candidate inventory does not cover candidate-manifest.json.'
    return [pscustomobject]@{
        Result='PASS';CandidateRoot=$candidateFull;InventoryPath=$inventoryFull
        CandidateManifestSha256=$manifestHash
        InventorySha256=(Get-FileHash -LiteralPath $inventoryFull -Algorithm SHA256).Hash.ToUpperInvariant()
        FileCount=$entries.Count;UnlistedPayloadFileCount=0;MismatchCount=0
    }
}

function Test-QacTransitionGuestArtifacts {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$Esp,
        [Parameter(Mandatory=$true)][string]$KernelSha256,
        [Parameter(Mandatory=$true)][string]$RamdiskSha256,
        [Parameter(Mandatory=$true)][string]$BootloaderSha256
    )
    $required = @(
        @((Join-Path $Esp 'kernel.elf'),$KernelSha256,'staged kernel'),
        @((Join-Path $Esp 'ramdisk.img'),$RamdiskSha256,'staged ramdisk'),
        @((Join-Path $Esp 'EFI\BOOT\BOOTX64.EFI'),$BootloaderSha256,'staged EFI bootloader')
    )
    foreach ($item in $required) {
        if (-not (Test-Path -LiteralPath $item[0] -PathType Leaf)) {
            throw "Required transition guest artifact is missing: $($item[2]) ($($item[0]))"
        }
        $actual = (Get-FileHash -LiteralPath $item[0] -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actual -cne ([string]$item[1]).ToUpperInvariant()) {
            throw "Required transition guest artifact hash mismatch: $($item[2])"
        }
    }
    return [pscustomobject]@{Result='PASS';RequiredArtifactCount=$required.Count}
}

function Test-QacTransitionCandidateProfile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$CandidateRoot,
        [Parameter(Mandatory=$true)][string]$PhaseRoot,
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [string]$InventoryPath = ''
    )

    $candidateFull = [System.IO.Path]::GetFullPath($CandidateRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,[System.IO.Path]::AltDirectorySeparatorChar)
    $phaseFull = [System.IO.Path]::GetFullPath($PhaseRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,[System.IO.Path]::AltDirectorySeparatorChar)
    $repoFull = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,[System.IO.Path]::AltDirectorySeparatorChar)
    if (-not (Test-QacCandidatePath -CandidateRoot $candidateFull -PhaseRoot $phaseFull -RequireExisting)) {
        throw "CandidateRoot must be a unique, canonical candidate below $phaseFull."
    }
    $manifestPath = Join-Path $candidateFull 'candidate-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Qualified transition candidate manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    $isManagedGcRootProbe = [string]$manifest.mode -ceq 'NativeAotManagedGcRootProbe'
    $isManagedSettingsObjectProbe = [string]$manifest.mode -ceq 'NativeAotManagedSettingsObjectProbe'
    $isManagedEntryProbe = [string]$manifest.mode -ceq 'NativeAotFirstManagedEntryProbe'
    $managedEntryDispatchProfile = $isManagedEntryProbe -or $isManagedSettingsObjectProbe -or $isManagedGcRootProbe
    $expectedIdentity = if ($isManagedSettingsObjectProbe -or $isManagedGcRootProbe) {
        'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r3-managed-settings-object-v1'
    } elseif ($isManagedEntryProbe) {
        'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r3-managed-entry-v1'
    } else { 'guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r3-transition-attribution-v1' }
    $expectedResult = if ($isManagedGcRootProbe) { 'MANAGED_GC_ROOT_PROBE_HOSTED_READY' } elseif ($isManagedSettingsObjectProbe) { 'MANAGED_SETTINGS_OBJECT_PROBE_HOSTED_READY' } elseif ($isManagedEntryProbe) { 'MANAGED_ENTRY_PROBE_HOSTED_READY' } else { 'TRANSITION_ATTRIBUTION_HOSTED_READY' }
    $expectedNextPhase = if ($isManagedGcRootProbe) { 'C188R13_One_Managed_GC_Collection_and_Live_Root_Guest' } elseif ($isManagedSettingsObjectProbe) { 'C188R12R4R5R4R5R2R2R4R2R3R12_Managed_Settings_Object_Guest' } elseif ($isManagedEntryProbe) { 'C188R12R4R5R2R3_First_Managed_Entry_Guest' } else { 'C188R12R4R5R4R5R2R2R4R2R3R1_InitializeModules_Transition_Attribution_Guest' }
    if ([string]$manifest.mode -cnotin @('NativeAotInitializeModulesTransitionAttribution','NativeAotFirstManagedEntryProbe','NativeAotManagedSettingsObjectProbe','NativeAotManagedGcRootProbe') -or
        [string]$manifest.result -cne $expectedResult -or
        [string]$manifest.candidateRoot -cne $candidateFull -or
        [string]$manifest.runtimePackIdentity -cne $expectedIdentity -or
        [string]$manifest.runtimePackSourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
        [bool]$manifest.qemuLaunched -or [bool]$manifest.qacReserved -or
        -not [bool]$manifest.c188R12R4R5R2R2R4SafeHandoff -or
        [string]$manifest.c188R12R4R5R2R2R4SafeHandoffMarker -cne 'C188R12R4R5R2R2R4_SAFE_HANDOFF' -or
        [string]$manifest.c188R12R4R5R2R2R4SafeHandoffTransition -cne 'disabled before RhpReversePInvoke and InitializeModules' -or
        -not [bool]$manifest.c188R12R4R5R2R3InitializeModulesAttribution -or
        [string]$manifest.c188R12R4R5R2R3InitializeModulesAttributionMarker -cne 'C188R12R4R5R2R3_INITIALIZEMODULES_ATTRIBUTION' -or
        [string]$manifest.nextGuestPhase -cne $expectedNextPhase -or
        [bool]$manifest.managedMainEnteredByCandidate -or [int]$manifest.managedGraphCallbacks -ne 0 -or
        -not [bool]$manifest.productionNativeAotPersistentCompositeLifecycle -or
        -not [bool]$manifest.productionIsolationChecks.diagnosticAndProductionOutputsSeparate -or
        -not [bool]$manifest.productionIsolationChecks.diagnosticInstrumentationAbsentFromProduction -or
        -not [bool]$manifest.productionIsolationChecks.productionPersistentCompositeLifecycle -or
        [bool]$manifest.productionIsolationChecks.productionRuntimePackSafeHandoffProfile -or
        [bool]$manifest.productionIsolationChecks.productionRuntimePackTransitionAttributionProfile -or
        -not [bool]$manifest.productionIsolationChecks.productionMapExcludesSafeHandoff -or
        -not [bool]$manifest.productionIsolationChecks.productionMapExcludesTransitionAttribution -or
        ($managedEntryDispatchProfile -and -not [bool]$manifest.productionIsolationChecks.productionMapExcludesManagedEntryProbe) -or
        ($managedEntryDispatchProfile -and [bool]$manifest.productionIsolationChecks.productionRuntimePackManagedEntryProbeProfile) -or
        -not [bool]$manifest.productionIsolationChecks.noGcOwnershipProofProfileInProduction) {
        throw 'Candidate manifest is not a consistent, unexecuted C188 transition-attribution profile.'
    }
    if ($isManagedEntryProbe -and
        (-not [bool]$manifest.c188R12R4R5R2R3ManagedEntryProbe -or
         [string]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.entryMethod -cne 'HostLogProof.ManagedEntryProbe.ManagedMain' -or
         [string]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.ABI -cne 'UnmanagedCallersOnly(CallConvCdecl)' -or
         [int]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.seed -ne 14 -or
         [int]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.expectedResult -ne 49 -or
         -not [bool]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.methodAllocationFree -or
         -not [bool]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.runtimeInitializationMayAllocate -or
         [bool]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.collectionRequested)) {
        throw 'R7 candidate managed-entry contract is missing, mismatched, or advertises an unsafe runtime guarantee.'
    }
    if ($isManagedSettingsObjectProbe -and
        (-not [bool]$manifest.c188R12R4R5R2R3ManagedEntryProbe -or
         -not [bool]$manifest.c188R12R4R5R2R3ManagedSettingsObjectProbe -or
         [string]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.entryMethod -cne 'HostLogProof.ManagedSettingsObjectProbe.ManagedMain' -or
         [string]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.ABI -cne 'UnmanagedCallersOnly(CallConvCdecl)' -or
         [int]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.seed -ne 14 -or
         [int]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.expectedResult -ne 1122 -or
         [bool]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.methodAllocationFree -or
         -not [bool]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.implicitCollectionMayOccur -or
         [bool]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.collectionRequested -or
         [string]$manifest.c188R12R4R5R2R3ManagedSettingsObjectAllocationContract.helper -cne 'RhpNewFast' -or
         [string]$manifest.c188R12R4R5R2R3ManagedSettingsObjectAllocationContract.stockHelper -cne 'guideXosStockRhpNewFast' -or
         -not [bool]$manifest.c188R12R4R5R2R3ManagedSettingsObjectAllocationContract.implicitCollectionCountRecorded -or
         [bool]$manifest.c188R12R4R5R2R3ManagedSettingsObjectAllocationContract.collectionRequestedByManagedProbe -or
         [bool]$manifest.c188R12R4R5R2R3ManagedSettingsObjectProbeContract.nativeObjectPointerPersisted -or
         [bool]$manifest.c188R12R4R5R2R3ManagedSettingsObjectProbeContract.collectionRequested)) {
        throw 'R12 managed-settings allocation, collection, or lifetime contract is missing or mismatched.'
    }
    if ($isManagedGcRootProbe) {
        $gcRootContract=$manifest.c188R13ManagedGcRootProbeContract
        if (-not [bool]$manifest.c188R13ManagedGcRootProbe -or
            -not [bool]$manifest.managedCollectionRequested -or
            [int]$gcRootContract.nativeRecordSizeBytes -ne 44 -or
            [int]$gcRootContract.initialResult -ne 1122 -or
            [int]$gcRootContract.expectedResult -ne 1123 -or
            -not [bool]$gcRootContract.oneCollectionOnly -or
            [string]$gcRootContract.entryMethod -cne 'HostLogProof.ManagedSettingsObjectProbe.ManagedMain' -or
            [string]$gcRootContract.allocationHelper -cne 'RhpNewFast' -or
            [string]$gcRootContract.collectionHelper -cne 'RhpCollect' -or
            [string]$gcRootContract.nativeCounterApi -cne 'guideXosNativeAotC188R12R4R5ReadCounters' -or
            [bool]$gcRootContract.finalizers -or [bool]$gcRootContract.managedThreads -or
            [bool]$gcRootContract.concurrentGC -or [bool]$gcRootContract.nativeObjectPointerPersisted -or
            [string]$gcRootContract.rootManagedRegister -notmatch 'RSI' -or
            [string]$gcRootContract.generatedGcInfoEvidence -notmatch 'EnumerateLiveSlots.*ReportSlotToGC') {
            throw 'R13 candidate managed-GC, native collection, or generated root-map contract is missing or mismatched.'
        }
    }
    $contract = $manifest.transitionAttributionContract
    if ([string]$contract.recordSchema -cne 'fixed-size C188R12R4R5R2R3_FATAL plus C188R12R4R5R2R3_STATE' -or
        [string]$contract.failFastReason -notmatch '0x50414C06' -or
        [string]$contract.transitionEntry -notmatch 'generated InitializeModules' -or
        [bool]$contract.managedEntryReached -or [bool]$contract.collectionRequested) {
        throw 'Transition attribution contract does not match the candidate transition boundary.'
    }

    if ([string]::IsNullOrWhiteSpace($InventoryPath)) {
        $InventoryPath = Join-Path $candidateFull 'candidate-integrity-inventory.json'
    }
    $inventoryResult = Test-QacCandidateIntegrityInventory -CandidateRoot $candidateFull -InventoryPath $InventoryPath
    if ($inventoryResult.CandidateManifestSha256 -cne $manifestHash) {
        throw 'Candidate manifest changed during profile validation.'
    }

    $rootPrefix = $candidateFull + [System.IO.Path]::DirectorySeparatorChar
    $RequireContainedFile = {
        param([string]$Path,[string]$Name,[bool]$Container=$false)
        if ([string]::IsNullOrWhiteSpace($Path)) { throw "$Name path is missing from candidate metadata." }
        $full = [System.IO.Path]::GetFullPath($Path)
        $pathType = if ($Container) { 'Container' } else { 'Leaf' }
        if (-not $full.StartsWith($rootPrefix,[System.StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $full -PathType $pathType)) { throw "$Name is missing or outside the candidate root: $full" }
        return $full
    }
    $RequireHash = {
        param([string]$Path,[string]$Expected,[string]$Name)
        $full = [System.IO.Path]::GetFullPath($Path)
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "$Name is missing: $full" }
        $actual = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actual -cne ([string]$Expected).ToUpperInvariant()) { throw "$Name hash mismatch." }
    }

    $esp = & $RequireContainedFile ([string]$manifest.esp) 'Staged candidate ESP' $true
    $managedPe = & $RequireContainedFile ([string]$manifest.managedPe) 'Diagnostic NativeAOT PE'
    $managedElf = & $RequireContainedFile ([string]$manifest.managedElf) 'Diagnostic NativeAOT ELF'
    $runtimePackManifest = & $RequireContainedFile ([string]$manifest.runtimePackManifest) 'Diagnostic runtime-pack manifest'
    $callSiteMap = & $RequireContainedFile ([string]$manifest.transitionCallSiteMap) 'Transition call-site map'
    $prerequisiteMatrix = & $RequireContainedFile ([string]$manifest.transitionPrerequisiteMatrix) 'Transition prerequisite matrix'
    $phasePlanPath = & $RequireContainedFile ([string]$manifest.transitionGuestPhasePlan) 'Transition guest phase plan'
    $guardTemplatePath = & $RequireContainedFile ([string]$manifest.transitionOneBootGuardTemplate) 'Transition one-boot guard template'
    $hostedResultsPath = & $RequireContainedFile ([string]$manifest.transitionAttributionHostedTestResults) 'Transition hosted-test results'
    $kernel = [System.IO.Path]::GetFullPath([string]$manifest.specializedKernel)
    $sourceRamdisk = [System.IO.Path]::GetFullPath((Join-Path $repoFull 'ESP\ramdisk.img'))
    $managedMap = [System.IO.Path]::ChangeExtension($managedPe,'.map')
    $productionNativeManifest = [System.IO.Path]::GetFullPath([string]$manifest.productionNativeAotManifest)
    $productionKernelManifest = [System.IO.Path]::GetFullPath([string]$manifest.productionKernelManifest)
    $artifactHashes = $manifest.artifactHashes
    $hashTargets = @(
        @($managedPe,[string]$manifest.managedPeSha256,'Diagnostic NativeAOT PE'),
        @($managedMap,[string]$manifest.managedMapSha256,'Diagnostic NativeAOT map'),
        @($managedElf,[string]$manifest.managedElfSha256,'Diagnostic NativeAOT ELF'),
        @($kernel,[string]$manifest.specializedKernelSha256,'Specialized diagnostic kernel'),
        @($runtimePackManifest,[string]$manifest.runtimePackManifestSha256,'Runtime-pack manifest'),
        @($callSiteMap,[string]$manifest.transitionCallSiteMapSha256,'Transition call-site map'),
        @($prerequisiteMatrix,[string]$artifactHashes.transitionPrerequisiteMatrix,'Transition prerequisite matrix'),
        @($phasePlanPath,[string]$artifactHashes.transitionGuestPhasePlan,'Transition guest phase plan'),
        @($guardTemplatePath,[string]$artifactHashes.transitionGuestGuardTemplate,'Transition guard template'),
        @($hostedResultsPath,[string]$artifactHashes.transitionHostedTestResults,'Transition hosted-test result'),
        @(([System.IO.Path]::GetFullPath((Join-Path $repoFull 'scripts\dotnet\analyze-c188r12r4r5r2r3-transition-attribution.ps1'))),[string]$artifactHashes.transitionAnalyzer,'Transition analyzer'),
        @($sourceRamdisk,[string]$manifest.sourceRamdiskSha256,'Protected source ramdisk'),
        @($productionNativeManifest,[string]$artifactHashes.productionNativeAotQualification,'Production NativeAOT qualification'),
        @($productionKernelManifest,[string]$artifactHashes.productionKernelQualification,'Production kernel qualification')
    )
    foreach ($target in $hashTargets) { & $RequireHash $target[0] $target[1] $target[2] }
    if ([string]$manifest.specializedKernelSha256 -cne [string]$manifest.espKernelSha256 -or
        [string]$manifest.sourceRamdiskSha256 -cne 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8' -or
        [string]$manifest.candidateRamdiskSha256 -cne [string]$manifest.sourceRamdiskSha256) {
        throw 'Diagnostic guest media does not match the candidate and protected source identities.'
    }
    $guestArtifactResult = Test-QacTransitionGuestArtifacts -Esp $esp `
        -KernelSha256 ([string]$manifest.espKernelSha256) `
        -RamdiskSha256 ([string]$manifest.candidateRamdiskSha256) `
        -BootloaderSha256 ([string]$artifactHashes.stagedEspBootloader)

    $runtime = Get-Content -LiteralPath $runtimePackManifest -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([string]$runtime.identity -cne $expectedIdentity -or
        [string]$runtime.runtimeIdentity.sourceCommit -cne [string]$manifest.runtimePackSourceCommit) {
        throw 'Runtime-pack manifest does not match the pinned transition candidate identity.'
    }
    $layoutEvidenceProperty = $manifest.PSObject.Properties['c188R12R4R5R2R3ThreadLayoutEvidence']
    if ($null -ne $layoutEvidenceProperty) {
        $layout = $layoutEvidenceProperty.Value
        $artifactRootProperty = $manifest.PSObject.Properties['artifactRoot']
        $layoutArtifactRoot = if ($null -eq $artifactRootProperty) { '' } else { [string]$artifactRootProperty.Value }
        $layoutPhaseName = if ($isManagedGcRootProbe) { 'c188r13-managed-gc-root' } elseif ($isManagedSettingsObjectProbe) { 'c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object' } elseif ($isManagedEntryProbe) { 'c188r12r4r5r2r3-managed-entry' } else { 'c188r12r4r5r4r5r2r2r4r2r3-transition-attribution' }
        $layoutPhaseRoot = [System.IO.Path]::GetFullPath((Join-Path $layoutArtifactRoot $layoutPhaseName))
        $layoutSourceFiles = $runtime.c188r12r4r5r4ThreadStoreReadinessSourceFiles
        if ([string]::IsNullOrWhiteSpace($layoutArtifactRoot) -or
            -not [string]::Equals($phaseFull,$layoutPhaseRoot,[System.StringComparison]::OrdinalIgnoreCase) -or
            [string]$layout.sourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
            [string]$layout.runtimeCellOffset -cne '0x30' -or
            [string]$layout.asmOffsetsThreadFields.m_ThreadStateFlags -cne '38' -or
            [string]$layout.asmOffsetsThreadFields.m_pTransitionFrame -cne '40' -or
            [string]$layout.asmOffsetsThreadFields.m_pDeferredTransitionFrame -cne '48' -or
            [string]$layout.staticAssertedRuntimeThreadLocalsOffsets.m_ThreadStateFlags -cne '0x38' -or
            [string]$layout.staticAssertedRuntimeThreadLocalsOffsets.m_pCachedTransitionFrame -cne '0x50' -or
            [string]$layout.staticAssertedRuntimeThreadLocalsOffsets.m_pNext -cne '0x58' -or
            -not [bool]$layout.legacySentinelWritesOnlyRuntimeCell -or
            $null -ne $layout.palFlsShadowOffset -or
            [string]$layout.threadHeaderSha256 -cne [string]$layoutSourceFiles.'coreclr/nativeaot/Runtime/thread.h' -or
            [string]$layout.asmOffsetsSha256 -cne [string]$layoutSourceFiles.'coreclr/nativeaot/Runtime/AsmOffsets.h' -or
            $null -ne $runtime.platformObject.PSObject.Properties['runtimeCellInitializedOffset'] -or
            $null -ne $runtime.platformObject.PSObject.Properties['flsCellOffset']) {
            throw 'R6 candidate layout evidence does not bind the pinned source, compiled ABI assertions, and removed TLS overlap fields.'
        }
    }
    $hosted = Get-Content -LiteralPath $hostedResultsPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([string]$hosted.result -cne 'PASS' -or [int]$hosted.assertionCount -lt 20 -or
        [bool]$hosted.qemuLaunched -or [bool]$hosted.qacReserved) {
        throw 'Transition candidate hosted evidence is missing, incomplete, or crossed the native-guest boundary.'
    }
    $plan = Get-Content -LiteralPath $phasePlanPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $expectedPhasePlanSchema = if ($isManagedGcRootProbe) { 'C188R13_GUEST_PHASE_PLAN_V1' } else { 'C188R12R4R5R2R3R1_GUEST_PHASE_PLAN_V1' }
    $expectedGuardPath = if ($isManagedGcRootProbe) {
        Join-Path $phaseFull ("guest-launch-attempt-" + [System.IO.Path]::GetFileName($candidateFull) + ".json")
    } else { [string]$plan.oneBootGuard.path }
    if ([string]$plan.schema -cne $expectedPhasePlanSchema -or
        [string]$plan.status -cne 'prepared_not_executed' -or
        [string]$plan.candidateManifest -cne $manifestPath -or
        [string]$plan.candidateMode -cne [string]$manifest.mode -or
        [string]$plan.admission.qacProject -cne 'guideXOSServer' -or
        [string]$plan.admission.qacMode -cne 'acceptance' -or
        [string]$plan.admission.qacReservation -cne 'not_requested' -or
        -not [bool]$plan.admission.noAdmissionNoGuard -or
        [bool]$plan.oneBootGuard.materialized -or [int]$plan.oneBootGuard.maxGuestBoots -ne 1 -or
        [bool]$plan.oneBootGuard.automaticRetry -or
        -not [bool]$plan.oneBootGuard.bindsCandidateManifestSha256 -or
        -not [bool]$plan.oneBootGuard.bindsDiagnosticElfKernelAndRamdiskSha256 -or
        ($isManagedGcRootProbe -and [System.IO.Path]::GetFullPath([string]$plan.oneBootGuard.path) -cne
            [System.IO.Path]::GetFullPath($expectedGuardPath)) -or
        [bool]$plan.executionNotPerformed.qemuLaunched -or [bool]$plan.executionNotPerformed.qacReserved -or
        [bool]$plan.executionNotPerformed.guardCreated -or [int]$plan.executionNotPerformed.guestBoots -ne 0 -or
        ($isManagedGcRootProbe -and ([string]$plan.executionContract[5] -notmatch 'exactly one synchronous GC\.Collect' -or
            [string]$plan.executionContract[5] -notmatch 'native collection-count advance and completion evidence' -or
            [string]$plan.executionContract[5] -notmatch '1123'))) {
        throw 'Prepared transition guest plan is malformed, already consumed, or allows more than one boot.'
    }
    $template = Get-Content -LiteralPath $guardTemplatePath -Raw | ConvertFrom-Json -ErrorAction Stop
    $expectedGuardTemplateSchema = if ($isManagedGcRootProbe) { 'C188R13_ONE_BOOT_GUARD_TEMPLATE_V1' } elseif ($isManagedSettingsObjectProbe) { 'C188R12R4R5R4R5R2R2R4R2R3R12_ONE_BOOT_GUARD_TEMPLATE_V1' } else { 'C188R12R4R5R2R3R1_ONE_BOOT_GUARD_TEMPLATE_V1' }
    if ([string]$template.schema -cne $expectedGuardTemplateSchema -or
        [string]$template.candidateManifest -cne $manifestPath -or
        [string]$template.diagnosticElfSha256 -cne [string]$manifest.managedElfSha256 -or
        [string]$template.diagnosticKernelSha256 -cne [string]$manifest.specializedKernelSha256 -or
        [string]$template.ramdiskSha256 -cne [string]$manifest.candidateRamdiskSha256 -or
        [string]$template.qacProject -cne 'guideXOSServer' -or [string]$template.qacMode -cne 'acceptance' -or
        [int]$template.maxGuestBoots -ne 1 -or [bool]$template.automaticRetry -or [bool]$template.consumed) {
        throw 'Transition one-boot guard template does not bind this candidate or violates the one-boot contract.'
    }
    $callMap = Get-Content -LiteralPath $callSiteMap -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([string]$callMap.lockedRuntimeCommit -cne [string]$manifest.runtimePackSourceCommit -or
        @($callMap.sites).Count -lt 1) { throw 'Transition call-site map is malformed or targets another runtime commit.' }
    $matrix = Get-Content -LiteralPath $prerequisiteMatrix -Raw | ConvertFrom-Json -ErrorAction Stop
    $managedDispatchConditionalProperty = $matrix.safetyGate.PSObject.Properties['managedDispatchConditional']
    $managedDispatchConditional = if ($null -eq $managedDispatchConditionalProperty) { $false } else { [bool]$managedDispatchConditionalProperty.Value }
    if ([string]$matrix.schema -cne 'C188R12R4R5R2R3_TRANSITION_PREREQUISITE_MATRIX_V1' -or
        [string]$matrix.candidateMode -cne [string]$manifest.mode -or
        ([bool]$matrix.safetyGate.noManagedDispatch -eq $managedEntryDispatchProfile) -or
        ($managedDispatchConditional -ne $managedEntryDispatchProfile) -or
        ([bool]$matrix.safetyGate.noCollection -eq $isManagedGcRootProbe)) {
        throw 'Transition prerequisite matrix does not preserve the diagnostic safety gate.'
    }
    if ($isManagedEntryProbe) {
        $probeDisassembly = Join-Path $candidateFull 'managedEntryProbe.disassembly.txt'
        $probeSource = Join-Path $repoFull 'samples\managed\HostLogProof\ManagedEntryProbe.cs'
        $probeHeader = Join-Path $repoFull 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_managed_entry_probe.h'
        $probeHostedResultsPath = & $RequireContainedFile ([string]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.hostedResults) 'R7 managed-entry hosted test results'
        foreach ($target in @(
            @($probeDisassembly,[string]$artifactHashes.candidateManagedEntryProbeDisassembly,'Managed entry probe disassembly'),
            @($probeSource,[string]$artifactHashes.managedEntryProbeSource,'Managed entry probe source'),
            @($probeHeader,[string]$artifactHashes.managedEntryProbeRuntimeHeader,'Managed entry probe native header'),
            @($probeHostedResultsPath,[string]$artifactHashes.managedEntryProbeHostedTestResults,'Managed entry probe hosted results')
        )) { & $RequireHash $target[0] $target[1] $target[2] }
        $probeMapSymbol = [string]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.entryMethodMapSymbol
        $probeMapText = Get-Content -LiteralPath $managedMap -Raw
        $probeDisassemblyText = Get-Content -LiteralPath $probeDisassembly -Raw
        $reversePInvokeMap = [regex]::Match($probeMapText,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpReversePInvoke\s+(?<address>[0-9A-Fa-f]{16})\s+f(?:\s|$)')
        $reversePInvokeReturnMap = [regex]::Match($probeMapText,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpReversePInvokeReturn\s+(?<address>[0-9A-Fa-f]{16})\s+f(?:\s|$)')
        $expectedProbeCallTargets = @()
        if ($reversePInvokeMap.Success -and $reversePInvokeReturnMap.Success) {
            $expectedProbeCallTargets += ('0x' + ([Convert]::ToUInt64($reversePInvokeMap.Groups['address'].Value,16)).ToString('x'))
            $expectedProbeCallTargets += ('0x' + ([Convert]::ToUInt64($reversePInvokeReturnMap.Groups['address'].Value,16)).ToString('x'))
        }
        $actualProbeCallTargets = @([regex]::Matches($probeDisassemblyText,'(?m)\bcall\s+(?<target>0x[0-9a-fA-F]+)\b') |
            ForEach-Object { $_.Groups['target'].Value.ToLowerInvariant() })
        $probeCallsMatch = @($expectedProbeCallTargets).Count -eq 2 -and
            @($actualProbeCallTargets).Count -eq 2 -and
            $actualProbeCallTargets[0] -ceq $expectedProbeCallTargets[0] -and
            $actualProbeCallTargets[1] -ceq $expectedProbeCallTargets[1]
        if ([string]::IsNullOrWhiteSpace($probeMapSymbol) -or
            -not (Select-String -LiteralPath $managedMap -Pattern ([regex]::Escape($probeMapSymbol)) -Quiet) -or
            -not (Select-String -LiteralPath $managedMap -Pattern 'GuideXosNativeAotC188R12R4R5R2R3ManagedEntryProbeCaptureState' -Quiet) -or
            -not (Select-String -LiteralPath $probeDisassembly -Pattern '5237454[eE]' -Quiet) -or
            -not (Select-String -LiteralPath $probeDisassembly -Pattern '5237444[eE]' -Quiet) -or
            -not $probeCallsMatch) {
            throw 'R7 candidate map or linked method body does not prove the entry symbol and stock reverse-P/Invoke marker path.'
        }
        $probeHosted = Get-Content -LiteralPath $probeHostedResultsPath -Raw | ConvertFrom-Json -ErrorAction Stop
        if ([string]$probeHosted.result -cne 'PASS' -or [int]$probeHosted.assertionCount -lt 12 -or
            [bool]$probeHosted.qemuLaunched -or [bool]$probeHosted.qacReserved -or
            [bool]$probeHosted.managedCollectionRequested) {
            throw 'R7 managed-entry focused hosted checks are incomplete or crossed the guest boundary.'
        }
    }
    if ($isManagedSettingsObjectProbe) {
        $probeDisassembly = Join-Path $candidateFull 'managedEntryProbe.disassembly.txt'
        $constructorDisassembly = Join-Path $candidateFull 'displaySettingsProbeConstructor.disassembly.txt'
        $calculateDisassembly = Join-Path $candidateFull 'displaySettingsProbeCalculate.disassembly.txt'
        $probeSource = Join-Path $repoFull 'samples\managed\HostLogProof\ManagedSettingsObjectProbe.cs'
        $probeHeader = Join-Path $repoFull 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_managed_settings_object_probe.h'
        $probeHostedResultsPath = & $RequireContainedFile ([string]$manifest.c188R12R4R5R2R3ManagedSettingsObjectProbeContract.hostedResults) 'R12 managed-settings hosted-test results'
        $r11HostedResultsPath = & $RequireContainedFile ([string]$manifest.c188R12R4R5R2R3ManagedSettingsObjectProbeContract.regressionHostedResults) 'R11 managed-entry regression hosted-test results'
        foreach ($target in @(
            @($probeDisassembly,[string]$artifactHashes.candidateManagedEntryProbeDisassembly,'R12 ManagedMain disassembly'),
            @($constructorDisassembly,[string]$artifactHashes.candidateSettingsObjectConstructorDisassembly,'R12 constructor disassembly'),
            @($calculateDisassembly,[string]$artifactHashes.candidateSettingsObjectCalculateDisassembly,'R12 field-consumer disassembly'),
            @($probeSource,[string]$artifactHashes.managedSettingsObjectProbeSource,'R12 managed source'),
            @($probeHeader,[string]$artifactHashes.managedSettingsObjectProbeRuntimeHeader,'R12 allocation record header'),
            @($probeHostedResultsPath,[string]$artifactHashes.managedSettingsObjectProbeHostedTestResults,'R12 hosted results'),
            @($r11HostedResultsPath,[string]$artifactHashes.managedEntryRegressionHostedTestResults,'R11 regression results'),
            @(([System.IO.Path]::GetFullPath((Join-Path $repoFull 'scripts\dotnet\analyze-c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object.ps1'))),[string]$artifactHashes.managedSettingsObjectProbeAnalyzer,'R12 serial analyzer'),
            @(([System.IO.Path]::GetFullPath((Join-Path $repoFull 'scripts\dotnet\test-c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object.ps1'))),[string]$artifactHashes.managedSettingsObjectProbeHostedTestScript,'R12 hosted-test source')
        )) { & $RequireHash $target[0] $target[1] $target[2] }
        $r12Hosted = Get-Content -LiteralPath $probeHostedResultsPath -Raw | ConvertFrom-Json -ErrorAction Stop
        $r11Hosted = Get-Content -LiteralPath $r11HostedResultsPath -Raw | ConvertFrom-Json -ErrorAction Stop
        $probeMapSymbol = [string]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.entryMethodMapSymbol
        $probeMapText = Get-Content -LiteralPath $managedMap -Raw
        $probeDisassemblyText = Get-Content -LiteralPath $probeDisassembly -Raw
        $constructorText = Get-Content -LiteralPath $constructorDisassembly -Raw
        $calculateText = Get-Content -LiteralPath $calculateDisassembly -Raw
        if ([string]::IsNullOrWhiteSpace($probeMapSymbol) -or
            -not (Select-String -LiteralPath $managedMap -Pattern ([regex]::Escape($probeMapSymbol)) -Quiet) -or
            -not (Select-String -LiteralPath $managedMap -Pattern 'DisplaySettingsProbe.*ctor' -Quiet) -or
            -not (Select-String -LiteralPath $managedMap -Pattern 'DisplaySettingsProbe.*Calculate' -Quiet) -or
            -not (Select-String -LiteralPath $managedMap -Pattern 'RhpNewFast' -Quiet) -or
            -not (Select-String -LiteralPath $managedMap -Pattern 'guideXosStockRhpNewFast' -Quiet) -or
            $probeDisassemblyText -notmatch '52313245' -or $probeDisassemblyText -notmatch '52313244' -or
            $constructorText -notmatch '52313243' -or $calculateText -notmatch '52313243' -or
            [string]$runtime.identity -cne $expectedIdentity -or
            [string]$runtime.runtimeIdentity.sourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
            [string]$runtime.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.helper -cne 'RhpNewFast' -or
            [string]$runtime.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.stockHelper -cne 'guideXosStockRhpNewFast' -or
            [string]$runtime.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.heap -cne 'pinned real Workstation GC; no C188 image-backed arena' -or
            -not [bool]$runtime.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.implicitCollectionCountRecorded -or
            [int]$r12Hosted.assertionCount -lt 18 -or [string]$r12Hosted.result -cne 'PASS' -or
            [bool]$r12Hosted.qemuLaunched -or [bool]$r12Hosted.qacReserved -or
            [bool]$r12Hosted.managedCollectionRequested -or
            [string]$r11Hosted.result -cne 'PASS' -or [bool]$r11Hosted.qemuLaunched -or
            [bool]$r11Hosted.qacReserved -or [bool]$r11Hosted.managedCollectionRequested) {
            throw 'R12 source, generated path, pinned allocation contract, or hosted regression evidence is incomplete.'
        }
    }
    if ($isManagedGcRootProbe) {
        $probeDisassembly = Join-Path $candidateFull 'managedEntryProbe.disassembly.txt'
        $gcCollectWrapper = & $RequireContainedFile ([string]$gcRootContract.collectionWrapperDisassembly) 'R13 GC.Collect wrapper disassembly'
        $constructorDisassembly = & $RequireContainedFile ([string]$gcRootContract.constructorDisassembly) 'R13 constructor disassembly'
        $calculateDisassembly = & $RequireContainedFile ([string]$gcRootContract.calculateDisassembly) 'R13 field-consumer disassembly'
        $probeSource = Join-Path $repoFull 'samples\managed\HostLogProof\ManagedGcRootProbe.cs'
        $probeHeader = Join-Path $repoFull 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_managed_gc_root_probe.h'
        $probeAnalyzer = Join-Path $repoFull 'scripts\dotnet\analyze-c188r13-managed-gc-root.ps1'
        $probeTestScript = Join-Path $repoFull 'scripts\dotnet\test-c188r13-managed-gc-root.ps1'
        $probeHostedResultsPath = & $RequireContainedFile ([string]$gcRootContract.hostedResults) 'R13 managed-GC hosted-test results'
        $r11HostedResultsPath = & $RequireContainedFile ([string]$manifest.c188R12R4R5R2R3ManagedEntryProbeContract.hostedResults) 'R11 managed-entry regression results'
        foreach ($target in @(
            @($probeDisassembly,[string]$artifactHashes.candidateManagedEntryProbeDisassembly,'R13 ManagedMain disassembly'),
            @($gcCollectWrapper,[string]$artifactHashes.candidateGcCollectWrapperDisassembly,'R13 GC.Collect wrapper disassembly'),
            @($constructorDisassembly,[string]$artifactHashes.candidateSettingsObjectConstructorDisassembly,'R13 constructor disassembly'),
            @($calculateDisassembly,[string]$artifactHashes.candidateSettingsObjectCalculateDisassembly,'R13 field-consumer disassembly'),
            @($probeSource,[string]$artifactHashes.managedGcRootProbeSource,'R13 managed probe source'),
            @($probeHeader,[string]$artifactHashes.managedGcRootProbeRuntimeHeader,'R13 native record header'),
            @($probeAnalyzer,[string]$artifactHashes.managedGcRootProbeAnalyzer,'R13 serial analyzer'),
            @($probeTestScript,[string]$artifactHashes.managedGcRootProbeHostedTestScript,'R13 hosted-test source'),
            @(([System.IO.Path]::GetFullPath((Join-Path $repoFull 'scripts\dotnet\c188r12r4r5r4-qac-admission.psm1'))),[string]$artifactHashes.transitionQacAdmissionModule,'R13 QAC admission module'),
            @($probeHostedResultsPath,[string]$artifactHashes.managedGcRootProbeHostedTestResults,'R13 hosted results'),
            @($r11HostedResultsPath,[string]$artifactHashes.managedEntryRegressionHostedTestResults,'R11 managed-entry regression results')
        )) { & $RequireHash $target[0] $target[1] $target[2] }

        $probeMapText = Get-Content -LiteralPath $managedMap -Raw
        $probeDisassemblyText = Get-Content -LiteralPath $probeDisassembly -Raw
        $wrapperDisassemblyText = Get-Content -LiteralPath $gcCollectWrapper -Raw
        $constructorText = Get-Content -LiteralPath $constructorDisassembly -Raw
        $calculateText = Get-Content -LiteralPath $calculateDisassembly -Raw
        $rhCollectMap = [regex]::Match($probeMapText,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+S_P_CoreLib_System_Runtime_InternalCalls__RhCollect\s+(?<address>[0-9A-Fa-f]{16})\b')
        $rhpCollectMap = [regex]::Match($probeMapText,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+RhpCollect\s+(?<address>[0-9A-Fa-f]{16})\s+f(?:\s|$)')
        $calculateMap = [regex]::Match($probeMapText,'(?m)^\s*0001:[0-9A-Fa-f]{8}\s+\S*DisplaySettingsProbe__Calculate\S*\s+(?<address>[0-9A-Fa-f]{16})\b')
        $r13CallPathValid = $rhCollectMap.Success -and $rhpCollectMap.Success -and $calculateMap.Success
        $collectionCallPosition = -1
        $postCollectionMutationPosition = -1
        $postCollectionCalculatePosition = -1
        if ($r13CallPathValid) {
            $rhCollectAddress = ([Convert]::ToUInt64($rhCollectMap.Groups['address'].Value,16)).ToString('x')
            $rhpCollectAddress = ([Convert]::ToUInt64($rhpCollectMap.Groups['address'].Value,16)).ToString('x')
            $calculateAddress = ([Convert]::ToUInt64($calculateMap.Groups['address'].Value,16)).ToString('x')
            $r13CallPathValid = $probeDisassemblyText -match ('(?mi)\bcall\s+0x' + $rhCollectAddress + '\b') -and
                $wrapperDisassemblyText -match ('(?mi)\bcall\s+0x' + $rhpCollectAddress + '\b')
            $collectionMatch = [regex]::Match($probeDisassemblyText,'(?mi)\bcall\s+0x' + $rhCollectAddress + '\b')
            if ($collectionMatch.Success) {
                $collectionCallPosition = $collectionMatch.Index
                $postCollectionMutationPosition = $probeDisassemblyText.IndexOf('DWORD PTR [rsi+0x10],0x3',$collectionCallPosition,[StringComparison]::OrdinalIgnoreCase)
                if ($postCollectionMutationPosition -ge 0) {
                    $calculateMatch = [regex]::Match($probeDisassemblyText.Substring($postCollectionMutationPosition),'(?mi)\bcall\s+0x' + $calculateAddress + '\b')
                    if ($calculateMatch.Success) { $postCollectionCalculatePosition = $postCollectionMutationPosition + $calculateMatch.Index }
                }
            }
        }
        if (-not (Select-String -LiteralPath $managedMap -Pattern 'ManagedSettingsObjectProbe.*ManagedMain' -Quiet) -or
            -not (Select-String -LiteralPath $managedMap -Pattern 'DisplaySettingsProbe.*___ctor' -Quiet) -or
            -not (Select-String -LiteralPath $managedMap -Pattern 'RhpNewFast' -Quiet) -or
            -not (Select-String -LiteralPath $managedMap -Pattern 'guideXosNativeAotC188R12R4R5ReadCounters' -Quiet) -or
            $probeDisassemblyText -notmatch '52313345' -or $probeDisassemblyText -notmatch '52313351' -or
            $probeDisassemblyText -notmatch '52313343' -or $probeDisassemblyText -notmatch '52313352' -or
            $probeDisassemblyText -notmatch 'mov\s+rsi,rax' -or -not $r13CallPathValid -or
            $postCollectionMutationPosition -le $collectionCallPosition -or
            $postCollectionCalculatePosition -le $postCollectionMutationPosition -or
            $probeMapText -notmatch '\.managedcode\$I' -or
            $probeMapText -notmatch 'EnumerateLiveSlots' -or $probeMapText -notmatch 'ReportSlotToGC' -or
            $constructorText -notmatch '5231334[fF]' -or
            [string]$runtime.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.helper -cne 'RhpNewFast' -or
            [string]$runtime.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.stockHelper -cne 'guideXosStockRhpNewFast' -or
            -not [bool]$runtime.c188r12r4r5r2r3ManagedSettingsObjectAllocationContract.implicitCollectionCountRecorded) {
            throw 'R13 map, generated live-root path, actual GC helper linkage, or stock allocation contract is incomplete.'
        }
        $r13Hosted = Get-Content -LiteralPath $probeHostedResultsPath -Raw | ConvertFrom-Json -ErrorAction Stop
        $r11Hosted = Get-Content -LiteralPath $r11HostedResultsPath -Raw | ConvertFrom-Json -ErrorAction Stop
        if ([string]$r13Hosted.result -cne 'PASS' -or [int]$r13Hosted.assertionCount -lt 34 -or
            -not [bool]$r13Hosted.managedCollectionRequested -or [bool]$r13Hosted.collectionExecuted -or
            [bool]$r13Hosted.qemuLaunched -or [bool]$r13Hosted.qacReserved -or
            [string]$r11Hosted.result -cne 'PASS' -or [bool]$r11Hosted.qemuLaunched -or
            [bool]$r11Hosted.qacReserved -or [bool]$r11Hosted.managedCollectionRequested) {
            throw 'R13/R11 hosted evidence is missing, incomplete, or crossed the hosted-only boundary.'
        }
        $planContent = Get-Content -LiteralPath $phasePlanPath -Raw
        if ($planContent -notmatch 'exactly one synchronous GC\.Collect' -or
            $planContent -notmatch 'native collection-count advance and completion evidence' -or
            $planContent -notmatch 'same rooted object.*1123') {
            throw 'R13 prepared guest plan does not authorize exactly one verified collection with a post-collection live-root result.'
        }
    }
    return [pscustomobject]@{
        CandidateRoot=$candidateFull;PhaseRoot=$phaseFull;ManifestPath=$manifestPath
        Manifest=$manifest;ManifestSha256=$manifestHash;InventoryPath=[System.IO.Path]::GetFullPath($InventoryPath)
        InventoryResult=$inventoryResult;GuestArtifactResult=$guestArtifactResult
        Esp=$esp;ManagedElf=$managedElf;Kernel=$kernel
        RuntimePackManifest=$runtimePackManifest;CallSiteMap=$callSiteMap;PrerequisiteMatrix=$prerequisiteMatrix
        PhasePlanPath=$phasePlanPath;GuardTemplatePath=$guardTemplatePath
        SourceRamdisk=$sourceRamdisk;HostedEvidence=$hosted
    }
}

function New-QacTransitionGuestState {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$CandidateEsp,
        [Parameter(Mandatory=$true)][string]$CandidateRoot,
        [Parameter(Mandatory=$true)][string]$PhaseRoot,
        [Parameter(Mandatory=$true)][string]$RunRoot,
        [Parameter(Mandatory=$true)][string]$UefiVarsTemplate
    )
    $candidateFull = [System.IO.Path]::GetFullPath($CandidateRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,[System.IO.Path]::AltDirectorySeparatorChar)
    $phaseFull = [System.IO.Path]::GetFullPath($PhaseRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,[System.IO.Path]::AltDirectorySeparatorChar)
    $runFull = [System.IO.Path]::GetFullPath($RunRoot).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,[System.IO.Path]::AltDirectorySeparatorChar)
    $candidatePrefix = $candidateFull + [System.IO.Path]::DirectorySeparatorChar
    $phasePrefix = $phaseFull + [System.IO.Path]::DirectorySeparatorChar
    if (-not $runFull.StartsWith($phasePrefix,[System.StringComparison]::OrdinalIgnoreCase) -or
        $runFull.StartsWith($candidatePrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Transition writable guest state must be a fresh run-owned directory outside the immutable candidate.'
    }
    if (Test-Path -LiteralPath $runFull) { throw "Refusing to reuse guest state from another run: $runFull" }
    $sourceEspFull = [System.IO.Path]::GetFullPath($CandidateEsp)
    if (-not (Test-Path -LiteralPath $sourceEspFull -PathType Container)) {
        throw "Transition candidate ESP is missing: $sourceEspFull"
    }
    $varsFull = [System.IO.Path]::GetFullPath($UefiVarsTemplate)
    if (-not (Test-Path -LiteralPath $varsFull -PathType Leaf)) {
        throw "Writable UEFI variables template is missing: $varsFull"
    }
    $varsItem = Get-Item -LiteralPath $varsFull -Force -ErrorAction Stop
    if (($varsItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Writable UEFI variables template cannot be a reparse point.'
    }
    $varsSourceHash = (Get-FileHash -LiteralPath $varsFull -Algorithm SHA256).Hash.ToUpperInvariant()
    $sourceTree = @(Get-QacTransitionTreeInventory -Root $sourceEspFull | Sort-Object Path)
    if ($sourceTree.Count -eq 0) { throw 'Transition candidate ESP is empty.' }
    New-Item -ItemType Directory -Path $runFull -ErrorAction Stop | Out-Null
    $isolatedEsp = Join-Path $runFull 'esp'
    New-Item -ItemType Directory -Path $isolatedEsp -ErrorAction Stop | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $sourceEspFull -Force -ErrorAction Stop)) {
        Copy-Item -LiteralPath $item.FullName -Destination $isolatedEsp -Recurse -Force -ErrorAction Stop
    }
    $isolatedTree = @(Get-QacTransitionTreeInventory -Root $isolatedEsp | Sort-Object Path)
    if ($isolatedTree.Count -ne $sourceTree.Count) { throw 'Isolated transition ESP file count does not match the candidate.' }
    for ($index=0; $index -lt $sourceTree.Count; $index++) {
        if ([string]$sourceTree[$index].Path -cne [string]$isolatedTree[$index].Path -or
            [int64]$sourceTree[$index].Length -ne [int64]$isolatedTree[$index].Length) {
            throw 'Isolated transition ESP layout differs from the immutable candidate.'
        }
        $sourceHash = (Get-FileHash -LiteralPath $sourceTree[$index].FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        $isolatedHash = (Get-FileHash -LiteralPath $isolatedTree[$index].FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($sourceHash -cne $isolatedHash) { throw "Isolated transition ESP hash mismatch: $($sourceTree[$index].Path)" }
    }
    $varsCopy = Join-Path $runFull 'uefi-vars.fd'
    Copy-Item -LiteralPath $varsFull -Destination $varsCopy -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $varsCopy -Algorithm SHA256).Hash.ToUpperInvariant() -cne $varsSourceHash) {
        throw 'Fresh writable UEFI variables image differs from its pinned template.'
    }
    return [pscustomobject]@{
        RunRoot=$runFull;Esp=$isolatedEsp;EspFileCount=$sourceTree.Count
        SourceEsp=$sourceEspFull;SourceEspSha256=(Get-FileHash -LiteralPath (Join-Path $sourceEspFull 'ramdisk.img') -Algorithm SHA256).Hash.ToUpperInvariant()
        IsolatedRamdiskSha256=(Get-FileHash -LiteralPath (Join-Path $isolatedEsp 'ramdisk.img') -Algorithm SHA256).Hash.ToUpperInvariant()
        UefiVarsTemplate=$varsFull;UefiVarsTemplateSha256=$varsSourceHash;UefiVarsImage=$varsCopy
        UefiVarsImageSha256=(Get-FileHash -LiteralPath $varsCopy -Algorithm SHA256).Hash.ToUpperInvariant()
        UefiVarsImageLengthBytes=[int64](Get-Item -LiteralPath $varsCopy).Length
    }
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

Export-ModuleMember -Function ConvertFrom-QacStatusSnapshot, ConvertFrom-QacListSnapshot, Get-QacReservationProperty, Test-QacTimestampNear, Test-QacCandidatePath, Test-QacCandidateIntegrityInventory, Test-QacTransitionGuestArtifacts, Test-QacTransitionCandidateProfile, Get-QacTransitionTreeInventory, New-QacTransitionGuestState, Test-QacClear, Test-QacClearWithCensus, Test-QacRunningOwned, Test-QacLocalQemuChildIdentity, ConvertFrom-QmpQueryStatusResponse
