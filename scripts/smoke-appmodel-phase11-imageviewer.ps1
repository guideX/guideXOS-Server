[CmdletBinding()]
param(
    [ValidateRange(3, 10)]
    [int]$ActivationCount = 3,
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "Run .\build.bat before this Phase 11 hosted-runtime smoke." }

$Stamp = [Guid]::NewGuid().ToString("N")
$TempRoot = [IO.Path]::GetFullPath((Join-Path $Root "tmp"))
$FixtureRoot = [IO.Path]::GetFullPath((Join-Path $TempRoot ("phase11-imageviewer-" + $Stamp)))
if (-not $FixtureRoot.StartsWith($TempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved Phase 11 fixture path escaped the repository temp directory: $FixtureRoot"
}
$Assets = Join-Path $Root "assets\Backgrounds"
$Sources = @(
    (Join-Path $Assets "blueflower_thumb.png"),
    (Join-Path $Assets "ameobagx_thumb.png"),
    (Join-Path $Assets "Wallpaper2_thumb.png")
)
foreach ($Source in $Sources) { if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Required PNG fixture missing: $Source" } }

function Get-PngDimensions {
    param([string]$Path)
    $Bytes = [IO.File]::ReadAllBytes($Path)
    if ($Bytes.Length -lt 24) { throw "PNG fixture has a short header: $Path" }
    $Width = ([uint32]$Bytes[16] -shl 24) -bor ([uint32]$Bytes[17] -shl 16) -bor ([uint32]$Bytes[18] -shl 8) -bor [uint32]$Bytes[19]
    $Height = ([uint32]$Bytes[20] -shl 24) -bor ([uint32]$Bytes[21] -shl 16) -bor ([uint32]$Bytes[22] -shl 8) -bor [uint32]$Bytes[23]
    return "${Width}x${Height}"
}

function Get-VirtualPath {
    param([string]$Path)
    $FullPath = [IO.Path]::GetFullPath($Path)
    $Prefix = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $FullPath.StartsWith($Prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Fixture escaped hosted File Explorer root: $FullPath" }
    return "/" + $FullPath.Substring($Prefix.Length).Replace([string][char]92, '/')
}

$RunnerSource = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
public sealed class Phase11ImageViewerRunner : IDisposable {
    private readonly Process process;
    private readonly object outputLock = new object();
    private readonly StringBuilder output = new StringBuilder();
    private readonly Dictionary<string, int> matchCounts = new Dictionary<string, int>();
    private readonly Dictionary<string, int> scannedLengths = new Dictionary<string, int>();
    public Phase11ImageViewerRunner(ProcessStartInfo info) {
        process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (s,e) => { if (e.Data != null) Append(e.Data); };
        process.ErrorDataReceived += (s,e) => { if (e.Data != null) Append(e.Data); };
    }
    private void Append(string value) { lock (outputLock) output.AppendLine(value); }
    public bool Start() { if (!process.Start()) return false; process.BeginOutputReadLine(); process.BeginErrorReadLine(); return true; }
    public void Send(string value) { process.StandardInput.WriteLine(value); process.StandardInput.Flush(); }
    public string Output() { lock (outputLock) return output.ToString(); }
    public int Count(string pattern) {
        lock (outputLock) {
            int previous = scannedLengths.TryGetValue(pattern, out int prior) ? prior : 0;
            int start = Math.Max(0, previous - 512);
            string tail = output.ToString(start, output.Length - start);
            int count = matchCounts.TryGetValue(pattern, out int existing) ? existing : 0;
            foreach (Match match in Regex.Matches(tail, pattern))
                if (start + match.Index + match.Length > previous) count++;
            matchCounts[pattern] = count;
            scannedLengths[pattern] = output.Length;
            return count;
        }
    }
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
$Failures = 0
function Assert-Phase11 {
    param([bool]$Condition, [string]$Name)
    $script:Checks++
    if ($Condition) { Write-Host "PASS: $Name" }
    else { $script:Failures++; Write-Host "FAIL: $Name" }
}
function Get-RuntimeOutput { if ($null -eq $script:Runner) { return "" }; return $script:Runner.Output() }
function Send-ServerCommand {
    param([string]$Command)
    $script:Runner.Send($Command)
}
function Wait-OutputCount {
    param([string]$Pattern, [int]$Expected, [int]$Timeout = $TimeoutSeconds)
    $Until = [DateTime]::UtcNow.AddSeconds($Timeout)
    while ([DateTime]::UtcNow -lt $Until) {
        if ($script:Runner.Count($Pattern) -ge $Expected) { return }
        Start-Sleep -Milliseconds 50
    }
    $Output = Get-RuntimeOutput
    $Start = [Math]::Max(0, $Output.Length - 6000)
    throw "Timed out waiting for output count $Expected for pattern: $Pattern`n$($Output.Substring($Start))"
}
function Get-LatestImageViewerWindowId {
    param([string]$FileName)
    Send-ServerCommand "desktop.windows.owners"
    $OwnerPattern = 'window id=(\d+) ownerPid=(\d+) ownerName=ImageViewer appId=gxos\.builtin\.imageviewer title=Image Viewer - ' + [regex]::Escape($FileName) + ' visible=true'
    Wait-OutputCount $OwnerPattern 1
    $Matches = [regex]::Matches((Get-RuntimeOutput), $OwnerPattern)
    if ($Matches.Count -eq 0) { throw "ImageViewer has no compositor-owned window for $FileName.`n$(Get-RuntimeOutput)" }
    return [uint64]$Matches[$Matches.Count - 1].Groups[1].Value
}
function Close-ImageViewer {
    param([uint64]$WindowId)
    $ExitPattern = [regex]::Escape("ImageViewer exiting window=$WindowId imageStateReleased=true activationPathReleased=true")
    $Before = $script:Runner.Count($ExitPattern)
    Send-ServerCommand "gui.close $WindowId"
    Wait-OutputCount $ExitPattern ($Before + 1)
}
function Close-FileExplorer {
    param([uint64]$WindowId)
    Send-ServerCommand "gui.close $WindowId"
}
function Assert-ImageViewerActivation {
    param([string]$VirtualPath, [string]$ExpectedDimensions, [int]$PreviousLoads)
    $PathPattern = [regex]::Escape($VirtualPath)
    Wait-OutputCount ([regex]::Escape("Built-in document dispatcher delivered canonical activation appId=gxos.builtin.imageviewer path=$VirtualPath")) 1
    Wait-OutputCount ([regex]::Escape("ImageViewer received App Model document activation appId=gxos.builtin.imageviewer path=$VirtualPath owner=0 generation=0")) 1
    Wait-OutputCount ([regex]::Escape("ImageViewer loaded PNG: $VirtualPath ($ExpectedDimensions)")) ($PreviousLoads + 1)
    $FileName = $VirtualPath.Substring($VirtualPath.LastIndexOf('/') + 1)
    $WindowId = Get-LatestImageViewerWindowId $FileName
    $OwnersOutput = Get-RuntimeOutput
    Assert-Phase11 ($OwnersOutput -match ("window id=" + $WindowId + " ownerPid=\d+ ownerName=ImageViewer appId=gxos\.builtin\.imageviewer title=Image Viewer - ")) `
        "ImageViewer owns its compositor window under the canonical App ID for $VirtualPath"
    Close-ImageViewer $WindowId
    return $WindowId
}

try {
    New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
    $NormalCases = @(
        @{ Folder = (Join-Path $FixtureRoot "normal-one"); Name = "one.png"; Source = $Sources[0] },
        @{ Folder = (Join-Path (Join-Path $FixtureRoot "normal-two") "nested folder"); Name = "MixedCase.PNG"; Source = $Sources[1] },
        @{ Folder = (Join-Path $FixtureRoot "normal-three"); Name = "third.Png"; Source = $Sources[2] }
    )
    $ExpectedDimensions = @()
    foreach ($Case in $NormalCases) {
        New-Item -ItemType Directory -Force -Path $Case.Folder | Out-Null
        $Case.Path = Join-Path $Case.Folder $Case.Name
        [IO.File]::Copy($Case.Source, $Case.Path, $true)
        $Case.VirtualPath = Get-VirtualPath $Case.Path
        $Case.VirtualFolder = Get-VirtualPath $Case.Folder
        $Case.Dimensions = Get-PngDimensions $Case.Source
        $ExpectedDimensions += $Case.Dimensions
    }
    Assert-Phase11 (@($ExpectedDimensions | Select-Object -Unique).Count -ge 2) "valid fixtures have different dimensions"

    $OpenWithFolder = Join-Path $FixtureRoot "open-with"
    New-Item -ItemType Directory -Force -Path $OpenWithFolder | Out-Null
    $OpenWithPath = Join-Path $OpenWithFolder "Open With.PNG"
    [IO.File]::Copy($Sources[0], $OpenWithPath, $true)
    $OpenWithVirtualPath = Get-VirtualPath $OpenWithPath
    $OpenWithVirtualFolder = Get-VirtualPath $OpenWithFolder
    $OpenWithDimensions = Get-PngDimensions $Sources[0]

    $ProbeFolder = Join-Path $FixtureRoot "probes"
    New-Item -ItemType Directory -Force -Path $ProbeFolder | Out-Null
    $MissingPath = Join-Path $ProbeFolder "missing.png"
    $EmptyPath = Join-Path $ProbeFolder "empty.png"
    $CorruptPath = Join-Path $ProbeFolder "corrupt.png"
    $SignaturePath = Join-Path $ProbeFolder "signature-only.png"
    $HeaderPath = Join-Path $ProbeFolder "truncated-header.png"
    $DataPath = Join-Path $ProbeFolder "truncated-data.png"
    $OversizedDimensionsPath = Join-Path $ProbeFolder "oversized-dimensions.png"
    $OversizedFilePath = Join-Path $ProbeFolder "oversized-file.png"
    [IO.File]::WriteAllBytes($EmptyPath, [byte[]]@())
    [IO.File]::WriteAllBytes($CorruptPath, [Text.Encoding]::ASCII.GetBytes("not a png"))
    $ValidBytes = [IO.File]::ReadAllBytes($Sources[0])
    [IO.File]::WriteAllBytes($SignaturePath, [byte[]]$ValidBytes[0..7])
    [IO.File]::WriteAllBytes($HeaderPath, [byte[]]$ValidBytes[0..30])
    [IO.File]::WriteAllBytes($DataPath, [byte[]]$ValidBytes[0..63])
    $OversizedHeader = [byte[]]::new(24)
    [byte[]]$PngSignature = @(137,80,78,71,13,10,26,10)
    [Array]::Copy($PngSignature, 0, $OversizedHeader, 0, 8)
    $OversizedHeader[12] = 73; $OversizedHeader[13] = 72; $OversizedHeader[14] = 68; $OversizedHeader[15] = 82
    $OversizedHeader[16] = 0; $OversizedHeader[17] = 0; $OversizedHeader[18] = 16; $OversizedHeader[19] = 1
    $OversizedHeader[20] = 0; $OversizedHeader[21] = 0; $OversizedHeader[22] = 0; $OversizedHeader[23] = 1
    [IO.File]::WriteAllBytes($OversizedDimensionsPath, $OversizedHeader)
    $OversizedFileBytes = [byte[]]::new(4 * 1024 * 1024 + 1)
    [IO.File]::WriteAllBytes($OversizedFilePath, $OversizedFileBytes)
    $UnknownPath = Join-Path $ProbeFolder "picture.xyz"
    [IO.File]::WriteAllBytes($UnknownPath, [Text.Encoding]::ASCII.GetBytes("unassociated image-like name"))

    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Exe
    $StartInfo.WorkingDirectory = $Root
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Runner = [Phase11ImageViewerRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }

    Send-ServerCommand "gui.start"
    Start-Sleep -Milliseconds 500
    Send-ServerCommand "desktop.appmodel.file-associations"
    Wait-OutputCount ([regex]::Escape("fileAssociationV1: OK")) 1
    $AssociationOutput = Get-RuntimeOutput
    Assert-Phase11 ($AssociationOutput.Contains("extension=.png handlerAppId=gxos.builtin.imageviewer") -and
        $AssociationOutput.Contains("extension=.jpg handlerAppId=gxos.builtin.imageviewer") -and
        $AssociationOutput.Contains("extension=.jpeg handlerAppId=gxos.builtin.imageviewer")) `
        "PNG and JPEG handler declarations are surfaced through AppRegistry"
    # The diagnostic command reads one whitespace-delimited token and does not strip quotes.
    Send-ServerCommand ("desktop.open.resolve {0}" -f $NormalCases[0].VirtualPath)
    Wait-OutputCount ([regex]::Escape("launchTarget: DocumentActivation")) 1
    Assert-Phase11 ((Get-RuntimeOutput).Contains("handlerAppId: gxos.builtin.imageviewer")) "normal Open resolves .png to the canonical AppRegistry handler"

    for ($Cycle = 1; $Cycle -le $ActivationCount; $Cycle++) {
        $Case = $NormalCases[($Cycle - 1) % $NormalCases.Count]
        $ExplorerBefore = $Runner.Count('FileExplorer window created: (\d+)')
        $LoadBefore = $Runner.Count([regex]::Escape("ImageViewer loaded PNG: $($Case.VirtualPath) ($($Case.Dimensions))"))
        Send-ServerCommand ("desktop.open `"{0}`" dir" -f $Case.VirtualFolder)
        Wait-OutputCount 'FileExplorer window created: (\d+)' ($ExplorerBefore + 1)
        $ExplorerMatches = [regex]::Matches((Get-RuntimeOutput), 'FileExplorer window created: (\d+)')
        $ExplorerId = [uint64]$ExplorerMatches[$ExplorerMatches.Count - 1].Groups[1].Value
        $DispatchPattern = [regex]::Escape("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$($Case.VirtualPath) classification=FileOpen")
        $DispatchBefore = $Runner.Count($DispatchPattern)
        Send-ServerCommand "gui.activate $ExplorerId"
        Start-Sleep -Milliseconds 80
        Send-ServerCommand "gui.mouse $ExplorerId 400 104 1 down"
        Send-ServerCommand "gui.mouse $ExplorerId 400 104 1 up"
        Send-ServerCommand "gui.mouse $ExplorerId 400 104 1 down"
        Send-ServerCommand "gui.mouse $ExplorerId 400 104 1 up"
        Wait-OutputCount $DispatchPattern ($DispatchBefore + 1)
        [void](Assert-ImageViewerActivation $Case.VirtualPath $Case.Dimensions $LoadBefore)
        Assert-Phase11 ((Get-RuntimeOutput).Contains("reason=Active typed dispatch delivered an owned document activation to gxos.builtin.imageviewer")) `
            "File Explorer normal Open uses DesktopService and AppRegistry for $($Case.VirtualPath)"
        Close-FileExplorer $ExplorerId
    }

    $OpenWithExplorerBefore = $Runner.Count('FileExplorer window created: (\d+)')
    $OpenWithLoadBefore = $Runner.Count([regex]::Escape("ImageViewer loaded PNG: $OpenWithVirtualPath ($OpenWithDimensions)"))
    Send-ServerCommand ("desktop.open `"{0}`" dir" -f $OpenWithVirtualFolder)
    Wait-OutputCount 'FileExplorer window created: (\d+)' ($OpenWithExplorerBefore + 1)
    $ExplorerMatches = [regex]::Matches((Get-RuntimeOutput), 'FileExplorer window created: (\d+)')
    $OpenWithExplorerId = [uint64]$ExplorerMatches[$ExplorerMatches.Count - 1].Groups[1].Value
    Send-ServerCommand "gui.activate $OpenWithExplorerId"
    Send-ServerCommand "gui.mouse $OpenWithExplorerId 400 104 2 down"
    Send-ServerCommand "gui.mouse $OpenWithExplorerId 400 104 2 up"
    Wait-OutputCount ([regex]::Escape("FileExplorer context menu created for path=$OpenWithVirtualPath")) 1
    Send-ServerCommand "gui.mouse $OpenWithExplorerId 410 140 1 down"
    Send-ServerCommand "gui.mouse $OpenWithExplorerId 410 140 1 up"
    Wait-OutputCount ([regex]::Escape("FileExplorer Open With submenu opened path=$OpenWithVirtualPath handlers=1")) 1
    Send-ServerCommand "gui.mouse $OpenWithExplorerId 640 140 1 down"
    Send-ServerCommand "gui.mouse $OpenWithExplorerId 640 140 1 up"
    Wait-OutputCount ([regex]::Escape("FileExplorer Open With selected canonical appId=gxos.builtin.imageviewer path=$OpenWithVirtualPath")) 1
    [void](Assert-ImageViewerActivation $OpenWithVirtualPath $OpenWithDimensions $OpenWithLoadBefore)
    Assert-Phase11 ((Get-RuntimeOutput).Contains("handlers=1")) "Open With discovers ImageViewer as the single capable PNG handler"
    Close-FileExplorer $OpenWithExplorerId

    $ProbeStatuses = @(
        @{ Path = $MissingPath; Status = "NotFound"; Label = "missing PNG" },
        @{ Path = $EmptyPath; Status = "NotFound"; Label = "empty PNG" },
        @{ Path = $CorruptPath; Status = "UnsupportedFormat"; Label = "corrupt PNG contents" },
        @{ Path = $SignaturePath; Status = "DecodeFailed"; Label = "signature-only PNG" },
        @{ Path = $HeaderPath; Status = "DecodeFailed"; Label = "truncated PNG header/chunk" },
        @{ Path = $DataPath; Status = "DecodeFailed"; Label = "truncated PNG data" },
        @{ Path = $OversizedDimensionsPath; Status = "TooLarge"; Label = "oversized PNG dimensions" },
        @{ Path = $OversizedFilePath; Status = "TooLarge"; Label = "oversized PNG file" }
    )
    foreach ($Probe in $ProbeStatuses) {
        $VirtualPath = Get-VirtualPath $Probe.Path
        $ActivationBefore = $Runner.Count([regex]::Escape("Built-in document dispatcher delivered canonical activation appId=gxos.builtin.imageviewer path=$VirtualPath"))
        $FailureBefore = $Runner.Count([regex]::Escape("ImageViewer: image load failed: $VirtualPath status=$($Probe.Status)"))
        Send-ServerCommand ("desktop.open `"{0}`"" -f $VirtualPath)
        Wait-OutputCount ([regex]::Escape("Built-in document dispatcher delivered canonical activation appId=gxos.builtin.imageviewer path=$VirtualPath")) ($ActivationBefore + 1)
        Wait-OutputCount ([regex]::Escape("ImageViewer: image load failed: $VirtualPath status=$($Probe.Status)")) ($FailureBefore + 1)
        Assert-Phase11 $true "AppRegistry selected ImageViewer and decoder failed safely for $($Probe.Label) with $($Probe.Status)"
        $ProbeWindowId = Get-LatestImageViewerWindowId ([IO.Path]::GetFileName($Probe.Path))
        Close-ImageViewer $ProbeWindowId
    }

    $UnknownVirtualPath = Get-VirtualPath $UnknownPath
    $ViewerStartBefore = $Runner.Count([regex]::Escape("ImageViewer received App Model document activation appId=gxos.builtin.imageviewer path=$UnknownVirtualPath"))
    Send-ServerCommand ("desktop.open `"{0}`"" -f $UnknownVirtualPath)
    Wait-OutputCount ([regex]::Escape("Desktop filesystem open failed: No file association registered for $UnknownVirtualPath")) 1
    Assert-Phase11 ($Runner.Count([regex]::Escape("ImageViewer received App Model document activation appId=gxos.builtin.imageviewer path=$UnknownVirtualPath")) -eq $ViewerStartBefore) `
        "unknown image-like extensions remain unassociated and do not launch ImageViewer"

    Send-ServerCommand "desktop.launch gxos.builtin.settings"
    Wait-OutputCount 'Settings Center starting' 1
    $SettingsPattern = 'window id=(\d+) ownerPid=\d+ ownerName=[^\s]+ appId=gxos\.builtin\.settings title=Settings'
    $SettingsWindowBefore = $Runner.Count($SettingsPattern)
    Send-ServerCommand "desktop.windows.owners"
    Wait-OutputCount $SettingsPattern ($SettingsWindowBefore + 1)
    $SettingsMatches = [regex]::Matches((Get-RuntimeOutput), $SettingsPattern)
    $SettingsWindowId = [uint64]$SettingsMatches[$SettingsMatches.Count - 1].Groups[1].Value
    Send-ServerCommand "gui.activate $SettingsWindowId"
    Start-Sleep -Milliseconds 150
    # Settings lays out Apps sixth in the category list; the Default apps tab
    # is the right-hand tab at the top of that page (980x700 window).
    Send-ServerCommand "gui.mouse $SettingsWindowId 100 400 1 down"
    Send-ServerCommand "gui.mouse $SettingsWindowId 100 400 1 up"
    Start-Sleep -Milliseconds 100
    Send-ServerCommand "gui.mouse $SettingsWindowId 500 165 1 down"
    Send-ServerCommand "gui.mouse $SettingsWindowId 500 165 1 up"
    Wait-OutputCount ([regex]::Escape("[SettingsDefaultAppsModel] row extension=.png builtInDefault=gxos.builtin.imageviewer configuredOverride=none effectiveDefault=gxos.builtin.imageviewer effectiveDisplayName=Image Viewer handlers=1")) 1
    Assert-Phase11 $true "Settings Default Apps production model discovers .png and its AppRegistry built-in default"
    $OwnerEnd = $Runner.Count('DESKTOP_WINDOW_OWNERS_END') + 1
    Send-ServerCommand "desktop.windows.owners"
    Wait-OutputCount 'DESKTOP_WINDOW_OWNERS_END' $OwnerEnd
    $SettingsWindows = [regex]::Matches((Get-RuntimeOutput), 'window id=(\d+) ownerPid=(\d+) ownerName=[^\s]+ appId=gxos\.builtin\.settings title=Settings')
    if ($SettingsWindows.Count -gt 0) { Send-ServerCommand ("gui.close {0}" -f $SettingsWindows[$SettingsWindows.Count - 1].Groups[1].Value) }

    Send-ServerCommand "exit"
    if (-not $Runner.WaitForExit(20000)) { throw "Hosted runtime did not exit after the ImageViewer activation proof." }

    Write-Output ("phase11RuntimeChecks={0}/{0}" -f ($Checks - $Failures))
    Write-Output ("normalOpenActivations={0}/{0}" -f $ActivationCount)
    Write-Output "openWithActivations=1/1"
    Write-Output "decoderFailureProbes=8/8"
    Write-Output "settingsDefaultAppsPng=PASS"
    if ($Failures -ne 0) { throw "Phase 11 ImageViewer runtime proof failed ($Failures failed checks).`n$(Get-RuntimeOutput)" }
}
catch {
    Write-Output ("Phase 11 ImageViewer runtime smoke failed: " + $_.Exception.Message)
    $RuntimeText = Get-RuntimeOutput
    foreach ($Needle in @("Settings Center starting", "SettingsDefaultAppsModel", "desktop.open", "window id=", "gui.keyto")) {
        $Index = $RuntimeText.LastIndexOf($Needle, [StringComparison]::Ordinal)
        if ($Index -ge 0) {
            $Start = [Math]::Max(0, $Index - 100)
            $Length = [Math]::Min(360, $RuntimeText.Length - $Start)
            Write-Output ($RuntimeText.Substring($Start, $Length) -replace "\r?\n", " ")
        }
    }
    throw
}
finally {
    if ($null -ne $Runner) {
        try { Send-ServerCommand "exit" } catch { }
        if (-not $Runner.WaitForExit(3000)) { try { $Runner.Kill() } catch { } }
        $Runner.Dispose()
    }
    foreach ($Saved in $PersistedSnapshot.Values) {
        if ($Saved.Exists) {
            [IO.File]::WriteAllBytes($Saved.Path, $Saved.Bytes)
            [IO.File]::SetAttributes($Saved.Path, $Saved.Attributes)
            [IO.File]::SetLastWriteTimeUtc($Saved.Path, $Saved.WriteTime)
        } elseif (Test-Path -LiteralPath $Saved.Path -PathType Leaf) {
            [IO.File]::Delete($Saved.Path)
        }
    }
}
