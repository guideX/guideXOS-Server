param(
    [Parameter(Mandatory = $true)][AllowEmptyString()][string]$CommandText,
    [Parameter(Mandatory = $true)][string]$CaptureName,
    [int]$WaitSeconds = 3,
    [switch]$NoEnter,
    [switch]$ToggleStart,
    [switch]$OpenConsole,
    [switch]$OpenTerminal,
    [switch]$CloseWindow,
    [switch]$QuitQemu,
    [int]$TabCount = 0,
    [int]$DownCount = 0,
    [int]$BackspaceCount = 0,
    [int]$MoveDx = 0,
    [int]$MoveDy = 0,
    [switch]$ClickHere,
    [switch]$PressEnter,
    [int]$ClickX = -1,
    [int]$ClickY = -1
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$identity = Get-Content -Raw -LiteralPath (Join-Path $root 'qemu-owner.json') | ConvertFrom-Json
$process = Get-CimInstance Win32_Process -Filter "ProcessId = $($identity.pid)"
if (-not $process -or $process.ExecutablePath -ne $identity.qemuPath -or
    $process.CommandLine -notlike "*127.0.0.1:$($identity.qmpPort),*" -or
    $process.CommandLine -notlike "*$($identity.espPath)*") {
    throw 'RX24 QEMU identity no longer matches; no guest input was sent.'
}
$listener = Get-NetTCPConnection -LocalPort $identity.qmpPort -State Listen -ErrorAction SilentlyContinue
if (-not $listener -or $listener.OwningProcess -notcontains $identity.pid) {
    throw 'RX24 QMP listener ownership mismatch; no guest input was sent.'
}

$tcp = [System.Net.Sockets.TcpClient]::new()
$tcp.Connect('127.0.0.1', [int]$identity.qmpPort)
$stream = $tcp.GetStream()
$reader = [System.IO.StreamReader]::new($stream)
$writer = [System.IO.StreamWriter]::new($stream)
$writer.AutoFlush = $true
$greeting = $reader.ReadLine()
$writer.WriteLine('{"execute":"qmp_capabilities"}')
$capabilities = $reader.ReadLine()
if ($capabilities -notmatch '"return"') { throw "QMP capability setup failed: $capabilities" }
if ($QuitQemu) {
    $quitRequest = '{"execute":"quit"}'
    $writer.WriteLine($quitRequest)
    $quitAck = $reader.ReadLine()
    $shutdownRecord = [ordered]@{
        requestedAtUtc = [DateTime]::UtcNow.ToString('o')
        action = 'QMP quit for the verified owned QEMU process only'
        pid = $identity.pid
        qmpPort = $identity.qmpPort
        qemuPath = $identity.qemuPath
        espPath = $identity.espPath
        request = $quitRequest
        acknowledgement = $quitAck
    }
    $shutdownRecord | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'qemu-shutdown.json') -Encoding utf8
    $tcp.Close()
    Write-Output "Requested QMP shutdown for owned QEMU PID $($identity.pid) on port $($identity.qmpPort)."
    return
}

function Send-Rx12Key([string[]]$keys) {
    $items = @($keys | ForEach-Object { @{ type = 'qcode'; data = $_ } })
    $request = ConvertTo-Json -Compress -Depth 8 @{ execute = 'send-key'; arguments = @{ keys = $items } }
    $writer.WriteLine($request)
    $ack = $reader.ReadLine()
    if ($ack -notmatch '"return"') { throw "QMP rejected key '$($keys -join '+')': $ack" }
}

function Send-Rx12Click([int]$x, [int]$y) {
    $currentX = if ($null -ne $identity.pointerX) { [int]$identity.pointerX } else { 640 }
    $currentY = if ($null -ne $identity.pointerY) { [int]$identity.pointerY } else { 400 }
    $moves = @(
        @{ type = 'rel'; data = @{ axis = 'x'; value = ($x - $currentX) } },
        @{ type = 'rel'; data = @{ axis = 'y'; value = ($y - $currentY) } }
    )
    $request = ConvertTo-Json -Compress -Depth 8 @{ execute = 'input-send-event'; arguments = @{ events = $moves } }
    $writer.WriteLine($request)
    $ack = $reader.ReadLine()
    if ($ack -notmatch '"return"') { throw "QMP rejected mouse positioning: $ack" }
    Start-Sleep -Milliseconds 100
    foreach ($down in @($true, $false)) {
        $events = @(@{ type = 'btn'; data = @{ down = $down; button = 'left' } })
        $request = ConvertTo-Json -Compress -Depth 8 @{ execute = 'input-send-event'; arguments = @{ events = $events } }
        $writer.WriteLine($request)
        $ack = $reader.ReadLine()
        if ($ack -notmatch '"return"') { throw "QMP rejected mouse click: $ack" }
        Start-Sleep -Milliseconds 100
    }
    $identity | Add-Member -NotePropertyName pointerX -NotePropertyValue $x -Force
    $identity | Add-Member -NotePropertyName pointerY -NotePropertyValue $y -Force
    $identity | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'qemu-owner.json') -Encoding utf8
}

