[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 30,
    [int]$StressCycles = 100
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build.bat before the Phase 22 Notepad isolation smoke."
}
if ($StressCycles -lt 100) {
    throw "Phase 22 runtime proof requires at least 100 overlap cycles."
}

$FixtureLeaf = "phase22-notepad-isolation-$([Guid]::NewGuid().ToString('N'))"
$FixtureRoot = Join-Path $Root (Join-Path "tmp" $FixtureLeaf)
$VirtualRoot = "/tmp/$FixtureLeaf"
$RuntimeLogPath = Join-Path $Root "tmp\phase22-notepad-isolation-$FixtureLeaf.runtime.log"
$ReportPath = Join-Path $Root "tmp\phase22-notepad-isolation-$FixtureLeaf.log"
$TimeoutMs = $TimeoutSeconds * 1000
$SnapshotPaths = @("desktop.json", "desktop.state", "window-bounds.cfg", "appmodel-default-handlers.cfg", "display-options.cfg")
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
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Threading;
public sealed class Phase22NotepadRunner : IDisposable {
    private const int MaxLines = 50000;
    private readonly Process process;
    private readonly object sync = new object();
    private readonly Queue<string> output = new Queue<string>();
    private readonly Dictionary<string, PatternCounter> counters = new Dictionary<string, PatternCounter>();
    private sealed class PatternCounter { public Regex regex; public int count; }
    public Phase22NotepadRunner(ProcessStartInfo info) {
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
        while (DateTime.UtcNow < until) { if (Count(pattern) >= expected) return true; Thread.Sleep(20); }
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
$Timeline = [System.Collections.Generic.List[string]]::new()
function Assert-Phase22([bool]$Condition, [string]$Description) {
    $script:Checks++
    if (-not $Condition) {
        $script:Timeline.Add("FAIL: $Description")
        throw "Phase 22 assertion failed: $Description. Runtime log: $script:RuntimeLogPath"
    }
    $script:Timeline.Add("PASS: $Description")
}
function Wait-OutputCount([string]$Pattern, [int]$Expected, [int]$WaitMs = $script:TimeoutMs) {
    if (-not $script:Runner.WaitForCount($Pattern, $Expected, $WaitMs)) {
        [IO.File]::WriteAllText($script:RuntimeLogPath, $script:Runner.Output())
        throw "Timed out waiting for pattern '$Pattern' count $Expected. Runtime log: $script:RuntimeLogPath"
    }
}
function Invoke-CommandAndWait([string]$Command, [string]$Pattern, [int]$WaitMs = $script:TimeoutMs) {
    $Before = $script:Runner.Count($Pattern)
    $script:Runner.Send($Command)
    Wait-OutputCount $Pattern ($Before + 1) $WaitMs
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
    $Output = Invoke-CommandAndWait "taskmanager.snapshot" "syntheticCounters=false"
    return Get-DelimitedBlock $Output "tabs=Processes,Performance,Tombstoned,Memory Details" "syntheticCounters=false"
}
function Get-AppState([string]$AppId) {
    $WindowBlock = Get-WindowBlock
    $ProcessBlock = Get-ProcessBlock
    $EscapedId = [regex]::Escape($AppId)
    $WindowMatches = [regex]::Matches($WindowBlock, "(?m)^window id=(\d+) ownerPid=(\d+) ownerName=([^\s]+) appId=$EscapedId title=(.*?) visible=")
    $ProcessMatches = [regex]::Matches($ProcessBlock, "(?m)^processRow pid=(\d+) appId=$EscapedId [^\r\n]*running=(true|false)")
    $Live = @($ProcessMatches | Where-Object { $_.Groups[2].Value -eq "true" })
    return [pscustomobject]@{
        AppId = $AppId
        WindowBlock = $WindowBlock
        ProcessBlock = $ProcessBlock
        Windows = @($WindowMatches)
        Processes = @($ProcessMatches)
        LiveProcesses = $Live
    }
}
function Test-OwnerInvariant($State) {
    if ($State.Windows.Count -ne $State.LiveProcesses.Count) { return $false }
    $LivePids = @($State.LiveProcesses | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if ($LivePids.Count -ne $State.LiveProcesses.Count) { return $false }
    $WindowPids = @($State.Windows | ForEach-Object { $_.Groups[2].Value } | Sort-Object -Unique)
    if ($WindowPids.Count -ne $State.Windows.Count) { return $false }
    foreach ($Window in $State.Windows) {
        if ($LivePids -notcontains $Window.Groups[2].Value) { return $false }
    }
    return $true
}
function Wait-AppState([string]$AppId, [int]$ExpectedWindows, [int]$ExpectedLive, [int]$WaitMs = $script:TimeoutMs) {
    $Until = [DateTime]::UtcNow.AddMilliseconds($WaitMs)
    $State = $null
    while ([DateTime]::UtcNow -lt $Until) {
        $State = Get-AppState $AppId
        if ($State.Windows.Count -eq $ExpectedWindows -and $State.LiveProcesses.Count -eq $ExpectedLive -and (Test-OwnerInvariant $State)) {
            return $State
        }
        Start-Sleep -Milliseconds 40
    }
    throw "$AppId did not settle to windows=$ExpectedWindows liveProcesses=$ExpectedLive.`n$($State.WindowBlock)`n$($State.ProcessBlock)"
}
function Wait-PendingClose([int]$ExpectedLive, [int]$WaitMs = $script:TimeoutMs) {
    $Until = [DateTime]::UtcNow.AddMilliseconds($WaitMs)
    $State = $null
    do {
        $State = Get-AppState "gxos.builtin.notepad"
        if ($State.Windows.Count -eq 0 -and $State.LiveProcesses.Count -eq $ExpectedLive) { return $State }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $Until)
    throw "Pending Notepad close did not settle to windows=0 liveProcesses=$ExpectedLive.`n$($State.WindowBlock)`n$($State.ProcessBlock)"
}
function Get-WindowForPath($State, [string]$Path, [int]$Occurrence = 0, [bool]$Dirty = $false) {
    $ExpectedTitle = if ($Dirty) { "$Path* - Notepad" } else { "$Path - Notepad" }
    $Matches = @($State.Windows | Where-Object { $_.Groups[4].Value -ceq $ExpectedTitle })
    if ($Matches.Count -le $Occurrence) { return $null }
    return $Matches[$Occurrence]
}
function Wait-DocumentTitles([string[]]$Paths, [int]$WaitMs = $script:TimeoutMs) {
    $Until = [DateTime]::UtcNow.AddMilliseconds($WaitMs)
    $State = $null
    do {
        $State = Get-AppState "gxos.builtin.notepad"
        $AllPresent = $true
        foreach ($Path in $Paths) {
            if (-not (Get-WindowForPath $State $Path)) { $AllPresent = $false; break }
        }
        if ($AllPresent) { return $State }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $Until)
    throw "Notepad titles did not retain their exact owned paths: $($Paths -join ', ').`n$($State.WindowBlock)"
}
function Open-Document([string]$VirtualPath, [int]$ExpectedWindows) {
    $LoadedPattern = "Notepad: Loaded file: " + [regex]::Escape($VirtualPath) + " \((\d+) bytes\)"
    $Before = $script:Runner.Count($LoadedPattern)
    $Command = "desktop.open `"$VirtualPath`""
    $script:Runner.Send($Command)
    Wait-OutputCount $LoadedPattern ($Before + 1)
    $State = Wait-AppState "gxos.builtin.notepad" $ExpectedWindows $ExpectedWindows
    $State = Wait-DocumentTitles @($VirtualPath)
    return $State
}
function Send-Key([string]$WindowId, [int]$KeyCode, [string]$Action) {
    $Pattern = "Key queued window="
    [void](Invoke-CommandAndWait "gui.keyto $WindowId $KeyCode $Action 0" ([regex]::Escape($Pattern)))
}
function Send-ControlShortcut([string]$WindowId, [int]$KeyCode) {
    Send-Key $WindowId 17 "down"
    Send-Key $WindowId $KeyCode "down"
    Send-Key $WindowId $KeyCode "up"
    Send-Key $WindowId 17 "up"
}
function Type-EditorText([string]$WindowId, [string]$Text, [bool]$Replace = $false) {
    if ($Replace) { Send-ControlShortcut $WindowId 65 }
    foreach ($Character in $Text.ToCharArray()) {
        $KeyCode = if ($Character -eq "`n" -or $Character -eq "`r") { 13 } else { [int][char]$Character }
        if ($Character -eq "`r") { continue }
        Send-Key $WindowId $KeyCode "down"
        Send-Key $WindowId $KeyCode "up"
    }
}
function Wait-DirtyTitle([string]$Path, [bool]$Dirty) {
    $Until = [DateTime]::UtcNow.AddMilliseconds($script:TimeoutMs)
    do {
        $State = Get-AppState "gxos.builtin.notepad"
        if ((Get-WindowForPath $State $Path 0 $Dirty)) { return $State }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $Until)
    throw "Notepad title dirty=$Dirty did not settle for $Path.`n$($State.WindowBlock)"
}
function Read-FixtureText([string]$Leaf) { return [IO.File]::ReadAllText((Join-Path $FixtureRoot $Leaf)) }
function Assert-VfsReloadByteCount([string]$VirtualPath, [string]$ExpectedText, [string]$Description) {
    $BeforeState = Get-AppState "gxos.builtin.notepad"
    $BeforeIds = @($BeforeState.Windows | ForEach-Object { $_.Groups[1].Value })
    $ExpectedWindows = $BeforeState.Windows.Count + 1
    $ExpectedBytes = [Text.Encoding]::UTF8.GetByteCount($ExpectedText)
    $LoadedPattern = "Notepad: Loaded file: " + [regex]::Escape($VirtualPath) + " \($ExpectedBytes bytes\)"
    $BeforeLoaded = $script:Runner.Count($LoadedPattern)
    $script:Runner.Send("desktop.open `"$VirtualPath`"")
    Wait-OutputCount $LoadedPattern ($BeforeLoaded + 1)
    $State = Wait-AppState "gxos.builtin.notepad" $ExpectedWindows $ExpectedWindows
    $NewWindows = @($State.Windows | Where-Object { $BeforeIds -notcontains $_.Groups[1].Value })
    Assert-Phase22 ($NewWindows.Count -eq 1) "$Description (reopened VFS content size=$ExpectedBytes bytes)"
    if ($NewWindows.Count -eq 1) {
        [void](Close-Notepad $NewWindows[0].Groups[1].Value ($ExpectedWindows - 1))
    }
}
function Save-Document([string]$WindowId, [string]$VirtualPath) {
    $Pattern = "Notepad: Saved to " + [regex]::Escape($VirtualPath) + " \((\d+) bytes\)"
    $Before = $script:Runner.Count($Pattern)
    Send-ControlShortcut $WindowId 83
    Wait-OutputCount $Pattern ($Before + 1)
    [void](Wait-DirtyTitle $VirtualPath $false)
}
function Close-Notepad([string]$WindowId, [int]$Remaining) {
    [void](Invoke-CommandAndWait "gui.close $WindowId" "Close requested")
    return Wait-AppState "gxos.builtin.notepad" $Remaining $Remaining
}
function Move-Window([string]$WindowId, [int]$X, [int]$Y) {
    [void](Invoke-CommandAndWait "gui.move $WindowId $X $Y" "Move queued")
    Start-Sleep -Milliseconds 300
}
function Click-Window([string]$WindowId, [int]$X, [int]$Y) {
    [void](Invoke-CommandAndWait "gui.mouse $WindowId $X $Y 1 down" "Mouse queued window=$WindowId")
    [void](Invoke-CommandAndWait "gui.mouse $WindowId $X $Y 1 up" "Mouse queued window=$WindowId")
}

try {
    New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
    $Files = @{
        "A.txt" = "ALPHA-INSTANCE-A"
        "B.txt" = "BRAVO-INSTANCE-B with a different length`nsecond line B"
        "C.txt" = "CHARLIE-INSTANCE-C`nline two`nline three"
        "D.txt" = "DELTA-INSTANCE-D"
        "E.txt" = "ECHO-INSTANCE-E has extra text for unequal lengths"
        "F.txt" = "FOXTROT-INSTANCE-F`nF line two"
        "G.txt" = "GOLF-INSTANCE-G"
        "H.txt" = "HOTEL-INSTANCE-H`nline two`nline three`nline four"
        "A with spaces.txt" = "SPACE-PATH-INSTANCE"
        "same.txt" = "SAME-FILE-ORIGINAL`nbase line"
    }
    foreach ($Entry in $Files.GetEnumerator()) {
        [IO.File]::WriteAllText((Join-Path $FixtureRoot $Entry.Key), $Entry.Value, [Text.UTF8Encoding]::new($false))
    }
    [IO.File]::WriteAllBytes((Join-Path $FixtureRoot "empty.txt"), [byte[]]@())
    $LongLines = @(); for ($Index = 0; $Index -lt 48; $Index++) { $LongLines += ("LONG-DOCUMENT-LINE-{0:D2}-independent-content" -f $Index) }
    [IO.File]::WriteAllText((Join-Path $FixtureRoot "long.txt"), ($LongLines -join "`n"), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $FixtureRoot "unsupported.bmp"), "unsupported", [Text.UTF8Encoding]::new($false))

    $Virtual = @{}
    foreach ($Leaf in $Files.Keys) { $Virtual[$Leaf] = "$VirtualRoot/$Leaf" }
    foreach ($Leaf in @("empty.txt", "long.txt", "unsupported.bmp")) { $Virtual[$Leaf] = "$VirtualRoot/$Leaf" }

    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $Exe
    $Info.WorkingDirectory = $Root
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardInput = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Runner = [Phase22NotepadRunner]::new($Info)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }
    if (-not $Runner.WaitForCount('pbytes \| help \| quit/exit', 1, $TimeoutMs)) { throw "Hosted server prompt did not appear." }
    $Runner.Send("gui.start")
    Start-Sleep -Milliseconds 350
    [void](Wait-AppState "gxos.builtin.notepad" 0 0)
    Assert-Phase22 $true "fresh hosted runtime has zero Notepad windows and processes"

