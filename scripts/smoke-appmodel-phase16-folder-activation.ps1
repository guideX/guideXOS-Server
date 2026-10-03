[CmdletBinding()]
param([int]$TimeoutSeconds = 30)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build.bat before the Phase 16 folder activation smoke."
}

$FixtureLeaf = "phase16-folder-activation-$([Guid]::NewGuid().ToString('N'))"
$FixtureRoot = Join-Path $Root (Join-Path "tmp" $FixtureLeaf)
$Tree = Join-Path $FixtureRoot "nested folder"
$Child = Join-Path $Tree "inside"
$Empty = Join-Path $FixtureRoot "empty folder"
$Spaced = Join-Path $FixtureRoot "folder with spaces"
$Dotted = Join-Path $FixtureRoot "example.test"
$FileNamedFolder = Join-Path (Join-Path $FixtureRoot "archive") "photo.jpg"
$Document = Join-Path $Tree "readme.txt"
New-Item -ItemType Directory -Force -Path $Child, $Empty, $Spaced, $Dotted, $FileNamedFolder | Out-Null
[IO.File]::WriteAllText($Document, "Phase 16 folder activation document fixture.`r`n")

$VirtualRoot = "/tmp/$FixtureLeaf"
$VirtualTree = "$VirtualRoot/nested folder"
$VirtualChild = "$VirtualTree/inside"
$VirtualEmpty = "$VirtualRoot/empty folder"
$VirtualSpaced = "$VirtualRoot/folder with spaces"
$VirtualDotted = "$VirtualRoot/example.test"
$VirtualFileNamedFolder = "$VirtualRoot/archive/photo.jpg"
$VirtualDocument = "$VirtualTree/readme.txt"

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

