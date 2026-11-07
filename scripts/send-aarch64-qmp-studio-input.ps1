[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$SessionPath,
    [int]$ClickX = -1,
    [int]$ClickY = -1,
    [string]$Text = '',
    [string[]]$SpecialKeys = @(),
    [string]$CapturePath = '',
    [switch]$RecordEditableControlCapture,
    [switch]$RefreshEditableControlCapture,
    [ValidateSet('project_open_path')][string]$EditableControlKind = 'project_open_path',
    [string]$ExpectedEditableText = 'guideXOS ARM64',
    [int]$FramebufferWidth = 800,
    [int]$FramebufferHeight = 600,
    [string]$QacCliPath = ''
)

$ErrorActionPreference = 'Stop'
$SessionPath = [IO.Path]::GetFullPath($SessionPath)
if (!(Test-Path -LiteralPath $SessionPath -PathType Leaf)) { throw "Session record is missing: $SessionPath" }
$session = Get-Content -LiteralPath $SessionPath -Raw | ConvertFrom-Json
if ($session.outcome -ne 'ABI3_STUDIO_GUEST_IN_PROGRESS' -or
    [string]::IsNullOrWhiteSpace([string]$session.reservation_id) -or
    [int]$session.qmp_port -lt 1 -or [int]$session.qmp_port -gt 65535) {
    throw 'QMP input requires an in-progress Studio session with a recorded reservation and QMP port.'
}
if (($ClickX -ge 0) -xor ($ClickY -ge 0)) { throw 'ClickX and ClickY must be supplied together.' }
if ($ClickX -lt 0 -and [string]::IsNullOrEmpty($Text) -and $SpecialKeys.Count -eq 0 -and
    [string]::IsNullOrWhiteSpace($CapturePath)) { throw 'Supply a screen click, text, special key, or capture path.' }
if ($RecordEditableControlCapture -and
    ([string]::IsNullOrWhiteSpace($CapturePath) -or [string]::IsNullOrEmpty($Text) -or
     $Text -cne $ExpectedEditableText)) {
    throw 'Recording editable-control evidence requires a capture and the exact expected test text.'
}
if ($RefreshEditableControlCapture -and
    ([string]::IsNullOrWhiteSpace($CapturePath) -or
     $ExpectedEditableText -cne 'guideXOS ARM64')) {
    throw 'Refreshing editable-control evidence requires a capture and the configured expected test text.'
}
if ($FramebufferWidth -lt 2 -or $FramebufferHeight -lt 2 -or
    ($ClickX -ge $FramebufferWidth) -or ($ClickY -ge $FramebufferHeight)) {
    throw 'Framebuffer dimensions or click coordinates are outside the declared screen bounds.'
}

if ([string]::IsNullOrWhiteSpace($QacCliPath)) { $QacCliPath = $env:GUIDEXOS_QAC_CLI }
if ([string]::IsNullOrWhiteSpace($QacCliPath)) {
    $repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $QacCliPath = Join-Path (Split-Path -Parent $repoRoot) 'guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
}
if (!(Test-Path -LiteralPath $QacCliPath -PathType Leaf)) { throw "QAC CLI not found: $QacCliPath" }
$qacProcessId = [int]$session.qac_process_id
$childProcessId = [int]$session.managed_qemu_child_pid
$qacProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$qacProcessId" -ErrorAction Stop
$qemuProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$childProcessId" -ErrorAction Stop
if (!$qacProcess -or !$qemuProcess -or [int]$qemuProcess.ParentProcessId -ne $qacProcessId) {
    throw 'Recorded QAC process and its managed QEMU child are not both live with the recorded ownership.'
}
$qacStatus = & $QacCliPath status 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $qacStatus -notmatch '(?m)^Coordinator: Online\r?$' -or
    $qacStatus -match 'Recovery: Required' -or
    [regex]::Match($qacStatus, '(?m)^QEMU processes: (\d+) \((\d+) foreign\)\r?$').Groups[2].Value -ne '0') {
    throw "QAC is not in a verifiably healthy exclusive state.`n$qacStatus"
}
$qacList = & $QacCliPath list 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "Unable to verify the recorded QAC reservation.`n$qacList" }
$ownedPattern = '(?m)^\s*' + [regex]::Escape([string]$session.reservation_id) +
    ' \| guideXOSServer-AARCH64-ABI3-Studio \| acceptance \| Running \| queue \d+ \| [^\r\n]*\(PID ' +
    $qacProcessId + '\) \| owner Verified \| child PID ' + $childProcessId + '\r?$'
