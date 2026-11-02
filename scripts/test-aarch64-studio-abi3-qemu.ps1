[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ArtifactDirectory,
    [string]$QacCliPath = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [int]$AdmissionTimeoutSeconds = 12,
    [int]$RuntimeSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$ArtifactDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to run the Studio guest outside repository out/: $ArtifactDirectory"
}
foreach ($path in @($QemuPath, $FirmwareCode, $FirmwareVarsTemplate)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required guest input is missing: $path" }
}
if ([string]::IsNullOrWhiteSpace($QacCliPath)) { $QacCliPath = $env:GUIDEXOS_QAC_CLI }
if ([string]::IsNullOrWhiteSpace($QacCliPath)) {
    $QacCliPath = Join-Path (Split-Path -Parent $repoRoot) 'guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
}
if (!(Test-Path -LiteralPath $QacCliPath -PathType Leaf)) { throw "QAC CLI not found: $QacCliPath" }
if ($AdmissionTimeoutSeconds -lt 1 -or $RuntimeSeconds -lt 1) { throw 'Timeouts must be positive.' }

$esp = Join-Path $ArtifactDirectory 'esp'
foreach ($required in @('EFI\BOOT\BOOTAA64.EFI','kernel.elf','ramdisk.img')) {
    if (!(Test-Path -LiteralPath (Join-Path $esp $required) -PathType Leaf)) { throw "Candidate ESP is missing $required" }
}
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logDirectory = Join-Path $ArtifactDirectory ("abi3-guest-{0}" -f $runId)
$null = New-Item -ItemType Directory -Path $logDirectory -Force
$qacLogPath = Join-Path $logDirectory 'qac.log'
$guestLogPath = Join-Path $logDirectory 'guest.log'
$sessionPath = Join-Path $logDirectory 'session.json'
$screenshotPath = Join-Path $logDirectory 'studio-initial.png'
$varsPath = Join-Path $logDirectory 'edk2-vars.fd'
Copy-Item -LiteralPath $FirmwareVarsTemplate -Destination $varsPath

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
try { $qmpPort = ([Net.IPEndPoint]$listener.LocalEndpoint).Port }
finally { $listener.Stop() }

$guestArguments = @(
    '-machine','virt,gic-version=2,acpi=off','-cpu','cortex-a53','-m','512M',
    '-drive',"if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
    '-drive',"if=pflash,format=raw,unit=1,file=$varsPath",
    '-drive',"file=fat:rw:$esp,format=raw",'-display','none','-device','ramfb',
    '-global','virtio-mmio.force-legacy=false','-device','virtio-keyboard-device','-device','virtio-tablet-device',
    '-monitor','none','-serial',"file:$guestLogPath",'-no-reboot',
    '-qmp',"tcp:127.0.0.1:$qmpPort,server=on,wait=off")

