[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ArtifactDirectory,
    [string]$QacCliPath = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [int]$AdmissionTimeoutSeconds = 12,
    [int]$RuntimeSeconds = 120
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$ArtifactDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to run the BOOTDTB1 guest outside repository out/: $ArtifactDirectory"
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
foreach ($required in @('EFI\BOOT\BOOTAA64.EFI', 'kernel.elf', 'ramdisk.img')) {
    if (!(Test-Path -LiteralPath (Join-Path $esp $required) -PathType Leaf)) {
        throw "Diagnostic ESP is missing $required"
    }
}

$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runDirectory = Join-Path $ArtifactDirectory ("bootdtb1-guest-{0}" -f $runId)
$null = New-Item -ItemType Directory -Path $runDirectory
$qacLogPath = Join-Path $runDirectory 'qac.log'
$guestLogPath = Join-Path $runDirectory 'guest.log'
$sessionPath = Join-Path $runDirectory 'session.json'
$varsPath = Join-Path $runDirectory 'edk2-vars.fd'
Copy-Item -LiteralPath $FirmwareVarsTemplate -Destination $varsPath

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
try { $qmpPort = ([Net.IPEndPoint]$listener.LocalEndpoint).Port }
finally { $listener.Stop() }

$project = 'guideXOSServer-AARCH64-BOOTDTB1'
$guestArguments = @(
    '-machine', 'virt,gic-version=2,acpi=off', '-cpu', 'cortex-a53', '-m', '512M',
    '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
    '-drive', "if=pflash,format=raw,unit=1,file=$varsPath",
    '-drive', "file=fat:rw:$esp,format=raw", '-display', 'none',
    '-monitor', 'none', '-serial', "file:$guestLogPath", '-no-reboot',
    '-qmp', "tcp:127.0.0.1:$qmpPort,server=on,wait=off")

function Quote-ProcessArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { ++$slashes; continue }
        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, (2 * $slashes + 1))
            [void]$builder.Append([char]34)
            $slashes = 0
            continue
        }
        if ($slashes -gt 0) { [void]$builder.Append([char]92, $slashes) }
        [void]$builder.Append($character)
        $slashes = 0
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

function Send-Qmp([IO.StreamReader]$Reader, [IO.StreamWriter]$Writer, [string]$Command) {
    $Writer.WriteLine((@{ execute = $Command } | ConvertTo-Json -Compress))
    while ($true) {
        $line = $Reader.ReadLine()
        if (!$line) { throw "QMP closed before returning $Command" }
        $response = $line | ConvertFrom-Json
        if ($response.error) { throw "QMP $Command failed: $($response.error.desc)" }
        if ($response.PSObject.Properties.Name -contains 'return') { return $response.return }
    }
}

function Get-FirstEvidenceLine([string]$Text, [string]$Pattern) {
    $match = [regex]::Match($Text, "(?m)^.*$Pattern.*$")
    if ($match.Success) { return $match.Value.Trim() }
    return ''
}

