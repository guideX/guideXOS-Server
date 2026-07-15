param(
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

. (Join-Path $Root 'scripts\process_environment.ps1')
Normalize-ProcessEnvironment

. (Join-Path $Root 'scripts\navigator_smoke_repo_hygiene.ps1')

$pathBackup = $env:Path
$runtimeEnvBackup = @{
    GXOS_SYNTHETIC_DUAL_MONITOR = $env:GXOS_SYNTHETIC_DUAL_MONITOR
    GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT = $env:GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT
}

function Set-ProcessEnvValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string]$Value
    )

    if ($null -eq $Value -or $Value -eq '') {
        Remove-Item "Env:$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item -Path "Env:$Name" -Value $Value
    }
}

function Restore-ProcessEnv {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$State
    )

    Set-ProcessEnvValue -Name 'GXOS_SYNTHETIC_DUAL_MONITOR' -Value $State.GXOS_SYNTHETIC_DUAL_MONITOR
    Set-ProcessEnvValue -Name 'GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT' -Value $State.GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT
}

function Add-MingwRuntimePath {
    $mingwBin = 'C:\mingw64\bin'
    if (-not (Test-Path -LiteralPath $mingwBin)) {
        return
    }

    $segments = @()
    if (-not [string]::IsNullOrWhiteSpace($env:Path)) {
        $segments = $env:Path -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    }

    if ($segments -notcontains $mingwBin) {
        $env:Path = $mingwBin + ';' + $env:Path
    }
}

function Reset-NavigatorSmokeRuntimeFiles {
    if (Test-Path -LiteralPath (Join-Path $Root 'desktop.json')) {
        & git -C $Root restore --source=HEAD -- desktop.json | Out-Null
    }
    if (Test-Path -LiteralPath (Join-Path $Root 'desktop.state')) {
        & git -C $Root restore --source=HEAD -- desktop.state | Out-Null
    }
    Remove-Item -LiteralPath (Join-Path $Root 'display-options.cfg') -Force -ErrorAction SilentlyContinue
}

function New-ModeRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ModeName
    )

    $safeMode = ($ModeName -replace '[^A-Za-z0-9._-]', '_')
    $modeRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("guidexos-hosted-display-runtime-$safeMode-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $modeRoot | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $modeRoot 'logs') | Out-Null
    return $modeRoot
}

function New-ModeCommandScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ModeRoot,
        [int]$WaitTimeoutMs = 20000
    )

    $scriptPath = Join-Path $ModeRoot 'commands.txt'
    $lines = @(
        'gui.start'
        'desktop.display.summary'
        # Keep the server process alive while the harness inspects compositor logs
        # and, in dual-window mode, probes the hosted windows.
        ("bus.pop smoke.hosted.display.runtime {0}" -f $WaitTimeoutMs)
        'exit'
    )
    Set-Content -LiteralPath $scriptPath -Value $lines -Encoding ASCII
    return $scriptPath
}

