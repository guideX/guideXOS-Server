[CmdletBinding()]
param(
    [string]$ArtifactDirectory = '',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [string]$FirmwareVarsTemplate = 'C:\Program Files\qemu\share\edk2-arm-vars.fd',
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QacCliPath = '',
    [int]$TimeoutSeconds = 300,
    [int]$AdmissionTimeoutSeconds = 1800
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
& (Join-Path $PSScriptRoot 'build-aarch64-gicv3.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $buildDirectory
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
    candidate_manifest = $null
    qac_attempts = @()
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
    if ($status -match 'Recovery: Required') {
        throw "$Context QAC requires recovery; refusing guest execution.`n$status"
    }
    $processMatch = [regex]::Match($status, '(?m)^QEMU processes: (\d+) \((\d+) foreign\)\r?$')
    if (!$processMatch.Success) { throw "$Context QAC status did not expose a parseable QEMU ownership count.`n$status" }
    if ([int]$processMatch.Groups[2].Value -ne 0) {
        throw "$Context QAC reports unmanaged/foreign QEMU processes; refusing guest execution.`n$status"
    }
    $list = & $QacCliPath list 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "$Context unable to inspect QAC reservations.`n$list" }
    return [pscustomobject]@{ Status = $status.Trim(); List = $list.Trim() }
}

function Assert-QacOwnedGuest([int]$QacProcessId, [string]$ExpectedQemuPath,
                              [string]$Machine, [string]$Cpu, [string]$Context) {
    $processes = Get-QemuProcesses
    if ($processes.Count -ne 1) {
        throw "$Context expected exactly one QEMU process after the exclusive grant; found $($processes.Count)."
    }
    $child = $processes[0]
    $pathMatches = [string]::Equals([string]$child.ExecutablePath, $ExpectedQemuPath,
        [StringComparison]::OrdinalIgnoreCase)
    $parentMatches = [int]$child.ParentProcessId -eq $QacProcessId
    $commandLine = [string]$child.CommandLine
    $machineMatches = [regex]::IsMatch($commandLine,
        '(?:^|\s)"?-machine"?\s+"?' + [regex]::Escape($Machine) + '"?(?:\s|$)')
    $cpuMatches = [regex]::IsMatch($commandLine,
        '(?:^|\s)"?-cpu"?\s+"?' + [regex]::Escape($Cpu) + '"?(?:\s|$)')
    if (!$pathMatches -or !$parentMatches -or !$machineMatches -or !$cpuMatches) {
        throw "$Context QEMU ownership/profile mismatch (pid=$($child.ProcessId), parent=$($child.ParentProcessId), path=$($child.ExecutablePath), command=$commandLine)."
    }
    return $child
}

function Wait-QacOwnedGuest([int]$QacProcessId, [string]$ExpectedQemuPath,
                            [string]$Machine, [string]$Cpu, [string]$Context) {
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds(15)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $processes = Get-QemuProcesses
        if ($processes.Count -gt 0) {
            return Assert-QacOwnedGuest $QacProcessId $ExpectedQemuPath $Machine $Cpu $Context
        }
        Start-Sleep -Milliseconds 100
    }
    throw "$Context QAC reported Running but no managed QEMU child appeared within 15 seconds."
}

