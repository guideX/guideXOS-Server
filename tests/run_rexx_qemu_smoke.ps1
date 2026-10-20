param(
    [ValidateSet('RX12R1','RX13','RX14','RX15','RX16','RX17','RX18','RX19','RX20')]
    [string]$Phase = 'RX12R1',
    [Parameter(Mandatory)][string]$KernelPath,
    [string]$SourceEsp = 'C:\Users\guideX\.codex\rx12-native-guest\esp',
    [string]$FirmwarePath = 'C:\Users\guideX\.codex\rx12-native-guest\firmware-code.fd'
)

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ((git -C $rootDir branch --show-current).Trim() -ne 'REXX_SUPPORT') {
    throw "$Phase QEMU qualification requires branch REXX_SUPPORT."
}
$guardManifest = if ($Phase -eq 'RX19') {
    Join-Path $rootDir 'docs\rexx\evidence\rx19\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX20') {
    Join-Path $rootDir 'docs\rexx\evidence\rx20\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX18') {
    Join-Path $rootDir 'docs\rexx\evidence\rx18\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX17') {
    Join-Path $rootDir 'docs\rexx\evidence\rx17\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX16') {
    Join-Path $rootDir 'docs\rexx\evidence\rx16\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX15') {
    Join-Path $rootDir 'docs\rexx\evidence\rx15\protected-artifacts-start.json'
} elseif ($Phase -eq 'RX14') {
    Join-Path $rootDir 'docs\rexx\evidence\rx14\protected-artifacts-start.json'
} else {
    Join-Path $rootDir 'docs\rexx\evidence\rx12r1\protected-artifacts-baseline.json'
}
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
$runRoot = Join-Path $rootDir "build\rexx\$($Phase.ToLowerInvariant())\qemu-$runId"
$workDir = Join-Path $runRoot 'temp'
$guestRoot = Join-Path $runRoot 'guest'
$guestEsp = Join-Path $guestRoot 'esp'
$stagedFirmware = Join-Path $guestRoot 'firmware-code.fd'
$helperRoot = Join-Path $runRoot 'helper'
$helperName = if ($Phase -eq 'RX20') { 'type_rx20_guest_command.ps1' } elseif ($Phase -eq 'RX19') { 'type_rx19_guest_command.ps1' } elseif ($Phase -eq 'RX18') { 'type_rx18_guest_command.ps1' } elseif ($Phase -eq 'RX17') { 'type_rx17_guest_command.ps1' } elseif ($Phase -eq 'RX16') { 'type_rx16_guest_command.ps1' } elseif ($Phase -eq 'RX15') { 'type_rx15_guest_command.ps1' } elseif ($Phase -eq 'RX14') { 'type_rx14_guest_command.ps1' } elseif ($Phase -eq 'RX13') { 'type_rx13_guest_command.ps1' } else { 'type_rx12_guest_command.ps1' }
$serialPath = Join-Path $runRoot 'serial.log'
$qemuStdoutPath = Join-Path $runRoot 'qemu.stdout.log'
$qemuStderrPath = Join-Path $runRoot 'qemu.stderr.log'
$qemuLogPath = Join-Path $runRoot 'qemu.log'
$ownerPath = Join-Path $helperRoot 'qemu-owner.json'
$rawCaptureDir = Join-Path $runRoot 'screenshots-raw'
$plannedOutputs = @(
    $serialPath,$qemuStdoutPath,$qemuStderrPath,$qemuLogPath,$ownerPath,
    $stagedFirmware,(Join-Path $helperRoot $helperName),
    (Join-Path $helperRoot 'ppm_to_png.py'),(Join-Path $helperRoot 'qemu-shutdown.json')
)
if ($Phase -eq 'RX13') {
    $plannedOutputs += @(
        (Join-Path $guestEsp 'rx13-string-builtins.rex'),
        (Join-Path $guestEsp 'rx13-word-utilities.rex'),
        (Join-Path $guestEsp 'rx13-syntax-recovery.rex'),
        (Join-Path $guestEsp 'rx13-oversized-output.rex'),
        (Join-Path $guestEsp 'rx13-recovery.rex'),
        (Join-Path $guestEsp 'rx13-exit-status.rex')
    )
}
if ($Phase -eq 'RX14') {
    $plannedOutputs += @(
        (Join-Path $guestEsp 'rx14-numeric-builtins.rex'),
        (Join-Path $guestEsp 'rx14-symbol-scope.rex'),
        (Join-Path $guestEsp 'rx14-xrange-bytes.rex'),
        (Join-Path $guestEsp 'rx14-interpret.rex'),
        (Join-Path $guestEsp 'rx14-syntax-recovery.rex'),
        (Join-Path $guestEsp 'rx14-resource-limit.rex'),
        (Join-Path $guestEsp 'rx14-recovery.rex'),
        (Join-Path $guestEsp 'rx14-exit-status.rex')
    )
}
if ($Phase -eq 'RX15') {
    $plannedOutputs += @(
        (Join-Path $guestEsp 'rx15-portable-conversions.rex'),
        (Join-Path $guestEsp 'rx15-conversion-error.rex'),
        (Join-Path $guestEsp 'rx15-conversion-resource-limit.rex'),
        (Join-Path $guestEsp 'rx15-conversion-recovery.rex')
    )
}
if ($Phase -eq 'RX16') {
    $plannedOutputs += @(
        (Join-Path $guestEsp 'rx16-literals.rex'),
        (Join-Path $guestEsp 'rx16-literal-error.rex'),
        (Join-Path $guestEsp 'rx16-recovery.rex')
    )
}
if ($Phase -eq 'RX17') {
    $plannedOutputs += @(
        (Join-Path $guestEsp 'rx17-bitwise.rex'),
        (Join-Path $guestEsp 'rx17-error-recovery.rex')
    )
}
if ($Phase -eq 'RX18') {
    $plannedOutputs += @(
        (Join-Path $guestEsp 'rx18-string-builtins.rex'),
        (Join-Path $guestEsp 'rx18-error-recovery.rex')
    )
}
if ($Phase -eq 'RX19') {
    $plannedOutputs += @(
        (Join-Path $guestEsp 'rx19-date-conversions.rex'),
        (Join-Path $guestEsp 'rx19-clock-error-recovery.rex')
    )
}
if ($Phase -eq 'RX20') {
    $plannedOutputs += @(
        (Join-Path $guestEsp 'rx20clk.rex'),
        (Join-Path $guestEsp 'rx20rcv.rex')
    )
}
$null = Start-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot `
    -WorkingDirectory $workDir -PlannedOutputs $plannedOutputs -Phase $Phase
$env:TEMP = $workDir
$env:TMP = $workDir
$env:TMPDIR = $workDir
New-Item -ItemType Directory -Path $guestRoot,$helperRoot,$rawCaptureDir | Out-Null

$evidenceRoot = Join-Path $rootDir "docs\rexx\evidence\$($Phase.ToLowerInvariant())\native-guest\run-$runId"
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$helperSource = if ($Phase -eq 'RX19') {
    Join-Path $rootDir 'docs\rexx\evidence\rx19\native-guest'
} elseif ($Phase -eq 'RX20') {
    Join-Path $rootDir 'docs\rexx\evidence\rx20\native-guest'
} elseif ($Phase -eq 'RX18') {
    Join-Path $rootDir 'docs\rexx\evidence\rx18\native-guest'
} elseif ($Phase -eq 'RX17') {
    Join-Path $rootDir 'docs\rexx\evidence\rx17\native-guest'
} elseif ($Phase -eq 'RX16') {
    Join-Path $rootDir 'docs\rexx\evidence\rx16\native-guest'
} elseif ($Phase -eq 'RX15') {
    Join-Path $rootDir 'docs\rexx\evidence\rx15\native-guest'
} elseif ($Phase -eq 'RX14') {
    Join-Path $rootDir 'docs\rexx\evidence\rx14\native-guest'
} elseif ($Phase -eq 'RX13') {
    Join-Path $rootDir 'docs\rexx\evidence\rx13\native-guest'
} else {
    Join-Path $rootDir 'docs\rexx\evidence\rx12\native-guest'
}
Copy-Item -LiteralPath $SourceEsp -Destination $guestEsp -Recurse
Copy-Item -LiteralPath $FirmwarePath -Destination $stagedFirmware
Copy-Item -LiteralPath (Join-Path $helperSource $helperName) -Destination $helperRoot
Copy-Item -LiteralPath (Join-Path $helperSource 'ppm_to_png.py') -Destination $helperRoot
Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $guestEsp 'kernel.elf') -Force
$rx13ScriptNames = @()
if ($Phase -eq 'RX13') {
    $rx13Scripts = Join-Path $rootDir 'docs\rexx\evidence\rx13\native-guest\scripts'
    $rx13ScriptFiles = @(Get-ChildItem -LiteralPath $rx13Scripts -Filter 'rx13-*.rex' -File | Sort-Object Name)
    if ($rx13ScriptFiles.Count -ne 6) { throw "Expected six reviewed RX13 native guest scripts; found $($rx13ScriptFiles.Count)." }
    foreach ($scriptFile in $rx13ScriptFiles) {
        Copy-Item -LiteralPath $scriptFile.FullName -Destination (Join-Path $guestEsp $scriptFile.Name)
        $rx13ScriptNames += $scriptFile.Name
    }
}
$rx14ScriptNames = @()
if ($Phase -eq 'RX14') {
    $rx14Scripts = Join-Path $rootDir 'docs\rexx\evidence\rx14\native-guest\scripts'
    $rx14ScriptFiles = @(Get-ChildItem -LiteralPath $rx14Scripts -Filter 'rx14-*.rex' -File | Sort-Object Name)
    if ($rx14ScriptFiles.Count -ne 8) { throw "Expected eight reviewed RX14 native guest scripts; found $($rx14ScriptFiles.Count)." }
    foreach ($scriptFile in $rx14ScriptFiles) {
        Copy-Item -LiteralPath $scriptFile.FullName -Destination (Join-Path $guestEsp $scriptFile.Name)
        $rx14ScriptNames += $scriptFile.Name
    }
}
$rx15ScriptNames = @()
if ($Phase -eq 'RX15') {
    $rx15Scripts = Join-Path $rootDir 'docs\rexx\evidence\rx15\native-guest\scripts'
    $rx15ScriptFiles = @(Get-ChildItem -LiteralPath $rx15Scripts -Filter 'rx15-*.rex' -File | Sort-Object Name)
    if ($rx15ScriptFiles.Count -ne 4) { throw "Expected four reviewed RX15 native guest scripts; found $($rx15ScriptFiles.Count)." }
    foreach ($scriptFile in $rx15ScriptFiles) {
        Copy-Item -LiteralPath $scriptFile.FullName -Destination (Join-Path $guestEsp $scriptFile.Name)
        $rx15ScriptNames += $scriptFile.Name
    }
}
$rx16ScriptNames = @()
if ($Phase -eq 'RX16') {
    $rx16Scripts = Join-Path $rootDir 'docs\rexx\evidence\rx16\native-guest\scripts'
    $rx16ScriptFiles = @(Get-ChildItem -LiteralPath $rx16Scripts -Filter 'rx16-*.rex' -File | Sort-Object Name)
    if ($rx16ScriptFiles.Count -ne 3) { throw "Expected three reviewed RX16 native guest scripts; found $($rx16ScriptFiles.Count)." }
    foreach ($scriptFile in $rx16ScriptFiles) {
        Copy-Item -LiteralPath $scriptFile.FullName -Destination (Join-Path $guestEsp $scriptFile.Name)
        $rx16ScriptNames += $scriptFile.Name
    }
}
$rx17ScriptNames = @()
if ($Phase -eq 'RX17') {
    $rx17Scripts = Join-Path $rootDir 'docs\rexx\evidence\rx17\native-guest\scripts'
    $rx17ScriptFiles = @(Get-ChildItem -LiteralPath $rx17Scripts -Filter 'rx17-*.rex' -File | Sort-Object Name)
    if ($rx17ScriptFiles.Count -ne 2) { throw "Expected two reviewed RX17 native guest scripts; found $($rx17ScriptFiles.Count)." }
    foreach ($scriptFile in $rx17ScriptFiles) {
        Copy-Item -LiteralPath $scriptFile.FullName -Destination (Join-Path $guestEsp $scriptFile.Name)
        $rx17ScriptNames += $scriptFile.Name
    }
}
$rx18ScriptNames = @()
if ($Phase -eq 'RX18') {
    $rx18Scripts = Join-Path $rootDir 'docs\rexx\evidence\rx18\native-guest\scripts'
    $rx18ScriptFiles = @(Get-ChildItem -LiteralPath $rx18Scripts -Filter 'rx18-*.rex' -File | Sort-Object Name)
    if ($rx18ScriptFiles.Count -ne 2) { throw "Expected two reviewed RX18 native guest scripts; found $($rx18ScriptFiles.Count)." }
    foreach ($scriptFile in $rx18ScriptFiles) {
        Copy-Item -LiteralPath $scriptFile.FullName -Destination (Join-Path $guestEsp $scriptFile.Name)
        $rx18ScriptNames += $scriptFile.Name
    }
}
$rx19ScriptNames = @()
if ($Phase -eq 'RX19') {
    $rx19Scripts = Join-Path $rootDir 'docs\rexx\evidence\rx19\native-guest\scripts'
    $rx19ScriptFiles = @(Get-ChildItem -LiteralPath $rx19Scripts -Filter 'rx19-*.rex' -File | Sort-Object Name)
    if ($rx19ScriptFiles.Count -ne 2) { throw "Expected two reviewed RX19 native guest scripts; found $($rx19ScriptFiles.Count)." }
    foreach ($scriptFile in $rx19ScriptFiles) {
        Copy-Item -LiteralPath $scriptFile.FullName -Destination (Join-Path $guestEsp $scriptFile.Name)
        $rx19ScriptNames += $scriptFile.Name
    }
}
$rx20ScriptNames = @()
if ($Phase -eq 'RX20') {
    $rx20Scripts = Join-Path $rootDir 'docs\rexx\evidence\rx20\native-guest\scripts'
    $rx20ScriptFiles = @(Get-ChildItem -LiteralPath $rx20Scripts -Filter 'rx20*.rex' -File | Sort-Object Name)
    if ($rx20ScriptFiles.Count -ne 2) { throw "Expected two reviewed RX20 native guest scripts; found $($rx20ScriptFiles.Count)." }
    foreach ($scriptFile in $rx20ScriptFiles) {
        Copy-Item -LiteralPath $scriptFile.FullName -Destination (Join-Path $guestEsp $scriptFile.Name)
        $rx20ScriptNames += $scriptFile.Name
    }
}

function Get-QemuCensus {
    $items = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" -ErrorAction SilentlyContinue)
    return [PSCustomObject]@{
        captured_at_utc = [DateTime]::UtcNow.ToString('o')
        count = $items.Count
        processes = @($items | ForEach-Object { [PSCustomObject]@{ pid=$_.ProcessId; image=[System.IO.Path]::GetFileName([string]$_.ExecutablePath); command_line=$_.CommandLine } })
        command_lines_redacted = $false
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

    $helper = Join-Path $helperRoot $helperName
    $commands = if ($Phase -eq 'RX20') {
        @(
            [PSCustomObject]@{ name='clock-capabilities-and-monotonic'; text='rexx /rx20clk.rex'; open_terminal=$true; expected='RX20_NATIVE_CLOCK_CAPABILITY_DONE' },
            [PSCustomObject]@{ name='post-error-date-recovery'; text='rexx /rx20rcv.rex'; expected='RX20_NATIVE_GUEST_RESPONSIVE' }
        )
    } elseif ($Phase -eq 'RX19') {
        @(
            [PSCustomObject]@{ name='deterministic-date-conversions'; text='rexx /rx19-date-conversions.rex'; open_terminal=$true; expected='RX19_NATIVE_DATE_CONVERSIONS_DONE' },
            [PSCustomObject]@{ name='clock-capability-and-error-recovery'; text='rexx /rx19-clock-error-recovery.rex'; expected='RX19_NATIVE_GUEST_RESPONSIVE' }
        )
    } elseif ($Phase -eq 'RX18') {
        @(
            [PSCustomObject]@{ name='classic-string-builtins'; text='rexx /rx18-string-builtins.rex'; open_terminal=$true; expected='RX18_NATIVE_ABBREV_CENTER_DONE' },
            [PSCustomObject]@{ name='invalid-argument-recovery'; text='rexx /rx18-error-recovery.rex'; expected='RX18_NATIVE_POST_ERROR_RESPONSIVE: 20206162632020' }
        )
    } elseif ($Phase -eq 'RX17') {
        @(
            [PSCustomObject]@{ name='bitwise-builtins'; text='rexx /rx17-bitwise.rex'; open_terminal=$true; expected='RX17_NATIVE_BITWISE_DONE' },
            [PSCustomObject]@{ name='handled-error-and-recovery'; text='rexx /rx17-error-recovery.rex'; expected='RX17_NATIVE_POST_ERROR_RESPONSIVE: 007F7F' }
        )
    } elseif ($Phase -eq 'RX16') {
        @(
            [PSCustomObject]@{ name='native-literals'; text='rexx /rx16-literals.rex'; open_terminal=$true; expected='RX16_NATIVE_LITERALS_DONE' },
            [PSCustomObject]@{ name='malformed-literal'; text='rexx /rx16-literal-error.rex'; expected='invalid hexadecimal string literal' },
            [PSCustomObject]@{ name='post-error-command'; text='rexx /rx16-recovery.rex'; expected='RX16_NATIVE_GUEST_RESPONSIVE: 4142' }
        )
    } elseif ($Phase -eq 'RX15') {
        @(
            [PSCustomObject]@{ name='portable-conversions'; text='rexx /rx15-portable-conversions.rex'; open_terminal=$true; expected='RX15_NATIVE_CONVERSIONS_DONE' },
            [PSCustomObject]@{ name='invalid-conversion'; text='rexx /rx15-conversion-error.rex'; expected='invalid arguments to REXX built-in function' },
            [PSCustomObject]@{ name='resource-limit'; text='rexx /rx15-conversion-resource-limit.rex'; expected='VM resource limit exceeded' },
            [PSCustomObject]@{ name='post-error-command'; text='rexx /rx15-conversion-recovery.rex'; expected='RX15_NATIVE_GUEST_RESPONSIVE: FFFFFFFF' }
        )
    } elseif ($Phase -eq 'RX14') {
        @(
            [PSCustomObject]@{ name='numeric-builtins'; text='rexx /rx14-numeric-builtins.rex'; open_terminal=$true; expected='RX14_NATIVE_NUMERIC_DONE' },
            [PSCustomObject]@{ name='symbol-scope'; text='rexx /rx14-symbol-scope.rex'; expected='RX14_NATIVE_SYMBOL_DONE' },
            [PSCustomObject]@{ name='xrange-bytes'; text='rexx /rx14-xrange-bytes.rex'; expected='XRANGE_BYTES_EQUAL: 1' },
            [PSCustomObject]@{ name='interpret'; text='rexx /rx14-interpret.rex'; expected='RX14_NATIVE_INTERPRET_DONE' },
            [PSCustomObject]@{ name='syntax-recovery'; text='rexx /rx14-syntax-recovery.rex'; expected='RX14_NATIVE_SYNTAX_RECOVERED: 2' },
            [PSCustomObject]@{ name='resource-limit'; text='rexx /rx14-resource-limit.rex'; expected='VM resource limit exceeded' },
            [PSCustomObject]@{ name='post-error-command'; text='rexx /rx14-recovery.rex'; expected='RX14_NATIVE_GUEST_RESPONSIVE: 5' },
            [PSCustomObject]@{ name='exit-status-script'; text='rexx /rx14-exit-status.rex'; expected='RX14_NATIVE_EXIT_STATUS_SCRIPT' },
            [PSCustomObject]@{ name='exit-status-check'; text='echo $?'; expected='23' }
        )
    } elseif ($Phase -eq 'RX13') {
        @(
            [PSCustomObject]@{ name='string-builtins'; text='rexx /rx13-string-builtins.rex'; open_terminal=$true; expected='RX13_NATIVE_STRINGS_DONE' },
            [PSCustomObject]@{ name='word-utilities'; text='rexx /rx13-word-utilities.rex'; expected='RX13_NATIVE_WORDS_DONE' },
            [PSCustomObject]@{ name='syntax-recovery'; text='rexx /rx13-syntax-recovery.rex'; expected='RX13_NATIVE_SYNTAX_RECOVERED: 2' },
            [PSCustomObject]@{ name='resource-recovery'; text='rexx /rx13-oversized-output.rex'; expected='VM resource limit exceeded' },
            [PSCustomObject]@{ name='post-error-command'; text='rexx /rx13-recovery.rex'; expected='RX13_NATIVE_GUEST_RESPONSIVE: 5' },
            [PSCustomObject]@{ name='exit-status-script'; text='rexx /rx13-exit-status.rex'; expected='RX13_NATIVE_EXIT_STATUS_SCRIPT' },
            [PSCustomObject]@{ name='exit-status-check'; text='echo $?'; expected='23' }
        )
    } else {
        @(
            [PSCustomObject]@{ name='string-builtins'; text='rexx /rx12-string-builtins.rex'; open_terminal=$true; expected='RX12_NATIVE_STRINGS_DONE' },
            [PSCustomObject]@{ name='arg-omissions'; text='rexx /rx12-arg.rex'; open_console=$false; expected='RX12_NATIVE_ARG_DONE' },
            [PSCustomObject]@{ name='syntax-recovery'; text='rexx /rx12-syntax-recovery.rex'; open_console=$false; expected='RX12_NATIVE_SYNTAX_RECOVERED: 2' },
            [PSCustomObject]@{ name='resource-recovery'; text='rexx /rx12-oversized-output.rex'; open_console=$false; expected='VM resource limit exceeded' },
            [PSCustomObject]@{ name='post-error-command'; text='rexx /rx12-recovery.rex'; open_console=$false; expected='RX12_NATIVE_GUEST_RESPONSIVE: 9' },
            [PSCustomObject]@{ name='exit-status-script'; text='rexx /rx12-exit-status.rex'; open_console=$false; expected='RX12_NATIVE_EXIT_STATUS_SCRIPT' },
            [PSCustomObject]@{ name='exit-status-check'; text='echo $?'; open_console=$false; expected='23' }
        )
    }
    $index = 0
    foreach ($command in $commands) {
        $index++
        $capturePrefix = if ($Phase -eq 'RX20') { 'rx20' } elseif ($Phase -eq 'RX19') { 'rx19' } elseif ($Phase -eq 'RX18') { 'rx18' } elseif ($Phase -eq 'RX17') { 'rx17' } elseif ($Phase -eq 'RX16') { 'rx16' } elseif ($Phase -eq 'RX15') { 'rx15' } elseif ($Phase -eq 'RX14') { 'rx14' } elseif ($Phase -eq 'RX13') { 'rx13' } else { 'rx12r1' }
        $captureName = "{0}-{1:D2}-{2}" -f $capturePrefix,$index,$command.name
        $commandWaitSeconds = if ($Phase -eq 'RX20') { 15 } else { 5 }
        if ($command.open_terminal) {
            & $helper -CommandText ([string]$command.text) -CaptureName $captureName -WaitSeconds $commandWaitSeconds -OpenTerminal
        } else {
            & $helper -CommandText ([string]$command.text) -CaptureName $captureName -WaitSeconds $commandWaitSeconds
        }
        if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "Guest input/screenshot helper failed for $($command.name) with exit code $LASTEXITCODE." }
        $null = Assert-OwnQemu -Identity $owner
    }

    $capturePrefix = if ($Phase -eq 'RX20') { 'rx20' } elseif ($Phase -eq 'RX19') { 'rx19' } elseif ($Phase -eq 'RX18') { 'rx18' } elseif ($Phase -eq 'RX17') { 'rx17' } elseif ($Phase -eq 'RX16') { 'rx16' } elseif ($Phase -eq 'RX15') { 'rx15' } elseif ($Phase -eq 'RX14') { 'rx14' } elseif ($Phase -eq 'RX13') { 'rx13' } else { 'rx12r1' }
    $screenshots = @(Get-ChildItem -LiteralPath $helperRoot -Filter "$capturePrefix-*.png" -File)
    if ($screenshots.Count -ne $commands.Count) { throw "Expected one Console screenshot per smoke command; found $($screenshots.Count) for $($commands.Count) commands." }

    Start-Sleep -Seconds 3
    $serial = Get-Content -Raw -LiteralPath $serialPath
    $heartbeatCountAfter = ([regex]::Matches($serial, '\[KERNEL-HEARTBEAT\]')).Count
    if ($heartbeatCountAfter -le $heartbeatCountBefore) { throw "Guest heartbeat did not continue during the $Phase smoke commands." }
    $scriptNames = if ($Phase -eq 'RX20') { $rx20ScriptNames } elseif ($Phase -eq 'RX19') { $rx19ScriptNames } elseif ($Phase -eq 'RX18') { $rx18ScriptNames } elseif ($Phase -eq 'RX17') { $rx17ScriptNames } elseif ($Phase -eq 'RX16') { $rx16ScriptNames } elseif ($Phase -eq 'RX15') { $rx15ScriptNames } elseif ($Phase -eq 'RX14') { $rx14ScriptNames } elseif ($Phase -eq 'RX13') { $rx13ScriptNames } else { @('rx12-string-builtins.rex','rx12-arg.rex','rx12-syntax-recovery.rex','rx12-oversized-output.rex','rx12-recovery.rex','rx12-exit-status.rex') }
    $serialScriptNames = if ($Phase -in @('RX20','RX19','RX18','RX17','RX16')) {
        $scriptNames
    } elseif ($Phase -eq 'RX15') {
        @($scriptNames | Where-Object { $_ -ne 'rx15-conversion-resource-limit.rex' })
    } elseif ($Phase -eq 'RX14') {
        @($scriptNames | Where-Object { $_ -ne 'rx14-resource-limit.rex' })
    } elseif ($Phase -eq 'RX13') {
        @($scriptNames | Where-Object { $_ -ne 'rx13-oversized-output.rex' })
    } else {
        $scriptNames
    }
    foreach ($scriptName in $serialScriptNames) {
        if (-not $serial.Contains("/" + $scriptName)) { throw "Serial log did not record guest script access: $scriptName" }
    }
    if ($Phase -eq 'RX20') {
        if ($serial -notmatch '\[REXX-CLOCK\] source=CMOS_RTC read=ok raw=1 local=0 utc=0 offset=unknown resolution_ns=3B9ACA00 accuracy=unknown monotonic=PIT') {
            throw 'RX20 guest did not report a successful CMOS raw reading with unknown interpretation and PIT monotonic capability.'
        }
        if ($serial -notmatch '\[NAVIGATOR-SMOKE\] tls_prereq.wall_clock_backend=CMOS RTC \(interpretation unknown; UTC unavailable\)' -or
            $serial -notmatch '\[NAVIGATOR-SMOKE\] tls_prereq.wall_clock_status=Unavailable' -or
            $serial -match 'CMOS RTC \(interpreted as UTC\)') {
            throw 'RX20 guest TLS prerequisites did not preserve unknown CMOS interpretation and unavailable UTC status.'
        }
    }
    $null = Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot -Stage 'qemu-smoke-guest' -Phase $Phase | Out-Null

    & $helper -QuitQemu -CommandText '' -CaptureName 'shutdown' -WaitSeconds 0
    $shutdownConfirmed = $true
    if (-not $qemuProcess.WaitForExit(30000)) { throw 'Verified QMP quit did not stop the isolated QEMU process within 30 seconds.' }
    $immediateCensus = Get-QemuCensus
    $immediateCensus | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qemu-immediate-post-shutdown-census.json') -Encoding utf8
} catch {
    Write-Error "$Phase QEMU smoke stopped before completion: $($_.Exception.Message)`n$($_.ScriptStackTrace)"
    throw
} finally {
    if ($null -ne $qemuProcess -and -not $shutdownConfirmed) {
        try {
            $null = Assert-OwnQemu -Identity $owner
            & (Join-Path $helperRoot $helperName) -QuitQemu -CommandText '' -CaptureName 'shutdown-recovery' -WaitSeconds 0
            $shutdownConfirmed = $true
            $null = $qemuProcess.WaitForExit(30000)
        } catch {
            Write-Warning "Could not verify and shut down the isolated QEMU owner: $($_.Exception.Message)"
        }
    }
}

