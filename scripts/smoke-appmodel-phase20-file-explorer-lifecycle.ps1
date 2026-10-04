[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 15,
    [int]$StressCycles = 100
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build.bat before the Phase 20 File Explorer lifecycle smoke."
}

$FixtureLeaf = "phase20-file-explorer-lifecycle-$([Guid]::NewGuid().ToString('N'))"
$FixtureRoot = Join-Path $Root (Join-Path "tmp" $FixtureLeaf)
$FolderA = Join-Path $FixtureRoot "folder A"
$FolderB = Join-Path $FixtureRoot "folder B"
$FolderC = Join-Path $FixtureRoot "folder C"
$FolderD = Join-Path $FixtureRoot "folder D"
$FolderE = Join-Path $FixtureRoot "folder E"
$FolderF = Join-Path $FixtureRoot "folder F"
$FolderG = Join-Path $FixtureRoot "folder G"
$FolderH = Join-Path $FixtureRoot "folder H"
$NotFolder = Join-Path $FixtureRoot "file.txt"
New-Item -ItemType Directory -Force -Path $FolderA, $FolderB, $FolderC, $FolderD, $FolderE, $FolderF, $FolderG, $FolderH | Out-Null
[IO.File]::WriteAllText($NotFolder, "not a directory`r`n")
$VirtualRoot = "/tmp/$FixtureLeaf"
$VirtualA = "$VirtualRoot/folder A"
$VirtualB = "$VirtualRoot/folder B"
$VirtualC = "$VirtualRoot/folder C"
$VirtualD = "$VirtualRoot/folder D"
$VirtualE = "$VirtualRoot/folder E"
$VirtualF = "$VirtualRoot/folder F"
$VirtualG = "$VirtualRoot/folder G"
$VirtualH = "$VirtualRoot/folder H"
$VirtualFile = "$VirtualRoot/file.txt"

