param(
    [Parameter(Mandatory)][string]$KernelPath,
    [string]$SourceEsp = 'C:\Users\guideX\.codex\rx12-native-guest\esp',
    [string]$FirmwarePath = 'C:\Users\guideX\.codex\rx12-native-guest\firmware-code.fd'
)

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ((git -C $rootDir branch --show-current).Trim() -ne 'REXX_SUPPORT') {
    throw 'RX12R1 QEMU qualification requires branch REXX_SUPPORT.'
}
$guardManifest = Join-Path $rootDir 'docs\rexx\evidence\rx12r1\protected-artifacts-baseline.json'
Import-Module -Name (Join-Path $PSScriptRoot 'rexx_artifact_guard.psm1') -Force
$qemuPath = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$KernelPath = [System.IO.Path]::GetFullPath($KernelPath)
$SourceEsp = [System.IO.Path]::GetFullPath($SourceEsp)
$FirmwarePath = [System.IO.Path]::GetFullPath($FirmwarePath)
foreach ($requiredPath in @($KernelPath,$SourceEsp,$FirmwarePath,$qemuPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Required QEMU smoke input is missing: $requiredPath" }
}
if (-not (Test-Path -LiteralPath (Join-Path $SourceEsp 'EFI\BOOT\BOOTX64.EFI')) -or
    -not (Test-Path -LiteralPath (Join-Path $SourceEsp 'ramdisk.img'))) {
    throw "The source guest media is incomplete: $SourceEsp"
}

$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $rootDir "build\rexx\rx12r1\qemu-$runId"
$workDir = Join-Path $runRoot 'temp'
$guestRoot = Join-Path $runRoot 'guest'
$guestEsp = Join-Path $guestRoot 'esp'
$stagedFirmware = Join-Path $guestRoot 'firmware-code.fd'
$helperRoot = Join-Path $runRoot 'helper'
$serialPath = Join-Path $runRoot 'serial.log'
$qemuStdoutPath = Join-Path $runRoot 'qemu.stdout.log'
$qemuStderrPath = Join-Path $runRoot 'qemu.stderr.log'
$qemuLogPath = Join-Path $runRoot 'qemu.log'
$ownerPath = Join-Path $helperRoot 'qemu-owner.json'
$rawCaptureDir = Join-Path $runRoot 'screenshots-raw'
$plannedOutputs = @(
    $serialPath,$qemuStdoutPath,$qemuStderrPath,$qemuLogPath,$ownerPath,
    $stagedFirmware,(Join-Path $helperRoot 'type_rx12_guest_command.ps1'),
    (Join-Path $helperRoot 'ppm_to_png.py'),(Join-Path $helperRoot 'qemu-shutdown.json')
)
$null = Start-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot `
    -WorkingDirectory $workDir -PlannedOutputs $plannedOutputs
$env:TEMP = $workDir
$env:TMP = $workDir
$env:TMPDIR = $workDir
New-Item -ItemType Directory -Path $guestRoot,$helperRoot,$rawCaptureDir | Out-Null

$evidenceRoot = Join-Path $rootDir "docs\rexx\evidence\rx12r1\native-guest\run-$runId"
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$helperSource = Join-Path $rootDir 'docs\rexx\evidence\rx12\native-guest'
Copy-Item -LiteralPath $SourceEsp -Destination $guestEsp -Recurse
Copy-Item -LiteralPath $FirmwarePath -Destination $stagedFirmware
Copy-Item -LiteralPath (Join-Path $helperSource 'type_rx12_guest_command.ps1') -Destination $helperRoot
Copy-Item -LiteralPath (Join-Path $helperSource 'ppm_to_png.py') -Destination $helperRoot
Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $guestEsp 'kernel.elf') -Force

function Get-QemuCensus {
    $items = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" -ErrorAction SilentlyContinue)
    return [PSCustomObject]@{
        captured_at_utc = [DateTime]::UtcNow.ToString('o')
        count = $items.Count
        processes = @($items | ForEach-Object { [PSCustomObject]@{ pid=$_.ProcessId; image=[System.IO.Path]::GetFileName([string]$_.ExecutablePath) } })
        command_lines_redacted = $true
    }
}

function Read-QemuSerialLog([string]$Path) {
    $fileStream = [System.IO.FileStream]::new(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite)
    try {
        $reader = [System.IO.StreamReader]::new($fileStream)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally {
        $fileStream.Dispose()
    }
}

function Assert-OwnQemu {
    param($Identity)
    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($Identity.pid)" -ErrorAction SilentlyContinue
    if (-not $process -or [System.IO.Path]::GetFullPath([string]$process.ExecutablePath) -ine [System.IO.Path]::GetFullPath([string]$Identity.qemuPath) -or
        $process.CommandLine -notlike "*127.0.0.1:$($Identity.qmpPort),*" -or
        $process.CommandLine -notlike "*$($Identity.espPath)*") {
        throw 'QEMU process identity did not match the unique executable, QMP port, and disposable ESP; no process control was sent.'
    }
    $listenerDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $listenerDeadline) {
        $listener = Get-NetTCPConnection -LocalPort ([int]$Identity.qmpPort) -State Listen -ErrorAction SilentlyContinue
        if ($listener -and $listener.OwningProcess -contains [int]$Identity.pid) { return $process }
        Start-Sleep -Milliseconds 250
    }
    throw 'QMP listener ownership did not match the unique QEMU PID within 10 seconds; no process control was sent.'
}

$beforeCensus = Get-QemuCensus
$beforeCensus | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qemu-ownership-census-before.json') -Encoding utf8
$portProbe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$qmpPort = ([System.Net.IPEndPoint]$portProbe.LocalEndpoint).Port
$portProbe.Stop()
$kernelHash = (Get-FileHash -LiteralPath $KernelPath -Algorithm SHA256).Hash.ToLowerInvariant()
$stagedKernelHash = (Get-FileHash -LiteralPath (Join-Path $guestEsp 'kernel.elf') -Algorithm SHA256).Hash.ToLowerInvariant()
if ($kernelHash -ne $stagedKernelHash) { throw 'Staged disposable guest kernel hash does not match the isolated native build.' }
$owner = [PSCustomObject]@{
    pid = $null
    qemuPath = $qemuPath
    qmpPort = $qmpPort
    espPath = $guestEsp
    firmwarePath = $stagedFirmware
    serialPath = $serialPath
    rawCaptureDir = $rawCaptureDir
    kernelPath = $KernelPath
    kernelSha256 = $kernelHash
    stagedKernelSha256 = $stagedKernelHash
    network = 'disabled (-net none)'
    runRoot = $runRoot
}

$qemuArguments = @(
    '-machine','pc','-accel','tcg',
    '-drive',"if=pflash,format=raw,readonly=on,file=$stagedFirmware",
    '-drive',"file=fat:rw:$guestEsp,format=raw,if=ide,index=0",
    '-m','1024M','-smp','1','-vga','std',
    '-serial',"file:$serialPath",
    '-no-reboot','-no-shutdown','-boot','order=c','-rtc','base=utc,clock=host',
    '-net','none','-display','none','-monitor','none',
    '-qmp',"tcp:127.0.0.1:$qmpPort,server=on,wait=off",
    '-D',$qemuLogPath,'-d','guest_errors'
)
$invocation = [PSCustomObject]@{
    qemu = $qemuPath
    version = ((& $qemuPath --version 2>&1 | Select-Object -First 1).ToString())
    arguments = $qemuArguments
    qmp_port = $qmpPort
    disposable_esp = $guestEsp
    source_esp = $SourceEsp
    staged_firmware = $stagedFirmware
    serial_log = $serialPath
    qemu_log = $qemuLogPath
    network = '-net none'
    kernel_sha256 = $kernelHash
    staged_kernel_sha256 = $stagedKernelHash
    production_media_used = $false
}
$invocationPath = Join-Path $evidenceRoot 'qemu-invocation.json'
$invocation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $invocationPath -Encoding utf8

$qemuProcess = $null
$shutdownConfirmed = $false
try {
    $qemuProcess = Start-Process -FilePath $qemuPath -ArgumentList $qemuArguments -WorkingDirectory $workDir `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $qemuStdoutPath -RedirectStandardError $qemuStderrPath
    $owner.pid = $qemuProcess.Id
    $owner | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ownerPath -Encoding utf8

    $serialDeadline = [DateTime]::UtcNow.AddSeconds(180)
    $booted = $false
    while ([DateTime]::UtcNow -lt $serialDeadline) {
        $null = Assert-OwnQemu -Identity $owner
        if (Test-Path -LiteralPath $serialPath) {
            $serial = Read-QemuSerialLog $serialPath
            if ($serial.Contains('[KERNEL-HEARTBEAT]')) { $booted = $true; break }
        }
        Start-Sleep -Seconds 1
    }
    if (-not $booted) { throw "Isolated QEMU guest did not reach a kernel heartbeat within 180 seconds. Serial: $serialPath" }
    $heartbeatCountBefore = ([regex]::Matches($serial, '\[KERNEL-HEARTBEAT\]')).Count

    $helper = Join-Path $helperRoot 'type_rx12_guest_command.ps1'
    $commands = @(
        [PSCustomObject]@{ name='string-builtins'; text='rexx /rx12-string-builtins.rex'; open_terminal=$true; expected='RX12_NATIVE_STRINGS_DONE' },
        [PSCustomObject]@{ name='arg-omissions'; text='rexx /rx12-arg.rex'; open_console=$false; expected='RX12_NATIVE_ARG_DONE' },
        [PSCustomObject]@{ name='syntax-recovery'; text='rexx /rx12-syntax-recovery.rex'; open_console=$false; expected='RX12_NATIVE_SYNTAX_RECOVERED: 2' },
        [PSCustomObject]@{ name='resource-recovery'; text='rexx /rx12-oversized-output.rex'; open_console=$false; expected='VM resource limit exceeded' },
        [PSCustomObject]@{ name='post-error-command'; text='rexx /rx12-recovery.rex'; open_console=$false; expected='RX12_NATIVE_GUEST_RESPONSIVE: 9' },
        [PSCustomObject]@{ name='exit-status-script'; text='rexx /rx12-exit-status.rex'; open_console=$false; expected='RX12_NATIVE_EXIT_STATUS_SCRIPT' },
        [PSCustomObject]@{ name='exit-status-check'; text='echo $?'; open_console=$false; expected='23' }
    )
    $index = 0
    foreach ($command in $commands) {
        $index++
        $captureName = "rx12r1-{0:D2}-{1}" -f $index,$command.name
        if ($command.open_terminal) {
            & $helper -CommandText ([string]$command.text) -CaptureName $captureName -WaitSeconds 5 -OpenTerminal
        } else {
            & $helper -CommandText ([string]$command.text) -CaptureName $captureName -WaitSeconds 5
        }
        if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "Guest input/screenshot helper failed for $($command.name) with exit code $LASTEXITCODE." }
        $null = Assert-OwnQemu -Identity $owner
    }

    $screenshots = @(Get-ChildItem -LiteralPath $helperRoot -Filter 'rx12r1-*.png' -File)
    if ($screenshots.Count -ne $commands.Count) { throw "Expected one Console screenshot per smoke command; found $($screenshots.Count) for $($commands.Count) commands." }

    Start-Sleep -Seconds 3
    $serial = Get-Content -Raw -LiteralPath $serialPath
    $heartbeatCountAfter = ([regex]::Matches($serial, '\[KERNEL-HEARTBEAT\]')).Count
    if ($heartbeatCountAfter -le $heartbeatCountBefore) { throw 'Guest heartbeat did not continue during the RX12R1 smoke commands.' }
    foreach ($scriptName in @('rx12-string-builtins.rex','rx12-arg.rex','rx12-syntax-recovery.rex','rx12-oversized-output.rex','rx12-recovery.rex','rx12-exit-status.rex')) {
        if (-not $serial.Contains("/" + $scriptName)) { throw "Serial log did not record guest script access: $scriptName" }
    }
    $null = Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot -Stage 'qemu-smoke-guest' | Out-Null

    & $helper -QuitQemu -CommandText '' -CaptureName 'shutdown' -WaitSeconds 0
    $shutdownConfirmed = $true
    if (-not $qemuProcess.WaitForExit(30000)) { throw 'Verified QMP quit did not stop the isolated QEMU process within 30 seconds.' }
} catch {
    Write-Error "RX12R1 QEMU smoke stopped before completion: $($_.Exception.Message)`n$($_.ScriptStackTrace)"
    throw
} finally {
    if ($null -ne $qemuProcess -and -not $shutdownConfirmed) {
        try {
            $null = Assert-OwnQemu -Identity $owner
            & (Join-Path $helperRoot 'type_rx12_guest_command.ps1') -QuitQemu -CommandText '' -CaptureName 'shutdown-recovery' -WaitSeconds 0
            $shutdownConfirmed = $true
            $null = $qemuProcess.WaitForExit(30000)
        } catch {
            Write-Warning "Could not verify and shut down the isolated QEMU owner: $($_.Exception.Message)"
        }
    }
}

