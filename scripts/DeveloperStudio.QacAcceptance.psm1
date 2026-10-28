Set-StrictMode -Version Latest

function Resolve-QacAcceptanceCliPath {
    param([string]$Path = '')

    $candidate = $Path
    if ([string]::IsNullOrWhiteSpace($candidate)) { $candidate = $env:GUIDEXOS_QAC_CLI }
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $command = Get-Command 'qac.exe' -ErrorAction SilentlyContinue
        if (-not $command) { $command = Get-Command 'qac' -ErrorAction SilentlyContinue }
        if ($command) { $candidate = $command.Source }
    }
    if ([string]::IsNullOrWhiteSpace($candidate) -or -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw 'QAC CLI unavailable. Build or install qac.exe, add it to PATH, set GUIDEXOS_QAC_CLI, or pass -QacCliPath. Acceptance did not launch QEMU.'
    }
    if ([IO.Path]::GetExtension($candidate) -ine '.exe') {
        throw "QAC CLI must be a Windows executable apphost (.exe): $candidate"
    }
    return [IO.Path]::GetFullPath($candidate)
}

function Get-QacAcceptancePassMarker {
    param([Parameter(Mandatory = $true)][ValidateSet('27E', '27F', '28Q', '28O', 'COMP4D', 'COMP4F')][string]$Phase)

    switch ($Phase) {
        '27E' { return 'phase27e=PASS' }
        '27F' { return 'phase27f=PASS' }
        '28Q' { return 'DEVELOPER_STUDIO_PHASE28Q_PASS' }
        '28O' { return 'DEVELOPER_STUDIO_PHASE28O_PASS' }
        'COMP4D' { return 'DEVELOPER_STUDIO_COMP4D_GUEST_PASS' }
        'COMP4F' { return 'DEVELOPER_STUDIO_COMP4F_GUEST_PASS' }
    }
}

function Get-QacAcceptanceCompletionMarker {
    param([Parameter(Mandatory = $true)][ValidateSet('COMP4D', 'COMP4F')][string]$Phase)

    switch ($Phase) {
        'COMP4D' { return 'DEVELOPER_STUDIO_COMP4D_GUEST_COMPLETE' }
        'COMP4F' { return 'DEVELOPER_STUDIO_COMP4F_GUEST_COMPLETE' }
    }
}

function ConvertTo-QacWindowsCommandLineArgument {
    param([AllowEmptyString()][Parameter(Mandatory = $true)][string]$Value)

    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = New-Object Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]'\') {
            ++$backslashes
            continue
        }
        if ($character -eq [char]'"') {
            if ($backslashes -gt 0) { [void]$builder.Append(('\' * (2 * $backslashes))) }
            [void]$builder.Append('\"')
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) { [void]$builder.Append(('\' * $backslashes)); $backslashes = 0 }
        [void]$builder.Append($character)
    }
    if ($backslashes -gt 0) { [void]$builder.Append(('\' * (2 * $backslashes))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function New-QacManagedRunArgumentString {
    param(
        [Parameter(Mandatory = $true)][string]$Project,
        [Parameter(Mandatory = $true)][ValidateSet('development', 'acceptance')][string]$Mode,
        [Parameter(Mandatory = $true)][string]$QemuExecutable,
        [string[]]$QemuArguments = @()
    )

    $arguments = @('run', '--project', $Project, '--mode', $Mode, '--', $QemuExecutable) + $QemuArguments
    return (($arguments | ForEach-Object { ConvertTo-QacWindowsCommandLineArgument -Value $_ }) -join ' ')
}

function New-QacCliArgumentString {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    return (($Arguments | ForEach-Object { ConvertTo-QacWindowsCommandLineArgument -Value $_ }) -join ' ')
}

function Read-QacSerialMarker {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$Marker,
        [Parameter(Mandatory = $true)][hashtable]$State
    )

    if (-not $State.ContainsKey('Offset')) { $State.Offset = [long]0 }
    if (-not $State.ContainsKey('Carry')) { $State.Carry = '' }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return [pscustomobject]@{ Found = $false; NewBytes = [long]0; TotalBytes = [long]$State.Offset }
    }

    $share = [IO.FileShare]([int][IO.FileShare]::ReadWrite -bor [int][IO.FileShare]::Delete)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
    try {
        if ($stream.Length -lt [long]$State.Offset) {
            $State.Offset = [long]0
            $State.Carry = ''
        }
        $stream.Position = [long]$State.Offset
        $buffer = New-Object byte[] 16384
        $text = New-Object Text.StringBuilder
        $newBytes = [long]0
        while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            [void]$text.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $read))
            $newBytes += $read
        }
        $State.Offset = $stream.Position
        $combined = [string]$State.Carry + $text.ToString()
        $found = $combined.IndexOf($Marker, [StringComparison]::Ordinal) -ge 0
        $carryLength = [Math]::Min($Marker.Length - 1, $combined.Length)
        $State.Carry = if ($carryLength -gt 0) { $combined.Substring($combined.Length - $carryLength, $carryLength) } else { '' }
        return [pscustomobject]@{ Found = $found; NewBytes = $newBytes; TotalBytes = [long]$State.Offset }
    } finally {
        $stream.Dispose()
    }
}

function Test-QacMarkerObservedWithinDeadline {
    param(
        [Parameter(Mandatory = $true)][DateTime]$ObservedAtUtc,
        [Parameter(Mandatory = $true)][DateTime]$DeadlineUtc
    )
    return $ObservedAtUtc.ToUniversalTime() -le $DeadlineUtc.ToUniversalTime()
}

function Test-QacMarkerObservedBeforeChildExit {
    param(
        [Parameter(Mandatory = $true)][DateTime]$ObservedAtUtc,
        [Parameter(Mandatory = $true)][DateTime]$ChildExitAtUtc
    )
    return $ObservedAtUtc.ToUniversalTime() -lt $ChildExitAtUtc.ToUniversalTime()
}

function Request-QacCancellation {
    param([Parameter(Mandatory = $true)][hashtable]$State)
    $State.CancelRequested = $true
}

function Invoke-QacCancelReservation {
    param(
        [Parameter(Mandatory = $true)][string]$QacCliPath,
        [Parameter(Mandatory = $true)][string]$ReservationId,
        [int]$TimeoutSeconds = 10
    )

    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $QacCliPath
    $start.Arguments = New-QacCliArgumentString -Arguments @('cancel', $ReservationId)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { return [pscustomobject]@{ ExitCode = 1; Output = ''; Error = 'Could not start qac cancel.'; Completed = $true } }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit([Math]::Max(1, $TimeoutSeconds) * 1000)) {
            return [pscustomobject]@{ ExitCode = 3; Output = ''; Error = 'qac cancel timed out.'; Completed = $false }
        }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Output = $stdout.GetAwaiter().GetResult()
            Error = $stderr.GetAwaiter().GetResult()
            Completed = $true
        }
    } finally {
        $process.Dispose()
    }
}

