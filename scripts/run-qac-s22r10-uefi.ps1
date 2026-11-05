[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EspPath,
    [Parameter(Mandatory)][string]$IsoPath,
    [Parameter(Mandatory)][string]$OvmfPath,
    [string]$RunId = 's22r10-pacman-boot2',
    [string]$OutputRoot,
    [string]$QacCliPath,
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-x86_64.exe',
    [string]$ExpectedEspSha256 = '5ffa5777faf76e10633bae441e61e81989d45ffb660aff42feff49674060efe7',
    [string]$ExpectedIsoSha256 = 'e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b',
    [string]$ExpectedOvmfSha256 = '2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792',
    [ValidateRange(1,86400)][int]$AdmissionTimeoutSeconds = 1800,
    [ValidateRange(0,86400)][int]$GuestRuntimeTimeoutSeconds = 0,
    [ValidateRange(5,120)][int]$QmpStartupTimeoutSeconds = 15,
    [ValidateRange(1,30)][int]$QmpConnectTimeoutSeconds = 5,
    [ValidateRange(1,120)][int]$ShutdownTimeoutSeconds = 30,
    [ValidateRange(1,10)][int]$MaxQmpPortAttempts = 5,
    [string]$CancelFilePath
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$isolationModule = Join-Path $PSScriptRoot 'qac-uefi-artifacts.psm1'
$launchModule = Join-Path $PSScriptRoot 'qac-s22r10-uefi.psm1'
Import-Module $isolationModule -Force -ErrorAction Stop
Import-Module $launchModule -Force -ErrorAction Stop

function Write-AtomicJson {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$Value)
    $temporary = "$Path.$PID.tmp"
    $json = ConvertTo-Json -InputObject $Value -Depth 24
    [IO.File]::WriteAllText($temporary, $json + "`n", (New-Object Text.UTF8Encoding($false)))
    [IO.File]::Move($temporary, $Path, $true)
}

function Write-ExecutionRecord {
    Write-AtomicJson -Path $script:ExecutionPath -Value $script:Execution
}

function Add-ExecutionEvent {
    param([Parameter(Mandatory)][string]$Type, [hashtable]$Data = @{})
    $entry = [ordered]@{ timestampUtc=[DateTimeOffset]::UtcNow.ToString('O'); type=$Type; data=$Data }
    $script:Execution.events.Add($entry)
    [IO.File]::AppendAllText($script:EventLogPath, (ConvertTo-Json -InputObject $entry -Compress -Depth 16) + "`n", (New-Object Text.UTF8Encoding($false)))
    Write-ExecutionRecord
}

function Set-ExecutionState {
    param([Parameter(Mandatory)][string]$State, [hashtable]$Data = @{})
    $script:Execution.state = $State
    Add-ExecutionEvent -Type 'state' -Data (@{state=$State} + $Data)
}

function Set-ManifestExecution {
    param(
        [Parameter(Mandatory)][ValidateSet('not-started','active','released','unknown')][string]$Status,
        [string]$ReservationId,
        [int]$ChildProcessId = 0,
        [bool]$ChildExitVerified = $false
    )
    $manifest = Get-Content -LiteralPath $script:ManifestPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $manifest.qacExecution.status = $Status
    $manifest.qacExecution.reservationId = if ($ReservationId) { $ReservationId } else { $null }
    $manifest.qacExecution.childProcessId = if ($ChildProcessId -gt 0) { $ChildProcessId } else { $null }
    $manifest.qacExecution.childExitVerified = $ChildExitVerified
    Write-AtomicJson -Path $script:ManifestPath -Value $manifest
}

function Resolve-QacCli {
    param([string]$ExplicitPath)
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) { $candidate = $ExplicitPath }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GUIDEXOS_QAC_CLI)) { $candidate = $env:GUIDEXOS_QAC_CLI }
    else {
        $command = Get-Command qac.exe -ErrorAction SilentlyContinue
        if ($command) { $candidate = $command.Source }
        else {
            $candidate = Join-Path (Split-Path -Parent $repoRoot) 'guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
        }
    }
    $resolved = (Resolve-Path -LiteralPath $candidate -ErrorAction Stop).Path
    if ([IO.Path]::GetFileName($resolved) -notmatch '^GuideXOS\.QAC\.Cli\.exe$|^qac\.exe$') { throw "QAC CLI must be a qac.exe or GuideXOS.QAC.Cli.exe apphost: $resolved" }
    return $resolved
}

function Invoke-QacCliText {
    param([Parameter(Mandatory)][string[]]$Arguments, [int]$TimeoutSeconds = 15)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $script:QacCli
    $start.WorkingDirectory = $repoRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add([string]$argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "Could not start QAC CLI: $($Arguments -join ' ')" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "QAC CLI command timed out: $($Arguments -join ' ')"
    }
    return [PSCustomObject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdoutTask.GetAwaiter().GetResult()
        Stderr = $stderrTask.GetAwaiter().GetResult()
        ProcessId = $process.Id
    }
}

function Get-QacInspectRecord {
    param([Parameter(Mandatory)][string]$ReservationId)
    $result = Invoke-QacCliText -Arguments @('inspect',$ReservationId) -TimeoutSeconds 15
    $path = Join-Path $script:OutputPath ("qac-inspect-{0}.json" -f $ReservationId)
    [IO.File]::WriteAllText($path, $result.Stdout + $result.Stderr, (New-Object Text.UTF8Encoding($false)))
    if ($result.ExitCode -ne 0) { throw "QAC inspect failed for reservation $ReservationId (exit $($result.ExitCode))." }
    try { return ($result.Stdout | ConvertFrom-Json -ErrorAction Stop) } catch { throw "QAC inspect returned malformed reservation data for $ReservationId." }
}

