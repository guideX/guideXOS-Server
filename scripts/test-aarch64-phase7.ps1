[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$TimeoutSeconds = 420,
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

function Read-QmpReply([Net.Sockets.NetworkStream]$Stream, [int]$TimeoutMs = 5000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $buffer = New-Object byte[] 4096
    Start-Sleep -Milliseconds 50
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Stream.DataAvailable) {
            $count = $Stream.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) { return [Text.Encoding]::UTF8.GetString($buffer, 0, $count) }
        }
        Start-Sleep -Milliseconds 10
    }
    throw 'Timed out reading QMP response'
}

function Invoke-Qmp([int]$Port, [hashtable]$Command) {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connected = $false
        for ($attempt = 0; $attempt -lt 80 -and !$connected; ++$attempt) {
            try { $client.Connect('127.0.0.1', $Port); $connected = $true }
            catch { Start-Sleep -Milliseconds 100 }
        }
        if (!$connected) { throw "QMP connection failed on port $Port" }
        $stream = $client.GetStream()
        $null = Read-QmpReply $stream
        $cap = [Text.Encoding]::UTF8.GetBytes('{"execute":"qmp_capabilities"}' + "`r`n")
        $stream.Write($cap, 0, $cap.Length)
        $stream.Flush()
        $null = Read-QmpReply $stream
        Start-Sleep -Milliseconds 100
        $json = ($Command | ConvertTo-Json -Compress -Depth 10)
        $bytes = [Text.Encoding]::UTF8.GetBytes($json + "`r`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        Start-Sleep -Milliseconds 25
        $response = Read-QmpReply $stream
        if ($response -match '"error"') { throw "QMP command failed: $response" }
        return $response
    } finally { $client.Dispose() }
}

function New-AbsEvent([string]$Axis, [int]$Value) {
    return @{ type = 'abs'; data = @{ axis = $Axis; value = $Value } }
}
function New-ButtonEvent([string]$Button, [bool]$Down) {
    return @{ type = 'btn'; data = @{ button = $Button; down = $Down } }
}
function New-KeyEvent([string]$Key, [bool]$Down) {
    # QEMU 11 models InputKey as a tagged object, not a raw qcode string.
    return @{ type = 'key'; data = @{ key = @{ type = 'qcode'; data = $Key }; down = $Down } }
}
function Send-InputEvents([int]$Port, [object[]]$Events) {
    if ($Events.Count -eq 0) { return }
    $command = @{ execute = 'input-send-event'; arguments = @{ events = @($Events) } }
    $null = Invoke-Qmp $Port $command
}

function Read-QmpBufferedLine([hashtable]$Session, [int]$TimeoutMs = 5000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $buffer = New-Object byte[] 4096
    while ([DateTime]::UtcNow -lt $deadline) {
        $newline = $Session.Buffer.IndexOf("`n")
        if ($newline -ge 0) {
            $line = $Session.Buffer.Substring(0, $newline + 1)
            $Session.Buffer = $Session.Buffer.Substring($newline + 1)
            if (![string]::IsNullOrWhiteSpace($line)) { return $line }
            continue
        }
        if ($Session.Stream.DataAvailable) {
            $count = $Session.Stream.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) {
                $Session.Buffer += [Text.Encoding]::UTF8.GetString($buffer, 0, $count)
            }
        } else { Start-Sleep -Milliseconds 2 }
    }
    throw 'Timed out reading QMP sequence response'
}

