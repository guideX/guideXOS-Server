[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build.bat before this Phase 12 hosted runtime smoke."
}

$TempRoot = [IO.Path]::GetFullPath((Join-Path $Root "tmp"))
$FixtureRoot = [IO.Path]::GetFullPath((Join-Path $TempRoot ("phase12-navigator-" + [Guid]::NewGuid().ToString("N"))))
if (-not $FixtureRoot.StartsWith($TempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved Phase 12 fixture path escaped the repository temp directory: $FixtureRoot"
}

function Get-VirtualPath {
    param([string]$Path)
    $FullPath = [IO.Path]::GetFullPath($Path)
    $Prefix = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $FullPath.StartsWith($Prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Fixture escaped hosted File Explorer root: $FullPath"
    }
    return "/" + $FullPath.Substring($Prefix.Length).Replace([string][char]92, '/')
}

function Convert-VirtualPathToFileUrl {
    param([string]$Path)
    $Parts = $Path.TrimStart('/').Split('/') | ForEach-Object { [Uri]::EscapeDataString($_) }
    return "file:///" + ($Parts -join '/')
}

$RunnerSource = @'
using System;
using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
public sealed class Phase12NavigatorRunner : IDisposable {
    private readonly Process process;
    private readonly object outputLock = new object();
    private readonly StringBuilder output = new StringBuilder();
    public Phase12NavigatorRunner(ProcessStartInfo info) {
        process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (s,e) => { if (e.Data != null) Append(e.Data); };
        process.ErrorDataReceived += (s,e) => { if (e.Data != null) Append(e.Data); };
    }
    private void Append(string value) { lock (outputLock) output.AppendLine(value); }
    public bool Start() { if (!process.Start()) return false; process.BeginOutputReadLine(); process.BeginErrorReadLine(); return true; }
    public void Send(string value) { process.StandardInput.WriteLine(value); process.StandardInput.Flush(); }
    public string Output() { lock (outputLock) return output.ToString(); }
    public int Count(string pattern) { lock (outputLock) return Regex.Matches(output.ToString(), pattern).Count; }
    public bool WaitForExit(int timeoutMs) { bool exited = process.WaitForExit(timeoutMs); if (exited) process.WaitForExit(); return exited; }
    public void Kill() { if (!process.HasExited) process.Kill(); }
    public void Dispose() { process.Dispose(); }
}
'@
Add-Type -TypeDefinition $RunnerSource -Language CSharp

$PersistedPaths = @("desktop.json", "desktop.state", "window-bounds.cfg", "appmodel-default-handlers.cfg")
$PersistedSnapshot = @{}
foreach ($Relative in $PersistedPaths) {
    $Path = Join-Path $Root $Relative
    $Exists = Test-Path -LiteralPath $Path -PathType Leaf
    $PersistedSnapshot[$Relative] = [pscustomobject]@{
        Path = $Path
        Exists = $Exists
        Bytes = if ($Exists) { [IO.File]::ReadAllBytes($Path) } else { $null }
        Attributes = if ($Exists) { [IO.File]::GetAttributes($Path) } else { [IO.FileAttributes]::Normal }
        WriteTime = if ($Exists) { [IO.File]::GetLastWriteTimeUtc($Path) } else { [DateTime]::MinValue }
    }
}

$Runner = $null
$Checks = 0
function Assert-Phase12 {
    param([bool]$Condition, [string]$Description)
    $script:Checks++
    if (-not $Condition) {
        $Output = if ($script:Runner) { $script:Runner.Output() } else { "" }
        $Start = [Math]::Max(0, $Output.Length - 8000)
        throw "FAIL: $Description`n$($Output.Substring($Start))"
    }
    Write-Host "PASS: $Description"
}

function Wait-OutputCount {
    param([string]$Pattern, [int]$Expected, [int]$Timeout = $TimeoutSeconds)
    $Until = [DateTime]::UtcNow.AddSeconds($Timeout)
    while ([DateTime]::UtcNow -lt $Until) {
        if ($script:Runner.Count($Pattern) -ge $Expected) { return }
        Start-Sleep -Milliseconds 50
    }
    $Output = $script:Runner.Output()
    $Start = [Math]::Max(0, $Output.Length - 8000)
    throw "Timed out waiting for output count $Expected for pattern: $Pattern`n$($Output.Substring($Start))"
}

function Send-ServerCommand {
    param([string]$Command)
    $script:Runner.Send($Command)
}

function Get-LatestId {
    param([string]$Pattern)
    $Matches = [regex]::Matches($script:Runner.Output(), $Pattern)
    if ($Matches.Count -eq 0) { throw "No ID matched $Pattern`n$($script:Runner.Output())" }
    return [uint64]$Matches[$Matches.Count - 1].Groups[1].Value
}

function Wait-WindowOwner {
    param([string]$Pattern, [int]$PreviousCount = 0, [int]$Timeout = $TimeoutSeconds)
    $Until = [DateTime]::UtcNow.AddSeconds($Timeout)
    while ([DateTime]::UtcNow -lt $Until) {
        Send-ServerCommand "desktop.windows.owners"
        if ($script:Runner.Count($Pattern) -gt $PreviousCount) { return Get-LatestId $Pattern }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for a compositor window owner matching $Pattern`n$($script:Runner.Output())"
}

function Open-ExplorerFolder {
    param([string]$VirtualFolder)
    $Before = $script:Runner.Count('FileExplorer window created: (\d+)')
    Send-ServerCommand ("desktop.open `"{0}`" dir" -f $VirtualFolder)
    Wait-OutputCount 'FileExplorer window created: (\d+)' ($Before + 1)
    $WindowId = Get-LatestId 'FileExplorer window created: (\d+)'
    Send-ServerCommand "gui.activate $WindowId"
    Start-Sleep -Milliseconds 100
    return $WindowId
}

function Open-NavigatorFromFirstRow {
    param([string]$VirtualPath, [string]$ExpectedTitle, [int]$ExpectedPngLoads = -1)
    $ActivationPattern = [regex]::Escape("Built-in document dispatcher delivered canonical activation appId=guidexos.navigator path=$VirtualPath")
    $Before = $script:Runner.Count($ActivationPattern)
    Send-ServerCommand ("gui.mouse {0} 400 104 1 down" -f $script:ExplorerId)
    Send-ServerCommand ("gui.mouse {0} 400 104 1 up" -f $script:ExplorerId)
    Send-ServerCommand ("gui.mouse {0} 400 104 1 down" -f $script:ExplorerId)
    Send-ServerCommand ("gui.mouse {0} 400 104 1 up" -f $script:ExplorerId)
    Wait-OutputCount $ActivationPattern ($Before + 1)
    Wait-OutputCount ([regex]::Escape("Navigator received App Model document activation appId=guidexos.navigator path=$VirtualPath owner=0 generation=0")) 1
    Assert-Phase12 ($script:Runner.Output().Contains("Navigator loadFileUrl: $VirtualPath")) "Navigator loads the exact AppRegistry activation path through the existing file document source"

    $AnyOwnerPattern = 'window id=(\d+) ownerPid=\d+ ownerName=navigator appId=guidexos\.navigator title=.* visible=true'
    $AnyOwnerBefore = $script:Runner.Count($AnyOwnerPattern)
    $script:NavigatorId = Wait-WindowOwner $AnyOwnerPattern $AnyOwnerBefore

    $Url = Convert-VirtualPathToFileUrl $VirtualPath
    $GotoBefore = $script:Runner.Count('NAVIGATOR_GOTO_RESULT: PASS')
    Send-ServerCommand "navigator.goto $Url"
    Wait-OutputCount 'NAVIGATOR_GOTO_RESULT: PASS' ($GotoBefore + 1)
    $Output = $script:Runner.Output()
    Assert-Phase12 ($Output.Contains("current_url=$Url") -and $Output.Contains("source_type=file")) "Navigator diagnostics preserve the truthful local file URL and file source type"
    if ($ExpectedPngLoads -ge 0) {
        Assert-Phase12 ($Output -match ("resource_png_loads=" + $ExpectedPngLoads + "\b")) "relative local PNG loads through Navigator's existing bounded image decoder"
    }

    $OwnerPattern = 'window id=(\d+) ownerPid=\d+ ownerName=navigator appId=guidexos\.navigator title=' + [regex]::Escape("$ExpectedTitle - guideXOS Navigator") + ' visible=true'
    $OwnerBefore = $script:Runner.Count($OwnerPattern)
    $script:NavigatorId = Wait-WindowOwner $OwnerPattern $OwnerBefore
    Assert-Phase12 ($script:Runner.Output() -match $OwnerPattern) "Navigator owns its visible window under its canonical App ID and parsed document title"
}

function Close-Navigator {
    param([uint64]$WindowId)
    $ExitPattern = [regex]::Escape("Navigator exiting window=$WindowId activationPathReleased=true documentStateReleased=true")
    $Before = $script:Runner.Count($ExitPattern)
    Send-ServerCommand "gui.close $WindowId"
    Wait-OutputCount $ExitPattern ($Before + 1)
}

try {
    $NormalFolder = Join-Path $FixtureRoot "normal-open\nested folder"
    $OpenWithFolder = Join-Path $FixtureRoot "open-with"
    $ProbeFolder = Join-Path $FixtureRoot "probes"
    New-Item -ItemType Directory -Force -Path $NormalFolder, $OpenWithFolder, $ProbeFolder | Out-Null
    $PagePath = Join-Path $NormalFolder "00 Local Start.HtMl"
    $SecondPath = Join-Path $NormalFolder "second.html"
    $ImagePath = Join-Path $NormalFolder "local image.png"
    $OpenWithPath = Join-Path $OpenWithFolder "OpenWith.htm"
    $MissingPath = Join-Path $ProbeFolder "missing.html"
    $EmptyPath = Join-Path $ProbeFolder "empty.html"
    $MalformedPath = Join-Path $ProbeFolder "malformed.htm"
    $UnsupportedPath = Join-Path $ProbeFolder "not-html.bin"
    $Utf8NoBom = [Text.UTF8Encoding]::new($false)
    $PageSource = '<!doctype html><html><head><title>Phase 12 Start</title><style>p { color: #123456; }</style></head><body><a href="second.html">Next</a><p>Local page</p><img src="local image.png" alt="fixture"></body></html>'
    [IO.File]::WriteAllText($PagePath, $PageSource, $Utf8NoBom)
    [IO.File]::WriteAllText($SecondPath, '<html><head><title>Phase 12 Second</title></head><body>Second page</body></html>', $Utf8NoBom)
    [IO.File]::WriteAllText($OpenWithPath, '<html><head><title>Phase 12 Open With</title></head><body>Open With</body></html>', $Utf8NoBom)
    [IO.File]::WriteAllText($EmptyPath, '', $Utf8NoBom)
    [IO.File]::WriteAllText($MalformedPath, '<html><body><p>Malformed but safe', $Utf8NoBom)
    [IO.File]::WriteAllText($UnsupportedPath, 'not HTML', $Utf8NoBom)
    [IO.File]::Copy((Join-Path $Root "assets\Backgrounds\blueflower_thumb.png"), $ImagePath, $true)
    $PageVirtualPath = Get-VirtualPath $PagePath
    $OpenWithVirtualPath = Get-VirtualPath $OpenWithPath

    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Exe
    $StartInfo.WorkingDirectory = $Root
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Runner = [Phase12NavigatorRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start the hosted guideXOS server runtime." }

    Send-ServerCommand "gui.start"
    Start-Sleep -Milliseconds 500
    Send-ServerCommand ("desktop.open.resolve {0}" -f $OpenWithVirtualPath)
    Wait-OutputCount 'launchTarget: DocumentActivation' 1
    Assert-Phase12 ($Runner.Output().Contains('handlerAppId: guidexos.navigator')) "AppRegistry resolves mixed-case and nested HTML to Navigator without a File Explorer extension route"

    $script:ExplorerId = Open-ExplorerFolder (Get-VirtualPath $NormalFolder)
    Open-NavigatorFromFirstRow $PageVirtualPath "Phase 12 Start" 1

    $SecondUrl = Convert-VirtualPathToFileUrl (Get-VirtualPath $SecondPath)
    $ClickBefore = $Runner.Count('NAVIGATOR_SMOKE_CLICK_FIRST_LINK_RESULT: PASS')
    Send-ServerCommand "navigator.smoke.click-first-link"
    Wait-OutputCount 'NAVIGATOR_SMOKE_CLICK_FIRST_LINK_RESULT: PASS' ($ClickBefore + 1)
    Assert-Phase12 ($Runner.Output().Contains("current_url=$SecondUrl")) "a relative local HTML link navigates to its sibling document"

    $PageUrl = Convert-VirtualPathToFileUrl $PageVirtualPath
    $BackBefore = $Runner.Count('NAVIGATOR_SMOKE_BACK_RESULT: PASS')
    Send-ServerCommand "navigator.smoke.back"
    Wait-OutputCount 'NAVIGATOR_SMOKE_BACK_RESULT: PASS' ($BackBefore + 1)
    Assert-Phase12 ($Runner.Output().Contains("current_url=$PageUrl")) "Navigator Back returns to the local source through its existing URL history"

    $ReloadPattern = [regex]::Escape("Navigator loadFileUrl: $PageVirtualPath")
    $ReloadBefore = $Runner.Count($ReloadPattern)
    [IO.File]::WriteAllText($PagePath, '<html><head><title>Phase 12 Reloaded</title></head><body>Updated externally</body></html>', $Utf8NoBom)
    Send-ServerCommand ("gui.mouse {0} 190 24 1 down" -f $script:NavigatorId)
    Send-ServerCommand ("gui.mouse {0} 190 24 1 up" -f $script:NavigatorId)
    Wait-OutputCount $ReloadPattern ($ReloadBefore + 1)
    $ReloadedTitlePattern = [regex]::Escape("current_title=Phase 12 Reloaded")
    $ReloadedTitleBefore = $Runner.Count($ReloadedTitlePattern)
    $ReloadedTitleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ($Runner.Count($ReloadedTitlePattern) -le $ReloadedTitleBefore -and [DateTime]::UtcNow -lt $ReloadedTitleDeadline) {
        Send-ServerCommand "navigator.smoke.document"
        Start-Sleep -Milliseconds 100
    }
    Assert-Phase12 ($Runner.Count($ReloadedTitlePattern) -gt $ReloadedTitleBefore) "Reload replaces Navigator's parsed document with the externally changed local HTML"
    Close-Navigator $script:NavigatorId
    Send-ServerCommand "gui.close $script:ExplorerId"

    $script:ExplorerId = Open-ExplorerFolder (Get-VirtualPath $OpenWithFolder)
    Send-ServerCommand ("gui.mouse {0} 400 104 2 down" -f $script:ExplorerId)
    Send-ServerCommand ("gui.mouse {0} 400 104 2 up" -f $script:ExplorerId)
    Wait-OutputCount ([regex]::Escape("FileExplorer context menu created for path=$OpenWithVirtualPath")) 1
    Send-ServerCommand ("gui.mouse {0} 410 140 1 down" -f $script:ExplorerId)
    Send-ServerCommand ("gui.mouse {0} 410 140 1 up" -f $script:ExplorerId)
    Wait-OutputCount ([regex]::Escape("FileExplorer Open With submenu opened path=$OpenWithVirtualPath handlers=1")) 1
    Send-ServerCommand ("gui.mouse {0} 640 140 1 down" -f $script:ExplorerId)
    Send-ServerCommand ("gui.mouse {0} 640 140 1 up" -f $script:ExplorerId)
    Wait-OutputCount ([regex]::Escape("FileExplorer Open With selected canonical appId=guidexos.navigator path=$OpenWithVirtualPath")) 1
    Wait-OutputCount ([regex]::Escape("Navigator received App Model document activation appId=guidexos.navigator path=$OpenWithVirtualPath owner=0 generation=0")) 1
    Assert-Phase12 $true "File Explorer Open With discovers and launches Navigator through its canonical handler identity"
    $OpenWithAnyOwner = 'window id=(\d+) ownerPid=\d+ ownerName=navigator appId=guidexos\.navigator title=.* visible=true'
    $OpenWithAnyOwnerBefore = $Runner.Count($OpenWithAnyOwner)
    $OpenWithNavigatorId = Wait-WindowOwner $OpenWithAnyOwner $OpenWithAnyOwnerBefore
    $OpenWithUrl = Convert-VirtualPathToFileUrl $OpenWithVirtualPath
    $OpenWithGotoBefore = $Runner.Count('NAVIGATOR_GOTO_RESULT: PASS')
    Send-ServerCommand "navigator.goto $OpenWithUrl"
    Wait-OutputCount 'NAVIGATOR_GOTO_RESULT: PASS' ($OpenWithGotoBefore + 1)
    $OpenWithOwner = 'window id=(\d+) ownerPid=\d+ ownerName=navigator appId=guidexos\.navigator title=Phase 12 Open With - guideXOS Navigator visible=true'
    $OpenWithOwnerBefore = $Runner.Count($OpenWithOwner)
    $OpenWithNavigatorId = Wait-WindowOwner $OpenWithOwner $OpenWithOwnerBefore
    Close-Navigator $OpenWithNavigatorId
    Send-ServerCommand "gui.close $script:ExplorerId"

    $MissingVirtualPath = Get-VirtualPath $MissingPath
    $BeforeMissing = $Runner.Count([regex]::Escape("Navigator received App Model document activation appId=guidexos.navigator path=$MissingVirtualPath"))
    Send-ServerCommand ("desktop.open `"{0}`"" -f $MissingVirtualPath)
    Wait-OutputCount ([regex]::Escape("Navigator received App Model document activation appId=guidexos.navigator path=$MissingVirtualPath owner=0 generation=0")) ($BeforeMissing + 1)
    Wait-OutputCount ([regex]::Escape("Navigator readTextFile: not found:")) 1
    $MissingUrl = Convert-VirtualPathToFileUrl $MissingVirtualPath
    $GotoBefore = $Runner.Count('NAVIGATOR_GOTO_RESULT: PASS')
    Send-ServerCommand "navigator.goto $MissingUrl"
    Wait-OutputCount 'NAVIGATOR_GOTO_RESULT: PASS' ($GotoBefore + 1)
    Assert-Phase12 ($Runner.Output().Contains('error_status=File not found')) "missing local HTML fails with a fresh file error page instead of stale document content"
    $MissingOwner = 'window id=(\d+) ownerPid=\d+ ownerName=navigator appId=guidexos\.navigator title=.* visible=true'
    $MissingOwnerBefore = $Runner.Count($MissingOwner)
    $MissingNavigatorId = Wait-WindowOwner $MissingOwner $MissingOwnerBefore
    Close-Navigator $MissingNavigatorId

    $UnsupportedVirtualPath = Get-VirtualPath $UnsupportedPath
    $UnsupportedBefore = $Runner.Count([regex]::Escape("Navigator received App Model document activation appId=guidexos.navigator path=$UnsupportedVirtualPath"))
    Send-ServerCommand ("desktop.open `"{0}`"" -f $UnsupportedVirtualPath)
    Wait-OutputCount ([regex]::Escape("Desktop filesystem open failed: No file association registered for $UnsupportedVirtualPath")) 1
    Assert-Phase12 ($Runner.Count([regex]::Escape("Navigator received App Model document activation appId=guidexos.navigator path=$UnsupportedVirtualPath")) -eq $UnsupportedBefore) "unsupported explicit HTML handler capability does not launch Navigator for arbitrary bytes"

    Send-ServerCommand "exit"
    if (-not $Runner.WaitForExit(5000)) { $Runner.Kill() }
    Write-Host "navigatorLocalActivationRuntimeChecks=$Checks passed"
    Write-Host "Navigator local HTML App Model runtime smoke PASS."
} finally {
    if ($Runner) {
        try {
            if (-not $Runner.WaitForExit(0)) {
                $Runner.Send("exit")
                if (-not $Runner.WaitForExit(5000)) { $Runner.Kill() }
            }
        } catch {}
        $Runner.Dispose()
    }
    foreach ($Relative in $PersistedPaths) {
        $Snapshot = $PersistedSnapshot[$Relative]
        if ($Snapshot.Exists) {
            [IO.File]::WriteAllBytes($Snapshot.Path, [byte[]]$Snapshot.Bytes)
            [IO.File]::SetAttributes($Snapshot.Path, $Snapshot.Attributes)
            [IO.File]::SetLastWriteTimeUtc($Snapshot.Path, $Snapshot.WriteTime)
        } elseif (Test-Path -LiteralPath $Snapshot.Path -PathType Leaf) {
            Remove-Item -LiteralPath $Snapshot.Path -Force
        }
    }
    if (Test-Path -LiteralPath $FixtureRoot) {
        $ResolvedFixtureRoot = [IO.Path]::GetFullPath($FixtureRoot)
        if ($ResolvedFixtureRoot.StartsWith($TempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $ResolvedFixtureRoot -Recurse -Force
        }
    }
}