$Snapshots = @{}
foreach ($Relative in @("desktop.json", "desktop.state", "window-bounds.cfg", "appmodel-default-handlers.cfg")) {
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

$RunnerSource = @'
using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Threading;
public sealed class Phase20LifecycleRunner : IDisposable {
    private const int MaxLines = 16000;
    private readonly Process process;
    private readonly object sync = new object();
    private readonly Queue<string> output = new Queue<string>();
    private readonly Dictionary<string, PatternCounter> counters = new Dictionary<string, PatternCounter>();
    private sealed class PatternCounter {
        public Regex regex;
        public int count;
    }
    public Phase20LifecycleRunner(ProcessStartInfo info) {
        process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (s,e) => Add(e.Data);
        process.ErrorDataReceived += (s,e) => Add(e.Data);
    }
    private void Add(string value) {
        if (value == null) return;
        lock (sync) {
            output.Enqueue(value);
            while (output.Count > MaxLines) output.Dequeue();
            foreach (var counter in counters.Values) counter.count += counter.regex.Matches(value).Count;
        }
    }
    public bool Start() { if (!process.Start()) return false; process.BeginOutputReadLine(); process.BeginErrorReadLine(); return true; }
    public void Send(string command) { process.StandardInput.WriteLine(command); process.StandardInput.Flush(); }
    public string Output() { lock (sync) return string.Join(Environment.NewLine, output.ToArray()); }
    public int Count(string pattern) {
        lock (sync) {
            PatternCounter counter;
            if (!counters.TryGetValue(pattern, out counter)) {
                counter = new PatternCounter { regex = new Regex(pattern), count = 0 };
                foreach (var line in output) counter.count += counter.regex.Matches(line).Count;
                counters.Add(pattern, counter);
            }
            return counter.count;
        }
    }
    public bool WaitForCount(string pattern, int expected, int timeoutMs) {
        var until = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < until) {
            if (Count(pattern) >= expected) return true;
            Thread.Sleep(20);
        }
        return Count(pattern) >= expected;
    }
    public void Stop(int timeoutMs) {
        if (process.HasExited) return;
        try { process.StandardInput.WriteLine("exit"); process.StandardInput.Close(); } catch { }
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
$Timeline = [System.Collections.Generic.List[string]]::new()
$TimeoutMs = $TimeoutSeconds * 1000
$ReportPath = Join-Path $Root "tmp\phase20-file-explorer-lifecycle-$FixtureLeaf.log"
$RuntimeLogPath = Join-Path $Root "tmp\phase20-file-explorer-lifecycle-$FixtureLeaf.runtime.log"
function Assert-Phase20([bool]$Condition, [string]$Description) {
    $script:Checks++
    if ($Condition) { Write-Host "PASS: $Description"; $script:Timeline.Add("PASS: $Description") }
    else { $script:Failures++; Write-Host "FAIL: $Description"; $script:Timeline.Add("FAIL: $Description") }
}
function Invoke-CommandAndWait([string]$Command, [string]$Pattern, [int]$WaitMs = $script:TimeoutMs) {
    $Before = $script:Runner.Count($Pattern)
    $script:Runner.Send($Command)
    if (-not $script:Runner.WaitForCount($Pattern, $Before + 1, $WaitMs)) {
        [IO.File]::WriteAllText($script:RuntimeLogPath, $script:Runner.Output())
        throw "Timed out waiting for '$Command' output matching '$Pattern'. Runtime log: $script:RuntimeLogPath`n$($script:Runner.Output())"
    }
    return $script:Runner.Output()
}
function Get-DelimitedBlock([string]$Output, [string]$StartMarker, [string]$EndMarker) {
    $Start = $Output.LastIndexOf($StartMarker, [StringComparison]::Ordinal)
    if ($Start -lt 0) { return "" }
    $End = $Output.IndexOf($EndMarker, $Start, [StringComparison]::Ordinal)
    if ($End -lt 0) { return "" }
    return $Output.Substring($Start, $End + $EndMarker.Length - $Start)
}
function Get-WindowBlock() {
    $Output = Invoke-CommandAndWait "desktop.windows.owners" "DESKTOP_WINDOW_OWNERS_END"
    return Get-DelimitedBlock $Output "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
}
function Get-ProcessBlock() {
    $Output = Invoke-CommandAndWait "taskmanager.snapshot" "syntheticCounters="
    return Get-DelimitedBlock $Output "tabs=Processes,Performance,Tombstoned,Memory Details" "syntheticCounters=false"
}
function Get-ExplorerState() {
    $WindowBlock = Get-WindowBlock
    $ProcessBlock = Get-ProcessBlock
    $WindowMatches = [regex]::Matches($WindowBlock, "(?m)^window id=(\d+) ownerPid=(\d+) ownerName=([^\s]+) appId=gxos\.builtin\.fileexplorer title=File Explorer - (.*?) visible=")
    $ProcessLines = @([regex]::Matches($ProcessBlock, "(?m)^processRow pid=\d+ appId=gxos\.builtin\.fileexplorer [^\r\n]*running=(?:true|false)") | ForEach-Object { $_.Value })
    $LiveLines = @($ProcessLines | Where-Object { $_ -match 'running=true$' })
    return [pscustomobject]@{
        WindowBlock = $WindowBlock
        ProcessBlock = $ProcessBlock
        Windows = @($WindowMatches)
        ProcessLines = $ProcessLines
        LiveLines = $LiveLines
    }
}
function Test-OwnerInvariant($State) {
    if ($State.Windows.Count -ne $State.LiveLines.Count) { return $false }
    $LivePids = @($State.LiveLines | ForEach-Object { [regex]::Match($_, '^processRow pid=(\d+)').Groups[1].Value })
    foreach ($Window in $State.Windows) {
        $OwnerPid = $Window.Groups[2].Value
        if ($LivePids -notcontains $OwnerPid) { return $false }
        if ((@($State.Windows | Where-Object { $_.Groups[2].Value -eq $OwnerPid })).Count -ne 1) { return $false }
    }
    foreach ($OwnerPid in $LivePids) {
        if ((@($State.Windows | Where-Object { $_.Groups[2].Value -eq $OwnerPid })).Count -lt 1) { return $false }
    }
    return $true
}
function Wait-ExplorerState([int]$ExpectedWindows, [int]$ExpectedProcesses, [int]$WaitMs = $script:TimeoutMs) {
    $Until = [DateTime]::UtcNow.AddMilliseconds($WaitMs)
    $State = $null
    while ([DateTime]::UtcNow -lt $Until) {
        $State = Get-ExplorerState
        if ($State.Windows.Count -eq $ExpectedWindows -and $State.LiveLines.Count -eq $ExpectedProcesses -and (Test-OwnerInvariant $State)) { return $State }
        Start-Sleep -Milliseconds 50
    }
    throw "File Explorer lifecycle did not settle to windows=$ExpectedWindows processes=$ExpectedProcesses.`nWindows:`n$($State.WindowBlock)`nProcesses:`n$($State.ProcessBlock)"
}
function Open-Folder([string]$Path, [int]$ExpectedCount, [bool]$RequireAppConsumption = $true) {
    $ActivationPattern = $null
    $BeforeActivation = 0
    if ($RequireAppConsumption) {
        $ActivationPattern = "FileExplorer consumed owned folder activation appId=gxos\.builtin\.fileexplorer path=" + [regex]::Escape($Path) + " entries=\d+"
        $BeforeActivation = $script:Runner.Count($ActivationPattern)
    }
    [void](Invoke-CommandAndWait "desktop.open.folder `"$Path`"" "Desktop folder activation successful:")
    $Settled = Wait-ExplorerState $ExpectedCount $ExpectedCount
    if ($RequireAppConsumption -and -not $script:Runner.WaitForCount($ActivationPattern, $BeforeActivation + 1, $script:TimeoutMs)) {
        [IO.File]::WriteAllText($script:RuntimeLogPath, $script:Runner.Output())
        throw "File Explorer did not consume the owned folder activation for $Path. Windows:`n$($Settled.WindowBlock)`nProcesses:`n$($Settled.ProcessBlock) Runtime log: $script:RuntimeLogPath"
    }
    $State = Get-ExplorerState
    $Matching = @($State.Windows | Where-Object { $_.Groups[4].Value -eq $Path })
    Assert-Phase20 ($Matching.Count -ge 1) "OpenFolder($Path) produces a live window titled with its owned activation path"
    return $State
}
function Find-Window($State, [string]$Path, [int]$Occurrence = 0) {
    $Matches = @($State.Windows | Where-Object { $_.Groups[4].Value -eq $Path })
    if ($Matches.Count -le $Occurrence) { return $null }
    return $Matches[$Occurrence]
}
function Close-ExplorerWindow([string]$WindowId, [int]$ExpectedCount) {
    [void](Invoke-CommandAndWait "taskbar.close $WindowId" "Close requested")
    return Wait-ExplorerState $ExpectedCount $ExpectedCount
}
function Assert-CurrentInvariant([string]$Description) {
    $State = Get-ExplorerState
    Assert-Phase20 (Test-OwnerInvariant $State) $Description
    return $State
}

try {
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $Exe
    $Info.WorkingDirectory = $Root
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardInput = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Runner = [Phase20LifecycleRunner]::new($Info)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }
    $Runner.Send("gui.start")
    Start-Sleep -Milliseconds 350
    $Baseline = Wait-ExplorerState 0 0
    Assert-Phase20 ($Baseline.Windows.Count -eq 0 -and $Baseline.LiveLines.Count -eq 0) "fresh runtime starts at zero File Explorer windows and processes"

    # Healthy single and sequential cases.
    $State = Open-Folder $VirtualA 1
    $Window = Find-Window $State $VirtualA
    Assert-Phase20 ($Window -and $Window.Groups[1].Value -and $Window.Groups[2].Value) "single activation has a process/window owner pair"
    [void](Close-ExplorerWindow $Window.Groups[1].Value 0)
    [void](Assert-CurrentInvariant "single final-window close leaves no live process without a window")
    $State = Open-Folder $VirtualB 1
    $Window = Find-Window $State $VirtualB
    [void](Close-ExplorerWindow $Window.Groups[1].Value 0)
    Assert-Phase20 ((Get-ExplorerState).LiveLines.Count -eq 0) "sequential launch after close returns to baseline"

    # Overlap different folders, close in launch order.
    [void](Open-Folder $VirtualA 1)
    $State = Open-Folder $VirtualB 2
    $WindowA = Find-Window $State $VirtualA
    $WindowB = Find-Window $State $VirtualB
    Assert-Phase20 ($WindowA -and $WindowB -and $WindowA.Groups[2].Value -ne $WindowB.Groups[2].Value -and $WindowA.Groups[1].Value -ne $WindowB.Groups[1].Value) "different-folder overlap owns two distinct processes and windows"
    [void](Close-ExplorerWindow $WindowA.Groups[1].Value 1)
    [void](Assert-CurrentInvariant "closing A leaves B's process/window ownership intact")
    [void](Close-ExplorerWindow $WindowB.Groups[1].Value 0)
    Assert-Phase20 ((Get-ExplorerState).LiveLines.Count -eq 0) "A then B close order returns to baseline"

    # Reverse close order.
    [void](Open-Folder $VirtualA 1)
    $State = Open-Folder $VirtualB 2
    $WindowA = Find-Window $State $VirtualA
    $WindowB = Find-Window $State $VirtualB
    [void](Close-ExplorerWindow $WindowB.Groups[1].Value 1)
    [void](Assert-CurrentInvariant "closing B leaves A's process/window ownership intact")
    [void](Close-ExplorerWindow $WindowA.Groups[1].Value 0)
    Assert-Phase20 ((Get-ExplorerState).LiveLines.Count -eq 0) "B then A close order returns to baseline"

    # Same folder is still two independent external activations.
    [void](Open-Folder $VirtualA 1)
    $State = Open-Folder $VirtualA 2
    $SameWindows = @($State.Windows | Where-Object { $_.Groups[4].Value -eq $VirtualA })
    Assert-Phase20 ($SameWindows.Count -eq 2 -and $SameWindows[0].Groups[2].Value -ne $SameWindows[1].Groups[2].Value) "same-folder overlap preserves two independently owned windows"
    [void](Close-ExplorerWindow $SameWindows[1].Groups[1].Value 1)
    [void](Close-ExplorerWindow $SameWindows[0].Groups[1].Value 0)
    Assert-Phase20 ((Get-ExplorerState).LiveLines.Count -eq 0) "same-folder overlap closes without an orphan"

    # Three independent instances close in non-launch order.
    [void](Open-Folder $VirtualA 1)
    [void](Open-Folder $VirtualB 2)
    $State = Open-Folder $VirtualC 3
    $WindowA = Find-Window $State $VirtualA
    $WindowB = Find-Window $State $VirtualB
    $WindowC = Find-Window $State $VirtualC
    $DistinctPids = @(@($WindowA.Groups[2].Value, $WindowB.Groups[2].Value, $WindowC.Groups[2].Value) | Sort-Object -Unique)
    Assert-Phase20 ($WindowA -and $WindowB -and $WindowC -and $DistinctPids.Count -eq 3) "three overlapping paths retain distinct process/window owners"
    [void](Close-ExplorerWindow $WindowB.Groups[1].Value 2)
    [void](Close-ExplorerWindow $WindowA.Groups[1].Value 1)
    [void](Close-ExplorerWindow $WindowC.Groups[1].Value 0)
    Assert-Phase20 ((Get-ExplorerState).LiveLines.Count -eq 0) "three-instance non-launch close order returns to baseline"

    # Eight simultaneous activations exercise a bounded practical overlap,
    # with unique paths, PIDs, and windows closed in a shuffled order.
    $EightPaths = @($VirtualA, $VirtualB, $VirtualC, $VirtualD, $VirtualE, $VirtualF, $VirtualG, $VirtualH)
    $State = $null
    for ($Index = 0; $Index -lt $EightPaths.Count; $Index++) {
        $State = Open-Folder $EightPaths[$Index] ($Index + 1)
    }
    $EightWindows = @($EightPaths | ForEach-Object { Find-Window $State $_ })
    $EightPids = @($EightWindows | ForEach-Object { $_.Groups[2].Value } | Sort-Object -Unique)
    $EightWindowIds = @($EightWindows | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $ExactPaths = $EightWindows.Count -eq 8
    for ($Index = 0; $Index -lt $EightPaths.Count -and $ExactPaths; $Index++) {
        $ExactPaths = $EightWindows[$Index].Groups[4].Value -eq $EightPaths[$Index]
    }
    Assert-Phase20 ($ExactPaths -and $EightPids.Count -eq 8 -and $EightWindowIds.Count -eq 8 -and (Test-OwnerInvariant $State)) "eight overlapping folders retain eight exact path/process/window owner pairs"
    $CloseOrder = @(3, 1, 5, 0, 7, 2, 6, 4)
    for ($CloseIndex = 0; $CloseIndex -lt $CloseOrder.Count; $CloseIndex++) {
        $Remaining = $CloseOrder.Count - $CloseIndex - 1
        [void](Close-ExplorerWindow $EightWindows[$CloseOrder[$CloseIndex]].Groups[1].Value $Remaining)
        if ($Remaining -gt 0) { [void](Assert-CurrentInvariant "eight-instance close isolation leaves $Remaining owned processes/windows") }
    }
    Assert-Phase20 ((Get-ExplorerState).LiveLines.Count -eq 0) "eight-instance shuffled close order returns to baseline"

    # Invalid external activation must fail before process/window creation.
    $BeforeInvalid = Get-ExplorerState
    foreach ($InvalidPath in @($VirtualFile, "$VirtualRoot/missing", "/bad`tpath")) {
        [void]$Runner.Send("desktop.open.folder `"$InvalidPath`"")
        Start-Sleep -Milliseconds 150
        $AfterInvalid = Get-ExplorerState
        Assert-Phase20 ($AfterInvalid.Windows.Count -eq $BeforeInvalid.Windows.Count -and $AfterInvalid.LiveLines.Count -eq $BeforeInvalid.LiveLines.Count) "invalid folder activation creates no process/window ($InvalidPath)"
    }
    # Preserve Phase 16's unquoted overlong command form. Quoting a line longer
    # than the console reader's bound splits the command into two input lines.
    $OverlongCommand = "desktop.open.folder /" + ("x" * 4096)
    [void](Invoke-CommandAndWait $OverlongCommand "Desktop folder activation failed: Invalid or overlong filesystem path")
    $AfterOverlong = Get-ExplorerState
    Assert-Phase20 ($AfterOverlong.Windows.Count -eq $BeforeInvalid.Windows.Count -and $AfterOverlong.LiveLines.Count -eq $BeforeInvalid.LiveLines.Count) "overlong folder activation creates no process/window"

    # The stress loop alternates different-folder and same-folder overlap and
    # alternates close order, asserting the authoritative process table/window map.
    $StressPassed = 0
    for ($Cycle = 1; $Cycle -le $StressCycles; $Cycle++) {
        $PathA = if (($Cycle % 2) -eq 0) { $VirtualA } else { $VirtualB }
        $PathB = if (($Cycle % 2) -eq 0) { $VirtualB } else { $VirtualA }
        if (($Cycle % 4) -eq 0) { $PathB = $PathA }
        [void](Open-Folder $PathA 1 $false)
        $State = Open-Folder $PathB 2 $false
        $WinA = Find-Window $State $PathA
        if ($PathA -eq $PathB) { $WinB = @($State.Windows | Where-Object { $_.Groups[4].Value -eq $PathB })[1] }
        else { $WinB = Find-Window $State $PathB }
        $OwnersValid = $WinA -and $WinB -and $WinA.Groups[2].Value -ne $WinB.Groups[2].Value -and (Test-OwnerInvariant $State)
        if (-not $OwnersValid) { throw "Stress cycle $Cycle path/process ownership mismatch." }
        if (($Cycle % 2) -eq 0) {
            [void](Close-ExplorerWindow $WinB.Groups[1].Value 1)
            [void](Close-ExplorerWindow $WinA.Groups[1].Value 0)
        } else {
            [void](Close-ExplorerWindow $WinA.Groups[1].Value 1)
            [void](Close-ExplorerWindow $WinB.Groups[1].Value 0)
        }
        $Final = Get-ExplorerState
        if ($Final.Windows.Count -ne 0 -or $Final.LiveLines.Count -ne 0 -or -not (Test-OwnerInvariant $Final)) {
            throw "Stress cycle $Cycle failed final baseline restoration.`n$($Final.WindowBlock)`n$($Final.ProcessBlock)"
        }
        $StressPassed++
        if (($Cycle % 10) -eq 0) { Write-Host "Phase 20 overlap stress: $Cycle/$StressCycles cycles returned to baseline." }
    }
    Assert-Phase20 ($StressPassed -eq $StressCycles) "$StressPassed/$StressCycles overlap cycles have zero orphan processes/windows and preserve path ownership"
    $Timeline.Add("stressCycles=$StressPassed")
    Write-Host "Phase 20 File Explorer lifecycle smoke: checks=$Checks failures=$Failures stressCycles=$StressPassed"
    if ($Failures -ne 0) { throw "Phase 20 lifecycle smoke had $Failures assertion failures." }
} finally {
    if ($Runner) { [IO.File]::WriteAllText($RuntimeLogPath, $Runner.Output()) }
    if ($Runner) { $Runner.Stop(5000); $Runner.Dispose() }
    foreach ($Entry in $Snapshots.GetEnumerator()) {
        $Snapshot = $Entry.Value
        if ($Snapshot.Exists) {
            [IO.File]::WriteAllBytes($Snapshot.Path, $Snapshot.Bytes)
            [IO.File]::SetAttributes($Snapshot.Path, $Snapshot.Attributes)
            [IO.File]::SetLastWriteTimeUtc($Snapshot.Path, $Snapshot.LastWriteTimeUtc)
        } elseif (Test-Path -LiteralPath $Snapshot.Path -PathType Leaf) {
            Remove-Item -LiteralPath $Snapshot.Path -Force
        }
    }
    [IO.File]::WriteAllLines($ReportPath, $Timeline)
    Write-Host "Phase 20 lifecycle report: $ReportPath"
}