if ($qacList -notmatch $ownedPattern) {
    throw "QAC does not confirm the same Running reservation and verified managed child.`n$qacList"
}
$expectedQemuPath = [IO.Path]::GetFullPath([string]$session.qemu)
$actualQemuPath = [IO.Path]::GetFullPath([string]$qemuProcess.ExecutablePath)
$qemuCommandLine = [string]$qemuProcess.CommandLine
if (![string]::Equals($expectedQemuPath, $actualQemuPath, [StringComparison]::OrdinalIgnoreCase) -or
    $qemuCommandLine -notmatch ('(?i)(?:^|\s)"?-qmp"?\s+"?tcp:127\.0\.0\.1:' + [int]$session.qmp_port + '(?:,|"|\s)')) {
    throw 'The recorded child executable or QMP endpoint does not match the live QAC-owned process.'
}

function New-KeyEvent([string]$Qcode, [bool]$Down) {
    return [ordered]@{
        type = 'key'
        data = [ordered]@{
            key = [ordered]@{ type = 'qcode'; data = $Qcode }
            down = $Down
        }
    }
}

$events = [Collections.Generic.List[object]]::new()
if ($ClickX -ge 0) {
    $x = [int][Math]::Round($ClickX * 32767.0 / ($FramebufferWidth - 1))
    $y = [int][Math]::Round($ClickY * 32767.0 / ($FramebufferHeight - 1))
    $events.Add([ordered]@{ type = 'abs'; data = [ordered]@{ axis = 'x'; value = $x } })
    $events.Add([ordered]@{ type = 'abs'; data = [ordered]@{ axis = 'y'; value = $y } })
    $events.Add([ordered]@{ type = 'btn'; data = [ordered]@{ button = 'left'; down = $true } })
    $events.Add([ordered]@{ type = 'btn'; data = [ordered]@{ button = 'left'; down = $false } })
}
foreach ($character in $Text.ToCharArray()) {
    $qcode = $null
    $upper = $false
    if ($character -ge 'A' -and $character -le 'Z') {
        $qcode = [string]$character.ToString().ToLowerInvariant()
        $upper = $true
    } elseif ($character -ge 'a' -and $character -le 'z') {
        $qcode = [string]$character
    } elseif ($character -ge '0' -and $character -le '9') {
        $qcode = [string]$character
    } elseif ($character -eq ' ') {
        $qcode = 'spc'
    } elseif ($character -eq '/') {
        $qcode = 'slash'
    } else {
        throw "Unsupported QMP character in text input: U+$([int]$character.ToString('X4'))"
    }
    if ($upper) { $events.Add((New-KeyEvent 'shift' $true)) }
    $events.Add((New-KeyEvent $qcode $true))
    $events.Add((New-KeyEvent $qcode $false))
    if ($upper) { $events.Add((New-KeyEvent 'shift' $false)) }
}
foreach ($specialKey in $SpecialKeys) {
    if ($specialKey.Equals('ControlN', [StringComparison]::OrdinalIgnoreCase)) {
        $events.Add((New-KeyEvent 'ctrl' $true))
        $events.Add((New-KeyEvent 'n' $true))
        $events.Add((New-KeyEvent 'n' $false))
        $events.Add((New-KeyEvent 'ctrl' $false))
        continue
    }
    if ($specialKey.Equals('ControlShiftO', [StringComparison]::OrdinalIgnoreCase)) {
        $events.Add((New-KeyEvent 'ctrl' $true))
        $events.Add((New-KeyEvent 'shift' $true))
        $events.Add((New-KeyEvent 'o' $true))
        $events.Add((New-KeyEvent 'o' $false))
        $events.Add((New-KeyEvent 'shift' $false))
        $events.Add((New-KeyEvent 'ctrl' $false))
        continue
    }
    if ($specialKey.Equals('ReleaseShiftX', [StringComparison]::OrdinalIgnoreCase)) {
        $events.Add((New-KeyEvent 'x' $false))
        $events.Add((New-KeyEvent 'shift' $false))
        continue
    }
    $keyQcode = switch ($specialKey.ToLowerInvariant()) {
        'tab' { 'tab' }
        'enter' { 'ret' }
        'escape' { 'esc' }
        'backspace' { 'backspace' }
        'left' { 'left' }
        'right' { 'right' }
        'up' { 'up' }
        'down' { 'down' }
        'super' { 'meta_l' }
        default { throw "Unsupported special QMP key: $specialKey" }
    }
    $events.Add((New-KeyEvent $keyQcode $true))
    $events.Add((New-KeyEvent $keyQcode $false))
}
if (![string]::IsNullOrWhiteSpace($CapturePath)) {
    $CapturePath = [IO.Path]::GetFullPath($CapturePath)
    $sessionDirectory = [IO.Path]::GetDirectoryName($SessionPath).TrimEnd('\') + '\'
    if (!$CapturePath.StartsWith($sessionDirectory, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to write a screendump outside this QAC session directory: $CapturePath"
    }
    if ($RecordEditableControlCapture -and [IO.Path]::GetDirectoryName($CapturePath) -ne
        [IO.Path]::GetDirectoryName($SessionPath)) {
        throw 'Editable-control evidence must be stored in the QAC session directory.'
    }
}

$client = [Net.Sockets.TcpClient]::new()
try {
    $client.ReceiveTimeout = 3000
    $client.SendTimeout = 3000
    $client.Connect([Net.IPAddress]::Loopback, [int]$session.qmp_port)
    $stream = $client.GetStream()
    $stream.ReadTimeout = 3000
    $stream.WriteTimeout = 3000
    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
    $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 4096, $true)
    $writer.AutoFlush = $true
    $null = $reader.ReadLine()
    $commands = [Collections.Generic.List[object]]::new()
    $commands.Add(@{ qmp = @{ execute = 'qmp_capabilities' }; delay_after_ms = 0 })
    if ($events.Count -gt 0) {
        if ($ClickX -ge 0) {
            # QEMU coalesces absolute pointer motion and a press/release pair
            # when they are sent in one input batch. Acknowledge each stage so
            # the guest input queue observes a real click at the target.
            $commands.Add(@{
                qmp = @{ execute = 'input-send-event'; arguments = @{ events = @($events[0], $events[1]) } }
                delay_after_ms = 75
            })
            $commands.Add(@{
                qmp = @{ execute = 'input-send-event'; arguments = @{ events = @($events[2]) } }
                delay_after_ms = 75
            })
            $commands.Add(@{
                qmp = @{ execute = 'input-send-event'; arguments = @{ events = @($events[3]) } }
                delay_after_ms = 25
            })
            if ($events.Count -gt 4) {
                for ($eventIndex = 4; $eventIndex -lt $events.Count; $eventIndex++) {
                    $commands.Add(@{
                        qmp = @{ execute = 'input-send-event'; arguments = @{ events = @($events[$eventIndex]) } }
                        delay_after_ms = 50
                    })
                }
            }
        } else {
            for ($eventIndex = 0; $eventIndex -lt $events.Count; $eventIndex++) {
                $commands.Add(@{
                    qmp = @{ execute = 'input-send-event'; arguments = @{ events = @($events[$eventIndex]) } }
                    delay_after_ms = 50
                })
            }
        }
    }
    if (![string]::IsNullOrWhiteSpace($CapturePath)) {
        $commands.Add(@{
            qmp = @{ execute = 'screendump'; arguments = @{ filename = $CapturePath; format = 'png' } }
            delay_after_ms = 0
        })
    }
    foreach ($command in $commands) {
        $writer.WriteLine(($command.qmp | ConvertTo-Json -Compress -Depth 8))
        while ($true) {
            $line = $reader.ReadLine()
            if (!$line) { throw 'QMP closed before acknowledging the input command.' }
            $response = $line | ConvertFrom-Json
            if ($response.error) { throw "QMP rejected the input command: $($response.error.desc)" }
            if ($response.PSObject.Properties.Name -contains 'return') { break }
        }
        if ([int]$command.delay_after_ms -gt 0) { Start-Sleep -Milliseconds ([int]$command.delay_after_ms) }
    }
} finally {
    $client.Dispose()
}

if ($RecordEditableControlCapture -or $RefreshEditableControlCapture) {
    if (!(Test-Path -LiteralPath $CapturePath -PathType Leaf)) {
        throw 'QMP acknowledged the editable-control capture but the image file is missing.'
    }
    $captureBytes = [IO.File]::ReadAllBytes($CapturePath)
    $pngSignature = [BitConverter]::ToString($captureBytes[0..7]).Replace('-', '')
    if ($captureBytes.Length -lt 8 -or
        $pngSignature -ne '89504E470D0A1A0A') {
        throw 'Editable-control capture is not a valid PNG screendump.'
    }
    $evidencePath = Join-Path ([IO.Path]::GetDirectoryName($SessionPath)) 'editable-control-evidence.json'
    $evidence = [ordered]@{
        control_kind = $EditableControlKind
        typed_text = if ($RefreshEditableControlCapture) { $ExpectedEditableText } else { $Text }
        screenshot = $CapturePath
        screenshot_sha256 = (Get-FileHash -LiteralPath $CapturePath -Algorithm SHA256).Hash.ToLowerInvariant()
        captured_utc = [DateTimeOffset]::UtcNow.ToString('o')
        visual_status = 'pending-review'
    }
    $evidence | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $evidencePath -Encoding utf8
    Write-Host "Editable Studio control capture recorded; visual review pending: $evidencePath" -ForegroundColor Cyan
}

Write-Host "Sent QMP input/capture under QAC reservation $($session.reservation_id); acceptance depends on guest logs and framebuffer evidence." -ForegroundColor Cyan
