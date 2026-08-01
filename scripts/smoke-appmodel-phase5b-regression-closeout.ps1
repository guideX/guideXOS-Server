param(
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase5b-regression-closeout"
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
$SmokeLog = Join-Path $LogDir "appmodel-phase5b-regression-closeout-$stamp.log"

$FixtureFolder = Join-Path $FixtureRoot "phase5b-folder"
$FixtureText = Join-Path $FixtureRoot "phase5b-safe-open.txt"
$FixtureLog = Join-Path $FixtureRoot "phase5b-safe-open.log"
$FixtureIni = Join-Path $FixtureRoot "phase5b-safe-open.ini"
$FixtureCfg = Join-Path $FixtureRoot "phase5b-safe-open.cfg"
$FixtureImage = Join-Path $FixtureRoot "phase5b-legacy-image.png"
$FixtureUnknown = Join-Path $FixtureRoot "phase5b-unsupported.xyz"
$FixtureRiskyExe = Join-Path $FixtureRoot "phase5b-risky.exe"
$FixtureRiskyGxapp = Join-Path $FixtureRoot "phase5b-risky.gxapp"
$FixtureRiskyElf = Join-Path $FixtureRoot "phase5b-risky.elf"

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

function Assert-NotContains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Reason
    )

    Assert-True (-not $Text.Contains($Needle)) "Unexpected text for ${Reason}: $Needle"
}