function Quote-ProcessArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { ++$slashes; continue }
        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, (2 * $slashes + 1)); [void]$builder.Append([char]34); $slashes = 0; continue
        }
        if ($slashes -gt 0) { [void]$builder.Append([char]92, $slashes) }
        [void]$builder.Append($character); $slashes = 0
    }
    if ($slashes -gt 0) { [void]$builder.Append([char]92, (2 * $slashes)) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Read-GuestLog {
    if (!(Test-Path -LiteralPath $guestLogPath -PathType Leaf)) { return '' }
    $stream = [IO.File]::Open($guestLogPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Send-Qmp([Net.Sockets.TcpClient]$Client, [IO.StreamReader]$Reader,
                  [IO.StreamWriter]$Writer, [string]$Command, [hashtable]$Arguments = $null) {
    $payload = @{ execute = $Command }
    if ($Arguments) { $payload.arguments = $Arguments }
    $Writer.WriteLine(($payload | ConvertTo-Json -Compress -Depth 8))
    while ($true) {
        $line = $Reader.ReadLine()
        if (!$line) { throw "QMP closed before returning $Command" }
        $response = $line | ConvertFrom-Json
        if ($response.error) { throw "QMP $Command failed: $($response.error.desc)" }
        if ($response.PSObject.Properties.Name -contains 'return') { return $response.return }
    }
}

function Capture-StudioScreen {
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds(10)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $client = [Net.Sockets.TcpClient]::new()
        try {
            $client.ReceiveTimeout = 3000; $client.SendTimeout = 3000
            $client.Connect([Net.IPAddress]::Loopback, $qmpPort)
            $stream = $client.GetStream(); $stream.ReadTimeout = 3000; $stream.WriteTimeout = 3000
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
            $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 4096, $true); $writer.AutoFlush = $true
            $null = $reader.ReadLine()
            $null = Send-Qmp $client $reader $writer 'qmp_capabilities'
            $null = Send-Qmp $client $reader $writer 'screendump' @{ filename = $screenshotPath; format = 'png' }
            return $true
        } catch {
            Start-Sleep -Milliseconds 250
        } finally { $client.Dispose() }
    }
    return $false
}

$qacArguments = @('run','--project','guideXOSServer-AARCH64-ABI3-Studio','--mode','acceptance','--',$QemuPath) + $guestArguments
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = [IO.Path]::GetFullPath($QacCliPath)
$startInfo.Arguments = (($qacArguments | ForEach-Object { Quote-ProcessArgument ([string]$_) }) -join ' ')
$startInfo.UseShellExecute = $false; $startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true; $startInfo.RedirectStandardError = $true
$qac = [Diagnostics.Process]::new(); $qac.StartInfo = $startInfo
$null = $qac.Start()
$stdoutTask = $qac.StandardOutput.ReadToEndAsync()
$stderrTask = $qac.StandardError.ReadToEndAsync()
$reservationId = ''
$running = $false
$admissionDeadline = [DateTimeOffset]::UtcNow.AddSeconds($AdmissionTimeoutSeconds)
while ([DateTimeOffset]::UtcNow -lt $admissionDeadline -and !$qac.HasExited) {
    $status = & $QacCliPath list 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        $match = [regex]::Match($status, '(?m)^\s*([0-9a-f]{32}) \| guideXOSServer-AARCH64-ABI3-Studio \| acceptance \| ([^|]+) \| queue \d+ \| [^\r\n]*\(PID ' + $qac.Id + '\) \| owner ')
        if ($match.Success) {
            $reservationId = $match.Groups[1].Value
            if ($match.Groups[2].Value.Trim() -eq 'Running') { $running = $true; break }
            if ($match.Groups[2].Value.Trim() -in @('Waiting','Blocked')) { Start-Sleep -Milliseconds 250; continue }
        }
    }
    Start-Sleep -Milliseconds 250
}

