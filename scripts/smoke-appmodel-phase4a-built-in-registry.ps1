param(
    [switch]$BuildHosted
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
$TempDir = Join-Path $Root "tmp\appmodel-phase4a-built-in-registry"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "appmodel-phase4a-built-in-registry-smoke-$stamp.log"
$TempArtifact = Join-Path $TempDir "phase4a-registry-smoke.txt"
$reportLines = $null
$cleanupOk = $false

function Invoke-ServerCommands {
    param([string[]]$Commands)

    $exe = Join-Path $Root "guideXOSServer.exe"
    if (-not (Test-Path $exe)) {
        throw "guideXOSServer.exe not found. Run .\build.bat first or pass -BuildHosted."
    }

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return $inputText | & $exe 2>&1
}

function Assert-Contains {
    param(
        [object]$Text,
        [string]$Needle,
        [string]$Reason
    )

    $haystack = ($Text | Out-String)
    if ($haystack -notmatch [regex]::Escape($Needle)) {
        throw "Missing expected text for ${Reason}: $Needle"
    }
}

function Assert-RegexCountAtLeast {
    param(
        [object]$Text,
        [string]$Pattern,
        [int]$Minimum,
        [string]$Reason
    )

    $haystack = ($Text | Out-String)
    $count = [regex]::Matches($haystack, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    if ($count -lt $Minimum) {
        throw "Expected at least $Minimum matches for ${Reason}, but saw $count. Pattern: $Pattern"
    }
}

Push-Location $Root
try {
    if ($BuildHosted) {
        & cmd.exe /c build.bat
        if ($LASTEXITCODE -ne 0) {
            throw "Hosted build failed with exit code $LASTEXITCODE"
        }
    }

    $summaryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.summary"
    )
    $coverageOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.coverage"
    )
    $launchCompareOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch.compare"
    )
    $launchTypesOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch.types"
    )
    $storagePreviewOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch.storage.preview"
    )

    Assert-Contains $summaryOutput "appModelPhase4ABuiltInRegistryExists=true" "registry exists"
    Assert-Contains $summaryOutput "appModelPhase4AStableAppIdsUnique=true" "stable app ids unique"
    Assert-Contains $summaryOutput "appModelPhase4ADisplayNamesNonEmpty=true" "display names non-empty"
    Assert-Contains $summaryOutput "appModelPhase4AAliasResolutionOk=true" "alias resolution"
    Assert-Contains $summaryOutput "appModelPhase4AStartMenuAppsRegistered=true" "start menu coverage"
    Assert-Contains $summaryOutput "appModelPhase4AActiveDispatchAppsRegistered=true" "active dispatch coverage"
    Assert-Contains $summaryOutput "appModelPhase4ARecentProgramNamesAligned=true" "recent program coverage"
    Assert-Contains $summaryOutput "appModelPhase4ARiskyEntriesNotActiveDispatchOwned=true" "risky entries excluded"
    Assert-Contains $summaryOutput "appModelPhase4AVisibleLaunchBehaviorChanged=false" "visible behavior unchanged"
    Assert-Contains $summaryOutput "appModelPhase4APersistentDesktopStorageWrites=false" "persistent desktop storage remains off"
    Assert-Contains $summaryOutput "appModelActiveDispatchEnabled=true" "product-default active typed dispatch"
    Assert-Contains $summaryOutput "appModelActiveDispatchVisibleLaunchBehaviorChanged=false" "visible launch behavior preserved"

    Assert-Contains $coverageOutput "[BuiltInMetadataCoverage]" "coverage section"
    Assert-Contains $coverageOutput "registryValidation:" "registry validation section"
    Assert-Contains $coverageOutput "alias=Files" "Files alias record"
    Assert-Contains $coverageOutput "canonicalAppId=gxos.builtin.fileexplorer" "Files/File Explorer canonical identity"
    Assert-Contains $coverageOutput "alias=File Explorer" "File Explorer alias record"
    Assert-Contains $coverageOutput "alias=Settings" "Settings alias record"
    Assert-Contains $coverageOutput "canonicalAppId=gxos.builtin.controlpanel" "Settings canonical identity"
    Assert-Contains $coverageOutput "alias=System Settings" "System Settings alias record"
    Assert-Contains $coverageOutput "canonicalAppId=gxos.builtin.displayoptions" "System Settings canonical identity"
    Assert-Contains $coverageOutput "alias=Display Options" "Display Options alias record"
    Assert-Contains $coverageOutput "alias=AppModel" "AppModel alias record"
    Assert-Contains $coverageOutput "alias=ImgViewer" "ImgViewer alias record"
    Assert-Contains $coverageOutput "alias=Terminal" "Terminal alias record"
    Assert-Contains $coverageOutput "record id=gxos.builtin.onscreenkeyboard displayName=OnScreenKeyboard" "on-screen keyboard registry record"
    Assert-Contains $coverageOutput "record id=gxos.builtin.shutdowndialog displayName=ShutdownDialog" "shutdown dialog registry record"
    Assert-Contains $coverageOutput "record id=gxos.builtin.nativeappdebugviewer displayName=Native App Debug Viewer" "native app debug viewer registry record"
    Assert-Contains $coverageOutput "record id=gxos.builtin.hdinstaller displayName=HDInstaller" "installer registry record"
    Assert-RegexCountAtLeast $coverageOutput 'record id=gxos\.builtin\.onscreenkeyboard .* riskyForActiveTypedDispatch=true' 1 "on-screen keyboard risky flag"
    Assert-RegexCountAtLeast $coverageOutput 'record id=gxos\.builtin\.shutdowndialog .* riskyForActiveTypedDispatch=true' 1 "shutdown dialog risky flag"
    Assert-RegexCountAtLeast $coverageOutput 'record id=gxos\.builtin\.nativeappdebugviewer .* riskyForActiveTypedDispatch=true' 1 "native app debug viewer risky flag"
    Assert-RegexCountAtLeast $coverageOutput 'record id=gxos\.builtin\.hdinstaller .* riskyForActiveTypedDispatch=true' 1 "installer risky flag"
    Assert-Contains $coverageOutput "registryValidation:" "registry validation summary"
    Assert-Contains $coverageOutput "persistentDesktopStorageWrites=false" "coverage storage marker"

    Assert-Contains $launchCompareOutput "overall: OK" "launch comparison health"
    Assert-Contains $launchTypesOutput "status: OK" "launch type coverage health"
    Assert-Contains $storagePreviewOutput "writesStorage: false" "storage preview non-persistent"

    $reportLines = @(
        "[AppModelPhase4ARegistrySmoke]",
        "mode=diagnostic-only",
        "appModelPhase4ABuiltInRegistryExists=true",
        "appModelPhase4AStableAppIdsUnique=true",
        "appModelPhase4AStartMenuAppsRegistered=true",
        "appModelPhase4AActiveDispatchAppsRegistered=true",
        "appModelPhase4ARecentProgramNamesAligned=true",
        "appModelPhase4ARiskyEntriesNotActiveDispatchOwned=true",
        "appModelPhase4AVisibleLaunchBehaviorChanged=false",
        "appModelPhase4APersistentDesktopStorageWrites=false",
        "persistentDesktopStorageWrites=false",
        "launchComparison=OK",
        "launchTypes=OK",
        "storagePreviewWritesStorage=false",
        "result=PASS"
    )

    Set-Content -Path $TempArtifact -Value ($reportLines -join [Environment]::NewLine) -Encoding ASCII
} finally {
    Pop-Location
    if (Test-Path -LiteralPath $TempDir) {
        Remove-Item -LiteralPath $TempDir -Recurse -Force
    }
    $cleanupOk = -not (Test-Path -LiteralPath $TempDir)
}

if ($null -eq $reportLines) {
    throw "Phase 4A registry smoke did not produce a report"
}

$finalReportLines = @($reportLines + @(
    "appModelPhase4ADisplayNamesNonEmpty=true",
    "appModelPhase4AAliasResolutionOk=true",
    "generatedSmokeArtifactsCleaned=$($cleanupOk.ToString().ToLowerInvariant())",
    "tempArtifact=$TempArtifact"
))
$report = $finalReportLines -join [Environment]::NewLine
Set-Content -Path $SmokeLog -Value ($report + [Environment]::NewLine) -Encoding ASCII

Write-Output $report
Write-Host "Smoke log: $SmokeLog"
if (-not $cleanupOk) {
    throw "Temporary smoke artifacts were not cleaned up"
}
