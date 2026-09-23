<#
.SYNOPSIS
    QEMU Missile Command bare-metal audio run (MC6, monitor-driven).

.DESCRIPTION
    Boots the proof kernel (GXOS_AUDIO_BOOT_SELFTEST + GXOS_AUDIO_MC_PROOF)
    with HDA audio and a QEMU monitor. The in-kernel app proof launches the
    real Missile Command package after the AudioBeep legs; this script waits
    for the game to run, lets ambient combat play (startup Alarm, falling
    missiles, detonations, losses), then delivers Escape via the monitor so
    the game exits cleanly and the hook reports. Non-interactive.
#>
[CmdletBinding()]
param(
    [int]$GameSeconds = 90,
    [int]$MonitorPort = 4445,
    [string]$SerialLog = "logs\qemu-mc-audio-serial.log",
    [string]$WavPath = "logs\qemu-mc-audio-capture.wav"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$QemuExe = "C:\Program Files\qemu\qemu-system-x86_64.exe"
if (!(Test-Path -LiteralPath $QemuExe)) { throw "QEMU not found at: $QemuExe" }
$OvmfCode = Join-Path $Root "OVMF.fd"
if (!(Test-Path -LiteralPath $OvmfCode)) { throw "Missing OVMF.fd" }

$SerialLogFull = [IO.Path]::GetFullPath($SerialLog)
$WavFull = [IO.Path]::GetFullPath($WavPath)
if (Test-Path -LiteralPath $SerialLogFull) { Remove-Item -Force -LiteralPath $SerialLogFull }
if (Test-Path -LiteralPath $WavFull) { Remove-Item -Force -LiteralPath $WavFull }

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
    "-device", "intel-hda",
    "-device", "hda-duplex,audiodev=audio0",
    "-serial", "file:$SerialLogFull",
    "-monitor", "tcp:127.0.0.1:$MonitorPort,server,nowait",
    "-rtc", "base=utc,clock=host",
    "-no-reboot"
)

Write-Host "QEMU MC audio run starting (monitor port $MonitorPort)..."
$proc = Start-Process -FilePath $QemuExe -ArgumentList $args -WorkingDirectory $Root -PassThru

function Send-Monitor($command) {
    try {
        $client = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $MonitorPort)
        $stream = $client.GetStream()
        $reader = New-Object System.IO.StreamReader($stream)
        $writer = New-Object System.IO.StreamWriter($stream)
        $writer.AutoFlush = $true
        Start-Sleep -Milliseconds 500
        while ($stream.DataAvailable) { $reader.ReadLine() | Out-Null }
        $writer.WriteLine($command)
        Start-Sleep -Milliseconds 500
        $out = ""
        while ($stream.DataAvailable) { $out += $reader.ReadLine() }
        $client.Close()
        return $out
    } catch {
        Write-Host ("monitor send failed: {0}" -f $_)
        return ""
    }
}

# Wait for the game launch marker.
$launched = $false
$deadline = [DateTime]::UtcNow.AddSeconds(240)
while ([DateTime]::UtcNow -lt $deadline) {
    if ($proc.HasExited) { Write-Host "QEMU exited early."; break }
    if (Test-Path -LiteralPath $SerialLogFull) {
        $text = Get-Content -LiteralPath $SerialLogFull -Raw -ErrorAction SilentlyContinue
        if ($text -and $text.Contains("missilecommand launch begin")) { $launched = $true; break }
    }
    Start-Sleep -Seconds 3
}
if (-not $launched) {
    Write-Host "Missile Command launch marker not seen; stopping."
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    exit 1
}

Write-Host ("Game running; letting ambient combat play {0}s..." -f $GameSeconds)
Start-Sleep -Seconds $GameSeconds

# Deliver Escape (up to 3 attempts) until the hook reports launch end.
for ($i = 1; $i -le 3; $i++) {
    Write-Host ("Escape attempt {0}..." -f $i)
    Send-Monitor "sendkey esc" | Out-Null
    Start-Sleep -Seconds 15
    $text = Get-Content -LiteralPath $SerialLogFull -Raw -ErrorAction SilentlyContinue
    if ($text -and $text.Contains("missilecommand launch=")) { break }
}

Start-Sleep -Seconds 10
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }

$text = Get-Content -LiteralPath $SerialLogFull -Raw -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "===== MC audio markers ====="
foreach ($m in @("missilecommand launch begin", "MissileCommand audio request", "MissileCommand", "missilecommand launch=", "app proof done")) {
    $hits = @($text.Split("`n") | Select-String -Pattern ([regex]::Escape($m)))
    Write-Host ("  {0}: {1} hit(s)" -f $m, $hits.Count)
    if ($hits.Count -gt 0 -and $hits.Count -le 6) {
        foreach ($h in $hits) { Write-Host ("    " + $h.Line.Trim()) }
    }
}
if ($text.Contains("missilecommand launch=PASS")) {
    Write-Host "MC bare-metal run PASS (game exited cleanly)."
    exit 0
} else {
    Write-Host "MC bare-metal run INCOMPLETE (see markers; audio may still be captured)."
    exit 1
}