    # Single-instance baseline and clean sequential close.
    $State = Open-Document $Virtual["A.txt"] 1
    $WindowA = Get-WindowForPath $State $Virtual["A.txt"]
    Assert-Phase22 ($WindowA -and $WindowA.Groups[2].Value -and (Test-OwnerInvariant $State)) "single activation has the exact loaded path and a live PID/window owner pair"
    [void](Close-Notepad $WindowA.Groups[1].Value 0)
    $State = Open-Document $Virtual["B.txt"] 1
    $WindowB = Get-WindowForPath $State $Virtual["B.txt"]
    [void](Close-Notepad $WindowB.Groups[1].Value 0)
    Assert-Phase22 ((Get-AppState "gxos.builtin.notepad").LiveProcesses.Count -eq 0) "sequential A-close/B-open lifecycle returns to zero live Notepads"

    # Two different documents: per-document buffer, path, and dirty-state checks use production keys and saves.
    [void](Open-Document $Virtual["A.txt"] 1)
    $State = Open-Document $Virtual["B.txt"] 2
    $WindowA = Get-WindowForPath $State $Virtual["A.txt"]
    $WindowB = Get-WindowForPath $State $Virtual["B.txt"]
    $Distinct = $WindowA -and $WindowB -and $WindowA.Groups[1].Value -ne $WindowB.Groups[1].Value -and $WindowA.Groups[2].Value -ne $WindowB.Groups[2].Value -and (Test-OwnerInvariant $State)
    Assert-Phase22 $Distinct "two different documents retain distinct PID/window/path ownership"
    $InitialB = Read-FixtureText "B.txt"
    Type-EditorText $WindowA.Groups[1].Value "edit-alpha`nsecond-a" $true
    $State = Wait-DirtyTitle $Virtual["A.txt"] $true
    Assert-Phase22 ((Get-WindowForPath $State $Virtual["B.txt"] 0 $false) -and (Read-FixtureText "B.txt") -ceq $InitialB) "editing A marks only A dirty and leaves B's file and title unchanged"
    Save-Document $WindowA.Groups[1].Value $Virtual["A.txt"]
    $SavedA = "edit-alpha`nsecond-a"
    Assert-VfsReloadByteCount $Virtual["A.txt"] $SavedA "saving A changes only A and reopens its exact VFS byte length"
    Assert-Phase22 ((Read-FixtureText "B.txt") -ceq $InitialB -and (Get-WindowForPath (Get-AppState "gxos.builtin.notepad") $Virtual["B.txt"] 0 $false)) "saving A leaves B's fixture and clean title unchanged"
    Type-EditorText $WindowB.Groups[1].Value "edit-bravo" $true
    $State = Wait-DirtyTitle $Virtual["B.txt"] $true
    Assert-Phase22 ($null -ne (Get-WindowForPath $State $Virtual["A.txt"] 0 $false)) "editing B marks only B dirty and leaves A clean"
    Save-Document $WindowB.Groups[1].Value $Virtual["B.txt"]
    $SavedB = "edit-bravo"
    Assert-VfsReloadByteCount $Virtual["B.txt"] $SavedB "saving B changes only B and reopens its exact VFS byte length"