$qacArguments = @('run', '--project', $project, '--mode', 'acceptance', '--', $QemuPath) + $guestArguments
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = [IO.Path]::GetFullPath($QacCliPath)
$startInfo.Arguments = (($qacArguments | ForEach-Object { Quote-ProcessArgument ([string]$_) }) -join ' ')
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$qac = [Diagnostics.Process]::new()
$qac.StartInfo = $startInfo
$null = $qac.Start()
$stdoutTask = $qac.StandardOutput.ReadToEndAsync()
$stderrTask = $qac.StandardError.ReadToEndAsync()
$reservationId = ''
$running = $false
$admissionDeadline = [DateTimeOffset]::UtcNow.AddSeconds($AdmissionTimeoutSeconds)
while ([DateTimeOffset]::UtcNow -lt $admissionDeadline -and !$qac.HasExited) {
    $status = & $QacCliPath list 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        $escapedProject = [regex]::Escape($project)
        $match = [regex]::Match($status, '(?m)^\s*([0-9a-f]{32}) \| ' + $escapedProject + ' \| acceptance \| ([^|]+) \| queue \d+ \| [^\r\n]*\(PID ' + $qac.Id + '\) \| owner ')
        if ($match.Success) {
            $reservationId = $match.Groups[1].Value
            if ($match.Groups[2].Value.Trim() -eq 'Running') { $running = $true; break }
            if ($match.Groups[2].Value.Trim() -in @('Waiting', 'Blocked')) {
                Start-Sleep -Milliseconds 250
                continue
            }
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
        outcome = 'ARM64_DTB_HOSTED_READY_QAC_PENDING'
        project = $project; reservation_id = $reservationId; qemu_launched = $false
        admission_timeout_seconds = $AdmissionTimeoutSeconds
        qac_status_after = (& $QacCliPath status 2>&1 | Out-String).Trim()
        guest_log = $guestLogPath; qac_log = $qacLogPath
        machine = 'virt,gic-version=2,acpi=off'; cpu = 'cortex-a53'; memory = '512M'
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
    Write-Host "ARM64_DTB_HOSTED_READY_QAC_PENDING: no exclusive grant in $AdmissionTimeoutSeconds seconds; QEMU was not launched." -ForegroundColor Yellow
    Write-Host "Session record: $sessionPath"
    exit 2
}

$record = [ordered]@{
    outcome = 'BOOTDTB1_GUEST_IN_PROGRESS'; project = $project
    reservation_id = $reservationId; qmp_port = $qmpPort; qemu_launched = $true
    machine = 'virt,gic-version=2,acpi=off'; cpu = 'cortex-a53'; memory = '512M'
    qemu = [IO.Path]::GetFullPath($QemuPath); firmware_code = [IO.Path]::GetFullPath($FirmwareCode)
    firmware_vars_template = [IO.Path]::GetFullPath($FirmwareVarsTemplate)
    guest_log = $guestLogPath; qac_log = $qacLogPath; runtime_deadline_seconds = $RuntimeSeconds
}
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
Write-Host "QAC granted exclusive reservation $reservationId for $project. QEMU is running as the managed child." -ForegroundColor Cyan

$deadline = [DateTimeOffset]::UtcNow.AddSeconds($RuntimeSeconds)
$lastGuest = ''
while ([DateTimeOffset]::UtcNow -lt $deadline) {
    $lastGuest = Read-GuestLog
    if ($lastGuest.Contains('AARCH64_PHASE4_PASS') -or
        $lastGuest -match 'DTB diagnostic=2|DTB handoff pointer or length: FAIL|DTB handoff memory-map coverage: FAIL|DTB header: status=[1-9]|AARCH64_PHASE4_ERROR|\[A64 UEFI\] ERROR') { break }
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
            $client.ReceiveTimeout = 3000; $client.SendTimeout = 3000
            $client.Connect([Net.IPAddress]::Loopback, $qmpPort)
            $stream = $client.GetStream(); $stream.ReadTimeout = 3000; $stream.WriteTimeout = 3000
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
            $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 4096, $true)
            $writer.AutoFlush = $true
            $null = $reader.ReadLine()
            $null = Send-Qmp $reader $writer 'qmp_capabilities'
            $null = Send-Qmp $reader $writer 'quit'
            $quitSent = $true
        } catch { Start-Sleep -Milliseconds 250 }
        finally { $client.Dispose() }
    }
    if (!$quitSent) { throw 'QMP shutdown failed; QAC remains responsible for the managed guest and reservation.' }
    if (!$qac.WaitForExit(15000)) { throw 'QAC did not verify QEMU exit after QMP shutdown.' }
}
$qac.WaitForExit()
$qacText = $stdoutTask.GetAwaiter().GetResult() + $stderrTask.GetAwaiter().GetResult()
$qacText | Set-Content -LiteralPath $qacLogPath -Encoding utf8
$lastGuest = Read-GuestLog

