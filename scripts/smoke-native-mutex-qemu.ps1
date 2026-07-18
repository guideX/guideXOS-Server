[CmdletBinding()]
param(
    [string]$QemuPath = "",
    [string]$QemuImgPath = "",
    [int]$TimeoutSeconds = 60,
    [switch]$SkipBuild,
    [switch]$SkipBaseline,
    [string]$OutputRoot = "",
    [string]$NormalKernelPath = "",
    [string]$TestKernelPath = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $Root "out\runtime\native-mutex-qemu-validation"
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$RunRoot = Join-Path $OutputRoot ("smoke-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff") + "-" + (Get-Random -Minimum 1000 -Maximum 10000))
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

function Find-Executable {
    param([string]$Explicit, [string]$EnvironmentName, [string]$CommandName, [string[]]$CommonPaths)
    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) { $candidates.Add($Explicit) }
    $value = [Environment]::GetEnvironmentVariable($EnvironmentName)
    if (-not [string]::IsNullOrWhiteSpace($value)) { $candidates.Add($value) }
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

function Quote-QemuValue { param([string]$Value) return '"' + $Value.Replace('"', '\"') + '"' }

function Invoke-Make {
    param([string]$Make, [string]$LogPath, [string[]]$ExtraArguments = @())
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    Push-Location $Root
    try {
        & $Make -C kernel ARCH=amd64 clean 2>&1 | Tee-Object -FilePath $LogPath
        if ($LASTEXITCODE -ne 0) { throw "Kernel clean failed with exit code $LASTEXITCODE." }
        if ($ExtraArguments.Count -eq 0) {
            & $Make -C kernel ARCH=amd64 2>&1 | Tee-Object -FilePath $LogPath -Append
        } else {
            & $Make -C kernel ARCH=amd64 ("EXTRA_CFLAGS=" + ($ExtraArguments -join " ")) 2>&1 |
                Tee-Object -FilePath $LogPath -Append
        }
        if ($LASTEXITCODE -ne 0) { throw "Kernel build failed with exit code $LASTEXITCODE." }
    } finally {
        Pop-Location
        $ErrorActionPreference = $previousErrorActionPreference
    }
}