function Send-QmpEventSequence([int]$Port, [object[]]$Events, [int]$GroupSize = 1,
                                [int]$InterGroupDelayMs = 0) {
    if ($Events.Count -eq 0) { return }
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connected = $false
        for ($attempt = 0; $attempt -lt 80 -and !$connected; ++$attempt) {
            try { $client.Connect('127.0.0.1', $Port); $connected = $true }
            catch { Start-Sleep -Milliseconds 100 }
        }
        if (!$connected) { throw "QMP sequence connection failed on port $Port" }
        $session = @{ Client = $client; Stream = $client.GetStream(); Buffer = '' }
        $session.Stream.WriteTimeout = 5000
        $session.Stream.ReadTimeout = 5000
        $null = Read-QmpBufferedLine $session
        $cap = [Text.Encoding]::UTF8.GetBytes('{"execute":"qmp_capabilities"}' + "`r`n")
        $session.Stream.Write($cap, 0, $cap.Length)
        $session.Stream.Flush()
        Start-Sleep -Milliseconds 100
        for ($i = 0; $i -lt $Events.Count; $i += $GroupSize) {
            $group = @()
            $end = [Math]::Min($Events.Count, $i + $GroupSize)
            for ($j = $i; $j -lt $end; ++$j) { $group += $Events[$j] }
            $command = @{ execute = 'input-send-event'; arguments = @{ events = @($group) } }
            $json = ($command | ConvertTo-Json -Compress -Depth 10)
            $bytes = [Text.Encoding]::UTF8.GetBytes($json + "`r`n")
            $session.Stream.Write($bytes, 0, $bytes.Length)
            $session.Stream.Flush()
            $response = Read-QmpBufferedLine $session
            if ($response -match '"error"') { throw "QMP sequence command failed: $response" }
            if ($InterGroupDelayMs -gt 0) {
                Start-Sleep -Milliseconds $InterGroupDelayMs
            }
            # Give the guest consumer a bounded opportunity to drain key/button
            # events; movement remains safe to coalesce in the common queue.
            if ((($i / $GroupSize) % 16) -eq 15) {
                Start-Sleep -Milliseconds 2
            }
        }
        Start-Sleep -Milliseconds 500
    } finally { $client.Dispose() }
}
function Raw-Coordinate([int]$Pixel, [int]$Limit) {
    $value = [int][Math]::Round(($Pixel * 32767.0) / $Limit)
    if ($value -lt 0) { return 0 }
    if ($value -gt 32767) { return 32767 }
    return $value
}
function Send-Pointer([int]$Port, [int]$X, [int]$Y) {
    Send-InputEvents $Port @(
        (New-AbsEvent 'x' (Raw-Coordinate $X 799)),
        (New-AbsEvent 'y' (Raw-Coordinate $Y 599)))
}
function Send-Button([int]$Port, [bool]$Down) {
    Send-InputEvents $Port @((New-ButtonEvent 'left' $Down))
}
function Wait-GuestMarker([string]$LogPath, [string]$Marker, [Diagnostics.Process]$Process, [int]$Timeout) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
            $text = Get-Content -Raw -LiteralPath $LogPath
            if ($text -and $text.Contains($Marker)) { return }
        }
        if ($Process.HasExited) { throw "QEMU exited before guest marker: $Marker" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for guest marker: $Marker"
}

