[CmdletBinding()]
param(
    [int]$ActivationCount = 20
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $Exe)) { throw "Run .\build.bat before this smoke." }
if ($ActivationCount -lt 20) { throw "Phase 6 runtime proof requires at least 20 document activations." }

$Stamp = [Guid]::NewGuid().ToString("N")
$FixtureRoot = [IO.Path]::GetFullPath((Join-Path $Root ("tmp\appmodel-phase6-runtime-" + $Stamp)))
$TempRoot = [IO.Path]::GetFullPath((Join-Path $Root "tmp"))
if (-not $FixtureRoot.StartsWith($TempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved Phase 6 fixture path escaped the repository temp directory: $FixtureRoot"
}
$NestedRoot = Join-Path $FixtureRoot "nested"
New-Item -ItemType Directory -Force -Path $NestedRoot | Out-Null

$CheckCounts = @{ fileExplorer = 0; notepad = 0; negative = 0 }
$CheckPassCounts = @{ fileExplorer = 0; notepad = 0; negative = 0 }
$Failures = 0
function Assert-Phase6 {
    param([bool]$Condition, [string]$Group, [string]$Name)
    $CheckCounts[$Group]++
    if ($Condition) {
        $CheckPassCounts[$Group]++
        Write-Host ("PASS: {0}" -f $Name)
    }
    else {
        Write-Host ("FAIL: {0}" -f $Name)
        $script:Failures++
    }
}

function Write-TextFixture {
    param([string]$Path, [string]$Text, [bool]$Crlf = $false)
    $Encoding = [Text.UTF8Encoding]::new($false)
    if ($Crlf) { $Text = $Text.Replace("`n", "`r`n") }
    [IO.File]::WriteAllText($Path, $Text, $Encoding)
}

function Get-VirtualPath {
    param([string]$Path)
    $FullPath = [IO.Path]::GetFullPath($Path)
    $RootPrefix = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $FullPath.StartsWith($RootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Fixture escaped the hosted File Explorer root: $FullPath"
    }
    return "/" + $FullPath.Substring($RootPrefix.Length).Replace([string][char]92, '/')
}

try {
    $DirectoryFixture = Join-Path $FixtureRoot "directory"
    $DirectoryVirtualPath = Get-VirtualPath $DirectoryFixture
    New-Item -ItemType Directory -Force -Path $DirectoryFixture | Out-Null
    $UnavailablePath = Join-Path $FixtureRoot "unsupported-backend.gxdemo"
    $UnknownPath = Join-Path $FixtureRoot "unsupported.xyz"
    $ExtensionlessPath = Join-Path $FixtureRoot "extensionless"
    $ExecutablePath = Join-Path $FixtureRoot "not-an-app.exe"
    Write-TextFixture $UnavailablePath "unsupported handler fixture`n"
    Write-TextFixture $UnknownPath "unknown extension fixture`n"
    Write-TextFixture $ExtensionlessPath "no extension fixture`n"
    Write-TextFixture $ExecutablePath "not an executable fixture`n"

    $Activations = @()
    for ($Index = 0; $Index -lt $ActivationCount; $Index++) {
        $Directory = if (($Index % 2) -eq 0) { $FixtureRoot } else { $NestedRoot }
        $Extension = switch ($Index % 4) {
            0 { ".txt" }
            1 { ".log" }
            2 { ".ini" }
            default { ".TXT" }
        }
        $Path = Join-Path $Directory ("document-{0:D2}{1}" -f $Index, $Extension)
        $Text = "phase6-document-{0}`nline-two`n" -f $Index
        Write-TextFixture -Path $Path -Text $Text -Crlf (($Index % 5) -eq 0)
        $Activations += [pscustomobject]@{ Path = $Path; VirtualPath = (Get-VirtualPath $Path); Length = (Get-Item -LiteralPath $Path).Length }
    }

    $Commands = [Collections.Generic.List[string]]::new()
    $Commands.Add("gui.start")
    $Commands.Add("desktop.open `"$DirectoryVirtualPath`" dir")
    foreach ($Activation in $Activations) { $Commands.Add("desktop.open `"$($Activation.VirtualPath)`"") }
    $MissingPath = Get-VirtualPath (Join-Path $FixtureRoot "missing-document.txt")
    $UnavailableVirtualPath = Get-VirtualPath $UnavailablePath
    $UnknownVirtualPath = Get-VirtualPath $UnknownPath
    $ExtensionlessVirtualPath = Get-VirtualPath $ExtensionlessPath
    $ExecutableVirtualPath = Get-VirtualPath $ExecutablePath
    $OverlongPath = ("p" * 4093) + ".txt"
    $Commands.Add("desktop.open `"$UnknownVirtualPath`"")
    $Commands.Add("desktop.open `"$ExtensionlessVirtualPath`"")
    $Commands.Add("desktop.open `"$ExecutableVirtualPath`"")
    $Commands.Add("desktop.open `"$UnavailableVirtualPath`"")
    $Commands.Add("desktop.open `"$MissingPath`"")
    $Commands.Add("desktop.open `"$(Join-Path $FixtureRoot 'unknown-no-extension')`"")
    $Commands.Add("desktop.open `"$OverlongPath`"")
    $Commands.Add("notepad")

    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Exe
    $StartInfo.WorkingDirectory = $Root
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $StartInfo
    if (-not $Process.Start()) { throw "Could not start hosted server runtime." }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    foreach ($Command in $Commands) {
        $Process.StandardInput.WriteLine($Command)
        $Process.StandardInput.Flush()
        Start-Sleep -Milliseconds 250
    }
    Start-Sleep -Milliseconds 750
    $Process.StandardInput.WriteLine("exit")
    $Process.StandardInput.Close()
    if (-not $Process.WaitForExit(20000)) {
        $Process.Kill()
        throw "Hosted runtime did not exit after the activation batch."
    }
    $Output = $StdoutTask.Result + [Environment]::NewLine + $StderrTask.Result

    Assert-Phase6 ($Output.Contains("source=HostedFilesystemEntry request=$DirectoryVirtualPath") -and
        $Output.Contains("selectedHandler=File Explorer")) "fileExplorer" "directory open stays on the File Explorer route"
    Assert-Phase6 ($Output.Contains("reason=Active typed dispatch handled the folder open in File Explorer")) "fileExplorer" "directory navigation is reported separately from extension associations"
    Assert-Phase6 ($Output.Contains("FileExplorer starting...")) "fileExplorer" "directory activation starts File Explorer on the requested virtual path"
    Assert-Phase6 ($Output.Contains("Desktop filesystem open requested path=$UnknownVirtualPath") -and
        $Output.Contains("No file association registered for")) "fileExplorer" "File Explorer Open service rejects unknown extensions safely"
    Assert-Phase6 ($Output.Contains("Desktop filesystem open requested path=$ExtensionlessVirtualPath") -and
        $Output.Contains("No file association registered for")) "fileExplorer" "extensionless files fail closed"
    Assert-Phase6 ($Output.Contains("Desktop filesystem open requested path=$ExecutableVirtualPath") -and
        $Output.Contains("No file association registered for")) "fileExplorer" "executable-style files do not fall through to command execution"
    Assert-Phase6 ($Output.Contains("Document association rejected for $UnavailableVirtualPath") -and
        $Output.Contains("does not declare document activation support")) "fileExplorer" "registered handler without activation capability is unavailable"
    Assert-Phase6 ($Output.Contains("Desktop open failed: Invalid or overlong document path") -and
        -not $Output.Contains($OverlongPath)) "negative" "overlength document path fails closed without truncation or oversized diagnostics"
    Assert-Phase6 ($Output.Contains("Desktop open successful: $MissingPath")) "negative" "missing associated document reaches its registered handler without fallback redirection"
    Assert-Phase6 ($Output.Contains("Notepad: Failed to read file: $MissingPath")) "notepad" "Notepad reports the missing file through its existing VFS loader"
    Assert-Phase6 ($Output.Contains("Document activation received appId=gxos.builtin.notepad path=$MissingPath")) "notepad" "missing-file request reached the canonical Notepad handler without redirection"

    $LoadedCount = [regex]::Matches($Output, "Notepad: Loaded file:").Count
    $ReceivedCount = [regex]::Matches($Output, "Notepad: Document activation received appId=gxos\.builtin\.notepad").Count
    Assert-Phase6 ($LoadedCount -eq $ActivationCount) "notepad" "Notepad loaded all $ActivationCount repeated document activations"
    Assert-Phase6 ($ReceivedCount -eq ($ActivationCount + 1)) "notepad" "Notepad received each owned activation plus the expected missing-file request"
    foreach ($Activation in $Activations) {
        $Expected = "Notepad: Loaded file: $($Activation.VirtualPath) ($($Activation.Length) bytes)"
        Assert-Phase6 ($Output.Contains($Expected)) "notepad" "Notepad loaded the exact path and byte count for $([IO.Path]::GetFileName($Activation.Path))"
    }
    foreach ($Needle in @(".TXT", "document-00.txt", "document-01.log", "document-02.ini")) {
        Assert-Phase6 ($Output.Contains($Needle)) "notepad" "runtime evidence includes representative extension case and text formats ($Needle)"
    }
    Assert-Phase6 ($Output.Contains("Notepad starting...")) "notepad" "ordinary Notepad launch without a document remains supported"
    Assert-Phase6 (-not $Output.Contains("No file association registered for $ExecutablePath`r`nNotepad starting")) "negative" "unsupported executable path does not create an activation"

    Write-Host ("fileExplorerChecks={0}/{1}" -f $CheckPassCounts.fileExplorer, $CheckCounts.fileExplorer)
    Write-Host ("notepadChecks={0}/{1}" -f $CheckPassCounts.notepad, $CheckCounts.notepad)
    Write-Host ("negativeRuntimeChecks={0}/{1}" -f $CheckPassCounts.negative, $CheckCounts.negative)
    Write-Host ("sampleDocumentHostPath={0}" -f $Activations[0].Path)
    Write-Host ("sampleDocumentVirtualPath={0}" -f $Activations[0].VirtualPath)
    if ($Failures -ne 0) { throw "Phase 6 hosted document activation smoke failed ($Failures failed assertions). Output follows:`n$Output" }
    Write-Host "Phase 6 hosted document activation runtime proof PASS."
}
finally {
    if (Test-Path -LiteralPath $FixtureRoot) {
        Remove-Item -LiteralPath $FixtureRoot -Recurse -Force
    }
}