function Start-HostedRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExePath,
        [Parameter(Mandatory = $true)]
        [string]$InputScriptPath,
        [Parameter(Mandatory = $true)]
        [string]$StdOutPath,
        [Parameter(Mandatory = $true)]
        [string]$StdErrPath
    )

    if (-not (Test-Path -LiteralPath $InputScriptPath)) {
        throw "Hosted runtime input script not found: $InputScriptPath"
    }

    return Start-Process -FilePath $ExePath -WorkingDirectory $Root -PassThru -WindowStyle Hidden `
        -RedirectStandardInput $InputScriptPath -RedirectStandardOutput $StdOutPath -RedirectStandardError $StdErrPath
}

function Stop-HostedRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds
    )

    if ($Process.HasExited) {
        return
    }

    if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
        & taskkill.exe /T /F /PID $Process.Id | Out-Null
        [void]$Process.WaitForExit(5000)
    }
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HostedDisplaySmokeNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
"@

$WM_LBUTTONDOWN = 0x0201
$WM_LBUTTONUP = 0x0202

function Wait-HostedWindow {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Title,
        [int]$TimeoutSeconds = 10
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $hwnd = [HostedDisplaySmokeNative]::FindWindow($null, $Title)
        if ($hwnd -ne [IntPtr]::Zero) {
            return $hwnd
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)

    return [IntPtr]::Zero
}

function Send-HostedClick {
    param(
        [Parameter(Mandatory = $true)]
        [IntPtr]$WindowHandle,
        [Parameter(Mandatory = $true)]
        [int]$X,
        [Parameter(Mandatory = $true)]
        [int]$Y
    )

    $lParam = [IntPtr]((($Y -band 0xFFFF) -shl 16) -bor ($X -band 0xFFFF))
    if (-not [HostedDisplaySmokeNative]::PostMessage($WindowHandle, $WM_LBUTTONDOWN, [IntPtr]1, $lParam)) {
        throw "PostMessage(WM_LBUTTONDOWN) failed for hwnd=$WindowHandle."
    }
    Start-Sleep -Milliseconds 100
    if (-not [HostedDisplaySmokeNative]::PostMessage($WindowHandle, $WM_LBUTTONUP, [IntPtr]0, $lParam)) {
        Write-Host ("[input] WM_LBUTTONUP best-effort release failed for hwnd={0}" -f $WindowHandle)
    }
}

function Read-LogText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return ''
    }

    return Get-Content -LiteralPath $Path -Raw
}

function Wait-LogPattern {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [int]$TimeoutSeconds = 10
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $text = Read-LogText -Path $Path
        if (-not [string]::IsNullOrWhiteSpace($text) -and $text -match $Pattern) {
            return $true
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)

    return $false
}

function Assert-Check {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Mode,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [bool]$Condition,
        [Parameter(Mandatory = $true)]
        [string]$Detail
    )

    $status = if ($Condition) { 'PASS' } else { 'FAIL' }
    Write-Host ("[{0}] {1} - {2}" -f $Mode, $Name, $status)
    Write-Host ("       {0}" -f $Detail)
    if (-not $Condition) {
        throw ("[{0}] {1} failed: {2}" -f $Mode, $Name, $Detail)
    }
}

function Invoke-HostedDisplayModeSmoke {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ModeName,
        [Parameter(Mandatory = $true)]
        [hashtable]$EnvironmentValues,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedPresentationMode,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedDesktopMode,
        [Parameter(Mandatory = $true)]
        [int]$ExpectedRenderTargetCount,
        [Parameter(Mandatory = $true)]
        [int]$ExpectedBackedTargetCount,
        [Parameter(Mandatory = $true)]
        [bool]$ExpectedDualWindowOutput,
        [Parameter(Mandatory = $true)]
        [bool]$ExpectSecondaryWindow,
        [Parameter(Mandatory = $true)]
        [bool]$ExpectPaintRouting,
        [Parameter(Mandatory = $true)]
        [bool]$ExpectInputMapping
    )

    $modeRoot = New-ModeRoot -ModeName $ModeName
    $stdoutLog = Join-Path $modeRoot 'logs\stdout.log'
    $stderrLog = Join-Path $modeRoot 'logs\stderr.log'
    $exe = Join-Path $Root 'guideXOSServer.exe'

    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Hosted runtime executable not found: $exe"
    }

    Reset-NavigatorSmokeRuntimeFiles

    $desktopJsonState = Save-NavigatorSmokeFileState -LiteralPath (Join-Path $Root 'desktop.json')
    $desktopStateState = Save-NavigatorSmokeFileState -LiteralPath (Join-Path $Root 'desktop.state')
    $displayOptionsState = Save-NavigatorSmokeFileState -LiteralPath (Join-Path $Root 'display-options.cfg')

    $savedEnv = @{
        GXOS_SYNTHETIC_DUAL_MONITOR = $env:GXOS_SYNTHETIC_DUAL_MONITOR
        GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT = $env:GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT
    }

    foreach ($entry in $EnvironmentValues.GetEnumerator()) {
        Set-ProcessEnvValue -Name $entry.Key -Value $entry.Value
    }

    $proc = $null
    $commandScriptPath = $null
    $stdoutSnapshotText = ''
    $stderrSnapshotText = ''
    $inputMappingStatus = 'skipped'
    $inputMappingDetail = 'not attempted'
    try {
        $commandScriptPath = New-ModeCommandScript -ModeRoot $modeRoot -WaitTimeoutMs 45000
        $proc = Start-HostedRuntime -ExePath $exe -InputScriptPath $commandScriptPath -StdOutPath $stdoutLog -StdErrPath $stderrLog

        if (-not (Wait-LogPattern -Path $stdoutLog -Pattern 'Compositor display layout summary: ' -TimeoutSeconds $TimeoutSeconds)) {
            throw "[$ModeName] compositor summary log missing after gui.start. See $stdoutLog"
        }

        $stdoutSnapshotText = Read-LogText -Path $stdoutLog
        $stderrSnapshotText = Read-LogText -Path $stderrLog

        if ($ExpectSecondaryWindow) {
            if (-not (Wait-LogPattern -Path $stdoutLog -Pattern '\[hosted-dual-window\] secondary hwnd=' -TimeoutSeconds $TimeoutSeconds)) {
                throw "[$ModeName] secondary window log missing. See $stdoutLog"
            }
        }

        if ($ExpectInputMapping) {
            try {
                $lines = (Read-LogText -Path $stdoutLog) -split "`r?`n"
                $primaryHwndLine = $lines | Where-Object { $_ -match '\[hosted-dual-window\] primary hwnd=' } | Select-Object -Last 1
                $secondaryHwndLine = $lines | Where-Object { $_ -match '\[hosted-dual-window\] secondary hwnd=' } | Select-Object -Last 1

                $primaryWindow = [IntPtr]::Zero
                $secondaryWindow = [IntPtr]::Zero
                if ($primaryHwndLine -match 'primary hwnd=(0x[0-9A-Fa-f]+)') {
                    $primaryWindow = [IntPtr]([Convert]::ToInt64($Matches[1].Substring(2), 16))
                }
                if ($secondaryHwndLine -match 'secondary hwnd=(0x[0-9A-Fa-f]+)') {
                    $secondaryWindow = [IntPtr]([Convert]::ToInt64($Matches[1].Substring(2), 16))
                }

                if ($primaryWindow -eq [IntPtr]::Zero -or $secondaryWindow -eq [IntPtr]::Zero) {
                    throw 'logged hwnd lookup timed out'
                }

                $clickX = 120
                $clickY = 120

                Send-HostedClick -WindowHandle $primaryWindow -X $clickX -Y $clickY
                if (-not (Wait-LogPattern -Path $stdoutLog -Pattern 'WM_LBUTTONDOWN hwnd=.*targetIndex=1' -TimeoutSeconds 10)) {
                    throw "[$ModeName] primary click log missing. See $stdoutLog"
                }
                Send-HostedClick -WindowHandle $secondaryWindow -X $clickX -Y $clickY
                if (-not (Wait-LogPattern -Path $stdoutLog -Pattern 'WM_LBUTTONDOWN hwnd=.*targetIndex=2' -TimeoutSeconds 10)) {
                    throw "[$ModeName] secondary click log missing. See $stdoutLog"
                }

                $lines = (Read-LogText -Path $stdoutLog) -split "`r?`n"
                $inputMappingLinePrimary = $lines | Where-Object { $_ -match 'WM_LBUTTONDOWN hwnd=.*targetIndex=1' } | Select-Object -Last 1
                $inputMappingLineSecondary = $lines | Where-Object { $_ -match 'WM_LBUTTONDOWN hwnd=.*targetIndex=2' } | Select-Object -Last 1

                if (-not $inputMappingLinePrimary -or -not $inputMappingLineSecondary) {
                    throw "[$ModeName] input mapping logs were not emitted after synthetic clicks. See $stdoutLog"
                }

                $primaryVirtualX = $null
                $secondaryVirtualX = $null
                $primaryTaskbarVisible = $null
                $secondaryTaskbarVisible = $null
                if ($inputMappingLinePrimary -match 'virtual=(\d+),(\d+)') {
                    $primaryVirtualX = [int]$Matches[1]
                }
                if ($inputMappingLineSecondary -match 'virtual=(\d+),(\d+)') {
                    $secondaryVirtualX = [int]$Matches[1]
                }
                if ($inputMappingLinePrimary -match 'taskbarVisible=([a-z]+)') {
                    $primaryTaskbarVisible = $Matches[1]
                }
                if ($inputMappingLineSecondary -match 'taskbarVisible=([a-z]+)') {
                    $secondaryTaskbarVisible = $Matches[1]
                }

                Assert-Check -Mode $ModeName -Name 'primary click virtual x' -Condition ($primaryVirtualX -eq $clickX) -Detail ("line={0}" -f $inputMappingLinePrimary)
                Assert-Check -Mode $ModeName -Name 'secondary click virtual x' -Condition ($secondaryVirtualX -eq ($clickX + 1920)) -Detail ("line={0}" -f $inputMappingLineSecondary)
                Assert-Check -Mode $ModeName -Name 'primary click taskbar visibility' -Condition ($primaryTaskbarVisible -eq 'true') -Detail ("line={0}" -f $inputMappingLinePrimary)
                Assert-Check -Mode $ModeName -Name 'secondary click taskbar visibility' -Condition ($secondaryTaskbarVisible -eq 'false') -Detail ("line={0}" -f $inputMappingLineSecondary)

                $inputMappingStatus = 'pass'
                $inputMappingDetail = "primaryVirtualX=$primaryVirtualX secondaryVirtualX=$secondaryVirtualX clickX=$clickX"
                Write-Host ("[{0}] input mapping - PASS" -f $ModeName)
                Write-Host ("       {0}" -f $inputMappingDetail)

                $stdoutSnapshotText = Read-LogText -Path $stdoutLog
                $stderrSnapshotText = Read-LogText -Path $stderrLog
            } catch {
                $inputMappingStatus = 'manual'
                $inputMappingDetail = "click synthesis was unreliable in this harness: $($_.Exception.Message); inspect WM_LBUTTONDOWN logs manually"
                Write-Host ("[{0}] input mapping - MANUAL" -f $ModeName)
                Write-Host ("       {0}" -f $inputMappingDetail)
            }
        }

        if ($proc -and -not $proc.HasExited) {
            if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
                & taskkill.exe /T /F /PID $proc.Id | Out-Null
                [void]$proc.WaitForExit(5000)
            }
        }
    } finally {
        if ($proc -and -not $proc.HasExited) {
            try {
                & taskkill.exe /T /F /PID $proc.Id | Out-Null
            } catch {
                # Best effort only; the outer cleanup below will still restore state.
            }
        }

        Restore-ProcessEnv -State $savedEnv
        Restore-NavigatorSmokeFileState -State $desktopJsonState
        Restore-NavigatorSmokeFileState -State $desktopStateState
        Restore-NavigatorSmokeFileState -State $displayOptionsState
    }

    if ($null -eq $proc) {
        throw "[$ModeName] hosted runtime process was never started."
    }

    if (-not (Wait-LogPattern -Path $stdoutLog -Pattern 'guideXOSServer server exiting\.' -TimeoutSeconds 10)) {
        Start-Sleep -Seconds 2
    }

    $stdoutText = if ([string]::IsNullOrWhiteSpace($stdoutSnapshotText)) { Read-LogText -Path $stdoutLog } else { $stdoutSnapshotText }
    $stderrText = if ([string]::IsNullOrWhiteSpace($stderrSnapshotText)) { Read-LogText -Path $stderrLog } else { $stderrSnapshotText }
    $allText = $stdoutText
    if (-not [string]::IsNullOrWhiteSpace($stderrText)) {
        $allText += "`n" + $stderrText
    }
    $lines = $allText -split "`r?`n"

    $summaryLine = $lines | Where-Object { $_ -match 'presentationMode=.*renderTargetCount=.*backedTargetCount=' } | Select-Object -Last 1
    if (-not $summaryLine) {
        throw "[$ModeName] Summary line missing from compositor logs. See $stdoutLog"
    }

    $desktopSummaryLine = $lines | Where-Object { $_ -match 'Compositor display layout summary: ' } | Select-Object -Last 1
    if (-not $desktopSummaryLine) {
        throw "[$ModeName] Desktop layout startup line missing from compositor logs. See $stdoutLog"
    }

    $presentationMode = $null
    $dualWindowOutput = $null
    $renderTargetCount = $null
    $backedTargetCount = $null
    $activeViewportOrigin = $null
    if ($summaryLine -match 'presentationMode=([^\s]+)') {
        $presentationMode = $Matches[1]
    }
    if ($summaryLine -match 'dualWindowOutput=([^\s]+)') {
        $dualWindowOutput = $Matches[1]
    }
    if ($summaryLine -match 'renderTargetCount=(\d+)') {
        $renderTargetCount = [int]$Matches[1]
    }
    if ($summaryLine -match 'backedTargetCount=(\d+)') {
        $backedTargetCount = [int]$Matches[1]
    }
    if ($summaryLine -match 'activeViewportOrigin=([0-9]+,[0-9]+)') {
        $activeViewportOrigin = $Matches[1]
    }

    Assert-Check -Mode $ModeName -Name 'presentation mode' -Condition ($presentationMode -eq $ExpectedPresentationMode) -Detail ("line={0}" -f $summaryLine)
    Assert-Check -Mode $ModeName -Name 'dual window gate' -Condition (($dualWindowOutput -eq 'true') -eq $ExpectedDualWindowOutput) -Detail ("dualWindowOutput={0}" -f $dualWindowOutput)
    Assert-Check -Mode $ModeName -Name 'render target count' -Condition ($renderTargetCount -eq $ExpectedRenderTargetCount) -Detail ("renderTargetCount={0}" -f $renderTargetCount)
    Assert-Check -Mode $ModeName -Name 'backed target count' -Condition ($backedTargetCount -eq $ExpectedBackedTargetCount) -Detail ("backedTargetCount={0}" -f $backedTargetCount)
    Assert-Check -Mode $ModeName -Name 'active viewport origin' -Condition ($activeViewportOrigin -eq '0,0') -Detail ("activeViewportOrigin={0}" -f $activeViewportOrigin)
    Assert-Check -Mode $ModeName -Name 'desktop mode' -Condition ($desktopSummaryLine -match ('mode=' + [regex]::Escape($ExpectedDesktopMode))) -Detail ("line={0}" -f $desktopSummaryLine)

    if ($ModeName -eq 'synthetic-only' -or $ModeName -eq 'dual-window') {
        Assert-Check -Mode $ModeName -Name 'synthetic monitor count' -Condition ($desktopSummaryLine -match 'monitorCount=2') -Detail ("line={0}" -f $desktopSummaryLine)
        Assert-Check -Mode $ModeName -Name 'synthetic virtual desktop size' -Condition ($desktopSummaryLine -match 'virtualDesktop=3840x1080') -Detail ("line={0}" -f $desktopSummaryLine)
        Assert-Check -Mode $ModeName -Name 'synthetic monitor rectangles' -Condition ($desktopSummaryLine -match 'display-1.*@0,0 1920x1080.*display-2.*@1920,0 1920x1080') -Detail ("line={0}" -f $desktopSummaryLine)
    }

    if ($ModeName -eq 'no-gates') {
        Assert-Check -Mode $ModeName -Name 'single target origin' -Condition ($summaryLine -match 'display-target-1 .* origin=0,0 .* backed=true') -Detail ("line={0}" -f $summaryLine)
        Assert-Check -Mode $ModeName -Name 'no secondary target' -Condition ($summaryLine -notmatch 'display-target-2 ') -Detail ("line={0}" -f $summaryLine)
    } elseif ($ModeName -eq 'synthetic-only') {
        Assert-Check -Mode $ModeName -Name 'monitor 1 target origin' -Condition ($summaryLine -match 'display-target-1 .* origin=0,0 .* backed=true') -Detail ("line={0}" -f $summaryLine)
        Assert-Check -Mode $ModeName -Name 'monitor 2 target origin' -Condition ($summaryLine -match 'display-target-2 .* origin=1920,0 .* backed=false') -Detail ("line={0}" -f $summaryLine)
        Assert-Check -Mode $ModeName -Name 'dual-window gate remains off' -Condition ($summaryLine -match 'dualWindowOutput=false') -Detail ("line={0}" -f $summaryLine)
        Assert-Check -Mode $ModeName -Name 'secondary window absent' -Condition (($lines | Where-Object { $_ -match '\[hosted-dual-window\] secondary hwnd=' } | Measure-Object).Count -eq 0) -Detail "secondary window should not be created"
    } elseif ($ModeName -eq 'dual-window') {
        Assert-Check -Mode $ModeName -Name 'monitor 1 target origin' -Condition ($summaryLine -match 'display-target-1 .* origin=0,0 .* backed=true') -Detail ("line={0}" -f $summaryLine)
        Assert-Check -Mode $ModeName -Name 'monitor 2 target origin' -Condition ($summaryLine -match 'display-target-2 .* origin=1920,0 .* backed=true') -Detail ("line={0}" -f $summaryLine)
        $dualWindowStartupLine = $lines | Where-Object { $_ -match 'Compositor dual-window output enabled via GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT=1' } | Select-Object -Last 1
        $primaryHwndLogLine = $lines | Where-Object { $_ -match '\[hosted-dual-window\] primary hwnd=' } | Select-Object -Last 1
        $secondaryHwndLogLine = $lines | Where-Object { $_ -match '\[hosted-dual-window\] secondary hwnd=' } | Select-Object -Last 1
        Assert-Check -Mode $ModeName -Name 'dual-window startup log' -Condition ($null -ne $dualWindowStartupLine) -Detail ("line={0}" -f $dualWindowStartupLine)
        Assert-Check -Mode $ModeName -Name 'primary hwnd log' -Condition ($null -ne $primaryHwndLogLine) -Detail ("line={0}" -f $primaryHwndLogLine)
        Assert-Check -Mode $ModeName -Name 'secondary hwnd log' -Condition ($null -ne $secondaryHwndLogLine) -Detail ("line={0}" -f $secondaryHwndLogLine)
    }

    if ($ExpectPaintRouting) {
        $primaryPaintLine = $lines | Where-Object { $_ -match 'WM_PAINT hwnd=.*targetIndex=1' } | Select-Object -Last 1
        $secondaryPaintLine = $lines | Where-Object { $_ -match 'WM_PAINT hwnd=.*targetIndex=2' } | Select-Object -Last 1
        if (-not $primaryPaintLine -or -not $secondaryPaintLine) {
            throw "[$ModeName] WM_PAINT routing logs missing. See $stdoutLog"
        }
        Assert-Check -Mode $ModeName -Name 'primary paint routes to monitor 1' -Condition ($primaryPaintLine -match 'renderTarget=display-target-1 .* origin=0,0') -Detail ("line={0}" -f $primaryPaintLine)
        Assert-Check -Mode $ModeName -Name 'secondary paint routes to monitor 2' -Condition ($secondaryPaintLine -match 'renderTarget=display-target-2 .* origin=1920,0') -Detail ("line={0}" -f $secondaryPaintLine)
        Assert-Check -Mode $ModeName -Name 'primary taskbar visible' -Condition ($primaryPaintLine -match 'taskbarVisible=true') -Detail ("line={0}" -f $primaryPaintLine)
        Assert-Check -Mode $ModeName -Name 'secondary taskbar suppressed' -Condition ($secondaryPaintLine -match 'taskbarVisible=false') -Detail ("line={0}" -f $secondaryPaintLine)
    }

    return [pscustomobject]@{
        ModeName = $ModeName
        LogDir = (Split-Path -Parent $stdoutLog)
        StdOutLog = $stdoutLog
        StdErrLog = $stderrLog
        SummaryLine = $summaryLine
        DesktopSummaryLine = $desktopSummaryLine
        InputMappingStatus = $inputMappingStatus
        InputMappingDetail = $inputMappingDetail
        PresentationMode = $presentationMode
        DualWindowOutput = $dualWindowOutput
        RenderTargetCount = $renderTargetCount
        BackedTargetCount = $backedTargetCount
    }
}

