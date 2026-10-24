param(
    [Parameter(Mandatory)][ValidateRange(1, 65535)][int]$Port,
    [Parameter(Mandatory)][ValidateSet('inspect', 'screenshot', 'key', 'move', 'click', 'hmp', 'hmpkey')][string]$Action,
    [Parameter(Mandatory)][string]$Value,
    [Parameter(Mandatory)][string]$QacCliPath,
    [Parameter(Mandatory)][string]$ReservationId,
    [Parameter(Mandatory)][ValidateRange(1, 2147483647)][int]$QemuPid
)

$ErrorActionPreference = 'Stop'
$scriptRoot = $PSScriptRoot
$transcriptPath = Join-Path $scriptRoot 'qmp-transcript.jsonl'
$qacTracePath = Join-Path $scriptRoot 'qac-ownership-trace.jsonl'
$tcp = $null
$reader = $null
$writer = $null
$script:buttonMayBeDown = $false
$script:sequence = 0

function Test-OwnedQacGuest {
    $observedAt = [DateTime]::UtcNow.ToString('o')
    $lines = @(& $QacCliPath list 2>&1)
    $exitCode = $LASTEXITCODE
    $joined = ($lines | ForEach-Object { [string]$_ }) -join "`n"
    $entry = [ordered]@{
        observedAtUtc = $observedAt
        reservationId = $ReservationId
        expectedQemuPid = $QemuPid
        exitCode = $exitCode
        output = $joined
    }
    Add-Content -LiteralPath $qacTracePath -Value ($entry | ConvertTo-Json -Compress -Depth 5) -Encoding utf8
    if ($exitCode -ne 0) { return $false }
    $escapedId = [regex]::Escape($ReservationId)
    $hasReservation = $joined -match "(?m)^\s*$escapedId\s*\|\s*guideXOSServer\s*\|\s*acceptance\s*\|\s*Running\b"
    $hasChild = $joined -match "(?m)^\s*QEMU PID $QemuPid\s*\|"
    return ($hasReservation -and $hasChild)
}

function Send-Qmp([hashtable]$Command) {
    $script:sequence += 1
    $id = $script:sequence
    $Command.id = $id
    $payload = $Command | ConvertTo-Json -Compress -Depth 10
    Add-Content -LiteralPath $transcriptPath -Value "$([DateTime]::UtcNow.ToString('o')) TX $payload" -Encoding utf8
    $writer.WriteLine($payload)
    while ($true) {
        $line = $reader.ReadLine()
        if ($null -eq $line) { throw 'QMP connection closed before a command reply.' }
        Add-Content -LiteralPath $transcriptPath -Value "$([DateTime]::UtcNow.ToString('o')) RX $line" -Encoding utf8
        $reply = $line | ConvertFrom-Json
        if ($reply.PSObject.Properties.Name -contains 'id' -and $reply.id -eq $id) {
            if ($reply.PSObject.Properties.Name -contains 'error') { throw "QMP command failed: $line" }
            return $reply.return
        }
    }
}

function Send-Key([string]$Qcode) {
    if (-not (Test-OwnedQacGuest)) { throw 'QAC ownership changed; no input was sent.' }
    $key = @{type='qcode'; data=$Qcode}
    try {
        [void](Send-Qmp @{execute='input-send-event'; arguments=@{events=@(@{type='key'; data=@{down=$true; key=$key}})}})
        Start-Sleep -Milliseconds 100
    } finally {
        if ($tcp -and $tcp.Connected -and (Test-OwnedQacGuest)) {
            [void](Send-Qmp @{execute='input-send-event'; arguments=@{events=@(@{type='key'; data=@{down=$false; key=$key}})}})
        }
    }
}

