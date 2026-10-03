[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 30,
    [int]$UnknownActivationCount = 20,
    [string]$Uri = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build.bat before this Phase 15 runtime smoke."
}
if ($UnknownActivationCount -lt 20) {
    throw "Phase 15 no-handler runtime proof requires at least 20 activations."
}

$Python = "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    $PythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ($PythonCommand) { $Python = $PythonCommand.Source }
    else { throw "Python is required to start the deterministic local Navigator HTTP fixture." }
}
$HttpServer = Join-Path $Root "scripts\navigator_kernel_http_server.py"

$PortProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$PortProbe.Start()
$Port = ([Net.IPEndPoint]$PortProbe.LocalEndpoint).Port
$PortProbe.Stop()
$UseLocalHttpFixture = [string]::IsNullOrWhiteSpace($Uri)
$TargetUri = if ($UseLocalHttpFixture) {
    "http://127.0.0.1:$Port/navigator-smoke/table-phase8b.html?phase15=owned-uri"
} else {
    $Uri.Trim()
}
$SchemeMatch = [regex]::Match($TargetUri, '^([A-Za-z][A-Za-z0-9+.-]*):')
if ($TargetUri.Length -gt 2048 -or -not $SchemeMatch.Success -or
    $SchemeMatch.Groups[1].Value -notmatch '^(?i:http|https)$' -or
    $TargetUri -match '[\x00-\x1f\x7f]') {
    throw "URI smoke accepts only bounded, control-free HTTP(S) URIs."
}
$TargetScheme = $SchemeMatch.Groups[1].Value.ToLowerInvariant()

$SnapshotPaths = @("desktop.json", "desktop.state", "window-bounds.cfg", "appmodel-default-handlers.cfg")
$Snapshots = @{}
foreach ($Relative in $SnapshotPaths) {
    $Path = Join-Path $Root $Relative
    $Exists = Test-Path -LiteralPath $Path -PathType Leaf
    $Snapshots[$Relative] = [pscustomobject]@{
        Path = $Path
        Exists = $Exists
        Bytes = if ($Exists) { [IO.File]::ReadAllBytes($Path) } else { $null }
        Attributes = if ($Exists) { [IO.File]::GetAttributes($Path) } else { [IO.FileAttributes]::Normal }
        LastWriteTimeUtc = if ($Exists) { [IO.File]::GetLastWriteTimeUtc($Path) } else { [DateTime]::MinValue }
    }
}

$TempBase = [IO.Path]::GetTempPath()
$HttpOut = Join-Path $TempBase ("guidexos-phase15-http-" + [Guid]::NewGuid().ToString("N") + ".log")
$HttpErr = Join-Path $TempBase ("guidexos-phase15-http-" + [Guid]::NewGuid().ToString("N") + ".err.log")
$RunnerSource = @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Threading;
public sealed class Phase15UriRunner : IDisposable {
    private readonly Process process;
    private readonly ConcurrentQueue<string> output = new ConcurrentQueue<string>();
    public Phase15UriRunner(ProcessStartInfo info) {
        process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (s,e) => { if (e.Data != null) output.Enqueue(e.Data); };
        process.ErrorDataReceived += (s,e) => { if (e.Data != null) output.Enqueue(e.Data); };
    }
    public bool Start() { if (!process.Start()) return false; process.BeginOutputReadLine(); process.BeginErrorReadLine(); return true; }
    public void Send(string command) { process.StandardInput.WriteLine(command); process.StandardInput.Flush(); }
    public string Output() { return string.Join(Environment.NewLine, output.ToArray()); }
    public bool WaitFor(string pattern, int timeoutMs) {
        var until = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < until) { if (Regex.IsMatch(Output(), pattern)) return true; Thread.Sleep(50); }
        return Regex.IsMatch(Output(), pattern);
    }
    public int Count(string pattern) { return Regex.Matches(Output(), pattern).Count; }
    public void Stop(int timeoutMs) {
        if (process.HasExited) return;
        try { Send("exit"); process.StandardInput.Close(); } catch { }
        if (!process.WaitForExit(timeoutMs)) process.Kill();
        if (process.HasExited) process.WaitForExit();
    }
    public void Dispose() { process.Dispose(); }
}
'@
Add-Type -TypeDefinition $RunnerSource -Language CSharp