function Read-SerialAvailable {
    $serialPath = $script:SerialPath
    if (-not (Test-Path -LiteralPath $serialPath -PathType Leaf)) { return }
    $stream = $null
    try {
        $stream = [IO.FileStream]::new($serialPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        if ($stream.Length -lt $script:SerialOffset) { $script:SerialOffset = 0; $script:SerialPending = '' }
        [void]$stream.Seek($script:SerialOffset,[IO.SeekOrigin]::Begin)
        $buffer = New-Object byte[] 8192
        while (($count = $stream.Read($buffer,0,$buffer.Length)) -gt 0) {
            $script:SerialOffset += $count
            $script:SerialPending += [Text.Encoding]::UTF8.GetString($buffer,0,$count)
            while ($script:SerialPending.Contains("`n")) {
                $newline = $script:SerialPending.IndexOf("`n")
                $line = $script:SerialPending.Substring(0,$newline).TrimEnd("`r")
                $script:SerialPending = $script:SerialPending.Substring($newline + 1)
                if ($line.Length -eq 0) { continue }
                $observed = [DateTimeOffset]::UtcNow
                $script:SerialLines.Add($line)
                if (-not $script:Execution.timings.firstGuestProgressAtUtc) {
                    $script:Execution.timings.firstGuestProgressAtUtc = $observed.ToString('O')
                    Add-ExecutionEvent -Type 'first-guest-progress' -Data @{observedAtUtc=$observed.ToString('O'); sample=$line.Substring(0,[Math]::Min(240,$line.Length))}
                }
                if ($script:RuntimeDeadlineUtc -and $observed -gt $script:RuntimeDeadlineUtc -and $line -match '(?i)(PASS|result=PASS)') {
                    Add-ExecutionEvent -Type 'late-guest-marker-rejected' -Data @{observedAtUtc=$observed.ToString('O');deadlineUtc=$script:RuntimeDeadlineUtc.ToString('O');line=$line}
                    continue
                }
                if ($line -match '(?i)discovery result=NO_APPS_DIRECTORY') {
                    Add-ExecutionEvent -Type 'guest-product-failure-marker' -Data @{observedAtUtc=$observed.ToString('O');line=$line}
                }
                if ($line -match '(?i)\[(?:NAVIGATOR|SETTINGS|PACMAN)[^\]]*\].*(?:PASS|FAIL|result=)') {
                    Add-ExecutionEvent -Type 'guest-acceptance-marker' -Data @{observedAtUtc=$observed.ToString('O');line=$line}
                }
            }
        }
    } catch [IO.IOException] {
        Add-ExecutionEvent -Type 'serial-reader-error' -Data @{message=$_.Exception.Message}
    } finally { if ($null -ne $stream) { $stream.Dispose() } }
}

function Add-QacOutputLine {
    param([Parameter(Mandatory)][string]$Stream, [AllowEmptyString()][string]$Line)
    if ($null -eq $Line) { return }
    $timestamp = [DateTimeOffset]::UtcNow.ToString('O')
    [IO.File]::AppendAllText($script:QacLogPath, "$timestamp [$Stream] $Line`n", (New-Object Text.UTF8Encoding($false)))
    if ($Line -match 'QAC reservation requested: id=([a-f0-9]+)') {
        $script:Execution.reservationId = $Matches[1]
        $script:Execution.timings.reservationRequestedAtUtc = $timestamp
        Set-ExecutionState -State 'AdmissionWaiting' -Data @{reservationId=$Matches[1]}
        return
    }
    if ($Line -match 'QAC queued for exclusive acceptance: id=([a-f0-9]+) position=([0-9]+)') {
        $script:Execution.queuePosition = [int]$Matches[2]
        Add-ExecutionEvent -Type 'queued' -Data @{reservationId=$Matches[1];queuePosition=[int]$Matches[2];observedAtUtc=$timestamp}
        return
    }
    if ($Line -match 'QAC exclusive reservation granted: id=([a-f0-9]+)') {
        $script:Execution.reservationId = $Matches[1]
        $script:Execution.timings.reservationGrantedAtUtc = $timestamp
        Set-ExecutionState -State 'AdmittedOrLaunching' -Data @{reservationId=$Matches[1]}
        return
    }
    if ($Line -match 'QAC QEMU launched: reservation=([a-f0-9]+) pid=([0-9]+).*launch_utc=([^ ]+)') {
        $script:Execution.reservationId = $Matches[1]
        $script:Execution.childProcessId = [int]$Matches[2]
        $script:Execution.childStartUtc = $Matches[3]
        $script:Execution.timings.qemuChildStartAtUtc = $Matches[3]
        if ($script:GuestRuntimeTimeoutSeconds -gt 0) { $script:RuntimeDeadlineUtc = ([DateTimeOffset]::Parse($Matches[3])).AddSeconds($script:GuestRuntimeTimeoutSeconds) }
        Set-ManifestExecution -Status 'active' -ReservationId $Matches[1] -ChildProcessId ([int]$Matches[2])
        Set-ExecutionState -State 'QemuRunning' -Data @{reservationId=$Matches[1];childProcessId=[int]$Matches[2];startUtc=$Matches[3]}
        return
    }
    if ($Line -match '^\{"reservationId":"([a-f0-9]+)".*"childProcessId":([0-9]+),.*"processStartTimeUtc":"([^"]+)"') {
        if (-not $script:Execution.childProcessId) {
            $script:Execution.reservationId = $Matches[1]
            $script:Execution.childProcessId = [int]$Matches[2]
            $script:Execution.childStartUtc = $Matches[3]
        }
        return
    }
    if ($Line -match 'QAC QEMU exited: reservation=([a-f0-9]+) pid=([0-9]+) exitCode=(-?[0-9]+) exit_utc=([^ ]+)') {
        $script:Execution.qemuExitCode = [int]$Matches[3]
        $script:Execution.timings.qemuChildExitAtUtc = $Matches[4]
        $script:Execution.childExitVerified = $true
        Add-ExecutionEvent -Type 'qemu-child-exit-reported' -Data @{reservationId=$Matches[1];childProcessId=[int]$Matches[2];exitCode=[int]$Matches[3];exitUtc=$Matches[4]}
        return
    }
    if ($Line -match 'QAC reservation released: id=([a-f0-9]+) pid=([0-9]+)') {
        $script:Execution.qacCliReleaseReportedReservationId = $Matches[1]
        $script:Execution.qacCliReleaseReportedChildProcessId = [int]$Matches[2]
        $script:Execution.qacCliReleaseLineObservedAtUtc = $timestamp
        Add-ExecutionEvent -Type 'qac-release-line-observed' -Data @{reservationId=$Matches[1];childProcessId=[int]$Matches[2];observedAtUtc=$timestamp}
        return
    }
    if ($Line -match 'Recovery Required|release is not verified|remains conservatively blocked') {
        $script:Execution.recoveryRequired = $true
        Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
        Set-ExecutionState -State 'RecoveryRequired' -Data @{message=$Line}
    }
}

function Confirm-QacReleaseReport {
    $reportedReservationId = [string]$script:Execution.qacCliReleaseReportedReservationId
    if ([string]::IsNullOrWhiteSpace($reportedReservationId)) { return $false }

    $reportedChildProcessId = [int]$script:Execution.qacCliReleaseReportedChildProcessId
    $matchesExecution = $script:Execution.reservationId -and $script:Execution.childProcessId -gt 0 -and
        (Test-QacS22R10ReleaseIdentity -ReportedReservationId $reportedReservationId -ReportedChildProcessId $reportedChildProcessId -ExpectedReservationId ([string]$script:Execution.reservationId) -ExpectedChildProcessId ([int]$script:Execution.childProcessId))
    $script:Execution.qacCliReleaseReported = [bool]$matchesExecution
    if ($matchesExecution) {
        $observedAtUtc = [string]$script:Execution.qacCliReleaseLineObservedAtUtc
        $script:Execution.timings.qacReleaseReportedAtUtc = $observedAtUtc
        Add-ExecutionEvent -Type 'qac-release-reported' -Data @{reservationId=$reportedReservationId;childProcessId=$reportedChildProcessId;observedAtUtc=$observedAtUtc}
    } else {
        Add-ExecutionEvent -Type 'qac-release-identity-mismatch' -Data @{reportedReservationId=$reportedReservationId;reportedChildProcessId=$reportedChildProcessId;expectedReservationId=$script:Execution.reservationId;expectedChildProcessId=$script:Execution.childProcessId;observedAtUtc=$script:Execution.qacCliReleaseLineObservedAtUtc}
    }
    return [bool]$matchesExecution
}

function Drain-QacOutput {
    param([Parameter(Mandatory)][Diagnostics.Process]$Process, [Parameter(Mandatory)][ref]$StdoutTask, [Parameter(Mandatory)][ref]$StderrTask)
    foreach ($streamName in @('stdout','stderr')) {
        $taskRef = if ($streamName -eq 'stdout') { $StdoutTask } else { $StderrTask }
        while ($null -ne $taskRef.Value -and $taskRef.Value.IsCompleted) {
            $line = $taskRef.Value.GetAwaiter().GetResult()
            if ($null -eq $line) { $taskRef.Value = $null; break }
            Add-QacOutputLine -Stream $streamName -Line $line
            if ($streamName -eq 'stdout') { $taskRef.Value = $Process.StandardOutput.ReadLineAsync() }
            else { $taskRef.Value = $Process.StandardError.ReadLineAsync() }
        }
    }
}

function Get-QacProcessRecord {
    param([Parameter(Mandatory)][int]$ProcessId)
    return Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction SilentlyContinue
}

function Get-QacProcessIdentitySummary {
    param($ProcessRecord)
    if (-not $ProcessRecord) { return $null }
    $creationUtc = $null
    try {
        if ($ProcessRecord.CreationDate -is [DateTime]) { $creationUtc = $ProcessRecord.CreationDate.ToUniversalTime().ToString('O') }
        elseif ($ProcessRecord.CreationDate -is [DateTimeOffset]) { $creationUtc = $ProcessRecord.CreationDate.UtcDateTime.ToString('O') }
        else { $creationUtc = [Management.ManagementDateTimeConverter]::ToDateTime([string]$ProcessRecord.CreationDate).ToUniversalTime().ToString('O') }
    } catch { }
    return [ordered]@{processId=[int]$ProcessRecord.ProcessId;parentProcessId=[int]$ProcessRecord.ParentProcessId;executablePath=[string]$ProcessRecord.ExecutablePath;creationTimeUtc=$creationUtc}
}

function Add-QmpListenerProbe {
    param([Parameter(Mandatory)]$Observation,[Parameter(Mandatory)][string]$Stage,[Parameter(Mandatory)][int]$Port)
    $script:QmpProbeSequence++
    $expectedStartUtc = if ($script:Execution.childStartUtc) { [DateTimeOffset]::Parse([string]$script:Execution.childStartUtc).ToUniversalTime().ToString('O') } else { $null }
    $entry = [ordered]@{
        timestampUtc=[DateTimeOffset]::UtcNow.ToString('O')
        monotonicElapsedMilliseconds=if ($script:QmpStartupStopwatch) { [Math]::Round($script:QmpStartupStopwatch.Elapsed.TotalMilliseconds,3) } else { $null }
        probeSequence=$script:QmpProbeSequence
        stage=$Stage
        reservationId=$script:Execution.reservationId
        expectedChildProcessIdentity=[ordered]@{processId=$script:Execution.childProcessId;parentProcessId=$script:QacProcess.Id;executablePath=$script:Execution.qemuExecutable;creationTimeUtc=$expectedStartUtc}
        observedProcessIdentity=$Observation.ProcessIdentity
        processAlive=[bool]$Observation.ProcessAlive
        configuredEndpoint=[ordered]@{protocol='tcp';address='127.0.0.1';addressFamily='InterNetwork';port=$Port;argument="tcp:127.0.0.1:$Port,server=on,wait=off"}
        observedMatchingEndpointCount=[int]$Observation.ObservedEndpointCount
        listenerCandidateCount=[int]$Observation.ListenerCount
        observedEndpoints=@($Observation.ObservedEndpoints)
        endpointPresent=[bool]$Observation.EndpointPresent
        endpointReady=[bool]$Observation.EndpointReady
        connectionEligible=[bool]$Observation.ConnectionEligible
        listenerStateReported=[bool]$Observation.ListenerStateReported
        readinessState=[string]$Observation.State
        discoveryState=[string]$Observation.DiscoveryState
        scopedListenerCount=[int]$Observation.ScopedListenerCount
        ownershipValidation=[ordered]@{processIdentityVerified=[bool]$Observation.ProcessIdentityVerified;listenerOwnerMatches=[bool]$Observation.OwnerMatches;configuredAddressMatches=[bool]$Observation.AddressMatches;uniqueEndpoint=([int]$Observation.ListenerCount -eq 1);authorized=[bool]$Observation.Owned}
        discoveryQueryStatus=[string]$Observation.DiscoveryQueryStatus
        queryError=[string]$Observation.QueryError
        processValidationFailure=[string]$Observation.Failure
        lastConnectionError=$script:Execution.qmp.lastConnectionError
    }
    [IO.File]::AppendAllText($script:QmpProbeLogPath,(ConvertTo-Json -InputObject $entry -Compress -Depth 12) + "`n",(New-Object Text.UTF8Encoding($false)))
    $script:Execution.qmp.lastListenerProbe = [ordered]@{probeSequence=$entry.probeSequence;stage=$entry.stage;monotonicElapsedMilliseconds=$entry.monotonicElapsedMilliseconds;readinessState=$entry.readinessState;observedMatchingEndpointCount=$entry.observedMatchingEndpointCount;listenerCandidateCount=$entry.listenerCandidateCount;endpointReady=$entry.endpointReady;connectionEligible=$entry.connectionEligible;discoveryState=$entry.discoveryState;lastConnectionError=$entry.lastConnectionError}
}

function Invoke-QacOwnedChildStartupCleanup {
    param([Parameter(Mandatory)][string]$Reason,[Parameter(Mandatory)][string]$Stage)
    if ($script:QacProcess.HasExited) { throw 'QAC owner process exited before an authorized child cleanup could be verified.' }
    $reservation = Get-QacInspectRecord -ReservationId ([string]$script:Execution.reservationId)
    $cleanup = Stop-QacS22R10OwnedChild -Reservation $reservation -ExpectedReservationId ([string]$script:Execution.reservationId) -ExpectedProject 'guideXOSServer' -ExpectedMode 'acceptance' -ExpectedParentProcessId $script:QacProcess.Id -ExpectedExecutable $script:Execution.qemuExecutable -ExpectedStartUtc ([DateTimeOffset]::Parse([string]$script:Execution.childStartUtc)) -ExpectedChildProcessId ([int]$script:Execution.childProcessId)
    Add-ExecutionEvent -Type 'qmp-startup-failure-owned-child-cleanup' -Data @{stage=$Stage;reason=$Reason;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;reservationState=$reservation.state;childExitVerified=[bool]$cleanup.ExitVerified;childAlreadyExited=[bool]$cleanup.AlreadyExited;cleanupMethod='exact-registered-QAC-child-process-handle'}
}

function Test-QacChildAndListener {
    param([Parameter(Mandatory)][int]$ProcessId,[Parameter(Mandatory)][int]$ParentId,[Parameter(Mandatory)][string]$QemuPath,[Parameter(Mandatory)][DateTimeOffset]$StartUtc,[Parameter(Mandatory)][int]$Port)
    $emptyObservation = Get-QacS22R10QmpListenerObservation -Connections ([object[]]@()) -Port $Port -ExpectedProcessId $ProcessId
    $processRecord = Get-QacProcessRecord -ProcessId $ProcessId
    if (-not $processRecord) {
        $emptyObservation.State = 'child-exited-before-qmp'
        $emptyObservation.ProcessAlive = $false
        $emptyObservation.ProcessIdentity = $null
        $emptyObservation.ProcessIdentityVerified = $false
        $emptyObservation.Failure = $null
        $emptyObservation.DiscoveryQueryStatus = 'not-run-child-exited'
        return $emptyObservation
    }
    $processSummary = Get-QacProcessIdentitySummary -ProcessRecord $processRecord
    if (-not (Test-QacS22R10DirectChildIdentity -ProcessRecord $processRecord -ExpectedPid $ProcessId -ExpectedParentPid $ParentId -ExpectedExecutable $QemuPath -ExpectedStartUtc $StartUtc)) {
        $emptyObservation.State = 'child-identity-mismatch'
        $emptyObservation.ProcessAlive = $true
        $emptyObservation.ProcessIdentity = $processSummary
        $emptyObservation.ProcessIdentityVerified = $false
        $emptyObservation.Failure = 'QAC child PID, parent, executable, or start identity does not match the admitted QEMU process.'
        $emptyObservation.DiscoveryQueryStatus = 'not-run-process-identity-mismatch'
        return $emptyObservation
    }
    $commandLine = [string]$processRecord.CommandLine
    $commandLineFailure = $null
    if ($commandLine.IndexOf("fat:rw:$($script:Validated.Esp.privatePath)",[StringComparison]::OrdinalIgnoreCase) -lt 0) { $commandLineFailure = 'Actual QEMU command line does not reference the run-private ESP with fat:rw.' }
    elseif ($commandLine.IndexOf([string]$script:Validated.Esp.sourcePath,[StringComparison]::OrdinalIgnoreCase) -ge 0) { $commandLineFailure = 'Actual QEMU command line references the canonical ESP.' }
    elseif ($commandLine.IndexOf([string]$script:Validated.Iso.privatePath,[StringComparison]::OrdinalIgnoreCase) -lt 0 -or $commandLine -notmatch '(?i)media=cdrom,readonly=on') { $commandLineFailure = 'Actual QEMU command line does not retain the verified read-only ISO.' }
    if ($commandLine.IndexOf([string]$script:Validated.Ovmf.privatePath,[StringComparison]::OrdinalIgnoreCase) -lt 0 -or $commandLine -notmatch '(?i)readonly=on,file=') { $commandLineFailure = 'Actual QEMU command line does not retain the verified read-only OVMF image.' }
    if (-not (Get-Command -Name Get-NetTCPConnection -CommandType Function,Cmdlet -ErrorAction SilentlyContinue)) {
        $emptyObservation.State = 'listener-query-unavailable'
        $emptyObservation.ProcessAlive = $true
        $emptyObservation.ProcessIdentity = $processSummary
        $emptyObservation.ProcessIdentityVerified = $true
        $emptyObservation.Failure = $commandLineFailure
        $emptyObservation.DiscoveryQueryStatus = 'Get-NetTCPConnection-unavailable'
        return $emptyObservation
    }
    $scopedListeners = @()
    $scopedQueryErrors = @()
    $queryErrorMessages = [System.Collections.Generic.List[string]]::new()
    $targetedQueryFailed = $false
    try {
        $scopedListeners = @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue -ErrorVariable +scopedQueryErrors)
    } catch {
        $targetedQueryFailed = $true
        [void]$queryErrorMessages.Add($_.Exception.Message)
    }
    if ($scopedQueryErrors.Count -gt 0) {
        $targetedQueryFailed = $true
        foreach ($queryError in $scopedQueryErrors) { if ($queryError.Exception.Message) { [void]$queryErrorMessages.Add([string]$queryError.Exception.Message) } }
    }
    $fallbackUsed = $targetedQueryFailed -or $scopedListeners.Count -eq 0
    $matchingListeners = @($scopedListeners)
    $unfilteredScopedRows = @()
    $unfilteredQueryFailed = $false
    $broadQueryFailed = $false
    if ($fallbackUsed) {
        $unfilteredScopedErrors = @()
        try {
            $unfilteredScopedRows = @(Get-NetTCPConnection -LocalPort $Port -ErrorAction SilentlyContinue -ErrorVariable +unfilteredScopedErrors)
            $matchingListeners = @($unfilteredScopedRows)
        } catch {
            $unfilteredQueryFailed = $true
            [void]$queryErrorMessages.Add($_.Exception.Message)
        }
        if ($unfilteredScopedErrors.Count -gt 0) {
            $unfilteredQueryFailed = $true
            foreach ($queryError in $unfilteredScopedErrors) { if ($queryError.Exception.Message) { [void]$queryErrorMessages.Add([string]$queryError.Exception.Message) } }
        }
        if ($matchingListeners.Count -eq 0) {
            $broadQueryErrors = @()
            try {
                $allConnectionRows = @(Get-NetTCPConnection -ErrorAction SilentlyContinue -ErrorVariable +broadQueryErrors)
                $matchingListeners = @($allConnectionRows | Where-Object { [int]$_.LocalPort -eq $Port })
            } catch {
                $broadQueryFailed = $true
                [void]$queryErrorMessages.Add($_.Exception.Message)
            }
            if ($broadQueryErrors.Count -gt 0) {
                $broadQueryFailed = $true
                foreach ($queryError in $broadQueryErrors) { if ($queryError.Exception.Message) { [void]$queryErrorMessages.Add([string]$queryError.Exception.Message) } }
            }
        }
        if ($unfilteredQueryFailed -and $matchingListeners.Count -eq 0) {
            $broadQueryFailed = $broadQueryFailed -or $unfilteredQueryFailed
        }
        if ($broadQueryFailed) {
            foreach ($queryError in $broadQueryErrors) { if ($queryError.Exception.Message) { [void]$queryErrorMessages.Add([string]$queryError.Exception.Message) } }
        }
    }
    $queryStatus = if (-not $fallbackUsed) { 'targeted-local-port-query-succeeded' }
        elseif ($matchingListeners.Count -gt 0 -and $unfilteredScopedRows.Count -gt 0) { 'listen-state-filter-empty-unfiltered-local-port-query-found-endpoint' }
        elseif ($matchingListeners.Count -gt 0) { 'listen-state-filter-and-scoped-query-empty-broad-table-found-configured-port' }
        elseif ($broadQueryFailed) { 'listen-state-filter-and-diagnostic-query-error' }
        else { 'listen-state-filter-and-diagnostic-query-empty-for-configured-port' }
    $queryError = (@($queryErrorMessages | Select-Object -Unique) -join ' | ')
    $observation = Get-QacS22R10QmpListenerObservation -Connections $matchingListeners -ScopedConnections $scopedListeners -UnfilteredScopedConnections $unfilteredScopedRows -Port $Port -ExpectedProcessId $ProcessId -FallbackQueryUsed $fallbackUsed -QueryError $queryError
    $observation.ProcessAlive = $true
    $observation.ProcessIdentity = $processSummary
    $observation.ProcessIdentityVerified = $true
    $observation.ActualCommandLine = $commandLine
    $observation.Failure = $commandLineFailure
    $observation.DiscoveryQueryStatus = $queryStatus
    $observation.FallbackQueryUsed = $fallbackUsed
    $postQueryProcess = Get-QacProcessRecord -ProcessId $ProcessId
    if (-not $postQueryProcess) {
        $observation.State = 'child-exited-during-listener-query'
        $observation.ProcessAlive = $false
        $observation.ProcessIdentityVerified = $false
        $observation.Owned = $false
        $observation.EndpointReady = $false
    } elseif (-not (Test-QacS22R10DirectChildIdentity -ProcessRecord $postQueryProcess -ExpectedPid $ProcessId -ExpectedParentPid $ParentId -ExpectedExecutable $QemuPath -ExpectedStartUtc $StartUtc)) {
        $observation.State = 'child-identity-changed-during-listener-query'
        $observation.ProcessAlive = $true
        $observation.ProcessIdentity = Get-QacProcessIdentitySummary -ProcessRecord $postQueryProcess
        $observation.ProcessIdentityVerified = $false
        $observation.Owned = $false
        $observation.EndpointReady = $false
        $observation.Failure = 'QAC child identity changed between the before-query and after-query process snapshots.'
    }
    return $observation
}

