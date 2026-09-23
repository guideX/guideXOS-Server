<#
.SYNOPSIS
    QEMU audio variant prober (MC6 triage, not part of the proof gate).

.DESCRIPTION
    Boots the current ESP/kernel with a selectable HDA controller/codec
    combination and reports the audio markers. Used to isolate
    controller-emulation vs codec-model issues without rebuilding.
#>
[CmdletBinding()]
param(
    [string]$Controller = "intel-hda",
    [string]$Codec = "hda-duplex",
    [int]$TimeoutSeconds = 120,
    [string]$Tag = "variant",
    [int]$HdaDebug = 0
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$QemuExe = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$OvmfCode = Join-Path $Root "OVMF.fd"
$SerialLogFull = Join-Path $Root ("logs\qemu-audio-" + $Tag + "-serial.log")
$WavFull = Join-Path $Root ("logs\qemu-audio-" + $Tag + "-capture.wav")
if (Test-Path -LiteralPath $SerialLogFull) { Remove-Item -Force -LiteralPath $SerialLogFull }
if (Test-Path -LiteralPath $WavFull) { Remove-Item -Force -LiteralPath $WavFull }

$ctlArg = $Controller
$codecArg = "$Codec,audiodev=audio0"
if ($HdaDebug -gt 0) {
    $ctlArg = "$Controller,debug=$HdaDebug"
    $codecArg = "$Codec,audiodev=audio0,debug=$HdaDebug"
}
$QemuStderrFull = Join-Path $Root ("logs\qemu-audio-" + $Tag + "-stderr.log")
if (Test-Path -LiteralPath $QemuStderrFull) { Remove-Item -Force -LiteralPath $QemuStderrFull }

$args = @(
    "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfCode",
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
    "-device", $ctlArg,
    "-device", $codecArg,
    "-serial", "file:$SerialLogFull",
    "-rtc", "base=utc,clock=host",
    "-no-reboot"
)

Write-Host ("QEMU variant: controller={0} codec={1}" -f $Controller, $Codec)
$proc = Start-Process -FilePath $QemuExe -ArgumentList $args -WorkingDirectory $Root -PassThru `
    -RedirectStandardError $QemuStderrFull
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
while ([DateTime]::UtcNow -lt $deadline) {
    if ($proc.HasExited) { break }
    if (Test-Path -LiteralPath $SerialLogFull) {
        $text = Get-Content -LiteralPath $SerialLogFull -Raw -ErrorAction SilentlyContinue
        if ($text -and ($text.Contains("boot self-test PASS") -or $text.Contains("boot self-test FAIL") -or $text.Contains("boot self-test SKIP"))) {
            Start-Sleep -Seconds 8
            break
        }
    }
    Start-Sleep -Seconds 2
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }

$text = ""
if (Test-Path -LiteralPath $SerialLogFull) {
    $text = Get-Content -LiteralPath $SerialLogFull -Raw
}
foreach ($m in @("Audio controllers detected", "diag: codec addr", "diag: rirb probe", "diag: afg-power", "diag: icir-vendor", "output SD index", "backend ready (default route", "backend ready", "no DMA motion", "dma=", "self-test PASS", "self-test FAIL", "self-test SKIP")) {
    $line = $text.Split("`n") | Select-String -Pattern ([regex]::Escape($m)) | Select-Object -First 1
    if ($line) { Write-Host ("  HIT {0}: {1}" -f $m, $line.Line.Trim()) }
    else { Write-Host ("  ... {0}" -f $m) }
}
if (Test-Path -LiteralPath $WavFull) {
    Write-Host ("  WAV bytes: {0}" -f (Get-Item -LiteralPath $WavFull).Length)
}
