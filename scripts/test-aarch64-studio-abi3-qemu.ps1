[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ArtifactDirectory,
    [string]$QacCliPath = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [int]$AdmissionTimeoutSeconds = 1800,
    [int]$RuntimeSeconds = 300,
    [int]$PostStudioExitSeconds = 20,
    [switch]$R2B1ExternalWorkflow
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
$artifactPrefix = 'E:\CodexArtifacts\'
if (!$ArtifactDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase) -and
    !$ArtifactDirectory.StartsWith($artifactPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to run the Studio guest outside repository out/ or E:\CodexArtifacts: $ArtifactDirectory"
}
foreach ($path in @($QemuPath, $FirmwareCode, $FirmwareVarsTemplate)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required guest input is missing: $path" }
}
$qemuSha256 = (Get-FileHash -LiteralPath $QemuPath -Algorithm SHA256).Hash.ToLowerInvariant()
$firmwareCodeSha256 = (Get-FileHash -LiteralPath $FirmwareCode -Algorithm SHA256).Hash.ToLowerInvariant()
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
$candidateKernelPath = Join-Path $esp 'kernel.elf'
$candidateEfiPath = Join-Path $esp 'EFI\BOOT\BOOTAA64.EFI'
$candidateRamdiskPath = Join-Path $esp 'ramdisk.img'
$candidateManifestPath = Join-Path $ArtifactDirectory 'candidate-manifest.json'
$candidateSourceHead = (& git -C $repoRoot rev-parse HEAD 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "Unable to record candidate source HEAD: $candidateSourceHead" }
$candidateKernelSha256 = (Get-FileHash -LiteralPath $candidateKernelPath -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateEfiSha256 = (Get-FileHash -LiteralPath $candidateEfiPath -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateRamdiskSha256 = (Get-FileHash -LiteralPath $candidateRamdiskPath -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateManifestSha256 = if (Test-Path -LiteralPath $candidateManifestPath -PathType Leaf) {
    (Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
} else { '' }
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$logDirectory = Join-Path $ArtifactDirectory ("abi3-guest-{0}" -f $runId)
$null = New-Item -ItemType Directory -Path $logDirectory -Force
$qacLogPath = Join-Path $logDirectory 'qac.log'
$qacStdoutPath = Join-Path $logDirectory 'qac.stdout.log'
$qacStderrPath = Join-Path $logDirectory 'qac.stderr.log'
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

function Wait-GuestMarker([string]$Marker, [int]$TimeoutSeconds = 5) {
    $markerDeadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTimeOffset]::UtcNow -lt $markerDeadline) {
        if ((Read-GuestLog).Contains($Marker)) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return (Read-GuestLog).Contains($Marker)
}

function Invoke-StudioInputHelper([hashtable]$Arguments) {
    $helperPath = Join-Path $PSScriptRoot 'send-aarch64-qmp-studio-input.ps1'
    try {
        $helperOutput = & $helperPath @Arguments 2>&1 | Out-String
        if (![string]::IsNullOrWhiteSpace($helperOutput)) {
            Add-Content -LiteralPath (Join-Path $logDirectory 'post-close-input-helper.log') `
                -Value $helperOutput -Encoding utf8
        }
        return $true
    } catch {
        Add-Content -LiteralPath (Join-Path $logDirectory 'post-close-input-helper.log') `
            -Value $_.Exception.Message -Encoding utf8
        return $false
    }
}

function Save-SessionRecord {
    if ($null -ne $record) {
        $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sessionPath -Encoding utf8
    }
}

function Save-QacLogSnapshot {
    if ($qac -and $qac.HasExited) {
        $stdout = if (Test-Path -LiteralPath $qacStdoutPath) { Get-Content -LiteralPath $qacStdoutPath -Raw } else { '' }
        $stderr = if (Test-Path -LiteralPath $qacStderrPath) { Get-Content -LiteralPath $qacStderrPath -Raw } else { '' }
        @("--- stdout ---", $stdout, "--- stderr ---", $stderr) |
            Set-Content -LiteralPath $qacLogPath -Encoding utf8
        return $true
    }
    return $false
}

function Save-RawQacHealthSnapshot {
    try {
        $status = & $QacCliPath status 2>&1 | Out-String
        $statusExit = $LASTEXITCODE
        $list = & $QacCliPath list 2>&1 | Out-String
        $listExit = $LASTEXITCODE
        $record.qac_status_final_snapshot = $status.Trim()
        $record.qac_status_final_exit_code = $statusExit
        $record.qac_list_final_snapshot = $list.Trim()
        $record.qac_list_final_exit_code = $listExit
        $record.qac_final_snapshot_available = ($statusExit -eq 0 -and $listExit -eq 0)
    } catch {
        $record.qac_final_snapshot_available = $false
        $record.qac_final_snapshot_failure = $_.Exception.Message
    }
}

function Get-StudioObservedKeyText([string]$GuestText) {
    $builder = [Text.StringBuilder]::new()
    $events = [regex]::Matches($GuestText, 'ABI3_STUDIO_KEY_EVENT sequence=(\d+) key=(\d+) action=(down|up) modifiers=(\d+)')
    $digitSymbols = ')!@#$%^&*('
    foreach ($event in $events) {
        if ($event.Groups[3].Value -ne 'down') { continue }
        $key = [int]$event.Groups[2].Value
        $shift = (([int]$event.Groups[4].Value -band 1) -ne 0)
        $character = [char]0
        if ($key -ge 65 -and $key -le 90) {
            $character = [char]$(if ($shift) { $key } else { $key + 32 })
        } elseif ($key -ge 48 -and $key -le 57) {
            $character = if ($shift) { $digitSymbols[$key - 48] } else { [char]$key }
        } else {
            switch ($key) {
                32 { $character = ' ' }
                186 { $character = if ($shift) { ':' } else { ';' } }
                187 { $character = if ($shift) { '+' } else { '=' } }
                188 { $character = if ($shift) { '<' } else { ',' } }
                189 { $character = if ($shift) { '_' } else { '-' } }
                190 { $character = if ($shift) { '>' } else { '.' } }
                191 { $character = if ($shift) { '?' } else { '/' } }
                192 { $character = if ($shift) { '~' } else { '`' } }
                219 { $character = if ($shift) { '{' } else { '[' } }
                220 { $character = if ($shift) { '|' } else { '\' } }
                221 { $character = if ($shift) { '}' } else { ']' } }
                222 { $character = if ($shift) { '"' } else { "'" } }
            }
        }
        if ($character -ne [char]0) { [void]$builder.Append($character) }
    }
    return [pscustomobject]@{
        Events = $events
        Count = $events.Count
        KeyDownCount = @($events | Where-Object { $_.Groups[3].Value -eq 'down' }).Count
        KeyUpCount = @($events | Where-Object { $_.Groups[3].Value -eq 'up' }).Count
        Text = $builder.ToString()
    }
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

function Capture-StudioScreen([string]$OutputPath = $screenshotPath) {
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
            $null = Send-Qmp $client $reader $writer 'screendump' @{ filename = $OutputPath; format = 'png' }
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
    $displayMatches = [regex]::IsMatch($commandLine, '(?:^|\s)"?-device"?\s+"?ramfb"?(?:\s|$)')
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
$qac = Start-Process -FilePath $startInfo.FileName -ArgumentList $startInfo.Arguments `
    -RedirectStandardOutput $qacStdoutPath -RedirectStandardError $qacStderrPath `
    -PassThru -WindowStyle Hidden
$reservationId = ''
$running = $false
$reservationState = ''
$ownedChild = $null
$ownershipFailure = ''
$launchProbeFailure = ''
$record = [ordered]@{
    outcome = 'ABI3_GUEST_ADMISSION_PENDING'; project = 'guideXOSServer-AARCH64-ABI3-Studio'
    reservation_id = ''; reservation_state = ''; qemu_launched = $false
    qmp_port = $qmpPort; guest_log = $guestLogPath; qac_log = $qacLogPath
    qac_stdout_log = $qacStdoutPath; qac_stderr_log = $qacStderrPath
    qac_process_id = $qac.Id; qac_process_still_active = $true
    qac_status_before = $qacBefore.Status; qac_list_before = $qacBefore.List
    qemu_process_ids_before = $baselineQemu; qemu_sha256 = $qemuSha256
    candidate_source_head = $candidateSourceHead
    candidate_manifest = if ($candidateManifestSha256) { $candidateManifestPath } else { $null }
    candidate_manifest_sha256 = $candidateManifestSha256
    candidate_kernel_sha256 = $candidateKernelSha256
    candidate_efi_sha256 = $candidateEfiSha256
    candidate_ramdisk_sha256 = $candidateRamdiskSha256
    firmware_code = [IO.Path]::GetFullPath($FirmwareCode); firmware_code_sha256 = $firmwareCodeSha256
    firmware_vars_initial_sha256 = $initialVarsSha256; firmware_vars_template_sha256 = $varsTemplateSha256
    qac_final_log_available = $false; qac_final_snapshot_available = $false
}
Save-SessionRecord
$admissionDeadline = [DateTimeOffset]::UtcNow.AddSeconds($AdmissionTimeoutSeconds)
while ([DateTimeOffset]::UtcNow -lt $admissionDeadline -and !$qac.HasExited) {
    $status = & $QacCliPath list 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        $match = [regex]::Match($status, '(?m)^\s*([0-9a-f]{32}) \| guideXOSServer-AARCH64-ABI3-Studio \| acceptance \| ([^|]+) \| queue \d+ \| [^\r\n]*\(PID ' + $qac.Id + '\) \| owner ')
        if ($match.Success) {
            $reservationId = $match.Groups[1].Value
            $reservationState = $match.Groups[2].Value.Trim()
            $record.reservation_id = $reservationId
            $record.reservation_state = $reservationState
            $record.reservation_identity_checkpoint_utc = [DateTimeOffset]::UtcNow.ToString('o')
            $record.qemu_launched = $reservationState -eq 'Running'
            Save-SessionRecord
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
    if ($ownershipFailure -and !$qac.WaitForExit(15000)) {
        $record.outcome = 'ABI3_GUEST_OWNERSHIP_UNVERIFIED'
        $record.qemu_launched = $reservationState -eq 'Running'
        $record.ownership_failure = $ownershipFailure
        $record.qac_process_still_active = $true
        $record.managed_qemu_child_pid = $null
        $record.final_evidence_status = 'QAC remains active; no child exit or reservation release evidence is available yet.'
        Save-RawQacHealthSnapshot
        Save-SessionRecord
        throw "Studio failed closed on QAC/QEMU ownership verification; QAC remains active and retains control. Do not start another guest until it exits. $ownershipFailure"
    }
    if (!$qac.HasExited) {
        $record.outcome = 'ABI3_GUEST_QAC_ACTIVE_UNVERIFIED'
        $record.qac_process_still_active = $true
        $record.final_evidence_status = 'QAC remains active; live stdout/stderr and reservation checkpoint are preserved, but final child-exit/release evidence is unavailable.'
        Save-RawQacHealthSnapshot
        Save-SessionRecord
        throw 'QAC did not confirm cancellation/exit of the reservation; its process remains responsible for the guest.'
    }
    $null = Save-QacLogSnapshot
    $qacText = (Get-Content -LiteralPath $qacStdoutPath -Raw -ErrorAction SilentlyContinue) +
               (Get-Content -LiteralPath $qacStderrPath -Raw -ErrorAction SilentlyContinue)
    $record.qac_process_still_active = $false
    $record.qac_process_exit_code = $qac.ExitCode
    $record.qac_final_log_available = $true
    $earlyGuestText = Read-GuestLog
    $earlyGuestText | Set-Content -LiteralPath (Join-Path $logDirectory 'guest-final.log') -Encoding utf8
    Save-RawQacHealthSnapshot
    Save-SessionRecord
    $qacAfter = Get-QacHealth 'Studio failed-admission cleanup'
    $remainingQemu = Get-QemuProcesses
    $guestLog = Read-GuestLog
    $guestLog | Set-Content -LiteralPath (Join-Path $logDirectory 'guest-final.log') -Encoding utf8
    $record = [ordered]@{
        outcome = if ($ownershipFailure) { 'ABI3_GUEST_OWNERSHIP_FAILURE' } else { 'ABI3_GUEST_PENDING_QAC' }
        reservation_id = $reservationId; reservation_state = $reservationState
        qemu_launched = $qacText -match "QAC QEMU launched: reservation=$reservationId"
        qmp_port = $qmpPort; guest_log = $guestLogPath; qac_log = $qacLogPath
        ownership_failure = $ownershipFailure; qac_status_after = $qacAfter.Status; qac_list_after = $qacAfter.List
        qac_status_before = $qacBefore.Status; qac_list_before = $qacBefore.List
        qemu_process_ids_before = $baselineQemu; qemu_process_ids_after = @($remainingQemu | ForEach-Object { [int]$_.ProcessId })
        qemu_sha256 = $qemuSha256; firmware_code = [IO.Path]::GetFullPath($FirmwareCode)
        firmware_code_sha256 = $firmwareCodeSha256
        firmware_vars_initial_sha256 = $initialVarsSha256; firmware_vars_template_sha256 = $varsTemplateSha256
        qac_stdout_log = $qacStdoutPath; qac_stderr_log = $qacStderrPath
        qac_final_log_available = $true
        candidate_source_head = $candidateSourceHead
        candidate_manifest = if ($candidateManifestSha256) { $candidateManifestPath } else { $null }
        candidate_manifest_sha256 = $candidateManifestSha256
        candidate_kernel_sha256 = $candidateKernelSha256
        candidate_efi_sha256 = $candidateEfiSha256
        candidate_ramdisk_sha256 = $candidateRamdiskSha256
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
    if ($ownershipFailure) {
        throw "Studio failed closed on QAC/QEMU ownership verification; the ended run and child logs were preserved. $ownershipFailure"
    }
    Write-Host "ABI3_GUEST_PENDING_QAC: request not granted within $AdmissionTimeoutSeconds seconds; no QEMU was launched." -ForegroundColor Yellow
    Write-Host "Session record: $sessionPath"
    exit 2
}

$record = [ordered]@{
    outcome = 'ABI3_STUDIO_GUEST_IN_PROGRESS'; project = 'guideXOSServer-AARCH64-ABI3-Studio'
    reservation_id = $reservationId; qmp_port = $qmpPort; machine = 'virt,gic-version=2,acpi=off'
    cpu = 'cortex-a53'; display = 'ramfb with EDK2-selected GOP mode'; qemu = [IO.Path]::GetFullPath($QemuPath); guest_log = $guestLogPath
    qac_log = $qacLogPath; screenshot = $screenshotPath; qemu_launched = $true
    studio_window_screenshot = $false; initial_frame_screendump_captured = $false
    initial_frame_visual_status = 'not-reviewed'; studio_close_and_cleanup = $false; runtime_deadline_seconds = $RuntimeSeconds
    desktop_after_close_screenshot = Join-Path $logDirectory 'desktop-after-close.png'
    desktop_after_close_screendump_captured = $false
    desktop_after_close_500ms_screenshot = Join-Path $logDirectory 'desktop-after-close-500ms.png'
    desktop_after_close_500ms_screendump_captured = $false
    desktop_after_close_pointer_response_screenshot = Join-Path $logDirectory 'desktop-after-close-pointer-response.png'
    desktop_after_close_keyboard_response_screenshot = Join-Path $logDirectory 'desktop-after-close-keyboard-response.png'
    post_close_input_attempted = $false
    post_studio_pointer_responsive = $false
    post_studio_keyboard_responsive = $false
    post_close_input_helper_log = Join-Path $logDirectory 'post-close-input-helper.log'
    editable_control_evidence = Join-Path $logDirectory 'editable-control-evidence.json'
    editable_control_capture_valid = $false
    editable_control_visual_status = 'not-used'
    editable_control_text = ''
    editable_context_established = $false
    post_studio_exit_seconds = $PostStudioExitSeconds; desktop_post_exit_wait_completed = $false
    qac_process_id = $qac.Id; managed_qemu_child_pid = [int]$ownedChild.ProcessId
    managed_qemu_child_parent_pid = [int]$ownedChild.ParentProcessId
    candidate_source_head = $candidateSourceHead
    candidate_manifest = if ($candidateManifestSha256) { $candidateManifestPath } else { $null }
    candidate_manifest_sha256 = $candidateManifestSha256
    candidate_kernel_sha256 = $candidateKernelSha256
    candidate_efi_sha256 = $candidateEfiSha256
    candidate_ramdisk_sha256 = $candidateRamdiskSha256
    qac_status_before = $qacBefore.Status; qac_list_before = $qacBefore.List
    qemu_process_ids_before = $baselineQemu
    qemu_sha256 = $qemuSha256
    firmware_code = [IO.Path]::GetFullPath($FirmwareCode); firmware_code_sha256 = $firmwareCodeSha256
    firmware_vars_initial_sha256 = $initialVarsSha256
    firmware_vars_template_sha256 = $varsTemplateSha256
}
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
Write-Host "QAC granted reservation $reservationId and launched QEMU PID $($qac.Id)." -ForegroundColor Cyan
Write-Host "QMP port: $qmpPort; interact using the recorded port while this script holds the acceptance reservation."

$scriptExitCode = 0
try {
$deadline = [DateTimeOffset]::UtcNow.AddSeconds($RuntimeSeconds)
$screenshotTaken = $false
$lastGuest = ''
$runtimeOwnershipFailure = ''
$nextOwnershipCheck = [DateTimeOffset]::UtcNow
$phase12PassObservedAt = $null
$postStudioExitObservedAt = $null
$postStudioExitDeadline = $null
$postStudioExitWaitCompleted = $false
$postCloseCaptureDone = $false
$postCloseResampleDone = $false
while ([DateTimeOffset]::UtcNow -lt $deadline -or
       ($null -ne $postStudioExitObservedAt -and $null -ne $postStudioExitDeadline -and
        [DateTimeOffset]::UtcNow -lt $postStudioExitDeadline)) {
    $lastGuest = Read-GuestLog
    if (!$screenshotTaken -and $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER initial_render=PASS')) {
        $screenshotTaken = Capture-StudioScreen
        if ($screenshotTaken -and (Test-Path -LiteralPath $screenshotPath -PathType Leaf)) {
            # A serial marker plus a QMP screendump does not establish that
            # the Studio surface is visible. Leave visual acceptance for a
            # human review of the preserved PNG.
            $record.initial_frame_screendump_captured = $true
            $record.initial_frame_visual_status = 'not-reviewed'
            $record.screenshot_sha256 = (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
            $record.screenshot_bytes = (Get-Item -LiteralPath $screenshotPath).Length
            $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
            Write-Host "Initial guest framebuffer captured; visual status requires review: $screenshotPath" -ForegroundColor Green
        }
    }
    if ($lastGuest.Contains('AARCH64_PHASE12_PASS')) {
        $phase12PassCount = [regex]::Matches($lastGuest, 'AARCH64_PHASE12_PASS').Count
        if ($null -eq $phase12PassObservedAt) {
            $phase12PassObservedAt = [DateTimeOffset]::UtcNow
            Write-Host 'Studio cleanup passed; capturing the desktop before post-close input.' -ForegroundColor Cyan
        }
        if (!$postCloseCaptureDone) {
            $postCloseCaptureDone = Capture-StudioScreen ([string]$record.desktop_after_close_screenshot)
            $record.desktop_after_close_screendump_captured = $postCloseCaptureDone -and
                (Test-Path -LiteralPath $record.desktop_after_close_screenshot -PathType Leaf)
            if ($record.desktop_after_close_screendump_captured) {
                $record.desktop_after_close_screenshot_sha256 = (Get-FileHash -LiteralPath $record.desktop_after_close_screenshot -Algorithm SHA256).Hash.ToLowerInvariant()
                $record.desktop_after_close_capture_utc = [DateTimeOffset]::UtcNow.ToString('o')
            }
            Save-SessionRecord
        }
        if ($phase12PassCount -ge 2 -and $null -eq $postStudioExitObservedAt) {
            $postStudioExitObservedAt = [DateTimeOffset]::UtcNow
            $postStudioExitDeadline = $postStudioExitObservedAt.AddSeconds($PostStudioExitSeconds)
            Write-Host "Phase 12 completed; keeping the desktop input loop alive for $PostStudioExitSeconds seconds of post-exit observation." -ForegroundColor Cyan
        }
        if (!$postCloseResampleDone -and [DateTimeOffset]::UtcNow -ge $phase12PassObservedAt.AddMilliseconds(500)) {
            $postCloseResampleDone = Capture-StudioScreen ([string]$record.desktop_after_close_500ms_screenshot)
            $record.desktop_after_close_500ms_screendump_captured = $postCloseResampleDone -and
                (Test-Path -LiteralPath $record.desktop_after_close_500ms_screenshot -PathType Leaf)
            if ($record.desktop_after_close_500ms_screendump_captured) {
                $record.desktop_after_close_500ms_screenshot_sha256 = (Get-FileHash -LiteralPath $record.desktop_after_close_500ms_screenshot -Algorithm SHA256).Hash.ToLowerInvariant()
                $record.desktop_after_close_500ms_capture_utc = [DateTimeOffset]::UtcNow.ToString('o')
            }
            Save-SessionRecord
        }
        if (!$record.post_close_input_attempted -and $postCloseCaptureDone -and
            $postCloseResampleDone -and
            $phase12PassCount -ge 2 -and
            $lastGuest.Contains('[guideXOS] Studio post-close input probe: armed')) {
            $record.post_close_input_attempted = $true
            Save-SessionRecord

            $pointerPath = [string]$record.desktop_after_close_pointer_response_screenshot
            $pointerSent = Invoke-StudioInputHelper @{
                SessionPath = $sessionPath; QacCliPath = $QacCliPath
                ClickX = 52; ClickY = 580; CapturePath = $pointerPath
            }
            $pointerMarker = $pointerSent -and
                (Wait-GuestMarker '[guideXOS] Studio post-close pointer input: PASS start-button')
            if ($pointerMarker) {
                $null = Capture-StudioScreen $pointerPath
                $record.desktop_after_close_pointer_response_sha256 =
                    (Get-FileHash -LiteralPath $pointerPath -Algorithm SHA256).Hash.ToLowerInvariant()
                $record.post_studio_pointer_responsive = $true
            }

            $keyboardPath = [string]$record.desktop_after_close_keyboard_response_screenshot
            $keyboardSent = $pointerMarker -and (Invoke-StudioInputHelper @{
                SessionPath = $sessionPath; QacCliPath = $QacCliPath
                SpecialKeys = @('Super'); CapturePath = $keyboardPath
            })
            $keyboardMarker = $keyboardSent -and
                (Wait-GuestMarker '[guideXOS] Studio post-close keyboard input: PASS start-menu-toggle')
            if ($keyboardMarker) {
                $null = Capture-StudioScreen $keyboardPath
                $record.desktop_after_close_keyboard_response_sha256 =
                    (Get-FileHash -LiteralPath $keyboardPath -Algorithm SHA256).Hash.ToLowerInvariant()
                $record.post_studio_keyboard_responsive = $true
            }
            $record.post_close_input_failure = if (!$pointerSent) {
                'QMP desktop pointer injection failed under the active QAC reservation.'
            } elseif (!$pointerMarker) {
                'Guest desktop did not process the post-Studio Start-button click.'
            } elseif (!$keyboardSent) {
                'QMP desktop keyboard injection failed under the active QAC reservation.'
            } elseif (!$keyboardMarker) {
                'Guest desktop did not process the post-Studio Super-key action.'
            } else { '' }
            Save-SessionRecord
        }
        if ($null -ne $postStudioExitDeadline -and
            [DateTimeOffset]::UtcNow -ge $postStudioExitDeadline) {
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

$timedOut = [DateTimeOffset]::UtcNow -ge $deadline -and $null -eq $postStudioExitObservedAt
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
    if (!$quitSent) {
        $ownedChildStillRunning = @(Get-QemuProcesses | Where-Object { [int]$_.ProcessId -eq [int]$ownedChild.ProcessId })
        if ($ownedChildStillRunning.Count -eq 0 -and $qac.WaitForExit(10000)) {
            # The managed child exited while the bounded QMP retry was in
            # progress. Let QAC confirm its exit and reservation release below.
        } else {
            $shutdownFailure = 'QMP shutdown was not confirmed and the managed QAC run did not finish cleanly.'
            $record.outcome = 'ABI3_STUDIO_GUEST_SHUTDOWN_UNVERIFIED'
            $record.qmp_shutdown_failure = $shutdownFailure
            $record.qac_process_alive = !$qac.HasExited
            $record.managed_child_alive = $ownedChildStillRunning.Count -ne 0
            $lastGuest = Read-GuestLog
            $lastGuest | Set-Content -LiteralPath (Join-Path $logDirectory 'guest-final.log') -Encoding utf8
            $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding utf8
            throw "$shutdownFailure QAC remains responsible for the exact guest child and reservation. See $sessionPath"
        }
    }
    if (!$qac.WaitForExit(15000)) { throw 'QAC did not verify QEMU exit after QMP shutdown.' }
}
$qac.WaitForExit()
$remainingQemu = Get-QemuProcesses
if (@($remainingQemu | Where-Object { [int]$_.ProcessId -eq [int]$ownedChild.ProcessId }).Count -ne 0) {
    throw "QAC reported exit but managed QEMU child PID $($ownedChild.ProcessId) remains."
}
$null = Save-QacLogSnapshot
$qacText = (Get-Content -LiteralPath $qacStdoutPath -Raw -ErrorAction SilentlyContinue) +
           (Get-Content -LiteralPath $qacStderrPath -Raw -ErrorAction SilentlyContinue)
$lastGuest = Read-GuestLog
$record.guest_pass_marker = [regex]::Matches($lastGuest, 'AARCH64_PHASE12_PASS').Count -ge 2
$record.studio_entry_called = $lastGuest.Contains('P28Z APP 00 gx_main_entry_raw')
$record.adapter_selected = $lastGuest.Contains('ABI adapter selected: guidexos-c-abi-v1-studio-v05-464')
$record.window_created = $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER main_window_creation=PASS')
$record.initial_render = $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER initial_render=PASS')
$record.mouse_event_delivered = $lastGuest.Contains('ABI3 guest observed Studio mouse event through v0.5 poll_event')
$record.key_event_delivered = $lastGuest.Contains('ABI3 guest observed Studio key event through v0.5 poll_event')
$record.focus_event_delivered = $lastGuest.Contains('ABI3 guest observed Studio focus event through v0.5 poll_event')
$record.close_event_delivered = $lastGuest.Contains('ABI3 guest observed Studio close event through v0.5 poll_event')
$record.close_and_cleanup = $lastGuest.Contains('NativeElf GUI cleanup: PASS') -or
    ([regex]::IsMatch($lastGuest, 'NativeElf GUI cleanup:[\s\S]{0,512}PASS mappings=0 windows=0 widgets=0') -and
     $lastGuest.Contains('[guideXOS] ABI3 Developer Studio runtime cleanup: PASS'))
$record.exposed_desktop_redraw = $lastGuest.Contains('NativeElf GUI cleanup: exposed desktop redraw=PASS')
$record.post_studio_pointer_responsive = $record.post_studio_pointer_responsive -and
    $lastGuest.Contains('[guideXOS] Studio post-close pointer input: PASS start-button') -and
    (Test-Path -LiteralPath $record.desktop_after_close_pointer_response_screenshot -PathType Leaf)
$record.post_studio_keyboard_responsive = $record.post_studio_keyboard_responsive -and
    $lastGuest.Contains('[guideXOS] Studio post-close keyboard input: PASS start-menu-toggle') -and
    (Test-Path -LiteralPath $record.desktop_after_close_keyboard_response_screenshot -PathType Leaf)
$record.project_create_succeeded = $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER project_create=PASS')
$record.project_open_succeeded = $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER project_open=PASS')
$record.project_context_established = $record.project_create_succeeded -or $record.project_open_succeeded
$record.document_open_succeeded = $lastGuest.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER document_open=PASS')
$keyEvidence = Get-StudioObservedKeyText $lastGuest
$record.studio_key_event_count = $keyEvidence.Count
$record.studio_key_down_count = $keyEvidence.KeyDownCount
$record.studio_key_up_count = $keyEvidence.KeyUpCount
$record.studio_observed_key_text = $keyEvidence.Text
$record.studio_keydown_keyup_balanced = $keyEvidence.KeyDownCount -eq $keyEvidence.KeyUpCount
$record.expected_test_text_occurrences = [regex]::Matches($keyEvidence.Text, 'guideXOS ARM64').Count
$record.expected_test_text_observed = $record.expected_test_text_occurrences -eq 1
$controlEvidencePath = [string]$record.editable_control_evidence
if (Test-Path -LiteralPath $controlEvidencePath -PathType Leaf) {
    try {
        $controlEvidence = Get-Content -LiteralPath $controlEvidencePath -Raw | ConvertFrom-Json
        $controlCapturePath = [IO.Path]::GetFullPath([string]$controlEvidence.screenshot)
        $expectedEvidencePath = [IO.Path]::GetFullPath((Join-Path $logDirectory 'editable-control.png'))
        $controlCaptureValid = [string]$controlEvidence.control_kind -eq 'project_open_path' -and
            [string]$controlEvidence.typed_text -ceq 'guideXOS ARM64' -and
            [string]::Equals($controlCapturePath, $expectedEvidencePath, [StringComparison]::OrdinalIgnoreCase) -and
            (Test-Path -LiteralPath $controlCapturePath -PathType Leaf) -and
            (Get-FileHash -LiteralPath $controlCapturePath -Algorithm SHA256).Hash.ToLowerInvariant() -eq
                [string]$controlEvidence.screenshot_sha256
        $record.editable_control_capture_valid = $controlCaptureValid
        $record.editable_control_visual_status = [string]$controlEvidence.visual_status
        $record.editable_control_text = [string]$controlEvidence.typed_text
        $record.editable_context_established = $controlCaptureValid -and
            $record.editable_control_visual_status -eq 'visible-in-studio-control' -and
            $record.expected_test_text_observed -and $record.studio_keydown_keyup_balanced
    } catch {
        $record.editable_control_evidence_error = $_.Exception.Message
    }
}
if ($record.project_context_established -and $record.document_open_succeeded) {
    $record.editable_context_established = $true
}
$record.qac_launched_child = $qacText -match "QAC QEMU launched: reservation=$reservationId"
$record.qac_released_reservation = $qacText -match "QAC reservation released: id=$reservationId"
$qacExitMatch = [regex]::Match($qacText, "QAC QEMU exited: reservation=$([regex]::Escape($reservationId)) pid=\d+ exitCode=(-?\d+)")
$record.qemu_exit_code = if ($qacExitMatch.Success) { [int]$qacExitMatch.Groups[1].Value } else { $qac.ExitCode }
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
$record.post_studio_exit_observed_seconds = if ($null -ne $postStudioExitObservedAt) {
    [Math]::Max(0, [int]([DateTimeOffset]::UtcNow - $postStudioExitObservedAt).TotalSeconds)
} else { 0 }
$r2b1WorkflowPass = $false
if ($R2B1ExternalWorkflow) {
    $r2b1WorkflowPath = Join-Path $logDirectory 'r2b1-workflow.json'
    if (Test-Path -LiteralPath $r2b1WorkflowPath -PathType Leaf) {
        try {
            $r2b1Workflow = Get-Content -LiteralPath $r2b1WorkflowPath -Raw | ConvertFrom-Json
            $requiredTransitions = @(
                'studio_initial_render', 'authentic_studio_project_create_open',
                'exact_source_saved_to_guest_vfs', 'authentic_studio_arm64_build',
                'arm64_entry_and_strict_base', 'artifact_vfs_readback', 'artifact_sha256_reported',
                'malformed_source_rejected_with_location', 'stale_output_removed',
                'no_false_artifact_success', 'rebuild_after_failure',
                'changed_source_changes_artifact_digest', 'staged_vfs_failure_project_open',
                'vfs_package_publication_failure', 'no_false_build_success_for_vfs_failure',
                'studio_clean_close'
            )
            $transitionsPass = $true
            foreach ($transition in $requiredTransitions) {
                if ($r2b1Workflow.transitions.$transition -ne $true) { $transitionsPass = $false; break }
            }
            $r2b1WorkflowPass = $transitionsPass -and
                [string]$r2b1Workflow.candidate_source_head -eq [string]$record.candidate_source_head -and
                [string]$r2b1Workflow.candidate_manifest_sha256 -eq [string]$record.candidate_manifest_sha256 -and
                [string]$r2b1Workflow.candidate_kernel_sha256 -eq [string]$record.candidate_kernel_sha256 -and
                [string]$r2b1Workflow.candidate_efi_sha256 -eq [string]$record.candidate_efi_sha256 -and
                [string]$r2b1Workflow.candidate_ramdisk_sha256 -eq [string]$record.candidate_ramdisk_sha256 -and
                [string]$r2b1Workflow.first_artifact_sha256 -match '^[0-9a-f]{64}$' -and
                [string]$r2b1Workflow.recovered_artifact_sha256 -match '^[0-9a-f]{64}$' -and
                [string]$r2b1Workflow.first_artifact_sha256 -ne [string]$r2b1Workflow.recovered_artifact_sha256
        } catch {
            $record.r2b1_workflow_validation_error = $_.Exception.Message
        }
    } else {
        $record.r2b1_workflow_validation_error = 'R2B1 external workflow record is missing.'
    }
    $record.r2b1_workflow_pass = $r2b1WorkflowPass
}
$commonLifecyclePass = $record.guest_pass_marker -and $record.studio_entry_called -and $record.adapter_selected -and
    $record.window_created -and $record.initial_render -and $record.mouse_event_delivered -and
    $record.key_event_delivered -and $record.focus_event_delivered -and $record.close_event_delivered -and
    $record.close_and_cleanup -and $record.exposed_desktop_redraw -and $record.post_studio_pointer_responsive -and
    $record.post_studio_keyboard_responsive -and $record.desktop_after_close_screendump_captured -and
    $record.desktop_after_close_500ms_screendump_captured -and $record.qac_launched_child -and
    $record.qac_released_reservation -and $record.desktop_post_exit_wait_completed -and !$timedOut
$workflowEvidencePass = if ($R2B1ExternalWorkflow) { $r2b1WorkflowPass } else {
    $record.editable_context_established -and $record.expected_test_text_observed -and $record.studio_keydown_keyup_balanced
}
$record.outcome = if ($commonLifecyclePass -and $workflowEvidencePass) {
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
if ($record.outcome -ne 'ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS') { $scriptExitCode = 1 }
} finally {
    try {
        $lastGuest = Read-GuestLog
        $lastGuest | Set-Content -LiteralPath (Join-Path $logDirectory 'guest-final.log') -Encoding utf8
        $record.guest_final_log = Join-Path $logDirectory 'guest-final.log'
        $record.guest_final_log_available = $true
    } catch {
        $record.guest_final_log_available = $false
        $record.guest_final_log_failure = $_.Exception.Message
    }
    if ($qac) {
        $record.qac_process_still_active = !$qac.HasExited
        $record.qac_process_exit_code = if ($qac.HasExited) { $qac.ExitCode } else { $null }
        $record.qac_final_log_available = Save-QacLogSnapshot
        if (!$record.qac_final_log_available) {
            $record.final_evidence_status = 'QAC remains active; live stdout/stderr files are preserved, but final child-exit and reservation-release evidence is unavailable.'
        }
        $record.managed_child_still_active = $null
        if ($record.managed_qemu_child_pid) {
            try {
                $ownedProcess = @(Get-QemuProcesses | Where-Object { [int]$_.ProcessId -eq [int]$record.managed_qemu_child_pid })
                $record.managed_child_still_active = $ownedProcess.Count -ne 0
            } catch {
                $record.managed_child_state_check_failure = $_.Exception.Message
            }
        }
        Save-RawQacHealthSnapshot
    }
    if ($record.outcome -eq 'ABI3_STUDIO_GUEST_IN_PROGRESS') {
        $record.outcome = if ($record.qac_process_still_active) { 'ABI3_STUDIO_GUEST_SHUTDOWN_UNVERIFIED' } else { 'ABI3_STUDIO_GUEST_RUN_ABORTED' }
    }
    Save-SessionRecord
}
if ($scriptExitCode -ne 0) { exit $scriptExitCode }
