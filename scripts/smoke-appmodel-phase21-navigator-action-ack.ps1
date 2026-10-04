[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 30,
    [int]$ActionInvocationCount = 100
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build.bat before the Phase 21 application-action runtime smoke."
}
if ($ActionInvocationCount -lt 100) {
    throw "Phase 21 runtime proof requires at least 100 real active-instance action invocations."
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
public sealed class Phase21ActionRunner : IDisposable {
    private readonly Process process;
    private readonly ConcurrentQueue<string> output = new ConcurrentQueue<string>();
    public Phase21ActionRunner(ProcessStartInfo info) {
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
function Assert-Phase21 {
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
    $Runner = [Phase21ActionRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }

    Send-Command "gui.start"
    Start-Sleep -Milliseconds 250

    $ActionListOutput = Invoke-CommandAndWait "desktop.app.actions guidexos.navigator" "APP_ACTIONS_END"
    $NavigatorActionBlock = Get-DelimitedBlock $ActionListOutput "APP_ACTIONS_BEGIN" "APP_ACTIONS_END"
    Assert-Phase21 ($NavigatorActionBlock -match 'declared=1 available=1' -and
        $NavigatorActionBlock -match 'action id=open-home label=Home') `
        "generic shell enumeration discovers Navigator Home from its AppRegistry declaration"

    $ZeroActionApps = @("gxos.builtin.notepad", "gxos.builtin.fileexplorer", "gxos.builtin.imageviewer", "com.guidexos.developerstudio")
    $ZeroActionPass = $true
    foreach ($AppId in $ZeroActionApps) {
        $Output = Invoke-CommandAndWait "desktop.app.actions $AppId" "APP_ACTIONS_END"
        $Block = Get-DelimitedBlock $Output "APP_ACTIONS_BEGIN" "APP_ACTIONS_END"
        $ZeroActionPass = $ZeroActionPass -and $Block -match 'declared=0 available=0'
    }
    Assert-Phase21 $ZeroActionPass "zero-action built-ins enumerate as empty without special shell knowledge"

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
    Assert-Phase21 ($BeforeWindows -eq $AfterFailureWindows) "unknown app/action and malformed/overlong IDs create no windows"
    Assert-Phase21 ($BeforeRecent -eq $AfterFailureRecent) "failed action calls do not mutate Recent Programs"
    Assert-Phase21 ($BeforeDefaultStore -eq $AfterFailureDefaultStore) "failed action calls do not mutate default-handler persistence"
    Assert-Phase21 (-not $Runner.Output().Contains("Desktop App Model action delivered appId=guidexos.navigator actionId=open-home")) `
        "failed action calls never fall back to ordinary application launch"

    [void](Invoke-CommandAndWait "desktop.app.action guidexos.navigator open-home" "Desktop app action accepted: appId=guidexos.navigator actionId=open-home")
    Wait-Count "Desktop App Model action delivered appId=guidexos.navigator actionId=open-home launchedNewProcess=true" 1
    Wait-Count "guideXOS Navigator starting" 1
    $StartedWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $StartedOwners = Get-DelimitedBlock $StartedWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $NavigatorMatch = [regex]::Match($StartedOwners, '(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=guidexos\.navigator title=')
    Assert-Phase21 ($NavigatorMatch.Success) "action on a stopped app follows normal Navigator process/window ownership"
    if (-not $NavigatorMatch.Success) { throw "Navigator window owner snapshot was missing after action launch." }
    $WindowId = [uint64]$NavigatorMatch.Groups[1].Value
    $OwnerPid = [uint64]$NavigatorMatch.Groups[2].Value
    $StartedRecent = Get-RecentPrograms
    Assert-Phase21 ($StartedRecent -match 'guideXOS Navigator') "launching through an action records the ordinary app in Recent Programs"

    $DocumentOutput = Invoke-CommandAndWait "navigator.smoke.document" "NAVIGATOR_SMOKE_DOCUMENT_RESULT: PASS"
    Assert-Phase21 ($DocumentOutput -match 'current_url=about:navigator') "initial action launch reaches Navigator's existing Home page"
    $RecentAfterLaunch = Get-RecentPrograms
    $ConsumedPattern = "Navigator consumed App Model action appId=guidexos.navigator actionId=open-home"
    [void](Invoke-CommandAndWait "navigator.goto about:bookmarks" "NAVIGATOR_GOTO_RESULT: PASS")
    for ($Index = 0; $Index -lt $ActionInvocationCount; $Index++) {
        $BeforeConsumed = $Runner.Count([regex]::Escape($ConsumedPattern))
        [void](Invoke-CommandAndWait "desktop.app.action guidexos.navigator open-home" "Desktop app action accepted: appId=guidexos.navigator actionId=open-home")
        Wait-Count $ConsumedPattern ($BeforeConsumed + 1)
        $DocumentOutput = Invoke-CommandAndWait "navigator.smoke.document" "NAVIGATOR_SMOKE_DOCUMENT_RESULT: PASS"
        if ($DocumentOutput -notmatch 'current_url=about:navigator') {
            throw "Navigator action $($Index + 1) did not reach the Home page."
        }
    }
    Assert-Phase21 ($Runner.Count([regex]::Escape($ConsumedPattern)) -ge $ActionInvocationCount) `
        "$ActionInvocationCount action messages are consumed by Navigator's existing UI-thread Home handler"

    $AfterActiveActionWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $AfterActiveOwners = Get-DelimitedBlock $AfterActiveActionWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    $AfterActiveMatch = [regex]::Match($AfterActiveOwners, '(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=guidexos\.navigator title=')
    $RecentAfterActions = Get-RecentPrograms
    Assert-Phase21 ($AfterActiveMatch.Success -and [uint64]$AfterActiveMatch.Groups[1].Value -eq $WindowId -and
        [uint64]$AfterActiveMatch.Groups[2].Value -eq $OwnerPid) "active action invocation preserves the existing canonical process and window owner"
    Assert-Phase21 ($RecentAfterActions -eq $RecentAfterLaunch) "actions on the running app follow existing lifecycle semantics without extra Recent Programs writes"

    Send-Command "gui.close $WindowId"
    Wait-Count "Navigator exiting window=$WindowId" 1
    $ClosedOwnersOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    $ClosedOwners = Get-DelimitedBlock $ClosedOwnersOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    Assert-Phase21 ($ClosedOwners -notmatch 'appId=guidexos\.navigator') "action-launched Navigator closes cleanly and releases its window owner"

    $PreviousOwnerPid = $OwnerPid
    $CloseRelaunchCycles = 25
    for ($Cycle = 1; $Cycle -le $CloseRelaunchCycles; $Cycle++) {
        $BeforeLaunchConsume = $Runner.Count([regex]::Escape($ConsumedPattern))
        [void](Invoke-CommandAndWait "desktop.app.action guidexos.navigator open-home" "Desktop app action accepted: appId=guidexos.navigator actionId=open-home")
        Wait-Count $ConsumedPattern ($BeforeLaunchConsume + 1)

        $CycleOwner = $null
        $OwnerDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([DateTime]::UtcNow -lt $OwnerDeadline -and -not $CycleOwner) {
            $CycleWindowsOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
            $CycleWindows = Get-DelimitedBlock $CycleWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
            $CycleMatch = [regex]::Match($CycleWindows, '(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=guidexos\.navigator title=')
            if ($CycleMatch.Success) {
                $CycleOwner = [pscustomobject]@{
                    WindowId = [uint64]$CycleMatch.Groups[1].Value
                    Pid = [uint64]$CycleMatch.Groups[2].Value
                }
            } else {
                Start-Sleep -Milliseconds 100
            }
        }
        Assert-Phase21 ($CycleOwner -and $CycleOwner.Pid -ne $PreviousOwnerPid) `
            "close/relaunch cycle $Cycle uses a fresh Navigator PID"
        if (-not $CycleOwner) { throw "Navigator window ownership was not restored in close/relaunch cycle $Cycle." }

        [void](Invoke-CommandAndWait "navigator.goto about:bookmarks" "NAVIGATOR_GOTO_RESULT: PASS")
        $BeforeActiveConsume = $Runner.Count([regex]::Escape($ConsumedPattern))
        [void](Invoke-CommandAndWait "desktop.app.action guidexos.navigator open-home" "Desktop app action accepted: appId=guidexos.navigator actionId=open-home")
        Wait-Count $ConsumedPattern ($BeforeActiveConsume + 1)

        Send-Command "gui.close $($CycleOwner.WindowId)"
        Wait-Count "Navigator exiting window=$($CycleOwner.WindowId)" 1
        $CycleClosedOutput = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
        $CycleClosed = Get-DelimitedBlock $CycleClosedOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
        Assert-Phase21 ($CycleClosed -notmatch 'appId=guidexos\.navigator') `
            "close/relaunch cycle $Cycle closes its consumed target before replacement"
        $PreviousOwnerPid = $CycleOwner.Pid
    }

    $ExpectedConsumptions = 1 + $ActionInvocationCount + (2 * $CloseRelaunchCycles)
    $TraceLines = $Runner.Output() -split '\r?\n'
    $QueueLines = @($TraceLines | Where-Object { $_.Contains("Navigator queued App Model action") })
    $ConsumedLines = @($TraceLines | Where-Object { $_.Contains("Navigator consumed App Model action") })
    $QueuedTargets = @{}
    $QueuePass = $QueueLines.Count -eq $ExpectedConsumptions
    foreach ($Line in $QueueLines) {
        $QueueIdentity = [regex]::Match($Line, 'targetPid=(\d+) requestToken=(\d+)')
        if ($QueueIdentity.Success) { $QueuedTargets[$QueueIdentity.Groups[2].Value] = $QueueIdentity.Groups[1].Value }
        else { $QueuePass = $false }
    }
    $UniqueTokens = [Collections.Generic.HashSet[string]]::new()
    $IdentityPass = $ConsumedLines.Count -eq $ExpectedConsumptions
    $TokenPass = $true
    foreach ($Line in $ConsumedLines) {
        $Identity = [regex]::Match($Line, 'targetPid=(\d+) consumingPid=(\d+) acknowledgingPid=(\d+) requestToken=(\d+)')
        $IdentityPass = $IdentityPass -and $Identity.Success -and
            $Identity.Groups[1].Value -eq $Identity.Groups[2].Value -and
            $Identity.Groups[1].Value -eq $Identity.Groups[3].Value -and
            $Identity.Success -and $QueuedTargets.ContainsKey($Identity.Groups[4].Value) -and
            $Identity.Groups[1].Value -eq $QueuedTargets[$Identity.Groups[4].Value]
        if ($Identity.Success) { $TokenPass = $TokenPass -and $UniqueTokens.Add($Identity.Groups[4].Value) }
        else { $TokenPass = $false }
    }
    $TracePath = Join-Path $Root "tmp\phase21-runtime-action-trace.log"
    [IO.File]::WriteAllLines($TracePath, @($QueueLines) + @($ConsumedLines))
    if (-not $IdentityPass -or -not $TokenPass) {
        Write-Host "Acknowledgement trace diagnostic: expected=$ExpectedConsumptions lines=$($ConsumedLines.Count) uniqueTokens=$($UniqueTokens.Count)"
        foreach ($Line in ($ConsumedLines | Select-Object -Last 5)) { Write-Host $Line }
    }
    Assert-Phase21 ($QueuePass -and $IdentityPass) "all $ExpectedConsumptions located/queued targets consumed and acknowledged on the same PID"
    Assert-Phase21 ($TokenPass -and $UniqueTokens.Count -eq $ExpectedConsumptions) "all consumed actions used distinct request tokens with no stale acknowledgement cross-talk"
    Assert-Phase21 ($Runner.Output() -notmatch 'Navigator did not consume the action|action mailbox is full|completed an App Model action after its acknowledgement expired') `
        "runtime stress completed with zero action timeouts, queue rejections, or late acknowledgements"

    Write-Host "Phase 21 production runtime invocations: $ActionInvocationCount active-instance actions, one initial launch-on-miss, and $CloseRelaunchCycles launch/active/close cycles."
    Write-Host "PID/token queue and acknowledgement trace: $TracePath"
    Write-Host "Visual inspection: native Navigator window capture is unavailable; proof uses its process/window owner and consumed-action/current-URL markers."
}
finally {
    if ($Runner) {
        try { $Runner.Stop(2000) } catch { }
        $Runner.Dispose()
    }
    Restore-Snapshots
}

Write-Host ("Phase 21 application-action runtime checks: {0}/{1} passed; failures={2}" -f ($Checks - $Failures), $Checks, $Failures)
if ($Failures -ne 0) { throw "Phase 21 application-action runtime smoke failed." }
