<#
.SYNOPSIS
    MC2 live-execution smoke for the Missile Command playable defense loop.

.DESCRIPTION
    Launches Apps/MissileCommand in the experimental hosted runtime
    (guideXOSServer.experimental.exe), fires one left-click shot and one
    right-drag shot, waits for the defense to resolve, then idles without
    firing so the level reaches a terminal state (LEVEL COMPLETE via quota
    drain or GAME OVER via city loss). Escape exits cleanly.

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
    [int]$IdleSeconds = 90,
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

$logDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $logDir ("missilecommand-mc2-smoke-" + $stamp + ".log")

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
    Send-Line "desktop.launch Missile Command (MC2)"
    Start-Sleep -Seconds $LaunchSeconds
    Send-Line "nativeapp.processes"
    Send-Line "gui.wlist"
    Start-Sleep -Seconds 2
    # Window 1000 is the first window id issued by a fresh compositor; the
    # targeted forms route input without requiring compositor focus.
    # Shot 1: conventional left click (VB left DOWN fires).
    Send-Line "gui.mouse 1000 240 100 1 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.mouse 1000 240 100 1 up"
    Start-Sleep -Seconds 2
    # Shot 2: right-drag (VB right MOVE fires, right DOWN alone does not).
    Send-Line "gui.mouse 1000 300 120 2 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.mouse 1000 320 110 2 move"
    Start-Sleep -Seconds 1
    Send-Line "gui.mouse 1000 320 110 2 up"
    Start-Sleep -Seconds $FireSettleSeconds
    Send-Line "nativeapp.debuglog 60"
    # Idle: no further defense, so the stranded level must terminate on its
    # own (quota drain -> LEVEL COMPLETE, or city loss -> GAME OVER). Poll
    # the debug log and stop early once a terminal marker appears; the
    # hosted loop runs the 50ms sim tick slightly under wall-clock rate,
    # so allow up to ~4 minutes for the full L1 quota to resolve.
    $terminalSeen = $false
    for ($round = 0; $round -lt 12 -and -not $terminalSeen; ++$round) {
        Start-Sleep -Seconds 20
        Send-Line "nativeapp.debuglog 120"
        Start-Sleep -Seconds 2
        $snapshot = $output.ToString()
        $terminalSeen = ($snapshot -match "MissileCommand LEVEL COMPLETE") -or
                        ($snapshot -match "MissileCommand GAME OVER")
    }
    Start-Sleep -Seconds 2
    Send-Line "gui.keyto 1000 27 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.keyto 1000 27 up"
    Start-Sleep -Seconds 3
    Send-Line "nativeapp.processes"
    Send-Line "nativeapp.debuglog 120"
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
    "launched"        = $text -match "Launched native app process: Missile Command \(MC2\)"
    "starting"        = $text -match "MissileCommand MC2 Native ELF starting"
    "initialFrame"    = $text -match "MissileCommand initial frame presented"
    "windowCreated"   = $text -match "Compositor created window id=1000"
    "hostileSpawned"  = $text -match "MissileCommand hostile spawned"
    "defensiveLaunch" = $text -match "MissileCommand defensive launch"
    "detonation"      = $text -match "MissileCommand defensive detonation"
    "combat"          = ($text -match "MissileCommand intercept kill") -or ($text -match "MissileCommand city destroyed")
    "terminalState"   = ($text -match "MissileCommand LEVEL COMPLETE") -or ($text -match "MissileCommand GAME OVER")
    "escapeDelivered" = $text -match "MissileCommand Escape pressed"
    "cleanExit"       = $text -match "MissileCommand MC2 exiting"
}

Write-Host ""
Write-Host "MissileCommand MC2 live-execution smoke"
foreach ($key in $checks.Keys) {
    Write-Host ("  {0,-16} {1}" -f $key, $(if ($checks[$key]) { "PASS" } else { "FAIL" }))
}
Write-Host ("  log: {0}" -f $logPath)

$failed = @($checks.Values | Where-Object { -not $_ })
if ($failed.Count -gt 0) { exit 1 }
Write-Host "MissileCommand MC2 smoke PASS."
