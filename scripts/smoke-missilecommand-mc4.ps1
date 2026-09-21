<#
.SYNOPSIS
    MC4 live-execution smoke for the Missile Command L1-L10 campaign.

.DESCRIPTION
    Launches Apps/MissileCommand in the experimental hosted runtime
    (guideXOSServer.experimental.exe), verifies DD.ini runtime loading and
    city.gximg original-art loading, enables the deterministic autopilot +
    fast test hooks ('A'/'F' keys, off by default in release play) to
    traverse L1 -> ... -> L10 with no manual input first (so the live tick
    stream reproduces the host reference run exactly -- default seed 34
    completes under the assisted policy), expects natural smart-bomb spawn
    + evasion, late-level markers, and GAME COMPLETE, then restarts and
    fires one left-click shot and one right-drag shot through the fixed
    compositor pointer path before a clean Escape exit.

    Stdout is drained asynchronously so the child never blocks on a full pipe.
    Command sequencing uses sleeps; the full log is analyzed at the end.

    Requires guideXOSServer.experimental.exe (build-native-experimental.bat)
    and the staged Apps/MissileCommand package (sdk/build-samples.ps1).
#>
[CmdletBinding()]
param(
    [int]$StartupSeconds = 25,
    [int]$LaunchSeconds = 10,
    [int]$FireSettleSeconds = 8,
    [int]$ShutdownSeconds = 30
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$exe = Join-Path $Root "guideXOSServer.experimental.exe"
if (!(Test-Path -LiteralPath $exe)) { throw "Missing experimental runtime: $exe (run build-native-experimental.bat)" }
if (!(Test-Path -LiteralPath (Join-Path $Root "Apps\MissileCommand\bin\amd64\missilecommand.elf"))) {
    throw "Missing staged MissileCommand ELF (run sdk/build-samples.ps1)"
}
if (!(Test-Path -LiteralPath (Join-Path $Root "Apps\MissileCommand\resources\DD.ini"))) {
    throw "Missing staged MissileCommand DD.ini (run sdk/build-samples.ps1)"
}
if (!(Test-Path -LiteralPath (Join-Path $Root "Apps\MissileCommand\resources\city.gximg"))) {
    throw "Missing staged MissileCommand city.gximg (run sdk/build-samples.ps1)"
}

$logDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $logDir ("missilecommand-mc4-smoke-" + $stamp + ".log")

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

function Write-ProgressLog() {
    try {
        [System.IO.File]::WriteAllText(($logPath + ".progress"), $output.ToString())
    } catch { }
}

try {
    Start-Sleep -Seconds $StartupSeconds
    Send-Line "gui.start"
    Start-Sleep -Seconds 2
    Send-Line "desktop.launch Missile Command (MC4)"
    # TIMING-CRITICAL OPENING: keys sent before the runtime subscription is
    # ready are dropped, and every unassisted minute attrits the campaign
    # (the seed completes with a 0-500 tick opening, not a 600+ one).
    # Instead of polling wlist (each dump floods the server bus), poll the
    # app debug log until "initial frame presented" proves the runtime
    # channel is live, THEN arm the hooks -- the first key press then lands
    # within seconds of readiness instead of racing it blindly.
    $channelReady = $false
    for ($w = 0; $w -lt 30 -and -not $channelReady; ++$w) {
        Start-Sleep -Seconds 3
        Send-Line "nativeapp.debuglog 10"
        Start-Sleep -Seconds 3
        $snapshot = $output.ToString()
        if ($snapshot -match "MissileCommand initial frame presented") { $channelReady = $true }
    }
    Send-Line "nativeapp.processes"
    Send-Line "gui.wlist"
    Start-Sleep -Seconds 2
    # Window 1000 is the first window id issued by a fresh compositor; the
    # targeted forms route input without requiring compositor focus.
    # Focus the game window WITHOUT firing (gui.activate sets compositor
    # focus with no gameplay side effects): targeted key events are
    # attributed to the focused window by the runtime, so A/F/R/Escape
    # delivery requires focus. A click would also focus but would spend
    # ammunition and perturb the deterministic reference stream.
    Send-Line "gui.activate 1000"
    Start-Sleep -Seconds 1
    # Arm the hooks with the IDEMPOTENT force-on keys ('O' autopilot on,
    # 'G' fast-forward on): repeats never flip, so send three copies over
    # ~5 s against cold-start input races (a dropped copy costs nothing;
    # more copies would flood the server bus). The legacy toggles ('A'/'F')
    # are not used by the smoke at all (a lagged retry of a toggle would
    # oscillate). No manual shots are fired before the campaign markers:
    # the live tick stream then reproduces the host reference run (default
    # seed 39, per-tick MPC decisions). The confirm cycle below re-arms if
    # the markers are still absent.
    for ($arm = 0; $arm -lt 3; ++$arm) {
        Send-Line "gui.keyto 1000 79 down"
        Start-Sleep -Milliseconds 200
        Send-Line "gui.keyto 1000 79 up"
        Start-Sleep -Milliseconds 200
        Send-Line "gui.keyto 1000 71 down"
        Start-Sleep -Milliseconds 200
        Send-Line "gui.keyto 1000 71 up"
        Start-Sleep -Seconds 1
    }
    function Get-ToggleState([string]$text, [string]$name) {
        $onIdx = $text.LastIndexOf("MissileCommand $name on")
        $offIdx = $text.LastIndexOf("MissileCommand $name off")
        if ($onIdx -lt 0 -and $offIdx -lt 0) { return "" }
        if ($onIdx -ge $offIdx) { return "on" } else { return "off" }
    }
    # Confirm the hooks with a single dump cycle (idempotent re-arm if the
    # markers are still absent). One cycle only: every dump stalls the app
    # thread, and the opening copies almost always deliver.
    $togglesSeen = $false
    Start-Sleep -Seconds 6
    Send-Line "nativeapp.debuglog 40"
    Start-Sleep -Seconds 6
    $snapshot = $output.ToString()
    $togglesSeen = ((Get-ToggleState $snapshot "autopilot") -eq "on") -and
                   ((Get-ToggleState $snapshot "fastforward") -eq "on")
    if (-not $togglesSeen) {
        Send-Line "gui.activate 1000"
        Start-Sleep -Seconds 1
        Send-Line "gui.keyto 1000 79 down"
        Start-Sleep -Milliseconds 300
        Send-Line "gui.keyto 1000 79 up"
        Start-Sleep -Milliseconds 300
        Send-Line "gui.keyto 1000 71 down"
        Start-Sleep -Milliseconds 300
        Send-Line "gui.keyto 1000 71 up"
        Start-Sleep -Seconds 2
    }
    Send-Line "nativeapp.debuglog 40"
    # Poll the debug log until campaign markers appear: early/mid/late level
    # starts, natural smart-bomb spawn + evasion, and GAME COMPLETE. L1-L9
    # traverse on fast-forward; L10 plays at wall rate for observability.
    # Dumps are kept small and infrequent: each dump appears to stall the
    # app thread for seconds, so fewer dumps mean faster play. Markers
    # accumulate in the captured output across dumps, so nothing is lost.
    $sawL2 = $false
    $sawSmart = $false
    $sawEvade = $false
    $sawL4 = $false
    $sawL7 = $false
    $sawL10 = $false
    $sawComplete = $false
    $sawGameOver = $false
    $sawToggles = $togglesSeen
    # Campaign watch with ADAPTIVE polling: LONG sleeps before L10 (every
    # dump stalls the app thread for seconds, so few dumps = fast
    # campaign), SHORT sleeps after L10 entry (the terminal lands within a
    # few minutes and must be caught promptly for restart/demo sequencing).
    # Markers accumulate across dumps so nothing is missed. Exits on either
    # terminal.
    $watchSleep = 90
    for ($round = 0; $round -lt 14 -and -not $sawComplete -and -not $sawGameOver; ++$round) {
        if ($sawL10) { $watchSleep = 15 }
        Start-Sleep -Seconds $watchSleep
        Send-Line "nativeapp.debuglog 40"
        Start-Sleep -Seconds 3
        Write-ProgressLog
        $snapshot = $output.ToString()
        if ($snapshot -match "MissileCommand LEVEL 2 started") { $sawL2 = $true }
        if ($snapshot -match "MissileCommand smart bomb spawned") { $sawSmart = $true }
        if ($snapshot -match "MissileCommand smart bomb evaded") { $sawEvade = $true }
        if ($snapshot -match "MissileCommand LEVEL 4 started") { $sawL4 = $true }
        if ($snapshot -match "MissileCommand LEVEL 7 started") { $sawL7 = $true }
        if ($snapshot -match "MissileCommand LEVEL 10 started") { $sawL10 = $true }
        if ($snapshot -match "MissileCommand GAME COMPLETE") { $sawComplete = $true }
        if ($snapshot -match "MissileCommand GAME OVER") { $sawGameOver = $true }
    }
    # Second-campaign vehicle: campaign 1 carries a ~600-tick unassisted
    # opening (launch-to-keypress latency) and usually dies on L10; it still
    # proves L10 reach. Only when campaign 1 did NOT complete, restart here
    # (R is a harmless no-op mid-game, a restart when terminal) and watch
    # campaign 2 with NO manual input at all -- hooks persist across
    # restarts, so campaign 2 is assisted from its tick 0 and reproduces
    # the host reference (GAME COMPLETE). Clicks would perturb its
    # trajectory, so the input demo waits until after its terminal.
    if (-not $sawComplete) {
        $sawGameOver = $false
        Send-Line "gui.activate 1000"
        Start-Sleep -Seconds 2
        Send-Line "gui.keyto 1000 82 down"
        Start-Sleep -Seconds 1
        Send-Line "gui.keyto 1000 82 up"
        Start-Sleep -Seconds 8
        $watchSleep2 = 90
        for ($round2 = 0; $round2 -lt 10 -and -not $sawComplete -and -not $sawGameOver; ++$round2) {
            if ($sawL10) { $watchSleep2 = 15 }
            Start-Sleep -Seconds $watchSleep2
            Send-Line "nativeapp.debuglog 40"
            Start-Sleep -Seconds 3
            Write-ProgressLog
            $snapshot = $output.ToString()
            if ($snapshot -match "MissileCommand LEVEL 10 started") { $sawL10 = $true }
            if ($snapshot -match "MissileCommand GAME COMPLETE") { $sawComplete = $true }
            if ($snapshot -match "MissileCommand GAME OVER") { $sawGameOver = $true }
        }
    }
    $sawTerminal = $sawComplete -or $sawGameOver
    # Input demo AFTER the campaign (so manual shots cannot perturb the
    # reference stream). First slow the game down: force fast-forward OFF
    # (idempotent 'H') so levels take ~30 s wall instead of ~3 s under FF --
    # at FF speed the demo clicks keep landing in 2 s LEVEL-COMPLETE dwells
    # or terminals where shots are refused. Autopilot stays on (it never
    # refuses a manual latch, it only overwrites it). Then restart into a
    # fresh game if terminal, then one left click (VB left DOWN fires) and
    # one right-drag (VB right MOVE fires, right DOWN alone does not).
    # Routing is VERIFIED via the debug log (order-free match) and retried.
    Send-Line "gui.activate 1000"
    Start-Sleep -Seconds 2
    Send-Line "gui.keyto 1000 72 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.keyto 1000 72 up"
    Start-Sleep -Seconds 4
    $sawClick = $false
    $sawDrag = $false
    for ($demo = 0; $demo -lt 3 -and -not ($sawClick -and $sawDrag); ++$demo) {
        Send-Line "gui.activate 1000"
        Start-Sleep -Seconds 2
        Send-Line "gui.keyto 1000 82 down"
        Start-Sleep -Seconds 1
        Send-Line "gui.keyto 1000 82 up"
        Start-Sleep -Seconds 8
        Send-Line "gui.mouse 1000 240 100 1 down"
        Start-Sleep -Seconds 1
        Send-Line "gui.mouse 1000 240 100 1 up"
        Start-Sleep -Seconds 2
        Send-Line "gui.mouse 1000 300 120 2 down"
        Start-Sleep -Seconds 1
        Send-Line "gui.mouse 1000 320 110 2 move"
        Start-Sleep -Seconds 1
        Send-Line "gui.mouse 1000 320 110 2 up"
        Start-Sleep -Seconds $FireSettleSeconds
        Send-Line "nativeapp.debuglog 40"
        Start-Sleep -Seconds 4
        $snapshot = $output.ToString()
        if ($snapshot -match "MissileCommand click routed") { $sawClick = $true }
        if ($snapshot -match "MissileCommand right-drag fire routed") { $sawDrag = $true }
    }
    Start-Sleep -Seconds 2
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
$checks = [ordered]@{
    "registered"      = $text -match "id=com\.guidexos\.missilecommand"
    "launched"        = $text -match "Launched native app process: Missile Command \(MC4\)"
    "starting"        = $text -match "MissileCommand MC4 Native ELF starting"
    "ddIniRuntime"    = $text -match "MissileCommand DD\.ini runtime loaded"
    "cityArtLoaded"   = $text -match "MissileCommand city art GXIM loaded"
    "initialFrame"    = $text -match "MissileCommand initial frame presented"
    "windowCreated"   = $text -match "Compositor created window id=1000"
    "hostileSpawned"  = $text -match "MissileCommand hostile spawned"
    "defensiveLaunch" = $text -match "MissileCommand defensive launch"
    "detonation"      = $text -match "MissileCommand defensive detonation"
    "hooks"           = ($text -match "MissileCommand autopilot on") -and ($text -match "MissileCommand fastforward on")
    "rightDrag"       = $text -match "MissileCommand right-drag fire routed"
    "level2"          = $text -match "MissileCommand LEVEL 2 started"
    "smartSpawned"    = $text -match "MissileCommand smart bomb spawned"
    "smartEvaded"     = $text -match "MissileCommand smart bomb evaded"
    "combat"          = ($text -match "MissileCommand intercept kill") -or ($text -match "MissileCommand city destroyed")
    "level4"          = $text -match "MissileCommand LEVEL 4 started"
    "level7"          = $text -match "MissileCommand LEVEL 7 started"
    "level10"         = $text -match "MissileCommand LEVEL 10 started"
    "gameComplete"    = $text -match "MissileCommand GAME COMPLETE"
    "escapeDelivered" = $text -match "MissileCommand Escape pressed"
    "cleanExit"       = $text -match "MissileCommand MC4 exiting"
}

Write-Host ""
Write-Host "MissileCommand MC4 live-execution smoke"
foreach ($key in $checks.Keys) {
    Write-Host ("  {0,-16} {1}" -f $key, $(if ($checks[$key]) { "PASS" } else { "FAIL" }))
}
Write-Host ("  log: {0}" -f $logPath)

$failed = @($checks.Values | Where-Object { -not $_ })
if ($failed.Count -gt 0) { exit 1 }
Write-Host "MissileCommand MC4 smoke PASS."
