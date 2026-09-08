[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$TimeoutSeconds = 900,
    [switch]$SkipBuild,
    [switch]$InteractiveOnly,
    [switch]$SkipHistoricalRegressions
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase7'
if (!$SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-aarch64-phase7.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
    if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 7 build failed' }
}

# QMP command ACKs mean that QEMU accepted the request, not that the
# virtio-input device has delivered every report to the guest.  Keep each
# command and guest watermark below the device-side queue/coalescing boundary.
$script:QmpBatchSize = 8
$script:WatermarkBatchSize = 32

function Read-QmpMessage([hashtable]$Session, [int]$TimeoutMs = 10000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $buffer = New-Object byte[] 8192
    while ([DateTime]::UtcNow -lt $deadline) {
        $newline = $Session.Buffer.IndexOf([char]10)
        if ($newline -ge 0) {
            $line = $Session.Buffer.Substring(0, $newline + 1)
            $Session.Buffer = $Session.Buffer.Substring($newline + 1)
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            try { $object = $line | ConvertFrom-Json -ErrorAction Stop }
            catch { throw "Malformed QMP response: $line" }
            return @{ Raw = $line; Object = $object }
        }
        if ($Session.Stream.DataAvailable) {
            $count = $Session.Stream.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) {
                $Session.Buffer += [Text.Encoding]::UTF8.GetString($buffer, 0, $count)
            }
        } else {
            Start-Sleep -Milliseconds 2
        }
    }
    throw ("QMP response timeout after {0}ms" -f $TimeoutMs)
}

function Invoke-QmpCommand([hashtable]$Session, [string]$Execute, [hashtable]$Arguments) {
    $id = 'guidexos-' + $Session.NextId
    ++$Session.NextId
    $command = @{ execute = $Execute; id = $id }
    if ($Arguments.Count -ne 0) { $command.arguments = $Arguments }
    $json = $command | ConvertTo-Json -Compress -Depth 20
    $bytes = [Text.Encoding]::UTF8.GetBytes($json + ([char]13) + ([char]10))
    $Session.Stream.Write($bytes, 0, $bytes.Length)
    $Session.Stream.Flush()
    if ($Execute -eq 'input-send-event') { ++$Session.InputCommandsSent }
    while ($true) {
        $message = Read-QmpMessage $Session
        if (!$message.Object.PSObject.Properties.Name.Contains('id')) { continue }
        if ([string]$message.Object.id -ne $id) { continue }
        if ($message.Object.PSObject.Properties.Name.Contains('error')) {
            throw "QMP command '$Execute' failed: $($message.Raw.Trim())"
        }
        if ($Execute -eq 'input-send-event') { ++$Session.InputCommandsAcknowledged }
        return $message.Object
    }
}

function Start-QmpInputRequest([hashtable]$Session, [object[]]$Events) {
    if ($Events.Count -eq 0 -or $Events.Count -gt $script:QmpBatchSize) {
        throw "Impossible pipelined input batch size $($Events.Count); maximum is $script:QmpBatchSize"
    }
    $id = 'guidexos-' + $Session.NextId
    ++$Session.NextId
    $command = @{ execute = 'input-send-event'; id = $id; arguments = @{ events = @($Events) } }
    $json = $command | ConvertTo-Json -Compress -Depth 20
    $bytes = [Text.Encoding]::UTF8.GetBytes($json + ([char]13) + ([char]10))
    $Session.Stream.Write($bytes, 0, $bytes.Length)
    $Session.Stream.Flush()
    ++$Session.InputCommandsSent
    return $id
}

function Complete-QmpInputRequests([hashtable]$Session, [object[]]$Ids) {
    $pending = @{}
    foreach ($id in $Ids) { $pending[[string]$id] = $true }
    while ($pending.Count -ne 0) {
        $message = Read-QmpMessage $Session
        if (!$message.Object.PSObject.Properties.Name.Contains('id')) { continue }
        $id = [string]$message.Object.id
        if (!$pending.ContainsKey($id)) { throw "Unmatched QMP response id '$id'" }
        if ($message.Object.PSObject.Properties.Name.Contains('error')) {
            throw "QMP input-send-event failed: $($message.Raw.Trim())"
        }
        $pending.Remove($id)
        ++$Session.InputCommandsAcknowledged
    }
}

