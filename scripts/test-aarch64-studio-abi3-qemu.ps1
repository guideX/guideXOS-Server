[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ArtifactDirectory,
    [string]$QacCliPath = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [int]$AdmissionTimeoutSeconds = 1800,
    [int]$RuntimeSeconds = 300,
    [int]$PostStudioExitSeconds = 20
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
if ($AdmissionTimeoutSeconds -lt 1 -or $RuntimeSeconds -lt 1 -or $PostStudioExitSeconds -lt 0) {
    throw 'Admission/runtime timeouts must be positive and the post-exit window cannot be negative.'
}

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
$initialVarsSha256 = (Get-FileHash -LiteralPath $varsPath -Algorithm SHA256).Hash.ToLowerInvariant()
$varsTemplateSha256 = (Get-FileHash -LiteralPath $FirmwareVarsTemplate -Algorithm SHA256).Hash.ToLowerInvariant()
if ($initialVarsSha256 -ne $varsTemplateSha256) { throw 'Studio guest did not receive a fresh UEFI variables copy.' }

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
try { $qmpPort = ([Net.IPEndPoint]$listener.LocalEndpoint).Port }
finally { $listener.Stop() }

$guestArguments = @(
    '-machine','virt,gic-version=2,acpi=off','-cpu','cortex-a53','-m','512M',
    '-drive',"if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
    '-drive',"if=pflash,format=raw,unit=1,file=$varsPath",
    '-drive',"file=fat:rw:$esp,format=raw",'-display','none','-device','ramfb,xres=1280,yres=800',
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

function Get-QemuProcesses {
    try {
        return @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-aarch64.exe'" -ErrorAction Stop)
    } catch {
        throw "Unable to verify QEMU process ownership through CIM: $($_.Exception.Message)"
    }
}

function Get-QacHealth([string]$Context) {
    $status = & $QacCliPath status 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $status -notmatch '(?m)^Coordinator: Online\r?$') {
        throw "$Context QAC coordinator is not verifiably online.`n$status"
    }
    if ($status -match 'Recovery: Required') { throw "$Context QAC requires recovery; refusing guest execution.`n$status" }
    $processMatch = [regex]::Match($status, '(?m)^QEMU processes: (\d+) \((\d+) foreign\)\r?$')
    if (!$processMatch.Success) { throw "$Context QAC status did not expose a parseable QEMU ownership count.`n$status" }
    if ([int]$processMatch.Groups[2].Value -ne 0) { throw "$Context QAC reports unmanaged/foreign QEMU processes.`n$status" }
    $list = & $QacCliPath list 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "$Context unable to inspect QAC reservations.`n$list" }
    return [pscustomobject]@{ Status = $status.Trim(); List = $list.Trim() }
}

function Assert-QacOwnedGuest([int]$QacProcessId, [string]$ReservationId,
                              [int]$ExpectedQmpPort, [string]$Context) {
    $health = Get-QacHealth $Context
    $pattern = '(?m)^\s*' + [regex]::Escape($ReservationId) +
        ' \| guideXOSServer-AARCH64-ABI3-Studio \| acceptance \| Running \| queue \d+ \| [^\r\n]*\(PID ' +
        $QacProcessId + '\) \| owner Verified \| child PID (\d+)\r?$'
    $childMatch = [regex]::Match($health.List, $pattern)
    if (!$childMatch.Success) { throw "$Context QAC does not confirm the Running reservation and verified child PID for $ReservationId.`n$($health.List)" }
    $childPid = [int]$childMatch.Groups[1].Value
    $child = Get-CimInstance Win32_Process -Filter "ProcessId=$childPid" -ErrorAction Stop
    if (!$child) { throw "$Context QAC child PID $childPid is not visible through CIM yet." }
    $expectedPath = [IO.Path]::GetFullPath($QemuPath)
    $pathMatches = [string]::Equals([string]$child.ExecutablePath, $expectedPath, [StringComparison]::OrdinalIgnoreCase)
    $parentMatches = [int]$child.ParentProcessId -eq $QacProcessId
    $commandLine = [string]$child.CommandLine
    $machineMatches = [regex]::IsMatch($commandLine, '(?:^|\s)"?-machine"?\s+"?virt,gic-version=2,acpi=off"?(?:\s|$)')
    $cpuMatches = [regex]::IsMatch($commandLine, '(?:^|\s)"?-cpu"?\s+"?cortex-a53"?(?:\s|$)')
    $displayMatches = [regex]::IsMatch($commandLine, '(?:^|\s)"?-device"?\s+"?ramfb,xres=1280,yres=800"?(?:\s|$)')
    $qmpMatches = [regex]::IsMatch($commandLine, '(?:^|\s)"?-qmp"?\s+"?tcp:127\.0\.0\.1:' + $ExpectedQmpPort + '(?:,|"|\s)')
    if (!$pathMatches -or !$parentMatches -or !$machineMatches -or !$cpuMatches -or !$displayMatches -or !$qmpMatches) {
        throw "$Context QEMU ownership/profile mismatch (pid=$($child.ProcessId), parent=$($child.ParentProcessId), path=$($child.ExecutablePath), command=$commandLine)."
    }
    return $child
}

