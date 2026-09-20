<#
.SYNOPSIS
    MC1 live-execution smoke for the Missile Command Native ELF skeleton.

.DESCRIPTION
    Launches Apps/MissileCommand in the experimental hosted runtime
    (guideXOSServer.experimental.exe), waits for the initial frame, delivers
    Escape to the focused native window, and asserts the skeleton's lifecycle
    markers: start, initial frame presented, Escape/close received, clean exit.

    Stdout is drained asynchronously so the child never blocks on a full pipe.
    Command sequencing uses sleeps; the full log is analyzed at the end.

    Requires guideXOSServer.experimental.exe (build-native-experimental.bat)
    and the staged Apps/MissileCommand package (sdk/build-samples.ps1).
#>
[CmdletBinding()]
param(
    [int]$StartupSeconds = 25,
    [int]$LaunchSeconds = 10,
    [int]$ShutdownSeconds = 25
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
$logPath = Join-Path $logDir ("missilecommand-mc1-smoke-" + $stamp + ".log")

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
    Send-Line "desktop.launch Missile Command (MC1 Skeleton)"
    Start-Sleep -Seconds $LaunchSeconds
    Send-Line "nativeapp.processes"
    Send-Line "gui.wlist"
    Start-Sleep -Seconds 2
    Send-Line "gui.pop"
    Send-Line "gui.pop"
    Start-Sleep -Seconds 2
    # Window 1000 is the first window id issued by a fresh compositor; the
    # targeted forms route input without requiring compositor focus.
    Send-Line "gui.mouse 1000 240 180 1 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.mouse 1000 240 180 1 up"
    Start-Sleep -Seconds 2
    Send-Line "gui.keyto 1000 27 down"
    Start-Sleep -Seconds 1
    Send-Line "gui.keyto 1000 27 up"
    Start-Sleep -Seconds 3
    Send-Line "nativeapp.processes"
    Send-Line "nativeapp.debuglog 60"
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
    "launched"        = $text -match "Launched native app process: Missile Command \(MC1 Skeleton\)"
    "starting"        = $text -match "MissileCommand MC1 Native ELF starting"
    "initialFrame"    = $text -match "MissileCommand initial frame presented"
    "windowCreated"   = $text -match "Compositor created window id=1000"
    "mouseRouted"     = $text -match "MissileCommand click routed"
    "escapeDelivered" = $text -match "MissileCommand Escape pressed"
    "cleanExit"       = $text -match "MissileCommand MC1 exiting"
}

Write-Host ""
Write-Host "MissileCommand MC1 live-execution smoke"
foreach ($key in $checks.Keys) {
    Write-Host ("  {0,-16} {1}" -f $key, $(if ($checks[$key]) { "PASS" } else { "FAIL" }))
}
Write-Host ("  log: {0}" -f $logPath)

$failed = @($checks.Values | Where-Object { -not $_ })
if ($failed.Count -gt 0) { exit 1 }
Write-Host "MissileCommand MC1 smoke PASS."
