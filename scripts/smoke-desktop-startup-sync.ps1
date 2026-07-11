param(
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $LogDir "desktop-startup-sync-$stamp.serial.log"

function Find-Qemu {
    $qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($qemu) { return $qemu.Source }
    foreach ($candidate in @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "D:\qemu\qemu-system-x86_64.exe"
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Find-Ovmf {
    foreach ($candidate in @(
        (Join-Path $Root "OVMF.fd"),
        (Join-Path $Root "ovmf.fd"),
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Test-SerialLogContains {
    param(
        [string]$Path,
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }

    $content = Get-Content -LiteralPath $Path -Raw
    if ($null -eq $content) { return $false }
    return $content.Contains($Pattern)
}

function Test-FileContainsAscii {
    param(
        [string]$Path,
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $needle = [System.Text.Encoding]::ASCII.GetBytes($Pattern)
    if ($needle.Length -eq 0 -or $bytes.Length -lt $needle.Length) {
        return $false
    }

    for ($i = 0; $i -le $bytes.Length - $needle.Length; $i++) {
        $matched = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($bytes[$i + $j] -ne $needle[$j]) {
                $matched = $false
                break
            }
        }
        if ($matched) {
            return $true
        }
    }

    return $false
}

$qemu = Find-Qemu
if (-not $qemu) { throw "qemu-system-x86_64 not found." }

$ovmf = Find-Ovmf
if (-not $ovmf) { throw "OVMF image not found." }

$esp = Join-Path $Root "ESP"
$bootloader = Join-Path $esp "EFI\BOOT\BOOTX64.EFI"
if (-not (Test-Path $bootloader)) {
    throw "ESP/EFI/BOOT/BOOTX64.EFI not found. Run build.bat first."
}

$marker = "desktopCleanupRuntimePass=2"
$builtKernel = Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
$espKernel = Join-Path $esp "kernel.elf"
if (-not (Test-FileContainsAscii -Path $builtKernel -Pattern $marker)) {
    throw "Built kernel does not contain runtime marker: $builtKernel"
}
if (-not (Test-FileContainsAscii -Path $espKernel -Pattern $marker)) {
    throw "ESP kernel does not contain runtime marker: $espKernel"
}

$args = @(
    "-machine", "pc",
    "-drive", "if=pflash,format=raw,readonly=on,file=`"$ovmf`"",
    "-drive", "file=fat:rw:`"$esp`",format=raw,if=ide,index=0",
    "-m", "512M",
    "-vga", "std",
    "-display", "none",
    "-serial", "file:`"$serialLog`"",
    "-no-reboot"
)

$proc = $null
try {
    $proc = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (Test-SerialLogContains -Path $serialLog -Pattern "[KERNEL] desktopCleanupRuntimePass=2 launch-smoke end") {
            break
        }
    }

    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $proc.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
} finally {
    Start-Sleep -Milliseconds 250
}

$output = if (Test-Path $serialLog) { Get-Content $serialLog -Raw } else { "" }
Write-Host $output

$markerPresent = $output.Contains($marker)
$iconInitStarted = $output.Contains("[desktop] bare-metal desktop icon init starting")
$iconInitCompleted = $output.Contains("[desktop] bare-metal desktop icon init completed")
$backingPathChosen = $output.Contains("[desktop] bare-metal desktop backing path chosen:")
$startupRequestedCount = [regex]::Matches($output, '\[desktop\] bare-metal startup desktop folder scan requested').Count
$startupCompletedCount = [regex]::Matches($output, '\[desktop\] bare-metal startup desktop folder scan completed').Count
$startupRequested = $startupRequestedCount -eq 1
$startupCompleted = $startupCompletedCount -eq 1
$launchSmokeBegin = $output.Contains("[KERNEL] desktopCleanupRuntimePass=2 launch-smoke begin")
$launchSmokeEnd = $output.Contains("[KERNEL] desktopCleanupRuntimePass=2 launch-smoke end")
$displayOptionsLaunch = $output.Contains("[KERNEL] desktopCleanupRuntimePass=2 launch app=DisplayOptions result=PASS")
$notepadLaunch = $output.Contains("[KERNEL] desktopCleanupRuntimePass=2 launch app=Notepad result=PASS")
$calculatorLaunch = $output.Contains("[KERNEL] desktopCleanupRuntimePass=2 launch app=Calculator result=PASS")
$summaryPresent = [regex]::IsMatch($output, '\[desktop\] Desktop folder enumeration completed discovered=')

$overallPass = $markerPresent -and $iconInitStarted -and $iconInitCompleted -and $backingPathChosen -and $startupRequested -and $startupCompleted -and $summaryPresent -and $launchSmokeBegin -and $launchSmokeEnd -and $displayOptionsLaunch -and $notepadLaunch -and $calculatorLaunch

Write-Host "runtime marker present: $markerPresent"
Write-Host "startup scan requested count: $startupRequestedCount"
Write-Host "startup scan completed count: $startupCompletedCount"
Write-Host "launch smoke begin: $launchSmokeBegin"
Write-Host "launch smoke end: $launchSmokeEnd"
Write-Host "DisplayOptions launch: $displayOptionsLaunch"
Write-Host "Notepad launch: $notepadLaunch"
Write-Host "Calculator launch: $calculatorLaunch"

if ($overallPass) {
    Write-Host "Desktop startup sync smoke PASS. Serial log: $serialLog"
    exit 0
}

Write-Host "Desktop startup sync smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
exit 1
