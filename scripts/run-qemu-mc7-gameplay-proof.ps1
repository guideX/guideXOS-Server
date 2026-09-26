<#
.SYNOPSIS
    MC7 bare-metal Missile Command gameplay + audio proof (isolated ESP).

.DESCRIPTION
    Builds the proof kernel (GXOS_AUDIO_BOOT_SELFTEST + GXOS_AUDIO_MC_PROOF),
    assembles an ISOLATED ESP copy (the tracked ESP/ tree is never modified),
    stages the real AudioBeep, permission-denied twin, and Missile Command
    packages into it, boots QEMU with Intel HDA, lets real MC gameplay run,
    exits the game via monitor-injected Escape, and reports deterministic
    markers:

      discovery -> ELF load -> DD.ini -> city art -> first 480x360 frame ->
      playable state -> play_pcm accepts -> HDA DMA -> clean exit.

    This is the MC7 closure of the MC6 gap: MC6 proved the HDA backend via
    AudioBeep while MC itself was blocked at its first frame by the 448x553
    present_frame restriction. The generic MC7 presentation fix unlocks the
    unchanged game; this script proves it.
#>
[CmdletBinding()]
param(
    [int]$GameSeconds = 75,
    [int]$MonitorPort = 4447,
    [string]$WorkDir = "out\mc7-proof",
    [string]$SerialLog = "logs\qemu-mc7-gameplay-serial.log",
    [string]$WavPath = "logs\qemu-mc7-gameplay-capture.wav",
    [switch]$ArmAutopilot,
    [switch]$ArmFastForward,
    [switch]$SkipBuild,
    [switch]$SkipRun
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$IsoEsp = Join-Path $WorkDir "esp"
$SerialLogFull = [IO.Path]::GetFullPath($SerialLog)
$WavFull = [IO.Path]::GetFullPath($WavPath)

if (-not $SkipBuild) {
    Write-Host "[mc7] building proof kernel..."
    $mainObj = Join-Path $Root "kernel\build\amd64\obj\core\main.o"
    $baremetalObj = Join-Path $Root "kernel\build\amd64\obj\core\native_elf_baremetal.o"
    Remove-Item -Force -ErrorAction SilentlyContinue $mainObj, $baremetalObj
    & "C:\mingw64\bin\mingw32-make.exe" -C (Join-Path $Root "kernel") ARCH=amd64 `
        'EXTRA_CFLAGS=-DGXOS_AUDIO_BOOT_SELFTEST -DGXOS_AUDIO_MC_PROOF' -j4
    if ($LASTEXITCODE -ne 0) { throw "proof kernel build failed" }
}

if (-not $SkipRun) {
    Write-Host "[mc7] assembling isolated ESP..."
    if (Test-Path -LiteralPath $IsoEsp) { Remove-Item -LiteralPath $IsoEsp -Recurse -Force }
    New-Item -ItemType Directory -Path $IsoEsp -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $Root "ESP") -Force |
        Copy-Item -Destination $IsoEsp -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $Root "kernel\build\amd64\bin\kernel.elf") `
        -Destination (Join-Path $IsoEsp "kernel.elf") -Force

    function Stage-Tree([string]$AppDir, [string]$StageName) {
        $src = Join-Path $Root ("Apps\" + $AppDir)
        $dst = Join-Path $IsoEsp ("Apps\" + $StageName)
        if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
        New-Item -ItemType Directory -Path $dst -Force | Out-Null
        foreach ($f in @(Get-ChildItem -LiteralPath $src -Recurse -File)) {
            $rel = $f.FullName.Substring($src.Length + 1)
            $target = Join-Path $dst $rel
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $f.FullName -Destination $target -Force
        }
    }

    Stage-Tree "AudioBeep" "AudioBeep"
    Stage-Tree "AudioBeep" "AudioBeepDenied"
    Stage-Tree "MissileCommand" "MissileCommand"

    # Denied twin: same ELF bytes, manifest without audio.output.
    $deniedManifest = Join-Path $IsoEsp "Apps\AudioBeepDenied\app.json"
    $manifest = Get-Content -LiteralPath $deniedManifest -Raw | ConvertFrom-Json
    $manifest.id = "com.guidexos.audiobeep.denied"
    $manifest.displayName = "Audio Beep (denied)"
    $manifest.permissions = @("log")
    $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $deniedManifest -Encoding ascii
    $beepElf = (Get-FileHash -LiteralPath (Join-Path $Root "Apps\AudioBeep\bin\amd64\audiobeep.elf") -Algorithm SHA256).Hash
    $deniedElf = (Get-FileHash -LiteralPath (Join-Path $IsoEsp "Apps\AudioBeepDenied\bin\amd64\audiobeep.elf") -Algorithm SHA256).Hash
    if ($beepElf -ne $deniedElf) { throw "denied twin ELF bytes differ" }
    Write-Host "[mc7] staged AudioBeep + denied twin (same ELF) + MissileCommand"

    $QemuExe = "C:\Program Files\qemu\qemu-system-x86_64.exe"
    if (!(Test-Path -LiteralPath $QemuExe)) { throw "QEMU not found" }
    $OvmfCode = Join-Path $Root "OVMF.fd"
    if (!(Test-Path -LiteralPath $OvmfCode)) { throw "Missing OVMF.fd" }

    $logDir = Split-Path -Parent $SerialLogFull
    if ($logDir -and !(Test-Path -LiteralPath $logDir)) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }
    if (Test-Path -LiteralPath $SerialLogFull) { Remove-Item -Force -LiteralPath $SerialLogFull }
    if (Test-Path -LiteralPath $WavFull) { Remove-Item -Force -LiteralPath $WavFull }

    $IsoEspFull = [IO.Path]::GetFullPath($IsoEsp)
    $args = @(
        "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfCode",
        "-machine", "pc,usb=off",
        "-drive", "file=fat:rw:$IsoEspFull,format=raw",
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
    Write-Host "[mc7] booting QEMU (monitor port $MonitorPort)..."
    $proc = Start-Process -FilePath $QemuExe -ArgumentList $args -WorkingDirectory $Root -PassThru

    function Send-Monitor([string]$command) {
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
            Write-Host ("[mc7] monitor send failed: {0}" -f $_)
            return ""
        }
    }

    $launched = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(300)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($proc.HasExited) { Write-Host "[mc7] QEMU exited early."; break }
        if (Test-Path -LiteralPath $SerialLogFull) {
            $text = Get-Content -LiteralPath $SerialLogFull -Raw -ErrorAction SilentlyContinue
            if ($text -and $text.Contains("missilecommand launch begin")) { $launched = $true; break }
        }
        Start-Sleep -Seconds 3
    }
    if (-not $launched) {
        Write-Host "[mc7] Missile Command launch marker not seen; stopping."
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        exit 1
    }

    Write-Host ("[mc7] game running; ambient combat {0}s..." -f $GameSeconds)
    if ($ArmAutopilot) {
        # 'A' toggles the game's existing deterministic autopilot defense
        # (test hook, off by default): the game then plays itself with
        # real launches, detonations, splits, and level progression.
        Start-Sleep -Seconds 5
        Write-Host "[mc7] arming autopilot (sendkey a)..."
        Send-Monitor "sendkey a" | Out-Null
        Start-Sleep -Seconds 5
    }
    if ($ArmFastForward) {
        # 'F' toggles the game's existing fast-forward hook: extra
        # deterministic fixed-step ticks per frame (same tick semantics).
        Write-Host "[mc7] arming fast-forward (sendkey f)..."
        Send-Monitor "sendkey f" | Out-Null
        Start-Sleep -Seconds 5
    }
    Start-Sleep -Seconds $GameSeconds

    for ($i = 1; $i -le 3; $i++) {
        Write-Host ("[mc7] Escape attempt {0}..." -f $i)
        Send-Monitor "sendkey esc" | Out-Null
        Start-Sleep -Seconds 15
        $text = Get-Content -LiteralPath $SerialLogFull -Raw -ErrorAction SilentlyContinue
        if ($text -and $text.Contains("missilecommand launch=")) { break }
    }

    Start-Sleep -Seconds 10
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
}

$text = Get-Content -LiteralPath $SerialLogFull -Raw -ErrorAction SilentlyContinue
if (-not $text) { Write-Host "[mc7] no serial log; FAIL"; exit 1 }

function Count-Marker([string]$needle) {
    return ([regex]::Matches($text, [regex]::Escape($needle))).Count
}

Write-Host ""
Write-Host "===== MC7 gameplay markers ====="
$markers = @(
    "App Model discovery root=",
    "App Model package discovered",
    "missilecommand launch begin",
    "MissileCommand MC5 Native ELF starting",
    "MissileCommand DD.ini runtime loaded",
    "MissileCommand city art GXIM loaded",
    "MissileCommand audio voices loaded",
    "MissileCommand initial state ready",
    "window PASS app=Missile Command",
    "frame PASS app=Missile Command",
    "size=480x360",
    "MissileCommand initial frame presented",
    "MissileCommand hostile spawned",
    "MissileCommand defensive detonation",
    "[APP-AUDIO] play queued",
    "[APP-AUDIO] stream running",
    "[APP-AUDIO] status backend=",
    "MissileCommand Escape pressed",
    "MissileCommand MC5 exiting",
    "lifecycle PASS",
    "missilecommand launch=PASS",
    "app proof done",
    "boot self-test PASS"
)
foreach ($m in $markers) {
    Write-Host ("  {0,-42} {1}" -f $m, (Count-Marker $m))
}

$frame480 = Count-Marker "frame PASS app=Missile Command"
$noFrameFail = -not ($text.Contains("frame presentation failed"))
$mcExit = $text.Contains("missilecommand launch=PASS")

if (Test-Path -LiteralPath $WavFull) {
    $wavBytes = (Get-Item -LiteralPath $WavFull).Length
    Write-Host ("  WAV capture: {0} bytes" -f $wavBytes)
} else {
    Write-Host "  WAV capture: MISSING"
}

Write-Host ""
if ($frame480 -ge 1 -and $noFrameFail -and $mcExit) {
    Write-Host "MC7 QEMU gameplay proof PASS."
    exit 0
} else {
    Write-Host "MC7 QEMU gameplay proof INCOMPLETE."
    exit 1
}