$immediatePath = Join-Path $evidenceRoot 'qemu-immediate-post-shutdown-census.json'
if (-not (Test-Path -LiteralPath $immediatePath)) {
    $immediateCensus = Get-QemuCensus
    $immediateCensus | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $immediatePath -Encoding utf8
}
Start-Sleep -Seconds 2
$finalCensus = Get-QemuCensus
$finalCensus | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qemu-final-census.json') -Encoding utf8
$afterCensus = Get-QemuCensus
$afterCensus | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qemu-ownership-census-after.json') -Encoding utf8
$beforePids = @($beforeCensus.processes | ForEach-Object { [int]$_.pid })
$immediatePids = @($immediateCensus.processes | ForEach-Object { [int]$_.pid })
$finalPids = @($finalCensus.processes | ForEach-Object { [int]$_.pid })
$foreignBefore = @($beforePids | Where-Object { $_ -ne [int]$owner.pid })
$foreignAbsentFinal = @($foreignBefore | Where-Object { $_ -notin $finalPids })
$phaseEvidenceRoot = Join-Path $rootDir "docs\rexx\evidence\$($Phase.ToLowerInvariant())"
$phasePreflightPath = Join-Path $phaseEvidenceRoot 'preflight.json'
$preflightPids = @()
if (Test-Path -LiteralPath $phasePreflightPath) {
    $preflightCensus = Get-Content -Raw -LiteralPath $phasePreflightPath | ConvertFrom-Json
    if ($null -ne $preflightCensus.qemu_process_census) {
        $preflightPids = @($preflightCensus.qemu_process_census | ForEach-Object { [int]$_.pid })
    }
}
$preflightAbsentBeforeLaunch = @($preflightPids | Where-Object { $_ -notin $beforePids })
$additionalForeignPids = @(@($immediatePids + $finalPids | Sort-Object -Unique) | Where-Object { $_ -notin $beforePids -and $_ -ne [int]$owner.pid })
$processReconciliation = [PSCustomObject]@{
    schema = "guidexos.rexx.$($Phase.ToLowerInvariant()).qemu-process-reconciliation.v1"
    owned_qemu_pid = $owner.pid
    owned_pid_absent_after_shutdown = [int]$owner.pid -notin $immediatePids -and [int]$owner.pid -notin $finalPids
    qmp_shutdown_acknowledged = $shutdownConfirmed
    phase_preflight_census = if (Test-Path -LiteralPath $phasePreflightPath) { 'preflight.json' } else { $null }
    preflight_qemu_pids = $preflightPids
    preflight_pids_absent_before_launch = $preflightAbsentBeforeLaunch
    pre_run_qemu_pids = $beforePids
    immediate_post_shutdown_qemu_pids = $immediatePids
    final_qemu_pids = $finalPids
    preexisting_foreign_pids_absent_at_final = $foreignAbsentFinal
    additional_foreign_pids_seen_after_launch = $additionalForeignPids
    control_sent_to_any_foreign_pid = $false
    foreign_process_changes_cause = "Unknown; $Phase sent no process control to any pre-existing or newly observed foreign QEMU process."
}
$processReconciliation | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qemu-process-reconciliation.json') -Encoding utf8
Copy-Item -LiteralPath $serialPath -Destination $evidenceRoot
Copy-Item -LiteralPath $qemuStdoutPath -Destination $evidenceRoot
Copy-Item -LiteralPath $qemuStderrPath -Destination $evidenceRoot
Copy-Item -LiteralPath $qemuLogPath -Destination $evidenceRoot -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $ownerPath -Destination $evidenceRoot
Copy-Item -LiteralPath (Join-Path $helperRoot 'qemu-shutdown.json') -Destination $evidenceRoot
Copy-Item -LiteralPath (Join-Path $helperRoot 'guest-commands.log') -Destination $evidenceRoot
Get-ChildItem -LiteralPath $helperRoot -Filter "$capturePrefix-*.png" -File | Copy-Item -Destination $evidenceRoot

