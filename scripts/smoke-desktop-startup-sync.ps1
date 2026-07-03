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

$qemu = Find-Qemu
if (-not $qemu) { throw "qemu-system-x86_64 not found." }

$ovmf = Find-Ovmf
if (-not $ovmf) { throw "OVMF image not found." }

$esp = Join-Path $Root "ESP"
$bootloader = Join-Path $esp "EFI\BOOT\BOOTX64.EFI"
if (-not (Test-Path $bootloader)) {
    throw "ESP/EFI/BOOT/BOOTX64.EFI not found. Run build.bat first."
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
        if (Test-SerialLogContains -Path $serialLog -Pattern "[desktop] bare-metal startup desktop folder scan completed") {
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

$iconInitStarted = $output.Contains("[desktop] bare-metal desktop icon init starting")
$iconInitCompleted = $output.Contains("[desktop] bare-metal desktop icon init completed")
$backingPathChosen = $output.Contains("[desktop] bare-metal desktop backing path chosen:")
$startupRequestedCount = [regex]::Matches($output, '\[desktop\] bare-metal startup desktop folder scan requested').Count
$startupCompletedCount = [regex]::Matches($output, '\[desktop\] bare-metal startup desktop folder scan completed').Count
$startupRequested = $startupRequestedCount -eq 1
$startupCompleted = $startupCompletedCount -eq 1
$summaryPresent = [regex]::IsMatch($output, '\[desktop\] Desktop folder enumeration completed discovered=')

$overallPass = $iconInitStarted -and $iconInitCompleted -and $backingPathChosen -and $startupRequested -and $startupCompleted -and $summaryPresent

Write-Host "startup scan requested count: $startupRequestedCount"
Write-Host "startup scan completed count: $startupCompletedCount"

if ($overallPass) {
    Write-Host "Desktop startup sync smoke PASS. Serial log: $serialLog"
    exit 0
}

Write-Host "Desktop startup sync smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
exit 1