try {
    if (-not (Test-OwnedQacGuest)) { throw 'The QAC reservation and QEMU child do not match this helper invocation.' }
    $tcp = [System.Net.Sockets.TcpClient]::new()
    $tcp.ReceiveTimeout = 8000
    $tcp.SendTimeout = 8000
    $tcp.Connect('127.0.0.1', $Port)
    $stream = $tcp.GetStream()
    $stream.ReadTimeout = 8000
    $stream.WriteTimeout = 8000
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $reader = [System.IO.StreamReader]::new($stream, $utf8, $false, 4096, $true)
    $writer = [System.IO.StreamWriter]::new($stream, $utf8, 4096, $true)
    $writer.NewLine = "`r`n"
    $writer.AutoFlush = $true
    $greeting = $reader.ReadLine()
    Add-Content -LiteralPath $transcriptPath -Value "$([DateTime]::UtcNow.ToString('o')) RX $greeting" -Encoding utf8
    [void](Send-Qmp @{execute='qmp_capabilities'})
    [void](Send-Qmp @{execute='query-status'})

    switch ($Action) {
        'inspect' {
            [void](Send-Qmp @{execute='query-mice'})
            foreach ($commandLine in @('info mice', 'help mouse_move', 'help mouse_button')) {
                [void](Send-Qmp @{execute='human-monitor-command'; arguments=@{'command-line'=$commandLine}})
            }
        }
        'screenshot' {
            [void](Send-Qmp @{execute='human-monitor-command'; arguments=@{'command-line'="screendump $Value"}})
        }
        'key' {
            Send-Key $Value
        }
        'move' {
            $parts = $Value.Split(',')
            if ($parts.Count -ne 2) { throw 'Move value must be dx,dy.' }
            $dx = [int]$parts[0]
            $dy = [int]$parts[1]
            if ([Math]::Abs($dx) -gt 49 -or [Math]::Abs($dy) -gt 49) {
                throw 'Move step exceeds the guest PS/2 driver safe range; both axes must be within -49..49.'
            }
            if (-not (Test-OwnedQacGuest)) { throw 'QAC ownership changed; no pointer input was sent.' }
            $events = @()
            if ($dx -ne 0) { $events += @{type='rel'; data=@{axis='x'; value=$dx}} }
            if ($dy -ne 0) { $events += @{type='rel'; data=@{axis='y'; value=$dy}} }
            if ($events.Count -eq 0) { throw 'Move must include a non-zero delta.' }
            [void](Send-Qmp @{execute='input-send-event'; arguments=@{events=$events}})
        }
        'click' {
            if (-not (Test-OwnedQacGuest)) { throw 'QAC ownership changed; no pointer button input was sent.' }
            $button = $Value
            if ($button -notin @('left', 'middle', 'right')) { throw 'Button must be left, middle, or right.' }
            $script:buttonMayBeDown = $true
            [void](Send-Qmp @{execute='input-send-event'; arguments=@{events=@(@{type='btn'; data=@{down=$true; button=$button}})}})
            Start-Sleep -Milliseconds 500
            if (-not (Test-OwnedQacGuest)) { throw 'QAC ownership changed while the button was down.' }
            [void](Send-Qmp @{execute='input-send-event'; arguments=@{events=@(@{type='btn'; data=@{down=$false; button=$button}})}})
            $script:buttonMayBeDown = $false
            Start-Sleep -Milliseconds 500
        }
        'hmp' {
            if ($Value -notin @('mouse_set 2', 'mouse_button 0', 'quit')) { throw 'Only the bounded diagnostic HMP and owned-guest shutdown commands are allowed.' }
            if (-not (Test-OwnedQacGuest)) { throw 'QAC ownership changed; no HMP command was sent.' }
            [void](Send-Qmp @{execute='human-monitor-command'; arguments=@{'command-line'=$Value}})
        }
        'hmpkey' {
            if ($Value -notin @('sendkey meta_l', 'sendkey tab', 'sendkey down')) { throw 'Only the evidenced QEMU keyboard controls are allowed.' }
            [void](Send-Qmp @{execute='human-monitor-command'; arguments=@{'command-line'=$Value}})
            Start-Sleep -Milliseconds 500
        }
    }
    Add-Content -LiteralPath $transcriptPath -Value "$([DateTime]::UtcNow.ToString('o')) ACTION_COMPLETE $Action $Value" -Encoding utf8
} finally {
    if ($script:buttonMayBeDown -and $tcp -and $tcp.Connected -and $writer -and $reader) {
        try {
            if (Test-OwnedQacGuest) {
                [void](Send-Qmp @{execute='input-send-event'; arguments=@{events=@(@{type='btn'; data=@{down=$false; button=$Value}})}})
                Add-Content -LiteralPath $transcriptPath -Value "$([DateTime]::UtcNow.ToString('o')) FAILSAFE_BUTTON_RELEASE sent button=$Value" -Encoding utf8
            } else {
                Add-Content -LiteralPath $transcriptPath -Value "$([DateTime]::UtcNow.ToString('o')) FAILSAFE_BUTTON_RELEASE skipped qac-ownership-not-verified" -Encoding utf8
            }
        } catch {
            Add-Content -LiteralPath $transcriptPath -Value "$([DateTime]::UtcNow.ToString('o')) FAILSAFE_BUTTON_RELEASE failed error=$($_.Exception.Message)" -Encoding utf8
        }
    }
    if ($reader) { $reader.Dispose() }
    if ($writer) { $writer.Dispose() }
    if ($tcp) { $tcp.Dispose() }
}