$Runner = $null
$HttpProcess = $null
$Checks = 0
$Failures = 0
function Assert-Phase15 {
    param([bool]$Condition, [string]$Description)
    $script:Checks++
    if ($Condition) { Write-Host "PASS: $Description" }
    else { $script:Failures++; Write-Host "FAIL: $Description" }
}
function Send-Command {
    param([string]$Command)
    $script:Runner.Send($Command)
}
function Invoke-CommandAndWait {
    param([string]$Command, [string]$Pattern, [int]$Timeout = ($TimeoutSeconds * 1000))
    $PreviousCount = $script:Runner.Count($Pattern)
    Send-Command $Command
    $Until = [DateTime]::UtcNow.AddMilliseconds($Timeout)
    while ([DateTime]::UtcNow -lt $Until) {
        if ($script:Runner.Count($Pattern) -gt $PreviousCount) { return $script:Runner.Output() }
        Start-Sleep -Milliseconds 50
    }
    $Output = $script:Runner.Output()
    $Start = [Math]::Max(0, $Output.Length - 10000)
    throw "Timed out waiting for command '$Command' output matching: $Pattern`n$($Output.Substring($Start))"
}
function Wait-Output {
    param([string]$Pattern, [int]$Timeout = ($TimeoutSeconds * 1000))
    if (-not $script:Runner.WaitFor($Pattern, $Timeout)) {
        $Output = $script:Runner.Output()
        $Start = [Math]::Max(0, $Output.Length - 10000)
        throw "Timed out waiting for runtime pattern: $Pattern`n$($Output.Substring($Start))"
    }
    return $script:Runner.Output()
}
function Get-DelimitedBlock {
    param([string]$Output, [string]$StartMarker, [string]$EndMarker)
    $Start = $Output.LastIndexOf($StartMarker, [StringComparison]::Ordinal)
    if ($Start -lt 0) { return "" }
    $End = $Output.IndexOf($EndMarker, $Start, [StringComparison]::Ordinal)
    if ($End -lt 0) { return "" }
    return $Output.Substring($Start, $End + $EndMarker.Length - $Start)
}
function Restore-Snapshots {
    foreach ($Entry in $script:Snapshots.GetEnumerator()) {
        $Snapshot = $Entry.Value
        if ($Snapshot.Exists) {
            [IO.File]::WriteAllBytes($Snapshot.Path, $Snapshot.Bytes)
            [IO.File]::SetAttributes($Snapshot.Path, $Snapshot.Attributes)
            [IO.File]::SetLastWriteTimeUtc($Snapshot.Path, $Snapshot.LastWriteTimeUtc)
        } elseif (Test-Path -LiteralPath $Snapshot.Path -PathType Leaf) {
            Remove-Item -LiteralPath $Snapshot.Path -Force
        }
    }
}