$qacBefore = Get-QacHealth 'Studio pre-admission'
$baselineQemu = @(Get-QemuProcesses | ForEach-Object { [int]$_.ProcessId })
if ($baselineQemu.Count -ne 0) { throw "Studio pre-admission found existing QEMU processes: $($baselineQemu -join ',')." }

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
$reservationState = ''
$ownedChild = $null
$ownershipFailure = ''
$launchProbeFailure = ''
$admissionDeadline = [DateTimeOffset]::UtcNow.AddSeconds($AdmissionTimeoutSeconds)
while ([DateTimeOffset]::UtcNow -lt $admissionDeadline -and !$qac.HasExited) {
    $status = & $QacCliPath list 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        $match = [regex]::Match($status, '(?m)^\s*([0-9a-f]{32}) \| guideXOSServer-AARCH64-ABI3-Studio \| acceptance \| ([^|]+) \| queue \d+ \| [^\r\n]*\(PID ' + $qac.Id + '\) \| owner ')
        if ($match.Success) {
            $reservationId = $match.Groups[1].Value
            $reservationState = $match.Groups[2].Value.Trim()
            if ($reservationState -eq 'Running') {
                try {
                    $launchDeadline = [DateTimeOffset]::UtcNow.AddSeconds(15)
                    while ([DateTimeOffset]::UtcNow -lt $launchDeadline) {
                        try {
                            $ownedChild = Assert-QacOwnedGuest $qac.Id $reservationId $qmpPort 'Studio exclusive launch'
                            $running = $true
                            break
                        } catch { $launchProbeFailure = $_.Exception.Message }
                        Start-Sleep -Milliseconds 100
                    }
                    if (!$running) { throw "QAC reported Running but its verified child did not become visible within 15 seconds. Last probe: $launchProbeFailure" }
                } catch { $ownershipFailure = $_.Exception.Message }
                break
            }
            if ($reservationState -in @('Waiting','Blocked')) { Start-Sleep -Milliseconds 250; continue }
        }
    }
    Start-Sleep -Milliseconds 250
}