    Type-EditorText $WindowA.Groups[1].Value "-a2"
    [void](Wait-DirtyTitle $Virtual["A.txt"] $true)
    Type-EditorText $WindowB.Groups[1].Value "-b2"
    $State = Wait-DirtyTitle $Virtual["B.txt"] $true
    Assert-Phase22 ((Get-WindowForPath $State $Virtual["A.txt"] 0 $true) -and (Get-WindowForPath $State $Virtual["B.txt"] 0 $true)) "alternating edits retain independent dirty markers"
    Save-Document $WindowA.Groups[1].Value $Virtual["A.txt"]
    $SavedA = "edit-alpha`nsecond-a-a2"
    $State = Get-AppState "gxos.builtin.notepad"
    Assert-Phase22 ((Get-WindowForPath $State $Virtual["A.txt"] 0 $false) -and (Get-WindowForPath $State $Virtual["B.txt"] 0 $true)) "saving A leaves B's unsaved buffer and dirty state intact"
    Assert-VfsReloadByteCount $Virtual["B.txt"] $SavedB "reopening B while its original editor is dirty reads the previous saved VFS state"
    Type-EditorText $WindowB.Groups[1].Value "-b2"
    $SavedB = "edit-bravo-b2-b2"
    Save-Document $WindowB.Groups[1].Value $Virtual["B.txt"]
    Assert-VfsReloadByteCount $Virtual["A.txt"] $SavedA "alternating edits retain A's saved VFS state"
    Assert-VfsReloadByteCount $Virtual["B.txt"] $SavedB "alternating edits retain B's saved VFS state"

