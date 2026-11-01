[CmdletBinding()]
param(
    [string]$ArtifactDirectory = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [string]$QacCliPath = '',
    [int]$TimeoutSeconds = 120,
    [int]$AdmissionTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$qacPreflightFailure = ''
if ($TimeoutSeconds -lt 1 -or $AdmissionTimeoutSeconds -lt 1) {
    throw 'Guest runtime and QAC admission timeouts must be positive.'
}
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    $ArtifactDirectory = Join-Path $repoRoot 'out\aarch64-gicv3-guest'
} else {
    $ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
}
$rootPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$ArtifactDirectory.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use GICv3 guest artifacts outside repository out/: $ArtifactDirectory"
}
if (!(Test-Path -LiteralPath $QemuPath -PathType Leaf)) { throw "QEMU not found: $QemuPath" }
if (!(Test-Path -LiteralPath $FirmwareCode -PathType Leaf)) { throw "AArch64 UEFI code firmware not found: $FirmwareCode" }
if (!(Test-Path -LiteralPath $FirmwareVarsTemplate -PathType Leaf)) { throw "AArch64 UEFI vars template not found: $FirmwareVarsTemplate" }

if ([string]::IsNullOrWhiteSpace($QacCliPath)) {
    $QacCliPath = $env:GUIDEXOS_QAC_CLI
}
if ([string]::IsNullOrWhiteSpace($QacCliPath)) {
    $qacCommand = Get-Command qac.exe, GuideXOS.QAC.Cli.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($qacCommand) { $QacCliPath = $qacCommand.Source }
}
if ([string]::IsNullOrWhiteSpace($QacCliPath)) {
    $QacCliPath = Join-Path (Split-Path -Parent $repoRoot) 'guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
}
if (!(Test-Path -LiteralPath $QacCliPath -PathType Leaf)) {
    $qacPreflightFailure = "QAC CLI not found at $QacCliPath. No QEMU guest was launched."
} else {
    $QacCliPath = [IO.Path]::GetFullPath($QacCliPath)
}

$version = & $QemuPath --version 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw 'Unable to read installed QEMU version' }
$cpuList = & $QemuPath -cpu help 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $cpuList -notmatch '(?im)^\s*cortex-a53\b') {
    throw 'Installed QEMU does not report cortex-a53 in its supported CPU models'
}
$machineList = & $QemuPath -machine help 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $machineList -notmatch '(?im)^\s*virt\b') {
    throw 'Installed QEMU does not report the virt machine'
}
$machineProperties = & $QemuPath -machine virt,help 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $machineProperties -notmatch '(?im)^\s*gic-version=<string>') {
    throw 'Installed QEMU virt machine does not report a gic-version property'
}
if (!(Test-Path -LiteralPath $ArtifactDirectory -PathType Container)) {
    $null = New-Item -ItemType Directory -Path $ArtifactDirectory -Force
}

$qacStatus = ''
$qacList = ''
if (!$qacPreflightFailure) {
    Write-Host 'Checking the real QAC coordinator before guest admission...' -ForegroundColor Cyan
    $qacStatus = & $QacCliPath status 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $qacStatus -notmatch '(?m)^Coordinator: Online\r?$') {
        $qacPreflightFailure = "QAC coordinator is unavailable. No QEMU guest was launched.`n$qacStatus"
    } else {
        $qacList = & $QacCliPath list 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            $qacPreflightFailure = "Unable to inspect QAC reservations. No QEMU guest was launched.`n$qacList"
        } elseif ($qacStatus -match 'Recovery: Required' -or $qacStatus -match '([1-9][0-9]*) foreign') {
            $qacPreflightFailure = "QAC reports recovery or foreign QEMU state. No QEMU guest was launched.`n$qacStatus"
        }
    }
} else {
    Write-Host 'QAC admission tooling is unavailable; building and recording readiness without launching QEMU.' -ForegroundColor Yellow
}

Write-Host 'Building the Phase 4 ARM64 candidate before QAC guest admission...' -ForegroundColor Cyan
$buildDirectory = Join-Path $ArtifactDirectory 'gicv3-image'
& (Join-Path $PSScriptRoot 'build-aarch64-gicv3.ps1') -OutputDirectory $buildDirectory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 GICv3 build failed' }
$esp = Join-Path $buildDirectory 'esp'
foreach ($required in @('EFI\BOOT\BOOTAA64.EFI', 'kernel.elf', 'ramdisk.img')) {
    if (!(Test-Path -LiteralPath (Join-Path $esp $required) -PathType Leaf)) {
        throw "GICv3 ESP is missing $required"
    }
}

