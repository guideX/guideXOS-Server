[CmdletBinding()]
param(
    [string]$QemuPath = "",
    [int]$TimeoutSeconds = 60,
    [int]$BaselineTimeoutSeconds = 0,
    [int]$TestTimeoutSeconds = 0,
    [switch]$SkipBaseline,
    [switch]$SkipBuild,
    [string]$OutputRoot = "",
    [string]$NormalKernelPath = "",
    [string]$TestKernelPath = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $root 'out\runtime\native-local-storage-qemu-validation'
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$runRoot = Join-Path $OutputRoot ('smoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + (Get-Random -Minimum 1000 -Maximum 10000))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

function Find-Executable([string]$Explicit, [string]$EnvironmentName, [string]$CommandName, [string[]]$CommonPaths) {
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
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    return $null
}

function Quote-QemuValue([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Stage-Esp([string]$EspPath, [string]$KernelPath, [string]$BootloaderPath, [string]$RamdiskPath) {
    New-Item -ItemType Directory -Force -Path (Join-Path $EspPath 'EFI\BOOT') | Out-Null
    Copy-Item -LiteralPath $BootloaderPath -Destination (Join-Path $EspPath 'EFI\BOOT\BOOTX64.EFI') -Force
    Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $EspPath 'kernel.elf') -Force
    Copy-Item -LiteralPath $RamdiskPath -Destination (Join-Path $EspPath 'ramdisk.img') -Force
}

function Invoke-Qemu([string]$EspPath, [string]$SerialPath, [string]$StdoutPath, [string]$StderrPath,
    [string]$MarkerPattern, [int]$Timeout) {
    $ovmf = $script:OvmfPath
    $args = @(
        '-accel', 'tcg,thread=single', '-machine', 'pc', '-smp', '1',
        '-drive', ('if=pflash,format=raw,readonly=on,file=' + (Quote-QemuValue $ovmf)),
        '-drive', ('file=fat:rw:' + (Quote-QemuValue $EspPath) + ',format=raw,if=ide,index=0'),
        '-m', '1024M', '-vga', 'std', '-display', 'none',
        '-serial', ('file:' + (Quote-QemuValue $SerialPath)),
        '-no-reboot', '-no-shutdown', '-rtc', 'base=utc,clock=host'
    )
    $process = Start-Process -FilePath $script:QemuPath -ArgumentList $args -WorkingDirectory $root `
        -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -PassThru
    $deadline = (Get-Date).AddSeconds($Timeout)
    $found = $false
    try {
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $SerialPath) {
                $text = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
                if ($text -match $MarkerPattern) { $found = $true; break }
            }
            if ($process.HasExited) { break }
        }
    }
    finally {
        if (-not $process.HasExited) {
            & taskkill.exe /PID $process.Id /T /F 2>$null | Out-Null
            $process.WaitForExit(5000) | Out-Null
        }
    }
    $finalText = if (Test-Path -LiteralPath $SerialPath) {
        Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
    } else { '' }
    [pscustomobject]@{
        MarkerFound = [bool]($finalText -match $MarkerPattern)
        SerialPath = $SerialPath
    }
}

$script:QemuPath = Find-Executable $QemuPath 'GXOS_QEMU_X64' 'qemu-system-x86_64.exe' @(
    'C:\Program Files\qemu\qemu-system-x86_64.exe',
    'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
    "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
    'C:\qemu\qemu-system-x86_64.exe', 'C:\msys64\mingw64\bin\qemu-system-x86_64.exe')
$script:OvmfPath = $null
foreach ($candidate in @(
    (Join-Path $root 'OVMF.fd'), (Join-Path $root 'ovmf.fd'),
    'C:\Program Files\qemu\share\edk2-x86_64-code.fd')) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $script:OvmfPath = [IO.Path]::GetFullPath($candidate); break
    }
}
$make = Get-Command mingw32-make -ErrorAction SilentlyContinue
$bootloader = Join-Path $root 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$ramdisk = Join-Path $root 'ESP\ramdisk.img'
$available = $null -ne $script:QemuPath -and $null -ne $script:OvmfPath -and
    $null -ne $make -and (Test-Path -LiteralPath $bootloader) -and
    (Test-Path -LiteralPath $ramdisk)
Write-Host ('QEMU located: ' + $(if ($available) { 'PASS' } else { 'FAIL' }))
if (-not $available) { exit 1 }
Write-Host ('QEMU version: ' + ((& $script:QemuPath --version 2>&1 | Out-String).Trim()))

$buildLog = Join-Path $runRoot 'build.log'
function Build-Kernel([string[]]$ExtraFlags) {
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    Push-Location $root
    try {
        & $make.Source '-C' (Join-Path $root 'kernel') 'ARCH=amd64' 'clean' 2>&1 |
            Tee-Object -FilePath $buildLog
        if ($LASTEXITCODE -ne 0) { return $false }
        $arguments = @('-C', (Join-Path $root 'kernel'), 'ARCH=amd64')
        if ($ExtraFlags.Count -ne 0) { $arguments += 'EXTRA_CFLAGS=' + ($ExtraFlags -join ' ') }
        & $make.Source @arguments 2>&1 | Tee-Object -FilePath $buildLog -Append
        return $LASTEXITCODE -eq 0
    }
    finally {
        Pop-Location
        $ErrorActionPreference = $previousErrorAction
    }
}

$normalKernel = if ([string]::IsNullOrWhiteSpace($NormalKernelPath)) {
    Join-Path $root 'kernel\build\amd64\bin\kernel.elf'
} else { [IO.Path]::GetFullPath($NormalKernelPath) }
$baselineTimeout = if ($BaselineTimeoutSeconds -gt 0) { $BaselineTimeoutSeconds } else { $TimeoutSeconds }
$testTimeout = if ($TestTimeoutSeconds -gt 0) { $TestTimeoutSeconds } else { $TimeoutSeconds }
$normalBuilt = if ($SkipBaseline) { $true } elseif ($SkipBuild) {
    Test-Path -LiteralPath $normalKernel
} else { Build-Kernel @() }
$baselineMarker = '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\s*$'
$baseline = $null
if (-not $SkipBaseline -and $normalBuilt) {
    $baselineEsp = Join-Path $runRoot 'baseline-ESP'
    Stage-Esp $baselineEsp $normalKernel $bootloader $ramdisk
    $baseline = Invoke-Qemu $baselineEsp (Join-Path $runRoot 'baseline.serial.log') `
        (Join-Path $runRoot 'baseline.stdout.log') (Join-Path $runRoot 'baseline.stderr.log') $baselineMarker $baselineTimeout
}
$baselinePass = $SkipBaseline -or ($normalBuilt -and $baseline.MarkerFound)
Write-Host ('Baseline boot: ' + $(if ($SkipBaseline) { 'SKIPPED (separately validated)' } elseif ($baselinePass) { 'PASS' } else { 'FAIL' }))
if (-not $baselinePass) { exit 1 }

$testBuilt = if ($SkipBuild) { $true } else { Build-Kernel @('-DGXOS_NATIVE_LOCAL_STORAGE_QEMU_TEST') }
$testKernel = if ([string]::IsNullOrWhiteSpace($TestKernelPath)) {
    Join-Path $root 'kernel\build\amd64\bin\kernel.elf'
} else { [IO.Path]::GetFullPath($TestKernelPath) }
$test = $null
if ($testBuilt -and (Test-Path -LiteralPath $testKernel)) {
    $testEsp = Join-Path $runRoot 'test-ESP'
    Stage-Esp $testEsp $testKernel $bootloader $ramdisk
    $test = Invoke-Qemu $testEsp (Join-Path $runRoot 'native-local-storage.serial.log') `
        (Join-Path $runRoot 'test.stdout.log') (Join-Path $runRoot 'test.stderr.log') `
        '(?m)^\[native-local-storage-test\]\s+ALL_PASS\s*$' $testTimeout
}
$serial = if ($null -ne $test -and (Test-Path -LiteralPath $test.SerialPath)) {
    Get-Content -LiteralPath $test.SerialPath -Raw
} else { '' }

function Guest-Pass([string]$Name) {
    return $serial -match ('(?m)^\[native-local-storage-test\]\s+' +
        [regex]::Escape($Name) + ':\s+PASS\s*$')
}

$summary = [ordered]@{
    'Manager initialization' = Guest-Pass 'Manager initialization'
    'Index allocation' = Guest-Pass 'Index allocation'
    'Index exhaustion' = Guest-Pass 'Index exhaustion'
    'Initial-thread value' = Guest-Pass 'Initial-thread value'
    'Worker-thread isolation' = Guest-Pass 'Worker-thread isolation'
    'Multiple-index isolation' = Guest-Pass 'Multiple-index isolation'
    'Detach callback value' = Guest-Pass 'Detach callback value'
    'Detach callback count' = Guest-Pass 'Detach callback count'
    'Callback repopulation policy' = Guest-Pass 'Callback repopulation policy'
    'Slot-generation reuse' = Guest-Pass 'Slot-generation reuse'
    'Stale-index rejection' = Guest-Pass 'Stale-index rejection'
    'TCB reuse clearing' = Guest-Pass 'TCB reuse clearing'
    'Initial-thread detach' = Guest-Pass 'Initial-thread detach'
    'Runtime shutdown cleanup' = Guest-Pass 'Runtime shutdown cleanup'
}
foreach ($entry in $summary.GetEnumerator()) {
    Write-Host ("{0}: {1}" -f $entry.Key, $(if ($entry.Value) { 'PASS' } else { 'FAIL' }))
}
$qemuPass = $null -ne $test -and $test.MarkerFound -and
    @($summary.Values | Where-Object { -not $_ }).Count -eq 0
Write-Host ('QEMU local-storage tests: ' + $(if ($qemuPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("Serial log: " + $(if ($null -ne $test) { $test.SerialPath } else { 'MISSING' }))
Write-Host "Artifacts: $runRoot"
if (-not $qemuPass) { exit 1 }
exit 0