function Confirm-QacRuntimeOwnership {
    if (-not $script:QacProcess -or $script:QacProcess.HasExited) { throw 'QAC owner process is no longer live; guest control is refused.' }
    $reservation = Get-QacInspectRecord -ReservationId ([string]$script:Execution.reservationId)
    if (-not (Test-QacS22R10ReservationIdentity -Reservation $reservation -ExpectedReservationId ([string]$script:Execution.reservationId) -ExpectedProject 'guideXOSServer' -ExpectedMode 'acceptance' -ExpectedState 'Running' -ExpectedChildProcessId ([int]$script:Execution.childProcessId))) {
        throw 'QAC reservation or registered child identity changed; guest control is refused.'
    }
    $childStartUtc = [DateTimeOffset]::Parse([string]$script:Execution.childStartUtc)
    $childRecord = Get-QacProcessRecord -ProcessId ([int]$script:Execution.childProcessId)
    if (-not $childRecord -or -not (Test-QacS22R10DirectChildIdentity -ProcessRecord $childRecord -ExpectedPid ([int]$script:Execution.childProcessId) -ExpectedParentPid $script:QacProcess.Id -ExpectedExecutable ([string]$script:Execution.qemuExecutable) -ExpectedStartUtc $childStartUtc)) {
        throw 'The exact QAC-owned QEMU child is not live; guest control is refused.'
    }
    $endpointParts = [string]$script:Execution.qmp.endpoint -split ':',2
    if ($endpointParts.Count -ne 2 -or $endpointParts[0] -cne '127.0.0.1') { throw 'Guest control endpoint is not the expected IPv4 loopback address.' }
    $port = 0
    if (-not [int]::TryParse($endpointParts[1],[ref]$port) -or $port -lt 49152 -or $port -gt 65535) { throw 'Guest control endpoint has an invalid QMP port.' }
    $observation = Test-QacChildAndListener -ProcessId ([int]$script:Execution.childProcessId) -ParentId $script:QacProcess.Id -QemuPath ([string]$script:Execution.qemuExecutable) -StartUtc $childStartUtc -Port $port
    Add-QmpListenerProbe -Observation $observation -Stage 'operator-command' -Port $port
    if (-not $observation.Owned -or -not $observation.ProcessIdentityVerified) { throw 'QMP listener no longer matches the exact QAC-owned child; guest control is refused.' }
}

