param(
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase4b-file-association-v1"
$FixtureRoot = Join-Path $TempRoot "fixtures"
$BackupRoot = Join-Path $TempRoot "restore-backup"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "guideXOSServer.exe not found: $Exe. Run .\build.bat first."
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "appmodel-phase4b-file-association-v1-$stamp.log"

$FixtureFolder = Join-Path $FixtureRoot "phase4b-folder"
$FixtureText = Join-Path $FixtureRoot "phase4b-safe-open.txt"
$FixtureLog = Join-Path $FixtureRoot "phase4b-safe-open.log"
$FixtureIni = Join-Path $FixtureRoot "phase4b-safe-open.ini"
$FixtureCfg = Join-Path $FixtureRoot "phase4b-safe-open.cfg"
$FixturePng = Join-Path $FixtureRoot "phase4b-legacy-image.png"
$FixtureJpg = Join-Path $FixtureRoot "phase4b-legacy-image.jpg"
$FixtureUnknown = Join-Path $FixtureRoot "phase4b-unsupported.xyz"
$FixtureExe = Join-Path $FixtureRoot "phase4b-risky.exe"
$FixtureGxapp = Join-Path $FixtureRoot "phase4b-risky.gxapp"
$FixtureElf = Join-Path $FixtureRoot "phase4b-risky.elf"

function Invoke-ServerCommands {
    param([string[]]$Commands)

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return (($inputText | & $Exe 2>&1) -join [Environment]::NewLine)
}

function Backup-TrackedArtifacts {
    $tracked = @(
        "desktop.json",
        "desktop.state",
        "display-options.cfg"
    )

    $state = [ordered]@{}
    foreach ($relativePath in $tracked) {
        $absolutePath = Join-Path $Root $relativePath
        $backupPath = Join-Path $BackupRoot $relativePath
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupPath) | Out-Null

        $exists = Test-Path -LiteralPath $absolutePath
        $state[$relativePath] = [pscustomobject]@{
            Exists = $exists
            Path = $absolutePath
            BackupPath = $backupPath
        }

        if ($exists) {
            Copy-Item -LiteralPath $absolutePath -Destination $backupPath -Force
        }
    }

    return $state
}

function Restore-TrackedArtifacts {
    param([hashtable]$State)

    foreach ($entry in $State.GetEnumerator()) {
        $record = $entry.Value
        if ($record.Exists) {
            Copy-Item -LiteralPath $record.BackupPath -Destination $record.Path -Force
        } else {
            Remove-Item -LiteralPath $record.Path -Force -ErrorAction SilentlyContinue
        }
    }
}

function Get-FileText {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return ""
    }
    return Get-Content -LiteralPath $Path -Raw
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Reason
    )

    Assert-True ($Text.Contains($Needle)) "Missing expected text for ${Reason}: $Needle"
}