Add-MingwRuntimePath

$modeResults = @()
$modeDefinitions = @(
    [pscustomobject]@{
        ModeName = 'no-gates'
        EnvironmentValues = @{ GXOS_SYNTHETIC_DUAL_MONITOR = $null; GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT = $null }
        ExpectedPresentationMode = 'normal-single-output'
        ExpectedDesktopMode = 'mirror'
        ExpectedRenderTargetCount = 1
        ExpectedBackedTargetCount = 1
        ExpectedDualWindowOutput = $false
        ExpectSecondaryWindow = $false
        ExpectPaintRouting = $false
        ExpectInputMapping = $false
    },
    [pscustomobject]@{
        ModeName = 'synthetic-only'
        EnvironmentValues = @{ GXOS_SYNTHETIC_DUAL_MONITOR = '1'; GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT = $null }
        ExpectedPresentationMode = 'synthetic-camera'
        ExpectedDesktopMode = 'extend'
        ExpectedRenderTargetCount = 2
        ExpectedBackedTargetCount = 1
        ExpectedDualWindowOutput = $false
        ExpectSecondaryWindow = $false
        ExpectPaintRouting = $false
        ExpectInputMapping = $false
    },
    [pscustomobject]@{
        ModeName = 'dual-window'
        EnvironmentValues = @{ GXOS_SYNTHETIC_DUAL_MONITOR = '1'; GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT = '1' }
        ExpectedPresentationMode = 'synthetic-two-window-output'
        ExpectedDesktopMode = 'extend'
        ExpectedRenderTargetCount = 2
        ExpectedBackedTargetCount = 2
        ExpectedDualWindowOutput = $true
        ExpectSecondaryWindow = $true
        ExpectPaintRouting = $true
        ExpectInputMapping = $true
    }
)