function Invoke-GuestOperatorCommand {
    param([Parameter(Mandatory)][string]$Text)
    $trimmed = $Text.Trim()
    if (-not $trimmed) { return $false }
    $parts = $trimmed -split '\s+',3
    switch ($parts[0].ToLowerInvariant()) {
        'help' {
            Write-Host 'Commands: status | screenshot LABEL | key KEY | move DX,DY | click BUTTON | wait SECONDS | result fail REASON | result pass | shutdown'
            Write-Host 'Keys include meta_l, ctrl-esc, esc, enter, tab, up, down, left, right, f12, alt-f4, letters, digits, slash, dot, shift-semicolon, and shift-letter.'
            Write-Host 'Mouse moves are relative, with each axis limited to -49..49. Clicks pair button down/up with ownership checks.'
            return $false
        }
        'status' {
            Confirm-QacRuntimeOwnership
            $state = Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'query-status'
            Write-Host ("QEMU status={0}; QAC reservation={1}; child PID={2}; serial lines observed={3}" -f $state.status,$script:Execution.reservationId,$script:Execution.childProcessId,$script:SerialLines.Count)
            return $false
        }
        'screenshot' {
            if ($parts.Count -lt 2 -or $parts[1] -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,48}$') { throw 'Use screenshot with a safe alphanumeric label.' }
            Confirm-QacRuntimeOwnership
            $path = Join-Path $script:ScreenshotPath ($parts[1] + '.ppm')
            $command = New-QacS22R10ScreendumpCommand -Path $path
            $response = Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'human-monitor-command' -Arguments @{ 'command-line' = $command }
            if (-not [string]::IsNullOrWhiteSpace([string]$response)) { throw "QEMU screendump command failed: $response" }
            $until = [DateTimeOffset]::UtcNow.AddSeconds(5)
            while (-not (Test-Path -LiteralPath $path -PathType Leaf) -and [DateTimeOffset]::UtcNow -lt $until) { Start-Sleep -Milliseconds 100 }
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw 'QEMU did not create the requested private screenshot.' }
            Read-SerialAvailable
            Add-ExecutionEvent -Type 'screenshot' -Data @{path=$path;bytes=(Get-Item -LiteralPath $path).Length;result=$response}
            Write-Host "Screenshot saved: $path"
            return $false
        }
        'key' {
            if ($parts.Count -lt 2 -or $parts[1] -notmatch '^(?i)(meta_l|meta_r|ctrl-esc|esc|enter|tab|space|up|down|left|right|home|end|pageup|pagedown|f12|alt-f4|slash|dot|shift-semicolon|shift-[a-z]|[a-z0-9])$') { throw 'Unsupported QMP key. Use one key from help.' }
            Confirm-QacRuntimeOwnership
            [void](Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'human-monitor-command' -Arguments @{ 'command-line' = "sendkey $($parts[1].ToLowerInvariant())" })
            Add-ExecutionEvent -Type 'guest-key' -Data @{key=$parts[1].ToLowerInvariant()}
            return $false
        }
        'move' {
            if ($parts.Count -ne 2 -or $parts[1] -notmatch '^(-?\d+),(-?\d+)$') { throw 'Use move dx,dy with integer deltas.' }
            $dx = [int]$Matches[1]
            $dy = [int]$Matches[2]
            if ([Math]::Abs($dx) -gt 49 -or [Math]::Abs($dy) -gt 49) { throw 'Each relative mouse axis must be within -49..49.' }
            if ($dx -eq 0 -and $dy -eq 0) { throw 'Relative mouse move must include a non-zero delta.' }
            Confirm-QacRuntimeOwnership
            $events = @()
            if ($dx -ne 0) { $events += @{type='rel';data=@{axis='x';value=$dx}} }
            if ($dy -ne 0) { $events += @{type='rel';data=@{axis='y';value=$dy}} }
            [void](Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'input-send-event' -Arguments @{events=$events})
            Add-ExecutionEvent -Type 'guest-pointer-move' -Data @{dx=$dx;dy=$dy;endpoint=$script:Execution.qmp.endpoint;childProcessId=$script:Execution.childProcessId}
            Write-Host "Relative mouse move sent: $dx,$dy"
            return $false
        }
        'click' {
            if ($parts.Count -ne 2 -or $parts[1].ToLowerInvariant() -notin @('left','middle','right')) { throw 'Use click with left, middle, or right.' }
            $button = $parts[1].ToLowerInvariant()
            $buttonMayBeDown = $false
            try {
                Confirm-QacRuntimeOwnership
                $buttonMayBeDown = $true
                [void](Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'input-send-event' -Arguments @{events=@(@{type='btn';data=@{down=$true;button=$button}})})
                Add-ExecutionEvent -Type 'guest-pointer-button-down' -Data @{button=$button;endpoint=$script:Execution.qmp.endpoint;childProcessId=$script:Execution.childProcessId}
                Start-Sleep -Milliseconds 500
                Confirm-QacRuntimeOwnership
                [void](Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'input-send-event' -Arguments @{events=@(@{type='btn';data=@{down=$false;button=$button}})})
                $buttonMayBeDown = $false
                Add-ExecutionEvent -Type 'guest-pointer-button-up' -Data @{button=$button;endpoint=$script:Execution.qmp.endpoint;childProcessId=$script:Execution.childProcessId}
                Start-Sleep -Milliseconds 500
            } finally {
                if ($buttonMayBeDown) {
                    try {
                        Confirm-QacRuntimeOwnership
                        [void](Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'input-send-event' -Arguments @{events=@(@{type='btn';data=@{down=$false;button=$button}})})
                        Add-ExecutionEvent -Type 'guest-pointer-button-failsafe-up' -Data @{button=$button;endpoint=$script:Execution.qmp.endpoint;childProcessId=$script:Execution.childProcessId}
                    } catch {
                        Add-ExecutionEvent -Type 'guest-pointer-button-failsafe-release-skipped' -Data @{button=$button;reason=$_.Exception.Message}
                    }
                }
            }
            return $false
        }
        'wait' {
            $seconds = 0
            if ($parts.Count -lt 2 -or -not [int]::TryParse($parts[1],[ref]$seconds) -or $seconds -lt 0 -or $seconds -gt 1200) { throw 'wait must be between 0 and 1200 seconds.' }
            $until = [DateTimeOffset]::UtcNow.AddSeconds($seconds)
            while ([DateTimeOffset]::UtcNow -lt $until -and -not $script:QacProcess.HasExited) {
                Read-SerialAvailable
                Drain-QacOutput -Process $script:QacProcess -StdoutTask ([ref]$script:StdoutTask) -StderrTask ([ref]$script:StderrTask)
                Start-Sleep -Milliseconds 100
            }
            Write-Host "Waited $seconds seconds; serial lines observed=$($script:SerialLines.Count)."
            return $false
        }
        'result' {
            if ($parts.Count -lt 2) { throw 'result requires pass or fail.' }
            $kind = $parts[1].ToLowerInvariant()
            if ($kind -eq 'pass') {
                $guest = Get-QacS22R10GuestResult -ObservedSerialLines $script:SerialLines.ToArray()
                if ($guest.Result -ne 'PASS') { throw 'PacMan PASS is rejected because the existing live guest PASS marker was not observed.' }
                $script:Execution.guestResult = 'PASS'
                $script:Execution.guestResultReason = $guest.Reason
            } elseif ($kind -eq 'fail') {
                $script:Execution.guestResult = 'FAIL'
                $script:Execution.guestResultReason = if ($parts.Count -ge 3) { $parts[2] } else { 'Operator classified the existing guest smoke workflow as failed.' }
            } else { throw 'result requires pass or fail.' }
            $script:Execution.timings.guestCompletionOrFailureAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
            Set-ExecutionState -State 'GuestEvaluationComplete' -Data @{guestResult=$script:Execution.guestResult;reason=$script:Execution.guestResultReason}
            Write-Host "Guest result recorded: $($script:Execution.guestResult) — $($script:Execution.guestResultReason)"
            return $false
        }
        { $_ -in @('shutdown','quit','done') } {
            if ($script:Execution.guestResult -notin @('PASS','FAIL')) { throw 'Record the guest result before requesting QMP shutdown.' }
            Confirm-QacRuntimeOwnership
            $script:Execution.timings.qmpShutdownRequestedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
            Set-ExecutionState -State 'ShutdownRequested' -Data @{qmpEndpoint=$script:Execution.qmp.endpoint}
            $reply = Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'quit' -TimeoutMilliseconds 5000
            $script:Execution.qmp.quitResponse = $reply
            $script:Execution.qmp.quitAcknowledged = $true
            Add-ExecutionEvent -Type 'qmp-quit-acknowledged' -Data @{response=$reply}
            Write-Host 'QMP quit was acknowledged; waiting for QAC to verify exact child exit and reservation release.'
            return $true
        }
        default { throw "Unknown guest operator command: $($parts[0]). Type help." }
    }
}

function New-QacExecutionProcess {
    param([Parameter(Mandatory)][string[]]$Arguments)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $script:QacCli
    $start.WorkingDirectory = $repoRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add([string]$argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw 'Could not start QAC managed-run CLI.' }
    return $process
}

function Complete-QacAttempt {
    param([Parameter(Mandatory)][Diagnostics.Process]$Process)
    $waitSeconds = if ($script:Execution.timings.qmpShutdownRequestedAtUtc) { $ShutdownTimeoutSeconds } else { 30 }
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($waitSeconds)
    while (-not $Process.HasExited -and [DateTimeOffset]::UtcNow -lt $deadline) {
        Drain-QacOutput -Process $Process -StdoutTask ([ref]$script:StdoutTask) -StderrTask ([ref]$script:StderrTask)
        Read-SerialAvailable
        Start-Sleep -Milliseconds 100
    }
    if (-not $Process.HasExited) {
        if ($script:Execution.childProcessId -gt 0) {
            Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
            Set-ExecutionState -State 'RecoveryRequired' -Data @{reason="QAC managed child did not exit within the bounded $waitSeconds-second supervision window"}
        }
        throw "QAC managed-run process remained active after the bounded $waitSeconds-second supervision window; it was not terminated."
    }
    $Process.WaitForExit()
    $remainingOutput = @(Complete-QacS22R10ProcessOutputDrain -Process $Process -StdoutTask ([ref]$script:StdoutTask) -StderrTask ([ref]$script:StderrTask))
    foreach ($outputLine in $remainingOutput) {
        Add-QacOutputLine -Stream $outputLine.Stream -Line $outputLine.Line
    }
    [void](Confirm-QacReleaseReport)
    $script:Execution.qacCliExitCode = $Process.ExitCode
    Add-ExecutionEvent -Type 'qac-cli-exit' -Data @{exitCode=$Process.ExitCode}
}

$script:Execution = $null
$script:ManifestPath = $null
$script:OutputPath = $null
$script:QacLogPath = $null
$script:EventLogPath = $null
$script:SerialPath = $null
$script:ScreenshotPath = $null
$script:SerialOffset = [int64]0
$script:SerialPending = ''
$script:SerialLines = [System.Collections.Generic.List[string]]::new()
$script:QmpClient = $null
$script:QmpProbeLogPath = $null
$script:QmpStartupStopwatch = $null
$script:QmpProbeSequence = 0
$script:StartupFailure = $null
$script:StartupFailureStage = $null
$script:QacProcess = $null
$script:StdoutTask = $null
$script:StderrTask = $null
$script:RuntimeDeadlineUtc = $null
$script:ConsoleInputTask = $null
$script:RuntimeShutdownSent = $false
$script:GuestRuntimeTimeoutSeconds = $GuestRuntimeTimeoutSeconds