$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logsDirectory = Join-Path $ArtifactDirectory ("logs-gicv3-{0}" -f $runId)
$qacLogsDirectory = Join-Path $logsDirectory 'qac'
$null = New-Item -ItemType Directory -Path $qacLogsDirectory -Force
$sessionRecord = [ordered]@{
    phase = 'AARCH64-GIC3-2'
    qac_cli = $QacCliPath
    qac_status_before = $qacStatus.Trim()
    qac_list_before = $qacList.Trim()
    qemu_path = [IO.Path]::GetFullPath($QemuPath)
    qemu_version = $version.Trim()
    machine = 'virt,gic-version=3,acpi=off'
    cpu = 'cortex-a53'
    firmware_code = [IO.Path]::GetFullPath($FirmwareCode)
    firmware_vars_template = [IO.Path]::GetFullPath($FirmwareVarsTemplate)
    build_directory = $buildDirectory
    kernel_sha256 = (Get-FileHash -LiteralPath (Join-Path $buildDirectory 'kernel.elf') -Algorithm SHA256).Hash.ToLowerInvariant()
    runs = @()
}

function Quote-ProcessArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) {
            ++$backslashes
            continue
        }
        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, (2 * $backslashes + 1))
            [void]$builder.Append([char]34)
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) { [void]$builder.Append([char]92, $backslashes) }
        [void]$builder.Append($character)
        $backslashes = 0
    }
    if ($backslashes -gt 0) { [void]$builder.Append([char]92, (2 * $backslashes)) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Find-QacRunReservation([string]$ListText, [string]$Project, [int]$OwnerPid) {
    $pattern = '(?m)^\s*([0-9a-f]{32}) \| ' + [regex]::Escape($Project) +
        ' \| acceptance \| ([^|]+) \| queue \d+ \| [^\r\n]*\(PID ' + $OwnerPid + '\) \| owner '
    return [regex]::Match($ListText, $pattern)
}

function Read-GuestLog([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
            try { return $reader.ReadToEnd() }
            finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    } catch [IO.IOException] { return '' }
}

function Get-FreeQmpPort {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

function Send-QmpQuit([int]$Port) {
    for ($attempt = 0; $attempt -lt 30; ++$attempt) {
        $client = [Net.Sockets.TcpClient]::new()
        try {
            $client.ReceiveTimeout = 3000
            $client.SendTimeout = 3000
            $client.Connect([Net.IPAddress]::Loopback, $Port)
            $stream = $client.GetStream()
            $stream.ReadTimeout = 3000
            $stream.WriteTimeout = 3000
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 1024, $true)
            $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII, 1024, $true)
            $writer.AutoFlush = $true
            $greeting = $reader.ReadLine()
            if ([string]::IsNullOrWhiteSpace($greeting) -or $greeting -notmatch '"QMP"') {
                throw 'QMP greeting was missing or malformed'
            }
            $writer.WriteLine('{"execute":"qmp_capabilities"}')
            $capabilities = $reader.ReadLine()
            if ([string]::IsNullOrWhiteSpace($capabilities) -or $capabilities -notmatch '"return"') {
                throw "QMP capability negotiation failed: $capabilities"
            }
            $writer.WriteLine('{"execute":"quit"}')
            return $true
        } catch {
            if ($attempt -eq 29) { return $false }
            Start-Sleep -Milliseconds 150
        } finally {
            if ($client) { $client.Dispose() }
        }
    }
    return $false
}