function Send-Rx12Move([int]$dx, [int]$dy) {
    $events = @(
        @{ type = 'rel'; data = @{ axis = 'x'; value = $dx } },
        @{ type = 'rel'; data = @{ axis = 'y'; value = $dy } }
    )
    $request = ConvertTo-Json -Compress -Depth 8 @{ execute = 'input-send-event'; arguments = @{ events = $events } }
    $writer.WriteLine($request)
    $ack = $reader.ReadLine()
    if ($ack -notmatch '"return"') { throw "QMP rejected mouse move: $ack" }
}

function Send-Rx12ClickHere {
    foreach ($down in @($true, $false)) {
        $events = @(@{ type = 'btn'; data = @{ down = $down; button = 'left' } })
        $request = ConvertTo-Json -Compress -Depth 8 @{ execute = 'input-send-event'; arguments = @{ events = $events } }
        $writer.WriteLine($request)
        $ack = $reader.ReadLine()
        if ($ack -notmatch '"return"') { throw "QMP rejected mouse click: $ack" }
        Start-Sleep -Milliseconds 100
    }
}

if ($ToggleStart) {
    Send-Rx12Key @('meta_l')
    Start-Sleep -Seconds 1
}
if ($CloseWindow) {
    Send-Rx12Key @('alt', 'f4')
    Start-Sleep -Seconds 1
}
if ($OpenConsole) {
    Send-Rx12Key @('meta_l')
    Start-Sleep -Milliseconds 500
    Send-Rx12Key @('down')
    Send-Rx12Key @('down')
    Send-Rx12Key @('ret')
    Start-Sleep -Seconds 2
}
if ($OpenTerminal) {
    $request = ConvertTo-Json -Compress -Depth 6 @{ execute = 'human-monitor-command'; arguments = @{ 'command-line' = 'sendkey 0x29' } }
    $writer.WriteLine($request)
    $ack = $reader.ReadLine()
    if ($ack -notmatch '"return"') { throw "QMP terminal hotkey failed: $ack" }
    Start-Sleep -Seconds 2
}
for ($keyIndex = 0; $keyIndex -lt $TabCount; $keyIndex++) { Send-Rx12Key @('tab') }
for ($keyIndex = 0; $keyIndex -lt $DownCount; $keyIndex++) { Send-Rx12Key @('down') }
for ($keyIndex = 0; $keyIndex -lt $BackspaceCount; $keyIndex++) { Send-Rx12Key @('backspace') }
if ($PressEnter) { Send-Rx12Key @('ret') }
if ($MoveDx -ne 0 -or $MoveDy -ne 0) {
    Send-Rx12Move $MoveDx $MoveDy
    Start-Sleep -Seconds 1
}
if ($ClickHere) {
    Send-Rx12ClickHere
    Start-Sleep -Seconds 1
}
if ($ClickX -ge 0 -and $ClickY -ge 0) {
    Send-Rx12Click $ClickX $ClickY
    Start-Sleep -Seconds 1
}

foreach ($character in $CommandText.ToCharArray()) {
    $key = $null
    $combo = $null
    if ($character -ge 'a' -and $character -le 'z') { $key = [string]$character }
    elseif ($character -ge 'A' -and $character -le 'Z') { $key = ([string]$character).ToLowerInvariant(); $combo = @('shift', $key) }
    elseif ($character -ge '0' -and $character -le '9') { $key = [string]$character }
    else {
        switch ([string]$character) {
            ' ' { $key = 'spc' }
            '.' { $key = 'dot' }
            '/' { $key = 'slash' }
            '-' { $key = 'minus' }
            '_' { $key = 'minus'; $combo = @('shift', 'minus') }
            '$' { $key = '4'; $combo = @('shift', '4') }
            ':' { $key = 'semicolon'; $combo = @('shift', 'semicolon') }
            '?' { $key = 'slash'; $combo = @('shift', 'slash') }
            default { throw "No QKeyCode mapping for '$character'." }
        }
    }
    if ($combo) { Send-Rx12Key $combo } else { Send-Rx12Key @($key) }
    Start-Sleep -Milliseconds 100
}
if (-not $NoEnter) { Send-Rx12Key @('ret') }
Start-Sleep -Seconds $WaitSeconds

$rawCaptureRoot = [System.IO.Path]::GetFullPath($identity.rawCaptureDir)
New-Item -ItemType Directory -Path $rawCaptureRoot -Force | Out-Null
$ppmPath = Join-Path $rawCaptureRoot ($CaptureName + '.ppm')
$pngPath = Join-Path $root ($CaptureName + '.png')
$dump = ConvertTo-Json -Compress -Depth 8 @{ execute = 'human-monitor-command'; arguments = @{ 'command-line' = "screendump $ppmPath" } }
$writer.WriteLine($dump)
$dumpAck = $reader.ReadLine()
$tcp.Close()
if ($dumpAck -notmatch '"return"') { throw "QMP screendump failed: $dumpAck" }

$logPath = Join-Path $root 'guest-commands.log'
$logEntry = "[$([DateTime]::UtcNow.ToString('o'))] $CommandText`nQMP screenshot: $ppmPath`n"
[System.IO.File]::AppendAllText($logPath, $logEntry, [System.Text.UTF8Encoding]::new($false))
python (Join-Path $root 'ppm_to_png.py') $ppmPath $pngPath
Get-Item -LiteralPath $pngPath | Select-Object FullName, Length