function Open-QmpSession([int]$Port) {
    $client = [Net.Sockets.TcpClient]::new()
    $connected = $false
    for ($attempt = 0; $attempt -lt 100 -and !$connected; ++$attempt) {
        try { $client.Connect('127.0.0.1', $Port); $connected = $true }
        catch { Start-Sleep -Milliseconds 100 }
    }
    if (!$connected) { $client.Dispose(); throw "QMP connection failed on port $Port" }
    $stream = $client.GetStream()
    $stream.WriteTimeout = 10000
    $stream.ReadTimeout = 10000
    $session = @{
        Client = $client
        Stream = $stream
        Buffer = ''
        NextId = 1
        InputCommandsSent = 0
        InputCommandsAcknowledged = 0
        InputEventsSent = 0
        InputRetries = 0
    }
    try {
        $greeting = Read-QmpMessage $session 10000
        if (!$greeting.Object.PSObject.Properties.Name.Contains('QMP')) {
            throw 'QMP greeting did not contain QMP capabilities'
        }
        $null = Invoke-QmpCommand $session 'qmp_capabilities' @{}
        $commands = Invoke-QmpCommand $session 'query-commands' @{}
        $hasInputSendEvent = @($commands.return | Where-Object { $_.name -eq 'input-send-event' }).Count -gt 0
        if (!$hasInputSendEvent) { throw 'QMP input-send-event is not advertised' }
        $version = Invoke-QmpCommand $session 'query-version' @{}
        Write-Host ("QMP connected: QEMU {0}.{1}.{2}, input-send-event=available" -f
            $version.return.qemu.major, $version.return.qemu.minor, $version.return.qemu.micro) -ForegroundColor DarkGray
        return $session
    } catch {
        $client.Dispose()
        throw
    }
}

function New-AbsEvent([int]$Value, [string]$Axis = 'x') {
    return @{ type = 'abs'; data = @{ axis = $Axis; value = $Value } }
}
function New-ButtonEvent([bool]$Down) {
    return @{ type = 'btn'; data = @{ button = 'left'; down = $Down } }
}
function New-KeyEvent([bool]$Down, [string]$Key = 'a') {
    return @{ type = 'key'; data = @{ key = @{ type = 'qcode'; data = $Key }; down = $Down } }
}
function Raw-Coordinate([int]$Pixel, [int]$Limit) {
    $value = [int][Math]::Round(($Pixel * 32767.0) / $Limit)
    return [Math]::Max(0, [Math]::Min(32767, $value))
}
function Add-Event([Collections.ArrayList]$List, [object]$Event) {
    [void]$List.Add($Event)
}
function Send-InputBatch([hashtable]$Session, [object[]]$Events) {
    if ($Events.Count -eq 0) { return }
    if ($Events.Count -gt $script:QmpBatchSize) {
        throw "Impossible input batch size $($Events.Count); maximum is $script:QmpBatchSize"
    }
    $null = Invoke-QmpCommand $Session 'input-send-event' @{ events = @($Events) }
    $Session.InputEventsSent += $Events.Count
}
function Send-Pointer([hashtable]$Session, [int]$X, [int]$Y) {
    # Keep axis reports as distinct acknowledged hardware commands.  This
    # matches the established high-level proof sequence and makes the tablet
    # position unambiguous across QEMU's input queue; durability pointer
    # reports use their own bounded batch path below.
    Send-InputBatch $Session @((New-AbsEvent (Raw-Coordinate $X 799) 'x'))
    Start-Sleep -Milliseconds 25
    Send-InputBatch $Session @((New-AbsEvent (Raw-Coordinate $Y 599) 'y'))
}
function Send-Button([hashtable]$Session, [bool]$Down) {
    Send-InputBatch $Session @((New-ButtonEvent $Down))
}
function Send-Key([hashtable]$Session, [string]$Key, [bool]$Down) {
    Send-InputBatch $Session @((New-KeyEvent $Down $Key))
}