$transcriptPath = Join-Path $evidenceRoot 'console-transcript.txt'
$transcriptLines = [System.Collections.Generic.List[string]]::new()
$transcriptLines.Add("$Phase isolated QEMU Console transcript")
$transcriptLines.Add("Kernel SHA-256: $kernelHash")
$transcriptLines.Add("Network: disabled (-net none); disposable ESP: $guestEsp")
foreach ($command in $commands) { $transcriptLines.Add("COMMAND: $($command.text) | expected visible marker/status: $($command.expected)") }
$transcriptLines.Add("Serial boot/heartbeat: PASS; heartbeat records before suite=$heartbeatCountBefore after suite=$heartbeatCountAfter")
$transcriptLines.Add('The per-command QMP screenshots are the visual evidence for the expected markers and shell exit status.')
[System.IO.File]::WriteAllLines($transcriptPath,$transcriptLines,[System.Text.UTF8Encoding]::new($false))

$smokeResult = [PSCustomObject]@{
    schema = "guidexos.rexx.$($Phase.ToLowerInvariant()).qemu-smoke.v1"
    completed_at_utc = [DateTime]::UtcNow.ToString('o')
    passed = $true
    qemu_version = $invocation.version
    kernel_sha256 = $kernelHash
    staged_kernel_sha256 = $stagedKernelHash
    network_disabled = $true
    copied_disposable_media = $true
    production_media_used = $false
    script_accesses_in_serial = $serialScriptNames
    heartbeat_count_before_suite = $heartbeatCountBefore
    heartbeat_count_after_suite = $heartbeatCountAfter
    qmp_shutdown_acknowledged = $shutdownConfirmed
    qemu_pre_run_census = 'qemu-ownership-census-before.json'
    qemu_immediate_post_shutdown_census = 'qemu-immediate-post-shutdown-census.json'
    qemu_final_census = 'qemu-final-census.json'
    qemu_process_reconciliation = 'qemu-process-reconciliation.json'
    evidence_directory = $evidenceRoot
}
$smokeResult | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'qemu-smoke-result.json') -Encoding utf8
Test-ReXXArtifactRun -ManifestPath $guardManifest -RunRoot $runRoot -Stage 'qemu-smoke-final' -Phase $Phase | Out-Null
$preservationEvidence = Join-Path $evidenceRoot 'preservation'
New-Item -ItemType Directory -Path $preservationEvidence | Out-Null
Copy-Item -LiteralPath (Join-Path $runRoot 'preservation') -Destination $preservationEvidence -Recurse

Write-Output 'Isolated QEMU smoke passed.'
Write-Output "Kernel SHA-256: $kernelHash"
Write-Output "Guest evidence: $evidenceRoot"