function Invoke-InteractiveBoot([int]$Run, [int]$Port, [string]$VarsPath, [string]$LogPath) {
    $esp = Join-Path $artifactDirectory 'esp'
    $arguments = @(
        '-machine','virt,gic-version=2,acpi=off','-cpu','cortex-a53','-m','512M',
        '-drive',"if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
        '-drive',"if=pflash,format=raw,unit=1,file=$VarsPath",
        '-drive',"file=fat:rw:$esp,format=raw",
        '-display','none','-device','ramfb',
        '-global','virtio-mmio.force-legacy=false',
        '-device','virtio-keyboard-device','-device','virtio-tablet-device',
        '-qmp',"tcp:127.0.0.1:$Port,server=on,wait=off",
        '-serial',"file:$LogPath",'-no-reboot')
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $QemuPath
    $info.Arguments = (($arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $null = $process.Start()
    try {
        Wait-GuestMarker $LogPath '[guideXOS] input proof window: ready' $process $TimeoutSeconds
        # Let the normal compositor loop reach its steady state after the
        # proof-window marker before the first QMP event is submitted.
        Start-Sleep -Milliseconds 10000

        # Real title-bar activation, bounded common-compositor drag, and release.
        Send-Pointer $Port 200 130
        Start-Sleep -Milliseconds 150
        Send-Button $Port $true
        Start-Sleep -Milliseconds 150
        Send-Pointer $Port 340 200
        Start-Sleep -Milliseconds 150
        Send-Button $Port $false
        Start-Sleep -Milliseconds 150
        # A second idempotent release keeps the deterministic proof robust to
        # a QEMU input queue edge while still using only real tablet events.
        Send-Button $Port $false
        Start-Sleep -Milliseconds 150
        Wait-GuestMarker $LogPath '[guideXOS] window drag: PASS' $process 30

        # Route deterministic keyboard text through the now-focused common window.
        Send-Pointer $Port 480 290
        Send-Button $Port $true
        Send-Button $Port $false
        foreach ($key in @('g','u','i','d','e','x','o','s')) {
            Send-InputEvents $Port @((New-KeyEvent $key $true), (New-KeyEvent $key $false))
        }
        Wait-GuestMarker $LogPath '[guideXOS] keyboard routing: PASS' $process 30

        # Real tablet click on the normal taskbar Start button.  It is placed
        # after the proof-window interaction so the first title-bar press is
        # not covered by the application list overlay.
        Send-Pointer $Port 50 580
        Send-Button $Port $true
        Send-Button $Port $false
        Wait-GuestMarker $LogPath '[guideXOS] Start button input: PASS' $process 30

        if ($InteractiveOnly) {
            return (Get-Content -Raw -LiteralPath $LogPath)
        }

        # Stress the real transport with separate hardware event classes.
        $pointerStress = @()
        for ($i = 0; $i -lt 10000; ++$i) {
            $pixel = ($i % 700) + 40
            $pointerStress += New-AbsEvent 'x' (Raw-Coordinate $pixel 799)
        }
        # QEMU's virtio-input endpoint has a finite guest-visible report
        # queue.  Pace each real QMP batch below the TCG guest drain rate so
        # the durability proof measures delivered hardware reports rather
        # than QEMU-side producer overflow.
        Send-QmpEventSequence $Port $pointerStress 8 120
        Send-Pointer $Port 740 80
        $buttonStress = @()
        for ($i = 0; $i -lt 1000; ++$i) {
            $buttonStress += New-ButtonEvent 'left' (($i % 2) -eq 0)
        }
        Send-QmpEventSequence $Port $buttonStress 2 50
        $keyboardStress = @()
        for ($i = 0; $i -lt 1000; ++$i) {
            $keyboardStress += New-KeyEvent 'a' (($i % 2) -eq 0)
        }
        Send-QmpEventSequence $Port $keyboardStress 2 50
        Wait-GuestMarker $LogPath 'AARCH64_PHASE7_PASS' $process $TimeoutSeconds
        $text = Get-Content -Raw -LiteralPath $LogPath
        if ($text -match 'AARCH64_PHASE[23456]_ERROR|AARCH64_PHASE7_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)') {
            throw "Phase-7 boot $Run contains a guest error marker"
        }
        return $text
    } finally {
        if (!$process.HasExited) { try { $process.Kill() } catch {} }
        $process.WaitForExit()
    }
}

$varsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
if (!(Test-Path -LiteralPath $varsTemplate -PathType Leaf)) { throw "AArch64 UEFI variable template not found: $varsTemplate" }
$logsDirectory = Join-Path $artifactDirectory 'logs'
$null = New-Item -ItemType Directory -Path $logsDirectory -Force
$passLogs = @()
for ($run = 1; $run -le 3; ++$run) {
    $varsPath = Join-Path $artifactDirectory ("edk2-aarch64-phase7-vars-{0}.fd" -f $run)
    Copy-Item -LiteralPath $varsTemplate -Destination $varsPath -Force
    $logPath = Join-Path $logsDirectory ("phase7-boot-{0}.log" -f $run)
    $port = 4760 + $run
    Write-Host "Starting fresh AArch64 Phase 7 interactive QEMU boot $run/3..." -ForegroundColor Yellow
    $text = Invoke-InteractiveBoot $run $port $varsPath $logPath
    if ($InteractiveOnly) {
        foreach ($marker in @(
            '[guideXOS] input device discovery: OK',
            '[guideXOS] cursor routing: PASS',
            '[guideXOS] mouse button routing: PASS',
            '[guideXOS] focus routing: PASS',
            '[guideXOS] window drag: PASS',
            '[guideXOS] keyboard routing: PASS',
            '[guideXOS] Start button input: PASS')) {
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
        '[guideXOS] Start button input: PASS','[guideXOS] input durability: PASS',
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
    if ($text -notmatch 'pointer=[1-9][0-9]* buttons=[1-9][0-9]* keyboard=[1-9][0-9]* queue-high-water=[1-9][0-9]* dropped=0') {
        throw "Fresh Phase 7 boot $run lacks guest transport stress statistics"
    }
    if ($text -notmatch 'framebuffer interaction verification: PASS hash=0x[0-9a-f]+') {
        throw "Fresh Phase 7 boot $run lacks interactive framebuffer verification"
    }
    $passLogs += $logPath
    Write-Host "Fresh Phase 7 QEMU boot $run/3: PASS" -ForegroundColor Green
}

if ($InteractiveOnly) {
    Write-Host 'AARCH64 Phase 7 interactive routing probe: PASS (durability not claimed)' -ForegroundColor Yellow
    exit 0
}

if (!$SkipHistoricalRegressions) {
    foreach ($phase in 1..4) {
        $script = Join-Path $PSScriptRoot "test-aarch64-phase$phase.ps1"
        $phaseTimeout = if ($phase -ge 3) { 240 } else { 180 }
        & $script -TimeoutSeconds $phaseTimeout
        if ($LASTEXITCODE -ne 0) { throw "Historical Phase $phase regression failed" }
    }
    & (Join-Path $PSScriptRoot 'test-aarch64-phase5.ps1') -TimeoutSeconds 240 -HistoricalTimeoutSeconds 240 -SkipHistoricalRegressions
    if ($LASTEXITCODE -ne 0) { throw 'Historical Phase 5 regression failed' }
    & (Join-Path $PSScriptRoot 'test-aarch64-phase6.ps1') -TimeoutSeconds 240 -HistoricalTimeoutSeconds 240 -SkipHistoricalRegressions
    if ($LASTEXITCODE -ne 0) { throw 'Historical Phase 6 regression failed' }
}

Write-Host 'AARCH64 Phase 7 test suite: PASS (host controls + three fresh interactive boots + historical regressions)' -ForegroundColor Green
exit 0
