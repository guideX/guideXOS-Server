[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$TimeoutSeconds = 900,
    [switch]$SkipBuild,
    [switch]$SkipHistoricalRegressions
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase8'
if (!$SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-aarch64-phase8.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
    if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 8 build failed' }
}

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
            if ($count -gt 0) { $Session.Buffer += [Text.Encoding]::UTF8.GetString($buffer, 0, $count) }
        } else { Start-Sleep -Milliseconds 2 }
    }
    throw "QMP response timeout after $TimeoutMs ms"
}

function Invoke-QmpCommand([hashtable]$Session, [string]$Execute, [hashtable]$Arguments) {
    $id = 'guidexos-phase8-' + $Session.NextId
    ++$Session.NextId
    $command = @{ execute = $Execute; id = $id }
    if ($Arguments.Count -ne 0) { $command.arguments = $Arguments }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($command | ConvertTo-Json -Compress -Depth 20) + ([char]13) + ([char]10))
    $Session.Stream.Write($bytes, 0, $bytes.Length)
    $Session.Stream.Flush()
    while ($true) {
        $message = Read-QmpMessage $Session
        if (!$message.Object.PSObject.Properties.Name.Contains('id')) { continue }
        if ([string]$message.Object.id -ne $id) { continue }
        if ($message.Object.PSObject.Properties.Name.Contains('error')) { throw "QMP command '$Execute' failed: $($message.Raw.Trim())" }
        return $message.Object
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
    $session = @{ Client = $client; Stream = $stream; Buffer = ''; NextId = 1 }
    try {
        $greeting = Read-QmpMessage $session
        if (!$greeting.Object.PSObject.Properties.Name.Contains('QMP')) { throw 'QMP greeting did not contain capabilities' }
        $null = Invoke-QmpCommand $session 'qmp_capabilities' @{}
        $commands = Invoke-QmpCommand $session 'query-commands' @{}
        if (@($commands.return | Where-Object { $_.name -eq 'input-send-event' }).Count -eq 0) { throw 'input-send-event is unavailable' }
        return $session
    } catch { $client.Dispose(); throw }
}

function New-AbsEvent([int]$Value, [string]$Axis) { return @{ type = 'abs'; data = @{ axis = $Axis; value = $Value } } }
function New-ButtonEvent([bool]$Down) { return @{ type = 'btn'; data = @{ button = 'left'; down = $Down } } }
function New-KeyEvent([bool]$Down, [string]$Key) { return @{ type = 'key'; data = @{ key = @{ type = 'qcode'; data = $Key }; down = $Down } } }
function Raw-Coordinate([int]$Pixel, [int]$Limit) {
    return [Math]::Max(0, [Math]::Min(32767, [int][Math]::Round(($Pixel * 32767.0) / $Limit)))
}
function Send-QmpEvent([hashtable]$Session, [hashtable]$Event) {
    $null = Invoke-QmpCommand $Session 'input-send-event' @{ events = @($Event) }
}
function Send-Pointer([hashtable]$Session, [int]$X, [int]$Y) {
    Send-QmpEvent $Session (New-AbsEvent (Raw-Coordinate $X 799) 'x')
    Start-Sleep -Milliseconds 25
    Send-QmpEvent $Session (New-AbsEvent (Raw-Coordinate $Y 599) 'y')
}
function Send-Button([hashtable]$Session, [bool]$Down) { Send-QmpEvent $Session (New-ButtonEvent $Down) }
function Send-Key([hashtable]$Session, [string]$Key, [bool]$Down) { Send-QmpEvent $Session (New-KeyEvent $Down $Key) }

function Wait-GuestMarkerAttempt([string]$LogPath, [string]$Marker, [Diagnostics.Process]$Process,
                                  [int]$WaitSeconds) {
    $attemptDeadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    try {
        $null = Wait-GuestMarker $LogPath $Marker $Process $attemptDeadline
        return $true
    } catch {
        if ($_.Exception.Message -notlike 'Timed out waiting for guest marker:*') { throw }
        return $false
    }
}

function Wait-GuestCountAttempt([string]$LogPath, [string]$Marker, [int]$Count,
                                [Diagnostics.Process]$Process, [int]$WaitSeconds) {
    $attemptDeadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    try {
        $null = Wait-GuestCount $LogPath $Marker $Count $Process $attemptDeadline
        return $true
    } catch {
        if ($_.Exception.Message -notlike 'Timed out waiting for occurrence*') { throw }
        return $false
    }
}

function Focus-Textbox([hashtable]$Session, [string]$LogPath, [Diagnostics.Process]$Process) {
    for ($attempt = 0; $attempt -lt 3; ++$attempt) {
        Send-Pointer $Session 300 250
        Start-Sleep -Milliseconds 250
        Send-Button $Session $true
        Start-Sleep -Milliseconds 150
        Send-Button $Session $false
        if (Wait-GuestMarkerAttempt $LogPath '[guideXOS] phase8 desktop input: textbox focus callback' $Process 3) { return }
    }
    throw 'Textbox focus was not delivered through the common input route'
}