$metric = [regex]::Match($lastGuest, 'memory/VFS durability: PASS [^\r\n]*ticks=(\d+) context-switches=(\d+) preemptions=(\d+) [^\r\n]*unexpected-irq=(\d+) exceptions=(\d+)')
$record.outcome = if ($lastGuest.Contains('AARCH64_PHASE4_PASS') -and $metric.Success -and
    [uint64]$metric.Groups[1].Value -gt 0 -and [uint64]$metric.Groups[2].Value -gt 0 -and
    [uint64]$metric.Groups[3].Value -gt 0 -and [uint64]$metric.Groups[4].Value -eq 0 -and
    [uint64]$metric.Groups[5].Value -eq 0) {
    'ARM64_DTB_GUEST_PROGRESS_PHASE4_PASS'
} elseif ($lastGuest -match 'DTB diagnostic=2|DTB handoff pointer or length: FAIL|DTB handoff memory-map coverage: FAIL|DTB header: status=[1-9]') {
    'ARM64_DTB_FAILURE_REPRODUCED_WITH_TRACE'
} elseif ($timedOut) {
    'BOOTDTB1_GUEST_RUNTIME_TIMEOUT'
} else {
    'BOOTDTB1_GUEST_STOPPED_BEFORE_PHASE4_PASS'
}
$record.runtime_timeout = $timedOut
$record.guest_pass_marker = $lastGuest.Contains('AARCH64_PHASE4_PASS')
$record.original_diagnostic_2 = $lastGuest.Contains('DTB diagnostic=2')
$record.loader_dtb_discovery = Get-FirstEvidenceLine $lastGuest 'DTB discovery:'
$record.loader_dtb_copy = Get-FirstEvidenceLine $lastGuest 'DTB copy:'
$record.kernel_dtb_handoff = Get-FirstEvidenceLine $lastGuest 'DTB handoff:'
$record.kernel_dtb_memory = Get-FirstEvidenceLine $lastGuest 'DTB handoff memory:'
$record.loader_dtb_header = Get-FirstEvidenceLine $lastGuest '\[A64 UEFI\] DTB header:'
$record.kernel_dtb_header = Get-FirstEvidenceLine $lastGuest '\[guideXOS\] DTB header:'
$record.kernel_dtb_parser_trace = Get-FirstEvidenceLine $lastGuest 'DTB parser trace:'
$parserMatch = [regex]::Match($lastGuest, 'DTB parser trace: parsed=(\d+) token=(\d+) parser-stage=(\d+) parser-error=(\d+) structure-offset=(\d+)')
$record.parser_stage_name = if ($parserMatch.Success) {
    switch ([int]$parserMatch.Groups[3].Value) { 1 { 'HEADER' } 2 { 'STRUCTURE' } 3 { 'PLATFORM' } 4 { 'COMPLETE' } default { 'UNKNOWN' } }
} else { '' }
$record.parser_error_name = if ($parserMatch.Success) {
    switch ([int]$parserMatch.Groups[4].Value) {
        0 { 'NONE' } 1 { 'TRUNCATED_TOKEN' } 2 { 'NESTING' } 3 { 'NODE_NAME' }
        4 { 'END_NODE' } 5 { 'PROPERTY_HEADER' } 6 { 'PROPERTY_VALUE' }
        7 { 'PROPERTY_NAME' } 8 { 'PROPERTY_VALUE_FORMAT' } 9 { 'REG_RANGE' }
        10 { 'CPU_AFFINITY' } 11 { 'STRUCTURE_END' } 12 { 'UNKNOWN_TOKEN' }
        13 { 'REQUIRED_PLATFORM' } 14 { 'CONTROLLER' } 15 { 'TIMER' }
        16 { 'STRUCTURE' } default { 'UNKNOWN' }
    }
} else { '' }
$record.parser_token_name = if ($parserMatch.Success -and [int]$parserMatch.Groups[2].Value -eq 3) { 'FDT_PROP' } else { '' }
$record.gic_v2_selected = $lastGuest.Contains('[guideXOS] GIC: OK version=2')
$record.timer_enabled = $lastGuest.Contains('[guideXOS] physical timer: enabled')
$record.timer_ticks = if ($metric.Success) { [uint64]$metric.Groups[1].Value } else { 0 }
$record.scheduler_context_switches = if ($metric.Success) { [uint64]$metric.Groups[2].Value } else { 0 }
$record.scheduler_preemptions = if ($metric.Success) { [uint64]$metric.Groups[3].Value } else { 0 }
$record.unexpected_irqs = if ($metric.Success) { [uint64]$metric.Groups[4].Value } else { 0 }
$record.exceptions = if ($metric.Success) { [uint64]$metric.Groups[5].Value } else { 0 }
$record.qac_launched_child = $qacText -match "QAC QEMU launched: reservation=$reservationId"
$record.qac_released_reservation = $qacText -match "QAC reservation released: id=$reservationId"
$record.qac_exit_code = $qac.ExitCode
$record.guest_sha256 = if (Test-Path -LiteralPath $guestLogPath -PathType Leaf) {
    (Get-FileHash -LiteralPath $guestLogPath -Algorithm SHA256).Hash.ToLowerInvariant()
} else { '' }
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
Write-Host "BOOTDTB1 guest result: $($record.outcome)" -ForegroundColor $(if ($record.guest_pass_marker) { 'Green' } else { 'Yellow' })
Write-Host "Guest log: $guestLogPath"
Write-Host "QAC log: $qacLogPath"
Write-Host "Session record: $sessionPath"
if ($record.outcome -eq 'ARM64_DTB_GUEST_PROGRESS_PHASE4_PASS') { exit 0 }
exit 1
