<#
.SYNOPSIS
    QEMU bare-metal audio proof for the App Model streaming backend (MC6).

.DESCRIPTION
    Boots the guideXOS kernel in QEMU (pc machine, OVMF UEFI, ESP FAT
    volume) with an Intel HDA controller + duplex codec attached and the
    guest output captured to a WAV file. The kernel must contain the
    opt-in GXOS_AUDIO_BOOT_SELFTEST probe (EXTRA_CFLAGS), which exercises
    validate/convert/mixer/DMA against real emulated hardware and reports
    PASS/FAIL over serial.

    Non-interactive: serial goes to a file, QEMU is killed after the probe
    marker appears or the timeout expires. Checks the serial log for the
    proof markers and reports the captured WAV size.

    This script does NOT modify run-qemu.bat (interactive default path).
#>
[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 150,
    [string]$SerialLog = "logs\qemu-audio-proof-serial.log",
    [string]$WavPath = "logs\qemu-audio-proof-capture.wav"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$QemuExe = "C:\Program Files\qemu\qemu-system-x86_64.exe"
if (!(Test-Path -LiteralPath $QemuExe)) { throw "QEMU not found at: $QemuExe" }

$OvmfCode = $null
$OvmfVars = $null
$SplitPflash = $false
if (Test-Path -LiteralPath (Join-Path $Root "OVMF.fd")) {
    $OvmfCode = Join-Path $Root "OVMF.fd"
} elseif (Test-Path -LiteralPath "C:\Program Files\qemu\share\edk2-x86_64-code.fd") {
    $OvmfCode = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    $SplitPflash = $true
    if (!(Test-Path -LiteralPath (Join-Path $Root "OVMF_VARS.fd"))) {
        Copy-Item -LiteralPath "C:\Program Files\qemu\share\edk2-x86_64-vars.fd" `
            -Destination (Join-Path $Root "OVMF_VARS.fd") -Force
    }
    $OvmfVars = Join-Path $Root "OVMF_VARS.fd"
} else {
    throw "No UEFI firmware found (OVMF.fd or edk2-x86_64-code.fd)."
}

$logDir = Split-Path -Parent ([IO.Path]::GetFullPath($SerialLog))
if ($logDir -and !(Test-Path -LiteralPath $logDir)) {
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
}
$SerialLogFull = [IO.Path]::GetFullPath($SerialLog)
$WavFull = [IO.Path]::GetFullPath($WavPath)
if (Test-Path -LiteralPath $SerialLogFull) { Remove-Item -Force -LiteralPath $SerialLogFull }
if (Test-Path -LiteralPath $WavFull) { Remove-Item -Force -LiteralPath $WavFull }

$args = @(
    "-machine", "pc,usb=off",
    "-drive", "file=fat:rw:ESP,format=raw",
    "-netdev", "user,id=net0",
    "-device", "e1000,netdev=net0",
    "-object", "rng-builtin,id=rng0",
    "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
    "-m", "1024M",
    "-vga", "std",
    "-display", "none",
    "-audiodev", "wav,id=audio0,path=$WavFull",
    "-device", "intel-hda",
    "-device", "hda-duplex,audiodev=audio0",
    "-serial", "file:$SerialLogFull",
    "-rtc", "base=utc,clock=host",
    "-no-reboot"
)
if ($SplitPflash) {
    $args = @(
        "-machine", "pc,usb=off",
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=$OvmfCode",
        "-drive", "if=pflash,format=raw,unit=1,file=$OvmfVars"
    ) + $args
} else {
    $args = @(
        "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfCode"
    ) + $args
}

Write-Host ("QEMU audio proof: {0} {1}" -f $QemuExe, ($args -join " "))
$proc = Start-Process -FilePath $QemuExe -ArgumentList $args -WorkingDirectory $Root -PassThru

$markers = @(
    "Audio controllers detected",
    "HDA audio at",
    "Mapping HDA audio MMIO",
    "codec bring-up",
    "ring programmed",
    "stream running",
    "backend ready",
    "boot self-test PASS",
    "boot self-test FAIL",
    "boot self-test SKIP"
)
$found = @{}
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$done = $false
while ([DateTime]::UtcNow -lt $deadline) {
    if ($proc.HasExited) {
        Write-Host "QEMU exited early with code $($proc.ExitCode)."
        break
    }
    if (Test-Path -LiteralPath $SerialLogFull) {
        $text = Get-Content -LiteralPath $SerialLogFull -Raw -ErrorAction SilentlyContinue
        if ($text) {
            foreach ($m in $markers) {
                if ($text.Contains($m)) { $found[$m] = $true }
            }
            if ($found.ContainsKey("boot self-test PASS") -or
                $found.ContainsKey("boot self-test FAIL") -or
                $found.ContainsKey("boot self-test SKIP")) {
                # Let the boot continue so the WAV captures the app-proof
                # beeps AND the post-exit silence decay (main-loop pump
                # drains the ring after applications exit).
                Start-Sleep -Seconds 25
                $done = $true
                break
            }
        }
    }
    Start-Sleep -Seconds 2
}

if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Write-Host "QEMU stopped after proof window."
}

Write-Host ""
Write-Host "===== audio proof markers ====="
foreach ($m in $markers) {
    Write-Host ("  [{0}] {1}" -f ($(if ($found.ContainsKey($m)) { "x" } else { " " }), $m))
}
if (Test-Path -LiteralPath $WavFull) {
    $wavBytes = (Get-Item -LiteralPath $WavFull).Length
    Write-Host ("  WAV capture: {0} ({1} bytes)" -f $WavFull, $wavBytes)
} else {
    Write-Host "  WAV capture: MISSING"
}
if (Test-Path -LiteralPath $SerialLogFull) {
    Write-Host ("  serial log: {0} ({1} bytes)" -f $SerialLogFull,
        (Get-Item -LiteralPath $SerialLogFull).Length)
}

if ($found.ContainsKey("boot self-test PASS")) {
    Write-Host "QEMU audio proof PASS (device-level)."
    exit 0
} else {
    Write-Host "QEMU audio proof did NOT pass (see markers above)."
    exit 1
}