$RunnerSource = @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Threading;
public sealed class Phase16FolderRunner : IDisposable {
    private readonly Process process;
    private readonly ConcurrentQueue<string> output = new ConcurrentQueue<string>();
    public Phase16FolderRunner(ProcessStartInfo info) {
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
        while (DateTime.UtcNow < until) { if (Count(pattern) >= expected) return true; Thread.Sleep(50); }
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
function Assert-Phase16([bool]$Condition, [string]$Description) {
    $script:Checks++
    if ($Condition) { Write-Host "PASS: $Description" }
    else { $script:Failures++; Write-Host "FAIL: $Description" }
}
function Send-Command([string]$Command) { $script:Runner.Send($Command) }
function Wait-Output([string]$Pattern, [int]$Timeout = ($TimeoutSeconds * 1000)) {
    if (-not $script:Runner.WaitForCount($Pattern, 1, $Timeout)) {
        $Output = $script:Runner.Output(); $Start = [Math]::Max(0, $Output.Length - 9000)
        throw "Timed out waiting for runtime pattern: $Pattern`n$($Output.Substring($Start))"
    }
    return $script:Runner.Output()
}
function Invoke-CommandAndWait([string]$Command, [string]$Pattern, [int]$Timeout = ($TimeoutSeconds * 1000)) {
    $Previous = $script:Runner.Count($Pattern)
    Send-Command $Command
    if (-not $script:Runner.WaitForCount($Pattern, $Previous + 1, $Timeout)) {
        $Output = $script:Runner.Output(); $Start = [Math]::Max(0, $Output.Length - 9000)
        throw "Timed out waiting for command '$Command' output matching: $Pattern`n$($Output.Substring($Start))"
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
function Wait-ExplorerWindow() {
    $Until = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $Block = Get-WindowBlock
    while (-not $Block.Contains("appId=gxos.builtin.fileexplorer") -and [DateTime]::UtcNow -lt $Until) {
        Start-Sleep -Milliseconds 75
        $Block = Get-WindowBlock
    }
    return $Block
}
function Get-ProcessSnapshot() {
    $Output = Invoke-CommandAndWait "taskmanager.snapshot" "(?m)^syntheticCounters="
    return $Output
}
function Get-ExplorerWindow([string]$Block) {
    return [regex]::Match($Block, "(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=gxos\.builtin\.fileexplorer title=File Explorer - (.*?) visible=")
}
function Close-Explorer([string]$WindowBlock) {
    $Window = Get-ExplorerWindow $WindowBlock
    if (-not $Window.Success) { throw "No owned File Explorer window found to close.`n$WindowBlock" }
    $WindowId = $Window.Groups[1].Value
    $OwnerProcessId = $Window.Groups[2].Value
    Send-Command "taskbar.close $WindowId"
    $Until = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $AfterBlock = $WindowBlock
    while ([DateTime]::UtcNow -lt $Until) {
        $AfterBlock = Get-WindowBlock
        if (-not $AfterBlock.Contains("appId=gxos.builtin.fileexplorer")) { break }
        Start-Sleep -Milliseconds 75
    }
    Assert-Phase16 (-not $AfterBlock.Contains("appId=gxos.builtin.fileexplorer")) "File Explorer compositor window closes cleanly"
    $Snapshot = Get-ProcessSnapshot
    $Stopped = "(?m)^processRow pid=$OwnerProcessId appId=gxos\.builtin\.fileexplorer .*running=false\r?$"
    $Until = [DateTime]::UtcNow.AddSeconds(5)
    while ($Snapshot -notmatch $Stopped -and [DateTime]::UtcNow -lt $Until) {
        Start-Sleep -Milliseconds 100
        $Snapshot = Get-ProcessSnapshot
    }
    Assert-Phase16 ($Snapshot -match $Stopped) "File Explorer activation process stops after its owned window closes"
}
function Open-FolderAndClose([string]$Command, [string]$ExpectedPath, [int]$ExpectedEntries = -1) {
    $Before = $script:Runner.Count("FileExplorer window created: (\d+)")
    $Pattern = "FileExplorer consumed owned folder activation appId=gxos\.builtin\.fileexplorer path=" + [regex]::Escape($ExpectedPath) + " entries=(\d+)"
    $BeforeOwned = $script:Runner.Count($Pattern)
    [void](Invoke-CommandAndWait $Command "Desktop folder activation successful:")
    if (-not $script:Runner.WaitForCount($Pattern, $BeforeOwned + 1, $TimeoutSeconds * 1000)) {
        throw "File Explorer did not consume the expected folder activation: $ExpectedPath"
    }
    $Output = $script:Runner.Output()
    $Match = [regex]::Match($Output, $Pattern)
    Assert-Phase16 ($Match.Success) "File Explorer consumes the exact normalized initial folder $ExpectedPath"
    if ($ExpectedEntries -ge 0) {
        Assert-Phase16 ($Match.Success -and [int]$Match.Groups[1].Value -eq $ExpectedEntries) "File Explorer enumerates $ExpectedEntries entries at $ExpectedPath"
    }
    $Until = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ($script:Runner.Count("FileExplorer window created: (\d+)") -lt ($Before + 1) -and [DateTime]::UtcNow -lt $Until) { Start-Sleep -Milliseconds 50 }
    $Windows = Wait-ExplorerWindow
    $Window = Get-ExplorerWindow $Windows
    $ExactWindowPath = $Window.Success -and $Window.Groups[3].Value -eq $ExpectedPath
    Assert-Phase16 $ExactWindowPath "folder activation owns a File Explorer window with the exact path title"
    if (-not $ExactWindowPath) { Write-Host "  expected title path='$ExpectedPath'; window block:`n$Windows" }
    if ($Window.Success) {
        $Processes = Get-ProcessSnapshot
        $OwnerProcessId = $Window.Groups[2].Value
        $Active = "(?m)^processRow pid=$OwnerProcessId appId=gxos\.builtin\.fileexplorer .*running=true\r?$"
        Assert-Phase16 ($Processes -match $Active) "File Explorer process identity and compositor owner agree for $ExpectedPath"
        Close-Explorer $Windows
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
    $Runner = [Phase16FolderRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }
    Send-Command "gui.start"
    Start-Sleep -Milliseconds 350

    Open-FolderAndClose 'desktop.open.folder /' '/'
    Open-FolderAndClose "desktop.open.folder `"$VirtualTree`"" $VirtualTree 2
    Open-FolderAndClose "desktop.open.folder `"$VirtualEmpty`"" $VirtualEmpty 0
    Open-FolderAndClose "desktop.open.folder `"$VirtualSpaced`"" $VirtualSpaced 0
    Open-FolderAndClose "desktop.open.folder `"$VirtualDotted`"" $VirtualDotted 0
    Open-FolderAndClose "desktop.open.folder `"$VirtualFileNamedFolder`"" $VirtualFileNamedFolder 0

    $NormalizedCommand = "desktop.open.folder `"$VirtualRoot//nested folder/../empty folder`""
    Open-FolderAndClose $NormalizedCommand $VirtualEmpty 0
    Assert-Phase16 $true "repeated separators and dot/parent segments follow File Explorer VFS normalization"

    $BeforeInvalidWindows = [regex]::Matches((Get-WindowBlock), "appId=gxos\.builtin\.fileexplorer").Count
    $BeforeInvalidProcesses = [regex]::Matches((Get-ProcessSnapshot), "(?m)^processRow .*appId=gxos\.builtin\.fileexplorer .*running=true\r?$").Count
    $BeforeOpenCount = $Runner.Count("FileExplorer window created: (\d+)")
    [void](Invoke-CommandAndWait "desktop.open.folder `"$VirtualDocument`"" "Desktop folder activation failed: Folder path exists but is not a directory")
    [void](Invoke-CommandAndWait "desktop.open.folder `"$VirtualRoot/missing-folder`"" "Desktop folder activation failed: Folder path not found")
    [void](Invoke-CommandAndWait ("desktop.open.folder /" + ("x" * 4096)) "Desktop folder activation failed: Invalid or overlong filesystem path")
    [void](Invoke-CommandAndWait "desktop.open.folder /bad`tpath" "Desktop folder activation failed: Invalid or overlong filesystem path")
    Start-Sleep -Milliseconds 250
    $AfterInvalidWindows = [regex]::Matches((Get-WindowBlock), "appId=gxos\.builtin\.fileexplorer").Count
    $AfterInvalidProcesses = [regex]::Matches((Get-ProcessSnapshot), "(?m)^processRow .*appId=gxos\.builtin\.fileexplorer .*running=true\r?$").Count
    $InvalidNoLaunch = $BeforeInvalidWindows -eq $AfterInvalidWindows -and $BeforeInvalidProcesses -eq $AfterInvalidProcesses -and
        $BeforeOpenCount -eq $Runner.Count("FileExplorer window created: (\d+)")
    Assert-Phase16 $InvalidNoLaunch "missing, file-as-folder, malformed, and overlong requests create no Explorer process or window"
    if (-not $InvalidNoLaunch) {
        Write-Host "  invalid-request counts windows=$BeforeInvalidWindows/$AfterInvalidWindows processes=$BeforeInvalidProcesses/$AfterInvalidProcesses creates=$BeforeOpenCount/$($Runner.Count('FileExplorer window created: (\d+)'))"
    }

    $BeforeNotepad = $Runner.Count("Notepad: Document activation received appId=gxos\.builtin\.notepad path=")
    Send-Command "desktop.open `"$VirtualDocument`" dir"
    $DocPattern = "Notepad: Document activation received appId=gxos\.builtin\.notepad path=" + [regex]::Escape($VirtualDocument)
    [void](Wait-Output $DocPattern)
    Assert-Phase16 ($Runner.Count("Notepad: Document activation received appId=gxos\.builtin\.notepad path=") -eq ($BeforeNotepad + 1)) "a real file remains document activation even when caller metadata says directory"
    $DocWindows = Get-WindowBlock
    $NotepadWindow = [regex]::Match($DocWindows, "(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=gxos\.builtin\.notepad title=")
    $UntilNotepad = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $NotepadWindow.Success -and [DateTime]::UtcNow -lt $UntilNotepad) {
        Start-Sleep -Milliseconds 100
        $DocWindows = Get-WindowBlock
        $NotepadWindow = [regex]::Match($DocWindows, "(?m)^window id=(\d+) ownerPid=(\d+) ownerName=[^\r\n]+ appId=gxos\.builtin\.notepad title=")
    }
    Assert-Phase16 $NotepadWindow.Success "document regression uses the canonical Notepad process/window"
    if ($NotepadWindow.Success) { Send-Command "taskbar.close $($NotepadWindow.Groups[1].Value)" }

    $Before = $Runner.Count("FileExplorer window created: (\d+)")
    $TreeActivationPattern = "FileExplorer consumed owned folder activation appId=gxos\.builtin\.fileexplorer path=" + [regex]::Escape($VirtualTree) + " entries=(\d+)"
    $BeforeTreeActivation = $Runner.Count($TreeActivationPattern)
    [void](Invoke-CommandAndWait "desktop.open.folder `"$VirtualTree`"" "Desktop folder activation successful:")
    if (-not $Runner.WaitForCount($TreeActivationPattern, $BeforeTreeActivation + 1, $TimeoutSeconds * 1000)) { throw "File Explorer did not consume the tree folder activation." }
    $TreeWindows = Wait-ExplorerWindow
    $TreeWindow = Get-ExplorerWindow $TreeWindows
    $RecentAfterExternalOutput = Invoke-CommandAndWait "desktop.recent" "Recent Documents \("
    $RecentAfterExternal = Get-DelimitedBlock $RecentAfterExternalOutput "Recent Programs (" "Recent Documents ("
    if ($TreeWindow.Success) {
        Send-Command "gui.keyto $($TreeWindow.Groups[1].Value) 13 down 0"
        [void](Wait-Output ("FileExplorer: Navigated to " + [regex]::Escape($VirtualChild)))
        $RecentAfterNavigationOutput = Invoke-CommandAndWait "desktop.recent" "Recent Documents \("
        $RecentAfterNavigation = Get-DelimitedBlock $RecentAfterNavigationOutput "Recent Programs (" "Recent Documents ("
        Assert-Phase16 ($RecentAfterExternal -eq $RecentAfterNavigation) "internal subfolder navigation remains inside File Explorer and does not add launch-history entries"
        $CurrentOutput = $Runner.Output()
        Assert-Phase16 ($CurrentOutput.Contains("FileExplorer: Navigated to $VirtualChild")) "existing File Explorer navigation model receives internal child navigation after external activation"
        Send-Command "gui.mouse $($TreeWindow.Groups[1].Value) 20 15 1 down"
        Send-Command "gui.mouse $($TreeWindow.Groups[1].Value) 20 15 1 up"
        [void](Wait-Output ("FileExplorer: Navigated to " + [regex]::Escape($VirtualTree)))
        Assert-Phase16 ($Runner.Output().Contains("FileExplorer: Navigated to $VirtualTree")) "existing File Explorer Back history returns to the external starting folder"
        $RecentAfterBackOutput = Invoke-CommandAndWait "desktop.recent" "Recent Documents \("
        $RecentAfterBack = Get-DelimitedBlock $RecentAfterBackOutput "Recent Programs (" "Recent Documents ("
        Assert-Phase16 ($RecentAfterExternal -eq $RecentAfterBack) "File Explorer Back history does not add a new application launch"
        Close-Explorer $TreeWindows
    } else {
        Assert-Phase16 $false "File Explorer window is available for internal-navigation routing"
    }
    Assert-Phase16 ($Runner.Count("FileExplorer window created: (\d+)") -ge ($Before + 1)) "external folder activation creates a real File Explorer process/window"

    $FinalWindows = Get-WindowBlock
    $UntilFinalWindows = [DateTime]::UtcNow.AddSeconds(5)
    while (($FinalWindows.Contains("appId=gxos.builtin.fileexplorer") -or $FinalWindows.Contains("appId=gxos.builtin.notepad")) -and [DateTime]::UtcNow -lt $UntilFinalWindows) {
        Start-Sleep -Milliseconds 100
        $FinalWindows = Get-WindowBlock
    }
    Assert-Phase16 (-not $FinalWindows.Contains("appId=gxos.builtin.fileexplorer") -and -not $FinalWindows.Contains("appId=gxos.builtin.notepad")) "all Phase 16 smoke windows close cleanly"
    Write-Host "Phase 16 folder runtime smoke: checks=$Checks failures=$Failures"
    if ($Failures -ne 0) { throw "Phase 16 folder runtime smoke had $Failures failed checks." }
} finally {
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
}
