[CmdletBinding()]
param([int]$TimeoutSeconds = 45)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build.bat or .\build-native-experimental.bat before this Phase 14 smoke."
}

$RunnerSource = @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;

public sealed class Phase14LegacyAssociationRunner : IDisposable
{
    private readonly Process process;
    private readonly ConcurrentQueue<string> output = new ConcurrentQueue<string>();
    public Phase14LegacyAssociationRunner(ProcessStartInfo info)
    {
        process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (s, e) => { if (e.Data != null) output.Enqueue(e.Data); };
        process.ErrorDataReceived += (s, e) => { if (e.Data != null) output.Enqueue(e.Data); };
    }
    public bool Start() { if (!process.Start()) return false; process.BeginOutputReadLine(); process.BeginErrorReadLine(); return true; }
    public void Send(string command) { process.StandardInput.WriteLine(command); process.StandardInput.Flush(); }
    public string Output() { return string.Join(Environment.NewLine, output.ToArray()); }
    public bool WaitForExit(int timeoutMs) { bool exited = process.WaitForExit(timeoutMs); if (exited) process.WaitForExit(); return exited; }
    public void Stop(int timeoutMs)
    {
        if (process.HasExited) return;
        try { Send("exit"); process.StandardInput.Close(); } catch { }
        if (!WaitForExit(timeoutMs)) process.Kill();
    }
    public void Dispose() { process.Dispose(); }
}
'@
Add-Type -TypeDefinition $RunnerSource -Language CSharp

