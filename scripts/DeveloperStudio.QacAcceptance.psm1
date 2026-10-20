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
        $State.CancelRequested = $true
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
        [bool]$RunTimedOut = $false,
        [bool]$CancellationRequested = $false,
        [bool]$RecoveryRequired = $false,
        [bool]$CoordinatorUnavailable = $false
    )
    return $ReservationReleased -and $CliExitCode -eq $QemuExitCode -and
        -not $RunTimedOut -and -not $CancellationRequested -and -not $RecoveryRequired -and -not $CoordinatorUnavailable
}

Export-ModuleMember -Function Resolve-QacAcceptanceCliPath, ConvertTo-QacWindowsCommandLineArgument,
    New-QacManagedRunArgumentString, New-QacCliArgumentString, Invoke-QacCancelReservation,
    Register-QacConsoleCancellation, Unregister-QacConsoleCancellation, New-QacLoopbackQmpPort,
    Send-QacQmpQuit, Test-QacManagedAcceptanceSuccess