    # Closing A while B is dirty leaves B live, dirty, and on its original path.
    Type-EditorText $WindowB.Groups[1].Value "-still-dirty"
    $State = Wait-DirtyTitle $Virtual["B.txt"] $true
    [void](Close-Notepad $WindowA.Groups[1].Value 1)
    $State = Get-AppState "gxos.builtin.notepad"
    Assert-Phase22 ($State.LiveProcesses.Count -eq 1 -and (Get-WindowForPath $State $Virtual["B.txt"] 0 $true)) "closing clean A cannot close or save dirty B"
    $SavedB = "edit-bravo-b2-b2-still-dirty"
    Save-Document $WindowB.Groups[1].Value $Virtual["B.txt"]
    [void](Close-Notepad $WindowB.Groups[1].Value 0)

    # Reverse close order.
    [void](Open-Document $Virtual["A.txt"] 1)
    $State = Open-Document $Virtual["B.txt"] 2
    $WindowA = Get-WindowForPath $State $Virtual["A.txt"]
    $WindowB = Get-WindowForPath $State $Virtual["B.txt"]
    [void](Close-Notepad $WindowB.Groups[1].Value 1)
    $State = Get-AppState "gxos.builtin.notepad"
    Assert-Phase22 ($State.LiveProcesses.Count -eq 1 -and (Get-WindowForPath $State $Virtual["A.txt"])) "closing B first leaves A's PID, window, and path intact"
    [void](Close-Notepad $WindowA.Groups[1].Value 0)

