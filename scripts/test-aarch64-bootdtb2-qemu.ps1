[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ArtifactDirectory,
    [string]$QacCliPath = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [string]$QacProject = 'guideXOSServer-AARCH64-BOOTDTB2',
    [int]$AdmissionTimeoutSeconds = 12,
    [int]$RuntimeSeconds = 120
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$ArtifactDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to run the BOOTDTB2 guest outside repository out/: $ArtifactDirectory"
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
$runDirectory = Join-Path $ArtifactDirectory ("bootdtb2-guest-{0}" -f $runId)
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

$project = $QacProject
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

function Send-QmpHmp([IO.StreamReader]$Reader, [IO.StreamWriter]$Writer, [string]$Command) {
    $payload = @{ execute = 'human-monitor-command'; arguments = @{ 'command-line' = $Command } }
    $Writer.WriteLine(($payload | ConvertTo-Json -Compress))
    while ($true) {
        $line = $Reader.ReadLine()
        if (!$line) { throw "QMP closed before HMP returned $Command" }
        $response = $line | ConvertFrom-Json
        if ($response.error) { throw "QMP HMP $Command failed: $($response.error.desc)" }
        if ($response.PSObject.Properties.Name -contains 'return') { return [string]$response.return }
    }
}

function Convert-HexToUInt64([string]$Value) {
    if ($Value -notmatch '^0x[0-9a-fA-F]+$') { throw "Invalid runtime guest address or size: $Value" }
    return [UInt64]::Parse($Value.Substring(2), [Globalization.NumberStyles]::HexNumber,
        [Globalization.CultureInfo]::InvariantCulture)
}

function Read-BigEndianU32([byte[]]$Bytes, [int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) { throw 'Truncated captured FDT header.' }
    return [uint32]([uint64]$Bytes[$Offset] * 16777216 + [uint64]$Bytes[$Offset + 1] * 65536 +
        [uint64]$Bytes[$Offset + 2] * 256 + [uint64]$Bytes[$Offset + 3])
}

function Test-GuestRamRange([string]$MemoryTree, [UInt64]$Base, [UInt64]$Size) {
    if ($Size -eq 0 -or $Base -gt ([UInt64]::MaxValue - $Size)) { return $false }
    $endExclusive = $Base + $Size
    $matches = [regex]::Matches($MemoryTree,
        '(?im)^\s*([0-9a-f]{8,16})-([0-9a-f]{8,16})\s+\([^)]*\bram\b[^)]*\):')
    foreach ($match in $matches) {
        $regionBase = [UInt64]::Parse($match.Groups[1].Value,
            [Globalization.NumberStyles]::HexNumber, [Globalization.CultureInfo]::InvariantCulture)
        $regionEndInclusive = [UInt64]::Parse($match.Groups[2].Value,
            [Globalization.NumberStyles]::HexNumber, [Globalization.CultureInfo]::InvariantCulture)
        if ($Base -ge $regionBase -and ($endExclusive - 1) -le $regionEndInclusive) { return $true }
    }
    return $false
}

function Save-ValidatedGuestDtb([IO.StreamReader]$Reader, [IO.StreamWriter]$Writer,
                                [string]$MemoryTree, [string]$Label,
                                [UInt64]$Base, [UInt64]$Size, [string]$Path,
                                [Diagnostics.Process]$QacProcess) {
    if ($QacProcess.HasExited) { throw 'QAC-managed QEMU exited before the DTB capture.' }
    if (!(Test-GuestRamRange $MemoryTree $Base $Size)) {
        throw "$Label DTB range is not wholly contained in a live QEMU RAM region."
    }
    if (Test-Path -LiteralPath $Path) { throw "Refusing to overwrite existing capture file: $Path" }
    $normalizedPath = [IO.Path]::GetFullPath($Path).Replace('\', '/')
    $command = 'pmemsave 0x{0:x} 0x{1:x} "{2}"' -f $Base, $Size, $normalizedPath
    $reply = Send-QmpHmp $Reader $Writer $command
    if ($reply -match '(?i)error|failed|cannot|invalid|not found') {
        throw "QMP pmemsave did not confirm $Label capture: $reply"
    }
    if ($QacProcess.HasExited) { throw 'QAC-managed QEMU exited during the DTB capture.' }
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "QMP pmemsave did not create $Label DTB artifact." }
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ([uint64]$bytes.Length -ne $Size) { throw "$Label DTB capture length $($bytes.Length) does not match declared size $Size." }
    $expectedMagic = [uint32]::Parse('d00dfeed', [Globalization.NumberStyles]::HexNumber,
        [Globalization.CultureInfo]::InvariantCulture)
    if ($bytes.Length -lt 40 -or (Read-BigEndianU32 $bytes 0) -ne $expectedMagic -or
        (Read-BigEndianU32 $bytes 4) -ne $Size) {
        throw "$Label DTB capture header magic or totalsize is invalid."
    }
    return [ordered]@{
        label = $Label; physical_base = ('0x{0:x}' -f $Base); declared_size = $Size
        file = $Path; file_length = $bytes.Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        capture_command = $command; qac_child_alive_after_capture = !$QacProcess.HasExited
    }
}

function Capture-GuestDtb([string]$GuestText, [string]$Directory, [Diagnostics.Process]$QacProcess) {
    $sourceLine = Get-FirstEvidenceLine $GuestText '\[A64 UEFI\] DTB discovery:'
    $copyLine = Get-FirstEvidenceLine $GuestText '\[A64 UEFI\] DTB copy:'
    $headerLine = Get-FirstEvidenceLine $GuestText '\[A64 UEFI\] DTB header:'
    $sourceMatch = [regex]::Match($sourceLine, 'pointer=(0x[0-9a-fA-F]+)')
    $copyMatch = [regex]::Match($copyLine, 'physical=(0x[0-9a-fA-F]+) size=(0x[0-9a-fA-F]+)')
    $headerMatch = [regex]::Match($headerLine, 'total=(0x[0-9a-fA-F]+)')
    if (!$sourceMatch.Success -or !$copyMatch.Success -or !$headerMatch.Success) {
        throw 'Runtime UEFI source address, copied range, or validated header total is missing.'
    }
    $sourceBase = Convert-HexToUInt64 $sourceMatch.Groups[1].Value
    $copyBase = Convert-HexToUInt64 $copyMatch.Groups[1].Value
    $copySize = Convert-HexToUInt64 $copyMatch.Groups[2].Value
    $headerSize = Convert-HexToUInt64 $headerMatch.Groups[1].Value
    if ($copySize -ne $headerSize -or $copySize -lt 40 -or $copySize -gt 16777216) {
        throw 'Loader copy size does not match the validated FDT totalsize or allowed DTB bounds.'
    }
    if ($sourceBase -gt ([UInt64]::MaxValue - $copySize) -or
        $copyBase -gt ([UInt64]::MaxValue - $copySize)) { throw 'Runtime DTB physical range overflows.' }

    $client = $null
    try {
        $lastFailure = $null
        for ($attempt = 0; $attempt -lt 20 -and !$client; ++$attempt) {
            try {
                $client = [Net.Sockets.TcpClient]::new()
                $client.ReceiveTimeout = 5000; $client.SendTimeout = 5000
                $client.Connect([Net.IPAddress]::Loopback, $qmpPort)
            } catch {
                if ($client) { $client.Dispose(); $client = $null }
                $lastFailure = $_.Exception.Message
                Start-Sleep -Milliseconds 250
            }
        }
        if (!$client) { throw "Unable to connect to live guest QMP for capture: $lastFailure" }
        $stream = $client.GetStream(); $stream.ReadTimeout = 5000; $stream.WriteTimeout = 5000
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
        $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 4096, $true)
        $writer.AutoFlush = $true
        $greeting = $reader.ReadLine() | ConvertFrom-Json
        if (!$greeting.QMP) { throw 'QMP greeting was not received.' }
        $null = Send-Qmp $reader $writer 'qmp_capabilities'
        $state = Send-Qmp $reader $writer 'query-status'
        if ($state.status -ne 'running') { throw "Guest was not running at capture entry (state=$($state.status))." }
        $null = Send-Qmp $reader $writer 'stop'
        $state = Send-Qmp $reader $writer 'query-status'
        if ($state.status -ne 'paused') { throw "QMP did not pause the diagnostic guest (state=$($state.status))." }
        if ($QacProcess.HasExited) { throw 'QAC-managed QEMU exited before capture while paused.' }
        $memoryTree = Send-QmpHmp $reader $writer 'info mtree'
        $memoryTreePath = Join-Path $Directory 'qmp-memory-tree.txt'
        $memoryTree | Set-Content -LiteralPath $memoryTreePath -Encoding utf8
        if (!(Test-GuestRamRange $memoryTree $sourceBase $copySize) -or
            !(Test-GuestRamRange $memoryTree $copyBase $copySize)) {
            throw 'One or both runtime DTB ranges are not wholly inside current QEMU RAM.'
        }
        $sourcePath = Join-Path $Directory 'firmware-source.dtb'
        $copyPath = Join-Path $Directory 'loader-copy.dtb'
        $source = Save-ValidatedGuestDtb $reader $writer $memoryTree 'firmware-source' `
            $sourceBase $copySize $sourcePath $QacProcess
        $copy = Save-ValidatedGuestDtb $reader $writer $memoryTree 'loader-copy' `
            $copyBase $copySize $copyPath $QacProcess
        $equal = $source.sha256 -eq $copy.sha256
        $capture = [ordered]@{
            status = 'CAPTURED_AND_VALIDATED'; guest_paused = $true
            guest_ram_map_file = $memoryTreePath; declared_fdt_total_size = $copySize
            source = $source; loader_copy = $copy; source_copy_bytes_equal = $equal
            qac_child_alive = !$QacProcess.HasExited
            source_artifact_sha256 = $source.sha256; loader_copy_sha256 = $copy.sha256
        }
        $capture | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Directory 'capture.json') -Encoding utf8
        return $capture
    } finally {
        if ($client) { $client.Dispose() }
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
        outcome = 'ARM64_DTB_CAPTURE_PENDING_QAC'
        project = $project; reservation_id = $reservationId; qemu_launched = $false
        admission_timeout_seconds = $AdmissionTimeoutSeconds
        qac_status_after = (& $QacCliPath status 2>&1 | Out-String).Trim()
        guest_log = $guestLogPath; qac_log = $qacLogPath
        machine = 'virt,gic-version=2,acpi=off'; cpu = 'cortex-a53'; memory = '512M'
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
    Write-Host "ARM64_DTB_CAPTURE_PENDING_QAC: no exclusive grant in $AdmissionTimeoutSeconds seconds; QEMU was not launched." -ForegroundColor Yellow
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
$captureResult = [ordered]@{ status = 'NOT_ATTEMPTED'; reason = '' }
if ($lastGuest -match 'DTB diagnostic=2') {
    try {
        $captureResult = Capture-GuestDtb $lastGuest $runDirectory $qac
    } catch {
        $captureResult = [ordered]@{ status = 'CAPTURE_FAILED'; reason = $_.Exception.Message }
        $captureResult | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runDirectory 'capture.json') -Encoding utf8
        Write-Host "Bounded QMP DTB capture failed: $($_.Exception.Message)" -ForegroundColor Yellow
    }
}
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
    if ($captureResult.status -eq 'CAPTURED_AND_VALIDATED') {
        'ARM64_DTB_PROPERTY_LOCALIZED'
    } else {
        'ARM64_DTB_CAPTURE_PENDING_QAC'
    }
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
$record.parser_property_rejection = Get-FirstEvidenceLine $lastGuest 'DTB property rejection:'
$record.capture = $captureResult
$record.candidate_manifest = Join-Path $ArtifactDirectory 'candidate-manifest.json'
$record.candidate_manifest_sha256 = if (Test-Path -LiteralPath $record.candidate_manifest) {
    (Get-FileHash -LiteralPath $record.candidate_manifest -Algorithm SHA256).Hash.ToLowerInvariant()
} else { '' }
$record.candidate_artifacts = [ordered]@{}
foreach ($artifact in @('BOOTAA64.EFI', 'kernel.elf', 'esp\ramdisk.img')) {
    $artifactPath = Join-Path $ArtifactDirectory $artifact
    $record.candidate_artifacts[$artifact] = if (Test-Path -LiteralPath $artifactPath) {
        (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { '' }
}
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
Write-Host "BOOTDTB2 guest result: $($record.outcome)" -ForegroundColor $(if ($record.guest_pass_marker) { 'Green' } else { 'Yellow' })
Write-Host "Guest log: $guestLogPath"
Write-Host "QAC log: $qacLogPath"
Write-Host "Session record: $sessionPath"
if ($record.outcome -eq 'ARM64_DTB_GUEST_PROGRESS_PHASE4_PASS') { exit 0 }
exit 1
