<#
.SYNOPSIS
    MC3 live-execution smoke for the Missile Command L1-L3 campaign.

.DESCRIPTION
    Launches Apps/MissileCommand in the experimental hosted runtime
    (guideXOSServer.experimental.exe), verifies DD.ini runtime loading,
    enables the deterministic autopilot + fast test hooks ('A'/'F' keys,
    off by default in release play) to traverse L1 -> L2 -> L3 with no
    manual input first (so the live tick stream reproduces the host
    reference run exactly), expects natural smart-bomb spawn + evasion,
    then fires one left-click shot and one right-drag shot through the
    fixed compositor pointer path before a clean Escape exit.

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

$logDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $logDir ("missilecommand-mc3-smoke-" + $stamp + ".log")

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
    Send-Line "desktop.launch Missile Command (MC3)"
    Start-Sleep -Seconds $LaunchSeconds
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
    Start-Sleep -Seconds 2
    # Enable deterministic autopilot + fast test hooks (keys A/F).
    # These are off by default and do not alter normal release behavior.
    # No manual shots are fired before the campaign markers: the live tick
    # stream then reproduces the host reference run exactly (default seed,
    # per-tick autopilot decisions), so L1 -> L2 -> L3 with natural L3
    # smart bombs is deterministic. The input demo runs after the markers
    # (or after a restart if the campaign already ended).
    # Retry the toggles until the app confirms (cold-start races can drop
    # early input before the runtime subscription is ready). State-aware:
    # overlapping debug dumps can hide a fresh toggle, and a blind retry
    # would flip an engaged hook back off — compare the LAST on/off markers.
    function Get-ToggleState([string]$text, [string]$name) {
        $onIdx = $text.LastIndexOf("MissileCommand $name on")
        $offIdx = $text.LastIndexOf("MissileCommand $name off")
        if ($onIdx -lt 0 -and $offIdx -lt 0) { return "" }
        if ($onIdx -ge $offIdx) { return "on" } else { return "off" }
    }
    $togglesSeen = $false
    for ($try = 0; $try -lt 4 -and -not $togglesSeen; ++$try) {
        $snapshot = $output.ToString()
        if ((Get-ToggleState $snapshot "autopilot") -ne "on") {
            Send-Line "gui.keyto 1000 65 down"
            Start-Sleep -Seconds 1
            Send-Line "gui.keyto 1000 65 up"
            Start-Sleep -Seconds 1
        }
        $snapshot = $output.ToString()
        if ((Get-ToggleState $snapshot "fastforward") -ne "on") {
            Send-Line "gui.keyto 1000 70 down"
            Start-Sleep -Seconds 1
            Send-Line "gui.keyto 1000 70 up"
            Start-Sleep -Seconds 1
        }
        Start-Sleep -Seconds 3
        Send-Line "nativeapp.debuglog 40"
        Start-Sleep -Seconds 4
        $snapshot = $output.ToString()
        $togglesSeen = ((Get-ToggleState $snapshot "autopilot") -eq "on") -and
                       ((Get-ToggleState $snapshot "fastforward") -eq "on")
        if (-not $togglesSeen) {
            Send-Line "gui.activate 1000"
            Start-Sleep -Seconds 3
        }
    }
    Send-Line "nativeapp.debuglog 40"
    # Poll the debug log until campaign markers appear: L2 start, natural
    # smart-bomb spawn + evasion, L3 start / game end. L1-L2 traverse on
    # fast-forward; L3 plays at wall rate for observability. Dumps are kept
    # small and infrequent: each dump appears to stall the app thread for
    # seconds, so fewer dumps mean faster play. Markers accumulate in the
    # captured output across dumps, so nothing is lost.
    $sawL2 = $false
    $sawSmart = $false
    $sawEvade = $false
    $sawL3 = $false
    $sawTerminal = $false
    $sawToggles = $togglesSeen
    for ($round = 0; $round -lt 14 -and -not ($sawL2 -and $sawSmart -and $sawEvade -and ($sawL3 -or $sawTerminal)); ++$round) {
        Start-Sleep -Seconds 25
        Send-Line "nativeapp.debuglog 40"
        Start-Sleep -Seconds 3
        $snapshot = $output.ToString()
        if ($snapshot -match "MissileCommand LEVEL 2 started") { $sawL2 = $true }
        if ($snapshot -match "MissileCommand smart bomb spawned") { $sawSmart = $true }
        if ($snapshot -match "MissileCommand smart bomb evaded") { $sawEvade = $true }
        if ($snapshot -match "MissileCommand LEVEL 3 started") { $sawL3 = $true }
        if (($snapshot -match "MissileCommand GAME COMPLETE") -or ($snapshot -match "MissileCommand GAME OVER")) { $sawTerminal = $true }
    }
    # Input demo AFTER the markers (so manual shots cannot perturb the
    # reference stream): one left click (VB left DOWN fires) and one
    # right-drag (VB right MOVE fires, right DOWN alone does not). After the
    # MC3 compositor fix both the hosted wParam path and this synthetic
    # "2 move" path forward the held right button.
    # Re-assert focus first (key attribution needs it); mouse input carries
    # its own target and needs none.
    # If the campaign already ended, restart first so the shots route.
    Send-Line "gui.activate 1000"
    Start-Sleep -Seconds 2
    if ($sawTerminal) {
        Send-Line "gui.keyto 1000 82 down"
        Start-Sleep -Seconds 1
        Send-Line "gui.keyto 1000 82 up"
        Start-Sleep -Seconds 8
    }
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
    $snapshot = $output.ToString()
    if (-not ($snapshot -match "MissileCommand right-drag fire routed")) {
        # Shots may have been refused (e.g. terminal arrived first):
        # restart into a fresh L1 and demonstrate routing there.
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
    "launched"        = $text -match "Launched native app process: Missile Command \(MC3\)"
    "starting"        = $text -match "MissileCommand MC3 Native ELF starting"
    "ddIniRuntime"    = $text -match "MissileCommand DD\.ini runtime loaded"
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
    "level3OrEnd"     = ($text -match "MissileCommand LEVEL 3 started") -or ($text -match "MissileCommand GAME COMPLETE") -or ($text -match "MissileCommand GAME OVER")
    "escapeDelivered" = $text -match "MissileCommand Escape pressed"
    "cleanExit"       = $text -match "MissileCommand MC3 exiting"
}

Write-Host ""
Write-Host "MissileCommand MC3 live-execution smoke"
foreach ($key in $checks.Keys) {
    Write-Host ("  {0,-16} {1}" -f $key, $(if ($checks[$key]) { "PASS" } else { "FAIL" }))
}
Write-Host ("  log: {0}" -f $logPath)

$failed = @($checks.Values | Where-Object { -not $_ })
if ($failed.Count -gt 0) { exit 1 }
Write-Host "MissileCommand MC3 smoke PASS."