try {
    if ($UseLocalHttpFixture) {
        $HttpArgs = @("`"$HttpServer`"", "--host", "127.0.0.1", "--port", "$Port", "--root", "`"$Root`"")
        $HttpProcess = Start-Process -FilePath $Python -ArgumentList $HttpArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $HttpOut -RedirectStandardError $HttpErr
        Start-Sleep -Milliseconds 400
        if ($HttpProcess.HasExited) { throw "Deterministic Navigator HTTP fixture exited early." }
    }

    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Exe
    $StartInfo.WorkingDirectory = $Root
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Runner = [Phase15UriRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }

    Send-Command "gui.start"
    Start-Sleep -Milliseconds 300
    $BeforeWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $BeforeWindows = Get-DelimitedBlock $BeforeWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $BeforeProcessOutput = Invoke-CommandAndWait "plist" "(?m)^Processes:"
    $BeforeProcesses = [regex]::Matches($BeforeProcessOutput, "(?m)^Processes:.*$")[-1].Value
    $BeforeRecentOutput = Invoke-CommandAndWait "desktop.recent" "Recent Documents \("
    $BeforeRecent = Get-DelimitedBlock $BeforeRecentOutput "Recent Programs (" "Recent Documents ("

    for ($Index = 0; $Index -lt $UnknownActivationCount; $Index++) {
        Send-Command "desktop.open.uri gxphase15-test://example"
    }
    $UnknownOutput = Wait-Output "URI activation rejected \(no-handler\)" ($TimeoutSeconds * 1000)
    $UntilUnknownCount = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $UntilUnknownCount -and
        $Runner.Count([regex]::Escape("Desktop URI activation failed: URI activation rejected (no-handler)")) -lt $UnknownActivationCount) {
        Start-Sleep -Milliseconds 50
    }
    $UnknownFailures = $Runner.Count([regex]::Escape("Desktop URI activation failed: URI activation rejected (no-handler)"))
    Assert-Phase15 ($UnknownFailures -eq $UnknownActivationCount) "$UnknownActivationCount unknown protocol activations return deterministic no-handler errors"

    $AfterUnknownWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $AfterUnknownWindows = Get-DelimitedBlock $AfterUnknownWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $AfterUnknownProcessOutput = Invoke-CommandAndWait "plist" "(?m)^Processes:"
    $AfterUnknownProcesses = [regex]::Matches($AfterUnknownProcessOutput, "(?m)^Processes:.*$")[-1].Value
    $AfterUnknownRecentOutput = Invoke-CommandAndWait "desktop.recent" "Recent Documents \("
    $AfterUnknownRecent = Get-DelimitedBlock $AfterUnknownRecentOutput "Recent Programs (" "Recent Documents ("
    Assert-Phase15 ($BeforeWindows -eq $AfterUnknownWindows) "unknown protocols create no compositor window"
    Assert-Phase15 ($BeforeProcesses -eq $AfterUnknownProcesses) "unknown protocols create no application process"
    Assert-Phase15 ($BeforeRecent -eq $AfterUnknownRecent) "unknown protocols do not mutate Recent Programs"

    Send-Command "desktop.open.uri $TargetUri"
    [void](Wait-Output ("Navigator received App Model URI activation appId=guidexos\.navigator uri=" + [regex]::Escape($TargetUri)))
    [void](Wait-Output ("Desktop URI activation delivered scheme=$TargetScheme appId=guidexos\.navigator uri=" + [regex]::Escape($TargetUri)))
    Assert-Phase15 ($Runner.Output().Contains("Built-in dispatcher delivered canonical owned URI activation appId=guidexos.navigator uri=$TargetUri")) "AppRegistry-selected Navigator receives the exact owned URI through the generic built-in dispatcher"

    $DocumentPattern = "current_url=" + [regex]::Escape($TargetUri)
    $UntilDocument = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Send-Command "navigator.smoke.document"
        Start-Sleep -Milliseconds 100
        $RuntimeOutput = $Runner.Output()
        if ($RuntimeOutput -match $DocumentPattern) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $UntilDocument)
    Assert-Phase15 ($RuntimeOutput -match $DocumentPattern) "Navigator state preserves the exact HTTP(S) URI through normal URL ingestion"

    $OwnerOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $OwnerBlock = Get-DelimitedBlock $OwnerOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $NavigatorWindow = [regex]::Match($OwnerBlock, "(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=guidexos\.navigator title=")
    $UntilOwner = [DateTime]::UtcNow.AddSeconds(5)
    while (-not $NavigatorWindow.Success -and [DateTime]::UtcNow -lt $UntilOwner) {
        Start-Sleep -Milliseconds 100
        $OwnerOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
        $OwnerBlock = Get-DelimitedBlock $OwnerOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
        $NavigatorWindow = [regex]::Match($OwnerBlock, "(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=guidexos\.navigator title=")
    }
    if (-not $NavigatorWindow.Success) { Write-Host "Navigator owner snapshot:`n$OwnerBlock" }
    Assert-Phase15 ($NavigatorWindow.Success) "Navigator has a compositor window owned by canonical App Model identity guidexos.navigator"

    if ($NavigatorWindow.Success) {
        $NavigatorPid = $NavigatorWindow.Groups[2].Value
        $ActiveProcessSnapshot = Invoke-CommandAndWait "taskmanager.snapshot" "(?m)^syntheticCounters="
        $ActiveProcessPattern = "(?m)^processRow pid=$NavigatorPid appId=guidexos\.navigator .*running=true\r?$"
        Assert-Phase15 ($ActiveProcessSnapshot -match $ActiveProcessPattern) "Navigator's owned process is active while its URI window is open"

        Send-Command ("taskbar.close " + $NavigatorWindow.Groups[1].Value)
        Start-Sleep -Milliseconds 500
        $ClosedOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
        $ClosedBlock = Get-DelimitedBlock $ClosedOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
        Assert-Phase15 (-not $ClosedBlock.Contains("appId=guidexos.navigator")) "Navigator window closes cleanly after the URI activation smoke"
        $ClosedProcessSnapshot = Invoke-CommandAndWait "taskmanager.snapshot" "(?m)^syntheticCounters="
        $StoppedProcessPattern = "(?m)^processRow pid=$NavigatorPid appId=guidexos\.navigator .*running=false\r?$"
        $UntilProcessRelease = [DateTime]::UtcNow.AddSeconds(5)
        while ($ClosedProcessSnapshot -notmatch $StoppedProcessPattern -and [DateTime]::UtcNow -lt $UntilProcessRelease) {
            Start-Sleep -Milliseconds 100
            $ClosedProcessSnapshot = Invoke-CommandAndWait "taskmanager.snapshot" "(?m)^syntheticCounters="
        }
        Assert-Phase15 ($ClosedProcessSnapshot -match $StoppedProcessPattern) "Navigator's owned process is stopped after closing its window"
    }

    $AfterSuccessRecentOutput = Invoke-CommandAndWait "desktop.recent" "Recent Documents \("
    $AfterSuccessRecent = Get-DelimitedBlock $AfterSuccessRecentOutput "Recent Programs (" "Recent Documents ("
    if ($AfterSuccessRecent -notmatch "(?mi)^\s+.*Navigator.*$") { Write-Host "Recent Programs snapshot:`n$AfterSuccessRecent" }
    Assert-Phase15 ($AfterSuccessRecent -match "(?mi)^\s+.*Navigator.*$") "successful URI activation adds Navigator to Recent Programs"

    $Output = $Runner.Output()
    if ($Output -match "Desktop URI activation successful: $([regex]::Escape($TargetUri))") {
        Assert-Phase15 $true ("successful {0} URI activation is accepted by DesktopService" -f $TargetScheme.ToUpperInvariant())
    } else {
        Assert-Phase15 $false ("successful {0} URI activation is accepted by DesktopService" -f $TargetScheme.ToUpperInvariant())
    }
} finally {
    if ($null -ne $Runner) {
        try { $Runner.Stop(5000) } catch { }
        $Runner.Dispose()
    }
    if ($HttpProcess -and -not $HttpProcess.HasExited) {
        Stop-Process -Id $HttpProcess.Id -Force
    }
    Restore-Snapshots
    Remove-Item -LiteralPath $HttpOut, $HttpErr -Force -ErrorAction SilentlyContinue
}

Write-Host "phase15UnknownProtocolActivations=$UnknownActivationCount/$UnknownActivationCount"
Write-Host "phase15RuntimeChecks=$($Checks - $Failures)/$Checks"
if ($Failures -ne 0) { throw "Phase 15 URI activation smoke failed with $Failures failed checks." }
Write-Host "Phase 15 URI activation runtime smoke PASS."
