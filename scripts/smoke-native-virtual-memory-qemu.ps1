[CmdletBinding()]
param(
    [string]$QemuPath = '',
    [int]$TimeoutSeconds = 60,
    [switch]$SkipBuild,
    [string]$OutputRoot = '',
    [string]$NormalKernelPath = '',
    [string]$TestKernelPath = ''
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $Root 'out\runtime\native-virtual-memory-qemu'
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$RunRoot = Join-Path $OutputRoot ('smoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

function Find-Executable([string]$Explicit, [string]$EnvironmentName, [string]$CommandName, [string[]]$CommonPaths) {
    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) { $candidates.Add($Explicit) }
    $environmentValue = [Environment]::GetEnvironmentVariable($EnvironmentName)
    if (-not [string]::IsNullOrWhiteSpace($environmentValue)) { $candidates.Add($environmentValue) }
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) { $candidates.Add($command.Source) }
    foreach ($path in $CommonPaths) { $candidates.Add($path) }
    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Quote-QemuValue([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-Make([string]$Make, [string]$LogPath, [string[]]$ExtraArguments = @()) {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    Push-Location $Root
    try {
        & $Make -C kernel ARCH=amd64 clean 2>&1 | Tee-Object -FilePath $LogPath
        if ($LASTEXITCODE -ne 0) { throw 'Kernel clean failed.' }
        if ($ExtraArguments.Count -eq 0) {
            & $Make -C kernel ARCH=amd64 -j2 2>&1 | Tee-Object -FilePath $LogPath -Append
        } else {
            & $Make -C kernel ARCH=amd64 -j2 ("EXTRA_CFLAGS=" + ($ExtraArguments -join ' ')) 2>&1 |
                Tee-Object -FilePath $LogPath -Append
        }
        if ($LASTEXITCODE -ne 0) { throw 'Kernel build failed.' }
    }
    finally {
        Pop-Location
        $ErrorActionPreference = $previousErrorActionPreference
    }
}

function Stage-Esp([string]$EspPath, [string]$KernelPath, [string]$BootloaderPath, [string]$RamdiskPath) {
    New-Item -ItemType Directory -Force -Path (Join-Path $EspPath 'EFI\BOOT') | Out-Null
    Copy-Item -LiteralPath $BootloaderPath -Destination (Join-Path $EspPath 'EFI\BOOT\BOOTX64.EFI') -Force
    Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $EspPath 'kernel.elf') -Force
    Copy-Item -LiteralPath $RamdiskPath -Destination (Join-Path $EspPath 'ramdisk.img') -Force
}

function Invoke-QemuBoot([string]$EspPath, [string]$SerialPath, [string]$DebugPath,
                         [string]$StdoutPath, [string]$StderrPath, [string]$MarkerPattern) {
    $arguments = @(
        '-accel', 'tcg,thread=single', '-machine', 'pc',
        '-drive', ('if=pflash,format=raw,readonly=on,file=' + (Quote-QemuValue $OvmfPath)),
        '-drive', ('file=fat:rw:' + (Quote-QemuValue $EspPath) + ',format=raw,if=ide,index=0'),
        '-m', '1024M', '-vga', 'std', '-display', 'none',
        '-serial', ('file:' + (Quote-QemuValue $SerialPath)),
        '-no-reboot', '-no-shutdown', '-rtc', 'base=utc,clock=host',
        '-d', 'int,cpu_reset', '-D', (Quote-QemuValue $DebugPath)
    )
    $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $Root `
        -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $markerFound = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path -LiteralPath $SerialPath) {
            $text = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
            if ($text -match $MarkerPattern) { $markerFound = $true; break }
        }
        if ($process.HasExited) { break }
    }
    $liveProcess = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
    if ($null -ne $liveProcess) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $stopDeadline = (Get-Date).AddSeconds(2)
        while ((Get-Date) -lt $stopDeadline -and
               $null -ne (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
            Start-Sleep -Milliseconds 100
        }
    }
    return [pscustomobject]@{ MarkerFound = $markerFound; SerialPath = $SerialPath; DebugPath = $DebugPath }
}

$QemuPath = Find-Executable $QemuPath 'GXOS_QEMU_X64' 'qemu-system-x86_64.exe' @(
    'C:\Program Files\qemu\qemu-system-x86_64.exe',
    'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
    "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
    'C:\qemu\qemu-system-x86_64.exe', "$env:USERPROFILE\qemu\qemu-system-x86_64.exe",
    'C:\msys64\mingw64\bin\qemu-system-x86_64.exe'
)
$make = Find-Executable '' 'GXOS_MINGW32_MAKE' 'mingw32-make.exe' @(
    'C:\mingw64\bin\mingw32-make.exe', 'C:\msys64\mingw64\bin\mingw32-make.exe'
)
$ovmfCandidates = @(
    (Join-Path $Root 'OVMF.fd'), (Join-Path $Root 'ovmf.fd'),
    'C:\Program Files\qemu\share\edk2-x86_64-code.fd'
)
$OvmfPath = $null
foreach ($candidate in $ovmfCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $OvmfPath = (Resolve-Path $candidate).Path; break }
}
$bootloaderPath = Join-Path $Root 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$ramdiskPath = Join-Path $Root 'ESP\ramdisk.img'
$buildLog = Join-Path $RunRoot 'build.log'
$baselineBuild = if ($SkipBuild) { 'SKIPPED' } else { 'FAIL' }
if (-not $SkipBuild) {
    if ($null -ne $make) {
        try {
            Invoke-Make $make $buildLog
            $baselineBuild = 'PASS'
        } catch {
            Write-Host ('Baseline kernel build error: ' + $_.Exception.Message)
        }
    } else {
        Write-Host 'Baseline kernel build error: mingw32-make.exe not found'
    }
}
Write-Host ('Kernel build (baseline): ' + $baselineBuild)
$normalKernel = if ([string]::IsNullOrWhiteSpace($NormalKernelPath)) {
    Join-Path $Root 'kernel\build\amd64\bin\kernel.elf'
} else { (Resolve-Path $NormalKernelPath).Path }
$baselineKernelSnapshot = Join-Path $RunRoot 'baseline-kernel.elf'
if (Test-Path $normalKernel) {
    Copy-Item -LiteralPath $normalKernel -Destination $baselineKernelSnapshot -Force
}
$baseline = [pscustomobject]@{ MarkerFound = $false; SerialPath = ''; DebugPath = '' }

$testBuild = if ($SkipBuild) { 'SKIPPED' } else { 'FAIL' }
if (-not $SkipBuild) {
    if ($null -ne $make) {
        try {
            Invoke-Make $make $buildLog @('-DGXOS_NATIVE_VIRTUAL_MEMORY_QEMU_TEST')
            $testBuild = 'PASS'
        } catch {
            Write-Host ('Native VM kernel build error: ' + $_.Exception.Message)
        }
    } else {
        Write-Host 'Native VM kernel build error: mingw32-make.exe not found'
    }
}
Write-Host ('Kernel build (true VM test): ' + $testBuild)
$testKernel = if ([string]::IsNullOrWhiteSpace($TestKernelPath)) {
    Join-Path $Root 'kernel\build\amd64\bin\kernel.elf'
} else { (Resolve-Path $TestKernelPath).Path }
$test = [pscustomobject]@{ MarkerFound = $false; SerialPath = ''; DebugPath = '' }

$qemuReady = $null -ne $QemuPath -and $null -ne $OvmfPath -and
    (Test-Path $bootloaderPath) -and (Test-Path $ramdiskPath) -and
    (Test-Path $normalKernel) -and (Test-Path $testKernel) -and
    ($SkipBuild -or (Test-Path $baselineKernelSnapshot)) -and
    ($SkipBuild -or ($baselineBuild -eq 'PASS' -and $testBuild -eq 'PASS'))
if (-not $qemuReady) {
    Write-Host 'QEMU located: FAIL'
    Write-Host ('Baseline boot: ' + $(if ($SkipBuild) { 'SKIPPED' } elseif ($baselineBuild -eq 'FAIL') { 'BLOCKED (kernel build failed)' } else { 'BLOCKED (QEMU/boot assets unavailable)' }))
    Write-Host ('Native VM ALL_PASS: BLOCKED (QEMU/boot assets unavailable)')
    Write-Host 'Frame allocation/release: BLOCKED'
    Write-Host 'Metadata capacity: BLOCKED'
    Write-Host 'Virtual-range exhaustion: BLOCKED'
    Write-Host 'Protection-fault handling: BLOCKED'
    Write-Host 'Rollback: BLOCKED'
    Write-Host 'Teardown: BLOCKED'
    Write-Host 'TLB invalidation: BLOCKED'
    Write-Host 'No leaks: BLOCKED'
    Write-Host 'Adapter true mode: BLOCKED (run by hosted smoke separately)'
    Write-Host 'Expected direct-read/write behavior: BLOCKED'
    exit 1
}
Write-Host 'QEMU located: PASS'
Write-Host ('QEMU version: ' + ((& $QemuPath --version 2>&1 | Out-String).Trim()))
Write-Host ('QEMU SHA256: ' + (Get-FileHash $QemuPath -Algorithm SHA256).Hash)

if (-not $SkipBuild) {
    $baselineEsp = Join-Path $RunRoot 'baseline-ESP'
    Stage-Esp $baselineEsp $baselineKernelSnapshot $bootloaderPath $ramdiskPath
    $baseline = Invoke-QemuBoot $baselineEsp (Join-Path $RunRoot 'baseline-serial.log') `
        (Join-Path $RunRoot 'baseline-qemu-debug.log') (Join-Path $RunRoot 'baseline.stdout.log') `
        (Join-Path $RunRoot 'baseline.stderr.log') '\[KERNEL\] Entering main loop \(waiting for input\)'
    $baselineStatus = if ($baseline.MarkerFound) { 'PASS' } else { 'FAIL' }
} else {
    $baselineStatus = 'SKIPPED'
}
Write-Host ('Baseline boot: ' + $baselineStatus)

$testEsp = Join-Path $RunRoot 'test-ESP'
Stage-Esp $testEsp $testKernel $bootloaderPath $ramdiskPath
$test = Invoke-QemuBoot $testEsp (Join-Path $RunRoot 'native-virtual-memory-serial.log') `
    (Join-Path $RunRoot 'native-virtual-memory-qemu-debug.log') (Join-Path $RunRoot 'test.stdout.log') `
    (Join-Path $RunRoot 'test.stderr.log') '\[native-virtual-memory-test\] ALL_(PASS|FAIL)'
$serial = if (Test-Path $test.SerialPath) { Get-Content $test.SerialPath -Raw } else { '' }

$summary = [ordered]@{
    'Kernel build (baseline)' = $baselineBuild
    'Kernel build (true VM test)' = $testBuild
    'QEMU located' = 'PASS'
    'Baseline boot' = $baselineStatus
    'Native VM ALL_PASS' = if ($test.MarkerFound -and $serial -match '\[native-virtual-memory-test\] ALL_PASS') { 'PASS' } else { 'FAIL' }
    'Frame allocation/release' = 'FAIL'
    'Metadata capacity' = 'FAIL'
    'Virtual-range exhaustion' = 'FAIL'
    'Protection-fault handling' = 'FAIL'
    'Rollback' = 'FAIL'
    'Teardown' = 'FAIL'
    'TLB invalidation' = 'FAIL'
    'No leaks' = 'FAIL'
    'Adapter true mode' = 'NOT_IN_QEMU (see hosted smoke)'
    'Expected direct-read/write behavior' = 'FAIL'
}

function Test-SerialPass([string[]]$Labels) {
    foreach ($label in $Labels) {
        $match = [regex]::Match($serial, '\[native-virtual-memory-test\]\s+' +
            [regex]::Escape($label) + ':\s+(PASS)')
        if (-not $match.Success) { return $false }
    }
    return $true
}

$summary['Frame allocation/release'] = if (Test-SerialPass @(
    'Partial commit', 'Zero initialization', 'Direct read/write behavior',
    'Decommit releases physical frame', 'Release')) { 'PASS' } else { 'FAIL' }
$summary['Metadata capacity'] = if (Test-SerialPass @('Metadata exhaustion')) { 'PASS' } else { 'FAIL' }
$summary['Virtual-range exhaustion'] = if (Test-SerialPass @('Virtual-range exhaustion')) { 'PASS' } else { 'FAIL' }
$summary['Protection-fault handling'] = if (Test-SerialPass @(
    'Protection transitions', 'Read-only enforcement', 'No-access enforcement',
    'Reserved-uncommitted fault', 'Decommitted-page fault', 'Released-page fault')) { 'PASS' } else { 'FAIL' }
$summary['Rollback'] = if (Test-SerialPass @('Physical-frame exhaustion rollback')) { 'PASS' } else { 'FAIL' }
$summary['Teardown'] = if (Test-SerialPass @('Process/address-space teardown')) { 'PASS' } else { 'FAIL' }
$summary['TLB invalidation'] = if (Test-SerialPass @('TLB invalidation')) { 'PASS' } else { 'FAIL' }
$summary['No leaks'] = if (Test-SerialPass @('Physical-frame leak check', 'Mapping leak check')) { 'PASS' } else { 'FAIL' }
$summary['Expected direct-read/write behavior'] = if (Test-SerialPass @(
    'Direct read/write behavior', 'Read-only enforcement', 'No-access enforcement')) { 'PASS' } else { 'FAIL' }
foreach ($entry in $summary.GetEnumerator()) { Write-Host ("{0}: {1}" -f $entry.Key, $entry.Value) }
Write-Host ('Serial log: ' + $test.SerialPath)
Write-Host ('QEMU debug log: ' + $test.DebugPath)

$blocked = @($summary.Values | Where-Object { $_ -eq 'BLOCKED' }).Count
$failed = @($summary.Values | Where-Object { $_ -eq 'FAIL' }).Count
Write-Host "Blocked checks: $blocked"
Write-Host "Failed checks: $failed"
if (-not $test.MarkerFound -or $failed -ne 0 -or $blocked -ne 0) { exit 1 }
exit 0
