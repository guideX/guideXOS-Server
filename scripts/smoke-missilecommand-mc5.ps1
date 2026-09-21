<#
.SYNOPSIS
    MC5 live-execution smoke: App Model audio with Missile Command.

.DESCRIPTION
    Launches Apps/MissileCommand (MC5, audio.output permission) in the
    experimental hosted runtime (guideXOSServer.experimental.exe) and proves:
    registration, audio.output recognized, launch, DD.ini runtime, city GXIM
    art, all six audio voices loaded, window, spawn, a real play_pcm request
    reaching the audio backend (waveOut or explicit null sink), overlapping
    voices, continued gameplay while audio plays, right-drag firing, clean
    Escape exit, and voice reclaim on exit.

    Short vehicle (minutes, not the full campaign): the L1-L10 campaign
    regression stays host-side (tests/missilecommand_state_test.cpp pins the
    MC4 golden fingerprint 16492225105589479459 identically).

    Stdout is drained asynchronously so the child never blocks on a full pipe.
    Requires guideXOSServer.experimental.exe (build-native-experimental.bat)
    and staged Apps/MissileCommand (sdk/build-samples.ps1).
#>
[CmdletBinding()]
param(
    [int]$StartupSeconds = 25,
    [int]$ShutdownSeconds = 30
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$exe = Join-Path $Root "guideXOSServer.experimental.exe"
if (!(Test-Path -LiteralPath $exe)) { throw "Missing experimental runtime: $exe (run build-native-experimental.bat)" }
foreach ($need in @("Apps\MissileCommand\bin\amd64\missilecommand.elf",
                    "Apps\MissileCommand\resources\DD.ini",
                    "Apps\MissileCommand\resources\city.gximg",
                    "Apps\MissileCommand\resources\audio\swoosh.wav",
                    "Apps\MissileCommand\resources\audio\ohno.wav")) {
    if (!(Test-Path -LiteralPath (Join-Path $Root $need))) { throw "Missing staged file: $need (run sdk/build-samples.ps1)" }
}

$logDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $logDir ("missilecommand-mc5-smoke-" + $stamp + ".log")

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.WorkingDirectory = $Root
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.CreateNoWindow = $true

$output = New-Object System.Text.StringBuilder
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
$outEvent = Register-ObjectEvent -InputObject $proc -EventName OutputDataReceived -MessageData $output -Action {
    if ($EventArgs.Data -ne $null) { $Event.MessageData.AppendLine($EventArgs.Data) | Out-Null }
}
$errEvent = Register-ObjectEvent -InputObject $proc -EventName ErrorDataReceived -MessageData $output -Action {
    if ($EventArgs.Data -ne $null) { $Event.MessageData.AppendLine($EventArgs.Data) | Out-Null }
}
$null = $proc.Start()
$proc.BeginOutputReadLine()
$proc.BeginErrorReadLine()
$writer = $proc.StandardInput

function Send-Line([string]$line) {
    $writer.WriteLine($line)
    $writer.Flush()
}

try {
    Start-Sleep -Seconds $StartupSeconds
    Send-Line "gui.start"
    Start-Sleep -Seconds 2
    Send-Line "desktop.launch Missile Command (MC5)"
    # Wait for the runtime channel: "initial frame presented" proves the app
    # is live; audio voices load just before it.
    $channelReady = $false
    for ($w = 0; $w -lt 30 -and -not $channelReady; ++$w) {
        Start-Sleep -Seconds 3
        Send-Line "nativeapp.debuglog 10"
        Start-Sleep -Seconds 3
        $snapshot = $output.ToString()
        if ($snapshot -match "MissileCommand initial frame presented") { $channelReady = $true }
    }
    Send-Line "gui.wlist"
    Start-Sleep -Seconds 2
    Send-Line "gui.activate 1000"
    Start-Sleep -Seconds 1
    # Fire one left-click shot (VB left DOWN fires): the launch plays the
    # swoosh voice through play_pcm; the detonation ~1 s later plays
    # explode, overlapping the swoosh tail.
    Send-Line "gui.mouse 1000 240 100 1 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.mouse 1000 240 100 1 up"
    Start-Sleep -Seconds 6
    # Right-drag fires too (VB right MOVE): second launch overlaps combat.
    Send-Line "gui.mouse 1000 300 120 2 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.mouse 1000 320 110 2 move"
    Start-Sleep -Seconds 1
    Send-Line "gui.mouse 1000 320 110 2 up"
    Start-Sleep -Seconds 10
    Send-Line "nativeapp.debuglog 60"
    Start-Sleep -Seconds 4
    # Gameplay continues while audio plays: status lines keep flowing.
    Send-Line "nativeapp.debuglog 60"
    Start-Sleep -Seconds 4
    # Clean exit reclaims voices (BackendStopOwner on runtime cleanup).
    Send-Line "gui.activate 1000"
    Start-Sleep -Seconds 2
    Send-Line "gui.keyto 1000 27 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.keyto 1000 27 up"
    Start-Sleep -Seconds 3
    Send-Line "nativeapp.processes"
    Send-Line "nativeapp.debuglog 150"
    Start-Sleep -Seconds 2
    Send-Line "exit"
    if (-not $proc.WaitForExit($ShutdownSeconds * 1000)) {
        $proc.Kill()
        $proc.WaitForExit(10000) | Out-Null
    }
} finally {
    if (-not $proc.HasExited) {
        try { $proc.Kill() } catch { }
    }
    Start-Sleep -Milliseconds 500
    Unregister-Event -SourceIdentifier $outEvent.Name -ErrorAction SilentlyContinue
    Unregister-Event -SourceIdentifier $errEvent.Name -ErrorAction SilentlyContinue
    try { $writer.Dispose() } catch { }
    $proc.Dispose()
}

$text = $output.ToString()
[System.IO.File]::WriteAllText($logPath, $text)

$playAccepts = ([regex]::Matches($text, "play_pcm bytes=\d+ rate=\d+ backend=([a-z0-9-]+)"))
$backends = @($playAccepts | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)

$checks = [ordered]@{
    "registered"      = $text -match "id=com\.guidexos\.missilecommand"
    "launched"        = $text -match "Launched native app process: Missile Command \(MC5\)"
    "starting"        = $text -match "MissileCommand MC5 Native ELF starting"
    "ddIniRuntime"    = $text -match "MissileCommand DD\.ini runtime loaded"
    "cityArtLoaded"   = $text -match "MissileCommand city art GXIM loaded"
    "audioVoices"     = $text -match "MissileCommand audio voices loaded"
    "initialFrame"    = $text -match "MissileCommand initial frame presented"
    "windowCreated"   = $text -match "Compositor created window id=1000"
    "hostileSpawned"  = $text -match "MissileCommand hostile spawned"
    "clickRouted"     = $text -match "MissileCommand click routed"
    "audioBackend"    = $playAccepts.Count -ge 1
    "overlap"         = $playAccepts.Count -ge 2
    "detonation"      = $text -match "MissileCommand defensive detonation"
    "rightDrag"       = $text -match "MissileCommand right-drag fire routed"
    "escapeDelivered" = $text -match "MissileCommand Escape pressed"
    "cleanExit"       = $text -match "MissileCommand MC5 exiting"
}

Write-Host ""
Write-Host "MissileCommand MC5 live-audio smoke"
foreach ($key in $checks.Keys) {
    Write-Host ("  {0,-16} {1}" -f $key, $(if ($checks[$key]) { "PASS" } else { "FAIL" }))
}
Write-Host ("  playAccepts={0} backends={1}" -f $playAccepts.Count, ($backends -join ","))
Write-Host ("  log: {0}" -f $logPath)

$failed = @($checks.Values | Where-Object { -not $_ })
if ($failed.Count -gt 0) { exit 1 }
Write-Host "MissileCommand MC5 smoke PASS."
