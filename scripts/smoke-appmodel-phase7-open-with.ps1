[CmdletBinding()]
param(
    [int]$ActivationCount = 20,
    [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe)) { throw "Run .\build.bat before this smoke." }
if ($ActivationCount -lt 20) { throw "Phase 7 runtime proof requires at least 20 explicit menu activations." }

$Stamp = [Guid]::NewGuid().ToString("N")
$TempRoot = [IO.Path]::GetFullPath((Join-Path $Root "tmp"))
$FixtureRoot = [IO.Path]::GetFullPath((Join-Path $TempRoot ("appmodel-phase7-open-with-" + $Stamp)))
if (-not $FixtureRoot.StartsWith($TempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved Phase 7 fixture path escaped the repository temp directory: $FixtureRoot"
}
$NestedRoot = Join-Path $FixtureRoot "nested"
$DocumentPath = Join-Path $NestedRoot "README.TXT"
$RelativePath = $DocumentPath.Substring($Root.TrimEnd([IO.Path]::DirectorySeparatorChar).Length + 1).Replace([string][char]92, '/')
$VirtualPath = "/" + $RelativePath
$VirtualRoot = "/" + $NestedRoot.Substring($Root.TrimEnd([IO.Path]::DirectorySeparatorChar).Length + 1).Replace([string][char]92, '/')
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
$Runner = $null
$Failures = 0
$Checks = 0

$RunnerSource = @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Linq;

public sealed class Phase7OpenWithRuntimeRunner : IDisposable
{
    private readonly Process process;
    private readonly ConcurrentQueue<string> output = new ConcurrentQueue<string>();

    public Phase7OpenWithRuntimeRunner(ProcessStartInfo startInfo)
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
    public bool HasExited { get { return process.HasExited; } }
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

function Wait-OutputMatch {
    param([string]$Pattern, [int]$Timeout = $script:TimeoutSeconds)
    $Timer = [Diagnostics.Stopwatch]::StartNew()
    while ($Timer.Elapsed.TotalSeconds -lt $Timeout) {
        $Match = [regex]::Match((Get-RuntimeOutput), $Pattern)
        if ($Match.Success) { return $Match }
        Start-Sleep -Milliseconds 40
    }
    throw "Timed out waiting for runtime output matching: $Pattern"
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

function Assert-Phase7 {
    param([bool]$Condition, [string]$Name)
    $script:Checks++
    if ($Condition) { Write-Host "PASS: $Name" }
    else { $script:Failures++; Write-Host "FAIL: $Name" }
}

function Invoke-OpenWithClick {
    param([uint64]$WindowId, [int]$Cycle, [string]$ExpectedPath)
    $MenuPattern = [regex]::Escape("FileExplorer context menu created for path=$ExpectedPath")
    $SubmenuPattern = [regex]::Escape("FileExplorer Open With submenu opened path=$ExpectedPath handlers=1")
    $SelectionPattern = [regex]::Escape("FileExplorer Open With selected canonical appId=gxos.builtin.notepad path=$ExpectedPath")
    $ActivationPattern = [regex]::Escape("Notepad: Document activation received appId=gxos.builtin.notepad path=$ExpectedPath")

    # The fixture folder contains one row. Return focus to Explorer because the
    # previous activation brings Notepad to the foreground. Coordinates target the first file row,
    # then Open With, then the only launchable child item in the existing menu.
    Send-ServerCommand "gui.activate $WindowId"
    Start-Sleep -Milliseconds 80
    Send-ServerCommand "gui.mouse $WindowId 400 104 2 down"
    Send-ServerCommand "gui.mouse $WindowId 400 104 2 up"
    [void](Wait-OutputCount -Pattern $MenuPattern -Expected $Cycle)
    Send-ServerCommand "gui.mouse $WindowId 410 140 1 down"
    Send-ServerCommand "gui.mouse $WindowId 410 140 1 up"
    [void](Wait-OutputCount -Pattern $SubmenuPattern -Expected $Cycle)
    Send-ServerCommand "gui.mouse $WindowId 640 140 1 down"
    Send-ServerCommand "gui.mouse $WindowId 640 140 1 up"
    [void](Wait-OutputCount -Pattern $SelectionPattern -Expected $Cycle)
    [void](Wait-OutputCount -Pattern $ActivationPattern -Expected $Cycle)

    # Close the clean Notepad window after each activation so Explorer is again
    # the topmost window and the next cycle exercises a fresh production menu.
    [void](Wait-OutputCount -Pattern 'Notepad window created: \d+' -Expected $Cycle)
    $NotepadWindows = [regex]::Matches((Get-RuntimeOutput), 'Notepad window created: (\d+)')
    if ($NotepadWindows.Count -lt $Cycle) { throw "Notepad did not create a window for activation $Cycle." }
    $NotepadWindowId = $NotepadWindows[$Cycle - 1].Groups[1].Value
    Send-ServerCommand "gui.close $NotepadWindowId"
    [void](Wait-OutputCount -Pattern ([regex]::Escape("Notepad closing...")) -Expected $Cycle)
}

try {
    Write-Output "Phase 7 Open With runtime smoke: creating the nested text fixture."
    New-Item -ItemType Directory -Force -Path $NestedRoot | Out-Null
    [IO.File]::WriteAllText($DocumentPath, "phase7 exact document activation`nline two`n", [Text.UTF8Encoding]::new($false))

    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Exe
    $StartInfo.WorkingDirectory = $Root
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Runner = [Phase7OpenWithRuntimeRunner]::new($StartInfo)
    if (-not $Runner.Start()) { throw "Could not start hosted server runtime." }

    Write-Output "Phase 7 Open With runtime smoke: starting the hosted desktop and File Explorer."
    Send-ServerCommand "gui.start"
    Start-Sleep -Milliseconds 300
    Send-ServerCommand "desktop.open `"$VirtualRoot`" dir"
    $WindowMatch = Wait-OutputMatch -Pattern 'FileExplorer window created: (\d+)'
    $WindowId = [uint64]$WindowMatch.Groups[1].Value
    Assert-Phase7 ($WindowId -gt 0) "production File Explorer created a window for the nested test directory"

    for ($Cycle = 1; $Cycle -le $ActivationCount; $Cycle++) {
        Invoke-OpenWithClick -WindowId $WindowId -Cycle $Cycle -ExpectedPath $VirtualPath
    }
    Assert-Phase7 (([regex]::Matches((Get-RuntimeOutput), [regex]::Escape("FileExplorer Open With selected canonical appId=gxos.builtin.notepad path=$VirtualPath")).Count) -eq $ActivationCount) `
        "$ActivationCount menu selections retained the canonical Notepad identity and exact mixed-case nested path"
    Assert-Phase7 (([regex]::Matches((Get-RuntimeOutput), [regex]::Escape("Notepad: Document activation received appId=gxos.builtin.notepad path=$VirtualPath")).Count) -eq $ActivationCount) `
        "$ActivationCount explicit activations reached Notepad with the exact owned path"

    Send-ServerCommand "desktop.open `"$VirtualPath`""
    [void](Wait-OutputCount -Pattern ([regex]::Escape("reason=Active typed dispatch delivered an owned document activation to gxos.builtin.notepad")) -Expected 1)
    Assert-Phase7 (([regex]::Matches((Get-RuntimeOutput), [regex]::Escape("Notepad: Document activation received appId=gxos.builtin.notepad path=$VirtualPath")).Count) -eq ($ActivationCount + 1)) `
        "ordinary Open after one-time Open With still resolves to the unchanged Notepad default"

    Send-ServerCommand "exit"
    if (-not $Runner.WaitForExit(20000)) { throw "Hosted runtime did not exit after the Open With proof." }

    Write-Host "oneHandlerOpenWith=PASS"
    Write-Host ("explicitMenuActivations={0}/{0}" -f $ActivationCount)
    Write-Host "ordinaryDefaultAfterOneTimeChoice=PASS"
    Write-Host ("runtimeChecks={0}/{0}" -f ($Checks - $Failures))
    if ($Failures -ne 0) { throw "Phase 7 File Explorer Open With runtime proof failed ($Failures failed checks).`n$(Get-RuntimeOutput)" }
}
catch {
    Write-Output ("Phase 7 Open With runtime smoke failed: " + $_.Exception.Message)
    $RuntimeLines = @((Get-RuntimeOutput) -split "`r?`n" | Where-Object { $_ -match 'FileExplorer (Open With|context menu)|Document activation received|Notepad: Loaded file|Notepad window created|Notepad closing' })
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
    if (Test-Path -LiteralPath $FixtureRoot) { Remove-Item -LiteralPath $FixtureRoot -Recurse -Force }
}
