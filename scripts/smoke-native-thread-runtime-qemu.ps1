[CmdletBinding()]
param(
    [string]$QemuPath = "",
    [string]$QemuImgPath = "",
    [int]$TimeoutSeconds = 60,
    [switch]$SkipBuild,
    [string]$OutputRoot = "",
    [string]$NormalKernelPath = "",
    [string]$TestKernelPath = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $Root "out\runtime\native-thread-qemu-validation"
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$RunId = Get-Date -Format "yyyyMMdd-HHmmss"
$RunRoot = Join-Path $OutputRoot ("smoke-" + $RunId)
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

function Find-Executable {
    param(
        [string]$Explicit,
        [string]$EnvironmentName,
        [string]$CommandName,
        [string[]]$CommonPaths
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) { $candidates.Add($Explicit) }
    $environmentValue = [Environment]::GetEnvironmentVariable($EnvironmentName)
    if (-not [string]::IsNullOrWhiteSpace($environmentValue)) { $candidates.Add($environmentValue) }
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) { $candidates.Add($command.Source) }
    foreach ($path in $CommonPaths) { $candidates.Add($path) }

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Quote-QemuValue {
    param([string]$Value)
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-Make {
    param(
        [string]$Make,
        [string]$LogPath,
        [string[]]$ExtraArguments = @()
    )

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    Push-Location $Root
    try {
        & $Make -C kernel ARCH=amd64 clean 2>&1 | Tee-Object -FilePath $LogPath
        if ($LASTEXITCODE -ne 0) { throw "Kernel clean failed with exit code $LASTEXITCODE." }
        if ($ExtraArguments.Count -eq 0) {
            & $Make -C kernel ARCH=amd64 2>&1 | Tee-Object -FilePath $LogPath -Append
        }
        else {
            & $Make -C kernel ARCH=amd64 ("EXTRA_CFLAGS=" + ($ExtraArguments -join " ")) 2>&1 |
                Tee-Object -FilePath $LogPath -Append
        }
        if ($LASTEXITCODE -ne 0) { throw "Kernel build failed with exit code $LASTEXITCODE." }
    }
    finally {
        Pop-Location
        $ErrorActionPreference = $previousErrorActionPreference
    }
}

function Stage-Esp {
    param(
        [string]$EspPath,
        [string]$KernelPath,
        [string]$BootloaderPath,
        [string]$RamdiskPath
    )

    New-Item -ItemType Directory -Force -Path (Join-Path $EspPath "EFI\BOOT") | Out-Null
    Copy-Item -LiteralPath $BootloaderPath -Destination (Join-Path $EspPath "EFI\BOOT\BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $EspPath "kernel.elf") -Force
    Copy-Item -LiteralPath $RamdiskPath -Destination (Join-Path $EspPath "ramdisk.img") -Force
}

function Invoke-QemuBoot {
    param(
        [string]$EspPath,
        [string]$SerialPath,
        [string]$DebugPath,
        [string]$StdoutPath,
        [string]$StderrPath,
        [string]$MarkerPattern
    )

    $driveFirmware = "if=pflash,format=raw,readonly=on,file=$(Quote-QemuValue $OvmfPath)"
    $driveEsp = "file=fat:rw:$(Quote-QemuValue $EspPath),format=raw,if=ide,index=0"
    $serial = "file:$(Quote-QemuValue $SerialPath)"
    $arguments = @(
        "-accel", "tcg,thread=single",
        "-machine", "pc",
        "-drive", $driveFirmware,
        "-drive", $driveEsp,
        "-m", "1024M",
        "-vga", "std",
        "-display", "none",
        "-serial", $serial,
        "-no-reboot",
        "-no-shutdown",
        "-rtc", "base=utc,clock=host",
        "-d", "int,cpu_reset",
        "-D", (Quote-QemuValue $DebugPath)
    )

    $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments `
        -WorkingDirectory $Root -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $markerFound = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path -LiteralPath $SerialPath) {
            $serialText = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
            if ($serialText -match $MarkerPattern) {
                $markerFound = $true
                break
            }
        }
        if ($process.HasExited) { break }
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $process.WaitForExit(5000) | Out-Null
    }
    $process.Refresh()
    return [pscustomobject]@{
        MarkerFound = $markerFound
        Exited = $process.HasExited
        ExitCode = $process.ExitCode
        SerialPath = $SerialPath
        DebugPath = $DebugPath
        StdoutPath = $StdoutPath
        StderrPath = $StderrPath
    }
}

$qemuCommon = @(
    "C:\Program Files\qemu\qemu-system-x86_64.exe",
    "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
    "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
    "C:\qemu\qemu-system-x86_64.exe",
    "$env:USERPROFILE\qemu\qemu-system-x86_64.exe",
    "C:\msys64\mingw64\bin\qemu-system-x86_64.exe",
    "C:\msys64\usr\bin\qemu-system-x86_64.exe"
)
$qemuImgCommon = @(
    "C:\Program Files\qemu\qemu-img.exe",
    "C:\Program Files (x86)\qemu\qemu-img.exe",
    "$env:LOCALAPPDATA\Programs\qemu\qemu-img.exe",
    "C:\qemu\qemu-img.exe",
    "$env:USERPROFILE\qemu\qemu-img.exe",
    "C:\msys64\mingw64\bin\qemu-img.exe",
    "C:\msys64\usr\bin\qemu-img.exe"
)
$QemuPath = Find-Executable $QemuPath "GXOS_QEMU_X64" "qemu-system-x86_64.exe" $qemuCommon
$QemuImgPath = Find-Executable $QemuImgPath "GXOS_QEMU_IMG" "qemu-img.exe" $qemuImgCommon
$ovmfCandidates = @(
    (Join-Path $Root "OVMF.fd"),
    (Join-Path $Root "ovmf.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
)
$OvmfPath = $null
foreach ($candidate in $ovmfCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $OvmfPath = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}
$bootloaderPath = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$ramdiskPath = Join-Path $Root "ESP\ramdisk.img"
$make = Find-Executable "" "GXOS_MINGW32_MAKE" "mingw32-make.exe" @(
    "C:\mingw64\bin\mingw32-make.exe",
    "C:\msys64\mingw64\bin\mingw32-make.exe"
)

$qemuLocated = ($null -ne $QemuPath -and $null -ne $QemuImgPath -and
    $null -ne $OvmfPath -and (Test-Path -LiteralPath $bootloaderPath) -and
    (Test-Path -LiteralPath $ramdiskPath) -and $null -ne $make)
if (-not $qemuLocated) {
    Write-Host "QEMU located: FAIL"
    Write-Host "QEMU path is not on PATH; use -QemuPath or GXOS_QEMU_X64 when needed."
    exit 1
}

$qemuVersion = (& $QemuPath --version 2>&1 | Out-String).Trim()
$qemuImgVersion = (& $QemuImgPath --version 2>&1 | Out-String).Trim()
$qemuHash = (Get-FileHash -LiteralPath $QemuPath -Algorithm SHA256).Hash
$qemuImgHash = (Get-FileHash -LiteralPath $QemuImgPath -Algorithm SHA256).Hash
$identityPath = Join-Path $RunRoot "qemu-identity.txt"
@(
    "QEMU: $QemuPath",
    "QEMU version: $qemuVersion",
    "QEMU SHA256: $qemuHash",
    "QEMU architecture: AMD64 system emulator",
    "qemu-img: $QemuImgPath",
    "qemu-img version: $qemuImgVersion",
    "qemu-img SHA256: $qemuImgHash",
    "Accelerator: TCG, thread=single",
    "Serial: headless -serial file:<run>\\*.serial.log",
    "Firmware: $OvmfPath"
) | Set-Content -LiteralPath $identityPath
Write-Host "QEMU located: PASS"
Write-Host "QEMU path: $QemuPath"
Write-Host "QEMU version: $qemuVersion"
Write-Host "QEMU SHA256: $qemuHash"

$normalKernel = if ([string]::IsNullOrWhiteSpace($NormalKernelPath)) {
    Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
} else {
    (Resolve-Path -LiteralPath $NormalKernelPath).Path
}
$buildLog = Join-Path $RunRoot "build.log"
if (-not $SkipBuild) {
    Invoke-Make $make $buildLog
}
if (-not (Test-Path -LiteralPath $normalKernel -PathType Leaf)) {
    throw "Normal kernel image was not produced: $normalKernel"
}
$baselineEsp = Join-Path $RunRoot "baseline-ESP"
Stage-Esp $baselineEsp $normalKernel $bootloaderPath $ramdiskPath
$baselineResult = Invoke-QemuBoot $baselineEsp `
    (Join-Path $RunRoot "baseline-serial.log") `
    (Join-Path $RunRoot "baseline-qemu-debug.log") `
    (Join-Path $RunRoot "baseline-qemu.stdout.log") `
    (Join-Path $RunRoot "baseline-qemu.stderr.log") `
    "\[KERNEL\] Entering main loop \(waiting for input\)"
if (-not $baselineResult.MarkerFound) {
    Write-Host "Baseline boot: FAIL"
    Write-Host "Baseline serial: $($baselineResult.SerialPath)"
    exit 1
}
Write-Host "Baseline boot: PASS"

if (-not $SkipBuild) {
    Invoke-Make $make $buildLog @("-DGXOS_NATIVE_THREAD_QEMU_TEST")
}
$testKernel = if ([string]::IsNullOrWhiteSpace($TestKernelPath)) {
    Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
} else {
    (Resolve-Path -LiteralPath $TestKernelPath).Path
}
if (-not (Test-Path -LiteralPath $testKernel -PathType Leaf)) {
    throw "Native-thread test kernel image was not produced: $testKernel"
}
$testEsp = Join-Path $RunRoot "test-ESP"
Stage-Esp $testEsp $testKernel $bootloaderPath $ramdiskPath
$testResult = Invoke-QemuBoot $testEsp `
    (Join-Path $RunRoot "native-thread-serial.log") `
    (Join-Path $RunRoot "native-thread-qemu-debug.log") `
    (Join-Path $RunRoot "test-qemu.stdout.log") `
    (Join-Path $RunRoot "test-qemu.stderr.log") `
    "\[native-thread-test\] (ALL_PASS|ALL_FAIL)"

$summary = [ordered]@{}
$summary["QEMU located"] = "PASS"
$summary["Baseline boot"] = if ($baselineResult.MarkerFound) { "PASS" } else { "FAIL" }
$summary["Single worker"] = "FAIL"
$summary["Context delivery"] = "FAIL"
$summary["Result capture"] = "FAIL"
$summary["Join-before-exit"] = "FAIL"
$summary["Join-after-exit"] = "FAIL"
$summary["Zero-timeout join"] = "FAIL"
$summary["Finite-timeout join"] = "FAIL"
$summary["Join retry"] = "FAIL"
$summary["TCB reuse"] = "FAIL"
$summary["Generation change"] = "FAIL"
$summary["Stale-handle rejection"] = "FAIL"
$summary["Detach before exit"] = "FAIL"
$summary["Detach after exit"] = "FAIL"
$summary["Multiple workers"] = "FAIL"
$summary["Wait/timer cleanup"] = "FAIL"
$summary["Process teardown"] = "BLOCKED"
$summary["Stack/TCB leak check"] = "FAIL"
$summary["Bootstrap bounds available"] = "FAIL"
$summary["Bootstrap RSP inside bounds"] = "FAIL"
$summary["Bootstrap bounds exact-source validation"] = "FAIL"
$summary["Initial-thread exact stack bounds"] = "FAIL"
$summary["Initial RSP validation"] = "FAIL"
$summary["Worker bounds available"] = "FAIL"
$summary["Worker RSP inside bounds"] = "FAIL"
$summary["Bounds valid during local-storage detach"] = "FAIL"
$summary["Bounds invalidated before TCB reuse"] = "FAIL"
$summary["TCB reuse receives new valid bounds"] = "FAIL"

$serialText = if (Test-Path -LiteralPath $testResult.SerialPath) {
    Get-Content -LiteralPath $testResult.SerialPath -Raw
} else { "" }
foreach ($name in @($summary.Keys | Select-Object -Skip 2)) {
    $pattern = "\[native-thread-test\]\s+" + [regex]::Escape($name) + ":\s+(PASS|FAIL)"
    $matches = [regex]::Matches($serialText, $pattern)
    if ($matches.Count -gt 0) {
        $values = @($matches | ForEach-Object { $_.Groups[1].Value })
        $summary[$name] = if ($values -contains "FAIL") { "FAIL" } else { "PASS" }
    }
}

foreach ($entry in $summary.GetEnumerator()) {
    Write-Host ("{0}: {1}" -f $entry.Key, $entry.Value)
}
Write-Host "Serial log: $($testResult.SerialPath)"
Write-Host "Fault/debug log: $($testResult.DebugPath)"
Write-Host "Artifacts: $RunRoot"

$failed = @($summary.Values | Where-Object { $_ -eq "FAIL" -or $_ -eq "BLOCKED" })
$testMarkerStatus = if ($testResult.MarkerFound) { "PASS" } else { "FAIL" }
Write-Host "Test marker: $testMarkerStatus"
Write-Host "Summary failures: $($failed.Count)"
if (-not $testResult.MarkerFound -or $failed.Count -ne 0) {
    exit 1
}
exit 0