function Send-KeyWithDelivery([hashtable]$Session, [string]$LogPath, [Diagnostics.Process]$Process,
                              [string]$Key, [int]$ExpectedKeyCallbacks) {
    for ($attempt = 0; $attempt -lt 3; ++$attempt) {
        Send-Key $Session $Key $true
        Start-Sleep -Milliseconds 100
        Send-Key $Session $Key $false
        if (Wait-GuestCountAttempt $LogPath '[guideXOS] phase8 desktop input: key callback' $ExpectedKeyCallbacks $Process 3) { return }
    }
    throw "Keyboard key '$Key' was not delivered through the common input route"
}

function Read-GuestText([string]$LogPath) {
    if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) { return '' }
    $text = Get-Content -Raw -LiteralPath $LogPath
    if ($null -eq $text) { return '' }
    return [string]$text
}
function Assert-GuestHealthy([string]$Text, [string]$Context) {
    if ($Text -match 'AARCH64_PHASE[2345678]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') {
        throw "$Context contains a guest error marker"
    }
    if ($Text.Contains('AARCH64_PHASE7_PASS')) { throw "$Context emitted forbidden retroactive AARCH64_PHASE7_PASS" }
}
function Wait-GuestMarker([string]$LogPath, [string]$Marker, [Diagnostics.Process]$Process, [DateTime]$Deadline) {
    while ([DateTime]::UtcNow -lt $Deadline) {
        $text = Read-GuestText $LogPath
        Assert-GuestHealthy $text "waiting for $Marker"
        if ($text.Contains($Marker)) { return $text }
        if ($Process.HasExited) { throw "QEMU exited before guest marker: $Marker" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for guest marker: $Marker"
}
function Wait-GuestCount([string]$LogPath, [string]$Marker, [int]$Count, [Diagnostics.Process]$Process, [DateTime]$Deadline) {
    while ([DateTime]::UtcNow -lt $Deadline) {
        $text = Read-GuestText $LogPath
        Assert-GuestHealthy $text "waiting for $Count occurrences of $Marker"
        if ([regex]::Matches($text, [regex]::Escape($Marker)).Count -ge $Count) { return $text }
        if ($Process.HasExited) { throw "QEMU exited before occurrence $Count of $Marker" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for occurrence $Count of $Marker"
}

function Test-HostNegativeControls {
    $batchFailure = $false
    try { if (@(New-AbsEvent 1 'x').Count -ne 1) { throw 'event construction failed' } } catch { $batchFailure = $true }
    if ($batchFailure) { throw 'QMP host control failed' }
    $invalid = $false
    try { '{"return":' | ConvertFrom-Json -ErrorAction Stop } catch { $invalid = $true }
    if (!$invalid) { throw 'malformed QMP response was accepted' }
    Write-Host 'AARCH64_PHASE8_QMP_CONTROLS_PASS (single-event pacing, malformed-response fail-closed)' -ForegroundColor Green
}

function Invoke-RealInteraction([hashtable]$Session, [string]$LogPath, [Diagnostics.Process]$Process,
                                 [DateTime]$Deadline, [int]$ExpectedTextCount, [int]$ExpectedButtonCount) {
    # The window is centered by the common compositor at 800x600: x=170,y=150,
    # width=460,height=300.  All reports are conservative one-command QMP
    # events with the established 25 ms tablet axis pacing.
    Focus-Textbox $Session $LogPath $Process
    Start-Sleep -Milliseconds 250
    $keyCallbackCount = 0
    foreach ($key in @('a','r','m','6','4')) {
        ++$keyCallbackCount
        Send-KeyWithDelivery $Session $LogPath $Process $key $keyCallbackCount
        Start-Sleep -Milliseconds 60
    }
    $null = Wait-GuestCount $LogPath '[phase8-app] text input: PASS value=arm64' $ExpectedTextCount $Process $Deadline

    Send-Pointer $Session 400 303
    Start-Sleep -Milliseconds 200
    Send-Button $Session $true
    Start-Sleep -Milliseconds 150
    Send-Button $Session $false
    $null = Wait-GuestCount $LogPath '[phase8-app] button event: PASS count=1' $ExpectedButtonCount $Process $Deadline

    # Drag the title bar by a meaningful (120,80) delta.  The resulting window
    # is still fully on the 800x576 compositor work area.
    Send-Pointer $Session 200 160
    Start-Sleep -Milliseconds 300
    Send-Button $Session $true
    Start-Sleep -Milliseconds 300
    foreach ($point in @(@(240, 190), @(280, 220), @(320, 240))) {
        Send-Pointer $Session $point[0] $point[1]
        Start-Sleep -Milliseconds 250
    }
    Send-Button $Session $false
    Start-Sleep -Milliseconds 300
    $null = Wait-GuestCount $LogPath '[guideXOS] application window drag: PASS' $ExpectedButtonCount $Process $Deadline

    # Close the moved window through its common title-bar decoration.
    # Click the center of the moved close decoration.  Using the left edge
    # makes normalized tablet rounding able to land one pixel outside it.
    Send-Pointer $Session 736 243
    Start-Sleep -Milliseconds 150
    Send-Button $Session $true
    Start-Sleep -Milliseconds 150
    Send-Button $Session $false
    $null = Wait-GuestCount $LogPath '[phase8-app] close requested' $ExpectedButtonCount $Process $Deadline
    $null = Wait-GuestCount $LogPath '[guideXOS] NativeElf GUI cleanup: PASS' $ExpectedButtonCount $Process $Deadline
}

function Invoke-Phase8Boot([int]$Run, [int]$Port, [string]$VarsPath, [string]$LogPath) {
    $esp = Join-Path $artifactDirectory 'esp'
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) { Remove-Item -LiteralPath $LogPath -Force }
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
    try {
        $null = Wait-GuestMarker $LogPath '[guideXOS] NativeElf application input bridge: ready' $process $deadline
        Start-Sleep -Milliseconds 10000
        $session = Open-QmpSession $Port
        Invoke-RealInteraction $session $LogPath $process $deadline 1 1
        $null = Wait-GuestCount $LogPath '[phase8-app] gx_main entered' 2 $process $deadline
        Invoke-RealInteraction $session $LogPath $process $deadline 2 2
        $null = Wait-GuestMarker $LogPath 'AARCH64_PHASE8_PASS' $process $deadline
        $text = Read-GuestText $LogPath
        Assert-GuestHealthy $text "complete Phase-8 boot $Run"
        return $text
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
    $varsPath = Join-Path $artifactDirectory ("edk2-aarch64-phase8-vars-{0}.fd" -f $run)
    Copy-Item -LiteralPath $varsTemplate -Destination $varsPath -Force
    $logPath = Join-Path $logsDirectory ("phase8-boot-{0}.log" -f $run)
    Write-Host "Starting fresh AArch64 Phase 8 QEMU boot $run/3..." -ForegroundColor Yellow
    $text = Invoke-Phase8Boot $run (4860 + $run) $varsPath $logPath
    $required = @(
        '[guideXOS] NativeElf application input bridge: ready','[guideXOS] App Model: ARM64 GUI app found',
        '[phase8-app] gx_main entered','[phase8-app] ABI: GUI services OK','[guideXOS] NativeElf GUI window: created',
        '[phase8-app] window created','[phase8-app] text input: PASS value=arm64','[phase8-app] button event: PASS count=1',
        '[phase8-app] close requested','[phase8-app] returning 42','[guideXOS] NativeElf GUI return: 42',
        '[guideXOS] application focus routing: PASS','[guideXOS] application input routing: PASS',
        '[guideXOS] framebuffer GUI regions: PASS',
        '[guideXOS] NativeElf GUI cleanup: PASS',
        '[guideXOS] ARM64 GUI App Model relaunch: PASS','[guideXOS] GUI App Model durability: PASS launches=25 completed=25 allocator-delta-pages=0',
        '[guideXOS] graphics/scheduler integration: PASS','AARCH64_PHASE8_PASS')
    $last = -1
    foreach ($marker in $required) {
        $index = $text.IndexOf($marker, [StringComparison]::Ordinal)
        if ($index -lt 0) { throw "Fresh Phase 8 boot $run is missing marker: $marker" }
        if ($index -le $last) { throw "Fresh Phase 8 boot $run marker order failed at: $marker" }
        $last = $index
    }
    if ([regex]::Matches($text, '\[phase8-app\] gx_main entered').Count -ne 25) { throw "Fresh Phase 8 boot $run did not complete 25 distinct entries" }
    if ([regex]::Matches($text, '\[guideXOS\] NativeElf GUI cleanup: PASS').Count -ne 25) { throw "Fresh Phase 8 boot $run did not clean up all 25 GUI lifecycles" }
    if ($text -notmatch 'unexpected-irq=0 exceptions=0') { throw "Fresh Phase 8 boot $run reports IRQs or exceptions" }
    Write-Host "Fresh Phase 8 GUI NativeElf boot $run/3: PASS (two real interactions + 25 lifecycle launches)" -ForegroundColor Green
}

if (!$SkipHistoricalRegressions) {
    & (Join-Path $PSScriptRoot 'test-aarch64-phase5.ps1') -TimeoutSeconds 240 -HistoricalTimeoutSeconds 240 -SkipHistoricalRegressions
    if ($LASTEXITCODE -ne 0) { throw 'Historical Phase 5 regression failed' }
    & (Join-Path $PSScriptRoot 'test-aarch64-phase6.ps1') -TimeoutSeconds 240 -HistoricalTimeoutSeconds 240 -SkipHistoricalRegressions
    if ($LASTEXITCODE -ne 0) { throw 'Historical Phase 6 regression failed' }
    & (Join-Path $PSScriptRoot 'test-aarch64-phase7.ps1') -InteractiveOnly -SkipHistoricalRegressions -TimeoutSeconds 240
    if ($LASTEXITCODE -ne 0) { throw 'Phase 7 bounded high-level interaction regression failed' }
}
Write-Host 'AARCH64 Phase 8 test suite: PASS (ABI controls + three fresh GUI boots + bounded lifecycle durability)' -ForegroundColor Green
