param(
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase4d-recent-programs"
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
$SmokeLog = Join-Path $LogDir "appmodel-phase4d-recent-programs-$stamp.log"

$FixtureFolder = Join-Path $FixtureRoot "phase4d-folder"
$FixtureText = Join-Path $FixtureRoot "phase4d-safe-open.txt"
$FixtureImage = Join-Path $FixtureRoot "phase4d-legacy-image.png"
$FixtureUnsupported = Join-Path $FixtureRoot "phase4d-unsupported.xyz"
$FixtureRiskyExe = Join-Path $FixtureRoot "phase4d-risky.exe"
$FixtureRiskyGxapp = Join-Path $FixtureRoot "phase4d-risky.gxapp"
$FixtureRiskyElf = Join-Path $FixtureRoot "phase4d-risky.elf"

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

function Get-DesktopRecentNames {
    $desktopJsonPath = Join-Path $Root "desktop.json"
    if (-not (Test-Path -LiteralPath $desktopJsonPath)) {
        return @()
    }

    try {
        $cfg = Get-Content -LiteralPath $desktopJsonPath -Raw | ConvertFrom-Json
        if ($null -eq $cfg.recent) {
            return @()
        }
        if ($cfg.recent -is [string]) {
            return @($cfg.recent.ToString())
        }
        return @($cfg.recent | ForEach-Object { $_.ToString() })
    } catch {
        return @()
    }
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

function Assert-RecentTop {
    param(
        [string]$Expected,
        [string]$Reason
    )

    $recent = @(Get-DesktopRecentNames)
    Assert-True ($recent.Count -gt 0) "Recent program list is empty while checking ${Reason}"
    Assert-True ($recent[0] -eq $Expected) "Unexpected top recent while checking ${Reason}: expected '$Expected' but saw '$($recent[0])'"
}

function Assert-RecentContainsAll {
    param(
        [string[]]$Expected,
        [string]$Reason
    )

    $recent = @(Get-DesktopRecentNames)
    foreach ($name in $Expected) {
        Assert-True ($recent -contains $name) "Missing expected recent '$name' while checking ${Reason}"
    }
}

function Assert-RecentExcludesAny {
    param(
        [string[]]$Excluded,
        [string]$Reason
    )

    $recent = @(Get-DesktopRecentNames)
    foreach ($name in $Excluded) {
        Assert-True ($recent -notcontains $name) "Unexpected recent '$name' while checking ${Reason}"
    }
}

function Invoke-LaunchAndAssertTop {
    param(
        [string[]]$Commands,
        [string]$ExpectedTop,
        [string]$Reason,
        [string[]]$RecentContains = @(),
        [string[]]$RecentExcludes = @(),
        [string[]]$OutputNeedles = @()
    )

    $output = Invoke-ServerCommands -Commands $Commands
    foreach ($needle in $OutputNeedles) {
        Assert-Contains $output $needle $Reason
    }
    Assert-RecentTop -Expected $ExpectedTop -Reason $Reason
    if ($RecentContains.Count -gt 0) {
        Assert-RecentContainsAll -Expected $RecentContains -Reason $Reason
    }
    if ($RecentExcludes.Count -gt 0) {
        Assert-RecentExcludesAny -Excluded $RecentExcludes -Reason $Reason
    }
    return $output
}

$artifactState = Backup-TrackedArtifacts
$desktopJsonBefore = Get-FileText (Join-Path $Root "desktop.json")
$desktopStateBefore = Get-FileText (Join-Path $Root "desktop.state")
$displayOptionsBefore = Get-FileText (Join-Path $Root "display-options.cfg")

$phase4DRecentProgramWritePathsInventoried = $false
$phase4DRecentProgramNamesRegistryAligned = $false
$phase4DNormalAppLaunchesRecordCanonicalRecents = $false
$phase4DShellObjectsNoUnexpectedRecents = $false
$phase4DFileFolderRecentsStable = $false
$phase4DUnsupportedTargetsDoNotPolluteRecents = $false
$phase4DRemoveRecentStillWorks = $false
$phase4DForceOffRecentsStable = $false
$phase4DResetProductDefaultRecentsStable = $false
$phase4DVisibleLaunchBehaviorChanged = $false
$phase4DPersistentDesktopStorageWrites = $false
$phase4DTemporarySmokeStateRestored = $false

try {
    New-Item -ItemType Directory -Force -Path $FixtureFolder | Out-Null
    Set-Content -LiteralPath $FixtureText -Value @"
Phase 4D recent-program smoke fixture
This text file should open through Notepad.
"@ -Encoding ASCII
    Copy-Item -LiteralPath (Join-Path $Root "assets\Backgrounds\ameoba.png") -Destination $FixtureImage -Force
    Set-Content -LiteralPath $FixtureUnsupported -Value @"
Phase 4D unsupported file smoke fixture
This file intentionally uses an unsupported extension.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureRiskyExe -Value @"
Phase 4D risky executable-style fixture
This file should stay unsupported.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureRiskyGxapp -Value @"
Phase 4D risky package-style fixture
This file should stay unsupported.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureRiskyElf -Value @"
Phase 4D risky ELF-style fixture
This file should stay unsupported.
"@ -Encoding ASCII

    $preflightOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.summary",
        "desktop.appmodel.shell-objects",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.recent"
    )

    Assert-Contains $preflightOutput "appModelPhase4DRecentProgramWritePathsInventoried=true" "phase 4D summary marker"
    Assert-Contains $preflightOutput "appModelPhase4DRecentProgramNamesRegistryAligned=true" "phase 4D summary marker"
    Assert-Contains $preflightOutput "appModelPhase4DCanonicalRecentCapableBuiltIns=12" "phase 4D canonical count"
    Assert-Contains $preflightOutput "appModelPhase4DRegistryRecentCapableBuiltIns=14" "phase 4D registry count"
    Assert-Contains $preflightOutput "appModelPhase4DShellObjectsRecentAllowed=7" "phase 4D shell allowed count"
    Assert-Contains $preflightOutput "appModelPhase4DShellObjectsRecentSuppressed=3" "phase 4D shell suppressed count"
    Assert-Contains $preflightOutput "appModelPhase4DShellObjectRecentPolicyAligned=true" "phase 4D shell policy"
    Assert-Contains $preflightOutput "appModelActiveDispatchEnabled=true" "product-default active dispatch"
    Assert-Contains $preflightOutput "appModelActiveDispatchEffectiveStateSource=product-default" "product-default active dispatch source"
    Assert-Contains $preflightOutput "visibleLaunchBehaviorChanged=false" "visible launch behavior unchanged"
    Assert-Contains $preflightOutput "persistentDesktopStorageWrites=false" "persistent storage remains off"
    Assert-Contains $preflightOutput "shellObjectRegistry:" "shell registry section"
    Assert-Contains $preflightOutput "shouldWriteRecentPrograms=true" "shell object registry policy field"

    $phase4DRecentProgramWritePathsInventoried = $true
    $phase4DRecentProgramNamesRegistryAligned = $true
    $phase4DVisibleLaunchBehaviorChanged = $false
    $phase4DPersistentDesktopStorageWrites = $false

    $desktopJsonPath = Join-Path $Root "desktop.json"
    if (Test-Path -LiteralPath $desktopJsonPath) {
        $desktopConfigFixture = Get-Content -LiteralPath $desktopJsonPath -Raw | ConvertFrom-Json
        $desktopConfigFixture.recent = @()
        ($desktopConfigFixture | ConvertTo-Json -Depth 64) | Set-Content -LiteralPath $desktopJsonPath -Encoding ASCII
    }
    Assert-True (@(Get-DesktopRecentNames).Count -eq 0) "Phase 4D smoke should start from an empty recent-program fixture"

    $normalAppLaunches = @(
        @{ Command = "desktop.launch Notepad"; Expected = "Notepad"; Recent = @("Notepad") },
        @{ Command = "desktop.launch Calculator"; Expected = "Calculator"; Recent = @("Calculator", "Notepad") },
        @{ Command = "desktop.launch Clock"; Expected = "Clock"; Recent = @("Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch Console"; Expected = "Console"; Recent = @("Console", "Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch FileExplorer"; Expected = "File Explorer"; Recent = @("File Explorer", "Console", "Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch Files"; Expected = "File Explorer"; Recent = @("File Explorer", "Console", "Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch ControlPanel"; Expected = "ControlPanel"; Recent = @("ControlPanel", "File Explorer", "Console", "Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch DisplayOptions"; Expected = "DisplayOptions"; Recent = @("DisplayOptions", "ControlPanel", "File Explorer", "Console", "Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch Paint"; Expected = "Paint"; Recent = @("Paint", "DisplayOptions", "ControlPanel", "File Explorer", "Console", "Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch TaskManager"; Expected = "TaskManager"; Recent = @("TaskManager", "Paint", "DisplayOptions", "ControlPanel", "File Explorer", "Console", "Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch DiskManager"; Expected = "DiskManager"; Recent = @("DiskManager", "TaskManager", "Paint", "DisplayOptions", "ControlPanel", "File Explorer", "Console", "Clock", "Calculator", "Notepad") },
        @{ Command = "desktop.launch guideXOS Navigator"; Expected = "guideXOS Navigator"; Recent = @("guideXOS Navigator", "DiskManager", "TaskManager", "Paint", "DisplayOptions", "ControlPanel", "File Explorer", "Console", "Clock", "Calculator") },
        @{ Command = "desktop.launch Trash"; Expected = "Trash"; Recent = @("Trash", "guideXOS Navigator", "DiskManager", "TaskManager", "Paint", "DisplayOptions", "ControlPanel", "File Explorer", "Console", "Clock") }
    )

    foreach ($case in $normalAppLaunches) {
        $launchOutput = Invoke-LaunchAndAssertTop `
            -Commands @("gui.start", $case.Command) `
            -ExpectedTop $case.Expected `
            -Reason $case.Command `
            -RecentContains $case.Recent `
            -RecentExcludes @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported") `
            -OutputNeedles @(
                "activeTypedDispatchHandled=true",
                "legacyFallbackUsed=false"
            )

        Assert-Contains $launchOutput "reason=Active typed dispatch handled" "active typed dispatch launch reason"
    }

    $phase4DNormalAppLaunchesRecordCanonicalRecents = $true
    $desktopConfigFixture = Get-Content -LiteralPath $desktopJsonPath -Raw | ConvertFrom-Json
    $desktopConfigFixture.recent = @()
    ($desktopConfigFixture | ConvertTo-Json -Depth 64) | Set-Content -LiteralPath $desktopJsonPath -Encoding ASCII
    Assert-True (@(Get-DesktopRecentNames).Count -eq 0) "Phase 4D smoke should reset the recent-program fixture before fallback checks"

    $legacyAppModelOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch AppModel"
    )
    Assert-Contains $legacyAppModelOutput "selectedHandler=App Model Demo" "AppModel legacy fallback"
    Assert-Contains $legacyAppModelOutput "legacyFallbackUsed=true" "AppModel legacy fallback"
    Assert-Contains $legacyAppModelOutput "reason=Active typed dispatch is not enabled for this app target" "AppModel legacy fallback reason"
    Assert-RecentExcludesAny -Excluded @("AppModel", "App Model Demo") -Reason "AppModel legacy fallback"

    $specialCaseOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.launch ComputerFiles") `
        -ExpectedTop "File Explorer" `
        -Reason "ComputerFiles compatibility fallback" `
        -RecentContains @("File Explorer") `
        -RecentExcludes @("ComputerFiles", "Unsupported", "TotallyUnknownLaunchThing") `
        -OutputNeedles @(
            "selectedHandler=FileExplorer",
            "legacyFallbackUsed=true"
        )

    Assert-Contains $specialCaseOutput "selectedHandler=FileExplorer" "ComputerFiles compatibility fallback"
    Assert-Contains $specialCaseOutput "legacyFallbackUsed=true" "ComputerFiles compatibility fallback"
    Assert-Contains $specialCaseOutput "reason=Active typed dispatch is not enabled for this shell action" "ComputerFiles fallback reason"
    Assert-RecentExcludesAny -Excluded @("ComputerFiles", "Unsupported", "TotallyUnknownLaunchThing") -Reason "ComputerFiles compatibility fallback"

    $shellLaunches = @(
        @{ Command = "desktop.launch Computer"; Expected = "File Explorer"; Reason = "Computer shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch This System"; Expected = "File Explorer"; Reason = "This System shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Documents"; Expected = "File Explorer"; Reason = "Documents shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Pictures"; Expected = "File Explorer"; Reason = "Pictures shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Music"; Expected = "File Explorer"; Reason = "Music shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Network"; Expected = "File Explorer"; Reason = "Network shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Settings"; Expected = "DisplayOptions"; Reason = "Settings shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Control Panel"; Expected = "ControlPanel"; Reason = "Control Panel shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Trash"; Expected = "Trash"; Reason = "Trash shell object"; OutputNeedles = @("classification=BuiltInApp") }
    )

    foreach ($case in $shellLaunches) {
        $shellOutput = Invoke-LaunchAndAssertTop `
            -Commands @("gui.start", $case.Command) `
            -ExpectedTop $case.Expected `
            -Reason $case.Reason `
            -RecentContains @($case.Expected) `
            -RecentExcludes @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported") `
            -OutputNeedles @(
                "activeTypedDispatchHandled=true",
                "legacyFallbackUsed=false"
            )

        foreach ($needle in $case.OutputNeedles) {
            Assert-Contains $shellOutput $needle $case.Reason
        }
    }

    $shellRecents = Get-DesktopRecentNames
    Assert-True (-not ($shellRecents -contains "Desktop")) "Desktop should not appear in recents"
    Assert-True (-not ($shellRecents -contains "Desktop Home")) "Desktop Home should not appear in recents"
    Assert-True (-not ($shellRecents -contains "Go to Desktop")) "Go to Desktop should not appear in recents"
    Assert-True (-not ($shellRecents -contains "This System")) "This System should not appear in recents"
    Assert-True (-not ($shellRecents -contains "Files")) "Files should not appear in recents"
    Assert-True ($shellRecents -contains "File Explorer") "Shell object launches should record File Explorer canonically"
    Assert-True ($shellRecents -contains "ControlPanel") "Shell object launches should record ControlPanel canonically"
    $phase4DShellObjectsNoUnexpectedRecents = $true

    $folderOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.open `"$FixtureFolder`" dir") `
        -ExpectedTop "File Explorer" `
        -Reason "folder open" `
        -RecentContains @("File Explorer", "ControlPanel") `
        -RecentExcludes @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported") `
        -OutputNeedles @(
            "selectedHandler=File Explorer",
            "activeTypedDispatchHandled=true",
            "legacyFallbackUsed=false",
            "reason=Active typed dispatch handled the folder open in File Explorer"
        )

    $textOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.open `"$FixtureText`"") `
        -ExpectedTop "Notepad" `
        -Reason "text file open" `
        -RecentContains @("Notepad", "File Explorer", "ControlPanel") `
        -RecentExcludes @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported") `
        -OutputNeedles @(
            "selectedHandler=Notepad",
            "activeTypedDispatchHandled=true",
            "legacyFallbackUsed=false",
            "reason=Active typed dispatch handled the text-file open in Notepad"
        )

    $imageOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.open `"$FixtureImage`"") `
        -ExpectedTop "Image Viewer" `
        -Reason "legacy image open" `
        -RecentContains @("Image Viewer", "Notepad", "File Explorer") `
        -RecentExcludes @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported") `
        -OutputNeedles @(
            "selectedHandler=ImageViewer",
            "activeTypedDispatchHandled=false",
            "legacyFallbackUsed=true"
        )

    Assert-Contains $imageOutput "reason=Active typed dispatch is not enabled for this filesystem entry" "legacy image fallback reason"
    $phase4DFileFolderRecentsStable = $true

    $unsupportedOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.open `"$FixtureUnsupported`"",
        "desktop.open `"$FixtureRiskyExe`"",
        "desktop.open `"$FixtureRiskyGxapp`"",
        "desktop.open `"$FixtureRiskyElf`""
    )
    Assert-Contains $unsupportedOutput "Desktop open failed: No file association registered for" "unsupported launch failure"
    Assert-RecentExcludesAny -Excluded @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported", "phase4d-unsupported.xyz") -Reason "unsupported targets"
    $phase4DUnsupportedTargetsDoNotPolluteRecents = $true

    $forceOffOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate force-off",
        "desktop.launch Notepad",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`""
    )
    Assert-Contains $forceOffOutput "mode: force-off" "force-off gate"
    Assert-Contains $forceOffOutput "appModelActiveDispatchEnabled=false" "force-off gate"
    Assert-Contains $forceOffOutput "appModelActiveDispatchEffectiveStateSource=force-off" "force-off gate"
    Assert-Contains $forceOffOutput "legacyFallbackUsed=true" "force-off launch"
    Assert-Contains $forceOffOutput "reason=Active typed dispatch gate is disabled" "force-off launch"
    Assert-RecentContainsAll -Expected @("Notepad", "File Explorer") -Reason "force-off recents"
    $phase4DForceOffRecentsStable = $true

    $resetOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.launch Notepad",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`""
    )
    Assert-Contains $resetOutput "mode: reset" "reset gate"
    Assert-Contains $resetOutput "appModelActiveDispatchEnabled=true" "reset gate"
    Assert-Contains $resetOutput "appModelActiveDispatchEffectiveStateSource=product-default" "reset gate"
    Assert-Contains $resetOutput "activeTypedDispatchHandled=true" "reset launch"
    Assert-Contains $resetOutput "legacyFallbackUsed=false" "reset launch"
    Assert-RecentContainsAll -Expected @("Notepad", "File Explorer") -Reason "reset recents"
    $phase4DResetProductDefaultRecentsStable = $true

    $removeSeedOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.launch ControlPanel") `
        -ExpectedTop "ControlPanel" `
        -Reason "remove seed control panel" `
        -RecentContains @("ControlPanel", "Notepad", "File Explorer") `
        -OutputNeedles @(
            "selectedHandler=Control Panel",
            "legacyFallbackUsed=false"
        )
    Assert-Contains $removeSeedOutput "reason=Active typed dispatch handled the Control Panel launch" "control panel seed reason"

    $removeOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.recent.remove Control Panel",
        "desktop.recent"
    )
    Assert-Contains $removeOutput "Recent program remove successful: Control Panel" "recent remove hook"
    Assert-True ((Get-DesktopRecentNames) -notcontains "ControlPanel") "ControlPanel should be removed from recents by alias"
    $phase4DRemoveRecentStillWorks = $true

    $restoreAfterRemoveOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.launch ControlPanel") `
        -ExpectedTop "ControlPanel" `
        -Reason "restore control panel after remove" `
        -RecentContains @("ControlPanel", "Notepad", "File Explorer") `
        -OutputNeedles @(
            "selectedHandler=Control Panel",
            "legacyFallbackUsed=false"
        )
    Assert-Contains $restoreAfterRemoveOutput "reason=Active typed dispatch handled the Control Panel launch" "control panel restore reason"

    $finalSummaryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.summary",
        "desktop.appmodel.shell-objects",
        "desktop.recent"
    )
    Assert-Contains $finalSummaryOutput "appModelPhase4DRecentProgramWritePathsInventoried=true" "final phase 4D summary"
    Assert-Contains $finalSummaryOutput "appModelPhase4DRecentProgramNamesRegistryAligned=true" "final phase 4D summary"
    Assert-Contains $finalSummaryOutput "appModelPhase4DCanonicalRecentCapableBuiltIns=12" "final phase 4D summary"
    Assert-Contains $finalSummaryOutput "appModelPhase4DRegistryRecentCapableBuiltIns=14" "final phase 4D summary"
    Assert-Contains $finalSummaryOutput "appModelPhase4DShellObjectsRecentAllowed=7" "final phase 4D summary"
    Assert-Contains $finalSummaryOutput "appModelPhase4DShellObjectsRecentSuppressed=3" "final phase 4D summary"
    Assert-Contains $finalSummaryOutput "appModelPhase4DShellObjectRecentPolicyAligned=true" "final phase 4D summary"
    Assert-Contains $finalSummaryOutput "appModelActiveDispatchEnabled=true" "final gate state"
    Assert-Contains $finalSummaryOutput "appModelActiveDispatchEffectiveStateSource=product-default" "final gate state"
    Assert-Contains $finalSummaryOutput "visibleLaunchBehaviorChanged=false" "final gate state"
    Assert-Contains $finalSummaryOutput "persistentDesktopStorageWrites=false" "final gate state"

    $phase4DTemporarySmokeStateRestored = $true
}
finally {
    Restore-TrackedArtifacts -State $artifactState
    Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$cleanupVerified = (-not (Test-Path -LiteralPath $TempRoot)) -and (-not (Test-Path -LiteralPath $BackupRoot))
$desktopJsonAfter = Get-FileText (Join-Path $Root "desktop.json")
$desktopStateAfter = Get-FileText (Join-Path $Root "desktop.state")
$displayOptionsAfter = Get-FileText (Join-Path $Root "display-options.cfg")
$phase4DTemporarySmokeStateRestored =
    $phase4DTemporarySmokeStateRestored -and
    $cleanupVerified -and
    ($desktopJsonBefore -eq $desktopJsonAfter) -and
    ($desktopStateBefore -eq $desktopStateAfter) -and
    ($displayOptionsBefore -eq $displayOptionsAfter)

$reportLines = @(
    "[AppModelPhase4DRecentProgramsSmoke]",
    "mode=hosted",
    "appModelPhase4DRecentProgramWritePathsInventoried=$($phase4DRecentProgramWritePathsInventoried.ToString().ToLowerInvariant())",
    "appModelPhase4DRecentProgramNamesRegistryAligned=$($phase4DRecentProgramNamesRegistryAligned.ToString().ToLowerInvariant())",
    "appModelPhase4DNormalAppLaunchesRecordCanonicalRecents=$($phase4DNormalAppLaunchesRecordCanonicalRecents.ToString().ToLowerInvariant())",
    "appModelPhase4DShellObjectsNoUnexpectedRecents=$($phase4DShellObjectsNoUnexpectedRecents.ToString().ToLowerInvariant())",
    "appModelPhase4DFileFolderRecentsStable=$($phase4DFileFolderRecentsStable.ToString().ToLowerInvariant())",
    "appModelPhase4DUnsupportedTargetsDoNotPolluteRecents=$($phase4DUnsupportedTargetsDoNotPolluteRecents.ToString().ToLowerInvariant())",
    "appModelPhase4DRemoveRecentStillWorks=$($phase4DRemoveRecentStillWorks.ToString().ToLowerInvariant())",
    "appModelPhase4DForceOffRecentsStable=$($phase4DForceOffRecentsStable.ToString().ToLowerInvariant())",
    "appModelPhase4DResetProductDefaultRecentsStable=$($phase4DResetProductDefaultRecentsStable.ToString().ToLowerInvariant())",
    "appModelPhase4DVisibleLaunchBehaviorChanged=$($phase4DVisibleLaunchBehaviorChanged.ToString().ToLowerInvariant())",
    "appModelPhase4DPersistentDesktopStorageWrites=$($phase4DPersistentDesktopStorageWrites.ToString().ToLowerInvariant())",
    "appModelPhase4DTemporarySmokeStateRestored=$($phase4DTemporarySmokeStateRestored.ToString().ToLowerInvariant())",
    "appModelActiveDispatchEnabled=true",
    "appModelActiveDispatchEffectiveStateSource=product-default",
    "result=PASS"
)

$report = $reportLines -join [Environment]::NewLine
Set-Content -LiteralPath $SmokeLog -Value $report -Encoding ASCII

Write-Output $report
Write-Host "Smoke log: $SmokeLog"
if ($cleanupVerified) {
    Write-Host "appModelPhase4DTemporaryArtifactsCleaned=true"
} else {
    Write-Host "appModelPhase4DTemporaryArtifactsCleaned=false"
}