    # Opening one path twice creates independent buffers; saves remain last-writer-wins on disk.
    [void](Open-Document $Virtual["same.txt"] 1)
    $State = Open-Document $Virtual["same.txt"] 2
    $SameWindows = @($State.Windows | Where-Object { $_.Groups[4].Value -ceq "$($Virtual['same.txt']) - Notepad" })
    Assert-Phase22 ($SameWindows.Count -eq 2 -and $SameWindows[0].Groups[1].Value -ne $SameWindows[1].Groups[1].Value -and $SameWindows[0].Groups[2].Value -ne $SameWindows[1].Groups[2].Value) "same-file overlap creates two independent process/window owners"
    Type-EditorText $SameWindows[0].Groups[1].Value "samefile-writer-a" $true
    $State = Wait-DirtyTitle $Virtual["same.txt"] $true
    Assert-Phase22 (@($State.Windows | Where-Object { $_.Groups[4].Value -ceq "$($Virtual['same.txt']) - Notepad" }).Count -eq 1) "editing one same-file instance leaves the other instance clean"
    Save-Document $SameWindows[0].Groups[1].Value $Virtual["same.txt"]
    Assert-Phase22 ($null -ne (Get-WindowForPath (Get-AppState "gxos.builtin.notepad") $Virtual["same.txt"] 0 $false)) "first same-file save leaves the other in-memory title clean"
    Assert-VfsReloadByteCount $Virtual["same.txt"] "samefile-writer-a" "first same-file writer persists before the later instance saves"
    Type-EditorText $SameWindows[1].Groups[1].Value "samefile-writer-b-longer" $true
    Save-Document $SameWindows[1].Groups[1].Value $Virtual["same.txt"]
    Assert-Phase22 $true "same-file concurrent saves retain existing last-writer-wins behavior"
    [void](Close-Notepad $SameWindows[1].Groups[1].Value 1)
    [void](Close-Notepad $SameWindows[0].Groups[1].Value 0)
    Assert-VfsReloadByteCount $Virtual["same.txt"] "samefile-writer-b-longer" "reopening after the second same-file save observes the later writer's distinct VFS byte length"