function Read-GuestText([string]$LogPath) {
    if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) { return '' }
    $text = Get-Content -Raw -LiteralPath $LogPath
    if ($null -eq $text) { return '' }
    return [string]$text
}
function Assert-GuestHealthy([string]$Text, [string]$Context) {
    if ($Text -match 'AARCH64_PHASE[23456]_ERROR|AARCH64_PHASE7_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)') {
        throw "$Context contains a guest error marker"
    }
}
function Wait-GuestMarker([string]$LogPath, [string]$Marker, [Diagnostics.Process]$Process,
                          [DateTime]$Deadline) {
    while ([DateTime]::UtcNow -lt $Deadline) {
        $text = Read-GuestText $LogPath
        Assert-GuestHealthy $text "waiting for $Marker"
        if ($text.Contains($Marker)) { return $text }
        if ($Process.HasExited) { throw "QEMU exited before guest marker: $Marker" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for guest marker: $Marker"
}
function Get-GuestProgress([string]$Text) {
    $pattern = 'input stress progress pointer=(\d+) buttons=(\d+) keyboard=(\d+) queue=(\d+) queue-high-water=(\d+) drops=(\d+) coalesced=(\d+) virtio-irq=(\d+) virtio-polls=(\d+) virtio-drains=(\d+) unknown-irq=(\d+)'
    $matches = [regex]::Matches($Text, $pattern)
    if ($matches.Count -eq 0) { return $null }
    $m = $matches[$matches.Count - 1]
    return [pscustomobject]@{
        Pointer = [uint64]$m.Groups[1].Value; Buttons = [uint64]$m.Groups[2].Value
        Keyboard = [uint64]$m.Groups[3].Value; Queue = [uint32]$m.Groups[4].Value
        QueueHighWater = [uint32]$m.Groups[5].Value; Drops = [uint64]$m.Groups[6].Value
        Coalesced = [uint64]$m.Groups[7].Value; Irq = [uint64]$m.Groups[8].Value
        Polls = [uint64]$m.Groups[9].Value; Drains = [uint64]$m.Groups[10].Value
        UnknownIrq = [uint32]$m.Groups[11].Value
    }
}
function Wait-GuestWatermark([string]$LogPath, [Diagnostics.Process]$Process,
                             [DateTime]$Deadline, [hashtable]$Target,
                             [int]$CheckpointTimeoutMs = 0) {
    $waitDeadline = $Deadline
    if ($CheckpointTimeoutMs -gt 0) {
        $checkpointDeadline = [DateTime]::UtcNow.AddMilliseconds($CheckpointTimeoutMs)
        if ($checkpointDeadline -lt $waitDeadline) { $waitDeadline = $checkpointDeadline }
    }
    while ([DateTime]::UtcNow -lt $waitDeadline) {
        $text = Read-GuestText $LogPath
        Assert-GuestHealthy $text 'guest watermark wait'
        $progress = Get-GuestProgress $text
        if ($progress -and $progress.Pointer -ge $Target.Pointer -and
            $progress.Buttons -ge $Target.Buttons -and
            $progress.Keyboard -ge $Target.Keyboard) { return $progress }
        if ($Process.HasExited) { throw 'QEMU exited during guest watermark wait' }
        Start-Sleep -Milliseconds 50
    }
    $last = Get-GuestProgress (Read-GuestText $LogPath)
    $detail = if ($last) { "pointer=$($last.Pointer) buttons=$($last.Buttons) keyboard=$($last.Keyboard)" } else { 'no telemetry checkpoint' }
    if ($CheckpointTimeoutMs -gt 0) { return $null }
    throw "Timed out waiting for guest watermark pointer=$($Target.Pointer) buttons=$($Target.Buttons) keyboard=$($Target.Keyboard): $detail"
}
function Send-StressClass([hashtable]$Session, [object[]]$Events, [string]$Class,
                          [hashtable]$Baseline, [Diagnostics.Process]$Process,
                          [string]$LogPath, [DateTime]$Deadline, [hashtable]$HostCounts) {
    # Use one eight-report command per guest watermark.  Only one command is
    # outstanding; a missing guest watermark can therefore be retried without
    # allowing an unbounded producer backlog.
    $groupSize = 8
    $interGroupDelayMs = if ($Class -eq 'pointer') { 120 } else { 50 }
    $checkpointWindow = $script:WatermarkBatchSize
    for ($offset = 0; $offset -lt $Events.Count; $offset += $groupSize) {
        $end = [Math]::Min($Events.Count, $offset + $groupSize)
        $group = [Collections.ArrayList]::new()
        for ($index = $offset; $index -lt $end; ++$index) { [void]$group.Add($Events[$index]) }
        $groupCount = $end - $offset
        if ($Class -eq 'pointer') { $HostCounts.PointerSent += $groupCount }
        elseif ($Class -eq 'buttons') { $HostCounts.ButtonsSent += $groupCount }
        else { $HostCounts.KeyboardSent += $groupCount }
        $submittedClassCount = if ($Class -eq 'pointer') { $HostCounts.PointerSent }
            elseif ($Class -eq 'buttons') { $HostCounts.ButtonsSent } else { $HostCounts.KeyboardSent }
        $shouldCheckpoint = ($submittedClassCount % $checkpointWindow) -eq 0 -or $end -eq $Events.Count
        $target = @{
            Pointer = $Baseline.Pointer + $HostCounts.PointerSent
            Buttons = $Baseline.Buttons + $HostCounts.ButtonsSent
            Keyboard = $Baseline.Keyboard + $HostCounts.KeyboardSent
        }
        $progress = $null
        for ($attempt = 0; $attempt -lt 4 -and $null -eq $progress; ++$attempt) {
            if ($attempt -gt 0) {
                ++$Session.InputRetries
                Write-Host ("QMP watermark retry class={0} checkpoint={1} attempt={2}" -f
                    $Class, $submittedClassCount, $attempt) -ForegroundColor DarkYellow
            }
            $ids = [Collections.ArrayList]::new()
            [void]$ids.Add((Start-QmpInputRequest $Session $group.ToArray()))
            $Session.InputEventsSent += $groupCount
            Complete-QmpInputRequests $Session $ids.ToArray()
            Start-Sleep -Milliseconds $interGroupDelayMs
            if ($shouldCheckpoint) {
                $progress = Wait-GuestWatermark $LogPath $Process $Deadline $target 2000
            } else {
                $progress = [pscustomobject]@{ Pointer = 0; Buttons = 0; Keyboard = 0 }
            }
        }
        if ($shouldCheckpoint -and $null -eq $progress) {
            throw "Guest watermark did not advance after bounded retries class=$Class checkpoint=$submittedClassCount"
        }
    }
}
function Test-HostNegativeControls {
    $failed = $false
    try {
        $fake = @{ InputEventsSent = 0; InputCommandsSent = 0; InputCommandsAcknowledged = 0 }
        Send-InputBatch $fake (1..33 | ForEach-Object { New-AbsEvent $_ })
    } catch { $failed = $true }
    if (!$failed) { throw 'negative control failed: impossible batch size was accepted' }
    try {
        '{"return":' | ConvertFrom-Json -ErrorAction Stop
        throw 'malformed response accepted'
    } catch {
        if ($_.Exception.Message -eq 'malformed response accepted') { throw }
    }
    Write-Host 'AARCH64_PHASE7_HOST_CONTROLS_PASS (QMP framing, batch bound, malformed-response fail-closed)' -ForegroundColor Green
}

function Invoke-InteractiveBoot([int]$Run, [int]$Port, [string]$VarsPath, [string]$LogPath) {
    $esp = Join-Path $artifactDirectory 'esp'
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
        Remove-Item -LiteralPath $LogPath -Force
    }
    $arguments = @(
        '-machine','virt,gic-version=2,acpi=off','-cpu','cortex-a53','-m','512M',
        '-drive',"if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
        '-drive',"if=pflash,format=raw,unit=1,file=$VarsPath",
        '-drive',"file=fat:rw:$esp,format=raw",'-display','none','-device','ramfb',
        '-global','virtio-mmio.force-legacy=false','-device','virtio-keyboard-device','-device','virtio-tablet-device',
        '-qmp',"tcp:127.0.0.1:$Port,server=on,wait=off",'-serial',"file:$LogPath",'-no-reboot')
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $QemuPath
    $info.Arguments = (($arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $null = $process.Start()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $session = $null
    $hostCounts = $null
    $stressCommandBase = 0
    $stressAckBase = 0
    $stressEventBase = 0
    $stressClock = $null
    try {
        $null = Wait-GuestMarker $LogPath '[guideXOS] input proof window: ready' $process $deadline
        # The proof-window marker is emitted before the desktop's first
        # post-launch compositor settle.  Preserve the established bounded
        # boot settle interval before submitting the first real tablet report.
        Start-Sleep -Milliseconds 10000
        $session = Open-QmpSession $Port
        Send-Pointer $session 200 130
        $null = Wait-GuestMarker $LogPath '[guideXOS] cursor routing: PASS' $process $deadline
        Send-Button $session $true
        $null = Wait-GuestMarker $LogPath '[guideXOS] focus routing: PASS' $process $deadline
        Send-Pointer $session 340 200
        Start-Sleep -Milliseconds 300
        Send-Button $session $false
        Start-Sleep -Milliseconds 150
        Send-Button $session $false
        $null = Wait-GuestMarker $LogPath '[guideXOS] mouse button routing: PASS' $process $deadline
        $null = Wait-GuestMarker $LogPath '[guideXOS] window drag: PASS' $process $deadline
        Send-Pointer $session 480 290
        Start-Sleep -Milliseconds 150
        Send-Button $session $true
        Start-Sleep -Milliseconds 150
        Send-Button $session $false
        Start-Sleep -Milliseconds 150
        foreach ($key in @('g','u','i','d','e','x','o','s')) {
            Send-Key $session $key $true
            Send-Key $session $key $false
            Start-Sleep -Milliseconds 50
        }
        $null = Wait-GuestMarker $LogPath '[guideXOS] text input: PASS value=guidexos' $process $deadline
        Send-Pointer $session 50 580
        Send-Button $session $true
        Send-Button $session $false
        $null = Wait-GuestMarker $LogPath '[guideXOS] Start button input: PASS' $process $deadline
        if ($InteractiveOnly) { return [pscustomobject]@{ Text = Read-GuestText $LogPath } }

        # The guest serializes the marker in several short writes.  Do not
        # treat the prefix alone as a complete checkpoint.
        $ready = [regex]::Match('', 'never')
        while ([DateTime]::UtcNow -lt $deadline -and !$ready.Success) {
            $readyText = Read-GuestText $LogPath
            Assert-GuestHealthy $readyText 'waiting for complete stress-ready marker'
            $ready = [regex]::Match($readyText,
                'input stress: ready pointer-base=(\d+) button-base=(\d+) keyboard-base=(\d+)')
            if (!$ready.Success) {
                if ($process.HasExited) { throw "QEMU exited before complete stress-ready marker in boot $Run" }
                Start-Sleep -Milliseconds 50
            }
        }
        if (!$ready.Success) { throw "Timed out waiting for complete stress-ready marker in boot $Run" }
        $baseline = @{
            Pointer = [uint64]$ready.Groups[1].Value
            Buttons = [uint64]$ready.Groups[2].Value
            Keyboard = [uint64]$ready.Groups[3].Value
        }
        $hostCounts = @{ PointerSent = 0; ButtonsSent = 0; KeyboardSent = 0 }
        $stressCommandBase = $session.InputCommandsSent
        $stressAckBase = $session.InputCommandsAcknowledged
        $stressEventBase = $session.InputEventsSent
        $stressClock = [Diagnostics.Stopwatch]::StartNew()
        $pointerStress = [Collections.ArrayList]::new()
        # Pad to the 32-report guest telemetry boundary so the final host
        # watermark is observable without printing once per event.
        for ($i = 0; $i -lt 10016; ++$i) { Add-Event $pointerStress (New-AbsEvent (Raw-Coordinate (($i % 700) + 40) 799) 'x') }
        Send-StressClass $session @($pointerStress) 'pointer' $baseline $process $LogPath $deadline $hostCounts
        Send-Pointer $session 740 80

        $buttonStress = [Collections.ArrayList]::new()
        for ($i = 0; $i -lt 1024; ++$i) { Add-Event $buttonStress (New-ButtonEvent (($i % 2) -eq 0)) }
        Send-StressClass $session @($buttonStress) 'buttons' $baseline $process $LogPath $deadline $hostCounts

        $keyboardStress = [Collections.ArrayList]::new()
        for ($i = 0; $i -lt 1024; ++$i) { Add-Event $keyboardStress (New-KeyEvent (($i % 2) -eq 0) 'a') }
        Send-StressClass $session @($keyboardStress) 'keyboard' $baseline $process $LogPath $deadline $hostCounts

        $null = Wait-GuestMarker $LogPath 'AARCH64_PHASE7_PASS' $process $deadline
        $stressClock.Stop()
        $text = Read-GuestText $LogPath
        Assert-GuestHealthy $text "complete Phase-7 boot $Run"
        return [pscustomobject]@{
            Text = $text
            Baseline = $baseline
            Counts = $hostCounts
            StressDurationMs = $stressClock.ElapsedMilliseconds
            StressQmpCommandsSent = $session.InputCommandsSent - $stressCommandBase
            StressQmpCommandsAcknowledged = $session.InputCommandsAcknowledged - $stressAckBase
            StressQmpEventsSent = $session.InputEventsSent - $stressEventBase
            StressQmpRetries = $session.InputRetries
        }
    } catch {
        if ($session) {
            $pointerSent = if ($hostCounts) { $hostCounts.PointerSent } else { 0 }
            $buttonsSent = if ($hostCounts) { $hostCounts.ButtonsSent } else { 0 }
            $keyboardSent = if ($hostCounts) { $hostCounts.KeyboardSent } else { 0 }
            $stressSeconds = if ($stressClock) {
                [Math]::Max(0.001, $stressClock.ElapsedMilliseconds / 1000.0)
            } else { 0.0 }
            Write-Host ("Boot {0}: host failure stress intended pointer={1} buttons={2} keyboard={3}; QMP commands sent/acknowledged={4}/{5}; events={6}; retries={7}" -f
                $Run, $pointerSent, $buttonsSent, $keyboardSent,
                ($session.InputCommandsSent - $stressCommandBase),
                ($session.InputCommandsAcknowledged - $stressAckBase),
                ($session.InputEventsSent - $stressEventBase),
                $session.InputRetries) -ForegroundColor DarkYellow
            if ($stressClock) {
                Write-Host ("Boot {0}: failed stress duration={1:N2}s; throughput pointer={2:N1}/s buttons={3:N1}/s keyboard={4:N1}/s QMP-ack={5:N1}/s" -f
                    $Run, $stressSeconds, ($pointerSent / $stressSeconds), ($buttonsSent / $stressSeconds),
                    ($keyboardSent / $stressSeconds),
                    (($session.InputCommandsAcknowledged - $stressAckBase) / $stressSeconds)) -ForegroundColor DarkYellow
            }
        }
        throw
    } finally {
        if ($session) { try { $session.Client.Dispose() } catch {} }
        if (!$process.HasExited) { try { $process.Kill() } catch {} }
        $process.WaitForExit()
    }
}

Test-HostNegativeControls
$varsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
if (!(Test-Path -LiteralPath $varsTemplate -PathType Leaf)) { throw "AArch64 UEFI variable template not found: $varsTemplate" }
$logsDirectory = Join-Path $artifactDirectory 'logs'
$null = New-Item -ItemType Directory -Path $logsDirectory -Force
for ($run = 1; $run -le 3; ++$run) {
    $varsPath = Join-Path $artifactDirectory ("edk2-aarch64-phase7-vars-{0}.fd" -f $run)
    Copy-Item -LiteralPath $varsTemplate -Destination $varsPath -Force
    $logPath = Join-Path $logsDirectory ("phase7-boot-{0}.log" -f $run)
    $port = 4760 + $run
    Write-Host "Starting fresh AArch64 Phase 7 complete QEMU boot $run/3..." -ForegroundColor Yellow
    $runResult = Invoke-InteractiveBoot $run $port $varsPath $logPath
    $text = $runResult.Text
    if ($InteractiveOnly) {
        $requiredInteractive = @(
            '[guideXOS] input device discovery: OK','[guideXOS] cursor routing: PASS',
            '[guideXOS] mouse button routing: PASS','[guideXOS] focus routing: PASS',
            '[guideXOS] window drag: PASS','[guideXOS] keyboard routing: PASS',
            '[guideXOS] text input: PASS value=guidexos','[guideXOS] Start button input: PASS')
        foreach ($marker in $requiredInteractive) {
            if (!$text.Contains($marker)) { throw "Interactive-only boot $run is missing marker: $marker" }
        }
        Write-Host "Fresh Phase 7 interactive routing boot $run/3: PASS (durability not claimed)" -ForegroundColor Yellow
        continue
    }
    $required = @(
        '[guideXOS] input device discovery: OK','[guideXOS] keyboard device: initialized',
        '[guideXOS] pointing device: initialized','[guideXOS] input IRQ registry: OK',
        '[guideXOS] common input queue: OK','[guideXOS] cursor routing: PASS',
        '[guideXOS] mouse button routing: PASS','[guideXOS] focus routing: PASS',
        '[guideXOS] window drag: PASS','[guideXOS] keyboard routing: PASS',
        '[guideXOS] text input: PASS value=guidexos','[guideXOS] Start button input: PASS',
        '[guideXOS] input stress pointer: PASS count=','[guideXOS] input stress buttons: PASS count=',
        '[guideXOS] input stress keyboard: PASS count=','[guideXOS] input queue integrity: PASS',
        '[guideXOS] input final state: PASS','[guideXOS] input durability: PASS',
        '[guideXOS] input/scheduler integration: PASS','[guideXOS] framebuffer interaction verification: PASS',
        '[guideXOS] graphics/scheduler integration: PASS','[guideXOS] App Model durability: PASS',
        'AARCH64_PHASE7_PASS')
    $last = -1
    foreach ($marker in $required) {
        $index = $text.IndexOf($marker, [StringComparison]::Ordinal)
        if ($index -lt 0) { throw "Fresh Phase 7 boot $run is missing marker: $marker" }
        if ($index -le $last) { throw "Fresh Phase 7 boot $run markers are out of order: $marker" }
        $last = $index
    }
    if ($text -notmatch 'input durability: PASS pointer=1[0-9]{4,} buttons=1[0-9]{3,} keyboard=1[0-9]{3,}') {
        throw "Fresh Phase 7 boot $run did not meet the original durability thresholds"
    }
    if ($text -notmatch 'input durability stats:.*queue-depth=0.*dropped=0.*virtio-polls=[1-9][0-9]*.*virtio-drains=[1-9][0-9]*') {
        throw "Fresh Phase 7 boot $run lacks final queue/drain statistics"
    }
    if ($text -notmatch 'input durability stats:.*stress-pointer=1[0-9]{4,}.*stress-buttons=1[0-9]{3,}.*stress-keyboard=1[0-9]{3,}') {
        throw "Fresh Phase 7 boot $run lacks stress-only transport counts"
    }
    $stressSeconds = [Math]::Max(0.001, $runResult.StressDurationMs / 1000.0)
    Write-Host ("Boot {0}: host stress intended pointer={1} buttons={2} keyboard={3}; events={4}; QMP commands sent/acknowledged={5}/{6}; retries={7}; duration={8:N2}s; throughput pointer={9:N1}/s buttons={10:N1}/s keyboard={11:N1}/s QMP-ack={12:N1}/s" -f
        $run, $runResult.Counts.PointerSent, $runResult.Counts.ButtonsSent, $runResult.Counts.KeyboardSent,
        $runResult.StressQmpEventsSent, $runResult.StressQmpCommandsSent,
        $runResult.StressQmpCommandsAcknowledged, $runResult.StressQmpRetries, $stressSeconds,
        $runResult.Counts.PointerSent / $stressSeconds,
        $runResult.Counts.ButtonsSent / $stressSeconds,
        $runResult.Counts.KeyboardSent / $stressSeconds,
        $runResult.StressQmpCommandsAcknowledged / $stressSeconds) -ForegroundColor DarkCyan
    if ($text -notmatch 'unexpected-irq=0 exceptions=0') { throw "Fresh Phase 7 boot $run reports an IRQ or exception" }
    $read = [regex]::Match($text, 'input durability stats: pointer=(\d+) buttons=(\d+) keyboard=(\d+).*queue-high-water=(\d+) dropped=(\d+) coalesced=(\d+).*irq=(\d+).*virtio-polls=(\d+) virtio-drains=(\d+)')
    if (!$read.Success) { throw "Fresh Phase 7 boot $run lacks final transport statistics" }
    Write-Host ("Boot {0}: PASS stress pointer={1} buttons={2} keyboard={3} queue-high-water={4} drops={5} coalesced={6} irq={7} polls={8} drains={9}" -f
        $run, ([regex]::Match($text, 'stress-pointer=(\d+)').Groups[1].Value),
        ([regex]::Match($text, 'stress-buttons=(\d+)').Groups[1].Value),
        ([regex]::Match($text, 'stress-keyboard=(\d+)').Groups[1].Value),
        $read.Groups[4].Value, $read.Groups[5].Value, $read.Groups[6].Value, $read.Groups[7].Value,
        $read.Groups[8].Value, $read.Groups[9].Value) -ForegroundColor Green
}

if ($InteractiveOnly) {
    Write-Host 'AARCH64 Phase 7 interactive routing probe: PASS (durability not claimed)' -ForegroundColor Yellow
    exit 0
}
if (!$SkipHistoricalRegressions) {
    foreach ($phase in 1..4) {
        $scriptPath = Join-Path $PSScriptRoot "test-aarch64-phase$phase.ps1"
        $phaseTimeout = if ($phase -ge 3) { 240 } else { 180 }
        & $scriptPath -TimeoutSeconds $phaseTimeout
        if ($LASTEXITCODE -ne 0) { throw "Historical Phase $phase regression failed" }
    }
    & (Join-Path $PSScriptRoot 'test-aarch64-phase5.ps1') -TimeoutSeconds 240 -HistoricalTimeoutSeconds 240 -SkipHistoricalRegressions
    if ($LASTEXITCODE -ne 0) { throw 'Historical Phase 5 regression failed' }
    & (Join-Path $PSScriptRoot 'test-aarch64-phase6.ps1') -TimeoutSeconds 240 -HistoricalTimeoutSeconds 240 -SkipHistoricalRegressions
    if ($LASTEXITCODE -ne 0) { throw 'Historical Phase 6 regression failed' }
}
Write-Host 'AARCH64 Phase 7 test suite: PASS (QMP controls + three complete durability boots + historical regressions)' -ForegroundColor Green
exit 0