if (!$running) {
    if ($reservationId) { $null = & $QacCliPath cancel $reservationId 2>&1 }
    if (!$qac.WaitForExit(5000)) { throw 'QAC did not confirm cancellation of the ungranted request; no guest was launched.' }
    $qacText = $stdoutTask.GetAwaiter().GetResult() + $stderrTask.GetAwaiter().GetResult()
    $qacText | Set-Content -LiteralPath $qacLogPath -Encoding utf8
    $record = [ordered]@{
        outcome = 'ABI3_GUEST_PENDING_QAC'; reservation_id = $reservationId
        qemu_launched = $false; qmp_port = $qmpPort; guest_log = $guestLogPath; qac_log = $qacLogPath
        qac_status_after = (& $QacCliPath status 2>&1 | Out-String).Trim()
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
    Write-Host "ABI3_GUEST_PENDING_QAC: request not granted within $AdmissionTimeoutSeconds seconds; no QEMU was launched." -ForegroundColor Yellow
    Write-Host "Session record: $sessionPath"
    exit 2
}

$record = [ordered]@{
    outcome = 'ABI3_STUDIO_GUEST_IN_PROGRESS'; project = 'guideXOSServer-AARCH64-ABI3-Studio'
    reservation_id = $reservationId; qmp_port = $qmpPort; machine = 'virt,gic-version=2,acpi=off'
    cpu = 'cortex-a53'; qemu = [IO.Path]::GetFullPath($QemuPath); guest_log = $guestLogPath
    qac_log = $qacLogPath; screenshot = $screenshotPath; qemu_launched = $true
    studio_window_screenshot = $false; studio_close_and_cleanup = $false; runtime_deadline_seconds = $RuntimeSeconds
}
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
Write-Host "QAC granted reservation $reservationId and launched QEMU PID $($qac.Id)." -ForegroundColor Cyan
Write-Host "QMP port: $qmpPort; interact using the recorded port while this script holds the acceptance reservation."

$deadline = [DateTimeOffset]::UtcNow.AddSeconds($RuntimeSeconds)
$screenshotTaken = $false
$lastGuest = ''
while ([DateTimeOffset]::UtcNow -lt $deadline) {
    $lastGuest = Read-GuestLog
    if (!$screenshotTaken -and $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER initial_render=PASS')) {
        $screenshotTaken = Capture-StudioScreen
        if ($screenshotTaken -and (Test-Path -LiteralPath $screenshotPath -PathType Leaf)) {
            $record.studio_window_screenshot = $true
            $record.screenshot_sha256 = (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
            $record.screenshot_bytes = (Get-Item -LiteralPath $screenshotPath).Length
            $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
            Write-Host "Studio initial frame captured: $screenshotPath" -ForegroundColor Green
        }
    }
    if ($lastGuest.Contains('AARCH64_PHASE12_PASS')) { break }
    if ($lastGuest -match 'AARCH64_PHASE[234]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') { break }
    if ($qac.HasExited) { break }
    Start-Sleep -Milliseconds 200
}

$timedOut = [DateTimeOffset]::UtcNow -ge $deadline
$lastGuest = Read-GuestLog
if (!$qac.HasExited) {
    $quitSent = $false
    for ($attempt = 0; $attempt -lt 20 -and !$quitSent; ++$attempt) {
        $client = [Net.Sockets.TcpClient]::new()
        try {
            $client.ReceiveTimeout = 3000; $client.SendTimeout = 3000; $client.Connect([Net.IPAddress]::Loopback, $qmpPort)
            $stream = $client.GetStream(); $stream.ReadTimeout = 3000; $stream.WriteTimeout = 3000
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
            $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 4096, $true); $writer.AutoFlush = $true
            $null = $reader.ReadLine(); $null = Send-Qmp $client $reader $writer 'qmp_capabilities'
            $null = Send-Qmp $client $reader $writer 'quit'; $quitSent = $true
        } catch { Start-Sleep -Milliseconds 250 }
        finally { $client.Dispose() }
    }
    if (!$quitSent) { throw 'QMP shutdown failed; QAC remains responsible for the exact guest child and reservation.' }
    if (!$qac.WaitForExit(15000)) { throw 'QAC did not verify QEMU exit after QMP shutdown.' }
}
$qac.WaitForExit()
$qacText = $stdoutTask.GetAwaiter().GetResult() + $stderrTask.GetAwaiter().GetResult()
$qacText | Set-Content -LiteralPath $qacLogPath -Encoding utf8
$lastGuest = Read-GuestLog
$record.guest_pass_marker = $lastGuest.Contains('AARCH64_PHASE12_PASS')
$record.studio_entry_called = $lastGuest.Contains('P28Z APP 00 gx_main_entry_raw')
$record.adapter_selected = $lastGuest.Contains('ABI adapter selected: guidexos-c-abi-v1-studio-v05-464')
$record.window_created = $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER main_window_creation=PASS')
$record.initial_render = $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER initial_render=PASS')
$record.mouse_event_delivered = $lastGuest.Contains('ABI3 guest observed Studio mouse event through v0.5 poll_event')
$record.key_event_delivered = $lastGuest.Contains('ABI3 guest observed Studio key event through v0.5 poll_event')
$record.close_and_cleanup = $lastGuest.Contains('NativeElf GUI cleanup: PASS')
$record.qac_launched_child = $qacText -match "QAC QEMU launched: reservation=$reservationId"
$record.qac_released_reservation = $qacText -match "QAC reservation released: id=$reservationId"
$record.qemu_exit_code = $qac.ExitCode
$record.outcome = if ($record.guest_pass_marker -and $record.studio_entry_called -and $record.adapter_selected -and
    $record.window_created -and $record.initial_render -and $record.mouse_event_delivered -and
    $record.key_event_delivered -and $record.close_and_cleanup -and $record.qac_launched_child -and
    $record.qac_released_reservation -and !$timedOut) {
    'ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS'
} elseif ($timedOut) { 'ABI3_STUDIO_GUEST_RUNTIME_TIMEOUT' } else { 'ABI3_STUDIO_GUEST_PARTIAL' }
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
$lastGuest | Set-Content -LiteralPath (Join-Path $logDirectory 'guest-final.log') -Encoding utf8
Write-Host "Outcome: $($record.outcome)" -ForegroundColor $(if ($record.outcome -eq 'ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS') { 'Green' } else { 'Yellow' })
Write-Host "Guest log: $(Join-Path $logDirectory 'guest-final.log')"
Write-Host "QAC log: $qacLogPath"
Write-Host "Session record: $sessionPath"
if ($record.outcome -ne 'ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS') { exit 1 }
