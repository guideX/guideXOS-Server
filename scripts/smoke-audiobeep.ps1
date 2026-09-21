<#
.SYNOPSIS
    Live proof of the App Model audio API independent of Missile Command.

.DESCRIPTION
    Launches Apps/AudioBeep (synthesized 440 Hz beep, two overlapping
    play_pcm requests) in the experimental hosted runtime and expects both
    requests to reach the audio backend. Then stages a transient
    permission-denied twin (same ELF, manifest WITHOUT audio.output),
    launches it, and expects explicit permission-denied silence. The twin
    package is removed afterwards; the checkout keeps only AudioBeep.
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
if (!(Test-Path -LiteralPath $exe)) { throw "Missing experimental runtime: $exe" }
if (!(Test-Path -LiteralPath (Join-Path $Root "Apps\AudioBeep\bin\amd64\audiobeep.elf"))) {
    throw "Missing staged AudioBeep ELF (run sdk/build-samples.ps1)"
}

$logDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $logDir ("audiobeep-smoke-" + $stamp + ".log")

# Transient denied twin: same ELF bytes, manifest without audio.output.
$twinDir = Join-Path $Root "Apps\AudioBeepDenied"
$twinElfDir = Join-Path $twinDir "bin\amd64"
try {
    if (Test-Path -LiteralPath $twinDir) { Remove-Item -Recurse -Force -LiteralPath $twinDir }
    New-Item -ItemType Directory -Force -Path $twinElfDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $Root "Apps\AudioBeep\bin\amd64\audiobeep.elf") `
        -Destination (Join-Path $twinElfDir "audiobeep.elf") -Force
    $manifest = Get-Content -LiteralPath (Join-Path $Root "Apps\AudioBeep\app.json") -Raw |
        ConvertFrom-Json
    $manifest.id = "com.guidexos.audiobeep.denied"
    $manifest.displayName = "Audio Beep Denied"
    $manifest.permissions = @("log")
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText((Join-Path $twinDir "app.json"),
        (($manifest | ConvertTo-Json -Depth 16) + [Environment]::NewLine), $utf8NoBom)

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
        Send-Line "desktop.launch Audio Beep"
        Start-Sleep -Seconds 12
        Send-Line "nativeapp.debuglog 40"
        Start-Sleep -Seconds 4
        Send-Line "desktop.launch Audio Beep Denied"
        Start-Sleep -Seconds 12
        Send-Line "nativeapp.debuglog 60"
        Start-Sleep -Seconds 4
        Send-Line "nativeapp.processes"
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

    $beepAccepts = ([regex]::Matches($text, "audiobeep \(Audio Beep\)\)? play_pcm")).Count
    $checks = [ordered]@{
        "beepRegistered" = $text -match "id=com\.guidexos\.audiobeep"
        "beepLaunched"   = $text -match "Launched native app process: Audio Beep"
        "beepOverlap"    = ($text -match "AudioBeep first beep") -and ($text -match "AudioBeep second beep")
        "beepAccepted"   = ($text -match "AudioBeep request accepted") -and ($beepAccepts -ge 2)
        "beepBackend"    = $text -match "audiobeep \(Audio Beep\)\)? play_pcm bytes=13230 rate=22050 backend="
        "deniedLaunched" = $text -match "Audio Beep Denied"
        "deniedSilent"   = $text -match "AudioBeep permission denied \(silent\)"
        "deniedEnforced" = $text -match "audiobeep\.denied \(Audio Beep Denied\)\)? play_pcm denied"
        "beepExiting"    = $text -match "AudioBeep exiting"
    }

    Write-Host ""
    Write-Host "AudioBeep live API/permission smoke"
    foreach ($key in $checks.Keys) {
        Write-Host ("  {0,-16} {1}" -f $key, $(if ($checks[$key]) { "PASS" } else { "FAIL" }))
    }
    Write-Host ("  log: {0}" -f $logPath)

    $failed = @($checks.Values | Where-Object { -not $_ })
    if ($failed.Count -gt 0) { exit 1 }
    Write-Host "AudioBeep smoke PASS."
} finally {
    if (Test-Path -LiteralPath $twinDir) { Remove-Item -Recurse -Force -LiteralPath $twinDir }
}