if (!$running) {
    if ($reservationId -and $reservationState -in @('Waiting','Blocked')) { $null = & $QacCliPath cancel $reservationId 2>&1 }
    if ($ownershipFailure) { throw "Studio failed closed on QAC/QEMU ownership verification: $ownershipFailure" }
    if (!$qac.WaitForExit(5000)) { throw 'QAC did not confirm cancellation of the ungranted request; no guest was launched.' }
    $qacText = $stdoutTask.GetAwaiter().GetResult() + $stderrTask.GetAwaiter().GetResult()
    $qacText | Set-Content -LiteralPath $qacLogPath -Encoding utf8
    $record = [ordered]@{
        outcome = 'ABI3_GUEST_PENDING_QAC'; reservation_id = $reservationId
        qemu_launched = $false; qmp_port = $qmpPort; guest_log = $guestLogPath; qac_log = $qacLogPath
        qac_status_after = (& $QacCliPath status 2>&1 | Out-String).Trim()
    qac_status_before = $qacBefore.Status; qac_list_before = $qacBefore.List
    qemu_process_ids_before = $baselineQemu
    firmware_vars_initial_sha256 = $initialVarsSha256
    firmware_vars_template_sha256 = $varsTemplateSha256
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
    Write-Host "ABI3_GUEST_PENDING_QAC: request not granted within $AdmissionTimeoutSeconds seconds; no QEMU was launched." -ForegroundColor Yellow
    Write-Host "Session record: $sessionPath"
    exit 2
}

$record = [ordered]@{
    outcome = 'ABI3_STUDIO_GUEST_IN_PROGRESS'; project = 'guideXOSServer-AARCH64-ABI3-Studio'
    reservation_id = $reservationId; qmp_port = $qmpPort; machine = 'virt,gic-version=2,acpi=off'
    cpu = 'cortex-a53'; display = '1280x800'; qemu = [IO.Path]::GetFullPath($QemuPath); guest_log = $guestLogPath
    qac_log = $qacLogPath; screenshot = $screenshotPath; qemu_launched = $true
    studio_window_screenshot = $false; studio_close_and_cleanup = $false; runtime_deadline_seconds = $RuntimeSeconds
    post_studio_exit_seconds = $PostStudioExitSeconds; desktop_post_exit_wait_completed = $false
    qac_process_id = $qac.Id; managed_qemu_child_pid = [int]$ownedChild.ProcessId
    managed_qemu_child_parent_pid = [int]$ownedChild.ParentProcessId
    qac_status_before = $qacBefore.Status; qac_list_before = $qacBefore.List
    qemu_process_ids_before = $baselineQemu
    firmware_vars_initial_sha256 = $initialVarsSha256
    firmware_vars_template_sha256 = $varsTemplateSha256
}
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
Write-Host "QAC granted reservation $reservationId and launched QEMU PID $($qac.Id)." -ForegroundColor Cyan
Write-Host "QMP port: $qmpPort; interact using the recorded port while this script holds the acceptance reservation."

$deadline = [DateTimeOffset]::UtcNow.AddSeconds($RuntimeSeconds)
$screenshotTaken = $false
$lastGuest = ''
$runtimeOwnershipFailure = ''
$nextOwnershipCheck = [DateTimeOffset]::UtcNow
$phase12PassObservedAt = $null
$postStudioExitDeadline = $null
$postStudioExitWaitCompleted = $false
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
    if ($lastGuest.Contains('AARCH64_PHASE12_PASS')) {
        if ($null -eq $phase12PassObservedAt) {
            $phase12PassObservedAt = [DateTimeOffset]::UtcNow
            $postStudioExitDeadline = $phase12PassObservedAt.AddSeconds($PostStudioExitSeconds)
            Write-Host "Studio cleanup passed; keeping the desktop alive for $PostStudioExitSeconds seconds of post-exit observation." -ForegroundColor Cyan
        }
        if ([DateTimeOffset]::UtcNow -ge $postStudioExitDeadline) {
            $postStudioExitWaitCompleted = $true
            break
        }
    }
    if ($lastGuest -match 'AARCH64_PHASE[234]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') { break }
    if ($qac.HasExited) { break }
    if ([DateTimeOffset]::UtcNow -ge $nextOwnershipCheck) {
        try {
            $active = Get-QacHealth 'Studio active guest'
            if ($active.List -notmatch [regex]::Escape($reservationId) -or
                $active.List -notmatch [regex]::Escape("PID $($qac.Id)")) {
                throw "QAC no longer lists Studio reservation $reservationId as owned by PID $($qac.Id)."
            }
            $activeChild = Assert-QacOwnedGuest $qac.Id $reservationId $qmpPort 'Studio active guest'
            if ([int]$activeChild.ProcessId -ne [int]$ownedChild.ProcessId) { throw 'The managed QEMU child PID changed during Studio execution.' }
        } catch { $runtimeOwnershipFailure = $_.Exception.Message; break }
        $nextOwnershipCheck = [DateTimeOffset]::UtcNow.AddSeconds(5)
    }
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
$remainingQemu = Get-QemuProcesses
if (@($remainingQemu | Where-Object { [int]$_.ProcessId -eq [int]$ownedChild.ProcessId }).Count -ne 0) {
    throw "QAC reported exit but managed QEMU child PID $($ownedChild.ProcessId) remains."
}
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
$record.focus_event_delivered = $lastGuest.Contains('ABI3 guest observed Studio focus event through v0.5 poll_event')
$record.close_event_delivered = $lastGuest.Contains('ABI3 guest observed Studio close event through v0.5 poll_event')
$record.close_and_cleanup = $lastGuest.Contains('NativeElf GUI cleanup: PASS')
$record.qac_launched_child = $qacText -match "QAC QEMU launched: reservation=$reservationId"
$record.qac_released_reservation = $qacText -match "QAC reservation released: id=$reservationId"
$record.qemu_exit_code = $qac.ExitCode
$record.qac_process_id = $qac.Id
$record.managed_qemu_child_pid = [int]$ownedChild.ProcessId
$record.managed_qemu_child_parent_pid = [int]$ownedChild.ParentProcessId
$record.qemu_process_ids_before = $baselineQemu
$record.qemu_process_count_after_cleanup = $remainingQemu.Count
$record.qac_exclusive_grant = $qacText -match "QAC exclusive reservation granted: id=$reservationId"
$record.qac_child_exit_verified = $qacText -match "QAC QEMU exited: reservation=$reservationId"
$record.qac_status_before = $qacBefore.Status
$record.qac_list_before = $qacBefore.List
$qacAfter = Get-QacHealth 'Studio post-cleanup'
$record.qac_status_after = $qacAfter.Status
$record.qac_list_after = $qacAfter.List
$record.firmware_vars_initial_sha256 = $initialVarsSha256
$record.firmware_vars_template_sha256 = $varsTemplateSha256
$record.firmware_vars_after_sha256 = (Get-FileHash -LiteralPath $varsPath -Algorithm SHA256).Hash.ToLowerInvariant()
$record.degraded_ownership_observed = $qacText -match '(?i)exclusive.{0,40}degraded|foreign QEMU process'
$record.runtime_ownership_failure = $runtimeOwnershipFailure
$record.desktop_post_exit_wait_completed = $postStudioExitWaitCompleted
$record.post_studio_exit_observed_seconds = if ($null -ne $phase12PassObservedAt) {
    [Math]::Max(0, [int]([DateTimeOffset]::UtcNow - $phase12PassObservedAt).TotalSeconds)
} else { 0 }
$record.outcome = if ($record.guest_pass_marker -and $record.studio_entry_called -and $record.adapter_selected -and
    $record.window_created -and $record.initial_render -and $record.mouse_event_delivered -and
    $record.key_event_delivered -and $record.focus_event_delivered -and $record.close_event_delivered -and
    $record.close_and_cleanup -and $record.qac_launched_child -and
    $record.qac_released_reservation -and $record.desktop_post_exit_wait_completed -and !$timedOut) {
    'ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS'
} elseif ($timedOut) { 'ABI3_STUDIO_GUEST_RUNTIME_TIMEOUT' } else { 'ABI3_STUDIO_GUEST_PARTIAL' }
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
$lastGuest | Set-Content -LiteralPath (Join-Path $logDirectory 'guest-final.log') -Encoding utf8
Write-Host "Outcome: $($record.outcome)" -ForegroundColor $(if ($record.outcome -eq 'ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS') { 'Green' } else { 'Yellow' })
Write-Host "Guest log: $(Join-Path $logDirectory 'guest-final.log')"
Write-Host "QAC log: $qacLogPath"
Write-Host "Session record: $sessionPath"
if (!$record.qac_exclusive_grant -or !$record.qac_launched_child -or !$record.qac_child_exit_verified -or
    !$record.qac_released_reservation -or $record.degraded_ownership_observed -or $runtimeOwnershipFailure -or
    $record.qemu_process_count_after_cleanup -ne 0 -or $record.qac_list_after -match [regex]::Escape($reservationId)) {
    throw "Studio run did not pass exclusive admission, ownership, child-exit, and reservation-release checks. See $sessionPath"
}
if ($record.outcome -ne 'ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS') { exit 1 }