function Stage-Esp {
    param([string]$EspPath, [string]$KernelPath, [string]$BootloaderPath, [string]$RamdiskPath)
    New-Item -ItemType Directory -Force -Path (Join-Path $EspPath "EFI\BOOT") | Out-Null
    Copy-Item -LiteralPath $BootloaderPath -Destination (Join-Path $EspPath "EFI\BOOT\BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $EspPath "kernel.elf") -Force
    Copy-Item -LiteralPath $RamdiskPath -Destination (Join-Path $EspPath "ramdisk.img") -Force
}

function Invoke-QemuBoot {
    param([string]$EspPath, [string]$SerialPath, [string]$DebugPath,
          [string]$StdoutPath, [string]$StderrPath, [string]$MarkerPattern)
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $OvmfPath)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $EspPath) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $SerialPath)),
        "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host",
        "-d", "int,cpu_reset", "-D", (Quote-QemuValue $DebugPath)
    )
    $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $Root `
        -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $found = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path -LiteralPath $SerialPath) {
            $text = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
            if ($text -match $MarkerPattern) { $found = $true; break }
        }
        if ($process.HasExited) { break }
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $process.WaitForExit(5000) | Out-Null
    }
    $process.Refresh()
    # The polling match only shortens a successful run.  Recompute the final
    # guest result from this run's complete serial file so partial output or a
    # stale substring cannot satisfy the runner.
    $finalText = if (Test-Path -LiteralPath $SerialPath) {
        Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
    } else { "" }
    $finalMarkerFound = $finalText -match $MarkerPattern
    $guestPass = $finalText -match '(?m)^\[native-mutex-test\]\s+ALL_PASS:\s+PASS\s*$'
    $guestFail = $finalText -match '(?m)^\[native-mutex-test\]\s+ALL_FAIL:\s+FAIL\s*$'
    [pscustomobject]@{
        MarkerFound = [bool]$finalMarkerFound
        GuestPass = [bool]($guestPass -and -not $guestFail)
        GuestFail = [bool]$guestFail
        SerialPath = $SerialPath
        DebugPath = $DebugPath
    }
}

$QemuPath = Find-Executable $QemuPath "GXOS_QEMU_X64" "qemu-system-x86_64.exe" @(
    "C:\Program Files\qemu\qemu-system-x86_64.exe",
    "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
    "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
    "C:\qemu\qemu-system-x86_64.exe", "C:\msys64\mingw64\bin\qemu-system-x86_64.exe")
$QemuImgPath = Find-Executable $QemuImgPath "GXOS_QEMU_IMG" "qemu-img.exe" @(
    "C:\Program Files\qemu\qemu-img.exe",
    "C:\Program Files (x86)\qemu\qemu-img.exe",
    "$env:LOCALAPPDATA\Programs\qemu\qemu-img.exe",
    "C:\qemu\qemu-img.exe", "C:\msys64\mingw64\bin\qemu-img.exe")
$OvmfPath = $null
foreach ($candidate in @(
    (Join-Path $Root "OVMF.fd"), (Join-Path $Root "ovmf.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd")) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $OvmfPath = (Resolve-Path -LiteralPath $candidate).Path; break
    }
}
$make = Find-Executable "" "GXOS_MINGW32_MAKE" "mingw32-make.exe" @(
    "C:\mingw64\bin\mingw32-make.exe", "C:\msys64\mingw64\bin\mingw32-make.exe")
$bootloaderPath = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$ramdiskPath = Join-Path $Root "ESP\ramdisk.img"
if ($null -eq $QemuPath -or $null -eq $QemuImgPath -or $null -eq $OvmfPath -or
    $null -eq $make -or -not (Test-Path -LiteralPath $bootloaderPath) -or
    -not (Test-Path -LiteralPath $ramdiskPath)) {
    Write-Host "QEMU located: FAIL"
    exit 1
}
Write-Host "QEMU located: PASS"
Write-Host "QEMU path: $QemuPath"
Write-Host "QEMU version: $((& $QemuPath --version 2>&1 | Out-String).Trim())"

$normalKernel = if ([string]::IsNullOrWhiteSpace($NormalKernelPath)) {
    Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
} else { (Resolve-Path -LiteralPath $NormalKernelPath).Path }
$buildLog = Join-Path $RunRoot "build.log"
if (-not $SkipBaseline) {
    if (-not $SkipBuild) { Invoke-Make $make $buildLog }
    if (-not (Test-Path -LiteralPath $normalKernel -PathType Leaf)) { throw "Normal kernel image missing." }
    $baselineEsp = Join-Path $RunRoot "baseline-ESP"
    Stage-Esp $baselineEsp $normalKernel $bootloaderPath $ramdiskPath
    $baseline = Invoke-QemuBoot $baselineEsp (Join-Path $RunRoot "baseline.serial.log") `
        (Join-Path $RunRoot "baseline.debug.log") (Join-Path $RunRoot "baseline.stdout.log") `
        (Join-Path $RunRoot "baseline.stderr.log") "\[KERNEL\] Entering main loop \(waiting for input\)"
    Write-Host ("Baseline boot: " + ($(if ($baseline.MarkerFound) { "PASS" } else { "FAIL" })))
    if (-not $baseline.MarkerFound) { exit 1 }
}
else {
    Write-Host "Baseline boot: SKIPPED (separately validated)"
}

if (-not $SkipBuild) {
    # Reuse the existing native-thread diagnostic trace for scheduler lifecycle
    # visibility; main.cpp suppresses that suite when the mutex probe is active.
    Invoke-Make $make $buildLog @(
        "-DGXOS_NATIVE_MUTEX_QEMU_TEST",
        "-DGXOS_NATIVE_THREAD_QEMU_TEST")
}
$testKernel = if ([string]::IsNullOrWhiteSpace($TestKernelPath)) {
    Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
} else { (Resolve-Path -LiteralPath $TestKernelPath).Path }
if (-not (Test-Path -LiteralPath $testKernel -PathType Leaf)) { throw "Mutex test kernel image missing." }
$testEsp = Join-Path $RunRoot "test-ESP"
Stage-Esp $testEsp $testKernel $bootloaderPath $ramdiskPath
$test = Invoke-QemuBoot $testEsp (Join-Path $RunRoot "native-mutex.serial.log") `
    (Join-Path $RunRoot "native-mutex.debug.log") (Join-Path $RunRoot "test.stdout.log") `
    (Join-Path $RunRoot "test.stderr.log") "\[native-mutex-test\] ALL_PASS: PASS"
$serial = if (Test-Path -LiteralPath $test.SerialPath) { Get-Content -LiteralPath $test.SerialPath -Raw } else { "" }
$names = @(
    "Basic acquire/release", "Nonrecursive self-lock", "Mutex lifecycle destroy",
    "Recursive acquire/release", "Try-lock free/contended", "Destroy with waiters",
    "Single contended waiter", "FIFO waiters", "Protected counter", "Wait-node cleanup",
    "Mutex leak check", "Non-owner unlock", "Destroy owned", "Owner-exit diagnostic")
$failed = 0
foreach ($name in $names) {
    $match = [regex]::Match($serial, "\[native-mutex-test\]\s+" + [regex]::Escape($name) + ":\s+(PASS|FAIL)")
    $result = if ($match.Success) { $match.Groups[1].Value } else { "FAIL" }
    if ($result -eq "FAIL") { ++$failed }
    Write-Host ("{0}: {1}" -f $name, $result)
}
$metric = [regex]::Match($serial, "Protected counter: expected=3 observed=([0-9A-Fa-f]+)")
Write-Host ("Protected counter metric: " + ($(if ($metric.Success) { $metric.Value } else { "MISSING" })))
Write-Host ("Guest marker: " + ($(if ($test.GuestPass) { "PASS" } elseif ($test.GuestFail) { "FAIL" } else { "MISSING" })))
Write-Host ("Runner parsed result: " + ($(if ($test.GuestPass -and $failed -eq 0) { "PASS" } else { "FAIL" })))
Write-Host "Serial log: $($test.SerialPath)"
Write-Host "Fault/debug log: $($test.DebugPath)"
Write-Host "Artifacts: $RunRoot"
if (-not $test.GuestPass -or $failed -ne 0) { exit 1 }
exit 0
