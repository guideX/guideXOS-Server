[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$CandidateDirectory,
    [Parameter(Mandatory = $true)] [string]$ArtifactRoot,
    [string]$QacCliPath = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [ValidateRange(1, 3600)] [int]$AdmissionTimeoutSeconds = 1800,
    [ValidateRange(30, 900)] [int]$RuntimeSeconds = 300,
    [ValidateRange(5, 180)] [int]$ChildCleanupSeconds = 45,
    [ValidateRange(5, 120)] [int]$ReservationCleanupSeconds = 20
)

$ErrorActionPreference = 'Stop'
$CandidateDirectory = [IO.Path]::GetFullPath($CandidateDirectory)
$ArtifactRoot = [IO.Path]::GetFullPath($ArtifactRoot)
$QacCliPath = [IO.Path]::GetFullPath($QacCliPath)
$QemuPath = [IO.Path]::GetFullPath($QemuPath)
$FirmwareCode = [IO.Path]::GetFullPath($FirmwareCode)
$FirmwareVarsTemplate = [IO.Path]::GetFullPath($FirmwareVarsTemplate)
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$null = Import-Module (Join-Path $PSScriptRoot 'aarch64-runsafe2-qac-supervisor.psm1') -Force
$project = 'guideXOSServer-AARCH64-RUNSAFE2-EL0-Negative'
$sessionId = 'el0-negative-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
$sessionDirectory = Join-Path $ArtifactRoot $sessionId
$null = New-Item -ItemType Directory -Path $sessionDirectory -Force
$manifestPath = Join-Path $CandidateDirectory 'candidate-manifest.json'
$guestLogPath = Join-Path $sessionDirectory 'guest.log'
$qacStdoutPath = Join-Path $sessionDirectory 'qac.stdout.log'
$qacStderrPath = Join-Path $sessionDirectory 'qac.stderr.log'
$sessionPath = Join-Path $sessionDirectory 'session.json'
$varsPath = Join-Path $sessionDirectory 'edk2-vars.fd'
$qac = $null
$ownedChild = $null
$ownedChildStartUtc = $null
$qacStartUtc = $null
$runtimeWatch = $null
$runtimeOffsetSeconds = 0.0
$cleanupWatch = $null
$releaseWatch = $null
$lastQmpAttemptUtc = [DateTimeOffset]::MinValue
$qmpAttempts = 0
$qmpQuitCommandAttempted = $false
$qmpQuitResponseKind = 'NotAttempted'
$qmpQuitError = ''
$qmpPort = 0
$reservationId = ''
$reservationState = ''
$runnerFailure = ''
$guestPassed = $false
$qmpQuitSent = $false
$qmpQuitAcknowledged = $false
$qacExitVerified = $false
$reservationReleased = $false
$exclusiveGrant = $false
$sessionState = New-QacSupervisorState
$logFinalizationOk = $true
$logFinalizationError = ''
$qacChildExitVerified = $false
$qacReleaseMarker = $false
$qemuExitCode = $null
$qacChildExitUtc = ''
$managedLaunchUtc = ''
$lastQacStatus = ''
$lastQacList = ''
$lastReservationObject = $null
$lastChildStatus = 'Pending'
$qacManualReleaseAttempted = $false
$seenGuestMarkers = @{}