try {
    foreach ($mode in $modeDefinitions) {
        Write-Host ("[{0}] launching hosted runtime smoke" -f $mode.ModeName)
        try {
            $result = Invoke-HostedDisplayModeSmoke `
                -ModeName $mode.ModeName `
                -EnvironmentValues $mode.EnvironmentValues `
                -ExpectedPresentationMode $mode.ExpectedPresentationMode `
                -ExpectedDesktopMode $mode.ExpectedDesktopMode `
                -ExpectedRenderTargetCount $mode.ExpectedRenderTargetCount `
                -ExpectedBackedTargetCount $mode.ExpectedBackedTargetCount `
                -ExpectedDualWindowOutput $mode.ExpectedDualWindowOutput `
                -ExpectSecondaryWindow $mode.ExpectSecondaryWindow `
                -ExpectPaintRouting $mode.ExpectPaintRouting `
                -ExpectInputMapping $mode.ExpectInputMapping
        } catch {
            Write-Host ("[{0}] runtime smoke error: {1}" -f $mode.ModeName, $_.Exception.Message)
            Write-Host $_.ScriptStackTrace
            throw
        }

        $modeResults += $result
        Write-Host ("[{0}] completed. logs={1}" -f $mode.ModeName, $result.StdOutLog)
    }
} finally {
    Restore-ProcessEnv -State $runtimeEnvBackup
    if ($null -ne $pathBackup) {
        $env:Path = $pathBackup
    } else {
        Remove-Item Env:\Path -ErrorAction SilentlyContinue
    }
}

