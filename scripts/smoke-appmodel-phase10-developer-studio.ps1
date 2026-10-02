[CmdletBinding()]
param(
    [ValidateRange(3, 20)]
    [int]$ActivationCount = 3,
    [int]$TimeoutSeconds = 12
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.experimental.exe"
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Run .\build-native-experimental.bat before this Phase 10 hosted-runtime smoke."
}

$Stamp = [Guid]::NewGuid().ToString("N")
$TempRoot = [IO.Path]::GetFullPath((Join-Path $Root "tmp"))
$FixtureRoot = [IO.Path]::GetFullPath((Join-Path $TempRoot ("p10-" + $Stamp)))
if (-not $FixtureRoot.StartsWith($TempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved Phase 10 fixture path escaped the repository temp directory: $FixtureRoot"
}
$TextRoot = Join-Path $FixtureRoot "nested text"
$CppRoot = Join-Path $FixtureRoot "nested source"
$TextPath = Join-Path $TextRoot "Shared.TXT"
$CppPath = Join-Path $CppRoot "ordinary-open.CPP"
$RootPrefix = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$TextVirtualPath = "/" + $TextPath.Substring($RootPrefix.Length).Replace([string][char]92, '/')
$TextVirtualRoot = "/" + $TextRoot.Substring($RootPrefix.Length).Replace([string][char]92, '/')
$CppVirtualPath = "/" + $CppPath.Substring($RootPrefix.Length).Replace([string][char]92, '/')
$CppVirtualRoot = "/" + $CppRoot.Substring($RootPrefix.Length).Replace([string][char]92, '/')
$TextHostPath = $TextPath.Replace([string][char]92, '/')
$CppHostPath = $CppPath.Replace([string][char]92, '/')
$TextModelPath = $TextHostPath.Substring(0, 2).ToLowerInvariant() + $TextHostPath.Substring(2)
$CppModelPath = $CppHostPath.Substring(0, 2).ToLowerInvariant() + $CppHostPath.Substring(2)

$DesktopJsonPath = Join-Path $Root "desktop.json"
$DesktopJsonBytes = [IO.File]::ReadAllBytes($DesktopJsonPath)
$DesktopJsonAttributes = [IO.File]::GetAttributes($DesktopJsonPath)
$DesktopJsonWriteTime = [IO.File]::GetLastWriteTimeUtc($DesktopJsonPath)
$WindowBoundsPath = Join-Path $Root "window-bounds.cfg"
$WindowBoundsExisted = Test-Path -LiteralPath $WindowBoundsPath
if ($WindowBoundsExisted) {
    $WindowBoundsBytes = [IO.File]::ReadAllBytes($WindowBoundsPath)
    $WindowBoundsAttributes = [IO.File]::GetAttributes($WindowBoundsPath)
    $WindowBoundsWriteTime = [IO.File]::GetLastWriteTimeUtc($WindowBoundsPath)
}
$DefaultHandlersPath = Join-Path $Root "appmodel-default-handlers.cfg"
$DefaultHandlersExisted = Test-Path -LiteralPath $DefaultHandlersPath
if ($DefaultHandlersExisted) {
    $DefaultHandlersBytes = [IO.File]::ReadAllBytes($DefaultHandlersPath)
    $DefaultHandlersAttributes = [IO.File]::GetAttributes($DefaultHandlersPath)
    $DefaultHandlersWriteTime = [IO.File]::GetLastWriteTimeUtc($DefaultHandlersPath)
}

$Runner = $null
$Checks = 0
$Failures = 0
$RunnerSource = @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;

public sealed class Phase10DeveloperStudioRuntimeRunner : IDisposable
{
    private readonly Process process;
    private readonly ConcurrentQueue<string> output = new ConcurrentQueue<string>();
    public Phase10DeveloperStudioRuntimeRunner(ProcessStartInfo startInfo)
    {
        process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
        process.OutputDataReceived += (sender, args) => { if (args.Data != null) output.Enqueue(args.Data); };
        process.ErrorDataReceived += (sender, args) => { if (args.Data != null) output.Enqueue(args.Data); };
    }
    public bool Start()
    {
        if (!process.Start()) return false;
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        return true;
    }
    public void Send(string command)
    {
        process.StandardInput.WriteLine(command);
        process.StandardInput.Flush();
    }
    public string GetOutput() { return string.Join(Environment.NewLine, output.ToArray()); }
    public bool WaitForExit(int timeoutMs)
    {
        bool exited = process.WaitForExit(timeoutMs);
        if (exited) process.WaitForExit();
        return exited;
    }
    public void Stop(int timeoutMs)
    {
        if (process.HasExited) return;
        try { Send("exit"); process.StandardInput.Close(); } catch { }
        if (!WaitForExit(timeoutMs)) process.Kill();
    }
    public void Dispose() { process.Dispose(); }
}

public static class Phase10DeveloperStudioRuntimeProof
{
    public static string Fnv1a64(byte[] bytes)
    {
        ulong hash = 1469598103934665603UL;
        foreach (byte value in bytes) { hash ^= value; unchecked { hash *= 1099511628211UL; } }
        return hash.ToString(CultureInfo.InvariantCulture);
    }
}
'@
Add-Type -TypeDefinition $RunnerSource -Language CSharp

function Get-RuntimeOutput {
    if ($null -eq $script:Runner) { return "" }
    return $script:Runner.GetOutput()
}

function Send-ServerCommand {
    param([string]$Command)
    $script:Runner.Send($Command)
}

function Wait-OutputCount {
    param([string]$Pattern, [int]$Expected, [int]$Timeout = $script:TimeoutSeconds)
    $Timer = [Diagnostics.Stopwatch]::StartNew()
    while ($Timer.Elapsed.TotalSeconds -lt $Timeout) {
        $Count = [regex]::Matches((Get-RuntimeOutput), $Pattern).Count
        if ($Count -ge $Expected) { return $Count }
        Start-Sleep -Milliseconds 40
    }
    throw "Timed out waiting for $Expected occurrences of: $Pattern"
}

function Assert-Phase10 {
    param([bool]$Condition, [string]$Name)
    $script:Checks++
    if ($Condition) { Write-Host "PASS: $Name" }
    else { $script:Failures++; Write-Host "FAIL: $Name" }
}

function Invoke-DocumentClose {
    param([int]$Cycle)
    $Output = Get-RuntimeOutput
    $SnapshotStart = $Output.LastIndexOf("DESKTOP_WINDOW_OWNERS_BEGIN", [StringComparison]::Ordinal)
    $BeforeSnapshots = [regex]::Matches($Output, "DESKTOP_WINDOW_OWNERS_BEGIN").Count
    Send-ServerCommand "desktop.windows.owners"
    [void](Wait-OutputCount -Pattern "DESKTOP_WINDOW_OWNERS_END" -Expected ($BeforeSnapshots + 1))
    $Output = Get-RuntimeOutput
    $SnapshotStart = $Output.LastIndexOf("DESKTOP_WINDOW_OWNERS_BEGIN", [StringComparison]::Ordinal)
    $SnapshotEnd = $Output.IndexOf("DESKTOP_WINDOW_OWNERS_END", $SnapshotStart, [StringComparison]::Ordinal)
    if ($SnapshotStart -lt 0 -or $SnapshotEnd -lt 0) { throw "Window ownership snapshot was incomplete." }
    $Snapshot = $Output.Substring($SnapshotStart, $SnapshotEnd - $SnapshotStart)
    $WindowMatches = [regex]::Matches($Snapshot, 'window id=(\d+) ownerPid=\d+ ownerName=.*? appId=com\.guidexos\.developerstudio title=guideXOS Developer Studio')
    if ($WindowMatches.Count -ne 1) { throw "Expected one owned Developer Studio window; found $($WindowMatches.Count).`n$Snapshot" }
    $WindowId = $WindowMatches[0].Groups[1].Value
    Send-ServerCommand "gui.close $WindowId"
    [void](Wait-OutputCount -Pattern 'GUIDEXOS_DEVELOPER_STUDIO_MARKER clean_close=PASS' -Expected $Cycle)
    Assert-Phase10 $true "Developer Studio window/process $Cycle closed through hosted GUI ownership"
}

try {
    New-Item -ItemType Directory -Force -Path $TextRoot, $CppRoot | Out-Null
    $TextBytes = [Text.Encoding]::UTF8.GetBytes("Phase 10 shared document`r`nline two with LF`n")
    $CppBytes = [Text.Encoding]::UTF8.GetBytes("int phase10_source = 10;`r`n")
    [IO.File]::WriteAllBytes($TextPath, $TextBytes)
    [IO.File]::WriteAllBytes($CppPath, $CppBytes)
    $TextHash = [Phase10DeveloperStudioRuntimeProof]::Fnv1a64($TextBytes)
    $CppHash = [Phase10DeveloperStudioRuntimeProof]::Fnv1a64($CppBytes)

    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Exe
    $StartInfo.WorkingDirectory = $Root
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Runner = [Phase10DeveloperStudioRuntimeRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start hosted experimental runtime." }

    Send-ServerCommand "gui.start"
    Start-Sleep -Milliseconds 300
    Send-ServerCommand "desktop.open `"$TextVirtualRoot`" dir"
    $FirstExplorer = Wait-OutputCount -Pattern 'FileExplorer window created: (\d+)' -Expected 1
    $FirstExplorerId = [regex]::Matches((Get-RuntimeOutput), 'FileExplorer window created: (\d+)')[$FirstExplorer - 1].Groups[1].Value

    for ($Cycle = 1; $Cycle -le $ActivationCount; $Cycle++) {
        Send-ServerCommand "gui.activate $FirstExplorerId"
        Start-Sleep -Milliseconds 100
        Send-ServerCommand "gui.mouse $FirstExplorerId 400 104 2 down"
        Send-ServerCommand "gui.mouse $FirstExplorerId 400 104 2 up"
        [void](Wait-OutputCount -Pattern ([regex]::Escape("FileExplorer context menu created for path=$TextVirtualPath")) -Expected $Cycle)
        Send-ServerCommand "gui.mouse $FirstExplorerId 410 140 1 down"
        Send-ServerCommand "gui.mouse $FirstExplorerId 410 140 1 up"
        [void](Wait-OutputCount -Pattern ([regex]::Escape("FileExplorer Open With submenu opened path=$TextVirtualPath handlers=2")) -Expected $Cycle)
        Send-ServerCommand "gui.mouse $FirstExplorerId 640 164 1 down"
        Send-ServerCommand "gui.mouse $FirstExplorerId 640 164 1 up"
        [void](Wait-OutputCount -Pattern ([regex]::Escape("FileExplorer Open With selected canonical appId=com.guidexos.developerstudio path=$TextVirtualPath")) -Expected $Cycle)
        $TextActivationPattern = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER appmodel_document_activation=PASS received=' +
            [regex]::Escape($TextHostPath) + ' model=' + [regex]::Escape($TextModelPath) +
            ' bytes=' + $TextBytes.Length + ' fnv1a64=' + $TextHash
        [void](Wait-OutputCount -Pattern $TextActivationPattern -Expected $Cycle)
        Assert-Phase10 $true "production Open With delivered the exact nested mixed-case .TXT path and CRLF/LF bytes ($Cycle)"
        Invoke-DocumentClose -Cycle $Cycle
    }

    Send-ServerCommand "desktop.recent"
    [void](Wait-OutputCount -Pattern 'Recent Documents \(\d+\):' -Expected 1)
    $StudioRecentOutput = Get-RuntimeOutput
    $StudioRecentStart = $StudioRecentOutput.LastIndexOf("Recent Programs (", [StringComparison]::Ordinal)
    $StudioRecentBlock = if ($StudioRecentStart -ge 0) { $StudioRecentOutput.Substring($StudioRecentStart) } else { "" }
    Assert-Phase10 ($StudioRecentBlock.Contains("guideXOS Developer Studio") -and
        $StudioRecentBlock.Contains("Recent Documents (0):") -and
        -not $StudioRecentBlock.Contains($TextVirtualPath)) `
        "Developer Studio activation records its app in Recent Programs without adding the file to Recent Documents"

    Send-ServerCommand "gui.activate $FirstExplorerId"
    Start-Sleep -Milliseconds 100
    Send-ServerCommand "gui.mouse $FirstExplorerId 400 104 2 down"
    Send-ServerCommand "gui.mouse $FirstExplorerId 400 104 2 up"
    [void](Wait-OutputCount -Pattern ([regex]::Escape("FileExplorer context menu created for path=$TextVirtualPath")) -Expected ($ActivationCount + 1))
    Send-ServerCommand "gui.mouse $FirstExplorerId 410 140 1 down"
    Send-ServerCommand "gui.mouse $FirstExplorerId 410 140 1 up"
    [void](Wait-OutputCount -Pattern ([regex]::Escape("FileExplorer Open With submenu opened path=$TextVirtualPath handlers=2")) -Expected ($ActivationCount + 1))
    Send-ServerCommand "gui.mouse $FirstExplorerId 640 140 1 down"
    Send-ServerCommand "gui.mouse $FirstExplorerId 640 140 1 up"
    [void](Wait-OutputCount -Pattern ([regex]::Escape("FileExplorer Open With selected canonical appId=gxos.builtin.notepad path=$TextVirtualPath")) -Expected 1)
    [void](Wait-OutputCount -Pattern ([regex]::Escape("Notepad: Document activation received appId=gxos.builtin.notepad path=$TextVirtualPath")) -Expected 1)
    [void](Wait-OutputCount -Pattern 'Notepad window created: (\d+)' -Expected 1)
    $NotepadMatches = [regex]::Matches((Get-RuntimeOutput), 'Notepad window created: (\d+)')
    $NotepadWindowId = $NotepadMatches[$NotepadMatches.Count - 1].Groups[1].Value
    Send-ServerCommand "gui.close $NotepadWindowId"
    [void](Wait-OutputCount -Pattern ([regex]::Escape("Notepad closing...")) -Expected 1)
    Assert-Phase10 $true "the same production submenu launches Notepad by its canonical ID as a one-time choice"

    Send-ServerCommand "desktop.open `"$CppVirtualRoot`" dir"
    [void](Wait-OutputCount -Pattern 'FileExplorer window created: (\d+)' -Expected 2)
    $ExplorerMatches = [regex]::Matches((Get-RuntimeOutput), 'FileExplorer window created: (\d+)')
    $CppExplorerId = $ExplorerMatches[1].Groups[1].Value
    Send-ServerCommand "gui.activate $CppExplorerId"
    Start-Sleep -Milliseconds 100
    Send-ServerCommand "gui.mouse $CppExplorerId 400 104 2 down"
    Send-ServerCommand "gui.mouse $CppExplorerId 400 104 2 up"
    [void](Wait-OutputCount -Pattern ([regex]::Escape("FileExplorer context menu created for path=$CppVirtualPath")) -Expected 1)
    Send-ServerCommand "gui.mouse $CppExplorerId 410 116 1 down"
    Send-ServerCommand "gui.mouse $CppExplorerId 410 116 1 up"
    [void](Wait-OutputCount -Pattern ([regex]::Escape("reason=Active typed dispatch delivered an owned document activation to com.guidexos.developerstudio")) -Expected 1)
    $CppActivationPattern = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER appmodel_document_activation=PASS received=' +
        [regex]::Escape($CppHostPath) + ' model=' + [regex]::Escape($CppModelPath) +
        ' bytes=' + $CppBytes.Length + ' fnv1a64=' + $CppHash
    [void](Wait-OutputCount -Pattern $CppActivationPattern -Expected 1)
    Assert-Phase10 $true "File Explorer context-menu Open resolved the single-handler .CPP default through AppRegistry"
    Invoke-DocumentClose -Cycle ($ActivationCount + 1)

    Send-ServerCommand "exit"
    if (-not $Runner.WaitForExit(20000)) { throw "Hosted runtime did not exit after the Phase 10 activation proof." }

    Write-Output "productionSharedHandlerCount=2 (gxos.builtin.notepad, com.guidexos.developerstudio)"
    Write-Output ("developerStudioRealProcessLaunchCloseCycles={0}/{0}" -f ($ActivationCount + 1))
    Write-Output ("hostedRuntimeChecks={0}/{0}" -f ($Checks - $Failures))
    if ($Failures -ne 0) { throw "Phase 10 hosted runtime proof failed ($Failures failed checks).`n$(Get-RuntimeOutput)" }
}
catch {
    Write-Output ("Phase 10 Developer Studio hosted smoke failed: " + $_.Exception.Message)
    $RuntimeLines = @((Get-RuntimeOutput) -split "`r?`n" | Where-Object {
        $_ -match 'FileExplorer (Open With|context menu)|Document activation|developerstudio|main_window_creation|appmodel_document_activation|clean_close|window id='
    })
    if ($RuntimeLines.Count -gt 0) { Write-Output ($RuntimeLines -join [Environment]::NewLine) }
    throw
}
finally {
    if ($null -ne $Runner) {
        $Runner.Stop(5000)
        $Runner.Dispose()
    }
    [IO.File]::WriteAllBytes($DesktopJsonPath, $DesktopJsonBytes)
    [IO.File]::SetLastWriteTimeUtc($DesktopJsonPath, $DesktopJsonWriteTime)
    [IO.File]::SetAttributes($DesktopJsonPath, $DesktopJsonAttributes)
    if ($WindowBoundsExisted) {
        [IO.File]::WriteAllBytes($WindowBoundsPath, $WindowBoundsBytes)
        [IO.File]::SetLastWriteTimeUtc($WindowBoundsPath, $WindowBoundsWriteTime)
        [IO.File]::SetAttributes($WindowBoundsPath, $WindowBoundsAttributes)
    } elseif (Test-Path -LiteralPath $WindowBoundsPath) {
        [IO.File]::Delete($WindowBoundsPath)
    }
    if ($DefaultHandlersExisted) {
        [IO.File]::WriteAllBytes($DefaultHandlersPath, $DefaultHandlersBytes)
        [IO.File]::SetLastWriteTimeUtc($DefaultHandlersPath, $DefaultHandlersWriteTime)
        [IO.File]::SetAttributes($DefaultHandlersPath, $DefaultHandlersAttributes)
    } elseif (Test-Path -LiteralPath $DefaultHandlersPath) {
        [IO.File]::Delete($DefaultHandlersPath)
    }
    if (Test-Path -LiteralPath $FixtureRoot) {
        $ResolvedFixture = [IO.Path]::GetFullPath($FixtureRoot)
        if (-not $ResolvedFixture.StartsWith($TempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean a Phase 10 fixture outside the repository temp directory: $ResolvedFixture"
        }
        Remove-Item -LiteralPath $ResolvedFixture -Recurse -Force
    }
}