try {
    $script:QacCli = Resolve-QacCli -ExplicitPath $QacCliPath
    $qemu = (Resolve-Path -LiteralPath $QemuPath -ErrorAction Stop).Path
    if ([IO.Path]::GetFileName($qemu) -cne 'qemu-system-x86_64.exe') { throw 'QEMU executable must be qemu-system-x86_64.exe.' }
    $versionStart = [Diagnostics.ProcessStartInfo]::new()
    $versionStart.FileName = $qemu
    $versionStart.WorkingDirectory = $repoRoot
    $versionStart.UseShellExecute = $false
    $versionStart.CreateNoWindow = $true
    $versionStart.RedirectStandardOutput = $true
    $versionStart.RedirectStandardError = $true
    $versionStart.ArgumentList.Add('--version')
    $versionProcess = [Diagnostics.Process]::new()
    $versionProcess.StartInfo = $versionStart
    if (-not $versionProcess.Start()) { throw 'Installed QEMU executable could not be started for its read-only version check.' }
    $versionStdoutTask = $versionProcess.StandardOutput.ReadToEndAsync()
    $versionStderrTask = $versionProcess.StandardError.ReadToEndAsync()
    if (-not $versionProcess.WaitForExit(10000)) {
        $versionProcess.Kill()
        $versionProcess.WaitForExit()
        throw 'Installed QEMU executable timed out during its read-only version check.'
    }
    $versionText = $versionStdoutTask.GetAwaiter().GetResult() + $versionStderrTask.GetAwaiter().GetResult()
    $qemuVersion = (($versionText -split "`r?`n")[0]).Trim()
    $versionExitCode = $versionProcess.ExitCode
    $versionProcess.Dispose()
    if ($versionExitCode -ne 0 -or $qemuVersion -notmatch '^QEMU emulator version') { throw 'Installed QEMU executable failed its read-only version check.' }

    $script:Execution = [ordered]@{
        schemaVersion=1; runId=$null; workspacePath=$null; manifestPath=$null; state='Preparing';
        project='guideXOSServer'; mode='acceptance'; workflow='s22r10-pacman-uefi-qac';
        qacCli=$script:QacCli; qemuExecutable=$qemu; qemuVersion=$qemuVersion;
        qacReservationId=$null; reservationId=$null; queuePosition=$null; qacCliProcessId=$null;
        childProcessId=$null; childStartUtc=$null; childExitVerified=$false; qemuExitCode=$null; qacCliExitCode=$null;
        qacCliReleaseReported=$false; qacCliReleaseReportedReservationId=$null; qacCliReleaseReportedChildProcessId=0; qacCliReleaseLineObservedAtUtc=$null;
        reservationReleaseVerified=$false; recoveryRequired=$false;
        manifestSchemaVersion=1; qmp=[ordered]@{endpoint=$null;status='not-allocated';ownership='unbound';quitAcknowledged=$false;quitResponse=$null;lastConnectionError=$null;lastListenerProbe=$null};
        qemuArguments=@(); guestResult='INCOMPLETE'; guestResultReason=$null; cleanupEligible=$false;
        timings=[ordered]@{stagingStartAtUtc=$null;stagingCompleteAtUtc=$null;reservationRequestedAtUtc=$null;reservationGrantedAtUtc=$null;qemuChildStartAtUtc=$null;firstGuestProgressAtUtc=$null;guestCompletionOrFailureAtUtc=$null;qmpShutdownRequestedAtUtc=$null;qemuChildExitAtUtc=$null;qacReleaseReportedAtUtc=$null;qacReleaseVerifiedAtUtc=$null};
        attempts=[System.Collections.Generic.List[object]]::new(); events=[System.Collections.Generic.List[object]]::new()
    }

    $script:Execution.timings.stagingStartAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
    $prepare = Join-Path $PSScriptRoot 'prepare-qac-uefi-artifacts.ps1'
    $prepareArguments = @{
        EspPath=$EspPath; IsoPath=$IsoPath; OvmfPath=$OvmfPath; RunId=$RunId;
        ExpectedEspSha256=$ExpectedEspSha256; ExpectedIsoSha256=$ExpectedIsoSha256; ExpectedOvmfSha256=$ExpectedOvmfSha256
    }
    if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) { $prepareArguments.OutputRoot = $OutputRoot }
    $prepareText = & $prepare @prepareArguments
    $prepared = ($prepareText -join "`n") | ConvertFrom-Json -ErrorAction Stop
    if (-not $prepared.prepared -or $prepared.qemuLaunched -or [string]::IsNullOrWhiteSpace($prepared.manifestPath)) { throw 'Accepted QAC3C2 helper did not return a completed staged workspace.' }

    $script:ManifestPath = [string]$prepared.manifestPath
    $script:Validated = Get-QacS22R10ValidatedManifest -ManifestPath $script:ManifestPath
    $manifest = $script:Validated.Manifest
    $script:Execution.runId = [string]$manifest.runId
    $script:Execution.workspacePath = [string]$manifest.workspacePath
    $script:Execution.manifestPath = $script:ManifestPath
    $script:Execution.manifestSchemaVersion = [int]$manifest.schemaVersion
    $script:Execution.sourceArtifacts = @($manifest.artifacts | ForEach-Object { [ordered]@{name=$_.name;sourcePath=$_.sourcePath;sourceSha256=$_.sourceSha256;privatePath=$_.privatePath;privateSha256=$_.privateSha256;access=$_.access;disposition=$_.disposition} })
    $script:Execution.timings.stagingCompleteAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
    $script:OutputPath = $script:Validated.Outputs
    $script:SerialPath = Get-QacS22R10FullPath (Join-Path $script:Validated.Workspace ([string]$manifest.outputs.serialLog).Replace('/','\'))
    $script:ScreenshotPath = Get-QacS22R10FullPath (Join-Path $script:Validated.Workspace ([string]$manifest.outputs.screenshots).Replace('/','\'))
    $script:QacLogPath = Join-Path $script:OutputPath 'qac-run.log'
    $script:EventLogPath = Join-Path $script:OutputPath 'host-execution.jsonl'
    $script:ExecutionPath = Join-Path $script:OutputPath 'execution.json'
    $script:QmpTranscriptPath = Get-QacS22R10FullPath (Join-Path $script:Validated.Workspace ([string]$manifest.outputs.qmpTranscript).Replace('/','\'))
    $script:QmpProbeLogPath = Join-Path $script:OutputPath 'qmp-listener-probes.jsonl'
    foreach ($path in @($script:QacLogPath,$script:EventLogPath,$script:QmpTranscriptPath,$script:QmpProbeLogPath)) { [IO.File]::WriteAllText($path,'',(New-Object Text.UTF8Encoding($false))) }
    Write-ExecutionRecord
    Set-ExecutionState -State 'Prepared' -Data @{manifestPath=$script:ManifestPath;workspacePath=$script:Validated.Workspace}

    $canonicalIdentitiesBefore = @{}
    foreach ($artifact in @($manifest.artifacts)) {
        $kind = [string]$artifact.kind
        $identity = Get-QacS22R10ArtifactIdentity -Path ([string]$artifact.sourcePath) -Kind $kind
        if ($identity.Sha256 -ne [string]$artifact.sourceSha256) { throw "Canonical artifact '$($artifact.name)' changed after staging." }
        $canonicalIdentitiesBefore[[string]$artifact.name] = [ordered]@{path=$identity.Path;sha256=$identity.Sha256;bytes=$identity.TotalBytes;files=$identity.FileCount}
    }
    $script:Execution.canonicalIdentitiesBefore = $canonicalIdentitiesBefore
    $script:Execution.hostResources = [ordered]@{
        availableMemoryBytes=[int64](Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory * 1KB
        qemuMemoryBytes=2GB
        availableWorkspaceBytes=[int64]([IO.DriveInfo]::new([IO.Path]::GetPathRoot($script:Validated.Workspace)).AvailableFreeSpace)
    }
    if ($script:Execution.hostResources.availableMemoryBytes -lt (2500MB)) { throw 'Less than 2.5 GiB of physical memory is available for the existing 2 GiB guest.' }
    if ($script:Execution.hostResources.availableWorkspaceBytes -lt (512MB)) { throw 'Less than 512 MiB of free space remains on the private workspace volume.' }

    $preflight = foreach ($name in @('status','list','queue')) {
        $result = Invoke-QacCliText -Arguments @($name) -TimeoutSeconds 15
        [IO.File]::WriteAllText((Join-Path $script:OutputPath "qac-preflight-$name.txt"),$result.Stdout + $result.Stderr,(New-Object Text.UTF8Encoding($false)))
        if ($result.ExitCode -ne 0) { throw "QAC $name preflight failed with exit $($result.ExitCode); launch is blocked." }
        [PSCustomObject]@{command=$name;exitCode=$result.ExitCode;stdout=$result.Stdout;stderr=$result.Stderr}
    }
    if (-not (Test-QacS22R10CoordinatorOnline -StatusOutput $preflight[0].stdout -ExitCode $preflight[0].exitCode)) { throw 'QAC coordinator is unavailable or in Recovery Required; refusing unmanaged fallback.' }
    $script:Execution.qacPreflight = @($preflight | ForEach-Object { [ordered]@{command=$_.command;exitCode=$_.exitCode;stdout=$_.stdout;stderr=$_.stderr} })
    Add-ExecutionEvent -Type 'preflight-complete' -Data @{qac='Online';reservationSnapshot=(Join-Path $script:OutputPath 'qac-preflight-list.txt');qemuVersion=$qemuVersion}
    Set-ManifestExecution -Status 'active'

    $smokeCompleted = $false
    $collisionAttempts = [System.Collections.Generic.List[int]]::new()
    for ($attemptNumber = 1; $attemptNumber -le $MaxQmpPortAttempts; $attemptNumber++) {
        $script:StartupFailure = $null
        $script:StartupFailureStage = $null
        $script:QmpStartupStopwatch = $null
        $script:QmpProbeSequence = 0
        $script:Execution.qmp.lastConnectionError = $null
        $script:Execution.qmp.lastListenerProbe = $null
        Set-ManifestExecution -Status 'active' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
        $port = Get-QacS22R10QmpPortCandidate -ExcludePort $collisionAttempts.ToArray()
        $collisionAttempts.Add($port)
        $script:Execution.qmp.endpoint = "127.0.0.1:$port"
        $script:Execution.qmp.status = 'candidate-unbound'
        $script:Execution.qmp.ownership = 'candidate-not-reserved'
        $script:Execution.reservationReleaseVerified = $false
        $script:Execution.childExitVerified = $false
        $script:Execution.qacCliReleaseReported = $false
        $script:Execution.qacCliReleaseReportedReservationId = $null
        $script:Execution.qacCliReleaseReportedChildProcessId = 0
        $script:Execution.qacCliReleaseLineObservedAtUtc = $null
        $script:Execution.qemuExitCode = $null
        $script:Execution.qacCliExitCode = $null
        $script:Execution.recoveryRequired = $false
        $script:Execution.reservationId = $null
        $script:Execution.childProcessId = $null
        $script:Execution.childStartUtc = $null
        $script:RuntimeDeadlineUtc = $null
        $script:RuntimeShutdownSent = $false
        $script:Execution.qemuArguments = @(New-QacS22R10QemuArguments -ValidatedManifest $script:Validated -QemuPath $qemu -QmpPort $port)
        $attempt = [ordered]@{number=$attemptNumber;qmpPort=$port;endpointStatus='unbound-candidate';reservationId=$null;childProcessId=$null;guestProgressObserved=$false;result='running'}
        $script:Execution.attempts.Add($attempt)
        Add-ExecutionEvent -Type 'qmp-endpoint-candidate' -Data @{attempt=$attemptNumber;endpoint=$script:Execution.qmp.endpoint;ownership='not-reserved-before-QAC-grant'}
        $attemptLogOffset = if (Test-Path -LiteralPath $script:QacLogPath) { (Get-Item -LiteralPath $script:QacLogPath).Length } else { [int64]0 }
        $qacArguments = @(New-QacS22R10QacArguments -QemuPath $qemu -QemuArguments $script:Execution.qemuArguments)
        $script:QacProcess = New-QacExecutionProcess -Arguments $qacArguments
        $script:Execution.qacCliProcessId = $script:QacProcess.Id
        $script:StdoutTask = $script:QacProcess.StandardOutput.ReadLineAsync()
        $script:StderrTask = $script:QacProcess.StandardError.ReadLineAsync()
        $script:Execution.timings.reservationRequestedAtUtc = $null
        $script:Execution.childProcessId = $null
        $script:Execution.childStartUtc = $null
        Set-ExecutionState -State 'AdmissionWaiting' -Data @{attempt=$attemptNumber;project='guideXOSServer';mode='acceptance';qacCliProcessId=$script:QacProcess.Id}
        $admissionDeadline = [DateTimeOffset]::UtcNow.AddSeconds($AdmissionTimeoutSeconds)
        $cancelSent = $false
        $qmpVerified = $false
        while (-not $script:QacProcess.HasExited) {
            Drain-QacOutput -Process $script:QacProcess -StdoutTask ([ref]$script:StdoutTask) -StderrTask ([ref]$script:StderrTask)
            Read-SerialAvailable
            if ($script:Execution.childProcessId -gt 0 -and -not $qmpVerified) {
                $attempt.childProcessId = $script:Execution.childProcessId
                $attempt.reservationId = $script:Execution.reservationId
                $startUtc = [DateTimeOffset]::Parse([string]$script:Execution.childStartUtc)
                if (-not $script:QmpStartupStopwatch) { $script:QmpStartupStopwatch = [Diagnostics.Stopwatch]::StartNew() }
                $listening = $false
                $listenerProbeCount = 0
                $lastListenerObservation = $null
                $startupDeadline = [DateTimeOffset]::UtcNow.AddSeconds($QmpStartupTimeoutSeconds)
                while ([DateTimeOffset]::UtcNow -lt $startupDeadline -and -not $script:QacProcess.HasExited) {
                    Drain-QacOutput -Process $script:QacProcess -StdoutTask ([ref]$script:StdoutTask) -StderrTask ([ref]$script:StderrTask)
                    Read-SerialAvailable
                    $lastListenerObservation = Test-QacChildAndListener -ProcessId $script:Execution.childProcessId -ParentId $script:QacProcess.Id -QemuPath $qemu -StartUtc $startUtc -Port $port
                    $listenerProbeCount++
                    Add-QmpListenerProbe -Observation $lastListenerObservation -Stage 'startup-poll' -Port $port
                    if ($lastListenerObservation.ActualCommandLine -and -not $script:Execution.qmp.actualChildCommandLine) {
                        $script:Execution.qmp.actualChildCommandLine = [string]$lastListenerObservation.ActualCommandLine
                        $script:Execution.qmp.observedChildProcessIdentity = $lastListenerObservation.ProcessIdentity
                        Add-ExecutionEvent -Type 'qmp-child-launch-identity-observed' -Data @{reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;parentProcessId=$script:QacProcess.Id;processIdentity=$lastListenerObservation.ProcessIdentity;actualCommandLine=$lastListenerObservation.ActualCommandLine;expectedQemuArguments=$script:Execution.qemuArguments}
                    }
                    if ($lastListenerObservation.State -eq 'child-exited-before-qmp') {
                        Add-ExecutionEvent -Type 'qmp-child-exited-before-handshake' -Data @{endpoint=$lastListenerObservation.Endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;probeCount=$listenerProbeCount}
                        $script:StartupFailure = 'QEMU child exited before its owned QMP listener was established.'
                        $script:StartupFailureStage = 'child-exited-before-listener'
                        break
                    }
                    if ($lastListenerObservation.Failure) {
                        $script:StartupFailure = [string]$lastListenerObservation.Failure
                        $script:StartupFailureStage = 'child-launch-identity'
                        break
                    }
                    $listening = [bool]$lastListenerObservation.Owned
                    if ($listening) { break }
                    Start-Sleep -Milliseconds 100
                }
                if ($script:StartupFailure) {
                    try {
                        Invoke-QacOwnedChildStartupCleanup -Reason $script:StartupFailure -Stage $script:StartupFailureStage
                    } catch {
                        Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
                        $script:Execution.recoveryRequired = $true
                        Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$_.Exception.Message;firstFailure=$script:StartupFailure;stage=$script:StartupFailureStage}
                        throw
                    }
                    break
                }
                if ($listening) {
                    $reservation = Get-QacInspectRecord -ReservationId $script:Execution.reservationId
                    if (-not (Test-QacS22R10ReservationIdentity -Reservation $reservation -ExpectedReservationId ([string]$script:Execution.reservationId) -ExpectedProject 'guideXOSServer' -ExpectedMode 'acceptance' -ExpectedState 'Running' -ExpectedChildProcessId ([int]$script:Execution.childProcessId))) {
                        Add-ExecutionEvent -Type 'qmp-preconnect-reservation-rejected' -Data @{reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;observedReservationId=$reservation.id;observedState=$reservation.state;observedChildProcessId=$reservation.processId}
                        throw 'QAC inspect did not confirm the expected active acceptance reservation and child.'
                    }
                    $script:Execution.reservation = $reservation
                    $script:Execution.qmp.status = [string]$lastListenerObservation.State
                    $script:Execution.qmp.ownership = 'verified-by-qac-parent-executable-start-and-listener-pid'
                    Add-ExecutionEvent -Type 'qmp-listener-connection-eligible' -Data @{endpoint=$script:Execution.qmp.endpoint;childProcessId=$script:Execution.childProcessId;qacCliProcessId=$script:QacProcess.Id;reservationId=$script:Execution.reservationId;probeCount=$listenerProbeCount;listenerState=$lastListenerObservation.State;listenerStateReported=$lastListenerObservation.ListenerStateReported;exactOwner=$lastListenerObservation.OwnerMatches;exactAddress=$lastListenerObservation.AddressMatches;connectionEligible=$lastListenerObservation.ConnectionEligible;endpointReady=$lastListenerObservation.EndpointReady}
                    try {
                        $script:QmpClient = Open-QacS22R10QmpClient -Port $port -TranscriptPath $script:QmpTranscriptPath -ConnectTimeoutMilliseconds ($QmpConnectTimeoutSeconds * 1000)
                    } catch {
                        $connectionError = $_.Exception.Message
                        $script:Execution.qmp.lastConnectionError = $connectionError
                        $script:StartupFailure = $connectionError
                        $script:StartupFailureStage = 'connect-or-greeting'
                        if (-not (Get-QacProcessRecord -ProcessId $script:Execution.childProcessId)) {
                            Add-ExecutionEvent -Type 'qmp-child-exited-during-handshake' -Data @{stage='connect-or-greeting';endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId}
                        }
                        Add-ExecutionEvent -Type 'qmp-connection-or-greeting-failed' -Data @{stage=$script:StartupFailureStage;endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;error=$connectionError;stateChangingCommandSent=$false}
                        try { Invoke-QacOwnedChildStartupCleanup -Reason $connectionError -Stage $script:StartupFailureStage }
                        catch {
                            Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
                            $script:Execution.recoveryRequired = $true
                            Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$_.Exception.Message;firstFailure=$connectionError;stage=$script:StartupFailureStage}
                            throw
                        }
                        break
                    }
                    $postConnectObservation = Test-QacChildAndListener -ProcessId $script:Execution.childProcessId -ParentId $script:QacProcess.Id -QemuPath $qemu -StartUtc $startUtc -Port $port
                    Add-QmpListenerProbe -Observation $postConnectObservation -Stage 'post-connect' -Port $port
                    if (-not $postConnectObservation.Owned) {
                        $script:StartupFailure = "QMP listener ownership changed during connection (state=$($postConnectObservation.State), listeners=$($postConnectObservation.ListenerCount))."
                        $script:StartupFailureStage = 'ownership-changed-during-connect'
                        Add-ExecutionEvent -Type 'qmp-listener-ownership-changed' -Data @{stage=$script:StartupFailureStage;endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;readinessState=$postConnectObservation.State;observedMatchingEndpointCount=$postConnectObservation.ListenerCount;stateChangingCommandSent=$false}
                        try { Invoke-QacOwnedChildStartupCleanup -Reason $script:StartupFailure -Stage $script:StartupFailureStage }
                        catch {
                            Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
                            $script:Execution.recoveryRequired = $true
                            Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$_.Exception.Message;firstFailure=$script:StartupFailure;stage=$script:StartupFailureStage}
                            throw
                        }
                        break
                    }
                    $expectedName = "guideXOS-$($manifest.runId)"
                    try {
                        $qmpIdentity = Initialize-QacS22R10QmpClient -Client $script:QmpClient -ExpectedName $expectedName
                    } catch {
                        $handshakeError = $_.Exception.Message
                        $script:Execution.qmp.lastConnectionError = $handshakeError
                        $script:StartupFailure = $handshakeError
                        $script:StartupFailureStage = 'capabilities-or-instance-identity'
                        if (-not (Get-QacProcessRecord -ProcessId $script:Execution.childProcessId)) {
                            Add-ExecutionEvent -Type 'qmp-child-exited-during-handshake' -Data @{stage='capabilities-or-identity';endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId}
                        }
                        Add-ExecutionEvent -Type 'qmp-capabilities-or-identity-failed' -Data @{stage=$script:StartupFailureStage;endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;error=$handshakeError;stateChangingCommandSent=$false}
                        try { Invoke-QacOwnedChildStartupCleanup -Reason $handshakeError -Stage $script:StartupFailureStage }
                        catch {
                            Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
                            $script:Execution.recoveryRequired = $true
                            Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$_.Exception.Message;firstFailure=$handshakeError;stage=$script:StartupFailureStage}
                            throw
                        }
                        break
                    }
                    if ($qmpIdentity.Status -notin @('prelaunch','paused')) {
                        $script:StartupFailure = "QEMU was not paused before QMP identity verification (status=$($qmpIdentity.Status))."
                        $script:StartupFailureStage = 'paused-state-verification'
                        $script:Execution.qmp.lastConnectionError = $script:StartupFailure
                        Add-ExecutionEvent -Type 'qmp-paused-state-rejected' -Data @{endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;observedStatus=$qmpIdentity.Status;stateChangingCommandSent=$false}
                        try { Invoke-QacOwnedChildStartupCleanup -Reason $script:StartupFailure -Stage $script:StartupFailureStage }
                        catch {
                            Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
                            $script:Execution.recoveryRequired = $true
                            Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$_.Exception.Message;firstFailure=$script:StartupFailure;stage=$script:StartupFailureStage}
                            throw
                        }
                        break
                    }
                    $script:Execution.qmp.greeting = $script:QmpClient.Greeting
                    $script:Execution.qmp.identity = $qmpIdentity
                    $script:Execution.qmp.status = 'identity-verified-paused'
                    $script:Execution.qmp.endpointReady = $true
                    $script:Execution.qmp.protocolReadiness = 'greeting-capabilities-instance-name-and-paused-status-verified'
                    Add-ExecutionEvent -Type 'qmp-identity-verified' -Data @{endpoint=$script:Execution.qmp.endpoint;greeting=$script:QmpClient.Greeting.QMP;instanceName=$qmpIdentity.Name;status=$qmpIdentity.Status;childProcessId=$script:Execution.childProcessId;reservationId=$script:Execution.reservationId;protocolReadiness=$script:Execution.qmp.protocolReadiness}
                    if ($script:QacProcess.HasExited) { throw 'QAC owner process exited before continuation; no state-changing command was sent.' }
                    $reservationBeforeContinuation = Get-QacInspectRecord -ReservationId $script:Execution.reservationId
                    if (-not (Test-QacS22R10ReservationIdentity -Reservation $reservationBeforeContinuation -ExpectedReservationId ([string]$script:Execution.reservationId) -ExpectedProject 'guideXOSServer' -ExpectedMode 'acceptance' -ExpectedState 'Running' -ExpectedChildProcessId ([int]$script:Execution.childProcessId))) {
                        throw 'QAC reservation or child ownership changed before continuation; no state-changing command was sent.'
                    }
                    $listenerBeforeContinuation = Test-QacChildAndListener -ProcessId $script:Execution.childProcessId -ParentId $script:QacProcess.Id -QemuPath $qemu -StartUtc $startUtc -Port $port
                    Add-QmpListenerProbe -Observation $listenerBeforeContinuation -Stage 'immediately-before-continuation' -Port $port
                    if (-not $listenerBeforeContinuation.Owned) {
                        $script:StartupFailure = "QMP listener ownership changed before continuation (state=$($listenerBeforeContinuation.State), listeners=$($listenerBeforeContinuation.ListenerCount))."
                        $script:StartupFailureStage = 'ownership-changed-before-continuation'
                        Add-ExecutionEvent -Type 'qmp-listener-ownership-changed' -Data @{stage=$script:StartupFailureStage;endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;readinessState=$listenerBeforeContinuation.State;observedMatchingEndpointCount=$listenerBeforeContinuation.ListenerCount;stateChangingCommandSent=$false}
                        try { Invoke-QacOwnedChildStartupCleanup -Reason $script:StartupFailure -Stage $script:StartupFailureStage }
                        catch {
                            Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
                            $script:Execution.recoveryRequired = $true
                            Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$_.Exception.Message;firstFailure=$script:StartupFailure;stage=$script:StartupFailureStage}
                            throw
                        }
                        break
                    }
                    $continuationAuthorized = Test-QacS22R10ContinuationAuthorization -QacOwnerAlive (-not $script:QacProcess.HasExited) -ReservationValid $true -ListenerOwned ([bool]$listenerBeforeContinuation.Owned) -QmpNegotiated ($null -ne $script:QmpClient.Greeting -and $null -ne $qmpIdentity) -QmpStatus ([string]$qmpIdentity.Status)
                    if (-not $continuationAuthorized) { throw 'QMP continuation authorization gate failed; no state-changing command was sent.' }
                    Add-ExecutionEvent -Type 'qmp-continuation-authorized' -Data @{endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId}
                    Add-ExecutionEvent -Type 'qmp-cont-issued' -Data @{endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;monotonicElapsedMilliseconds=$script:QmpStartupStopwatch.Elapsed.TotalMilliseconds}
                    [void](Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'cont')
                    $continued = Invoke-QacS22R10Qmp -Client $script:QmpClient -Execute 'query-status'
                    if (-not $continued.running) { throw 'QEMU did not enter its running state after the verified QMP cont request.' }
                    $script:Execution.qmp.status = 'running-owned'
                    Set-ExecutionState -State 'QemuRunning' -Data @{reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;qmpEndpoint=$script:Execution.qmp.endpoint}
                    $qmpVerified = $true
                    Write-Host "QAC accepted run $($manifest.runId): reservation $($script:Execution.reservationId), QEMU PID $($script:Execution.childProcessId), QMP $($script:Execution.qmp.endpoint)."
                    Write-Host "QEMU is running from the private ESP. QMP screenshots and keys can be sent from this prompt; type help for commands."
                }
                if (-not $qmpVerified -and -not $script:QacProcess.HasExited -and -not $script:StartupFailure) {
                    $script:Execution.qmp.status = 'startup-timeout-or-unowned'
                    $script:Execution.qmp.ownership = 'unverified'
                    $lastState = if ($null -ne $lastListenerObservation) { [string]$lastListenerObservation.State } else { 'no-probe-result' }
                    $lastCount = if ($null -ne $lastListenerObservation) { [int]$lastListenerObservation.ListenerCount } else { 0 }
                    $script:StartupFailure = 'QMP listener was not proven to belong to the QAC child before startup deadline.'
                    $script:StartupFailureStage = 'listener-startup-deadline'
                    Add-ExecutionEvent -Type 'qmp-listener-startup-deadline' -Data @{endpoint=$script:Execution.qmp.endpoint;reservationId=$script:Execution.reservationId;childProcessId=$script:Execution.childProcessId;startupTimeoutSeconds=$QmpStartupTimeoutSeconds;probeCount=$listenerProbeCount;elapsedMilliseconds=if($script:QmpStartupStopwatch){[Math]::Round($script:QmpStartupStopwatch.Elapsed.TotalMilliseconds,3)}else{$null};lastObservedState=$lastState;lastListenerCount=$lastCount;discoveryQueryStatus=if($lastListenerObservation){$lastListenerObservation.DiscoveryQueryStatus}else{$null};queryError=if($lastListenerObservation){$lastListenerObservation.QueryError}else{$null};lastConnectionError=$script:Execution.qmp.lastConnectionError}
                    try { Invoke-QacOwnedChildStartupCleanup -Reason $script:StartupFailure -Stage $script:StartupFailureStage }
                    catch {
                        Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
                        $script:Execution.recoveryRequired = $true
                        Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$_.Exception.Message;firstFailure=$script:StartupFailure;stage=$script:StartupFailureStage;lastObservedState=$lastState;probeCount=$listenerProbeCount}
                        throw
                    }
                    break
                }
            }
            if ($script:Execution.childProcessId -eq 0) {
                $cancelDue = $false
                $cancelReason = $null
                if ($CancelFilePath -and (Test-Path -LiteralPath $CancelFilePath -PathType Leaf)) { $cancelDue=$true; $cancelReason='operator cancellation file' }
                elseif ([DateTimeOffset]::UtcNow -ge $admissionDeadline) { $cancelDue=$true; $cancelReason='bounded admission timeout' }
                if ($cancelDue -and -not $cancelSent -and $script:Execution.reservationId) {
                    $pending = Get-QacInspectRecord -ReservationId $script:Execution.reservationId
                    if (Test-QacS22R10QueuedCancellationNeeded -ReservationState ([string]$pending.state) -CancellationRequested $true) {
                        $cancelResult = Invoke-QacCliText -Arguments @('cancel',[string]$script:Execution.reservationId) -TimeoutSeconds 15
                        [IO.File]::WriteAllText((Join-Path $script:OutputPath 'qac-queued-cancel.json'),$cancelResult.Stdout + $cancelResult.Stderr,(New-Object Text.UTF8Encoding($false)))
                        if ($cancelResult.ExitCode -ne 0) { throw 'QAC could not cancel the queued reservation; its conservative owner process remains active.' }
                        Add-ExecutionEvent -Type 'queued-cancellation-requested' -Data @{reservationId=$script:Execution.reservationId;reason=$cancelReason}
                        $cancelSent = $true
                    } elseif ($pending.state -eq 'Granted') {
                        $script:Execution.timings.reservationGrantedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
                        Set-ExecutionState -State 'AdmittedOrLaunching' -Data @{reservationId=$script:Execution.reservationId}
                    }
                } elseif ($cancelDue -and -not $script:Execution.reservationId -and -not $script:QacProcess.HasExited) {
                    Set-ManifestExecution -Status 'unknown'
                    $script:Execution.recoveryRequired = $true
                    Set-ExecutionState -State 'RecoveryRequired' -Data @{reason='Admission wait expired before QAC returned a reservation identity; owner state is uncertain.'}
                    throw 'QAC admission did not return a reservation identity within the bounded wait; no QEMU command was started and the workspace is retained for recovery.'
                }
            }
            if ($script:QmpClient -and $qmpVerified) {
                if ($script:RuntimeDeadlineUtc -and [DateTimeOffset]::UtcNow -ge $script:RuntimeDeadlineUtc -and $script:Execution.guestResult -eq 'INCOMPLETE') {
                    $script:Execution.guestResult = 'FAIL'
                    $script:Execution.guestResultReason = 'Configured guest runtime deadline expired.'
                    $script:Execution.timings.guestCompletionOrFailureAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
                    Add-ExecutionEvent -Type 'guest-runtime-timeout' -Data @{deadlineUtc=$script:RuntimeDeadlineUtc.ToString('O')}
                    Set-ExecutionState -State 'GuestEvaluationComplete' -Data @{guestResult='FAIL';reason=$script:Execution.guestResultReason}
                    if (-not $script:RuntimeShutdownSent) {
                        if (Invoke-GuestOperatorCommand 'shutdown') { $smokeCompleted = $true; break }
                        $script:RuntimeShutdownSent = $true
                    }
                }
                if (-not $script:QacProcess.HasExited) {
                    if (-not (Get-QacProcessRecord -ProcessId $script:Execution.childProcessId)) {
                        if ($script:Execution.guestResult -eq 'INCOMPLETE') {
                            $script:Execution.guestResult = 'FAIL'
                            $script:Execution.guestResultReason = 'QEMU child exited before guest evaluation was completed.'
                            $script:Execution.timings.guestCompletionOrFailureAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
                            Add-ExecutionEvent -Type 'unexpected-early-qemu-exit' -Data @{childProcessId=$script:Execution.childProcessId}
                        }
                        Close-QacS22R10QmpClient -Client $script:QmpClient
                        $script:QmpClient = $null
                        break
                    }
                    if (-not $script:ConsoleInputTask) { $script:ConsoleInputTask = [Console]::In.ReadLineAsync() }
                    if ($script:ConsoleInputTask.IsCompleted) {
                        $operatorLine = $script:ConsoleInputTask.GetAwaiter().GetResult()
                        $script:ConsoleInputTask = $null
                        if ($null -eq $operatorLine) { throw 'Interactive input ended while QEMU was active; QAC ownership and the workspace remain protected.' }
                        if ([string]::IsNullOrWhiteSpace($operatorLine)) { continue }
                        if (Invoke-GuestOperatorCommand -Text $operatorLine) { $smokeCompleted = $true; break }
                    }
                }
            }
            Start-Sleep -Milliseconds 100
        }

        Complete-QacAttempt -Process $script:QacProcess
        if ($script:Execution.reservationId) {
            try {
                $finalReservation = Get-QacInspectRecord -ReservationId $script:Execution.reservationId
                $script:Execution.finalReservation = $finalReservation
                $launchedChild = $script:Execution.childProcessId -gt 0
                if ($launchedChild) {
                    $script:Execution.reservationReleaseVerified = Test-QacS22R10ReservationIdentity -Reservation $finalReservation -ExpectedReservationId ([string]$script:Execution.reservationId) -ExpectedProject 'guideXOSServer' -ExpectedMode 'acceptance' -ExpectedState 'Released' -ExpectedChildProcessId ([int]$script:Execution.childProcessId)
                    if (-not $script:Execution.reservationReleaseVerified) { throw 'QAC inspect did not confirm the exact Released acceptance reservation and child identity.' }
                    if (-not $script:Execution.qacCliReleaseReported) { throw 'QAC inspect says Released but qac run did not report verified release.' }
                } else {
                    if ([string]$finalReservation.id -cne [string]$script:Execution.reservationId) { throw 'QAC inspect returned a different childless reservation identity.' }
                    $script:Execution.reservationReleaseVerified = $finalReservation.state -in @('Cancelled','Expired','Failed','Released')
                    if (-not $script:Execution.reservationReleaseVerified) { throw "Childless QAC reservation ended in unexpected state '$($finalReservation.state)'." }
                    $script:Execution.cancelledBeforeLaunch = $finalReservation.state -eq 'Cancelled'
                }
                $script:Execution.timings.qacReleaseVerifiedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
                $script:Execution.childExitVerified = $launchedChild
                if ($script:Execution.childProcessId -gt 0) {
                    $samePid = Get-QacProcessRecord -ProcessId $script:Execution.childProcessId
                    $sameIdentity = $false
                    if ($samePid) {
                        $expectedChildStart = [DateTimeOffset]::Parse([string]$script:Execution.childStartUtc)
                        $sameIdentity = Test-QacS22R10DirectChildIdentity -ProcessRecord $samePid -ExpectedPid $script:Execution.childProcessId -ExpectedParentPid $script:QacProcess.Id -ExpectedExecutable $qemu -ExpectedStartUtc $expectedChildStart
                    }
                    if ($sameIdentity) {
                        throw 'QAC reported release while the exact child PID/start identity is still alive.'
                    }
                }
                if ($script:Execution.childProcessId -gt 0) {
                    Set-ManifestExecution -Status 'released' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId -ChildExitVerified $true
                    $script:Execution.cleanupEligible = Test-QacS22R10CleanupEligible -ReservationState $finalReservation.state -ChildWasLaunched $true -ChildExitVerified $true -ReservationReleaseVerified $true
                } else {
                    Set-ManifestExecution -Status 'not-started' -ReservationId $script:Execution.reservationId
                    $script:Execution.cleanupEligible = Test-QacS22R10CleanupEligible -ReservationState $finalReservation.state -ChildWasLaunched $false -ChildExitVerified $false -ReservationReleaseVerified $true
                }
                Add-ExecutionEvent -Type 'qac-release-verified' -Data @{reservationId=$script:Execution.reservationId;state=$finalReservation.state;childExitVerified=$script:Execution.childExitVerified}
                Set-ExecutionState -State 'ExecutionReleased' -Data @{reservationId=$script:Execution.reservationId;guestResult=$script:Execution.guestResult}
            } catch {
                $script:Execution.recoveryRequired = $true
                Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId
                Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$_.Exception.Message}
                throw
            }
        } else {
            Set-ManifestExecution -Status 'not-started'
            $script:Execution.cleanupEligible = $true
        }

        $attemptOutput = ''
        if (Test-Path -LiteralPath $script:QacLogPath) {
            $allOutput = [IO.File]::ReadAllText($script:QacLogPath)
            if ($attemptLogOffset -lt $allOutput.Length) { $attemptOutput = $allOutput.Substring([int]$attemptLogOffset) }
        }
        $attempt.childExitVerified = [bool]$script:Execution.childExitVerified
        $attempt.reservationReleased = [bool]$script:Execution.reservationReleaseVerified
        $attempt.guestProgressObserved = [bool]$script:Execution.timings.firstGuestProgressAtUtc
        if (-not $qmpVerified -and (Test-QacS22R10RetryablePortCollision -Output $attemptOutput -ChildExitVerified ([bool]$script:Execution.childExitVerified) -ReservationReleased ([bool]$script:Execution.reservationReleaseVerified) -GuestProgressObserved ([bool]$attempt.guestProgressObserved)) -and $attemptNumber -lt $MaxQmpPortAttempts) {
            $attempt.result = 'verified-qmp-bind-collision-retried-before-guest-execution'
            Add-ExecutionEvent -Type 'qmp-port-collision-retry' -Data @{attempt=$attemptNumber;port=$port;nextAttempt=$attemptNumber+1}
            $attempt.reservationId = $script:Execution.reservationId
            $attempt.childProcessId = $script:Execution.childProcessId
            continue
        }
        if (-not $qmpVerified -and $script:Execution.childProcessId -gt 0 -and -not $script:StartupFailure) { throw 'QEMU exited before an owned QMP endpoint was established.' }
        break
    }

    if ($script:Execution.reservationReleaseVerified -and $script:Execution.childProcessId -gt 0) {
        Read-SerialAvailable
        $script:Execution.guestAssessment = Get-QacS22R10GuestResult -ObservedSerialLines $script:SerialLines.ToArray()
        if ($script:Execution.guestResult -eq 'INCOMPLETE') {
            $script:Execution.guestResult = [string]$script:Execution.guestAssessment.Result
            $script:Execution.guestResultReason = [string]$script:Execution.guestAssessment.Reason
        }
        if ($script:Execution.guestResult -eq 'FAIL' -and -not $script:Execution.timings.guestCompletionOrFailureAtUtc) { $script:Execution.timings.guestCompletionOrFailureAtUtc = [DateTimeOffset]::UtcNow.ToString('O') }
        if ($script:StartupFailure) {
            $script:Execution.guestResult = 'INCOMPLETE'
            $script:Execution.guestResultReason = [string]$script:StartupFailure
            Set-ExecutionState -State 'FailedWithRetainedEvidence' -Data @{integrationLifecycle='released-after-startup-failure';guestResult=$script:Execution.guestResult;stage=$script:StartupFailureStage;reason=$script:StartupFailure}
        } else {
            Set-ExecutionState -State 'Completed' -Data @{integrationLifecycle='released';guestResult=$script:Execution.guestResult}
        }
    } elseif ($script:Execution.cancelledBeforeLaunch) {
        Set-ExecutionState -State 'FailedWithRetainedEvidence' -Data @{reason='QAC reservation was cancelled while queued; no QEMU child launched.'}
    } elseif ($script:Execution.state -notin @('RecoveryRequired')) {
        Set-ExecutionState -State 'FailedWithRetainedEvidence' -Data @{reason='QAC managed execution did not reach verified child exit and release.'}
    }

    foreach ($artifact in @($manifest.artifacts)) {
        $identity = Get-QacS22R10ArtifactIdentity -Path ([string]$artifact.sourcePath) -Kind ([string]$artifact.kind)
        $before = $canonicalIdentitiesBefore[[string]$artifact.name]
        if ($identity.Sha256 -ne [string]$before.sha256) { throw "Canonical artifact '$($artifact.name)' changed during execution." }
    }
    $script:Execution.canonicalArtifactsUnchangedAfterRun = $true
    $finalStatus = Invoke-QacCliText -Arguments @('status') -TimeoutSeconds 15
    [IO.File]::WriteAllText((Join-Path $script:OutputPath 'qac-final-status.txt'),$finalStatus.Stdout + $finalStatus.Stderr,(New-Object Text.UTF8Encoding($false)))
    if ($finalStatus.ExitCode -ne 0) { throw 'Final QAC coordinator status could not be verified.' }
    $script:Execution.finalQacStatus = $finalStatus.Stdout.Trim()
    $script:Execution.timings.qacReleaseVerifiedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
    $script:Execution.completedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
    Write-ExecutionRecord
    if ($script:Execution.reservationReleaseVerified -and -not $script:StartupFailure) {
        Set-ExecutionState -State 'Completed' -Data @{integrationLifecycle='verified';guestResult=$script:Execution.guestResult;guestResultReason=$script:Execution.guestResultReason}
    }
    Write-Host "Run workspace: $($script:Validated.Workspace)"
    Write-Host "Integration lifecycle: $($script:Execution.state); guest product result: $($script:Execution.guestResult)."
    Write-Host "QAC reservation: $($script:Execution.reservationId); QEMU child: $($script:Execution.childProcessId); exit: $($script:Execution.qemuExitCode); release verified: $($script:Execution.reservationReleaseVerified)."
    Write-Host "Workspace retained. Guarded cleanup eligibility: $($script:Execution.cleanupEligible)."
    if ($script:Execution.cancelledBeforeLaunch) { exit 130 }
    if ($script:StartupFailure -or -not $script:Execution.reservationReleaseVerified -or $script:Execution.recoveryRequired) { exit 1 }
    exit 0
} catch {
    $message = $_.Exception.Message
    if ($script:Execution) {
        $script:Execution.failure = $message
        if ($script:Execution.childProcessId -gt 0 -and -not $script:Execution.reservationReleaseVerified) {
            $script:Execution.recoveryRequired = $true
            if ($script:ManifestPath -and (Test-Path -LiteralPath $script:ManifestPath)) { Set-ManifestExecution -Status 'unknown' -ReservationId $script:Execution.reservationId -ChildProcessId $script:Execution.childProcessId }
            Set-ExecutionState -State 'RecoveryRequired' -Data @{reason=$message}
        } elseif ($script:ManifestPath -and (Test-Path -LiteralPath $script:ManifestPath) -and $script:Execution.reservationReleaseVerified) {
            Set-ExecutionState -State 'FailedWithRetainedEvidence' -Data @{reason=$message}
        }
        if ($script:ExecutionPath -and (Test-Path -LiteralPath $script:ExecutionPath)) { Write-ExecutionRecord }
        Write-Error "QAC3C3 UEFI runner failed: $message"
        if ($script:Execution.recoveryRequired) { Write-Error 'QAC ownership or QMP identity is uncertain; the private workspace is retained and must not be cleaned.' }
    } else { Write-Error $message }
    exit 1
} finally {
    Close-QacS22R10QmpClient -Client $script:QmpClient
}