function Get-FieldValue {
    param(
        [string]$Text,
        [string]$Field,
        [string]$Default = ""
    )

    $pattern = "(?m)^$([regex]::Escape($Field))=(.*)$"
    $match = [regex]::Match($Text, $pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    if ($match.Success -and $match.Groups.Count -gt 1) {
        return $match.Groups[1].Value.Trim()
    }
    return $Default
}

function Get-CountValue {
    param(
        [string]$Text,
        [string]$Field
    )

    $value = Get-FieldValue -Text $Text -Field $Field -Default ""
    if ($value -eq "") {
        throw "Missing numeric field: $Field"
    }
    return [int64]$value
}

function Get-InventoryCounts {
    param([string]$Text)

    $line = ($Text -split "`r?`n" | Where-Object { $_.TrimStart().StartsWith("counts:") } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "Missing inventory counts line"
    }

    $values = @{}
    foreach ($token in ($line.Trim() -split '\s+')) {
        if ($token -notlike '*=*') {
            continue
        }

        $parts = $token.Split('=', 2)
        if ($parts.Count -ne 2) {
            continue
        }

        $values[$parts[0]] = $parts[1]
    }

    foreach ($required in @("builtInApps", "shellObjects", "fileAssociations", "activeDispatchOwnedCoverage", "fallbackUnsupportedCoverage")) {
        if (-not $values.ContainsKey($required)) {
            throw "Missing inventory counts field: $required"
        }
    }

    return [pscustomobject]@{
        BuiltInApps = [int64]$values["builtInApps"]
        ShellObjects = [int64]$values["shellObjects"]
        FileAssociations = [int64]$values["fileAssociations"]
        ActiveDispatchOwnedCoverage = [int64]$values["activeDispatchOwnedCoverage"]
        FallbackUnsupportedCoverage = [int64]$values["fallbackUnsupportedCoverage"]
    }
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
    if ($ExpectedTop -ne "") {
        Assert-RecentTop -Expected $ExpectedTop -Reason $Reason
    }
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

$appModelPhase5BV1RegressionCloseout = $false
$appModelPhase5BStatusAndInventoryAgree = $false
$appModelPhase5BProductDefaultActiveDispatchEnabled = $false
$appModelPhase5BEmergencyRollbackVerified = $false
$appModelPhase5BLegacyFallbackVerified = $false
$appModelPhase5BCoreLaunchEquivalenceVerified = $false
$appModelPhase5BRecentProgramsVerified = $false
$appModelPhase5BRiskyDestructiveTargetsExcluded = $false
$appModelPhase5BImagesRemainLegacy = $false
$appModelPhase5BOutOfScopeBoundaryVerified = $false
$appModelPhase5BVisibleLaunchBehaviorChanged = $false
$appModelPhase5BPersistentDesktopStorageWrites = $false
$appModelPhase5BGeneratedArtifactsCleaned = $false
$appModelV1Complete = $false

try {
    New-Item -ItemType Directory -Force -Path $FixtureFolder | Out-Null
    Set-Content -LiteralPath $FixtureText -Value @"
Phase 5B text file fixture
This file should open through Notepad.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureLog -Value @"
Phase 5B log file fixture
This file should open through Notepad.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureIni -Value @"
Phase 5B ini file fixture
This file should open through Notepad.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureCfg -Value @"
Phase 5B cfg file fixture
This file should open through Notepad.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureUnknown -Value @"
Phase 5B unknown file fixture
This file intentionally uses an unsupported extension.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureRiskyExe -Value @"
Phase 5B executable-style fixture
This file should stay unsupported.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureRiskyGxapp -Value @"
Phase 5B package-style fixture
This file should stay unsupported.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureRiskyElf -Value @"
Phase 5B ELF-style fixture
This file should stay unsupported.
"@ -Encoding ASCII
    Copy-Item -LiteralPath (Join-Path $Root "assets\Backgrounds\ameoba.png") -Destination $FixtureImage -Force

    $summaryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.summary"
    )
    $inventoryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.inventory"
    )
    $shellOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.shell-objects"
    )
    $gateOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate"
    )
    $typedDispatchGateOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.typed-dispatch-gate"
    )

    Assert-Contains $summaryOutput "appModelV1StatusSurfaceExists=true" "phase 5B status surface"
    Assert-Contains $summaryOutput "appModelV1StatusReady=true" "phase 5B status ready"
    Assert-Contains $summaryOutput "appModelV1StatusSource=product-default" "phase 5B product-default source"
    Assert-Contains $summaryOutput "appModelV1ActiveTypedDispatchEnabled=true" "phase 5B active dispatch enabled"
    Assert-Contains $summaryOutput "appModelV1EmergencyForceOffAvailable=true" "phase 5B emergency force-off marker"
    Assert-Contains $summaryOutput "appModelV1LegacyFallbackAvailable=true" "phase 5B legacy fallback marker"
    Assert-Contains $summaryOutput "appModelV1VisibleLaunchBehaviorChanged=false" "phase 5B visible launch behavior"
    Assert-Contains $summaryOutput "appModelV1PersistentDesktopStorageWrites=false" "phase 5B persistent storage marker"
    Assert-Contains $summaryOutput "appModelV1RecentProgramsAligned=true" "phase 5B recent-program alignment"
    Assert-Contains $summaryOutput "appModelV1RiskyDestructiveTargetsExcluded=true" "phase 5B risky/destructive boundary"
    Assert-Contains $summaryOutput "appModelV1TrashOpenOnlyBoundary=true" "phase 5B trash boundary"
    Assert-Contains $summaryOutput "appModelV1ImagesRemainLegacy=true" "phase 5B images boundary"
    Assert-Contains $summaryOutput "appModelV1OutOfScopeBoundary=true" "phase 5B out-of-scope boundary"
    Assert-Contains $summaryOutput "appModelV1OutOfScopeScope=GXAppExecution|ELFLoading|PackageInstall|Sandboxing|Permissions|IDEBehavior|OpenWith|AppStore|UninstallUpdateLifecycle|TrashDestructiveActions|ImageActiveDispatchOwnership" "phase 5B out-of-scope scope"

    $summaryBuiltInCount = Get-CountValue -Text $summaryOutput -Field "appModelV1BuiltInAppRegistryCount"
    $summaryShellObjectCount = Get-CountValue -Text $summaryOutput -Field "appModelV1ShellObjectRegistryCount"
    $summaryFileAssociationCount = Get-CountValue -Text $summaryOutput -Field "appModelV1FileAssociationCount"
    $summaryActiveDispatchCoverageCount = Get-CountValue -Text $summaryOutput -Field "appModelV1ActiveDispatchOwnedCoverageCount"
    $summaryFallbackUnsupportedCoverageCount = Get-CountValue -Text $summaryOutput -Field "appModelV1FallbackUnsupportedCoverageCount"

    $inventoryCounts = Get-InventoryCounts -Text $inventoryOutput
    Assert-True ($summaryBuiltInCount -eq $inventoryCounts.BuiltInApps) "Summary/inventory built-in counts disagree"
    Assert-True ($summaryShellObjectCount -eq $inventoryCounts.ShellObjects) "Summary/inventory shell-object counts disagree"
    Assert-True ($summaryFileAssociationCount -eq $inventoryCounts.FileAssociations) "Summary/inventory file-association counts disagree"
    Assert-True ($summaryActiveDispatchCoverageCount -eq $inventoryCounts.ActiveDispatchOwnedCoverage) "Summary/inventory active-dispatch coverage counts disagree"
    Assert-True ($summaryFallbackUnsupportedCoverageCount -eq $inventoryCounts.FallbackUnsupportedCoverage) "Summary/inventory fallback coverage counts disagree"
    Assert-True ($summaryBuiltInCount -eq 18) "Expected 18 built-in apps"
    Assert-True ($summaryShellObjectCount -eq 10) "Expected 10 shell objects"
    Assert-True ($summaryFileAssociationCount -eq 14) "Expected 14 file associations"
    Assert-True ($summaryActiveDispatchCoverageCount -eq 11) "Expected 11 active-dispatch-owned coverage entries"
    Assert-True ($summaryFallbackUnsupportedCoverageCount -eq 1) "Expected 1 fallback/unsupported coverage entry"

    Assert-Contains $inventoryOutput "inventorySurfaceExists=true" "phase 5B inventory surface"
    Assert-Contains $inventoryOutput "statusSurface=desktop.appmodel.summary" "phase 5B inventory status surface"
    Assert-Contains $inventoryOutput "counts: builtInApps=18 shellObjects=10 fileAssociations=14 activeDispatchOwnedCoverage=11 fallbackUnsupportedCoverage=1" "phase 5B inventory counts"
    Assert-Contains $inventoryOutput "recentProgramPolicy:" "phase 5B inventory recent-program policy"
    Assert-Contains $inventoryOutput "aligned=true" "phase 5B inventory recent-program alignment"
    Assert-Contains $inventoryOutput "canonicalRecentCapableBuiltIns=12" "phase 5B inventory canonical recent count"
    Assert-Contains $inventoryOutput "registryRecentCapableBuiltIns=14" "phase 5B inventory registry recent count"
    Assert-Contains $inventoryOutput "shellObjectsAllowedForRecent=7" "phase 5B inventory shell-object recent allowance"
    Assert-Contains $inventoryOutput "shellObjectsSuppressedForRecent=3" "phase 5B inventory shell-object recent suppression"
    Assert-Contains $inventoryOutput "fallbackExclusions:" "phase 5B inventory fallback exclusions"
    Assert-Contains $inventoryOutput "core=GXAppExecution|ELFLoading|PackageInstall|Sandboxing|Permissions|IDEBehavior|OpenWith|AppStore|UninstallUpdateLifecycle|TrashDestructiveActions|ImageActiveDispatchOwnership" "phase 5B inventory out-of-scope boundary"
    Assert-Contains $inventoryOutput "legacyFallbacks=AppModel|ComputerFiles|Image Viewer|ImgViewer" "phase 5B inventory legacy fallback list"
    Assert-Contains $inventoryOutput "builtInApps:" "phase 5B inventory built-ins"
    Assert-Contains $inventoryOutput "shellObjects:" "phase 5B inventory shell objects"
    Assert-Contains $inventoryOutput "fileAssociations:" "phase 5B inventory file associations"
    Assert-Contains $inventoryOutput "record id=gxos.builtin.appmodeldemo displayName=App Model Demo" "phase 5B inventory includes App Model Demo"
    Assert-Contains $inventoryOutput "record id=gxos.shell.trash-open displayName=Trash" "phase 5B inventory includes Trash shell object"
    Assert-Contains $inventoryOutput "record key=.txt kind=extension" "phase 5B inventory includes text association"
    Assert-Contains $inventoryOutput "trashDestructiveActionsExcluded=true" "phase 5B inventory trash destructive exclusion"
    Assert-Contains $inventoryOutput "imagesRemainLegacy=true" "phase 5B inventory images remain legacy"

    Assert-Contains $shellOutput "registryExists: true" "phase 5B shell registry exists"
    Assert-Contains $shellOutput "shellObjectRegistryIdsUnique: true" "phase 5B shell registry unique ids"
    Assert-Contains $shellOutput "shellObjectRegistryDisplayNamesNonEmpty: true" "phase 5B shell registry display names"
    Assert-Contains $shellOutput "shellObjectRegistryAliasesResolve: true" "phase 5B shell alias resolution"
    Assert-Contains $shellOutput "shellObjectRegistryHandlersResolveToRegistry: true" "phase 5B shell handler resolution"
    Assert-Contains $shellOutput "rightColumnShellObjectsRegistered: true" "phase 5B shell right-column object coverage"
    Assert-Contains $shellOutput "systemObjectsRegistered: true" "phase 5B shell system object coverage"
    Assert-Contains $shellOutput "trashOpenOnlySafe: true" "phase 5B shell trash open-only boundary"
    Assert-Contains $shellOutput "trashDestructiveActionsExcluded: true" "phase 5B shell trash destructive exclusion"
    Assert-Contains $shellOutput "computerFilesFallbackPreserved: true" "phase 5B shell ComputerFiles fallback"
    Assert-Contains $shellOutput "recentProgramsNotPolluted: true" "phase 5B shell recent-program boundary"
    Assert-NotContains $shellOutput "alias=Empty Trash" "phase 5B shell destructive alias absence"
    Assert-NotContains $shellOutput "alias=delete" "phase 5B shell destructive alias absence"
    Assert-NotContains $shellOutput "alias=restore" "phase 5B shell destructive alias absence"
    Assert-NotContains $shellOutput "alias=purge" "phase 5B shell destructive alias absence"

    Assert-Contains $gateOutput "mode: status" "phase 5B gate status"
    Assert-Contains $gateOutput "appModelActiveDispatchEnabled=true" "phase 5B gate enabled"
    Assert-Contains $gateOutput "appModelActiveDispatchEffectiveStateSource=product-default" "phase 5B gate product default"
    Assert-Contains $gateOutput "runtimeLaunchBehaviorChanged=true" "phase 5B gate runtime behavior"
    Assert-Contains $gateOutput "visibleLaunchBehaviorChanged=false" "phase 5B gate visible behavior"
    Assert-Contains $gateOutput "persistentDesktopStorageWrites=false" "phase 5B gate storage behavior"

    Assert-Contains $typedDispatchGateOutput "typedDispatchForcedOffSupported=true" "phase 5B typed-dispatch gate force-off support"
    Assert-Contains $typedDispatchGateOutput "fallbackToLegacyRequired" "phase 5B typed-dispatch gate legacy fallback"
    Assert-Contains $typedDispatchGateOutput "check=fallbackToLegacyRequired status=PASS" "phase 5B typed-dispatch gate fallback check"
    Assert-Contains $typedDispatchGateOutput "check=appModelSummaryOverall status=PASS" "phase 5B typed-dispatch gate summary check"

    $forceOffOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate force-off",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.appmodel.summary",
        "desktop.appmodel.typed-dispatch-gate"
    )
    Assert-Contains $forceOffOutput "mode: force-off" "phase 5B rollback force-off mode"
    Assert-Contains $forceOffOutput "appModelActiveDispatchEnabled=false" "phase 5B rollback force-off enabled state"
    Assert-Contains $forceOffOutput "appModelActiveDispatchEffectiveStateSource=force-off" "phase 5B rollback force-off source"
    Assert-Contains $forceOffOutput "appModelActiveDispatchCurrentState=false" "phase 5B rollback force-off current state"
    Assert-Contains $forceOffOutput "appModelV1StatusSource=force-off" "phase 5B rollback force-off summary source"
    Assert-Contains $forceOffOutput "appModelV1ActiveTypedDispatchEnabled=false" "phase 5B rollback force-off summary enabled state"
    Assert-Contains $forceOffOutput "appModelV1LegacyFallbackAvailable=true" "phase 5B rollback force-off legacy fallback"
    Assert-Contains $forceOffOutput "appModelV1VisibleLaunchBehaviorChanged=false" "phase 5B rollback force-off visible behavior"
    Assert-Contains $forceOffOutput "appModelV1PersistentDesktopStorageWrites=false" "phase 5B rollback force-off storage behavior"
    Assert-Contains $forceOffOutput "typedDispatchForcedOffSupported=true" "phase 5B rollback force-off typed gate support"
    Assert-Contains $forceOffOutput "fallbackToLegacyRequired" "phase 5B rollback force-off fallback evidence"
    Assert-Contains $forceOffOutput "check=fallbackToLegacyRequired status=PASS" "phase 5B rollback force-off fallback check"

    $forceOffLaunchOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate force-off",
        "desktop.launch Notepad",
        "desktop.launch AppModel",
        "desktop.launch ComputerFiles",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`""
    )
    Assert-Contains $forceOffLaunchOutput "selectedHandler=Notepad" "phase 5B rollback force-off Notepad handler"
    Assert-Contains $forceOffLaunchOutput "activeTypedDispatchHandled=false" "phase 5B rollback force-off legacy fallback path"
    Assert-Contains $forceOffLaunchOutput "legacyFallbackUsed=true" "phase 5B rollback force-off legacy fallback path"
    Assert-Contains $forceOffLaunchOutput "selectedHandler=App Model Demo" "phase 5B rollback force-off AppModel fallback"
    Assert-Contains $forceOffLaunchOutput "selectedHandler=FileExplorer" "phase 5B rollback force-off ComputerFiles fallback"
    Assert-Contains $forceOffLaunchOutput "reason=Active typed dispatch gate is disabled" "phase 5B rollback force-off gate-disabled reason"

    $forceOnOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate force-on",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.appmodel.summary"
    )
    Assert-Contains $forceOnOutput "mode: force-on" "phase 5B rollback force-on mode"
    Assert-Contains $forceOnOutput "appModelActiveDispatchEnabled=true" "phase 5B rollback force-on enabled state"
    Assert-Contains $forceOnOutput "appModelActiveDispatchEffectiveStateSource=force-on" "phase 5B rollback force-on source"
    Assert-Contains $forceOnOutput "appModelActiveDispatchCurrentState=true" "phase 5B rollback force-on current state"
    Assert-Contains $forceOnOutput "appModelV1StatusSource=force-on" "phase 5B rollback force-on summary source"
    Assert-Contains $forceOnOutput "appModelV1ActiveTypedDispatchEnabled=true" "phase 5B rollback force-on summary enabled state"
    Assert-Contains $forceOnOutput "appModelV1VisibleLaunchBehaviorChanged=false" "phase 5B rollback force-on visible behavior"
    Assert-Contains $forceOnOutput "appModelV1PersistentDesktopStorageWrites=false" "phase 5B rollback force-on storage behavior"

    $resetOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.appmodel.summary"
    )
    Assert-Contains $resetOutput "mode: reset" "phase 5B rollback reset mode"
    Assert-Contains $resetOutput "appModelActiveDispatchEnabled=true" "phase 5B rollback reset enabled state"
    Assert-Contains $resetOutput "appModelActiveDispatchEffectiveStateSource=product-default" "phase 5B rollback reset product default"
    Assert-Contains $resetOutput "appModelActiveDispatchCurrentState=true" "phase 5B rollback reset current state"
    Assert-Contains $resetOutput "appModelV1StatusSource=product-default" "phase 5B rollback reset summary source"
    Assert-Contains $resetOutput "appModelV1ActiveTypedDispatchEnabled=true" "phase 5B rollback reset summary enabled state"
    Assert-Contains $resetOutput "appModelV1LegacyFallbackAvailable=true" "phase 5B rollback reset legacy fallback"
    Assert-Contains $resetOutput "appModelV1VisibleLaunchBehaviorChanged=false" "phase 5B rollback reset visible behavior"
    Assert-Contains $resetOutput "appModelV1PersistentDesktopStorageWrites=false" "phase 5B rollback reset storage behavior"

    $desktopJsonPath = Join-Path $Root "desktop.json"
    if (Test-Path -LiteralPath $desktopJsonPath) {
        $desktopConfig = Get-Content -LiteralPath $desktopJsonPath -Raw | ConvertFrom-Json
        $desktopConfig.recent = @()
        ($desktopConfig | ConvertTo-Json -Depth 64) | Set-Content -LiteralPath $desktopJsonPath -Encoding ASCII
    }
    Assert-True (@(Get-DesktopRecentNames).Count -eq 0) "Phase 5B smoke should start from an empty recent-program fixture"

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

        Assert-Contains $launchOutput "reason=Active typed dispatch handled" "phase 5B active typed dispatch launch reason"
    }

    $appModelLaunchOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch AppModel"
    )
    Assert-Contains $appModelLaunchOutput "selectedHandler=App Model Demo" "phase 5B AppModel fallback"
    Assert-Contains $appModelLaunchOutput "legacyFallbackUsed=true" "phase 5B AppModel fallback"
    Assert-Contains $appModelLaunchOutput "reason=Active typed dispatch is not enabled for this app target" "phase 5B AppModel fallback reason"
    Assert-RecentExcludesAny -Excluded @("AppModel", "App Model Demo") -Reason "phase 5B AppModel fallback"

    $computerFilesOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.launch ComputerFiles") `
        -ExpectedTop "File Explorer" `
        -Reason "phase 5B ComputerFiles compatibility fallback" `
        -RecentContains @("File Explorer") `
        -RecentExcludes @("ComputerFiles", "Unsupported", "TotallyUnknownLaunchThing") `
        -OutputNeedles @(
            "selectedHandler=FileExplorer",
            "legacyFallbackUsed=true"
        )
    Assert-Contains $computerFilesOutput "reason=Active typed dispatch is not enabled for this shell action" "phase 5B ComputerFiles fallback reason"

    $shellLaunches = @(
        @{ Command = "desktop.launch Computer"; Expected = "File Explorer"; Reason = "phase 5B Computer shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch This System"; Expected = "File Explorer"; Reason = "phase 5B This System shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Documents"; Expected = "File Explorer"; Reason = "phase 5B Documents shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Pictures"; Expected = "File Explorer"; Reason = "phase 5B Pictures shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Music"; Expected = "File Explorer"; Reason = "phase 5B Music shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Network"; Expected = "File Explorer"; Reason = "phase 5B Network shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Settings"; Expected = "DisplayOptions"; Reason = "phase 5B Settings shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Control Panel"; Expected = "ControlPanel"; Reason = "phase 5B Control Panel shell object"; OutputNeedles = @("classification=ShellAction") },
        @{ Command = "desktop.launch Trash"; Expected = "Trash"; Reason = "phase 5B Trash shell object"; OutputNeedles = @("classification=BuiltInApp") }
    )

    foreach ($case in $shellLaunches) {
        $shellLaunchOutput = Invoke-LaunchAndAssertTop `
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
            Assert-Contains $shellLaunchOutput $needle $case.Reason
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
    Assert-True ($shellRecents -contains "DisplayOptions") "Shell object launches should record DisplayOptions canonically"
    Assert-True ($shellRecents -contains "Trash") "Trash shell object launches should record Trash canonically"

    $folderOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.open `"$FixtureFolder`" dir") `
        -ExpectedTop "File Explorer" `
        -Reason "phase 5B folder open" `
        -RecentContains @("File Explorer", "ControlPanel") `
        -RecentExcludes @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported") `
        -OutputNeedles @(
            "selectedHandler=File Explorer",
            "activeTypedDispatchHandled=true",
            "legacyFallbackUsed=false",
            "reason=Active typed dispatch handled the folder open in File Explorer"
        )

    $textOutputs = @(
        @{ Path = $FixtureText; Label = "txt" },
        @{ Path = $FixtureLog; Label = "log" },
        @{ Path = $FixtureIni; Label = "ini" },
        @{ Path = $FixtureCfg; Label = "cfg" }
    )
    foreach ($fixture in $textOutputs) {
        $textOutput = Invoke-LaunchAndAssertTop `
            -Commands @("gui.start", "desktop.open `"$($fixture.Path)`"") `
            -ExpectedTop "Notepad" `
            -Reason "phase 5B $($fixture.Label) file open" `
            -RecentContains @("Notepad", "File Explorer", "ControlPanel") `
            -RecentExcludes @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported") `
            -OutputNeedles @(
                "selectedHandler=Notepad",
                "activeTypedDispatchHandled=true",
                "legacyFallbackUsed=false",
                "reason=Active typed dispatch handled the text-file open in Notepad"
            )
        Assert-Contains $textOutput "path=$($fixture.Path)" "phase 5B $($fixture.Label) file open path"
    }

    $imageOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.open `"$FixtureImage`"") `
        -ExpectedTop "Image Viewer" `
        -Reason "phase 5B legacy image open" `
        -RecentContains @("Image Viewer", "Notepad", "File Explorer") `
        -RecentExcludes @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported") `
        -OutputNeedles @(
            "selectedHandler=ImageViewer",
            "activeTypedDispatchHandled=false",
            "legacyFallbackUsed=true"
        )
    Assert-Contains $imageOutput "reason=Active typed dispatch is not enabled for this filesystem entry" "phase 5B legacy image reason"

    $unsupportedOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.open `"$FixtureUnknown`"",
        "desktop.open `"$FixtureRiskyExe`"",
        "desktop.open `"$FixtureRiskyGxapp`"",
        "desktop.open `"$FixtureRiskyElf`""
    )
    Assert-Contains $unsupportedOutput "Desktop open failed: No file association registered for" "phase 5B unsupported launch failure"
    Assert-RecentExcludesAny -Excluded @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported", "phase5b-unsupported.xyz", "phase5b-risky.exe", "phase5b-risky.gxapp", "phase5b-risky.elf") -Reason "phase 5B unsupported targets"

    $removeSeedOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.launch ControlPanel") `
        -ExpectedTop "ControlPanel" `
        -Reason "phase 5B remove seed control panel" `
        -RecentContains @("ControlPanel", "Notepad", "File Explorer") `
        -OutputNeedles @(
            "selectedHandler=Control Panel",
            "legacyFallbackUsed=false"
        )
    Assert-Contains $removeSeedOutput "reason=Active typed dispatch handled the Control Panel launch" "phase 5B control panel seed reason"

    $removeOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.recent.remove Control Panel",
        "desktop.recent"
    )
    Assert-Contains $removeOutput "Recent program remove successful: Control Panel" "phase 5B recent remove hook"
    Assert-True ((Get-DesktopRecentNames) -notcontains "ControlPanel") "ControlPanel should be removed from recents by alias"

    $restoreAfterRemoveOutput = Invoke-LaunchAndAssertTop `
        -Commands @("gui.start", "desktop.launch ControlPanel") `
        -ExpectedTop "ControlPanel" `
        -Reason "phase 5B restore control panel after remove" `
        -RecentContains @("ControlPanel", "Notepad", "File Explorer") `
        -OutputNeedles @(
            "selectedHandler=Control Panel",
            "legacyFallbackUsed=false"
        )
    Assert-Contains $restoreAfterRemoveOutput "reason=Active typed dispatch handled the Control Panel launch" "phase 5B control panel restore reason"

    $finalSummaryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.summary",
        "desktop.appmodel.inventory",
        "desktop.appmodel.shell-objects",
        "desktop.recent"
    )
    Assert-Contains $finalSummaryOutput "appModelV1StatusReady=true" "phase 5B final summary"
    Assert-Contains $finalSummaryOutput "appModelV1StatusSource=product-default" "phase 5B final summary source"
    Assert-Contains $finalSummaryOutput "appModelV1ActiveTypedDispatchEnabled=true" "phase 5B final summary active dispatch"
    Assert-Contains $finalSummaryOutput "appModelV1LegacyFallbackAvailable=true" "phase 5B final summary legacy fallback"
    Assert-Contains $finalSummaryOutput "appModelV1VisibleLaunchBehaviorChanged=false" "phase 5B final summary visible behavior"
    Assert-Contains $finalSummaryOutput "appModelV1PersistentDesktopStorageWrites=false" "phase 5B final summary storage behavior"
    Assert-Contains $finalSummaryOutput "appModelV1RecentProgramsAligned=true" "phase 5B final summary recent-program alignment"
    Assert-Contains $finalSummaryOutput "appModelV1RiskyDestructiveTargetsExcluded=true" "phase 5B final summary risky/destructive boundary"
    Assert-Contains $finalSummaryOutput "appModelV1TrashOpenOnlyBoundary=true" "phase 5B final summary trash boundary"
    Assert-Contains $finalSummaryOutput "appModelV1ImagesRemainLegacy=true" "phase 5B final summary images boundary"
    Assert-Contains $finalSummaryOutput "appModelV1OutOfScopeBoundary=true" "phase 5B final summary out-of-scope boundary"
    Assert-Contains $finalSummaryOutput "appModelV1BuiltInAppRegistryCount=18" "phase 5B final summary built-in count"
    Assert-Contains $finalSummaryOutput "appModelV1ShellObjectRegistryCount=10" "phase 5B final summary shell-object count"
    Assert-Contains $finalSummaryOutput "appModelV1FileAssociationCount=14" "phase 5B final summary file-association count"
    Assert-Contains $finalSummaryOutput "appModelV1ActiveDispatchOwnedCoverageCount=11" "phase 5B final summary active-owned coverage"
    Assert-Contains $finalSummaryOutput "appModelV1FallbackUnsupportedCoverageCount=1" "phase 5B final summary fallback coverage"
    Assert-Contains $finalSummaryOutput "counts: builtInApps=18 shellObjects=10 fileAssociations=14 activeDispatchOwnedCoverage=11 fallbackUnsupportedCoverage=1" "phase 5B final inventory counts"
    Assert-Contains $finalSummaryOutput "recentProgramPolicy:" "phase 5B final recent-program policy"
    Assert-Contains $finalSummaryOutput "aligned=true" "phase 5B final recent-program alignment"
    Assert-Contains $finalSummaryOutput "trashOpenOnlySafe=true" "phase 5B final trash boundary"
    Assert-Contains $finalSummaryOutput "trashDestructiveActionsExcluded=true" "phase 5B final trash destructive exclusion"
    Assert-Contains $finalSummaryOutput "imagesRemainLegacy=true" "phase 5B final images boundary"
    Assert-Contains $finalSummaryOutput "legacyFallbacks=AppModel|ComputerFiles|Image Viewer|ImgViewer" "phase 5B final legacy fallback list"
    Assert-Contains $finalSummaryOutput "recentProgramsNotPolluted: true" "phase 5B final shell recent boundary"

    $appModelPhase5BV1RegressionCloseout = $true
    $appModelPhase5BStatusAndInventoryAgree = $true
    $appModelPhase5BProductDefaultActiveDispatchEnabled = $true
    $appModelPhase5BEmergencyRollbackVerified = $true
    $appModelPhase5BLegacyFallbackVerified = $true
    $appModelPhase5BCoreLaunchEquivalenceVerified = $true
    $appModelPhase5BRecentProgramsVerified = $true
    $appModelPhase5BRiskyDestructiveTargetsExcluded = $true
    $appModelPhase5BImagesRemainLegacy = $true
    $appModelPhase5BOutOfScopeBoundaryVerified = $true
    $appModelPhase5BVisibleLaunchBehaviorChanged = $false
    $appModelPhase5BPersistentDesktopStorageWrites = $false
    $appModelV1Complete = $true
}
finally {
    Restore-TrackedArtifacts -State $artifactState
    Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$cleanupVerified = (-not (Test-Path -LiteralPath $TempRoot)) -and (-not (Test-Path -LiteralPath $BackupRoot))
$desktopJsonAfter = Get-FileText (Join-Path $Root "desktop.json")
$desktopStateAfter = Get-FileText (Join-Path $Root "desktop.state")
$displayOptionsAfter = Get-FileText (Join-Path $Root "display-options.cfg")
$appModelPhase5BGeneratedArtifactsCleaned =
    $cleanupVerified -and
    ($desktopJsonBefore -eq $desktopJsonAfter) -and
    ($desktopStateBefore -eq $desktopStateAfter) -and
    ($displayOptionsBefore -eq $displayOptionsAfter)

$reportLines = @(
    "[AppModelPhase5BRegressionCloseoutSmoke]",
    "mode=read-only-validation",
    "appModelPhase5BV1RegressionCloseout=$($appModelPhase5BV1RegressionCloseout.ToString().ToLowerInvariant())",
    "appModelPhase5BStatusAndInventoryAgree=$($appModelPhase5BStatusAndInventoryAgree.ToString().ToLowerInvariant())",
    "appModelPhase5BProductDefaultActiveDispatchEnabled=$($appModelPhase5BProductDefaultActiveDispatchEnabled.ToString().ToLowerInvariant())",
    "appModelPhase5BEmergencyRollbackVerified=$($appModelPhase5BEmergencyRollbackVerified.ToString().ToLowerInvariant())",
    "appModelPhase5BLegacyFallbackVerified=$($appModelPhase5BLegacyFallbackVerified.ToString().ToLowerInvariant())",
    "appModelPhase5BCoreLaunchEquivalenceVerified=$($appModelPhase5BCoreLaunchEquivalenceVerified.ToString().ToLowerInvariant())",
    "appModelPhase5BRecentProgramsVerified=$($appModelPhase5BRecentProgramsVerified.ToString().ToLowerInvariant())",
    "appModelPhase5BRiskyDestructiveTargetsExcluded=$($appModelPhase5BRiskyDestructiveTargetsExcluded.ToString().ToLowerInvariant())",
    "appModelPhase5BImagesRemainLegacy=$($appModelPhase5BImagesRemainLegacy.ToString().ToLowerInvariant())",
    "appModelPhase5BOutOfScopeBoundaryVerified=$($appModelPhase5BOutOfScopeBoundaryVerified.ToString().ToLowerInvariant())",
    "appModelPhase5BVisibleLaunchBehaviorChanged=$($appModelPhase5BVisibleLaunchBehaviorChanged.ToString().ToLowerInvariant())",
    "appModelPhase5BPersistentDesktopStorageWrites=$($appModelPhase5BPersistentDesktopStorageWrites.ToString().ToLowerInvariant())",
    "appModelPhase5BGeneratedArtifactsCleaned=$($appModelPhase5BGeneratedArtifactsCleaned.ToString().ToLowerInvariant())",
    "appModelV1Complete=$($appModelV1Complete.ToString().ToLowerInvariant())",
    "result=PASS"
)

$logParts = @($reportLines -join [Environment]::NewLine)
$logParts += ""
$logParts += "[summary-output]"
$logParts += $summaryOutput
$logParts += ""
$logParts += "[inventory-output]"
$logParts += $inventoryOutput
$logParts += ""
$logParts += "[shell-output]"
$logParts += $shellOutput
$logParts += ""
$logParts += "[gate-output]"
$logParts += $gateOutput
$logParts += ""
$logParts += "[typed-dispatch-gate-output]"
$logParts += $typedDispatchGateOutput
$logParts += ""
$logParts += "[final-summary-output]"
$logParts += $finalSummaryOutput
Set-Content -LiteralPath $SmokeLog -Value ($logParts -join [Environment]::NewLine) -Encoding ASCII

Write-Output ($reportLines -join [Environment]::NewLine)
Write-Host "Smoke log: $SmokeLog"
if ($appModelPhase5BGeneratedArtifactsCleaned) {
    Write-Host "appModelPhase5BGeneratedArtifactsCleaned=true"
} else {
    Write-Host "appModelPhase5BGeneratedArtifactsCleaned=false"
}
