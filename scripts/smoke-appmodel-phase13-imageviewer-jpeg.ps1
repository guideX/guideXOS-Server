[CmdletBinding()]
param([int]$TimeoutSeconds = 15)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "Run .\build.bat before this Phase 13 hosted-runtime smoke." }

$Stamp = [Guid]::NewGuid().ToString("N")
$TempRoot = [IO.Path]::GetFullPath((Join-Path $Root "tmp"))
$FixtureRoot = [IO.Path]::GetFullPath((Join-Path $TempRoot ("phase13-imageviewer-jpeg-" + $Stamp)))
if (-not $FixtureRoot.StartsWith($TempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved Phase 13 fixture path escaped the repository temp directory: $FixtureRoot"
}
$Sources = @(
    (Join-Path $Root "bkup\appdemo.jpg"),
    (Join-Path $Root "navigator_remote_image_2a7b107bf2d33622.jpg"),
    (Join-Path $Root "navigator_remote_image_c5ea2d8c743d34ee.jpg")
)
foreach ($Source in $Sources) { if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Required JPEG fixture missing: $Source" } }

function Get-JpegDimensions {
    param([string]$Path)
    $Bytes = [IO.File]::ReadAllBytes($Path)
    for ($Index = 2; $Index + 8 -lt $Bytes.Length; $Index++) {
        if ($Bytes[$Index] -ne 255) { continue }
        $Marker = [int]$Bytes[$Index + 1]
        if ($Marker -notin @(192, 193, 194)) { continue }
        $Height = ([int]$Bytes[$Index + 5] -shl 8) -bor [int]$Bytes[$Index + 6]
        $Width = ([int]$Bytes[$Index + 7] -shl 8) -bor [int]$Bytes[$Index + 8]
        return "${Width}x${Height}"
    }
    throw "No supported JPEG frame header found in fixture: $Path"
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
using System.Diagnostics;
using System.Text;
public sealed class Phase13ImageViewerRunner : IDisposable {
    private readonly Process process;
    private readonly object outputLock = new object();
    private readonly StringBuilder output = new StringBuilder();
    public Phase13ImageViewerRunner(ProcessStartInfo info) {
        process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (s,e) => { if (e.Data != null) Append(e.Data); };
        process.ErrorDataReceived += (s,e) => { if (e.Data != null) Append(e.Data); };
    }
    private void Append(string value) { lock (outputLock) output.AppendLine(value); }
    public bool Start() { if (!process.Start()) return false; process.BeginOutputReadLine(); process.BeginErrorReadLine(); return true; }
    public void Send(string value) { process.StandardInput.WriteLine(value); process.StandardInput.Flush(); }
    public string Output() { lock (outputLock) return output.ToString(); }
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
function Assert-Phase13 {
    param([bool]$Condition, [string]$Name)
    $script:Checks++
    if ($Condition) { Write-Host "PASS: $Name" }
    else { $script:Failures++; Write-Host "FAIL: $Name" }
}
function Get-RuntimeOutput { if ($null -eq $script:Runner) { return "" }; return $script:Runner.Output() }
function Send-ServerCommand { param([string]$Command); $script:Runner.Send($Command) }
function Wait-Output {
    param([string]$Pattern, [int]$Timeout = $TimeoutSeconds)
    $Until = [DateTime]::UtcNow.AddSeconds($Timeout)
    while ([DateTime]::UtcNow -lt $Until) {
        $Output = Get-RuntimeOutput
        if ([regex]::IsMatch($Output, $Pattern)) { return $Output }
        Start-Sleep -Milliseconds 50
    }
    $Output = Get-RuntimeOutput
    $Start = [Math]::Max(0, $Output.Length - 7000)
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
    $Start = [Math]::Max(0, $Output.Length - 7000)
    throw "Timed out waiting for $Expected matches of runtime pattern: $Pattern`n$($Output.Substring($Start))"
}
function Get-LatestViewerWindowId {
    param([string]$FileName)
    Send-ServerCommand "desktop.windows.owners"
    $OwnerPattern = 'window id=(\d+) ownerPid=(\d+) ownerName=ImageViewer appId=gxos\.builtin\.imageviewer title=Image Viewer - ' + [regex]::Escape($FileName) + ' visible=true'
    $Output = Wait-Output $OwnerPattern
    $Matches = [regex]::Matches($Output, $OwnerPattern)
    if ($Matches.Count -eq 0) { throw "ImageViewer has no compositor-owned window for $FileName.`n$Output" }
    return [uint64]$Matches[$Matches.Count - 1].Groups[1].Value
}
function Close-ImageViewer {
    param([uint64]$WindowId)
    Send-ServerCommand "gui.close $WindowId"
    [void](Wait-Output ([regex]::Escape("ImageViewer exiting window=$WindowId imageStateReleased=true activationPathReleased=true")))
}
function Open-FileExplorerImage {
    param($Case)
    $ExplorerBefore = [regex]::Matches((Get-RuntimeOutput), 'FileExplorer window created: (\d+)').Count
    Send-ServerCommand ("desktop.open `"{0}`" dir" -f $Case.VirtualFolder)
    $Output = Wait-OutputCount 'FileExplorer window created: (\d+)' ($ExplorerBefore + 1)
    $ExplorerMatches = [regex]::Matches($Output, 'FileExplorer window created: (\d+)')
    if ($ExplorerMatches.Count -le $ExplorerBefore) { throw "File Explorer did not create a new window for $($Case.VirtualPath).`n$Output" }
    $ExplorerId = [uint64]$ExplorerMatches[$ExplorerMatches.Count - 1].Groups[1].Value
    Send-ServerCommand "gui.activate $ExplorerId"
    Start-Sleep -Milliseconds 80
    Send-ServerCommand "gui.mouse $ExplorerId 400 104 1 down"
    Send-ServerCommand "gui.mouse $ExplorerId 400 104 1 up"
    Send-ServerCommand "gui.mouse $ExplorerId 400 104 1 down"
    Send-ServerCommand "gui.mouse $ExplorerId 400 104 1 up"
    $Dispatch = [regex]::Escape("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$($Case.VirtualPath) classification=FileOpen")
    $Output = Wait-Output $Dispatch
    [void](Wait-Output ([regex]::Escape("Built-in document dispatcher delivered canonical activation appId=gxos.builtin.imageviewer path=$($Case.VirtualPath)")))
    [void](Wait-Output ([regex]::Escape("ImageViewer received App Model document activation appId=gxos.builtin.imageviewer path=$($Case.VirtualPath) owner=0 generation=0")))
    [void](Wait-Output ([regex]::Escape("ImageViewer loaded JPEG: $($Case.VirtualPath) ($($Case.Dimensions))")))
    $WindowId = Get-LatestViewerWindowId ([IO.Path]::GetFileName($Case.VirtualPath))
    $OwnerPattern = 'window id=' + $WindowId + ' ownerPid=\d+ ownerName=ImageViewer appId=gxos\.builtin\.imageviewer title=Image Viewer - '
    Assert-Phase13 ($Output -match [regex]::Escape("handlerAppId: gxos.builtin.imageviewer") -or (Get-RuntimeOutput).Contains("canonical activation appId=gxos.builtin.imageviewer path=$($Case.VirtualPath)")) `
        "File Explorer Open resolves $($Case.VirtualPath) through AppRegistry and the built-in document dispatcher"
    Assert-Phase13 ((Get-RuntimeOutput) -match $OwnerPattern) "decoded JPEG has an ImageViewer-owned compositor window for $($Case.VirtualPath)"
    Close-ImageViewer $WindowId
    Send-ServerCommand "gui.close $ExplorerId"
    return $WindowId
}

try {
    New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
    $NormalCases = @(
        @{ Folder = (Join-Path $FixtureRoot "normal-jpg"); Name = "photo.jpg"; Source = $Sources[0] },
        @{ Folder = (Join-Path (Join-Path $FixtureRoot "normal-jpeg") "nested folder"); Name = "progressive.JPEG"; Source = $Sources[1] },
        @{ Folder = (Join-Path (Join-Path $FixtureRoot "normal-mixed") "nested"); Name = "mixed.JpG"; Source = $Sources[2] }
    )
    foreach ($Case in $NormalCases) {
        New-Item -ItemType Directory -Force -Path $Case.Folder | Out-Null
        $Case.Path = Join-Path $Case.Folder $Case.Name
        [IO.File]::Copy($Case.Source, $Case.Path, $true)
        $Case.VirtualPath = Get-VirtualPath $Case.Path
        $Case.VirtualFolder = Get-VirtualPath $Case.Folder
        $Case.Dimensions = Get-JpegDimensions $Case.Source
    }
    Assert-Phase13 (@($NormalCases.Dimensions | Select-Object -Unique).Count -ge 2) "valid JPEG fixtures cover multiple dimensions"

    $RenamedJpegAsBmpPath = Join-Path $NormalCases[0].Folder "valid-jpeg-renamed.bmp"
    [IO.File]::Copy($Sources[0], $RenamedJpegAsBmpPath, $true)
    $RenamedJpegAsBmpVirtualPath = Get-VirtualPath $RenamedJpegAsBmpPath

    $OpenWithFolder = Join-Path $FixtureRoot "open-with"
    New-Item -ItemType Directory -Force -Path $OpenWithFolder | Out-Null
    $OpenWithPath = Join-Path $OpenWithFolder "Open With.jpeg"
    [IO.File]::Copy($Sources[0], $OpenWithPath, $true)
    $OpenWithVirtualPath = Get-VirtualPath $OpenWithPath
    $OpenWithCase = @{ VirtualPath = $OpenWithVirtualPath; VirtualFolder = Get-VirtualPath $OpenWithFolder; Dimensions = Get-JpegDimensions $Sources[0] }

    $ProbeFolder = Join-Path $FixtureRoot "probes"
    New-Item -ItemType Directory -Force -Path $ProbeFolder | Out-Null
    $MissingPath = Join-Path $ProbeFolder "missing.jpg"
    $EmptyPath = Join-Path $ProbeFolder "empty.jpeg"
    $CorruptPath = Join-Path $ProbeFolder "corrupt.jpg"
    $TruncatedPath = Join-Path $ProbeFolder "truncated.jpeg"
    $HeaderOnlyPath = Join-Path $ProbeFolder "header-only.jpg"
    $OversizedDimensionsPath = Join-Path $ProbeFolder "oversized-dimensions.jpeg"
    $OversizedFilePath = Join-Path $ProbeFolder "oversized-file.jpg"
    [IO.File]::WriteAllBytes($EmptyPath, [byte[]]@())
    [IO.File]::WriteAllBytes($CorruptPath, [Text.Encoding]::ASCII.GetBytes("not a JPEG"))
    $KnownJpegBytes = [IO.File]::ReadAllBytes($Sources[0])
    [IO.File]::WriteAllBytes($TruncatedPath, [byte[]]$KnownJpegBytes[0..([Math]::Min(63, $KnownJpegBytes.Length - 1))])
    [IO.File]::WriteAllBytes($HeaderOnlyPath, [byte[]]$KnownJpegBytes[0..([Math]::Min(39, $KnownJpegBytes.Length - 1))])
    $OversizedDimensionBytes = [byte[]]$KnownJpegBytes.Clone()
    $SofPosition = -1
    for ($Index = 2; $Index + 8 -lt $OversizedDimensionBytes.Length; $Index++) {
        if ($OversizedDimensionBytes[$Index] -eq 255 -and $OversizedDimensionBytes[$Index + 1] -in @(192, 193, 194)) { $SofPosition = $Index; break }
    }
    if ($SofPosition -lt 0) { throw "Could not find a supported SOF marker in the valid JPEG fixture." }
    $OversizedDimensionBytes[$SofPosition + 7] = 16
    $OversizedDimensionBytes[$SofPosition + 8] = 1
    [IO.File]::WriteAllBytes($OversizedDimensionsPath, $OversizedDimensionBytes)
    [IO.File]::WriteAllBytes($OversizedFilePath, [byte[]]::new(4 * 1024 * 1024 + 1))

    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Exe
    $StartInfo.WorkingDirectory = $Root
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Runner = [Phase13ImageViewerRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start the hosted server runtime." }

    Send-ServerCommand "gui.start"
    Start-Sleep -Milliseconds 500
    Send-ServerCommand "desktop.appmodel.file-associations"
    $AssociationOutput = Wait-Output 'fileAssociationV1: OK'
    Assert-Phase13 ($AssociationOutput.Contains("extension=.png handlerAppId=gxos.builtin.imageviewer") -and
        $AssociationOutput.Contains("extension=.jpg handlerAppId=gxos.builtin.imageviewer") -and
        $AssociationOutput.Contains("extension=.jpeg handlerAppId=gxos.builtin.imageviewer")) `
        "PNG and JPEG handler declarations are surfaced through AppRegistry"
    Send-ServerCommand "desktop.open.resolve /phase13-case/photo.JpG"
    $ResolveOutput = Wait-Output 'handlerAppId: gxos\.builtin\.imageviewer'
    Assert-Phase13 ($ResolveOutput.Contains("handlerAppId: gxos.builtin.imageviewer")) "mixed-case JPG extension resolves to canonical ImageViewer in AppRegistry"

    foreach ($Case in $NormalCases) { [void](Open-FileExplorerImage $Case) }

    $ImageViewerDispatchPattern = [regex]::Escape("Built-in document dispatcher delivered canonical activation appId=gxos.builtin.imageviewer")
    $ImageViewerDispatchesBeforeBmpRename = [regex]::Matches((Get-RuntimeOutput), $ImageViewerDispatchPattern).Count
    Send-ServerCommand ("desktop.open `"{0}`"" -f $RenamedJpegAsBmpVirtualPath)
    $RenamedBmpOutput = Wait-Output ([regex]::Escape("Desktop open failed: No file association registered for $RenamedJpegAsBmpVirtualPath"))
    $ImageViewerDispatchesAfterBmpRename = [regex]::Matches((Get-RuntimeOutput), $ImageViewerDispatchPattern).Count
    Assert-Phase13 ($RenamedBmpOutput.Contains("Desktop open failed: No file association registered for $RenamedJpegAsBmpVirtualPath") -and
        $ImageViewerDispatchesBeforeBmpRename -eq $ImageViewerDispatchesAfterBmpRename) `
        "valid JPEG bytes renamed to BMP remain unsupported and do not launch ImageViewer"

    $OpenWithExplorerBefore = [regex]::Matches((Get-RuntimeOutput), 'FileExplorer window created: (\d+)').Count
    Send-ServerCommand ("desktop.open `"{0}`" dir" -f $OpenWithCase.VirtualFolder)
    $Output = Wait-OutputCount 'FileExplorer window created: (\d+)' ($OpenWithExplorerBefore + 1)
    $ExplorerMatches = [regex]::Matches($Output, 'FileExplorer window created: (\d+)')
    if ($ExplorerMatches.Count -le $OpenWithExplorerBefore) { throw "File Explorer did not open the Open With fixture." }
    $ExplorerId = [uint64]$ExplorerMatches[$ExplorerMatches.Count - 1].Groups[1].Value
    Send-ServerCommand "gui.activate $ExplorerId"
    Send-ServerCommand "gui.mouse $ExplorerId 400 104 2 down"
    Send-ServerCommand "gui.mouse $ExplorerId 400 104 2 up"
    [void](Wait-Output ([regex]::Escape("FileExplorer context menu created for path=$($OpenWithCase.VirtualPath)")))
    Send-ServerCommand "gui.mouse $ExplorerId 410 140 1 down"
    Send-ServerCommand "gui.mouse $ExplorerId 410 140 1 up"
    [void](Wait-Output ([regex]::Escape("FileExplorer Open With submenu opened path=$($OpenWithCase.VirtualPath) handlers=1")))
    Send-ServerCommand "gui.mouse $ExplorerId 640 140 1 down"
    Send-ServerCommand "gui.mouse $ExplorerId 640 140 1 up"
    [void](Wait-Output ([regex]::Escape("FileExplorer Open With selected canonical appId=gxos.builtin.imageviewer path=$($OpenWithCase.VirtualPath)")))
    [void](Wait-Output ([regex]::Escape("ImageViewer loaded JPEG: $($OpenWithCase.VirtualPath) ($($OpenWithCase.Dimensions))")))
    $OpenWithWindowId = Get-LatestViewerWindowId ([IO.Path]::GetFileName($OpenWithCase.VirtualPath))
    Assert-Phase13 ((Get-RuntimeOutput).Contains("handlers=1")) "Open With discovers ImageViewer generically as the sole JPEG handler"
    Close-ImageViewer $OpenWithWindowId
    Send-ServerCommand "gui.close $ExplorerId"

    $ProbeCases = @(
        @{ Path = $MissingPath; Status = "NotFound"; Label = "missing JPEG" },
        @{ Path = $EmptyPath; Status = "NotFound"; Label = "empty JPEG" },
        @{ Path = $CorruptPath; Status = "UnsupportedFormat"; Label = "corrupt JPEG bytes" },
        @{ Path = $TruncatedPath; Status = "DecodeFailed"; Label = "truncated JPEG" },
        @{ Path = $HeaderOnlyPath; Status = "DecodeFailed"; Label = "JPEG header without a complete scan" },
        @{ Path = $OversizedDimensionsPath; Status = "TooLarge"; Label = "JPEG dimension beyond 4096" },
        @{ Path = $OversizedFilePath; Status = "TooLarge"; Label = "JPEG file over 4 MiB" }
    )
    foreach ($Probe in $ProbeCases) {
        $VirtualPath = Get-VirtualPath $Probe.Path
        Send-ServerCommand ("desktop.open `"{0}`"" -f $VirtualPath)
        [void](Wait-Output ([regex]::Escape("Built-in document dispatcher delivered canonical activation appId=gxos.builtin.imageviewer path=$VirtualPath")))
        [void](Wait-Output ([regex]::Escape("ImageViewer: image load failed: $VirtualPath status=$($Probe.Status)")))
        Assert-Phase13 $true "AppRegistry activates ImageViewer safely for $($Probe.Label) ($($Probe.Status))"
        $ProbeWindowId = Get-LatestViewerWindowId ([IO.Path]::GetFileName($Probe.Path))
        Close-ImageViewer $ProbeWindowId
    }

    Send-ServerCommand "desktop.launch gxos.builtin.settings"
    [void](Wait-Output 'Settings Center starting')
    Send-ServerCommand "desktop.windows.owners"
    $SettingsPattern = 'window id=(\d+) ownerPid=\d+ ownerName=[^\s]+ appId=gxos\.builtin\.settings title=Settings'
    $SettingsOutput = Wait-Output $SettingsPattern
    $SettingsMatches = [regex]::Matches($SettingsOutput, $SettingsPattern)
    $SettingsWindowId = [uint64]$SettingsMatches[$SettingsMatches.Count - 1].Groups[1].Value
    Send-ServerCommand "gui.activate $SettingsWindowId"
    Start-Sleep -Milliseconds 150
    Send-ServerCommand "gui.mouse $SettingsWindowId 100 400 1 down"
    Send-ServerCommand "gui.mouse $SettingsWindowId 100 400 1 up"
    Start-Sleep -Milliseconds 100
    Send-ServerCommand "gui.mouse $SettingsWindowId 500 165 1 down"
    Send-ServerCommand "gui.mouse $SettingsWindowId 500 165 1 up"
    $JpgSettings = "[SettingsDefaultAppsModel] row extension=.jpg builtInDefault=gxos.builtin.imageviewer configuredOverride=none effectiveDefault=gxos.builtin.imageviewer effectiveDisplayName=Image Viewer handlers=1"
    $JpegSettings = "[SettingsDefaultAppsModel] row extension=.jpeg builtInDefault=gxos.builtin.imageviewer configuredOverride=none effectiveDefault=gxos.builtin.imageviewer effectiveDisplayName=Image Viewer handlers=1"
    [void](Wait-Output ([regex]::Escape($JpgSettings)))
    [void](Wait-Output ([regex]::Escape($JpegSettings)))
    Assert-Phase13 $true "Settings Default Apps discovers .jpg as ImageViewer built-in/effective with no override"
    Assert-Phase13 $true "Settings Default Apps discovers .jpeg as ImageViewer built-in/effective with no override"
    Send-ServerCommand ("gui.close {0}" -f $SettingsWindowId)

    Send-ServerCommand "exit"
    if (-not $Runner.WaitForExit(20000)) { throw "Hosted runtime did not exit after the Phase 13 JPEG proof." }
    Write-Output ("phase13RuntimeChecks={0}/{0}" -f ($Checks - $Failures))
    Write-Output "normalOpenJpegActivations=3/3"
    Write-Output "openWithJpegActivation=1/1"
    Write-Output "jpegDecoderFailureProbes=7/7"
    Write-Output "settingsDefaultAppsJpgJpeg=2/2"
    if ($Failures -ne 0) { throw "Phase 13 JPEG runtime proof failed ($Failures failed checks).`n$(Get-RuntimeOutput)" }
}
catch {
    Write-Output ("Phase 13 ImageViewer JPEG smoke failed: " + $_.Exception.Message)
    $RuntimeText = Get-RuntimeOutput
    $Start = [Math]::Max(0, $RuntimeText.Length - 10000)
    if ($RuntimeText.Length -gt 0) { Write-Output $RuntimeText.Substring($Start) }
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
    if (Test-Path -LiteralPath $FixtureRoot) { Remove-Item -LiteralPath $FixtureRoot -Recurse -Force }
}