function Register-QacConsoleCancellation {
    param([Parameter(Mandatory = $true)][hashtable]$State)

    $callback = {
        param($sender, $eventArgs)
        $eventArgs.Cancel = $true
        Request-QacCancellation -State $State
    }.GetNewClosure()
    $handler = [ConsoleCancelEventHandler]$callback
    [Console]::add_CancelKeyPress($handler)
    return $handler
}

function Unregister-QacConsoleCancellation {
    param([Parameter(Mandatory = $true)][ConsoleCancelEventHandler]$Handler)
    [Console]::remove_CancelKeyPress($Handler)
}

function New-QacLoopbackQmpPort {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
}

function Send-QacQmpQuit {
    param(
        [Parameter(Mandatory = $true)][ValidateRange(1, 65535)][int]$Port,
        [int]$TimeoutSeconds = 5
    )

    $client = [Net.Sockets.TcpClient]::new()
    $reader = $null
    $writer = $null
    try {
        $connect = $client.BeginConnect([Net.IPAddress]::Loopback, $Port, $null, $null)
        if (-not $connect.AsyncWaitHandle.WaitOne([Math]::Max(1, $TimeoutSeconds) * 1000)) {
            throw "Timed out connecting to QEMU's loopback QMP endpoint on port $Port."
        }
        $client.EndConnect($connect)
        $stream = $client.GetStream()
        $stream.ReadTimeout = [Math]::Max(1, $TimeoutSeconds) * 1000
        $stream.WriteTimeout = [Math]::Max(1, $TimeoutSeconds) * 1000
        $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8)
        $writer = New-Object IO.StreamWriter($stream, [Text.Encoding]::ASCII)
        $writer.NewLine = "`n"
        $writer.AutoFlush = $true

        $greeting = $reader.ReadLine()
        if ([string]::IsNullOrWhiteSpace($greeting) -or $greeting -notmatch '"QMP"') {
            throw 'QEMU did not return a valid QMP greeting.'
        }
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        $capabilityDeadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(1, $TimeoutSeconds))
        $capabilitiesAccepted = $false
        while ([DateTime]::UtcNow -lt $capabilityDeadline) {
            $line = $reader.ReadLine()
            if ($null -eq $line) { throw 'QEMU closed QMP before accepting capabilities.' }
            if ($line -match '"error"') { throw "QEMU rejected QMP capabilities: $line" }
            if ($line -match '"return"') { $capabilitiesAccepted = $true; break }
        }
        if (-not $capabilitiesAccepted) { throw 'Timed out waiting for QMP capabilities acceptance.' }
        $writer.WriteLine('{"execute":"quit"}')
        return $true
    } finally {
        if ($writer) { $writer.Dispose() }
        if ($reader) { $reader.Dispose() }
        $client.Dispose()
    }
}

function Test-QacManagedAcceptanceSuccess {
    param(
        [Parameter(Mandatory = $true)][int]$CliExitCode,
        [Parameter(Mandatory = $true)][int]$QemuExitCode,
        [Parameter(Mandatory = $true)][bool]$ReservationReleased,
        [bool]$MarkerObservedWithinDeadline = $false,
        [bool]$RunTimedOut = $false,
        [bool]$CancellationRequested = $false,
        [bool]$AdmissionTimedOut = $false,
        [bool]$RecoveryRequired = $false,
        [bool]$CoordinatorUnavailable = $false
    )
    return $ReservationReleased -and $CliExitCode -eq 0 -and $QemuExitCode -eq 0 -and
        $MarkerObservedWithinDeadline -and -not $RunTimedOut -and -not $CancellationRequested -and
        -not $AdmissionTimedOut -and -not $RecoveryRequired -and -not $CoordinatorUnavailable
}

Export-ModuleMember -Function Resolve-QacAcceptanceCliPath, Get-QacAcceptancePassMarker, Get-QacAcceptanceCompletionMarker, ConvertTo-QacWindowsCommandLineArgument,
    New-QacManagedRunArgumentString, New-QacCliArgumentString, Invoke-QacCancelReservation,
    Register-QacConsoleCancellation, Unregister-QacConsoleCancellation, New-QacLoopbackQmpPort,
    Send-QacQmpQuit, Read-QacSerialMarker, Test-QacMarkerObservedWithinDeadline,
    Test-QacMarkerObservedBeforeChildExit,
    Request-QacCancellation, Test-QacManagedAcceptanceSuccess