function Get-FileIdentity([string]$Path) {
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    return [ordered]@{
        path = [IO.Path]::GetFullPath($Path)
        bytes = $item.Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Write-CandidateManifest {
    $repoInputs = @(& git -C $repoRoot ls-files -- 'kernel/arch/arm64' 'kernel/core' 'guideXOSBootLoader/aarch64' `
        'tests/aarch64_phase2_host_tests.cpp' 'tests/aarch64_gicv3_host_tests.cpp' `
        'tests/aarch64_phase3_host_tests.cpp' 'tests/aarch64_phase4_host_tests.cpp' `
        'tests/aarch64_scheduler_repeatability_host_tests.cpp')
    if ($LASTEXITCODE -ne 0 -or $repoInputs.Count -eq 0) { throw 'Unable to enumerate source-matched GICv3 build inputs.' }
    $explicitInputs = @(
        'scripts/build-aarch64-gicv3.ps1', 'scripts/build-aarch64-phase4.ps1',
        'scripts/stage-aarch64-phase4-ramdisk.ps1', 'scripts/test-aarch64-gicv3-host.ps1',
        'scripts/test-aarch64-gicv3-qemu.ps1', 'kernel/arch/arm64/phase4_boot_info.h'
    )
    $inputHashes = [ordered]@{}
    foreach ($relative in @($repoInputs + $explicitInputs | Sort-Object -Unique)) {
        $path = Join-Path $repoRoot ($relative -replace '/', '\\')
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Build input is missing: $relative" }
        $inputHashes[$relative] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $clang = Join-Path $LlvmRoot 'clang++.exe'
    $linker = Join-Path $LlvmRoot 'ld.lld.exe'
    $gitStatus = (& git -C $repoRoot status --short --branch | Out-String).Trim()
    $artifacts = [ordered]@{}
    foreach ($relative in @('BOOTAA64.EFI', 'kernel.elf', 'esp/EFI/BOOT/BOOTAA64.EFI', 'esp/kernel.elf', 'esp/ramdisk.img')) {
        $path = Join-Path $buildDirectory ($relative -replace '/', '\\')
        $artifacts[$relative] = Get-FileIdentity $path
    }
    $manifest = [ordered]@{
        schemaVersion = 1
        candidate = 'AARCH64 GICv3 Phase 4 minimal guest; production DTB-selected controller; no Studio payload'
        source = [ordered]@{
            branch = (& git -C $repoRoot branch --show-current).Trim()
            head = (& git -C $repoRoot rev-parse HEAD).Trim()
            worktree = $gitStatus
            inputSha256 = $inputHashes
        }
        build = [ordered]@{
            command = 'scripts/build-aarch64-gicv3.ps1 -OutputDirectory <candidate>'
            profile = 'GXOS_AARCH64_PHASE4; freestanding AArch64 UEFI/kernel; production GICv2/GICv3 selector; 512 MiB; minimal Phase 4 VFS/scheduler workload'
            qemuMachine = 'virt,gic-version=3,acpi=off'
            qemuCpu = 'cortex-a53'
            llvmRoot = $LlvmRoot
            clangVersion = (& $clang --version 2>&1 | Select-Object -First 1).ToString()
            linkerVersion = (& $linker --version 2>&1 | Select-Object -First 1).ToString()
            clang = Get-FileIdentity $clang
            linker = Get-FileIdentity $linker
        }
        runtime = [ordered]@{
            qemu = Get-FileIdentity $QemuPath
            firmwareCode = Get-FileIdentity $FirmwareCode
            firmwareVarsTemplate = Get-FileIdentity $FirmwareVarsTemplate
        }
        artifacts = $artifacts
    }
    $manifestPath = Join-Path $buildDirectory 'candidate-manifest.json'
    $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    return [pscustomobject]@{
        Path = $manifestPath
        Sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        Data = $manifest
    }
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
    if ($Text -match 'AARCH64_PHASE[234]_ERROR\b|\[guideXOS\].*(FAIL\b|FATAL\b|ERROR\b)|\[A64 UEFI\] ERROR\b') {
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
    if ($irqId -ne $timerPpi -or $ticks -lt 10000 -or $acks -ne $ticks -or
        $eois -ne $acks -or $invalid -ne 0 -or $completionErrors -ne 0 -or $pending -ne 0) {
        throw "$Name failed bounded timer acknowledgement/EOI acceptance"
    }
    $scheduler = [regex]::Match($Text, 'GICv3 scheduler: preemptions=(\d+) context-switches=(\d+) task-A=(\d+) task-B=(\d+) task-C=(\d+)')
    if (!$scheduler.Success) { throw "$Name lacks guest scheduler task progress counters" }
    if ([uint64]$scheduler.Groups[1].Value -lt 10000 -or
        [uint64]$scheduler.Groups[2].Value -eq 0 -or
        [uint64]$scheduler.Groups[3].Value -eq 0 -or
        [uint64]$scheduler.Groups[4].Value -eq 0 -or
        [uint64]$scheduler.Groups[5].Value -eq 0) {
        throw "$Name lacks timer-driven preemption or progress from two scheduler tasks"
    }
    if ($Text -notmatch 'unexpected-irq=0 exceptions=0') { throw "$Name reports an unexpected IRQ or synchronous exception" }
    $vfs = [regex]::Match($Text, 'scheduler/VFS integration: PASS reads=(\d+) enumerations=(\d+)')
    if (!$vfs.Success -or [uint64]$vfs.Groups[1].Value -eq 0 -or [uint64]$vfs.Groups[2].Value -eq 0) {
        throw "$Name lacks successful Phase 4 VFS read and enumeration evidence"
    }
    $durability = [regex]::Match($Text, 'memory/VFS durability: PASS .* ticks=(\d+) context-switches=(\d+) preemptions=(\d+) vfs-reads=(\d+) vfs-enumerations=(\d+) unexpected-irq=(\d+) exceptions=(\d+)')
    if (!$durability.Success -or [uint64]$durability.Groups[1].Value -lt 10000 -or
        [uint64]$durability.Groups[3].Value -lt 10000 -or
        [uint64]$durability.Groups[4].Value -eq 0 -or [uint64]$durability.Groups[5].Value -eq 0 -or
        [uint64]$durability.Groups[6].Value -ne 0 -or [uint64]$durability.Groups[7].Value -ne 0) {
        throw "$Name lacks 10,000 timer/preemption, VFS, and zero-exception Phase 4 evidence"
    }
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
    $before = Get-QacHealth "$Name pre-admission"
    $baselineProcesses = @(Get-QemuProcesses | ForEach-Object { [int]$_.ProcessId })
    $initialVarsSha256 = (Get-FileHash -LiteralPath $VarsPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $varsTemplateSha256 = (Get-FileHash -LiteralPath $FirmwareVarsTemplate -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($initialVarsSha256 -ne $varsTemplateSha256) {
        throw "$Name firmware variable state is not a fresh copy of the accepted template."
    }
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
    $ownedChild = $null
    $ownershipFailure = ''
    while ([DateTimeOffset]::UtcNow -lt $admissionDeadline) {
        if ($process.HasExited) { break }
        $snapshot = & $QacCliPath list 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) {
            $reservationSnapshot = Find-QacRunReservation $snapshot $Project $process.Id
            if ($reservationSnapshot.Success) {
                $reservationId = $reservationSnapshot.Groups[1].Value
                $reservationState = $reservationSnapshot.Groups[2].Value.Trim()
                if ($reservationState -eq 'Running') {
                    try {
                        $ownedChild = Wait-QacOwnedGuest $process.Id ([IO.Path]::GetFullPath($QemuPath)) $Machine $Cpu "$Name exclusive launch"
                        $launched = $true
                        break
                    } catch {
                        $ownershipFailure = $_.Exception.Message
                        break
                    }
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
                    try {
                        $ownedChild = Wait-QacOwnedGuest $process.Id ([IO.Path]::GetFullPath($QemuPath)) $Machine $Cpu "$Name exclusive launch"
                        $launched = $true
                    } catch {
                        $ownershipFailure = $_.Exception.Message
                    }
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
                    if ($reservationState -eq 'Running') {
                        try {
                            $ownedChild = Wait-QacOwnedGuest $process.Id ([IO.Path]::GetFullPath($QemuPath)) $Machine $Cpu "$Name exclusive launch"
                            $launched = $true
                            break
                        } catch {
                            $ownershipFailure = $_.Exception.Message
                            break
                        }
                    }
                }
            }
            Start-Sleep -Milliseconds 250
        }
    }

    if ($ownershipFailure) {
        $matchingChildren = @(Get-QemuProcesses | Where-Object {
            [int]$_.ParentProcessId -eq $process.Id -and
            [string]::Equals([string]$_.ExecutablePath, [IO.Path]::GetFullPath($QemuPath), [StringComparison]::OrdinalIgnoreCase) -and
            [regex]::IsMatch([string]$_.CommandLine,
                '(?:^|\s)"?-qmp"?\s+"?tcp:127\.0\.0\.1:' + $qmpPort + '(?:,|"|\s)')
        })
        if ($matchingChildren.Count -eq 1) {
            if (!(Send-QmpQuit $qmpPort)) {
                $null = Stop-QacOwnedChildIfQmpFails $process.Id ([IO.Path]::GetFullPath($QemuPath))
            }
            if (!$process.WaitForExit(15000)) {
                throw "$Name ownership anomaly could not be cleaned up by QAC; reservation remains under QAC recovery. $ownershipFailure"
            }
        }
        throw "$Name failed closed on QAC/QEMU ownership verification: $ownershipFailure"
    }

    if (!$launched -and !$process.HasExited) {
        throw "$Name exceeded the bounded $AdmissionTimeoutSeconds-second QAC admission wait. QAC reservation state is $reservationState."
    }

    $timedOut = $false
    $passSeen = $false
    $guestText = ''
    $shutdownNote = ''
    $runtimeOwnershipFailure = ''
    $nextOwnershipCheck = [DateTimeOffset]::UtcNow
    if ($launched) {
        $deadline = [DateTimeOffset]::UtcNow.AddSeconds($RuntimeSeconds)
        while ([DateTimeOffset]::UtcNow -lt $deadline) {
            $guestText = Read-GuestLog $GuestLogPath
            if ($guestText.Contains($PassMarker)) { $passSeen = $true; break }
            if ($guestText -match 'AARCH64_PHASE[234]_ERROR\b|\[guideXOS\].*(FAIL\b|FATAL\b|ERROR\b)|\[A64 UEFI\] ERROR\b') { break }
            if ($process.HasExited) { break }
            if ([DateTimeOffset]::UtcNow -ge $nextOwnershipCheck) {
                try {
                    $activeQacHealth = Get-QacHealth "$Name active guest"
                    $activeReservation = Find-QacRunReservation $activeQacHealth.List $Project $process.Id
                    if (!$activeReservation.Success -or $activeReservation.Groups[2].Value.Trim() -ne 'Running') {
                        throw "$Name QAC no longer reports its reservation as Running."
                    }
                    $activeChild = Assert-QacOwnedGuest $process.Id ([IO.Path]::GetFullPath($QemuPath)) $Machine $Cpu "$Name active guest"
                    if ([int]$activeChild.ProcessId -ne [int]$ownedChild.ProcessId) {
                        throw "$Name active QAC child changed from PID $($ownedChild.ProcessId) to PID $($activeChild.ProcessId)."
                    }
                } catch {
                    $runtimeOwnershipFailure = $_.Exception.Message
                    break
                }
                $nextOwnershipCheck = [DateTimeOffset]::UtcNow.AddSeconds(5)
            }
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
    $remainingQemu = Get-QemuProcesses
    if (@($remainingQemu | Where-Object { [int]$_.ProcessId -eq [int]$ownedChild.ProcessId }).Count -ne 0) {
        throw "$Name QAC child PID $($ownedChild.ProcessId) remained after QAC reported exit."
    }
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
    if ($qacText -notmatch "QAC exclusive reservation granted: id=$reservationId" -or
        $qacText -notmatch "QAC QEMU launched: reservation=$reservationId" -or
        $qacText -notmatch "QAC reservation released: id=$reservationId") {
        if ($qacText -notmatch "QAC exclusive reservation granted: id=$reservationId") {
            throw "$Name QAC did not record an exclusive grant for reservation $reservationId."
        }
        if ($qacText -notmatch "QAC QEMU launched: reservation=$reservationId") {
            $error = [InvalidOperationException]::new("$Name QAC reservation did not launch an authorized QEMU child: $qacText")
            $error.Data['QacAdmissionFailed'] = $true
            throw $error
        }
        throw "$Name QAC did not verify release for reservation $reservationId."
    }
    $degradedOwnership = $qacText -match '(?i)exclusive.{0,40}degraded|foreign QEMU process'
    $after = $null
    $postCleanupFailure = ''
    try {
        $after = Get-QacHealth "$Name post-cleanup"
        if ($after.List -match [regex]::Escape($reservationId)) {
            $postCleanupFailure = "$Name QAC reservation $reservationId remained listed after cleanup."
        }
    } catch {
        $postCleanupFailure = $_.Exception.Message
    }
    $sessionRecord.qac_attempts += [ordered]@{
        name = $Name
        project = $Project
        reservation_id = $reservationId
        qac_log = $QacLogPath
        guest_log = $GuestLogPath
        qac_process_id = $process.Id
        managed_qemu_child_pid = if ($ownedChild) { [int]$ownedChild.ProcessId } else { $null }
        managed_qemu_child_parent_pid = if ($ownedChild) { [int]$ownedChild.ParentProcessId } else { $null }
        qemu_process_ids_before_admission = $baselineProcesses
        qemu_process_count_after_cleanup = $remainingQemu.Count
        exclusive_grant_verified = $qacText -match "QAC exclusive reservation granted: id=$reservationId"
        managed_child_launch_verified = $qacText -match "QAC QEMU launched: reservation=$reservationId"
        child_exit_verified = $qacText -match "QAC QEMU exited: reservation=$reservationId"
        reservation_release_verified = $qacText -match "QAC reservation released: id=$reservationId"
        degraded_ownership_observed = $degradedOwnership
        guest_pass_marker = $passSeen
        guest_exit_code = $exitCode
        runtime_timeout = $timedOut
        firmware_vars_path = [IO.Path]::GetFullPath($VarsPath)
        firmware_vars_initial_sha256 = $initialVarsSha256
        firmware_vars_template_sha256 = $varsTemplateSha256
        firmware_vars_after_sha256 = (Get-FileHash -LiteralPath $VarsPath -Algorithm SHA256).Hash.ToLowerInvariant()
        qac_status_before = $before.Status
        qac_list_before = $before.List
        qac_status_after_cleanup = if ($after) { $after.Status } else { $null }
        qac_list_after_cleanup = if ($after) { $after.List } else { $null }
        post_cleanup_failure = $postCleanupFailure
    }
    Save-SessionRecord
    if ($degradedOwnership) {
        throw "$Name QAC reported degraded exclusive ownership. See $QacLogPath."
    }
    if ($postCleanupFailure) { throw $postCleanupFailure }
    if ($runtimeOwnershipFailure) {
        throw "$Name failed closed after a runtime QAC/QEMU ownership anomaly: $runtimeOwnershipFailure"
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
        VarsPath = [IO.Path]::GetFullPath($VarsPath)
        InitialVarsSha256 = $initialVarsSha256
        VarsTemplateSha256 = $varsTemplateSha256
        VarsSha256 = (Get-FileHash -LiteralPath $VarsPath -Algorithm SHA256).Hash.ToLowerInvariant()
        BaselineQemuProcessIds = $baselineProcesses
        QacProcessId = $process.Id
        QemuProcessId = [int]$ownedChild.ProcessId
        QemuParentProcessId = [int]$ownedChild.ParentProcessId
        QemuExecutablePath = [string]$ownedChild.ExecutablePath
        QemuCommandLine = [string]$ownedChild.CommandLine
        QacStatusBefore = $before.Status
        QacListBefore = $before.List
        QacStatusAfterCleanup = $after.Status
        QacListAfterCleanup = $after.List
        QemuProcessCountAfterCleanup = $remainingQemu.Count
        QacChildExitVerified = $true
        ReservationReleaseVerified = $true
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
        vars_path = $Run.VarsPath
        fresh_vars_sha256_at_start = $Run.InitialVarsSha256
        firmware_vars_template_sha256 = $Run.VarsTemplateSha256
        vars_sha256_after_guest = $Run.VarsSha256
        qemu_process_ids_before_admission = $Run.BaselineQemuProcessIds
        qac_process_id = $Run.QacProcessId
        managed_qemu_child_pid = $Run.QemuProcessId
        managed_qemu_child_parent_pid = $Run.QemuParentProcessId
        managed_qemu_executable = $Run.QemuExecutablePath
        managed_qemu_command_line = $Run.QemuCommandLine
        qac_child_exit_verified = $Run.QacChildExitVerified
        qac_exclusive_grant_verified = $true
        reservation_release_verified = $Run.ReservationReleaseVerified
        qac_status_before = $Run.QacStatusBefore
        qac_list_before = $Run.QacListBefore
        qac_status_after_cleanup = $Run.QacStatusAfterCleanup
        qac_list_after_cleanup = $Run.QacListAfterCleanup
        qemu_process_count_after_cleanup = $Run.QemuProcessCountAfterCleanup
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
    $sessionRecord.outcome = if ($gicv3Runs.Count -eq 0) { 'GICV3_QAC_PENDING' } else { 'GICV3_NATIVE_PARTIAL' }
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

function Save-Gicv3Failure([string]$Stage, [string]$Reason, [string]$GuestLogPath) {
    $sessionRecord.outcome = 'GICV3_NATIVE_PARTIAL'
    $sessionRecord.failure_stage = $Stage
    $sessionRecord.failure_guest_stage = $Stage
    if ($GuestLogPath -and (Test-Path -LiteralPath $GuestLogPath -PathType Leaf)) {
        $guestText = Read-GuestLog $GuestLogPath
        $failureLine = [regex]::Match($guestText, '(?m)^\[guideXOS\] ([^\r\n]*(?:FAIL|FATAL|ERROR)\b[^\r\n]*)')
        if ($failureLine.Success) { $sessionRecord.failure_guest_stage = $failureLine.Groups[1].Value.Trim() }
        $sessionRecord.failure_guest_log = $GuestLogPath
    }
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

$candidateManifest = Write-CandidateManifest
$sessionRecord.candidate_manifest = [ordered]@{
    path = $candidateManifest.Path
    sha256 = $candidateManifest.Sha256
    source_head = $candidateManifest.Data.source.head
    source_branch = $candidateManifest.Data.source.branch
    artifacts = $candidateManifest.Data.artifacts
}
Save-SessionRecord

if ($qacPreflightFailure) {
    $sessionRecord.outcome = 'GICV3_QAC_PENDING'
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
    Save-Gicv3Failure $diagnosticName $_.Exception.Message $diagnosticGuestLog
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
        Save-Gicv3Failure $runName $_.Exception.Message $runGuestLog
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
$gicv2BuildInputs = @(& git -C $repoRoot ls-files -- 'kernel/arch/arm64' 'guideXOSBootLoader/aarch64') + @('scripts/build-aarch64-phase2.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Unable to enumerate preserved GICv2 build inputs.' }
$gicv2InputHashes = [ordered]@{}
foreach ($relative in @($gicv2BuildInputs | Sort-Object -Unique)) {
    $sourcePath = Join-Path $repoRoot ($relative -replace '/', '\\')
    $gicv2InputHashes[$relative] = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
}
$gicv2Artifacts = [ordered]@{}
foreach ($relative in @('BOOTAA64.EFI', 'kernel.elf', 'esp/EFI/BOOT/BOOTAA64.EFI', 'esp/kernel.elf')) {
    $gicv2Artifacts[$relative] = Get-FileIdentity (Join-Path $phase2Directory ($relative -replace '/', '\\'))
}
$gicv2Manifest = [ordered]@{
    schemaVersion = 1
    candidate = 'AARCH64 Phase 2 GICv2 architecture/timer regression control'
    source = [ordered]@{
        branch = (& git -C $repoRoot branch --show-current).Trim()
        head = (& git -C $repoRoot rev-parse HEAD).Trim()
        worktree = (& git -C $repoRoot status --short --branch | Out-String).Trim()
        inputSha256 = $gicv2InputHashes
    }
    build = [ordered]@{
        command = 'scripts/build-aarch64-phase2.ps1 -OutputDirectory <candidate>'
        profile = 'freestanding AArch64 Phase 2; production DTB controller selector; GICv2 regression control'
        qemuMachine = 'virt,gic-version=2,acpi=off'
        qemuCpu = 'cortex-a53'
        llvmRoot = $LlvmRoot
    }
    artifacts = $gicv2Artifacts
}
$gicv2ManifestPath = Join-Path $phase2Directory 'candidate-manifest.json'
$gicv2Manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $gicv2ManifestPath -Encoding utf8
$sessionRecord.gicv2_candidate_manifest = [ordered]@{
    path = $gicv2ManifestPath
    sha256 = (Get-FileHash -LiteralPath $gicv2ManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    artifacts = $gicv2Artifacts
}
Save-SessionRecord
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
    if ($gicv2Text -match 'AARCH64_PHASE2_ERROR\b|\[guideXOS\].*(FAIL\b|FATAL\b|ERROR\b)|\[A64 UEFI\] ERROR\b') {
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
$sessionRecord.outcome = 'GICV3_NATIVE_REPEATABILITY_VERIFIED'
Save-SessionRecord

Write-Host 'AARCH64_GICV3_QEMU_PASS: diagnostic plus three fresh QAC-authorized Cortex-A53 GICv3 boots passed timer, EOI, scheduler, and exception-return acceptance.' -ForegroundColor Green
Write-Host 'Outcome: GICV3_NATIVE_REPEATABILITY_VERIFIED' -ForegroundColor Green
if ($gicv2Pending) {
    Write-Host 'GICV2_GUEST_PENDING_QAC' -ForegroundColor Yellow
} else {
    Write-Host 'AARCH64_GICV2_QEMU_REGRESSION_PASS' -ForegroundColor Green
}
Write-Host "Guest logs: $logsDirectory" -ForegroundColor DarkGray
exit 0