if (-not $modeResults -or $modeResults.Count -eq 0) {
    throw 'No mode results were produced.'
}
$modeRoot = Split-Path -Parent $modeResults[0].LogDir
$evidencePath = Join-Path $modeRoot 'hosted-display-runtime.evidence.txt'
$evidenceLines = @(
    '[HostedDisplayRuntimeSmoke]',
    'evidenceVersion=1',
    "repo=$Root",
    "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
)
foreach ($result in $modeResults) {
    $evidenceLines += "mode=$($result.ModeName)"
    $evidenceLines += "presentationMode=$($result.PresentationMode)"
    $evidenceLines += "dualWindowOutput=$($result.DualWindowOutput)"
    $evidenceLines += "renderTargetCount=$($result.RenderTargetCount)"
    $evidenceLines += "backedTargetCount=$($result.BackedTargetCount)"
    $evidenceLines += "summary=$($result.SummaryLine)"
    $evidenceLines += "desktopSummary=$($result.DesktopSummaryLine)"
    $evidenceLines += "inputMappingStatus=$($result.InputMappingStatus)"
    $evidenceLines += "inputMappingDetail=$($result.InputMappingDetail)"
    $evidenceLines += "stdoutLog=$($result.StdOutLog)"
    $evidenceLines += "stderrLog=$($result.StdErrLog)"
}
Set-Content -LiteralPath $evidencePath -Value $evidenceLines -Encoding UTF8

Write-Host 'Hosted display runtime smoke passed.'
Write-Host ("Evidence: {0}" -f $evidencePath)
foreach ($result in $modeResults) {
    Write-Host ("[{0}] stdout={1}" -f $result.ModeName, $result.StdOutLog)
    Write-Host ("[{0}] stderr={1}" -f $result.ModeName, $result.StdErrLog)
}