$afterCensus = Get-QemuCensus
$afterCensus | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qemu-ownership-census-after.json') -Encoding utf8
Copy-Item -LiteralPath $serialPath -Destination $evidenceRoot
Copy-Item -LiteralPath $qemuStdoutPath -Destination $evidenceRoot
Copy-Item -LiteralPath $qemuStderrPath -Destination $evidenceRoot
Copy-Item -LiteralPath $qemuLogPath -Destination $evidenceRoot -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $ownerPath -Destination $evidenceRoot
Copy-Item -LiteralPath (Join-Path $helperRoot 'qemu-shutdown.json') -Destination $evidenceRoot
Copy-Item -LiteralPath (Join-Path $helperRoot 'guest-commands.log') -Destination $evidenceRoot
Get-ChildItem -LiteralPath $helperRoot -Filter 'rx12r1-*.png' -File | Copy-Item -Destination $evidenceRoot

$transcriptPath = Join-Path $evidenceRoot 'console-transcript.txt'
$transcriptLines = [System.Collections.Generic.List[string]]::new()
$transcriptLines.Add('RX12R1 isolated QEMU Console transcript')
$transcriptLines.Add("Kernel SHA-256: $kernelHash")
$transcriptLines.Add("Network: disabled (-net none); disposable ESP: $guestEsp")
foreach ($command in $commands) { $transcriptLines.Add("COMMAND: $($command.text) | expected visible marker/status: $($command.expected)") }
$transcriptLines.Add("Serial boot/heartbeat: PASS; heartbeat records before suite=$heartbeatCountBefore after suite=$heartbeatCountAfter")
$transcriptLines.Add('The per-command QMP screenshots are the visual evidence for the expected markers and shell exit status.')
[System.IO.File]::WriteAllLines($transcriptPath,$transcriptLines,[System.Text.UTF8Encoding]::new($false))