function Assert-RegexCountAtLeast {
    param(
        [string]$Text,
        [string]$Pattern,
        [int]$Minimum,
        [string]$Reason
    )

    $count = [regex]::Matches($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    Assert-True ($count -ge $Minimum) "Expected at least $Minimum matches for ${Reason}, but saw $count. Pattern: $Pattern"
}

function Test-PathExists {
    param([string]$Path)

    return Test-Path -LiteralPath $Path
}

function New-AsciiTextFile {
    param(
        [string]$Path,
        [string]$Text
    )

    Set-Content -LiteralPath $Path -Value $Text -Encoding ASCII
}

$artifactState = Backup-TrackedArtifacts

try {
    New-Item -ItemType Directory -Force -Path $FixtureFolder | Out-Null
    New-AsciiTextFile -Path $FixtureText -Text @"
Phase 4B text file fixture
This file should open in Notepad.
"@
    New-AsciiTextFile -Path $FixtureLog -Text @"
Phase 4B log file fixture
This file should open in Notepad.
"@
    New-AsciiTextFile -Path $FixtureIni -Text @"
Phase 4B ini file fixture
This file should open in Notepad.
"@
    New-AsciiTextFile -Path $FixtureCfg -Text @"
Phase 4B cfg file fixture
This file should open in Notepad.
"@
    New-AsciiTextFile -Path $FixtureUnknown -Text @"
Phase 4B unknown file fixture
This file should stay unsupported.
"@
    New-AsciiTextFile -Path $FixtureExe -Text @"
Phase 4B executable-style fixture
This file should stay unsupported.
"@
    New-AsciiTextFile -Path $FixtureGxapp -Text @"
Phase 4B package-style fixture
This file should stay unsupported.
"@
    New-AsciiTextFile -Path $FixtureElf -Text @"
Phase 4B ELF-style fixture
This file should stay unsupported.
"@

    Copy-Item -LiteralPath (Join-Path $Root "assets\Backgrounds\ameoba.png") -Destination $FixturePng -Force
    Copy-Item -LiteralPath (Join-Path $Root "bkup\appmodeldemo.jpg") -Destination $FixtureJpg -Force
    New-Item -ItemType Directory -Force -Path $FixtureFolder | Out-Null

    $desktopJsonBefore = Get-FileText (Join-Path $Root "desktop.json")
    $desktopStateBefore = Get-FileText (Join-Path $Root "desktop.state")
    $displayOptionsBefore = Get-FileText (Join-Path $Root "display-options.cfg")

    $summaryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.summary"
    )
    $assocOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.file-associations"
    )
    $gateOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate"
    )
    $launchCompareOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch.compare"
    )
    $storagePreviewOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch.storage.preview"
    )

    Assert-Contains $summaryOutput "appModelPhase4BFileAssociationTableExists=true" "file association table exists"
    Assert-Contains $summaryOutput "appModelPhase4BFolderAssociationRegistered=true" "folder association registered"
    Assert-Contains $summaryOutput "appModelPhase4BTextAssociationsRegistered=true" "text associations registered"
    Assert-Contains $summaryOutput "appModelPhase4BHandlersResolveToRegistry=true" "handlers resolve to registry"
    Assert-Contains $summaryOutput "appModelPhase4BTextFilesOpenWithNotepad=true" "text files open with Notepad"
    Assert-Contains $summaryOutput "appModelPhase4BFoldersOpenWithFileExplorer=true" "folders open with File Explorer"
    Assert-Contains $summaryOutput "appModelPhase4BImagesRemainLegacy=true" "images remain legacy"
    Assert-Contains $summaryOutput "appModelPhase4BUnknownExtensionsFallback=true" "unknown fallback"
    Assert-Contains $summaryOutput "appModelPhase4BRiskyExtensionsNotActiveDispatchOwned=true" "risky excluded"
    Assert-Contains $summaryOutput "appModelPhase4BVisibleLaunchBehaviorChanged=false" "visible launch behavior unchanged"
    Assert-Contains $summaryOutput "appModelPhase4BPersistentDesktopStorageWrites=false" "persistent storage remains off"
    Assert-Contains $summaryOutput "appModelActiveDispatchEnabled=true" "product-default active typed dispatch"
    Assert-Contains $summaryOutput "appModelActiveDispatchEffectiveStateSource=product-default" "product-default active typed dispatch source"

    Assert-Contains $assocOutput "[FileAssociationV1]" "file association diagnostic section"
    Assert-Contains $assocOutput "registryResolved=true" "registry-backed handler resolution"
    Assert-Contains $assocOutput "fileAssociationV1KeyMappings: folder->File Explorer; .txt/.log/.ini/.cfg->Notepad; .png/.bmp/.jpg/.gif/.jpeg->Image Viewer (legacy direct path); unknown/risky->Unsupported" "key mappings"
    Assert-Contains $assocOutput "key=<folder> kind=folder" "folder table row"
    Assert-Contains $assocOutput "key=.txt kind=extension" "text table row"
    Assert-Contains $assocOutput "handlerAppId=gxos.builtin.notepad" "text table handler"
    Assert-Contains $assocOutput "key=.png kind=extension" "image table row"
    Assert-Contains $assocOutput "legacyDirectPath=true" "image table legacy direct path"
    Assert-Contains $assocOutput "key=<unknown> kind=unknown-fallback" "unknown table row"
    Assert-Contains $assocOutput "key=.exe kind=risky-fallback" "risky table row"

    Assert-Contains $gateOutput "appModelActiveDispatchEnabled=true" "gate default-on"
    Assert-Contains $gateOutput "appModelActiveDispatchEffectiveStateSource=product-default" "gate default source"
    Assert-Contains $gateOutput "visibleLaunchBehaviorChanged=false" "gate visible behavior unchanged"
    Assert-Contains $gateOutput "persistentDesktopStorageWrites=false" "gate persistent storage off"

    Assert-Contains $launchCompareOutput "overall: OK" "launch comparison remains OK"
    Assert-Contains $storagePreviewOutput "writesStorage: false" "storage preview remains non-persistent"

    $resolveOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.open.resolve $FixtureFolder dir",
        "desktop.open.resolve $FixtureText",
        "desktop.open.resolve $FixtureLog",
        "desktop.open.resolve $FixtureIni",
        "desktop.open.resolve $FixtureCfg",
        "desktop.open.resolve $FixturePng",
        "desktop.open.resolve $FixtureJpg",
        "desktop.open.resolve $FixtureUnknown",
        "desktop.open.resolve $FixtureExe",
        "desktop.open.resolve $FixtureGxapp",
        "desktop.open.resolve $FixtureElf"
    )

    Assert-Contains $resolveOutput "associationKey: <folder>" "folder resolve association"
    Assert-Contains $resolveOutput "associationKind: folder" "folder association kind"
    Assert-Contains $resolveOutput "handlerDisplayName: File Explorer" "folder handler display name"
    Assert-Contains $resolveOutput "launchTarget: FileExplorer" "folder launch target"
    Assert-Contains $resolveOutput "status: supported" "folder status"

    Assert-RegexCountAtLeast $resolveOutput 'associationKind: extension' 6 "supported extension kinds"
    Assert-RegexCountAtLeast $resolveOutput 'handlerDisplayName: Notepad' 4 "text handlers"
    Assert-RegexCountAtLeast $resolveOutput 'launchTarget: Notepad' 4 "text launch targets"
    Assert-RegexCountAtLeast $resolveOutput 'legacyDirectPath: true' 2 "image legacy direct path"
    Assert-RegexCountAtLeast $resolveOutput 'handlerDisplayName: Image Viewer' 2 "image handler display names"
    Assert-Contains $resolveOutput "associationKind: unknown-fallback" "unknown fallback kind"
    Assert-Contains $resolveOutput "launchTarget: Unsupported" "unknown fallback target"
    Assert-Contains $resolveOutput "associationKind: risky-fallback" "risky fallback kind"
    Assert-Contains $resolveOutput "risky: true" "risky classification"
    Assert-Contains $resolveOutput "handlerRegistryResolved: false" "unsupported rows stay out of registry-backed handler resolution"

    $forceOffOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate force-off",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`"",
        "desktop.open `"$FixtureLog`"",
        "desktop.open `"$FixtureIni`"",
        "desktop.open `"$FixtureCfg`"",
        "desktop.open `"$FixturePng`"",
        "desktop.open `"$FixtureJpg`"",
        "desktop.open `"$FixtureUnknown`"",
        "desktop.open `"$FixtureExe`"",
        "desktop.open `"$FixtureGxapp`"",
        "desktop.open `"$FixtureElf`""
    )

    Assert-Contains $forceOffOutput "mode: force-off" "force-off gate transition"
    Assert-Contains $forceOffOutput "appModelActiveDispatchEnabled=false" "force-off gate transition"
    Assert-Contains $forceOffOutput "appModelActiveDispatchEffectiveStateSource=force-off" "force-off gate transition"
    Assert-Contains $forceOffOutput "activeTypedDispatchHandled=false" "force-off fallback path"
    Assert-Contains $forceOffOutput "legacyFallbackUsed=true" "force-off fallback path"
    Assert-Contains $forceOffOutput "Desktop open failed: No file association registered for" "unsupported fallback failure"

    $resetOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`"",
        "desktop.open `"$FixtureLog`"",
        "desktop.open `"$FixtureIni`"",
        "desktop.open `"$FixtureCfg`"",
        "desktop.open `"$FixturePng`"",
        "desktop.open `"$FixtureJpg`""
    )

    Assert-Contains $resetOutput "mode: reset" "reset gate transition"
    Assert-Contains $resetOutput "appModelActiveDispatchEnabled=true" "reset returns to enabled"
    Assert-Contains $resetOutput "appModelActiveDispatchEffectiveStateSource=product-default" "reset returns to product-default"
    Assert-Contains $resetOutput "appModelActiveDispatchCurrentState=true" "reset current state"
    Assert-Contains $resetOutput "activeTypedDispatchHandled=true" "reset restored active typed dispatch"
    Assert-Contains $resetOutput "reason=Active typed dispatch handled the folder open in File Explorer" "reset folder active reason"
    Assert-Contains $resetOutput "reason=Active typed dispatch handled the text-file open in Notepad" "reset text active reason"
    Assert-Contains $resetOutput "activeTypedDispatchHandled=false" "image remains legacy after reset"
    Assert-Contains $resetOutput "selectedHandler=ImageViewer" "image remains legacy after reset"
    Assert-Contains $resetOutput "legacyFallbackUsed=true" "image remains legacy after reset"

} finally {
    Restore-TrackedArtifacts -State $artifactState
    Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$cleanupVerified = (-not (Test-Path -LiteralPath $TempRoot)) -and (-not (Test-Path -LiteralPath $BackupRoot))
$desktopJsonAfterCleanup = Get-FileText (Join-Path $Root "desktop.json")
$desktopStateAfterCleanup = Get-FileText (Join-Path $Root "desktop.state")
$displayOptionsAfterCleanup = Get-FileText (Join-Path $Root "display-options.cfg")
$phase4BTemporarySmokeStateRestored =
    $cleanupVerified -and
    ($desktopJsonBefore -eq $desktopJsonAfterCleanup) -and
    ($desktopStateBefore -eq $desktopStateAfterCleanup) -and
    ($displayOptionsBefore -eq $displayOptionsAfterCleanup)
$phase4BGeneratedSmokeArtifactsCleaned = $cleanupVerified

$reportLines = @(
    "[AppModelPhase4BFileAssociationV1Smoke]",
    "mode=hosted",
    "appModelPhase4BFileAssociationTableExists=true",
    "appModelPhase4BFolderAssociationRegistered=true",
    "appModelPhase4BTextAssociationsRegistered=true",
    "appModelPhase4BHandlersResolveToRegistry=true",
    "appModelPhase4BTextFilesOpenWithNotepad=true",
    "appModelPhase4BFoldersOpenWithFileExplorer=true",
    "appModelPhase4BImagesRemainLegacy=true",
    "appModelPhase4BUnknownExtensionsFallback=true",
    "appModelPhase4BRiskyExtensionsNotActiveDispatchOwned=true",
    "appModelPhase4BVisibleLaunchBehaviorChanged=false",
    "appModelPhase4BPersistentDesktopStorageWrites=false",
    "appModelPhase4BTemporarySmokeStateRestored=$($phase4BTemporarySmokeStateRestored.ToString().ToLowerInvariant())",
    "appModelPhase4BGeneratedSmokeArtifactsCleaned=$($phase4BGeneratedSmokeArtifactsCleaned.ToString().ToLowerInvariant())",
    "result=PASS"
)

$logParts = @(
    ($reportLines -join [Environment]::NewLine),
    "",
    "[summary-output]",
    $summaryOutput,
    "",
    "[association-output]",
    $assocOutput,
    "",
    "[gate-output]",
    $gateOutput,
    "",
    "[launch-compare-output]",
    $launchCompareOutput,
    "",
    "[storage-preview-output]",
    $storagePreviewOutput,
    "",
    "[resolve-output]",
    $resolveOutput,
    "",
    "[force-off-output]",
    $forceOffOutput,
    "",
    "[reset-output]",
    $resetOutput
)

Set-Content -LiteralPath $SmokeLog -Value ($logParts -join [Environment]::NewLine) -Encoding ASCII

Write-Output ($reportLines -join [Environment]::NewLine)
Write-Host "Smoke log: $SmokeLog"
Write-Host "appModelPhase4BTemporaryArtifactsCleaned=$($phase4BGeneratedSmokeArtifactsCleaned.ToString().ToLowerInvariant())"
Write-Host "appModelPhase4BTemporarySmokeStateRestored=$($phase4BTemporarySmokeStateRestored.ToString().ToLowerInvariant())"