    # Empty, long, spaced-path, and failed document activation cases.
    [void](Open-Document $Virtual["empty.txt"] 1)
    $State = Open-Document $Virtual["long.txt"] 2
    Assert-Phase22 ($script:Runner.Output().Contains("Notepad: Loaded file: $($Virtual['empty.txt']) (0 bytes)") -and $script:Runner.Output().Contains("Notepad: Loaded file: $($Virtual['long.txt']) ($([IO.File]::ReadAllBytes((Join-Path $FixtureRoot 'long.txt')).Length) bytes)")) "empty and longer multiline documents load with their exact owned paths and byte counts"
    [void](Close-Notepad (Get-WindowForPath $State $Virtual["empty.txt"]).Groups[1].Value 1)
    [void](Close-Notepad (Get-WindowForPath $State $Virtual["long.txt"]).Groups[1].Value 0)
    $State = Open-Document $Virtual["A with spaces.txt"] 1
    Assert-Phase22 ((Get-WindowForPath $State $Virtual["A with spaces.txt"]) -and (Read-FixtureText "A with spaces.txt")) "a spaced filename retains exact path ownership"
    $MissingVirtual = "$VirtualRoot/missing-file.txt"
    $BeforeMissing = $script:Runner.Count("Notepad: Failed to read file: " + [regex]::Escape($MissingVirtual))
    $script:Runner.Send("desktop.open `"$MissingVirtual`"")
    Wait-OutputCount ("Notepad: Failed to read file: " + [regex]::Escape($MissingVirtual)) ($BeforeMissing + 1)
    Wait-OutputCount 'Notepad: Document activation failed; closing unowned editor window' 1
    $State = Wait-AppState "gxos.builtin.notepad" 1 1
    Assert-Phase22 ((Get-WindowForPath $State $Virtual["A with spaces.txt"]) -and $State.LiveProcesses.Count -eq 1) "a failed document activation exits without leaving a stale process/window or disturbing the open editor"
    $Unsupported = $Virtual["unsupported.bmp"]
    $BeforeUnsupported = $script:Runner.Count("Desktop filesystem open failed: No file association registered for " + [regex]::Escape($Unsupported))
    $script:Runner.Send("desktop.open `"$Unsupported`"")
    Wait-OutputCount ("Desktop filesystem open failed: No file association registered for " + [regex]::Escape($Unsupported)) ($BeforeUnsupported + 1)
    $State = Wait-AppState "gxos.builtin.notepad" 1 1
    Assert-Phase22 ((Get-WindowForPath $State $Virtual["A with spaces.txt"]) -and $State.LiveProcesses.Count -eq 1) "unsupported activation creates no Notepad process/window and leaves the current document intact"
    [void](Close-Notepad (Get-WindowForPath $State $Virtual["A with spaces.txt"]).Groups[1].Value 0)

    # Three and eight distinct documents, shuffled close, and final cleanup.
    [void](Open-Document $Virtual["A.txt"] 1)
    [void](Open-Document $Virtual["B.txt"] 2)
    $State = Open-Document $Virtual["C.txt"] 3
    $ThreePaths = @($Virtual["A.txt"], $Virtual["B.txt"], $Virtual["C.txt"])
    $ThreeWindows = @($ThreePaths | ForEach-Object { Get-WindowForPath $State $_ })
    $ThreePids = @($ThreeWindows | ForEach-Object { $_.Groups[2].Value } | Sort-Object -Unique)
    Assert-Phase22 ($ThreeWindows.Count -eq 3 -and $ThreePids.Count -eq 3 -and (Test-OwnerInvariant $State)) "three simultaneous documents have independent PID/window/path state"
    foreach ($Path in @($Virtual["B.txt"], $Virtual["A.txt"], $Virtual["C.txt"])) {
        $State = Get-AppState "gxos.builtin.notepad"
        [void](Close-Notepad (Get-WindowForPath $State $Path).Groups[1].Value ($State.Windows.Count - 1))
    }
    Assert-Phase22 ((Get-AppState "gxos.builtin.notepad").LiveProcesses.Count -eq 0) "three-instance shuffled close order returns to zero live processes/windows"

    $EightLeaves = @("A.txt", "B.txt", "C.txt", "D.txt", "E.txt", "F.txt", "G.txt", "H.txt")
    $State = $null
    for ($Index = 0; $Index -lt $EightLeaves.Count; $Index++) { $State = Open-Document $Virtual[$EightLeaves[$Index]] ($Index + 1) }
    $EightWindows = @($EightLeaves | ForEach-Object { Get-WindowForPath $State $Virtual[$_] })
    $EightPids = @($EightWindows | ForEach-Object { $_.Groups[2].Value } | Sort-Object -Unique)
    $EightIds = @($EightWindows | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    Assert-Phase22 ($EightWindows.Count -eq 8 -and $EightPids.Count -eq 8 -and $EightIds.Count -eq 8 -and (Test-OwnerInvariant $State)) "eight simultaneous documents preserve eight independent PID/window/path owners"
    foreach ($Index in @(3, 1, 5, 0, 7, 2, 6, 4)) {
        $State = Get-AppState "gxos.builtin.notepad"
        $Path = $Virtual[$EightLeaves[$Index]]
        [void](Close-Notepad (Get-WindowForPath $State $Path).Groups[1].Value ($State.Windows.Count - 1))
    }
    Assert-Phase22 ((Get-AppState "gxos.builtin.notepad").LiveProcesses.Count -eq 0) "eight-instance shuffled close order returns to the final baseline"

    # Open and Save As dialogs each retain their own process/window state under overlap.
    [void](Open-Document $Virtual["A.txt"] 1)
    $State = Open-Document $Virtual["B.txt"] 2
    $WindowA = Get-WindowForPath $State $Virtual["A.txt"]
    $WindowB = Get-WindowForPath $State $Virtual["B.txt"]
    Move-Window $WindowA.Groups[1].Value 20 30
    Move-Window $WindowB.Groups[1].Value 660 30
    $BeforeSaveDialogs = $script:Runner.Count('SaveDialog window created: (\d+)')
    Click-Window $WindowA.Groups[1].Value 280 12
    Click-Window $WindowB.Groups[1].Value 280 12
    Wait-OutputCount 'SaveDialog window created: (\d+)' ($BeforeSaveDialogs + 2)
    $State = Wait-AppState "gxos.dialog.savedialog" 2 2
    $SaveDialogA = $State.Windows[0]
    $SaveDialogB = $State.Windows[1]
    Assert-Phase22 ($SaveDialogA.Groups[1].Value -ne $SaveDialogB.Groups[1].Value -and $SaveDialogA.Groups[2].Value -ne $SaveDialogB.Groups[2].Value -and (Test-OwnerInvariant $State)) "two concurrent Save As dialogs own distinct windows, process IDs, paths, and callbacks"
    [void](Invoke-CommandAndWait "gui.close $($SaveDialogA.Groups[1].Value)" "Close requested")
    [void](Wait-AppState "gxos.dialog.savedialog" 1 1)
    [void](Invoke-CommandAndWait "gui.close $($SaveDialogB.Groups[1].Value)" "Close requested")
    [void](Wait-AppState "gxos.dialog.savedialog" 0 0)
    Assert-Phase22 $true "closing both Save As dialogs leaves zero live dialog processes and windows"

    $State = Get-AppState "gxos.builtin.notepad"
    $WindowA = Get-WindowForPath $State $Virtual["A.txt"]
    $WindowB = Get-WindowForPath $State $Virtual["B.txt"]
    Send-ControlShortcut $WindowA.Groups[1].Value 79
    Send-ControlShortcut $WindowB.Groups[1].Value 79
    $State = Wait-AppState "gxos.dialog.opendialog" 2 2
    $OpenDialogs = @($State.Windows)
    Assert-Phase22 ($OpenDialogs.Count -eq 2 -and $OpenDialogs[0].Groups[1].Value -ne $OpenDialogs[1].Groups[1].Value -and $OpenDialogs[0].Groups[2].Value -ne $OpenDialogs[1].Groups[2].Value -and (Test-OwnerInvariant $State)) "two concurrent Open dialogs retain distinct process/window state"
    [void](Invoke-CommandAndWait "gui.close $($OpenDialogs[0].Groups[1].Value)" "Close requested")
    [void](Wait-AppState "gxos.dialog.opendialog" 1 1)
    [void](Invoke-CommandAndWait "gui.close $($OpenDialogs[1].Groups[1].Value)" "Close requested")
    [void](Wait-AppState "gxos.dialog.opendialog" 0 0)
    Assert-Phase22 $true "closing both Open dialogs leaves zero live dialog processes and windows"
    $State = Get-AppState "gxos.builtin.notepad"
    [void](Close-Notepad (Get-WindowForPath $State $Virtual["B.txt"]).Groups[1].Value 1)
    [void](Close-Notepad (Get-WindowForPath (Get-AppState "gxos.builtin.notepad") $Virtual["A.txt"]).Groups[1].Value 0)

    # Dirty close prompts return the decision to the matching Notepad process.
    [void](Open-Document $Virtual["A.txt"] 1)
    $State = Open-Document $Virtual["B.txt"] 2
    $WindowA = Get-WindowForPath $State $Virtual["A.txt"]
    $WindowB = Get-WindowForPath $State $Virtual["B.txt"]
    $OriginalA = $SavedA
    $OriginalB = $SavedB
    Type-EditorText $WindowA.Groups[1].Value "dirty-discard-a" $true
    Type-EditorText $WindowB.Groups[1].Value "dirty-save-b" $true
    [void](Wait-DirtyTitle $Virtual["A.txt"] $true)
    [void](Wait-DirtyTitle $Virtual["B.txt"] $true)
    $FirstPromptCount = $script:Runner.Count('SaveChangesDialog window created: (\d+)')
    [void](Invoke-CommandAndWait "gui.close $($WindowA.Groups[1].Value)" "Close requested")
    Wait-OutputCount 'SaveChangesDialog window created: (\d+)' ($FirstPromptCount + 1)
    [void](Invoke-CommandAndWait "gui.close $($WindowB.Groups[1].Value)" "Close requested")
    Wait-OutputCount 'SaveChangesDialog window created: (\d+)' ($FirstPromptCount + 2)
    $PromptState = Wait-AppState "gxos.dialog.savechangesdialog" 2 2
    $PromptA = $PromptState.Windows[0]
    $PromptB = $PromptState.Windows[1]
    Assert-Phase22 ($PromptA.Groups[1].Value -ne $PromptB.Groups[1].Value -and $PromptA.Groups[2].Value -ne $PromptB.Groups[2].Value -and (Test-OwnerInvariant $PromptState)) "two dirty-close prompts retain separate process/window/callback ownership"
    # Both prompts open at the same position; act on the newest topmost prompt
    # first, then the older prompt becomes available at the same coordinates.
    Click-Window $PromptB.Groups[1].Value 50 135
    Wait-OutputCount "SaveChangesDialog: Save clicked" 1
    Wait-OutputCount ("Notepad: Saved to " + [regex]::Escape($Virtual["B.txt"]) + " \((\d+) bytes\)") 1
    Wait-OutputCount "Notepad: Closing after save changes decision" 1
    [void](Wait-PendingClose 1)
    [void](Wait-AppState "gxos.dialog.savechangesdialog" 1 1)
    Click-Window $PromptA.Groups[1].Value 150 135
    Wait-OutputCount "SaveChangesDialog: Don't Save clicked" 1
    Wait-OutputCount "Notepad: Closing after save changes decision" 2
    [void](Wait-AppState "gxos.builtin.notepad" 0 0)
    [void](Wait-AppState "gxos.dialog.savechangesdialog" 0 0)
    Assert-VfsReloadByteCount $Virtual["A.txt"] $OriginalA "discarding A preserves its previously saved VFS state"
    Assert-VfsReloadByteCount $Virtual["B.txt"] "dirty-save-b" "saving B from its close prompt persists B's own VFS byte length"

    # Cancel keeps the dirty buffer alive by restoring its editor window.
    [void](Open-Document $Virtual["A.txt"] 1)
    $State = Get-AppState "gxos.builtin.notepad"
    $WindowA = Get-WindowForPath $State $Virtual["A.txt"]
    $BeforePrompt = $script:Runner.Count('SaveChangesDialog window created: (\d+)')
    Type-EditorText $WindowA.Groups[1].Value "dirty-cancel-a" $true
    [void](Wait-DirtyTitle $Virtual["A.txt"] $true)
    [void](Invoke-CommandAndWait "gui.close $($WindowA.Groups[1].Value)" "Close requested")
    Wait-OutputCount 'SaveChangesDialog window created: (\d+)' ($BeforePrompt + 1)
    $CancelDialog = Wait-AppState "gxos.dialog.savechangesdialog" 1 1
    Click-Window $CancelDialog.Windows[0].Groups[1].Value 270 135
    Wait-OutputCount "SaveChangesDialog: Cancel clicked" 1
    Wait-OutputCount "Notepad: Close prompt cancelled" 1
    $State = Wait-AppState "gxos.builtin.notepad" 1 1
    [void](Wait-AppState "gxos.dialog.savechangesdialog" 0 0)
    Assert-Phase22 ($null -ne (Get-WindowForPath $State $Virtual["A.txt"] 0 $true)) "Cancel restores the same dirty document window"
    Assert-VfsReloadByteCount $Virtual["A.txt"] $OriginalA "Cancel keeps the previous saved VFS state unchanged"
    $BeforePrompt = $script:Runner.Count('SaveChangesDialog window created: (\d+)')
    [void](Invoke-CommandAndWait "gui.close $((Get-WindowForPath $State $Virtual["A.txt"] 0 $true).Groups[1].Value)" "Close requested")
    Wait-OutputCount 'SaveChangesDialog window created: (\d+)' ($BeforePrompt + 1)
    $CancelPrompt = Wait-AppState "gxos.dialog.savechangesdialog" 1 1
    Click-Window $CancelPrompt.Windows[0].Groups[1].Value 150 135
    Wait-OutputCount "SaveChangesDialog: Don't Save clicked" 2
    [void](Wait-AppState "gxos.builtin.notepad" 0 0)
    [void](Wait-AppState "gxos.dialog.savechangesdialog" 0 0)

    # Failed document activation while two healthy editors remain cannot contaminate either.
    [void](Open-Document $Virtual["A.txt"] 1)
    $State = Open-Document $Virtual["B.txt"] 2
    $BeforeMissing = $script:Runner.Count("Notepad: Failed to read file: " + [regex]::Escape($MissingVirtual))
    $script:Runner.Send("desktop.open `"$MissingVirtual`"")
    Wait-OutputCount ("Notepad: Failed to read file: " + [regex]::Escape($MissingVirtual)) ($BeforeMissing + 1)
    Wait-OutputCount 'Notepad: Document activation failed; closing unowned editor window' 2
    $State = Wait-AppState "gxos.builtin.notepad" 2 2
    Assert-Phase22 ((Get-WindowForPath $State $Virtual["A.txt"]) -and (Get-WindowForPath $State $Virtual["B.txt"]) -and (Test-OwnerInvariant $State)) "failed activation during overlap leaves both existing Notepads unchanged and no failed window"
    $State = Get-AppState "gxos.builtin.notepad"
    [void](Close-Notepad (Get-WindowForPath $State $Virtual["A.txt"]).Groups[1].Value 1)
    [void](Close-Notepad (Get-WindowForPath (Get-AppState "gxos.builtin.notepad") $Virtual["B.txt"]).Groups[1].Value 0)

    # Recent Programs remains the existing app-level, deduplicated entry.
    $Recent = Invoke-CommandAndWait "desktop.recent" "Recent Documents \(\d+\):"
    $RecentProgramCount = [regex]::Matches($Recent, "(?m)^  Notepad\r?$").Count
    Assert-Phase22 ($RecentProgramCount -eq 1) "multiple document launches retain one deduplicated Notepad Recent Programs entry"

    # Repeated overlap with alternating close order; every cycle returns to exact process/window baseline.
    $StressPassed = 0
    for ($Cycle = 1; $Cycle -le $StressCycles; $Cycle++) {
        [void](Open-Document $Virtual["A.txt"] 1)
        $State = Open-Document $Virtual["B.txt"] 2
        $WindowA = Get-WindowForPath $State $Virtual["A.txt"]
        $WindowB = Get-WindowForPath $State $Virtual["B.txt"]
        if (-not ($WindowA -and $WindowB -and $WindowA.Groups[2].Value -ne $WindowB.Groups[2].Value -and (Test-OwnerInvariant $State))) {
            throw "Stress cycle $Cycle did not retain two distinct path/PID/window owners."
        }
        if (($Cycle % 2) -eq 0) {
            [void](Close-Notepad $WindowB.Groups[1].Value 1)
            [void](Close-Notepad $WindowA.Groups[1].Value 0)
        } else {
            [void](Close-Notepad $WindowA.Groups[1].Value 1)
            [void](Close-Notepad $WindowB.Groups[1].Value 0)
        }
        $Final = Get-AppState "gxos.builtin.notepad"
        if ($Final.Windows.Count -ne 0 -or $Final.LiveProcesses.Count -ne 0 -or -not (Test-OwnerInvariant $Final)) {
            throw "Stress cycle $Cycle failed zero-process/zero-window baseline.`n$($Final.WindowBlock)`n$($Final.ProcessBlock)"
        }
        $StressPassed++
        if (($Cycle % 10) -eq 0) { Write-Host "Phase 22 Notepad overlap stress: $Cycle/$StressCycles cycles returned to baseline." }
    }
    Assert-Phase22 ($StressPassed -eq $StressCycles) "$StressPassed overlap cycles had zero path mismatches, buffer contamination, orphan Notepads, or orphan windows"

    $FinalNotepads = Get-AppState "gxos.builtin.notepad"
    $FinalSaveDialogs = Get-AppState "gxos.dialog.savedialog"
    $FinalOpenDialogs = Get-AppState "gxos.dialog.opendialog"
    $FinalCloseDialogs = Get-AppState "gxos.dialog.savechangesdialog"
    Assert-Phase22 ($FinalNotepads.Windows.Count -eq 0 -and $FinalNotepads.LiveProcesses.Count -eq 0 -and $FinalSaveDialogs.Windows.Count -eq 0 -and $FinalSaveDialogs.LiveProcesses.Count -eq 0 -and $FinalOpenDialogs.Windows.Count -eq 0 -and $FinalOpenDialogs.LiveProcesses.Count -eq 0 -and $FinalCloseDialogs.Windows.Count -eq 0 -and $FinalCloseDialogs.LiveProcesses.Count -eq 0) "all Notepad and dialog processes/windows return to zero after qualification"

    $Timeline.Add("stressCycles=$StressPassed")
    $Timeline.Add("maximumSimultaneousDocuments=8")
    Write-Output "phase22NotepadIsolationChecks=$Checks/$Checks"
    Write-Output "overlapStressCycles=$StressPassed/$StressCycles"
    Write-Output "maximumSimultaneousDocuments=8"
    Write-Output "finalNotepadProcesses=0"
    Write-Output "finalNotepadWindows=0"
} finally {
    if ($Runner) { [IO.File]::WriteAllText($RuntimeLogPath, $Runner.Output()); $Runner.Stop(5000); $Runner.Dispose() }
    foreach ($Entry in $Snapshots.GetEnumerator()) {
        $Snapshot = $Entry.Value
        if ($Snapshot.Exists) {
            [IO.File]::WriteAllBytes($Snapshot.Path, [Convert]::FromBase64String($Snapshot.BytesBase64))
            [IO.File]::SetAttributes($Snapshot.Path, $Snapshot.Attributes)
            [IO.File]::SetLastWriteTimeUtc($Snapshot.Path, $Snapshot.LastWriteTimeUtc)
        } elseif (Test-Path -LiteralPath $Snapshot.Path -PathType Leaf) {
            Remove-Item -LiteralPath $Snapshot.Path -Force
        }
    }
    [IO.File]::WriteAllLines($ReportPath, $Timeline)
    Write-Host "Phase 22 Notepad runtime log: $RuntimeLogPath"
    Write-Host "Phase 22 Notepad summary: $ReportPath"
}
