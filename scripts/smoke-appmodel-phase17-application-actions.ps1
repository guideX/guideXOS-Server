[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 30,
    [int]$ActionInvocationCount = 10
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build.bat before the Phase 17 application-action runtime smoke."
}
if ($ActionInvocationCount -lt 10) {
    throw "Phase 17 runtime proof requires at least 10 real action invocations."
}

$SnapshotPaths = @("desktop.json", "desktop.state", "window-bounds.cfg", "appmodel-default-handlers.cfg")
$Snapshots = @{}
foreach ($Relative in $SnapshotPaths) {
    $Path = Join-Path $Root $Relative
    $Exists = Test-Path -LiteralPath $Path -PathType Leaf
    $Snapshots[$Relative] = [pscustomobject]@{
        Path = $Path
        Exists = $Exists
        BytesBase64 = if ($Exists) { [Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) } else { $null }
        Attributes = if ($Exists) { [IO.File]::GetAttributes($Path) } else { [IO.FileAttributes]::Normal }
        LastWriteTimeUtc = if ($Exists) { [IO.File]::GetLastWriteTimeUtc($Path) } else { [DateTime]::MinValue }
    }
}

$RunnerSource = @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Threading;
public sealed class Phase17ActionRunner : IDisposable {
    private readonly Process process;
    private readonly ConcurrentQueue<string> output = new ConcurrentQueue<string>();
    public Phase17ActionRunner(ProcessStartInfo info) {
        process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (s,e) => { if (e.Data != null) output.Enqueue(e.Data); };
        process.ErrorDataReceived += (s,e) => { if (e.Data != null) output.Enqueue(e.Data); };
    }
    public bool Start() { if (!process.Start()) return false; process.BeginOutputReadLine(); process.BeginErrorReadLine(); return true; }
    public void Send(string command) { process.StandardInput.WriteLine(command); process.StandardInput.Flush(); }
    public string Output() { return string.Join(Environment.NewLine, output.ToArray()); }
    public int Count(string pattern) { return Regex.Matches(Output(), pattern).Count; }
    public bool WaitForCount(string pattern, int expected, int timeoutMs) {
        var until = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < until) { if (Count(pattern) >= expected) return true; Thread.Sleep(40); }
        return Count(pattern) >= expected;
    }
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
$Checks = 0
$Failures = 0
function Assert-Phase17 {
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
    $Escaped = [regex]::Escape($Pattern)
    $Before = $script:Runner.Count($Escaped)
    Send-Command $Command
    if (-not $script:Runner.WaitForCount($Escaped, $Before + 1, $Timeout)) {
        $Output = $script:Runner.Output()
        $Start = [Math]::Max(0, $Output.Length - 12000)
        throw "Timed out waiting for '$Command' output '$Pattern'.`n$($Output.Substring($Start))"
    }
    return $script:Runner.Output()
}
function Wait-Count {
    param([string]$Text, [int]$Expected, [int]$Timeout = ($TimeoutSeconds * 1000))
    $Escaped = [regex]::Escape($Text)
    if (-not $script:Runner.WaitForCount($Escaped, $Expected, $Timeout)) {
        $Output = $script:Runner.Output()
        $Start = [Math]::Max(0, $Output.Length - 12000)
        throw "Timed out waiting for $Expected occurrences of '$Text'.`n$($Output.Substring($Start))"
    }
}
function Get-DelimitedBlock {
    param([string]$Output, [string]$StartMarker, [string]$EndMarker)
    $Start = $Output.LastIndexOf($StartMarker, [StringComparison]::Ordinal)
    if ($Start -lt 0) { return "" }
    $End = $Output.IndexOf($EndMarker, $Start, [StringComparison]::Ordinal)
    if ($End -lt 0) { return "" }
    return $Output.Substring($Start, $End + $EndMarker.Length - $Start)
}
function Get-RecentPrograms {
    $Output = Invoke-CommandAndWait "desktop.recent" "Recent Documents ("
    return Get-DelimitedBlock $Output "Recent Programs (" "Recent Documents ("
}
function Restore-Snapshots {
    foreach ($Entry in $script:Snapshots.GetEnumerator()) {
        $Snapshot = $Entry.Value
        if ($Snapshot.Exists) {
            [byte[]]$SnapshotBytes = [Convert]::FromBase64String($Snapshot.BytesBase64)
            [IO.File]::WriteAllBytes($Snapshot.Path, $SnapshotBytes)
            [IO.File]::SetAttributes($Snapshot.Path, $Snapshot.Attributes)
            [IO.File]::SetLastWriteTimeUtc($Snapshot.Path, $Snapshot.LastWriteTimeUtc)
        } elseif (Test-Path -LiteralPath $Snapshot.Path -PathType Leaf) {
            Remove-Item -LiteralPath $Snapshot.Path -Force
        }
    }
}