function Get-Sha256Hex([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Save-SessionRecord {
    $record.updated_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $record.qac_state_transitions = @($sessionState.transitions.ToArray())
    $record.state_machine_phase = $sessionState.phase
    $record.state_machine_action = $sessionState.last_action
    $json = $record | ConvertTo-Json -Depth 20
    $temporaryPath = $sessionPath + '.tmp'
    try {
        [IO.File]::WriteAllText($temporaryPath, $json, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporaryPath -Destination $sessionPath -Force
        return $true
    } catch {
        $record.partial_log_finalization_error = $_.Exception.Message
        $script:logFinalizationOk = $false
        $script:logFinalizationError = $_.Exception.Message
        try {
            [IO.File]::WriteAllText(($sessionPath + '.partial.json'), ($record | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
        } catch { }
        return $false
    }
}

function Quote-ProcessArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { ++$backslashes; continue }
        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, (2 * $backslashes + 1))
            [void]$builder.Append([char]34)
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) { [void]$builder.Append([char]92, $backslashes) }
        [void]$builder.Append($character)
        $backslashes = 0
    }
    if ($backslashes -gt 0) { [void]$builder.Append([char]92, (2 * $backslashes)) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Get-QemuProcesses {
    try { return @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-aarch64.exe'" -ErrorAction Stop) }
    catch { throw "Unable to verify QEMU ownership through CIM: $($_.Exception.Message)" }
}

function Get-QacHealth([string]$Context) {
    $status = & $QacCliPath status 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $status -notmatch '(?m)^Coordinator: Online\r?$') {
        throw "$Context QAC coordinator is not verifiably online.`n$status"
    }
    $count = [regex]::Match($status, '(?m)^QEMU processes: (\d+) \((\d+) foreign\)\r?$')
    if (!$count.Success) {
        throw "$Context QAC reports an unparseable QEMU state.`n$status"
    }
    $list = & $QacCliPath list 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "$Context unable to inspect QAC reservations.`n$list" }
    return [pscustomobject]@{
        Status = $status.Trim(); List = $list.Trim()
        ProcessCount = [int]$count.Groups[1].Value
        ForeignCount = [int]$count.Groups[2].Value
        RecoveryRequired = $status -match 'Recovery: Required'
    }
}

function Find-QacOwnedGuest([int]$QacProcessId, [int]$ExpectedQmpPort) {
    $processes = Get-QemuProcesses
    $owned = @()
    $qacDescendants = @()
    foreach ($child in $processes) {
        if ([int]$child.ParentProcessId -ne $QacProcessId) { continue }
        $qacDescendants += $child
        $command = [string]$child.CommandLine
        if ([string]::Equals([string]$child.ExecutablePath, $QemuPath, [StringComparison]::OrdinalIgnoreCase) -and
            $command -match [regex]::Escape('virt,gic-version=2,acpi=off') -and
            $command -match [regex]::Escape('cortex-a53') -and
            $command -match [regex]::Escape("tcp:127.0.0.1:$ExpectedQmpPort") -and
            $command -match [regex]::Escape("-name $sessionId") -and
            $command -match [regex]::Escape($varsPath)) {
            $owned += $child
        }
    }
    if ($owned.Count -gt 1) {
        return [pscustomobject]@{ Status = 'Unexpected'; Child = $null; Diagnostic = "QAC process has $($owned.Count) matching QEMU children." }
    }
    if ($owned.Count -eq 1 -and $qacDescendants.Count -eq 1) {
        return [pscustomobject]@{ Status = 'OwnedActive'; Child = $owned[0]; Diagnostic = '' }
    }
    if ($qacDescendants.Count -gt 0) {
        return [pscustomobject]@{ Status = 'Unexpected'; Child = $null; Diagnostic = 'QAC has a direct ARM64 QEMU child whose executable or launch profile does not match this reservation.' }
    }
    $unrelated = @($processes | Where-Object { [int]$_.ParentProcessId -ne $QacProcessId })
    if ($unrelated.Count -gt 0) {
        return [pscustomobject]@{ Status = 'UnrelatedOnly'; Child = $null; Diagnostic = 'Other ARM64 QEMU process(es) exist; none matches this QAC parent and session profile.' }
    }
    return [pscustomobject]@{ Status = 'Pending'; Child = $null; Diagnostic = '' }
}

function Get-QacChildStatus {
    if (!$ownedChild) { return [pscustomobject]@{ Status = $lastChildStatus; Child = $null; Diagnostic = '' } }
    try {
        $current = Get-CimInstance Win32_Process -Filter "ProcessId=$([int]$ownedChild.ProcessId)" -ErrorAction Stop
    } catch {
        return [pscustomobject]@{ Status = 'Ambiguous'; Child = $null; Diagnostic = "Unable to inspect child PID $($ownedChild.ProcessId): $($_.Exception.Message)" }
    }
    if (!$current) {
        if ($qacChildExitVerified) { return [pscustomobject]@{ Status = 'Exited'; Child = $null; Diagnostic = '' } }
        return [pscustomobject]@{ Status = 'Ambiguous'; Child = $null; Diagnostic = 'The recorded child PID is absent without QAC child-exit evidence.' }
    }
    $currentStart = [DateTimeOffset]::new(([DateTime]$current.CreationDate).ToUniversalTime())
    $sameStart = [Math]::Abs(($currentStart - $ownedChildStartUtc).TotalSeconds) -lt 1.0
    $samePath = [string]::Equals([string]$current.ExecutablePath, $QemuPath, [StringComparison]::OrdinalIgnoreCase)
    $currentCommand = [string]$current.CommandLine
    $sameProfile = $currentCommand -match [regex]::Escape("tcp:127.0.0.1:$qmpPort") -and
        $currentCommand -match [regex]::Escape($varsPath) -and
        $currentCommand -match [regex]::Escape('virt,gic-version=2,acpi=off') -and
        $currentCommand -match [regex]::Escape('cortex-a53') -and
        $currentCommand -match [regex]::Escape("-name $sessionId")
    if (!$sameStart -or !$samePath -or !$sameProfile) {
        return [pscustomobject]@{ Status = 'Unexpected'; Child = $null; Diagnostic = 'Recorded PID now identifies a different process.' }
    }
    return [pscustomobject]@{ Status = 'OwnedActive'; Child = $current; Diagnostic = '' }
}

function Read-GuestLog {
    if (!(Test-Path -LiteralPath $guestLogPath -PathType Leaf)) { return '' }
    $stream = [IO.File]::Open($guestLogPath, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Read-SharedText([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Invoke-QacRead([string[]]$Arguments) {
    try {
        $text = & $QacCliPath @Arguments 2>&1 | Out-String
        return [pscustomobject]@{ Success = ($LASTEXITCODE -eq 0); Text = $text.Trim(); ExitCode = $LASTEXITCODE }
    } catch {
        return [pscustomobject]@{ Success = $false; Text = $_.Exception.Message; ExitCode = -1 }
    }
}

function Get-QacReservationSnapshot {
    param([string]$Id)
    $status = Invoke-QacRead @('status')
    $list = Invoke-QacRead @('list')
    $inspect = $null
    $inspectValid = $false
    $state = ''
    $ownership = 'Unknown'
    $diagnostic = ''
    $online = $status.Success -and $status.Text -match '(?m)^Coordinator: Online\r?$'
    if (!$status.Success -or !$list.Success -or !$online) {
        $diagnostic = "status=$($status.Text); list=$($list.Text)"
    }
    if ($Id) {
        $inspect = Invoke-QacRead @('inspect', $Id)
        if ($inspect.Success) {
            try {
                $reservation = $inspect.Text | ConvertFrom-Json
                $ownerPid = [regex]::Match([string]$reservation.owner, '\(PID (\d+)\)')
                $ownerStartMatches = $true
                if ($reservation.ownerProcessStartedAtUtc -and $qacStartUtc) {
                    $ownerStart = [DateTimeOffset]::Parse([string]$reservation.ownerProcessStartedAtUtc).ToUniversalTime()
                    $ownerStartMatches = [Math]::Abs(($ownerStart - $qacStartUtc).TotalSeconds) -lt 2.0
                }
                if ([string]$reservation.id -eq $Id -and
                    [string]$reservation.project -eq $project -and
                    [string]$reservation.mode -eq 'acceptance' -and
                    $ownerPid.Success -and [int]$ownerPid.Groups[1].Value -eq [int]$qac.Id -and
                    $ownerStartMatches) {
                    $ownership = 'Owned'
                    $state = [string]$reservation.state
                    $script:lastReservationObject = $reservation
                    $inspectValid = $true
                } else {
                    $ownership = 'Lost'
                    $diagnostic = 'QAC inspect returned a reservation identity that does not match this QAC owner.'
                }
            } catch {
                $diagnostic = "QAC inspect returned invalid reservation data: $($_.Exception.Message)"
            }
        } elseif ($online -and $qacReleaseMarker) {
            $lineExists = $list.Text -match ('(?m)^\s*' + [regex]::Escape($Id) + '\s+\|')
            if (!$lineExists) {
                $ownership = 'Owned'
                $state = 'Released'
                $diagnostic = 'QAC release marker is present and the reservation is absent from the live list.'
            } else {
                $ownership = 'Lost'
                $diagnostic = "QAC inspect failed while the exact reservation remains listed: $($inspect.Text)"
            }
        } elseif ($online -and $qacChildExitVerified -and $qac.HasExited -and
                  $list.Text -notmatch ('(?m)^\s*' + [regex]::Escape($Id) + '\s+\|')) {
            $ownership = 'Owned'
            $state = 'Released'
            $diagnostic = 'The exact child exit was logged and QAC no longer lists the reservation.'
        } elseif ($online) {
            $ownership = 'Lost'
            $diagnostic = "QAC inspect failed for the known reservation: $($inspect.Text)"
        }
    }
    $script:lastQacStatus = $status.Text
    $script:lastQacList = $list.Text
    return [pscustomobject]@{
        CommunicationOk = [bool]($inspectValid -or ($status.Success -and $list.Success -and $online -and $ownership -in @('Owned','Lost')))
        Online = $online
        Status = $status.Text
        List = $list.Text
        ReservationState = $state
        ReservationOwnership = $ownership
        Diagnostic = $diagnostic
        Reservation = $script:lastReservationObject
    }
}

function Read-QacLifecycleLogs {
    $stdout = Read-SharedText $qacStdoutPath
    $stderr = Read-SharedText $qacStderrPath
    $text = $stdout + "`r`n" + $stderr
    if (!$reservationId) {
        $requested = [regex]::Match($text, 'QAC reservation requested: id=([0-9a-f]{32})')
        if ($requested.Success) {
            $script:reservationId = $requested.Groups[1].Value
            $record.reservation_id = $reservationId
            Add-QacTransition $sessionState 'ReservationRequested' "QAC acceptance reservation $reservationId requested." ([DateTimeOffset]::UtcNow)
        }
    }
    if ($reservationId) {
        $grant = [regex]::Match($text, 'QAC exclusive reservation granted: id=' + [regex]::Escape($reservationId))
        if ($grant.Success -and !$exclusiveGrant) {
            $script:exclusiveGrant = $true
            $record.qac_exclusive_grant = $true
            Add-QacTransition $sessionState 'ExclusiveGrant' "QAC granted acceptance reservation $reservationId." ([DateTimeOffset]::UtcNow)
        }
        $launch = [regex]::Match($text, 'QAC QEMU launched: reservation=' + [regex]::Escape($reservationId) + ' pid=(\d+).*?launch_utc=([^\s]+)')
        if ($launch.Success) {
            $record.qac_qemu_launch_marker = $true
            $record.qac_launch_marker_pid = [int]$launch.Groups[1].Value
            try { $record.qac_reported_launch_utc = [DateTimeOffset]::Parse($launch.Groups[2].Value).ToUniversalTime().ToString('o') } catch { }
        }
        $exit = [regex]::Match($text, 'QAC QEMU exited: reservation=' + [regex]::Escape($reservationId) + ' pid=(\d+) exitCode=(-?\d+) exit_utc=([^\s]+)')
        if ($exit.Success) {
            $exitPid = [int]$exit.Groups[1].Value
            if ($null -ne $record.qemu_child_pid -and $exitPid -eq [int]$record.qemu_child_pid) {
                $script:qacChildExitVerified = $true
                $script:qemuExitCode = [int]$exit.Groups[2].Value
                $script:qacChildExitUtc = $exit.Groups[3].Value
                $record.qac_child_exit_verified = $true
                $record.qemu_exit_code = $qemuExitCode
                $record.qac_child_exit_utc = $qacChildExitUtc
            }
        }
        $script:qacReleaseMarker = $text -match ('QAC reservation released: id=' + [regex]::Escape($reservationId) + '(?:\s|$)')
        $record.qac_reservation_released = $qacReleaseMarker
        $record.qac_child_exit_marker = [bool]$qacChildExitVerified
        if ($qacReleaseMarker) { $record.qac_reservation_release_status = 'ReleasedMarkerObserved' }
    }
    return [pscustomobject]@{ Text = $text; Stdout = $stdout; Stderr = $stderr }
}

function Set-OwnedChild([object]$Child, [string]$LaunchUtcText = '') {
    $script:ownedChild = $Child
    $created = ([DateTime]$Child.CreationDate).ToUniversalTime()
    $script:ownedChildStartUtc = [DateTimeOffset]::new($created)
    if ($LaunchUtcText) {
        try {
            $reported = [DateTimeOffset]::Parse($LaunchUtcText).ToUniversalTime()
            if ([Math]::Abs(($reported - $ownedChildStartUtc).TotalSeconds) -le 2.0) { $script:ownedChildStartUtc = $reported }
        } catch { }
    }
    $script:managedLaunchUtc = $ownedChildStartUtc.ToString('o')
    $record.qemu_child_pid = [int]$Child.ProcessId
    $record.qemu_child_start_utc = $ownedChildStartUtc.ToString('o')
    $record.managed_child_launch_utc = $managedLaunchUtc
    $script:runtimeWatch = [Diagnostics.Stopwatch]::StartNew()
    $age = [Math]::Max(0.0, ([DateTimeOffset]::UtcNow - $ownedChildStartUtc).TotalSeconds)
    $script:runtimeOffsetSeconds = [Math]::Min([double]$RuntimeSeconds, $age)
    $record.runtime_deadline_monotonic_started_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $record.runtime_deadline_utc_estimate = $ownedChildStartUtc.AddSeconds($RuntimeSeconds).ToString('o')
    Add-QacTransition $sessionState 'ManagedChildIdentified' "QEMU PID $($Child.ProcessId), created $($ownedChildStartUtc.ToString('o'))." ([DateTimeOffset]::UtcNow)
}

function Find-AndTrackQacChild {
    if ($ownedChild) { return (Get-QacChildStatus) }
    if (!$qac) { return [pscustomobject]@{ Status = 'Pending'; Child = $null; Diagnostic = '' } }
    $found = Find-QacOwnedGuest ([int]$qac.Id) $qmpPort
    if ($found.Status -eq 'OwnedActive') {
        $logs = Read-QacLifecycleLogs
        $launchMatch = [regex]::Match($logs.Text, 'QAC QEMU launched: reservation=' + [regex]::Escape($reservationId) + ' pid=' + [int]$found.Child.ProcessId + '.*?launch_utc=([^\s]+)')
        Set-OwnedChild $found.Child $(if ($launchMatch.Success) { $launchMatch.Groups[1].Value } else { '' })
        return [pscustomobject]@{ Status = 'OwnedActive'; Child = $found.Child; Diagnostic = '' }
    }
    return $found
}

function Get-RuntimeElapsedSeconds {
    if (!$runtimeWatch) { return 0.0 }
    return ($runtimeOffsetSeconds + $runtimeWatch.Elapsed.TotalSeconds)
}

function Get-FinalQacStateVerified([string]$Id, [string]$Status, [string]$List, [bool]$ReleaseMarker) {
    if ($Status -notmatch '(?m)^Coordinator: Online\r?$') { return $false }
    if (!$Id) { return $false }
    $line = [regex]::Match($List, '(?m)^\s*' + [regex]::Escape($Id) + '\s+\|\s+[^|]+\|\s+[^|]+\|\s+([^|]+)\|')
    if ($line.Success) { return $line.Groups[1].Value.Trim() -eq 'Released' }
    return $ReleaseMarker
}

function Send-Qmp([Net.Sockets.TcpClient]$Client, [IO.StreamReader]$Reader,
                  [IO.StreamWriter]$Writer, [string]$Command, [switch]$GracefulQuit) {
    if ($GracefulQuit) {
        $script:qmpQuitCommandAttempted = $true
        $record.qmp_quit_transmission_status = 'UnverifiedWrite'
        $record.qmp_quit_result = 'TransmissionUnverified'
    }
    $Writer.WriteLine((@{ execute = $Command } | ConvertTo-Json -Compress))
    if ($GracefulQuit) {
        $Writer.Flush()
        $script:qmpQuitSent = $true
        $record.qmp_quit_sent = $true
        $record.qmp_quit_request_transmitted = $true
        $record.qmp_shutdown_requested = $true
        $record.qmp_quit_sent_utc = [DateTimeOffset]::UtcNow.ToString('o')
        $record.qmp_quit_transmission_status = 'WrittenAndFlushed'
        $script:qmpQuitResponseKind = 'AwaitingResponse'
        $record.qmp_quit_result = 'AwaitingResponse'
    }
    while ($true) {
        try { $line = $Reader.ReadLine() }
        catch {
            if ($GracefulQuit -and (Test-QmpReadTimeout $_.Exception)) {
                $script:qmpQuitResponseKind = 'ReadTimeoutAfterTransmission'
                $script:qmpQuitError = $_.Exception.Message
                $record.qmp_quit_result = $script:qmpQuitResponseKind
                $record.qmp_quit_error = $script:qmpQuitError
                $record.qmp_last_error = $_.Exception.Message
                return $null
            }
            throw
        }
        if ($null -eq $line) {
            if ($GracefulQuit) {
                $script:qmpQuitResponseKind = 'PrematureEofAfterTransmission'
                $record.qmp_quit_result = $script:qmpQuitResponseKind
                return $null
            }
            throw "QMP closed before returning $Command"
        }
        $response = $line | ConvertFrom-Json
        if ($response.PSObject.Properties.Name -contains 'event' -and $response.event -eq 'SHUTDOWN') {
            $record.qmp_shutdown_event_observed = $true
            $record.qmp_shutdown_event_reason = [string]$response.data.reason
            $record.qmp_shutdown_event_guest = if ($null -ne $response.data.guest) { [bool]$response.data.guest } else { $null }
            $record.qmp_shutdown_event_timestamp = $response.timestamp
            $record.qmp_shutdown_cause_established = $record.qmp_shutdown_event_reason -eq 'host-qmp-quit'
            continue
        }
        if ($response.error) {
            if ($GracefulQuit) {
                $script:qmpQuitResponseKind = 'ExplicitError'
                $script:qmpQuitError = [string]$response.error.desc
                $record.qmp_quit_result = $script:qmpQuitResponseKind
                $record.qmp_quit_error = $script:qmpQuitError
                return $null
            }
            throw "QMP $Command failed: $($response.error.desc)"
        }
        if ($response.PSObject.Properties.Name -contains 'return') {
            if ($GracefulQuit) {
                $script:qmpQuitAcknowledged = $true
                $script:qmpQuitResponseKind = 'Acknowledged'
            }
            return $response.return
        }
    }
}

function Test-QmpReadTimeout([Exception]$Exception) {
    $current = $Exception
    while ($null -ne $current) {
        if ($current -is [Net.Sockets.SocketException] -and
            $current.SocketErrorCode -eq [Net.Sockets.SocketError]::TimedOut) { return $true }
        if ($current.Message -match '(?i)timed out|timeout|period of time') { return $true }
        $current = $current.InnerException
    }
    return $false
}

function Request-QmpQuit {
    if ($script:qmpQuitCommandAttempted) { return $false }
    if (!$ownedChild) { return $false }
    $childStatus = Get-QacChildStatus
    if ($childStatus.Status -ne 'OwnedActive') { return $false }
    $script:qmpQuitResponseKind = 'ConnectionFailureBeforeTransmission'
    $script:qmpQuitError = ''
    $listeners = @()
    $listenerQueryReliable = $true
    try {
        $listeners = @(Get-NetTCPConnection -State Listen -LocalPort $qmpPort -ErrorAction Stop)
    } catch {
        $listenerQueryReliable = $false
        $record.qmp_listener_query_error = $_.Exception.Message
    }
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $client.ReceiveTimeout = 1500; $client.SendTimeout = 1500
        $client.Connect([Net.IPAddress]::Loopback, $qmpPort)
        $stream = $client.GetStream(); $stream.ReadTimeout = 1500; $stream.WriteTimeout = 1500
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
        $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 4096, $true); $writer.AutoFlush = $true
        $greetingLine = $reader.ReadLine()
        if (!$greetingLine) { throw 'QMP endpoint closed before returning its greeting.' }
        $greeting = $greetingLine | ConvertFrom-Json
        $ownerPids = @($listeners | ForEach-Object { [int]$_.OwningProcess })
        $verification = Get-QacQmpEndpointVerification -ExpectedChildPid ([int]$ownedChild.ProcessId) `
            -ObservedChildPid ([int]$childStatus.Child.ProcessId) -CommandLine ([string]$childStatus.Child.CommandLine) `
            -QmpPort $qmpPort -ListenerQueryReliable $listenerQueryReliable -ListenerOwnerPids $ownerPids `
            -Greeting $greeting -ExpectedQemuName $sessionId -ObservedQemuName ''
        if (!$verification.ReadyForIdentityCheck) {
            $script:qmpQuitResponseKind = 'EndpointIdentityMismatch'
            throw $verification.Diagnostic
        }
        $script:qmpQuitResponseKind = 'FailureBeforeTransmission'
        $null = Send-Qmp $client $reader $writer 'qmp_capabilities'
        $nameResult = Send-Qmp $client $reader $writer 'query-name'
        $observedQemuName = [string]$nameResult.name
        $verification = Get-QacQmpEndpointVerification -ExpectedChildPid ([int]$ownedChild.ProcessId) `
            -ObservedChildPid ([int]$childStatus.Child.ProcessId) -CommandLine ([string]$childStatus.Child.CommandLine) `
            -QmpPort $qmpPort -ListenerQueryReliable $listenerQueryReliable -ListenerOwnerPids $ownerPids `
            -Greeting $greeting -ExpectedQemuName $sessionId -ObservedQemuName $observedQemuName
        if (!$verification.Verified) {
            $script:qmpQuitResponseKind = 'EndpointIdentityMismatch'
            throw $verification.Diagnostic
        }
        $record.qmp_endpoint_verification_method = $verification.Method
        $record.qmp_greeting = $greeting
        $record.qmp_instance_name = $observedQemuName
        if ($verification.Method -eq 'SocketTableAndQmpInstanceName') {
            $record.qmp_listener_pid = [int]$listeners[0].OwningProcess
        } elseif ($verification.Diagnostic) {
            Add-QacTransition $sessionState 'QmpSocketTableFallback' $verification.Diagnostic ([DateTimeOffset]::UtcNow)
        }
        $null = Send-Qmp $client $reader $writer 'quit' -GracefulQuit
        if ($qmpQuitResponseKind -eq 'Acknowledged') {
            $record.qmp_shutdown_acknowledged = $true
            $record.qmp_shutdown_acknowledged_utc = [DateTimeOffset]::UtcNow.ToString('o')
        }
        $record.qmp_quit_response_kind = $qmpQuitResponseKind
        ++$script:qmpAttempts
        $script:lastQmpAttemptUtc = [DateTimeOffset]::UtcNow
        Add-QacTransition $sessionState 'QmpQuitResponse' "QMP quit response status: $qmpQuitResponseKind." ([DateTimeOffset]::UtcNow)
        return [bool]($qmpQuitResponseKind -eq 'Acknowledged')
    } catch {
        ++$script:qmpAttempts
        $script:lastQmpAttemptUtc = [DateTimeOffset]::UtcNow
        $record.qmp_last_error = $_.Exception.Message
        if ($qmpQuitSent -and !$qmpQuitAcknowledged -and $qmpQuitResponseKind -eq 'AwaitingResponse') {
            $script:qmpQuitResponseKind = 'OtherFailureAfterTransmission'
        } elseif (!$qmpQuitCommandAttempted -and $qmpQuitResponseKind -eq 'ConnectionFailureBeforeTransmission') {
            $script:qmpQuitResponseKind = 'ConnectionFailureBeforeTransmission'
        } elseif ($qmpQuitCommandAttempted -and !$qmpQuitSent) {
            $script:qmpQuitResponseKind = 'TransmissionUnverified'
        }
        $record.qmp_quit_result = $qmpQuitResponseKind
        $record.qmp_quit_transmission_status = if ($qmpQuitSent) { 'WrittenAndFlushed' } elseif ($qmpQuitCommandAttempted) { 'UnverifiedWrite' } else { 'NotTransmitted' }
        $record.qmp_quit_request_transmitted = [bool]$qmpQuitSent
        $record.qmp_quit_response_kind = $qmpQuitResponseKind
        $record.qmp_quit_error = if ($qmpQuitError) { $qmpQuitError } else { $_.Exception.Message }
        Add-QacTransition $sessionState 'QmpQuitFailed' $_.Exception.Message ([DateTimeOffset]::UtcNow)
        return $false
    } finally { $client.Dispose() }
}

function Get-RecordField([string]$Line, [string]$Name) {
    $match = [regex]::Match($Line, '(?:^|\s)' + [regex]::Escape($Name) + '=([^\s]+)')
    if (!$match.Success) { return '' }
    return $match.Groups[1].Value
}

function Convert-Hex([string]$Value) {
    if ($Value -notmatch '^0x[0-9a-fA-F]+$') { throw "Invalid hexadecimal guest field: $Value" }
    return [Convert]::ToUInt64($Value.Substring(2), 16)
}

function Assert-NativeGuestEvidence([string]$Text) {
    foreach ($marker in @(
        'AARCH64_EL0_QUALIFICATION_PASS observed-exit=2a cleanup=PASS',
        'AARCH64_EL0_NEGATIVE_KERNEL_DATA_PASS',
        'AARCH64_EL0_NEGATIVE_GICD_PASS',
        'AARCH64_EL0_NEGATIVE_CONTROLS_PASS',
        'exception return: PASS scheduler continuation resumed')) {
        if (!$Text.Contains($marker)) { throw "Guest evidence marker is missing: $marker" }
    }
    if ([regex]::Matches($Text, 'AARCH64_PHASE12_PASS').Count -lt 2) {
        throw 'Guest did not complete both Phase 12 pass checkpoints.'
    }
    if ($Text -match 'AARCH64_EL0_CRITICAL_ISOLATION_FAILURE|AARCH64_EL0_NEGATIVE_.*_FAIL|AARCH64_PHASE12_ERROR|AARCH64_PHASE[0-9]+_ERROR') {
        throw 'Guest emitted an isolation or Phase 12 failure marker.'
    }
    $records = @($Text -split '\r?\n' | Where-Object { $_ -match 'AARCH64_EL0_PROBE_RECORD probe=' })
    $kernelLine = $records | Where-Object { $_ -match 'probe=kernel-data' } | Select-Object -First 1
    $mmioLine = $records | Where-Object { $_ -match 'probe=gicd-mmio' } | Select-Object -First 1
    if (!$kernelLine -or !$mmioLine) { throw 'Separate kernel-data and GICD fault records were not captured.' }

    foreach ($item in @(@{ Line = $kernelLine; Probe = 'kernel-data'; ExpectedAp = 2; ExpectedAttr = 1 },
                        @{ Line = $mmioLine; Probe = 'gicd-mmio'; ExpectedAp = 0; ExpectedAttr = 0 })) {
        $line = [string]$item.Line
        if ($line -notmatch 'state=faulted access=denied-or-not-entered' -or
            $line -notmatch 'kernel-root=restored cleanup=PASS released-data-pages=2 released-table-pages=4') {
            throw "$($item.Probe) record lacks a terminal child fault, root restore, or complete cleanup: $line"
        }
        $esr = Convert-Hex (Get-RecordField $line 'ESR')
        $elr = Convert-Hex (Get-RecordField $line 'ELR')
        $spsr = Convert-Hex (Get-RecordField $line 'SPSR')
        $far = Convert-Hex (Get-RecordField $line 'FAR')
        $target = Convert-Hex (Get-RecordField $line 'target')
        $descriptor = Convert-Hex (Get-RecordField $line 'descriptor')
        $child = Convert-Hex (Get-RecordField $line 'child')
        $owner = Convert-Hex (Get-RecordField $line 'owner')
        if ($target -eq 0 -or $far -ne $target -or $elr -ne [uint64]0x0000400000000010 -or
            $spsr -ne [uint64]0x3c0 -or (($esr -shr 26) -band 0x3f) -ne 0x24 -or
            ($esr -band 0x7f) -ne 0x0f -or $child -eq 0 -or $owner -ne [uint64]0x454c305155414c31 -or
            (($descriptor -shr 6) -band 3) -ne $item.ExpectedAp -or
            (($descriptor -shr 2) -band 7) -ne $item.ExpectedAttr -or
            (($descriptor -shr 10) -band 1) -ne 1 -or
            (($descriptor -shr 53) -band 1) -ne 1 -or
            (($descriptor -shr 54) -band 1) -ne 1) {
            throw "$($item.Probe) record did not prove the expected EL0 read permission fault and privileged mapping: $line"
        }
        if ((Convert-Hex (Get-RecordField $line 'EC')) -ne 0x24 -or
            (Convert-Hex (Get-RecordField $line 'DFSC')) -ne 0x0f -or
            (Convert-Hex (Get-RecordField $line 'WnR')) -ne 0) {
            throw "$($item.Probe) syndrome fields do not identify an EL0 read permission fault: $line"
        }
    }
    if ((Get-RecordField $kernelLine 'child') -eq (Get-RecordField $mmioLine 'child')) {
        throw 'Kernel-data and GICD probes did not use separate child identities.'
    }
    $stats = [regex]::Matches($Text, '(?m)ticks=(\d+) context-switches=(\d+) preemptions=(\d+) vfs-reads=(\d+) vfs-enumerations=(\d+)[^\r\n]*unexpected-irq=(\d+) exceptions=(\d+) last-unexpected-irq=(\d+)')
    if ($stats.Count -eq 0) { throw 'Timer/scheduler continuation statistics are missing.' }
    $last = $stats[$stats.Count - 1]
    if ([int64]$last.Groups[1].Value -le 0 -or [int64]$last.Groups[2].Value -le 0 -or
        [int64]$last.Groups[3].Value -lt 1000 -or [int64]$last.Groups[6].Value -ne 0 -or
        [int64]$last.Groups[8].Value -ne 0) {
        throw 'Timer delivery, scheduler continuation, or unexpected-IRQ health check failed.'
    }
    return [pscustomobject]@{
        KernelData = $kernelLine.Trim(); Gicd = $mmioLine.Trim()
        KernelChildIdentity = Get-RecordField $kernelLine 'child'
        GicdChildIdentity = Get-RecordField $mmioLine 'child'
        TimerTicks = [int64]$last.Groups[1].Value
        ContextSwitches = [int64]$last.Groups[2].Value
        Preemptions = [int64]$last.Groups[3].Value
        UnexpectedIrqs = [int64]$last.Groups[6].Value
        ExceptionCounter = [int64]$last.Groups[7].Value
        LastUnexpectedIrq = [int64]$last.Groups[8].Value
        Phase12PassMarkers = [regex]::Matches($Text, 'AARCH64_PHASE12_PASS').Count
    }
}

$record = [ordered]@{
    outcome = 'EL0_QAC_PREFLIGHT'
    project = $project
    candidate_directory = $CandidateDirectory
    candidate_manifest = $manifestPath
    session_directory = $sessionDirectory
    guest_log = $guestLogPath
    qac_stdout_log = $qacStdoutPath
    qac_stderr_log = $qacStderrPath
    firmware_vars_file = $varsPath
    qac_process_id = $null
    qac_process_start_utc = ''
    qemu_child_pid = $null
    qemu_child_start_utc = ''
    managed_child_launch_utc = ''
    qac_child_exit_utc = ''
    managed_child_lifetime_source = ''
    qemu_exit_code = $null
    qac_exit_code = $null
    reservation_id = ''
    reservation_state = ''
    qac_state_transitions = @()
    qac_snapshot_history = @()
    qmp_port = $null
    qac_exclusive_grant = $false
    qac_qemu_launch_marker = $false
    qac_child_exit_verified = $false
    qac_reservation_released = $false
    qac_reservation_release_status = 'NotObserved'
    qac_final_state_verified = $false
    qac_communication_failures = @()
    qmp_quit_attempts = 0
    qmp_quit_sent = $false
    qmp_quit_request_transmitted = $false
    qmp_quit_sent_utc = ''
    qmp_quit_result = 'NotAttempted'
    qmp_quit_transmission_status = 'NotAttempted'
    qmp_quit_response_kind = 'NotAttempted'
    qmp_quit_error = ''
    qmp_shutdown_requested = $false
    qmp_shutdown_acknowledged = $false
    qmp_shutdown_acknowledged_utc = ''
    qmp_shutdown_event_observed = $false
    qmp_shutdown_event_reason = ''
    qmp_shutdown_event_guest = $null
    qmp_shutdown_event_timestamp = $null
    qmp_shutdown_cause_established = $false
    qmp_listener_pid = $null
    qmp_listener_query_error = ''
    qmp_endpoint_verification_method = ''
    qmp_greeting = $null
    qmp_instance_name = ''
    qemu_instance_name = $sessionId
    runtime_limit_seconds = $RuntimeSeconds
    runtime_deadline_monotonic_started_utc = ''
    runtime_elapsed_seconds = $null
    guest_runtime_seconds = $null
    managed_child_lifetime_seconds = $null
    runtime_deadline_utc_estimate = ''
    child_cleanup_limit_seconds = $ChildCleanupSeconds
    child_cleanup_elapsed_seconds = $null
    child_cleanup_deadline_met = $false
    reservation_cleanup_limit_seconds = $ReservationCleanupSeconds
    reservation_cleanup_elapsed_seconds = $null
    reservation_cleanup_deadline_met = $false
    runtime_timeout_observed = $false
    child_cleanup_deadline_expired = $false
    reservation_cleanup_deadline_expired = $false
    qemu_process_ids_after = @()
    qac_reservation_id_after = ''
    qac_reservation_list_state_after = ''
    qac_manual_release_attempted = $false
    qac_manual_release_result = ''
    final_qac_status = ''
    final_qac_list = ''
    state_machine_phase = $sessionState.phase
    state_machine_action = $sessionState.last_action
    partial_log_finalization_error = ''
    candidate_manifest_sha256 = ''
    candidate_kernel_sha256 = ''
    candidate_efi_sha256 = ''
    candidate_ramdisk_sha256 = ''
    qemu_sha256 = ''
    firmware_code_sha256 = ''
    firmware_vars_template_sha256 = ''
    firmware_vars_initial_sha256 = ''
    qac_status_before = ''
    qac_list_before = ''
    qac_preflight_status = 'NotChecked'
    qac_occupied = $false
    qac_status_after = ''
    qac_list_after = ''
    guest_evidence = $null
    guest_milestones = @()
    guest_log_bytes = 0
    guest_log_last_write_utc = ''
    failure = ''
}

try {
    foreach ($path in @($manifestPath, (Join-Path $CandidateDirectory 'kernel.elf'),
                        (Join-Path $CandidateDirectory 'BOOTAA64.EFI'),
                        (Join-Path $CandidateDirectory 'esp\ramdisk.img'),
                        $QacCliPath, $QemuPath, $FirmwareCode, $FirmwareVarsTemplate)) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required candidate/tool file is missing: $path" }
    }
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.profile -ne 'runsafe2-el0-negative-controls' -or
        $manifest.artifacts.el0NegativeProbes.Count -ne 2 -or
        $manifest.artifacts.el0NegativeProbes[0] -ne 'kernel-data-read' -or
        $manifest.artifacts.el0NegativeProbes[1] -ne 'gic-distributor-read') {
        throw 'Candidate manifest does not identify the preserved RUNSAFE2 kernel-data and GICD probes.'
    }
    foreach ($artifact in @(
        @{ Path = (Join-Path $CandidateDirectory 'kernel.elf'); Hash = $manifest.artifacts.kernelElfSha256 },
        @{ Path = (Join-Path $CandidateDirectory 'BOOTAA64.EFI'); Hash = $manifest.artifacts.bootEfiSha256 },
        @{ Path = (Join-Path $CandidateDirectory 'esp\ramdisk.img'); Hash = $manifest.artifacts.ramdiskSha256 })) {
        if ((Get-Sha256Hex $artifact.Path) -ne $artifact.Hash) { throw "Candidate artifact hash mismatch: $($artifact.Path)" }
    }
    $currentHead = (& git -C $repoRoot rev-parse HEAD).Trim()
    $currentBranch = (& git -C $repoRoot branch --show-current).Trim()
    if ($currentBranch -ne $manifest.server.branch) { throw 'Current branch differs from the candidate branch.' }
    foreach ($source in $manifest.server.worktree.files) {
        $sourcePath = Join-Path $repoRoot ([string]$source.path)
        if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf) -or
            (Get-Sha256Hex $sourcePath) -ne $source.sha256) {
            throw "Candidate source file hash differs from its manifest: $($source.path)"
        }
    }
    $record.candidate_source_head = [string]$manifest.server.head
    $record.current_source_head = $currentHead
    $record.current_source_branch = $currentBranch
    $record.candidate_manifest_sha256 = Get-Sha256Hex $manifestPath
    $record.candidate_kernel_sha256 = Get-Sha256Hex (Join-Path $CandidateDirectory 'kernel.elf')
    $record.candidate_efi_sha256 = Get-Sha256Hex (Join-Path $CandidateDirectory 'BOOTAA64.EFI')
    $record.candidate_ramdisk_sha256 = Get-Sha256Hex (Join-Path $CandidateDirectory 'esp\ramdisk.img')
    $record.qemu_sha256 = Get-Sha256Hex $QemuPath
    $record.firmware_code_sha256 = Get-Sha256Hex $FirmwareCode
    $record.firmware_vars_template_sha256 = Get-Sha256Hex $FirmwareVarsTemplate

    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { $qmpPort = ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
    $record.qmp_port = $qmpPort
    $before = Get-QacHealth 'RUNSAFE2 R2 pre-admission'
    $record.qac_status_before = $before.Status
    $record.qac_list_before = $before.List
    if ($before.RecoveryRequired -or $before.ProcessCount -ne 0 -or $before.ForeignCount -ne 0 -or
        (Get-QemuProcesses).Count -ne 0 -or $before.List -notmatch '(?m)^Reservations: 0\r?$') {
        $record.qac_occupied = [bool]($before.ProcessCount -gt 0 -or $before.ForeignCount -gt 0 -or $before.List -notmatch '(?m)^Reservations: 0\r?$')
        $record.qac_preflight_status = if ($before.RecoveryRequired) { 'RECOVERY_REQUIRED' } elseif ($record.qac_occupied) { 'OCCUPIED' } else { 'UNVERIFIED' }
        $record.outcome = 'EL0_QAC_SUPERVISION_HOSTED_READY'
        $record.failure = 'QAC is occupied or not healthy; no reservation request or guest launch was made.'
        if (!(Save-SessionRecord)) { throw 'QAC preflight was blocked and its session evidence could not be persisted.' }
        throw ('QAC_SLOT_OCCUPIED: QAC preflight=' + $record.qac_preflight_status + '. No guest was started.')
    }
    $record.qac_preflight_status = 'CLEAR'
    Copy-Item -LiteralPath $FirmwareVarsTemplate -Destination $varsPath
    $record.firmware_vars_initial_sha256 = Get-Sha256Hex $varsPath
    if ($record.firmware_vars_initial_sha256 -ne $record.firmware_vars_template_sha256) {
        throw 'The QEMU run variables are not a fresh copy of the configured firmware template.'
    }
    if (!(Save-SessionRecord)) { throw 'Could not persist the preflight session record.' }

    $guestArguments = @(
        '-name',$sessionId,'-machine','virt,gic-version=2,acpi=off','-cpu','cortex-a53','-m','512M',
        '-drive',"if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
        '-drive',"if=pflash,format=raw,unit=1,file=$varsPath",
        '-drive',"file=fat:rw:$(Join-Path $CandidateDirectory 'esp'),format=raw",
        '-display','none','-device','ramfb','-global','virtio-mmio.force-legacy=false',
        '-device','virtio-keyboard-device','-device','virtio-tablet-device',
        '-monitor','none','-serial',"file:$guestLogPath",'-no-reboot',
        '-qmp',"tcp:127.0.0.1:$qmpPort,server=on,wait=off")
    $qacArguments = @('run','--project',$project,'--mode','acceptance','--',$QemuPath) + $guestArguments
    $argumentString = ($qacArguments | ForEach-Object { Quote-ProcessArgument ([string]$_) }) -join ' '
    $qac = Start-Process -FilePath $QacCliPath -ArgumentList $argumentString `
        -RedirectStandardOutput $qacStdoutPath -RedirectStandardError $qacStderrPath `
        -PassThru -WindowStyle Hidden -WorkingDirectory $repoRoot
    $qacStartUtc = [DateTimeOffset]::new((Get-Process -Id $qac.Id).StartTime.ToUniversalTime())
    $record.qac_process_id = [int]$qac.Id
    $record.qac_process_start_utc = $qacStartUtc.ToString('o')
    $record.outcome = 'EL0_QAC_ADMISSION_PENDING'
    $admissionWatch = [Diagnostics.Stopwatch]::StartNew()
    $nextSnapshotWatch = [Diagnostics.Stopwatch]::StartNew()
    $guestFailureMarker = ''
    $cancelAttempted = $false
    $lastSnapshotKey = ''
    $lastKnownSnapshot = $null
    $snapshotAttempted = $false
    $lastStepAction = 'Wait'

    while ($true) {
        $now = [DateTimeOffset]::UtcNow
        $qacLogs = Read-QacLifecycleLogs
        if ($reservationId) { $record.reservation_id = $reservationId }

        $snapshot = [pscustomobject]@{
            CommunicationOk = $true; Online = $true; Status = ''; List = ''
            ReservationState = $(if ($reservationId) { '' } else { 'Waiting' })
            ReservationOwnership = 'Unknown'; Diagnostic = 'Reservation ID has not appeared in QAC output.'; Reservation = $null
        }
        if ($reservationId -and (!$snapshotAttempted -or $nextSnapshotWatch.Elapsed.TotalSeconds -ge 1.0)) {
            $nextSnapshotWatch.Restart()
            $snapshotAttempted = $true
            $snapshot = Get-QacReservationSnapshot $reservationId
            $lastKnownSnapshot = $snapshot
            if ($snapshot.Status) { $record.qac_status_last = $snapshot.Status }
            if ($snapshot.List) { $record.qac_list_last = $snapshot.List }
            if ($snapshot.ReservationState) { $reservationState = $snapshot.ReservationState; $record.reservation_state = $reservationState }
            $snapshotKey = "$($snapshot.CommunicationOk)|$($snapshot.ReservationOwnership)|$($snapshot.ReservationState)|$($snapshot.Diagnostic)"
            if ($snapshotKey -ne $lastSnapshotKey) {
                $record.qac_snapshot_history += [pscustomobject]@{
                    at_utc = $now.ToString('o'); communication_ok = $snapshot.CommunicationOk
                    reservation_ownership = $snapshot.ReservationOwnership; reservation_state = $snapshot.ReservationState
                    diagnostic = $snapshot.Diagnostic
                }
                if ($record.qac_snapshot_history.Count -gt 256) { $record.qac_snapshot_history = @($record.qac_snapshot_history | Select-Object -Last 256) }
                $lastSnapshotKey = $snapshotKey
            }
            if (!$snapshot.CommunicationOk) {
                $record.qac_communication_failures += [pscustomobject]@{ at_utc = $now.ToString('o'); detail = $snapshot.Diagnostic }
                if ($record.qac_communication_failures.Count -gt 128) { $record.qac_communication_failures = @($record.qac_communication_failures | Select-Object -Last 128) }
            }
        } elseif ($reservationId -and $lastKnownSnapshot) {
            $snapshot = $lastKnownSnapshot
        } elseif ($reservationId) {
            $snapshot.CommunicationOk = $false
            $snapshot.ReservationState = ''
            $snapshot.Diagnostic = 'Waiting for the first QAC inspect snapshot.'
        }

        $childObservation = Find-AndTrackQacChild
        $lastChildStatus = $childObservation.Status
        if ($childObservation.Status -eq 'Unexpected' -and !$runnerFailure) {
            $runnerFailure = $childObservation.Diagnostic
            $record.failure = $runnerFailure
        }
        if ($ownedChild) { $record.qemu_child_pid = [int]$ownedChild.ProcessId }
        if ($reservationState -in @('Granted','Starting','Running')) {
            $record.outcome = if ($childObservation.Status -eq 'OwnedActive') { 'EL0_QAC_GUEST_RUNNING' } else { 'EL0_QAC_ADMISSION_GRANTED' }
        }
        $qacLogs = Read-QacLifecycleLogs

        $guestText = ''
        try { $guestText = Read-GuestLog } catch {
            $logFinalizationOk = $false
            $logFinalizationError = "Guest log could not be read during monitoring: $($_.Exception.Message)"
            $record.partial_log_finalization_error = $logFinalizationError
        }
        foreach ($line in ($guestText -split '\r?\n')) {
            $milestone = $line.Trim()
            if ($milestone -and $milestone -match 'AARCH64_|exception return:') {
                $key = $milestone
                if (!$seenGuestMarkers.ContainsKey($key)) {
                    $seenGuestMarkers[$key] = $true
                    $record.guest_milestones += [pscustomobject]@{ observed_utc = $now.ToString('o'); text = $milestone }
                }
            }
        }
        try {
            $guestItem = Get-Item -LiteralPath $guestLogPath -ErrorAction Stop
            $record.guest_log_bytes = [long]$guestItem.Length
            $record.guest_log_last_write_utc = ([DateTimeOffset]::new($guestItem.LastWriteTimeUtc)).ToString('o')
        } catch { }
        if ($guestText -match 'AARCH64_EL0_CRITICAL_ISOLATION_FAILURE') {
            $record.critical_isolation_failure = $true
            $guestFailureMarker = 'EL0 unexpectedly read a protected kernel or GIC Distributor address.'
        } elseif ($guestText -match 'AARCH64_EL0_NEGATIVE_.*_FAIL|AARCH64_PHASE[0-9]+_ERROR') {
            $guestFailureMarker = 'Guest emitted a failure marker before completing the EL0 qualification.'
        }
        try {
            $guestEvidence = Assert-NativeGuestEvidence $guestText
            $guestPassed = $true
            $record.guest_evidence = $guestEvidence
            $record.guest_evidence_completed_utc = $now.ToString('o')
        } catch { }

        $runtimeElapsed = Get-RuntimeElapsedSeconds
        $runtimeExpired = $runtimeWatch -and $runtimeElapsed -ge $RuntimeSeconds
        if ($runtimeWatch) { $record.runtime_elapsed_seconds = [Math]::Round($runtimeElapsed, 3) }
        if ($runtimeExpired) { $record.runtime_timeout_observed = $true }

        $childNow = if ($ownedChild) { Get-QacChildStatus } else { $childObservation }
        $lastChildStatus = $childNow.Status
        if ($childNow.Status -eq 'Exited' -and !$releaseWatch) { $releaseWatch = [Diagnostics.Stopwatch]::StartNew() }
        $qacExited = $qac.HasExited
        $qacExitCodeNow = if ($qacExited) { [int]$qac.ExitCode } else { $null }
        $stopRequested = [bool]($guestPassed -or $runtimeExpired -or $guestFailureMarker)
        if ($cleanupWatch) { $record.child_cleanup_elapsed_seconds = [Math]::Round($cleanupWatch.Elapsed.TotalSeconds, 3) }
        if ($releaseWatch) { $record.reservation_cleanup_elapsed_seconds = [Math]::Round($releaseWatch.Elapsed.TotalSeconds, 3) }
        $record.child_cleanup_deadline_met = [bool]($cleanupWatch -and $qacChildExitVerified -and
            $childNow.Status -eq 'Exited' -and !$record.child_cleanup_deadline_expired -and
            $cleanupWatch.Elapsed.TotalSeconds -le $ChildCleanupSeconds)
        $record.reservation_cleanup_deadline_met = [bool]($releaseWatch -and
            ($qacReleaseMarker -or $snapshot.ReservationState -eq 'Released') -and
            !$record.reservation_cleanup_deadline_expired -and
            $releaseWatch.Elapsed.TotalSeconds -le $ReservationCleanupSeconds)
        $observation = [ordered]@{
            NowUtc = $now
            QacCommunicationOk = [bool]$snapshot.CommunicationOk
            QacDiagnostic = [string]$snapshot.Diagnostic
            ReservationOwnership = [string]$snapshot.ReservationOwnership
            ReservationState = [string]$snapshot.ReservationState
            ChildStatus = [string]$childNow.Status
            ChildPid = if ($ownedChild) { [int]$ownedChild.ProcessId } else { 0 }
            ChildStartUtc = if ($ownedChildStartUtc) { $ownedChildStartUtc.ToString('o') } else { '' }
            GuestPass = [bool]$guestPassed
            StopRequested = $stopRequested
            RuntimeExpired = [bool]$runtimeExpired
            CleanupExpired = $false
            ReleaseDeadlineExpired = $false
            QacChildExitVerified = [bool]$qacChildExitVerified
            ManagedChildExitCode = if ($qacChildExitVerified) { $qemuExitCode } else { $null }
            QacReleaseVerified = [bool]($qacReleaseMarker -or $snapshot.ReservationState -eq 'Released')
            QacProcessExited = [bool]$qacExited
            QacExitCode = $qacExitCodeNow
            QmpShutdownRequested = [bool]$qmpQuitSent
            QmpShutdownRequestTransmitted = [bool]$qmpQuitSent
            QmpShutdownAcknowledged = [bool]$qmpQuitAcknowledged
            QmpShutdownResponseKind = [string]$qmpQuitResponseKind
            QmpShutdownError = [string]$qmpQuitError
            QmpShutdownEventReason = [string]$record.qmp_shutdown_event_reason
            ChildCleanupDeadlineMet = [bool]$record.child_cleanup_deadline_met
            ReservationCleanupDeadlineMet = [bool]$record.reservation_cleanup_deadline_met
            FinalNoOwnedChild = [bool]($childNow.Status -eq 'Exited')
            FinalQacStateVerified = $false
            LogFinalizationOk = [bool]$logFinalizationOk
        }
        $lastStepAction = Step-QacSupervisor -Session $sessionState -Observation $observation
        $record.state_machine_phase = $sessionState.phase
        $record.state_machine_action = $lastStepAction
        $record.reservation_state = $sessionState.reservation_state
        if ($guestFailureMarker) {
            $record.failure = $guestFailureMarker
            if ([string]::IsNullOrWhiteSpace($runnerFailure)) { $runnerFailure = $guestFailureMarker }
        }
        if ($lastStepAction -eq 'RequestGracefulStop') {
            if (!$cleanupWatch) {
                $cleanupWatch = [Diagnostics.Stopwatch]::StartNew()
                $record.guest_runtime_seconds = [Math]::Round($runtimeElapsed, 3)
                Add-QacTransition $sessionState 'ChildCleanupStarted' "Bounded child cleanup started; limit=$ChildCleanupSeconds seconds." $now
            }
            if (!$qmpQuitCommandAttempted -and
                ($lastQmpAttemptUtc -eq [DateTimeOffset]::MinValue -or ($now - $lastQmpAttemptUtc).TotalSeconds -ge 2.0)) {
                $null = Request-QmpQuit
                $record.qmp_quit_attempts = $qmpAttempts
                $record.qmp_quit_sent = $qmpQuitSent
            }
        }
        if ($stopRequested -and $childNow.Status -in @('Pending','Ambiguous','Unexpected','UnrelatedOnly') -and !$cleanupWatch) {
            $cleanupWatch = [Diagnostics.Stopwatch]::StartNew()
            $record.guest_runtime_seconds = [Math]::Round($runtimeElapsed, 3)
            Add-QacTransition $sessionState 'UnverifiedChildCleanupStarted' 'A stop condition exists, but no exact active child identity is currently available.' $now
        }
        if ($reservationId -and $qacExited -and $snapshot.Online -and
            $snapshot.List -notmatch ('(?m)^\s*' + [regex]::Escape($reservationId) + '\s+\|') -and
            $childNow.Status -eq 'Exited') {
            $observation.FinalQacStateVerified = $true
            $observation.FinalNoOwnedChild = $true
            $observation.LogFinalizationOk = [bool]$logFinalizationOk
            $observation.ChildCleanupDeadlineMet = [bool]$record.child_cleanup_deadline_met
            $observation.ReservationCleanupDeadlineMet = [bool]$record.reservation_cleanup_deadline_met
            $lastStepAction = Step-QacSupervisor -Session $sessionState -Observation $observation
        }
        if (!(Save-SessionRecord)) { $logFinalizationOk = $false }

        if ($sessionState.qualifying) {
            $record.outcome = 'QAC_BOUNDED_LIFECYCLE_VERIFIED'
            break
        }
        if ($runtimeWatch -and $runtimeExpired -and !$cleanupWatch) {
            $record.failure = "Guest exceeded the $RuntimeSeconds-second managed runtime budget."
        }
        if ($cleanupWatch -and $cleanupWatch.Elapsed.TotalSeconds -ge $ChildCleanupSeconds -and $childNow.Status -ne 'Exited') {
            $record.child_cleanup_deadline_expired = $true
            $record.failure = if ($childNow.Status -eq 'OwnedActive') { 'Child did not exit by the finite cleanup deadline; QAC ownership remains held.' } else { 'The child identity or exit could not be verified before the finite cleanup deadline; QAC ownership is retained.' }
            if ($childNow.Status -eq 'OwnedActive') { $null = Step-QacSupervisor -Session $sessionState -Observation ([ordered]@{
                NowUtc = [DateTimeOffset]::UtcNow; QacCommunicationOk = [bool]$snapshot.CommunicationOk
                QacDiagnostic = [string]$snapshot.Diagnostic; ReservationOwnership = [string]$snapshot.ReservationOwnership
                ReservationState = [string]$snapshot.ReservationState; ChildStatus = 'OwnedActive'
                ChildPid = [int]$ownedChild.ProcessId; ChildStartUtc = $ownedChildStartUtc.ToString('o')
                GuestPass = [bool]$guestPassed; StopRequested = $true; RuntimeExpired = [bool]$runtimeExpired
                CleanupExpired = $true; ReleaseDeadlineExpired = $false; QacChildExitVerified = $false
                QacReleaseVerified = [bool]$qacReleaseMarker; QacProcessExited = [bool]$qacExited
                QacExitCode = $qacExitCodeNow; QmpShutdownRequested = [bool]$qmpQuitSent
                QmpShutdownAcknowledged = [bool]$qmpQuitAcknowledged
                FinalNoOwnedChild = $false; FinalQacStateVerified = $false
                LogFinalizationOk = [bool]$logFinalizationOk
            }) } else { Add-QacTransition $sessionState 'ChildCleanupUnverified' $record.failure ([DateTimeOffset]::UtcNow) }
            break
        }
        if ($releaseWatch -and $qacExited -and $qacChildExitVerified -and !$qacReleaseMarker -and
            !$qacManualReleaseAttempted -and $releaseWatch.Elapsed.TotalSeconds -ge [Math]::Max(1, $ReservationCleanupSeconds - 3) -and
            $snapshot.ReservationState -in @('Granted','Running','Recovery Required') -and $childNow.Status -eq 'Exited') {
            $qacManualReleaseAttempted = $true
            $record.qac_manual_release_attempted = $true
            $releaseResult = Invoke-QacRead @('release', $reservationId)
            $record.qac_manual_release_result = $releaseResult.Text
            try {
                $releasedInfo = $releaseResult.Text | ConvertFrom-Json
                if ($releaseResult.Success -and $releasedInfo.state -eq 'Released') {
                    $record.qac_reservation_release_status = 'ManualQacReleaseVerifiedAfterChildExit'
                    Add-QacTransition $sessionState 'QacReleaseRequestedAfterVerifiedExit' 'QAC release was requested after the exact managed child exit was verified.' ([DateTimeOffset]::UtcNow)
                    $snapshot = Get-QacReservationSnapshot $reservationId
                }
            } catch { }
        }
        if ($releaseWatch -and $releaseWatch.Elapsed.TotalSeconds -ge $ReservationCleanupSeconds -and !$sessionState.reservation_release_verified) {
            $record.reservation_cleanup_deadline_expired = $true
            $record.qac_reservation_release_status = 'ReleaseDeadlineExpired'
            $record.failure = 'QAC did not verify reservation release within the finite release deadline.'
            $null = Step-QacSupervisor -Session $sessionState -Observation ([ordered]@{
                NowUtc = [DateTimeOffset]::UtcNow; QacCommunicationOk = [bool]$snapshot.CommunicationOk
                QacDiagnostic = [string]$snapshot.Diagnostic; ReservationOwnership = [string]$snapshot.ReservationOwnership
                ReservationState = [string]$snapshot.ReservationState; ChildStatus = 'Exited'
                ChildPid = [int]$record.qemu_child_pid; ChildStartUtc = [string]$record.qemu_child_start_utc
                GuestPass = [bool]$guestPassed; StopRequested = $true; RuntimeExpired = [bool]$runtimeExpired
                CleanupExpired = $false; ReleaseDeadlineExpired = $true; QacChildExitVerified = [bool]$qacChildExitVerified
                QacReleaseVerified = [bool]$qacReleaseMarker; QacProcessExited = [bool]$qacExited
                QacExitCode = $qacExitCodeNow; QmpShutdownRequested = [bool]$qmpQuitSent
                QmpShutdownAcknowledged = [bool]$qmpQuitAcknowledged
                FinalNoOwnedChild = $true; FinalQacStateVerified = $false
                LogFinalizationOk = [bool]$logFinalizationOk
            })
            break
        }
        if ($qacExited -and $childNow.Status -ne 'OwnedActive') {
            if (!$qacChildExitVerified -and $record.qemu_child_pid) {
                $record.failure = 'QAC exited without a matching managed-child exit marker.'
            }
            if ($snapshot.ReservationState -in @('Released','Cancelled','Expired','Failed') -or !$reservationId -or $sessionState.reservation_release_verified) { break }
        }
        if (!$ownedChild -and $admissionWatch.Elapsed.TotalSeconds -ge $AdmissionTimeoutSeconds) {
            $record.failure = "No managed child was launched within the $AdmissionTimeoutSeconds-second admission window."
            if ($reservationId -and $snapshot.ReservationState -in @('Waiting','Blocked') -and !$cancelAttempted) {
                $cancelAttempted = $true
                $cancelResult = Invoke-QacRead @('cancel', $reservationId)
                $record.qac_cancel_result = $cancelResult.Text
                Add-QacTransition $sessionState 'PendingReservationCancelRequested' $cancelResult.Text $now
                $releaseWatch = [Diagnostics.Stopwatch]::StartNew()
            } elseif (!$reservationId -or $snapshot.ReservationState -notin @('Waiting','Blocked')) {
                $record.failure += ' The reservation was not pending, so no cancellation was attempted.'
                break
            }
        }
        Start-Sleep -Milliseconds 250
    }
}
catch {
    $runnerFailure = $_.Exception.Message
    $record.failure = $runnerFailure
    if ($runnerFailure -match '^QAC_SLOT_OCCUPIED:') {
        $record.outcome = 'EL0_QAC_SUPERVISION_HOSTED_READY'
    } elseif ($runnerFailure -match 'EL0 unexpectedly read a protected') {
        $record.outcome = 'QAC_SUPERVISION_DEFECT_LOCALIZED'
    } elseif (!$exclusiveGrant) {
        $record.outcome = 'EL0_QAC_ADMISSION_PENDING'
    } else {
        $record.outcome = 'EL0_QAC_SUPERVISION_INCOMPLETE'
    }
}
finally {
    if ($qac) {
        try {
            $null = Read-QacLifecycleLogs
            if ($qac.HasExited) {
                $qacExitVerified = $true
                $record.qac_exit_code = [int]$qac.ExitCode
                $sessionState.qac_exit_verified = $true
                $sessionState.qac_exit_code = [int]$qac.ExitCode
            }
        } catch { $record.partial_log_finalization_error = $_.Exception.Message; $logFinalizationOk = $false }
    }

    if ($qac -and $ownedChild) {
        $childNow = Get-QacChildStatus
        if ($childNow.Status -eq 'OwnedActive' -and !$cleanupWatch) {
            $cleanupWatch = [Diagnostics.Stopwatch]::StartNew()
            if ($runtimeWatch) { $record.guest_runtime_seconds = [Math]::Round((Get-RuntimeElapsedSeconds), 3) }
            Add-QacTransition $sessionState 'FinalizerCleanupStarted' 'Monitor exited abnormally; QAC-owned child cleanup remains supervised by bounded finalization.' ([DateTimeOffset]::UtcNow)
        }
        while ($childNow.Status -eq 'OwnedActive' -and $cleanupWatch -and $cleanupWatch.Elapsed.TotalSeconds -lt $ChildCleanupSeconds) {
            if (!$qmpQuitCommandAttempted -and
                ($lastQmpAttemptUtc -eq [DateTimeOffset]::MinValue -or ([DateTimeOffset]::UtcNow - $lastQmpAttemptUtc).TotalSeconds -ge 2.0)) { $null = Request-QmpQuit }
            Start-Sleep -Milliseconds 250
            $null = Read-QacLifecycleLogs
            $childNow = Get-QacChildStatus
        }
        $record.qmp_quit_attempts = $qmpAttempts
        $record.qmp_quit_sent = $qmpQuitSent
        if ($childNow.Status -eq 'OwnedActive') {
            $record.child_cleanup_deadline_expired = $true
            $record.failure = 'QAC remains the owner of a live child after the bounded cleanup deadline; no unowned process termination was attempted.'
        }
    }

    if ($qac -and $ownedChild -and $qacChildExitVerified -and !$releaseWatch) { $releaseWatch = [Diagnostics.Stopwatch]::StartNew() }
    if ($qac -and $qacChildExitVerified -and !$qac.HasExited -and $releaseWatch) {
        $releaseDeadline = [Math]::Max(0.0, $ReservationCleanupSeconds - $releaseWatch.Elapsed.TotalSeconds)
        if ($releaseDeadline -gt 0) { $null = $qac.WaitForExit([int]([Math]::Min(1000.0, $releaseDeadline * 1000.0))) }
    }

    if ($qac -and $qac.HasExited) {
        $qacExitVerified = $true
        $record.qac_exit_code = [int]$qac.ExitCode
        $record.qac_process_exit_verified = $true
        $null = Read-QacLifecycleLogs
        $record.qac_child_exit_verified = $qacChildExitVerified
        $record.qemu_exit_code = $qemuExitCode
        $record.qac_child_exit_utc = $qacChildExitUtc
    } elseif ($qac) {
        $record.qac_process_exit_verified = $false
        $record.final_evidence_status = 'QAC run process remains active and retains the reservation; do not start another guest.'
    }

    if ($reservationId) {
        $finalSnapshot = Get-QacReservationSnapshot $reservationId
        $record.final_qac_status = $finalSnapshot.Status
        $record.final_qac_list = $finalSnapshot.List
        $record.qac_status_after = $finalSnapshot.Status
        $record.qac_list_after = $finalSnapshot.List
        $finalChild = if ($ownedChild) { Get-QacChildStatus } else { [pscustomobject]@{ Status = 'Pending'; Child = $null; Diagnostic = '' } }
        $record.qemu_process_ids_after = @((Get-QemuProcesses | Where-Object { [int]$_.ProcessId -eq [int]$record.qemu_child_pid } | ForEach-Object { [int]$_.ProcessId }))
        $listedReservation = [regex]::Match($finalSnapshot.List, '(?m)^\s*' + [regex]::Escape($reservationId) + '\s+\|\s+[^|]+\|\s+[^|]+\|\s+([^|]+)\|')
        $record.qac_reservation_list_state_after = if ($listedReservation.Success) { $listedReservation.Groups[1].Value.Trim() } else { '' }
        $record.qac_reservation_id_after = if ($listedReservation.Success -and $record.qac_reservation_list_state_after -notin @('Released','Cancelled','Expired','Failed')) { $reservationId } else { '' }
        $record.qac_reservation_released_in_status = [bool]($finalSnapshot.ReservationState -eq 'Released' -or $record.qac_reservation_list_state_after -eq 'Released' -or ($qacReleaseMarker -and !$record.qac_reservation_id_after))
        $record.qac_final_state_verified = [bool]((Get-FinalQacStateVerified $reservationId $finalSnapshot.Status $finalSnapshot.List $qacReleaseMarker) -and $finalChild.Status -eq 'Exited')
        if ($record.qac_reservation_released_in_status -and $record.qac_reservation_release_status -notmatch '^ManualQacReleaseVerified') { $record.qac_reservation_release_status = 'ReleasedVerified' }
        elseif ($record.qac_reservation_id_after) { $record.qac_reservation_release_status = "StillPresent:$($finalSnapshot.ReservationState)" }
        else { $record.qac_reservation_release_status = 'Unverified' }

        if ($cleanupWatch) { $record.child_cleanup_elapsed_seconds = [Math]::Round($cleanupWatch.Elapsed.TotalSeconds, 3) }
        if ($releaseWatch) { $record.reservation_cleanup_elapsed_seconds = [Math]::Round($releaseWatch.Elapsed.TotalSeconds, 3) }
        $record.child_cleanup_deadline_met = [bool]($cleanupWatch -and $qacChildExitVerified -and
            $finalChild.Status -eq 'Exited' -and !$record.child_cleanup_deadline_expired -and
            $cleanupWatch.Elapsed.TotalSeconds -le $ChildCleanupSeconds)
        $record.reservation_cleanup_deadline_met = [bool]($releaseWatch -and $record.qac_reservation_released_in_status -and
            !$record.reservation_cleanup_deadline_expired -and
            $releaseWatch.Elapsed.TotalSeconds -le $ReservationCleanupSeconds)

        $finalObservation = [ordered]@{
            NowUtc = [DateTimeOffset]::UtcNow; QacCommunicationOk = [bool]$finalSnapshot.CommunicationOk
            QacDiagnostic = [string]$finalSnapshot.Diagnostic
            ReservationOwnership = [string]$finalSnapshot.ReservationOwnership
            ReservationState = if ($finalSnapshot.ReservationState) { [string]$finalSnapshot.ReservationState } elseif ($qacReleaseMarker -and !$record.qac_reservation_id_after) { 'Released' } else { '' }
            ChildStatus = if ($finalChild.Status -eq 'Exited') { 'Exited' } elseif ($finalChild.Status -eq 'OwnedActive') { 'OwnedActive' } else { 'Ambiguous' }
            ChildPid = if ($ownedChild) { [int]$ownedChild.ProcessId } else { 0 }
            ChildStartUtc = if ($ownedChildStartUtc) { $ownedChildStartUtc.ToString('o') } else { '' }
            GuestPass = [bool]$guestPassed; StopRequested = [bool]($guestPassed -or $record.runtime_timeout_observed)
            RuntimeExpired = [bool]$record.runtime_timeout_observed; CleanupExpired = [bool]$record.child_cleanup_deadline_expired
            ReleaseDeadlineExpired = [bool]$record.reservation_cleanup_deadline_expired
            QacChildExitVerified = [bool]$qacChildExitVerified
            ManagedChildExitCode = if ($qacChildExitVerified) { $qemuExitCode } else { $null }
            QacReleaseVerified = [bool]$record.qac_reservation_released_in_status
            QacProcessExited = [bool]$qacExitVerified; QacExitCode = $record.qac_exit_code
            QmpShutdownRequested = [bool]$qmpQuitSent
            QmpShutdownRequestTransmitted = [bool]$qmpQuitSent
            QmpShutdownAcknowledged = [bool]$qmpQuitAcknowledged
            QmpShutdownResponseKind = [string]$qmpQuitResponseKind
            QmpShutdownError = [string]$qmpQuitError
            QmpShutdownEventReason = [string]$record.qmp_shutdown_event_reason
            ChildCleanupDeadlineMet = [bool]$record.child_cleanup_deadline_met
            ReservationCleanupDeadlineMet = [bool]$record.reservation_cleanup_deadline_met
            FinalNoOwnedChild = [bool]($finalChild.Status -eq 'Exited')
            FinalQacStateVerified = [bool]$record.qac_final_state_verified
            LogFinalizationOk = [bool]$logFinalizationOk
        }
        $lastStepAction = Step-QacSupervisor -Session $sessionState -Observation $finalObservation
        $record.state_machine_phase = $sessionState.phase
        $record.state_machine_action = $lastStepAction
        $record.qac_state_transitions = @($sessionState.transitions.ToArray())
        if ($sessionState.qualifying) {
            $record.outcome = 'QAC_BOUNDED_LIFECYCLE_VERIFIED'
            $record.failure = ''
        } elseif ($record.outcome -notmatch 'DEFECT|PENDING|BLOCKED') {
            if ($record.child_cleanup_deadline_expired -or $record.reservation_cleanup_deadline_expired) { $record.outcome = 'QAC_SUPERVISION_DEFECT_LOCALIZED' }
            elseif (Test-QacSupervisorPartial -Session $sessionState -GuestPass ([bool]$guestPassed) `
                    -FinalQacStateVerified ([bool]$record.qac_final_state_verified) `
                    -RuntimeExpired ([bool]$record.runtime_timeout_observed) `
                    -ChildCleanupExpired ([bool]$record.child_cleanup_deadline_expired) `
                    -ReservationCleanupExpired ([bool]$record.reservation_cleanup_deadline_expired)) {
                $record.outcome = 'EL0_QAC_SUPERVISION_PARTIAL'
                $record.failure = 'The managed child exited and QAC released the reservation within bounds, but the QMP quit request transmission was not established.'
            }
            elseif ($record.runtime_timeout_observed) { $record.outcome = 'EL0_QAC_RUNTIME_TIMEOUT_CLEANED' }
            elseif (!$exclusiveGrant) { $record.outcome = 'EL0_QAC_SUPERVISION_HOSTED_READY' }
            else { $record.outcome = 'EL0_QAC_SUPERVISION_INCOMPLETE' }
        }
    }

    if ($runtimeWatch) {
        if ($ownedChildStartUtc -and $qacChildExitUtc) {
            $record.managed_child_lifetime_seconds = Get-QacManagedChildLifetimeSeconds `
                -ChildStartUtc $ownedChildStartUtc.ToString('o') -ChildExitUtc $qacChildExitUtc
            $record.managed_child_lifetime_source = 'ExactQacChildStartAndExitUtc'
        } else {
            $record.managed_child_lifetime_seconds = $null
            $record.managed_child_lifetime_source = 'UnavailableWithoutVerifiedQacChildExitUtc'
        }
        if ($null -eq $record.guest_runtime_seconds) {
            $record.guest_runtime_seconds = [Math]::Round((Get-RuntimeElapsedSeconds), 3)
        }
        $record.runtime_elapsed_seconds = $record.guest_runtime_seconds
    }
    if ($cleanupWatch) { $record.child_cleanup_elapsed_seconds = [Math]::Round($cleanupWatch.Elapsed.TotalSeconds, 3) }
    if ($releaseWatch) { $record.reservation_cleanup_elapsed_seconds = [Math]::Round($releaseWatch.Elapsed.TotalSeconds, 3) }
    $record.child_cleanup_deadline_met = [bool]($sessionState.child_cleanup_deadline_met)
    $record.reservation_cleanup_deadline_met = [bool]($sessionState.reservation_cleanup_deadline_met)
    $record.qmp_quit_attempts = $qmpAttempts
    $record.qmp_quit_sent = $qmpQuitSent
    $record.qmp_quit_request_transmitted = $qmpQuitSent
    $record.qmp_shutdown_requested = $qmpQuitSent
    $record.qmp_shutdown_acknowledged = $qmpQuitAcknowledged
    $record.qmp_quit_response_kind = $qmpQuitResponseKind
    if ($qmpQuitError) { $record.qmp_quit_error = $qmpQuitError }
    $record.qac_process_exit_verified = $qacExitVerified
    if (!$record.qac_child_exit_verified -and $qacChildExitVerified) { $record.qac_child_exit_verified = $true }
    $record.partial_log_finalization_error = $logFinalizationError
    if (!(Save-SessionRecord)) { $record.partial_log_finalization_error = $logFinalizationError }
}

if ($record.outcome -ne 'QAC_BOUNDED_LIFECYCLE_VERIFIED') {
    throw "RUNSAFE2 QAC supervision did not qualify ($($record.outcome)): $($record.failure) See $sessionPath"
}
Write-Host "RUNSAFE2 QAC supervision outcome: $($record.outcome)" -ForegroundColor Green
Write-Host "Reservation: $reservationId; QEMU child: $($record.qemu_child_pid); runtime: $($record.runtime_elapsed_seconds)s; cleanup: $($record.child_cleanup_elapsed_seconds)s; evidence: $sessionPath" -ForegroundColor Cyan
