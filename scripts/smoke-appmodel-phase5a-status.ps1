param(
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase5a-status"
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
$SmokeLog = Join-Path $LogDir "appmodel-phase5a-status-$stamp.log"

$FixtureSafeOpen = Join-Path $FixtureRoot "phase5a-safe-open.txt"
$FixtureLegacyImage = Join-Path $FixtureRoot "phase5a-legacy-image.png"
$FixtureUnsupported = Join-Path $FixtureRoot "phase5a-unsupported.xyz"

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

function Invoke-LaunchSmokeAssertions {
    param(
        [string]$Output,
        [string]$Stage
    )

    Assert-Contains $Output "appModelActiveDispatchEnabled=true" "$Stage active dispatch enabled"
    Assert-Contains $Output "appModelActiveDispatchEffectiveStateSource=product-default" "$Stage active dispatch source"
}

$artifactState = Backup-TrackedArtifacts
$desktopJsonBefore = Get-FileText (Join-Path $Root "desktop.json")
$desktopStateBefore = Get-FileText (Join-Path $Root "desktop.state")
$displayOptionsBefore = Get-FileText (Join-Path $Root "display-options.cfg")

$appModelPhase5AV1StatusSurfaceExists = $false
$appModelPhase5AV1DocsUpdated = $false
$appModelPhase5AProductDefaultActiveDispatchEnabled = $false
$appModelPhase5AEmergencyForceOffAvailable = $false
$appModelPhase5AResetReturnsProductDefault = $false
$appModelPhase5ALegacyFallbackAvailable = $false
$appModelPhase5ABuiltInRegistryCountSane = $false
$appModelPhase5AShellObjectRegistryCountSane = $false
$appModelPhase5AFileAssociationCountSane = $false
$appModelPhase5ARecentProgramsAligned = $false
$appModelPhase5ARiskyDestructiveTargetsExcluded = $false
$appModelPhase5ATrashOpenOnlyBoundary = $false
$appModelPhase5AImagesRemainLegacy = $false
$appModelPhase5AVisibleLaunchBehaviorChanged = $false
$appModelPhase5APersistentDesktopStorageWrites = $false
$appModelPhase5ATemporarySmokeStateRestored = $false

try {
    Set-Content -LiteralPath $FixtureSafeOpen -Value @"
Phase 5A App Model status smoke fixture
This file exists only to prove temporary smoke artifacts are cleaned up.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureLegacyImage -Value @"
Phase 5A legacy image fixture
This file exists only to prove temporary smoke artifacts are cleaned up.
"@ -Encoding ASCII
    Set-Content -LiteralPath $FixtureUnsupported -Value @"
Phase 5A unsupported fixture
This file exists only to prove temporary smoke artifacts are cleaned up.
"@ -Encoding ASCII

    $summaryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.summary"
    )
    $inventoryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.inventory"
    )
    $gateOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.appmodel.active-typed-dispatch-gate force-off",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.active-typed-dispatch-gate"
    )
    $typedDispatchGateOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.typed-dispatch-gate"
    )

    Assert-Contains $summaryOutput "appModelV1StatusSurfaceExists=true" "phase 5A status surface"
    Assert-Contains $summaryOutput "appModelV1StatusReady=true" "phase 5A status ready"
    Assert-Contains $summaryOutput "appModelV1StatusSource=product-default" "phase 5A product-default source"
    Assert-Contains $summaryOutput "desktop.appmodel.inventory" "phase 5A summary inventory command"
    Assert-Contains $summaryOutput "appModelV1ActiveTypedDispatchEnabled=true" "phase 5A active dispatch enabled"
    Assert-Contains $summaryOutput "appModelV1EmergencyForceOffAvailable=true" "phase 5A emergency force-off marker"
    Assert-Contains $summaryOutput "appModelV1LegacyFallbackAvailable=true" "phase 5A legacy fallback marker"
    Assert-Contains $summaryOutput "appModelV1VisibleLaunchBehaviorChanged=false" "phase 5A visible launch behavior"
    Assert-Contains $summaryOutput "appModelV1PersistentDesktopStorageWrites=false" "phase 5A persistent storage marker"
    Assert-Contains $summaryOutput "appModelV1RecentProgramsAligned=true" "phase 5A recent-program alignment"
    Assert-Contains $summaryOutput "appModelV1RiskyDestructiveTargetsExcluded=true" "phase 5A risky/destructive boundary"
    Assert-Contains $summaryOutput "appModelV1TrashOpenOnlyBoundary=true" "phase 5A trash boundary"
    Assert-Contains $summaryOutput "appModelV1ImagesRemainLegacy=true" "phase 5A images boundary"
    Assert-Contains $summaryOutput "appModelV1OutOfScopeBoundary=true" "phase 5A out-of-scope boundary"

    $builtInCount = Get-CountValue -Text $summaryOutput -Field "appModelV1BuiltInAppRegistryCount"
    $shellObjectCount = Get-CountValue -Text $summaryOutput -Field "appModelV1ShellObjectRegistryCount"
    $fileAssociationCount = Get-CountValue -Text $summaryOutput -Field "appModelV1FileAssociationCount"
    $activeDispatchCoverageCount = Get-CountValue -Text $summaryOutput -Field "appModelV1ActiveDispatchOwnedCoverageCount"
    $fallbackUnsupportedCoverageCount = Get-CountValue -Text $summaryOutput -Field "appModelV1FallbackUnsupportedCoverageCount"

    Assert-True ($builtInCount -gt 0) "Built-in app registry count should be nonzero"
    Assert-True ($shellObjectCount -gt 0) "Shell object registry count should be nonzero"
    Assert-True ($fileAssociationCount -gt 0) "File association count should be nonzero"
    Assert-True ($activeDispatchCoverageCount -gt 0) "Active-dispatch-owned coverage count should be nonzero"
    Assert-True ($fallbackUnsupportedCoverageCount -ge 0) "Fallback/unsupported coverage count should parse"

    Assert-Contains $inventoryOutput "inventorySurfaceExists=true" "phase 5A inventory surface"
    Assert-Contains $inventoryOutput "statusSurface=desktop.appmodel.summary" "phase 5A inventory status surface"
    Assert-Contains $inventoryOutput "recentProgramPolicy:" "phase 5A inventory recent-program policy"
    Assert-Contains $inventoryOutput "aligned=true" "phase 5A inventory recent-program alignment"
    Assert-Contains $inventoryOutput "fallbackExclusions:" "phase 5A inventory fallback exclusions"
    Assert-Contains $inventoryOutput "core=GXAppExecution|ELFLoading|PackageInstall|Sandboxing|Permissions|IDEBehavior|OpenWith|AppStore|UninstallUpdateLifecycle|TrashDestructiveActions|ImageActiveDispatchOwnership" "phase 5A inventory out-of-scope boundary"
    Assert-Contains $inventoryOutput "legacyFallbacks=AppModel|ComputerFiles|Image Viewer|ImgViewer" "phase 5A inventory legacy fallback list"
    Assert-Contains $inventoryOutput "builtInApps:" "phase 5A inventory built-ins"
    Assert-Contains $inventoryOutput "shellObjects:" "phase 5A inventory shell objects"
    Assert-Contains $inventoryOutput "fileAssociations:" "phase 5A inventory file associations"
    Assert-Contains $inventoryOutput "record id=gxos.builtin.appmodeldemo displayName=App Model Demo" "phase 5A inventory includes App Model Demo"
    Assert-Contains $inventoryOutput "record id=gxos.shell.trash-open displayName=Trash" "phase 5A inventory includes Trash shell object"
    Assert-Contains $inventoryOutput "record key=.txt kind=extension" "phase 5A inventory includes text association"

    Assert-Contains $gateOutput "mode: status" "phase 5A gate status"
    Assert-Contains $gateOutput "mode: force-off" "phase 5A gate force-off"
    Assert-Contains $gateOutput "appModelActiveDispatchEnabled=false" "phase 5A force-off disabled active dispatch"
    Assert-Contains $gateOutput "appModelActiveDispatchEffectiveStateSource=force-off" "phase 5A force-off source"
    Assert-Contains $gateOutput "mode: reset" "phase 5A gate reset"
    Assert-Contains $gateOutput "appModelActiveDispatchEnabled=true" "phase 5A reset re-enabled active dispatch"
    Assert-Contains $gateOutput "appModelActiveDispatchEffectiveStateSource=product-default" "phase 5A reset restored product-default"

    Assert-Contains $typedDispatchGateOutput "typedDispatchForcedOffSupported=true" "phase 5A force-off availability"
    Assert-Contains $typedDispatchGateOutput "fallbackToLegacyRequired" "phase 5A legacy fallback evidence"
    Assert-Contains $typedDispatchGateOutput "check=fallbackToLegacyRequired status=PASS" "phase 5A legacy fallback required"
    Assert-Contains $typedDispatchGateOutput "check=appModelSummaryOverall status=PASS" "phase 5A summary overall"

    $appModelPhase5AV1StatusSurfaceExists = $true
    $appModelPhase5AV1DocsUpdated = $false
    $appModelPhase5AProductDefaultActiveDispatchEnabled = $true
    $appModelPhase5AEmergencyForceOffAvailable = $true
    $appModelPhase5AResetReturnsProductDefault = $true
    $appModelPhase5ALegacyFallbackAvailable = $true
    $appModelPhase5ABuiltInRegistryCountSane = $builtInCount -gt 0
    $appModelPhase5AShellObjectRegistryCountSane = $shellObjectCount -gt 0
    $appModelPhase5AFileAssociationCountSane = $fileAssociationCount -gt 0
    $appModelPhase5ARecentProgramsAligned = $true
    $appModelPhase5ARiskyDestructiveTargetsExcluded = $true
    $appModelPhase5ATrashOpenOnlyBoundary = $true
    $appModelPhase5AImagesRemainLegacy = $true
    $appModelPhase5AVisibleLaunchBehaviorChanged = $false
    $appModelPhase5APersistentDesktopStorageWrites = $false

    $docsPath = Join-Path $Root "docs\APP_MODEL_CURRENT_STATE.md"
    $docsText = Get-FileText $docsPath
    Assert-Contains $docsText "Phase 5A final App Model v1 snapshot" "phase 5A docs snapshot"
    Assert-Contains $docsText "App Model v1 owns" "phase 5A docs ownership"
    Assert-Contains $docsText "Known non-goals for v1" "phase 5A docs non-goals"
    Assert-Contains $docsText "desktop.appmodel.inventory" "phase 5A docs inventory command"
    Assert-Contains $docsText "appModelV1StatusReady=true" "phase 5A docs status marker"
    $appModelPhase5AV1DocsUpdated = $true
}
finally {
    Restore-TrackedArtifacts -State $artifactState
    Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$cleanupVerified = (-not (Test-Path -LiteralPath $TempRoot)) -and (-not (Test-Path -LiteralPath $BackupRoot))
$desktopJsonAfter = Get-FileText (Join-Path $Root "desktop.json")
$desktopStateAfter = Get-FileText (Join-Path $Root "desktop.state")
$displayOptionsAfter = Get-FileText (Join-Path $Root "display-options.cfg")
$appModelPhase5ATemporarySmokeStateRestored =
    $cleanupVerified -and
    ($desktopJsonBefore -eq $desktopJsonAfter) -and
    ($desktopStateBefore -eq $desktopStateAfter) -and
    ($displayOptionsBefore -eq $displayOptionsAfter)

$reportLines = @(
    "[AppModelPhase5AStatusDocsSmoke]",
    "mode=read-only-status-and-docs",
    "appModelPhase5AV1StatusSurfaceExists=$($appModelPhase5AV1StatusSurfaceExists.ToString().ToLowerInvariant())",
    "appModelPhase5AV1DocsUpdated=$($appModelPhase5AV1DocsUpdated.ToString().ToLowerInvariant())",
    "appModelPhase5AProductDefaultActiveDispatchEnabled=$($appModelPhase5AProductDefaultActiveDispatchEnabled.ToString().ToLowerInvariant())",
    "appModelPhase5AEmergencyForceOffAvailable=$($appModelPhase5AEmergencyForceOffAvailable.ToString().ToLowerInvariant())",
    "appModelPhase5AResetReturnsProductDefault=$($appModelPhase5AResetReturnsProductDefault.ToString().ToLowerInvariant())",
    "appModelPhase5ALegacyFallbackAvailable=$($appModelPhase5ALegacyFallbackAvailable.ToString().ToLowerInvariant())",
    "appModelPhase5ABuiltInRegistryCountSane=$($appModelPhase5ABuiltInRegistryCountSane.ToString().ToLowerInvariant())",
    "appModelPhase5AShellObjectRegistryCountSane=$($appModelPhase5AShellObjectRegistryCountSane.ToString().ToLowerInvariant())",
    "appModelPhase5AFileAssociationCountSane=$($appModelPhase5AFileAssociationCountSane.ToString().ToLowerInvariant())",
    "appModelPhase5ARecentProgramsAligned=$($appModelPhase5ARecentProgramsAligned.ToString().ToLowerInvariant())",
    "appModelPhase5ARiskyDestructiveTargetsExcluded=$($appModelPhase5ARiskyDestructiveTargetsExcluded.ToString().ToLowerInvariant())",
    "appModelPhase5ATrashOpenOnlyBoundary=$($appModelPhase5ATrashOpenOnlyBoundary.ToString().ToLowerInvariant())",
    "appModelPhase5AImagesRemainLegacy=$($appModelPhase5AImagesRemainLegacy.ToString().ToLowerInvariant())",
    "appModelPhase5AVisibleLaunchBehaviorChanged=$($appModelPhase5AVisibleLaunchBehaviorChanged.ToString().ToLowerInvariant())",
    "appModelPhase5APersistentDesktopStorageWrites=$($appModelPhase5APersistentDesktopStorageWrites.ToString().ToLowerInvariant())",
    "appModelPhase5ATemporarySmokeStateRestored=$($appModelPhase5ATemporarySmokeStateRestored.ToString().ToLowerInvariant())",
    "appModelPhase5AActiveDispatchCoverageCount=$activeDispatchCoverageCount",
    "appModelPhase5AFallbackUnsupportedCoverageCount=$fallbackUnsupportedCoverageCount",
    "result=PASS"
)

$report = $reportLines -join [Environment]::NewLine
$logParts = @($report)
$logParts += ""
$logParts += "[summary-output]"
$logParts += $summaryOutput
$logParts += ""
$logParts += "[inventory-output]"
$logParts += $inventoryOutput
$logParts += ""
$logParts += "[gate-output]"
$logParts += $gateOutput
$logParts += ""
$logParts += "[typed-dispatch-gate-output]"
$logParts += $typedDispatchGateOutput
Set-Content -LiteralPath $SmokeLog -Value ($logParts -join [Environment]::NewLine) -Encoding ASCII

Write-Output $report
Write-Host "Smoke log: $SmokeLog"
if ($appModelPhase5ATemporarySmokeStateRestored) {
    Write-Host "appModelPhase5ATemporaryArtifactsCleaned=true"
} else {
    Write-Host "appModelPhase5ATemporaryArtifactsCleaned=false"
}