$Runner = $null
$Checks = 0
$Failures = 0
function Assert-Phase14 {
    param([bool]$Condition, [string]$Name)
    $script:Checks++
    if ($Condition) { Write-Host "PASS: $Name" }
    else { $script:Failures++; Write-Host "FAIL: $Name" }
}
function Get-RuntimeOutput {
    if ($null -eq $script:Runner) { return "" }
    return $script:Runner.Output()
}
function Send-ServerCommand {
    param([string]$Command)
    $script:Runner.Send($Command)
}
function Wait-Output {
    param([string]$Pattern, [int]$Timeout = $TimeoutSeconds)
    $Until = [DateTime]::UtcNow.AddSeconds($Timeout)
    while ([DateTime]::UtcNow -lt $Until) {
        $Output = Get-RuntimeOutput
        if ([regex]::IsMatch($Output, $Pattern)) { return $Output }
        Start-Sleep -Milliseconds 50
    }
    $Output = Get-RuntimeOutput
    $Start = [Math]::Max(0, $Output.Length - 8000)
    throw "Timed out waiting for runtime pattern: $Pattern`n$($Output.Substring($Start))"
}
function Wait-OutputCount {
    param([string]$Pattern, [int]$Expected, [int]$Timeout = $TimeoutSeconds)
    $Until = [DateTime]::UtcNow.AddSeconds($Timeout)
    while ([DateTime]::UtcNow -lt $Until) {
        $Output = Get-RuntimeOutput
        if ([regex]::Matches($Output, $Pattern).Count -ge $Expected) { return $Output }
        Start-Sleep -Milliseconds 50
    }
    $Output = Get-RuntimeOutput
    $Start = [Math]::Max(0, $Output.Length - 8000)
    throw "Timed out waiting for $Expected matches of runtime pattern: $Pattern`n$($Output.Substring($Start))"
}
function Get-LastSection {
    param([string]$Text, [string]$StartMarker, [string]$EndMarker)
    $Start = $Text.LastIndexOf($StartMarker, [StringComparison]::Ordinal)
    if ($Start -lt 0) { return "" }
    $End = $Text.IndexOf($EndMarker, $Start, [StringComparison]::Ordinal)
    if ($End -lt 0) { return "" }
    return $Text.Substring($Start, $End + $EndMarker.Length - $Start)
}
function Get-LastLine {
    param([string]$Text, [string]$Pattern)
    $Matches = [regex]::Matches($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    if ($Matches.Count -eq 0) { return "" }
    return $Matches[$Matches.Count - 1].Value
}
function Get-LastOpenResolveBlock {
    param([string]$ExpectedPath)
    $Output = Get-RuntimeOutput
    $Start = $Output.LastIndexOf("[FilesystemEntryLaunchTarget]", [StringComparison]::Ordinal)
    if ($Start -lt 0) { throw "No filesystem-entry resolution block was emitted for $ExpectedPath." }
    $Block = $Output.Substring($Start)
    if (-not $Block.Contains("path: $ExpectedPath")) { throw "The most recent resolution block did not match $ExpectedPath.`n$Block" }
    return $Block
}
function Resolve-ExpectedHandler {
    param([string]$Path, [string]$AppId)
    Send-ServerCommand "desktop.open.resolve $Path"
    [void](Wait-Output ([regex]::Escape("path: $Path")))
    $Block = Get-LastOpenResolveBlock $Path
    Assert-Phase14 ($Block.Contains("associationKind: app-model-extension") -and
        $Block.Contains("handlerAppId: $AppId") -and
        $Block.Contains("launchTarget: DocumentActivation") -and
        $Block.Contains("status: supported")) "$Path resolves through AppRegistry to $AppId"
}
function Resolve-NoHandler {
    param([string]$Path)
    Send-ServerCommand "desktop.open.resolve $Path"
    [void](Wait-Output ([regex]::Escape("path: $Path")))
    $Block = Get-LastOpenResolveBlock $Path
    Assert-Phase14 ([regex]::IsMatch($Block, "(?m)^handlerAppId:\s*$") -and
        $Block.Contains("launchTarget: Unsupported") -and
        $Block.Contains("status: unsupported")) "$Path resolves without an application handler"
}
function Get-DefaultStoreSnapshot {
    $Path = Join-Path $Root "appmodel-default-handlers.cfg"
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return "absent" }
    $Info = Get-Item -LiteralPath $Path
    return ([Convert]::ToBase64String([IO.File]::ReadAllBytes($Path)) + "|" +
        $Info.Attributes.ToString() + "|" + $Info.LastWriteTimeUtc.Ticks)
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
    $Runner = [Phase14LegacyAssociationRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }

    Send-ServerCommand "gui.start"
    Start-Sleep -Milliseconds 300
    Send-ServerCommand "desktop.appmodel.file-associations"
    $AssociationOutput = Wait-Output "associationRegistryCapacity="
    $RegisteredSection = Get-LastSection $AssociationOutput "registeredAssociations:" "nonFatal: true"
    Assert-Phase14 (-not $RegisteredSection.Contains("extension=.bmp") -and
        -not $RegisteredSection.Contains("extension=.gif")) "AppRegistry declares no BMP or GIF capabilities"

    foreach ($Item in @(
        @{ Extension = ".txt"; AppId = "gxos.builtin.notepad" },
        @{ Extension = ".log"; AppId = "gxos.builtin.notepad" },
        @{ Extension = ".ini"; AppId = "gxos.builtin.notepad" },
        @{ Extension = ".cfg"; AppId = "gxos.builtin.notepad" },
        @{ Extension = ".png"; AppId = "gxos.builtin.imageviewer" },
        @{ Extension = ".jpg"; AppId = "gxos.builtin.imageviewer" },
        @{ Extension = ".jpeg"; AppId = "gxos.builtin.imageviewer" },
        @{ Extension = ".html"; AppId = "guidexos.navigator" },
        @{ Extension = ".htm"; AppId = "guidexos.navigator" }
    )) {
        Resolve-ExpectedHandler "/phase14/probe$($Item.Extension)" $Item.AppId
    }
    foreach ($Path in @(
        "/phase14/probe.bmp",
        "/phase14/probe.gif",
        "/phase14/probe.xyzphase14",
        "/phase14/README",
        "/phase14/.gitignore",
        "/phase14/trailing."
    )) {
        Resolve-NoHandler $Path
    }

    Send-ServerCommand "desktop.windows.owners"
    $BeforeWindowsOutput = Wait-Output "DESKTOP_WINDOW_OWNERS_END"
    $BeforeWindows = Get-LastSection $BeforeWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    Send-ServerCommand "plist"
    $BeforeProcessOutput = Wait-Output "(?m)^Processes:"
    $BeforeProcesses = Get-LastLine $BeforeProcessOutput "(?m)^Processes:.*$"
    Send-ServerCommand "desktop.appmodel.file-associations"
    $BeforeAssociationOutput = Wait-Output "associationRegistryCapacity="
    $BeforeRegistered = Get-LastSection $BeforeAssociationOutput "registeredAssociations:" "nonFatal: true"
    Send-ServerCommand "desktop.apps.verbose"
    $BeforeAppsOutput = Wait-Output "manifestScan"
    $BeforeApps = Get-LastSection $BeforeAppsOutput "registeredApps:" "launchPolicy:"
    $RecentProgramsHeadersBeforeProbe = [regex]::Matches((Get-RuntimeOutput), "Recent Programs \(").Count
    $RecentDocumentsHeadersBeforeProbe = [regex]::Matches((Get-RuntimeOutput), "Recent Documents \(").Count
    Send-ServerCommand "desktop.recent"
    $BeforeRecentOutput = Wait-OutputCount "Recent Programs \(" ($RecentProgramsHeadersBeforeProbe + 1)
    $BeforeRecentOutput = Wait-OutputCount "Recent Documents \(" ($RecentDocumentsHeadersBeforeProbe + 1)
    $BeforeRecentStart = $BeforeRecentOutput.LastIndexOf("Recent Programs (", [StringComparison]::Ordinal)
    $BeforeRecentEnd = $BeforeRecentOutput.LastIndexOf("Recent Documents (", [StringComparison]::Ordinal)
    $BeforeRecent = if ($BeforeRecentStart -ge 0 -and $BeforeRecentEnd -gt $BeforeRecentStart) {
        $BeforeRecentOutput.Substring($BeforeRecentStart, $BeforeRecentEnd - $BeforeRecentStart).Trim()
    } else { "" }
    $StoreBefore = Get-DefaultStoreSnapshot
    $ImageViewerDispatchCountBefore = [regex]::Matches((Get-RuntimeOutput), "ImageViewer received App Model document activation").Count

    $OpenCommands = [System.Collections.Generic.List[string]]::new()
    for ($Cycle = 0; $Cycle -lt 34; $Cycle++) {
        $OpenCommands.Add("desktop.open /phase14/stress.bmp")
        $OpenCommands.Add("desktop.open /phase14/stress.gif")
        $OpenCommands.Add("desktop.open /phase14/stress.xyzphase14")
    }
    $OpenCommands.Add("desktop.open /phase14/README")
    $OpenCommands.Add("desktop.open /phase14/.gitignore")
    $OpenCommands.Add("desktop.open /phase14/trailing.")
    $OpenCommands.Add("desktop.open /phase14/renamed-valid-jpeg.bmp")
    foreach ($Command in $OpenCommands) { Send-ServerCommand $Command }
    $ExpectedFailures = $OpenCommands.Count
    $OpenOutput = Wait-OutputCount "Desktop open failed: No file association registered for /phase14/" $ExpectedFailures 60
    Assert-Phase14 ([regex]::Matches($OpenOutput, "Desktop open failed: No file association registered for /phase14/stress\.bmp").Count -eq 34) "34 repeated BMP opens return the same no-handler error"
    Assert-Phase14 ([regex]::Matches($OpenOutput, "Desktop open failed: No file association registered for /phase14/stress\.gif").Count -eq 34) "34 repeated GIF opens return the same no-handler error"
    Assert-Phase14 ([regex]::Matches($OpenOutput, "Desktop open failed: No file association registered for /phase14/stress\.xyzphase14").Count -eq 34) "34 repeated unknown-extension opens return the same no-handler error"
    Assert-Phase14 (-not $OpenOutput.Contains("Desktop open successful: /phase14/renamed-valid-jpeg.bmp")) "extension-based BMP rename does not content-sniff into ImageViewer"

    Send-ServerCommand "desktop.windows.owners"
    $AfterWindowsOutput = Wait-Output "DESKTOP_WINDOW_OWNERS_END"
    $AfterWindows = Get-LastSection $AfterWindowsOutput "DESKTOP_WINDOW_OWNERS_BEGIN" "DESKTOP_WINDOW_OWNERS_END"
    Send-ServerCommand "plist"
    $AfterProcessOutput = Wait-Output "(?m)^Processes:"
    $AfterProcesses = Get-LastLine $AfterProcessOutput "(?m)^Processes:.*$"
    Send-ServerCommand "desktop.appmodel.file-associations"
    $AfterAssociationOutput = Wait-Output "associationRegistryCapacity="
    $AfterRegistered = Get-LastSection $AfterAssociationOutput "registeredAssociations:" "nonFatal: true"
    Send-ServerCommand "desktop.apps.verbose"
    $AfterAppsOutput = Wait-Output "manifestScan"
    $AfterApps = Get-LastSection $AfterAppsOutput "registeredApps:" "launchPolicy:"
    $RecentProgramsHeadersBeforeAfterProbe = [regex]::Matches((Get-RuntimeOutput), "Recent Programs \(").Count
    $RecentDocumentsHeadersBeforeAfterProbe = [regex]::Matches((Get-RuntimeOutput), "Recent Documents \(").Count
    Send-ServerCommand "desktop.recent"
    $AfterRecentOutput = Wait-OutputCount "Recent Programs \(" ($RecentProgramsHeadersBeforeAfterProbe + 1)
    $AfterRecentOutput = Wait-OutputCount "Recent Documents \(" ($RecentDocumentsHeadersBeforeAfterProbe + 1)
    $AfterRecentStart = $AfterRecentOutput.LastIndexOf("Recent Programs (", [StringComparison]::Ordinal)
    $AfterRecentEnd = $AfterRecentOutput.LastIndexOf("Recent Documents (", [StringComparison]::Ordinal)
    $AfterRecent = if ($AfterRecentStart -ge 0 -and $AfterRecentEnd -gt $AfterRecentStart) {
        $AfterRecentOutput.Substring($AfterRecentStart, $AfterRecentEnd - $AfterRecentStart).Trim()
    } else { "" }
    $StoreAfter = Get-DefaultStoreSnapshot
    $ImageViewerDispatchCountAfter = [regex]::Matches((Get-RuntimeOutput), "ImageViewer received App Model document activation").Count

    Assert-Phase14 ($BeforeProcesses -eq $AfterProcesses) "unsupported opens create no process"
    Assert-Phase14 ($BeforeWindows -eq $AfterWindows) "unsupported opens create no compositor window"
    Assert-Phase14 ($ImageViewerDispatchCountBefore -eq $ImageViewerDispatchCountAfter) "unsupported opens do not invoke the ImageViewer document dispatcher"
    Assert-Phase14 ($BeforeRegistered -eq $AfterRegistered) "unsupported opens do not mutate AppRegistry capabilities or known associations"
    Assert-Phase14 ($BeforeApps -eq $AfterApps) "unsupported opens do not mutate application registrations"
    Assert-Phase14 ($BeforeRecent -eq $AfterRecent) "unsupported opens do not mutate Recent Programs or add ImageViewer"
    Assert-Phase14 ($StoreBefore -eq $StoreAfter) "unsupported opens do not mutate persisted default policy"

    $FailureCount = [regex]::Matches($OpenOutput, "Desktop open failed: No file association registered for /phase14/").Count
    Write-Host "phase14NoHandlerOpenCycles=$FailureCount/$ExpectedFailures"
} finally {
    if ($null -ne $Runner) {
        try { $Runner.Stop(5000) } catch { }
        $Runner.Dispose()
    }
}

Write-Host "phase14RuntimeChecks=$($Checks - $Failures)/$Checks"
if ($Failures -ne 0) { throw "Phase 14 legacy-association runtime smoke failed with $Failures failed checks." }
Write-Host "Phase 14 legacy-association runtime smoke PASS."