try {
    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Exe
    $StartInfo.WorkingDirectory = $Root
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Runner = [Phase17ActionRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }

    Send-Command "gui.start"
    Start-Sleep -Milliseconds 250

    $ActionListOutput = Invoke-CommandAndWait "desktop.app.actions guidexos.navigator" "APP_ACTIONS_END"
    $NavigatorActionBlock = Get-DelimitedBlock $ActionListOutput "APP_ACTIONS_BEGIN" "APP_ACTIONS_END"
    Assert-Phase17 ($NavigatorActionBlock -match 'declared=1 available=1' -and
        $NavigatorActionBlock -match 'action id=open-home label=Home') `
        "generic shell enumeration discovers Navigator Home from its AppRegistry declaration"

    $ZeroActionApps = @("gxos.builtin.notepad", "gxos.builtin.fileexplorer", "gxos.builtin.imageviewer", "com.guidexos.developerstudio")
    $ZeroActionPass = $true
    foreach ($AppId in $ZeroActionApps) {
        $Output = Invoke-CommandAndWait "desktop.app.actions $AppId" "APP_ACTIONS_END"
        $Block = Get-DelimitedBlock $Output "APP_ACTIONS_BEGIN" "APP_ACTIONS_END"
        $ZeroActionPass = $ZeroActionPass -and $Block -match 'declared=0 available=0'
    }
    Assert-Phase17 $ZeroActionPass "zero-action built-ins enumerate as empty without special shell knowledge"

    $BeforeWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $BeforeWindows = Get-DelimitedBlock $BeforeWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $BeforeRecent = Get-RecentPrograms
    $BeforeDefaultStore = if ($Snapshots["appmodel-default-handlers.cfg"].Exists) {
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($Snapshots["appmodel-default-handlers.cfg"].Path))
    } else { "<absent>" }

    $FailureCommands = @(
        @{ Command = "desktop.app.action missing.app open-home"; Marker = "Desktop app action failed (app-unavailable)" },
        @{ Command = "desktop.app.action guidexos.navigator unknown-action"; Marker = "Desktop app action failed (action-unavailable)" },
        @{ Command = "desktop.app.action guidexos.navigator bad_ID"; Marker = "Desktop app action failed (action-unavailable)" },
        @{ Command = "desktop.app.action guidexos.navigator " + ("a" * 49); Marker = "Desktop app action failed (action-unavailable)" }
    )
    foreach ($Failure in $FailureCommands) {
        [void](Invoke-CommandAndWait $Failure.Command $Failure.Marker)
    }
    $AfterFailureWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $AfterFailureWindows = Get-DelimitedBlock $AfterFailureWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $AfterFailureRecent = Get-RecentPrograms
    $AfterFailureDefaultStore = if (Test-Path -LiteralPath $Snapshots["appmodel-default-handlers.cfg"].Path -PathType Leaf) {
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($Snapshots["appmodel-default-handlers.cfg"].Path))
    } else { "<absent>" }
    Assert-Phase17 ($BeforeWindows -eq $AfterFailureWindows) "unknown app/action and malformed/overlong IDs create no windows"
    Assert-Phase17 ($BeforeRecent -eq $AfterFailureRecent) "failed action calls do not mutate Recent Programs"
    Assert-Phase17 ($BeforeDefaultStore -eq $AfterFailureDefaultStore) "failed action calls do not mutate default-handler persistence"
    Assert-Phase17 (-not $Runner.Output().Contains("Desktop App Model action delivered appId=guidexos.navigator actionId=open-home")) `
        "failed action calls never fall back to ordinary application launch"

    [void](Invoke-CommandAndWait "desktop.app.action guidexos.navigator open-home" "Desktop app action accepted: appId=guidexos.navigator actionId=open-home")
    Wait-Count "Desktop App Model action delivered appId=guidexos.navigator actionId=open-home launchedNewProcess=true" 1
    Wait-Count "guideXOS Navigator starting" 1
    $StartedWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $StartedOwners = Get-DelimitedBlock $StartedWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $NavigatorMatch = [regex]::Match($StartedOwners, '(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=guidexos\.navigator title=')
    Assert-Phase17 ($NavigatorMatch.Success) "action on a stopped app follows normal Navigator process/window ownership"
    if (-not $NavigatorMatch.Success) { throw "Navigator window owner snapshot was missing after action launch." }
    $WindowId = [uint64]$NavigatorMatch.Groups[1].Value
    $OwnerPid = [uint64]$NavigatorMatch.Groups[2].Value
    $StartedRecent = Get-RecentPrograms
    Assert-Phase17 ($StartedRecent -match 'guideXOS Navigator') "launching through an action records the ordinary app in Recent Programs"

    $DocumentOutput = Invoke-CommandAndWait "navigator.smoke.document" "NAVIGATOR_SMOKE_DOCUMENT_RESULT: PASS"
    Assert-Phase17 ($DocumentOutput -match 'current_url=about:navigator') "initial action launch reaches Navigator's existing Home page"
    $RecentAfterLaunch = Get-RecentPrograms
    $ConsumedPattern = "Navigator consumed App Model action appId=guidexos.navigator actionId=open-home"
    for ($Index = 0; $Index -lt $ActionInvocationCount; $Index++) {
        [void](Invoke-CommandAndWait "navigator.goto about:bookmarks" "NAVIGATOR_GOTO_RESULT: PASS")
        $BeforeConsumed = $Runner.Count([regex]::Escape($ConsumedPattern))
        [void](Invoke-CommandAndWait "desktop.app.action guidexos.navigator open-home" "Desktop app action accepted: appId=guidexos.navigator actionId=open-home")
        Wait-Count $ConsumedPattern ($BeforeConsumed + 1)
        $DocumentOutput = Invoke-CommandAndWait "navigator.smoke.document" "NAVIGATOR_SMOKE_DOCUMENT_RESULT: PASS"
        if ($DocumentOutput -notmatch 'current_url=about:navigator') {
            throw "Navigator action $($Index + 1) did not reach the Home page."
        }
    }
    Assert-Phase17 ($Runner.Count([regex]::Escape($ConsumedPattern)) -ge $ActionInvocationCount) `
        "$ActionInvocationCount action messages are consumed by Navigator's existing UI-thread Home handler"

    $AfterActiveActionWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $AfterActiveOwners = Get-DelimitedBlock $AfterActiveActionWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $AfterActiveMatch = [regex]::Match($AfterActiveOwners, '(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=guidexos\.navigator title=')
    $RecentAfterActions = Get-RecentPrograms
    Assert-Phase17 ($AfterActiveMatch.Success -and [uint64]$AfterActiveMatch.Groups[1].Value -eq $WindowId -and
        [uint64]$AfterActiveMatch.Groups[2].Value -eq $OwnerPid) "active action invocation preserves the existing canonical process and window owner"
    Assert-Phase17 ($RecentAfterActions -eq $RecentAfterLaunch) "actions on the running app follow existing lifecycle semantics without extra Recent Programs writes"

    Send-Command "gui.close $WindowId"
    Wait-Count "Navigator exiting window=$WindowId" 1
    $ClosedOwnersOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $ClosedOwners = Get-DelimitedBlock $ClosedOwnersOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    Assert-Phase17 ($ClosedOwners -notmatch 'appId=guidexos\.navigator') "action-launched Navigator closes cleanly and releases its window owner"
    Write-Host "Phase 17 production runtime invocations: $ActionInvocationCount active-instance actions plus one launch-on-miss action."
    Write-Host "Visual inspection: native Navigator window capture is unavailable; proof uses its process/window owner and consumed-action/current-URL markers."
}
finally {
    if ($Runner) {
        try { $Runner.Stop(2000) } catch { }
        $Runner.Dispose()
    }
    Restore-Snapshots
}

Write-Host ("Phase 17 application-action runtime checks: {0}/{1} passed; failures={2}" -f ($Checks - $Failures), $Checks, $Failures)
if ($Failures -ne 0) { throw "Phase 17 application-action runtime smoke failed." }