function Stop-QacOwnedChildIfQmpFails([int]$QacProcessId, [string]$ExpectedQemuPath) {
    $processes = Get-CimInstance Win32_Process -Filter "Name='qemu-system-aarch64.exe'" -ErrorAction SilentlyContinue
    $owned = @($processes | Where-Object {
        $_.ParentProcessId -eq $QacProcessId -and
        [string]::Equals($_.ExecutablePath, $ExpectedQemuPath, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($owned.Count -eq 1) {
        Stop-Process -Id ([int]$owned[0].ProcessId) -Force
        return "Stopped the exact QAC-owned QEMU child PID $($owned[0].ProcessId) after QMP shutdown failed."
    }
    if ($owned.Count -eq 0) {
        return 'No direct QAC-owned QEMU child remained when the bounded shutdown check ran.'
    }
    throw 'More than one matching direct QAC-owned QEMU child was found; refusing to stop an ambiguous process set.'
}

function Assert-Gicv3GuestLog([string]$Text, [string]$Name) {
    $required = @(
        '[guideXOS] AARCH64 kernel entry', '[guideXOS] DTB: OK',
        '[guideXOS] GIC: OK version=3', '[guideXOS] DTB GICv3 layout:',
        '[guideXOS] GICv3 redistributor:', '[guideXOS] GICv3 ICC:',
        '[guideXOS] GICv3 timer PPI:', '[guideXOS] physical timer: programmed',
        '[guideXOS] scheduler: initialized', '[guideXOS] physical timer: enabled',
        '[guideXOS] exception return: PASS scheduler continuation resumed',
        '[guideXOS] GICv3 timer dispatch:', '[guideXOS] GICv3 scheduler:',
        'AARCH64_PHASE4_PASS'
    )
    if ($Text -match 'AARCH64_PHASE[234]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') {
        throw "$Name contains a guest fatal/error marker"
    }
    $last = -1
    foreach ($marker in $required) {
        $index = $Text.IndexOf($marker, [StringComparison]::Ordinal)
        if ($index -lt 0 -or $index -le $last) { throw "$Name is missing or misorders marker: $marker" }
        $last = $index
    }
    if ($Text -notmatch 'DTB GICv3 layout:.*timer-source=physical timer-PPI=(\d+)') {
        throw "$Name does not show a DTB-selected physical timer PPI"
    }
    $timerPpi = [uint32]$Matches[1]
    if ($timerPpi -lt 16 -or $timerPpi -ge 32) { throw "$Name selected an INTID outside the PPI range" }
    if ($Text -notmatch 'GICv3 redistributor:.*affinity-match=1 awake=1') {
        throw "$Name lacks matched and awake boot-CPU Redistributor evidence"
    }
    if ($Text -notmatch 'GICv3 ICC:.*SRE=\S+.*IGRPEN1=\S+.*EOImode=combined') {
        throw "$Name lacks enabled ICC system-register and combined-EOI evidence"
    }
    if ($Text -notmatch 'GICv3 timer PPI: INTID=\d+ group1=1 enabled=1 priority=128 trigger=level') {
        throw "$Name lacks the configured level-sensitive Group 1 timer PPI evidence"
    }
    $timer = [regex]::Match($Text, 'GICv3 timer dispatch: INTID=(\d+) ticks=(\d+) acknowledgements=(\d+) EOIs=(\d+) invalid-ack=(\d+) completion-errors=(\d+) pending=(\d+)')
    if (!$timer.Success) { throw "$Name lacks guest timer acknowledgement/EOI counters" }
    $irqId = [uint32]$timer.Groups[1].Value
    $ticks = [uint64]$timer.Groups[2].Value
    $acks = [uint64]$timer.Groups[3].Value
    $eois = [uint64]$timer.Groups[4].Value
    $invalid = [uint64]$timer.Groups[5].Value
    $completionErrors = [uint64]$timer.Groups[6].Value
    $pending = [uint64]$timer.Groups[7].Value
    if ($irqId -ne $timerPpi -or $ticks -lt 1000 -or $acks -ne $ticks -or
        $eois -ne $acks -or $invalid -ne 0 -or $completionErrors -ne 0 -or $pending -ne 0) {
        throw "$Name failed bounded timer acknowledgement/EOI acceptance"
    }
    $scheduler = [regex]::Match($Text, 'GICv3 scheduler: preemptions=(\d+) context-switches=(\d+) task-A=(\d+) task-B=(\d+) task-C=(\d+)')
    if (!$scheduler.Success) { throw "$Name lacks guest scheduler task progress counters" }
    if ([uint64]$scheduler.Groups[1].Value -lt 1000 -or
        [uint64]$scheduler.Groups[2].Value -eq 0 -or
        [uint64]$scheduler.Groups[3].Value -eq 0 -or
        [uint64]$scheduler.Groups[4].Value -eq 0 -or
        [uint64]$scheduler.Groups[5].Value -eq 0) {
        throw "$Name lacks timer-driven preemption or progress from two scheduler tasks"
    }
    if ($Text -notmatch 'unexpected-irq=0 exceptions=0') { throw "$Name reports an unexpected IRQ or synchronous exception" }
    if ($Text -notmatch 'AARCH64_PHASE4_PASS') { throw "$Name lacks the Phase 4 guest pass marker" }
}

function Invoke-QacManagedBoot {
    param(
        [string]$Name,
        [string]$Project,
        [string]$Machine,
        [string]$Cpu,
        [string]$EspDirectory,
        [string]$VarsPath,
        [string]$GuestLogPath,
        [string]$QacLogPath,
        [string]$PassMarker,
        [int]$RuntimeSeconds
    )
    $qmpPort = Get-FreeQmpPort
    $guestArgs = @(
        '-machine', $Machine, '-cpu', $Cpu, '-m', '512M',
        '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$FirmwareCode",
        '-drive', "if=pflash,format=raw,unit=1,file=$VarsPath",
        '-drive', "file=fat:rw:$EspDirectory,format=raw",
        '-display', 'none', '-monitor', 'none', '-serial', "file:$GuestLogPath",
        '-qmp', "tcp:127.0.0.1:$qmpPort,server=on,wait=off", '-no-reboot'
    )
    $qacArgs = @('run', '--project', $Project, '--mode', 'acceptance', '--', $QemuPath) + $guestArgs
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $QacCliPath
    $startInfo.Arguments = (($qacArgs | ForEach-Object { Quote-ProcessArgument ([string]$_) }) -join ' ')
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $null = $process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()

    $admissionDeadline = [DateTimeOffset]::UtcNow.AddSeconds($AdmissionTimeoutSeconds)
    $reservationId = ''
    $reservationState = ''
    $launched = $false
    while ([DateTimeOffset]::UtcNow -lt $admissionDeadline) {
        if ($process.HasExited) { break }
        $snapshot = & $QacCliPath list 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) {
            $reservationSnapshot = Find-QacRunReservation $snapshot $Project $process.Id
            if ($reservationSnapshot.Success) {
                $reservationId = $reservationSnapshot.Groups[1].Value
                $reservationState = $reservationSnapshot.Groups[2].Value.Trim()
                if ($reservationState -eq 'Running') {
                    $launched = $true
                    break
                }
            }
        }
        Start-Sleep -Milliseconds 500
    }

    if (!$launched -and !$process.HasExited -and $reservationId -and $reservationState -in @('Waiting', 'Blocked')) {
        $cancelText = & $QacCliPath cancel $reservationId 2>&1 | Out-String
        $cancelExit = $LASTEXITCODE
        if ($cancelExit -eq 0) {
            $null = $process.WaitForExit(15000)
        } else {
            $snapshot = & $QacCliPath list 2>&1 | Out-String
            if ($LASTEXITCODE -eq 0) {
                $reservationSnapshot = Find-QacRunReservation $snapshot $Project $process.Id
                if ($reservationSnapshot.Success -and $reservationSnapshot.Groups[2].Value.Trim() -eq 'Running') {
                    $launched = $true
                } elseif (!$process.HasExited) {
                    throw "$Name could not confirm QAC cancellation or QEMU launch. Reservation ${reservationId}: $cancelText"
                }
            } elseif (!$process.HasExited) {
                throw "$Name could not confirm QAC cancellation or reservation state for $reservationId. No unverified QEMU shutdown was attempted."
            }
        }
    }

    if (!$launched -and !$process.HasExited -and $reservationState -in @('Granted', 'Starting')) {
        $launchDeadline = [DateTimeOffset]::UtcNow.AddSeconds(15)
        while ([DateTimeOffset]::UtcNow -lt $launchDeadline -and !$process.HasExited) {
            $snapshot = & $QacCliPath list 2>&1 | Out-String
            if ($LASTEXITCODE -eq 0) {
                $reservationSnapshot = Find-QacRunReservation $snapshot $Project $process.Id
                if ($reservationSnapshot.Success) {
                    $reservationState = $reservationSnapshot.Groups[2].Value.Trim()
                    if ($reservationState -eq 'Running') { $launched = $true; break }
                }
            }
            Start-Sleep -Milliseconds 250
        }
    }

    if (!$launched -and !$process.HasExited) {
        throw "$Name exceeded the bounded $AdmissionTimeoutSeconds-second QAC admission wait. QAC reservation state is $reservationState."
    }

    $timedOut = $false
    $passSeen = $false
    $guestText = ''
    $shutdownNote = ''
    if ($launched) {
        $deadline = [DateTimeOffset]::UtcNow.AddSeconds($RuntimeSeconds)
        while ([DateTimeOffset]::UtcNow -lt $deadline) {
            $guestText = Read-GuestLog $GuestLogPath
            if ($guestText.Contains($PassMarker)) { $passSeen = $true; break }
            if ($guestText -match 'AARCH64_PHASE[234]_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') { break }
            if ($process.HasExited) { break }
            Start-Sleep -Milliseconds 100
        }
        if (!$passSeen -and !$process.HasExited -and [DateTimeOffset]::UtcNow -ge $deadline) { $timedOut = $true }
    }

    if (!$process.HasExited) {
        $shutdownSent = Send-QmpQuit $qmpPort
        if (!$shutdownSent) {
            $shutdownNote = Stop-QacOwnedChildIfQmpFails $process.Id ([IO.Path]::GetFullPath($QemuPath))
        }
        if (!$process.WaitForExit(15000)) {
            throw "${Name}: QAC did not verify QEMU exit after bounded QMP shutdown; reservation remains under QAC recovery."
        }
    }
    $process.WaitForExit()
    $qacText = $stdoutTask.GetAwaiter().GetResult() + $stderrTask.GetAwaiter().GetResult()
    $qacText | Set-Content -LiteralPath $QacLogPath -Encoding utf8
    if ($shutdownNote) { Add-Content -LiteralPath $QacLogPath -Value $shutdownNote -Encoding utf8 }
    $guestText = Read-GuestLog $GuestLogPath
    $exitCode = [int]$process.ExitCode
    $reservationMatch = [regex]::Match($qacText, 'QAC reservation requested: id=([0-9a-f]+)')
    if (!$reservationMatch.Success) {
        $error = [InvalidOperationException]::new("$Name did not receive a QAC reservation. No guest acceptance credit is claimed.")
        $error.Data['QacAdmissionFailed'] = $true
        throw $error
    }
    $reservationId = $reservationMatch.Groups[1].Value
    if ($qacText -notmatch "QAC QEMU launched: reservation=$reservationId" -or
        $qacText -notmatch "QAC reservation released: id=$reservationId") {
        if ($qacText -notmatch "QAC QEMU launched: reservation=$reservationId") {
            $error = [InvalidOperationException]::new("$Name QAC reservation did not launch an authorized QEMU child: $qacText")
            $error.Data['QacAdmissionFailed'] = $true
            throw $error
        }
        throw "$Name QAC did not verify release for reservation $reservationId."
    }
    if ($timedOut) { throw "$Name exceeded the bounded $RuntimeSeconds-second guest deadline." }
    if ($exitCode -ne 0) { throw "$Name QAC-managed QEMU exited with code $exitCode. See $QacLogPath and $GuestLogPath." }
    if (!$passSeen) { throw "$Name exited before its guest pass marker. See $GuestLogPath." }

    return [pscustomobject]@{
        Name = $Name
        Project = $Project
        ReservationId = $reservationId
        GuestLogPath = $GuestLogPath
        QacLogPath = $QacLogPath
        ExitCode = $exitCode
        Text = $guestText
    }
}

function Copy-FreshVars([string]$Name) {
    $path = Join-Path $ArtifactDirectory ("edk2-gicv3-{0}-{1}-vars.fd" -f $runId, $Name)
    Copy-Item -LiteralPath $FirmwareVarsTemplate -Destination $path
    return $path
}

function Save-RunRecord($Run) {
    $sessionRecord.runs += [ordered]@{
        name = $Run.Name
        project = $Run.Project
        reservation_id = $Run.ReservationId
        guest_log = $Run.GuestLogPath
        qac_log = $Run.QacLogPath
        exit_code = $Run.ExitCode
    }
    Save-SessionRecord
}

function Save-SessionRecord {
    $sessionRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $logsDirectory 'session.json') -Encoding utf8
}

function Save-QacPending([string]$Name, [string]$Project, [string]$GuestLogPath, [string]$QacLogPath, [string]$Reason) {
    $qacText = if (Test-Path -LiteralPath $QacLogPath -PathType Leaf) {
        Get-Content -LiteralPath $QacLogPath -Raw
    } else { '' }
    $reservationMatch = [regex]::Match($qacText, 'QAC reservation requested: id=([0-9a-f]+)')
    $sessionRecord.runs += [ordered]@{
        name = $Name
        project = $Project
        reservation_id = if ($reservationMatch.Success) { $reservationMatch.Groups[1].Value } else { $null }
        guest_log = $GuestLogPath
        qac_log = $QacLogPath
        outcome = 'QAC_PENDING_BEFORE_LAUNCH'
    }
    $sessionRecord.outcome = 'GICV3_GUEST_PENDING_QAC'
    $sessionRecord.pending_reason = $Reason
    $sessionRecord.gicv3_boots = $gicv3Runs.Count
    $sessionRecord.gicv2_guest = 'GICV2_GUEST_PENDING_QAC'
    $sessionRecord.qac_status_after = (& $QacCliPath status 2>&1 | Out-String).Trim()
    $sessionRecord.qac_list_after = (& $QacCliPath list 2>&1 | Out-String).Trim()
    Save-SessionRecord
    Write-Host "GICV3_GUEST_PENDING_QAC: $Reason" -ForegroundColor Yellow
    Write-Host "Guest logs: $logsDirectory" -ForegroundColor DarkGray
    exit 2
}

function Save-Gicv3Failure([string]$Stage, [string]$Reason) {
    $sessionRecord.outcome = 'GICV3_GUEST_FAILURE_LOCALIZED'
    $sessionRecord.failure_stage = $Stage
    $sessionRecord.failure_reason = $Reason
    $sessionRecord.gicv3_boots = $gicv3Runs.Count
    $sessionRecord.gicv2_guest = 'NOT_RUN_GICV3_INCOMPLETE'
    $sessionRecord.qac_status_after = (& $QacCliPath status 2>&1 | Out-String).Trim()
    $sessionRecord.qac_list_after = (& $QacCliPath list 2>&1 | Out-String).Trim()
    Save-SessionRecord
}

function Save-Gicv2Pending([string]$GuestLogPath, [string]$QacLogPath, [string]$Reason) {
    $qacText = if (Test-Path -LiteralPath $QacLogPath -PathType Leaf) {
        Get-Content -LiteralPath $QacLogPath -Raw
    } else { '' }
    $reservationMatch = [regex]::Match($qacText, 'QAC reservation requested: id=([0-9a-f]+)')
    $sessionRecord.runs += [ordered]@{
        name = 'GICv2 Phase 2 architecture/timer regression'
        project = 'guideXOSServer-AARCH64-GIC3-2-GICv2'
        reservation_id = if ($reservationMatch.Success) { $reservationMatch.Groups[1].Value } else { $null }
        guest_log = $GuestLogPath
        qac_log = $QacLogPath
        outcome = 'QAC_PENDING_BEFORE_LAUNCH'
    }
    $sessionRecord.gicv2_guest = 'GICV2_GUEST_PENDING_QAC'
    $sessionRecord.gicv2_pending_reason = $Reason
    $sessionRecord.qac_status_after = (& $QacCliPath status 2>&1 | Out-String).Trim()
    $sessionRecord.qac_list_after = (& $QacCliPath list 2>&1 | Out-String).Trim()
    Save-SessionRecord
}

if ($qacPreflightFailure) {
    $sessionRecord.outcome = 'GICV3_GUEST_PENDING_QAC'
    $sessionRecord.pending_reason = $qacPreflightFailure
    $sessionRecord.gicv3_boots = 0
    $sessionRecord.gicv2_guest = 'GICV2_GUEST_PENDING_QAC'
    if (Test-Path -LiteralPath $QacCliPath -PathType Leaf) {
        $sessionRecord.qac_status_after = (& $QacCliPath status 2>&1 | Out-String).Trim()
        $sessionRecord.qac_list_after = (& $QacCliPath list 2>&1 | Out-String).Trim()
    } else {
        $sessionRecord.qac_status_after = 'QAC CLI unavailable'
        $sessionRecord.qac_list_after = 'QAC CLI unavailable'
    }
    Save-SessionRecord
    Write-Host "GICV3_GUEST_PENDING_QAC: $qacPreflightFailure" -ForegroundColor Yellow
    Write-Host "Guest logs: $logsDirectory" -ForegroundColor DarkGray
    exit 2
}

Write-Host "QAC online; $($version.Trim()); virt with gic-version and cortex-a53 are supported." -ForegroundColor Cyan
Write-Host "GICv3 build SHA-256: $($sessionRecord.kernel_sha256)" -ForegroundColor Cyan
$sessionRecord.outcome = 'GICV3_GUEST_IN_PROGRESS'
$sessionRecord.gicv3_boots = 0
$sessionRecord.gicv2_guest = 'NOT_RUN'
Save-SessionRecord

$gicv3Runs = @()
$diagnosticVars = Copy-FreshVars 'diagnostic'
$diagnosticName = 'GICv3 diagnostic boot'
$diagnosticProject = 'guideXOSServer-AARCH64-GIC3-2'
$diagnosticGuestLog = Join-Path $logsDirectory 'diagnostic.log'
$diagnosticQacLog = Join-Path $qacLogsDirectory 'diagnostic.log'
$diagnostic = $null
try {
    $diagnostic = Invoke-QacManagedBoot -Name $diagnosticName `
        -Project $diagnosticProject -Machine 'virt,gic-version=3,acpi=off' -Cpu 'cortex-a53' `
        -EspDirectory $esp -VarsPath $diagnosticVars `
        -GuestLogPath $diagnosticGuestLog -QacLogPath $diagnosticQacLog `
        -PassMarker 'AARCH64_PHASE4_PASS' -RuntimeSeconds $TimeoutSeconds
    Assert-Gicv3GuestLog $diagnostic.Text $diagnosticName
    Save-RunRecord $diagnostic
    $gicv3Runs += $diagnostic
    $sessionRecord.gicv3_boots = $gicv3Runs.Count
    Save-SessionRecord
} catch {
    if ($_.Exception.Data['QacAdmissionFailed']) {
        Save-QacPending $diagnosticName $diagnosticProject $diagnosticGuestLog $diagnosticQacLog $_.Exception.Message
    }
    if ($diagnostic) { Save-RunRecord $diagnostic }
    Save-Gicv3Failure $diagnosticName $_.Exception.Message
    throw
}
Write-Host "GICv3 diagnostic boot: PASS reservation=$($diagnostic.ReservationId)" -ForegroundColor Green

for ($run = 1; $run -le 3; ++$run) {
    $varsPath = Copy-FreshVars ("boot-{0}" -f $run)
    $runName = "fresh GICv3 boot {0}/3" -f $run
    $runGuestLog = Join-Path $logsDirectory ("boot-{0}.log" -f $run)
    $runQacLog = Join-Path $qacLogsDirectory ("boot-{0}.log" -f $run)
    $result = $null
    try {
        $result = Invoke-QacManagedBoot -Name $runName `
            -Project 'guideXOSServer-AARCH64-GIC3-2' -Machine 'virt,gic-version=3,acpi=off' -Cpu 'cortex-a53' `
            -EspDirectory $esp -VarsPath $varsPath `
            -GuestLogPath $runGuestLog -QacLogPath $runQacLog `
            -PassMarker 'AARCH64_PHASE4_PASS' -RuntimeSeconds $TimeoutSeconds
        Assert-Gicv3GuestLog $result.Text $runName
        Save-RunRecord $result
        $gicv3Runs += $result
        $sessionRecord.gicv3_boots = $gicv3Runs.Count
        Save-SessionRecord
    } catch {
        if ($_.Exception.Data['QacAdmissionFailed']) {
            Save-QacPending $runName 'guideXOSServer-AARCH64-GIC3-2' $runGuestLog $runQacLog $_.Exception.Message
        }
        if ($result) { Save-RunRecord $result }
        Save-Gicv3Failure $runName $_.Exception.Message
        throw
    }
    Write-Host "Fresh GICv3 boot $run/3: PASS reservation=$($result.ReservationId)" -ForegroundColor Green
}

Write-Host 'Building the preserved GICv2 Phase 2 regression candidate...' -ForegroundColor Cyan
$phase2Directory = Join-Path $ArtifactDirectory ("gicv2-regression-{0}" -f $runId)
& (Join-Path $PSScriptRoot 'build-aarch64-phase2.ps1') -OutputDirectory $phase2Directory
if ($LASTEXITCODE -ne 0) { throw 'AArch64 GICv2 Phase 2 build failed' }
$phase2Esp = Join-Path $phase2Directory 'esp'
foreach ($required in @('EFI\BOOT\BOOTAA64.EFI', 'kernel.elf')) {
    if (!(Test-Path -LiteralPath (Join-Path $phase2Esp $required) -PathType Leaf)) {
        throw "GICv2 Phase 2 ESP is missing $required"
    }
}
$gicv2Vars = Join-Path $ArtifactDirectory ("edk2-gicv2-{0}-vars.fd" -f $runId)
Copy-Item -LiteralPath $FirmwareVarsTemplate -Destination $gicv2Vars
$gicv2GuestLog = Join-Path $logsDirectory 'gicv2-regression.log'
$gicv2QacLog = Join-Path $qacLogsDirectory 'gicv2-regression.log'
$gicv2Result = $null
$gicv2Pending = $false
try {
    $gicv2Result = Invoke-QacManagedBoot -Name 'GICv2 Phase 2 architecture/timer regression' `
        -Project 'guideXOSServer-AARCH64-GIC3-2-GICv2' -Machine 'virt,gic-version=2,acpi=off' -Cpu 'cortex-a53' `
        -EspDirectory $phase2Esp -VarsPath $gicv2Vars `
        -GuestLogPath $gicv2GuestLog `
        -QacLogPath $gicv2QacLog `
        -PassMarker 'AARCH64_PHASE2_PASS' -RuntimeSeconds 30
    $gicv2Text = $gicv2Result.Text
    if ($gicv2Text -match 'AARCH64_PHASE2_ERROR|\[guideXOS\].*(FAIL|FATAL|ERROR)|\[A64 UEFI\] ERROR') {
        throw 'GICv2 regression guest emitted a fatal/error marker'
    }
    foreach ($marker in @(
        '[guideXOS] GIC: OK version=2',
        '[guideXOS] GICv2 path: GICD/GICC MMIO active; GICR/ICC EL1 paths skipped',
        '[guideXOS] timer IRQ: PASS count=1 (returned from IRQ)',
        'AARCH64_PHASE2_PASS'
    )) {
        if (!$gicv2Text.Contains($marker)) { throw "GICv2 regression is missing marker: $marker" }
    }
    if ($gicv2Text -match 'GIC: OK version=3|GICv3 ICC:|GICv3 redistributor:') {
        throw 'GICv2 regression unexpectedly entered GICv3 Redistributor or ICC diagnostics'
    }
    Save-RunRecord $gicv2Result
    Write-Host "GICv2 architecture/timer regression: PASS reservation=$($gicv2Result.ReservationId)" -ForegroundColor Green
} catch {
    if ($_.Exception.Data['QacAdmissionFailed']) {
        $gicv2Pending = $true
        Save-Gicv2Pending $gicv2GuestLog $gicv2QacLog $_.Exception.Message
        Write-Host "GICV2_GUEST_PENDING_QAC: $($_.Exception.Message)" -ForegroundColor Yellow
    } else {
        throw
    }
}

$finalStatus = & $QacCliPath status 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "Unable to capture final QAC status.`n$finalStatus" }
$sessionRecord.qac_status_after = $finalStatus.Trim()
$sessionRecord.qac_list_after = (& $QacCliPath list 2>&1 | Out-String).Trim()
$sessionRecord.gicv3_boots = $gicv3Runs.Count
$sessionRecord.gicv2_guest = if ($gicv2Pending) { 'GICV2_GUEST_PENDING_QAC' } else { 'PASS' }
$sessionRecord.outcome = if ($gicv2Pending) {
    'GICV3_GUEST_VERIFIED_GICV2_PENDING_QAC'
} else { 'GICV3_GUEST_VERIFIED' }
Save-SessionRecord

Write-Host 'AARCH64_GICV3_QEMU_PASS: diagnostic plus three fresh QAC-authorized Cortex-A53 GICv3 boots passed timer, EOI, scheduler, and exception-return acceptance.' -ForegroundColor Green
if ($gicv2Pending) {
    Write-Host 'GICV2_GUEST_PENDING_QAC' -ForegroundColor Yellow
} else {
    Write-Host 'AARCH64_GICV2_QEMU_REGRESSION_PASS' -ForegroundColor Green
}
Write-Host "Guest logs: $logsDirectory" -ForegroundColor DarkGray
exit 0