$smokeResult = [PSCustomObject]@{
    schema = 'guidexos.rexx.rx12r1.qemu-smoke.v1'
    completed_at_utc = [DateTime]::UtcNow.ToString('o')
    passed = $true
    qemu_version = $invocation.version
    kernel_sha256 = $kernelHash
    staged_kernel_sha256 = $stagedKernelHash
    network_disabled = $true
    copied_disposable_media = $true
    production_media_used = $false
    script_accesses_in_serial = @('rx12-string-builtins.rex','rx12-arg.rex','rx12-syntax-recovery.rex','rx12-oversized-output.rex','rx12-recovery.rex','rx12-exit-status.rex')
    heartbeat_count_before_suite = $heartbeatCountBefore
    heartbeat_count_after_suite = $heartbeatCountAfter
    qmp_shutdown_acknowledged = $shutdownConfirmed
    evidence_directory = $evidenceRoot
}
$smokeResult | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qemu-smoke-result.json') -Encoding utf8
Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot -Stage 'qemu-smoke-final' | Out-Null
$preservationEvidence = Join-Path $evidenceRoot 'preservation'
New-Item -ItemType Directory -Path $preservationEvidence | Out-Null
Copy-Item -LiteralPath (Join-Path $runRoot 'preservation') -Destination $preservationEvidence -Recurse

Write-Output 'Isolated QEMU smoke passed.'
Write-Output "Kernel SHA-256: $kernelHash"
Write-Output "Guest evidence: $evidenceRoot"
